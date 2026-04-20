idf_component_register(SRCS "main.c"
       REQUIRES esp32-camera esp_wifi esp_http_server nvs_flash driver esp_timer mdns esp_http_client lwip)