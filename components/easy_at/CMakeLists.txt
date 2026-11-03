

idf_component_register(SRCS "src/easy_at.c" 
                        INCLUDE_DIRS "include" 
                        REQUIRES myusb json cherry-embedded__cherryusb flash wifi)
