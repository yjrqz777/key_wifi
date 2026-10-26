/***************************************************************************************************
 * Author: yjrqz777 3210551161@qq.com
 * Date: 2025-06-04 20:09:02
 * LastEditTime: 2026-03-08 19:00:01
 * LastEditors: yjrqz777 3210551161@qq.com
 * Description: 
 * FilePath: /key_wifi/components/all_control/src/mywifi.c
 * @YJRQZ777
***************************************************************************************************/
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/timers.h"
#include "freertos/event_groups.h"

#include "esp_mac.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "nvs_flash.h"


#include "all_control.h"

#include "esp_http_server.h"

#include "lwip/lwip_napt.h"
#include "lwip/inet.h"

#include "myusb.h"

// #include "lwip/err.h"
// #include "lwip/sys.h"

// #include "mywifi.h"
// #include "var.h"
/* The examples use WiFi configuration that you can set via project configuration menu.

   If you'd rather not, just change the below entries to strings with
   the config you want - ie #define AP_WIFI_SSID "mywifissid"
*/
#define AP_ESP_WIFI_SSID      "dtest"
#define AP_ESP_WIFI_PASS      "12345678"
#define AP_ESP_WIFI_CHANNEL   3
#define AP_MAX_STA_CONN       10

#define TIMEOFF 3


static const char *TAG = "wifi STA-AP";


static TimerHandle_t xStaTimeoutTimer;
static uint8_t u8TimerRunEn = 1;
static uint8_t u8TimeMin = TIMEOFF;
static uint8_t u8TimeSec = 0;
static uint8_t u8Fg = 1;


static struct 
{
    uint8_t u8ApSta;  // 0:AP模式,1:STA模式
    uint8_t u8ApStatus;  // 0:AP模式不激活,1:AP模式激活
    uint8_t u8StaStatus;  // 0:STA模式不激活,1:STA模式激活
}twifiData;

static wifi_config_t g_sta_wifi_config = {
    .sta = {
        .ssid = "STA_WIFI",
        .password = "12345678",
        .scan_method = WIFI_FAST_SCAN,
        .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        .failure_retry_cnt = 5,
    },
};


httpd_handle_t start_webserver(void);

static bool ap_credentials_valid(const char *ssid, const char *password)
{
    size_t ssid_len = strnlen(ssid, 33);
    size_t pass_len = strnlen(password, 65);

    if (ssid_len == 0 || ssid_len > 32) {
        return false;
    }
    if (!(pass_len == 0 || (pass_len >= 8 && pass_len <= 63))) {
        return false;
    }
    return true;
}

static wifi_mode_t wifi_mode_from_enable(bool ap_enable, bool sta_enable)
{
    if (ap_enable && sta_enable) {
        return WIFI_MODE_APSTA;
    }
    if (ap_enable) {
        return WIFI_MODE_AP;
    }
    return WIFI_MODE_STA;
}

esp_err_t wifi_get_ap_sta_enabled(bool *ap_enable, bool *sta_enable)
{
    if ((ap_enable == NULL) || (sta_enable == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }

    wifi_mode_t mode;
    esp_err_t err = esp_wifi_get_mode(&mode);
    if (err != ESP_OK) {
        return err;
    }

    *ap_enable = (mode == WIFI_MODE_AP || mode == WIFI_MODE_APSTA);
    *sta_enable = (mode == WIFI_MODE_STA || mode == WIFI_MODE_APSTA);
    return ESP_OK;
}

esp_err_t wifi_set_ap_sta_enabled(bool ap_enable, bool sta_enable)
{
    if (!ap_enable && !sta_enable) {
        return ESP_ERR_INVALID_ARG;
    }

    wifi_mode_t target_mode = wifi_mode_from_enable(ap_enable, sta_enable);
    wifi_mode_t current_mode;
    esp_err_t err = esp_wifi_get_mode(&current_mode);
    if (err != ESP_OK) {
        return err;
    }

    if (current_mode == target_mode) {
        if (sta_enable) {
            esp_wifi_connect();
        }
        return ESP_OK;
    }

    err = esp_wifi_stop();
    if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_STOPPED) {
        return err;
    }

    err = esp_wifi_set_mode(target_mode);
    if (err != ESP_OK) {
        return err;
    }
    if (sta_enable) {
        err = esp_wifi_set_config(WIFI_IF_STA, &g_sta_wifi_config);
        if (err != ESP_OK) {
            return err;
        }
    }
    err = esp_wifi_start();
    if (err != ESP_OK) {
        return err;
    }

    if (sta_enable) {
        esp_wifi_connect();
    }
    return ESP_OK;
}







/***************************************************************************************************
 * 功能描述: 
 * 输入参数: 
 * 输出参数: 
 * 返 回 值: 
 * 其它说明: 
***************************************************************************************************/
void wifi_button_handler(void)
{
    if (twifiData.u8ApStatus == 0)
    {
        esp_wifi_start();
    }
    else if (twifiData.u8ApStatus == 1)
    {
        esp_wifi_stop();
    }
}
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
    static uint8_t u8SendFg = 0;  // 是否发送数据到LED队列
    tws2812Def tws2812Data={0};
    ESP_LOGI(TAG, "event_base=%s, event_id=%ld", (char *)event_base, event_id);
    if (event_base == WIFI_EVENT)
    {
        if (event_id == WIFI_EVENT_AP_START)
        {
            twifiData.u8ApStatus = 1;
            ESP_LOGI(TAG, "AP START");
            tws2812Data.v = 0;
            tws2812Data.r = 0;
            tws2812Data.g = 15;
            tws2812Data.b = 0;
            u8SendFg = 1;
        }
        else if (event_id == WIFI_EVENT_AP_STOP)
        {
            twifiData.u8ApStatus = 0;
            ESP_LOGI(TAG, "AP STOP");
            tws2812Data.v = 0;
            tws2812Data.r = 15;
            tws2812Data.g = 0;
            tws2812Data.b = 0;
            u8SendFg = 1;
        }
        else if (event_id == WIFI_EVENT_AP_STACONNECTED) 
        {
            wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
            ESP_LOGI(TAG, "station "MACSTR" join, AID=%d",
                    MAC2STR(event->mac), event->aid);
            tws2812Data.v = 0;
            tws2812Data.r = 0;
            tws2812Data.g = 0;
            tws2812Data.b = 15;
            u8SendFg = 1;
        } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) 
        {
            wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
            ESP_LOGI(TAG, "station "MACSTR" leave, AID=%d",
                    MAC2STR(event->mac), event->aid);
            tws2812Data.v = 0;
            tws2812Data.r = 15;
            tws2812Data.g = 10;
            tws2812Data.b = 0;
            u8SendFg = 1;
        }
        else if (event_id == WIFI_EVENT_SCAN_DONE) 
        {
            ESP_LOGI(TAG, "SAT SCAN_DONE");
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
            twifiData.u8StaStatus = 1;
            ESP_LOGI(TAG, "SAT START");
            // wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
            esp_wifi_connect();
        }
        else if (event_id == WIFI_EVENT_STA_STOP) 
        {
            twifiData.u8StaStatus = 0;
            ESP_LOGI(TAG, "SAT STOP");
        }
        else if (event_id == WIFI_EVENT_STA_CONNECTED) 
        {
            twifiData.u8StaStatus = 2;
            ESP_LOGI(TAG, "SAT CONNECTED");
        }
        else if (event_id == WIFI_EVENT_STA_DISCONNECTED)
        {
            twifiData.u8StaStatus = 1;
            ESP_LOGI(TAG, "SAT DISCONNECTED");
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
    if (u8SendFg == 1)
    {
        u8SendFg = 0;  // 清除发送标志
        if (xQueueSend(xQueueLed, &tws2812Data, portMAX_DELAY) == pdTRUE) 
        {
            // printf("发送RGB: R=%d, G=%d, B=%d\n", AllQueue.r, AllQueue.g, AllQueue.b);
        }
    }
}
/***************************************************************************************************
 * 功能描述: 
 * 输入参数: 
 * 输出参数: 
 * 返 回 值: 
 * 其它说明: 
***************************************************************************************************/
void wifi_sta_ap(void)
{
    char u8ssid[33] = {0};        // SSID 最大 32 字符 + NULL 终止符
    char u8password[65] = {0};    // 密码最大 64 字符 + NULL 终止符

    wifi_config_t AP_wifi_config = {
        .ap = {
            .ssid = AP_ESP_WIFI_SSID,
            .ssid_len = strlen(AP_ESP_WIFI_SSID),
            .channel = AP_ESP_WIFI_CHANNEL,
            .password = AP_ESP_WIFI_PASS,
            .max_connection = AP_MAX_STA_CONN,
            .authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {
                    .required = false,
                    .capable = true,
            },
        },
    };
    // if (strlen(AP_ESP_WIFI_PASS) == 0) {
    //     AP_wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    // }

    // if(1 == read_wifi_credentials(u8ssid, u8password))
    // {
    //     if (ap_credentials_valid(u8ssid, u8password)) {
    //         size_t ssid_len = strnlen(u8ssid, sizeof(u8ssid));
    //         size_t pass_len = strnlen(u8password, sizeof(u8password));

    //         memset(AP_wifi_config.ap.ssid, 0, sizeof(AP_wifi_config.ap.ssid));
    //         memset(AP_wifi_config.ap.password, 0, sizeof(AP_wifi_config.ap.password));
    //         memcpy(AP_wifi_config.ap.ssid, u8ssid, ssid_len);
    //         memcpy(AP_wifi_config.ap.password, u8password, pass_len);
    //         AP_wifi_config.ap.ssid_len = ssid_len;

    //         if (pass_len == 0) {
    //             AP_wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    //         } else {
    //             AP_wifi_config.ap.authmode = WIFI_AUTH_WPA_WPA2_PSK;
    //         }
    //         ESP_LOGI(TAG, "Use saved AP credentials");
    //     } else {
    //         ESP_LOGW(TAG, "Saved AP credentials invalid, fallback to default");
    //     }
    // }


    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_create_default_wifi_ap();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));


    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    // AP only at boot, STA config is set only when STA is enabled.
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

    ESP_LOGI(TAG, "wifi_init_softap finished. SSID:%s password:%s channel:%d authmode:%d",
             AP_wifi_config.ap.ssid, AP_wifi_config.ap.password, AP_wifi_config.ap.channel, AP_wifi_config.ap.authmode);

}

/***************************************************************************************************
 * 功能描述: 
 * 输入参数: 
 * 输出参数: 
 * 返 回 值: 
 * 其它说明: 
***************************************************************************************************/
void StaTimeout()
{
    static uint16_t u16TimeCount = 0;
    if (++ u16TimeCount <= 3*60) // 每秒执行一次
    {
        // ESP_LOGI(TAG, "u16TimeCount %d", u16TimeCount);
        return;
    }
    ESP_LOGI(TAG, "u16TimeCount %d", u16TimeCount);
    u16TimeCount = 0; // 重置计数器
    // 1. 获取当前Wi-Fi配置 (用于保留STA配置)
    wifi_config_t sta_config;
    wifi_config_t ap_config;
    esp_wifi_get_config(WIFI_IF_STA, &sta_config); // 仅获取STA配置备用
    esp_wifi_get_config(WIFI_IF_AP, &ap_config); // 获取AP配置 (可选，如果你不需要保留AP配置则可不做)

    ESP_LOGI(TAG, "STA SSID: %s, Password: %s", 
             sta_config.sta.ssid, sta_config.sta.password);
    ESP_LOGI(TAG, "AP SSID: %s, Password: %s",
                ap_config.ap.ssid, ap_config.ap.password);
    // 2. 停止Wi-Fi
    esp_wifi_stop();

    esp_wifi_set_mode(WIFI_MODE_AP); // 关键：切换回APSTA模式
    esp_wifi_set_config(WIFI_IF_AP, &ap_config);   // 设置AP配置

    // 3. 重新启动Wi-Fi
    esp_wifi_start();
    xTimerStop(xStaTimeoutTimer, 0);

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


    // xStaTimeoutTimer = xTimerCreate(
    //     "xStaTimeoutTimer",        // 定时器名字
    //     pdMS_TO_TICKS(1000),   // 定时器周期：1000ms
    //     pdTRUE,                // 自动重载
    //     (void *)0,             // 定时器ID
    //     StaTimeout         // 到期时回调函数
    // );


    // if (xStaTimeoutTimer != NULL) {
    //     // 启动定时器
    //     xTimerStart(xStaTimeoutTimer, 0);
    // }
    xStaTimeoutTimer = NULL;
    // // 当需要重置定时器时
    // if (xTimerReset(xStaTimeoutTimer, portMAX_DELAY) != pdPASS) {
    //     // 处理失败情况
    // }
    // xTaskCreate(vTimerCallback, "vTimerCallback", 1024*4, NULL, 5, NULL);
    // ESP_LOGI(TAG, "ESP_WIFI_MODE_AP");
    wifi_sta_ap();
    start_webserver();

    while (1) 
    {
        // esp_wifi_stop();
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}
