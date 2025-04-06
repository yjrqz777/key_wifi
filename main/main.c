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
#include "freertos/semphr.h"
#include "nvs_flash.h"

#include "myusb.h"
#include "mywifi.h"
#include "ws2812.h"
#include "key.h"
#include "var.h"

#include "soc/soc_caps.h"
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

const char *TAG = "main";



adc_oneshot_unit_init_cfg_t init_config__with_oneshot = {
    .unit_id = ADC_UNIT_1,
    .ulp_mode = ADC_ULP_MODE_DISABLE,
};

adc_oneshot_unit_handle_t adc_handle_with_oneshot = NULL;
adc_oneshot_chan_cfg_t config__with_oneshot = {
    .atten = ADC_ATTEN_DB_11,
    .bitwidth = ADC_BITWIDTH_12,    
};
void adc_init_with_oneshot()
{
    adc_oneshot_new_unit(&init_config__with_oneshot,&adc_handle_with_oneshot); 
    adc_oneshot_config_channel(adc_handle_with_oneshot,ADC_CHANNEL_2,&config__with_oneshot);
    ESP_LOGI(TAG, "adc_init_with_oneshot success\n");
}

// unsigned char button_flag = 0;


void adc_task(void *pvParameters)
{
    int p = 0;
    // float voltage = 0;
    adc_init_with_oneshot();
    while(1)
    {

        // ESP_LOGI(TAG, "key_task_run_with_oneshot begin\n");
        // ESP_ERROR_CHECK(adc_oneshot_read(adc_handle_with_oneshot,ADC_CHANNEL_2,&p));
        // voltage = p *3.3/4096;
        // ESP_LOGI(TAG, "adc_oneshot_read=%d\n",(int)p);
        // }
        vTaskDelay(10000 / portTICK_PERIOD_MS);


    // if (p> 300 && p < 500)
    // {
    //     button_flag = 1;
    // }
    // else if (p > 850 && p < 1050)
    // {
    //     button_flag = 2;
    // }
    // else if (p > 2250 && p < 2450)
    // {
    //     button_flag = 3;
    // }
    // else if (p > 2800 && p < 3000)
    // {
    //     button_flag = 5;
    // }
    // else
    // {
    //     button_flag = 0;
    // }

// {BUTTON_MENU, 2800, 3000}, {BUTTON_PLAY, 2250, 2450}, {BUTTON_UP, 300, 500}, {BUTTON_DOWN, 850, 1050}

        // std::cout << "adc_oneshot_read = " << voltage << std::endl;
        // if(*p != 0)
        // {
            // ESP_LOGE(TAG, "adc_oneshot_read=%d\n",*p);
        // }
        // vTaskDelay(10 / portTICK_PERIOD_MS);
    }
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
    ESP_LOGI(TAG, "---Initializing Key WIFI---");

    xQueueLed = xQueueCreate(5,sizeof(struct tRgbKeyDef));
    xQueueKey = xQueueCreate(5,sizeof(struct tRgbKeyDef));


    xTaskCreate(vat_task, "vat_task", 1024*3, NULL, 9, NULL);
    xTaskCreate(usb_task, "usb_task", 4096*5, NULL, 15, NULL);
    xTaskCreate(dap_task, "dap_task", 4096, NULL, 10, NULL);
    xTaskCreate(wifi_task, "wifi_task", 4096*3, NULL, 5, NULL);
    xTaskCreate(ws2812_task, "ws2812_task", 1024*3, NULL, 5, NULL);
    xTaskCreate(key_task, "key_task", 1024*3, NULL, 6, NULL);

    xTaskCreate(adc_task, "adc_task", 1024*4, NULL, 1, NULL);



    vTaskDelay(3000 / portTICK_PERIOD_MS);

    printf("NVS Entries:\n");
    printf("----------------------------------------\n");
    printf("Namespace | Key      | Type       | Value\n");
    printf("----------------------------------------\n");

    // 遍历所有 NVS 条目
    nvs_iterator_t it = NULL;
    esp_err_t res = nvs_entry_find("nvs", NULL, NVS_TYPE_ANY, &it);
    if (res != ESP_OK) {
        printf("No entries found in NVS.\n");
        return;
    }


    while (res == ESP_OK) {
        nvs_entry_info_t entry_info;
        nvs_entry_info(it, &entry_info);
        
        // 打开命名空间
        nvs_handle_t handle;
        ESP_ERROR_CHECK(nvs_open(entry_info.namespace_name, NVS_READONLY, &handle));
        
        // 根据数据类型读取值
        switch (entry_info.type) {
            case NVS_TYPE_I8: {
                int8_t val;
                nvs_get_i8(handle, entry_info.key, &val);
                printf("%-9s | %-8s | int8_t    | %d\n", entry_info.namespace_name, entry_info.key, val);
                break;
            }
            case NVS_TYPE_U8: {
                uint8_t val;
                nvs_get_u8(handle, entry_info.key, &val);
                printf("%-9s | %-8s | uint8_t   | %u\n", entry_info.namespace_name, entry_info.key, val);
                break;
            }
            case NVS_TYPE_I16: {
                int16_t val;
                nvs_get_i16(handle, entry_info.key, &val);
                printf("%-9s | %-8s | int16_t   | %d\n", entry_info.namespace_name, entry_info.key, val);
                break;
            }
            case NVS_TYPE_U16: {
                uint16_t val;
                nvs_get_u16(handle, entry_info.key, &val);
                printf("%-9s | %-8s | uint16_t  | %u\n", entry_info.namespace_name, entry_info.key, val);
                break;
            }
            case NVS_TYPE_I32: {
                int32_t val;
                nvs_get_i32(handle, entry_info.key, &val);
                printf("%-9s | %-8s | int32_t   | %ld\n", entry_info.namespace_name, entry_info.key, val);
                break;
            }
            case NVS_TYPE_U32: {
                uint32_t val;
                nvs_get_u32(handle, entry_info.key, &val);
                printf("%-9s | %-8s | uint32_t  | %lu\n", entry_info.namespace_name, entry_info.key, val);
                break;
            }
            case NVS_TYPE_STR: {
                char buf[128];
                size_t len = sizeof(buf);
                nvs_get_str(handle, entry_info.key, buf, &len);
                printf("%-9s | %-8s | string    | %s\n", entry_info.namespace_name, entry_info.key, buf);
                break;
            }
            case NVS_TYPE_BLOB: {
                uint8_t buf[128];
                size_t len = sizeof(buf);
                nvs_get_blob(handle, entry_info.key, buf, &len);
                printf("%-9s | %-8s | blob      | [%u bytes]\n", entry_info.namespace_name, entry_info.key, len);
                break;
            }
            default:
                printf("%-9s | %-8s | unknown   | \n", entry_info.namespace_name, entry_info.key);
        }



        nvs_close(handle);
        res = nvs_entry_next(&it);
    }

    nvs_release_iterator(it);

//     nvs_handle_t handle;
//     esp_err_t err = nvs_open("wifi_config", NVS_READWRITE, &handle);
//     if (err != ESP_OK) return;
//     // 写入新的 SSID 到 NVS
//     char new_ssid[32] = "new_ssid";
//     nvs_set_str(handle, "sta.ssid", new_ssid);
//     nvs_commit(handle); // 必须提交！
//     nvs_close(handle);


// // nvs_handle_t handle;
// nvs_open("wifi_config", NVS_READONLY, &handle);
// char ssid[32];
// size_t ssid_len = sizeof(ssid);
// nvs_get_str(handle, "sta.ssid", ssid, &ssid_len);
// printf("Current SSID: %s\n", ssid); // 确认是否更新
// nvs_close(handle);
}
