/**
 * @file ws2812.h
 * @brief WS2812 状态灯组件的公共接口：灯光消息结构、初始化和发送接口。
 * @details 队列由本组件持有，调用方只通过 ws2812_send() 投递颜色消息，
 *          不直接接触 FreeRTOS 队列句柄。
 */
#ifndef WS2812_H
#define WS2812_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

/**
 * @brief WS2812 灯光控制消息，经内部队列在发送方与 ws2812_task 之间传递。
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

/**
 * @brief 创建灯光消息队列，必须在任何 ws2812_send() 之前调用。
 * @retval ESP_OK 队列已就绪，重复调用直接返回 ESP_OK。
 * @retval ESP_ERR_NO_MEM 队列创建失败。
 */
esp_err_t ws2812_init(void);

/**
 * @brief 投递一条灯光消息。
 * @param[in] data 待发送的消息，函数内部按值复制到队列。
 * @param[in] wait_ticks 队列满时的等待 tick 数，portMAX_DELAY 表示一直等待。
 * @retval true 消息已入队。
 * @retval false 未初始化、参数为空或等待超时。
 */
bool ws2812_send(const tws2812Def *data, TickType_t wait_ticks);

/**
 * @brief WS2812 任务入口：初始化 RMT 通道与编码器，循环刷新灯珠颜色。
 * @param[in] pvParameters 未使用的 FreeRTOS 任务参数。
 */
void ws2812_task(void *pvParameters);

#endif // WS2812_H
