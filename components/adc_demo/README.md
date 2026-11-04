# ADC 单次转换 + 校准要点

ESP32-S3 / ESP-IDF 5.5，依据 v5.5.6 中文文档「ADC 单次转换模式驱动」和「ADC 校准驱动程序」整理。

1. 头文件三个：`esp_adc/adc_oneshot.h`、`esp_adc/adc_cali.h`、`esp_adc/adc_cali_scheme.h`。
2. 构建依赖：组件 `CMakeLists.txt` 加 `REQUIRES esp_adc` 或 `PRIV_REQUIRES esp_adc`。
3. 建单元：`adc_oneshot_unit_init_cfg_t` 填 `unit_id`，`ulp_mode` 设 `ADC_ULP_MODE_DISABLE`，`clk_src` 填 0 用默认；再调 `adc_oneshot_new_unit()` 拿 `adc_oneshot_unit_handle_t`。
4. 定通道：确认引脚属于哪个 ADC 单元和第几通道，可用 `adc_oneshot_io_to_channel()` 反查，不要照猜。
5. 配通道：`adc_oneshot_chan_cfg_t` 填 `atten` 和 `bitwidth`，调 `adc_oneshot_config_channel()`；同一个单元可多次调用配多个通道。
6. 建校准句柄：`adc_cali_curve_fitting_config_t` 填 `unit_id`、`atten`、`bitwidth`，调 `adc_cali_create_scheme_curve_fitting()`。
7. 第 6 条的 `unit_id`、`atten`、`bitwidth` 必须和第 3、5 条完全一致，不一致不报错但 mV 是错的。
8. 读原始值：`adc_oneshot_read(handle, channel, &raw)`；返回 `ESP_ERR_TIMEOUT` 说明 ADC 正被 Wi-Fi 或 RNG 占用，此次结果无效。
9. 换算电压：`adc_cali_raw_to_voltage(cali_handle, raw, &mv)`，输出单位 mV；返回 `ESP_ERR_INVALID_STATE` 表示校准句柄没建成功。
10. 释放顺序：先 `adc_cali_delete_scheme_curve_fitting()`，再 `adc_oneshot_del_unit()`。
11. 量程由 `atten` 决定，具体数值查数据手册 ADC 特性表，代码里不要写死电压范围。
12. 精度补充：校准只修参考电压偏差，噪声还在，输入脚并 100 nF 电容并多次采样取平均。
13. 使用限制：`read` 不能在 ISR 里调用；cache 关闭时不要调这些 API，否则可能 `Illegal Instruction`。
14. 线程安全：`new_unit`、`config_channel`、`read` 可跨任务直接调；`del_unit` 不是线程安全的，不要和其他任务并发调。
15. 选单元：能用 ADC1 就用 ADC1，ADC2 会和 Wi-Fi 抢硬件，容易读超时。

## 本组件对应关系

- `src/adc_demo.c` 里 `adc_demo_init()` 对应第 3 到 6 条，`adc_demo_read_mv()` 对应第 8、9 条，`adc_demo_deinit()` 对应第 10 条。
- `include/adc_demo.h` 的三个声明就是外部调用面：初始化、读一次 mV、释放。
- 句柄和配置结构体建议做成组件内静态变量，不要外露，跨组件只用头文件里的三个函数。
