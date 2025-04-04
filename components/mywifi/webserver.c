#include <string.h>
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

#include "mywifi.h"

#include "cJSON.h"

static const char *TAG = "-webserver-";
/***************************************************************************************************
***************************************************************************************************/


extern const char root_start[] asm("_binary_root_html_start");
extern const char root_end[] asm("_binary_root_html_end");



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
    
    // 动态获取 IP 地址
    esp_netif_ip_info_t ip_info;
    esp_netif_get_ip_info(esp_netif_get_handle_from_ifkey("WIFI_STA_DEF"), &ip_info);
    char ip_str[16];
    snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&ip_info.ip));
    ESP_LOGI(TAG, "IP Address: %s", ip_str);
    // // 替换占位符
                char *ip_placeholder = strstr(html, "%IP%");
                // if (ip_placeholder) {
                //     memmove(ip_placeholder + strlen(ip_str), 
                //             ip_placeholder + 4, 
                //             html + html_len - (ip_placeholder + 4));
                //     memcpy(ip_placeholder, ip_str, strlen(ip_str));
                // }
        
    // 发送响应
    
// 计算新内存需求
    size_t new_len = html_len - 4 + strlen(ip_str) + 1; // +1保留终止符
    char *new_html = (char*)malloc(new_len);

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
    ESP_LOGI(TAG, "原始JSON: %s", cJSON_PrintUnformatted(rgb_json));
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
    // r = (r < 0) ? 0 : (r > 255) ? 255 : r;
    // g = (g < 0) ? 0 : (g > 255) ? 255 : g;
    // b = (b < 0) ? 0 : (b > 255) ? 255 : b;

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
        httpd_register_err_handler(server, HTTPD_404_NOT_FOUND, http_404_error_handler);
    }
    return server;
}

