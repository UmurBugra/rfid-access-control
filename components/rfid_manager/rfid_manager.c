#include "rfid_manager.h"

#include <string.h>
#include <stdio.h>
#include "esp_log.h"
#include "rc522.h"
#include "driver/rc522_spi.h"
#include "rc522_picc.h"

static const char *TAG = "rfid_mgr";

/* ============================================================
 * Pin Tanimlari (AGENTS.md'den)
 * RC522 SPI baglantisi — VSPI (SPI3_HOST)
 * ============================================================ */
#define RC522_SPI_BUS_GPIO_MISO     19
#define RC522_SPI_BUS_GPIO_MOSI     23
#define RC522_SPI_BUS_GPIO_SCLK     18
#define RC522_SPI_SCANNER_GPIO_CS   5
#define RC522_SCANNER_GPIO_RST      22

/* ============================================================
 * Modul degiskenleri
 * ============================================================ */
static rc522_driver_handle_t s_driver = NULL;
static rc522_handle_t s_scanner = NULL;
static QueueHandle_t s_event_queue = NULL;

/* ============================================================
 * Dahili fonksiyonlar
 * ============================================================ */

/**
 * @brief UID byte dizisini buyuk harfli hex string'e cevirir
 * 
 * Sadece ilk 4 byte'i alir (nvs_store ile uyumlu).
 * Ornek: {0xA3, 0xF2, 0xC1, 0xB0} -> "A3F2C1B0"
 */
static void uid_to_hex_string(const uint8_t *uid_bytes, uint8_t uid_len, char *out, size_t out_size)
{
    /* Sadece ilk 4 byte kullanilir (nvs_store UID_MAX_LEN = 9 = 8 hex + null) */
    uint8_t use_len = (uid_len > 4) ? 4 : uid_len;

    if (out_size < (use_len * 2 + 1)) {
        out[0] = '\0';
        return;
    }

    for (uint8_t i = 0; i < use_len; i++) {
        sprintf(&out[i * 2], "%02X", uid_bytes[i]);
    }
    out[use_len * 2] = '\0';
}

/**
 * @brief RC522 PICC durum degisikligi event handler
 * 
 * Kart ACTIVE durumuna gectiginde UID'yi hex string'e cevirip Queue'ya gonderir.
 */
static void on_picc_state_changed(void *arg, esp_event_base_t base, int32_t event_id, void *data)
{
    rc522_picc_state_changed_event_t *event = (rc522_picc_state_changed_event_t *)data;
    rc522_picc_t *picc = event->picc;

    if (picc->state == RC522_PICC_STATE_ACTIVE) {
        /* Kart algilandi — UID'yi isle */
        rfid_event_t evt;
        memset(&evt, 0, sizeof(evt));

        uid_to_hex_string(picc->uid.value, picc->uid.length, evt.uid_hex, sizeof(evt.uid_hex));

        if (evt.uid_hex[0] == '\0') {
            ESP_LOGW(TAG, "UID cevirme basarisiz (uzunluk: %d)", picc->uid.length);
            return;
        }

        ESP_LOGI(TAG, "Kart okundu: %s", evt.uid_hex);

        /* Queue'ya gonder — beklemeden (tasmissa drop et) */
        if (xQueueSend(s_event_queue, &evt, 0) != pdTRUE) {
            ESP_LOGW(TAG, "Event queue dolu, kart eventi atildi");
        }
    }
    else if (picc->state == RC522_PICC_STATE_IDLE && event->old_state >= RC522_PICC_STATE_ACTIVE) {
        ESP_LOGI(TAG, "Kart cikarildi");
    }
}

/* ============================================================
 * Public API
 * ============================================================ */

esp_err_t rfid_manager_init(QueueHandle_t event_queue)
{
    if (event_queue == NULL) {
        ESP_LOGE(TAG, "event_queue NULL olamaz");
        return ESP_ERR_INVALID_ARG;
    }

    s_event_queue = event_queue;

    /* SPI surucusu olustur */
    rc522_spi_config_t driver_config = {
        .host_id = SPI3_HOST,
        .bus_config = &(spi_bus_config_t){
            .miso_io_num = RC522_SPI_BUS_GPIO_MISO,
            .mosi_io_num = RC522_SPI_BUS_GPIO_MOSI,
            .sclk_io_num = RC522_SPI_BUS_GPIO_SCLK,
        },
        .dev_config = {
            .spics_io_num = RC522_SPI_SCANNER_GPIO_CS,
        },
        .rst_io_num = RC522_SCANNER_GPIO_RST,
    };

    esp_err_t ret = rc522_spi_create(&driver_config, &s_driver);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPI driver olusturulamadi: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = rc522_driver_install(s_driver);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPI driver yuklenemedi: %s", esp_err_to_name(ret));
        return ret;
    }

    /* RC522 scanner olustur */
    rc522_config_t scanner_config = {
        .driver = s_driver,
        .poll_interval_ms = 100,
        .task_stack_size = 4096,
        .task_priority = 5,
    };

    ret = rc522_create(&scanner_config, &s_scanner);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "RC522 scanner olusturulamadi: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Event handler kaydet */
    ret = rc522_register_events(s_scanner, RC522_EVENT_PICC_STATE_CHANGED, on_picc_state_changed, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Event handler kaydedilemedi: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "RFID manager baslatildi (CS=%d, SCK=%d, MOSI=%d, MISO=%d, RST=%d)",
             RC522_SPI_SCANNER_GPIO_CS, RC522_SPI_BUS_GPIO_SCLK,
             RC522_SPI_BUS_GPIO_MOSI, RC522_SPI_BUS_GPIO_MISO,
             RC522_SCANNER_GPIO_RST);

    return ESP_OK;
}

esp_err_t rfid_manager_start(void)
{
    if (s_scanner == NULL) {
        ESP_LOGE(TAG, "Scanner baslatilmamis, once rfid_manager_init() cagirin");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = rc522_start(s_scanner);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "RC522 baslatilamadi: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "RFID kart okuma baslatildi");
    return ESP_OK;
}

esp_err_t rfid_manager_stop(void)
{
    if (s_scanner == NULL) {
        ESP_LOGE(TAG, "Scanner baslatilmamis, once rfid_manager_init() cagirin");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = rc522_pause(s_scanner);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "RC522 durdurulamadi: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "RFID kart okuma durduruldu");
    return ESP_OK;
}
