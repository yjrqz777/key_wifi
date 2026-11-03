

idf_component_register(SRCS "src/nvs_store.c" 
                        INCLUDE_DIRS "include" 
                        REQUIRES esp_partition spi_flash nvs_flash)
