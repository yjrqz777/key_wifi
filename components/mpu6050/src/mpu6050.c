/**
 * @file mpu6050.c
 * @brief MPU6050 的 I2C 总线初始化与探测示例。
 */

#include "mpu6050.h"

#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "driver/i2c_master.h"


static const char *TAG = "mpu6050";


static i2c_master_bus_handle_t i2c0_bus_handle = NULL;
static i2c_master_dev_handle_t dev_handle = NULL;
static i2c_master_bus_config_t i2c0_bus_config = {
    .i2c_port = I2C_NUM_0,
    .sda_io_num = 17,
    .scl_io_num = 18,
    .clk_source = I2C_CLK_SRC_DEFAULT,
    .glitch_ignore_cnt = 7,
    .intr_priority = 0,
    .trans_queue_depth = 0,
    .flags = {
        .enable_internal_pullup = 1,
        .allow_pd = 0,
    }
};

static i2c_device_config_t mpu6050_config = {
    .dev_addr_length = I2C_ADDR_BIT_LEN_7,
    .device_address = 0x68,
    .scl_speed_hz = 100*1000,
    .scl_wait_us = 0,
    .flags = {
        .disable_ack_check = 0,
    }
};

/**
 * @brief 扫描 7 位 I2C 地址空间，以 8 行 16 列表格打印应答情况。
 * @param[in] bus_handle 已初始化的 I2C 主机总线句柄。
 * @note 表格行首是地址高 4 位，表头是低 4 位；格子里显示地址表示该地址有应答，`..` 表示无应答。
 * @note 只扫 0x00 到 0x7F。0x80 到 0xFF 是同一批地址带上读写位，不是新的设备。
 * @note 单地址超时 20 ms，地址全空时最坏耗时约 2.6 秒。
 * @note 总线必须处于同步模式，异步模式下 i2c_master_probe() 的结果不可信。
 */
void mpu6050_i2c_scan(i2c_master_bus_handle_t bus_handle)
{
    char Line[80];
    int Offset;
    uint8_t Row;
    uint8_t Column;
    uint16_t Address;
    uint8_t Found = 0;

    ESP_LOGI(TAG, "开始扫描 I2C 地址 0x00 到 0x7F");

    Offset = snprintf(Line, sizeof(Line), "     ");
    for (Column = 0; Column < 16; Column++)
    {
        Offset += snprintf(Line + Offset, sizeof(Line) - Offset, "%2X ", Column);
    }
    ESP_LOGI(TAG, "%s", Line);

    for (Row = 0; Row < 8; Row++)
    {
        Offset = snprintf(Line, sizeof(Line), "0x%X0 ", Row);
        for (Column = 0; Column < 16; Column++)
        {
            Address = (uint16_t)Row * 16 + Column;
            if (i2c_master_probe(bus_handle, Address, 20) == ESP_OK)
            {
                Offset += snprintf(Line + Offset, sizeof(Line) - Offset, "%02X ", Address);
                Found++;
            }
            else
            {
                Offset += snprintf(Line + Offset, sizeof(Line) - Offset, ".. ");
            }

        }
        ESP_LOGI(TAG, "%s", Line);
    }

    if (Found == 0)
    {
        ESP_LOGE(TAG, "总线上没有任何设备应答");
    }
    else
    {
        ESP_LOGI(TAG, "扫描结束，共 %d 个设备应答", Found);
    }
    ESP_LOGI(TAG, "test1");



}

/**
 * @brief 创建 I2C 主机总线、挂载 MPU6050 设备并探测其是否应答。
 * @note 探测结束后立即删除总线，设备句柄是函数局部变量，因此本函数返回后再无法读写该设备。
 * @warning 设备地址 0x68 与上面配置结构体中的地址重复写死，改动其中之一时必须同步。
 */
void mpu6050_init(void)
{
    esp_err_t ret;

    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c0_bus_config, &i2c0_bus_handle));
    vTaskDelay(1000/portTICK_PERIOD_MS);
    mpu6050_i2c_scan(i2c0_bus_handle);
    ESP_ERROR_CHECK(i2c_master_bus_add_device(i2c0_bus_handle, &mpu6050_config, &dev_handle));

    ret = i2c_master_probe(i2c0_bus_handle, 0x68, -1);
    ESP_LOGE(TAG, "i2c_master_probe = %d", ret);
    ESP_ERROR_CHECK(i2c_master_bus_rm_device(dev_handle));
    ESP_ERROR_CHECK(i2c_del_master_bus(i2c0_bus_handle));
}


/**
 * @brief 采集任务入口，先初始化总线，随后进入空循环。
 * @param[in] pvParameters 未使用的任务参数。
 * @note 任务不会自行退出，循环内目前没有任何采集动作。
 */
void iic_task(void *pvParameters)
{
    mpu6050_init();
    ESP_LOGI(TAG, "mpu6050_init");

    while (1) 
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
