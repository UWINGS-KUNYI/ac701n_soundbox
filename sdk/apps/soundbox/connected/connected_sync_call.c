/*********************************************************************************************
    *   Filename        : app_connected.c

    *   Description     :

    *   Author          : Weixin Liang

    *   Email           : liangweixin@zh-jieli.com

    *   Last modifiled  : 2022-12-21 15:53

    *   Copyright:(c)JIELI  2011-2022  @ , All Rights Reserved.
*********************************************************************************************/
#include "system/includes.h"
#include "app_config.h"
#include "connected_api.h"
#include "app_connected.h"

#if TCFG_CONNECTED_ENABLE

/**************************************************************************************************
  Macros
**************************************************************************************************/
#define LOG_TAG_CONST       APP
#define LOG_TAG             "[CONNECTED]"
#define LOG_ERROR_ENABLE
#define LOG_DEBUG_ENABLE
#define LOG_INFO_ENABLE
/* #define LOG_DUMP_ENABLE */
#define LOG_CLI_ENABLE
#include "debug.h"

#define EVENT_POOL_SIZE         64

/**************************************************************************************************
  Data Types
**************************************************************************************************/
struct connected_event_sync_call {
    u8 type;
    u8  tx;
    u16 timer;
    int uuid;
    int priv;
    int instant;
    struct list_head entry;
};

struct connected_data_transmit {
    u8 type;
    u8 length;
    int uuid;
};

struct connected_key_event_sync {
    u8 tx_seqn;
    u8 rx_seqn;
    u8 tx_index;
    u8 rx_index;
    u8 event_lock;
    u8 event_len[2];
    u8 key_events[2][EVENT_POOL_SIZE];
    u16 hi_timer;
};

/**************************************************************************************************
  Global Variables
**************************************************************************************************/
static struct list_head sync_call_head = LIST_HEAD_INIT(sync_call_head);
static struct connected_key_event_sync key_event_sync;

/**************************************************************************************************
  Function Declarations
**************************************************************************************************/

/* --------------------------------------------------------------------------*/
/**
 * @brief 通过uuid找到对应回调函数
 *
 * @param uuid:注册回调时配置的ID
 *
 * @return 注册的回调结构体(struct connected_sync_call)指针
 */
/* ----------------------------------------------------------------------------*/
static const struct connected_sync_call *__get_sync_call_func_by_uuid(int uuid)
{
    const struct connected_sync_call *p;

    list_for_each_connected_sync_call(p) {
        if (p->uuid == uuid) {
            return p;
        }
    }
    return NULL;
}

/* --------------------------------------------------------------------------*/
/**
 * @brief 超时任务执行函数
 *
 * @param _sc:同步事件调用结构体(struct connected_event_sync_call)
 */
/* ----------------------------------------------------------------------------*/
static void __call_func_timeout(void *_sc)
{
    int msg[4];
    const struct connected_sync_call *p;
    struct connected_event_sync_call *sc = (struct connected_event_sync_call *)_sc;
    sc->timer = 0;

    p = __get_sync_call_func_by_uuid(sc->uuid);
    if (!p) {
        r_printf("sync call func not found!");
        free(sc);
        return;
    }

    int err = sc->tx ? CONNECTED_SYNC_CALL_TX : CONNECTED_SYNC_CALL_RX;

    if (!p->task_name) {
        p->func(sc->priv, err);
    } else {
        msg[0] = (int)p->func;
        msg[1] = 2;
        msg[2] = sc->priv;
        msg[3] = err;

        int os_err = os_taskq_post_type(p->task_name, Q_CALLBACK, 4, msg);
        if (os_err != OS_ERR_NONE) {
            log_error("sync_call_err: %x, %d\n", sc->uuid, os_err);
        }
    }

    free(sc);
}

/* --------------------------------------------------------------------------*/
/**
 * @brief 任务同步调用接口，约定事件双方同时执行对应的事件任务
 *
 * @param uuid:注册回调时配置的ID
 * @param priv:需要同步执行的事件
 * @param delay_ms:约定的延时时间
 * @param device_channel:需要跟本机同步执行的远端设备的标识
 *
 * @return len:实际发出去的数据长度
 */
/* ----------------------------------------------------------------------------*/
int connected_api_sync_call_by_uuid(int uuid, int priv, int delay_ms, u8 device_channel)
{
    if (!app_cis_is_connected(device_channel)) {
        return 0;
    }

    int buffer_len = sizeof(struct connected_event_sync_call);
    struct connected_event_sync_call *sc = zalloc(buffer_len);
    if (!sc) {
        return -ENOMEM;
    }

    sc->type    = CONNECTED_EVENT_SYNC_TYPE;
    sc->tx      = 1;
    sc->uuid    = uuid;
    sc->priv    = priv;
    sc->instant = delay_ms;
    int len = app_connected_send_acl_data(device_channel, (void *)sc, buffer_len);
    if (!len) {
        free(sc);
    }

    return len;
}

/* --------------------------------------------------------------------------*/
/**
 * @brief 同步事件发送成功接口
 *
 * @param data:发出去的数据
 * @param len:发出去的数据长度
 *
 * @return 0:不拦截后续外部的tx_events_suss函数的遍历 1:拦截后续外部的遍历
 */
/* ----------------------------------------------------------------------------*/
static int connected_tx_events_suss(void *data, int len)
{
    struct connected_event_sync_call *sc = (struct connected_event_sync_call *)data;
    if (sc->type != CONNECTED_EVENT_SYNC_TYPE) {
        return false;
    }
    sc->timer = sys_hi_timeout_add(sc, __call_func_timeout, sc->instant);
    list_add_tail(&sc->entry, &sync_call_head);
    return true;
}

/* --------------------------------------------------------------------------*/
/**
 * @brief 同步事件接收成功接口
 *
 * @param acl_hdl:acl数据通道句柄
 * @param data:接收到的数据
 * @param len:接收到的数据长度
 *
 * @return 0:不拦截后续外部的tx_events_suss函数的遍历 1:拦截后续外部的遍历
 */
/* ----------------------------------------------------------------------------*/
static int connected_rx_events_suss(u16 acl_hdl, void *data, int len)
{
    struct connected_event_sync_call sc;
    memcpy(&sc, data, sizeof(struct connected_event_sync_call));
    if (sc.type != CONNECTED_EVENT_SYNC_TYPE) {
        return false;
    }
    u8 *buffer = zalloc(len);
    if (!buffer) {
        ASSERT(0);
    }
    memcpy(buffer, data, len);
    sc.tx    = 0;
    sc.timer = sys_hi_timeout_add(buffer, __call_func_timeout, sc.instant);
    memcpy(buffer, &sc, sizeof(struct connected_event_sync_call));
    list_add_tail(&sc.entry, &sync_call_head);
    return true;
}

/* --------------------------------------------------------------------------*/
/**
 * @brief CIG状态回调函数，用于处理部分状态下事件同步执行的流程
 *
 * @param event:CIG状态事件
 * @param arg:状态事件附带的参数
 */
/* ----------------------------------------------------------------------------*/
static void connected_sync_call_events_handler(const CIG_EVENT event, void *arg)
{
    struct connected_event_sync_call *sc, *n;
    switch (event) {
    case CIG_EVENT_CENTRAL_CONNECT:
    case CIG_EVENT_PERIP_CONNECT:
        break;
    case CIG_EVENT_CENTRAL_DISCONNECT:
    case CIG_EVENT_PERIP_DISCONNECT:
        list_for_each_entry_safe(sc, n, &sync_call_head, entry) {
            if (sc->timer) {
                sys_hi_timeout_del(sc->timer);
            }
            list_del(&sc->entry);
            __call_func_timeout((void *)sc);
        }
        break;
    default:
        break;
    }
}

CONNECTED_SYNC_CALLBACK_REGISTER(connected_event_sync) = {
    .event_handler  = connected_sync_call_events_handler,
    .tx_events_suss = connected_tx_events_suss,
    .rx_events_suss = connected_rx_events_suss,
};

/* --------------------------------------------------------------------------*/
/**
 * @brief 发送数据给远端设备
 *
 * @param uuid:注册回调时配置的ID
 * @param data:需要发送的数据
 * @param len:发送数据的长度
 * @param device_channel:远端设备的标识
 *
 * @return 实际发送的数据长度
 */
/* ----------------------------------------------------------------------------*/
int connected_send_data_to_sibling(int uuid, void *data, u16 len, u8 device_channel)
{
    if (!app_cis_is_connected(device_channel)) {
        return 0;
    }

    int buffer_len = sizeof(struct connected_data_transmit) + len;
    struct connected_data_transmit *dt = zalloc(buffer_len);
    if (!dt) {
        return -ENOMEM;
    }

    dt->type = CONNECTED_DATA_TRANSMIT_TYPE;   //第一个Byte记录传输数据的类型
    dt->uuid = uuid;
    dt->length = len;
    memcpy((u8 *)dt + sizeof(*dt), data, len);

    int slen = app_connected_send_acl_data(device_channel, (void *)dt, buffer_len);
    if (!slen) {
        r_printf("connected_send_data_to_sibling err");
        free(dt);
    }

    return slen;
}

/* --------------------------------------------------------------------------*/
/**
 * @brief 数据发送成功接口
 *
 * @param data:实际发送出去的数据
 * @param len:数据长度
 *
 * @return 0:不拦截后续外部的tx_events_suss函数的遍历 1:拦截后续外部的遍历
 */
/* ----------------------------------------------------------------------------*/
static int connected_data_tx_suss(void *data, int len)
{
    struct connected_data_transmit *dt = (struct connected_data_transmit *)data;
    if (dt->type != CONNECTED_DATA_TRANSMIT_TYPE) {
        return false;
    }
    free(data);
    return true;
}

/* --------------------------------------------------------------------------*/
/**
 * @brief 数据接收成功接口
 *
 * @param acl_hdl:acl数据通道句柄
 * @param data:实际接收到的数据
 * @param len:数据长度
 *
 * @return 0:不拦截后续外部的tx_events_suss函数的遍历 1:拦截后续外部的遍历
 */
/* ----------------------------------------------------------------------------*/
static int connected_data_rx_suss(u16 acl_hdl, void *data, int len)
{
    int msg[6];
    struct connected_data_transmit dt;
    memcpy(&dt, data, sizeof(struct connected_data_transmit));
    if (dt.type != CONNECTED_DATA_TRANSMIT_TYPE) {
        return false;
    }

    u8 find = 0;
    const struct conn_data_trans_stub *p;
    list_for_each_connected_data_trans(p) {
        if (p->uuid == dt.uuid) {
            find = 1;
            break;
        }
    }
    if (!find) {
        r_printf("data trans callback func not found!");
        return false;
    }

    u8 *buffer = zalloc(dt.length);
    if (!buffer) {
        ASSERT(0);
    }
    memcpy(buffer, (u8 *)data + sizeof(struct connected_data_transmit), dt.length);
    if (!p->task_name) {
        msg[0] = CONNECTED_SYNC_CALL_RX;
        msg[1] = (int)buffer;
        msg[2] = dt.length;
        p->func(acl_hdl, 3, &msg);
    } else {
        msg[0] = (int)p->func;
        msg[1] = 4;
        msg[2] = acl_hdl;
        msg[3] = CONNECTED_SYNC_CALL_RX;
        msg[4] = (int)buffer;
        msg[5] = dt.length;

        int os_err = os_taskq_post_type(p->task_name, Q_CALLBACK, 6, msg);
        if (os_err != OS_ERR_NONE) {
            log_error("data_trans_err: %x, %d\n", p->uuid, os_err);
            free(buffer);
        }
    }

    return true;
}

CONNECTED_SYNC_CALLBACK_REGISTER(connected_data_transmit) = {
    .event_handler  = NULL,
    .tx_events_suss = connected_data_tx_suss,
    .rx_events_suss = connected_data_rx_suss,
};

/* --------------------------------------------------------------------------*/
/**
 * @brief 按键事件同步超时接口，按键事件在规定时间内没有转发成功时执行
 *
 * @param p:超时任务附带参数
 */
/* ----------------------------------------------------------------------------*/
static void connected_key_event_timeout(void *p)
{
    struct sys_event evt = {0};

    evt.arg     = (void *)DEVICE_EVENT_FROM_KEY;
    evt.type    = SYS_KEY_EVENT;

    local_irq_disable();

    key_event_sync.event_lock = 1;
    u8 index = key_event_sync.tx_index & BIT(0);

__again:
    if (key_event_sync.key_events[index]) {
        u8 *data = key_event_sync.key_events[index];
        for (int i = 0; i < key_event_sync.event_len[index]; i += 3) {
            evt.u.key.type = *data++;
            evt.u.key.event = *data++;
            evt.u.key.value = *data++;
            sys_event_notify(&evt);
        }
        key_event_sync.event_len[index] = 0;
    }
    if (key_event_sync.rx_index != index) {
        index = key_event_sync.rx_index;
        goto __again;
    }

    key_event_sync.hi_timer = 0;
    key_event_sync.rx_index = 0;
    key_event_sync.tx_index = 0;
    key_event_sync.event_lock = 0;

    local_irq_enable();
}

/* --------------------------------------------------------------------------*/
/**
 * @brief 按键事件同步接口，默认在按键消息池处截取按键消息
 *
 * @param evt:具体的按键消息
 *
 * @return 按键消息转发长度
 */
/* ----------------------------------------------------------------------------*/
int connected_key_event_sync(struct sys_event *evt)
{
#if (CONNECTED_KEY_EVENT_SYNC == 0)
    return 0;
#endif
    if (evt->type != SYS_KEY_EVENT) {
        return 0;
    }
    if ((u32)evt->arg == KEY_EVENT_FROM_CIG) {
        return 0;
    }
    if (key_event_sync.event_lock) {
        return 0;
    }
    if (!app_cis_is_connected(CONNECTED_DEVICE_IDENTIFICATION_M | CONNECTED_DEVICE_IDENTIFICATION_L | CONNECTED_DEVICE_IDENTIFICATION_R)) {
        return 0;
    }

    local_irq_disable();
    int len = key_event_sync.event_len[key_event_sync.rx_index];

    if (len + 3 <= EVENT_POOL_SIZE) {
        key_event_sync.key_events[key_event_sync.rx_index][len++] =  evt->u.key.type;
        key_event_sync.key_events[key_event_sync.rx_index][len++] =  evt->u.key.event;
        key_event_sync.key_events[key_event_sync.rx_index][len++] =  evt->u.key.value;
        key_event_sync.event_len[key_event_sync.rx_index] = len;
    } else {
        local_irq_enable();
        log_w("connected_key: event_pool_full\n");
        return 0;
    }

    u8 *buffer = NULL;
    int index = key_event_sync.tx_index & BIT(0);
    if (key_event_sync.event_len[index]) {
        buffer = zalloc(key_event_sync.event_len[index] + 2);
        if (!buffer) {
            return 0;
        }
        key_event_sync.rx_index = !index;
        key_event_sync.tx_index |= BIT(1);
        buffer[0] = CONNECTED_KEY_SYNC_TYPE;
        buffer[1] = key_event_sync.tx_seqn;
        memcpy(buffer + 2, key_event_sync.key_events[index], key_event_sync.event_len[index]);
    }

    if (key_event_sync.hi_timer == 0) {
        key_event_sync.hi_timer = usr_timeout_add(NULL, connected_key_event_timeout, 500, 1);
    } else {
        usr_timeout_modify(key_event_sync.hi_timer, 500);
    }

    local_irq_enable();

    int slen = 0;
    if (buffer) {
        slen = app_connected_send_acl_data(CONNECTED_DEVICE_IDENTIFICATION_M | CONNECTED_DEVICE_IDENTIFICATION_L | CONNECTED_DEVICE_IDENTIFICATION_R,
                                           (void *)buffer, key_event_sync.event_len[index] + 2);
        if (!slen) {
            free(buffer);
        }
    }

    return slen;
}

/* --------------------------------------------------------------------------*/
/**
 * @brief CIG状态回调函数，用于处理部分状态下按键同步执行的流程
 *
 * @param event:CIG状态事件
 * @param arg:状态事件附带的参数
 */
/* ----------------------------------------------------------------------------*/
static void connected_sync_key_events_handler(const CIG_EVENT event, void *arg)
{
    switch (event) {
    case CIG_EVENT_CENTRAL_CONNECT:
    case CIG_EVENT_PERIP_CONNECT:
        key_event_sync.rx_seqn = 0;
        key_event_sync.tx_seqn = 1;
        key_event_sync.rx_index = 0;
        key_event_sync.tx_index = 0;
        key_event_sync.event_lock = 0;
        key_event_sync.event_len[0] = 0;
        key_event_sync.event_len[1] = 0;
        break;
    case CIG_EVENT_CENTRAL_DISCONNECT:
    case CIG_EVENT_PERIP_DISCONNECT:
        break;
    default:
        break;
    }
}

/* --------------------------------------------------------------------------*/
/**
 * @brief 按键事件发送成功接口
 *
 * @param data:实际发送出去的数据
 * @param len:数据长度
 *
 * @return 0:不拦截后续外部的tx_events_suss函数的遍历 1:拦截后续外部的遍历
 */
/* ----------------------------------------------------------------------------*/
static int connected_tx_key_events_suss(void *data, int len)
{
    u8 *buffer = (u8 *)data;
    if (buffer[0] != CONNECTED_KEY_SYNC_TYPE) {
        return false;
    }
    int index;
    struct sys_event evt = {0};

    evt.arg     = (void *)DEVICE_EVENT_FROM_KEY;
    evt.type    = SYS_KEY_EVENT;

    local_irq_disable();

    index = key_event_sync.tx_index & BIT(0);
    if (key_event_sync.tx_index & BIT(1)) {
        u8 *key_events = buffer + 2;
        key_event_sync.event_lock = 1;
        for (int i = 0; i < len - 2; i += 3) {
            evt.u.key.type = *key_events++;
            evt.u.key.event = *key_events++;
            evt.u.key.value = *key_events++;
            sys_event_notify(&evt);
        }
        key_event_sync.event_lock = 0;
        key_event_sync.event_len[index] = 0;
        key_event_sync.tx_index = !index;
        key_event_sync.tx_seqn++;
    }

    if (key_event_sync.event_len[key_event_sync.rx_index] == 0) {
        if (key_event_sync.hi_timer) {
            usr_timeout_del(key_event_sync.hi_timer);
            key_event_sync.hi_timer = 0;
        }
    }

    free(data);

    local_irq_enable();

    return true;
}

/* --------------------------------------------------------------------------*/
/**
 * @brief 按键事件接收成功接口
 *
 * @param acl_hdl:acl数据通道句柄
 * @param data:实际接收到的数据
 * @param len:数据长度
 *
 * @return 0:不拦截后续外部的tx_events_suss函数的遍历 1:拦截后续外部的遍历
 */
/* ----------------------------------------------------------------------------*/
static int connected_rx_key_events_suss(u16 acl_hdl, void *data, int len)
{
    u8 *buffer = (u8 *)data;
    if (buffer[0] != CONNECTED_KEY_SYNC_TYPE) {
        return false;
    }

    struct sys_event evt = {0};

    if (buffer[1] == key_event_sync.rx_seqn) {
        return false;
    }
    key_event_sync.rx_seqn = buffer[1];

    evt.arg     = (void *)KEY_EVENT_FROM_CIG;
    evt.type    = SYS_KEY_EVENT;

    u8 *key_events = buffer + 2;
    for (int i = 1; i < len; i += 3) {
        evt.u.key.type = *key_events++;
        evt.u.key.event = *key_events++;
        evt.u.key.value = *key_events++;
        sys_event_notify(&evt);
    }

    return true;
}

CONNECTED_SYNC_CALLBACK_REGISTER(connected_key_sync) = {
    .event_handler  = connected_sync_key_events_handler,
    .tx_events_suss = connected_tx_key_events_suss,
    .rx_events_suss = connected_rx_key_events_suss,
};

/*==========================================================================
 *                               demo
 *=========================================================================*/
#if 0
static void common_state_sync_handler(int state, int sync_call_role)
{
    switch (state) {
    default:
        g_printf("%s %d, state:0x%x, sync_call_role:%d", __FUNCTION__, __LINE__, state, sync_call_role);
        break;
    }
}

CONNECTED_SYNC_CALL_REGISTER(common_state_sync) = {
    .uuid = 'C',
    .task_name = "app_core",
    .func = common_state_sync_handler,
};

static void data_transmit_handler(u16 acl_hdl, u8 arg_num, int *argv)
{
    int trans_role = argv[0];
    u8 *data = (u8 *)argv[1];
    int len = argv[2];
    g_printf("%s %d, length:%d, trans_role:%d", __FUNCTION__, __LINE__, len, trans_role);
    put_buf(data, len);
    //由于转发流程中申请了内存，因此执行完毕后必须释放
    if (data) {
        free(data);
    }
}

CONN_DATA_TRANS_STUB_REGISTER(data_transmit) = {
    .uuid = 'C',
    /* 没有给task_name赋值时，func默认在收数中断执行。当func在收数中断执行时，func内的代码尽量简短，避免对后续收数产生影响 */
    .task_name = "app_core",
    .func = data_transmit_handler,
};
#endif

#endif

