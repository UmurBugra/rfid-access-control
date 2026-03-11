#ifndef OTA_MANAGER_H
#define OTA_MANAGER_H

#include "esp_err.h"
#include <stddef.h>
#include <stdbool.h>

/**
 * @brief OTA durum bilgisi
 */
typedef struct {
    const char *running_partition;   /**< Aktif partition label (ota_0 / ota_1) */
    const char *next_partition;      /**< Sonraki OTA partition label */
    const char *app_version;         /**< Mevcut firmware versiyonu */
    const char *idf_version;         /**< ESP-IDF versiyonu */
    const char *compile_date;        /**< Derleme tarihi */
    const char *compile_time;        /**< Derleme saati */
} ota_info_t;

/**
 * @brief OTA manager baslatma
 * 
 * Mevcut firmware'i gecerli olarak isaretle (rollback iptal).
 * app_main() icinde erken cagirilmali — NVS'ten once bile olabilir.
 * 
 * @return ESP_OK her zaman (hata olsa bile sistem calismaya devam etmeli)
 */
esp_err_t ota_manager_init(void);

/**
 * @brief OTA guncelleme oturumu baslat
 * 
 * Hedef partition'i bul ve yazma oturumu ac.
 * 
 * @param firmware_size Firmware boyutu (0 ise bilinmiyor)
 * @return ESP_OK basarili, ESP_ERR_INVALID_STATE zaten devam ediyor,
 *         ESP_ERR_NOT_FOUND partition bulunamadi,
 *         ESP_ERR_INVALID_SIZE partition'a sigmaz
 */
esp_err_t ota_manager_begin(size_t firmware_size);

/**
 * @brief OTA verisi yaz
 * 
 * Chunk chunk cagirilir. Her cagri bir veri parcasini OTA partition'a yazar.
 * 
 * @param data Veri
 * @param len  Veri boyutu
 * @return ESP_OK basarili, ESP_ERR_INVALID_STATE oturum acik degil
 */
esp_err_t ota_manager_write(const char *data, size_t len);

/**
 * @brief OTA guncelleme tamamla
 * 
 * Firmware'i dogrula, boot partition'i yeni firmware'e ayarla.
 * Basarili donerse sistem yeniden baslatilmali.
 * 
 * @return ESP_OK basarili (restart gerekli), ESP_ERR_INVALID_STATE oturum yok
 */
esp_err_t ota_manager_end(void);

/**
 * @brief OTA guncellemeyi iptal et
 * 
 * Devam eden OTA oturumunu temizle.
 * 
 * @return ESP_OK basarili veya iptal edilecek bir sey yok
 */
esp_err_t ota_manager_abort(void);

/**
 * @brief OTA bilgilerini al
 * 
 * Aktif partition, sonraki partition, firmware versiyonu vb.
 * Donen pointer'lar statik alana isaret eder — kopyalamaya gerek yok
 * ama thread-safe degildir.
 * 
 * @param[out] info Bilgi yapisi
 * @return ESP_OK basarili
 */
esp_err_t ota_manager_get_info(ota_info_t *info);

/**
 * @brief OTA guncelleme devam ediyor mu?
 * 
 * @return true devam ediyor, false hayir
 */
bool ota_manager_is_in_progress(void);

#endif /* OTA_MANAGER_H */
