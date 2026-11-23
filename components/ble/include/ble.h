/**
 * @file ble.h
 * @brief BLE 串口组件的公共接口：启动广播、收发数据、查询连接状态。
 * @details 组件内部用 NimBLE 注册一个自定义 GATT 服务：写特征收手机发来的数据，
 *          通知特征往手机发数据。组件不直接操作 UART，收到的数据通过注册的回调
 *          交给调用方决定怎么处理。
 */
#ifndef BLE_H
#define BLE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

void ble_task(void *arg);
#endif
