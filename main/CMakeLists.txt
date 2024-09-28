set(srcs "main.c")
set(requires fatfs console myusb key all_var)

if(CONFIG_EXAMPLE_STORAGE_MEDIA_SPIFLASH)
    list(APPEND requires wear_levelling)
endif()

idf_component_register(
    SRCS "${srcs}"
    INCLUDE_DIRS .
    # REQUIRES "${requires}"
)
