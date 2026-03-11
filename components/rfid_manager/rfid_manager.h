#ifndef RFID_MANAGER_H
#define RFID_MANAGER_H

#include <stdint.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Maksimum UID hex string uzunlugu
 * 
 * RC522 UID en fazla 10 byte olabilir (triple size),
 * ancak cogu kart 4 byte kullanir: "AABBCCDD" = 8 karakter.
 * 10 byte = "AABBCCDDEEFF112233AA" = 20 karakter + null = 21.
 * nvs_store ile uyumlu olmasi icin 4 byte UID destekliyoruz (8 hex + null = 9).
 */
#define RFID_UID_HEX_MAX_LEN  9

/**
 * @brief Queue'ya gonderilen RFID olay yapisi
 * 
 * Kart okundugunda bu struct Queue'ya gonderilir.
 * door_task bu Queue'yu dinler.
 */
typedef struct {
    char uid_hex[RFID_UID_HEX_MAX_LEN];  /**< UID hex string, ornek: "A3F2C1B0" */
} rfid_event_t;

/**
 * @brief RFID manager componentini baslatir
 * 
 * RC522 SPI surucusunu ve scanner'i olusturur.
 * Event handler kaydeder.
 * 
 * Okunan kart UID'leri verilen Queue'ya rfid_event_t olarak gonderilir.
 * Queue'yu olusturmak ve dinlemek cagiran tarafin (door_task) sorumlulugundarir.
 * 
 * @param event_queue  rfid_event_t tipinde elemanlari olan FreeRTOS Queue handle
 * @return ESP_OK basarili, hata kodu aksi halde
 */
esp_err_t rfid_manager_init(QueueHandle_t event_queue);

/**
 * @brief RFID kart okumaya baslar
 * 
 * RC522 polling baslatir. Kutuphane kendi task'ini olusturur.
 * 
 * @return ESP_OK basarili
 */
esp_err_t rfid_manager_start(void);

/**
 * @brief RFID kart okumayı durdurur
 * 
 * RC522 polling duraklatir. Tekrar baslatmak icin rfid_manager_start() cagrilir.
 * 
 * @return ESP_OK basarili
 */
esp_err_t rfid_manager_stop(void);

#ifdef __cplusplus
}
#endif

#endif // RFID_MANAGER_H
