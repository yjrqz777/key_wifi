/**
 * @file devmode.c
 * @brief 保存设备运行模式标志。
 * @details 标志只在一个 32 位字上读写，ESP32 上对齐字访问是原子的，因此不再加锁。
 */

#include "devmode.h"

/* 当前设备运行模式，上电默认查看模式 */
static volatile devmode_t s_mode = DEVMODE_VIEW;

devmode_t devmode_get(void)
{
    return s_mode;
}

void devmode_set(devmode_t mode)
{
    s_mode = (mode == DEVMODE_TRANSFER) ? DEVMODE_TRANSFER : DEVMODE_VIEW;
}

bool devmode_is_transfer(void)
{
    return s_mode == DEVMODE_TRANSFER;
}
