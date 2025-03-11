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

#include "driver/uart.h"

#include "myusb.h"

/*
 * Copyright (c) 2024, sakumisu
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "usbd_core.h"
#include "usbd_cdc_acm.h"
#include "chry_ringbuffer.h"


#define ECHO_TEST_TXD (4)
#define ECHO_TEST_RXD (5)
#define ECHO_TEST_RTS (UART_PIN_NO_CHANGE)
#define ECHO_TEST_CTS (UART_PIN_NO_CHANGE)

#define ECHO_UART_PORT_NUM      2
#define ECHO_UART_BAUD_RATE     115200

#define BUF_SIZE (1024)


static const char *TAG = "Cherry USB";


/*!< endpoint address */
#define CDC_IN_EP  0x81
#define CDC_OUT_EP 0x02
#define CDC_INT_EP 0x83

#define USBD_VID           0xFFFF
#define USBD_PID           0xFFFF
#define USBD_MAX_POWER     100
#define USBD_LANGID_STRING 1033

/*!< config descriptor size */
#define USB_CONFIG_SIZE (9 + CDC_ACM_DESCRIPTOR_LEN)

#ifdef CONFIG_USB_HS
#define CDC_MAX_MPS 512
#else
#define CDC_MAX_MPS 64
#endif


/* TinyUSB descriptors
   ********************************************************************* */
enum {
    ITF_NUM_CDC = 0,
    ITF_NUM_CDC_DATA,
    ITF_NUM_MSC ,
    ITF_NUM_HID,
    ITF_NUM_TOTAL
};

enum {
    EDPT_CTRL_OUT = 0x00,
    EDPT_CTRL_IN  = 0x80,

    EDPT_MSC_OUT = 0x04,
    EDPT_MSC_IN = 0x84,

    EDPT_CDC_NOTIFY = 0x81,
    EDPT_CDC_OUT = 0x02,
    EDPT_CDC_IN = 0x82,

    EDPT_HID_OUT = 0x03,
    EDPT_HID_IN = 0x83,
};


chry_ringbuffer_t rb;
uint8_t mempool[1024];


#ifdef CONFIG_USBDEV_ADVANCE_DESC
static const uint8_t device_descriptor[] = {
    USB_DEVICE_DESCRIPTOR_INIT(USB_2_0, 0xEF, 0x02, 0x01, USBD_VID, USBD_PID, 0x0100, 0x01)
};

static const uint8_t config_descriptor[] = {
    USB_CONFIG_DESCRIPTOR_INIT(USB_CONFIG_SIZE, 0x02, 0x01, USB_CONFIG_BUS_POWERED, USBD_MAX_POWER),
    CDC_ACM_DESCRIPTOR_INIT(0x00, CDC_INT_EP, CDC_OUT_EP, CDC_IN_EP, CDC_MAX_MPS, 0x02)
};

static const uint8_t device_quality_descriptor[] = {
    ///////////////////////////////////////
    /// device qualifier descriptor
    ///////////////////////////////////////
    0x0a,
    USB_DESCRIPTOR_TYPE_DEVICE_QUALIFIER,
    0x00,
    0x02,
    0x00,
    0x00,
    0x00,
    0x40,
    0x00,
    0x00,
};

static const char *string_descriptors[] = {
    (const char[]){ 0x09, 0x04 }, /* Langid */
    "CherryUSB",                  /* Manufacturer */
    "CherryUSB CDC DEMO",         /* Product */
    "2022123456",                 /* Serial Number */
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
    if (index > 3) {
        return NULL;
    }
    return string_descriptors[index];
}

const struct usb_descriptor cdc_descriptor = {
    .device_descriptor_callback = device_descriptor_callback,
    .config_descriptor_callback = config_descriptor_callback,
    .device_quality_descriptor_callback = device_quality_descriptor_callback,
    .string_descriptor_callback = string_descriptor_callback
};
#else
/*!< global descriptor */
static const uint8_t cdc_descriptor[] = {
    USB_DEVICE_DESCRIPTOR_INIT(USB_2_0, 0xEF, 0x02, 0x01, USBD_VID, USBD_PID, 0x0100, 0x01),
    USB_CONFIG_DESCRIPTOR_INIT(USB_CONFIG_SIZE, 0x02, 0x01, USB_CONFIG_BUS_POWERED, USBD_MAX_POWER),
    CDC_ACM_DESCRIPTOR_INIT(0x00, CDC_INT_EP, CDC_OUT_EP, CDC_IN_EP, CDC_MAX_MPS, 0x02),
    ///////////////////////////////////////
    /// string0 descriptor
    ///////////////////////////////////////
    USB_LANGID_INIT(USBD_LANGID_STRING),
    ///////////////////////////////////////
    /// string1 descriptor
    ///////////////////////////////////////
    0x14,                       /* bLength */
    USB_DESCRIPTOR_TYPE_STRING, /* bDescriptorType */
    'C', 0x00,                  /* wcChar0 */
    'h', 0x00,                  /* wcChar1 */
    'e', 0x00,                  /* wcChar2 */
    'r', 0x00,                  /* wcChar3 */
    'r', 0x00,                  /* wcChar4 */
    'y', 0x00,                  /* wcChar5 */
    'U', 0x00,                  /* wcChar6 */
    'S', 0x00,                  /* wcChar7 */
    'B', 0x00,                  /* wcChar8 */
    ///////////////////////////////////////
    /// string2 descriptor
    ///////////////////////////////////////
    0x26,                       /* bLength */
    USB_DESCRIPTOR_TYPE_STRING, /* bDescriptorType */
    'C', 0x00,                  /* wcChar0 */
    'h', 0x00,                  /* wcChar1 */
    'e', 0x00,                  /* wcChar2 */
    'r', 0x00,                  /* wcChar3 */
    'r', 0x00,                  /* wcChar4 */
    'y', 0x00,                  /* wcChar5 */
    'U', 0x00,                  /* wcChar6 */
    'S', 0x00,                  /* wcChar7 */
    'B', 0x00,                  /* wcChar8 */
    ' ', 0x00,                  /* wcChar9 */
    'C', 0x00,                  /* wcChar10 */
    'D', 0x00,                  /* wcChar11 */
    'C', 0x00,                  /* wcChar12 */
    ' ', 0x00,                  /* wcChar13 */
    'D', 0x00,                  /* wcChar14 */
    'E', 0x00,                  /* wcChar15 */
    'M', 0x00,                  /* wcChar16 */
    'O', 0x00,                  /* wcChar17 */
    ///////////////////////////////////////
    /// string3 descriptor
    ///////////////////////////////////////
    0x16,                       /* bLength */
    USB_DESCRIPTOR_TYPE_STRING, /* bDescriptorType */
    '2', 0x00,                  /* wcChar0 */
    '0', 0x00,                  /* wcChar1 */
    '2', 0x00,                  /* wcChar2 */
    '2', 0x00,                  /* wcChar3 */
    '1', 0x00,                  /* wcChar4 */
    '2', 0x00,                  /* wcChar5 */
    '3', 0x00,                  /* wcChar6 */
    '4', 0x00,                  /* wcChar7 */
    '5', 0x00,                  /* wcChar8 */
    '6', 0x00,                  /* wcChar9 */
#ifdef CONFIG_USB_HS
    ///////////////////////////////////////
    /// device qualifier descriptor
    ///////////////////////////////////////
    0x0a,
    USB_DESCRIPTOR_TYPE_DEVICE_QUALIFIER,
    0x00,
    0x02,
    0x00,
    0x00,
    0x00,
    0x40,
    0x00,
    0x00,
#endif
    0x00
};
#endif

USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX uint8_t read_buffer[CDC_MAX_MPS]; /* 2048 is only for test speed , please use CDC_MAX_MPS for common*/
USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX uint8_t write_buffer[CDC_MAX_MPS];

volatile bool ep_tx_busy_flag = false;

static void usbd_event_handler(uint8_t busid, uint8_t event)
{
    switch (event) {
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
            usbd_ep_start_read(busid, CDC_OUT_EP, read_buffer, 64);
            break;
        case USBD_EVENT_SET_REMOTE_WAKEUP:
            break;
        case USBD_EVENT_CLR_REMOTE_WAKEUP:
            break;

        default:
            break;
    }
}

uint32_t read_len = 0;
void usbd_cdc_acm_bulk_out(uint8_t busid, uint8_t ep, uint32_t nbytes)
{
    USB_LOG_RAW("actual out len:%d\r\n", nbytes);
    // chry_ringbuffer_write(&rb, read_buffer, nbytes);
    read_len = nbytes;
    uart_write_bytes(ECHO_UART_PORT_NUM, (const char *) read_buffer, nbytes);
    for (int i = 0; i < nbytes; i++) {
        // printf("%02x ", read_buffer[i]);
        USB_LOG_RAW("%c", (char)read_buffer[i]);
    }
    USB_LOG_RAW("\r\n");
    // printf("\r\n");
    /* setup next out ep read transfer */
    usbd_ep_start_read(busid, CDC_OUT_EP, read_buffer, CDC_MAX_MPS);
}

void usbd_cdc_acm_bulk_in(uint8_t busid, uint8_t ep, uint32_t nbytes)
{
    USB_LOG_RAW("actual in len:%d\r\n", nbytes);

    if ((nbytes % usbd_get_ep_mps(busid, ep)) == 0 && nbytes) {
        /* send zlp */
        usbd_ep_start_write(busid, CDC_IN_EP, (uint8_t *)"12345", 6);
    } 
    else 
    {
        ep_tx_busy_flag = false;
    }
    USB_LOG_RAW("ep_tx_busy_flag:%d\r\n", ep_tx_busy_flag);
}

/*!< endpoint call back */
struct usbd_endpoint cdc_out_ep = {
    .ep_addr = CDC_OUT_EP,
    .ep_cb = usbd_cdc_acm_bulk_out
};

struct usbd_endpoint cdc_in_ep = {
    .ep_addr = CDC_IN_EP,
    .ep_cb = usbd_cdc_acm_bulk_in
};

static struct usbd_interface intf0;
static struct usbd_interface intf1;

/***************************************************************************************************
 * 功能描述: 
 * 输入参数: 
 * 输出参数: 
 * 返 回 值: 
 * 其它说明: 
 * param {uint8_t} busid
 * param {uintptr_t} reg_base
***************************************************************************************************/
void my_cdc_acm_init(uint8_t busid, uintptr_t reg_base)
{
    const uint8_t data[10] = { 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x30 };

    // intf0.class_endpoint_handler = usbd_cdc_acm_class_handle;


    memcpy(&write_buffer[0], data, 10);
    // memset(&write_buffer[10], 'a', 64-10);

#ifdef CONFIG_USBDEV_ADVANCE_DESC
    usbd_desc_register(busid, &cdc_descriptor);
#else
    usbd_desc_register(busid, cdc_descriptor);
#endif
    usbd_add_interface(busid, usbd_cdc_acm_init_intf(busid, &intf0));
    usbd_add_interface(busid, usbd_cdc_acm_init_intf(busid, &intf1));
    usbd_add_endpoint(busid, &cdc_out_ep);
    usbd_add_endpoint(busid, &cdc_in_ep);
    usbd_initialize(busid, reg_base, usbd_event_handler);
}

volatile uint8_t dtr_enable = 0;
/***************************************************************************************************
 * 功能描述: 
 * 输入参数: 
 * 输出参数: 
 * 返 回 值: 
 * 其它说明: 
 * param {uint8_t} busid
 * param {uint8_t} intf
 * param {bool} dtr
***************************************************************************************************/
void usbd_cdc_acm_set_dtr(uint8_t busid, uint8_t intf, bool dtr)
{
    if (dtr) {
        dtr_enable = 1;
    } else {
        dtr_enable = 0;
    }
}
/***************************************************************************************************
 * 功能描述: 
 * 输入参数: 
 * 输出参数: 
 * 返 回 值: 
 * 其它说明: 
 * param {uint8_t} busid
***************************************************************************************************/
void cdc_acm_data_send_with_dtr_test(uint8_t busid)
{
    if (dtr_enable) {
        ep_tx_busy_flag = true;
        usbd_ep_start_write(busid, CDC_IN_EP, write_buffer, 2048);
        while (ep_tx_busy_flag) {
        }
    }
}


uint8_t Detection_Effect(uint8_t class,uint8_t data)
{
    // uart_word_length_t
    if (class == 1)
    {
        if (data >=0x00 && data <= 0x03)
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
        if (data >=0x01 && data <= 0x03)
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
    uart_set_word_length(ECHO_UART_PORT_NUM, (uart_word_length_t)Detection_Effect(1,line_coding->bDataBits - 5));
    uart_set_stop_bits(ECHO_UART_PORT_NUM, (uart_stop_bits_t)Detection_Effect(2,line_coding->bCharFormat + 1));
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

    uart_get_baudrate(ECHO_UART_PORT_NUM,&uart_config.baud_rate);
    uart_get_word_length(ECHO_UART_PORT_NUM,&uart_config.data_bits);
    uart_get_stop_bits(ECHO_UART_PORT_NUM,&uart_config.stop_bits);
    // uart_get_parity(ECHO_UART_PORT_NUM,&uart_config.parity);


    line_coding->dwDTERate = uart_config.baud_rate;
    line_coding->bDataBits = uart_config.data_bits + 5;
    line_coding->bCharFormat = uart_config.stop_bits - 1;
    line_coding->bParityType = 0;
    // line_coding->bParityType = uart_config.parity - 1;
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
        .parity    = UART_PARITY_DISABLE,
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
    esp_err_t ret;
    uint8_t *data = (uint8_t *) malloc(BUF_SIZE);
    uint8_t u8data[65];




    /**
     * 需要注意的点是，init 函数第三个参数是内存池的大小（字节为单位）
     * 也是ringbuffer的深度，必须为 2 的幂次！！！。
     * 例如 4、16、32、64、128、1024、8192、65536等
     */
    if(0 == chry_ringbuffer_init(&rb, mempool, 1024)){
        printf("success\r\n");
    }else{
        printf("error\r\n");
    }

    uint32_t baud_rate = 0;

    my_cdc_acm_init(0, ESP_USBD_BASE);
    Uart_init();
    // vTaskDelay(pdMS_TO_TICKS(2000));
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10));
        // uart_get_baudrate(ECHO_UART_PORT_NUM,&baud_rate);

        // printf("%ld\r\n",baud_rate);


        // uint32_t used = chry_ringbuffer_get_used(&rb);
        // if (used != 0)
        // {
        //     chry_ringbuffer_read(&rb, u8data, read_len);
        //     uart_write_bytes(ECHO_UART_PORT_NUM, (const char *) u8data, read_len);
        //     usbd_ep_start_read(0, CDC_OUT_EP, read_buffer, CDC_MAX_MPS);
        //     printf("%ld,%ld\n",used,read_len);
        // }
        
        
        


        // char data[1024];
        // while(1){
        //     uint32_t len =chry_ringbuffer_read(&rb, data, 11);
        //     if (len){
        //         printf("[C] read success, read %d byte\r\n",len);
        //         data[11]='\0';
        //         printf("%s\r\n",data);
        //     }else{
        //         printf("[C] read faild, no data in ringbuffer\r\n");
        //     }
        //     vTaskDelay(100);
        // }




        // // Read data from the UART
        int len = uart_read_bytes(ECHO_UART_PORT_NUM, data, (BUF_SIZE - 1), 20 / portTICK_PERIOD_MS);
        // Write data back to the UART
        // uart_write_bytes(ECHO_UART_PORT_NUM, (const char *) data, len);
        usbd_ep_start_write(0, CDC_IN_EP, (uint8_t *)data, len);
        // if (len) {
        //     data[len] = '\0';
        //     ESP_LOGI(TAG, "Recv str: %s", (char *) data);
        //     usbd_ep_start_write(0, CDC_IN_EP, (uint8_t *)"yjrqz", 6);
        // }

        // cdc_acm_data_send_with_dtr_test(0);
    }

}
