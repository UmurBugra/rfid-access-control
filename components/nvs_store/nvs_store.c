#include "nvs_store.h"

#include <string.h>
#include <stdio.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "nvs_store";

/* NVS namespace isimleri */
#define NVS_NS_CARDS   "cards"
#define NVS_NS_CONFIG  "config"

/* Config key isimleri */
#define KEY_DOOR_DELAY  "door_delay"
#define KEY_AP_PASS     "ap_pass"
#define KEY_WIFI_SSID   "wifi_ssid"
#define KEY_WIFI_PASS   "wifi_pass"

/* Varsayilan degerler */
#define DEFAULT_DOOR_DELAY  3
#define DEFAULT_AP_PASS     "kapi1234"

/* NVS erisimi icin mutex */
static SemaphoreHandle_t s_nvs_mutex = NULL;

/* ============================================================
 * Yardimci fonksiyonlar
 * ============================================================ */

/**
 * @brief Mutex al (NVS erisimi oncesi)
 */
static bool nvs_lock(void)
{
    if (s_nvs_mutex == NULL) {
        ESP_LOGE(TAG, "Mutex baslatilmamis");
        return false;
    }
    if (xSemaphoreTake(s_nvs_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE(TAG, "Mutex alinamadi (timeout)");
        return false;
    }
    return true;
}

/**
 * @brief Mutex birak (NVS erisimi sonrasi)
 */
static void nvs_unlock(void)
{
    xSemaphoreGive(s_nvs_mutex);
}

/* ============================================================
 * Baslatma
 * ============================================================ */

esp_err_t nvs_store_init(void)
{
    /* Mutex olustur */
    s_nvs_mutex = xSemaphoreCreateMutex();
    if (s_nvs_mutex == NULL) {
        ESP_LOGE(TAG, "Mutex olusturulamadi");
        return ESP_ERR_NO_MEM;
    }

    /* NVS namespace'lerin erisilebilir oldugunu dogrula */
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NS_CARDS, NVS_READWRITE, &handle);
    if (ret == ESP_OK) {
        nvs_close(handle);
    } else {
        ESP_LOGE(TAG, "NVS '%s' namespace acilamadi: %s", NVS_NS_CARDS, esp_err_to_name(ret));
        return ret;
    }

    ret = nvs_open(NVS_NS_CONFIG, NVS_READWRITE, &handle);
    if (ret == ESP_OK) {
        nvs_close(handle);
    } else {
        ESP_LOGE(TAG, "NVS '%s' namespace acilamadi: %s", NVS_NS_CONFIG, esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "NVS store baslatildi");
    return ESP_OK;
}

/* ============================================================
 * Kart islemleri
 * ============================================================ */

esp_err_t nvs_store_card_add(const char *uid_hex, const char *name)
{
    if (uid_hex == NULL || name == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (strlen(uid_hex) == 0 || strlen(uid_hex) >= NVS_STORE_UID_MAX_LEN) {
        ESP_LOGE(TAG, "Gecersiz UID uzunlugu: %d", (int)strlen(uid_hex));
        return ESP_ERR_INVALID_ARG;
    }

    if (strlen(name) == 0 || strlen(name) >= NVS_STORE_NAME_MAX_LEN) {
        ESP_LOGE(TAG, "Gecersiz isim uzunlugu: %d", (int)strlen(name));
        return ESP_ERR_INVALID_ARG;
    }

    if (!nvs_lock()) {
        return ESP_ERR_TIMEOUT;
    }

    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NS_CARDS, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NVS acilamadi: %s", esp_err_to_name(ret));
        nvs_unlock();
        return ret;
    }

    /* UID'yi key, ismi value olarak kaydet */
    ret = nvs_set_str(handle, uid_hex, name);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Kart kaydedilemedi [%s]: %s", uid_hex, esp_err_to_name(ret));
        nvs_close(handle);
        nvs_unlock();
        return ret;
    }

    ret = nvs_commit(handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NVS commit hatasi: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Kart eklendi: %s -> %s", uid_hex, name);
    }

    nvs_close(handle);
    nvs_unlock();
    return ret;
}

esp_err_t nvs_store_card_remove(const char *uid_hex)
{
    if (uid_hex == NULL || strlen(uid_hex) == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!nvs_lock()) {
        return ESP_ERR_TIMEOUT;
    }

    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NS_CARDS, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NVS acilamadi: %s", esp_err_to_name(ret));
        nvs_unlock();
        return ret;
    }

    ret = nvs_erase_key(handle, uid_hex);
    if (ret != ESP_OK) {
        if (ret == ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGW(TAG, "Kart bulunamadi: %s", uid_hex);
        } else {
            ESP_LOGE(TAG, "Kart silinemedi [%s]: %s", uid_hex, esp_err_to_name(ret));
        }
        nvs_close(handle);
        nvs_unlock();
        return ret;
    }

    ret = nvs_commit(handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NVS commit hatasi: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Kart silindi: %s", uid_hex);
    }

    nvs_close(handle);
    nvs_unlock();
    return ret;
}

bool nvs_store_card_exists(const char *uid_hex)
{
    if (uid_hex == NULL || strlen(uid_hex) == 0) {
        return false;
    }

    if (!nvs_lock()) {
        return false;
    }

    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NS_CARDS, NVS_READONLY, &handle);
    if (ret != ESP_OK) {
        nvs_unlock();
        return false;
    }

    /* Sadece varligi kontrol et, degeri okuma */
    size_t required_size = 0;
    ret = nvs_get_str(handle, uid_hex, NULL, &required_size);

    nvs_close(handle);
    nvs_unlock();

    return (ret == ESP_OK);
}

esp_err_t nvs_store_card_get_name(const char *uid_hex, char *name_buf, size_t len)
{
    if (uid_hex == NULL || name_buf == NULL || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!nvs_lock()) {
        return ESP_ERR_TIMEOUT;
    }

    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NS_CARDS, NVS_READONLY, &handle);
    if (ret != ESP_OK) {
        nvs_unlock();
        return ret;
    }

    size_t required_size = len;
    ret = nvs_get_str(handle, uid_hex, name_buf, &required_size);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "Kart bulunamadi: %s", uid_hex);
    } else if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Kart ismi okunamadi [%s]: %s", uid_hex, esp_err_to_name(ret));
    }

    nvs_close(handle);
    nvs_unlock();
    return ret;
}

esp_err_t nvs_store_card_list(card_entry_t *out, size_t *count)
{
    if (out == NULL || count == NULL || *count == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!nvs_lock()) {
        return ESP_ERR_TIMEOUT;
    }

    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NS_CARDS, NVS_READONLY, &handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NVS acilamadi: %s", esp_err_to_name(ret));
        nvs_unlock();
        *count = 0;
        return ret;
    }

    /* NVS iterator ile tum key'leri tara */
    nvs_iterator_t it = NULL;
    ret = nvs_entry_find(NVS_NS_CARDS, NVS_NS_CARDS, NVS_TYPE_STR, &it);

    size_t max_count = *count;
    size_t found = 0;

    while (ret == ESP_OK && found < max_count) {
        nvs_entry_info_t info;
        nvs_entry_info(it, &info);

        /* UID'yi kopyala */
        strncpy(out[found].uid, info.key, NVS_STORE_UID_MAX_LEN - 1);
        out[found].uid[NVS_STORE_UID_MAX_LEN - 1] = '\0';

        /* Ismi oku */
        size_t name_len = NVS_STORE_NAME_MAX_LEN;
        esp_err_t get_ret = nvs_get_str(handle, info.key, out[found].name, &name_len);
        if (get_ret != ESP_OK) {
            strncpy(out[found].name, "?", NVS_STORE_NAME_MAX_LEN);
        }

        found++;
        ret = nvs_entry_next(&it);
    }

    /* Iterator temizle */
    if (it != NULL) {
        nvs_release_iterator(it);
    }

    *count = found;

    nvs_close(handle);
    nvs_unlock();

    ESP_LOGI(TAG, "Kart listesi: %d kart bulundu", (int)found);
    return ESP_OK;
}

/* ============================================================
 * Config islemleri
 * ============================================================ */

esp_err_t nvs_store_config_get_door_delay(uint16_t *value)
{
    if (value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!nvs_lock()) {
        return ESP_ERR_TIMEOUT;
    }

    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NS_CONFIG, NVS_READONLY, &handle);
    if (ret != ESP_OK) {
        nvs_unlock();
        *value = DEFAULT_DOOR_DELAY;
        return ret;
    }

    ret = nvs_get_u16(handle, KEY_DOOR_DELAY, value);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        *value = DEFAULT_DOOR_DELAY;
        ESP_LOGI(TAG, "door_delay bulunamadi, varsayilan: %d", DEFAULT_DOOR_DELAY);
    } else if (ret != ESP_OK) {
        ESP_LOGE(TAG, "door_delay okunamadi: %s", esp_err_to_name(ret));
        *value = DEFAULT_DOOR_DELAY;
    }

    nvs_close(handle);
    nvs_unlock();
    return ret;
}

esp_err_t nvs_store_config_set_door_delay(uint16_t value)
{
    if (value < 1 || value > 30) {
        ESP_LOGE(TAG, "Gecersiz door_delay: %d (1-30 arasi olmali)", value);
        return ESP_ERR_INVALID_ARG;
    }

    if (!nvs_lock()) {
        return ESP_ERR_TIMEOUT;
    }

    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NS_CONFIG, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NVS acilamadi: %s", esp_err_to_name(ret));
        nvs_unlock();
        return ret;
    }

    ret = nvs_set_u16(handle, KEY_DOOR_DELAY, value);
    if (ret == ESP_OK) {
        ret = nvs_commit(handle);
    }

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "door_delay kaydedilemedi: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "door_delay guncellendi: %d", value);
    }

    nvs_close(handle);
    nvs_unlock();
    return ret;
}

/**
 * @brief Genel string config okuma yardimcisi
 */
static esp_err_t config_get_str(const char *key, char *buf, size_t len, const char *default_val)
{
    if (buf == NULL || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!nvs_lock()) {
        return ESP_ERR_TIMEOUT;
    }

    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NS_CONFIG, NVS_READONLY, &handle);
    if (ret != ESP_OK) {
        nvs_unlock();
        if (default_val != NULL) {
            strncpy(buf, default_val, len - 1);
            buf[len - 1] = '\0';
        }
        return ret;
    }

    size_t required_size = len;
    ret = nvs_get_str(handle, key, buf, &required_size);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        if (default_val != NULL) {
            strncpy(buf, default_val, len - 1);
            buf[len - 1] = '\0';
            ESP_LOGI(TAG, "%s bulunamadi, varsayilan kullaniliyor", key);
        }
    } else if (ret != ESP_OK) {
        ESP_LOGE(TAG, "%s okunamadi: %s", key, esp_err_to_name(ret));
        if (default_val != NULL) {
            strncpy(buf, default_val, len - 1);
            buf[len - 1] = '\0';
        }
    }

    nvs_close(handle);
    nvs_unlock();
    return ret;
}

/**
 * @brief Genel string config yazma yardimcisi
 */
static esp_err_t config_set_str(const char *key, const char *value)
{
    if (key == NULL || value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!nvs_lock()) {
        return ESP_ERR_TIMEOUT;
    }

    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NS_CONFIG, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NVS acilamadi: %s", esp_err_to_name(ret));
        nvs_unlock();
        return ret;
    }

    ret = nvs_set_str(handle, key, value);
    if (ret == ESP_OK) {
        ret = nvs_commit(handle);
    }

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "%s kaydedilemedi: %s", key, esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "%s guncellendi", key);
    }

    nvs_close(handle);
    nvs_unlock();
    return ret;
}

/* --- AP Pass --- */

esp_err_t nvs_store_config_get_ap_pass(char *buf, size_t len)
{
    return config_get_str(KEY_AP_PASS, buf, len, DEFAULT_AP_PASS);
}

esp_err_t nvs_store_config_set_ap_pass(const char *pass)
{
    if (pass == NULL || strlen(pass) < 8) {
        ESP_LOGE(TAG, "AP sifre en az 8 karakter olmali");
        return ESP_ERR_INVALID_ARG;
    }
    return config_set_str(KEY_AP_PASS, pass);
}

/* --- Wi-Fi SSID --- */

esp_err_t nvs_store_config_get_wifi_ssid(char *buf, size_t len)
{
    return config_get_str(KEY_WIFI_SSID, buf, len, NULL);
}

esp_err_t nvs_store_config_set_wifi_ssid(const char *ssid)
{
    return config_set_str(KEY_WIFI_SSID, ssid);
}

/* --- Wi-Fi Pass --- */

esp_err_t nvs_store_config_get_wifi_pass(char *buf, size_t len)
{
    return config_get_str(KEY_WIFI_PASS, buf, len, NULL);
}

esp_err_t nvs_store_config_set_wifi_pass(const char *pass)
{
    return config_set_str(KEY_WIFI_PASS, pass);
}
