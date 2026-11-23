# ESP32 BLE 上位机通信协议 v1

本文档定义设备与上位机之间的 BLE 通信协议第一版，只覆盖两项功能：设置 WS2812 颜色、转发 CDC 串口数据。

本文档是协议规范，不是实现说明；设备侧 BLE 代码尚未编写，联调前以本文档为准，实现落地后再补充实测数据。

## 1. 文档范围

覆盖：

- 上位机设置 WS2812 的 RGB 颜色
- UART2 串口数据双向转发：目标板发来的数据显示在上位机，上位机也能向目标板发送数据

不覆盖：

- Wi-Fi 开关与状态查询
- 配对、绑定、加密
- OTA 升级
- 超过单帧长度的数据

同一批功能在 Wi-Fi 下的 HTTP 版本见 [http_communication_protocol.md](http_communication_protocol.md)。

## 2. 硬件与配置前提

| 项目 | 说明 |
|---|---|
| 芯片 | ESP32-S3 |
| 蓝牙能力 | 仅 BLE 5.0，不支持经典蓝牙 BR/EDR，因此没有 SPP，上位机不会出现蓝牙虚拟串口 |
| 主机协议栈 | 推荐 NimBLE，占用低于 Bluedroid |
| 射频共存 | ESP32-S3 只有一套 2.4 GHz 射频，Wi-Fi 与 BLE 由 `ESP_COEX` 分时共存 |
| 当前 sdkconfig | `CONFIG_ESP_COEX_ENABLED=y` 已开启，`CONFIG_BT_ENABLED` 未开启 |

启用蓝牙的配置路径：`idf.py menuconfig` → Component config → Bluetooth → 选中 Enable，主机栈选 NimBLE。

## 3. 传输方式

### 3.1 GATT 结构

设备暴露一个主服务，内含两个特征值：

| 角色 | UUID | 属性 | 用途 |
|---|---|---|---|
| 服务 | `6b65f000-7769-4f9a-8d21-7c3b5e9a1f04` | Primary | 协议总入口 |
| 写特征 | `6b65f001-7769-4f9a-8d21-7c3b5e9a1f04` | Write、Write Without Response | 上位机下发命令 |
| 通知特征 | `6b65f002-7769-4f9a-8d21-7c3b5e9a1f04` | Notify | 设备上行数据 |

- 通知特征自带 CCCD，上位机必须先写 CCCD 开启通知，否则收不到任何数据。
- 不使用指示（Indicate），全部走通知，省掉逐包确认的往返。
- 不使用 GATT 描述符传递额外配置，所有语义都在帧类型里。

### 3.2 广播

- 设备名：`key_wifi-ble`
- 广播包携带完整的 128 位服务 UUID，上位机可据此过滤设备。
- 广播间隔建议 100 到 300 ms，连接建立后停止广播。

### 3.3 连接参数

| 参数 | 建议值 | 说明 |
|---|---|---|
| ATT MTU | 247 | 对应 ATT 载荷 244 字节，帧载荷上限 241 字节 |
| 连接间隔 | 7.5 到 15 ms | 间隔越小吞吐越高，功耗也越高 |
| 从机延迟 | 0 | 不允许跳包，保证延迟稳定 |
| 监督超时 | 4 s | |
| PHY | 优先 2M，回退 1M | 2M 只影响空口速率，不改变帧格式 |

MTU 由双方协商，设备按协商结果分片，不能假定一定是 247。iPhone 常见协商结果是 185，此时帧载荷上限降为 179 字节。

## 4. 帧格式

所有数据都封装成帧，一帧对应一个 ATT 写请求或一个通知。

```text
+-----------+-----------+-----------+------------------+
|   TYPE    |  LEN_LO   |  LEN_HI   |     PAYLOAD      |
|    1B     |    1B     |    1B     |     LEN 字节     |
+-----------+-----------+-----------+------------------+
```

| 字段 | 长度 | 含义 |
|---|---|---|
| `TYPE` | 1 字节 | 帧类型，取值见第 5 节 |
| `LEN` | 2 字节，小端 | 载荷字节数，不含帧头 |
| `PAYLOAD` | `LEN` 字节 | 帧内容，可以为空 |

约束：

- 一帧必须完整落在一个 ATT 写或通知里，不允许跨包拼接。
- 载荷上限为 `MTU - 6`；MTU 为 247 时上限 241 字节，MTU 为 185 时上限 179 字节。
- 载荷是纯二进制，不做转义，`0x00`、`0x0A` 等字节都可以直接出现。
- 不加帧尾、不加 CRC。BLE 链路层自带 CRC 与 L2CAP 重传，重复校验不增加可靠性。
- `TYPE` 的最高位表示方向：`0x0x` 为上位机发往设备，`0x8x` 为设备发往上位机。

## 5. 帧类型总表

| TYPE | 方向 | 名称 | 载荷 | 载荷上限 |
|---|---|---|---|---|
| `0x01` | 上位机→设备 | SET_RGB | R、G、B 各 1 字节 | 3 |
| `0x02` | 上位机→设备 | SERIAL_TX | 串口发送数据 | MTU 减 6 |
| `0x81` | 设备→上位机 | SERIAL_RX | 串口接收数据 | MTU 减 6 |
| `0x82` | 设备→上位机 | STATUS | 状态码 1 字节 + 累计丢弃字节数 4 字节 | 5 |

- 设备收到未定义的 `TYPE` 时直接丢弃该帧，并回一帧 `STATUS`，状态码为 `0x02`。
- `0x03` 到 `0x7F`、`0x83` 到 `0xFF` 保留，供后续版本扩展，上位机应忽略不认识的帧类型而不是断开连接。

## 6. SET_RGB

### 6.1 请求

| 字节 | 内容 |
|---|---|
| `TYPE` | `0x01` |
| `LEN` | `0x0003` |
| `PAYLOAD[0]` | R，取值 0 到 255 |
| `PAYLOAD[1]` | G，取值 0 到 255 |
| `PAYLOAD[2]` | B，取值 0 到 255 |

示例，设置纯红：

```text
01 03 00 FF 00 00
```

设置暗橙：

```text
01 03 00 FF 80 20
```

设置熄灭：

```text
01 03 00 00 00 00
```

### 6.2 行为

- 设备收到后立即调用 `ws2812_send()` 刷新灯珠，不需要应答。
- 越界值按 0 到 255 限幅处理，上位机仍应自行保证范围。
- 连续下发时设备只保留最新一次的颜色，中间值可能被覆盖，这是预期行为，上位机不需要等待确认。
- 该命令使用 Write With Response 发送，保证命令不丢。

### 6.3 设备侧对应实现

送到 `ws2812_send()` 时，`tws2812Def` 结构体只填 `r`、`g`、`b`，并把 `v` 置 0。这样会跳过 HSV 到 RGB 的换算，直接使用给定的 RGB 值，对应 [../components/ws2812/src/ws2812.c](../components/ws2812/src/ws2812.c) 中 `tws2812Data.v != 0` 的判断分支。

## 7. SERIAL_TX

### 7.1 请求

| 字节 | 内容 |
|---|---|
| `TYPE` | `0x02` |
| `LEN` | 载荷字节数，小端 |
| `PAYLOAD` | 要写到 UART2 TX 的原始字节 |

示例，发送字符串 `AT+RST\r\n`：

```text
02 08 00 41 54 2B 52 53 54 0D 0A
```

### 7.2 行为

- 载荷原样写入 UART2 发送环形缓冲区，与 USB CDC 共用同一个缓冲。
- 单帧超过上限时由上位机自行拆成多帧，设备不做跨帧重组。
- 该方向建议使用 Write Without Response，避免每帧等待确认拖慢吞吐；代价是极端拥塞时可能丢帧，需要可靠传输时改用 Write With Response。
- 缓冲区空间不足时丢弃新数据，丢弃字节数累加到 `STATUS` 的计数里。

## 8. SERIAL_RX

### 8.1 帧结构

| 字节 | 内容 |
|---|---|
| `TYPE` | `0x81` |
| `LEN` | 载荷字节数，小端 |
| `PAYLOAD` | UART2 RX 收到的原始字节 |

示例，目标板打印 `OK\r\n`：

```text
81 04 00 4F 4B 0D 0A
```

### 8.2 行为

- 这是串口接收方向的实时流，不是快照。上位机把连续收到的多个 `SERIAL_RX` 载荷按顺序拼接，就是完整的字节流。
- 载荷边界没有任何语义，可能从一行的中间切开，上位机不能按帧边界解释文本。
- 数据为原始字节，不区分文本和二进制，也不做不可打印字符替换。这一点与 HTTP 版本 `/serial` 不同，那个接口会把不可打印字符换成点号。
- 设备侧攒够一帧能装下的字节数就发一帧，不会因为等待换行而延迟。

## 9. STATUS

### 9.1 帧结构

| 字节 | 内容 |
|---|---|
| `TYPE` | `0x82` |
| `LEN` | `0x0005` |
| `PAYLOAD[0]` | 状态码 |
| `PAYLOAD[1..4]` | 开机以来累计丢弃的字节数，小端 32 位 |

状态码定义：

| 状态码 | 含义 |
|---|---|
| `0x00` | 正常，心跳 |
| `0x01` | 串口缓冲溢出，已丢数据 |
| `0x02` | 收到无法识别的帧 |

示例，累计丢弃 0 字节：

```text
82 05 00 00 00 00 00 00
```

累计丢弃 300 字节：

```text
82 05 00 01 2C 01 00 00
```

### 9.2 行为

- 每秒最多上报一次，且只在内容变化时发，避免占满空口。
- 丢弃计数是开机以来的累计值，不是增量。上位机用两次读数相减得到本次丢失量。
- 上位机应把丢弃量显示出来，而不是当成无数据。

## 10. 串口数据流与缓冲

接收方向：

1. UART2 收到的字节先进设备侧环形缓冲，建议 8 KB。
2. 缓冲里攒够一帧能装下的字节数（MTU 减 6）就发一个通知。
3. 通知发送失败或空口拥塞时不丢字节，数据留在缓冲等下一次。
4. 缓冲写满时丢弃最旧数据，累计丢弃量写进 `STATUS`。

发送方向：

1. `SERIAL_TX` 载荷写入 UART 发送环形缓冲，容量由 `CONFIG_UARTTX_RINGBUF_SIZE` 决定，当前 4096 字节。
2. 现有搬运逻辑会自动把它送进 UART 外设，BLE 侧不需要额外的消费者。

两条方向都与 USB CDC 并存，同一份串口数据可以同时送 USB 和 BLE，互不替代。

## 11. 速度预算

以下按 MTU 247、每帧有效数据 241 字节、空口每包 244 字节估算。

| 连接间隔 | 每个连接事件 1 包 | 每个连接事件 2 包 |
|---|---|---|
| 15 ms | 约 16.2 kB/s | 约 32.5 kB/s |
| 11.25 ms | 约 21.7 kB/s | 约 43.4 kB/s |
| 7.5 ms | 约 32.5 kB/s | 约 65 kB/s |

对照需求：

- 115200 8N1 对应约 11.5 kB/s，15 ms 间隔下单包模式够用，余量不大。
- 921600 对应约 92 kB/s，BLE 达不到。
- 开启 Wi-Fi 共存后有效吞吐大致减半，这时要把间隔压到 7.5 到 11.25 ms 并启用 2M PHY。
- 仅看控制与低速日志场景，实际占用通常不到 1 kB/s，速度不是瓶颈。

## 12. 设备侧实现位置

| 功能 | 落点 |
|---|---|
| SET_RGB 调用 | `ws2812_send()`，见 [../components/ws2812/src/ws2812.c](../components/ws2812/src/ws2812.c) |
| SERIAL_RX 取数 | UART2 读取处 [../components/myusb/myusb.c](../components/myusb/myusb.c#L1067)，与 CDC IN、镜像日志并列分发 |
| SERIAL_TX 落点 | 同一个 UART 发送环形缓冲 [../components/myusb/myusb.c](../components/myusb/myusb.c#L731) |
| 缓冲容量 | `CONFIG_UARTTX_RINGBUF_SIZE`，定义在 [../components/myusb/myusb.c](../components/myusb/myusb.c#L74) |
| 串口默认波特率 | `ECHO_UART_BAUD_RATE`，定义在 [../components/myusb/include/myusb.h](../components/myusb/include/myusb.h#L24) |

实现注意：

- 不要复用 4096 字节的镜像日志做 BLE 上行，那是给 HTTP 快照用的，每次整窗重发。
- 传输模式（XMODEM）下 CDC 通道被独占，BLE 串口转发应遵循同样的互斥规则，避免两路数据混进同一次传输。
- BLE 任务优先级应低于 `usb_task`，避免挤占 USB 中断相关处理。

## 13. 上位机实现示例

### 13.1 Python（bleak）

```python
import asyncio

from bleak import BleakClient, BleakScanner

DEVICE_NAME = "key_wifi-ble"
RX_UUID = "6b65f001-7769-4f9a-8d21-7c3b5e9a1f04"
TX_UUID = "6b65f002-7769-4f9a-8d21-7c3b5e9a1f04"

TYPE_SET_RGB = 0x01
TYPE_SERIAL_TX = 0x02
TYPE_SERIAL_RX = 0x81
TYPE_STATUS = 0x82


def build_frame(msg_type: int, payload: bytes = b"") -> bytes:
    return bytes([msg_type]) + len(payload).to_bytes(2, "little") + payload


def on_notify(_sender, data: bytearray) -> None:
    if len(data) < 3:
        return
    msg_type = data[0]
    length = int.from_bytes(data[1:3], "little")
    payload = bytes(data[3:3 + length])
    if msg_type == TYPE_SERIAL_RX:
        print(payload.decode("utf-8", "replace"), end="", flush=True)
    elif msg_type == TYPE_STATUS:
        dropped = int.from_bytes(payload[1:5], "little")
        print(f"\n[status] code={payload[0]} dropped={dropped}")


async def main() -> None:
    device = await BleakScanner.find_device_by_filter(
        lambda d, ad: ad.local_name == DEVICE_NAME,
        timeout=10,
    )
    async with BleakClient(device) as client:
        print("MTU:", client.mtu_size)
        await client.start_notify(TX_UUID, on_notify)
        await client.write_gatt_char(
            RX_UUID,
            build_frame(TYPE_SET_RGB, bytes([255, 128, 32])),
            response=True,
        )
        await asyncio.sleep(30)


asyncio.run(main())
```

### 13.2 C#（WinRT）

```csharp
using System;
using System.Threading.Tasks;
using Windows.Devices.Bluetooth;
using Windows.Devices.Bluetooth.Advertisement;
using Windows.Devices.Bluetooth.GenericAttributeProfile;
using Windows.Storage.Streams;

static class BleCdc
{
    static readonly Guid ServiceUuid = Guid.Parse("6b65f000-7769-4f9a-8d21-7c3b5e9a1f04");
    static readonly Guid RxUuid = Guid.Parse("6b65f001-7769-4f9a-8d21-7c3b5e9a1f04");
    static readonly Guid TxUuid = Guid.Parse("6b65f002-7769-4f9a-8d21-7c3b5e9a1f04");

    public static async Task<(GattCharacteristic Rx, GattCharacteristic Tx)> ConnectAsync()
    {
        var found = new TaskCompletionSource<BluetoothLEDevice>();
        var watcher = new BluetoothLEAdvertisementWatcher
        {
            ScanningMode = BluetoothLEScanningMode.Active
        };
        watcher.Received += async (_, e) =>
        {
            if (e.Advertisement.LocalName != "key_wifi-ble") return;
            var dev = await BluetoothLEDevice.FromBluetoothAddressAsync(e.BluetoothAddress);
            if (dev != null) found.TrySetResult(dev);
        };

        watcher.Start();
        var device = await found.Task;
        watcher.Stop();

        var services = await device.GetGattServicesForUuidAsync(ServiceUuid);
        if (services.Status != GattCommunicationStatus.Success || services.Services.Count == 0)
            throw new InvalidOperationException("service not found");

        var service = services.Services[0];
        var tx = (await service.GetCharacteristicsForUuidAsync(TxUuid)).Characteristics[0];
        var rx = (await service.GetCharacteristicsForUuidAsync(RxUuid)).Characteristics[0];
        return (rx, tx);
    }

    public static async Task EnableNotifyAsync(GattCharacteristic tx)
    {
        var status = await tx.WriteClientCharacteristicConfigurationDescriptorAsync(
            GattClientCharacteristicConfigurationDescriptorValue.Notify);
        if (status != GattCommunicationStatus.Success)
            throw new InvalidOperationException("cccd write failed");
    }

    public static byte[] BuildFrame(byte type, byte[] payload = null)
    {
        payload ??= Array.Empty<byte>();
        var frame = new byte[3 + payload.Length];
        frame[0] = type;
        frame[1] = (byte)(payload.Length & 0xFF);
        frame[2] = (byte)(payload.Length >> 8);
        payload.CopyTo(frame, 3);
        return frame;
    }

    public static async Task SendAsync(GattCharacteristic rx, byte type, byte[] payload = null)
    {
        var writer = new DataWriter();
        writer.WriteBytes(BuildFrame(type, payload));
        var status = await rx.WriteValueAsync(
            writer.DetachBuffer(),
            GattWriteOption.WriteWithResponse);
        if (status != GattCommunicationStatus.Success)
            throw new InvalidOperationException($"write failed: {status}");
    }
}
```

## 14. 调试与联调

- 用 nRF Connect 连上 `key_wifi-ble`，确认服务与两个特征值都在。
- 手动写 `01 03 00 FF 00 00`，灯应变红，用来验证下行链路。
- 开启通知后观察是否有 `81` 开头的帧，用来验证上行链路。
- 在设备日志里打印协商后的 MTU 和连接间隔，排查吞吐不达标的原因。
- 若出现短包粘连或解析错位，先检查上位机是否按 `LEN` 切包，而不是假设一包一帧以外的行为。

## 15. 错误处理规则

| 情况 | 设备行为 | 上位机应做 |
|---|---|---|
| 未知 `TYPE` | 丢帧，回 `STATUS` 码 `0x02` | 记录并忽略 |
| `LEN` 超过 `MTU - 6` | 丢帧 | 按实际 MTU 分片 |
| `LEN` 与实际载荷不符 | 丢帧 | 严格按格式打包 |
| 串口缓冲溢出 | 丢最旧数据，`STATUS` 计数增加 | 显示丢失量 |
| 连接断开 | 停止上行，缓冲继续积压到满 | 重连后重新开启通知 |

## 16. 版本规划

v1 已定，不做：

- Wi-Fi 开关与状态查询
- 配对、绑定、加密
- OTA
- HSV 控制与亮度参数
- L2CAP CoC 面向连接通道
- 超过 `MTU - 6` 的单帧分片重组

后续版本若加入 Wi-Fi 控制，按 `0x03` 及以上的 `TYPE` 追加，帧格式不变。

## 17. 修订记录

| 版本 | 日期 | 说明 |
|---|---|---|
| v1 | 2026-11-22 | 首版，定义 RGB 控制与串口双向转发 |
