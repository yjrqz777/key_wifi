/***************************************************************************************************
 * Author: yjrqz777 3210551161@qq.com
 * Date: 2025-06-03 21:15:39
 * LastEditTime: 2025-06-04 21:09:26
 * LastEditors: yjrqz777 3210551161@qq.com
 * Description: 
 * FilePath: /key_wifi/components/all_control/src/key.c
 * @YJRQZ777
***************************************************************************************************/

/**
 * @file key.c
 * @brief 按键采集任务：把 GPIO 按键事件转换成 WS2812 控制消息。
 * @details 基于 iot_button 组件注册按键回调，按键动作被映射成 HSV 颜色消息投递到 ws2812 组件；
 *          按键结束事件还会调用 wifi_button_handler() 触发 Wi-Fi 相关动作。
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"


#include "driver/gpio.h"
#include "button_gpio.h"
#include "button_interface.h"
#include "iot_button.h"

#include "wifi.h"
#include "ws2812.h"


static const char *TAG = "Key";

#define BUTTON_IO_NUM  0                    /* 按键连接的 GPIO 编号 */
#define BUTTON_ACTIVE_LEVEL   0             /* 按键有效电平，0 表示低电平有效 */






/**
 * @brief 按键事件回调，把按键事件翻译成 WS2812 颜色消息。
 * @param[in] arg 产生事件的按键句柄，用于查询事件类型、重复次数和长按时长。
 * @param[in] data 注册时绑定的用户数据，多点按事件中表示点击次数。
 * @note 回调运行在按键组件的任务上下文中，投递队列使用 portMAX_DELAY，队列满时会阻塞该任务。
 * @note 所有事件共用一份静态消息缓存，只有置位 u8SendFg 的事件才会真正入队。
 */
static void button_event_cb(void *arg, void *data)
{
    static uint8_t u8SendFg = 0;
    static tws2812Def tws2812Data = {0};
    button_event_t event = iot_button_get_event(arg);
    // ESP_LOGI(TAG, "%s", iot_button_get_event_str(event));

    if (BUTTON_PRESS_REPEAT == event || BUTTON_PRESS_REPEAT_DONE == event) {
        ESP_LOGI(TAG, "\tREPEAT[%d]", iot_button_get_repeat(arg));
    }

    if (BUTTON_PRESS_UP == event) 
    {
        /*触发按键闪一下*/
        tws2812Data.h = 0;
        tws2812Data.s = 0;
        tws2812Data.v = 0;
        tws2812Data.u32time = 0;
        u8SendFg = 1;
    }
    if (BUTTON_LONG_PRESS_HOLD == event || BUTTON_LONG_PRESS_UP == event) 
    {
        /*长按 Rainbow*/
        ESP_LOGI(TAG, "\tTICKS[%"PRIu32"]", iot_button_get_ticks_time(arg));
        tws2812Data.h += 1;
        tws2812Data.s = 100;
        tws2812Data.v = 5;
        tws2812Data.u32time = 0;
        u8SendFg = 1;
    }
    if (BUTTON_MULTIPLE_CLICK == event) {
        ESP_LOGI(TAG, "\tMULTIPLE[%d]", (int)data);
    }
    
    if (BUTTON_PRESS_END == event)
    {
        /**/
        ESP_LOGI(TAG, "BUTTON_PRESS_END");
        // tws2812Data.h = 299;
        // tws2812Data.s = 100;
        // tws2812Data.v = 0;
        // tws2812Data.u32time = 0;
        // u8SendFg = 1;
        wifi_button_handler();
    }

    if (u8SendFg == 1)
    {
        /* 一次事件只投递一次，发送后立即清标志，避免重复入队。 */
        u8SendFg = 0;
        if (ws2812_send(&tws2812Data, portMAX_DELAY)) 
        {
            // printf("发送RGB: R=%d, G=%d, B=%d\n", AllQueue.r, AllQueue.g, AllQueue.b);
        }
    }

}

/**
 * @brief 按键任务入口：创建 GPIO 按键设备并注册全部事件回调。
 * @param[in] pvParameters 未使用的 FreeRTOS 任务参数。
 * @note 回调注册完成后任务进入无限延时循环，函数不会返回。
 * @note iot_button_new_gpio_device() 的返回值未做检查，设备创建失败时任务仍继续注册回调。
 */
void key_task(void *pvParameters)
{
    const button_config_t btn_cfg = {0};
    const button_gpio_config_t btn_gpio_cfg = {
        .gpio_num = BUTTON_IO_NUM,
        .active_level = BUTTON_ACTIVE_LEVEL,
    };

    button_handle_t btn = NULL;
    esp_err_t ret = iot_button_new_gpio_device(&btn_cfg, &btn_gpio_cfg, &btn);
    // TEST_ASSERT(ret == ESP_OK);
    // TEST_ASSERT_NOT_NULL(btn);
    iot_button_register_cb(btn, BUTTON_PRESS_DOWN, NULL, button_event_cb, NULL);
    iot_button_register_cb(btn, BUTTON_PRESS_UP, NULL, button_event_cb, NULL);
    iot_button_register_cb(btn, BUTTON_PRESS_REPEAT, NULL, button_event_cb, NULL);
    iot_button_register_cb(btn, BUTTON_PRESS_REPEAT_DONE, NULL, button_event_cb, NULL);
    iot_button_register_cb(btn, BUTTON_SINGLE_CLICK, NULL, button_event_cb, NULL);
    iot_button_register_cb(btn, BUTTON_DOUBLE_CLICK, NULL, button_event_cb, NULL);

    /*!< Multiple Click must provide button_event_args_t */
    /*!< Double Click */
    button_event_args_t args = {
        .multiple_clicks.clicks = 2,
    };
    iot_button_register_cb(btn, BUTTON_MULTIPLE_CLICK, &args, button_event_cb, (void *)2);
    /*!< Triple Click */
    args.multiple_clicks.clicks = 3;
    iot_button_register_cb(btn, BUTTON_MULTIPLE_CLICK, &args, button_event_cb, (void *)3);
    iot_button_register_cb(btn, BUTTON_LONG_PRESS_START, NULL, button_event_cb, NULL);
    iot_button_register_cb(btn, BUTTON_LONG_PRESS_HOLD, NULL, button_event_cb, NULL);
    iot_button_register_cb(btn, BUTTON_LONG_PRESS_UP, NULL, button_event_cb, NULL);
    iot_button_register_cb(btn, BUTTON_PRESS_END, NULL, button_event_cb, NULL);

    uint8_t level = 0;
    level = iot_button_get_key_level(btn);
    ESP_LOGI(TAG, "button level is %d", level);

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    iot_button_delete(btn);
}