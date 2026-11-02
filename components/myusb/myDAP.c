/***************************************************************************************************
 * Author: yjrqz777 3210551161@qq.com
 * Date: 2025-03-23 22:06:08
 * LastEditTime: 2026-06-06 10:07:55
 * LastEditors: yjrqz777 3210551161@qq.com
 * Description: 
 * FilePath: /key_wifi/components/myusb/myDAP.c
 * @YJRQZ777
***************************************************************************************************/

/**
 * @file myDAP.c
 * @brief 在 WinUSB 端点与 CMSIS-DAP 命令处理器之间搬运数据包。
 * @details 端点回调只负责入队与续传，请求和响应各用一组 DAP_PACKET_COUNT 个缓冲构成环形队列；
 *          dap_task 循环调用 chry_dap_handle 执行命令，并按 current_dap_mode 尝试读取 SWD IDCODE。
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"

#include "usbd_core.h"
#include "usbd_cdc_acm.h"
#include "usbd_msc.h"
#include "usbd_hid.h"



#include "myusb.h"
#include "DAP_config.h"
#include "DAP.h"
#include "swd_host.h"

static const char *TAG = "Cherry USB";

#define USB_NOCACHE_RAM_SECTION /* 空宏占位，保留 CherryUSB 对缓冲区段属性的写法 */

/* 请求队列状态：写入侧在中断回调中推进，读出侧在 chry_dap_handle 中推进 */
static volatile uint16_t USB_RequestIndexI; // Request  Index In
static volatile uint16_t USB_RequestIndexO; // Request  Index Out
static volatile uint16_t USB_RequestCountI; // Request  Count In
static volatile uint16_t USB_RequestCountO; // Request  Count Out
static volatile uint8_t USB_RequestIdle;    // Request  Idle  Flag


/* 响应队列状态：与请求队列同一套环形缓冲语义，由结束发送的回调推进 */
static volatile uint16_t USB_ResponseIndexI; // Response Index In
static volatile uint16_t USB_ResponseIndexO; // Response Index Out
static volatile uint16_t USB_ResponseCountI; // Response Count In
static volatile uint16_t USB_ResponseCountO; // Response Count Out
static volatile uint8_t USB_ResponseIdle;    // Response Idle  Flag



/* DAP 收发缓冲区：每包 DAP_PACKET_SIZE 字节，各 DAP_PACKET_COUNT 个槽位，放在非缓存 RAM 中 */
__attribute__ ((aligned (4))) USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX uint8_t USB_Request[DAP_PACKET_COUNT][DAP_PACKET_SIZE];  // Request  Buffer
__attribute__ ((aligned (4))) USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX uint8_t USB_Response[DAP_PACKET_COUNT][DAP_PACKET_SIZE]; // Response Buffer
__attribute__ ((aligned (4))) uint16_t USB_RespSize[DAP_PACKET_COUNT];       


/**
 * @brief 设置请求队列的空闲标志。
 * @param[in] idle 非零表示队列空闲、可以重新启动接收，0 表示清除该标志。
 * @note 在 USB 配置完成事件的处理流程中调用，该事件由中断上下文触发。
 */
void SetUSB_RequestIdle(uint8_t idle)
{
    USB_RequestIdle = idle;
}




/**
 * @brief WinUSB OUT 端点接收完成回调，把请求包放入请求队列。
 * @param[in] busid USB 总线编号。
 * @param[in] ep 触发回调的端点地址，本实现不使用。
 * @param[in] nbytes 本次接收的字节数，本实现不使用。
 * @note 首字节为 ID_DAP_TransferAbort 时只置中止标志，不放入队列。
 * @note 队列已满时置 USB_RequestIdle，由 chry_dap_handle 在消费后重新启动接收。
 * @note 在 USB 中断上下文中执行，不能阻塞或调用非 ISR 安全的接口。
 */
void dap_out_callback(uint8_t busid, uint8_t ep, uint32_t nbytes)
{
    if (USB_Request[USB_RequestIndexI][0] == ID_DAP_TransferAbort) {
        /* 中止命令不入队，只置标志让当前的传输尽快退出 */
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
/**
 * @brief WinUSB IN 端点发送完成回调，继续发送下一个响应包。
 * @param[in] busid USB 总线编号。
 * @param[in] ep 触发回调的端点地址，本实现不使用。
 * @param[in] nbytes 本次发送的字节数，本实现不使用。
 * @note 响应队列为空时置 USB_ResponseIdle，由 chry_dap_handle 在产生新响应后重新启动发送。
 * @note 在 USB 中断上下文中执行，不能阻塞或调用非 ISR 安全的接口。
 */
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
/**
 * @brief 复位请求队列与响应队列的全部索引、计数和空闲标志。
 * @note 必须在 USB 配置完成并开始传输之前调用，残留的计数会让队列的空满判断失真。
 */
void chry_dap_state_init(void)
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


/**
 * @brief 取出排队的 DAP 请求，执行命令并把响应放入发送队列。
 * @note 需要在任务循环中反复调用；一次调用处理完当前所有待处理请求后返回，本身不阻塞。
 * @note 连续的 ID_DAP_QueueCommands 请求会被就地改写为 ID_DAP_ExecuteCommands，
 *       使一次执行覆盖整批排队命令。
 * @warning 与端点回调共享索引和计数，队列空满的交接依赖两个空闲标志，不能在中断上下文中调用。
 */
void chry_dap_handle(void)
{
    uint32_t n;

    // Process pending requests
    while (USB_RequestCountI != USB_RequestCountO) {
        // Handle Queue Commands
        n = USB_RequestIndexO;
        /* 连续的排队命令就地改写为执行命令，按 CMSIS-DAP 约定一次执行整批请求 */
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
        /* 返回值的低 16 位为响应字节数，写入对应响应槽的长度表 */
        USB_RespSize[USB_ResponseIndexI] =
            (uint16_t)DAP_ExecuteCommand(USB_Request[USB_RequestIndexO], USB_Response[USB_ResponseIndexI]);

        // Update Request Index and Count
        USB_RequestIndexO++;
        if (USB_RequestIndexO == DAP_PACKET_COUNT) {
            USB_RequestIndexO = 0U;
        }
        USB_RequestCountO++;

        if (USB_RequestIdle) {
            /* 队列出现空位时重新启动 OUT 接收，恢复因队列满而暂停的接收 */
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
            /* 有响应待发且上一次发送已结束，启动下一包 IN 发送 */
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

static TimerHandle_t xSWD_read_idcodeTimer;
extern uint8_t swd_read_idcode(uint32_t *id);
/**
 * @brief 在 DAP 模式下尝试读取目标芯片的 SWD IDCODE。
 * @note 仅在 current_dap_mode 为 1（SWD 引脚已由 DAP 命令使能）时访问目标芯片，否则直接返回。
 * @note 读取成功时打印芯片 UID；失败时打印 no chip 并调用 swd_init_debug 重新初始化调试口。
 */
void SWD_Read_idcode()
{
    static uint32_t id = 0;

    if(current_dap_mode == 1)
    {
        if(swd_read_idcode(&id))
        {
            printf("chip uid is %lx\r\n",id);
        }
        else
        {
            printf("no chip\r\n");
            swd_init_debug();
        }
    }
}


/**
 * @brief DAP 任务入口，循环处理 WinUSB 收到的调试命令。
 * @param[in] arg FreeRTOS 任务参数，未使用。
 * @note 创建周期 500ms、自动重载的软件定时器，到期回调为 SWD_Read_idcode。
 * @note 每轮循环执行一次 chry_dap_handle 后让出 1 个 tick，避免空转占满 CPU。
 */
void dap_task(void *)
{
    /* 周期尝试读取目标芯片 IDCODE，是否真正访问由 current_dap_mode 决定 */
    xSWD_read_idcodeTimer = xTimerCreate(
        "SWD_read_idcodeTimer",        // 定时器名字
        pdMS_TO_TICKS(500),   // 定时器周期：500ms
        pdTRUE,                // 自动重载
        (void *)1,             // 定时器ID
        SWD_Read_idcode         // 到期时回调函数
    );
    
    while(1)
    {
        chry_dap_handle();
        vTaskDelay(1);
    }
}