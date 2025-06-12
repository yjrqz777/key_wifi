#ifndef MY_USB_H_
#define MY_USB_H_





#include <stdbool.h>


#define ECHO_TEST_TXD (4)
#define ECHO_TEST_RXD (5)
#define ECHO_TEST_RTS (UART_PIN_NO_CHANGE)
#define ECHO_TEST_CTS (UART_PIN_NO_CHANGE)

#define ECHO_UART_PORT_NUM 2
#define ECHO_UART_BAUD_RATE 115200

#define BUF_SIZE (1024)



#define BUSID 0

#define WINUSB_IN_EP 0x81
#define WINUSB_OUT_EP 0x01

#define CDC_IN_EP 0x82      /*主机 IN*/
#define CDC_OUT_EP 0x02     /*主机 OUT*/
#define CDC_INT_EP 0x83     /*主机 INT*/

#define MSC_IN_EP  0x84         /*主机 IN*/
#define MSC_OUT_EP 0x04         /*主机 OUT*/


// #define USBD_VID 0xFFFE
// #define USBD_PID 0xFFFF
#define USBD_VID           0x1234      // 自定义厂商ID
#define USBD_PID           0x5678      // 自定义产品ID
#define USBD_MAX_POWER 500
#define USBD_LANGID_STRING 1033


#define WINUSB_DESCRIPTOR_LEN (9 + 7 + 7)

#define DAP_DESCRIPTOR_LEN WINUSB_DESCRIPTOR_LEN


// #define USB_CONFIG_SIZE (9 + DAP_DESCRIPTOR_LEN + CDC_ACM_DESCRIPTOR_LEN)
// #define INTF_NUM 3


#define CONFIG_CHERRYDAP_USE_MSC 1


#ifndef CONFIG_CHERRYDAP_USE_MSC
#define USB_CONFIG_SIZE (9 + DAP_DESCRIPTOR_LEN + CDC_ACM_DESCRIPTOR_LEN)
#define INTF_NUM        3
#else
#define USB_CONFIG_SIZE (9 + DAP_DESCRIPTOR_LEN + CDC_ACM_DESCRIPTOR_LEN + MSC_DESCRIPTOR_LEN)
#define INTF_NUM        4
#endif



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







extern char current_dap_mode;

#include "stdio.h"
typedef struct tusbatdataDef
{
    char u8read_buffer[128];
    uint8_t u8ok;
}tusbatdataDef;


extern tusbatdataDef tusbatdata;



void usb_task();
void dap_task(void);
bool save_wifi_credentials(const char* ssid, const char* password);
uint8_t read_wifi_credentials(char* ssid, char* password);
void usb_CDC_ACM_Data_Dispose(uint32_t nbytes, uint8_t *read_buffer);

#endif // MYUSB_H