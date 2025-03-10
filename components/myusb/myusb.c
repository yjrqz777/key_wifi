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

#include <errno.h>
#include <dirent.h>
#include "esp_console.h"
#include "esp_check.h"
#include "driver/gpio.h"
#include "tinyusb.h"
#include "tusb_msc_storage.h"
#include "tusb_cdc_acm.h"
#include "sdkconfig.h"


#include "myfile.h"
#include "esp_vfs_fat.h"

// #include "DAP_config.h"
// #include "DAP.h"

// TaskHandle_t kDAPTaskHandle = NULL;
// void DAP_Thread(void *pvParameters);

#ifdef CONFIG_EXAMPLE_STORAGE_MEDIA_SDMMCCARD
#include "diskio_impl.h"
#include "diskio_sdmmc.h"
#endif

static const char *TAG = "for usb";

/* TinyUSB descriptors
   ********************************************************************* */
enum {
    ITF_NUM_CDC = 0,
    ITF_NUM_CDC_DATA,
    ITF_NUM_MSC ,
    ITF_NUM_HID,
    ITF_NUM_TOTAL
};

enum {
    EDPT_CTRL_OUT = 0x00,
    EDPT_CTRL_IN  = 0x80,

    EDPT_MSC_OUT = 0x04,
    EDPT_MSC_IN = 0x84,

    EDPT_CDC_NOTIFY = 0x81,
    EDPT_CDC_OUT = 0x02,
    EDPT_CDC_IN = 0x82,

    EDPT_HID_OUT = 0x03,
    EDPT_HID_IN = 0x83,


};

void usb_task(void)
{
    esp_err_t ret;

    





    // xTaskCreate(DAP_Thread, "DAP_Task", 2048, NULL, 10, &kDAPTaskHandle);
    // list_dir(BASE_PATH);

    vTaskDelay(pdMS_TO_TICKS(2000));
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(2000));
    }

}
