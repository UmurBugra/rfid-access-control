idf_component_register(SRCS "wifi_manager.c"
                       INCLUDE_DIRS "."
                       REQUIRES nvs_store esp_wifi esp_netif esp_event lwip)
