/*********************************************************************************************
    *   Filename        : app_broadcast.c

    *   Description     :

    *   Author          : Weixin Liang

    *   Email           : liangweixin@zh-jieli.com

    *   Last modifiled  : 2022-12-15 14:18

    *   Copyright:(c)JIELI  2011-2022  @ , All Rights Reserved.
*********************************************************************************************/
#ifdef SUPPORT_MS_EXTENSIONS
#pragma bss_seg(".app_broadcast_bss")
#pragma data_seg(".app_broadcast_data")
#pragma code_seg(".app_broadcast_text")
#pragma const_seg(".app_broadcast_const")
#endif

#include "system/includes.h"
#include "broadcast_api.h"
#include "app_broadcast.h"
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

#if TCFG_BROADCAST_ENABLE

/**************************************************************************************************
  Macros
**************************************************************************************************/
#define LOG_TAG_CONST       APP
#define LOG_TAG             "[BC]"
#define LOG_ERROR_ENABLE
#define LOG_DEBUG_ENABLE
#define LOG_INFO_ENABLE
/* #define LOG_DUMP_ENABLE */
#define LOG_CLI_ENABLE
#include "debug.h"

#define TRANSMITTER_AUTO_TEST_EN    0
#define RECEIVER_AUTO_TEST_EN       0

/**************************************************************************************************
  Data Types
**************************************************************************************************/
struct app_big_hdl_info {
    u8 used;
    u16 volatile big_status;
    big_hdl_t hdl;
};

/**************************************************************************************************
  Static Prototypes
**************************************************************************************************/
static bool is_broadcast_as_transmitter();
static void broadcast_pair_tx_event_callback(const PAIR_EVENT event, void *priv);
static void broadcast_pair_rx_event_callback(const PAIR_EVENT event, void *priv);
int broadcast_padv_data_deal(void *priv);
const static u8 audio_mode_map[][2] = {
    {APP_BT_TASK, LIVE_A2DP_CAPTURE_MODE},
    {APP_MUSIC_TASK, LIVE_FILE_CAPTURE_MODE},
    {APP_LINEIN_TASK, LIVE_AUX_CAPTURE_MODE},
    {APP_PC_TASK, LIVE_USB_CAPTURE_MODE},
    {APP_LIVE_MIC_TASK, LIVE_MIC_CAPTURE_MODE},
    {APP_LIVE_IIS_TASK, LIVE_IIS_CAPTURE_MODE},
};

/**************************************************************************************************
  Global Variables
**************************************************************************************************/
static OS_MUTEX mutex;
static u8 broadcast_last_role = 0; /*!< 挂起前广播角色 */
static u8 broadcast_app_mode_exit = 0;  /*!< 音源模式退出标志 */
static struct broadcast_sync_info app_broadcast_data_sync;  /*!< 用于记录广播同步状态 */
static struct app_big_hdl_info app_big_hdl_info[BIG_MAX_NUMS];
static u8 config_broadcast_as_master = 0;   /*!< 配置广播强制做主机 */
static const pair_callback_t pair_tx_cb = {
    .pair_event_cb = broadcast_pair_tx_event_callback,
};
static const pair_callback_t pair_rx_cb = {
    .pair_event_cb = broadcast_pair_rx_event_callback,
};

/**************************************************************************************************
  Function Declarations
**************************************************************************************************/

/* --------------------------------------------------------------------------*/
/**
 * @brief 申请互斥量，用于保护临界区代码，与app_broadcast_mutex_post成对使用
 *
 * @param mutex:已创建的互斥量指针变量
 */
/* ----------------------------------------------------------------------------*/
static inline void app_broadcast_mutex_pend(OS_MUTEX *mutex, u32 line)
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
 * @brief 释放互斥量，用于保护临界区代码，与app_broadcast_mutex_pend成对使用
 *
 * @param mutex:已创建的互斥量指针变量
 */
/* ----------------------------------------------------------------------------*/
static inline void app_broadcast_mutex_post(OS_MUTEX *mutex, u32 line)
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
 * @brief BIG开关提示音结束回调接口
 *
 * @param priv:传递的参数
 * @param flag:结束方式 -- 0正常关闭，1被打断关闭
 */
/* ----------------------------------------------------------------------------*/
static void  broadcast_tone_play_end_callback(void *priv, int flag)
{
    u32 index = (u32)priv;
    int temp_broadcast_hdl;

    switch (index) {
    case IDEX_TONE_BROADCAST_OPEN:
        break;
    case IDEX_TONE_BROADCAST_CLOSE:
        break;
    default:
        break;
    }
}

/* --------------------------------------------------------------------------*/
/**
 * @brief BIG状态事件处理函数
 *
 * @param wireless_trans:状态事件附带的返回参数，参数类型是big_hdl_t
 */
/* ----------------------------------------------------------------------------*/
void app_broadcast_conn_status_event_handler(struct wireless_trans_event *wireless_trans)
{
    u8 i, mode;
    u8 find = 0;
    u8 big_hdl;
    u16 crc16;
    int ret;
    big_hdl_t *hdl;
    switch (wireless_trans->event) {
    case BIG_EVENT_TRANSMITTER_CONNECT:
        g_printf("BIG_EVENT_TRANSMITTER_CONNECT");
        //由于是异步操作需要加互斥量保护，避免broadcast_close的代码与其同时运行,添加的流程请放在互斥量保护区里面
        app_broadcast_mutex_pend(&mutex, __LINE__);

        hdl = (big_hdl_t *)wireless_trans->value;
        crc16 = CRC16(hdl, sizeof(big_hdl_t));

        for (i = 0; i < BIG_MAX_NUMS; i++) {
            if (app_big_hdl_info[i].used && (app_big_hdl_info[i].hdl.big_hdl == hdl->big_hdl)) {
                find = 1;
                memcpy(&app_big_hdl_info[i].hdl, hdl, sizeof(big_hdl_t));
                break;
            }
        }

        if ((!find) || (crc16 != wireless_trans->crc16)) {
            //释放互斥量
            app_broadcast_mutex_post(&mutex, __LINE__);
            break;
        }

        mode = app_get_curr_task();
        for (i = 0; i < (sizeof(audio_mode_map) / sizeof(audio_mode_map[0])); i++) {
            if (mode == audio_mode_map[i][0]) {
                mode = audio_mode_map[i][1];
                break;
            }
        }
        ret = broadcast_transmitter_connect_deal((void *)wireless_trans->value, wireless_trans->crc16, mode);
        if (ret < 0) {
            r_printf("broadcast_transmitter_connect_deal fail");
        }

        //释放互斥量
        app_broadcast_mutex_post(&mutex, __LINE__);
        break;

    case BIG_EVENT_RECEIVER_CONNECT:
        g_printf("BIG_EVENT_RECEIVER_CONNECT");
#if defined(WIRELESS_1tN_POLLING_PAIR) && (WIRELESS_1tN_POLLING_PAIR)
        app_broadcast_pair_flag_set(0);
        app_broadcast_conn_flag_set(1);
#endif
        //由于是异步操作需要加互斥量保护，避免broadcast_close的代码与其同时运行,添加的流程请放在互斥量保护区里面
        app_broadcast_mutex_pend(&mutex, __LINE__);

        hdl = (big_hdl_t *)wireless_trans->value;
        crc16 = CRC16(hdl, sizeof(big_hdl_t));

        for (i = 0; i < BIG_MAX_NUMS; i++) {
            if (app_big_hdl_info[i].used && (app_big_hdl_info[i].hdl.big_hdl == hdl->big_hdl)) {
                find = 1;
                memcpy(&app_big_hdl_info[i].hdl, hdl, sizeof(big_hdl_t));
                break;
            }
        }

        if ((!find) || (crc16 != wireless_trans->crc16)) {
            //释放互斥量
            app_broadcast_mutex_post(&mutex, __LINE__);
            break;
        }

        if (app_get_curr_task() == APP_BT_TASK) { // 防止从机在蓝牙模式自动关机
            sys_auto_shut_down_disable();
        }

        ret = broadcast_receiver_connect_deal((void *)wireless_trans->value, wireless_trans->crc16);
        if (ret < 0) {
            r_printf("broadcast_receiver_connect_deal fail");
        }

        //释放互斥量
        app_broadcast_mutex_post(&mutex, __LINE__);
        break;

    case BIG_EVENT_RECEIVER_DISCONNECT:
        g_printf("BIG_EVENT_RECEIVER_DISCONNECT");
#if defined(WIRELESS_1tN_POLLING_PAIR) && (WIRELESS_1tN_POLLING_PAIR)
        app_broadcast_conn_flag_set(0);
        app_broadcast_pair_polling_sw();
#endif
        //由于是异步操作需要加互斥量保护，避免broadcast_close的代码与其同时运行,添加的流程请放在互斥量保护区里面
        app_broadcast_mutex_pend(&mutex, __LINE__);

        big_hdl = wireless_trans->value;

        for (i = 0; i < BIG_MAX_NUMS; i++) {
            if (app_big_hdl_info[i].used && (app_big_hdl_info[i].hdl.big_hdl == big_hdl)) {
                find = 1;
                break;
            }
        }

        if (!find) {
            //释放互斥量
            app_broadcast_mutex_post(&mutex, __LINE__);
            break;
        }

        if (app_get_curr_task() == APP_BT_TASK) {
            sys_auto_shut_down_enable();   // 使能自动关机
        }

        ret = broadcast_receiver_disconnect_deal((void *)wireless_trans->value);
        if (ret < 0) {
            r_printf("broadcast_receiver_disconnect_deal fail");
        }

        //释放互斥量
        app_broadcast_mutex_post(&mutex, __LINE__);
        break;

    case BIG_EVENT_PADV_DATA_SYNC:
        g_printf("BIG_EVENT_PADV_DATA_SYNC");
        broadcast_padv_data_deal((void *)wireless_trans->value);
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
u8 get_broadcast_app_mode_exit_flag(void)
{
    return broadcast_app_mode_exit;
}

/* --------------------------------------------------------------------------*/
/**
 * @brief 判断当前设备作为广播发送设备还是广播接收设备
 *
 * @return true:发送设备，false:接收设备
 */
/* ----------------------------------------------------------------------------*/
static bool is_broadcast_as_transmitter()
{
#if 1
    if (broadcast_fixed_role == 1) {
        return true;
    } else if (broadcast_fixed_role == 2) {
        return false;
    }
    //当前处于蓝牙模式并且已连接手机设备时，
    //(1)播歌作为广播发送设备；
    //(2)暂停作为广播接收设备。
    if ((app_get_curr_task() == APP_BT_TASK) &&
        (get_bt_connect_status() != BT_STATUS_WAITINT_CONN)) {
        /* if (get_a2dp_decoder_status()) { */
        /* if (get_a2dp_start_flag()) { */
        if ((a2dp_get_status() == BT_MUSIC_STATUS_STARTING) ||
            get_a2dp_decoder_status() ||
            get_a2dp_start_flag()) {
            return true;
        } else {
            return false;
        }
    }

#if TCFG_APP_LIVE_IIS_EN
    if (app_get_curr_task() == APP_LIVE_IIS_TASK) {
        return true;
    }
#endif

#if TCFG_LINEIN_ENABLE
    if (app_get_curr_task() == APP_LINEIN_TASK)  {
        if (linein_get_status() || config_broadcast_as_master) {
            return true;
        } else {
            return false;
        }
    }
#endif

    if (app_get_curr_task() == APP_MUSIC_TASK) {
        if ((music_player_get_play_status() == FILE_DEC_STATUS_PLAY) || config_broadcast_as_master) {
            return true;
        } else {
            return false;
        }
    }

    //当处于下面几种模式时，作为广播发送设备
    if ((app_get_curr_task() == APP_FM_TASK) ||
        (app_get_curr_task() == APP_PC_TASK) ||
        (app_get_curr_task() == APP_LIVE_MIC_TASK)) {
        return true;
    }

    return false;
#else
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
#endif
}

/* --------------------------------------------------------------------------*/
/**
 * @brief 检测广播当前是否处于挂起状态
 *
 * @return true:处于挂起状态，false:处于非挂起状态
 */
/* ----------------------------------------------------------------------------*/
static bool is_need_resume_broadcast()
{
    u8 i;
    u8 find = 0;
    for (i = 0; i < BIG_MAX_NUMS; i++) {
        if (app_big_hdl_info[i].big_status == APP_BROADCAST_STATUS_SUSPEND) {
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
 * @brief 广播从挂起状态恢复
 */
/* ----------------------------------------------------------------------------*/
static void app_broadcast_resume()
{
    int temp_broadcast_hdl;
    big_parameter_t *params;

    if (!app_bt_hdl.init_ok) {
        return;
    }

    if (!is_need_resume_broadcast()) {
        return;
    }

    app_broadcast_open();
}

/* --------------------------------------------------------------------------*/
/**
 * @brief 广播进入挂起状态
 */
/* ----------------------------------------------------------------------------*/
static void app_broadcast_suspend()
{
    u8 i;
    u8 find = 0;
    for (i = 0; i < BIG_MAX_NUMS; i++) {
        if (app_big_hdl_info[i].used) {
            find = 1;
            break;
        }
    }
    if (find) {
        broadcast_last_role = get_broadcast_role();
        app_broadcast_close(APP_BROADCAST_STATUS_SUSPEND);
    }
}

/* --------------------------------------------------------------------------*/
/**
 * @brief 开启广播
 */
/* ----------------------------------------------------------------------------*/
void app_broadcast_open()
{
    u8 i;
    u8 big_available_num = 0;
    int temp_broadcast_hdl;
    big_parameter_t *params;

    if (!app_bt_hdl.init_ok) {
        return;
    }

    for (i = 0; i < BIG_MAX_NUMS; i++) {
        if (!app_big_hdl_info[i].used) {
            big_available_num++;
        }
    }

    if (!big_available_num) {
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

    log_info("broadcast_open");
#if defined(RCSP_MODE) && RCSP_MODE
#if RCSP_BLE_MASTER
    extern void setRcspConnectBleAddr(u8 * addr);
    setRcspConnectBleAddr(NULL);
#endif
    setLeAudioModeMode(JL_LeAudioModeBig);
    ble_module_enable(0);
#endif
    if (is_broadcast_as_transmitter()) {
        //初始化广播发送端参数
        params = set_big_params(app_get_curr_task(), BROADCAST_ROLE_TRANSMITTER, 0);

        //打开big，打开成功后会在函数app_broadcast_conn_status_event_handler做后续处理
        temp_broadcast_hdl = broadcast_transmitter(params);
#if TRANSMITTER_AUTO_TEST_EN
        extern void wireless_trans_auto_test3_init(void);
        extern void wireless_trans_auto_test4_init(void);
        //不定时切换模式
        wireless_trans_auto_test3_init();
        //不定时暂停播放
        wireless_trans_auto_test4_init();
#endif
    } else {
        //初始化广播接收端参数
        params = set_big_params(app_get_curr_task(), BROADCAST_ROLE_RECEIVER, 0);

        //打开big，打开成功后会在函数app_broadcast_conn_status_event_handler做后续处理
        temp_broadcast_hdl = broadcast_receiver(params);
#if RECEIVER_AUTO_TEST_EN
        extern void wireless_trans_auto_test3_init(void);
        extern void wireless_trans_auto_test4_init(void);
        //不定时切换模式
        wireless_trans_auto_test3_init();
        //不定时暂停播放
        wireless_trans_auto_test4_init();
#endif
    }
    if (temp_broadcast_hdl >= 0) {
        for (i = 0; i < BIG_MAX_NUMS; i++) {
            if (!app_big_hdl_info[i].used) {
                app_big_hdl_info[i].hdl.big_hdl = temp_broadcast_hdl;
                app_big_hdl_info[i].big_status = APP_BROADCAST_STATUS_START;
                app_big_hdl_info[i].used = 1;
                break;
            }
        }
    }
}

/* --------------------------------------------------------------------------*/
/**
 * @brief 关闭广播
 */
/* ----------------------------------------------------------------------------*/
void app_broadcast_close(u8 status)
{
    u8 i;

    log_info("broadcast_close");

    //由于是异步操作需要加互斥量保护，避免和开启开广播的流程同时运行,添加的流程请放在互斥量保护区里面
    app_broadcast_mutex_pend(&mutex, __LINE__);

    for (i = 0; i < BIG_MAX_NUMS; i++) {
        if (app_big_hdl_info[i].used && app_big_hdl_info[i].hdl.big_hdl) {
            broadcast_close(app_big_hdl_info[i].hdl.big_hdl);
            memset(&app_big_hdl_info[i], 0, sizeof(struct app_big_hdl_info));
            app_big_hdl_info[i].big_status = status;
        }
    }

    //释放互斥量
    app_broadcast_mutex_post(&mutex, __LINE__);

#if defined(RCSP_MODE) && RCSP_MODE
    if (status != APP_BROADCAST_STATUS_SUSPEND) {
        setLeAudioModeMode(JL_LeAudioModeNone);
        ble_module_enable(1);
    }
#endif
}

/* --------------------------------------------------------------------------*/
/**
 * @brief 广播开关切换
 *
 * @return -1:切换失败，1:关闭成功，2：开启成功
 */
/* ----------------------------------------------------------------------------*/
int app_broadcast_switch(void)
{
    u8 i;
    u8 find = 0;
    int temp_broadcast_hdl;
    big_parameter_t *params;

    if (!app_bt_hdl.init_ok) {
        return -1;
    }

    if ((app_get_curr_task() == APP_BT_TASK) &&
        (get_call_status() != BT_CALL_HANGUP)) {
        return -1;
    }

    if ((app_get_curr_task() == APP_FM_TASK) ||
        (app_get_curr_task() == APP_PC_TASK)) {
        return -1;
    }

    for (i = 0; i < BIG_MAX_NUMS; i++) {
        if (app_big_hdl_info[i].used) {
            find = 1;
            break;
        }
    }

    if (find) {
        tone_play_with_callback_by_name(tone_table[IDEX_TONE_BROADCAST_CLOSE], 1, broadcast_tone_play_end_callback, (void *)IDEX_TONE_BROADCAST_CLOSE);
        app_broadcast_close(APP_BROADCAST_STATUS_STOP);
        return 1;
    } else {
        tone_play_with_callback_by_name(tone_table[IDEX_TONE_BROADCAST_OPEN], 1, broadcast_tone_play_end_callback, (void *)IDEX_TONE_BROADCAST_OPEN);
        app_broadcast_open();
        return 2;
    }
}

/* --------------------------------------------------------------------------*/
/**
 * @brief 广播开启情况下，不同场景的处理流程
 *
 * @param switch_mode:当前系统状态
 *
 * @return -1:无需处理，0:处理事件但不拦截后续流程，1:处理事件并拦截后续流程
 */
/* ----------------------------------------------------------------------------*/
int app_broadcast_deal(int switch_mode)
{
    u8 i;
    int ret = -1;
    static int cur_mode = -1;
    static u8 phone_start_cnt = 0;

    if (!app_bt_hdl.init_ok) {
        return ret;
    }

    if ((app_get_curr_task() == APP_BT_TASK) && (get_cur_wireless_trans_mode() != WIRELESS_TRANS_BIG_MODE)) {
        return ret;
    }

    if ((cur_mode == switch_mode) &&
        (switch_mode != BROADCAST_PHONE_START) &&
        (switch_mode != BROADCAST_PHONE_STOP)) {
        log_error("app_broadcast_deal,cur_mode not be modified:%d", switch_mode);
        return ret;
    }

    cur_mode = switch_mode;

    switch (switch_mode) {
    case BROADCAST_APP_MODE_ENTER:
        log_info("BROADCAST_APP_MODE_ENTER");
        //进入当前模式
        broadcast_app_mode_exit = 0;
        ret = 0;
        if (app_get_curr_task() == APP_BT_TASK) {
            if (broadcast_last_role == BROADCAST_ROLE_TRANSMITTER) {
                /* break; */
            }
        }
#if TCFG_BLUETOOTH_BACK_MODE
        config_broadcast_as_master = 1;
        if (is_need_resume_broadcast()) {
            app_broadcast_resume();
            ret = 1;
        }
#else
        config_broadcast_as_master = 0;
#endif
        break;
    case BROADCAST_APP_MODE_EXIT:
        log_info("BROADCAST_APP_MODE_EXIT");
        //退出当前模式
        broadcast_app_mode_exit = 1;
        config_broadcast_as_master = 0;
        if (broadcast_1tn_en) {
            app_broadcast_close(APP_BROADCAST_STATUS_STOP);
        } else {
#if TCFG_BLUETOOTH_BACK_MODE
            app_broadcast_suspend();
#else
            app_broadcast_close(APP_BROADCAST_STATUS_STOP);
#endif
        }
        ret = 0;
        break;
    case BROADCAST_MUSIC_START:
    case BROADCAST_A2DP_START:
        log_info("BROADCAST_MUSIC_START");
        //启动a2dp播放
        ret = 0;
        /* broadcast_app_mode_exit = 0; */
        config_broadcast_as_master = 1;
        if (broadcast_1tn_en) {
            break;
        }
        if (broadcast_app_mode_exit) {
            //防止蓝牙非后台情况下退出蓝牙模式时，会先出现BROADCAST_APP_MODE_EXIT，再出现BROADCAST_A2DP_START，导致广播状态发生改变
            break;
        }
#if BROADCAST_AUTO_SWITCH_ROLE
        for (i = 0; i < BIG_MAX_NUMS; i++) {
            if (app_big_hdl_info[i].used && (app_big_hdl_info[i].big_status == APP_BROADCAST_STATUS_START)) {
                //(1)当处于广播开启并且作为接收设备时，挂起广播，播放当前手机音乐；
                //(2)当前广播处于挂起状态时，恢复广播并作为发送设备。
                if (get_broadcast_role() == BROADCAST_ROLE_RECEIVER) {
                    app_broadcast_suspend();
                } else if (get_broadcast_role() == BROADCAST_ROLE_TRANSMITTER) {
                    ret = 1;
                }
                break;
            }
        }
#else
        if (get_broadcast_role() == BROADCAST_ROLE_TRANSMITTER) {
            for (i = 0; i < BIG_MAX_NUMS; i++) {
                broadcast_audio_capture_reset(app_big_hdl_info[i].hdl.big_hdl);
            }
            ret = 1;
            break;
        }
#endif

#if BT_SUPPORT_MUSIC_VOL_SYNC
        bt_set_music_device_volume(get_music_sync_volume());
#endif
        if (is_need_resume_broadcast()) {
            app_broadcast_resume();
            ret = 1;
        }
        break;
    case BROADCAST_MUSIC_STOP:
    case BROADCAST_A2DP_STOP:
        log_info("BROADCAST_MUSIC_STOP");
        //停止a2dp播放
        ret = 0;
        config_broadcast_as_master = 0;
        if (broadcast_1tn_en) {
            break;
        }
        if (broadcast_app_mode_exit) {
            //防止蓝牙非后台情况下退出蓝牙模式时，会先出现BROADCAST_APP_MODE_EXIT，再出现BROADCAST_A2DP_STOP，导致广播状态发生改变
            break;
        }
#if BROADCAST_AUTO_SWITCH_ROLE
        //当前处于广播挂起状态时，停止手机播放，恢复广播并接收其他设备的音频数据
        app_broadcast_suspend();
#else
        if (get_broadcast_role() == BROADCAST_ROLE_TRANSMITTER) {
            for (i = 0; i < BIG_MAX_NUMS; i++) {
                broadcast_audio_capture_close(app_big_hdl_info[i].hdl.big_hdl);
            }
            ret = 1;
            break;
        }
#endif
        if (is_need_resume_broadcast()) {
            app_broadcast_resume();
        }
        break;
    case BROADCAST_PHONE_START:
        log_info("BROADCAST_PHONE_START");
        //通话时，挂起广播
        phone_start_cnt++;
        app_broadcast_suspend();
        ret = 0;
        break;
    case BROADCAST_PHONE_STOP:
        log_info("BROADCAST_PHONE_STOP");
        //通话结束恢复广播
        ret = 0;
        phone_start_cnt--;
        printf("===phone_start_cnt:%d===\n", phone_start_cnt);
        if (phone_start_cnt) {
            log_info("phone_start_cnt:%d", phone_start_cnt);
            break;
        }
        //当前处于蓝牙模式并且挂起前广播作为发送设备，恢复广播的操作在播放a2dp处执行
        if (app_get_curr_task() == APP_BT_TASK) {
            if (broadcast_last_role == BROADCAST_ROLE_TRANSMITTER) {
            }
        }
        //当前处于蓝牙模式并且挂起前广播，恢复广播并作为接收设备
        if (is_need_resume_broadcast()) {
            app_broadcast_resume();
        }
        break;
    case BROADCAST_EDR_DISCONN:
        log_info("BROADCAST_EDR_DISCONN");
        ret = 0;
        if (broadcast_app_mode_exit) {
            //防止蓝牙非后台情况下退出蓝牙模式时，会先出现BROADCAST_APP_MODE_EXIT，再出现BROADCAST_EDR_DISCONN，导致广播状态发生改变
            break;
        }
        //当经典蓝牙断开后，作为发送端的广播设备挂起广播
        if (get_broadcast_role() == BROADCAST_ROLE_TRANSMITTER) {
            app_broadcast_suspend();
        }
        if (is_need_resume_broadcast()) {
            app_broadcast_resume();
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
 * @brief 初始化同步的状态数据的内容
 */
/* ----------------------------------------------------------------------------*/
static void app_broadcast_sync_data_init(void)
{
    app_broadcast_data_sync.volume = app_audio_get_volume(APP_AUDIO_STATE_MUSIC);
    broadcast_sync_data_init(&app_broadcast_data_sync);
}

/* --------------------------------------------------------------------------*/
/**
 * @brief 更新广播同步状态的数据
 *
 * @param type:更新项
 * @param value:更新值
 *
 * @return -1:fail，0:success
 */
/* ----------------------------------------------------------------------------*/
int update_broadcast_sync_data(u8 type, int value)
{
    u8 i;
    switch (type) {
    case BROADCAST_SYNC_VOL:
        if (app_broadcast_data_sync.volume == value) {
            return 0;
        }
        app_broadcast_data_sync.volume = value;
        break;
    case BROADCAST_SYNC_SOFT_OFF:
        if (app_broadcast_data_sync.softoff == value) {
            return 0;
        }
        app_broadcast_data_sync.softoff = value;
        break;
    default:
        return -1;
    }

    for (i = 0; i < BIG_MAX_NUMS; i++) {
        /* if (app_big_hdl_info[i].used) { */
        broadcast_set_sync_data(app_big_hdl_info[i].hdl.big_hdl, &app_broadcast_data_sync, sizeof(struct broadcast_sync_info));
        /* } */
    }

    return 0;
}

/* --------------------------------------------------------------------------*/
/**
 * @brief 接收到广播发送端的同步数据，并更新本地配置
 *
 * @param priv:接收到的同步数据
 *
 * @return
 */
/* ----------------------------------------------------------------------------*/
int broadcast_padv_data_deal(void *priv)
{
    struct broadcast_sync_info *sync_data = (struct broadcast_sync_info *)priv;
    if (app_broadcast_data_sync.volume != sync_data->volume) {
        app_broadcast_data_sync.volume = sync_data->volume;
        app_audio_set_volume(APP_AUDIO_STATE_MUSIC, app_broadcast_data_sync.volume, 1);
    }
    if (app_broadcast_data_sync.softoff != sync_data->softoff) {
        app_broadcast_data_sync.softoff = sync_data->softoff;
        if (!app_var.goto_poweroff_flag) {
            sys_timeout_add(NULL, sys_enter_soft_poweroff, app_broadcast_data_sync.softoff);
        }
    }
    memcpy(&app_broadcast_data_sync, sync_data, sizeof(struct broadcast_sync_info));
    return 0;
}

/* --------------------------------------------------------------------------*/
/**
 * @brief 广播资源初始化
 */
/* ----------------------------------------------------------------------------*/
void app_broadcast_init(void)
{
    if (!app_bt_hdl.init_ok) {
        return;
    }

    int os_ret = os_mutex_create(&mutex);
    if (os_ret != OS_NO_ERR) {
        log_error("%s %d err, os_ret:0x%x", __FUNCTION__, __LINE__, os_ret);
        ASSERT(0);
    }

    broadcast_init();
    app_broadcast_sync_data_init();
}

/* --------------------------------------------------------------------------*/
/**
 * @brief 广播资源卸载
 */
/* ----------------------------------------------------------------------------*/
void app_broadcast_uninit(void)
{
    if (!app_bt_hdl.init_ok) {
        return;
    }

    int os_ret = os_mutex_del(&mutex, OS_DEL_NO_PEND);
    if (os_ret != OS_NO_ERR) {
        log_error("%s %d err, os_ret:0x%x", __FUNCTION__, __LINE__, os_ret);
    }

    broadcast_uninit();
}

/* --------------------------------------------------------------------------*/
/**
 * @brief 广播接收端配对事件回调
 *
 * @param event：配对事件
 * @param priv：事件附带的参数
 */
/* ----------------------------------------------------------------------------*/
static void broadcast_pair_rx_event_callback(const PAIR_EVENT event, void *priv)
{
    int pair_flag = 0;
    switch (event) {
    case PAIR_EVENT_RX_PRI_CHANNEL_CREATE_SUCCESS:
        u32 *private_connect_access_addr = (u32 *)priv;
        g_printf("PAIR_EVENT_RX_PRI_CHANNEL_CREATE_SUCCESS:0x%x", *private_connect_access_addr);
        int ret = syscfg_write(VM_WIRELESS_PAIR_CODE0, private_connect_access_addr, sizeof(u32));
        if (broadcast_1tn_en) {
#if defined(WIRELESS_1tN_POLLING_PAIR) && (WIRELESS_1tN_POLLING_PAIR)
            app_broadcast_pair_flag_set(1);
#endif
            app_broadcast_open();
        }
        if (ret <= 0) {
            r_printf(">>>>>>wireless pair code save err");
        }
        break;
    case PAIR_EVENT_RX_OPEN_PAIR_MODE_SUCCESS:
        g_printf("PAIR_EVENT_RX_OPEN_PAIR_MODE_SUCCESS");
        break;
    case PAIR_EVENT_RX_CLOSE_PAIR_MODE_SUCCESS:
        g_printf("PAIR_EVENT_RX_CLOSE_PAIR_MODE_SUCCESS");
        break;
    }
}

/* --------------------------------------------------------------------------*/
/**
 * @brief 广播发送端配对事件回调
 *
 * @param event：配对事件
 * @param priv：事件附带的参数
 */
/* ----------------------------------------------------------------------------*/
static void broadcast_pair_tx_event_callback(const PAIR_EVENT event, void *priv)
{
    int pair_flag = 0;
    switch (event) {
    case PAIR_EVENT_TX_PRI_CHANNEL_CREATE_SUCCESS:
        u32 *private_connect_access_addr = (u32 *)priv;
        g_printf("PAIR_EVENT_TX_PRI_CHANNEL_CREATE_SUCCESS:0x%x", *private_connect_access_addr);
        int ret = syscfg_write(VM_WIRELESS_PAIR_CODE0, private_connect_access_addr, sizeof(u32));
        if (ret <= 0) {
            r_printf(">>>>>>wireless pair code save err");
        }
        break;
    case PAIR_EVENT_TX_OPEN_PAIR_MODE_SUCCESS:
        g_printf("PAIR_EVENT_TX_OPEN_PAIR_MODE_SUCCESS");
        break;
    case PAIR_EVENT_TX_CLOSE_PAIR_MODE_SUCCESS:
        g_printf("PAIR_EVENT_TX_CLOSE_PAIR_MODE_SUCCESS");
        if (broadcast_1tn_en) {
            app_broadcast_open();
        }
        break;
    }
}

/* --------------------------------------------------------------------------*/
/**
 * @brief 广播进入配对模式
 *
 * @param role：广播角色接收端还是发送端
 */
/* ----------------------------------------------------------------------------*/
void app_broadcast_enter_pair(u8 role)
{
    int ret;
    u32 private_connect_access_addr = 0;;

    app_broadcast_close(APP_BROADCAST_STATUS_STOP);

    ret = syscfg_read(VM_WIRELESS_PAIR_CODE0, &private_connect_access_addr, sizeof(u32));

    if (role == BROADCAST_ROLE_UNKNOW) {
        if (is_broadcast_as_transmitter()) {
            broadcast_enter_pair(BROADCAST_ROLE_TRANSMITTER, (void *)&pair_tx_cb, private_connect_access_addr);
        } else {
            broadcast_enter_pair(BROADCAST_ROLE_RECEIVER, (void *)&pair_rx_cb, private_connect_access_addr);
        }
    } else if (role == BROADCAST_ROLE_TRANSMITTER) {
        broadcast_enter_pair(role, (void *)&pair_tx_cb, private_connect_access_addr);
    } else if (role == BROADCAST_ROLE_RECEIVER) {
        broadcast_enter_pair(role, (void *)&pair_rx_cb, private_connect_access_addr);
    }
}

/* --------------------------------------------------------------------------*/
/**
 * @brief 退出广播配对模式
 *
 * @param role：广播角色接收端还是发送端
 */
/* ----------------------------------------------------------------------------*/
void app_broadcast_exit_pair(u8 role)
{
    if (role == BROADCAST_ROLE_UNKNOW) {
        if (is_broadcast_as_transmitter()) {
            broadcast_exit_pair(BROADCAST_ROLE_TRANSMITTER);
        } else {
            broadcast_exit_pair(BROADCAST_ROLE_RECEIVER);
        }
    } else {
        broadcast_exit_pair(role);
    }
}

/* --------------------------------------------------------------------------*/
/**
 * @brief 非蓝牙后台情况下，在其他音源模式开启BIG，前提要先开蓝牙协议栈
 */
/* ----------------------------------------------------------------------------*/
void app_broadcast_open_in_other_mode()
{
    app_broadcast_init();
    if (broadcast_1tn_en) {
        int pair_code = 0;
        int ret = syscfg_read(VM_WIRELESS_PAIR_CODE0, &pair_code, sizeof(int));
        if (ret > 0) {
            if (broadcast_fixed_role == BROADCAST_ROLE_RECEIVER) {
                if (pair_code != 0xFFFFFFFF) {
                    app_broadcast_open();
                }
            } else {
                if (pair_code != 0xFFFFFFFE) {
                    app_broadcast_open();
                }
            }
        }
    } else {
        app_broadcast_open();
    }
}

/* --------------------------------------------------------------------------*/
/**
 * @brief 非蓝牙后台情况下，在其他音源模式关闭BIG
 */
/* ----------------------------------------------------------------------------*/
void app_broadcast_close_in_other_mode()
{
    app_broadcast_close(APP_BROADCAST_STATUS_STOP);
    app_broadcast_uninit();
}

#if defined(WIRELESS_1tN_POLLING_PAIR) && (WIRELESS_1tN_POLLING_PAIR)
struct __pair_polling_info pair_polling_info = {
    .pair_polling_timer = 0,
    .cur_pair_mode = 0,
    .connect_succ_flag = 0,
    .pair_succ_flag = 0,
};
static void pair_polling_handler(void *priv)
{
    u32 private_connect_access_addr;
    int ret = 0;
    if (pair_polling_info.pair_succ_flag || pair_polling_info.connect_succ_flag) {
        if (pair_polling_info.pair_polling_timer) {
            sys_timer_del(pair_polling_info.pair_polling_timer);
            pair_polling_info.pair_polling_timer = 0;
        }
        return;
    }
    ret = syscfg_read(VM_WIRELESS_PAIR_CODE0, &private_connect_access_addr, sizeof(u32));
    if (ret <= 0) {
        app_broadcast_close(0);
        app_broadcast_enter_pair(0);
        if (pair_polling_info.pair_polling_timer) {
            sys_timer_del(pair_polling_info.pair_polling_timer);
            pair_polling_info.pair_polling_timer = 0;
        }
    } else {
        if (pair_polling_info.cur_pair_mode) {
            app_broadcast_close(0);
            app_broadcast_enter_pair(0);
            pair_polling_info.cur_pair_mode = 0;
        } else {
            app_broadcast_exit_pair(0);
            app_broadcast_open();
            pair_polling_info.cur_pair_mode = 1;
        }
    }
}
void app_broadcast_pair_flag_set(u8 flag)
{
    pair_polling_info.pair_succ_flag = flag;
}

void app_broadcast_conn_flag_set(u8 flag)
{
    pair_polling_info.connect_succ_flag = flag;
}

void app_broadcast_pair_polling_sw()
{
    if (pair_polling_info.pair_polling_timer == 0) {
        pair_polling_info.pair_polling_timer = sys_timer_add(NULL, pair_polling_handler, 3000);
    } else {
        sys_timer_del(pair_polling_info.pair_polling_timer);
        pair_polling_info.pair_polling_timer = 0;
    }
}
#endif

#endif

