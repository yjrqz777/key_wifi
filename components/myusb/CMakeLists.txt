
idf_component_register(SRCS "myusb.c" #"DAP_handle.c" "myfile.c"
                       INCLUDE_DIRS "include"
                       REQUIRES cherry-embedded__cherryusb cherryrb fatfs console fatfs#DAP
                       )
