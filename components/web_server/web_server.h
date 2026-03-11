#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Web server componentini baslatir
 *
 * - esp_http_server baslatir (port 80)
 * - Tum REST API endpoint'lerini kaydeder
 * - Embedded HTML sayfalari sunar (login + dashboard)
 *
 * wifi_manager_init(), auth_manager_init(), nvs_store_init(),
 * door_controller_init() ONCEDEN cagrilmis olmali.
 *
 * @return ESP_OK basarili, hata kodu aksi halde
 */
esp_err_t web_server_init(void);

/**
 * @brief Web server'i durdurur
 *
 * OTA guncelleme oncesi cagrilabilir.
 *
 * @return ESP_OK basarili
 */
esp_err_t web_server_stop(void);

#ifdef __cplusplus
}
#endif

#endif // WEB_SERVER_H
