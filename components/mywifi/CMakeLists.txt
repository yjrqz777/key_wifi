idf_component_register(SRCS "mywifi.c"
                    INCLUDE_DIRS "include"
                    REQUIRES driver esp_wifi nvs_flash  all_var
                    )

