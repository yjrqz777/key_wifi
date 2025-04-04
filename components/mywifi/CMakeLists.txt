# set(COMPONENT_REQUIRES "json")

# 添加自定义组件路径（示例路径）
# set(EXTRA_COMPONENT_DIRS "components/cJSON")

idf_component_register(SRCS "webserver.c" "mywifi.c" "webserver.c"
                    INCLUDE_DIRS "include"
                    REQUIRES driver esp_wifi nvs_flash all_var json esp_http_server 
                    EMBED_FILES 
                    root.html
                    )

