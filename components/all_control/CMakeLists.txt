# 本组件只做任务编排：key.c 是本组件的按键实现，其余功能由 wifi、ws2812、webserver 等组件提供。
file(GLOB SRCS "src/*.c") # 获取 src 下所有 .c 文件
set(REQ driver espressif__button myusb easy_at wifi ws2812 webserver filetransfer mpu6050 adc_demo)



idf_component_register(SRCS "all_control.c" ${SRCS}
                    INCLUDE_DIRS "include"
                    REQUIRES ${REQ}
                    )
