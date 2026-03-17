#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <stdbool.h>
#include "esp_err.h"
#include "esp_netif_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Wi-Fi AP varsayilan ayarlar
 */
#define WIFI_AP_SSID            "KAPI-SISTEMI-v1.1"
#define WIFI_AP_CHANNEL         1
#define WIFI_AP_MAX_CONN        4

/**
 * @brief STA baglanti yeniden deneme ayarlari
 */
#define WIFI_STA_RETRY_INIT_MS  5000    /* Ilk deneme bekleme suresi */
#define WIFI_STA_RETRY_MAX_MS   60000   /* Maksimum bekleme suresi */

/**
 * @brief Wi-Fi manager componentini baslatir
 *
 * - esp_netif_init + event loop olusturur
 * - AP+STA modunda Wi-Fi baslatir
 * - AP: SSID "KAPI-SISTEMI", sifre NVS'ten (ap_pass, varsayilan "kapi1234")
 * - STA: NVS'te wifi_ssid varsa baglanti denemesi baslatir
 * - Event handler kaydeder (baglanti/kopma/IP alma)
 *
 * nvs_store_init() ONCEDEN cagrilmis olmali.
 *
 * @return ESP_OK basarili, hata kodu aksi halde
 */
esp_err_t wifi_manager_init(void);

/**
 * @brief STA baglantisi aktif mi?
 * @return true STA bagli ve IP almis, false degil
 */
bool wifi_manager_is_sta_connected(void);

/**
 * @brief STA IP adresini getirir
 *
 * @param ip_buf    IP string yazilacak buffer (en az 16 byte)
 * @param buf_len   Buffer uzunlugu
 * @return ESP_OK basarili, ESP_ERR_INVALID_STATE STA bagli degil
 */
esp_err_t wifi_manager_get_sta_ip(char *ip_buf, size_t buf_len);

/**
 * @brief AP IP adresini getirir
 *
 * @param ip_buf    IP string yazilacak buffer (en az 16 byte)
 * @param buf_len   Buffer uzunlugu
 * @return ESP_OK basarili
 */
esp_err_t wifi_manager_get_ap_ip(char *ip_buf, size_t buf_len);

/**
 * @brief AP'ye bagli istemci sayisini getirir
 * @return Bagli istemci sayisi
 */
uint8_t wifi_manager_get_ap_client_count(void);

/**
 * @brief Wi-Fi yapilandirmasini NVS'ten yeniden okuyup uygular
 *
 * Web panelinden AP sifresi veya STA ayarlari degistirildiginde
 * bu fonksiyon cagrilarak Wi-Fi yeniden yapilandirilir.
 *
 * @return ESP_OK basarili
 */
esp_err_t wifi_manager_reconfigure(void);

#ifdef __cplusplus
}
#endif

#endif // WIFI_MANAGER_H
