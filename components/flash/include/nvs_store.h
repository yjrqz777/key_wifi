#ifndef NVS_STORE_H
#define NVS_STORE_H

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "nvs.h"

/**
 * @brief nvs_read_data() 的读取结果，按实际存储类型填充对应成员。
 */
typedef struct {
    nvs_type_t type;      /*!< 实际存储类型，取值见 NVS_TYPE_* */
    union {
        int8_t   i8;
        uint8_t  u8;
        int16_t  i16;
        uint16_t u16;
        int32_t  i32;
        uint32_t u32;
        int64_t  i64;
        uint64_t u64;
    } num;                /*!< 整数类型时有效，按 type 选择成员 */
    size_t len;           /*!< 字符串字节数（含结尾 '\0'）；blob 字节数 */
} nvs_value_t;

esp_err_t nvs_save_data(const char* namespace_name, const char* key, const char* value);
/**
 * @brief 读取任意类型键值，自动识别存储类型。
 * @param[in]  namespace_name 已存在的命名空间名，最长 15 字符。
 * @param[in]  key            键名，最长 15 字符。
 * @param[out] out_value      结果结构，type 返回实际类型，整数填入 num，字符串/blob 返回长度。
 * @param[out] buf            字符串/blob 的接收缓冲区；传 NULL 时只查询所需长度。
 * @param[in]  buf_size       缓冲区大小。
 * @retval ESP_OK                 读取成功；buf 为 NULL 时表示长度查询成功。
 * @retval ESP_ERR_NVS_NOT_FOUND  命名空间或键不存在。
 * @retval ESP_ERR_NVS_INVALID_LENGTH buf 不足，out_value->len 已回填所需大小。
 * @retval ESP_ERR_NVS_TYPE_MISMATCH 未知类型。
 * @note 整数类型忽略 buf 和 buf_size；每次调用都会开关一次命名空间句柄。
 */
esp_err_t nvs_read_data(const char *namespace_name, const char *key,
                        nvs_value_t *out_value, void *buf, size_t buf_size);

void NvsShowAllEntries(void);
#endif // NVS_STORE_H
