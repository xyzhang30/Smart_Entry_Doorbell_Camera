idf_component_register(SRCS "main.c"
                    INCLUDE_DIRS "."
                    REQUIRES esp_http_client
                    REQUIRES esp_timer
                    REQUIRES esp_wifi
                    REQUIRES nvs_flash)
