#include "ota_manager.h"

#include <string.h>
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_app_desc.h"
#include "esp_system.h"

static const char *TAG = "ota_mgr";

/* ============================================================
 * Modul degiskenleri
 * ============================================================ */
static esp_ota_handle_t s_ota_handle = 0;
static const esp_partition_t *s_update_partition = NULL;
static bool s_ota_in_progress = false;
static size_t s_bytes_written = 0;

/* ============================================================
 * Public API
 * ============================================================ */

esp_err_t ota_manager_init(void)
{
    /*
     * Mevcut firmware'i gecerli olarak isaretle.
     * Bu cagri rollback zamanlayicisini iptal eder.
     * Ilk boot'ta veya OTA kullanilmamissa hata donebilir — sorun degil.
     */
    esp_err_t ret = esp_ota_mark_app_valid_cancel_rollback();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Rollback iptal edilemedi: %s (ilk boot olabilir)",
                 esp_err_to_name(ret));
    }

    /* Aktif partition bilgisi */
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (running != NULL) {
        ESP_LOGI(TAG, "Aktif partition: %s (addr=0x%lx)",
                 running->label, (unsigned long)running->address);
    }

    /* Firmware bilgileri */
    const esp_app_desc_t *app_desc = esp_app_get_description();
    if (app_desc != NULL) {
        ESP_LOGI(TAG, "Firmware: %s, IDF: %s", app_desc->version, app_desc->idf_ver);
        ESP_LOGI(TAG, "Derleme: %s %s", app_desc->date, app_desc->time);
    }

    return ESP_OK;
}

esp_err_t ota_manager_begin(size_t firmware_size)
{
    if (s_ota_in_progress) {
        ESP_LOGE(TAG, "OTA zaten devam ediyor");
        return ESP_ERR_INVALID_STATE;
    }

    /* Hedef partition'i bul */
    s_update_partition = esp_ota_get_next_update_partition(NULL);
    if (s_update_partition == NULL) {
        ESP_LOGE(TAG, "Hedef OTA partition bulunamadi");
        return ESP_ERR_NOT_FOUND;
    }

    ESP_LOGI(TAG, "OTA basladi — hedef: %s (addr=0x%lx, size=%lu)",
             s_update_partition->label,
             (unsigned long)s_update_partition->address,
             (unsigned long)s_update_partition->size);

    /* Boyut kontrolu */
    if (firmware_size > 0 && firmware_size > s_update_partition->size) {
        ESP_LOGE(TAG, "Firmware boyutu (%u) partition'a sigmaz (%lu)",
                 (unsigned)firmware_size,
                 (unsigned long)s_update_partition->size);
        s_update_partition = NULL;
        return ESP_ERR_INVALID_SIZE;
    }

    /* OTA yazma oturumu ac */
    esp_err_t ret = esp_ota_begin(s_update_partition,
                                  firmware_size > 0 ? firmware_size : OTA_SIZE_UNKNOWN,
                                  &s_ota_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin basarisiz: %s", esp_err_to_name(ret));
        s_update_partition = NULL;
        return ret;
    }

    s_ota_in_progress = true;
    s_bytes_written = 0;
    return ESP_OK;
}

esp_err_t ota_manager_write(const char *data, size_t len)
{
    if (!s_ota_in_progress) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = esp_ota_write(s_ota_handle, data, len);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_write basarisiz (offset=%u): %s",
                 (unsigned)s_bytes_written, esp_err_to_name(ret));
        return ret;
    }

    s_bytes_written += len;
    return ESP_OK;
}

esp_err_t ota_manager_end(void)
{
    if (!s_ota_in_progress) {
        return ESP_ERR_INVALID_STATE;
    }

    /* Yazmayi bitir — firmware dogrulanir (SHA-256 vs.) */
    esp_err_t ret = esp_ota_end(s_ota_handle);
    s_ota_in_progress = false;
    s_ota_handle = 0;

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end basarisiz: %s", esp_err_to_name(ret));
        s_update_partition = NULL;
        s_bytes_written = 0;
        return ret;
    }

    ESP_LOGI(TAG, "OTA yazma tamamlandi (%u bytes)", (unsigned)s_bytes_written);

    /* Boot partition'i yeni firmware'e ayarla */
    ret = esp_ota_set_boot_partition(s_update_partition);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Boot partition ayarlanamadi: %s", esp_err_to_name(ret));
        s_update_partition = NULL;
        s_bytes_written = 0;
        return ret;
    }

    ESP_LOGI(TAG, "Boot partition ayarlandi: %s — yeniden baslatma gerekli",
             s_update_partition->label);
    s_update_partition = NULL;
    s_bytes_written = 0;
    return ESP_OK;
}

esp_err_t ota_manager_abort(void)
{
    if (!s_ota_in_progress) {
        return ESP_OK;
    }

    esp_err_t ret = esp_ota_abort(s_ota_handle);
    s_ota_in_progress = false;
    s_ota_handle = 0;
    s_update_partition = NULL;
    s_bytes_written = 0;

    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "esp_ota_abort basarisiz: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "OTA iptal edildi");
    }
    return ret;
}

esp_err_t ota_manager_get_info(ota_info_t *info)
{
    if (info == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(info, 0, sizeof(ota_info_t));

    /* Aktif partition */
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (running != NULL) {
        info->running_partition = running->label;
    }

    /* Sonraki OTA partition */
    const esp_partition_t *next = esp_ota_get_next_update_partition(NULL);
    if (next != NULL) {
        info->next_partition = next->label;
    }

    /* Firmware bilgileri */
    const esp_app_desc_t *app_desc = esp_app_get_description();
    if (app_desc != NULL) {
        info->app_version  = app_desc->version;
        info->idf_version  = app_desc->idf_ver;
        info->compile_date = app_desc->date;
        info->compile_time = app_desc->time;
    }

    return ESP_OK;
}

bool ota_manager_is_in_progress(void)
{
    return s_ota_in_progress;
}
