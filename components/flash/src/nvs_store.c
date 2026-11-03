#include <string.h>

#include "nvs_store.h"
#include "nvs_flash.h"
#include "esp_log.h"


static const char *TAG = "NVS_STORE";


esp_err_t nvs_save_data(const char* namespace_name, const char* key, const char* value)
{
    if (namespace_name == NULL || key == NULL || value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    nvs_handle_t handle;
    ESP_ERROR_CHECK(nvs_open(namespace_name, NVS_READWRITE, &handle));
    ESP_ERROR_CHECK(nvs_set_str(handle, key, value));
    
    esp_err_t commit_err = nvs_commit(handle);
    nvs_close(handle);
    return commit_err;
}

esp_err_t nvs_read_data(const char *namespace_name, const char *key,
                        nvs_value_t *out_value, void *buf, size_t buf_size)
{
    if (namespace_name == NULL || key == NULL || out_value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(out_value, 0, sizeof(*out_value));

    nvs_handle_t handle;
    esp_err_t err = nvs_open(namespace_name, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "open namespace %s failed: %s", namespace_name, esp_err_to_name(err));
        return err;
    }

    /* 先查类型，再按类型分发，避免读错类型拿不到值。 */
    nvs_type_t type;
    err = nvs_find_key(handle, key, &type);
    if (err != ESP_OK) {
        nvs_close(handle);
        return err;
    }
    out_value->type = type;

    size_t len = buf_size;
    switch (type) {
    case NVS_TYPE_I8:  err = nvs_get_i8(handle, key, &out_value->num.i8);   break;
    case NVS_TYPE_U8:  err = nvs_get_u8(handle, key, &out_value->num.u8);   break;
    case NVS_TYPE_I16: err = nvs_get_i16(handle, key, &out_value->num.i16); break;
    case NVS_TYPE_U16: err = nvs_get_u16(handle, key, &out_value->num.u16); break;
    case NVS_TYPE_I32: err = nvs_get_i32(handle, key, &out_value->num.i32); break;
    case NVS_TYPE_U32: err = nvs_get_u32(handle, key, &out_value->num.u32); break;
    case NVS_TYPE_I64: err = nvs_get_i64(handle, key, &out_value->num.i64); break;
    case NVS_TYPE_U64: err = nvs_get_u64(handle, key, &out_value->num.u64); break;
    case NVS_TYPE_STR:
        /* buf 为 NULL 时 nvs_get_str 只回填所需长度。 */
        err = nvs_get_str(handle, key, (char *)buf, &len);
        out_value->len = len;
        break;
    case NVS_TYPE_BLOB:
        err = nvs_get_blob(handle, key, buf, &len);
        out_value->len = len;
        break;
    default:
        err = ESP_ERR_NVS_TYPE_MISMATCH;
        break;
    }

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "read %s/%s type %d failed: %s",
                 namespace_name, key, (int)type, esp_err_to_name(err));
    }

    nvs_close(handle);
    return err;
}

esp_err_t nvs_read_wifidata(const char* namespace_name, char* ssid, char* password)
{
    char u8ssid[33] = {0};        // SSID 最大 32 字符 + NULL 终止符
    char u8password[65] = {0};
    esp_err_t err;
    nvs_handle_t handle;
    ESP_LOGI(TAG, "Reading WiFi data from namespace: %s", namespace_name);
    // 1. 打开 NVS 命名空间
    err = nvs_open(namespace_name, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open namespace %s: %s", namespace_name, esp_err_to_name(err));
        return 0;
    }
    
    // 2. 读取 SSID
    size_t required_size = 0;
    
    // 第一次调用：获取 SSID 所需缓冲区大小
    err = nvs_get_str(handle, "ssid", NULL, &required_size);
    if (err == ESP_OK && required_size <= sizeof(u8ssid)) {
        // 第二次调用：实际读取 SSID 值
        err = nvs_get_str(handle, "ssid", u8ssid, &required_size);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to read SSID: %s", esp_err_to_name(err));
            nvs_close(handle);
            return 0;
        }
    }
    else {
        ESP_LOGE(TAG, "Failed to get SSID length: %s", esp_err_to_name(err));
        nvs_close(handle);
        return 0;
    }
    
    // 3. 读取密码
    required_size = 0;
    err = nvs_get_str(handle, "pass", NULL, &required_size);
    if (err == ESP_OK && required_size <= sizeof(u8password)) {
        err = nvs_get_str(handle, "pass", u8password, &required_size);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to read password: %s", esp_err_to_name(err));
            nvs_close(handle);
            return 0;
        }
    }
    else {
        ESP_LOGE(TAG, "Failed to get password length: %s", esp_err_to_name(err));
        nvs_close(handle);
        return 0;
    }
    
    // 4. 验证凭据是否完整有效
    if (strlen(u8ssid) > 0 && strlen(u8password) > 0) {
        ESP_LOGI(TAG, "读取到 WiFi 凭据: SSID=%s, Password=%s", u8ssid, u8password);
    } else {
        ESP_LOGW(TAG, "未找到有效的 WiFi 凭据");
        nvs_close(handle);
        return 0;
    }
    memcpy(ssid, u8ssid, sizeof(u8ssid));
    memcpy(password, u8password, sizeof(u8password));
    nvs_close(handle);
    return 1;
}




void NvsShowAllEntries(void)
{
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
}
