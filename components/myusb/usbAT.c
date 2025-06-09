/***************************************************************************************************
 * Author: yjrqz777 3210551161@qq.com
 * Date: 2025-06-08 22:51:42
 * LastEditTime: 2025-06-09 22:23:19
 * LastEditors: yjrqz777 3210551161@qq.com
 * Description: 
 * FilePath: /key_wifi/components/myusb/usbAT.c
 * @YJRQZ777
***************************************************************************************************/
#include "myusb.h"
#include "cJSON.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include <stdio.h>
#include <string.h>

#include "esp_wifi.h"
#include "esp_netif.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "usbd_core.h"
#include "usbd_cdc_acm.h"
// #include "usbd_msc.h"
// #include "usbd_hid.h"
/***************************************************************************************************/

static const char *TAG = "usbAT";


/***************************************************************************************************/
tusbatdataDef tusbatdata = {0};
/***************************************************************************************************/

/***************************************************************************************************
 * 功能描述: 
 * 输入参数: 
 * 输出参数: 
 * 返 回 值: 
 * 其它说明: 
 * param {char} *ssid
 * param {char} *password
***************************************************************************************************/
void wifi_ap_start(char *ssid, char *password)
{ 
    static uint8_t u8AP_OK[] = "AP_OK";
    static uint8_t u8AP_FAIL[] = "AP_FAIL";

    wifi_config_t ap_config;
    esp_wifi_get_config(WIFI_IF_AP, &ap_config); // 获取AP配置 (可选，如果你不需要保留AP配置则可不做)
    esp_wifi_stop();


    memcpy(ap_config.ap.ssid, ssid, 32);
    memcpy(ap_config.ap.password, password, 64);


    esp_wifi_set_mode(WIFI_MODE_AP); // 关键：切换回APSTA模式
    esp_wifi_set_config(WIFI_IF_AP, &ap_config);   // 设置AP配置
    // 3. 重新启动Wi-Fi
    if (ESP_OK  == esp_wifi_start())
    {
        usbd_ep_start_write(BUSID, CDC_IN_EP, (uint8_t *)u8AP_OK, sizeof(u8AP_OK));
    }
    else
    {
        usbd_ep_start_write(BUSID, CDC_IN_EP, (uint8_t *)u8AP_FAIL, sizeof(u8AP_FAIL));
    }
}
/***************************************************************************************************
 * 功能描述: 
 * 输入参数: 
 * 输出参数: 
 * 返 回 值: 
 * 其它说明: 
 * param {char*} ssid
 * param {char*} password
***************************************************************************************************/
bool save_wifi_credentials(const char* ssid, const char* password)
{
    nvs_handle_t handle;
    ESP_ERROR_CHECK(nvs_open("wifi_config", NVS_READWRITE, &handle));
    
    ESP_ERROR_CHECK(nvs_set_str(handle, "ssid", ssid));
    ESP_ERROR_CHECK(nvs_set_str(handle, "pass", password));
    
    esp_err_t commit_err = nvs_commit(handle);
    nvs_close(handle);
    return commit_err == ESP_OK;
}
/***************************************************************************************************
 * 功能描述: 
 * 输入参数: 
 * 输出参数: 
 * 返 回 值: 
 * 其它说明: 
***************************************************************************************************/
uint8_t read_wifi_credentials(char* ssid, char* password)
{
    char u8ssid[33] = {0};        // SSID 最大 32 字符 + NULL 终止符
    char u8password[65] = {0};
    esp_err_t err;
    nvs_handle_t handle;
    ESP_LOGE("NVS", "------------------------------------------------");
    // 1. 打开 NVS 命名空间
    err = nvs_open("wifi_config", NVS_READONLY, &handle);
    if (err != ESP_OK) {
        ESP_LOGE("NVS", "打开命名空间失败: %s", esp_err_to_name(err));
        return 0;
    }
    
    // 2. 读取 SSID
    size_t required_size = 0;
    
    // 第一次调用：获取 SSID 所需缓冲区大小
    err = nvs_get_str(handle, "ssid", NULL, &required_size);
    if (err == ESP_OK && required_size <= sizeof(u8ssid)) {
        // 第二次调用：实际读取 SSID 值
        err = nvs_get_str(handle, "ssid", u8ssid, &required_size);
        if (err != ESP_OK) {
            ESP_LOGE("NVS", "读取 SSID 失败: %s", esp_err_to_name(err));
            nvs_close(handle);
            return 0;
        }
    }
    else {
        ESP_LOGE("NVS", "获取 SSID 长度失败: %s", esp_err_to_name(err));
        nvs_close(handle);
        return 0;
    }
    
    // 3. 读取密码
    required_size = 0;
    err = nvs_get_str(handle, "pass", NULL, &required_size);
    if (err == ESP_OK && required_size <= sizeof(u8password)) {
        err = nvs_get_str(handle, "pass", u8password, &required_size);
        if (err != ESP_OK) {
            ESP_LOGE("NVS", "读取密码失败: %s", esp_err_to_name(err));
            nvs_close(handle);
            return 0;
        }
    }
    else {
        ESP_LOGE("NVS", "获取密码长度失败: %s", esp_err_to_name(err));
        nvs_close(handle);
        return 0;
    }
    
    // 4. 验证凭据是否完整有效
    if (strlen(u8ssid) > 0 && strlen(u8password) > 0) {
        ESP_LOGI("-------NVS", "读取到 WiFi 凭据: SSID=%s, Password=%s", u8ssid, u8password);
    } else {
        ESP_LOGW("-------NVS", "未找到有效的 WiFi 凭据");
        nvs_close(handle);
        return 0;
    }
    memcpy(ssid, u8ssid, sizeof(u8ssid));
    nvs_close(handle);
    return 1;
}

/***************************************************************************************************
 * 功能描述: 
 * 输入参数: 
 * 输出参数: 
 * 返 回 值: 
 * 其它说明: 
 * param {uint32_t} nbytes
 * param {uint8_t} *read_buffer
***************************************************************************************************/
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




/***************************************************************************************************
 * 功能描述: 
 * 输入参数: 
 * 输出参数: 
 * 返 回 值: 
 * 其它说明: 
***************************************************************************************************/
uint8_t usbATLoop()
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
            
            printf("成功解析 WiFi 配置:\n");
            printf("SSID: %s\n", wifi_ssid);
            printf("Password: %s\n", wifi_pass);
            save_wifi_credentials(wifi_ssid, wifi_pass);
            wifi_ap_start(wifi_ssid, wifi_pass); // 启动AP模式
            // 在这里可以调用你的 WiFi 连接函数
            // connect_to_wifi(wifi_ssid, wifi_pass);
        }
        
        // 清理内存
        cJSON_Delete(config_json);
        memset(&tusbatdata, 0, sizeof(tusbatdata));
    }
    return 1;
}







/***************************************************************************************************
 * 功能描述: 
 * 输入参数: 
 * 输出参数: 
 * 返 回 值: 
 * 其它说明: "YJRQZDAP_HELP_JSONMAX:64Byte:\n{\"ap\":{\"ssid\":\"test\",\"passwd\":\"12345678\"}}";
 * param {void} *Pr
***************************************************************************************************/
void usb_AT(void *Pr)
{
    while (true)
    {
        usbATLoop();
        vTaskDelay(50 / portTICK_PERIOD_MS);
    }
}
