/**
 * @file ble.c
 * @brief BLE 串口实现：NimBLE 初始化、自定义 GATT 服务、广播与收发。
 * @details 这里只保留骨架和调用顺序，标了 TODO 的地方按注释补参数和逻辑。
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

#include "esp_err.h"
#include "esp_log.h"

#include "ble.h"

static const char *TAG = "ble";

void ble_init(void)
{
    ESP_LOGI(TAG, "Initializing BLE...");
}




void ble_task(void *arg)
{
    ESP_LOGI(TAG, "Starting BLE task...");
    ble_init();
}