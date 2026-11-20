/**
 * @file mpu6050.h
 * @brief 提供 MPU6050 采集任务的入口声明。
 */

#ifndef __MPU_6050_H__
#define __MPU_6050_H__

#include "driver/i2c_master.h"

void iic_task(void *pvParameters);
void mpu6050_i2c_scan(i2c_master_bus_handle_t bus_handle);

#endif /* __MPU_6050_H__ */
