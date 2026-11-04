/**
 * @file mpu6050.c
 * @brief MPU6050 的 I2C 总线初始化与探测示例。
 */

#include "mpu6050.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "driver/i2c_master.h"


static const char *TAG = "mpu6050";


static i2c_master_bus_handle_t i2c0_bus_handle = NULL;
static i2c_master_dev_handle_t dev_handle = NULL;
static i2c_master_bus_config_t i2c0_bus_config = {
    .i2c_port = I2C_NUM_0,
    .sda_io_num = 13,
    .scl_io_num = 14,
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
    .scl_speed_hz = 400*1000,
    .scl_wait_us = 0,
    .flags = {
        .disable_ack_check = 0,
    }
};

/**
 * @brief 创建 I2C 主机总线、挂载 MPU6050 设备并探测其是否应答。
 * @note 探测结束后立即删除总线，设备句柄是函数局部变量，因此本函数返回后再无法读写该设备。
 * @warning 设备地址 0x68 与上面配置结构体中的地址重复写死，改动其中之一时必须同步。
 */
void mpu6050_init(void)
{
    esp_err_t ret;

    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c0_bus_config, &i2c0_bus_handle));
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
