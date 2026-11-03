/**
 * @file myfile.h
 * @brief 声明 SPI Flash FAT 文件系统的挂载接口。
 */

#ifndef MY_FILE_H
#define MY_FILE_H
#include <stdio.h>
#include <string.h>
#include "esp_err.h"
// #include <stddef.h>
// void list_dir(const char *path);
// void read_wifi_config(const char *file_path, char *ssid, size_t ssid_len, char *password, size_t password_len);
// void read_file(const char *file_path);
esp_err_t my_fatfs(void);

/**
 * @brief 重新挂载 storage 分区，用于查看模式与传输模式切换后刷新目录缓存。
 * @retval ESP_OK 重新挂载成功。
 * @retval 其他 卸载或挂载失败的错误码。
 * @note 失败时不会格式化分区。
 */
esp_err_t my_fatfs_remount(void);
#endif