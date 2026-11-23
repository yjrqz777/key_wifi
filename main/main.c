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
#include <inttypes.h>
#include "esp_log.h"
#include "soc/soc_caps.h"
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "nvs_flash.h"

#include "myusb.h"
#include "all_control.h"
#include "nvs_store.h"

#include "esp_ota_ops.h"
#include "esp_flash.h"
#include "esp_system.h"
const char *TAG = "main";

/**
 * @brief 打印分区的完整信息。
 * @param[in] part 分区指针，不能为 NULL。
 * @param[in] name 日志中的分区标识，如 "Running partition"。
 */
static void print_partition_info(const esp_partition_t *part, const char *name)
{
    ESP_LOGI(TAG,
            "%s: chip_id=0x%" PRIx32
            ", type=%" PRIu32 ", subtype=%" PRIu32
            ", address=0x%" PRIx32 ", size=0x%" PRIx32 " (%" PRIu32 " B)"
            ", erase_size=0x%" PRIx32 ", label=\"%s\", encrypted=%d, readonly=%d",
            name,
            part->flash_chip->chip_id,
            (uint32_t)part->type,
            (uint32_t)part->subtype,
            part->address,
            part->size,
            part->size,
            part->erase_size,
            part->label,
            part->encrypted,
            part->readonly
            );
}


/***************************************************************************************************
 * 功能描述: 
 * 输入参数: 
 * 输出参数: 
 * 返 回 值: 
 * 其它说明: 
***************************************************************************************************/
void app_main(void)
{
    //Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
        ESP_LOGW(TAG, "NVS Flash Erase & Init Success");
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGW(TAG, "NVS Flash Init Success");

    NvsShowAllEntries();

// esp_ota_get_running_partition	esp_ota_ops.c	获取当前正在运行的分区
// esp_ota_get_boot_partition	esp_ota_ops.c	获取下次启动分区
// esp_ota_get_next_update_partition	esp_ota_ops.c	获取本次应写入的 OTA 分区
// esp_partition_find_first	partition.c	按类型、子类型或名字查找分区
    static const esp_partition_t *curr_partition = NULL;
// typedef struct {
//     esp_flash_t* flash_chip;            /*!< SPI flash chip on which the partition resides */
//     esp_partition_type_t type;          /*!< partition type (app/data) */
//     esp_partition_subtype_t subtype;    /*!< partition subtype */
//     uint32_t address;                   /*!< starting address of the partition in flash */
//     uint32_t size;                      /*!< size of the partition, in bytes */
//     uint32_t erase_size;                /*!< size the erase operation should be aligned to */
//     char label[17];                     /*!< partition label, zero-terminated ASCII string */
//     bool encrypted;                     /*!< flag is set to true if partition is encrypted */
//     bool readonly;                      /*!< flag is set to true if partition is read-only */
// } esp_partition_t;
    curr_partition = esp_ota_get_running_partition();
    if (curr_partition == NULL) 
    {
        ESP_LOGE(TAG, "get running partition failed");
        return;
    }

    print_partition_info(curr_partition, "Running partition");

    /* esp_ota_get_boot_partition() 返回 otadata 指向的下次启动 app 分区，不是 otadata 分区本身。 */
    static const esp_partition_t *otadata_partition = NULL;
    otadata_partition = esp_ota_get_boot_partition();
    if (otadata_partition == NULL)
    {
        ESP_LOGE(TAG, "get boot partition failed");
        return;
    }

    print_partition_info(otadata_partition, "Boot partition");


    all_control_main(0, NULL);
    ESP_LOGI(TAG, "all_control_main init Success");

    while (1)
    {
        vTaskDelay(3000 / portTICK_PERIOD_MS);
    }
}
