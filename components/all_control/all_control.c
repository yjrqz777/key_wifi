/***************************************************************************************************
 * Author: yjrqz777 3210551161@qq.com
 * Date: 2025-06-03 20:57:53
 * LastEditTime: 2026-06-06 10:06:54
 * LastEditors: yjrqz777 3210551161@qq.com
 * Description: 
 * FilePath: /key_wifi/components/all_control/all_control.c
 * @YJRQZ777
***************************************************************************************************/
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"


#include "all_control.h"
#include "myusb.h"
QueueHandle_t xQueueLed;
// QueueHandle_t xQueueKey;


/***************************************************************************************************
 * 功能描述: 所有控制的入口函数
 * 输入参数: 
 * 输出参数: 
 * 返 回 值: 
 * 其它说明: 
 * param {int} argc
 * param {int} *argv
***************************************************************************************************/
void all_control_main(int argc,int *argv)
{
    xQueueLed = xQueueCreate(5,sizeof(struct tws2812Def));


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

