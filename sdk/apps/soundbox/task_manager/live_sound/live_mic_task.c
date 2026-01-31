/*************************************************************
  此文件函数主要是 mic 模式按键处理和事件处理

  void app_live_mic_task
  mic模式主函数

  static int live_mic_sys_event_handler(struct sys_event *event)
  linein模式系统事件所有处理入口

  static void live_mic_task_close(void)
  linein模式退出

 **************************************************************/

#include "system/app_core.h"
#include "system/includes.h"
#include "server/server_core.h"

#include "app_config.h"
#include "app_cfg.h"
#include "app_task.h"
#include "live_mic.h"
#include "linein/linein.h"
#include "bt/bt.h"
#include "btstack/avctp_user.h"
#include "media/includes.h"
#include "tone_player.h"

#include "app_charge.h"
#include "app_main.h"
#include "app_online_cfg.h"
#include "app_power_manage.h"

#include "audio_dec_iis.h"

#include "key_event_deal.h"
#include "user_cfg.h"
#include "clock_cfg.h"

#include "app_connected.h"
#include "connected_api.h"
#include "app_broadcast.h"
#include "broadcast_api.h"
#include "audio_mode.h"

#if TCFG_APP_LIVE_MIC_EN

/* u8 local_mic_is_on = 0;		//该值为1说明当前本地mic打开的状态 */


static int connected_detect_timer = 0;

static void connected_on_off_detect(void *priv)
{
    static bool detect = false;
    static bool on_off = true;

    if (on_off != get_connected_on_off()) {
        on_off = !on_off;
        detect = true;
    }
    if (detect) {
        if (!on_off) {
            user_send_cmd_prepare(USER_CTRL_WRITE_SCAN_ENABLE, 0, NULL);
            user_send_cmd_prepare(USER_CTRL_WRITE_CONN_ENABLE, 0, NULL);
        } else {
            bt_close_discoverable_and_connectable();
        }
        detect = false;
    }
}



//*----------------------------------------------------------------------------*/
/**@brief    broadcast mic 入口
   @param    无
   @return
   @note
*/
/*----------------------------------------------------------------------------*/
static void live_mic_app_init(void)
{
    sys_key_event_enable();
    clock_idle(LINEIN_IDLE_CLOCK);
    if (le_broadcast_en || le_connected_en) {
        if (connected_2t1_duplex) {
            extern void wireless_multiple_audio_codec_interface_register(void);
            extern void set_cig_audio_dev_online(u8 mode);
            wireless_multiple_audio_codec_interface_register();
            if (!bt_back_mode_en) {
                btstack_init_in_other_mode();
            } else {
                if (!app_bt_hdl.init_ok) {
                    bt_direct_init();
                }
            }

            /* connected_detect_timer = sys_timer_add(NULL, connected_on_off_detect, 200); */
        } else {
            mic_wireless_audio_codec_interface_register();
            if (!bt_back_mode_en) {
                btstack_init_in_other_mode();
            }
        }
    }
}

//*----------------------------------------------------------------------------*/
/**@brief    broadcast mic 提示音播放结束回调函数
   @param    无
   @return
   @note
*/
/*----------------------------------------------------------------------------*/
static void  live_mic_tone_play_end_callback(void *priv, int flag)
{
    u32 index = (u32)priv;
    if (app_next_task) {
        printf("\n-- error: curr task isn't APP_LIVE_MIC_TASK!\n");
        return;
    }
    switch (index) {
    case IDEX_TONE_MIC:
        app_task_put_key_msg(KEY_MIC_START, 0);
        break;
    default:
        break;
    }
}

static void live_mic_start(void)
{
    if (le_broadcast_en || le_connected_en) {
        mic_start(TCFG_AUDIO_ADC_MIC_CHA, JLA_CODING_SAMPLERATE, 6);
    } else {
        mic_start(TCFG_AUDIO_ADC_MIC_CHA, 44100, 6);
    }
}

static void multiple_dev_start(void)
{
    if (connected_role_config == CONNECTED_ROLE_PERIP) {
        mic_start(TCFG_AUDIO_ADC_MIC_CHA, JLA_CODING_SAMPLERATE, 6);
    }

#if TCFG_LINEIN_ENABLE
    linein_start();
#endif

#if TCFG_AUDIO_INPUT_IIS
    iis_in_dec_open(TCFG_IIS_SR);
#endif //#if TCFG_AUDIO_INPUT_IIS

}

//*----------------------------------------------------------------------------*/
/**@brief   bc mic 按键消息入口
   @param    无
   @return   1、消息已经处理，不需要发送到common  0、消息发送到common处理
   @note
*/
/*----------------------------------------------------------------------------*/
static int live_mic_key_msg_deal(struct sys_event *event)
{
    int result = 0;
    int ret = true;
    int err = 0;
    u8 vol;
    int key_event = event->u.key.event;
    int key_value = event->u.key.value;
    switch (key_event) {
    case KEY_MIC_START:

        if (le_broadcast_en) {
            result = app_broadcast_deal(BROADCAST_APP_MODE_ENTER);
        }
        if (le_connected_en) {
            result = app_connected_deal(CONNECTED_APP_MODE_ENTER);
        }
        if (result <= 0) {
            if (connected_2t1_duplex) {
                app_connected_open(0);
                multiple_dev_start();
            } else {
                if (!(!bt_back_mode_en && (le_broadcast_en || le_connected_en))) {  //防止在上面初始化蓝牙协议栈成功后会自动开广播，从而开启广播音频后，此处开启音频打断广播音频.
                    live_mic_start();
                }
            }
        }

        break;
    default:
        ret = false;
        break;
    }
    return ret;
}



//*----------------------------------------------------------------------------*/
/**@brief    bc mic 模式活跃状态 所有消息入口
   @param    无
   @return   1、当前消息已经处理，不需要发送comomon 0、当前消息不是linein处理的，发送到common统一处理
   @note
*/
/*----------------------------------------------------------------------------*/
static int live_mic_sys_event_handler(struct sys_event *event)
{
    int ret = TRUE;
    switch (event->type) {
    case SYS_KEY_EVENT:
        return live_mic_key_msg_deal(event);
        break;
    default:
        return false;
    }
    return false;
}

static void live_mic_task_close(void)
{
    if (le_broadcast_en) {
        app_broadcast_deal(BROADCAST_APP_MODE_EXIT);
    }
    if (le_connected_en) {
        app_connected_deal(CONNECTED_APP_MODE_EXIT);
        if (connected_detect_timer) {
            sys_timer_del(connected_detect_timer);
            connected_detect_timer = 0;
        }
    }

    if (!bt_back_mode_en) {
        if (le_broadcast_en) {
            app_broadcast_close_in_other_mode();
        }
        if (le_connected_en) {
            app_connected_close_in_other_mode();
        }
        btstack_exit_in_other_mode();
    }
    extern void mic_stop(void);
    mic_stop();
}

//*----------------------------------------------------------------------------*/
/**@brief    broadcast mic 任务主体
   @param    无
   @return
   @note
*/
/*----------------------------------------------------------------------------*/
void app_live_mic_task()
{
    int res;
    int msg[32];
    live_mic_app_init();
    tone_play_with_callback_by_name(tone_table[IDEX_TONE_MIC], 1, live_mic_tone_play_end_callback, (void *)IDEX_TONE_MIC);

    while (1) {
        app_task_get_msg(msg, ARRAY_SIZE(msg), 1);
        switch (msg[0]) {
        case APP_MSG_SYS_EVENT:
            if (live_mic_sys_event_handler((struct sys_event *)(&msg[1])) == false) {
                app_default_event_deal((struct sys_event *)(&msg[1]));
            }
            break;
        default:
            break;
        }

        if (app_task_exitting()) {
            live_mic_task_close();
            return;
        }
    }
}

#else
void app_live_mic_task(void)
{

}

#endif /* TCFG_APP_live_mic_EN */


