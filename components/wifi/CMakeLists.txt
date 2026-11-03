idf_component_register(SRCS "src/wifi.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_wifi esp_netif esp_event nvs_flash lwip ws2812
                       )
