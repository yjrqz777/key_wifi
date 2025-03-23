/***************************************************************************************************
 * Author: yjrqz777 3210551161@qq.com
 * Date: 2025-03-10 19:22:19
 * LastEditTime: 2025-03-23 21:58:23
 * LastEditors: yjrqz777 3210551161@qq.com
 * Description: 
 * FilePath: /key_wifi/components/myusb/myfile.c
 * @YJRQZ777
***************************************************************************************************/

#include "myfile.h"
#include <dirent.h>
#include <errno.h>

#include "esp_vfs.h"
#include "esp_vfs_fat.h"
#include "esp_system.h"

#include "sdkconfig.h"

#include <stdio.h>
#include <string.h>
#include "esp_log.h"

#define TAG "FATFS"


// // Mount path for the partition
// const char *base_path = "/spiflash";

// // Handle of the wear levelling library instance
// static wl_handle_t s_wl_handle = WL_INVALID_HANDLE;
// static const esp_partition_t *msc_partition;
// void my_fatfs() 
// {

//     ESP_LOGI(TAG, "Mounting FAT filesystem");
//     // To mount device we need name of device partition, define base_path
//     // and allow format partition in case if it is new one and was not formatted before
//     const esp_vfs_fat_mount_config_t mount_config = {
//             .max_files = 4,
//             .format_if_mount_failed = false,
//             .allocation_unit_size = CONFIG_WL_SECTOR_SIZE
//     };
//     esp_err_t err;
//     err = esp_vfs_fat_spiflash_mount(base_path, "storage", &mount_config, &s_wl_handle);

//     if (err != ESP_OK) {
//         ESP_LOGE(TAG, "Failed to mount FATFS (%s)", esp_err_to_name(err));
//         return;
//     }
//     ESP_LOGI(TAG, "Opening file");
// }















