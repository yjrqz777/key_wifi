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
#include "nimble/ble.h"
#include "modlog/modlog.h"
// #include "esp_peripheral.h"
#ifdef __cplusplus
extern "C" {
#endif

/* 16 Bit SPP Service UUID */
#define BLE_SVC_SPP_UUID16                                  0xABF0

/* 16 Bit SPP Service Characteristic UUID */
#define BLE_SVC_SPP_CHR_UUID16                              0xABF1

struct ble_hs_cfg;
struct ble_gatt_register_ctxt;
void ble_task(void *arg);
#ifdef __cplusplus
}
#endif





#endif
