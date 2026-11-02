/***************************************************************************************************
 * Author: yjrqz777 3210551161@qq.com
 * Date: 2025-03-19 19:37:18
 * LastEditTime: 2026-06-06 10:24:08
 * LastEditors: yjrqz777 3210551161@qq.com
 * Description: 
 * FilePath: /key_wifi/components/myusb/myusb.c
 * @YJRQZ777
***************************************************************************************************/
/*
 * SPDX-FileCopyrightText: 2022-2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

/* DESCRIPTION:
 * This example contains code to make ESP32-S3 based device recognizable by USB-hosts as a USB Mass Storage Device.
 * It either allows the embedded application i.e. example to access the partition or Host PC accesses the partition over USB MSC.
 * They can't be allowed to access the partition at the same time.
 * For different scenarios and behaviour, Refer to README of this example.
 */

/**
 * @file myusb.c
 * @brief 实现 USB 复合设备相关的描述符注册、端点回调与任务调度。
 * @details usb_task 依次完成 DAP 状态、FAT 文件系统、MSC 虚拟 FAT 内容与 USB 初始化，
 *          并循环转发 USB CDC 与 UART 之间的数据。
 */

#include <errno.h>
#include <dirent.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "freertos/semphr.h"
// #include "esp_console.h"
#include "esp_check.h"
#include "driver/gpio.h"
#include "sdkconfig.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "cJSON.h"

#include "esp_task_wdt.h"
#include "driver/uart.h"



#include "myusb.h"
#include "myfile.h"
/*
 * Copyright (c) 2024, sakumisu
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "usbd_core.h"
#include "usbd_cdc_acm.h"
#include "usbd_msc.h"
#include "usbd_hid.h"

#include "chry_ringbuffer.h"

#include "DAP_config.h"
#include "DAP.h"
#include "swd_host.h"


static const char *TAG = "my USB";



#define CONFIG_UARTTX_RINGBUF_SIZE (4096) /* UART 发送方向环形缓冲区字节数，必须为 2 的幂 */
// #define CONFIG_USBRX_RINGBUF_SIZE  (8 * 1024)



/* DAP 命令请求缓冲区，由 DAP 组件提供，需 4 字节对齐并放在非缓存 USB 内存 */
extern __attribute__ ((aligned (4))) USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX uint8_t USB_Request[DAP_PACKET_COUNT][DAP_PACKET_SIZE];
/* DAP 命令响应缓冲区，由 DAP 组件提供，需 4 字节对齐并放在非缓存 USB 内存 */
extern __attribute__ ((aligned (4))) USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX uint8_t USB_Response[DAP_PACKET_COUNT][DAP_PACKET_SIZE];
/* 各 DAP 响应包的实际长度，由 DAP 组件维护 */
extern __attribute__ ((aligned (4))) uint16_t USB_RespSize[DAP_PACKET_COUNT];       
/***************************************************************************************************/
/* global */
char current_dap_mode = 0; /* DAP 调试口模式：1 表示 SWD 引脚已使能，0 表示引脚已关闭 */
/***************************************************************************************************/


/***************************************************************************************************/
/* part */
/* UART 发送方向环形缓冲区的存储区，位于非缓存 RAM 且按 USB 对齐要求放置 */
static USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX uint8_t uarttx_ringbuffer[CONFIG_UARTTX_RINGBUF_SIZE];
// static USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX uint8_t usbrx_ringbuffer[CONFIG_USBRX_RINGBUF_SIZE];
// static USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX uint8_t usb_tmpbuffer[DAP_PACKET_SIZE];

// static volatile uint8_t usbrx_idle_flag = 0;
// static volatile uint8_t usbtx_idle_flag = 0;
static volatile uint8_t uarttx_buff_full = 0; /* 缓冲区满标志：为 1 时暂停启动下一次 CDC OUT 接收 */
chry_ringbuffer_t g_uarttx;                   /* UART 发送方向环形缓冲区对象，连接 CDC OUT 与 UART 发送 */
// chry_ringbuffer_t g_usbrx;


// USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX uint8_t usb_read_buffer[2048];
USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX uint8_t cdc_read_buffer[WINUSB_EP_MPS]; /* CDC OUT 单次接收缓冲区，长度为一个端点最大包 */
// USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX uint8_t write_buffer[2048];

volatile bool ep_tx_busy_flag = false; /* CDC IN 端点发送忙标志，避免发送完成前重入写端点 */

#define SERIAL_LOG_BUF_SIZE 4096 /* CDC 串口镜像日志容量，供网页 /serial 读取 */
static char s_serial_log[SERIAL_LOG_BUF_SIZE];                       /* 串口镜像日志正文，始终以 '\0' 结尾 */
static size_t s_serial_log_len = 0;                                  /* 日志当前有效字节数 */
static portMUX_TYPE s_serial_log_mux = portMUX_INITIALIZER_UNLOCKED; /* 日志读写的临界区锁 */

/**
 * @brief 把一段数据按可打印 ASCII 形式打印到调试输出，不可打印字符替换为点号。
 * @param[in] data 待打印的数据。
 * @param[in] len 数据字节数，最多打印 64 字节，超出部分以省略号表示。
 */
static void usb_log_ascii(const uint8_t *data, uint32_t len)
{
    uint32_t show = (len > 64U) ? 64U : len;
    char out[65];
    for (uint32_t i = 0; i < show; i++) {
        uint8_t ch = data[i];
        if ((ch >= 32U && ch <= 126U) || ch == '\r' || ch == '\n' || ch == '\t') {
            out[i] = (char)ch;
        } else {
            out[i] = '.';
        }
    }
    out[show] = '\0';
    USB_LOG_RAW("%s", out);
    if (len > show) {
        USB_LOG_RAW("...");
    }
    USB_LOG_RAW("\r\n");
}

/**
 * @brief 把 CDC 串口数据追加到内存镜像日志，供网页 /serial 读取。
 * @param[in] data 待追加的原始数据。
 * @param[in] len 数据字节数，超出 BUF_SIZE 时只取前部。
 * @note 不可打印字符会被替换为点号；日志满时丢弃最旧的数据，保证保留最新内容。
 */
void usb_serial_log_append(const uint8_t *data, size_t len)
{
    if ((data == NULL) || (len == 0)) {
        return;
    }

    char local_buf[BUF_SIZE];
    size_t local_len = (len > sizeof(local_buf)) ? sizeof(local_buf) : len;
    for (size_t i = 0; i < local_len; i++) {
        uint8_t ch = data[i];
        if ((ch == '\r') || (ch == '\n') || (ch == '\t') || (ch >= 32 && ch <= 126)) {
            local_buf[i] = (char)ch;
        } else {
            local_buf[i] = '.';
        }
    }

    taskENTER_CRITICAL(&s_serial_log_mux);
    if (local_len >= (SERIAL_LOG_BUF_SIZE - 1)) {
        memcpy(s_serial_log, &local_buf[local_len - (SERIAL_LOG_BUF_SIZE - 1)], SERIAL_LOG_BUF_SIZE - 1);
        s_serial_log_len = SERIAL_LOG_BUF_SIZE - 1;
    } else {
        size_t free_space = (SERIAL_LOG_BUF_SIZE - 1) - s_serial_log_len;
        if (local_len > free_space) {
            size_t drop_len = local_len - free_space;
            memmove(s_serial_log, s_serial_log + drop_len, s_serial_log_len - drop_len);
            s_serial_log_len -= drop_len;
        }
        memcpy(s_serial_log + s_serial_log_len, local_buf, local_len);
        s_serial_log_len += local_len;
    }
    s_serial_log[s_serial_log_len] = '\0';
    taskEXIT_CRITICAL(&s_serial_log_mux);
}

/**
 * @brief 拷贝一份串口镜像日志快照。
 * @param[out] out 接收日志文本的缓冲区，写入后以 '\0' 结尾。
 * @param[in] out_size 缓冲区字节数，可容纳的最大有效字符数为 out_size - 1。
 * @return 实际拷贝的字节数，不包含结尾 '\0'。
 */
size_t usb_serial_log_snapshot(char *out, size_t out_size)
{
    if ((out == NULL) || (out_size == 0)) {
        return 0;
    }

    taskENTER_CRITICAL(&s_serial_log_mux);
    size_t copy_len = s_serial_log_len;
    if (copy_len >= out_size) {
        copy_len = out_size - 1;
    }
    memcpy(out, s_serial_log, copy_len);
    out[copy_len] = '\0';
    taskEXIT_CRITICAL(&s_serial_log_mux);
    return copy_len;
}

/**
 * @brief 在日志中按方向追加一段带 [PC->DEV] 或 [DEV->PC] 前缀的数据。
 * @param[in] dir 方向标记文本。
 * @param[in] data 待追加的原始数据。
 * @param[in] len 数据字节数。
 */
static void usb_serial_log_append_direction(const char *dir, const uint8_t *data, size_t len)
{
    char prefix[24];
    int n = snprintf(prefix, sizeof(prefix), "\r\n[%s] ", dir);
    if (n > 0) {
        usb_serial_log_append((const uint8_t *)prefix, (size_t)n);
    }
    usb_serial_log_append(data, len);
}

/**
 * @brief 把 UART 发送环形缓冲区中的数据搬运到 UART 外设。
 * @param[in] busid USB 总线编号，用于在缓冲区腾空后重启 CDC OUT 接收。
 * @param[in,out] txdata 搬运用的中转缓冲区。
 * @param[in] txbuf_size 中转缓冲区字节数，单次搬运不超过该长度。
 * @return true 本次搬出了数据；false 环形缓冲区为空。
 * @note 缓冲区此前因满而暂停接收时，只要剩余空间达到一个端点最大包就重新启动 CDC OUT 接收。
 */
static bool drain_usb_to_uart(uint8_t busid, uint8_t *txdata, uint32_t txbuf_size)
{
    uint32_t used = chry_ringbuffer_get_used(&g_uarttx);
    if (used == 0) {
        if (uarttx_buff_full && chry_ringbuffer_get_free(&g_uarttx) >= WINUSB_EP_MPS) {
            usbd_ep_start_read(busid, CDC_OUT_EP, cdc_read_buffer, WINUSB_EP_MPS);
            uarttx_buff_full = 0;
        }
        return false;
    }

    uint32_t chunk = (used > txbuf_size) ? txbuf_size : used;
    int len = (int)chry_ringbuffer_read(&g_uarttx, txdata, chunk);
    if (len > 0) {
        uart_write_bytes(ECHO_UART_PORT_NUM, (const char *)txdata, len);
    }

    if (uarttx_buff_full && chry_ringbuffer_get_free(&g_uarttx) >= WINUSB_EP_MPS) {
        usbd_ep_start_read(busid, CDC_OUT_EP, cdc_read_buffer, WINUSB_EP_MPS);
        uarttx_buff_full = 0;
    }
    return (len > 0);
}

/***************************************************************************************************/




/* DAP 组件导出的接口，供端点回调与调试口状态复位使用 */
void SetUSB_RequestIdle(uint8_t idle);
void dap_in_callback(uint8_t busid, uint8_t ep, uint32_t nbytes);
void dap_out_callback(uint8_t busid, uint8_t ep, uint32_t nbytes);


/* DAP 调试口状态初始化接口 */
void chry_dap_state_init(void);
#if 0 /* 虚拟 FAT 已停用，MSC 改为直接使用 storage 分区，下面三个接口不再调用 */
void create_readme_file_entry(void);
void init_fat_table(void);
void prepare_file_content(void);
#endif






/* WinUSB 的 MS OS 2.0 描述符集合：声明 DAP 功能接口的兼容 ID 与设备接口 GUID */
__ALIGN_BEGIN const uint8_t USBD_WinUSBDescriptorSetDescriptor[] = {
    WBVAL(WINUSB_DESCRIPTOR_SET_HEADER_SIZE), /* wLength */
    WBVAL(WINUSB_SET_HEADER_DESCRIPTOR_TYPE), /* wDescriptorType */
    0x00, 0x00, 0x03, 0x06, /* >= Win 8.1 */  /* dwWindowsVersion*/
    WBVAL(USBD_WINUSB_DESC_SET_LEN),          /* wDescriptorSetTotalLength */
#if (USBD_WEBUSB_ENABLE)
    WBVAL(WINUSB_FUNCTION_SUBSET_HEADER_SIZE), // wLength
    WBVAL(WINUSB_SUBSET_HEADER_FUNCTION_TYPE), // wDescriptorType
    0,                                         // bFirstInterface USBD_WINUSB_IF_NUM
    0,                                         // bReserved
    WBVAL(FUNCTION_SUBSET_LEN),                // wSubsetLength
    WBVAL(WINUSB_FEATURE_COMPATIBLE_ID_SIZE),  // wLength
    WBVAL(WINUSB_FEATURE_COMPATIBLE_ID_TYPE),  // wDescriptorType
    'W', 'I', 'N', 'U', 'S', 'B', 0, 0,        // CompatibleId
    0, 0, 0, 0, 0, 0, 0, 0,                    // SubCompatibleId
    WBVAL(DEVICE_INTERFACE_GUIDS_FEATURE_LEN), // wLength
    WBVAL(WINUSB_FEATURE_REG_PROPERTY_TYPE),   // wDescriptorType
    WBVAL(WINUSB_PROP_DATA_TYPE_REG_MULTI_SZ), // wPropertyDataType
    WBVAL(42),                                 // wPropertyNameLength
    'D', 0, 'e', 0, 'v', 0, 'i', 0, 'c', 0, 'e', 0,
    'I', 0, 'n', 0, 't', 0, 'e', 0, 'r', 0, 'f', 0, 'a', 0, 'c', 0, 'e', 0,
    'G', 0, 'U', 0, 'I', 0, 'D', 0, 's', 0, 0, 0,
    WBVAL(80), // wPropertyDataLength
    '{', 0,
    '9', 0, '2', 0, 'C', 0, 'E', 0, '6', 0, '4', 0, '6', 0, '2', 0, '-', 0,
    '9', 0, 'C', 0, '7', 0, '7', 0, '-', 0,
    '4', 0, '6', 0, 'F', 0, 'E', 0, '-', 0,
    '9', 0, '3', 0, '3', 0, 'B', 0, '-',
    0, '3', 0, '1', 0, 'C', 0, 'B', 0, '9', 0, 'C', 0, '5', 0, 'A', 0, 'A', 0, '3', 0, 'B', 0, '9', 0,
    '}', 0, 0, 0, 0, 0
#endif
#if USBD_BULK_ENABLE
    WBVAL(WINUSB_FUNCTION_SUBSET_HEADER_SIZE), /* wLength */
    WBVAL(WINUSB_SUBSET_HEADER_FUNCTION_TYPE), /* wDescriptorType */
    0,                                         /* bFirstInterface USBD_BULK_IF_NUM*/
    0,                                         /* bReserved */
    WBVAL(FUNCTION_SUBSET_LEN),                /* wSubsetLength */
    WBVAL(WINUSB_FEATURE_COMPATIBLE_ID_SIZE),  /* wLength */
    WBVAL(WINUSB_FEATURE_COMPATIBLE_ID_TYPE),  /* wDescriptorType */
    'W', 'I', 'N', 'U', 'S', 'B', 0, 0,        /* CompatibleId*/
    0, 0, 0, 0, 0, 0, 0, 0,                    /* SubCompatibleId*/
    WBVAL(DEVICE_INTERFACE_GUIDS_FEATURE_LEN), /* wLength */
    WBVAL(WINUSB_FEATURE_REG_PROPERTY_TYPE),   /* wDescriptorType */
    WBVAL(WINUSB_PROP_DATA_TYPE_REG_MULTI_SZ), /* wPropertyDataType */
    WBVAL(42),                                 /* wPropertyNameLength */
    'D', 0, 'e', 0, 'v', 0, 'i', 0, 'c', 0, 'e', 0,
    'I', 0, 'n', 0, 't', 0, 'e', 0, 'r', 0, 'f', 0, 'a', 0, 'c', 0, 'e', 0,
    'G', 0, 'U', 0, 'I', 0, 'D', 0, 's', 0, 0, 0,
    WBVAL(80), /* wPropertyDataLength */
    '{', 0,
    'C', 0, 'D', 0, 'B', 0, '3', 0, 'B', 0, '5', 0, 'A', 0, 'D', 0, '-', 0,
    '2', 0, '9', 0, '3', 0, 'B', 0, '-', 0,
    '4', 0, '6', 0, '6', 0, '3', 0, '-', 0,
    'A', 0, 'A', 0, '3', 0, '6', 0, '-',
    0, '1', 0, 'A', 0, 'A', 0, 'E', 0, '4', 0, '6', 0, '4', 0, '6', 0, '3', 0, '7', 0, '7', 0, '6', 0,
    '}', 0, 0, 0, 0, 0
#endif
};

#define USBD_NUM_DEV_CAPABILITIES (USBD_WEBUSB_ENABLE + USBD_WINUSB_ENABLE) /* 设备能力描述符个数 */

#define USBD_WEBUSB_DESC_LEN 24 /* WebUSB 设备能力描述符长度 */
#define USBD_WINUSB_DESC_LEN 28 /* WinUSB 设备能力描述符长度 */

/* BOS 描述符总长度：头部 5 字节加上各设备能力描述符长度 */
#define USBD_BOS_WTOTALLENGTH (0x05 +                                      \
                               USBD_WEBUSB_DESC_LEN * USBD_WEBUSB_ENABLE + \
                               USBD_WINUSB_DESC_LEN * USBD_WINUSB_ENABLE)





/* BOS 描述符：登记 WebUSB 与 WinUSB 两项平台能力，供主机枚举时识别 */
__ALIGN_BEGIN const uint8_t USBD_BinaryObjectStoreDescriptor[] = {
    0x05,                         /* bLength */
    0x0f,                         /* bDescriptorType */
    WBVAL(USBD_BOS_WTOTALLENGTH), /* wTotalLength */
    USBD_NUM_DEV_CAPABILITIES,    /* bNumDeviceCaps */
#if (USBD_WEBUSB_ENABLE)
    USBD_WEBUSB_DESC_LEN,           /* bLength */
    0x10,                           /* bDescriptorType */
    USB_DEVICE_CAPABILITY_PLATFORM, /* bDevCapabilityType */
    0x00,                           /* bReserved */
    0x38, 0xB6, 0x08, 0x34,         /* PlatformCapabilityUUID */
    0xA9, 0x09, 0xA0, 0x47,
    0x8B, 0xFD, 0xA0, 0x76,
    0x88, 0x15, 0xB6, 0x65,
    WBVAL(0x0100), /* 1.00 */ /* bcdVersion */
    USBD_WINUSB_VENDOR_CODE,  /* bVendorCode */
    0,                        /* iLandingPage */
#endif
#if (USBD_WINUSB_ENABLE)
    USBD_WINUSB_DESC_LEN,           /* bLength */
    0x10,                           /* bDescriptorType */
    USB_DEVICE_CAPABILITY_PLATFORM, /* bDevCapabilityType */
    0x00,                           /* bReserved */
    0xDF, 0x60, 0xDD, 0xD8,         /* PlatformCapabilityUUID */
    0x89, 0x45, 0xC7, 0x4C,
    0x9C, 0xD2, 0x65, 0x9D,
    0x9E, 0x64, 0x8A, 0x9F,
    0x00, 0x00, 0x03, 0x06, /* >= Win 8.1 */ /* dwWindowsVersion*/
    WBVAL(USBD_WINUSB_DESC_SET_LEN),         /* wDescriptorSetTotalLength */
    USBD_WINUSB_VENDOR_CODE,                 /* bVendorCode */
    0,                                       /* bAltEnumCode */
#endif
};

/* MS OS 2.0 描述符集合，挂到设备枚举时返回给 Windows 主机 */
struct usb_msosv2_descriptor msosv2_desc = {
    .vendor_code = USBD_WINUSB_VENDOR_CODE,
    .compat_id = USBD_WinUSBDescriptorSetDescriptor,
    .compat_id_len = USBD_WINUSB_DESC_SET_LEN,
};

/* BOS 描述符对象，挂到设备枚举时返回给支持 BOS 的主机 */
struct usb_bos_descriptor bos_desc = {
    .string = USBD_BinaryObjectStoreDescriptor,
    .string_len = USBD_BOS_WTOTALLENGTH};

/* 高级描述符模式：设备描述符、配置描述符、限定符描述符与字符串描述符分开返回 */
#ifdef CONFIG_USBDEV_ADVANCE_DESC
static const uint8_t device_descriptor[] = {
    USB_DEVICE_DESCRIPTOR_INIT(USB_2_1, 0xEF, 0x02, 0x01, USBD_VID, USBD_PID, 0x0100, 0x01)};

static const uint8_t config_descriptor[] = {
    /* Configuration 0 */
    USB_CONFIG_DESCRIPTOR_INIT(USB_CONFIG_SIZE, INTF_NUM, 0x01, USB_CONFIG_BUS_POWERED, USBD_MAX_POWER),
    /* Interface 0 */
    USB_INTERFACE_DESCRIPTOR_INIT(0x00, 0x00, 0x02, 0xFF, 0x00, 0x00, 0x04),
    /* Endpoint OUT 2 */
    USB_ENDPOINT_DESCRIPTOR_INIT(WINUSB_OUT_EP, USB_ENDPOINT_TYPE_BULK, WINUSB_EP_MPS, 0x00),
    /* Endpoint IN 1 */
    USB_ENDPOINT_DESCRIPTOR_INIT(WINUSB_IN_EP, USB_ENDPOINT_TYPE_BULK, WINUSB_EP_MPS, 0x00),
    CDC_ACM_DESCRIPTOR_INIT(0x01, CDC_INT_EP, CDC_OUT_EP, CDC_IN_EP, WINUSB_EP_MPS, 0x05)};

static const uint8_t device_quality_descriptor[] = {
    ///////////////////////////////////////
    /// device qualifier descriptor
    ///////////////////////////////////////
    0x0a,
    USB_DESCRIPTOR_TYPE_DEVICE_QUALIFIER,
    0x10,
    0x02,
    0x00,
    0x00,
    0x00,
    0x40,
    0x00,
    0x00,
};

/* 字符串描述符表：0 语言 ID，1 厂商，2 产品，3 序列号，4 WinUSB 接口名，5 CDC 接口名 */
static const char *string_descriptors[] = {
    (const char[]){0x09, 0x04}, /* Langid */
    "CherryUSB",                /* Manufacturer */
    "CherryUSB WINUSB DEMO",    /* Product */
    "2022123456",               /* Serial Number */
    "YJRQZ777 DAP",              /* WinUSB接口名称 */
    "MyCDC Port",               /* CDC接口名称 */
};

static const uint8_t *device_descriptor_callback(uint8_t speed)
{
    return device_descriptor;
}

static const uint8_t *config_descriptor_callback(uint8_t speed)
{
    return config_descriptor;
}

static const uint8_t *device_quality_descriptor_callback(uint8_t speed)
{
    return device_quality_descriptor;
}

static const char *string_descriptor_callback(uint8_t speed, uint8_t index)
{
    if (index > 5)
    {
        return NULL;
    }
    return string_descriptors[index];
}

/* 描述符回调集合，供 CherryUSB 在枚举过程中按类型取用 */
const struct usb_descriptor winusbv2_descriptor = {
    .device_descriptor_callback = device_descriptor_callback,
    .config_descriptor_callback = config_descriptor_callback,
    .device_quality_descriptor_callback = device_quality_descriptor_callback,
    .string_descriptor_callback = string_descriptor_callback,
    .msosv2_descriptor = &msosv2_desc,
    .bos_descriptor = &bos_desc};
#else
/* 非高级描述符模式：设备、配置、接口、端点与字符串描述符按枚举顺序拼成一个连续数组 */
const uint8_t winusbv2_descriptor[] = {
    USB_DEVICE_DESCRIPTOR_INIT(USB_2_1, 0xEF, 0x02, 0x01, USBD_VID, USBD_PID, 0x0100, 0x01),
    /* Configuration 0 */
    USB_CONFIG_DESCRIPTOR_INIT(USB_CONFIG_SIZE, INTF_NUM, 0x01, USB_CONFIG_BUS_POWERED, USBD_MAX_POWER),
    /* Interface 0 */
    USB_INTERFACE_DESCRIPTOR_INIT(0x00, 0x00, 0x02, 0xFF, 0x00, 0x00, 0x04),
    /* Endpoint OUT 2 */
    USB_ENDPOINT_DESCRIPTOR_INIT(WINUSB_OUT_EP, USB_ENDPOINT_TYPE_BULK, WINUSB_EP_MPS, 0x00),
    /* Endpoint IN 1 */
    USB_ENDPOINT_DESCRIPTOR_INIT(WINUSB_IN_EP, USB_ENDPOINT_TYPE_BULK, WINUSB_EP_MPS, 0x00),
    CDC_ACM_DESCRIPTOR_INIT(0x01, CDC_INT_EP, CDC_OUT_EP, CDC_IN_EP, WINUSB_EP_MPS, 0x05),
#ifdef CONFIG_CHERRYDAP_USE_MSC
    MSC_DESCRIPTOR_INIT(0x03, MSC_OUT_EP, MSC_IN_EP, DAP_PACKET_SIZE, 0x00),
#endif

    /* String 0 (LANGID) */
    USB_LANGID_INIT(USBD_LANGID_STRING),
    /* String 1 (Manufacturer) */
    0x14,                       /* bLength */
    USB_DESCRIPTOR_TYPE_STRING, /* bDescriptorType */
    'C', 0x00, 
    'h', 0x00, 
    'e', 0x00, 
    'r', 0x00, 
    'r', 0x00, 
    'y', 0x00, 
    'U', 0x00, 
    'S', 0x00, 
    'B', 0x00,

    /* String 2 (Product) */
    0x1A, /* bLength: 13 characters + 2 = 0x1A bytes */
    USB_DESCRIPTOR_TYPE_STRING,
    'Y', 0x00,
    'J', 0x00,
    'R', 0x00,
    'Q', 0x00,
    'Z', 0x00,
    '7', 0x00,
    '7', 0x00, 
    '7', 0x00, 
    ' ', 0x00, 
    'U', 0x00, 
    'S', 0x00, 
    'B', 0x00,

    /* String 3 (Serial) */
    0x2A, /* bLength */
    USB_DESCRIPTOR_TYPE_STRING,
    'Y', 0x00, /* wcChar0 */
    'J', 0x00, /* wcChar1 */
    'R', 0x00, /* wcChar2 */
    'Q', 0x00, /* wcChar3 */
    'Z', 0x00, /* wcChar4 */
    ' ', 0x00, /* wcChar5 */
    '7', 0x00, /* wcChar6 */
    '7', 0x00, /* wcChar7 */
    '7', 0x00, /* wcChar8 */
    ' ', 0x00, /* wcChar9 */
    'U', 0x00, /* wcChar10 */
    'S', 0x00, /* wcChar11 */
    'B', 0x00, /* wcChar12 */
    ' ', 0x00, /* wcChar13 */
    'D', 0x00, /* wcChar14 */
    'A', 0x00, /* wcChar15 */
    'P', 0x00, /* wcChar16 */
    ' ', 0x00, /* wcChar17 */
    ' ', 0x00, /* wcChar18 */
    ' ', 0x00, /* wcChar19 */

    /* String 4 (WinUSB接口名称) */
    0x1A, /* bLength: 11字符 * 2 + 2 = 0x1A */
    USB_DESCRIPTOR_TYPE_STRING,
    'Y', 0x00,  /* wcChar0 */
    'J', 0x00,  /* wcChar1 */
    'R', 0x00,  /* wcChar2 */
    'Q', 0x00,  /* wcChar3 */     
    'Z', 0x00,  /* wcChar4 */
    '7', 0x00,  /* wcChar5 */
    '7', 0x00,  /* wcChar6 */
    '7', 0x00,  /* wcChar7 */
    '-', 0x00,  /* wcChar8 */
    'D', 0x00,  /* wcChar9 */
    'A', 0x00,  /* wcChar10 */
    'P', 0x00,  /* wcChar11 */

    /* String 5 (CDC接口名称) */
    0x16, /* bLength: 10字符 * 2 + 2 = 0x18 */
    USB_DESCRIPTOR_TYPE_STRING,
    'M', 0x00, /* wcChar0 */
    'y', 0x00, /* wcChar1 */
    'C', 0x00, /* wcChar2 */
    'D', 0x00, /* wcChar3 */
    'C', 0x00, /* wcChar4 */
    ' ', 0x00, /* wcChar5 */
    'U', 0x00, /* wcChar6 */
    'A', 0x00, /* wcChar7 */
    'R', 0x00, /* wcChar8 */
    'T', 0x00, /* wcChar9 */

#ifdef CONFIG_USB_HS
    /* Device Qualifier */
    0x0a,
    USB_DESCRIPTOR_TYPE_DEVICE_QUALIFIER,
    0x10,
    0x02,
    0x00,
    0x00,
    0x00,
    0x40,
    0x00,
    0x00,
#endif
    /* End */
    0x00};
#endif




/**
 * @brief 处理 CherryUSB 设备事件。
 * @param[in] busid USB 总线编号。
 * @param[in] event 事件类型，仅配置完成事件需要启动首批端点接收。
 * @note 配置完成后清除发送忙标志，并分别启动 WinUSB OUT 与 CDC OUT 的首次接收。
 */
static void usbd_event_handler(uint8_t busid, uint8_t event)
{
    switch (event)
    {
    case USBD_EVENT_RESET:
        break;
    case USBD_EVENT_CONNECTED:
        break;
    case USBD_EVENT_DISCONNECTED:
        break;
    case USBD_EVENT_RESUME:
        break;
    case USBD_EVENT_SUSPEND:
        break;
    case USBD_EVENT_CONFIGURED:
        ep_tx_busy_flag = false;
        /* 复位 DAP 请求空闲状态，允许接收主机的新命令 */
        SetUSB_RequestIdle(0);

        /* 启动 WinUSB OUT 与 CDC OUT 的首次接收 */
        usbd_ep_start_read(busid, WINUSB_OUT_EP, USB_Request[0], DAP_PACKET_SIZE);
        usbd_ep_start_read(busid, CDC_OUT_EP, cdc_read_buffer, WINUSB_EP_MPS);
        break;
    case USBD_EVENT_SET_REMOTE_WAKEUP:
        break;
    case USBD_EVENT_CLR_REMOTE_WAKEUP:
        break;

    default:
        break;
    }
}


/* AT 命令处理任务入口，实现在 usbAT.c 中 */
extern void usb_AT(void *Pr);

/**
 * @brief CDC OUT 端点接收完成回调，把主机数据转发到 UART 并写入串口镜像日志。
 * @param[in] busid USB 总线编号。
 * @param[in] ep 触发回调的端点地址，本实现不使用。
 * @param[in] nbytes 本次接收到的字节数，为 0 时只重启接收。
 * @note 环形缓冲区剩余空间不足一个最大包时置满标志并停止启动接收，等待发送端消费后由转发路径重新启动。
 * @warning 缓冲区空间不足的数据会被丢弃，丢弃字节数通过调试输出打印。
 */
void usbd_cdc_acm_out(uint8_t busid, uint8_t ep, uint32_t nbytes)
{
    (void)ep;
    if (nbytes == 0) {
        usbd_ep_start_read(busid, CDC_OUT_EP, cdc_read_buffer, WINUSB_EP_MPS);
        return;
    }

    /* 主机到设备方向的数据监视：调试打印二进制收发内容 */
    USB_LOG_RAW("CDC OUT len:%lu data(ascii): ", (unsigned long)nbytes);
    usb_log_ascii(cdc_read_buffer, nbytes);
    usb_serial_log_append_direction("PC->DEV", cdc_read_buffer, (size_t)nbytes);

    uint32_t write_len = chry_ringbuffer_write(&g_uarttx, cdc_read_buffer, nbytes);
    if (write_len < nbytes) {
        USB_LOG_RAW("CDC OUT drop:%lu\r\n", (unsigned long)(nbytes - write_len));
        uarttx_buff_full = 1;
    }

    /* 保持 AT 命令解析路径不变，数据同时送给 usbAT.c 处理 */
    usb_CDC_ACM_Data_Dispose(nbytes, cdc_read_buffer);

    if (chry_ringbuffer_get_free(&g_uarttx) >= WINUSB_EP_MPS)
    {
        /* 缓冲区空间足够，启动下一次 CDC OUT 接收 */
        usbd_ep_start_read(busid, CDC_OUT_EP, cdc_read_buffer, WINUSB_EP_MPS);
    }
    else
    {
        uarttx_buff_full = 1;
    }
}

/**
 * @brief CDC IN 端点发送完成回调。
 * @param[in] busid USB 总线编号。
 * @param[in] ep CDC IN 端点地址。
 * @param[in] nbytes 本次发送的字节数。
 * @note 发送长度为端点最大包整数倍时补发零长度包，否则清除发送忙标志，允许任务继续写端点。
 */
void usbd_cdc_acm_in(uint8_t busid, uint8_t ep, uint32_t nbytes)
{
    // USB_LOG_RAW("actual in len:%d\r\n", nbytes);

    if ((nbytes % usbd_get_ep_mps(busid, ep)) == 0 && nbytes)
    {
        /* 发送长度为整数个最大包，补发零长度包结束本次传输 */
        usbd_ep_start_write(busid, CDC_IN_EP, NULL, 0);
    }
    else
    {
        ep_tx_busy_flag = false;
    }
}

/* WinUSB 批量端点：OUT 端点的回调由 DAP 组件实现，接收调试命令 */
struct usbd_endpoint winusb_out_ep1 = {
    .ep_addr = WINUSB_OUT_EP,
    .ep_cb = dap_out_callback};

/* WinUSB 批量端点：IN 端点的回调由 DAP 组件实现，回传调试响应 */
struct usbd_endpoint winusb_in_ep1 = {
    .ep_addr = WINUSB_IN_EP,
    .ep_cb = dap_in_callback};

/* CDC ACM 端点：OUT 端点接收主机数据并转发到 UART */
static struct usbd_endpoint cdc_out_ep = {
    .ep_addr = CDC_OUT_EP,
    .ep_cb = usbd_cdc_acm_out};

/* CDC ACM 端点：IN 端点回传 UART 收到的数据 */
static struct usbd_endpoint cdc_in_ep = {
    .ep_addr = CDC_IN_EP,
    .ep_cb = usbd_cdc_acm_in};

/* 接口实例：WinUSB（DAP）、CDC 的两个接口，以及按需启用的 MSC 接口 */
struct usbd_interface winusb_intf;
struct usbd_interface intf1;
struct usbd_interface intf2;
struct usbd_interface intf3;

/**
 * @brief 注册描述符、接口和端点并启动 USB 设备协议栈。
 * @param[in] busid USB 总线编号。
 * @param[in] reg_base USB 控制器寄存器基址。
 * @note 启用的功能包含 WinUSB（DAP）、CDC ACM，以及由 CONFIG_CHERRYDAP_USE_MSC 控制的 MSC。
 */
void my_USB_init(uint8_t busid, uintptr_t reg_base)
{
#ifdef CONFIG_USBDEV_ADVANCE_DESC
    usbd_desc_register(busid, &winusbv2_descriptor);
#else
    usbd_desc_register(busid, winusbv2_descriptor);
#endif

#ifndef CONFIG_USBDEV_ADVANCE_DESC
    usbd_bos_desc_register(busid, &bos_desc);
    usbd_msosv2_desc_register(busid, &msosv2_desc);
#endif
    /* WinUSB 接口与两个批量端点 */
    usbd_add_interface(busid, &winusb_intf);
    usbd_add_endpoint(busid, &winusb_out_ep1);
    usbd_add_endpoint(busid, &winusb_in_ep1);

    /* CDC ACM 的两个接口与两个批量端点 */
    usbd_add_interface(busid, usbd_cdc_acm_init_intf(busid, &intf1));
    usbd_add_interface(busid, usbd_cdc_acm_init_intf(busid, &intf2));
    usbd_add_endpoint(busid, &cdc_out_ep);
    usbd_add_endpoint(busid, &cdc_in_ep);

#ifdef CONFIG_CHERRYDAP_USE_MSC
    usbd_add_interface(busid, usbd_msc_init_intf(busid, &intf3, MSC_OUT_EP, MSC_IN_EP));
#endif

    usbd_initialize(busid, reg_base, usbd_event_handler);
}

/* 保留的 DTR 检测与测试发送代码，当前未启用 */
// volatile uint8_t dtr_enable = 0;

// void usbd_cdc_acm_set_dtr(uint8_t busid, uint8_t intf, bool dtr)
// {
//     if (dtr)
//     {
//         dtr_enable = 1;
//     }
//     else
//     {
//         dtr_enable = 0;
//     }
// }

// void cdc_acm_data_send_with_dtr_test(uint8_t busid)
// {
//     if (dtr_enable)
//     {
//         // // memset(&write_buffer[10], 'a', 2038);
//         // ep_tx_busy_flag = true;
//         // usbd_ep_start_write(busid, CDC_IN_EP, write_buffer, 2048);
//         // while (ep_tx_busy_flag)
//         // {
//         // }
//     }
// }

/**
 * @brief 把 USB 线路编码字段限幅到 UART 驱动支持的取值。
 * @param[in] class 1 表示数据位类别，2 表示停止位类别。
 * @param[in] data 待校验的取值。
 * @return 取值合法时原样返回，越界时返回该类别的默认值。
 */
uint8_t Detection_Effect(uint8_t class, uint8_t data)
{
    // uart_word_length_t
    if (class == 1)
    {
        if (data >= 0x00 && data <= 0x03)
        {
            return data;
        }
        else
        {
            return 0x03;
        }
    }
    else if (class == 2)
    {
        if (data >= 0x01 && data <= 0x03)
        {
            return data;
        }
        else
        {
            return 0x01;
        }
    }
    return 0x01;
}

/* 把 UART 数据位配置转换为 CDC 线路编码的数据位数值 */
static uint8_t uart_word_length_to_cdc_data_bits(uart_word_length_t data_bits)
{
    switch (data_bits) {
    case UART_DATA_5_BITS:
        return 5;
    case UART_DATA_6_BITS:
        return 6;
    case UART_DATA_7_BITS:
        return 7;
    case UART_DATA_8_BITS:
    default:
        return 8;
    }
}

/* 把 UART 停止位配置转换为 CDC 线路编码的停止位代码：0 为 1 位，1 为 1.5 位，2 为 2 位 */
static uint8_t uart_stop_bits_to_cdc_char_format(uart_stop_bits_t stop_bits)
{
    switch (stop_bits) {
    case UART_STOP_BITS_1_5:
        return 1;
    case UART_STOP_BITS_2:
        return 2;
    case UART_STOP_BITS_1:
    default:
        return 0;
    }
}

/* 把 UART 校验位配置转换为 CDC 线路编码的校验类型：0 无校验，1 奇校验，2 偶校验 */
static uint8_t uart_parity_to_cdc_parity_type(uart_parity_t parity)
{
    switch (parity) {
    case UART_PARITY_ODD:
        return 1;
    case UART_PARITY_EVEN:
        return 2;
    case UART_PARITY_DISABLE:
    default:
        return 0;
    }
}
/**
 * @brief 把主机下发的线路编码参数应用到 UART 外设。
 * @param[in] busid USB 总线编号，本实现不使用。
 * @param[in] intf 接口编号，本实现不使用。
 * @param[in] line_coding 主机下发的线路编码参数。
 * @note 当前只应用波特率、数据位与停止位，校验位设置未生效。
 */
void usbd_cdc_acm_set_line_coding(uint8_t busid, uint8_t intf, struct cdc_line_coding *line_coding)
{
    (void)busid;
    (void)intf;

    uart_set_baudrate(ECHO_UART_PORT_NUM, line_coding->dwDTERate);
    uart_set_word_length(ECHO_UART_PORT_NUM, (uart_word_length_t)Detection_Effect(1, line_coding->bDataBits - 5));
    uart_set_stop_bits(ECHO_UART_PORT_NUM, (uart_stop_bits_t)Detection_Effect(2, line_coding->bCharFormat + 1));
    // uart_set_parity(ECHO_UART_PORT_NUM, (uart_parity_t)line_coding->bParityType);
}

/**
 * @brief 读取 UART 外设当前线路参数并转换为 USB 线路编码格式。
 * @param[in] busid USB 总线编号，本实现不使用。
 * @param[in] intf 接口编号，本实现不使用。
 * @param[out] line_coding 接收线路编码参数的指针。
 */
void usbd_cdc_acm_get_line_coding(uint8_t busid, uint8_t intf, struct cdc_line_coding *line_coding)
{
    uint32_t baud_rate = ECHO_UART_BAUD_RATE;
    uart_word_length_t data_bits = UART_DATA_8_BITS;
    uart_stop_bits_t stop_bits = UART_STOP_BITS_1;
    uart_parity_t parity = UART_PARITY_DISABLE;
    (void)busid;
    (void)intf;

    uart_get_baudrate(ECHO_UART_PORT_NUM, &baud_rate);
    uart_get_word_length(ECHO_UART_PORT_NUM, &data_bits);
    uart_get_stop_bits(ECHO_UART_PORT_NUM, &stop_bits);
    uart_get_parity(ECHO_UART_PORT_NUM, &parity);

    line_coding->dwDTERate = baud_rate;
    line_coding->bDataBits = uart_word_length_to_cdc_data_bits(data_bits);
    line_coding->bCharFormat = uart_stop_bits_to_cdc_char_format(stop_bits);
    line_coding->bParityType = uart_parity_to_cdc_parity_type(parity);
}

/**
 * @brief 安装 UART 驱动、配置线路参数并绑定收发引脚。
 * @note 接收与发送缓冲区均为 BUF_SIZE 的两倍；端口、波特率与引脚定义来自 myusb.h。
 * @note 配置失败会由 ESP_ERROR_CHECK 直接中止，不会返回错误码。
 */
void Uart_init(void)
{
    /* 配置 UART 驱动参数、通信引脚并安装驱动 */
    uart_config_t uart_config = {
        .baud_rate = ECHO_UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    int intr_alloc_flags = 0;

#if CONFIG_UART_ISR_IN_IRAM
    intr_alloc_flags = ESP_INTR_FLAG_IRAM;
#endif

    ESP_ERROR_CHECK(uart_driver_install(ECHO_UART_PORT_NUM, BUF_SIZE * 2, BUF_SIZE * 2, 0, NULL, intr_alloc_flags));
    ESP_ERROR_CHECK(uart_param_config(ECHO_UART_PORT_NUM, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(ECHO_UART_PORT_NUM, ECHO_TEST_TXD, ECHO_TEST_RXD, ECHO_TEST_RTS, ECHO_TEST_CTS));

    // Configure a temporary buffer for the incoming data
}

/**
 * @brief USB 主任务：初始化 DAP、FAT、USB 设备与 UART，并循环双向转发 CDC 与 UART 数据。
 * @param[in] arg FreeRTOS 任务参数，未使用。
 * @note 收发缓冲申请失败时释放已申请内存并删除自身任务。
 * @note 一轮循环没有任何数据搬运时延迟 1 个 tick，避免空转占满 CPU。
 * @warning 创建 usb_AT 任务的返回值未检查，创建失败时 AT 命令通道会静默不可用。
 */
void usb_task(void *)
{
    // esp_err_t ret;
    uint8_t *Rxdata = (uint8_t *)malloc(BUF_SIZE); /* UART 接收数据中转缓冲区 */
    uint8_t *Txdata = (uint8_t *)malloc(BUF_SIZE); /* UART 发送数据中转缓冲区 */
    if ((Rxdata == NULL) || (Txdata == NULL)) {
        USB_LOG_RAW("usb_task malloc failed\r\n");
        if (Rxdata) {
            free(Rxdata);
        }
        if (Txdata) {
            free(Txdata);
        }
        vTaskDelete(NULL);
        return;
    }
    //  uint8_t u8data[65];
    int len = 0;

    /*
     * 注意：第三个参数是环形缓冲区的内存池大小（字节），也就是缓冲区深度，
     * 必须为 2 的幂，例如 4、16、32、64、128、1024、8192、65536。
     */
    chry_ringbuffer_init(&g_uarttx, uarttx_ringbuffer, CONFIG_UARTTX_RINGBUF_SIZE);
    swd_init();
    chry_dap_state_init();
    my_fatfs(); /* 挂载 storage 分区的 FAT 文件系统到 /spiflash */

#if 0 /* MSC 已改为直接访问 storage 分区，虚拟 FAT 的三步初始化不再需要 */
    create_readme_file_entry();
    init_fat_table();
    prepare_file_content();
#endif


    my_USB_init(BUSID, ESP_USBD_BASE);
    Uart_init();
    xTaskCreate(usb_AT, "usb_AT", 4096, NULL, 5, NULL);
    while (1)
    {
        bool did_work = false;

        /* 优先搬运主机到 UART 方向的数据，避免环形缓冲区被填满 */
        while (drain_usb_to_uart(BUSID, Txdata, BUF_SIZE)) {
            did_work = true;
        }

        /* 非阻塞读取 UART 数据，写入镜像日志后通过 CDC IN 端点回传主机 */
        len = uart_read_bytes(ECHO_UART_PORT_NUM, Rxdata, (BUF_SIZE - 1), 0);
        if (len > 0) {
            did_work = true;
            usb_serial_log_append_direction("DEV->PC", (const uint8_t *)Rxdata, (size_t)len);
            if (!ep_tx_busy_flag) {
                ep_tx_busy_flag = true;
                if (usbd_ep_start_write(BUSID, CDC_IN_EP, (uint8_t *)Rxdata, len) < 0) {
                    ep_tx_busy_flag = false;
                }
            }
        }

        /* 本轮无数据可处理时让出 CPU */
        if (!did_work) {
            vTaskDelay(1);
        }
    }
}
