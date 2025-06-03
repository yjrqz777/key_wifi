set(srcs "main.c")
set(requires fatfs console myusb key)

if(CONFIG_EXAMPLE_STORAGE_MEDIA_SPIFLASH)
    list(APPEND requires wear_levelling)
endif()

idf_component_register(
    SRCS "task.c" "${srcs}"
    INCLUDE_DIRS .
    # REQUIRES "${requires}"
)
