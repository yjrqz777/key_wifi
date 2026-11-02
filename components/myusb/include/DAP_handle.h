/**
 * @file DAP_handle.h
 * @brief 声明 DAP 数据处理线程与环形缓冲区重载接口。
 */

#ifndef __DAP_HANDLE_H__
#define __DAP_HANDLE_H__

// #include "components/USBIP/usbip_defs.h"

/**
 * @brief DAP 句柄重启请求的类型。
 */
enum reset_handle_t
{
    NO_SIGNAL = 0,     /**< 无重启请求。 */
    RESET_HANDLE = 1,  /**< 释放后重建收发环形缓冲区。 */
    DELETE_HANDLE = 2, /**< 只释放环形缓冲区，不重建。 */
};

// void handle_dap_data_request(usbip_stage2_header *header, uint32_t length);
// void handle_dap_data_response(usbip_stage2_header *header);
// void handle_swo_trace_response(usbip_stage2_header *header);
void handle_dap_unlink();

void DAP_Thread(void *argument);

int fast_reply(uint8_t *buf, uint32_t length);

#endif