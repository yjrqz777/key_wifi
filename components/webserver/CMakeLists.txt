file(GLOB SRCS "src/*.c")

idf_component_register(SRCS ${SRCS}
                       INCLUDE_DIRS "include"
                       REQUIRES myusb ws2812 wifi json esp_http_server esp_wifi esp_netif nvs_flash lwip
                       EMBED_FILES root.html serial_view.html
                       )
