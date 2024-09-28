/*
 * SPDX-FileCopyrightText: 2022-2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

/* DESCRIPTION:
 * This example contains code to make ESP32-S3 based device recognizable by USB-hosts as a USB Mass Storage Device.
 * It either allows the embedded application i.e. example to access the partition or Host PC accesses the partition over USB MSC.
 * They can't be allowed to access the partition at the same time.
 * For different scenarios and behaviour, Refer to README of this example.
 */

#include <stdint.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "myusb.h"

#include "mywifi.h"
#include "ws2812.h"

void app_main(void)
{
    ESP_LOGI("TAG", "Initializing storage...");
    xTaskCreate(usb_task, "usb_task", 4096, NULL, 5, NULL);


    xTaskCreate(wifi_task, "wifi_task", 4096, NULL, 5, NULL);
    xTaskCreate(ws2812_task, "ws2812_task", 4096, NULL, 5, NULL);



}
