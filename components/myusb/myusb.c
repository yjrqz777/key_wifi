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
#include "esp_console.h"
#include "esp_check.h"
#include "driver/gpio.h"
#include "tinyusb.h"
#include "tusb_msc_storage.h"
#include "tusb_cdc_acm.h"
#include "sdkconfig.h"



#include "esp_vfs_fat.h"

// #include "DAP_config.h"
// #include "DAP.h"

// TaskHandle_t kDAPTaskHandle = NULL;
// void DAP_Thread(void *pvParameters);

#ifdef CONFIG_EXAMPLE_STORAGE_MEDIA_SDMMCCARD
#include "diskio_impl.h"
#include "diskio_sdmmc.h"
#endif

static const char *TAG = "for usb";

/* TinyUSB descriptors
   ********************************************************************* */
#define EPNUM_MSC       1
#define TUSB_DESC_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_MSC_DESC_LEN + TUD_CDC_DESC_LEN + TUD_HID_INOUT_DESC_LEN)

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
static uint8_t const desc_hid_dap_report[] = {TUD_HID_REPORT_DESC_GENERIC_INOUT(CFG_TUD_HID_EP_BUFSIZE)};

static uint8_t const desc_configuration[] = {
    // Config number, interface count, string index, total length, attribute, power in mA
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, TUSB_DESC_TOTAL_LEN, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),

    // Interface number, string index, EP Out & EP In address, EP size
    TUD_MSC_DESCRIPTOR(ITF_NUM_MSC, 4, EDPT_MSC_OUT, EDPT_MSC_IN, TUD_OPT_HIGH_SPEED ? 512 : 64),

    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC, 5, EDPT_CDC_NOTIFY, 8, EDPT_CDC_OUT, EDPT_CDC_IN, CFG_TUD_CDC_EP_BUFSIZE),

    TUD_HID_INOUT_DESCRIPTOR(ITF_NUM_HID, 6, HID_ITF_PROTOCOL_NONE, sizeof(desc_hid_dap_report), EDPT_HID_OUT, EDPT_HID_IN, CFG_TUD_HID_EP_BUFSIZE, 1)
};

static tusb_desc_device_t descriptor_config = {
    .bLength = sizeof(descriptor_config),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    .bDeviceClass = TUSB_CLASS_MISC,
    .bDeviceSubClass = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = 0x303A, // This is Espressif VID. This needs to be changed according to Users / Customers
    .idProduct = 0x4002,
    .bcdDevice = 0x100,
    .iManufacturer = 0x01,
    .iProduct = 0x02,
    .iSerialNumber = 0x03,
    .bNumConfigurations = 0x01
};

static char const *string_desc_arr[] = {
    (const char[]) { 0x09, 0x04 },  // 0: is supported language is English (0x0409)
    "TinyUSB",                      // 1: Manufacturer
    "TinyUSB Device",               // 2: Product
    "123456",                       // 3: Serials
    "Example MSC",                  // 4. MSC
    "Example CDC",                  // 5. CDC
    "Example HID",                  // 6. HID
};
/*********************************************************************** TinyUSB descriptors*/

#define BASE_PATH "/data" // base path to mount the partition

#define PROMPT_STR CONFIG_IDF_TARGET

uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance)
{
    return desc_hid_dap_report;
}

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t *buffer, uint16_t reqlen)
{
    return 0;
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t const *buffer, uint16_t bufsize)
{
    static uint8_t s_tx_buf[CFG_TUD_HID_EP_BUFSIZE];

    // DAP_ProcessCommand(buffer, s_tx_buf);
    tud_hid_report(0, s_tx_buf, sizeof(s_tx_buf));
}

// mount the partition and show all the files in BASE_PATH


static esp_err_t storage_init_spiflash(wl_handle_t *wl_handle)
{
    ESP_LOGI(TAG, "Initializing wear levelling");

    const esp_partition_t *data_partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_FAT, NULL);
    if (data_partition == NULL) {
        ESP_LOGE(TAG, "Failed to find FATFS partition. Check the partition table.");
        return ESP_ERR_NOT_FOUND;
    }

    return wl_mount(data_partition, wl_handle);
}
static uint8_t buf[CONFIG_TINYUSB_CDC_RX_BUFSIZE + 1];

void tinyusb_cdc_rx_callback(int itf, cdcacm_event_t *event)
{
    /* initialization */
    size_t rx_size = 0;

    /* read */
    esp_err_t ret = tinyusb_cdcacm_read(itf, buf, CONFIG_TINYUSB_CDC_RX_BUFSIZE, &rx_size);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Data from channel %d:", itf);
        ESP_LOG_BUFFER_HEXDUMP(TAG, buf, rx_size, ESP_LOG_INFO);
    } else {
        ESP_LOGE(TAG, "Read error");
    }

    /* write back */
    tinyusb_cdcacm_write_queue(itf, buf, rx_size);
    tinyusb_cdcacm_write_flush(itf, 0);
}

void tinyusb_cdc_coding_callback(int itf, cdcacm_event_t *event)
{
    ESP_LOGI(TAG, "Line coding changed");
    cdc_line_coding_t const *coding = event->line_coding_changed_data.p_line_coding;
    ESP_LOGI(TAG, "bit_rate = %ld", coding->bit_rate);
    ESP_LOGI(TAG, "stop_bits = %d", coding->stop_bits);
    ESP_LOGI(TAG, "parity = %d", coding->parity);
    ESP_LOGI(TAG, "data_bits = %d", coding->data_bits);

    // uint32_t baudrate = 0;

    // if (cdc_uart_get_baudrate(&baudrate) && (baudrate != coding->bit_rate))
    // {
    //     cdc_uart_set_baudrate(coding->bit_rate);
    // }
}

// TinyUSB MSC mount callback
// void tud_mount_cb(void) {
//     ESP_LOGE(TAG, "MSC Device mounted");

//     // Mount the FAT filesystem on the MSC device
//     // esp_vfs_fat_mount_config_t mount_config = {
//     //     .max_files = 5,
//     //     .format_if_mount_failed = false,
//     //     .allocation_unit_size = 16 * 1024
//     // };

//     // esp_err_t ret = esp_vfs_fat_spiflash_mount(base_path, "storage", &mount_config, &s_wl_handle);
//     // if (ret != ESP_OK) {
//     //     ESP_LOGE(TAG, "Failed to mount filesystem (%s)", esp_err_to_name(ret));
//     // } else {
//     //     ESP_LOGI(TAG, "Filesystem mounted successfully on %s", base_path);
//     // }
// }


void list_dir(const char *path) {  
    DIR *dir = opendir(path);  
    if (dir == NULL) {  
        ESP_LOGE(TAG, "Failed to open directory: %s", path);  
        return;  
    }  
  
    struct dirent *entry;  
    while ((entry = readdir(dir)) != NULL) {  
        ESP_LOGI(TAG, "-+--------%s", entry->d_name);  
    }  
  
    closedir(dir);  
}  

void usb_task(void)
{
    esp_err_t ret;
    // DAP_Setup();
    // const esp_vfs_fat_mount_config_t mount_config = {
    //         .max_files = 4,
    //         .format_if_mount_failed = true,
    //         .allocation_unit_size = CONFIG_WL_SECTOR_SIZE
    // };
    // ESP_ERROR_CHECK(esp_vfs_fat_spiflash_mount_ro(BASE_PATH, "storage", &mount_config));

    // ESP_LOGI(TAG, "FAT filesystem mounted at %s", BASE_PATH);

    // list_dir(BASE_PATH);

    ESP_LOGI(TAG, "Initializing storage...");
    static wl_handle_t wl_handle = WL_INVALID_HANDLE;
    ESP_ERROR_CHECK(storage_init_spiflash(&wl_handle));

    // esp_vfs_fat_register(BASE_PATH, "storage", 5, NULL);


    const tinyusb_msc_spiflash_config_t config_spi = {
        .wl_handle = wl_handle
    };
    ESP_ERROR_CHECK(tinyusb_msc_storage_init_spiflash(&config_spi));


    ESP_LOGI(TAG, "Mount storage...");
    ESP_ERROR_CHECK(tinyusb_msc_storage_mount(BASE_PATH));



    ESP_LOGI(TAG, "USB MSC initialization");
    const tinyusb_config_t tusb_cfg = {
        .device_descriptor = &descriptor_config,
        .string_descriptor = string_desc_arr,
        .string_descriptor_count = sizeof(string_desc_arr) / sizeof(string_desc_arr[0]),
        .external_phy = false,
        .configuration_descriptor = desc_configuration,
    };


    tinyusb_config_cdcacm_t acm_cfg = {
        .usb_dev = TINYUSB_USBDEV_0,
        .cdc_port = TINYUSB_CDC_ACM_0,
        .rx_unread_buf_sz = 64,
        .callback_rx = &tinyusb_cdc_rx_callback, // the first way to register a callback
        .callback_rx_wanted_char = NULL,
        .callback_line_state_changed = NULL,
        .callback_line_coding_changed = &tinyusb_cdc_coding_callback
    };

    


    ESP_ERROR_CHECK(tinyusb_driver_install(&tusb_cfg));
    ESP_LOGI(TAG, "USB MSC initialization DONE");

    ESP_ERROR_CHECK(tusb_cdc_acm_init(&acm_cfg));

    // xTaskCreate(DAP_Thread, "DAP_Task", 2048, NULL, 10, &kDAPTaskHandle);


        vTaskDelay(pdMS_TO_TICKS(2000));
        list_dir(BASE_PATH);
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(2000));
    }

}
