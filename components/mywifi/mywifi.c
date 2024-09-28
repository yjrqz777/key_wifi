/*  WiFi softAP Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/timers.h"

#include "esp_mac.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "lwip/err.h"
#include "lwip/sys.h"

#include "mywifi.h"
#include "var.h"
/* The examples use WiFi configuration that you can set via project configuration menu.

   If you'd rather not, just change the below entries to strings with
   the config you want - ie #define EXAMPLE_WIFI_SSID "mywifissid"
*/
#define EXAMPLE_ESP_WIFI_SSID      "test"
#define EXAMPLE_ESP_WIFI_PASS      "12345678"
#define EXAMPLE_ESP_WIFI_CHANNEL   7
#define EXAMPLE_MAX_STA_CONN       10
#define ESP_WIFI_SOFTAP_SAE_SUPPORT 0

#define TIMEOFF 3


static const char *TAG = "wifi softAP";


static TimerHandle_t xExampleTimer;
static uint8_t u8TimerRunEn = 1;
static uint8_t u8TimeMin = TIMEOFF;
static uint8_t u8TimeSec = 0;
static uint8_t u8Fg = 1;

static uint8_t u8APFg = 0;

static tRgbKeyDef AllQueue={0, 0, 0, 0};



static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                    int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_START) {
        ESP_LOGI(TAG, "AP 模式已开启");
        u8APFg = 1;
    }
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STOP) {
        ESP_LOGI(TAG, "AP 模式已停止");
        u8APFg = 0;
    }
    if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
        ESP_LOGI(TAG, "station "MACSTR" join, AID=%d",
                 MAC2STR(event->mac), event->aid);
        AllQueue.r = 0;
        AllQueue.g = 0;
        AllQueue.b = 15;
        u8Fg = 2;
    } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
        ESP_LOGI(TAG, "station "MACSTR" leave, AID=%d",
                 MAC2STR(event->mac), event->aid);
        AllQueue.r = 15;
        AllQueue.g = 10;
        AllQueue.b = 0;
        u8TimeMin = TIMEOFF;  // 定时时间清除
        u8TimeSec = 0;  // 定时时间清除
        u8Fg = 0;
    }else if (event_id == WIFI_EVENT_AP_START)
    {
        ESP_LOGI(TAG, "station++++++++++++++++++++++++++++++++++++++++++++++++");
        AllQueue.r = 0;
        AllQueue.g = 15;
        AllQueue.b = 0;
    }else if (event_id == WIFI_EVENT_AP_STOP)
    {
        ESP_LOGI(TAG, "station------------------------------------------------");
        AllQueue.r = 15;
        AllQueue.g = 0;
        AllQueue.b = 0;
    }
    // if (AllQueue.r || AllQueue.g || AllQueue.b)
    // {
    //     if (xQueueSend(xQueueLed, &AllQueue, portMAX_DELAY) == pdTRUE)
    //     {
    //         ESP_LOGI(TAG,"发送RGB: R=%d, G=%d, B=%d\n", AllQueue.r, AllQueue.g, AllQueue.b);
    //     }
    // }
    

}

void wifi_init_softap(void)
{
    static uint8_t FG = 0;
    tRgbKeyDef rAllQueue;
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = EXAMPLE_ESP_WIFI_SSID,
            .ssid_len = strlen(EXAMPLE_ESP_WIFI_SSID),
            .channel = EXAMPLE_ESP_WIFI_CHANNEL,
            .password = EXAMPLE_ESP_WIFI_PASS,
            .max_connection = EXAMPLE_MAX_STA_CONN,
// #ifdef ESP_WIFI_SOFTAP_SAE_SUPPORT
//             .authmode = WIFI_AUTH_WPA3_PSK,
//             .sae_pwe_h2e = WPA3_SAE_PWE_BOTH,
// #else /* CONFIG_ESP_WIFI_SOFTAP_SAE_SUPPORT */
            .authmode = WIFI_AUTH_WPA2_PSK,
// #endif
            .pmf_cfg = {
                    .required = true,
            },
        },
    };
    if (strlen(EXAMPLE_ESP_WIFI_PASS) == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    // ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "wifi_init_softap finished. SSID:%s password:%s channel:%d",
             EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS, EXAMPLE_ESP_WIFI_CHANNEL);
    while (1) 
    {

        if (xQueueReceive(xQueueKey, &rAllQueue, portMAX_DELAY) == pdTRUE);
        if(rAllQueue.k == 1 && FG == 0)
        {
            if (u8APFg)
                esp_wifi_stop();
            else 
                esp_wifi_start();
            FG = 1;
            u8TimeMin = TIMEOFF;  // 定时时间清除
            u8TimeSec = 0;  // 定时时间清除
            u8Fg = 0;
        }
        else if(rAllQueue.k == 0 && FG == 1)
        {
            if (u8APFg)
                esp_wifi_stop();
            else 
                esp_wifi_start();
            FG = 0;
            u8TimeMin = TIMEOFF;  // 定时时间清除
            u8TimeSec = 0;  // 定时时间清除
            u8Fg = 0;
        }
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
}


//HSV模型表示色相（Hue）、饱和度（Saturation）和亮度（Value）
static void led_strip_hsv2rgb(uint32_t h, uint32_t s, uint32_t v, uint32_t *r, uint32_t *g, uint32_t *b)
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


// 定时器回调函数
void vTimerCallback(TimerHandle_t xTimer) {
    static uint8_t u8TimerMinLast = 0;
    static uint8_t r = 0;
    static uint8_t add = 0;
    while (true)
    {
        if(u8Fg == 2)
        {
            u8TimeMin = TIMEOFF;  // 定时时间清除
            u8TimeSec = 0;  // 定时时间清除
            goto delay;
        }
        if(u8Fg)
        {
            if (add)r++;else r--;

            led_strip_hsv2rgb(r, 100, 3, &AllQueue.r, &AllQueue.g, &AllQueue.b);

            if (r==255)add = 0;
            if (r==0)add = 1;

            // ESP_LOGI("vTimerCallback","r=%d",r);
            vTaskDelay(16 / portTICK_PERIOD_MS);
            u8TimeSec = 0;  // 定时时间清除
            continue;
        }


        // ESP_LOGI("vTimerCallback","定时器触发!");
        if (u8TimerRunEn == 0)
        {
            u8TimeMin = TIMEOFF;  // 定时时间清除
            u8TimeSec = 0;  // 定时时间清除
            goto delay;
        }

        if (u8TimeMin != u8TimerMinLast)
        {
            // 定时小时数有变化,分钟清零,秒钟清零
            u8TimerMinLast = u8TimeMin;
            u8TimeSec = 0;
        }


        if (u8TimeMin)
        {
            // 定时时间大于0，即定时模式开启
            if (++u8TimeSec < 60)
            {
                ESP_LOGI("vTimerCallback","u8TimeSec=%d",u8TimeSec);
                goto delay;
            }
            u8TimeSec = 0;

            if (--u8TimeMin == 0) //test
            {
                // return;
                // if (xQueueSend(xQueueLed, &AllQueue, portMAX_DELAY) == pdTRUE)
                esp_wifi_stop();
                u8Fg = 1;
                // xTimerStop(xExampleTimer, 0);
            }
        }
        // vTaskDelay(5000 / portTICK_PERIOD_MS);
        delay:
            vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}




void SendQueueTimer()
{
    if (xQueueSend(xQueueLed, &AllQueue, 2) == pdTRUE)
    {
        // ESP_LOGI("SendQueue","-OK");
        // ESP_LOGI(TAG,"发送RGB: R=%d, G=%d, B=%d\n", AllQueue.r, AllQueue.g, AllQueue.b);
    }
    else
    {
        // ESP_LOGI("SendQueue","-timeout");
    }
}



void wifi_task()
{
    //Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // vTimerCallback(NULL);

    xExampleTimer = xTimerCreate(
        "ExampleTimer",        // 定时器名字
        pdMS_TO_TICKS(20),   // 定时器周期：1000ms
        pdTRUE,                // 自动重载
        (void *)0,             // 定时器ID
        SendQueueTimer         // 到期时回调函数
    );


    if (xExampleTimer != NULL) {
        // 启动定时器
        xTimerStart(xExampleTimer, 0);
    }
    xTaskCreate(vTimerCallback, "vTimerCallback", 1024*4, NULL, 5, NULL);
    ESP_LOGI(TAG, "ESP_WIFI_MODE_AP");
    wifi_init_softap();


    while (1) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}
