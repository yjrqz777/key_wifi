/**
 * @file myusb.h
 * @brief 声明 USB 复合设备（WinUSB/DAP、CDC ACM、MSC）的配置宏、全局状态与对外接口。
 * @details 这里的引脚、端点号、VID/PID 与描述符长度直接参与描述符构造，改动会同时影响主机枚举结果。
 */

#ifndef MY_USB_H_
#define MY_USB_H_





#include <stdbool.h>


#define ECHO_TEST_TXD (4)               /* CDC 到 UART 桥接使用的发送引脚 GPIO4 */
#define ECHO_TEST_RXD (5)               /* CDC 到 UART 桥接使用的接收引脚 GPIO5 */
#define ECHO_TEST_RTS (UART_PIN_NO_CHANGE) /* 不使用硬件流控 RTS */
#define ECHO_TEST_CTS (UART_PIN_NO_CHANGE) /* 不使用硬件流控 CTS */

#define ECHO_UART_PORT_NUM 2            /* 桥接使用的 UART 端口号 */
#define ECHO_UART_BAUD_RATE 115200      /* 上电默认波特率，主机可通过线路编码修改 */

#define BUF_SIZE (1024)                 /* USB 与 UART 之间单次中转缓冲区字节数 */



#define BUSID 0                         /* USB 设备控制器编号 */

#define WINUSB_IN_EP 0x81               /* DAP 调试数据 IN 端点地址 */
#define WINUSB_OUT_EP 0x01              /* DAP 调试数据 OUT 端点地址 */

#define CDC_IN_EP 0x82      /*主机 IN*/
#define CDC_OUT_EP 0x02     /*主机 OUT*/
#define CDC_INT_EP 0x83     /*主机 INT*/

#define MSC_IN_EP  0x84         /*主机 IN*/
#define MSC_OUT_EP 0x04         /*主机 OUT*/


// #define USBD_VID 0xFFFE
// #define USBD_PID 0xFFFF
#define USBD_VID           0x1234      /* 自定义厂商 ID */
#define USBD_PID           0x5678      /* 自定义产品 ID */
#define USBD_MAX_POWER 500             /* 设备声明从总线取用的最大电流，单位 mA */
#define USBD_LANGID_STRING 1033        /* 字符串描述符语言 ID，1033 即 0x0409 英文（美国） */


#define WINUSB_DESCRIPTOR_LEN (9 + 7 + 7) /* 配置描述符中 WinUSB 部分长度：配置 9 + 接口 7 + 端点 7 */

#define DAP_DESCRIPTOR_LEN WINUSB_DESCRIPTOR_LEN /* DAP 接口的描述符长度，与 WinUSB 部分相同 */


// #define USB_CONFIG_SIZE (9 + DAP_DESCRIPTOR_LEN + CDC_ACM_DESCRIPTOR_LEN)
// #define INTF_NUM 3


#define CONFIG_CHERRYDAP_USE_MSC 1 /* 取值为 1 时启用 MSC 接口，多出一对批量端点 */


#ifndef CONFIG_CHERRYDAP_USE_MSC
#define USB_CONFIG_SIZE (9 + DAP_DESCRIPTOR_LEN + CDC_ACM_DESCRIPTOR_LEN) /* 配置描述符总长度，不含 MSC */
#define INTF_NUM        3                                                /* 接口数量，不含 MSC */
#else
#define USB_CONFIG_SIZE (9 + DAP_DESCRIPTOR_LEN + CDC_ACM_DESCRIPTOR_LEN + MSC_DESCRIPTOR_LEN) /* 配置描述符总长度，含 MSC */
#define INTF_NUM        4                                                                     /* 接口数量，含 MSC */
#endif



#ifdef CONFIG_USB_HS
#define WINUSB_EP_MPS 512 /* 高速模式下批量端点最大包长 */
#else
#define WINUSB_EP_MPS 64  /* 全速模式下批量端点最大包长 */
#endif

#define USBD_WINUSB_VENDOR_CODE 0x20 /* 主机访问 MS OS 描述符时使用的厂商请求码 */

#define USBD_WEBUSB_ENABLE 0  /* 是否生成 WebUSB 设备能力描述符 */
#define USBD_BULK_ENABLE 1    /* 是否生成 WinUSB 批量传输功能子集描述符 */
#define USBD_WINUSB_ENABLE 1  /* 是否生成 WinUSB 设备能力描述符 */

/* WinUSB Microsoft OS 2.0 描述符各字段长度 */
#define WINUSB_DESCRIPTOR_SET_HEADER_SIZE 10 /* 描述符集合头长度 */
#define WINUSB_FUNCTION_SUBSET_HEADER_SIZE 8 /* 功能子集头长度 */
#define WINUSB_FEATURE_COMPATIBLE_ID_SIZE 20 /* 兼容 ID 特性长度 */

#define FUNCTION_SUBSET_LEN 160               /* 单个功能子集描述符总长度 */
#define DEVICE_INTERFACE_GUIDS_FEATURE_LEN 132 /* 设备接口 GUID 特性长度 */

#define USBD_WINUSB_DESC_SET_LEN (WINUSB_DESCRIPTOR_SET_HEADER_SIZE + USBD_WEBUSB_ENABLE * FUNCTION_SUBSET_LEN + USBD_BULK_ENABLE * FUNCTION_SUBSET_LEN) /* MS OS 2.0 描述符集合总长度 */







extern char current_dap_mode; /* DAP 调试口模式：1 表示 SWD 引脚已使能，0 表示引脚已关闭 */

#include "stdio.h"

void usb_task(void *);
void dap_task(void *);
void usb_CDC_ACM_Data_Dispose(uint32_t nbytes, uint8_t *read_buffer);
void usb_serial_log_append(const uint8_t *data, size_t len);
size_t usb_serial_log_snapshot(char *out, size_t out_size);
#endif // MYUSB_H
