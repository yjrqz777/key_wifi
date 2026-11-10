/**
 * @file all_control.c
 * @brief 启动设备各功能任务，把组件的初始化串起来。
 * @details 本组件只做编排：先调用各组件自己的初始化接口，再创建它们提供的任务入口，
 *          自身不实现功能逻辑。LED 队列由 ws2812 组件持有，其他组件通过
 *          ws2812_send() 投递消息，不直接访问队列。
 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "all_control.h"
#include "webserver.h"
#include "easy_at.h"
#include "filetransfer.h"
#include "myusb.h"
#include "wifi.h"
#include "ws2812.h"

#include "mpu6050.h"
#include "adc_demo.h"
/**
 * @brief 网络任务：先拉起 Wi-Fi 网络栈，再启动依赖网络栈的 HTTP 服务器。
 * @param[in] pvParameters 未使用的 FreeRTOS 任务参数。
 * @note 顺序不能颠倒：httpd_start() 需要 lwIP 与默认事件循环已经初始化。
 */
static void net_task(void *pvParameters)
{
    wifi_sta_ap();
    start_webserver();
    vTaskDelete(NULL);
}

/**
 * @brief 初始化共享资源并启动设备功能任务。
 * @param[in] argc 未使用的兼容性参数数量。
 * @param[in] argv 未使用的兼容性参数数组。
 * @note 函数会先初始化 LED 队列，再启动可能投递颜色消息的功能任务。
 */
void all_control_main(int argc,int *argv)
{
    /* LED 队列必须先于任何发送方就绪。 */
    ESP_ERROR_CHECK(ws2812_init());

    /* 传输组件：接管 CDC 数据，等待按键切到传输模式后接收 XMODEM 文件。 */
    ESP_ERROR_CHECK(filetransfer_init());

    /* 依次启动 LED、USB、CMSIS-DAP、按键和 AT 命令任务。 */
    xTaskCreate(ws2812_task,    "ws2812_task",  1024*3, NULL, 5, NULL);
    xTaskCreate(usb_task,       "usb_task",     4096*5, NULL, 15, NULL);
    xTaskCreate(dap_task,       "dap_task",     4096,   NULL, 10, NULL);
    xTaskCreate(key_task,       "key_task",     1024*3, NULL, 6, NULL);
    xTaskCreate(usb_AT,         "usb_AT",       4096,   NULL, 5, NULL);

    xTaskCreate(iic_task,       "iic_task",     4096*2, NULL, 7, NULL);
    xTaskCreate(adc_demo_task,  "adc_demo_task",4096*2, NULL, 2, NULL);
    /* 网络初始化与网页服务放在独立任务里，避免阻塞 app_main。 */
    xTaskCreate(net_task,       "net_task",     4096*3, NULL, 5, NULL);
}
