/*********************************************************************************************
    *   Filename        : app_connected.c

    *   Description     :

    *   Author          : Weixin Liang

    *   Email           : liangweixin@zh-jieli.com

    *   Last modifiled  : 2022-12-15 14:30

    *   Copyright:(c)JIELI  2011-2022  @ , All Rights Reserved.
*********************************************************************************************/
#include "system/includes.h"
#include "cig.h"
#include "connected_api.h"
#include "app_connected.h"
#include "app_config.h"
#include "app_cfg.h"
#include "bt/bt.h"
#include "app_task.h"
#include "btstack/avctp_user.h"
#include "tone_player.h"
#include "app_main.h"
#include "audio_mode.h"
#if TCFG_LINEIN_ENABLE
#include "linein/linein.h"
#endif
#include "music_player.h"
#if defined(RCSP_MODE) && RCSP_MODE
#include "ble_rcsp_server.h"
#include "rcsp_feature.h"
#endif

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

#define TRANSMITTER_AUTO_TEST_EN    0
#define RECEIVER_AUTO_TEST_EN       0

#define CIG_CONNECTED_TIMEOUT   (10 * 1000L)  //单位:ms

/**************************************************************************************************
  Data Types
**************************************************************************************************/
struct app_cis_conn_info {
    u8 cis_status;
    u8 remote_dev_identification;
    u16 cis_hdl;
    u16 acl_hdl;
};

struct app_cig_conn_info {
    u8 used;
    u8 cig_hdl;
    u8 cig_status;
    u8 local_dev_identification;
    struct app_cis_conn_info cis_conn_info[CIG_MAX_CIS_NUMS];
};

/**************************************************************************************************
  Static Prototypes
**************************************************************************************************/
static bool is_connected_as_central();
static void app_cig_connection_timeout(void *priv);

/**************************************************************************************************
  Global Variables
**************************************************************************************************/
static OS_MUTEX mutex;
static u8 connected_last_role = 0; /*!< 挂起前CIG角色 */
static u8 connected_app_mode_exit = 0;  /*!< 音源模式退出标志 */
static u8 config_connected_as_master = 0;   /*!< 配置强制做发送设备 */
static u8 acl_connected_nums = 0;
static u8 cis_connected_nums = 0;
static struct app_cig_conn_info app_cig_conn_info[CIG_MAX_NUMS];
static int cig_connection_timeout = 0;
const static u8 audio_mode_map[][2] = {
    {APP_BT_TASK, LIVE_A2DP_CAPTURE_MODE},
    {APP_MUSIC_TASK, LIVE_FILE_CAPTURE_MODE},
    {APP_LINEIN_TASK, LIVE_AUX_CAPTURE_MODE},
    {APP_PC_TASK, LIVE_USB_CAPTURE_MODE},
    {APP_LIVE_MIC_TASK, LIVE_MIC_CAPTURE_MODE},
    {APP_LIVE_IIS_TASK, LIVE_IIS_CAPTURE_MODE},
};

/**************************************************************************************************
  Function Declarations
**************************************************************************************************/

/* --------------------------------------------------------------------------*/
/**
 * @brief 申请互斥量，用于保护临界区代码，与app_connected_mutex_post成对使用
 *
 * @param mutex:已创建的互斥量指针变量
 */
/* ----------------------------------------------------------------------------*/
static inline void app_connected_mutex_pend(OS_MUTEX *mutex, u32 line)
{
    int os_ret;
    os_ret = os_mutex_pend(mutex, 0);
    if (os_ret != OS_NO_ERR) {
        log_error("%s err, os_ret:0x%x", __FUNCTION__, os_ret);
        ASSERT(os_ret != OS_ERR_PEND_ISR, "line:%d err, os_ret:0x%x", line, os_ret);
    }
}

/* --------------------------------------------------------------------------*/
/**
 * @brief 释放互斥量，用于保护临界区代码，与app_connected_mutex_pend成对使用
 *
 * @param mutex:已创建的互斥量指针变量
 */
/* ----------------------------------------------------------------------------*/
static inline void app_connected_mutex_post(OS_MUTEX *mutex, u32 line)
{
    int os_ret;
    os_ret = os_mutex_post(mutex);
    if (os_ret != OS_NO_ERR) {
        log_error("%s err, os_ret:0x%x", __FUNCTION__, os_ret);
        ASSERT(os_ret != OS_ERR_PEND_ISR, "line:%d err, os_ret:0x%x", line, os_ret);
    }
}

/* --------------------------------------------------------------------------*/
/**
 * @brief ACL数据接口回调，用于记录远端设备标识，与hdl形成映射关系
 *
 * @param acl_hdl:对应特定远端设备的句柄
 * @param arg_num:参数个数
 * @param argv:首个参数地址
 */
/* ----------------------------------------------------------------------------*/
static void identification_record_handler(u16 acl_hdl, u8 arg_num, int *argv)
{
    u8 i, j;
    int trans_role = argv[0];
    u8 *data = (u8 *)argv[1];
    int len = argv[2];
    if (trans_role == CONNECTED_SYNC_CALL_RX) {
        for (i = 0; i < CIG_MAX_NUMS; i++) {
            for (j = 0; j < CIG_MAX_CIS_NUMS; j++) {
                if (app_cig_conn_info[i].cis_conn_info[j].acl_hdl == acl_hdl) {
                    memcpy(&app_cig_conn_info[i].cis_conn_info[j].remote_dev_identification, data, len);
                    /* app_connected_sync_volume(app_cig_conn_info[i].cis_conn_info[j].remote_dev_identification); */
                    /* g_printf("remote_dev_identification:0x%x", app_cig_conn_info[i].cis_conn_info[j].remote_dev_identification); */
                    break;
                }
            }
        }
    }
    //由于转发流程中申请了内存，因此执行完毕后必须释放
    if (data) {
        free(data);
    }
}

CONN_DATA_TRANS_STUB_REGISTER(identification_record) = {
    .uuid = CONNECTED_IDTF_SYNC_FUNC_ID,
    .task_name = "app_core",
    .func = identification_record_handler,
};

/* --------------------------------------------------------------------------*/
/**
 * @brief CIG连接成功后central发起音量同步
 */
/* ----------------------------------------------------------------------------*/
void app_connected_sync_volume(u8 remote_dev_identification)
{
    u8 data[2];
    data[0] = app_audio_get_volume(APP_AUDIO_STATE_MUSIC);
    data[1] = app_audio_get_volume(APP_AUDIO_STATE_CALL);
    /* g_printf("music_vol_sync: %d, call_vol_sync: %d\n", data[0], data[1]); */
    //同步音量
    connected_send_data_to_sibling(CONNECTED_VOLS_SYNC_FUNC_ID, data, sizeof(data), remote_dev_identification);
}

static void volume_sync_handler(u16 acl_hdl, u8 arg_num, int *argv)
{
    u8 i, j;
    int trans_role = argv[0];
    u8 *data = (u8 *)argv[1];
    int len = argv[2];
    if (trans_role == CONNECTED_SYNC_CALL_RX) {
        for (i = 0; i < CIG_MAX_NUMS; i++) {
            for (j = 0; j < CIG_MAX_CIS_NUMS; j++) {
                if (app_cig_conn_info[i].cis_conn_info[j].acl_hdl == acl_hdl) {
                    app_audio_set_volume(APP_AUDIO_STATE_MUSIC, data[0], 1);
                    app_audio_set_volume(APP_AUDIO_STATE_CALL, data[1], 1);
                    /* r_printf("music_vol_sync: %d, call_vol_sync: %d\n", data[0], data[1]); */
                    break;
                }
            }
        }
    }
    //由于转发流程中申请了内存，因此执行完毕后必须释放
    if (data) {
        free(data);
    }
}

CONN_DATA_TRANS_STUB_REGISTER(volume_sync) = {
    .uuid = CONNECTED_VOLS_SYNC_FUNC_ID,
    .task_name = "app_core",
    .func = volume_sync_handler,
};

/* --------------------------------------------------------------------------*/
/**
 * @brief CIG开关提示音结束回调接口
 *
 * @param priv:传递的参数
 * @param flag:结束方式 -- 0正常关闭，1被打断关闭
 */
/* ----------------------------------------------------------------------------*/
static void  connected_tone_play_end_callback(void *priv, int flag)
{
    u32 index = (u32)priv;
    int temp_connected_hdl;

    switch (index) {
    case IDEX_TONE_CONNECTED_OPEN:
        break;
    case IDEX_TONE_CONNECTED_CLOSE:
        break;
    default:
        break;
    }
}

/* --------------------------------------------------------------------------*/
/**
 * @brief CIG连接状态事件处理函数
 *
 * @param wireless_trans:连接事件附带的返回参数，参数类型是cig_hdl_t
 */
/* ----------------------------------------------------------------------------*/
void app_connected_conn_status_event_handler(struct wireless_trans_event *wireless_trans)
{
    u8 i, j, mode;
    u8 find = 0;
    u16 crc16;
    static u8 local_dev_identification = 0;
    static u8 remote_dev_identification = 0;
    int ret = 0;
    u64 pair_addr0 = 0;
    u64 pair_addr1 = 0;
    cig_hdl_t *hdl;
    cis_acl_info_t *acl_info;
    struct connected_sync_channel *p;

    if (!app_get_connected_role()) {
        return;
    }

    switch (wireless_trans->event) {
    case CIG_EVENT_CENTRAL_CONNECT:
        g_printf("CIG_EVENT_CENTRAL_CONNECT");
        //由于是异步操作需要加互斥量保护，避免connected_close的代码与其同时运行,添加的流程请放在互斥量保护区里面
        app_connected_mutex_pend(&mutex, __LINE__);

        hdl = (cig_hdl_t *)wireless_trans->value;
        crc16 = CRC16(hdl, sizeof(cig_hdl_t));

        //记录设备的cig_hdl等信息
        for (i = 0; i < CIG_MAX_NUMS; i++) {
            if (app_cig_conn_info[i].used && (app_cig_conn_info[i].cig_hdl == hdl->cig_hdl)) {
                app_cig_conn_info[i].local_dev_identification = CONNECTED_DEVICE_IDENTIFICATION_M;
                for (j = 0; j < CIG_MAX_CIS_NUMS; j++) {
                    if (!app_cig_conn_info[i].cis_conn_info[j].cis_hdl) {
                        app_cig_conn_info[i].cis_conn_info[j].cis_hdl = hdl->cis_hdl;
                        app_cig_conn_info[i].cis_conn_info[j].acl_hdl = hdl->acl_hdl;
                        app_cig_conn_info[i].cis_conn_info[j].remote_dev_identification = CONNECTED_DEVICE_IDENTIFICATION_L;
                        app_cig_conn_info[i].cis_conn_info[j].cis_status = APP_CONNECTED_STATUS_CONNECT;
                        find = 1;
                        break;
                    }
                }
            }
        }

        if (!find || (crc16 != wireless_trans->crc16)) {
            //释放互斥量
            app_connected_mutex_post(&mutex, __LINE__);
            break;
        }

        cis_connected_nums++;
        ASSERT(cis_connected_nums <= CIG_MAX_CIS_NUMS && cis_connected_nums >= 0, "cis_connected_nums:%d", cis_connected_nums);

        //有设备连接上时删掉连接超时处理定时
        if (cig_connection_timeout) {
            sys_timeout_del(cig_connection_timeout);
            cig_connection_timeout = 0;
        }

        list_for_each_cig_sync_channel(p) {
            if (p->event_handler) {
                p->event_handler(wireless_trans->event, (void *)wireless_trans->value);
            }
        }

        mode = app_get_curr_task();
        for (i = 0; i < (sizeof(audio_mode_map) / sizeof(audio_mode_map[0])); i++) {
            if (mode == audio_mode_map[i][0]) {
                mode = audio_mode_map[i][1];
                break;
            }
        }
        ret = connected_central_connect_deal((void *)wireless_trans->value, wireless_trans->crc16, mode);
        if (ret < 0) {
            r_printf("connected_central_connect_deal fail");
        }

        hdl->cis_hdl = 0;
        //释放互斥量
        app_connected_mutex_post(&mutex, __LINE__);
        break;

    case CIG_EVENT_CENTRAL_DISCONNECT:
        g_printf("CIG_EVENT_CENTRAL_DISCONNECT");
        //由于是异步操作需要加互斥量保护，避免connected_close的代码与其同时运行,添加的流程请放在互斥量保护区里面
        app_connected_mutex_pend(&mutex, __LINE__);

        list_for_each_cig_sync_channel(p) {
            if (p->event_handler) {
                p->event_handler(wireless_trans->event, (void *)wireless_trans->value);
            }
        }

        hdl = (cig_hdl_t *)wireless_trans->value;
        for (i = 0; i < CIG_MAX_NUMS; i++) {
            if (app_cig_conn_info[i].used && (app_cig_conn_info[i].cig_hdl == hdl->cig_hdl)) {
                for (j = 0; j < CIG_MAX_CIS_NUMS; j++) {
                    if (app_cig_conn_info[i].cis_conn_info[j].cis_hdl == hdl->cis_hdl) {
                        app_cig_conn_info[i].cis_conn_info[j].cis_hdl = 0;
                        app_cig_conn_info[i].cis_conn_info[j].acl_hdl = 0;
                        app_cig_conn_info[i].cis_conn_info[j].remote_dev_identification = 0;
                        app_cig_conn_info[i].cis_conn_info[j].cis_status = APP_CONNECTED_STATUS_DISCONNECT;
                        break;
                    }
                }
                find = 1;
            }
        }

        if (!find) {
            //释放互斥量
            app_connected_mutex_post(&mutex, __LINE__);
            break;
        }

        cis_connected_nums--;
        ASSERT(cis_connected_nums <= CIG_MAX_CIS_NUMS && cis_connected_nums >= 0, "cis_connected_nums:%d", cis_connected_nums);

        ret = connected_central_disconnect_deal((void *)wireless_trans->value);
        if (ret < 0) {
            r_printf("connected_central_disconnect_deal fail");
        }

#if CIG_CONNECTED_TIMEOUT
        if (!cis_connected_nums && !cig_connection_timeout) {
            y_printf("app_cig_connection_timeout add:%d", __LINE__);
            cig_connection_timeout = sys_timeout_add(NULL, app_cig_connection_timeout, CIG_CONNECTED_TIMEOUT);
        }
#endif
        hdl->cis_hdl = 0;
        //释放互斥量
        app_connected_mutex_post(&mutex, __LINE__);
        break;

    case CIG_EVENT_PERIP_CONNECT:
        g_printf("CIG_EVENT_PERIP_CONNECT");
        //由于是异步操作需要加互斥量保护，避免connected_close的代码与其同时运行,添加的流程请放在互斥量保护区里面
        app_connected_mutex_pend(&mutex, __LINE__);

        hdl = (cig_hdl_t *)wireless_trans->value;
        crc16 = CRC16(hdl, sizeof(cig_hdl_t));

        //记录设备的cig_hdl等信息
        for (i = 0; i < CIG_MAX_NUMS; i++) {
            if (app_cig_conn_info[i].used && (app_cig_conn_info[i].cig_hdl == 0xFF)) {
                app_cig_conn_info[i].cig_hdl = hdl->cig_hdl;
                app_cig_conn_info[i].local_dev_identification = CONNECTED_DEVICE_IDENTIFICATION_L;
                local_dev_identification = app_cig_conn_info[i].local_dev_identification;
                for (j = 0; j < CIG_MAX_CIS_NUMS; j++) {
                    if (!app_cig_conn_info[i].cis_conn_info[j].cis_hdl) {
                        app_cig_conn_info[i].cis_conn_info[j].cis_hdl = hdl->cis_hdl;
                        app_cig_conn_info[i].cis_conn_info[j].acl_hdl = hdl->acl_hdl;
                        app_cig_conn_info[i].cis_conn_info[j].remote_dev_identification = CONNECTED_DEVICE_IDENTIFICATION_M;
                        app_cig_conn_info[i].cis_conn_info[j].cis_status = APP_CONNECTED_STATUS_CONNECT;
                        remote_dev_identification = app_cig_conn_info[i].cis_conn_info[j].remote_dev_identification;
                        find = 1;
                        break;
                    }
                }
            }
        }

        if (!find || (crc16 != wireless_trans->crc16)) {
            //释放互斥量
            app_connected_mutex_post(&mutex, __LINE__);
            break;
        }

        cis_connected_nums++;
        ASSERT(cis_connected_nums <= CIG_MAX_CIS_NUMS && cis_connected_nums >= 0, "cis_connected_nums:%d", cis_connected_nums);

        //有设备连接上时删掉连接超时处理定时
        if (cig_connection_timeout) {
            sys_timeout_del(cig_connection_timeout);
            cig_connection_timeout = 0;
        }

        list_for_each_cig_sync_channel(p) {
            if (p->event_handler) {
                p->event_handler(wireless_trans->event, (void *)wireless_trans->value);
            }
        }

        mode = app_get_curr_task();
        for (i = 0; i < (sizeof(audio_mode_map) / sizeof(audio_mode_map[0])); i++) {
            if (mode == audio_mode_map[i][0]) {
                mode = audio_mode_map[i][1];
                break;
            }
        }
        ret = connected_perip_connect_deal((void *)wireless_trans->value, wireless_trans->crc16, mode);
        if (ret < 0) {
            r_printf("connected_perip_connect_deal fail");
        } else {
            //告知主机本机的设备标识
            connected_send_data_to_sibling(CONNECTED_IDTF_SYNC_FUNC_ID, &local_dev_identification, sizeof(local_dev_identification), remote_dev_identification);
        }

        //释放互斥量
        app_connected_mutex_post(&mutex, __LINE__);
        break;

    case CIG_EVENT_PERIP_DISCONNECT:
        g_printf("CIG_EVENT_PERIP_DISCONNECT");
        //由于是异步操作需要加互斥量保护，避免connected_close的代码与其同时运行,添加的流程请放在互斥量保护区里面
        app_connected_mutex_pend(&mutex, __LINE__);

        list_for_each_cig_sync_channel(p) {
            if (p->event_handler) {
                p->event_handler(wireless_trans->event, (void *)wireless_trans->value);
            }
        }

        hdl = (cig_hdl_t *)wireless_trans->value;
        for (i = 0; i < CIG_MAX_NUMS; i++) {
            if (app_cig_conn_info[i].used && (app_cig_conn_info[i].cig_hdl == hdl->cig_hdl)) {
                for (j = 0; j < CIG_MAX_CIS_NUMS; j++) {
                    if (app_cig_conn_info[i].cis_conn_info[j].cis_hdl == hdl->cis_hdl) {
                        app_cig_conn_info[i].cis_conn_info[j].cis_hdl = 0;
                        app_cig_conn_info[i].cis_conn_info[j].acl_hdl = 0;
                        app_cig_conn_info[i].cis_conn_info[j].remote_dev_identification = 0;
                        app_cig_conn_info[i].cis_conn_info[j].cis_status = APP_CONNECTED_STATUS_DISCONNECT;
                        app_cig_conn_info[i].cig_hdl = 0xFF;
                        break;
                    }
                }
                find = 1;
            }
        }

        if (!find) {
            //释放互斥量
            app_connected_mutex_post(&mutex, __LINE__);
            break;
        }

        cis_connected_nums--;
        ASSERT(cis_connected_nums <= CIG_MAX_CIS_NUMS && cis_connected_nums >= 0, "cis_connected_nums:%d", cis_connected_nums);


        ret = connected_perip_disconnect_deal((void *)wireless_trans->value);
        if (ret < 0) {
            r_printf("connected_perip_disconnect_deal fail");
        }

#if CIG_CONNECTED_TIMEOUT
        if (!cis_connected_nums && !cig_connection_timeout) {
            y_printf("app_cig_connection_timeout add:%d", __LINE__);
            cig_connection_timeout = sys_timeout_add(NULL, app_cig_connection_timeout, CIG_CONNECTED_TIMEOUT);
        }
#endif
        //释放互斥量
        app_connected_mutex_post(&mutex, __LINE__);
        break;

    case CIG_EVENT_ACL_CONNECT:
        g_printf("CIG_EVENT_ACL_CONNECT");
        acl_info = (cis_acl_info_t *)wireless_trans->value;
        if (acl_info->conn_type) {
            log_info("connect test box ble");
            return;
        }
#if CIG_CONNECTED_TIMEOUT
        if (cig_connection_timeout) {
            sys_timeout_del(cig_connection_timeout);
            cig_connection_timeout = 0;
        }
#endif
        //由于是异步操作需要加互斥量保护，避免connected_close的代码与其同时运行,添加的流程请放在互斥量保护区里面
        app_connected_mutex_pend(&mutex, __LINE__);

        g_printf("remote device addr:");
        put_buf(&acl_info->pri_ch, sizeof(acl_info->pri_ch));
        crc16 = CRC16(acl_info, sizeof(cis_acl_info_t));
        if (crc16 != wireless_trans->crc16) {
            r_printf("CIG_EVENT_ACL_CONNECT crc check fail");
            //释放互斥量
            app_connected_mutex_post(&mutex, __LINE__);
            break;
        }

        acl_connected_nums++;
        ASSERT(acl_connected_nums <= CIG_MAX_CIS_NUMS && acl_connected_nums >= 0, "acl_connected_nums:%d", acl_connected_nums);

        syscfg_read(VM_WIRELESS_PAIR_CODE0, &pair_addr0, sizeof(pair_addr0));
        syscfg_read(VM_WIRELESS_PAIR_CODE1, &pair_addr1, sizeof(pair_addr1));
        if ((acl_info->pri_ch != pair_addr0) && (acl_info->pri_ch != pair_addr1)) {
            //记录远端设备地址，用于下次连接时过滤设备
            if (acl_connected_nums == 1) {
                ret = syscfg_write(VM_WIRELESS_PAIR_CODE0, &acl_info->pri_ch, sizeof(acl_info->pri_ch));
            } else if (acl_connected_nums == 2) {
                ret = syscfg_write(VM_WIRELESS_PAIR_CODE1, &acl_info->pri_ch, sizeof(acl_info->pri_ch));
            }
            if (ret <= 0) {
                r_printf(">>>>>>wireless pair code save err:%d, ret:%d", acl_connected_nums, ret);
            }
        }

        acl_info->acl_hdl = 0;
        //释放互斥量
        app_connected_mutex_post(&mutex, __LINE__);
        break;

    case CIG_EVENT_ACL_DISCONNECT:
        g_printf("CIG_EVENT_ACL_DISCONNECT");
        acl_info = (cis_acl_info_t *)wireless_trans->value;
        if (acl_info->conn_type) {
            log_info("disconnect test box ble");
            return;
        }

        //由于是异步操作需要加互斥量保护，避免connected_close的代码与其同时运行,添加的流程请放在互斥量保护区里面
        app_connected_mutex_pend(&mutex, __LINE__);

        crc16 = CRC16(acl_info, sizeof(cis_acl_info_t));
        if (crc16 != wireless_trans->crc16) {
            r_printf("CIG_EVENT_ACL_DISCONNECT crc check fail");
            //释放互斥量
            app_connected_mutex_post(&mutex, __LINE__);
            break;
        }

        acl_connected_nums--;
        ASSERT(acl_connected_nums <= CIG_MAX_CIS_NUMS && acl_connected_nums >= 0, "acl_connected_nums:%d", acl_connected_nums);

        acl_info->acl_hdl = 0;
        //释放互斥量
        app_connected_mutex_post(&mutex, __LINE__);
        break;

    default:
        break;
    }
}

/* --------------------------------------------------------------------------*/
/**
 * @brief 获取当前是否在退出模式的状态
 *
 * @return 1；是，0：否
 */
/* ----------------------------------------------------------------------------*/
u8 get_connected_app_mode_exit_flag(void)
{
    return connected_app_mode_exit;
}

/* --------------------------------------------------------------------------*/
/**
 * @brief 判断当前设备作为CIG发送设备还是接收设备
 *
 * @return true:发送设备，false:接收设备
 */
/* ----------------------------------------------------------------------------*/
static bool is_connected_as_transmitter()
{
    if ((connected_transport_mode == CIG_MODE_CToP) || (connected_transport_mode == CIG_MODE_DUPLEX)) {
        if (connected_role_config == CONNECTED_ROLE_CENTRAL) {
            return true;
        } else if (connected_role_config == CONNECTED_ROLE_PERIP) {
            return false;
        }
    } else if (connected_transport_mode == CIG_MODE_PToC) {
        if (connected_role_config == CONNECTED_ROLE_CENTRAL) {
            return false;
        } else if (connected_role_config == CONNECTED_ROLE_PERIP) {
            return true;
        }
    }

    gpio_set_direction(IO_PORTC_04, 1);
    gpio_set_pull_down(IO_PORTC_04, 0);
    gpio_set_pull_up(IO_PORTC_04, 1);
    gpio_set_die(IO_PORTC_04, 1);
    os_time_dly(2);
    if (gpio_read(IO_PORTC_04) == 0) {
        return true;
    } else {
        return false;
    }
}

/* --------------------------------------------------------------------------*/
/**
 * @brief 检测当前是否处于挂起状态
 *
 * @return true:处于挂起状态，false:处于非挂起状态
 */
/* ----------------------------------------------------------------------------*/
static bool is_need_resume_connected()
{
    u8 i;
    u8 find = 0;
    for (i = 0; i < CIG_MAX_NUMS; i++) {
        if (app_cig_conn_info[i].cig_status == APP_CONNECTED_STATUS_SUSPEND) {
            find = 1;
            break;
        }
    }
    if (find) {
        return true;
    } else {
        return false;
    }
}

/* --------------------------------------------------------------------------*/
/**
 * @brief CIG从挂起状态恢复
 */
/* ----------------------------------------------------------------------------*/
static void app_connected_resume()
{
    cig_hdl_t temp_connected_hdl;

    if (!app_bt_hdl.init_ok) {
        return;
    }

    if (!is_need_resume_connected()) {
        return;
    }

    app_connected_open(0);
}

/* --------------------------------------------------------------------------*/
/**
 * @brief CIG进入挂起状态
 */
/* ----------------------------------------------------------------------------*/
static void app_connected_suspend()
{
    u8 i;
    u8 find = 0;
    for (i = 0; i < CIG_MAX_NUMS; i++) {
        if (app_cig_conn_info[i].used) {
            find = 1;
            break;
        }
    }
    if (find) {
        connected_last_role = app_get_connected_role();
        app_connected_close_all(APP_CONNECTED_STATUS_SUSPEND);
    }
}

/* --------------------------------------------------------------------------*/
/**
 * @brief 开启CIG
 *
 * @param pair_without_addr:不匹配配对地址，重新自由配对
 */
/* ----------------------------------------------------------------------------*/
void app_connected_open(u8 pair_without_addr)
{
    u8 i;
    u8 role = 0;
    u8 cig_available_num = 0;
    cig_hdl_t temp_connected_hdl;
    cig_parameter_t *params;
    int ret;
    u64 pair_addr;

    if (!app_bt_hdl.init_ok) {
        return;
    }

    for (i = 0; i < CIG_MAX_NUMS; i++) {
        if (!app_cig_conn_info[i].used) {
            cig_available_num++;
        }
    }

    if (!cig_available_num) {
        return;
    }

    if ((app_get_curr_task() == APP_BT_TASK) &&
        (get_call_status() != BT_CALL_HANGUP)) {
        return;
    }

    if ((app_get_curr_task() == APP_FM_TASK) ||
        (app_get_curr_task() == APP_PC_TASK)) {
        return;
    }

    log_info("connected_open");
#if defined(RCSP_MODE) && RCSP_MODE
#if RCSP_BLE_MASTER
    extern void setRcspConnectBleAddr(u8 * addr);
    setRcspConnectBleAddr(NULL);
#endif
    setLeAudioModeMode(JL_LeAudioModeCig);
    ble_module_enable(0);
#endif
    if (is_connected_as_transmitter()) {
        if ((connected_transport_mode == CIG_MODE_CToP) || (connected_transport_mode == CIG_MODE_DUPLEX)) {
            //初始化cig发送端参数
            params = set_cig_params(app_get_curr_task(), CONNECTED_ROLE_CENTRAL, pair_without_addr);
            //打开big，打开成功后会在函数app_connected_conn_status_event_handler做后续处理
            temp_connected_hdl.cig_hdl = connected_central_open(params);
            //记录角色
            role = CONNECTED_ROLE_CENTRAL;
        } else if (connected_transport_mode == CIG_MODE_PToC) {
            //初始化cig发送端参数
            params = set_cig_params(app_get_curr_task(), CONNECTED_ROLE_PERIP, pair_without_addr);
            //打开big，打开成功后会在函数app_connected_conn_status_event_handler做后续处理
            temp_connected_hdl.cig_hdl = connected_perip_open(params);
        }

#if TRANSMITTER_AUTO_TEST_EN
        extern void wireless_trans_auto_test3_init(void);
        extern void wireless_trans_auto_test4_init(void);
        //不定时切换模式
        wireless_trans_auto_test3_init();
        //不定时暂停播放
        wireless_trans_auto_test4_init();
#endif
    } else {
        if ((connected_transport_mode == CIG_MODE_CToP) || (connected_transport_mode == CIG_MODE_DUPLEX)) {
            //初始化cig接收端参数
            params = set_cig_params(app_get_curr_task(), CONNECTED_ROLE_PERIP, pair_without_addr);
            //打开big，打开成功后会在函数app_connected_conn_status_event_handler做后续处理
            temp_connected_hdl.cig_hdl = connected_perip_open(params);
        } else if (connected_transport_mode == CIG_MODE_PToC) {
            //初始化cig发送端参数
            params = set_cig_params(app_get_curr_task(), CONNECTED_ROLE_CENTRAL, pair_without_addr);
            //打开big，打开成功后会在函数app_connected_conn_status_event_handler做后续处理
            temp_connected_hdl.cig_hdl = connected_central_open(params);
            //记录角色
            role = CONNECTED_ROLE_CENTRAL;
        }

#if RECEIVER_AUTO_TEST_EN
        extern void wireless_trans_auto_test3_init(void);
        extern void wireless_trans_auto_test4_init(void);
        //不定时切换模式
        wireless_trans_auto_test3_init();
        //不定时暂停播放
        wireless_trans_auto_test4_init();
#endif
    }
    if (temp_connected_hdl.cig_hdl >= 0) {
        //只有按地址配对时才需要注册超时任务
        if (!pair_without_addr) {
            //读取配对地址，只要有一个配对地址存在就注册连接超时处理函数
            ret = syscfg_read(VM_WIRELESS_PAIR_CODE0, &pair_addr, sizeof(pair_addr));
            if (ret <= 0) {
                if (role == CONNECTED_ROLE_CENTRAL) {
                    ret = syscfg_read(VM_WIRELESS_PAIR_CODE1, &pair_addr, sizeof(pair_addr));
                }
            }
            if (ret > 0) {
#if CIG_CONNECTED_TIMEOUT
                if (!cig_connection_timeout) {
                    y_printf("app_cig_connection_timeout add:%d", __LINE__);
                    cig_connection_timeout = sys_timeout_add(NULL, app_cig_connection_timeout, CIG_CONNECTED_TIMEOUT);
                } else {
                    sys_timeout_del(cig_connection_timeout);
                    cig_connection_timeout = 0;
                }
#endif
            }
        }
        for (i = 0; i < CIG_MAX_NUMS; i++) {
            if (!app_cig_conn_info[i].used) {
                app_cig_conn_info[i].cig_hdl = temp_connected_hdl.cig_hdl;
                app_cig_conn_info[i].cig_status = APP_CONNECTED_STATUS_START;
                app_cig_conn_info[i].used = 1;
                break;
            }
        }
    }
}

/* --------------------------------------------------------------------------*/
/**
 * @brief 关闭对应cig_hdl的CIG连接
 *
 * @param cig_hdl:需要关闭的CIG连接对应的hdl
 * @param status:关闭后CIG进入的suspend还是stop状态
 */
/* ----------------------------------------------------------------------------*/
void app_connected_close(u8 cig_hdl, u8 status)
{
    u8 i;
    u8 find = 0;
    log_info("connected_close");
    //由于是异步操作需要加互斥量保护，避免和开启的流程同时运行,添加的流程请放在互斥量保护区里面
    app_connected_mutex_pend(&mutex, __LINE__);

    for (i = 0; i < CIG_MAX_NUMS; i++) {
        if (app_cig_conn_info[i].used && (app_cig_conn_info[i].cig_hdl == cig_hdl)) {
            memset(&app_cig_conn_info[i], 0, sizeof(struct app_cig_conn_info));
            app_cig_conn_info[i].cig_status = status;
            find = 1;
            break;
        }
    }

    if (find) {
        connected_close(cig_hdl);
        acl_connected_nums -= CIG_MAX_CIS_NUMS;
        ASSERT(acl_connected_nums <= CIG_MAX_CIS_NUMS && acl_connected_nums >= 0, "acl_connected_nums:%d", acl_connected_nums);
        cis_connected_nums -= CIG_MAX_CIS_NUMS;
        ASSERT(cis_connected_nums <= CIG_MAX_CIS_NUMS && cis_connected_nums >= 0, "cis_connected_nums:%d", cis_connected_nums);
        if (cig_connection_timeout) {
            sys_timeout_del(cig_connection_timeout);
            cig_connection_timeout = 0;
        }
    }

    //释放互斥量
    app_connected_mutex_post(&mutex, __LINE__);

#if defined(RCSP_MODE) && RCSP_MODE
    if (status != APP_CONNECTED_STATUS_SUSPEND) {
        setLeAudioModeMode(JL_LeAudioModeNone);
        ble_module_enable(1);
    }
#endif
}

bool get_connected_on_off(void)
{
    bool find = false;
    for (int i = 0; i < CIG_MAX_NUMS; i++) {
        if (app_cig_conn_info[i].used) {
            find = true;
            break;
        }
    }
    return find;

}

/* --------------------------------------------------------------------------*/
/**
 * @brief 关闭所有cig_hdl的CIG连接
 *
 * @param status:关闭后CIG进入的suspend还是stop状态
 */
/* ----------------------------------------------------------------------------*/
void app_connected_close_all(u8 status)
{
    u8 i;
    log_info("connected_close");
    //由于是异步操作需要加互斥量保护，避免和开启的流程同时运行,添加的流程请放在互斥量保护区里面
    app_connected_mutex_pend(&mutex, __LINE__);

    for (i = 0; i < CIG_MAX_NUMS; i++) {
        if (app_cig_conn_info[i].used && app_cig_conn_info[i].cig_hdl) {
            connected_close(app_cig_conn_info[i].cig_hdl);
            memset(&app_cig_conn_info[i], 0, sizeof(struct app_cig_conn_info));
            app_cig_conn_info[i].cig_status = status;
        }
    }

    acl_connected_nums = 0;
    cis_connected_nums = 0;

    if (cig_connection_timeout) {
        sys_timeout_del(cig_connection_timeout);
        cig_connection_timeout = 0;
    }

    //释放互斥量
    app_connected_mutex_post(&mutex, __LINE__);

#if defined(RCSP_MODE) && RCSP_MODE
    if (status != APP_CONNECTED_STATUS_SUSPEND) {
        setLeAudioModeMode(JL_LeAudioModeNone);
        ble_module_enable(1);
    }
#endif
}

/* --------------------------------------------------------------------------*/
/**
 * @brief CIG开关切换
 *
 * @return -1:切换失败，0:切换成功
 */
/* ----------------------------------------------------------------------------*/
int app_connected_switch(void)
{
    u8 i;
    u8 find = 0;
    cig_hdl_t temp_connected_hdl;

    if (!app_bt_hdl.init_ok) {
        return -1;
    }

    if ((app_get_curr_task() == APP_BT_TASK) &&
        (get_call_status() != BT_CALL_HANGUP)) {
        return -1;
    }

    for (i = 0; i < CIG_MAX_NUMS; i++) {
        if (app_cig_conn_info[i].used) {
            find = 1;
            break;
        }
    }

    if (find) {
        tone_play_with_callback_by_name(tone_table[IDEX_TONE_CONNECTED_CLOSE], 1, connected_tone_play_end_callback, (void *)IDEX_TONE_CONNECTED_CLOSE);
        app_connected_close_all(APP_CONNECTED_STATUS_STOP);
    } else {
        tone_play_with_callback_by_name(tone_table[IDEX_TONE_CONNECTED_OPEN], 1, connected_tone_play_end_callback, (void *)IDEX_TONE_CONNECTED_OPEN);
        app_connected_open(0);
    }
    return 0;
}


void app_connected_open_with_role(u8 role_in, u8 pair_without_addr)
{
    u8 i;
    u8 role = 0;
    u8 cig_available_num = 0;
    cig_hdl_t temp_connected_hdl;
    cig_parameter_t *params;
    int ret;
    u64 pair_addr;

    if (!app_bt_hdl.init_ok) {
        return;
    }

    for (i = 0; i < CIG_MAX_NUMS; i++) {
        if (!app_cig_conn_info[i].used) {
            cig_available_num++;
        }
    }

    if (!cig_available_num) {
        return;
    }

    if ((app_get_curr_task() == APP_BT_TASK) &&
        (get_call_status() != BT_CALL_HANGUP)) {
        return;
    }

    if ((app_get_curr_task() == APP_FM_TASK) ||
        (app_get_curr_task() == APP_PC_TASK)) {
        return;
    }

    log_info("connected_open");
#if defined(RCSP_MODE) && RCSP_MODE
#if RCSP_BLE_MASTER
    extern void setRcspConnectBleAddr(u8 * addr);
    setRcspConnectBleAddr(NULL);
#endif
    setLeAudioModeMode(JL_LeAudioModeCig);
    ble_module_enable(0);
#endif
    if ((role_in & CONNECTED_ROLE_PERIP) == CONNECTED_ROLE_PERIP) {
        if ((connected_transport_mode == CIG_MODE_CToP) || (connected_transport_mode == CIG_MODE_DUPLEX)) {
            //初始化cig发送端参数
            params = set_cig_params(app_get_curr_task(), CONNECTED_ROLE_CENTRAL, pair_without_addr);
            //打开big，打开成功后会在函数app_connected_conn_status_event_handler做后续处理
            temp_connected_hdl.cig_hdl = connected_central_open(params);
            //记录角色
            role = CONNECTED_ROLE_CENTRAL;
        } else if (connected_transport_mode == CIG_MODE_PToC) {
            //初始化cig发送端参数
            params = set_cig_params(app_get_curr_task(), CONNECTED_ROLE_PERIP, pair_without_addr);
            //打开big，打开成功后会在函数app_connected_conn_status_event_handler做后续处理
            temp_connected_hdl.cig_hdl = connected_perip_open(params);
        }

#if TRANSMITTER_AUTO_TEST_EN
        extern void wireless_trans_auto_test3_init(void);
        extern void wireless_trans_auto_test4_init(void);
        //不定时切换模式
        wireless_trans_auto_test3_init();
        //不定时暂停播放
        wireless_trans_auto_test4_init();
#endif
    } else if ((role_in & CONNECTED_ROLE_CENTRAL) == CONNECTED_ROLE_CENTRAL) {
        if ((connected_transport_mode == CIG_MODE_CToP) || (connected_transport_mode == CIG_MODE_DUPLEX)) {
            //初始化cig接收端参数
            params = set_cig_params(app_get_curr_task(), CONNECTED_ROLE_PERIP, pair_without_addr);
            //打开big，打开成功后会在函数app_connected_conn_status_event_handler做后续处理
            temp_connected_hdl.cig_hdl = connected_perip_open(params);
        } else if (connected_transport_mode == CIG_MODE_PToC) {
            //初始化cig发送端参数
            params = set_cig_params(app_get_curr_task(), CONNECTED_ROLE_CENTRAL, pair_without_addr);
            //打开big，打开成功后会在函数app_connected_conn_status_event_handler做后续处理
            temp_connected_hdl.cig_hdl = connected_central_open(params);
            //记录角色
            role = CONNECTED_ROLE_CENTRAL;
        }

#if RECEIVER_AUTO_TEST_EN
        extern void wireless_trans_auto_test3_init(void);
        extern void wireless_trans_auto_test4_init(void);
        //不定时切换模式
        wireless_trans_auto_test3_init();
        //不定时暂停播放
        wireless_trans_auto_test4_init();
#endif
    }
    if (temp_connected_hdl.cig_hdl >= 0) {
        //只有按地址配对时才需要注册超时任务
        if (!pair_without_addr) {
            //读取配对地址，只要有一个配对地址存在就注册连接超时处理函数
            ret = syscfg_read(VM_WIRELESS_PAIR_CODE0, &pair_addr, sizeof(pair_addr));
            if (ret <= 0) {
                if (role == CONNECTED_ROLE_CENTRAL) {
                    ret = syscfg_read(VM_WIRELESS_PAIR_CODE1, &pair_addr, sizeof(pair_addr));
                }
            }
            if (ret > 0) {
#if CIG_CONNECTED_TIMEOUT
                if (!cig_connection_timeout) {
                    y_printf("app_cig_connection_timeout add:%d", __LINE__);
                    cig_connection_timeout = sys_timeout_add(NULL, app_cig_connection_timeout, CIG_CONNECTED_TIMEOUT);
                } else {
                    sys_timeout_del(cig_connection_timeout);
                    cig_connection_timeout = 0;
                }
#endif
            }
        }
        for (i = 0; i < CIG_MAX_NUMS; i++) {
            if (!app_cig_conn_info[i].used) {
                app_cig_conn_info[i].cig_hdl = temp_connected_hdl.cig_hdl;
                app_cig_conn_info[i].cig_status = APP_CONNECTED_STATUS_START;
                app_cig_conn_info[i].used = 1;
                break;
            }
        }
    }
}

int app_connected_role_switch(void)
{
    u8 i;
    u8 find = 0;
    cig_hdl_t temp_connected_hdl;

    if (!app_bt_hdl.init_ok) {
        return -1;
    }

    if ((app_get_curr_task() == APP_BT_TASK) &&
        (get_call_status() != BT_CALL_HANGUP)) {
        return -1;
    }

    for (i = 0; i < CIG_MAX_NUMS; i++) {
        if (app_cig_conn_info[i].used) {
            find = 1;
            break;
        }
    }
    u8 role = get_connected_role();
    printf("===============curr role %d, ready switch============", role);
    if (find) {
        tone_play_with_callback_by_name(tone_table[IDEX_TONE_CONNECTED_CLOSE], 1, connected_tone_play_end_callback, (void *)IDEX_TONE_CONNECTED_CLOSE);
        app_connected_close_all(APP_CONNECTED_STATUS_STOP);
    }
    if (((role & CONNECTED_ROLE_CENTRAL) == CONNECTED_ROLE_CENTRAL) || (role & CONNECTED_ROLE_PERIP) == CONNECTED_ROLE_PERIP) {
        tone_play_with_callback_by_name(tone_table[IDEX_TONE_CONNECTED_OPEN], 1, connected_tone_play_end_callback, (void *)IDEX_TONE_CONNECTED_OPEN);
        app_connected_open_with_role(role, 0);
    }
    return 0;
}


/* --------------------------------------------------------------------------*/
/**
 * @brief CIG开启情况下，不同场景的处理流程
 *
 * @param switch_mode:当前系统状态
 *
 * @return -1:无需处理，0:处理事件但不拦截后续流程，1:处理事件并拦截后续流程
 */
/* ----------------------------------------------------------------------------*/
int app_connected_deal(int switch_mode)
{
    u8 i, j;
    u8 find = 0;
    int ret = -1;
    static int cur_mode = -1;
    static u8 phone_start_cnt = 0;

    if (!app_bt_hdl.init_ok) {
        return ret;
    }

    if ((cur_mode == switch_mode) &&
        (switch_mode != CONNECTED_PHONE_START) &&
        (switch_mode != CONNECTED_PHONE_STOP)) {
        log_error("app_connected_deal,cur_mode not be modified:%d", switch_mode);
        return ret;
    }

    cur_mode = switch_mode;

    switch (switch_mode) {
    case CONNECTED_APP_MODE_ENTER:
        log_info("CONNECTED_APP_MODE_ENTER");
        //进入当前模式
        connected_app_mode_exit = 0;
        config_connected_as_master = 1;
        ret = 0;
        if (app_get_curr_task() == APP_BT_TASK) {
            if (connected_last_role == APP_CONNECTED_ROLE_TRANSMITTER) {
                /* break; */
            }
        }
        if (is_need_resume_connected()) {
            app_connected_resume();
            ret = 1;
        }
        break;
    case CONNECTED_APP_MODE_EXIT:
        log_info("CONNECTED_APP_MODE_EXIT");
        //退出当前模式
        connected_app_mode_exit = 1;
        config_connected_as_master = 0;
        if (connected_2t1_duplex) {
            app_connected_close_all(APP_CONNECTED_STATUS_STOP);
        } else {
            app_connected_suspend();
        }
        ret = 0;
        break;
    case CONNECTED_MUSIC_START:
    case CONNECTED_A2DP_START:
        log_info("CONNECTED_MUSIC_START");
        ret = 0;
        connected_app_mode_exit = 0;
        config_connected_as_master = 1;
        if (app_get_connected_role() == APP_CONNECTED_ROLE_TRANSMITTER) {
            for (i = 0; i < CIG_MAX_NUMS; i++) {
                for (j = 0; j < CIG_MAX_CIS_NUMS; j++) {
                    cis_audio_capture_reset(app_cig_conn_info[i].cis_conn_info[j].cis_hdl);
                }
            }
            ret = 1;
        }
        break;
    case CONNECTED_MUSIC_STOP:
    case CONNECTED_A2DP_STOP:
        log_info("CONNECTED_MUSIC_STOP");
        ret = 0;
        config_connected_as_master = 0;
        if (app_get_connected_role() == APP_CONNECTED_ROLE_TRANSMITTER) {
            for (i = 0; i < CIG_MAX_NUMS; i++) {
                for (j = 0; j < CIG_MAX_CIS_NUMS; j++) {
                    cis_audio_capture_close(app_cig_conn_info[i].cis_conn_info[j].cis_hdl);
                }
            }
            ret = 1;
        }
        break;
    case CONNECTED_PHONE_START:
        log_info("CONNECTED_PHONE_START");
        //通话时，挂起
        phone_start_cnt++;
        app_connected_suspend();
        ret = 0;
        break;
    case CONNECTED_PHONE_STOP:
        log_info("CONNECTED_PHONE_STOP");
        //通话结束恢复
        ret = 0;
        phone_start_cnt--;
        printf("===phone_start_cnt:%d===\n", phone_start_cnt);
        if (phone_start_cnt) {
            log_info("phone_start_cnt:%d", phone_start_cnt);
            break;
        }
        //当前处于蓝牙模式并且挂起前作为发送设备，恢复的操作在播放a2dp处执行
        if (app_get_curr_task() == APP_BT_TASK) {
            if (connected_last_role == APP_CONNECTED_ROLE_TRANSMITTER) {
            }
        }
        //当前处于蓝牙模式并且挂起前，恢复并作为接收设备
        if (is_need_resume_connected()) {
            app_connected_resume();
        }
        break;
    case CONNECTED_EDR_DISCONN:
        log_info("CONNECTED_EDR_DISCONN");
        break;
        ret = 0;
        //当经典蓝牙断开后，作为发送端的设备挂起
        if ((app_get_connected_role() == APP_CONNECTED_ROLE_TRANSMITTER) ||
            (app_get_connected_role() == APP_CONNECTED_ROLE_DUPLEX)) {
            app_connected_suspend();
        }
        if (is_need_resume_connected()) {
            app_connected_resume();
        }
        break;
    default:
        log_error("%s invalid operation\n", __FUNCTION__);
        break;
    }

    return ret;
}

/* --------------------------------------------------------------------------*/
/**
 * @brief ACL数据发送接口
 *
 * @param device_channel:发送给远端设备的标识
 * @param data:需要发送的数据
 * @param length:数据长度
 *
 * @return slen:实际发送数据的总长度
 */
/* ----------------------------------------------------------------------------*/
int app_connected_send_acl_data(u8 device_channel, void *data, size_t length)
{
    u8 i, j;
    int slen = 0;
    struct connected_sync_channel *p;

    for (i = 0; i < CIG_MAX_NUMS; i++) {
        if (app_cig_conn_info[i].used) {
            for (j = 0; j < CIG_MAX_CIS_NUMS; j++) {
                if (app_cig_conn_info[i].cis_conn_info[j].remote_dev_identification & device_channel) {
                    slen += connected_send_acl_data(app_cig_conn_info[i].cis_conn_info[j].acl_hdl, data, length);
                }
            }
        }
    }

    if (slen) {
        list_for_each_cig_sync_channel(p) {
            if (p->tx_events_suss) {
                if (p->tx_events_suss(data, length)) {
                    break;
                }
            }
        }
    }

    return slen;
}

/* --------------------------------------------------------------------------*/
/**
 * @brief 判断对应设备标识的CIS当前是否处于连接状态
 *
 * @param device_channel:远端设备标识
 *
 * @return connected_status:连接状态，按bit判断
 */
/* ----------------------------------------------------------------------------*/
u8 app_cis_is_connected(u8 device_channel)
{
    u8 i, j;
    u8 connected_status = 0;
    for (i = 0; i < CIG_MAX_NUMS; i++) {
        if (app_cig_conn_info[i].used) {
            for (j = 0; j < CIG_MAX_CIS_NUMS; j++) {
                if (app_cig_conn_info[i].cis_conn_info[j].remote_dev_identification & device_channel) {
                    if (app_cig_conn_info[i].cis_conn_info[j].cis_status == APP_CONNECTED_STATUS_CONNECT) {
                        connected_status |= app_cig_conn_info[i].cis_conn_info[j].remote_dev_identification;
                    }
                }
            }
        }
    }
    return connected_status;
}

/* --------------------------------------------------------------------------*/
/**
 * @brief CIG资源初始化
 */
/* ----------------------------------------------------------------------------*/
void app_connected_init(void)
{
    int os_ret = os_mutex_create(&mutex);
    if (os_ret != OS_NO_ERR) {
        log_error("%s %d err, os_ret:0x%x", __FUNCTION__, __LINE__, os_ret);
        ASSERT(0);
    }
}

/* --------------------------------------------------------------------------*/
/**
 * @brief CIG资源卸载
 */
/* ----------------------------------------------------------------------------*/
void app_connected_uninit(void)
{
    int os_ret = os_mutex_del(&mutex, OS_DEL_NO_PEND);
    if (os_ret != OS_NO_ERR) {
        log_error("%s %d err, os_ret:0x%x", __FUNCTION__, __LINE__, os_ret);
    }
}

/* --------------------------------------------------------------------------*/
/**
 * @brief 非蓝牙后台情况下，在其他音源模式开启CIG，前提要先开蓝牙协议栈
 */
/* ----------------------------------------------------------------------------*/
void app_connected_open_in_other_mode()
{
    app_connected_init();
    app_connected_open(0);
}

/* --------------------------------------------------------------------------*/
/**
 * @brief
 * @brief 非蓝牙后台情况下，在其他音源模式关闭CIG
 */
/* ----------------------------------------------------------------------------*/
void app_connected_close_in_other_mode()
{
    app_connected_close_all(APP_CONNECTED_STATUS_STOP);
    app_connected_uninit();
}

/* --------------------------------------------------------------------------*/
/**
 * @brief CIG连接角色重定义
 *
 * @return 设备角色为发送设备还是接收设备
 */
/* ----------------------------------------------------------------------------*/
u8 app_get_connected_role(void)
{
    u8 bit7_value = 0;
    u8 role = APP_CONNECTED_ROLE_UNKNOW;
    u8 connected_role = get_connected_role();
    if (connected_role & BIT(7)) {
        bit7_value = 1;
        connected_role &= ~BIT(7);
    }
    if (connected_transport_mode == CIG_MODE_CToP) {
        if (connected_role == CONNECTED_ROLE_CENTRAL) {
            role = APP_CONNECTED_ROLE_TRANSMITTER;
        } else if (connected_role == CONNECTED_ROLE_PERIP) {
            role = APP_CONNECTED_ROLE_RECEIVER;
        }
    } else if (connected_transport_mode == CIG_MODE_PToC) {
        if (connected_role == CONNECTED_ROLE_CENTRAL) {
            role = APP_CONNECTED_ROLE_RECEIVER;
        } else if (connected_role == CONNECTED_ROLE_PERIP) {
            role = APP_CONNECTED_ROLE_TRANSMITTER;
        }
    } else if (connected_transport_mode == CIG_MODE_DUPLEX) {
        if ((connected_role == CONNECTED_ROLE_CENTRAL) ||
            (connected_role == CONNECTED_ROLE_PERIP)) {
            role = APP_CONNECTED_ROLE_DUPLEX;
        }
    }
    if (role && bit7_value) {
        //没有设备连接时，或上BIT(7)，防止外部流程判断错误
        role |= BIT(7);
    }
    return role;
}

/* --------------------------------------------------------------------------*/
/**
 * @brief 根据地址连接超时，重新进入自由配对
 *
 * @param priv
 */
/* ----------------------------------------------------------------------------*/
static void app_cig_connection_timeout(void *priv)
{
    app_connected_close_all(APP_CONNECTED_STATUS_STOP);
    app_connected_open(1);
}

/* --------------------------------------------------------------------------*/
/**
 * @brief 清除配对信息，并重新配对
 */
/* ----------------------------------------------------------------------------*/
void app_connected_remove_pairs_addr(void)
{
    u64 pair_addr = 0;
    u8 status = app_get_connected_role();
    if (status) {
        app_connected_close_all(APP_CONNECTED_STATUS_STOP);
    }
    syscfg_write(VM_WIRELESS_PAIR_CODE0, &pair_addr, sizeof(pair_addr));
    syscfg_write(VM_WIRELESS_PAIR_CODE1, &pair_addr, sizeof(pair_addr));
    if (status) {
        app_connected_open(1);
    }
}

#endif

