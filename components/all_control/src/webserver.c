#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/timers.h"

#include "esp_http_server.h"

#include "esp_mac.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "lwip/lwip_napt.h"
#include "lwip/inet.h"
#include "lwip/err.h"
#include "lwip/sys.h"

#include "cJSON.h"

#include "all_control.h"
#include "myusb.h"
// #include "var.h"
static const char *TAG = "WEB";
/***************************************************************************************************
***************************************************************************************************/


extern const char root_start[] asm("_binary_root_html_start");
extern const char root_end[] asm("_binary_root_html_end");
extern const char serial_view_start[] asm("_binary_serial_view_html_start");
extern const char serial_view_end[] asm("_binary_serial_view_html_end");



/***************************************************************************************************
 * 功能描述: 
 * 输入参数: 
 * 输出参数: 
 * 返 回 值: 
 * 其它说明: 
***************************************************************************************************/
// HTTP Error (404) Handler - Redirects all requests to the root page
esp_err_t http_404_error_handler(httpd_req_t *req, httpd_err_code_t err)
{
    // Set status
    httpd_resp_set_status(req, "302 Temporary Redirect");
    // Redirect to the "/" root directory
    httpd_resp_set_hdr(req, "Location", "/");
    // iOS requires content in the response to detect a captive portal, simply redirecting is not sufficient.
    httpd_resp_send(req, "Redirect to the captive portal", HTTPD_RESP_USE_STRLEN);

    ESP_LOGI(TAG, "Redirecting to root");
    return ESP_OK;
}

/***************************************************************************************************
 * 功能描述: // HTTP GET Handler
 * 输入参数: 
 * 输出参数: 
 * 返 回 值: 
 * 其它说明: 
***************************************************************************************************/
static esp_err_t root_get_handler(httpd_req_t *req)
{
    size_t html_len = root_end - root_start;

    // 获取嵌入的 HTML 内容
    char *html = strndup((char*)root_start, html_len); // 复制到堆内存
    if (html == NULL) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No memory");
        return ESP_FAIL;
    }
    
    // 动态获取 IP 地址
    esp_netif_ip_info_t ip_info = {0};
    esp_netif_t *netif_ap = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    esp_err_t ip_ret = ESP_FAIL;
    if (netif_ap != NULL) {
        ip_ret = esp_netif_get_ip_info(netif_ap, &ip_info);
    }
    char ip_str[16];
    if (ip_ret == ESP_OK) {
        snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&ip_info.ip));
    } else {
        snprintf(ip_str, sizeof(ip_str), "0.0.0.0");
    }
    ESP_LOGI(TAG, "IP Address: %s", ip_str);
    // 替换占位符
    char *ip_placeholder = strstr(html, "%IP%");
    if (ip_placeholder == NULL) {
        ESP_LOGW(TAG, "No %%IP%% placeholder in root page");
        httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
        free(html);
        return ESP_OK;
    }
        
    // 发送响应
    
    // 计算新内存需求
    size_t new_len = html_len - 4 + strlen(ip_str) + 1; // +1保留终止符
    char *new_html = (char*)malloc(new_len);
    if (new_html == NULL) {
        free(html);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No memory");
        return ESP_FAIL;
    }

    // 分割处理字符串
    char *seg1_end = ip_placeholder;
    size_t seg1_len = seg1_end - html;
    size_t seg2_len = html_len - (seg1_end - html) - 4;

    // 分段拷贝
    memcpy(new_html, html, seg1_len);
    memcpy(new_html + seg1_len, ip_str, strlen(ip_str));
    memcpy(new_html + seg1_len + strlen(ip_str), seg1_end + 4, seg2_len);
    new_html[new_len-1] = '\0';

    // 释放原内存并替换指针
    free(html);
    html = new_html;


    ESP_LOGI(TAG, "Serve root");
    // httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
    // httpd_resp_send(req, root_start, root_len);

            free(html);
    return ESP_OK;
}

static esp_err_t serial_view_get_handler(httpd_req_t *req)
{
    size_t html_len = serial_view_end - serial_view_start;
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, serial_view_start, html_len);
}
/***************************************************************************************************
 * 功能描述: // 控制LED的处理函数
 * 输入参数: 
 * 输出参数: 
 * 返 回 值: 
 * 其它说明: 
***************************************************************************************************/
esp_err_t led_control_handler(httpd_req_t *req) {
    char*  buf;
    size_t buf_len;

    buf_len = httpd_req_get_url_query_len(req) + 1;
    ESP_LOGI(TAG, "buf_len: %d", buf_len);
    // if (buf_len <= 1) {
    //     ESP_LOGI(TAG, "No query string found");
    //     httpd_resp_send(req, NULL, 0); // 发送空响应
    //     return ESP_OK;
    // }
    ESP_LOGI(TAG, "buf_len: %d", buf_len);
    if (buf_len > 1) {
        buf = malloc(buf_len);
        if(httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
            char param[32];
            if (httpd_query_key_value(buf, "state", param, sizeof(param)) == ESP_OK) {
                if (strcmp(param, "on") == 0) {
                    // blink_led = 1;
                    ESP_LOGI(TAG, "on");
                    // gpio_set_level(LED_GPIO, 1); // 打开LED
                } else if (strcmp(param, "off") == 0) {
                    // blink_led = 0;
                    ESP_LOGI(TAG, "off");
                    // gpio_set_level(LED_GPIO, 0); // 关闭LED
                }
            }
        }
        free(buf);
    }
    httpd_resp_send(req, NULL, 0); // 发送空响应
    return ESP_OK;
}



static tws2812Def tws2812Data={0};


// 控制请求处理
esp_err_t rgb_control_handler(httpd_req_t *req) {
    char content[128];
    int received = 0;
    
    // 分片接收POST数据
    while (received < sizeof(content) - 1) {
        int ret = httpd_req_recv(req, content + received, sizeof(content) - received - 1);
        if (ret <= 0) break;
        received += ret;
    }
    content[received] = '\0';
    
    // 解析JSON
    cJSON *rgb_json = cJSON_Parse(content);
    if (!rgb_json) {
        ESP_LOGE(TAG, "JSON解析失败");
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    // 提取各通道值
    int r = cJSON_GetObjectItem(rgb_json, "r")->valueint;
    int g = cJSON_GetObjectItem(rgb_json, "g")->valueint;
    int b = cJSON_GetObjectItem(rgb_json, "b")->valueint;

    // 限幅处理
    r = (r < 0) ? 0 : (r > 255) ? 255 : r;
    g = (g < 0) ? 0 : (g > 255) ? 255 : g;
    b = (b < 0) ? 0 : (b > 255) ? 255 : b;

    tws2812Data.r = r;
    tws2812Data.g = g;
    tws2812Data.b = b;
    xQueueSend(xQueueLed, &tws2812Data, 2);
    // 更新PWM输出
    // ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, r);
    // ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, g);
    // ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_2, b);
    
    // ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
    // ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1);
    // ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_2);

    ESP_LOGI(TAG, "设置颜色: R=%d, G=%d, B=%d", r, g, b);
    
    cJSON_Delete(rgb_json);
    httpd_resp_sendstr(req, "OK");
    return ESP_OK;
}

static esp_err_t serial_get_handler(httpd_req_t *req)
{
    char *serial_buf = (char *)malloc(4096);
    if (serial_buf == NULL) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No memory");
        return ESP_FAIL;
    }

    size_t used = usb_serial_log_snapshot(serial_buf, 4096);
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    esp_err_t ret = httpd_resp_send(req, serial_buf, used);
    free(serial_buf);
    return ret;
}

static esp_err_t wifi_status_get_handler(httpd_req_t *req)
{
    bool ap_en = false;
    bool sta_en = false;
    esp_err_t err = wifi_get_ap_sta_enabled(&ap_en, &sta_en);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "wifi status read failed");
        return err;
    }

    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "ap", ap_en);
    cJSON_AddBoolToObject(root, "sta", sta_en);
    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (json == NULL) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "json alloc failed");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    esp_err_t ret = httpd_resp_sendstr(req, json);
    cJSON_free(json);
    return ret;
}

static esp_err_t wifi_control_get_handler(httpd_req_t *req)
{
    char query[64] = {0};
    char ap_val[8] = {0};
    char sta_val[8] = {0};
    bool ap_en = false;
    bool sta_en = false;

    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing query");
        return ESP_FAIL;
    }

    if (httpd_query_key_value(query, "ap", ap_val, sizeof(ap_val)) != ESP_OK ||
        httpd_query_key_value(query, "sta", sta_val, sizeof(sta_val)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing ap/sta");
        return ESP_FAIL;
    }

    ap_en = (strcmp(ap_val, "1") == 0);
    sta_en = (strcmp(sta_val, "1") == 0);

    esp_err_t err = wifi_set_ap_sta_enabled(ap_en, sta_en);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "wifi switch failed");
        return err;
    }

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}






/***************************************************************************************************
 * 功能描述: 
 * 输入参数: 
 * 输出参数: 
 * 返 回 值: 
 * 其它说明: 
***************************************************************************************************/
static const httpd_uri_t root = {
    .uri = "/",
    .method = HTTP_GET,
    .handler = root_get_handler
};

httpd_uri_t led_control = {
    .uri       = "/control",
    .method    = HTTP_GET,
    .handler   = led_control_handler,
    .user_ctx  = NULL
};

httpd_uri_t rgb_control = {
    .uri       = "/rgbcontrol",
    .method    = HTTP_POST,
    .handler   = rgb_control_handler,
    .user_ctx  = NULL
};

httpd_uri_t serial_get = {
    .uri       = "/serial",
    .method    = HTTP_GET,
    .handler   = serial_get_handler,
    .user_ctx  = NULL
};

httpd_uri_t serial_view = {
    .uri       = "/serial_view",
    .method    = HTTP_GET,
    .handler   = serial_view_get_handler,
    .user_ctx  = NULL
};

httpd_uri_t wifi_status = {
    .uri       = "/wifi_status",
    .method    = HTTP_GET,
    .handler   = wifi_status_get_handler,
    .user_ctx  = NULL
};

httpd_uri_t wifi_control = {
    .uri       = "/wifi_control",
    .method    = HTTP_GET,
    .handler   = wifi_control_get_handler,
    .user_ctx  = NULL
};


/***************************************************************************************************
 * 功能描述: 
 * 输入参数: 
 * 输出参数: 
 * 返 回 值: 
 * 其它说明: 
***************************************************************************************************/
httpd_handle_t start_webserver(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_open_sockets = 7;
    config.lru_purge_enable = true;
    config.stack_size = 8192;
    // Start the httpd server
    ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);
    if (httpd_start(&server, &config) == ESP_OK) {
        // Set URI handlers
        ESP_LOGI(TAG, "Registering URI handlers");
        httpd_register_uri_handler(server, &root);
        httpd_register_uri_handler(server, &led_control);
        // httpd_register_uri_handler(server, &get_status);
        // httpd_register_uri_handler(server, &set_led_brightness);
        httpd_register_uri_handler(server, &rgb_control);
        httpd_register_uri_handler(server, &serial_get);
        httpd_register_uri_handler(server, &serial_view);
        httpd_register_uri_handler(server, &wifi_status);
        httpd_register_uri_handler(server, &wifi_control);
        httpd_register_err_handler(server, HTTPD_404_NOT_FOUND, http_404_error_handler);
    }
    return server;
}
