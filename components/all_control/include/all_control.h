#ifndef ALL_CONTROL_H
#define ALL_CONTROL_H

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <stdbool.h>
#include "esp_err.h"

// 定义事件位（最多可用24位）
#define SAT_EN  (1 << 0)
#define AP_EN   (1 << 1)




/**
 * @brief Type of led strip encoder configuration
 */
typedef struct {
    uint32_t resolution; /*!< Encoder resolution, in Hz */
} led_strip_encoder_config_t;


typedef struct tws2812Def
{
    uint32_t h;
    uint32_t s;
    uint32_t v;
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint32_t u32time;
} tws2812Def;





extern QueueHandle_t xQueueLed;
// extern QueueHandle_t xQueueKey;



void all_control_main(int argc,int *argv);
void key_task(void *pvParameters);
void ws2812_task(void *pvParameters);
void wifi_task(void *pvParameters);
esp_err_t wifi_set_ap_sta_enabled(bool ap_enable, bool sta_enable);
esp_err_t wifi_get_ap_sta_enabled(bool *ap_enable, bool *sta_enable);




#endif
