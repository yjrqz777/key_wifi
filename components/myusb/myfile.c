#include "myfile.h"
#include <dirent.h>
#include <errno.h>


#include <stdio.h>
#include <string.h>
#include "esp_log.h"

#define TAG "WIFI_CONFIG"

void read_wifi_config(const char *file_path, char *ssid, size_t ssid_len, char *password, size_t password_len) {
    // 打开文件
    FILE *file = fopen(file_path, "r");
    if (file == NULL) {
        ESP_LOGE(TAG, "Failed to open file: %s", file_path);
        return;
    }

    // 读取文件的第一行（SSID）
    if (fgets(ssid, ssid_len, file) == NULL) {
        ESP_LOGE(TAG, "Failed to read SSID from file: %s", file_path);
        fclose(file);
        return;
    }
    // 去掉SSID行末尾的换行符
    ssid[strcspn(ssid, "\r\n")] = 0;

    // 读取文件的第二行（密码）
    if (fgets(password, password_len, file) == NULL) {
        ESP_LOGE(TAG, "Failed to read password from file: %s", file_path);
        fclose(file);
        return;
    }
    // 去掉密码行末尾的换行符
    password[strcspn(password, "\r\n")] = 0;

    // 关闭文件
    fclose(file);

    // 输出读取到的配置
    ESP_LOGI(TAG, "SSID: %s", ssid);
    ESP_LOGI(TAG, "Password: %s", password);
}


void read_file(const char *file_path) {
    // 打开文件
    FILE *file = fopen(file_path, "r");
    if (file == NULL) {
        ESP_LOGE(TAG, "Failed to open file: %s", file_path);
        return;
    }

    // 读取文件内容并输出到日志
    char buffer[128];
    while (fgets(buffer, sizeof(buffer), file) != NULL) {
        ESP_LOGI(TAG, "%s", buffer);  // 输出每行内容
    }

    // 关闭文件
    fclose(file);
    ESP_LOGI(TAG, "File read completed: %s", file_path);
}

char ssid[64];
char password[64];




void list_dir(const char *path) {  
    DIR *dir = opendir(path);  
    if (dir == NULL) {  
        ESP_LOGE(TAG, "Failed to open directory: %s", path);  
        return;  
    }  
  
    struct dirent *entry;  
    while ((entry = readdir(dir)) != NULL) {  
        ESP_LOGI(TAG, "-+--------%s", entry->d_name);  
    }  
    closedir(dir);  

    // read_file("/data/wificonfig.txt");
    read_wifi_config("/data/wificonfig.txt", ssid, sizeof(ssid), password, sizeof(password));
}  