/**
 * @file adc_demo.h
 * @brief 提供 ADC 单次转换加校准的初始化和毫伏读取接口。
 */

#ifndef __ADC_DEMO_H__
#define __ADC_DEMO_H__

#include <stdint.h>
#include "esp_err.h"

#define ADC_DEMO_GPIO_NUM   (1)    /* 采样引脚，必须是 ADC 支持的 GPIO */

esp_err_t adc_demo_init(void);
esp_err_t adc_demo_read_mv(int *voltage_mv);
void adc_demo_deinit(void);
void adc_demo_task(void *pvParameter);

#endif /* __ADC_DEMO_H__ */
