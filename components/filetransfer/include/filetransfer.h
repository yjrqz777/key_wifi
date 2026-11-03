/**
 * @file filetransfer.h
 * @brief 传输模式接口：XMODEM 接收文件写入 spiflash、模式切换入口。
 * @details 进入传输模式后 MSC 不再访问 spiflash，CDC 虚拟串口用于 XMODEM 接收，
 *          收到的文件写到 /spiflash 目录；退出传输模式后 MSC 恢复访问。
 */
#ifndef FILETRANSFER_H
#define FILETRANSFER_H

#include "esp_err.h"

/**
 * @brief 初始化传输组件：创建接收缓冲与传输任务，并接管 CDC 接收。
 * @retval ESP_OK 初始化成功。
 * @retval ESP_ERR_NO_MEM 接收缓冲或任务创建失败。
 */
esp_err_t filetransfer_init(void);

/**
 * @brief 在查看模式与传输模式之间切换。
 * @note 由按键长按 5 秒调用；切换后 MSC 可见性与 CDC 数据去向立即改变。
 */
void filetransfer_toggle_mode(void);

#endif // FILETRANSFER_H
