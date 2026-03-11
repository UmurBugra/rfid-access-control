#include "web_server.h"

#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>
#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "cJSON.h"
#include "auth_manager.h"
#include "nvs_store.h"
#include "door_controller.h"
#include "wifi_manager.h"
#include "ota_manager.h"

static const char *TAG = "web_srv";

/* ============================================================
 * Embedded HTML dosyalari (EMBED_TXTFILES ile)
 * ============================================================ */
extern const char login_html_start[]     asm("_binary_login_html_start");
extern const char dashboard_html_start[] asm("_binary_dashboard_html_start");

/* ============================================================
 * Sabitler
 * ============================================================ */
#define MAX_BODY_SIZE       512     /* POST body maks boyut */
#define LOG_READ_BUF_SIZE   8192   /* Log okuma buffer boyutu */
#define MAX_URI_HANDLERS    18

/* ============================================================
 * Modul degiskenleri
 * ============================================================ */
static httpd_handle_t s_server = NULL;

/* ============================================================
 * Yardimci fonksiyonlar
 * ============================================================ */

/**
 * @brief Cookie header'dan session token'i cikarir
 */
static esp_err_t get_session_token(httpd_req_t *req, char *token_buf, size_t buf_len)
{
    char cookie_buf[128];
    if (httpd_req_get_hdr_value_str(req, "Cookie", cookie_buf, sizeof(cookie_buf)) != ESP_OK) {
        return ESP_ERR_NOT_FOUND;
    }

    char *start = strstr(cookie_buf, "session=");
    if (start == NULL) {
        return ESP_ERR_NOT_FOUND;
    }
    start += 8; /* "session=" uzunlugu */

    char *end = strchr(start, ';');
    size_t len = end ? (size_t)(end - start) : strlen(start);

    if (len == 0 || len >= buf_len) {
        return ESP_ERR_INVALID_SIZE;
    }

    memcpy(token_buf, start, len);
    token_buf[len] = '\0';
    return ESP_OK;
}

/**
 * @brief Session cookie dogrulamasi
 * @return true gecerli oturum, false gecersiz
 */
static bool validate_session(httpd_req_t *req)
{
    char token[AUTH_TOKEN_LEN];
    if (get_session_token(req, token, sizeof(token)) != ESP_OK) {
        return false;
    }
    return auth_manager_validate_token(token);
}

/**
 * @brief Korunmayan endpoint'lerde 401 yaniti gonderir
 */
static esp_err_t send_unauthorized(httpd_req_t *req)
{
    httpd_resp_set_status(req, "401 Unauthorized");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"error\":\"Oturum gecersiz\"}");
    return ESP_OK;
}

/**
 * @brief POST body'sini okur
 */
static esp_err_t read_body(httpd_req_t *req, char *buf, size_t buf_size)
{
    int content_len = req->content_len;
    if (content_len <= 0 || content_len >= (int)buf_size) {
        return ESP_ERR_INVALID_SIZE;
    }

    int total_recv = 0;
    while (total_recv < content_len) {
        int ret = httpd_req_recv(req, buf + total_recv, content_len - total_recv);
        if (ret <= 0) {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;
            }
            return ESP_FAIL;
        }
        total_recv += ret;
    }
    buf[total_recv] = '\0';
    return ESP_OK;
}

/**
 * @brief URL query string'den parametre degerini cikarir
 */
static esp_err_t get_query_param(httpd_req_t *req, const char *key, char *val, size_t val_len)
{
    size_t query_len = httpd_req_get_url_query_len(req);
    if (query_len == 0) {
        return ESP_ERR_NOT_FOUND;
    }

    char *query = malloc(query_len + 1);
    if (query == NULL) {
        return ESP_ERR_NO_MEM;
    }

    if (httpd_req_get_url_query_str(req, query, query_len + 1) != ESP_OK) {
        free(query);
        return ESP_ERR_NOT_FOUND;
    }

    esp_err_t ret = httpd_query_key_value(query, key, val, val_len);
    free(query);
    return ret;
}

/* ============================================================
 * GET /login — Login sayfasi
 * ============================================================ */
static esp_err_t login_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=UTF-8");
    httpd_resp_sendstr(req, login_html_start);
    return ESP_OK;
}

/* ============================================================
 * POST /login — Giris dogrulama
 * ============================================================ */
static esp_err_t login_post_handler(httpd_req_t *req)
{
    char body[MAX_BODY_SIZE];
    if (read_body(req, body, sizeof(body)) != ESP_OK) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"error\":\"Gecersiz istek\"}");
        return ESP_OK;
    }

    cJSON *json = cJSON_Parse(body);
    if (json == NULL) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"error\":\"Gecersiz JSON\"}");
        return ESP_OK;
    }

    cJSON *j_user = cJSON_GetObjectItem(json, "username");
    cJSON *j_pass = cJSON_GetObjectItem(json, "password");

    if (!cJSON_IsString(j_user) || !cJSON_IsString(j_pass)) {
        cJSON_Delete(json);
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"error\":\"username ve password gerekli\"}");
        return ESP_OK;
    }

    char token[AUTH_TOKEN_LEN];
    esp_err_t ret = auth_manager_verify(j_user->valuestring, j_pass->valuestring, token);
    cJSON_Delete(json);

    if (ret != ESP_OK) {
        httpd_resp_set_status(req, "401 Unauthorized");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"error\":\"Kullanici adi veya sifre hatali\"}");
        return ESP_OK;
    }

    /* Session cookie ayarla — HttpOnly; Path=/ */
    char cookie_hdr[128];
    snprintf(cookie_hdr, sizeof(cookie_hdr), "session=%s; HttpOnly; Path=/", token);
    httpd_resp_set_hdr(req, "Set-Cookie", cookie_hdr);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true}");

    ESP_LOGI(TAG, "Basarili giris");
    return ESP_OK;
}

/* ============================================================
 * POST /logout — Cikis
 * ============================================================ */
static esp_err_t logout_post_handler(httpd_req_t *req)
{
    auth_manager_logout();

    httpd_resp_set_hdr(req, "Set-Cookie", "session=; HttpOnly; Path=/; Max-Age=0");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true}");

    ESP_LOGI(TAG, "Oturum kapatildi");
    return ESP_OK;
}

/* ============================================================
 * GET / — Dashboard sayfasi (korumali)
 * ============================================================ */
static esp_err_t dashboard_get_handler(httpd_req_t *req)
{
    if (!validate_session(req)) {
        httpd_resp_set_status(req, "302 Found");
        httpd_resp_set_hdr(req, "Location", "/login");
        httpd_resp_sendstr(req, "");
        return ESP_OK;
    }

    httpd_resp_set_type(req, "text/html; charset=UTF-8");
    httpd_resp_sendstr(req, dashboard_html_start);
    return ESP_OK;
}

/* ============================================================
 * GET /api/cards — Kart listesi (korumali)
 * ============================================================ */
static esp_err_t cards_get_handler(httpd_req_t *req)
{
    if (!validate_session(req)) {
        return send_unauthorized(req);
    }

    card_entry_t *cards = malloc(sizeof(card_entry_t) * NVS_STORE_MAX_CARDS);
    if (cards == NULL) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_sendstr(req, "{\"error\":\"Bellek yetersiz\"}");
        return ESP_OK;
    }

    size_t count = NVS_STORE_MAX_CARDS;
    nvs_store_card_list(cards, &count);

    cJSON *root = cJSON_CreateObject();
    cJSON *arr = cJSON_AddArrayToObject(root, "cards");

    for (size_t i = 0; i < count; i++) {
        cJSON *item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "uid", cards[i].uid);
        cJSON_AddStringToObject(item, "name", cards[i].name);
        cJSON_AddItemToArray(arr, item);
    }

    free(cards);

    char *resp = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, resp ? resp : "{\"cards\":[]}");
    free(resp);

    return ESP_OK;
}

/* ============================================================
 * POST /api/cards — Kart ekle (korumali)
 * ============================================================ */
static esp_err_t cards_post_handler(httpd_req_t *req)
{
    if (!validate_session(req)) {
        return send_unauthorized(req);
    }

    char body[MAX_BODY_SIZE];
    if (read_body(req, body, sizeof(body)) != ESP_OK) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"error\":\"Gecersiz istek\"}");
        return ESP_OK;
    }

    cJSON *json = cJSON_Parse(body);
    if (json == NULL) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"error\":\"Gecersiz JSON\"}");
        return ESP_OK;
    }

    cJSON *j_uid  = cJSON_GetObjectItem(json, "uid");
    cJSON *j_name = cJSON_GetObjectItem(json, "name");

    if (!cJSON_IsString(j_uid) || !cJSON_IsString(j_name) ||
        strlen(j_uid->valuestring) == 0 || strlen(j_name->valuestring) == 0) {
        cJSON_Delete(json);
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"error\":\"uid ve name gerekli\"}");
        return ESP_OK;
    }

    /* UID uzunluk kontrolu */
    if (strlen(j_uid->valuestring) >= NVS_STORE_UID_MAX_LEN) {
        cJSON_Delete(json);
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"error\":\"UID cok uzun (maks 8 karakter)\"}");
        return ESP_OK;
    }

    esp_err_t ret = nvs_store_card_add(j_uid->valuestring, j_name->valuestring);
    ESP_LOGI(TAG, "Kart ekleme istegi: %s", j_uid->valuestring);
    cJSON_Delete(json);

    if (ret != ESP_OK) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"error\":\"Kart eklenemedi\"}");
        return ESP_OK;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true}");
    return ESP_OK;
}

/* ============================================================
 * DELETE /api/cards?uid=XX — Kart sil (korumali)
 * ============================================================ */
static esp_err_t cards_delete_handler(httpd_req_t *req)
{
    if (!validate_session(req)) {
        return send_unauthorized(req);
    }

    char uid[NVS_STORE_UID_MAX_LEN];
    if (get_query_param(req, "uid", uid, sizeof(uid)) != ESP_OK) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"error\":\"uid parametresi gerekli\"}");
        return ESP_OK;
    }

    esp_err_t ret = nvs_store_card_remove(uid);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        httpd_resp_set_status(req, "404 Not Found");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"error\":\"Kart bulunamadi\"}");
        return ESP_OK;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true}");
    ESP_LOGI(TAG, "Kart silindi: %s", uid);
    return ESP_OK;
}

/* ============================================================
 * GET /api/logs?date=YYYY-MM-DD — Log goruntuleme (korumali)
 * ============================================================ */
static esp_err_t logs_get_handler(httpd_req_t *req)
{
    if (!validate_session(req)) {
        return send_unauthorized(req);
    }

    char date[DOOR_LOG_DATE_LEN];
    if (get_query_param(req, "date", date, sizeof(date)) != ESP_OK) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"error\":\"date parametresi gerekli\"}");
        return ESP_OK;
    }

    char *buf = malloc(LOG_READ_BUF_SIZE);
    if (buf == NULL) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_sendstr(req, "{\"error\":\"Bellek yetersiz\"}");
        return ESP_OK;
    }

    size_t out_len = 0;
    esp_err_t ret = door_controller_read_log(date, buf, LOG_READ_BUF_SIZE, &out_len);

    if (ret == ESP_ERR_NOT_FOUND) {
        free(buf);
        httpd_resp_set_status(req, "404 Not Found");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"error\":\"Log bulunamadi\"}");
        return ESP_OK;
    }

    httpd_resp_set_type(req, "text/plain; charset=UTF-8");
    httpd_resp_send(req, buf, (ssize_t)out_len);
    free(buf);
    return ESP_OK;
}

/* ============================================================
 * GET /api/logs/dates — Mevcut log tarihleri (korumali)
 * ============================================================ */
static esp_err_t logs_dates_handler(httpd_req_t *req)
{
    if (!validate_session(req)) {
        return send_unauthorized(req);
    }

    char dates[DOOR_MAX_LOG_DATES][DOOR_LOG_DATE_LEN];
    size_t count = 0;
    door_controller_get_log_dates(dates, DOOR_MAX_LOG_DATES, &count);

    cJSON *root = cJSON_CreateObject();
    cJSON *arr = cJSON_AddArrayToObject(root, "dates");
    for (size_t i = 0; i < count; i++) {
        cJSON_AddItemToArray(arr, cJSON_CreateString(dates[i]));
    }

    char *resp = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, resp ? resp : "{\"dates\":[]}");
    free(resp);
    return ESP_OK;
}

/* ============================================================
 * DELETE /api/logs?date=YYYY-MM-DD — Log sil (korumali)
 * ============================================================ */
static esp_err_t logs_delete_handler(httpd_req_t *req)
{
    if (!validate_session(req)) {
        return send_unauthorized(req);
    }

    char date[DOOR_LOG_DATE_LEN];
    if (get_query_param(req, "date", date, sizeof(date)) != ESP_OK) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"error\":\"date parametresi gerekli\"}");
        return ESP_OK;
    }

    esp_err_t ret = door_controller_delete_log(date);
    if (ret == ESP_ERR_NOT_FOUND) {
        httpd_resp_set_status(req, "404 Not Found");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"error\":\"Log bulunamadi\"}");
        return ESP_OK;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true}");
    ESP_LOGI(TAG, "Log silindi: %s", date);
    return ESP_OK;
}

/* ============================================================
 * GET /api/status — Sistem durumu (korumali)
 * ============================================================ */
static esp_err_t status_get_handler(httpd_req_t *req)
{
    if (!validate_session(req)) {
        return send_unauthorized(req);
    }

    cJSON *root = cJSON_CreateObject();

    /* Uptime */
    int64_t uptime_us = esp_timer_get_time();
    cJSON_AddNumberToObject(root, "uptime_sec", (double)(uptime_us / 1000000));

    /* Free heap */
    cJSON_AddNumberToObject(root, "free_heap", (double)esp_get_free_heap_size());

    /* Wi-Fi STA durumu */
    cJSON_AddBoolToObject(root, "wifi_sta_connected", wifi_manager_is_sta_connected());
    char ip_buf[16];
    if (wifi_manager_get_sta_ip(ip_buf, sizeof(ip_buf)) == ESP_OK) {
        cJSON_AddStringToObject(root, "wifi_sta_ip", ip_buf);
    } else {
        cJSON_AddStringToObject(root, "wifi_sta_ip", "");
    }

    /* Wi-Fi AP durumu */
    if (wifi_manager_get_ap_ip(ip_buf, sizeof(ip_buf)) == ESP_OK) {
        cJSON_AddStringToObject(root, "wifi_ap_ip", ip_buf);
    } else {
        cJSON_AddStringToObject(root, "wifi_ap_ip", "192.168.4.1");
    }
    cJSON_AddNumberToObject(root, "wifi_ap_clients", (double)wifi_manager_get_ap_client_count());

    /* LittleFS durumu */
    size_t fs_total = 0, fs_used = 0;
    door_controller_get_fs_info(&fs_total, &fs_used);
    cJSON_AddNumberToObject(root, "fs_total_kb", (double)(fs_total / 1024));
    cJSON_AddNumberToObject(root, "fs_used_kb", (double)(fs_used / 1024));

    /* Kart sayisi */
    card_entry_t *cards = malloc(sizeof(card_entry_t) * NVS_STORE_MAX_CARDS);
    size_t card_count = 0;
    if (cards != NULL) {
        card_count = NVS_STORE_MAX_CARDS;
        nvs_store_card_list(cards, &card_count);
        free(cards);
    }
    cJSON_AddNumberToObject(root, "card_count", (double)card_count);

    /* OTA / Firmware bilgileri */
    ota_info_t ota_info;
    if (ota_manager_get_info(&ota_info) == ESP_OK) {
        cJSON *ota = cJSON_AddObjectToObject(root, "firmware");
        cJSON_AddStringToObject(ota, "version", ota_info.app_version ? ota_info.app_version : "?");
        cJSON_AddStringToObject(ota, "idf_version", ota_info.idf_version ? ota_info.idf_version : "?");
        cJSON_AddStringToObject(ota, "compile_date", ota_info.compile_date ? ota_info.compile_date : "?");
        cJSON_AddStringToObject(ota, "compile_time", ota_info.compile_time ? ota_info.compile_time : "?");
        cJSON_AddStringToObject(ota, "running_partition", ota_info.running_partition ? ota_info.running_partition : "?");
        cJSON_AddStringToObject(ota, "next_partition", ota_info.next_partition ? ota_info.next_partition : "?");
    }

    /* Config bilgileri */
    cJSON *cfg = cJSON_AddObjectToObject(root, "config");

    uint16_t door_delay = 3;
    nvs_store_config_get_door_delay(&door_delay);
    cJSON_AddNumberToObject(cfg, "door_delay", (double)door_delay);

    char str_buf[64];
    if (nvs_store_config_get_ap_pass(str_buf, sizeof(str_buf)) == ESP_OK) {
        cJSON_AddStringToObject(cfg, "ap_pass", str_buf);
    } else {
        cJSON_AddStringToObject(cfg, "ap_pass", "kapi1234");
    }

    if (nvs_store_config_get_wifi_ssid(str_buf, sizeof(str_buf)) == ESP_OK) {
        cJSON_AddStringToObject(cfg, "wifi_ssid", str_buf);
    } else {
        cJSON_AddStringToObject(cfg, "wifi_ssid", "");
    }

    if (nvs_store_config_get_wifi_pass(str_buf, sizeof(str_buf)) == ESP_OK) {
        cJSON_AddStringToObject(cfg, "wifi_pass", str_buf);
    } else {
        cJSON_AddStringToObject(cfg, "wifi_pass", "");
    }

    char *resp = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, resp ? resp : "{}");
    free(resp);
    return ESP_OK;
}

/* ============================================================
 * POST /api/config — Ayar guncelleme (korumali)
 * ============================================================ */
static esp_err_t config_post_handler(httpd_req_t *req)
{
    if (!validate_session(req)) {
        return send_unauthorized(req);
    }

    char body[MAX_BODY_SIZE];
    if (read_body(req, body, sizeof(body)) != ESP_OK) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"error\":\"Gecersiz istek\"}");
        return ESP_OK;
    }

    cJSON *json = cJSON_Parse(body);
    if (json == NULL) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"error\":\"Gecersiz JSON\"}");
        return ESP_OK;
    }

    bool wifi_changed = false;

    /* door_delay */
    cJSON *j_delay = cJSON_GetObjectItem(json, "door_delay");
    if (cJSON_IsNumber(j_delay)) {
        int val = j_delay->valueint;
        if (val >= 1 && val <= 30) {
            nvs_store_config_set_door_delay((uint16_t)val);
            ESP_LOGI(TAG, "door_delay guncellendi: %d", val);
        }
    }

    /* ap_pass */
    cJSON *j_ap = cJSON_GetObjectItem(json, "ap_pass");
    if (cJSON_IsString(j_ap) && strlen(j_ap->valuestring) >= 8) {
        nvs_store_config_set_ap_pass(j_ap->valuestring);
        wifi_changed = true;
        ESP_LOGI(TAG, "ap_pass guncellendi");
    }

    /* wifi_ssid */
    cJSON *j_ssid = cJSON_GetObjectItem(json, "wifi_ssid");
    if (cJSON_IsString(j_ssid)) {
        nvs_store_config_set_wifi_ssid(j_ssid->valuestring);
        wifi_changed = true;
        ESP_LOGI(TAG, "wifi_ssid guncellendi: %s", j_ssid->valuestring);
    }

    /* wifi_pass */
    cJSON *j_wpass = cJSON_GetObjectItem(json, "wifi_pass");
    if (cJSON_IsString(j_wpass)) {
        nvs_store_config_set_wifi_pass(j_wpass->valuestring);
        wifi_changed = true;
        ESP_LOGI(TAG, "wifi_pass guncellendi");
    }

    cJSON_Delete(json);

    /* Wi-Fi ayarlari degistiyse yeniden yapilandir */
    if (wifi_changed) {
        ESP_LOGI(TAG, "Wi-Fi ayarlari degisti — yeniden yapilandiriliyor");
        wifi_manager_reconfigure();
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true}");
    return ESP_OK;
}

/* ============================================================
 * POST /api/auth/password — Sifre degistirme (korumali)
 * ============================================================ */
static esp_err_t password_post_handler(httpd_req_t *req)
{
    if (!validate_session(req)) {
        return send_unauthorized(req);
    }

    char body[MAX_BODY_SIZE];
    if (read_body(req, body, sizeof(body)) != ESP_OK) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"error\":\"Gecersiz istek\"}");
        return ESP_OK;
    }

    cJSON *json = cJSON_Parse(body);
    if (json == NULL) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"error\":\"Gecersiz JSON\"}");
        return ESP_OK;
    }

    cJSON *j_old = cJSON_GetObjectItem(json, "current_password");
    cJSON *j_new = cJSON_GetObjectItem(json, "new_password");

    if (!cJSON_IsString(j_old) || !cJSON_IsString(j_new)) {
        cJSON_Delete(json);
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"error\":\"current_password ve new_password gerekli\"}");
        return ESP_OK;
    }

    esp_err_t ret = auth_manager_change_password(j_old->valuestring, j_new->valuestring);
    cJSON_Delete(json);

    if (ret == ESP_FAIL) {
        httpd_resp_set_status(req, "401 Unauthorized");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"error\":\"Mevcut sifre hatali\"}");
        return ESP_OK;
    }

    if (ret == ESP_ERR_INVALID_ARG) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"error\":\"Yeni sifre en az 4 karakter olmali\"}");
        return ESP_OK;
    }

    /* Cookie temizle — kullanici yeniden giris yapmali */
    httpd_resp_set_hdr(req, "Set-Cookie", "session=; HttpOnly; Path=/; Max-Age=0");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true}");
    ESP_LOGI(TAG, "Sifre degistirildi");
    return ESP_OK;
}

/* ============================================================
 * POST /api/time — Sistem saatini ayarla (korumali)
 * ============================================================ */
static esp_err_t time_post_handler(httpd_req_t *req)
{
    if (!validate_session(req)) {
        return send_unauthorized(req);
    }

    char body[MAX_BODY_SIZE];
    if (read_body(req, body, sizeof(body)) != ESP_OK) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"error\":\"Gecersiz istek\"}");
        return ESP_OK;
    }

    cJSON *json = cJSON_Parse(body);
    if (json == NULL) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"error\":\"Gecersiz JSON\"}");
        return ESP_OK;
    }

    cJSON *j_ts = cJSON_GetObjectItem(json, "timestamp");
    if (!cJSON_IsNumber(j_ts)) {
        cJSON_Delete(json);
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"error\":\"timestamp gerekli\"}");
        return ESP_OK;
    }

    time_t new_time = (time_t)j_ts->valuedouble;
    cJSON_Delete(json);

    /* Gecerlilik kontrolu — 2024 oncesi kabul etme */
    if (new_time < 1704067200) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"error\":\"Gecersiz zaman damgasi\"}");
        return ESP_OK;
    }

    struct timeval tv = {
        .tv_sec = new_time,
        .tv_usec = 0,
    };
    settimeofday(&tv, NULL);

    ESP_LOGI(TAG, "Sistem saati ayarlandi: %ld", (long)new_time);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true}");
    return ESP_OK;
}

/* ============================================================
 * POST /api/ota — OTA firmware guncelleme (korumali, streaming)
 * ============================================================ */
#define OTA_BUF_SIZE 1024

static esp_err_t ota_post_handler(httpd_req_t *req)
{
    if (!validate_session(req)) {
        return send_unauthorized(req);
    }

    int content_len = req->content_len;
    if (content_len <= 0) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"error\":\"Firmware verisi bos\"}");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "OTA baslatiliyor — firmware boyutu: %d bytes", content_len);

    /* OTA oturumu baslat */
    esp_err_t ret = ota_manager_begin((size_t)content_len);
    if (ret != ESP_OK) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_set_type(req, "application/json");
        if (ret == ESP_ERR_INVALID_SIZE) {
            httpd_resp_sendstr(req, "{\"error\":\"Firmware partition'a sigmaz\"}");
        } else if (ret == ESP_ERR_INVALID_STATE) {
            httpd_resp_sendstr(req, "{\"error\":\"OTA zaten devam ediyor\"}");
        } else {
            httpd_resp_sendstr(req, "{\"error\":\"OTA baslatilamadi\"}");
        }
        return ESP_OK;
    }

    /* Streaming okuma — chunk chunk oku ve yaz */
    char buf[OTA_BUF_SIZE];
    int total_recv = 0;
    bool failed = false;

    while (total_recv < content_len) {
        int to_read = content_len - total_recv;
        if (to_read > OTA_BUF_SIZE) {
            to_read = OTA_BUF_SIZE;
        }

        int recv_len = httpd_req_recv(req, buf, to_read);
        if (recv_len <= 0) {
            if (recv_len == HTTPD_SOCK_ERR_TIMEOUT) {
                continue; /* Timeout — tekrar dene */
            }
            ESP_LOGE(TAG, "OTA veri alma hatasi: %d", recv_len);
            failed = true;
            break;
        }

        ret = ota_manager_write(buf, (size_t)recv_len);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "OTA yazma hatasi: %s", esp_err_to_name(ret));
            failed = true;
            break;
        }

        total_recv += recv_len;

        /* Her 100KB'da ilerleme logla */
        if ((total_recv % (100 * 1024)) < OTA_BUF_SIZE) {
            ESP_LOGI(TAG, "OTA ilerleme: %d / %d bytes (%d%%)",
                     total_recv, content_len,
                     (int)((int64_t)total_recv * 100 / content_len));
        }
    }

    if (failed) {
        ota_manager_abort();
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"error\":\"OTA yazma basarisiz\"}");
        return ESP_OK;
    }

    /* OTA tamamla — firmware dogrulama + boot partition ayarlama */
    ret = ota_manager_end();
    if (ret != ESP_OK) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"error\":\"Firmware dogrulama basarisiz\"}");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "OTA basarili — %d bytes yazildi, yeniden baslatiliyor...",
             total_recv);

    /* Basarili yanit gonder */
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true,\"message\":\"Guncelleme basarili, sistem yeniden baslatiliyor\"}");

    /* 1 saniye bekle (yanit gonderilsin) sonra restart */
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();

    return ESP_OK; /* Buraya ulasilmaz */
}

/* ============================================================
 * URI kayitlari
 * ============================================================ */

static const httpd_uri_t uri_login_get = {
    .uri = "/login", .method = HTTP_GET, .handler = login_get_handler
};
static const httpd_uri_t uri_login_post = {
    .uri = "/login", .method = HTTP_POST, .handler = login_post_handler
};
static const httpd_uri_t uri_logout_post = {
    .uri = "/logout", .method = HTTP_POST, .handler = logout_post_handler
};
static const httpd_uri_t uri_dashboard = {
    .uri = "/", .method = HTTP_GET, .handler = dashboard_get_handler
};
static const httpd_uri_t uri_cards_get = {
    .uri = "/api/cards", .method = HTTP_GET, .handler = cards_get_handler
};
static const httpd_uri_t uri_cards_post = {
    .uri = "/api/cards", .method = HTTP_POST, .handler = cards_post_handler
};
static const httpd_uri_t uri_cards_delete = {
    .uri = "/api/cards", .method = HTTP_DELETE, .handler = cards_delete_handler
};
static const httpd_uri_t uri_logs_get = {
    .uri = "/api/logs", .method = HTTP_GET, .handler = logs_get_handler
};
static const httpd_uri_t uri_logs_dates = {
    .uri = "/api/logs/dates", .method = HTTP_GET, .handler = logs_dates_handler
};
static const httpd_uri_t uri_logs_delete = {
    .uri = "/api/logs", .method = HTTP_DELETE, .handler = logs_delete_handler
};
static const httpd_uri_t uri_status_get = {
    .uri = "/api/status", .method = HTTP_GET, .handler = status_get_handler
};
static const httpd_uri_t uri_config_post = {
    .uri = "/api/config", .method = HTTP_POST, .handler = config_post_handler
};
static const httpd_uri_t uri_password_post = {
    .uri = "/api/auth/password", .method = HTTP_POST, .handler = password_post_handler
};
static const httpd_uri_t uri_ota_post = {
    .uri = "/api/ota", .method = HTTP_POST, .handler = ota_post_handler
};
static const httpd_uri_t uri_time_post = {
    .uri = "/api/time", .method = HTTP_POST, .handler = time_post_handler
};

/* ============================================================
 * Public API
 * ============================================================ */

esp_err_t web_server_init(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = MAX_URI_HANDLERS;
    config.stack_size = 10240;   /* OTA handler icin arttirildi (8192 → 10240) */
    config.lru_purge_enable = true;
    config.recv_wait_timeout = 30; /* OTA upload icin 30 sn timeout */

    ESP_LOGI(TAG, "HTTP server baslatiliyor (port %d)", config.server_port);

    esp_err_t ret = httpd_start(&s_server, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "HTTP server baslatilamadi: %s", esp_err_to_name(ret));
        return ret;
    }

    /* URI handler'lari kaydet */
    httpd_register_uri_handler(s_server, &uri_login_get);
    httpd_register_uri_handler(s_server, &uri_login_post);
    httpd_register_uri_handler(s_server, &uri_logout_post);
    httpd_register_uri_handler(s_server, &uri_dashboard);
    httpd_register_uri_handler(s_server, &uri_cards_get);
    httpd_register_uri_handler(s_server, &uri_cards_post);
    httpd_register_uri_handler(s_server, &uri_cards_delete);
    httpd_register_uri_handler(s_server, &uri_logs_get);
    httpd_register_uri_handler(s_server, &uri_logs_dates);
    httpd_register_uri_handler(s_server, &uri_logs_delete);
    httpd_register_uri_handler(s_server, &uri_status_get);
    httpd_register_uri_handler(s_server, &uri_config_post);
    httpd_register_uri_handler(s_server, &uri_password_post);
    httpd_register_uri_handler(s_server, &uri_ota_post);
    httpd_register_uri_handler(s_server, &uri_time_post);

    ESP_LOGI(TAG, "HTTP server baslatildi — 15 endpoint kayitli");
    return ESP_OK;
}

esp_err_t web_server_stop(void)
{
    if (s_server == NULL) {
        return ESP_OK;
    }

    esp_err_t ret = httpd_stop(s_server);
    if (ret == ESP_OK) {
        s_server = NULL;
        ESP_LOGI(TAG, "HTTP server durduruldu");
    }
    return ret;
}
