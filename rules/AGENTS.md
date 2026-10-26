# C 语言编码规则

## 适用范围

- 本规则适用于本仓库中新建或修改的 C 源文件、头文件及项目目录。
- 整个项目的命名和格式必须保持一致。名称必须清楚表达其用途及所属模块。
- 除非任务明确要求进行清理，否则不要仅为满足本规则而重命名无关的现有代码。
- 第三方库、标准协议栈、生成文件以及客户提供的文件不受本规则约束，除非任务明确要求修改这些文件。
- 避免局部变量与文件作用域变量使用相同的名称。

## 项目目录布局

- 用户自有程序目录使用既定命名形式：`UserApp`、`UserBsp` 和 `UserDrv`。
- 每个项目都必须提供并使用 `User_global.h` 和 `user_config.h`。这两个必需文件名是通用小写文件命名规则的明确例外。

## 文件命名

- C 源文件和头文件使用小写单词命名，单词之间以下划线分隔。
- 示例：`user_app.c`、`user_app.h`、`bsp_motor.c`、`bsp_motor.h`、`drv_pwm.c` 和 `drv_pwm.h`。

## 源文件与头文件组织

- 将所有 `#include` 指令集中放在每个 C 文件的开头。不要将包含指令放在变量定义、函数定义或实现代码之间。
- 避免在 C 文件中定义宏。模块使用或复用的宏应放在对应的头文件中。
- 整数、十六进制数和浮点数字面量形式的宏值必须使用括号包围。
- 宏需要注释时，在同一行末尾使用 `/* ... */` 块注释。
- 头文件保护宏应由文件名转换而来：将文件名转换为大写下划线形式，并在前后各添加两个下划线。例如，`user_time.h` 使用 `__USER_TIME_H__`。

```c
/* user_app.c */
#include "user_app.h"
#include "User_global.h"
```

```c
/* user_app.h */
#ifndef __USER_APP_H__
#define __USER_APP_H__

#define FRAME_SOF       (0xAAu)    /* 帧起始 */
#define RETRY_COUNT     (3u)       /* 重试次数 */
#define SPEED_LIMIT     (1000.0f)  /* 速度上限 */

#endif /* __USER_APP_H__ */
```

## 函数命名

- 在 `user_xxx.c` 或 `user_xxx.h` 中声明或定义的函数，使用 `Usr` + 模块名 + 函数名的 PascalCase 形式，例如 `UsrSensorRead()`。
- 在 `bsp_xxx.c` 或 `bsp_xxx.h` 中声明或定义的函数，使用 `Bsp` + 模块名 + 函数名的 PascalCase 形式，例如 `BspMotorSetSpeed()`。
- 在 `drv_xxx.c` 或 `drv_xxx.h` 中声明或定义的函数，使用 `Drv` + 模块名 + 函数名的 PascalCase 形式，例如 `DrvPwmSetDutyCycle()`。

## 数据类型

- 使用 `User_global.h` 提供的标准类型。
- 基本数值类型使用 `uint8_t`、`int8_t`、`uint16_t`、`int16_t`、`uint32_t`、`int32_t`、`uint64_t`、`int64_t`、`float` 和 `double`。
- 标识符使用以下类型前缀：
  - `uint8_t`：`u8`
  - `uint16_t`：`u16`
  - `uint32_t`：`u32`
  - `uint64_t`：`u64`
  - `int8_t`：`s8`
  - `int16_t`：`s16`
  - `int32_t`：`s32`
  - `int64_t`：`s64`
  - `float`：`f32`
  - `double`：`f64`

## 结构体

- 结构体类型定义使用 `t` + PascalCase 名称 + `Def` 命名，例如 `tUserFilterDef`。
- 结构体变量使用 `t` + PascalCase 名称命名，例如 `tUserFilter`。
- 结构体指针变量使用 `pt` + PascalCase 名称命名，例如 `ptUserFilter`。

```c
typedef struct tUserFilterDef
{
    uint16_t u16BufferSize;
    uint32_t u32SumValue;
} tUserFilterDef;

tUserFilterDef * ptUserFilter;
tUserFilterDef tUserFilter;
```

## 联合体

- 联合体类型定义使用 `u` + PascalCase 名称 + `Def` 命名，例如 `uDeviceDataDef`。
- 联合体变量使用 `u` + PascalCase 名称命名，例如 `uDeviceData`。
- 联合体指针变量使用 `pu` + PascalCase 名称命名，例如 `puDeviceData`。

```c
typedef union uDeviceDataDef
{
    uint32_t u32Value;
    uint8_t u8Bytes[4];
} uDeviceDataDef;

uDeviceDataDef * puDeviceData;
uDeviceDataDef uDeviceData;
```

## 枚举

- 枚举类型定义使用 `e` + PascalCase 名称 + `Def` 命名，例如 `eFilterModeDef`。
- 枚举成员使用 `E_` + 大写下划线分隔单词命名。
- 枚举变量使用 `e` + PascalCase 名称命名，例如 `eCurrentMode`。

```c
typedef enum
{
    E_FILTER_MODE_AVERAGE = 0,
    E_FILTER_MODE_MEDIAN,
    E_FILTER_MODE_SLIDING
} eFilterModeDef;

eFilterModeDef eCurrentMode;
```

## 宏

- 宏使用大写单词命名，单词之间以下划线分隔。
- 字面量宏值必须使用括号包围。
- 宏注释使用 `/* ... */` 形式并放在同一行末尾。

```c
#define FRAME_SOF (0xAAu) /* 帧起始 */
#define FRAME_EOF (0x55u) /* 帧结束 */
```

## 变量与参数

- 全局标量变量和文件作用域静态标量变量使用类型前缀 + PascalCase 描述命名。
- 示例：`uint32_t u32TotalCount`、`uint16_t u16TotalCount` 和 `static uint8_t u8TotalCount`。
- 有符号标量变量使用 `s8`、`s16`、`s32` 或 `s64` + PascalCase 描述命名。
- 浮点标量变量使用 `f32` 或 `f64` + PascalCase 描述命名。
- 非指针标量函数参数使用类型前缀 + PascalCase 描述命名，例如 `uint16_t u16Speed` 和 `uint8_t u8Duty`。
- 非结构体指针变量使用 `p` + 类型前缀 + PascalCase 描述命名。
- 指针参数同样遵循该指针命名规则。例如：`uint8_t * pu8DataBuf` 和 `uint32_t * pu32DataBuf`。
- 普通函数局部标量变量使用不带类型前缀的 PascalCase 名称，例如 `TotalAmount` 和 `Index`。
- 允许使用简短的循环索引 `i`、`j` 和 `k`。
- 结构体、联合体和枚举变量遵循各自的专用规则，不使用标量类型前缀规则。

## 函数声明参考

```c
/* user_app.c */
void UsrFilterProcessData(void);

/* bsp_motor.c */
void BspMotorSetSpeed(uint16_t u16Speed);

/* drv_pwm.c */
void DrvPwmSetDutyCycle(uint8_t u8Duty);
```

## 审查清单

创建或审查 C 代码时，请确认：

- 项目自有目录和文件名符合规定的命名方式。
- 所有包含指令集中放在每个 C 文件的开头。
- 可复用宏位于头文件中，字面量宏值使用括号包围，宏注释采用规定格式。
- 头文件保护宏与头文件名匹配。
- 函数使用与其模块层级对应的前缀。
- 结构体、联合体、枚举、变量、指针和参数使用正确的前缀及大小写形式。
- 名称具有描述性、保持一致、体现模块归属，并且不会造成可避免的作用域冲突。

## Doxygen 注释

- 使用中文注释
- 使用 `/** ... */` 形式的 Doxygen 块注释。不要使用 `///`。
- 文档文本使用简洁的英语，完整句子以句号结尾。
- 修改相关代码时，必须同步更新注释，使其与实际行为保持一致。
- 注释应描述意图、约束、归属、副作用和失败条件，不要简单复述代码。
- 不要添加 `@author`、`@date`、版本历史或变更日志字段。

### 文件文档

- 每个项目自有的 `.c` 和 `.h` 文件都必须添加 Doxygen 文件注释。
- 文件注释放在所有 `#include` 指令和头文件保护宏之前。
- 使用 `@file` 和 `@brief`。仅当模块需要额外背景说明时才添加 `@details`。
- 生成文件、第三方库和客户提供的文件仍然不受此规则约束。

```c
/**
 * @file app_camera.h
 * @brief 提供摄像头采集和 LVGL 预览接口。
 */
```

### 函数文档

- 每个公共函数都必须在其 `.c` 文件中的定义上方添加文档。
- 不要在头文件的函数声明处添加函数文档。
- 仅当文件局部 `static` 函数的行为、约束、副作用或算法不直观时，才为其添加文档。
- 除非存在重要约束，否则不要为简单的 getter、setter、包装函数或含义明确的辅助函数添加文档。
- 以简洁的 `@brief` 句子开头，不要重复函数名。
- 参数说明必须按照声明顺序排列。
- 每个参数都必须标记为 `@param[in]`、`@param[out]` 或 `@param[in,out]`。
- 使用 `@return` 描述返回值的一般含义。
- 当不同返回值具有不同含义时，使用 `@retval` 分别说明。
- 返回类型为 `void` 的函数不要添加 `@return` 或 `@retval`。
- 使用 `@note` 说明重要的使用约束；使用 `@warning` 说明可能导致数据丢失、数据损坏、死锁或硬件损坏的情况。

```c
/* user_camera.c */
/**
 * @brief 启动一帧摄像头图像的采集。
 * @param[in] ptDcmi 指向已初始化 DCMI 句柄的指针。
 * @param[out] pu32ElapsedMs 接收采集耗时的指针。
 * @retval 0 图像帧采集成功。
 * @retval 1 图像帧采集失败或超时。
 * @note 目标图像帧缓冲区必须按缓存行对齐。
 */
uint8_t UsrCameraCaptureFrame(DCMI_HandleTypeDef * ptDcmi,
                              uint32_t * pu32ElapsedMs)
{
    /* 实现。 */
}
```

### 类型与成员

- 在公共结构体、联合体、枚举和类型定义上方添加 Doxygen 注释。
- 使用行尾 `/**< ... */` 注释记录成员和枚举值。
- 对于名称已经清楚表达含义的私有成员，不要额外添加文档。

```c
/**
 * @brief 描述当前摄像头采集状态。
 */
typedef enum
{
    E_CAMERA_CAPTURE_IDLE = 0, /**< 当前没有正在进行的采集。 */
    E_CAMERA_CAPTURE_BUSY,     /**< 正在采集图像帧。 */
    E_CAMERA_CAPTURE_DONE,     /**< 图像帧已准备好进行处理。 */
    E_CAMERA_CAPTURE_ERROR     /**< 最近一次采集失败。 */
} eCameraCaptureStateDef;
```

### 宏与局部注释

- 普通宏注释继续使用现有的行尾 `/* ... */` 格式。
- 仅当宏属于需要生成文档的公共 API 时，才使用 Doxygen 注释。
- 在函数内部，使用普通 `/* ... */` 注释说明不直观的算法、硬件约束、同步、缓存处理或寄存器操作顺序。
- 不要对局部变量或单独的实现步骤使用 Doxygen 注释。
