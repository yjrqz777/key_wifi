
idf_component_register(SRCS "usbAT.c" "virtualfat.c" "myDAP.c" "myusb.c"  "myDAP.c"  # "myfile.c" #"DAP_handle.c" 
                       INCLUDE_DIRS "include"
                       REQUIRES nvs_flash cherry-embedded__cherryusb cherryrb json fatfs DAP esp_wifi
                       )
