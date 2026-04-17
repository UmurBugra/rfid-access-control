#ifndef AUTH_MANAGER_H
#define AUTH_MANAGER_H

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Session token string uzunlugu (64 hex karakter + null)
 * 32 byte = 64 hex karakter
 */
#define AUTH_TOKEN_LEN  65

/**
 * @brief Maksimum kullanici adi uzunlugu (null dahil)
 */
#define AUTH_USERNAME_MAX_LEN  32

/**
 * @brief Maksimum sifre uzunlugu (null dahil)
 */
#define AUTH_PASSWORD_MAX_LEN  64

/**
 * @brief Ayni anda aktif olabilecek maksimum session sayisi
 */
#define AUTH_MAX_SESSIONS  8

/**
 * @brief auth_manager componentini baslatir
 *
 * NVS "auth" namespace'ini kontrol eder.
 * Eger hash kayitli degilse fabrika varsayilanini yazar: admin / admin1234
 * nvs_store_init() SONRA cagrilmali.
 *
 * @return ESP_OK basarili
 */
esp_err_t auth_manager_init(void);

/**
 * @brief Kullanici adi ve sifre ile giris dogrulama
 *
 * SHA-256(password) hesaplar, NVS'teki hash ile karsilastirir.
 * Eslesirse 32 byte rastgele token uretir, RAM'deki session listesine ekler,
 * expiry = simdi + 86400 saniye (24 saat).
 * Ayni anda en fazla AUTH_MAX_SESSIONS kadar aktif oturum olabilir.
 * Limit asildiysa en eski oturum silinir.
 *
 * @param username   Kullanici adi
 * @param password   Sifre (duz metin -- hash'lenmeden once)
 * @param token_out  Basarili giriste token yazilacak buffer (en az AUTH_TOKEN_LEN byte)
 * @return ESP_OK basarili giris, ESP_ERR_INVALID_ARG gecersiz parametre,
 *         ESP_FAIL yanlis kullanici adi veya sifre
 */
esp_err_t auth_manager_verify(const char *username, const char *password, char *token_out);

/**
 * @brief Session token gecerliligi kontrol eder
 *
 * Token RAM'deki session listesinde var mi ve expiry gecmemis mi kontrol eder.
 *
 * @param token  Kontrol edilecek token string
 * @return true gecerli, false gecersiz veya suresi dolmus
 */
bool auth_manager_validate_token(const char *token);

/**
 * @brief Belirli bir token'in oturumunu kapatir
 *
 * RAM'deki session listesinden ilgili token'i siler.
 * Sadece o cihazin oturumu kapatilir, diger oturumlar etkilenmez.
 *
 * @param token  Kapatilacak oturumun token'i (NULL ise islem yapilmaz)
 * @return ESP_OK basarili
 */
esp_err_t auth_manager_logout(const char *token);

/**
 * @brief Tum aktif oturumlari kapatir
 *
 * RAM'deki tum session'lari gecersiz kilar.
 * Sifre degistirme gibi durumlarda kullanilir.
 *
 * @return ESP_OK basarili
 */
esp_err_t auth_manager_logout_all(void);

/**
 * @brief Admin sifresini degistirir
 *
 * Eski sifre dogrulanir, yenisi SHA-256 hash olarak NVS'e yazilir.
 * Tum aktif oturumlar gecersiz kilinir.
 *
 * @param old_pass  Mevcut sifre (duz metin)
 * @param new_pass  Yeni sifre (duz metin, en az 4 karakter)
 * @return ESP_OK basarili, ESP_FAIL eski sifre yanlis,
 *         ESP_ERR_INVALID_ARG gecersiz parametre
 */
esp_err_t auth_manager_change_password(const char *old_pass, const char *new_pass);

#ifdef __cplusplus
}
#endif

#endif // AUTH_MANAGER_H
