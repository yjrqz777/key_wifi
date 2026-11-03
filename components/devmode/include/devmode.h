/**
 * @file devmode.h
 * @brief 设备运行模式：查看模式（MSC 可见）与传输模式（MSC 不可用）。
 * @details 本组件只保存一个全局模式标志，不依赖任何其他组件，供 USB、文件传输和按键
 *          等模块共同读取，避免模块之间互相包含。
 */
#ifndef DEVMODE_H
#define DEVMODE_H

#include <stdbool.h>

/**
 * @brief 设备运行模式。
 */
typedef enum {
    DEVMODE_VIEW = 0,     /**< 查看模式：MSC 正常访问 spiflash，串口照旧转发 UART */
    DEVMODE_TRANSFER = 1, /**< 传输模式：MSC 拒绝访问 spiflash，CDC 交给 XMODEM 接收文件 */
} devmode_t;

/**
 * @brief 读取当前模式。
 * @return 当前模式枚举值。
 */
devmode_t devmode_get(void);

/**
 * @brief 切换当前模式。
 * @param[in] mode 目标模式。
 * @note 只在两个取值之间切换，传其他值时按查看模式处理。
 */
void devmode_set(devmode_t mode);

/**
 * @brief 判断当前是否处于传输模式。
 * @retval true 传输模式。
 * @retval false 查看模式。
 */
bool devmode_is_transfer(void);

#endif // DEVMODE_H
