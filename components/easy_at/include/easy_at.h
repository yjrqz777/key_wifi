/**
 * @file easy_at.h
 * @brief USB CDC 简易 AT 命令通道（JSON 形式 Wi-Fi 配置）的对外接口。
 * @details 主机发送 "help" 获取命令格式，发送 {"ap":{"ssid":"...","passwd":"..."}}
 *          后由 CDC OUT 回调暂存，再由 usb_AT 任务解析、保存并应用 AP 参数。
 */

#ifndef __EASY_AT_H__
#define __EASY_AT_H__

#include <stddef.h>
#include <stdint.h>

/**
 * @brief USB AT 命令通道的数据缓存与处理状态。
 */
typedef struct tusbatdataDef
{
    char u8read_buffer[128]; /**< 从 CDC OUT 收到的一行命令缓存 */
    uint8_t u8ok;            /**< 待处理标志，为 1 表示缓存中有一条命令等待 usbATLoop 解析 */
}tusbatdataDef;

extern tusbatdataDef tusbatdata; /* AT 命令通道的全局缓存实例，定义在 easy_at.c */

/**
 * @brief 解析暂存的 AP 配置 JSON，保存凭据并重启 AP。
 * @retval 1 本次调用结束，没有待处理数据时同样返回 1。
 * @retval 0 JSON 解析失败或缺少合法的 ap 对象。
 */
uint8_t usbATLoop(void);

/**
 * @brief AT 命令处理任务入口，每 50ms 轮询一次是否有待处理命令。
 * @param[in] pvParameters 任务参数，未使用。
 * @note 本任务不会退出，必须由使用方创建后 CDC 命令才会被解析。
 */
void usb_AT(void *pvParameters);

#endif // __EASY_AT_H__
