/**
 * @file ble.c
 * @brief BLE 串口服务端：NimBLE 从机、自定义 GATT 服务、串口桥接。
 * @details 移植自 ESP-IDF 示例 bluetooth/nimble/ble_spp/spp_server：注册 0xABF0 服务与
 *          0xABF1 特征，广播后被客户端连接；串口收到的数据用通知推给已订阅的客户端，
 *          客户端写入的数据原样转发到串口。串口沿用示例的 UART0（同时是日志口），
 *          115200 8N1，不开硬件流控。
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "driver/uart.h"
#include "host/ble_att.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/ble_store.h"
#include "host/ble_uuid.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

#include "ble.h"

/* 协议栈没把这个函数放进公共头文件，示例也是自己声明 */
void ble_store_config_init(void);

static const char *TAG = "ble";

#define BLE_DEVICE_NAME             "Key_wifi" /* 手机扫描时看到的设备名 */
/* SPP 服务和特征 UUID 定义在 ble.h 里（BLE_SVC_SPP_UUID16 / BLE_SVC_SPP_CHR_UUID16），
   必须和客户端（例如 IDF 的 spp_client 示例）保持一致 */

#define BLE_UART_PORT               UART_NUM_0           /* 桥接串口，沿用示例的 UART0 */
#define BLE_UART_BAUD_RATE          115200
#define BLE_UART_RX_BUF_SIZE        4096                 /* RX 环形缓冲大小 */
#define BLE_UART_TX_BUF_SIZE        8192                 /* TX 环形缓冲大小 */
#define BLE_UART_EVENT_QUEUE_SIZE   10                   /* UART 事件队列深度 */
#define BLE_UART_TASK_STACK_SIZE    4096
#define BLE_UART_TASK_PRIO          8

/* 每个连接一个“是否已订阅通知”标志，下标就是 conn_handle */
static bool s_conn_subs[CONFIG_BT_NIMBLE_MAX_CONNECTIONS + 1];
/* 0xABF1 特征的值句柄，注册时由协议栈回填，发通知时要用 */
static uint16_t s_spp_chr_val_handle;
/* 广播用的本机地址类型 */
static uint8_t s_own_addr_type;
/* UART 事件队列句柄 */
static QueueHandle_t s_uart_queue;

static void ble_on_reset(int reason);
static void ble_on_sync(void);
static void ble_host_task(void *param);
static void ble_uart_task(void *arg);
static int ble_gap_cb(struct ble_gap_event *event, void *arg);
static int ble_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                          struct ble_gatt_access_ctxt *ctxt, void *arg);
static int gatt_svr_init(void);

/**
 * @brief 打印一条连接的参数摘要。
 * @param[in] desc 协议栈返回的连接描述符，不能为 NULL。
 */
static void ble_print_conn_desc(const struct ble_gap_conn_desc *desc)
{
    ESP_LOGI(TAG, "handle=%d itvl=%d latency=%d timeout=%d encrypted=%d bonded=%d",
             desc->conn_handle, desc->conn_itvl, desc->conn_latency,
             desc->supervision_timeout, desc->sec_state.encrypted,
             desc->sec_state.bonded);
}

/**
 * @brief 配置广播数据并开始广播：可发现、可连接、带服务 UUID 0xABF0。
 */
static void ble_advertise(void)
{
    struct ble_gap_adv_params adv_params;
    struct ble_hs_adv_fields fields;
    const char *name;
    int rc;

    memset(&fields, 0, sizeof(fields));

    /* 通用可发现 + 不支持 BR/EDR */
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;

    /* 发射功率由协议栈自动填写 */
    fields.tx_pwr_lvl_is_present = 1;
    fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;

    /* 设备名 */
    name = ble_svc_gap_device_name();
    fields.name = (uint8_t *)name;
    fields.name_len = (uint8_t)strlen(name);
    fields.name_is_complete = 1;

    /* 16 位服务 UUID，客户端靠它认出这是 SPP 服务端 */
    fields.uuids16 = (ble_uuid16_t[]) {
        BLE_UUID16_INIT(BLE_SVC_SPP_UUID16)
    };
    fields.num_uuids16 = 1;
    fields.uuids16_is_complete = 1;

    rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "设置广播数据失败: %d", rc);
        return;
    }

    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

    rc = ble_gap_adv_start(s_own_addr_type, NULL, BLE_HS_FOREVER, &adv_params,
                           ble_gap_cb, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "启动广播失败: %d", rc);
    }
}

/**
 * @brief NimBLE 的 GAP 事件回调：连接、断开、参数更新、广播结束、MTU、订阅。
 * @param[in] event 事件内容，类型由 event->type 区分。
 * @param[in] arg 注册回调时传入的参数，这里不用。
 * @return 0 表示事件已处理。
 */
static int ble_gap_cb(struct ble_gap_event *event, void *arg)
{
    struct ble_gap_conn_desc desc;
    int rc;

    (void)arg;

    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        ESP_LOGI(TAG, "连接%s status=%d",
                 event->connect.status == 0 ? "建立" : "失败",
                 event->connect.status);
        if (event->connect.status == 0) {
            rc = ble_gap_conn_find(event->connect.conn_handle, &desc);
            if (rc == 0) {
                ble_print_conn_desc(&desc);
            }
        }
        /* 连接失败，或者允许多连接时，继续广播等下一个客户端 */
        if (event->connect.status != 0 || CONFIG_BT_NIMBLE_MAX_CONNECTIONS > 1) {
            ble_advertise();
        }
        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "断开 reason=%d", event->disconnect.reason);
        if (event->disconnect.conn.conn_handle <= CONFIG_BT_NIMBLE_MAX_CONNECTIONS) {
            s_conn_subs[event->disconnect.conn.conn_handle] = false;
        }
        /* 断开后必须重新广播，否则客户端再也搜不到 */
        ble_advertise();
        return 0;

    case BLE_GAP_EVENT_CONN_UPDATE:
        ESP_LOGI(TAG, "连接参数更新 status=%d", event->conn_update.status);
        return 0;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        ESP_LOGI(TAG, "广播结束 reason=%d", event->adv_complete.reason);
        ble_advertise();
        return 0;

    case BLE_GAP_EVENT_MTU:
        ESP_LOGI(TAG, "MTU 更新 conn_handle=%d mtu=%d",
                 event->mtu.conn_handle, event->mtu.value);
        return 0;

    case BLE_GAP_EVENT_SUBSCRIBE:
        ESP_LOGI(TAG, "订阅变化 conn_handle=%d notify=%d indicate=%d",
                 event->subscribe.conn_handle,
                 event->subscribe.cur_notify,
                 event->subscribe.cur_indicate);
        if (event->subscribe.conn_handle <= CONFIG_BT_NIMBLE_MAX_CONNECTIONS) {
            s_conn_subs[event->subscribe.conn_handle] = event->subscribe.cur_notify;
        }
        return 0;

    default:
        return 0;
    }
}

/**
 * @brief 协议栈复位回调。
 * @param[in] reason 复位原因码。
 */
static void ble_on_reset(int reason)
{
    ESP_LOGW(TAG, "协议栈复位 reason=%d", reason);
}

/**
 * @brief 协议栈就绪回调：确定地址后开始广播。
 */
static void ble_on_sync(void)
{
    uint8_t addr_val[6] = {0};
    int rc;

    rc = ble_hs_util_ensure_addr(0);
    if (rc != 0) {
        ESP_LOGE(TAG, "准备地址失败: %d", rc);
        return;
    }

    rc = ble_hs_id_infer_auto(0, &s_own_addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "推断地址类型失败: %d", rc);
        return;
    }

    rc = ble_hs_id_copy_addr(s_own_addr_type, addr_val, NULL);
    if (rc == 0) {
        ESP_LOGI(TAG, "本机地址 %02x:%02x:%02x:%02x:%02x:%02x",
                 addr_val[5], addr_val[4], addr_val[3],
                 addr_val[2], addr_val[1], addr_val[0]);
    }

    ble_advertise();
}

/**
 * @brief NimBLE 主机任务：跑协议栈事件循环，只在 nimble_port_stop() 之后才返回。
 * @param[in] param 未使用。
 */
static void ble_host_task(void *param)
{
    (void)param;

    ESP_LOGI(TAG, "主机任务启动");
    nimble_port_run();
    nimble_port_freertos_deinit();
}

/**
 * @brief 自定义 GATT 服务的访问回调：读到日志，写入转发到串口，其余忽略。
 * @param[in] conn_handle 来源连接句柄。
 * @param[in] attr_handle 被访问的属性句柄，这里不用。
 * @param[in] ctxt 访问上下文，ctxt->op 区分读写，ctxt->om 携带数据。
 * @param[in] arg 注册服务时传入的参数，这里不用。
 * @return 0 表示处理成功。
 */
static int ble_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                          struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)attr_handle;
    (void)arg;

    switch (ctxt->op) {
    case BLE_GATT_ACCESS_OP_READ_CHR:
        ESP_LOGD(TAG, "读取请求 conn_handle=%u", (unsigned)conn_handle);
        break;

    case BLE_GATT_ACCESS_OP_WRITE_CHR: {
        uint8_t buf[256];
        uint16_t len = OS_MBUF_PKTLEN(ctxt->om);

        if (len == 0) {
            break;
        }
        if (len > sizeof(buf)) {
            len = sizeof(buf);
        }
        os_mbuf_copydata(ctxt->om, 0, len, buf);
        uart_write_bytes(BLE_UART_PORT, (const char *)buf, len);
        ESP_LOGD(TAG, "写入 %u 字节并转发到串口", (unsigned)len);
        break;
    }

    default:
        break;
    }
    return 0;
}

/* SPP 服务：0xABF0 下挂一个 0xABF1 特征，可读、可写、可通知 */
static const struct ble_gatt_svc_def s_gatt_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(BLE_SVC_SPP_UUID16),
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = BLE_UUID16_DECLARE(BLE_SVC_SPP_CHR_UUID16),
                .access_cb = ble_chr_access,
                .val_handle = &s_spp_chr_val_handle,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_NOTIFY,
            }, {
                0, /* 特征表结束 */
            }
        },
    }, {
        0, /* 服务表结束 */
    },
};

/**
 * @brief GATT 元素注册回调，只做调试打印。
 * @param[in] ctxt 注册上下文。
 * @param[in] arg 未使用。
 */
static void ble_gatt_register_cb(struct ble_gatt_register_ctxt *ctxt, void *arg)
{
    char buf[BLE_UUID_STR_LEN];

    (void)arg;

    switch (ctxt->op) {
    case BLE_GATT_REGISTER_OP_SVC:
        ESP_LOGD(TAG, "注册服务 %s handle=%d",
                 ble_uuid_to_str(ctxt->svc.svc_def->uuid, buf), ctxt->svc.handle);
        break;

    case BLE_GATT_REGISTER_OP_CHR:
        ESP_LOGD(TAG, "注册特征 %s def_handle=%d val_handle=%d",
                 ble_uuid_to_str(ctxt->chr.chr_def->uuid, buf),
                 ctxt->chr.def_handle, ctxt->chr.val_handle);
        break;

    case BLE_GATT_REGISTER_OP_DSC:
        ESP_LOGD(TAG, "注册描述符 %s handle=%d",
                 ble_uuid_to_str(ctxt->dsc.dsc_def->uuid, buf), ctxt->dsc.handle);
        break;

    default:
        break;
    }
}

/**
 * @brief 注册标准 GAP/GATT 服务和自定义 SPP 服务。
 * @return 0 成功，非 0 是协议栈错误码。
 */
static int gatt_svr_init(void)
{
    int rc;

#if CONFIG_BT_NIMBLE_GAP_SERVICE
    ble_svc_gap_init();
#endif
    ble_svc_gatt_init();

    rc = ble_gatts_count_cfg(s_gatt_svcs);
    if (rc != 0) {
        return rc;
    }
    return ble_gatts_add_svcs(s_gatt_svcs);
}

/**
 * @brief 把一段数据按当前 MTU 分成若干包，用通知发给指定连接。
 * @param[in] conn_handle 目标连接句柄。
 * @param[in] data 待发送数据。
 * @param[in] len 数据长度。
 * @note mbuf 交给协议栈后由它释放，这里不再碰。
 */
static void ble_notify(uint16_t conn_handle, const uint8_t *data, uint16_t len)
{
    uint16_t mtu = ble_att_mtu(conn_handle);
    uint16_t chunk = (mtu > 3) ? (uint16_t)(mtu - 3) : (uint16_t)20;

    while (len > 0) {
        uint16_t n = (len > chunk) ? chunk : len;
        struct os_mbuf *om = ble_hs_mbuf_from_flat(data, n);
        int rc;

        if (om == NULL) {
            ESP_LOGE(TAG, "通知失败: mbuf 不足");
            return;
        }
        rc = ble_gatts_notify_custom(conn_handle, s_spp_chr_val_handle, om);
        if (rc != 0) {
            ESP_LOGW(TAG, "通知失败: %d", rc);
            return;
        }
        data += n;
        len -= n;
    }
}

/**
 * @brief 串口接收任务：读到数据后转发给所有已订阅的客户端。
 * @param[in] arg 未使用。
 */
static void ble_uart_task(void *arg)
{
    uart_event_t event;
    uint8_t *buf;
    int n;

    (void)arg;

    ESP_LOGI(TAG, "串口任务启动");

    for (;;) {
        if (xQueueReceive(s_uart_queue, &event, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        if (event.type != UART_DATA || event.size == 0) {
            continue;
        }

        buf = malloc(event.size);
        if (buf == NULL) {
            ESP_LOGE(TAG, "申请 %u 字节失败", (unsigned)event.size);
            continue;
        }

        n = uart_read_bytes(BLE_UART_PORT, buf, event.size, portMAX_DELAY);
        if (n > 0) {
            for (int i = 0; i <= CONFIG_BT_NIMBLE_MAX_CONNECTIONS; i++) {
                if (s_conn_subs[i]) {
                    ble_notify((uint16_t)i, buf, (uint16_t)n);
                }
            }
        }
        free(buf);
    }
}

/**
 * @brief 初始化串口驱动并启动接收任务。
 * @return ESP_OK 成功，其余为 ESP-IDF 错误码。
 */
static esp_err_t ble_uart_init(void)
{
    const uart_config_t uart_config = {
        .baud_rate = BLE_UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    esp_err_t err;

    err = uart_driver_install(BLE_UART_PORT, BLE_UART_RX_BUF_SIZE,
                              BLE_UART_TX_BUF_SIZE, BLE_UART_EVENT_QUEUE_SIZE,
                              &s_uart_queue, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "安装串口驱动失败: %s", esp_err_to_name(err));
        return err;
    }

    err = uart_param_config(BLE_UART_PORT, &uart_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "配置串口失败: %s", esp_err_to_name(err));
        return err;
    }

    err = uart_set_pin(BLE_UART_PORT, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE,
                       UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "设置串口引脚失败: %s", esp_err_to_name(err));
        return err;
    }

    if (xTaskCreate(ble_uart_task, "ble_uart", BLE_UART_TASK_STACK_SIZE, NULL,
                    BLE_UART_TASK_PRIO, NULL) != pdPASS) {
        ESP_LOGE(TAG, "创建串口任务失败");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

/**
 * @brief 初始化 NimBLE、GATT 服务和串口桥接。
 * @return ESP_OK 成功，其余表示初始化失败。
 * @note 必须在 ble_task 里调用，NVS 由 main 初始化。
 */
static esp_err_t ble_init(void)
{
    esp_err_t err;
    int rc;

    err = nimble_port_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nimble_port_init 失败: %s", esp_err_to_name(err));
        return err;
    }

    for (int i = 0; i <= CONFIG_BT_NIMBLE_MAX_CONNECTIONS; i++) {
        s_conn_subs[i] = false;
    }

    err = ble_uart_init();
    if (err != ESP_OK) {
        return err;
    }

    ble_hs_cfg.reset_cb = ble_on_reset;
    ble_hs_cfg.sync_cb = ble_on_sync;
    ble_hs_cfg.gatts_register_cb = ble_gatt_register_cb;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

    /* 无输入无输出的 IO 能力，配对时不弹密钥输入 */
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_NO_INPUT_OUTPUT;
    ble_hs_cfg.sm_sc = 0;

    rc = gatt_svr_init();
    if (rc != 0) {
        ESP_LOGE(TAG, "注册 GATT 服务失败: %d", rc);
        return ESP_FAIL;
    }

    rc = ble_svc_gap_device_name_set(BLE_DEVICE_NAME);
    if (rc != 0) {
        ESP_LOGE(TAG, "设置设备名失败: %d", rc);
        return ESP_FAIL;
    }

    ble_store_config_init();

    nimble_port_freertos_init(ble_host_task);
    return ESP_OK;
}

/**
 * @brief BLE 组件入口任务：初始化完成后删除自身，协议栈在自己的任务里跑。
 * @param[in] arg 未使用。
 */
void ble_task(void *arg)
{
    esp_err_t err;

    (void)arg;

    ESP_LOGI(TAG, "启动 BLE");

    err = ble_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "BLE 初始化失败: %s", esp_err_to_name(err));
    }

    vTaskDelete(NULL);
}
