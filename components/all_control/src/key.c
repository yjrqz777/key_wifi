/***************************************************************************************************
 * Author: yjrqz777 3210551161@qq.com
 * Date: 2025-06-03 21:15:39
 * LastEditTime: 2025-06-04 21:09:26
 * LastEditors: yjrqz777 3210551161@qq.com
 * Description: 
 * FilePath: /key_wifi/components/all_control/src/key.c
 * @YJRQZ777
***************************************************************************************************/

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"


#include "driver/gpio.h"
#include "button_gpio.h"
#include "button_interface.h"
#include "iot_button.h"

#include "all_control.h"


static const char *TAG = "Key";

#define BUTTON_IO_NUM  0
#define BUTTON_ACTIVE_LEVEL   0


extern void wifi_button_handler(void);






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
        u8SendFg = 0;
        if (xQueueSend(xQueueLed, &tws2812Data, portMAX_DELAY) == pdTRUE) 
        {
            // printf("发送RGB: R=%d, G=%d, B=%d\n", AllQueue.r, AllQueue.g, AllQueue.b);
        }
    }

}

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