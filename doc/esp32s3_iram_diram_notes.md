# ESP32-S3 IRAM 与 DIRAM

## 内存概况

ESP32-S3 包含：

- 512 KB 内部 SRAM；
- 8 KB RTC FAST SRAM；
- 8 KB RTC SLOW SRAM；
- 可选外部 PSRAM，容量取决于具体模组。

内部 SRAM 不会全部提供给应用。Flash/PSRAM cache、系统保留区域和链接布局会占用一部分空间，实际可用容量应以链接报告为准。

## IRAM 和 DRAM 的用途

IRAM（Instruction RAM）主要存放：

- 中断向量；
- `IRAM_ATTR` 函数；
- Flash cache 关闭期间仍需执行的代码；
- 对延迟敏感的系统和驱动代码。

DRAM（Data RAM）主要存放：

- `.data`：已初始化的全局变量和静态变量；
- `.bss`：未初始化或初始化为零的全局变量和静态变量；
- 任务栈；
- 内部堆；
- DMA 和驱动缓冲区。

普通函数和只读常量通常分别位于 Flash 的 `.text` 和 `.rodata` 中。

## IRAM 与 DIRAM 共享

ESP32-S3 有专用 IRAM，也有可以用于指令或数据的共享内部 SRAM。可以简化理解为：

```text
内部 SRAM
┌──────────────────────────────┐
│ 专用 IRAM                   │
├──────────────────────────────┤
│ IRAM/DIRAM 共享区域          │
│                              │
│ IRAM 代码  →        ← DRAM 数据│
└──────────────────────────────┘
```

共享区域是同一块物理 SRAM 的不同地址映射，不是两块独立内存。因此：

- IRAM 代码增加会减少 DRAM 和内部堆的可用空间；
- IRAM 代码减少通常会释放空间给 DRAM 和内部堆；
- 不能把 IRAM 和 DIRAM 的容量简单相加；
- 应结合链接报告中的剩余空间判断整体内部 SRAM 压力。

## IRAM 100% 的含义

链接器会优先填充专用 IRAM，再把后续 IRAM 代码放入共享区域。该过程发生在链接阶段，不是运行时动态搬运。

因此，专用 IRAM 显示 `100%` 不一定代表内部 SRAM 已耗尽。只要共享区域仍有空间，链接器仍可继续放置 IRAM 代码。

真正的区域溢出通常会产生链接错误：

```text
IRAM0 segment data does not fit
DRAM segment data does not fit
```

## 默认内存放置

通常不需要手工管理代码和变量的位置。编译器、链接器及 ESP-IDF 内存分配器会完成默认放置：

- 普通函数放入 Flash；
- 需要驻留内存的代码放入 IRAM；
- 普通全局变量和静态变量放入 DRAM；
- 局部变量放入任务栈；
- 只读常量通常放入 Flash；
- 动态内存根据配置及内存能力从相应堆中分配。

不应仅因为专用 IRAM 显示 `100%`，就给普通函数添加 `IRAM_ATTR`。

## 显式放置代码

需要在 Flash cache 关闭期间运行，或确实对中断延迟敏感的函数，可以使用 `IRAM_ATTR`：

```c
#include "esp_attr.h"

IRAM_ATTR void gpio_isr_handler(void *arg)
{
    // ISR code
}
```

不存在单独的 `DIRAM_IRAM_ATTR`。`IRAM_ATTR` 只声明函数需要放入 IRAM，具体使用专用 IRAM 还是共享区域由链接器安排。

## 显式放置数据

需要确保位于内部 DRAM 的静态数据可以使用 `DRAM_ATTR`：

```c
#include "esp_attr.h"

DRAM_ATTR static volatile uint32_t interrupt_count;
```

大对象可以在配置允许时放入外部 PSRAM：

```c
#include "esp_attr.h"

EXT_RAM_BSS_ATTR static uint8_t large_buffer[64 * 1024];
```

## 按内存能力动态分配

ESP-IDF 的 capability heap 可以明确指定所需内存类型：

```c
#include "esp_heap_caps.h"

void *internal_buffer = heap_caps_malloc(
    1024,
    MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);

void *psram_buffer = heap_caps_malloc(
    64 * 1024,
    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

void *dma_buffer = heap_caps_malloc(
    4096,
    MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
```

常用能力包括：

- `MALLOC_CAP_INTERNAL`：内部 RAM；
- `MALLOC_CAP_SPIRAM`：外部 PSRAM；
- `MALLOC_CAP_DMA`：DMA 可访问内存；
- `MALLOC_CAP_8BIT`：支持普通字节访问；
- `MALLOC_CAP_32BIT`：只保证对齐的 32 位访问。

USB、SPI 等 DMA 缓冲区必须满足具体驱动对内存能力、地址范围和对齐方式的要求。

`MALLOC_CAP_32BIT` 获得的内存不能默认当作普通字节缓冲区使用，也不适合字符串或任意类型数据。

## IRAM-safe ISR

给 ISR 入口添加 `IRAM_ATTR` 并不等于整个调用链已经 IRAM-safe。还需要保证：

- ISR 调用到的函数在 cache 关闭时可执行；
- ISR 访问的数据位于内部 RAM；
- ISR 不读取 Flash 中的常量或字符串；
- 不调用普通 `malloc()`、`printf()` 或不确定是否 IRAM-safe 的日志路径；
- 使用明确支持 ISR 的 FreeRTOS `...FromISR()` API。

示例：

```c
DRAM_ATTR static volatile uint32_t interrupt_count;

IRAM_ATTR static void gpio_isr(void *arg)
{
    interrupt_count++;
}
```

## 内存检查

构建阶段可以查看静态内存分布：

```bash
idf.py size
idf.py size-components
idf.py size-files
```

运行阶段可以检查内部堆：

```c
size_t internal_free =
    heap_caps_get_free_size(MALLOC_CAP_INTERNAL);

size_t internal_largest =
    heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
```

开发过程中应关注：

- DIRAM 剩余空间；
- 内部堆剩余量；
- 最大连续空闲块；
- DMA 等特定 capability 的剩余内存；
- 是否发生内存分配失败；
- 是否出现 IRAM 或 DRAM 链接区域溢出。
