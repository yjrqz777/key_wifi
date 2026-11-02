/**
 * @file all_control.h
 * @brief all_control 组件的公共接口：共享队列、任务入口和 Wi-Fi 模式控制。
 * @details 本头文件只做声明，函数说明统一写在对应 .c 文件的定义处。
 */
#ifndef ALL_CONTROL_H
#define ALL_CONTROL_H

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <stdbool.h>
#include "esp_err.h"

// 定义事件位（最多可用24位）
#define SAT_EN  (1 << 0) /* STA 模式使能事件位 */
#define AP_EN   (1 << 1) /* AP 模式使能事件位 */




/**
 * @brief RMT LED 灯带编码器的配置参数。
 */
typedef struct {
    uint32_t resolution; /**< 编码器分辨率，单位 Hz */
} led_strip_encoder_config_t;


/**
 * @brief WS2812 灯光控制消息，经 xQueueLed 在控制任务与 ws2812_task 之间传递。
 * @details h/s/v 与 r/g/b 两组字段同时存在：发送方可以只填写 HSV，
 *          由 ws2812_task 换算并回填 RGB；v 为 0 时保持熄灭。
 */
typedef struct tws2812Def
{
    uint32_t h;         /**< 色相，单位度，换算时按 360 取模 */
    uint32_t s;         /**< 饱和度，取值 0 到 100 */
    uint32_t v;         /**< 亮度，取值 0 到 100，为 0 时不点亮 */
    uint8_t r;          /**< 红色分量，由 ws2812_task 从 HSV 换算后回填，取值 0 到 255 */
    uint8_t g;          /**< 绿色分量，由 ws2812_task 从 HSV 换算后回填，取值 0 到 255 */
    uint8_t b;          /**< 蓝色分量，由 ws2812_task 从 HSV 换算后回填，取值 0 到 255 */
    uint32_t u32time;   /**< 自动换色的起始计数值，收到新消息时由发送方重置为 0 */
} tws2812Def;





/** LED 颜色更新消息队列，由 all_control_main() 在启动任务前创建。 */
extern QueueHandle_t xQueueLed;
// extern QueueHandle_t xQueueKey;



void all_control_main(int argc,int *argv);
void key_task(void *pvParameters);
void ws2812_task(void *pvParameters);
void wifi_task(void *pvParameters);
esp_err_t wifi_set_ap_sta_enabled(bool ap_enable, bool sta_enable);
esp_err_t wifi_get_ap_sta_enabled(bool *ap_enable, bool *sta_enable);




#endif
