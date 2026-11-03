idf_component_register(SRCS "src/filetransfer.c"
                       INCLUDE_DIRS "include"
                       REQUIRES devmode myusb cherryrb espressif__xymodem
                       )
