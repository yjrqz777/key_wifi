
idf_component_register(SRCS "myusb.c"
                       INCLUDE_DIRS "include"
                       REQUIRES espressif__esp_tinyusb fatfs console
                       )
