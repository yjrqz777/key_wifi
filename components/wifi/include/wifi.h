/**
 * @file wifi.h
 * @brief Wi-Fi 组件的公共接口：AP/STA 模式控制、AP 重启和事件状态查询。
 * @details 组件内部完成 esp_netif、事件循环、esp_wifi 的初始化，并把 Wi-Fi 事件
 *          映射成 WS2812 颜色消息；调用方只通过本头文件提供的接口操作 Wi-Fi。
 */
#ifndef WIFI_H
#define WIFI_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

/**
 * @brief 初始化 lwIP、默认事件循环与 Wi-Fi 驱动，并以 SoftAP 模式启动热点。
 * @note 内部大量使用 ESP_ERROR_CHECK，任一初始化失败会直接 abort 整机。
 */
void wifi_sta_ap(void);

/**
 * @brief 用给定凭据重新配置 Wi-Fi 并启动 AP。
 * @param[in] ssid 新的 AP 名称，函数固定读取前 32 字节，调用方须保证该长度可读。
 * @param[in] password 新的 AP 密码，函数固定读取前 64 字节，调用方须保证该长度可读。
 * @retval ESP_OK esp_wifi_start() 返回成功。
 * @retval 其他值 esp_wifi_start() 的错误码。
 * @warning 函数会先停止 Wi-Fi 并把模式切换为 WIFI_MODE_AP，已连接的 STA 会立即断开且不再恢复。
 */
esp_err_t wifi_ap_start(const char *ssid, const char *password);

/**
 * @brief 按键触发时启动或停止 AP。
 * @note 判定依据是缓存的 AP 状态，不是驱动的实时状态。
 */
void wifi_button_handler(void);

/**
 * @brief 查询当前 Wi-Fi 模式对应的 AP/STA 使能状态。
 * @param[out] ap_enable 输出 AP 是否使能。
 * @param[out] sta_enable 输出 STA 是否使能。
 * @retval ESP_OK 查询成功。
 * @retval ESP_ERR_INVALID_ARG 输出参数为空。
 * @retval 其他值 esp_wifi_get_mode() 的错误码。
 */
esp_err_t wifi_get_ap_sta_enabled(bool *ap_enable, bool *sta_enable);

/**
 * @brief 切换 AP/STA 使能组合并应用到 Wi-Fi 驱动。
 * @param[in] ap_enable 是否启用 AP。
 * @param[in] sta_enable 是否启用 STA。
 * @retval ESP_OK 切换成功。
 * @retval ESP_ERR_INVALID_ARG 两者都为 false。
 * @retval 其他值 esp_wifi 系列调用返回的错误码。
 */
esp_err_t wifi_set_ap_sta_enabled(bool ap_enable, bool sta_enable);

/**
 * @brief 从 NVS 读回已保存的 AP 凭据。
 * @param[out] ssid 接收 SSID 的缓冲区，长度至少 33 字节。
 * @param[out] password 接收密码的缓冲区，长度至少 65 字节。
 * @retval 1 读取成功，两个缓冲区都已写入以空字符结尾的字符串。
 * @retval 0 命名空间或键缺失、长度越界、内容为空，此时不写调用方的缓冲区。
 * @warning 复制长度固定为 33 字节和 65 字节，缓冲区更小会越界写。
 * @note 读到的 SSID 与密码会以明文打印到日志。
 */
uint8_t read_wifi_credentials(char *ssid, char *password);

#endif // WIFI_H
