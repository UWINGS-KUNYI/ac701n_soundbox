#include "system/includes.h"
#include "media/includes.h"
#include "application/audio_dec_app.h"
#include "tone_player.h"
#include "bt_tws.h"
#include "app_task.h"

#if TCFG_TONE2TWS_ENABLE

//////////////////////////////////////////////////////////////////////////////


#define TWS_FUNC_ID_TONE2TWS		TWS_FUNC_ID('2', 'T', 'W', 'S')

struct tws_tone_message {
    u8 tone_index;
    u8 follow;
    u8 preemption;
    int delay_msec;
    void *evt_priv;
    void (*evt_handler)(void *priv, int flag);
};

// 用于转发的提示音
static const char *const tone2tws_index[] = {
    TONE_BT_MODE,
    TONE_MUSIC,
    TONE_LINEIN,
    TONE_FM,
    TONE_PC,
    TONE_RTC,
    TONE_RECORD,
    TONE_RES_ROOT_PATH"tone/sd.*",
    TONE_RES_ROOT_PATH"tone/udisk.*",
} ;

static u32 tone2tws_dat = 0;

/*----------------------------------------------------------------------------*/
/**@brief    tws提示音数据处理
   @param    dat: 数据
   @return
   @note     在任务中集中处理tws信息
*/
/*----------------------------------------------------------------------------*/
static void tone2tws_rx_play(void *msg)
{
    struct tws_tone_message *rx_msg = (struct tws_tone_message *)msg;
    int index = rx_msg->tone_index;
    if (index >= ARRAY_SIZE(tone2tws_index)) {
        return;
    }
    /* y_printf("tone2tws_rx_play name:%d \n", tone2tws_index[index]); */
    char *single_file[2] = {NULL};
    // is file name
    single_file[0] = (char *)tone2tws_index[index];
    single_file[1] = NULL;
    tone_play_open_with_callback_base(single_file, 0, rx_msg->preemption, rx_msg->evt_handler, rx_msg->evt_priv, rx_msg->delay_msec);
}

static void tone2tws_rx_callback_func(void *msg)
{
    /* printf("tone2tws_rx_callback_func\n"); */
    if (!msg) {
        return;
    }

    tone2tws_rx_play(msg);

    free(msg);
}

/*----------------------------------------------------------------------------*/
/**@brief    提示音tws私有信息处理
   @param    *data: 数据
   @param    len: 数据长度
   @param    rx: 1-接受端
   @return
   @note
*/
/*----------------------------------------------------------------------------*/
static void tone2tws_tws_rx_data(void *data, u16 len, bool rx)
{
    g_printf("tone2tws_tws_rx_data, l:%d, rx:%d \n", len, rx);
    /* printf("data rx \n"); */
    /* printf_buf(data, len); */

    struct tws_tone_message *tws_tone_rx_msg;
    tws_tone_rx_msg = zalloc(sizeof(struct tws_tone_message));
    if (!tws_tone_rx_msg) {
        return;
    }
    memcpy(tws_tone_rx_msg, data, sizeof(struct tws_tone_message));

    // 该函数是在中断里面调用，实际处理放在task里面
    int argv[3];
    argv[0] = (int)tone2tws_rx_callback_func;
    argv[1] = 1;
    argv[2] = (int)tws_tone_rx_msg;
    int ret = os_taskq_post_type("app_core", Q_CALLBACK, 3, argv);
    if (ret) {
        log_e("taskq post err, ret:%d, len:%d\n", ret, len);
    }
}

REGISTER_TWS_FUNC_STUB(tws_tone2tws_rx) = {
    .func_id = TWS_FUNC_ID_TONE2TWS,
    .func = tone2tws_tws_rx_data,
};

static u8 tone2tws_play(const char **list, u8 follow, u8 preemption, void (*evt_handler)(void *priv, int flag), void *evt_priv, int delay_msec)
{
    struct tws_tone_message msg = {
        .tone_index = 0,
        .follow = follow,
        .preemption = preemption,
        .delay_msec = delay_msec,
        .evt_priv = evt_priv,
        .evt_handler = evt_handler,
    };
    int state = tws_api_get_tws_state();
    if (state & TWS_STA_SIBLING_CONNECTED) {
        // 有tws连接，检查是否需要把提示音推送给对方
        char *name = list[0];
        for (int i = 0; i < ARRAY_SIZE(tone2tws_index); i++) {
            if (IS_REPEAT_BEGIN(name) || IS_REPEAT_END(name)) {
                break;
            }
            if (IS_DEFAULT_SINE(name) || IS_DEFAULT_SINE(tone2tws_index[i])) {
                if ((u32)name == (u32)tone2tws_index[i]) {
                    // is sine idx
                    msg.tone_index = i;
                    tws_api_send_data_to_sibling(&msg, sizeof(struct tws_tone_message), TWS_FUNC_ID_TONE2TWS);
                    break;
                }
            } else if (!strcmp(tone2tws_index[i], name)) {
                // is file name
                msg.tone_index = i;
                tws_api_send_data_to_sibling(&msg, sizeof(struct tws_tone_message), TWS_FUNC_ID_TONE2TWS);
                break;
            }
        }
    }

    if (msg.tone_index) {
        return true;
    } else {
        return false;
    }
}

static int tws_tone_play_open_with_callback(const char **list, u8 follow, u8 preemption, void (*evt_handler)(void *priv, int flag), void *evt_priv, int delay_msec)
{
    if (tone2tws_play(list, follow, preemption, evt_handler, evt_priv, delay_msec)) {
        return 0;
    }
    return -1;
}

/*----------------------------------------------------------------------------*/
/**@brief    按名字播放提示音
   @param    *name: 文件名
   @param    preemption: 打断标记
   @param    *evt_handler: 事件回调接口
   @param    *evt_priv: 事件回调私有句柄
   @param    delay_msec: 播放延时
   @return   0: 成功
   @note
*/
/*----------------------------------------------------------------------------*/
int tws_tone_play_with_callback_by_name(char *name,
                                        u8 preemption,
                                        void (*evt_handler)(void *priv, int flag),
                                        void *evt_priv,
                                        int delay_msec)
{
    char *single_file[2] = {NULL};
    single_file[0] = name;
    single_file[1] = NULL;
    return tws_tone_play_open_with_callback(single_file, 0, preemption, evt_handler, evt_priv, delay_msec);
}

#endif

