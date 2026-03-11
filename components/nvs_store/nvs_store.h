#ifndef NVS_STORE_H
#define NVS_STORE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Maksimum kart UID hex string uzunlugu (8 hex karakter + null)
 * 4 byte UID = "AABBCCDD" = 8 karakter
 */
#define NVS_STORE_UID_MAX_LEN   9

/**
 * @brief Maksimum kart isim uzunlugu (null dahil)
 */
#define NVS_STORE_NAME_MAX_LEN  32

/**
 * @brief Maksimum kayitli kart sayisi
 * NVS namespace basina key limiti var, 100 kart yeterli
 */
#define NVS_STORE_MAX_CARDS     100

/**
 * @brief Kart bilgisi yapisi
 */
typedef struct {
    char uid[NVS_STORE_UID_MAX_LEN];      // UID hex string ("AABBCCDD")
    char name[NVS_STORE_NAME_MAX_LEN];    // Kart sahibi ismi
} card_entry_t;

/* ============================================================
 * Baslatma
 * ============================================================ */

/**
 * @brief NVS store componentini baslatir
 * 
 * NVS flash'i baslatir ve mutex olusturur.
 * Bu fonksiyon nvs_flash_init() SONRA cagrilmali.
 * 
 * @return ESP_OK basarili, hata kodu aksi halde
 */
esp_err_t nvs_store_init(void);

/* ============================================================
 * Kart islemleri (NVS namespace: "cards")
 * ============================================================ */

/**
 * @brief Yeni kart ekler
 * 
 * @param uid_hex  UID hex string (ornek: "AABBCCDD")
 * @param name     Kart sahibi ismi
 * @return ESP_OK basarili, ESP_ERR_NVS_NOT_ENOUGH_SPACE dolu, ESP_ERR_INVALID_ARG gecersiz parametre
 */
esp_err_t nvs_store_card_add(const char *uid_hex, const char *name);

/**
 * @brief Kart siler
 * 
 * @param uid_hex  Silinecek kartin UID hex string'i
 * @return ESP_OK basarili, ESP_ERR_NVS_NOT_FOUND bulunamadi
 */
esp_err_t nvs_store_card_remove(const char *uid_hex);

/**
 * @brief Kartin kayitli olup olmadigini kontrol eder
 * 
 * @param uid_hex  Kontrol edilecek UID hex string
 * @return true kayitli, false kayitli degil
 */
bool nvs_store_card_exists(const char *uid_hex);

/**
 * @brief Kartin sahibi ismini getirir
 * 
 * @param uid_hex   UID hex string
 * @param name_buf  Ismin yazilacagi buffer
 * @param len       Buffer uzunlugu
 * @return ESP_OK basarili, ESP_ERR_NVS_NOT_FOUND bulunamadi
 */
esp_err_t nvs_store_card_get_name(const char *uid_hex, char *name_buf, size_t len);

/**
 * @brief Tum kayitli kartlari listeler
 * 
 * @param out    Kart dizisi (en az NVS_STORE_MAX_CARDS elemanli olmali)
 * @param count  Giris: dizinin kapasitesi, Cikis: bulunan kart sayisi
 * @return ESP_OK basarili
 */
esp_err_t nvs_store_card_list(card_entry_t *out, size_t *count);

/* ============================================================
 * Config islemleri (NVS namespace: "config")
 * ============================================================ */

/**
 * @brief Kapi acik kalma suresini getirir (saniye)
 * @param value  Deger yazilacak pointer
 * @return ESP_OK basarili, ESP_ERR_NVS_NOT_FOUND ayar yok (varsayilan 3 kullanilir)
 */
esp_err_t nvs_store_config_get_door_delay(uint16_t *value);

/**
 * @brief Kapi acik kalma suresini ayarlar (saniye)
 * @param value  Yeni deger (1-30 arasi)
 * @return ESP_OK basarili
 */
esp_err_t nvs_store_config_set_door_delay(uint16_t value);

/**
 * @brief AP modu sifresini getirir
 * @param buf  Sifrenin yazilacagi buffer
 * @param len  Buffer uzunlugu
 * @return ESP_OK basarili, ESP_ERR_NVS_NOT_FOUND ayar yok (varsayilan "kapi1234")
 */
esp_err_t nvs_store_config_get_ap_pass(char *buf, size_t len);

/**
 * @brief AP modu sifresini ayarlar
 * @param pass  Yeni sifre (en az 8 karakter)
 * @return ESP_OK basarili
 */
esp_err_t nvs_store_config_set_ap_pass(const char *pass);

/**
 * @brief Wi-Fi STA SSID'sini getirir
 * @param buf  SSID'nin yazilacagi buffer
 * @param len  Buffer uzunlugu
 * @return ESP_OK basarili, ESP_ERR_NVS_NOT_FOUND ayar yok
 */
esp_err_t nvs_store_config_get_wifi_ssid(char *buf, size_t len);

/**
 * @brief Wi-Fi STA SSID'sini ayarlar
 * @param ssid  Yeni SSID
 * @return ESP_OK basarili
 */
esp_err_t nvs_store_config_set_wifi_ssid(const char *ssid);

/**
 * @brief Wi-Fi STA sifresini getirir
 * @param buf  Sifrenin yazilacagi buffer
 * @param len  Buffer uzunlugu
 * @return ESP_OK basarili, ESP_ERR_NVS_NOT_FOUND ayar yok
 */
esp_err_t nvs_store_config_get_wifi_pass(char *buf, size_t len);

/**
 * @brief Wi-Fi STA sifresini ayarlar
 * @param pass  Yeni sifre
 * @return ESP_OK basarili
 */
esp_err_t nvs_store_config_set_wifi_pass(const char *pass);

#ifdef __cplusplus
}
#endif

#endif // NVS_STORE_H
