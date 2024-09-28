
idf_component_register(SRCS "myusb.c" #"DAP_handle.c"
                       INCLUDE_DIRS "include"
                       REQUIRES espressif__esp_tinyusb fatfs console #DAP
                       )
