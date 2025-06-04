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
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#include "esp_mac.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "nvs_flash.h"


#include "all_control.h"

// #include "esp_http_server.h"

#include "lwip/lwip_napt.h"
#include "lwip/inet.h"

// #include "lwip/err.h"
// #include "lwip/sys.h"

// #include "mywifi.h"
// #include "var.h"
/* The examples use WiFi configuration that you can set via project configuration menu.

   If you'd rather not, just change the below entries to strings with
   the config you want - ie #define AP_WIFI_SSID "mywifissid"
*/
#define AP_ESP_WIFI_SSID      "test"
#define AP_ESP_WIFI_PASS      "12345678"
#define AP_ESP_WIFI_CHANNEL   7
#define AP_MAX_STA_CONN       10

#define TIMEOFF 3


static const char *TAG = "-wifi softAP-";


static TimerHandle_t xExampleTimer;
static uint8_t u8TimerRunEn = 1;
static uint8_t u8TimeMin = TIMEOFF;
static uint8_t u8TimeSec = 0;
static uint8_t u8Fg = 1;

static uint8_t u8APFg = 0;

/***************************************************************************************************
 * 功能描述: 
 * 输入参数: 
 * 输出参数: 
 * 返 回 值: 
 * 其它说明: 
***************************************************************************************************/

// httpd_handle_t start_webserver(void);

/***************************************************************************************************
 * 功能描述: 
 * 输入参数: 
 * 输出参数: 
 * 返 回 值: 
 * 其它说明: 
***************************************************************************************************/
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                    int32_t event_id, void* event_data)
{
    static tws2812Def tws2812Data={0};
    ESP_LOGI("----------", "event_base=%s, event_id=%ld", (char *)event_base, event_id);
    if (event_base == WIFI_EVENT)
    {
        if (event_id == WIFI_EVENT_AP_STACONNECTED) 
        {
            wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
            ESP_LOGI(TAG, "station "MACSTR" join, AID=%d",
                    MAC2STR(event->mac), event->aid);
            tws2812Data.r = 0;
            tws2812Data.g = 0;
            tws2812Data.b = 15;
            u8Fg = 2;
        } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) 
        {
            wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
            ESP_LOGI(TAG, "station "MACSTR" leave, AID=%d",
                    MAC2STR(event->mac), event->aid);
            tws2812Data.r = 15;
            tws2812Data.g = 10;
            tws2812Data.b = 0;
            u8TimeMin = TIMEOFF;  // 定时时间清除
            u8TimeSec = 0;  // 定时时间清除
            u8Fg = 0;
        }else if (event_id == WIFI_EVENT_AP_START)
        {
            ESP_LOGI(TAG, "station++++++++++++++++++++++++++++++++++++++++++++++++");
            ESP_LOGI(TAG, "AP 模式已开启");
            u8APFg = 1;
            tws2812Data.r = 0;
            tws2812Data.g = 15;
            tws2812Data.b = 0;
        }else if (event_id == WIFI_EVENT_AP_STOP)
        {
            ESP_LOGI(TAG, "station------------------------------------------------");
            ESP_LOGI(TAG, "AP 模式已停止");
            u8APFg = 0;
            tws2812Data.r = 15;
            tws2812Data.g = 0;
            tws2812Data.b = 0;
        }
        else if (event_id == WIFI_EVENT_SCAN_DONE) 
        {
            ESP_LOGI(TAG, "SAT 扫描完成");
            wifi_event_sta_scan_done_t* event = (wifi_event_sta_scan_done_t*) event_data;
            if (event->status == 0) 
            {
                ESP_LOGI(TAG, "扫描到 %d 个AP", event->number);
            } 
            else 
            {
                ESP_LOGI(TAG, "扫描AP失败");
            }
        }
        else if (event_id == WIFI_EVENT_STA_START) 
        {
            ESP_LOGI(TAG, "SAT START");
            // wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
            esp_wifi_connect();
        }
        else if (event_id == WIFI_EVENT_STA_CONNECTED) 
        {
            ESP_LOGI(TAG, "SAT 连接到AP");
        }
        else if (event_id == WIFI_EVENT_STA_DISCONNECTED)
        {
            ESP_LOGI(TAG, "SAT 断开连接到AP");
            // esp_wifi_connect();
        }
    }

    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) 
    {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        // s_retry_num = 0;
        // xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }

}

void wifi_sta_ap(void)
{

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_create_default_wifi_ap();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_config_t AP_wifi_config = {
        .ap = {
            .ssid = AP_ESP_WIFI_SSID,
            .ssid_len = strlen(AP_ESP_WIFI_SSID),
            .channel = AP_ESP_WIFI_CHANNEL,
            .password = AP_ESP_WIFI_PASS,
            .max_connection = AP_MAX_STA_CONN,
            .authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {
                    .required = true,
            },
        },
    };
    // if (strlen(AP_ESP_WIFI_PASS) == 0) {
    //     AP_wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    // }
    wifi_config_t SAT_wifi_config = {
        .sta = {
            .ssid = "天翼2.4G",
            .password = "66661111",
            .scan_method = WIFI_FAST_SCAN,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            // .sae_pwe_h2e = ESP_WIFI_SAE_MODE,
            // .sae_h2e_identifier = AP_H2E_IDENTIFIER,
            .failure_retry_cnt = 5,
        },

    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &SAT_wifi_config));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &AP_wifi_config));


    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_got_ip));






    ESP_ERROR_CHECK(esp_wifi_start());

    esp_netif_ip_info_t ip_info;
    esp_netif_get_ip_info(esp_netif_get_handle_from_ifkey("WIFI_AP_DEF"), &ip_info);

    char ip_addr[16];
    inet_ntoa_r(ip_info.ip.addr, ip_addr, 16);
    ESP_LOGI(TAG, "Set up softAP with IP: %s", ip_addr);

    ESP_LOGI(TAG, "wifi_init_softap finished. SSID:%s password:%s channel:%d",
             AP_ESP_WIFI_SSID, AP_ESP_WIFI_PASS, AP_ESP_WIFI_CHANNEL);

}


// 定时器回调函数
// void vTimerCallback(TimerHandle_t xTimer) {
//     static uint8_t u8TimerMinLast = 0;
//     static uint8_t r = 0;
//     static uint8_t add = 0;
//     while (true)
//     {

//         if(u8Fg == 2)
//         {
//             u8TimeMin = TIMEOFF;  // 定时时间清除
//             u8TimeSec = 0;  // 定时时间清除
//             goto delay;
//         }
//         if(u8Fg)
//         {
//             if (add)r++;else r--;

//             led_strip_hsv2rgb(r, 100, 3, &tws2812Data.r, &tws2812Data.g, &tws2812Data.b);
//             // xQueueSend(xQueueLed, &tws2812Data, 100);
//             if (r==255)add = 0;
//             if (r==0)add = 1;

//             // ESP_LOGI("vTimerCallback","r=%d",r);
//             vTaskDelay(16 / portTICK_PERIOD_MS);
//             u8TimeSec = 0;  // 定时时间清除
//             continue;
//         }


//         // ESP_LOGI("vTimerCallback","定时器触发!");
//         if (u8TimerRunEn == 0)
//         {
//             u8TimeMin = TIMEOFF;  // 定时时间清除
//             u8TimeSec = 0;  // 定时时间清除
//             goto delay;
//         }

//         if (u8TimeMin != u8TimerMinLast)
//         {
//             // 定时小时数有变化,分钟清零,秒钟清零
//             u8TimerMinLast = u8TimeMin;
//             u8TimeSec = 0;
//         }


//         if (u8TimeMin)
//         {
//             // 定时时间大于0，即定时模式开启
//             if (++u8TimeSec < 60)
//             {
//                 ESP_LOGI("vTimerCallback","u8TimeSec=%d",u8TimeSec);
//                 goto delay;
//             }
//             u8TimeSec = 0;

//             if (--u8TimeMin == 0) //test
//             {
//                 // return;
//                 // if (xQueueSend(xQueueLed, &tws2812Data, portMAX_DELAY) == pdTRUE)
//                 esp_wifi_stop();
//                 u8Fg = 1;
//                 // xTimerStop(xExampleTimer, 0);
//             }
//         }
//         // vTaskDelay(5000 / portTICK_PERIOD_MS);
//         delay:
//             vTaskDelay(1000 / portTICK_PERIOD_MS);
//     }
// }




void SendQueueTimer()
{
    // if (xQueueSend(xQueueLed, &tws2812Data, 2) == pdTRUE)
    // {
    //     // ESP_LOGI("SendQueue","-OK");
    //     // ESP_LOGI(TAG,"发送RGB: R=%d, G=%d, B=%d\n", tws2812Data.r, tws2812Data.g, tws2812Data.b);
    // }
    // else
    // {
    //     // ESP_LOGI("SendQueue","-timeout");
    // }
}

/***************************************************************************************************
 * 功能描述: 
 * 输入参数: 
 * 输出参数: 
 * 返 回 值: 
 * 其它说明: 
***************************************************************************************************/
void wifi_task(void *pvParameters)
{
    //Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // xExampleTimer = xTimerCreate(
    //     "ExampleTimer",        // 定时器名字
    //     pdMS_TO_TICKS(20),   // 定时器周期：1000ms
    //     pdTRUE,                // 自动重载
    //     (void *)0,             // 定时器ID
    //     SendQueueTimer         // 到期时回调函数
    // );


    // if (xExampleTimer != NULL) {
    //     // 启动定时器
    //     xTimerStart(xExampleTimer, 0);
    // }

    // xTaskCreate(vTimerCallback, "vTimerCallback", 1024*4, NULL, 5, NULL);
    // ESP_LOGI(TAG, "ESP_WIFI_MODE_AP");
    wifi_sta_ap();
    // start_webserver();

    while (1) 
    {
        // esp_wifi_stop();
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}
