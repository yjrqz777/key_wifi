# I2C 使用要点

ESP32-S3 / ESP-IDF 5.5，依据 v5.5.6 中文文档「I2C 接口」章节整理。

1. 硬件：两根开漏线，SDA（Serial Data Line，串行数据线）和 SCL（Serial Clock Line，串行时钟线），需要外部上拉电阻，常用 2 到 5 kΩ（1 到 10 kΩ 区间均可）。
2. 端口：ESP32-S3 有 2 个 I2C 控制器，配置里用 `i2c_port` 选一个。
3. 头文件：用 `driver/i2c_master.h`，不要同时包含旧的 `driver/i2c.h`，新旧驱动不能共存。
4. 总线：填 `i2c_master_bus_config_t` 后调 `i2c_new_master_bus()`，必填 `i2c_port`、`sda_io_num`、`scl_io_num`、`clk_source`，`glitch_ignore_cnt` 通常填 7。
5. 设备：填 `i2c_device_config_t` 后调 `i2c_master_bus_add_device()`，`device_address` 填 7 位原始地址（不带读写位），`scl_speed_hz` 不超过 400 kHz。
6. 收发 API：`i2c_master_transmit()` 只写，`i2c_master_receive()` 只读，`i2c_master_transmit_receive()` 先写后读（读寄存器用，中间不插 STOP 条件）；最后一个参数是超时毫秒数，`-1` 表示永久等待。
7. 探测：`i2c_master_probe()` 返回 `ESP_OK` 说明该地址上有设备响应。
8. 释放：先 `i2c_master_bus_rm_device()`，再 `i2c_del_master_bus()`，顺序不能反。
9. 从机模式才用 `driver/i2c_slave.h` 加 `on_request` / `on_receive` 回调，主机通信不需要。
10. 内部上拉不够用，探测报超时（`ESP_ERR_TIMEOUT`）基本都是没接外部上拉电阻。

## 本组件对应关系

`src/mpu6050.c` 的结构体初始化和 `mpu6050_init()` 就是第 4、5 步，参数为端口 0、SDA 21、SCL 22、地址 0x68、400 kHz。拿到 `dev_handle` 后直接调第 6 条里的函数即可收发，5 到 6 之间没有别的步骤。
