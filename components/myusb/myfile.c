/***************************************************************************************************
 * Author: yjrqz777 3210551161@qq.com
 * Date: 2025-03-10 19:22:19
 * LastEditTime: 2025-03-23 21:58:23
 * LastEditors: yjrqz777 3210551161@qq.com
 * Description: 
 * FilePath: /key_wifi/components/myusb/myfile.c
 * @YJRQZ777
***************************************************************************************************/

/**
 * @file myfile.c
 * @brief 提供 SPI Flash 上 storage 分区的 FAT 文件系统挂载接口。
 * @details 使用 wear levelling 方式挂载，挂载点在 /spiflash，挂载失败时可自动格式化分区。
 */

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

#define TAG "FATFS" /* 日志标签 */

/* 分区挂载路径 */
#define FATFS_MOUNT_POINT "/spiflash"

/* 待挂载的 FAT 分区标签 */
#define FATFS_PARTITION_LABEL "storage"

/* wear levelling 库实例句柄，未挂载时为 WL_INVALID_HANDLE */
static wl_handle_t s_wl_handle = WL_INVALID_HANDLE;

/**
 * @brief 将 storage 分区以 wear levelling 方式挂载为 FAT 文件系统到 /spiflash。
 * @retval ESP_OK 挂载成功。
 * @retval 其他 挂载失败，返回底层挂载接口的错误码。
 * @note 挂载失败时会把文件内静态句柄复位为 WL_INVALID_HANDLE，避免后续继续使用失效句柄。
 * @note 成功与失败结果都通过 ESP_LOGE 输出，串口上以 E 级别呈现。
 */
esp_err_t my_fatfs(void)
{
    ESP_LOGI(TAG, "Mounting FAT filesystem");

    /* 最多同时打开 4 个文件；挂载失败时格式化分区；分配单元大小与 wear levelling 扇区一致 */
    const esp_vfs_fat_mount_config_t mount_config = {
        .max_files = 4,
        .format_if_mount_failed = true,
        .allocation_unit_size = CONFIG_WL_SECTOR_SIZE,
    };

    esp_err_t err = esp_vfs_fat_spiflash_mount_rw_wl(FATFS_MOUNT_POINT, FATFS_PARTITION_LABEL,
                                                     &mount_config, &s_wl_handle);
    if (err != ESP_OK) {
        /* 挂载失败后句柄可能已被部分初始化，复位为无效句柄 */
        s_wl_handle = WL_INVALID_HANDLE;
        ESP_LOGE(TAG, "Failed to mount FATFS on %s (%s, 0x%x)", FATFS_MOUNT_POINT,
                 esp_err_to_name(err), err);
        return err;
    }

    ESP_LOGE(TAG, "FATFS mounted on %s", FATFS_MOUNT_POINT);
    return ESP_OK;
}















