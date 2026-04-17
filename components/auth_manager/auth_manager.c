#include "auth_manager.h"

#include <string.h>
#include <stdio.h>
#include <time.h>
#include "esp_log.h"
#include "esp_random.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "mbedtls/sha256.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "auth_mgr";

/* NVS namespace ve key isimleri */
#define NVS_NS_AUTH     "auth"
#define KEY_USERNAME    "username"
#define KEY_PASS_HASH   "pass_hash"

/* Fabrika varsayilanlari */
#define DEFAULT_USERNAME  "admin"
#define DEFAULT_PASSWORD  "admin1234"

/* Token gecerlilik suresi (saniye) */
#define TOKEN_LIFETIME_SEC  86400  /* 24 saat */

/* SHA-256 hash hex string uzunlugu (64 hex + null) */
#define SHA256_HEX_LEN  65

/* ============================================================
 * Coklu session yapisi (RAM'de tutulur)
 * ============================================================ */

typedef struct {
    char token[AUTH_TOKEN_LEN];  /* 64 hex + null */
    int64_t expiry;              /* unix timestamp */
    bool active;                 /* slot kullaniliyor mu */
} session_entry_t;

static session_entry_t s_sessions[AUTH_MAX_SESSIONS];

/* Erisim icin mutex */
static SemaphoreHandle_t s_auth_mutex = NULL;

/* ============================================================
 * Yardimci fonksiyonlar
 * ============================================================ */

static bool auth_lock(void)
{
    if (s_auth_mutex == NULL) {
        ESP_LOGE(TAG, "Mutex baslatilmamis");
        return false;
    }
    if (xSemaphoreTake(s_auth_mutex, pdMS_TO_TICKS(2000)) != pdTRUE) {
        ESP_LOGE(TAG, "Mutex alinamadi (timeout)");
        return false;
    }
    return true;
}

static void auth_unlock(void)
{
    xSemaphoreGive(s_auth_mutex);
}

/**
 * @brief Duz metin sifrenin SHA-256 hash'ini hex string olarak hesaplar
 *
 * @param password   Duz metin sifre
 * @param hash_out   64 hex karakter + null (en az SHA256_HEX_LEN byte buffer)
 * @return ESP_OK basarili
 */
static esp_err_t compute_sha256(const char *password, char *hash_out)
{
    unsigned char sha_buf[32];

    /* mbedtls_sha256: son parametre 0 = SHA-256 (1 olursa SHA-224) */
    int ret = mbedtls_sha256((const unsigned char *)password, strlen(password), sha_buf, 0);
    if (ret != 0) {
        ESP_LOGE(TAG, "SHA-256 hesaplama hatasi: %d", ret);
        return ESP_FAIL;
    }

    /* Binary -> hex string */
    for (int i = 0; i < 32; i++) {
        sprintf(hash_out + (i * 2), "%02x", sha_buf[i]);
    }
    hash_out[64] = '\0';

    return ESP_OK;
}

/**
 * @brief 32 byte rastgele token uretir ve hex string olarak yazar
 *
 * @param token_out  64 hex karakter + null (en az AUTH_TOKEN_LEN byte buffer)
 */
static void generate_token(char *token_out)
{
    uint8_t rand_bytes[32];

    /* esp_random() kriptografik kalitede rastgele sayi uretir */
    for (int i = 0; i < 32; i += 4) {
        uint32_t r = esp_random();
        memcpy(&rand_bytes[i], &r, 4);
    }

    for (int i = 0; i < 32; i++) {
        sprintf(token_out + (i * 2), "%02x", rand_bytes[i]);
    }
    token_out[64] = '\0';
}

/**
 * @brief Simdiki zamani unix timestamp olarak dondurur
 */
static int64_t get_current_time(void)
{
    /* ESP32'de NTP senkronizasyonu yoksa time() epoch'tan bu yana
       gecen sureyi donerdir. Sistem uptime olarak kullanilabilir.
       SNTP yoksa boot zamani 0'dan baslar, ama token karsilastirmasi
       icin tutarli oldugu surece sorun yok. */
    time_t now;
    time(&now);
    return (int64_t)now;
}

/**
 * @brief Suresi dolmus session'lari temizler (mutex ALINMIS olmali)
 */
static void cleanup_expired_sessions(void)
{
    int64_t now = get_current_time();
    for (int i = 0; i < AUTH_MAX_SESSIONS; i++) {
        if (s_sessions[i].active && now > s_sessions[i].expiry) {
            ESP_LOGW(TAG, "Session suresi doldu, slot %d temizlendi", i);
            s_sessions[i].active = false;
            memset(s_sessions[i].token, 0, AUTH_TOKEN_LEN);
        }
    }
}

/**
 * @brief Bos session slot'u bulur. Yoksa en eski oturumu atar.
 *        (mutex ALINMIS olmali)
 * @return slot index
 */
static int find_free_session_slot(void)
{
    /* Oncelikle bos slot ara */
    for (int i = 0; i < AUTH_MAX_SESSIONS; i++) {
        if (!s_sessions[i].active) {
            return i;
        }
    }

    /* Bos slot yok — en eski expiry'ye sahip olani bul ve at */
    int oldest_idx = 0;
    int64_t oldest_expiry = s_sessions[0].expiry;
    for (int i = 1; i < AUTH_MAX_SESSIONS; i++) {
        if (s_sessions[i].expiry < oldest_expiry) {
            oldest_expiry = s_sessions[i].expiry;
            oldest_idx = i;
        }
    }

    ESP_LOGW(TAG, "Session slot dolu, en eski oturum (slot %d) silindi", oldest_idx);
    s_sessions[oldest_idx].active = false;
    memset(s_sessions[oldest_idx].token, 0, AUTH_TOKEN_LEN);

    return oldest_idx;
}

/* ============================================================
 * Baslatma
 * ============================================================ */

esp_err_t auth_manager_init(void)
{
    /* Mutex olustur */
    s_auth_mutex = xSemaphoreCreateMutex();
    if (s_auth_mutex == NULL) {
        ESP_LOGE(TAG, "Mutex olusturulamadi");
        return ESP_ERR_NO_MEM;
    }

    /* Session listesini temizle */
    memset(s_sessions, 0, sizeof(s_sessions));

    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NS_AUTH, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NVS '%s' namespace acilamadi: %s", NVS_NS_AUTH, esp_err_to_name(ret));
        return ret;
    }

    /* Hash kayitli mi kontrol et */
    size_t required_size = 0;
    ret = nvs_get_str(handle, KEY_PASS_HASH, NULL, &required_size);

    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        /* Fabrika varsayilanini yaz */
        ESP_LOGW(TAG, "Auth bilgisi bulunamadi, fabrika varsayilani yaziliyor");

        /* Username yaz */
        ret = nvs_set_str(handle, KEY_USERNAME, DEFAULT_USERNAME);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Username yazilamadi: %s", esp_err_to_name(ret));
            nvs_close(handle);
            return ret;
        }

        /* Varsayilan sifrenin hash'ini hesapla ve yaz */
        char hash_hex[SHA256_HEX_LEN];
        ret = compute_sha256(DEFAULT_PASSWORD, hash_hex);
        if (ret != ESP_OK) {
            nvs_close(handle);
            return ret;
        }

        ret = nvs_set_str(handle, KEY_PASS_HASH, hash_hex);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Password hash yazilamadi: %s", esp_err_to_name(ret));
            nvs_close(handle);
            return ret;
        }

        ret = nvs_commit(handle);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "NVS commit hatasi: %s", esp_err_to_name(ret));
            nvs_close(handle);
            return ret;
        }

        ESP_LOGI(TAG, "Fabrika varsayilani yazildi: %s / ****", DEFAULT_USERNAME);
    } else if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Mevcut auth bilgisi bulundu");
    } else {
        ESP_LOGE(TAG, "NVS okuma hatasi: %s", esp_err_to_name(ret));
        nvs_close(handle);
        return ret;
    }

    /* Eski tek-token NVS key'lerini temizle (varsa) — artik RAM'de tutuyoruz */
    nvs_erase_key(handle, "token");
    nvs_erase_key(handle, "expiry");
    nvs_commit(handle);

    nvs_close(handle);
    ESP_LOGI(TAG, "Auth manager baslatildi (maks %d esanli oturum)", AUTH_MAX_SESSIONS);
    return ESP_OK;
}

/* ============================================================
 * Giris dogrulama
 * ============================================================ */

esp_err_t auth_manager_verify(const char *username, const char *password, char *token_out)
{
    if (username == NULL || password == NULL || token_out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (strlen(username) == 0 || strlen(password) == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!auth_lock()) {
        return ESP_ERR_TIMEOUT;
    }

    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NS_AUTH, NVS_READONLY, &handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NVS acilamadi: %s", esp_err_to_name(ret));
        auth_unlock();
        return ret;
    }

    /* Kullanici adini kontrol et */
    char stored_username[AUTH_USERNAME_MAX_LEN];
    size_t uname_len = sizeof(stored_username);
    ret = nvs_get_str(handle, KEY_USERNAME, stored_username, &uname_len);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Username okunamadi: %s", esp_err_to_name(ret));
        nvs_close(handle);
        auth_unlock();
        return ESP_FAIL;
    }

    if (strcmp(username, stored_username) != 0) {
        ESP_LOGW(TAG, "Yanlis kullanici adi: %s", username);
        nvs_close(handle);
        auth_unlock();
        return ESP_FAIL;
    }

    /* Sifre hash'ini kontrol et */
    char stored_hash[SHA256_HEX_LEN];
    size_t hash_len = sizeof(stored_hash);
    ret = nvs_get_str(handle, KEY_PASS_HASH, stored_hash, &hash_len);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Password hash okunamadi: %s", esp_err_to_name(ret));
        nvs_close(handle);
        auth_unlock();
        return ESP_FAIL;
    }

    nvs_close(handle);

    char input_hash[SHA256_HEX_LEN];
    ret = compute_sha256(password, input_hash);
    if (ret != ESP_OK) {
        auth_unlock();
        return ESP_FAIL;
    }

    if (strcmp(input_hash, stored_hash) != 0) {
        ESP_LOGW(TAG, "Yanlis sifre girildi (kullanici: %s)", username);
        auth_unlock();
        return ESP_FAIL;
    }

    /* Giris basarili -- suresi dolmus session'lari temizle */
    cleanup_expired_sessions();

    /* Yeni token uret */
    generate_token(token_out);

    /* Bos slot bul ve tokeni kaydet */
    int slot = find_free_session_slot();
    strncpy(s_sessions[slot].token, token_out, AUTH_TOKEN_LEN - 1);
    s_sessions[slot].token[AUTH_TOKEN_LEN - 1] = '\0';
    s_sessions[slot].expiry = get_current_time() + TOKEN_LIFETIME_SEC;
    s_sessions[slot].active = true;

    /* Aktif oturum sayisini hesapla */
    int active_count = 0;
    for (int i = 0; i < AUTH_MAX_SESSIONS; i++) {
        if (s_sessions[i].active) {
            active_count++;
        }
    }

    ESP_LOGI(TAG, "Giris basarili: %s (slot %d, aktif oturum: %d)", username, slot, active_count);

    auth_unlock();
    return ESP_OK;
}

/* ============================================================
 * Token dogrulama
 * ============================================================ */

bool auth_manager_validate_token(const char *token)
{
    if (token == NULL || strlen(token) == 0) {
        return false;
    }

    if (!auth_lock()) {
        return false;
    }

    int64_t now = get_current_time();

    for (int i = 0; i < AUTH_MAX_SESSIONS; i++) {
        if (!s_sessions[i].active) {
            continue;
        }

        /* Suresi dolmus mu kontrol et */
        if (now > s_sessions[i].expiry) {
            ESP_LOGW(TAG, "Session suresi doldu (slot %d), temizleniyor", i);
            s_sessions[i].active = false;
            memset(s_sessions[i].token, 0, AUTH_TOKEN_LEN);
            continue;
        }

        /* Token eslesiyor mu */
        if (strcmp(token, s_sessions[i].token) == 0) {
            auth_unlock();
            return true;
        }
    }

    auth_unlock();
    return false;
}

/* ============================================================
 * Cikis — belirli token'i sil
 * ============================================================ */

esp_err_t auth_manager_logout(const char *token)
{
    if (token == NULL || strlen(token) == 0) {
        /* Token yoksa islem yapma, hata donme */
        ESP_LOGW(TAG, "Logout: token bos, islem yapilmadi");
        return ESP_OK;
    }

    if (!auth_lock()) {
        return ESP_ERR_TIMEOUT;
    }

    bool found = false;
    for (int i = 0; i < AUTH_MAX_SESSIONS; i++) {
        if (s_sessions[i].active && strcmp(token, s_sessions[i].token) == 0) {
            s_sessions[i].active = false;
            memset(s_sessions[i].token, 0, AUTH_TOKEN_LEN);
            found = true;
            ESP_LOGI(TAG, "Oturum kapatildi (slot %d)", i);
            break;
        }
    }

    if (!found) {
        ESP_LOGW(TAG, "Logout: token bulunamadi (zaten gecersiz olabilir)");
    }

    auth_unlock();
    return ESP_OK;
}

/* ============================================================
 * Tum oturumlari kapat
 * ============================================================ */

esp_err_t auth_manager_logout_all(void)
{
    if (!auth_lock()) {
        return ESP_ERR_TIMEOUT;
    }

    int count = 0;
    for (int i = 0; i < AUTH_MAX_SESSIONS; i++) {
        if (s_sessions[i].active) {
            s_sessions[i].active = false;
            memset(s_sessions[i].token, 0, AUTH_TOKEN_LEN);
            count++;
        }
    }

    ESP_LOGI(TAG, "Tum oturumlar kapatildi (%d adet)", count);

    auth_unlock();
    return ESP_OK;
}

/* ============================================================
 * Sifre degistirme
 * ============================================================ */

esp_err_t auth_manager_change_password(const char *old_pass, const char *new_pass)
{
    if (old_pass == NULL || new_pass == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (strlen(new_pass) < 4) {
        ESP_LOGE(TAG, "Yeni sifre en az 4 karakter olmali");
        return ESP_ERR_INVALID_ARG;
    }

    if (!auth_lock()) {
        return ESP_ERR_TIMEOUT;
    }

    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NS_AUTH, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NVS acilamadi: %s", esp_err_to_name(ret));
        auth_unlock();
        return ret;
    }

    /* Eski sifre dogrulama */
    char stored_hash[SHA256_HEX_LEN];
    size_t hash_len = sizeof(stored_hash);
    ret = nvs_get_str(handle, KEY_PASS_HASH, stored_hash, &hash_len);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Mevcut hash okunamadi: %s", esp_err_to_name(ret));
        nvs_close(handle);
        auth_unlock();
        return ESP_FAIL;
    }

    char old_hash[SHA256_HEX_LEN];
    ret = compute_sha256(old_pass, old_hash);
    if (ret != ESP_OK) {
        nvs_close(handle);
        auth_unlock();
        return ESP_FAIL;
    }

    if (strcmp(old_hash, stored_hash) != 0) {
        ESP_LOGW(TAG, "Sifre degistirme basarisiz: eski sifre yanlis");
        nvs_close(handle);
        auth_unlock();
        return ESP_FAIL;
    }

    /* Yeni sifre hash'ini hesapla ve yaz */
    char new_hash[SHA256_HEX_LEN];
    ret = compute_sha256(new_pass, new_hash);
    if (ret != ESP_OK) {
        nvs_close(handle);
        auth_unlock();
        return ESP_FAIL;
    }

    ret = nvs_set_str(handle, KEY_PASS_HASH, new_hash);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Yeni hash yazilamadi: %s", esp_err_to_name(ret));
        nvs_close(handle);
        auth_unlock();
        return ret;
    }

    ret = nvs_commit(handle);
    nvs_close(handle);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NVS commit hatasi: %s", esp_err_to_name(ret));
        auth_unlock();
        return ret;
    }

    /* Tum aktif oturumlari gecersiz kil */
    int count = 0;
    for (int i = 0; i < AUTH_MAX_SESSIONS; i++) {
        if (s_sessions[i].active) {
            s_sessions[i].active = false;
            memset(s_sessions[i].token, 0, AUTH_TOKEN_LEN);
            count++;
        }
    }

    ESP_LOGI(TAG, "Sifre basariyla degistirildi, %d oturum gecersiz kilinid", count);

    auth_unlock();
    return ret;
}
