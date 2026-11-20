# ESP32-S3 OTA 底层函数说明

## 文档范围

本文只介绍 ESP-IDF 提供的 OTA 底层接口，重点是 `esp_ota_ops` 组件中的函数及其使用方式。HTTP 上传、URL 下载等上层传输方式不在本文范围内。

## 环境

- 芯片：ESP32-S3
- ESP-IDF：v5.5.6
- OTA 组件：`app_update`
- 分区组件：`esp_partition`
- 分区表要求：包含 `otadata` 数据分区，以及至少两个 app 分区（`factory` + `ota_0` + `ota_1`，或 `ota_0` + `ota_1`）

当前工程的 `partitions.csv` 已包含 `otadata`、`factory`、`ota_0`、`ota_1`，每个 app 分区 2 MB，可以直接使用本文接口。

## 一、函数总览

### 1.1 分区定位

| 函数 | 所在文件 | 用途 |
| --- | --- | --- |
| `esp_ota_get_running_partition` | `esp_ota_ops.c` | 获取当前正在运行的分区 |
| `esp_ota_get_boot_partition` | `esp_ota_ops.c` | 获取下次启动分区 |
| `esp_ota_get_next_update_partition` | `esp_ota_ops.c` | 获取本次应写入的 OTA 分区 |
| `esp_partition_find_first` | `partition.c` | 按类型、子类型或名字查找分区 |

### 1.2 写入

| 函数 | 所在文件 | 用途 |
| --- | --- | --- |
| `esp_ota_begin` | `esp_ota_ops.c` | 开始 OTA，擦除目标分区并获取句柄 |
| `esp_ota_write` | `esp_ota_ops.c` | 顺序写入一块固件数据 |
| `esp_ota_write_with_offset` | `esp_ota_ops.c` | 按指定偏移写入，用于非连续数据 |
| `esp_ota_resume` | `esp_ota_ops.c` | 从中断位置继续 OTA |
| `esp_ota_set_final_partition` | `esp_ota_ops.c` | 指定最终目标分区（staging/final 分离时使用） |

### 1.3 结束与切换

| 函数 | 所在文件 | 用途 |
| --- | --- | --- |
| `esp_ota_end` | `esp_ota_ops.c` | 结束写入并校验镜像 |
| `esp_ota_abort` | `esp_ota_ops.c` | 放弃本次 OTA，释放句柄 |
| `esp_ota_set_boot_partition` | `esp_ota_ops.c` | 设置下次启动分区 |

### 1.4 状态与回滚

| 函数 | 所在文件 | 用途 |
| --- | --- | --- |
| `esp_ota_get_state_partition` | `esp_ota_ops.c` | 查询分区的 OTA 状态 |
| `esp_ota_get_last_invalid_partition` | `esp_ota_ops.c` | 获取最近一次无效的分区 |
| `esp_ota_check_rollback_is_possible` | `esp_ota_ops.c` | 判断是否可以回滚 |
| `esp_ota_mark_app_valid_cancel_rollback` | `esp_ota_ops.c` | 标记当前固件有效，取消回滚 |
| `esp_ota_mark_app_invalid_rollback` | `esp_ota_ops.c` | 标记当前固件无效并准备回滚 |
| `esp_ota_mark_app_invalid_rollback_and_reboot` | `esp_ota_ops.c` | 标记无效并重启到旧固件 |
| `esp_ota_erase_last_boot_app_partition` | `esp_ota_ops.c` | 擦除上一次启动的 app 分区 |
| `esp_ota_invalidate_inactive_ota_data_slot` | `esp_ota_ops.c` | 使上一次启动分区对应的 otadata 槽失效 |

### 1.5 信息查询与辅助

| 函数 | 所在文件 | 用途 |
| --- | --- | --- |
| `esp_ota_get_partition_description` | `esp_ota_ops.c` | 读取指定分区中的固件描述 |
| `esp_app_get_description` | `esp_app_desc.c` | 读取当前运行固件的描述 |
| `esp_ota_get_app_partition_count` | `esp_ota_ops.c` | 获取分区表中的 OTA app 分区数量 |
| `esp_restart` | `esp_system.c` | 软件重启 |

---

## 二、典型调用流程

一次完整的 OTA 分为五步：

1. 调用 `esp_ota_get_next_update_partition` 找到目标分区。
2. 调用 `esp_ota_begin` 开始 OTA。
3. 循环调用 `esp_ota_write` 写入固件数据。
4. 调用 `esp_ota_end` 校验镜像。
5. 调用 `esp_ota_set_boot_partition` 设置启动分区，再调用 `esp_restart` 重启。

```c
#include "esp_ota_ops.h"
#include "esp_system.h"

esp_err_t ota_write_image(const uint8_t *data, size_t len)
{
    const esp_partition_t *part = esp_ota_get_next_update_partition(NULL);
    if (part == NULL) {
        return ESP_ERR_NOT_FOUND;
    }
    if (len > part->size) {
        return ESP_ERR_INVALID_SIZE;
    }

    esp_ota_handle_t handle = 0;
    esp_err_t err = esp_ota_begin(part, len, &handle);
    if (err != ESP_OK) {
        return err;
    }

    err = esp_ota_write(handle, data, len);
    if (err != ESP_OK) {
        esp_ota_abort(handle);
        return err;
    }

    err = esp_ota_end(handle);
    if (err != ESP_OK) {
        /* esp_ota_end 无论成功失败都会释放句柄，这里不能再调用 esp_ota_abort。 */
        return err;
    }

    err = esp_ota_set_boot_partition(part);
    if (err != ESP_OK) {
        return err;
    }

    esp_restart();
    return ESP_OK;
}
```

如果数据是分块到达的，把一次性写入改成循环：

```c
esp_ota_handle_t handle = 0;

ESP_ERROR_CHECK(esp_ota_begin(part, OTA_WITH_SEQUENTIAL_WRITES, &handle));

while (remaining > 0) {
    size_t chunk = remaining > sizeof(buf) ? sizeof(buf) : remaining;
    size_t received = read_chunk(buf, chunk);
    if (received == 0) {
        esp_ota_abort(handle);
        return ESP_FAIL;
    }

    esp_err_t err = esp_ota_write(handle, buf, received);
    if (err != ESP_OK) {
        esp_ota_abort(handle);
        return err;
    }
    remaining -= received;
}

ESP_ERROR_CHECK(esp_ota_end(handle));
```

---

## 三、函数使用方法

### 3.1 查找目标分区

```c
const esp_partition_t *esp_ota_get_next_update_partition(const esp_partition_t *start_from);
```

- `start_from` 传 `NULL` 时，从当前运行分区开始找下一个 OTA 分区。
- 当前运行在 `factory` 时返回 `ota_0`；运行在 `ota_0` 时返回 `ota_1`，两个 OTA 槽交替使用。
- 返回 `NULL` 表示分区表中没有可用的 OTA 分区。

```c
const esp_partition_t *part = esp_ota_get_next_update_partition(NULL);
```

也可以用 `esp_partition_find_first` 按名字查找：

```c
const esp_partition_t *part = esp_partition_find_first(
    ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, NULL);
```

`esp_ota_get_running_partition` 和 `esp_ota_get_boot_partition` 只用于查询：

```c
const esp_partition_t *running = esp_ota_get_running_partition();
const esp_partition_t *boot = esp_ota_get_boot_partition();
```

- `running`：当前代码实际运行的分区。
- `boot`：otadata 中记录的下次启动分区；调用过 `esp_ota_set_boot_partition` 后，它会与 `running` 不同。

### 3.2 开始 OTA

```c
esp_err_t esp_ota_begin(const esp_partition_t *partition,
                        size_t image_size,
                        esp_ota_handle_t *out_handle);
```

- `partition`：目标 OTA 分区，必须是 app 类型分区。
- `image_size`：
  - 传入实际固件大小时，`esp_ota_begin` 会按该大小擦除分区；
  - 传 `OTA_SIZE_UNKNOWN`（或 0）时，擦除整个分区；
  - 传 `OTA_WITH_SEQUENTIAL_WRITES` 时，按写入进度增量擦除，速度更快，但后续必须顺序写。
- `out_handle`：成功时返回句柄，供 `esp_ota_write` 和 `esp_ota_end` 使用。
- 成功返回 `ESP_OK`；失败常见原因包括目标分区是当前运行分区、otadata 损坏、参数为空、内存不足。

```c
esp_ota_handle_t handle = 0;
esp_err_t err = esp_ota_begin(part, OTA_WITH_SEQUENTIAL_WRITES, &handle);
```

启用回滚（`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE`）后，如果当前固件还处于 `ESP_OTA_IMG_PENDING_VERIFY` 状态，`esp_ota_begin` 会返回 `ESP_ERR_OTA_ROLLBACK_INVALID_STATE`。此时应先调用 `esp_ota_mark_app_valid_cancel_rollback` 确认当前固件。

### 3.3 写入固件

```c
esp_err_t esp_ota_write(esp_ota_handle_t handle, const void *data, size_t size);
```

- 可以在 OTA 过程中多次调用。
- 数据必须按顺序写入，中间不能有空洞。
- 每次写入前检查返回值，失败立即停止。
- 首字节会校验 app 镜像魔数，不是合法 app 镜像时返回 `ESP_ERR_OTA_VALIDATE_FAILED`。

```c
esp_err_t err = esp_ota_write(handle, buf, received);
if (err != ESP_OK) {
    esp_ota_abort(handle);
    return err;
}
```

如果数据包乱序到达，可以使用按偏移写入：

```c
esp_err_t esp_ota_write_with_offset(esp_ota_handle_t handle,
                                    const void *data,
                                    size_t size,
                                    uint32_t offset);
```

- 支持非连续写入，目标区域必须已经擦除。
- 不建议和 `esp_ota_write` 混用。
- 启用 Flash 加密时，数据需要 16 字节对齐。

### 3.4 中断后续传

```c
esp_err_t esp_ota_resume(const esp_partition_t *partition,
                         size_t erase_size,
                         size_t image_offset,
                         esp_ota_handle_t *out_handle);
```

- 用于 OTA 被中断后从已有数据继续写入。
- `image_offset` 设置为已经成功写入的字节数。
- `erase_size` 指定续写前需要擦除的字节数。
- 与 `esp_ota_begin` 不同，它不会重新擦除整个分区。

### 3.5 结束并校验

```c
esp_err_t esp_ota_end(esp_ota_handle_t handle);
```

- 结束本次 OTA，并校验已写入的 app 镜像。
- 校验内容包括镜像格式、校验和；启用 Secure Boot 时还会校验签名。
- 无论成功还是失败，调用后句柄都会释放，不能再传给其他 OTA 函数。
- 返回 `ESP_OK` 表示镜像合法，可以继续设置启动分区。
- 返回 `ESP_ERR_OTA_VALIDATE_FAILED` 表示镜像无效，此时不要调用 `esp_ota_set_boot_partition`。

如果在 `esp_ota_end` 之前决定放弃本次 OTA，调用：

```c
esp_err_t esp_ota_abort(esp_ota_handle_t handle);
```

`esp_ota_abort` 只释放句柄和内存，不会修改 otadata，设备仍从旧固件启动。

### 3.6 设置启动分区并重启

```c
esp_err_t esp_ota_set_boot_partition(const esp_partition_t *partition);
```

- 把目标分区写入 otadata，作为下次启动分区。
- 函数内部会校验目标分区中的 app 镜像；镜像无效时返回 `ESP_ERR_OTA_VALIDATE_FAILED`。
- 返回 `ESP_OK` 后，调用 `esp_restart()` 才会进入新固件。
- 只设置启动分区、不重启时，下一次正常复位也会进入新固件。

```c
ESP_ERROR_CHECK(esp_ota_set_boot_partition(part));
esp_restart();
```

### 3.7 查询固件信息

读取指定分区中的固件描述，可用于升级前比较版本：

```c
esp_app_desc_t desc;
esp_err_t err = esp_ota_get_partition_description(part, &desc);
if (err == ESP_OK) {
    ESP_LOGI(TAG, "version: %s", desc.version);
}
```

读取当前运行固件的描述：

```c
const esp_app_desc_t *desc = esp_app_get_description();
ESP_LOGI(TAG, "running version: %s", desc->version);
```

### 3.8 回滚

以下函数只在启用 `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE` 时产生实际回滚效果。

查询分区状态：

```c
esp_ota_img_states_t state;
esp_err_t err = esp_ota_get_state_partition(part, &state);
```

常见状态：

- `ESP_OTA_IMG_NEW`：新写入，尚未启动验证。
- `ESP_OTA_IMG_PENDING_VERIFY`：新固件已启动，等待确认。
- `ESP_OTA_IMG_VALID`：固件已确认有效。
- `ESP_OTA_IMG_INVALID`：固件被标记无效。
- `ESP_OTA_IMG_ABORTED`：启动或验证过程被中止。
- `ESP_OTA_IMG_UNDEFINED`：没有 OTA 状态记录。

新固件自检通过后，必须尽早确认：

```c
esp_ota_mark_app_valid_cancel_rollback();
```

如果自检失败，主动回滚：

```c
if (esp_ota_check_rollback_is_possible()) {
    esp_ota_mark_app_invalid_rollback_and_reboot();
}
```

- `esp_ota_mark_app_invalid_rollback`：标记当前固件无效，但不重启。
- `esp_ota_mark_app_invalid_rollback_and_reboot`：标记无效并立即重启到旧固件。
- `esp_ota_check_rollback_is_possible`：判断是否存在可启动的旧固件。

确认当前固件有效后，可以擦除上一个启动分区：

```c
esp_ota_erase_last_boot_app_partition();
```

### 3.9 staging 与 final 分区

默认情况下，`esp_ota_begin` 传入的分区既是下载暂存分区（staging），也是最终启动分区（final）。如果两者需要分离，可以在 `esp_ota_begin` 之后、第一次 `esp_ota_write` 之前调用：

```c
esp_err_t esp_ota_set_final_partition(esp_ota_handle_t handle,
                                      const esp_partition_t *final,
                                      bool finalize_with_copy);
```

- `final`：最终要启动的分区。
- `finalize_with_copy` 为 `true` 时，`esp_ota_end` 会把 staging 分区内容复制到 final 分区。
- 为 `false` 时，需要自己调用 `esp_partition_copy` 完成复制。
- 一般应用不需要使用这个接口。

---

## 四、常见错误码

| 错误码 | 含义 |
| --- | --- |
| `ESP_ERR_OTA_VALIDATE_FAILED` | 镜像无效，或分区中的 app 不可启动 |
| `ESP_ERR_OTA_PARTITION_CONFLICT` | 试图写入当前正在运行的分区 |
| `ESP_ERR_OTA_SELECT_INFO_INVALID` | otadata 内容损坏 |
| `ESP_ERR_OTA_ROLLBACK_INVALID_STATE` | 当前固件仍处于待验证状态，不能开始新的 OTA |
| `ESP_ERR_OTA_ROLLBACK_FAILED` | 没有可回滚的合法分区 |
| `ESP_ERR_INVALID_SIZE` | 写入超出分区范围，或分区大小与 Flash 配置不匹配 |
| `ESP_ERR_NOT_FOUND` | 找不到分区、句柄或固件描述 |

---

## 五、注意事项

1. OTA 只能更新 app 分区。bootloader、分区表、NVS、FAT 数据分区不会被 OTA 修改；修改分区表或 bootloader 必须通过串口重新烧录。
2. 写入的镜像必须是 app 的 `.bin` 文件，不能是完整 Flash 合并镜像。
3. `esp_ota_write` 必须顺序写入，不能跳块；乱序数据使用 `esp_ota_write_with_offset`。
4. `esp_ota_end` 调用后句柄即释放，失败时不要再调用 `esp_ota_abort`。
5. 写入过程中掉电或写入失败时，不要调用 `esp_ota_set_boot_partition`，otadata 保持不变，设备仍会从旧固件启动。
6. `esp_ota_set_boot_partition` 成功即代表启动分区已切换，之后再掉电也会进入新固件。
7. 启用回滚后，新固件必须在自检通过后调用 `esp_ota_mark_app_valid_cancel_rollback`，否则复位时会回滚。
8. 每次 OTA 前确认固件大小不超过目标分区容量；当前工程单个 OTA 分区为 2 MB。
9. 生产环境应配合 Secure Boot 和签名镜像，避免未签名固件被写入。
