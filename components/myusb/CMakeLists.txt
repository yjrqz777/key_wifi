
idf_component_register(SRCS "myusb.c" # "myfile.c" #"DAP_handle.c" 
                       INCLUDE_DIRS "include"
                       REQUIRES cherry-embedded__cherryusb cherryrb console fatfs DAP
                       )
