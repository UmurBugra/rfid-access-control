idf_component_register(SRCS "door_controller.c"
                       INCLUDE_DIRS "."
                       REQUIRES nvs_store rfid_manager littlefs driver)
