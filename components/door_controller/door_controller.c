#include "door_controller.h"

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <dirent.h>
#include "esp_log.h"
#include "esp_littlefs.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "nvs_store.h"
#include "rfid_manager.h"

static const char *TAG = "door_ctrl";

/* ============================================================
 * Sabitler
 * ============================================================ */

/* GPIO pin tanimlari (AGENTS.md) */
#define GPIO_RELAY          GPIO_NUM_16     /* MOSFET tetikleme (HIGH = aktif) */
#define GPIO_RED_LED        GPIO_NUM_14     /* Kirmizi LED — surekli yanik, erisim verilince soner */

/* LittleFS yapilandirmasi */
#define STORAGE_PARTITION   "storage"
#define MOUNT_POINT         "/littlefs"
#define LOG_DIR             MOUNT_POINT "/logs"

/* Log saklama suresi (gun) */
#define LOG_RETENTION_DAYS  14

/* Temizleme kontrol araligi (saniye) — her 1 saatte bir */
#define CLEANUP_INTERVAL_SEC  3600

/* Minimum gecerli zaman damgasi (2024-01-01 00:00:00 UTC) */
#define MIN_VALID_TIMESTAMP   1704067200

/* ============================================================
 * Modul degiskenleri
 * ============================================================ */
static QueueHandle_t s_rfid_queue = NULL;
static SemaphoreHandle_t s_fs_mutex = NULL;

/* ============================================================
 * Dahili fonksiyon bildirimleri
 * ============================================================ */
static void door_task(void *arg);
static esp_err_t mount_littlefs(void);
static void init_gpio(void);
static void write_log_entry(const char *uid_hex, const char *name, const char *result);
static void cleanup_old_logs(void);
static void get_date_string(char *buf, size_t len);

/* ============================================================
 * Public API
 * ============================================================ */

esp_err_t door_controller_init(QueueHandle_t rfid_queue)
{
    if (rfid_queue == NULL) {
        ESP_LOGE(TAG, "rfid_queue NULL olamaz");
        return ESP_ERR_INVALID_ARG;
    }

    s_rfid_queue = rfid_queue;

    /* Mutex olustur */
    s_fs_mutex = xSemaphoreCreateMutex();
    if (s_fs_mutex == NULL) {
        ESP_LOGE(TAG, "FS mutex olusturulamadi");
        return ESP_ERR_NO_MEM;
    }

    /* LittleFS mount */
    esp_err_t ret = mount_littlefs();
    if (ret != ESP_OK) {
        return ret;
    }

    /* Log dizinini olustur (yoksa) */
    struct stat st;
    if (stat(LOG_DIR, &st) != 0) {
        if (mkdir(LOG_DIR, 0775) != 0) {
            ESP_LOGE(TAG, "Log dizini olusturulamadi: %s", LOG_DIR);
            return ESP_FAIL;
        }
        ESP_LOGI(TAG, "Log dizini olusturuldu: %s", LOG_DIR);
    }

    /* GPIO yapilandir */
    init_gpio();

    /* Baslangicta eski loglari temizle */
    cleanup_old_logs();

    /* door_task olustur — Oncelik 4, Core 0 */
    BaseType_t task_ret = xTaskCreatePinnedToCore(
        door_task,
        "door_task",
        6144,
        NULL,
        4,
        NULL,
        0
    );

    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "door_task olusturulamadi");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Door controller baslatildi (MOSFET_GPIO=%d aktif-HIGH, RED_LED=%d surekli yanik)",
             GPIO_RELAY, GPIO_RED_LED);

    return ESP_OK;
}

esp_err_t door_controller_read_log(const char *date, char *buf, size_t buf_size, size_t *out_len)
{
    if (date == NULL || buf == NULL || buf_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    char filepath[64];
    snprintf(filepath, sizeof(filepath), "%s/%s.log", LOG_DIR, date);

    if (xSemaphoreTake(s_fs_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE(TAG, "FS mutex alinamadi (read_log)");
        return ESP_ERR_TIMEOUT;
    }

    FILE *f = fopen(filepath, "r");
    if (f == NULL) {
        xSemaphoreGive(s_fs_mutex);
        return ESP_ERR_NOT_FOUND;
    }

    size_t space = buf_size - 1;  /* null terminator icin yer birak */
    size_t total_read = fread(buf, 1, space, f);
    buf[total_read] = '\0';

    fclose(f);
    xSemaphoreGive(s_fs_mutex);

    if (out_len != NULL) {
        *out_len = total_read;
    }

    return ESP_OK;
}

esp_err_t door_controller_get_log_dates(char dates[][DOOR_LOG_DATE_LEN], size_t max_dates, size_t *count)
{
    if (dates == NULL || count == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *count = 0;

    if (xSemaphoreTake(s_fs_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE(TAG, "FS mutex alinamadi (get_log_dates)");
        return ESP_ERR_TIMEOUT;
    }

    DIR *dir = opendir(LOG_DIR);
    if (dir == NULL) {
        xSemaphoreGive(s_fs_mutex);
        ESP_LOGW(TAG, "Log dizini acilamadi");
        return ESP_OK;  /* Bos liste doner */
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL && *count < max_dates) {
        /* Dosya adi "YYYY-MM-DD.log" formatinda olmali (14 karakter) */
        size_t name_len = strlen(entry->d_name);
        if (name_len == 14 && strcmp(&entry->d_name[10], ".log") == 0) {
            memcpy(dates[*count], entry->d_name, 10);
            dates[*count][10] = '\0';
            (*count)++;
        }
    }

    closedir(dir);
    xSemaphoreGive(s_fs_mutex);

    return ESP_OK;
}

esp_err_t door_controller_delete_log(const char *date)
{
    if (date == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    char filepath[64];
    snprintf(filepath, sizeof(filepath), "%s/%s.log", LOG_DIR, date);

    if (xSemaphoreTake(s_fs_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE(TAG, "FS mutex alinamadi (delete_log)");
        return ESP_ERR_TIMEOUT;
    }

    int ret = remove(filepath);
    xSemaphoreGive(s_fs_mutex);

    if (ret != 0) {
        return ESP_ERR_NOT_FOUND;
    }

    ESP_LOGI(TAG, "Log silindi: %s", date);
    return ESP_OK;
}

esp_err_t door_controller_get_fs_info(size_t *total_bytes, size_t *used_bytes)
{
    return esp_littlefs_info(STORAGE_PARTITION, total_bytes, used_bytes);
}

/* ============================================================
 * LittleFS mount
 * ============================================================ */

static esp_err_t mount_littlefs(void)
{
    esp_vfs_littlefs_conf_t conf = {
        .base_path = MOUNT_POINT,
        .partition_label = STORAGE_PARTITION,
        .format_if_mount_failed = true,
        .dont_mount = false,
    };

    esp_err_t ret = esp_vfs_littlefs_register(&conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LittleFS mount basarisiz: %s", esp_err_to_name(ret));
        return ret;
    }

    size_t total = 0, used = 0;
    esp_littlefs_info(STORAGE_PARTITION, &total, &used);
    ESP_LOGI(TAG, "LittleFS mount edildi — Toplam: %d KB, Kullanilan: %d KB",
             (int)(total / 1024), (int)(used / 1024));

    return ESP_OK;
}

/* ============================================================
 * GPIO yapilandirmasi
 * ============================================================ */

static void init_gpio(void)
{
    /* GPIO 16 — MOSFET tetikleme (aktif-HIGH)
     * ONCE LOW yap (MOSFET kapali), SONRA output olarak yapilandir. */
    gpio_set_level(GPIO_RELAY, 0);
    gpio_config_t door_conf = {
        .pin_bit_mask = (1ULL << GPIO_RELAY),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&door_conf));
    gpio_set_level(GPIO_RELAY, 0);

    ESP_LOGI(TAG, "GPIO %d LOW yapildi (MOSFET kapali)", GPIO_RELAY);

    /* GPIO 14 — Kirmizi LED (cikis, baslangicta HIGH — surekli yanik) */
    gpio_config_t red_conf = {
        .pin_bit_mask = (1ULL << GPIO_RED_LED),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&red_conf));
    gpio_set_level(GPIO_RED_LED, 1);

    ESP_LOGI(TAG, "GPIO yapilandirildi");
}

/* ============================================================
 * Tarih ve zaman yardimcilari
 * ============================================================ */

static void get_date_string(char *buf, size_t len)
{
    time_t now;
    time(&now);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    strftime(buf, len, "%Y-%m-%d", &timeinfo);
}

/* ============================================================
 * Log yazma
 * ============================================================ */

static void write_log_entry(const char *uid_hex, const char *name, const char *result)
{
    if (xSemaphoreTake(s_fs_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE(TAG, "FS mutex alinamadi — log yazilamadi");
        return;
    }

    /* Tarih string olustur */
    char date_str[DOOR_LOG_DATE_LEN];
    get_date_string(date_str, sizeof(date_str));

    /* Dosya yolu */
    char filepath[64];
    snprintf(filepath, sizeof(filepath), "%s/%s.log", LOG_DIR, date_str);

    /* Dosyayi append modunda ac */
    FILE *f = fopen(filepath, "a");
    if (f == NULL) {
        xSemaphoreGive(s_fs_mutex);
        ESP_LOGE(TAG, "Log dosyasi acilamadi: %s", filepath);
        return;
    }

    /* Zaman damgasi */
    time_t ts;
    time(&ts);

    /* JSON satiri yaz */
    fprintf(f, "{\"ts\":%ld,\"uid\":\"%s\",\"name\":\"%s\",\"result\":\"%s\"}\n",
            (long)ts, uid_hex, name, result);

    fclose(f);
    xSemaphoreGive(s_fs_mutex);

    ESP_LOGI(TAG, "Log yazildi: [%s] %s %s -> %s", date_str, uid_hex, name, result);
}

/* ============================================================
 * Eski log temizleme (14 gun)
 * ============================================================ */

static void cleanup_old_logs(void)
{
    time_t now;
    time(&now);

    /* Sistem saati ayarlanmamissa temizleme yapma */
    if (now < MIN_VALID_TIMESTAMP) {
        ESP_LOGW(TAG, "Sistem saati ayarlanmamis, log temizleme atlandi");
        return;
    }

    time_t cutoff = now - (LOG_RETENTION_DAYS * 86400);

    if (xSemaphoreTake(s_fs_mutex, pdMS_TO_TICKS(2000)) != pdTRUE) {
        ESP_LOGE(TAG, "FS mutex alinamadi — temizleme atlandi");
        return;
    }

    DIR *dir = opendir(LOG_DIR);
    if (dir == NULL) {
        xSemaphoreGive(s_fs_mutex);
        return;
    }

    struct dirent *entry;
    int deleted_count = 0;

    while ((entry = readdir(dir)) != NULL) {
        /* Sadece "YYYY-MM-DD.log" formatindaki dosyalar */
        size_t name_len = strlen(entry->d_name);
        if (name_len != 14 || strcmp(&entry->d_name[10], ".log") != 0) {
            continue;
        }

        /* Tarih parse et */
        struct tm file_tm = {0};
        char date_part[11];
        memcpy(date_part, entry->d_name, 10);
        date_part[10] = '\0';

        int year, month, day;
        if (sscanf(date_part, "%d-%d-%d", &year, &month, &day) != 3) {
            continue;
        }

        file_tm.tm_year = year - 1900;
        file_tm.tm_mon = month - 1;
        file_tm.tm_mday = day;

        time_t file_time = mktime(&file_tm);
        if (file_time < cutoff) {
            char filepath[280];
            snprintf(filepath, sizeof(filepath), "%s/%s", LOG_DIR, entry->d_name);
            if (remove(filepath) == 0) {
                deleted_count++;
                ESP_LOGI(TAG, "Eski log silindi: %s", entry->d_name);
            }
        }
    }

    closedir(dir);
    xSemaphoreGive(s_fs_mutex);

    if (deleted_count > 0) {
        ESP_LOGI(TAG, "%d eski log dosyasi silindi", deleted_count);
    }
}

/* ============================================================
 * door_task — Ana kapi kontrol dongusu
 * ============================================================ */

static void door_task(void *arg)
{
    rfid_event_t evt;
    time_t last_cleanup;
    time(&last_cleanup);

    ESP_LOGI(TAG, "door_task baslatildi — kart bekleniyor");

    while (1) {
        /*
         * Queue'dan RFID event bekle.
         * 30 saniye timeout — timeout olursa periyodik temizleme kontrolu yap.
         */
        if (xQueueReceive(s_rfid_queue, &evt, pdMS_TO_TICKS(30000)) == pdTRUE) {

            /* Karti NVS'te ara */
            bool authorized = nvs_store_card_exists(evt.uid_hex);

            /* Kart sahibi ismini al */
            char name[NVS_STORE_NAME_MAX_LEN];
            if (authorized) {
                nvs_store_card_get_name(evt.uid_hex, name, sizeof(name));
            } else {
                strcpy(name, "unknown");
            }

            if (authorized) {
                /* ============== ERISIM VERILDI ============== */
                ESP_LOGI(TAG, "ERISIM VERILDI — %s (%s)", name, evt.uid_hex);

                /* Kirmizi LED sondir (erisim gostergesi) */
                gpio_set_level(GPIO_RED_LED, 0);

                /* MOSFET tetikle — kapi ac (HIGH = aktif) */
                gpio_set_level(GPIO_RELAY, 1);

                /* Log yaz */
                write_log_entry(evt.uid_hex, name, "OK");

                /* door_delay kadar bekle */
                uint16_t delay_sec = 3;  /* varsayilan */
                nvs_store_config_get_door_delay(&delay_sec);
                vTaskDelay(pdMS_TO_TICKS(delay_sec * 1000));

                /* MOSFET kapat — kapi kapat (LOW = kapali) */
                gpio_set_level(GPIO_RELAY, 0);

                /* Kirmizi LED tekrar yak (standby durumu) */
                gpio_set_level(GPIO_RED_LED, 1);

            } else {
                /* ============== ERISIM REDDEDILDI ============== */
                ESP_LOGW(TAG, "ERISIM REDDEDILDI — Bilinmeyen kart: %s", evt.uid_hex);

                /* Kirmizi LED zaten surekli yanik — ek islem yok */

                /* Log yaz */
                write_log_entry(evt.uid_hex, name, "DENY");
            }
        }

        /* Periyodik log temizleme (her CLEANUP_INTERVAL_SEC saniyede bir) */
        time_t now;
        time(&now);
        if ((now - last_cleanup) >= CLEANUP_INTERVAL_SEC) {
            cleanup_old_logs();
            last_cleanup = now;
        }
    }
}
