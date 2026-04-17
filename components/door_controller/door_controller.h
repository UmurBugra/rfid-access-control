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
 * - GPIO yapilandirir (16 = MOSFET tetikleme aktif-HIGH, 14 = kirmizi LED surekli yanik)
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

/**
 * @brief Web panelinden manuel kapi acma
 *
 * Admin oturumu dogrulanmis kullanici icin dogrudan kapıyı acar.
 * RFID Queue ve NVS kart kontrolu bypass edilir.
 * GPIO 16 (MOSFET) tetiklenir, log yazilir (kaynak: "WEB").
 * door_delay kadar bekledikten sonra kapi kapatilir.
 *
 * NOT: Bu fonksiyon door_delay suresi kadar BLOKLAYICI calisir.
 * Cagiran task'in bu sureye toleransi olmali.
 *
 * @return ESP_OK basarili, ESP_ERR_INVALID_STATE kapi zaten acik
 */
esp_err_t door_controller_trigger_open(void);

#ifdef __cplusplus
}
#endif

#endif // DOOR_CONTROLLER_H
