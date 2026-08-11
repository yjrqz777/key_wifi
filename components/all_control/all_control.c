/**
 * @file all_control.c
 * @brief 创建共享控制队列并启动设备功能任务。
 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"


#include "all_control.h"
#include "myusb.h"

/**
 * @brief 用于在控制任务之间传递 WS2812 颜色更新消息的队列。
 *
 * 该队列由 all_control_main() 在启动功能任务前创建。
 */
QueueHandle_t xQueueLed;
// QueueHandle_t xQueueKey;

/**
 * @brief 初始化共享控制资源并启动设备功能任务。
 * @param[in] argc 未使用的兼容性参数数量。
 * @param[in] argv 未使用的兼容性参数数组。
 * @note 函数会先创建 xQueueLed，再启动可能使用该队列的功能任务。
 */
void all_control_main(int argc,int *argv)
{
    /* 先创建队列，确保 WS2812 消费任务启动时资源已经就绪。 */
    xQueueLed = xQueueCreate(5,sizeof(struct tws2812Def));

    /* 依次启动 LED、USB、CMSIS-DAP、按键和 Wi-Fi 控制任务。 */
    xTaskCreate(ws2812_task, "ws2812_task", 1024*3, NULL, 5, NULL);
    xTaskCreate(usb_task, "usb_task", 4096*5, NULL, 15, NULL);
    xTaskCreate(dap_task, "dap_task", 4096, NULL, 10, NULL);
    xTaskCreate(key_task, "key_task", 1024*3, NULL, 6, NULL);
    xTaskCreate(wifi_task, "wifi_task", 4096*3, NULL, 5, NULL);

    // while (true)
    // {
    //     vTaskDelay(10/ portTICK_PERIOD_MS);
    // }
    

}
