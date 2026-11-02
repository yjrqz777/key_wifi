/***************************************************************************************************
 * Author: yjrqz777 3210551161@qq.com
 * Date: 2025-06-04 20:09:02
 * LastEditTime: 2026-03-08 19:00:01
 * LastEditors: yjrqz777 3210551161@qq.com
 * Description: 
 * FilePath: /key_wifi/components/all_control/src/mywifi.c
 * @YJRQZ777
***************************************************************************************************/
/**
 * @file mywifi.c
 * @brief 初始化 SoftAP 网络，维护 AP/STA 模式状态并把 Wi-Fi 事件映射到状态灯。
 * @details 开机固定以 AP 模式启动，STA 配置只在启用 STA 时下发；网页端通过
 *          wifi_set_ap_sta_enabled() 切换模式，事件回调把连接状态转成 WS2812
 *          颜色消息投递到 xQueueLed。
 */
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
#define AP_ESP_WIFI_SSID      "dtest"      /* 默认热点名称 */
#define AP_ESP_WIFI_PASS      "12345678"   /* 默认热点密码，长度为 8 到 63 个字符 */
#define AP_ESP_WIFI_CHANNEL   3            /* 热点固定使用的信道 */
#define AP_MAX_STA_CONN       10           /* 热点允许接入的最大 STA 数 */

#define TIMEOFF 3   /* STA 超时时间的分钟数，由 StaTimeout() 按秒累计 */


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

/* STA 模式使用的默认上级热点配置，仅在启用 STA 时写入 WIFI_IF_STA。 */
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

/**
 * @brief 校验 AP 的 SSID 与密码长度是否符合 Wi-Fi 驱动要求。
 * @param[in] ssid 待校验的热点名称。
 * @param[in] password 待校验的热点密码，允许为空表示开放热点。
 * @retval true 长度合法。
 * @retval false SSID 为空或超过 32 个字符，或密码长度不为 0 且不在 8 到 63 个字符之间。
 * @note 读取上限固定为 33 与 65 字节，调用方需保证缓冲区至少有这么长或提前以 NUL 结尾。
 */
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

/*
 * 将 AP/STA 使能组合映射成 esp_wifi 工作模式。
 * 两者均为 false 时返回 WIFI_MODE_STA，调用方已经在入口处拦截该组合。
 */
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

/**
 * @brief 查询当前 Wi-Fi 工作模式，换算成 AP 与 STA 的使能状态。
 * @param[out] ap_enable 输出 AP 是否启用。
 * @param[out] sta_enable 输出 STA 是否启用。
 * @retval ESP_OK 查询成功，两个输出参数已写入。
 * @retval ESP_ERR_INVALID_ARG 输出参数为空指针。
 * @retval 其他错误码 直接透传 esp_wifi_get_mode() 的返回值。
 * @note 结果来自驱动的实时工作模式，不是本文件缓存的 twifiData。
 */
esp_err_t wifi_get_ap_sta_enabled(bool *ap_enable, bool *sta_enable)
{
    if ((ap_enable == NULL) || (sta_enable == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }

    /* 由工作模式反推两种角色是否启用。 */
    wifi_mode_t mode;
    esp_err_t err = esp_wifi_get_mode(&mode);
    if (err != ESP_OK) {
        return err;
    }

    *ap_enable = (mode == WIFI_MODE_AP || mode == WIFI_MODE_APSTA);
    *sta_enable = (mode == WIFI_MODE_STA || mode == WIFI_MODE_APSTA);
    return ESP_OK;
}

/**
 * @brief 切换 AP 与 STA 的启用组合，必要时重启 Wi-Fi 并重新连接 STA。
 * @param[in] ap_enable true 启用 AP。
 * @param[in] sta_enable true 启用 STA。
 * @retval ESP_OK 模式已按要求生效。
 * @retval ESP_ERR_INVALID_ARG AP 与 STA 被同时关闭。
 * @retval 其他错误码 来自 esp_wifi_get_mode()、esp_wifi_set_mode()、esp_wifi_set_config() 或 esp_wifi_start()。
 * @note 模式未变化且启用 STA 时，只补发一次 esp_wifi_connect()，不重启 Wi-Fi。
 * @warning 切换模式会先调用 esp_wifi_stop()，已建立的 AP 与 STA 连接都会断开并重新发起。
 */
esp_err_t wifi_set_ap_sta_enabled(bool ap_enable, bool sta_enable)
{
    /* 两种模式同时关闭会让设备彻底失联，直接拒绝。 */
    if (!ap_enable && !sta_enable) {
        return ESP_ERR_INVALID_ARG;
    }

    wifi_mode_t target_mode = wifi_mode_from_enable(ap_enable, sta_enable);
    wifi_mode_t current_mode;
    esp_err_t err = esp_wifi_get_mode(&current_mode);
    if (err != ESP_OK) {
        return err;
    }

    /* 模式未变化时无需重启 Wi-Fi，仅在需要 STA 时补一次连接。 */
    if (current_mode == target_mode) {
        if (sta_enable) {
            esp_wifi_connect();
        }
        return ESP_OK;
    }

    /* 允许 Wi-Fi 已经处于停止状态。 */
    err = esp_wifi_stop();
    if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_STOPPED) {
        return err;
    }

    err = esp_wifi_set_mode(target_mode);
    if (err != ESP_OK) {
        return err;
    }
    /* 启用 STA 时下发默认上级热点配置，随后重新启动并连接。 */
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







/**
 * @brief 按键触发时启动或停止 AP。
 * @note 判定依据是缓存的 twifiData.u8ApStatus，不是驱动的实时状态。
 * @note 在 APSTA 模式下 esp_wifi_stop() 会同时断开 STA，影响整条 Wi-Fi 链路。
 */
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
/**
 * @brief Wi-Fi 与 IP 事件回调：刷新模式状态，并把事件映射成 WS2812 颜色消息。
 * @note 各事件分支只填写颜色，函数末尾统一按 u8SendFg 投递一次队列。
 * @note 投递使用 portMAX_DELAY，队列满时会阻塞事件任务。
 */
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
    /* 事件分支处理完后统一入队，避免同一事件重复发送颜色。 */
    if (u8SendFg == 1)
    {
        u8SendFg = 0;  // 清除发送标志
        if (xQueueSend(xQueueLed, &tws2812Data, portMAX_DELAY) == pdTRUE) 
        {
            // printf("发送RGB: R=%d, G=%d, B=%d\n", AllQueue.r, AllQueue.g, AllQueue.b);
        }
    }
}
/**
 * @brief 初始化 lwIP、默认事件循环与 Wi-Fi 驱动，并以 SoftAP 模式启动热点。
 * @note 只设置 AP 配置；STA 配置在 wifi_set_ap_sta_enabled() 启用 STA 时才下发。
 * @note 内部大量使用 ESP_ERROR_CHECK，任一初始化失败会直接 abort 整机。
 */
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
    /* AP 与 STA 两个 netif 都创建，运行时切换模式可直接复用。 */
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));


    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    // AP only at boot, STA config is set only when STA is enabled.
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &AP_wifi_config));


    /* WIFI_EVENT 订阅全部事件，IP_EVENT 只订阅 STA 获取到 IP。 */
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


    /* 启动后由事件回调置位 AP 状态并推送状态灯颜色。 */
    ESP_ERROR_CHECK(esp_wifi_start());

    esp_netif_ip_info_t ip_info;
    esp_netif_get_ip_info(esp_netif_get_handle_from_ifkey("WIFI_AP_DEF"), &ip_info);

    char ip_addr[16];
    inet_ntoa_r(ip_info.ip.addr, ip_addr, 16);
    ESP_LOGI(TAG, "Set up softAP with IP: %s", ip_addr);

    ESP_LOGI(TAG, "wifi_init_softap finished. SSID:%s password:%s channel:%d authmode:%d",
             AP_wifi_config.ap.ssid, AP_wifi_config.ap.password, AP_wifi_config.ap.channel, AP_wifi_config.ap.authmode);

}

/**
 * @brief STA 超时定时器回调：累计约 3 分钟后强制切回 AP 模式。
 * @note 回调按 1 秒周期调用，函数内部用静态计数器折算分钟，计时到点后停止定时器。
 * @note 定时器创建代码当前被注释、xStaTimeoutTimer 为 NULL，现有流程不会触发本回调。
 * @warning 切回 AP 前会调用 esp_wifi_stop()，正在使用的 STA 连接会被强制断开。
 */
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

/**
 * @brief Wi-Fi 控制任务入口：拉起 SoftAP 与 HTTP 服务器后进入常驻循环。
 * @param[in] pvParameters FreeRTOS 任务参数，当前未使用。
 * @note 超时定时器句柄在此被置为 NULL，StaTimeout() 在现有流程中不会被调度。
 */
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
    /* 先按 AP 模式拉起网络，再启动 HTTP 服务器对外提供控制页面。 */
    wifi_sta_ap();
    start_webserver();

    while (1) 
    {
        // esp_wifi_stop();
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}
