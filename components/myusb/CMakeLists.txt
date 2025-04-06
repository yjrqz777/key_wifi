
idf_component_register(SRCS "virtualfat.c" "myDAP.c" "myusb.c"  "myDAP.c"  # "myfile.c" #"DAP_handle.c" 
                       INCLUDE_DIRS "include"
                       REQUIRES cherry-embedded__cherryusb cherryrb json fatfs DAP
                       )
