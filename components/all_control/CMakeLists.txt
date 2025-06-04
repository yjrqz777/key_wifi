# set(SRCS src/*)
file(GLOB SRCS "src/*.c") # 获取 src 下所有 .c 文件
set(REQ driver esp_wifi esp_http_server espressif__button nvs_flash json DAP myusb)



idf_component_register(SRCS "all_control.c" ${SRCS}
                    INCLUDE_DIRS "include"
                    REQUIRES ${REQ}
                    EMBED_FILES root.html
                    )

