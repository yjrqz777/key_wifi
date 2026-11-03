/**
 * @file filetransfer.c
 * @brief 传输模式实现：经 USB CDC 用 XMODEM 接收文件，写入 /spiflash 目录。
 * @details 进入传输模式后 MSC 拒绝访问 spiflash，CDC 收到的字节被投递到本组件的环形缓冲，
 *          由传输任务交给 XMODEM 协议层；收到一整块数据后先缓存在内存里，等下一块到来才落盘，
 *          这样最后一块里的 0x1A 填充可以在收尾时去掉。
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"

#include "chry_ringbuffer.h"
#include "devmode.h"
#include "filetransfer.h"
#include "myfile.h"
#include "myusb.h"
#include "xymodem.h"

#define TAG "XFER"

#define XFER_DIR            "/spiflash"          /* 文件写入目录，与 FATFS 挂载点一致 */
#define XFER_FILE_PATTERN   XFER_DIR "/recv_%03u.bin" /* 接收文件命名格式，XMODEM 不带文件名 */
#define XFER_FILE_MAX_INDEX 999                  /* 自动命名最多尝试的序号 */
#define XFER_RX_POOL_SIZE   8192                 /* CDC 接收环形缓冲大小，必须为 2 的幂 */
#define XFER_POLL_TICKS     1                    /* 接收轮询间隔，单位 tick */
#define XFER_BLOCK_MAX      1024                 /* XMODEM 单块最大有效数据长度 */
#define XFER_TASK_STACK     (1024 * 8)           /* 传输任务栈大小 */
#define XFER_TASK_PRIO      5                    /* 传输任务优先级 */

/* CDC 接收环形缓冲：USB 回调写入，传输任务读出 */
static chry_ringbuffer_t s_xfer_rx;
static uint8_t s_xfer_rx_pool[XFER_RX_POOL_SIZE];
static bool s_xfer_rx_ready = false; /* 环形缓冲是否已经初始化 */

/**
 * @brief 接收上下文：目标文件句柄与待落盘的上一块数据。
 */
typedef struct {
    FILE *fp;                     /**< 已打开的目标文件，未收到数据前为 NULL */
    char path[64];                /**< 目标文件完整路径 */
    uint8_t pending[XFER_BLOCK_MAX]; /**< 暂存最近一块数据，收尾时去掉尾部 0x1A 填充 */
    size_t pending_len;           /**< 暂存块的有效字节数 */
    size_t written;               /**< 已落盘的字节数 */
} xfer_file_t;

/**
 * @brief 把 CDC 收到的数据写入接收环形缓冲，由 USB 回调调用。
 * @param[in] data 收到的数据首地址。
 * @param[in] len 数据长度，单位字节。
 * @note 运行在 USB 回调上下文，只使用无锁环形缓冲；缓冲满时多余字节被丢弃。
 */
static void filetransfer_feed(const uint8_t *data, uint32_t len)
{
    if (data == NULL || len == 0 || !s_xfer_rx_ready) {
        return;
    }
    chry_ringbuffer_write(&s_xfer_rx, (void *)data, len);
}

/**
 * @brief XMODEM 传输层的接收实现：在超时时间内尽量收满请求长度。
 * @param[in] ctx 未使用。
 * @param[out] buf 接收缓冲区。
 * @param[in] len 期望接收的字节数。
 * @param[out] recv_len 实际接收到的字节数，超时时可能小于 len。
 * @param[in] timeout_ms 超时时间，单位毫秒。
 * @retval ESP_OK 正常返回，包括超时。
 * @retval ESP_ERR_INVALID_STATE 传输模式已被退出，通知协议层中止。
 */
static esp_err_t xfer_recv(void *ctx, void *buf, size_t len, size_t *recv_len, uint32_t timeout_ms)
{
    (void)ctx;
    *recv_len = 0;

    TickType_t start = xTaskGetTickCount();
    TickType_t timeout_ticks = pdMS_TO_TICKS(timeout_ms);

    while (*recv_len < len) {
        if (!devmode_is_transfer()) {
            return ESP_ERR_INVALID_STATE;
        }

        uint32_t n = chry_ringbuffer_read(&s_xfer_rx, (uint8_t *)buf + *recv_len,
                                          (uint32_t)(len - *recv_len));
        *recv_len += n;
        if (*recv_len >= len) {
            break;
        }
        if ((xTaskGetTickCount() - start) >= timeout_ticks) {
            break;
        }
        vTaskDelay(XFER_POLL_TICKS);
    }

    return ESP_OK;
}

/**
 * @brief XMODEM 传输层的发送实现：把应答字节写到 CDC IN 端点。
 * @param[in] ctx 未使用。
 * @param[in] buf 待发送数据。
 * @param[in] len 数据长度，单位字节。
 * @retval ESP_OK 数据已发出。
 * @retval ESP_FAIL CDC 端点写失败或超时。
 */
static esp_err_t xfer_send(void *ctx, const void *buf, size_t len)
{
    (void)ctx;
    return (usb_cdc_write((const uint8_t *)buf, (uint32_t)len) == 0) ? ESP_OK : ESP_FAIL;
}

/**
 * @brief 判断文件是否已存在。
 * @param[in] path 文件完整路径。
 * @retval true 文件存在。
 * @retval false 文件不存在或无法打开。
 */
static bool xfer_file_exists(const char *path)
{
    FILE *fp = fopen(path, "rb");
    if (fp == NULL) {
        return false;
    }
    fclose(fp);
    return true;
}

/**
 * @brief 生成一个新的接收文件名，序号从 001 开始递增。
 * @param[out] out 输出缓冲区。
 * @param[in] out_len 输出缓冲区长度。
 * @retval true 找到可用文件名。
 * @retval false 序号用尽，没有可用文件名。
 */
static bool xfer_pick_name(char *out, size_t out_len)
{
    for (unsigned i = 1; i <= XFER_FILE_MAX_INDEX; i++) {
        snprintf(out, out_len, XFER_FILE_PATTERN, i);
        if (!xfer_file_exists(out)) {
            return true;
        }
    }
    return false;
}

/**
 * @brief 数据落地实现：收到第一块数据时才创建文件，并保留最后一块用于去掉填充。
 * @param[in] ctx 接收上下文。
 * @param[in] buf 本次收到的数据块。
 * @param[in] len 数据块长度，单位字节。
 * @retval ESP_OK 数据已处理。
 * @retval ESP_FAIL 打开文件或写文件失败。
 */
static esp_err_t xfer_sink_write(void *ctx, const void *buf, size_t len)
{
    xfer_file_t *f = ctx;
    if (f == NULL || buf == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (f->fp == NULL) {
        if (!xfer_pick_name(f->path, sizeof(f->path))) {
            ESP_LOGE(TAG, "没有可用的接收文件名");
            return ESP_FAIL;
        }
        f->fp = fopen(f->path, "wb");
        if (f->fp == NULL) {
            ESP_LOGE(TAG, "打开 %s 失败", f->path);
            return ESP_FAIL;
        }
        ESP_LOGI(TAG, "开始接收，文件名 %s", f->path);
    }

    /* 先把上一块落盘，把当前块留在内存里，最后一块才可能含尾部填充 */
    if (f->pending_len > 0) {
        if (fwrite(f->pending, 1, f->pending_len, f->fp) != f->pending_len) {
            ESP_LOGE(TAG, "写入 %s 失败", f->path);
            return ESP_FAIL;
        }
        f->written += f->pending_len;
    }

    if (len > sizeof(f->pending)) {
        /* 超出单块上限的异常长度，直接落盘，不做填充裁剪 */
        if (fwrite(buf, 1, len, f->fp) != len) {
            ESP_LOGE(TAG, "写入 %s 失败", f->path);
            return ESP_FAIL;
        }
        f->written += len;
        f->pending_len = 0;
        return ESP_OK;
    }

    memcpy(f->pending, buf, len);
    f->pending_len = len;
    return ESP_OK;
}

/**
 * @brief 收尾：把最后一块去掉尾部 0x1A 填充后落盘。
 * @param[in] f 接收上下文。
 * @retval ESP_OK 落盘成功。
 * @retval ESP_FAIL 写文件失败。
 */
static esp_err_t xfer_sink_finish(xfer_file_t *f)
{
    if (f->fp == NULL || f->pending_len == 0) {
        return ESP_OK;
    }

    size_t len = f->pending_len;
    while (len > 0 && f->pending[len - 1] == 0x1A) {
        len--;
    }
    if (len > 0) {
        if (fwrite(f->pending, 1, len, f->fp) != len) {
            ESP_LOGE(TAG, "写入 %s 失败", f->path);
            return ESP_FAIL;
        }
        f->written += len;
    }
    f->pending_len = 0;
    return ESP_OK;
}

/**
 * @brief 执行一次 XMODEM 接收，成功则保留文件，失败则删除半成品。
 */
static void xfer_receive_once(void)
{
    xfer_file_t file = {0};

    const xymodem_transport_t transport = {
        .recv = xfer_recv,
        .send = xfer_send,
        .ctx = NULL,
    };
    const xmodem_data_sink_t sink = {
        .write = xfer_sink_write,
        .ctx = &file,
    };
    xymodem_recv_config_t config = XYMODEM_RECV_CONFIG_DEFAULT();

    /* 丢掉上一轮残留的字节，避免握手阶段读到半条旧数据 */
    chry_ringbuffer_reset(&s_xfer_rx);

    ESP_LOGI(TAG, "等待主机通过 XMODEM 发送文件");
    esp_err_t err = xmodem_recv(&transport, &config, &sink);
    if (err == ESP_OK) {
        err = xfer_sink_finish(&file);
    }

    if (file.fp != NULL) {
        fclose(file.fp);
        file.fp = NULL;
    }

    if (err == ESP_OK) {
        if (file.path[0] != '\0') {
            ESP_LOGI(TAG, "接收完成：%s，%u 字节", file.path, (unsigned)file.written);
        }
    } else if (file.path[0] != '\0') {
        ESP_LOGE(TAG, "接收失败：%s，删除 %s", esp_err_to_name(err), file.path);
        remove(file.path);
    }
}

/**
 * @brief 通过 CDC 给上位机打印一行提示。
 * @param[in] text 以 '\0' 结尾的提示文本。
 * @note 主机串口没打开时发送会失败，只记录日志，不影响后续接收。
 */
static void xfer_print_cdc(const char *text)
{
    size_t len = strlen(text);
    if (len == 0) {
        return;
    }
    if (usb_cdc_write((const uint8_t *)text, (uint32_t)len) != 0) {
        ESP_LOGW(TAG, "CDC 提示发送失败，主机串口可能未打开");
    }
}

/**
 * @brief 传输任务：跟随模式切换执行 XMODEM 接收。
 * @param[in] pvParameters 未使用的 FreeRTOS 任务参数。
 * @note 进入传输模式时重新挂载 FATFS，保证设备侧看到的是主机最后一次写入后的分区内容。
 */
static void transfer_task(void *pvParameters)
{
    (void)pvParameters;
    bool last_transfer = false;

    while (1) {
        bool transfer = devmode_is_transfer();

        if (transfer != last_transfer) {
            if (transfer) {
                ESP_LOGW(TAG, "进入传输模式：MSC 停止访问 spiflash，CDC 改作 XMODEM 接收");
                esp_err_t err = my_fatfs_remount();
                if (err != ESP_OK) {
                    ESP_LOGE(TAG, "重新挂载 %s 失败：%s", XFER_DIR, esp_err_to_name(err));
                }
                /* 先给上位机一行提示，再开始 XMODEM 握手 */
                xfer_print_cdc("\r\n[XFER] 进入传输模式，等待 XMODEM 发送文件\r\n");
            } else {
                ESP_LOGW(TAG, "退出传输模式：MSC 恢复访问 spiflash");
            }
            last_transfer = transfer;
        }

        if (transfer) {
            xfer_receive_once();
            vTaskDelay(pdMS_TO_TICKS(500));
        } else {
            vTaskDelay(pdMS_TO_TICKS(50));
        }
    }
}

esp_err_t filetransfer_init(void)
{
    if (chry_ringbuffer_init(&s_xfer_rx, s_xfer_rx_pool, sizeof(s_xfer_rx_pool)) < 0) {
        ESP_LOGE(TAG, "接收环形缓冲初始化失败");
        return ESP_ERR_INVALID_STATE;
    }
    s_xfer_rx_ready = true;

    usb_cdc_set_transfer_hook(filetransfer_feed);

    if (xTaskCreate(transfer_task, "file_xfer", XFER_TASK_STACK, NULL, XFER_TASK_PRIO, NULL) != pdPASS) {
        ESP_LOGE(TAG, "传输任务创建失败");
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

void filetransfer_toggle_mode(void)
{
    if (devmode_is_transfer()) {
        devmode_set(DEVMODE_VIEW);
    } else {
        devmode_set(DEVMODE_TRANSFER);
    }
}
