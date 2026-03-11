#include <stdio.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "nvs_store.h"
#include "auth_manager.h"
#include "rfid_manager.h"
#include "door_controller.h"
#include "wifi_manager.h"
#include "web_server.h"
#include "ota_manager.h"

static const char *TAG = "main";

/**
 * @brief RFID event queue boyutu
 * 
 * rfid_manager kart okundugunda bu Queue'ya rfid_event_t gonderir.
 * door_task bu Queue'yu dinleyerek kapi kontrolu yapar.
 */
#define RFID_QUEUE_SIZE  4

void app_main(void)
{
    ESP_LOGI(TAG, "Sistem baslatiliyor");

    /* NVS flash baslatma */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition siliniyor ve yeniden baslatiliyor");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* OTA manager baslatma — rollback iptal, firmware gecerli isaretle
     * NVS'ten hemen sonra, diger component'lerden once cagirilmali.
     * Boylece OTA sonrasi ilk boot'ta firmware hemen onaylanir. */
    ESP_ERROR_CHECK(ota_manager_init());

    /* NVS store baslatma */
    ESP_ERROR_CHECK(nvs_store_init());

    /* Auth manager baslatma */
    ESP_ERROR_CHECK(auth_manager_init());

    /* Wi-Fi manager baslatma — AP+STA */
    ESP_ERROR_CHECK(wifi_manager_init());

    /* RFID Queue olustur */
    QueueHandle_t rfid_queue = xQueueCreate(RFID_QUEUE_SIZE, sizeof(rfid_event_t));
    if (rfid_queue == NULL) {
        ESP_LOGE(TAG, "RFID queue olusturulamadi");
        return;
    }

    /* RFID manager baslatma — donanim bagli degilse sistem calismaya devam eder */
    bool rfid_ok = false;
    ret = rfid_manager_init(rfid_queue);
    if (ret == ESP_OK) {
        ret = rfid_manager_start();
        if (ret == ESP_OK) {
            rfid_ok = true;
            ESP_LOGI(TAG, "RFID manager basariyla baslatildi");
        } else {
            ESP_LOGW(TAG, "RC522 baslatilamadi (donanim bagli degil?): %s", esp_err_to_name(ret));
        }
    } else {
        ESP_LOGW(TAG, "RFID manager init basarisiz (donanim bagli degil?): %s", esp_err_to_name(ret));
    }

    if (!rfid_ok) {
        ESP_LOGW(TAG, "RFID devre disi — sistem RFID'siz devam edecek");
    }

    /* Door controller baslatma — LittleFS mount, GPIO init, door_task olustur */
    ESP_ERROR_CHECK(door_controller_init(rfid_queue));

    /* Web server baslatma — HTTP server, REST API, embedded HTML */
    ESP_ERROR_CHECK(web_server_init());

    ESP_LOGI(TAG, "Sistem baslatma tamamlandi");
}
