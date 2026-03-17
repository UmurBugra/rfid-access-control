#ifndef DOOR_CONTROLLER_H
#define DOOR_CONTROLLER_H

#include <stddef.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Log tarih string uzunlugu ("YYYY-MM-DD" + null = 11)
 */
#define DOOR_LOG_DATE_LEN   11

/**
 * @brief Maksimum listelenecek log tarihi sayisi
 */
#define DOOR_MAX_LOG_DATES  30

/**
 * @brief Door controller componentini baslatir
 *
 * - LittleFS'i "storage" partition uzerine mount eder
 * - GPIO yapilandirir (26 = role tetikleme, 14 = kirmizi LED)
 * - door_task olusturur (oncelik 4, Core 0)
 * - Eski loglari temizler (14 gun)
 *
 * @param rfid_queue  rfid_event_t tipinde elemanlari olan FreeRTOS Queue handle
 * @return ESP_OK basarili, hata kodu aksi halde
 */
esp_err_t door_controller_init(QueueHandle_t rfid_queue);

/* ============================================================
 * Log islemleri (web_server tarafindan kullanilacak)
 * ============================================================ */

/**
 * @brief Belirli bir tarihin log dosyasini okur
 *
 * @param date      Tarih string "YYYY-MM-DD"
 * @param buf       Log iceriginin yazilacagi buffer
 * @param buf_size  Buffer boyutu
 * @param out_len   Okunan byte sayisi (NULL olabilir)
 * @return ESP_OK basarili, ESP_ERR_NOT_FOUND log dosyasi yok
 */
esp_err_t door_controller_read_log(const char *date, char *buf, size_t buf_size, size_t *out_len);

/**
 * @brief Mevcut log tarihlerini listeler
 *
 * @param dates      Tarih dizisi (her eleman DOOR_LOG_DATE_LEN byte)
 * @param max_dates  Dizinin kapasitesi
 * @param count      Bulunan tarih sayisi
 * @return ESP_OK basarili
 */
esp_err_t door_controller_get_log_dates(char dates[][DOOR_LOG_DATE_LEN], size_t max_dates, size_t *count);

/**
 * @brief Belirli bir tarihin log dosyasini siler
 *
 * @param date  Tarih string "YYYY-MM-DD"
 * @return ESP_OK basarili, ESP_ERR_NOT_FOUND log dosyasi yok
 */
esp_err_t door_controller_delete_log(const char *date);

/**
 * @brief LittleFS dosya sistemi bilgisini getirir
 *
 * @param total_bytes  Toplam alan (byte)
 * @param used_bytes   Kullanilan alan (byte)
 * @return ESP_OK basarili
 */
esp_err_t door_controller_get_fs_info(size_t *total_bytes, size_t *used_bytes);

#ifdef __cplusplus
}
#endif

#endif // DOOR_CONTROLLER_H
