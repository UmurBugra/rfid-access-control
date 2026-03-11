idf_component_register(SRCS "auth_manager.c"
                       INCLUDE_DIRS "."
                       REQUIRES nvs_flash mbedtls)
