# key_wifi (ESP32-S3)

ESP32-S3 多功能固件，集成了：

- CherryUSB CDC ACM 虚拟串口桥接（PC <-> UART 设备）
- DAPLink 调试接口能力
- SoftAP + WebServer 控制页面
- WS2812 控制
- 串口数据网页监控（含方向标记）

## 主要功能

### 1. USB CDC 虚拟串口

- 电脑通过 USB CDC 向 ESP32 发送数据，转发到 UART 设备
- UART 设备返回数据，再通过 USB CDC 回传到电脑
- 支持在日志中区分方向：
  - `[PC->DEV]`
  - `[DEV->PC]`

相关文件：

- `components/myusb/myusb.c`
- `components/myusb/include/myusb.h`

### 2. Wi-Fi 与网页控制

默认启动 AP 模式，并启动 HTTP 服务。

网页功能包含：

- RGB 控制
- AP/STA 开关控制
- 串口日志显示与清除
- 独立串口监控页

主要路由：

- `/` 首页
- `/serial_view` 串口监控页
- `/serial` 串口日志数据接口
- `/wifi_status` AP/STA 状态查询
- `/wifi_control?ap=1&sta=0` AP/STA 切换
- `/rgbcontrol` RGB 控制

相关文件：

- `components/all_control/src/webserver.c`
- `components/all_control/src/mywifi.c`
- `components/all_control/root.html`
- `components/all_control/serial_view.html`

### 3. DAP 与 USB 相关配置

- CherryUSB 配置：`components/cherry-embedded__cherryusb/osal/idf/usb_config.h`
- DAP 配置：`components/DAP/Include/DAP_config.h`

## 编译与烧录

先进入 ESP-IDF 环境：

```bash
source ~/esp/v5.1.5/esp-idf/export.sh
```

编译：

```bash
idf.py build
```

烧录并查看日志：

```bash
idf.py -p <PORT> flash monitor
```

查看体积分析：

```bash
idf.py size
idf.py size-components
idf.py size-files
```

## 分区说明

当前分区表 `partitions.csv` 使用：

- `factory` / `ota_0` / `ota_1`：每个 2MB
- `storage`：9MB

## 版本记录

### release1.0.0

- 基于 ESP32-S3 核心板
- WS2812
- 按键控制 Wi-Fi AP
- CDC ACM 虚拟串口
- DAPLINK
- WebServer

### release1.0.1

- CDC ACM 虚拟串口 `help` 命令


### release1.0.2

- idf -> V5.5