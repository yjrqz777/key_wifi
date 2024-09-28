#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "sdkconfig.h"

#include "key.h"
#include "var.h"

static const char *TAG = "key";

int key11_status = 1;

//上一次开关状态变量
int key11_last_status;

#define KEY_GPIO GPIO_NUM_0
uint8_t s_key_state = 0;

static void configure_led(void)
{
    ESP_LOGI(TAG, "Example configured to blink GPIO LED!");
    gpio_reset_pin(KEY_GPIO);
    /* Set the GPIO as a push/pull output */
    gpio_set_direction(KEY_GPIO, GPIO_MODE_INPUT);
}

void key_task()
{
    tRgbKeyDef AllQueue = {0, 0, 0, 0};
    static uint8_t fg = 0;
    configure_led();

    while(1)
    {
		key11_last_status = key11_status;
		//读取当前按键状态
		key11_status = gpio_get_level(KEY_GPIO);

		if(key11_status && !key11_last_status)
            fg = 1; 
        if (fg)
        {
            fg = 0;
            AllQueue.k = !AllQueue.k;
        }
        if (xQueueSend(xQueueKey, &AllQueue, portMAX_DELAY) == pdTRUE) {
            // printf("发送RGB: R=%d, G=%d, B=%d\n", color.r, color.g, color.b);
        }

        // ESP_LOGI(TAG, "--%d--",s_key_state);

        vTaskDelay(10 / portTICK_PERIOD_MS);
        
    }

}