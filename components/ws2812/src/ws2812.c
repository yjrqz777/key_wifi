/*
 * SPDX-FileCopyrightText: 2021-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file ws2812.c
 * @brief 通过 RMT 外设驱动板载 WS2812 状态灯。
 * @details 自定义 RMT 编码器产生 WS2812 的 0/1 码和 50us 复位码；ws2812_task 从 xQueueLed
 *          接收 HSV/RGB 消息刷新灯珠，队列空闲时按固定步进自动做彩虹渐变。
 */
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_check.h"
#include "esp_log.h"
#include "driver/rmt_tx.h"
#include "driver/rmt_encoder.h"

#include "ws2812.h"

#define RMT_LED_STRIP_RESOLUTION_HZ 10000000 /* RMT 分辨率 10MHz，1 tick = 0.1us，WS2812 需要高分辨率 */
#define RMT_LED_STRIP_GPIO_NUM      48       /* WS2812 数据线所接的 GPIO 编号 */

#define EXAMPLE_LED_NUMBERS         1        /* 级联的 WS2812 灯珠数量 */
#define EXAMPLE_CHASE_SPEED_MS      20       /* 主循环步进周期，单位毫秒 */
#define LED_QUEUE_LENGTH            5        /* 颜色消息队列深度，由 ws2812_init() 创建 */
static uint8_t led_strip_pixels[EXAMPLE_LED_NUMBERS * 3];
static const char *TAG = "led_encoder";

/* 组件内部持有的颜色消息队列，只通过 ws2812_send() 写入。 */
static QueueHandle_t xQueueLed;

/**
 * @brief RMT LED 灯带编码器的配置参数。
 */
typedef struct {
    uint32_t resolution; /**< 编码器分辨率，单位 Hz */
} led_strip_encoder_config_t;

/**
 * @brief 创建颜色消息队列。
 * @retval ESP_OK 队列已就绪，重复调用直接返回 ESP_OK。
 * @retval ESP_ERR_NO_MEM 队列创建失败。
 */
esp_err_t ws2812_init(void)
{
    if (xQueueLed != NULL) {
        return ESP_OK;
    }
    xQueueLed = xQueueCreate(LED_QUEUE_LENGTH, sizeof(tws2812Def));
    return (xQueueLed != NULL) ? ESP_OK : ESP_ERR_NO_MEM;
}

/**
 * @brief 投递一条灯光消息。
 * @param[in] data 待发送的消息，函数内部按值复制到队列。
 * @param[in] wait_ticks 队列满时的等待 tick 数。
 * @retval true 消息已入队。
 * @retval false 未初始化、参数为空或等待超时。
 */
bool ws2812_send(const tws2812Def *data, TickType_t wait_ticks)
{
    if (xQueueLed == NULL || data == NULL) {
        return false;
    }
    return xQueueSend(xQueueLed, data, wait_ticks) == pdTRUE;
}





/**
 * @brief WS2812 专用的 RMT 编码器：先输出 RGB 数据，再输出复位码。
 */
typedef struct {
    rmt_encoder_t base;             /**< 编码器基类，必须是结构体的首成员，供容器转换使用 */
    rmt_encoder_t *bytes_encoder;   /**< 把 RGB 字节编码成 RMT 符号的字节编码器 */
    rmt_encoder_t *copy_encoder;    /**< 原样输出复位码的拷贝编码器 */
    int state;                      /**< 编码状态机，0 表示发送 RGB 数据，1 表示发送复位码 */
    rmt_symbol_word_t reset_code;   /**< 复位码符号，由两个电平 0 的半周期符号组成 */
} rmt_led_strip_encoder_t;

/**
 * @brief 编码一次完整传输：RGB 数据加复位码。
 * @param[in] encoder 编码器基类指针，内部通过 __containerof 还原为完整结构体。
 * @param[in] channel 目标 RMT 发送通道。
 * @param[in] primary_data 待发送的原始 RGB 字节缓冲区。
 * @param[in] data_size 原始缓冲区长度，单位字节。
 * @param[out] ret_state 本次编码状态，返回 RMT_ENCODING_COMPLETE 表示整帧发送完成。
 * @return 本次写入 RMT 的符号数量。
 * @note 状态 0 完成后直接落入状态 1，只有复位码也发送完成才置位 RMT_ENCODING_COMPLETE。
 * @note RMT 内存已满时返回 RMT_ENCODING_MEM_FULL，调用方需要再次调用本函数续发剩余部分。
 */
static size_t rmt_encode_led_strip(rmt_encoder_t *encoder, rmt_channel_handle_t channel, const void *primary_data, size_t data_size, rmt_encode_state_t *ret_state)
{
    rmt_led_strip_encoder_t *led_encoder = __containerof(encoder, rmt_led_strip_encoder_t, base);
    rmt_encoder_handle_t bytes_encoder = led_encoder->bytes_encoder;
    rmt_encoder_handle_t copy_encoder = led_encoder->copy_encoder;
    rmt_encode_state_t session_state = RMT_ENCODING_RESET;
    rmt_encode_state_t state = RMT_ENCODING_RESET;
    size_t encoded_symbols = 0;
    switch (led_encoder->state) {
    case 0: // send RGB data
        encoded_symbols += bytes_encoder->encode(bytes_encoder, channel, primary_data, data_size, &session_state);
        if (session_state & RMT_ENCODING_COMPLETE) {
            led_encoder->state = 1; // switch to next state when current encoding session finished
        }
        if (session_state & RMT_ENCODING_MEM_FULL) {
            state |= RMT_ENCODING_MEM_FULL;
            goto out; // yield if there's no free space for encoding artifacts
        }
    // fall-through
    case 1: // send reset code
        encoded_symbols += copy_encoder->encode(copy_encoder, channel, &led_encoder->reset_code,
                                                sizeof(led_encoder->reset_code), &session_state);
        if (session_state & RMT_ENCODING_COMPLETE) {
            led_encoder->state = RMT_ENCODING_RESET; // back to the initial encoding session
            state |= RMT_ENCODING_COMPLETE;
        }
        if (session_state & RMT_ENCODING_MEM_FULL) {
            state |= RMT_ENCODING_MEM_FULL;
            goto out; // yield if there's no free space for encoding artifacts
        }
    }
out:
    *ret_state = state;
    return encoded_symbols;
}

static esp_err_t rmt_del_led_strip_encoder(rmt_encoder_t *encoder)
{
    rmt_led_strip_encoder_t *led_encoder = __containerof(encoder, rmt_led_strip_encoder_t, base);
    rmt_del_encoder(led_encoder->bytes_encoder);
    rmt_del_encoder(led_encoder->copy_encoder);
    free(led_encoder);
    return ESP_OK;
}

static esp_err_t rmt_led_strip_encoder_reset(rmt_encoder_t *encoder)
{
    rmt_led_strip_encoder_t *led_encoder = __containerof(encoder, rmt_led_strip_encoder_t, base);
    rmt_encoder_reset(led_encoder->bytes_encoder);
    rmt_encoder_reset(led_encoder->copy_encoder);
    led_encoder->state = RMT_ENCODING_RESET;
    return ESP_OK;
}

/**
 * @brief 创建 WS2812 灯带使用的 RMT 编码器。
 * @param[in] config 编码器配置，主要提供 RMT 时钟分辨率，用于换算时序 tick 数。
 * @param[out] ret_encoder 成功时写入编码器句柄，失败时不写入。
 * @retval ESP_OK 编码器创建成功。
 * @retval ESP_ERR_INVALID_ARG config 或 ret_encoder 为空，或 RMT 底层编码器参数非法。
 * @retval ESP_ERR_NO_MEM 编码器结构体或内部编码器申请内存失败。
 * @note 内部会额外创建字节编码器和拷贝编码器，任一创建失败时会释放已申请的资源。
 */
esp_err_t rmt_new_led_strip_encoder(const led_strip_encoder_config_t *config, rmt_encoder_handle_t *ret_encoder)
{
    esp_err_t ret = ESP_OK;
    rmt_led_strip_encoder_t *led_encoder = NULL;
    ESP_GOTO_ON_FALSE(config && ret_encoder, ESP_ERR_INVALID_ARG, err, TAG, "invalid argument");
    led_encoder = calloc(1, sizeof(rmt_led_strip_encoder_t));
    ESP_GOTO_ON_FALSE(led_encoder, ESP_ERR_NO_MEM, err, TAG, "no mem for led strip encoder");
    led_encoder->base.encode = rmt_encode_led_strip;
    led_encoder->base.del = rmt_del_led_strip_encoder;
    led_encoder->base.reset = rmt_led_strip_encoder_reset;
    // different led strip might have its own timing requirements, following parameter is for WS2812
    rmt_bytes_encoder_config_t bytes_encoder_config = {
        .bit0 = {
            .level0 = 1,
            .duration0 = 0.3 * config->resolution / 1000000, // T0H=0.3us
            .level1 = 0,
            .duration1 = 0.9 * config->resolution / 1000000, // T0L=0.9us
        },
        .bit1 = {
            .level0 = 1,
            .duration0 = 0.9 * config->resolution / 1000000, // T1H=0.9us
            .level1 = 0,
            .duration1 = 0.3 * config->resolution / 1000000, // T1L=0.3us
        },
        .flags.msb_first = 1 // WS2812 transfer bit order: G7...G0R7...R0B7...B0
    };
    ESP_GOTO_ON_ERROR(rmt_new_bytes_encoder(&bytes_encoder_config, &led_encoder->bytes_encoder), err, TAG, "create bytes encoder failed");
    rmt_copy_encoder_config_t copy_encoder_config = {};
    ESP_GOTO_ON_ERROR(rmt_new_copy_encoder(&copy_encoder_config, &led_encoder->copy_encoder), err, TAG, "create copy encoder failed");

    uint32_t reset_ticks = config->resolution / 1000000 * 50 / 2; // reset code duration defaults to 50us
    led_encoder->reset_code = (rmt_symbol_word_t) {
        .level0 = 0,
        .duration0 = reset_ticks,
        .level1 = 0,
        .duration1 = reset_ticks,
    };
    *ret_encoder = &led_encoder->base;
    return ESP_OK;
err:
    if (led_encoder) {
        if (led_encoder->bytes_encoder) {
            rmt_del_encoder(led_encoder->bytes_encoder);
        }
        if (led_encoder->copy_encoder) {
            rmt_del_encoder(led_encoder->copy_encoder);
        }
        free(led_encoder);
    }
    return ret;
}

/**
 * @brief 把 HSV 颜色空间换算成 RGB 颜色空间。
 * @details 公式参考 https://en.wikipedia.org/wiki/HSL_and_HSV ；
 *          HSV 模型分别表示色相（Hue）、饱和度（Saturation）和亮度（Value）。
 * @param[in] h 色相，单位为度，内部按 360 取模。
 * @param[in] s 饱和度，取值 0 到 100。
 * @param[in] v 亮度，取值 0 到 100。
 * @param[out] r 换算得到的红色分量，取值 0 到 255。
 * @param[out] g 换算得到的绿色分量，取值 0 到 255。
 * @param[out] b 换算得到的蓝色分量，取值 0 到 255。
 */
void led_strip_hsv2rgb(uint32_t h, uint32_t s, uint32_t v, uint32_t *r, uint32_t *g, uint32_t *b)
{
    h %= 360; // h -> [0,360]
    uint32_t rgb_max = v * 2.55f;
    uint32_t rgb_min = rgb_max * (100 - s) / 100.0f;

    uint32_t i = h / 60;
    uint32_t diff = h % 60;

    // RGB adjustment amount by hue
    uint32_t rgb_adj = (rgb_max - rgb_min) * diff / 60;

    switch (i) {
    case 0:
        *r = rgb_max;
        *g = rgb_min + rgb_adj;
        *b = rgb_min;
        break;
    case 1:
        *r = rgb_max - rgb_adj;
        *g = rgb_max;
        *b = rgb_min;
        break;
    case 2:
        *r = rgb_min;
        *g = rgb_max;
        *b = rgb_min + rgb_adj;
        break;
    case 3:
        *r = rgb_min;
        *g = rgb_max - rgb_adj;
        *b = rgb_max;
        break;
    case 4:
        *r = rgb_min + rgb_adj;
        *g = rgb_min;
        *b = rgb_max;
        break;
    default:
        *r = rgb_max;
        *g = rgb_min;
        *b = rgb_max - rgb_adj;
        break;
    }
}
/**
 * @brief WS2812 任务入口：初始化 RMT 通道与编码器，循环刷新灯珠颜色。
 * @param[in] pvParameters 未使用的 FreeRTOS 任务参数。
 * @note 队列无消息时按 20ms 步进累加色相，做自动彩虹渐变；收到消息时直接使用消息里的 HSV 或 RGB。
 * @warning RMT 初始化与发送结果都用 ESP_ERROR_CHECK 处理，任一调用失败会直接中止程序。
 */
void ws2812_task(void *pvParameters)
{
    // uint8_t red = 0;
    // uint8_t green = 0;
    // uint8_t blue = 0;
    uint32_t u32timecount = 0;
    tws2812Def tws2812Data = {100, 100, 3, 0, 0, 0, 0};
    ESP_LOGI(TAG, "Create RMT TX channel");
    rmt_channel_handle_t led_chan = NULL;
    rmt_tx_channel_config_t tx_chan_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT, // select source clock
        .gpio_num = RMT_LED_STRIP_GPIO_NUM,
        .mem_block_symbols = 48, // one WS2812 LED only needs 24 data symbols plus reset
        .resolution_hz = RMT_LED_STRIP_RESOLUTION_HZ,
        .trans_queue_depth = 1, // keep only one pending transaction for the status LED
    };
    ESP_ERROR_CHECK(rmt_new_tx_channel(&tx_chan_config, &led_chan));

    ESP_LOGI(TAG, "Install led strip encoder");
    rmt_encoder_handle_t led_encoder = NULL;
    led_strip_encoder_config_t encoder_config = {
        .resolution = RMT_LED_STRIP_RESOLUTION_HZ,
    };
    ESP_ERROR_CHECK(rmt_new_led_strip_encoder(&encoder_config, &led_encoder));

    ESP_LOGI(TAG, "Enable RMT TX channel");
    ESP_ERROR_CHECK(rmt_enable(led_chan));

    ESP_LOGI(TAG, "Start LED rainbow chase");
    rmt_transmit_config_t tx_config = {
        .loop_count = 0, // no transfer loop
    };
    while (1) {
            bool need_update = false;
            if (xQueueReceive(xQueueLed, &tws2812Data, pdMS_TO_TICKS(5)) == pdTRUE)
            {
                u32timecount = tws2812Data.u32time;
                need_update = true;
                // red = tws2812Data.r;
                // green = tws2812Data.g;
                // blue = tws2812Data.b;
                ESP_LOGI(TAG,"RecRGB: H=%ld, S=%ld, V=%ld, R=%d, G=%d, B=%d, time=%ld\n", tws2812Data.h, tws2812Data.s, tws2812Data.v,tws2812Data.r, tws2812Data.g, tws2812Data.b, tws2812Data.u32time);
            }
            else
            {
                /* 队列空闲时累加步进计数，约 10 分钟后才让色相自动递增，形成彩虹渐变。 */
                if (++u32timecount > 10*60*1000 / EXAMPLE_CHASE_SPEED_MS)
                {
                    u32timecount = 10*60*1000 / EXAMPLE_CHASE_SPEED_MS;
                    tws2812Data.h++;
                    need_update = true;
                }
            }

            if (need_update) {
                if (tws2812Data.v != 0)
                {
                    uint32_t r = 0;
                    uint32_t g = 0;
                    uint32_t b = 0;
                    led_strip_hsv2rgb(tws2812Data.h, tws2812Data.s, tws2812Data.v, &r, &g, &b);
                    tws2812Data.r = r;
                    tws2812Data.g = g;
                    tws2812Data.b = b;
                }

                // ESP_LOGI(TAG,"RGB: R=%d, G=%d, B=%d\n", red, green, blue);
                /* WS2812 要求按 GRB 顺序发送，因此缓冲区依次填入 G、R、B。 */
                led_strip_pixels[0] = tws2812Data.g;
                led_strip_pixels[1] = tws2812Data.r;
                led_strip_pixels[2] = tws2812Data.b;
                // Flush RGB values to LEDs
                ESP_ERROR_CHECK(rmt_transmit(led_chan, led_encoder, led_strip_pixels, sizeof(led_strip_pixels), &tx_config));
                ESP_ERROR_CHECK(rmt_tx_wait_all_done(led_chan, portMAX_DELAY));
            }
            vTaskDelay(pdMS_TO_TICKS(EXAMPLE_CHASE_SPEED_MS));
            // memset(led_strip_pixels, 0, sizeof(led_strip_pixels));
            // ESP_ERROR_CHECK(rmt_transmit(led_chan, led_encoder, led_strip_pixels, sizeof(led_strip_pixels), &tx_config));
            // ESP_ERROR_CHECK(rmt_tx_wait_all_done(led_chan, portMAX_DELAY));
            // vTaskDelay(pdMS_TO_TICKS(EXAMPLE_CHASE_SPEED_MS));
    }
}

