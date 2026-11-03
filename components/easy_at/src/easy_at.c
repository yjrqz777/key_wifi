/***************************************************************************************************
 * Author: yjrqz777 3210551161@qq.com
 * Date: 2025-06-08 22:51:42
 * LastEditTime: 2025-06-09 22:23:19
 * LastEditors: yjrqz777 3210551161@qq.com
 * Description: 
 * FilePath: /key_wifi/components/easy_at/src/easy_at.c
 * @YJRQZ777
***************************************************************************************************/

/**
 * @file easy_at.c
 * @brief 通过 USB CDC 串口接收 JSON 形式的 Wi-Fi 配置命令，保存并应用 AP 参数。
 * @details 主机发送 "help" 可获取命令格式，发送 {"ap":{"ssid":"...","passwd":"..."}} 形式的
 *          JSON 后，数据先在 CDC OUT 端点回调中暂存，再由 usb_AT 任务解析；
 *          凭据写入 NVS 后重启 Wi-Fi 并切换到纯 AP 模式。
 */

#include <stdio.h>
#include <string.h>

#include "easy_at.h"
#include "myusb.h"
#include "cJSON.h"
#include "esp_log.h"
#include "wifi.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "usbd_core.h"
#include "usbd_cdc_acm.h"

#include "nvs_store.h"
// #include "usbd_msc.h"
// #include "usbd_hid.h"
/***************************************************************************************************/

static const char *TAG = "EASY_AT";


/***************************************************************************************************/
tusbatdataDef tusbatdata = {0};
/***************************************************************************************************/

/**
 * @brief 暂存 CDC 收到的 "help" 请求或 AP 配置 JSON，交给 usb_AT 任务处理。
 * @param[in] nbytes 本次接收到的字节数。
 * @param[in] read_buffer 接收数据的缓冲区，长度不小于 nbytes。
 * @note 本函数由 CDC OUT 端点回调在 USB 中断上下文中调用，不能阻塞。
 * @note 仅当数据以 {"ap" 开头且不少于 37 字节时才复制到 tusbatdata 并置 u8ok；
 *       u8ok 已为 1 时新数据被丢弃，等待 usbATLoop 处理完上一条命令。
 * @warning nbytes 不能超过 tusbatdata.u8read_buffer 的 128 字节，否则会越界写。
 */
void usb_CDC_ACM_Data_Dispose(uint32_t nbytes, uint8_t *read_buffer)
{ 
    static uint8_t u8help_buffer[] = "YJRQZDAP_HELP_JSON_MAX:64Byte:\n{\"ap\":{\"ssid\":\"test\",\"passwd\":\"12345678\"}}";
    if (nbytes == 4)
    {
        if (read_buffer[0] == 'h' && read_buffer[1] == 'e' && read_buffer[2] == 'l' && read_buffer[3] == 'p')
        {
            usbd_ep_start_write(BUSID, CDC_IN_EP, (uint8_t *)u8help_buffer, sizeof(u8help_buffer));
        }
    }
    
    if (nbytes >= 37 && 0 == tusbatdata.u8ok)
    {
        /* 逐字节核对约定的 JSON 前缀 {"ap"，避免把普通串口数据当成 AT 命令 */
        if (read_buffer[0] != '{')
        {
            return;
        }
        if (read_buffer[1] != '\"')
        {
            return;
        }
        if (read_buffer[2] != 'a')
        {
            return;
        }
        if (read_buffer[3] != 'p')
        {
            return;
        }
        if (read_buffer[4] != '\"')
        {
            return;
        }    

        memcpy(tusbatdata.u8read_buffer, read_buffer, nbytes);
        tusbatdata.u8ok = 1;
    }

}




/**
 * @brief 解析暂存的 AP 配置 JSON，保存凭据并重启 AP。
 * @retval 1 本次调用结束，没有待处理数据时同样返回 1。
 * @retval 0 JSON 解析失败或缺少合法的 ap 对象。
 * @note 处理结束后清零 tusbatdata；ssid 或 passwd 缺失、类型错误时只打印提示，
 *       既不保存凭据也不重启 AP。
 */
uint8_t usbATLoop(void)
{ 
    if (tusbatdata.u8ok == 1)
    {
        // 解析JSON
        cJSON *config_json = cJSON_Parse(tusbatdata.u8read_buffer);
        
        if (!config_json) {
            ESP_LOGE(TAG, "JSON解析失败, 错误: %s", cJSON_GetErrorPtr());
            tusbatdata.u8ok = 0;
            memset(&tusbatdata, 0, sizeof(tusbatdata));
            return 0;
        }
        ESP_LOGI(TAG, "原始JSON: %s", cJSON_PrintUnformatted(config_json));
        ESP_LOGI(TAG, "JSON解析成功");
        // 获取 "ap" 对象
        cJSON *ap = cJSON_GetObjectItemCaseSensitive(config_json, "ap");
        if (!ap || !cJSON_IsObject(ap)) {
            printf("未找到 'ap' 对象\n");
            memset(&tusbatdata, 0, sizeof(tusbatdata));
            cJSON_Delete(config_json);
            return 0;
        }
    
        // 从 "ap" 对象中提取数据
        cJSON *ssid = cJSON_GetObjectItemCaseSensitive(ap, "ssid");
        cJSON *passwd = cJSON_GetObjectItemCaseSensitive(ap, "passwd");
    
        if (!ssid || !cJSON_IsString(ssid)) {
            printf("无效或缺失的 SSID\n");
        } else if (!passwd || !cJSON_IsString(passwd)) {
            printf("无效或缺失的密码\n");
        } else {
            // 安全地复制值到缓冲区
            char wifi_ssid[33] = {0};  // SSID 最大长度 32 字符 + 空终止符
            char wifi_pass[65] = {0};  // 密码最大长度 64 字符 + 空终止符
            
            snprintf(wifi_ssid, sizeof(wifi_ssid), "%s", ssid->valuestring);
            snprintf(wifi_pass, sizeof(wifi_pass), "%s", passwd->valuestring);
            
            ESP_LOGI(TAG, "ssid :%s, passwd :%s", wifi_ssid, wifi_pass);

            nvs_save_data("wifi_config", "ssid", wifi_ssid);
            nvs_save_data("wifi_config", "pass", wifi_pass);

            /* AP 重启结果通过 CDC 回给主机，USB 侧细节留在本组件内。 */
            static uint8_t u8AP_OK[] = "AP_OK";
            static uint8_t u8AP_FAIL[] = "AP_FAIL";
            esp_err_t ap_err = wifi_ap_start(wifi_ssid, wifi_pass);
            usbd_ep_start_write(BUSID, CDC_IN_EP,
                                (ap_err == ESP_OK) ? u8AP_OK : u8AP_FAIL,
                                (ap_err == ESP_OK) ? sizeof(u8AP_OK) : sizeof(u8AP_FAIL));
            // 在这里可以调用你的 WiFi 连接函数
            // connect_to_wifi(wifi_ssid, wifi_pass);
        }
        
        // 清理内存
        cJSON_Delete(config_json);
        memset(&tusbatdata, 0, sizeof(tusbatdata));
    }
    return 1;
}






/**
 * @brief AT 命令处理任务入口，每 50ms 轮询一次是否有待处理命令。
 * @param[in] Pr FreeRTOS 任务参数，未使用。
 * @note 本任务不会退出，必须由使用方创建后 CDC 命令才会被解析。
 * @note 帮助回复内容为 u8help_buffer 中的固定字符串：YJRQZDAP_HELP_JSON_MAX:64Byte
 *       加一行 {"ap":{"ssid":"test","passwd":"12345678"}}，其中数值仅作格式示例。
 */
void usb_AT(void *pvParameters)
{
    while (true)
    {
        usbATLoop();
        vTaskDelay(50 / portTICK_PERIOD_MS);
    }
}
