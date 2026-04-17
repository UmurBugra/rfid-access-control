#include "wifi_manager.h"

#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "mdns.h"
#include "esp_sntp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "nvs_store.h"

static const char *TAG = "wifi_mgr";

/* mDNS hostname — simcleverkapi.local */
#define MDNS_HOSTNAME   "simcleverkapi"

/* ============================================================
 * Event group bitleri
 * ============================================================ */
#define WIFI_STA_CONNECTED_BIT   BIT0

/* ============================================================
 * Modul degiskenleri
 * ============================================================ */
static EventGroupHandle_t s_wifi_event_group = NULL;
static esp_netif_t       *s_netif_ap         = NULL;
static esp_netif_t       *s_netif_sta        = NULL;
static bool               s_sta_configured   = false;
static uint32_t           s_retry_delay_ms   = WIFI_STA_RETRY_INIT_MS;
static esp_event_handler_instance_t s_wifi_handler_instance = NULL;
static esp_event_handler_instance_t s_ip_handler_instance   = NULL;

/* ============================================================
 * Dahili fonksiyon bildirimleri
 * ============================================================ */
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data);
static void ip_event_handler(void *arg, esp_event_base_t event_base,
                             int32_t event_id, void *event_data);
static esp_err_t configure_ap(void);
static esp_err_t configure_sta(void);
static void start_sntp(void);
static void start_mdns(void);

/* ============================================================
 * Public API
 * ============================================================ */

esp_err_t wifi_manager_init(void)
{
    esp_err_t ret;

    /* Event group olustur */
    s_wifi_event_group = xEventGroupCreate();
    if (s_wifi_event_group == NULL) {
        ESP_LOGE(TAG, "Event group olusturulamadi");
        return ESP_ERR_NO_MEM;
    }

    /* Network interface baslatma */
    ret = esp_netif_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_netif_init basarisiz: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Default event loop olustur */
    ret = esp_event_loop_create_default();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Event loop olusturulamadi: %s", esp_err_to_name(ret));
        return ret;
    }

    /* AP ve STA netif olustur */
    s_netif_ap  = esp_netif_create_default_wifi_ap();
    s_netif_sta = esp_netif_create_default_wifi_sta();

    if (s_netif_ap == NULL || s_netif_sta == NULL) {
        ESP_LOGE(TAG, "Netif olusturulamadi");
        return ESP_FAIL;
    }

    /* Wi-Fi driver baslatma (varsayilan config) */
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ret = esp_wifi_init(&cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_init basarisiz: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Event handler kaydet — Wi-Fi olaylari */
    ret = esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID,
        &wifi_event_handler, NULL, &s_wifi_handler_instance);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi event handler kayit basarisiz: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Event handler kaydet — IP olaylari */
    ret = esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP,
        &ip_event_handler, NULL, &s_ip_handler_instance);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "IP event handler kayit basarisiz: %s", esp_err_to_name(ret));
        return ret;
    }

    /* APSTA modu — AP ve STA ayni anda aktif */
    ret = esp_wifi_set_mode(WIFI_MODE_APSTA);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi mod ayarlama basarisiz: %s", esp_err_to_name(ret));
        return ret;
    }

    /* AP yapilandir */
    ret = configure_ap();
    if (ret != ESP_OK) {
        return ret;
    }

    /* STA yapilandir (opsiyonel — NVS'te SSID varsa) */
    ret = configure_sta();
    if (ret != ESP_OK && ret != ESP_ERR_NOT_FOUND) {
        return ret;
    }

    /* Wi-Fi baslat */
    ret = esp_wifi_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_start basarisiz: %s", esp_err_to_name(ret));
        return ret;
    }

    /* mDNS baslat — simcleverkapi.local ile erisim */
    start_mdns();

    ESP_LOGI(TAG, "Wi-Fi baslatildi (APSTA modu)");
    ESP_LOGI(TAG, "AP SSID: %s", WIFI_AP_SSID);

    if (s_sta_configured) {
        ESP_LOGI(TAG, "STA modu aktif — baglanti denemesi yapilacak");
    } else {
        ESP_LOGI(TAG, "STA modu yapilandirilmadi — sadece AP aktif");
    }

    return ESP_OK;
}

bool wifi_manager_is_sta_connected(void)
{
    if (s_wifi_event_group == NULL) {
        return false;
    }
    EventBits_t bits = xEventGroupGetBits(s_wifi_event_group);
    return (bits & WIFI_STA_CONNECTED_BIT) != 0;
}

esp_err_t wifi_manager_get_sta_ip(char *ip_buf, size_t buf_len)
{
    if (ip_buf == NULL || buf_len < 16) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!wifi_manager_is_sta_connected()) {
        ip_buf[0] = '\0';
        return ESP_ERR_INVALID_STATE;
    }

    esp_netif_ip_info_t ip_info;
    esp_err_t ret = esp_netif_get_ip_info(s_netif_sta, &ip_info);
    if (ret != ESP_OK) {
        return ret;
    }

    snprintf(ip_buf, buf_len, IPSTR, IP2STR(&ip_info.ip));
    return ESP_OK;
}

esp_err_t wifi_manager_get_ap_ip(char *ip_buf, size_t buf_len)
{
    if (ip_buf == NULL || buf_len < 16) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_netif_ip_info_t ip_info;
    esp_err_t ret = esp_netif_get_ip_info(s_netif_ap, &ip_info);
    if (ret != ESP_OK) {
        return ret;
    }

    snprintf(ip_buf, buf_len, IPSTR, IP2STR(&ip_info.ip));
    return ESP_OK;
}

uint8_t wifi_manager_get_ap_client_count(void)
{
    wifi_sta_list_t sta_list;
    if (esp_wifi_ap_get_sta_list(&sta_list) != ESP_OK) {
        return 0;
    }
    return (uint8_t)sta_list.num;
}

esp_err_t wifi_manager_reconfigure(void)
{
    ESP_LOGI(TAG, "Wi-Fi yeniden yapilandiriliyor...");

    /* Wi-Fi durdur */
    esp_wifi_disconnect();
    esp_wifi_stop();

    /* mDNS durdur (yeniden baslatilacak) */
    mdns_free();

    /* STA baglanti durumunu sifirla */
    xEventGroupClearBits(s_wifi_event_group, WIFI_STA_CONNECTED_BIT);
    s_retry_delay_ms = WIFI_STA_RETRY_INIT_MS;

    /* AP yeniden yapilandir */
    esp_err_t ret = configure_ap();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "AP yeniden yapilandirma basarisiz");
        return ret;
    }

    /* STA yeniden yapilandir */
    ret = configure_sta();
    if (ret != ESP_OK && ret != ESP_ERR_NOT_FOUND) {
        ESP_LOGE(TAG, "STA yeniden yapilandirma basarisiz");
        return ret;
    }

    /* Wi-Fi yeniden baslat */
    ret = esp_wifi_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi yeniden baslatma basarisiz: %s", esp_err_to_name(ret));
        return ret;
    }

    /* mDNS yeniden baslat */
    start_mdns();

    ESP_LOGI(TAG, "Wi-Fi yeniden yapilandirildi");
    return ESP_OK;
}

/* ============================================================
 * AP yapilandirmasi
 * ============================================================ */

static esp_err_t configure_ap(void)
{
    /* AP sifresini NVS'ten oku */
    char ap_pass[64];
    esp_err_t ret = nvs_store_config_get_ap_pass(ap_pass, sizeof(ap_pass));
    if (ret != ESP_OK) {
        /* Varsayilan sifre (nvs_store zaten "kapi1234" doner ama garanti olsun) */
        strcpy(ap_pass, "kapi1234");
    }

    wifi_config_t ap_config = {
        .ap = {
            .channel        = WIFI_AP_CHANNEL,
            .max_connection = WIFI_AP_MAX_CONN,
            .authmode       = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {
                .required = false,
            },
        },
    };

    /* SSID kopyala */
    strncpy((char *)ap_config.ap.ssid, WIFI_AP_SSID, sizeof(ap_config.ap.ssid) - 1);
    ap_config.ap.ssid_len = strlen(WIFI_AP_SSID);

    /* Sifre kopyala */
    strncpy((char *)ap_config.ap.password, ap_pass, sizeof(ap_config.ap.password) - 1);

    /* Sifre bossa open mod */
    if (strlen(ap_pass) == 0) {
        ap_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ret = esp_wifi_set_config(WIFI_IF_AP, &ap_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "AP config ayarlama basarisiz: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "AP yapilandirildi — SSID: %s, Kanal: %d", WIFI_AP_SSID, WIFI_AP_CHANNEL);
    return ESP_OK;
}

/* ============================================================
 * STA yapilandirmasi
 * ============================================================ */

static esp_err_t configure_sta(void)
{
    char wifi_ssid[33] = {0};
    char wifi_pass[64] = {0};

    /* NVS'ten STA SSID oku */
    esp_err_t ret = nvs_store_config_get_wifi_ssid(wifi_ssid, sizeof(wifi_ssid));
    if (ret != ESP_OK || strlen(wifi_ssid) == 0) {
        /* STA yapilandirilmayacak — sadece AP modunda calis */
        s_sta_configured = false;
        ESP_LOGI(TAG, "STA SSID bulunamadi — sadece AP modu");
        return ESP_ERR_NOT_FOUND;
    }

    /* NVS'ten STA sifresi oku (bos olabilir — open network) */
    nvs_store_config_get_wifi_pass(wifi_pass, sizeof(wifi_pass));

    wifi_config_t sta_config = {0};
    strncpy((char *)sta_config.sta.ssid, wifi_ssid, sizeof(sta_config.sta.ssid) - 1);
    strncpy((char *)sta_config.sta.password, wifi_pass, sizeof(sta_config.sta.password) - 1);

    /* Scan metodu: hizli baglanti icin hedef SSID ile tara */
    sta_config.sta.scan_method = WIFI_FAST_SCAN;
    sta_config.sta.threshold.authmode = WIFI_AUTH_OPEN;

    ret = esp_wifi_set_config(WIFI_IF_STA, &sta_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "STA config ayarlama basarisiz: %s", esp_err_to_name(ret));
        s_sta_configured = false;
        return ret;
    }

    s_sta_configured = true;
    s_retry_delay_ms = WIFI_STA_RETRY_INIT_MS;

    ESP_LOGI(TAG, "STA yapilandirildi — SSID: %s", wifi_ssid);
    return ESP_OK;
}

/* ============================================================
 * Wi-Fi event handler
 * ============================================================ */

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    switch (event_id) {

    case WIFI_EVENT_STA_START:
        if (s_sta_configured) {
            ESP_LOGI(TAG, "STA baslatildi — baglanti denemesi");
            esp_wifi_connect();
        }
        break;

    case WIFI_EVENT_STA_CONNECTED:
        ESP_LOGI(TAG, "STA baglandi — IP bekleniyor...");
        s_retry_delay_ms = WIFI_STA_RETRY_INIT_MS;
        break;

    case WIFI_EVENT_STA_DISCONNECTED: {
        xEventGroupClearBits(s_wifi_event_group, WIFI_STA_CONNECTED_BIT);

        if (s_sta_configured) {
            ESP_LOGW(TAG, "STA baglanti koptu — %lu ms sonra yeniden denenecek",
                     (unsigned long)s_retry_delay_ms);

            vTaskDelay(pdMS_TO_TICKS(s_retry_delay_ms));

            /* Exponential backoff */
            if (s_retry_delay_ms < WIFI_STA_RETRY_MAX_MS) {
                s_retry_delay_ms *= 2;
                if (s_retry_delay_ms > WIFI_STA_RETRY_MAX_MS) {
                    s_retry_delay_ms = WIFI_STA_RETRY_MAX_MS;
                }
            }

            esp_wifi_connect();
        }
        break;
    }

    case WIFI_EVENT_AP_STACONNECTED: {
        wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *)event_data;
        ESP_LOGI(TAG, "AP: Istemci baglandi — MAC: %02x:%02x:%02x:%02x:%02x:%02x",
                 event->mac[0], event->mac[1], event->mac[2],
                 event->mac[3], event->mac[4], event->mac[5]);
        break;
    }

    case WIFI_EVENT_AP_STADISCONNECTED: {
        wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t *)event_data;
        ESP_LOGI(TAG, "AP: Istemci ayrildi — MAC: %02x:%02x:%02x:%02x:%02x:%02x",
                 event->mac[0], event->mac[1], event->mac[2],
                 event->mac[3], event->mac[4], event->mac[5]);
        break;
    }

    default:
        break;
    }
}

/* ============================================================
 * IP event handler
 * ============================================================ */

static void ip_event_handler(void *arg, esp_event_base_t event_base,
                             int32_t event_id, void *event_data)
{
    if (event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "STA IP alindi: " IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(s_wifi_event_group, WIFI_STA_CONNECTED_BIT);

        /* Internet baglantisi var — SNTP baslat */
        start_sntp();
    }
}

/* ============================================================
 * SNTP saat senkronizasyonu
 * ============================================================ */

static void sntp_sync_notification(struct timeval *tv)
{
    ESP_LOGI(TAG, "SNTP saat senkronize edildi");
}

static void start_sntp(void)
{
    /* Zaten baslatildiysa tekrar baslatma */
    if (esp_sntp_enabled()) {
        ESP_LOGI(TAG, "SNTP zaten aktif");
        return;
    }

    ESP_LOGI(TAG, "SNTP baslatiliyor...");
    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_setservername(1, "time.google.com");
    esp_sntp_set_time_sync_notification_cb(sntp_sync_notification);
    esp_sntp_init();

    /* Saat dilimi: Turkiye (UTC+3) */
    setenv("TZ", "UTC-3", 1);
    tzset();
}

/* ============================================================
 * mDNS servisi — simcleverkapi.local
 * ============================================================ */

static void start_mdns(void)
{
    esp_err_t ret = mdns_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "mDNS baslatma basarisiz: %s", esp_err_to_name(ret));
        return;
    }

    ret = mdns_hostname_set(MDNS_HOSTNAME);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "mDNS hostname ayarlama basarisiz: %s", esp_err_to_name(ret));
        return;
    }

    ret = mdns_instance_name_set("KAPI Erisim Kontrol Sistemi");
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "mDNS instance name ayarlama basarisiz: %s", esp_err_to_name(ret));
    }

    /* HTTP servisi tanimlama */
    mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);

    ESP_LOGI(TAG, "mDNS baslatildi — hostname: %s.local", MDNS_HOSTNAME);
}
