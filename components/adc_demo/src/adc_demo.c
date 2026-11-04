/**
 * @file adc_demo.c
 * @brief 演示 ADC 单次转换加校准的完整流程。
 * @details 用单次模式驱动取原始码值，再用曲线拟合校准方案换算成毫伏。
 */

#include "adc_demo.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "adc_demo";

static adc_oneshot_unit_handle_t adc_demo_handle = NULL;
static adc_cali_handle_t adc_demo_cali_handle = NULL;
static adc_channel_t eSampleChannel;   /* 采样通道，初始化时由 ADC_DEMO_GPIO_NUM 反查得到。 */

/* 单元配置，unit_id 会被 adc_demo_init() 按实际引脚覆盖。 */
static adc_oneshot_unit_init_cfg_t adc_demo_init_cfg =
{
    .unit_id = ADC_UNIT_1,
    // .clk_src = ADC_RTC_CLK_SRC_DEFAULT,
    .ulp_mode = ADC_ULP_MODE_DISABLE,
};

/* 通道采样配置，衰减档位和位宽必须与校准配置一致。 */
static adc_oneshot_chan_cfg_t adc_demo_chan_cfg =
{
    .atten = ADC_ATTEN_DB_12,
    .bitwidth = ADC_BITWIDTH_12,
};

/* 校准配置，chan 是保留字段，不参与曲线计算。 */
static adc_cali_curve_fitting_config_t adc_demo_cali_cfg =
{
    .unit_id = ADC_UNIT_1,
    .chan = ADC_CHANNEL_0,
    .atten = ADC_ATTEN_DB_12,
    .bitwidth = ADC_BITWIDTH_12,
};




/**
 * @brief 创建 ADC 单元、配置通道并建立校准方案。
 * @return 初始化成功返回 ESP_OK，否则返回驱动错误码。
 * @retval ESP_OK 两个句柄均已就绪，可以调用 adc_demo_read_mv()。
 * @retval ESP_ERR_NOT_FOUND ADC_DEMO_GPIO_NUM 不是 ADC 支持的引脚。
 * @warning 同一 ADC 单元重复初始化会返回 ESP_ERR_NOT_FOUND 并中止程序。
 */
esp_err_t adc_demo_init(void)
{
    adc_unit_t unit;
    adc_channel_t chan;
    esp_err_t ret = adc_oneshot_io_to_channel(ADC_DEMO_GPIO_NUM, &unit, &chan);

    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "GPIO%d 不支持 ADC: %s", ADC_DEMO_GPIO_NUM, esp_err_to_name(ret));
        return ret;
    }

    adc_demo_init_cfg.unit_id = unit;
    adc_demo_cali_cfg.unit_id = unit;
    adc_demo_cali_cfg.chan = chan;
    eSampleChannel = chan;

    ESP_ERROR_CHECK(adc_oneshot_new_unit(&adc_demo_init_cfg, &adc_demo_handle));
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_demo_handle, eSampleChannel, &adc_demo_chan_cfg));
    ESP_ERROR_CHECK(adc_cali_create_scheme_curve_fitting(&adc_demo_cali_cfg, &adc_demo_cali_handle));
    return ESP_OK;
}

/**
 * @brief 读取一次原始转换值并换算成毫伏电压。
 * @param[out] voltage_mv 接收电压值的指针，单位 mV。
 * @return 读取并换算成功返回 ESP_OK，否则返回驱动错误码。
 * @retval ESP_OK 电压值已写入 voltage_mv。
 * @retval ESP_ERR_TIMEOUT ADC 被其他驱动占用，voltage_mv 内容无效。
 * @note 采样若落在 ADC2，会与 Wi-Fi 争用硬件，读取可能超时。
 */
esp_err_t adc_demo_read_mv(int *voltage_mv)
{
    int RawValue = 0;
    esp_err_t ret = adc_oneshot_read(adc_demo_handle, eSampleChannel, &RawValue);

    if (ret != ESP_OK)
    {
        return ret;
    }

    return adc_cali_raw_to_voltage(adc_demo_cali_handle, RawValue, voltage_mv);
}

/**
 * @brief 预留的资源释放接口。
 * @note 当前实现为空，校准句柄和 ADC 单元句柄都还没有回收。
 */
void adc_demo_deinit(void)
{
}

/**
 * @brief ADC 采样任务，每 500 ms 读取一次电压并打印日志。
 * @param[in] pvParameter 未使用的任务参数。
 * @note 初始化失败时任务自行退出，不会带着空句柄继续运行。
 */
void adc_demo_task(void *pvParameter)
{
    static int voltage_mv;

    if (adc_demo_init() != ESP_OK)
    {
        ESP_LOGE(TAG, "ADC 初始化失败，任务退出");
        vTaskDelete(NULL);
        return;
    }

    while(1)
    {
        if (adc_demo_read_mv(&voltage_mv) == ESP_OK)
        {
            ESP_LOGI(TAG, "ADC voltage: %dmV", voltage_mv);
        }
        vTaskDelay(500 / portTICK_PERIOD_MS);
    }
}
