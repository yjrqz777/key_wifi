# BLE 串口组件：用 NimBLE 起自定义 GATT 服务，收数据走回调，发数据走通知。
idf_component_register(SRCS "src/ble.c"
                       INCLUDE_DIRS "include"
                       REQUIRES bt driver
                       )
