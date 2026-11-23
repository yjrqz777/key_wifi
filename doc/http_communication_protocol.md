# ESP32 HTTP 上位机通信协议

本文档只说明 [components/webserver/src/webserver.c](/home/yjrqz/esp/esp_code/key_wifi/components/webserver/src/webserver.c) 中的 HTTP 数据传输格式，用于自定义上位机。

## 1. 传输方式

设备提供一个普通 HTTP/1.x 服务：

- 不使用二进制自定义协议
- 不使用 WebSocket
- 不使用分隔符帧或帧头
- 使用 HTTP 方法、URI 查询串和 JSON 正文表示数据
- 默认端口由 ESP HTTP 服务配置决定；通常为 80
- AP 热点模式下设备常用地址为根页面实际展示的 IP，通常是 `192.168.4.1`

上位机只需使用 TCP HTTP 客户端即可，不需要连接 USB CDC、串口或发送 AT 命令。

## 2. 接口总表

| 操作 | 方法 | URI | 请求数据 | 成功响应 |
|---|---|---|---|---|
| 查询 Wi-Fi 状态 | GET | `/wifi_status` | 无 | JSON |
| 切换 Wi-Fi 模式 | GET | `/wifi_control` | URL 查询参数 | JSON |
| 设置 RGB | POST | `/rgbcontrol` | JSON 正文 | 文本 `OK` |
| 查询串口日志快照 | GET | `/serial` | 无 | UTF-8 文本 |
| 打开/关闭 LED | GET | `/control` | URL 查询参数 | 空正文 |
| 获取根页面 | GET | `/` | 无 | HTML |
| 获取串口页面 | GET | `/serial_view` | 无 | HTML |

## 3. 查询 Wi-Fi 状态

### 请求

```http
GET /wifi_status HTTP/1.1
Host: 192.168.4.1
Connection: close
```

### 响应

响应头：

```http
Content-Type: application/json
```

响应正文是一个无空格的 JSON 对象：

```json
{"ap":true,"sta":false}
```

字段含义：

| 字段 | 类型 | 含义 |
|---|---|---|
| `ap` | boolean | AP 是否启用 |
| `sta` | boolean | STA 是否启用 |

上位机应按 JSON 解析，不应直接匹配空格或字段顺序。

读取失败时：

```http
HTTP/1.1 500 Internal Server Error

wifi status read failed
```

## 4. 切换 Wi-Fi 模式

### 请求封装

```text
/wifi_control?ap=<0或1>&sta=<0或1>
```

参数规则：

| 参数 | 值 | 含义 |
|---|---|---|
| `ap` | `1` | 启用 AP |
| `ap` | `0` | 禁用 AP |
| `sta` | `1` | 启用 STA |
| `sta` | `0` | 禁用 STA |

示例：

```http
GET /wifi_control?ap=1&sta=0 HTTP/1.1
Host: 192.168.4.1
Connection: close
```

### 成功响应

```http
HTTP/1.1 200 OK
Content-Type: application/json

{"ok":true}
```

上位机使用响应状态码判断成功；只可把 `{"ok":true}` 视为成功确认。

### 错误响应

缺少参数：

```http
HTTP/1.1 400 Bad Request

missing ap/sta
```

读取查询串失败：

```http
HTTP/1.1 400 Bad Request

missing query
```

切换失败：

```http
HTTP/1.1 500 Internal Server Error

wifi switch failed
```

实现要求至少保留一个模式。不要发送 `ap=0&sta=0`。

## 5. 设置 RGB

### 请求封装

```http
POST /rgbcontrol HTTP/1.1
Host: 192.168.4.1
Content-Type: application/json
Content-Length: 22
Connection: close

{"r":255,"g":128,"b":32}
```

`Content-Length` 为请求正文的字节数。字段类型必须是可由 `cJSON_Parse()` 识别的数字。

```json
{"r":255,"g":128,"b":32}
```

字段定义：

| 字段 | 类型 | 合法范围 | 越界处理 |
|---|---|---|---|
| `r` | JSON 数字 | 0 到 255 | 小于 0 变 0，大于 255 变 255 |
| `g` | JSON 数字 | 0 到 255 | 小于 0 变 0，大于 255 变 255 |
| `b` | JSON 数字 | 0 到 255 | 小于 0 变 0，大于 255 变 255 |

字段顺序可以任意；字段名区分大小写。推荐包含全部三个字段。

### 成功响应

```http
HTTP/1.1 200 OK

OK
```

响应没有 JSON 包装；将正文精确比较为 `OK` 即可。

### JSON 错误响应

```http
HTTP/1.1 400 Bad Request

Invalid JSON
```

### 上位机打包示例

Python：

```python
import requests

HOST = "http://192.168.4.1"

response = requests.post(
    HOST + "/rgbcontrol",
    json={"r": 255, "g": 128, "b": 32},
    timeout=2,
)

response.raise_for_status()
if response.text != "OK":
    raise RuntimeError(response.text)
```

C#：

```csharp
using System.Net.Http;
using System.Text;
using System.Text.Json;

var host = "http://192.168.4.1";
using var client = new HttpClient();

var data = JsonSerializer.Serialize(new {
    r = 255,
    g = 128,
    b = 32
});

using var content = new StringContent(data, Encoding.UTF8, "application/json");
using var response = await client.PostAsync(host + "/rgbcontrol", content);
var text = await response.Content.ReadAsStringAsync();

response.EnsureSuccessStatusCode();
if (text != "OK")
    throw new InvalidOperationException(text);
```

## 6. 查询串口日志快照

### 请求

```http
GET /serial HTTP/1.1
Host: 192.168.4.1
Connection: close
```

### 响应

响应头：

```http
HTTP/1.1 200 OK
Content-Type: text/plain; charset=utf-8
Cache-Control: no-store
```

响应正文是设备内最近的串口日志快照，最大有效长度为 4095 字符，不保证包含 NUL 字符。

例如：

```text
[PC->DEV] hello
[DEV->PC] world
```

不可打印字节在设备端会转换为 `.`。日志满时丢弃最旧数据。

内存不足时：

```http
HTTP/1.1 500 Internal Server Error

No memory
```

## 7. `/control` 打包格式

当前实现不实际驱动 LED，只根据参数打印日志：

```text
GET /control?state=on
GET /control?state=off
```

响应为空正文：

```http
HTTP/1.1 200 OK
Content-Length: 0
```

参数缺失或不是 `on`/`off` 时，仍正常返回空响应。

## 8. 自定义上位机建议

### 8.1 基础请求封装

所有使用查询参数的接口都应遵循 URL 编码规则：

```text
键值以 = 分隔
多个参数以 & 分隔
参数名和参数值使用百分号编码
```

此固件使用的固定参数不含空格或特殊字符，因此上位机可直接拼接：

```text
/wifi_control?ap=1&sta=0
/control?state=on
```

### 8.2 Wi-Fi 状态解析

推荐流程：

1. GET `/wifi_status`
2. 检查 HTTP 状态码为 200
3. 按 JSON 解析
4. 从 `ap` 和 `sta` 布尔字段更新界面

### 8.3 RGB 设置解析

推荐流程：

1. 将每个通道限制到 0 到 255
2. 打包为 compact JSON：

   ```json
   {"r":255,"g":128,"b":32}
   ```

3. 设置 `Content-Type: application/json`
4. 检查 200 状态码和 `OK` 正文

### 8.4 串口日志轮询

由于该接口是快照而不是实时流：

1. 周期性 GET `/serial`
2. 不要假设新日志增量
3. 每次得到的是完整快照
4. 上位机自行去重或显示最新快照

## 9. 错误处理规则

| 状态码 | 含义 |
|---|---|
| `200` | 请求被正常处理 |
| `400` | 参数缺失或 JSON 非法 |
| `500` | 内存不足或 Wi-Fi 状态/切换失败 |
| `302` | 未注册 URI，被重定向到 `/` |

未注册 URI 会返回：

```http
HTTP/1.1 302 Temporary Redirect
Location: /

Redirect to the captive portal
```

上位机应检查最终状态码，不要把 302 重定向后的设备 HTML 当作接口响应。

## 10. 当前实现限制

- `/rgbcontrol` 最多读取 127 字节请求正文。
- `/rgbcontrol` 不校验缺失字段；缺少 `r`、`g` 或 `b`可能导致空指针问题。
- `/rgbcontrol` 不校验 JSON 值类型；建议上位机只发送整数。
- `/rgbcontrol` 队列满时可能丢弃更新，但仍可能返回 `OK`。
- `/wifi_control` 两个参数都必须存在；任何非 `1` 的值都会当作 `0`。
- `/control` 当前只是桩接口，不能用于实际 LED 控制。
- `/serial` 每次返回的最多是 4096 字节缓冲区快照，不是持续数据流。
