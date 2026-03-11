#include <stdio.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs_store.h"

static const char *TAG = "main";

void app_main(void)
{
    ESP_LOGI(TAG, "Sistem baslatiliyor");

    /* NVS flash baslatma */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition siliniyor ve yeniden baslatiliyor");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* NVS store baslatma */
    ESP_ERROR_CHECK(nvs_store_init());

    ESP_LOGI(TAG, "Sistem baslatma tamamlandi");
}
