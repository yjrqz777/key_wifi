/***************************************************************************************************
 * Author: yjrqz777 3210551161@qq.com
 * Date: 2025-03-10 19:22:19
 * LastEditTime: 2025-03-23 10:06:54
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

// void read_wifi_config(const char *file_path, char *ssid, size_t ssid_len, char *password, size_t password_len) {
//     // 打开文件
//     FILE *file = fopen(file_path, "r");
//     if (file == NULL) {
//         ESP_LOGE(TAG, "Failed to open file: %s", file_path);
//         return;
//     }

//     // 读取文件的第一行（SSID）
//     if (fgets(ssid, ssid_len, file) == NULL) {
//         ESP_LOGE(TAG, "Failed to read SSID from file: %s", file_path);
//         fclose(file);
//         return;
//     }
//     // 去掉SSID行末尾的换行符
//     ssid[strcspn(ssid, "\r\n")] = 0;

//     // 读取文件的第二行（密码）
//     if (fgets(password, password_len, file) == NULL) {
//         ESP_LOGE(TAG, "Failed to read password from file: %s", file_path);
//         fclose(file);
//         return;
//     }
//     // 去掉密码行末尾的换行符
//     password[strcspn(password, "\r\n")] = 0;

//     // 关闭文件
//     fclose(file);

//     // 输出读取到的配置
//     ESP_LOGI(TAG, "SSID: %s", ssid);
//     ESP_LOGI(TAG, "Password: %s", password);
// }


// void read_file(const char *file_path) {
//     // 打开文件
//     FILE *file = fopen(file_path, "r");
//     if (file == NULL) {
//         ESP_LOGE(TAG, "Failed to open file: %s", file_path);
//         return;
//     }

//     // 读取文件内容并输出到日志
//     char buffer[128];
//     while (fgets(buffer, sizeof(buffer), file) != NULL) {
//         ESP_LOGI(TAG, "%s", buffer);  // 输出每行内容
//     }

//     // 关闭文件
//     fclose(file);
//     ESP_LOGI(TAG, "File read completed: %s", file_path);
// }

// char ssid[64];
// char password[64];




// void list_dir(const char *path) {  
//     DIR *dir = opendir(path);  
//     if (dir == NULL) {  
//         ESP_LOGE(TAG, "Failed to open directory: %s", path);  
//         return;  
//     }  
  
//     struct dirent *entry;  
//     while ((entry = readdir(dir)) != NULL) {  
//         ESP_LOGI(TAG, "-+--------%s", entry->d_name);  
//     }  
//     closedir(dir);  

//     // read_file("/data/wificonfig.txt");
//     read_wifi_config("/data/wificonfig.txt", ssid, sizeof(ssid), password, sizeof(password));
// }  


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












