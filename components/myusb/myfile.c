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
 * @brief 提供 storage 分区 FAT 文件系统的挂载接口，并把该分区作为 MSC 磁盘提供给主机。
 * @details 挂载使用 wear levelling 方式，挂载点为 /spiflash；MSC 的容量与扇区读写回调
 *          直接访问同一 wear levelling 句柄，因此主机看到的就是 storage 分区本身。
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

#include "usb_util.h"
#include "usbd_msc.h"

#define TAG "FATFS" /* 日志标签 */

/* 分区挂载路径 */
#define FATFS_MOUNT_POINT "/spiflash"

/* 待挂载的 FAT 分区标签 */
#define FATFS_PARTITION_LABEL "storage"

/* 预留的 README 文件完整路径 */
#define FATFS_README_PATH FATFS_MOUNT_POINT "/README.txt"

/* 预留的 README 文件内容，首次挂载成功后写入 storage 分区 */
#define FILE_CONTENT "This is virtual Fat !!! \n\
  _____ _     _       _             _      _               _   _____     _     _ _ _  \n\
 |_   _| |__ (_)___  (_)___  __   _(_)_ __| |_ _   _  __ _| | |  ___|_ _| |_  | | | | \n\
   | | | '_ \\| / __| | / __| \\ \\ / / | '__| __| | | |/ _` | | | |_ / _` | __| | | | | \n\
   | | | | | | \\__ \\ | \\__ \\  \\ V /| | |  | |_| |_| | (_| | | |  _| (_| | |_  |_|_|_| \n\
   |_| |_| |_|_|___/ |_|___/   \\_/ |_|_|   \\__|\\__,_|\\__,_|_| |_|  \\__,_|\\__| (_|_|_) \n\
\n\
YJRQZDAP_HELP_JSON_MAX:64Byte:\n\
{\"ap\":{\"ssid\":\"test\",\"passwd\":\"12345678\"}} \n"

/* wear levelling 库实例句柄，未挂载时为 WL_INVALID_HANDLE */
static wl_handle_t s_wl_handle = WL_INVALID_HANDLE;

/**
 * @brief 在已挂载的分区中预留 README 文件。
 * @return ESP_OK 文件已存在或创建成功；其他为创建失败的错误码。
 * @note 文件已存在时保留原内容，不会覆盖主机写入的数据。
 */
static esp_err_t my_fatfs_reserve_readme(void)
{
    FILE *file = fopen(FATFS_README_PATH, "r");
    if (file != NULL) {
        fclose(file);
        return ESP_OK;
    }

    file = fopen(FATFS_README_PATH, "w");
    if (file == NULL) {
        ESP_LOGE(TAG, "Failed to create %s", FATFS_README_PATH);
        return ESP_FAIL;
    }

    fwrite(FILE_CONTENT, 1, sizeof(FILE_CONTENT) - 1, file);
    fclose(file);
    ESP_LOGI(TAG, "Reserved %s", FATFS_README_PATH);
    return ESP_OK;
}

/**
 * @brief 将 storage 分区以 wear levelling 方式挂载为 FAT 文件系统到 /spiflash。
 * @retval ESP_OK 挂载成功，并已完成 README 文件的预留。
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

    my_fatfs_reserve_readme();
    return ESP_OK;
}

/**
 * @brief 向主机报告 MSC 磁盘容量，容量来自 wear levelling 包装后的 storage 分区。
 * @param[in] busid USB 总线编号，本实现不使用。
 * @param[in] lun 逻辑单元号，本实现只有一个逻辑单元。
 * @param[out] block_num 逻辑块总数。
 * @param[out] block_size 逻辑块字节数，等于 wear levelling 扇区大小。
 * @note 未成功挂载时报告零容量，避免访问失效的 wear levelling 句柄。
 */
void usbd_msc_get_cap(uint8_t busid, uint8_t lun, uint32_t *block_num, uint32_t *block_size)
{
    (void)busid;
    (void)lun;

    *block_size = CONFIG_WL_SECTOR_SIZE;
    *block_num = 0;

    if (s_wl_handle == WL_INVALID_HANDLE) {
        ESP_LOGE(TAG, "MSC capacity unavailable, FATFS is not mounted");
        return;
    }

    *block_size = (uint32_t)wl_sector_size(s_wl_handle);
    *block_num = (uint32_t)(wl_size(s_wl_handle) / wl_sector_size(s_wl_handle));
}

/**
 * @brief 主机读取 MSC 扇区，数据直接取自 wear levelling 包装后的 storage 分区。
 * @param[in] busid USB 总线编号，本实现不使用。
 * @param[in] lun 逻辑单元号，本实现只有一个逻辑单元。
 * @param[in] sector 起始逻辑块号。
 * @param[out] buffer 接收数据的缓冲区，长度不小于 length。
 * @param[in] length 读取字节数，为逻辑块大小的整数倍。
 * @retval 0 读取成功。
 * @retval -1 未挂载或读取失败。
 */
int usbd_msc_sector_read(uint8_t busid, uint8_t lun, uint32_t sector, uint8_t *buffer, uint32_t length)
{
    (void)busid;
    (void)lun;

    if (s_wl_handle == WL_INVALID_HANDLE) {
        return -1;
    }

    size_t addr = (size_t)sector * wl_sector_size(s_wl_handle);
    esp_err_t err = wl_read(s_wl_handle, addr, buffer, length);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "MSC read failed at sector %lu (%s)", (unsigned long)sector, esp_err_to_name(err));
        return -1;
    }
    return 0;
}

/**
 * @brief 主机写入 MSC 扇区，先擦除再写入 wear levelling 包装后的 storage 分区。
 * @param[in] busid USB 总线编号，本实现不使用。
 * @param[in] lun 逻辑单元号，本实现只有一个逻辑单元。
 * @param[in] sector 起始逻辑块号。
 * @param[in] buffer 待写入的数据。
 * @param[in] length 写入字节数，为逻辑块大小的整数倍。
 * @retval 0 写入成功。
 * @retval -1 未挂载、擦除失败或写入失败。
 * @warning 主机与设备侧 FATFS 不应同时访问同一分区；主机写入后设备侧的目录缓存可能已过期。
 */
int usbd_msc_sector_write(uint8_t busid, uint8_t lun, uint32_t sector, uint8_t *buffer, uint32_t length)
{
    (void)busid;
    (void)lun;

    if (s_wl_handle == WL_INVALID_HANDLE) {
        return -1;
    }

    size_t addr = (size_t)sector * wl_sector_size(s_wl_handle);
    esp_err_t err = wl_erase_range(s_wl_handle, addr, length);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "MSC erase failed at sector %lu (%s)", (unsigned long)sector, esp_err_to_name(err));
        return -1;
    }

    err = wl_write(s_wl_handle, addr, buffer, length);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "MSC write failed at sector %lu (%s)", (unsigned long)sector, esp_err_to_name(err));
        return -1;
    }
    return 0;
}
