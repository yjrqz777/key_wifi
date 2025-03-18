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

#include <errno.h>
#include <dirent.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_console.h"
#include "esp_check.h"
#include "driver/gpio.h"
#include "sdkconfig.h"
#include "esp_log.h"

#include "esp_task_wdt.h"
#include "driver/uart.h"

#include "myusb.h"

/*
 * Copyright (c) 2024, sakumisu
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "usbd_core.h"
#include "usbd_cdc_acm.h"
#include "usbd_hid.h"
#include "chry_ringbuffer.h"

#include "DAP_config.h"
#include "DAP.h"
#include "swd_host.h"

#define ECHO_TEST_TXD (4)
#define ECHO_TEST_RXD (5)
#define ECHO_TEST_RTS (UART_PIN_NO_CHANGE)
#define ECHO_TEST_CTS (UART_PIN_NO_CHANGE)

#define ECHO_UART_PORT_NUM 2
#define ECHO_UART_BAUD_RATE 115200

#define BUF_SIZE (512)

static const char *TAG = "Cherry USB";

#define BUSID 0

#define WINUSB_IN_EP 0x81
#define WINUSB_OUT_EP 0x02

#define CDC_IN_EP 0x83
#define CDC_OUT_EP 0x04
#define CDC_INT_EP 0x85

#define USBD_VID 0xFFFE
#define USBD_PID 0xFFFF
#define USBD_MAX_POWER 500
#define USBD_LANGID_STRING 1033


#define WINUSB_DESCRIPTOR_LEN (9 + 7 + 7)

#define DAP_DESCRIPTOR_LEN WINUSB_DESCRIPTOR_LEN


#define USB_CONFIG_SIZE (9 + DAP_DESCRIPTOR_LEN + CDC_ACM_DESCRIPTOR_LEN)
#define INTF_NUM 3

#ifdef CONFIG_USB_HS
#define WINUSB_EP_MPS 512
#else
#define WINUSB_EP_MPS 64
#endif

#define USBD_WINUSB_VENDOR_CODE 0x20

#define USBD_WEBUSB_ENABLE 0
#define USBD_BULK_ENABLE 1
#define USBD_WINUSB_ENABLE 1

/* WinUSB Microsoft OS 2.0 descriptor sizes */
#define WINUSB_DESCRIPTOR_SET_HEADER_SIZE 10
#define WINUSB_FUNCTION_SUBSET_HEADER_SIZE 8
#define WINUSB_FEATURE_COMPATIBLE_ID_SIZE 20

#define FUNCTION_SUBSET_LEN 160
#define DEVICE_INTERFACE_GUIDS_FEATURE_LEN 132

#define USBD_WINUSB_DESC_SET_LEN (WINUSB_DESCRIPTOR_SET_HEADER_SIZE + USBD_WEBUSB_ENABLE * FUNCTION_SUBSET_LEN + USBD_BULK_ENABLE * FUNCTION_SUBSET_LEN)

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

#define USBD_NUM_DEV_CAPABILITIES (USBD_WEBUSB_ENABLE + USBD_WINUSB_ENABLE)

#define USBD_WEBUSB_DESC_LEN 24
#define USBD_WINUSB_DESC_LEN 28

#define USBD_BOS_WTOTALLENGTH (0x05 +                                      \
                               USBD_WEBUSB_DESC_LEN * USBD_WEBUSB_ENABLE + \
                               USBD_WINUSB_DESC_LEN * USBD_WINUSB_ENABLE)

#define CONFIG_UARTTX_RINGBUF_SIZE (1024)
// #define CONFIG_USBRX_RINGBUF_SIZE  (8 * 1024)

// #define GD32_UID_BASE        0x1FFF7A10UL           /*!< Unique device ID register base address */
// #define SERIAL_NUMBER_INDEX  208 // 序列号在数组中的起始索引

static USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX uint8_t uarttx_ringbuffer[CONFIG_UARTTX_RINGBUF_SIZE];
// static USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX uint8_t usbrx_ringbuffer[CONFIG_USBRX_RINGBUF_SIZE];
// static USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX uint8_t usb_tmpbuffer[DAP_PACKET_SIZE];

// static volatile uint8_t usbrx_idle_flag = 0;
// static volatile uint8_t usbtx_idle_flag = 0;
static volatile uint8_t uarttx_buff_full = 0;

chry_ringbuffer_t g_uarttx;
// chry_ringbuffer_t g_usbrx;

// __attribute__ ((aligned (4))) static uint8_t _usbtx_buffer[CONFIG_UARTRX_RINGBUF_SIZE];

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

struct usb_msosv2_descriptor msosv2_desc = {
    .vendor_code = USBD_WINUSB_VENDOR_CODE,
    .compat_id = USBD_WinUSBDescriptorSetDescriptor,
    .compat_id_len = USBD_WINUSB_DESC_SET_LEN,
};

struct usb_bos_descriptor bos_desc = {
    .string = USBD_BinaryObjectStoreDescriptor,
    .string_len = USBD_BOS_WTOTALLENGTH};

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

static const char *string_descriptors[] = {
    (const char[]){0x09, 0x04}, /* Langid */
    "CherryUSB",                /* Manufacturer */
    "CherryUSB WINUSB DEMO",    /* Product */
    "2022123456",               /* Serial Number */
    "MyWinUSB IF",              /* WinUSB接口名称 */
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

const struct usb_descriptor winusbv2_descriptor = {
    .device_descriptor_callback = device_descriptor_callback,
    .config_descriptor_callback = config_descriptor_callback,
    .device_quality_descriptor_callback = device_quality_descriptor_callback,
    .string_descriptor_callback = string_descriptor_callback,
    .msosv2_descriptor = &msosv2_desc,
    .bos_descriptor = &bos_desc};
#else
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
    /* String 0 (LANGID) */
    USB_LANGID_INIT(USBD_LANGID_STRING),
    /* String 1 (Manufacturer) */
    0x14,                       /* bLength */
    USB_DESCRIPTOR_TYPE_STRING, /* bDescriptorType */
    'C', 0x00, 'h', 0x00, 'e', 0x00, 'r', 0x00, 'r', 0x00, 'y', 0x00, 'U', 0x00, 'S', 0x00, 'B', 0x00,

    /* String 2 (Product) */
    0x1E, /* bLength: 13 characters + 2 = 0x1A bytes */
    USB_DESCRIPTOR_TYPE_STRING,
    'M', 0x00, 'y', 0x00, 'C', 0x00, 'u', 0x00, 's', 0x00, 't', 0x00, 'o', 0x00, 'm', 0x00, 'D', 0x00, 'e', 0x00, 'v', 0x00, 'i', 0x00, 'c', 0x00, 'e', 0x00,

    /* String 3 (Serial) */
    0x16, /* bLength */
    USB_DESCRIPTOR_TYPE_STRING,
    '2', 0x00, '0', 0x00, '2', 0x00, '2', 0x00, '1', 0x00, '2', 0x00, '3', 0x00, '4', 0x00, '5', 0x00, '6', 0x00,

    /* String 4 (WinUSB接口名称) */
    0x18, /* bLength: 11字符 * 2 + 2 = 0x1A */
    USB_DESCRIPTOR_TYPE_STRING,
    'M', 0x00, 'y', 0x00, 'W', 0x00, 'i', 0x00, 'n', 0x00, 'U', 0x00, 'S', 0x00, 'B', 0x00, ' ', 0x00, 'I', 0x00, 'F', 0x00,

    /* String 5 (CDC接口名称) */
    0x16, /* bLength: 10字符 * 2 + 2 = 0x18 */
    USB_DESCRIPTOR_TYPE_STRING,
    'M', 0x00, 'y', 0x00, 'C', 0x00, 'D', 0x00, 'C', 0x00, ' ', 0x00, 'P', 0x00, 'o', 0x00, 'r', 0x00, 't', 0x00,

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



static volatile uint16_t USB_RequestIndexI; // Request  Index In
static volatile uint16_t USB_RequestIndexO; // Request  Index Out
static volatile uint16_t USB_RequestCountI; // Request  Count In
static volatile uint16_t USB_RequestCountO; // Request  Count Out
static volatile uint8_t USB_RequestIdle;    // Request  Idle  Flag

static volatile uint16_t USB_ResponseIndexI; // Response Index In
static volatile uint16_t USB_ResponseIndexO; // Response Index Out
static volatile uint16_t USB_ResponseCountI; // Response Count In
static volatile uint16_t USB_ResponseCountO; // Response Count Out
static volatile uint8_t USB_ResponseIdle;    // Response Idle  Flag

__attribute__ ((aligned (4))) static USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX uint8_t USB_Request[DAP_PACKET_COUNT][DAP_PACKET_SIZE];  // Request  Buffer
__attribute__ ((aligned (4))) static USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX uint8_t USB_Response[DAP_PACKET_COUNT][DAP_PACKET_SIZE]; // Response Buffer
__attribute__ ((aligned (4))) static uint16_t USB_RespSize[DAP_PACKET_COUNT];           



USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX uint8_t usb_read_buffer[2048];
USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX uint8_t cdc_read_buffer[WINUSB_EP_MPS];
USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX uint8_t write_buffer[2048];

volatile bool ep_tx_busy_flag = false;

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
        /* setup first out ep read transfer */
        USB_RequestIdle = 0U;

        // usbd_ep_start_read(DAP_OUT_EP, USB_Request[0], DAP_PACKET_SIZE);

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

void dap_out_callback(uint8_t busid, uint8_t ep, uint32_t nbytes)
{
    if (USB_Request[USB_RequestIndexI][0] == ID_DAP_TransferAbort) {
        DAP_TransferAbort = 1U;
    } else {
        USB_RequestIndexI++;
        if (USB_RequestIndexI == DAP_PACKET_COUNT) {
            USB_RequestIndexI = 0U;
        }
        USB_RequestCountI++;
    }

    // Start reception of next request packet
    if ((uint16_t)(USB_RequestCountI - USB_RequestCountO) != DAP_PACKET_COUNT) {
        usbd_ep_start_read(busid, WINUSB_OUT_EP, USB_Request[USB_RequestIndexI], DAP_PACKET_SIZE);
    } else {
        USB_RequestIdle = 1U;
    }
}

void dap_in_callback(uint8_t busid, uint8_t ep, uint32_t nbytes)
{
    if (USB_ResponseCountI != USB_ResponseCountO) {
        // Load data from response buffer to be sent back
        usbd_ep_start_write(busid, WINUSB_IN_EP, USB_Response[USB_ResponseIndexO], USB_RespSize[USB_ResponseIndexO]);
        USB_ResponseIndexO++;
        if (USB_ResponseIndexO == DAP_PACKET_COUNT) {
            USB_ResponseIndexO = 0U;
        }
        USB_ResponseCountO++;
    } else {
        USB_ResponseIdle = 1U;
    }
}

void usbd_cdc_acm_out(uint8_t busid, uint8_t ep, uint32_t nbytes)
{
    //  USB_LOG_RAW("actual out len:%d\r\n", nbytes);
    chry_ringbuffer_write(&g_uarttx, cdc_read_buffer, nbytes);
    if (chry_ringbuffer_get_free(&g_uarttx) >= nbytes)
    {
        /* setup next out ep read transfer */
        usbd_ep_start_read(busid, CDC_OUT_EP, cdc_read_buffer, WINUSB_EP_MPS);
    }
    else
    {
        uarttx_buff_full = 1;
    }
}

void usbd_cdc_acm_in(uint8_t busid, uint8_t ep, uint32_t nbytes)
{
    //  USB_LOG_RAW("actual in len:%d\r\n", nbytes);

    if ((nbytes % usbd_get_ep_mps(busid, ep)) == 0 && nbytes)
    {
        /* send zlp */
        usbd_ep_start_write(busid, CDC_IN_EP, NULL, 0);
    }
    else
    {
        ep_tx_busy_flag = false;
    }
}

static void chry_dap_state_init(void)
{
    // Initialize variables
    USB_RequestIndexI = 0U;
    USB_RequestIndexO = 0U;
    USB_RequestCountI = 0U;
    USB_RequestCountO = 0U;
    USB_RequestIdle = 1U;
    USB_ResponseIndexI = 0U;
    USB_ResponseIndexO = 0U;
    USB_ResponseCountI = 0U;
    USB_ResponseCountO = 0U;
    USB_ResponseIdle = 1U;
}



struct usbd_endpoint winusb_out_ep1 = {
    .ep_addr = WINUSB_OUT_EP,
    .ep_cb = dap_out_callback};

struct usbd_endpoint winusb_in_ep1 = {
    .ep_addr = WINUSB_IN_EP,
    .ep_cb = dap_in_callback};

static struct usbd_endpoint cdc_out_ep = {
    .ep_addr = CDC_OUT_EP,
    .ep_cb = usbd_cdc_acm_out};

static struct usbd_endpoint cdc_in_ep = {
    .ep_addr = CDC_IN_EP,
    .ep_cb = usbd_cdc_acm_in};

struct usbd_interface winusb_intf;
struct usbd_interface intf1;
struct usbd_interface intf2;

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
    /*!< winusb */
    usbd_add_interface(busid, &winusb_intf);
    usbd_add_endpoint(busid, &winusb_out_ep1);
    usbd_add_endpoint(busid, &winusb_in_ep1);

    /*!< cdc acm */
    usbd_add_interface(busid, usbd_cdc_acm_init_intf(busid, &intf1));
    usbd_add_interface(busid, usbd_cdc_acm_init_intf(busid, &intf2));
    usbd_add_endpoint(busid, &cdc_out_ep);
    usbd_add_endpoint(busid, &cdc_in_ep);

    usbd_initialize(busid, reg_base, usbd_event_handler);
}

volatile uint8_t dtr_enable = 0;

void usbd_cdc_acm_set_dtr(uint8_t busid, uint8_t intf, bool dtr)
{
    if (dtr)
    {
        dtr_enable = 1;
    }
    else
    {
        dtr_enable = 0;
    }
}

void cdc_acm_data_send_with_dtr_test(uint8_t busid)
{
    if (dtr_enable)
    {
        // // memset(&write_buffer[10], 'a', 2038);
        // ep_tx_busy_flag = true;
        // usbd_ep_start_write(busid, CDC_IN_EP, write_buffer, 2048);
        // while (ep_tx_busy_flag)
        // {
        // }
    }
}

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
/***************************************************************************************************
 * 功能描述:
 * 输入参数:
 * 输出参数:
 * 返 回 值:
 * 其它说明:
 * param {uint8_t} busid
 * param {uint8_t} intf
 * param {cdc_line_coding} *line_coding
 ***************************************************************************************************/
void usbd_cdc_acm_set_line_coding(uint8_t busid, uint8_t intf, struct cdc_line_coding *line_coding)
{
    (void)busid;
    (void)intf;

    uart_set_baudrate(ECHO_UART_PORT_NUM, line_coding->dwDTERate);
    uart_set_word_length(ECHO_UART_PORT_NUM, (uart_word_length_t)Detection_Effect(1, line_coding->bDataBits - 5));
    uart_set_stop_bits(ECHO_UART_PORT_NUM, (uart_stop_bits_t)Detection_Effect(2, line_coding->bCharFormat + 1));
    // uart_set_parity(ECHO_UART_PORT_NUM, (uart_parity_t)line_coding->bParityType);
}

/***************************************************************************************************
 * 功能描述:
 * 输入参数:
 * 输出参数:
 * 返 回 值:
 * 其它说明:
 * param {uint8_t} busid
 * param {uint8_t} intf
 * param {cdc_line_coding} *line_coding
 ***************************************************************************************************/
void usbd_cdc_acm_get_line_coding(uint8_t busid, uint8_t intf, struct cdc_line_coding *line_coding)
{
    uart_config_t uart_config = {0};
    (void)busid;
    (void)intf;

    uart_get_baudrate(ECHO_UART_PORT_NUM, &uart_config.baud_rate);
    uart_get_word_length(ECHO_UART_PORT_NUM, &uart_config.data_bits);
    uart_get_stop_bits(ECHO_UART_PORT_NUM, &uart_config.stop_bits);
    // uart_get_parity(ECHO_UART_PORT_NUM,&uart_config.parity);

    line_coding->dwDTERate = uart_config.baud_rate;
    line_coding->bDataBits = uart_config.data_bits + 5;
    line_coding->bCharFormat = uart_config.stop_bits - 1;
    line_coding->bParityType = 0;
    // line_coding->bParityType = uart_config.parity - 1;
}




void chry_dap_handle(void)
{
    uint32_t n;

    // Process pending requests
    while (USB_RequestCountI != USB_RequestCountO) {
        // Handle Queue Commands
        n = USB_RequestIndexO;
        while (USB_Request[n][0] == ID_DAP_QueueCommands) {
            USB_Request[n][0] = ID_DAP_ExecuteCommands;
            n++;
            if (n == DAP_PACKET_COUNT) {
                n = 0U;
            }
            if (n == USB_RequestIndexI) {
                // flags = osThreadFlagsWait(0x81U, osFlagsWaitAny, osWaitForever);
                // if (flags & 0x80U) {
                //     break;
                // }
            }
        }

        // Execute DAP Command (process request and prepare response)
        USB_RespSize[USB_ResponseIndexI] =
            (uint16_t)DAP_ExecuteCommand(USB_Request[USB_RequestIndexO], USB_Response[USB_ResponseIndexI]);

        // Update Request Index and Count
        USB_RequestIndexO++;
        if (USB_RequestIndexO == DAP_PACKET_COUNT) {
            USB_RequestIndexO = 0U;
        }
        USB_RequestCountO++;

        if (USB_RequestIdle) {
            if ((uint16_t)(USB_RequestCountI - USB_RequestCountO) != DAP_PACKET_COUNT) {
                USB_RequestIdle = 0U;
                usbd_ep_start_read(BUSID,WINUSB_OUT_EP, USB_Request[USB_RequestIndexI], DAP_PACKET_SIZE);
            }
        }

        // Update Response Index and Count
        USB_ResponseIndexI++;
        if (USB_ResponseIndexI == DAP_PACKET_COUNT) {
            USB_ResponseIndexI = 0U;
        }
        USB_ResponseCountI++;

        if (USB_ResponseIdle) {
            if (USB_ResponseCountI != USB_ResponseCountO) {
                // Load data from response buffer to be sent back
                n = USB_ResponseIndexO++;
                if (USB_ResponseIndexO == DAP_PACKET_COUNT) {
                    USB_ResponseIndexO = 0U;
                }
                USB_ResponseCountO++;
                USB_ResponseIdle = 0U;
                usbd_ep_start_write(BUSID ,WINUSB_IN_EP, USB_Response[n], USB_RespSize[n]);
            }
        }
    }
}



void dap_task(void)
{
    while(1)
    {
        chry_dap_handle();
        vTaskDelay(1);
    }
}




/***************************************************************************************************
 * 功能描述:
 * 输入参数:
 * 输出参数:
 * 返 回 值:
 * 其它说明:
 ***************************************************************************************************/
void Uart_init(void)
{
    /* Configure parameters of an UART driver,
     * communication pins and install the driver */
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

void usb_task(void)
{
    // esp_err_t ret;
    uint8_t *Rxdata = (uint8_t *)malloc(BUF_SIZE);
    uint8_t *Txdata = (uint8_t *)malloc(BUF_SIZE);
    //  uint8_t u8data[65];
    int len = 0;

    /**
     * 需要注意的点是，init 函数第三个参数是内存池的大小（字节为单位）
     * 也是ringbuffer的深度，必须为 2 的幂次！！！。
     * 例如 4、16、32、64、128、1024、8192、65536等
     */
    chry_ringbuffer_init(&g_uarttx, uarttx_ringbuffer, CONFIG_UARTTX_RINGBUF_SIZE);

    swd_init();

    chry_dap_state_init();

    my_USB_init(BUSID, ESP_USBD_BASE);
    Uart_init();

    while (1)
    {
        len = uart_read_bytes(ECHO_UART_PORT_NUM, Rxdata, (BUF_SIZE - 1), 20 / portTICK_PERIOD_MS);
        usbd_ep_start_write(BUSID, CDC_IN_EP, (uint8_t *)Rxdata, len);

        if (uarttx_buff_full)
        {
            len = chry_ringbuffer_read(&g_uarttx, Txdata, BUF_SIZE);
            uart_write_bytes(ECHO_UART_PORT_NUM, (const char *)Txdata, len);
            usbd_ep_start_read(BUSID, CDC_OUT_EP, cdc_read_buffer, WINUSB_EP_MPS);
            uarttx_buff_full = 0;
        }
        else
        {
            if (chry_ringbuffer_get_used(&g_uarttx))
            {
                len = chry_ringbuffer_read(&g_uarttx, Txdata, BUF_SIZE);
                uart_write_bytes(ECHO_UART_PORT_NUM, (const char *)Txdata, len);
            }
        }
        vTaskDelay(1);
    }
}
