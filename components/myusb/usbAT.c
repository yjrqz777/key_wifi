/***************************************************************************************************
 * Author: yjrqz777 3210551161@qq.com
 * Date: 2025-06-08 22:51:42
 * LastEditTime: 2025-06-09 22:23:19
 * LastEditors: yjrqz777 3210551161@qq.com
 * Description: 
 * FilePath: /key_wifi/components/myusb/usbAT.c
 * @YJRQZ777
***************************************************************************************************/

/**
 * @file usbAT.c
 * @brief 通过 USB CDC 串口接收 JSON 形式的 Wi-Fi 配置命令，保存并应用 AP 参数。
 * @details 主机发送 "help" 可获取命令格式，发送 {"ap":{"ssid":"...","passwd":"..."}} 形式的
 *          JSON 后，数据先在 CDC OUT 端点回调中暂存，再由 usb_AT 任务解析；
 *          凭据写入 NVS 后重启 Wi-Fi 并切换到纯 AP 模式。
 */

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

/**
 * @brief 用给定凭据重新配置 Wi-Fi 并启动 AP。
 * @param[in] ssid 新的 AP 名称，函数固定读取前 32 字节，调用方须保证该长度可读。
 * @param[in] password 新的 AP 密码，函数固定读取前 64 字节，调用方须保证该长度可读。
 * @note 结果以 "AP_OK" 或 "AP_FAIL" 字符串写入 CDC IN 端点，不通过返回值报告。
 * @warning 函数会先停止 Wi-Fi 并把模式切换为 WIFI_MODE_AP，已连接的 STA 会立即断开且不再恢复。
 */
void wifi_ap_start(char *ssid, char *password)
{ 
    static uint8_t u8AP_OK[] = "AP_OK";
    static uint8_t u8AP_FAIL[] = "AP_FAIL";

    wifi_config_t ap_config;
    esp_wifi_get_config(WIFI_IF_AP, &ap_config); // 获取AP配置 (可选，如果你不需要保留AP配置则可不做)
    /* 新的 AP 参数要先停止协议栈再重新启动才会生效 */
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
/**
 * @brief 把 AP 的 SSID 与密码写入 NVS 的 wifi_config 命名空间。
 * @param[in] ssid 以空字符结尾的 SSID 字符串。
 * @param[in] password 以空字符结尾的密码字符串。
 * @retval true NVS 提交成功，凭据已持久化。
 * @retval false NVS 提交失败，本次写入不会保留到下次上电。
 * @note 打开命名空间与写入失败由 ESP_ERROR_CHECK 处理，会直接触发断言重启，不会返回 false。
 */
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
/**
 * @brief 从 NVS 读回已保存的 AP 凭据。
 * @param[out] ssid 接收 SSID 的缓冲区，长度至少 33 字节。
 * @param[out] password 接收密码的缓冲区，长度至少 65 字节。
 * @retval 1 读取成功，两个缓冲区都已写入以空字符结尾的字符串。
 * @retval 0 命名空间或键缺失、长度越界、内容为空，此时不写调用方的缓冲区。
 * @warning 复制长度固定为 33 字节和 65 字节，缓冲区更小会越界写。
 * @note 读到的 SSID 与密码会以明文打印到日志。
 */
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
    memcpy(password, u8password, sizeof(u8password));
    nvs_close(handle);
    return 1;
}

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







/**
 * @brief AT 命令处理任务入口，每 50ms 轮询一次是否有待处理命令。
 * @param[in] Pr FreeRTOS 任务参数，未使用。
 * @note 本任务不会退出，必须由 usb_task 创建后 CDC 命令才会被解析。
 * @note 帮助回复内容为 u8help_buffer 中的固定字符串：YJRQZDAP_HELP_JSON_MAX:64Byte
 *       加一行 {"ap":{"ssid":"test","passwd":"12345678"}}，其中数值仅作格式示例。
 */
void usb_AT(void *Pr)
{
    while (true)
    {
        usbATLoop();
        vTaskDelay(50 / portTICK_PERIOD_MS);
    }
}
