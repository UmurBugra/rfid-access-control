idf_component_register(SRCS "web_server.c"
                       INCLUDE_DIRS "."
                       REQUIRES esp_http_server nvs_store auth_manager door_controller wifi_manager json esp_timer ota_manager
                       EMBED_TXTFILES "login.html" "dashboard.html")
