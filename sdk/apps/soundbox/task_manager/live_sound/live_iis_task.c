/*************************************************************
   此文件函数主要是 live iis 模式按键处理和事件处理

	void app_live_iis_task
   live iis模式主函数

	static int live iis _sys_event_handler(struct sys_event *event)
   live iis模式系统事件所有处理入口

	static void live_iis_task_close(void)
	live iis模式退出

**************************************************************/

#include "system/app_core.h"
#include "system/includes.h"
#include "server/server_core.h"

#include "app_config.h"
#include "app_task.h"

#include "live_iis.h"
#include "media/includes.h"
#include "tone_player.h"

#include "app_charge.h"
#include "app_main.h"
#include "app_online_cfg.h"
#include "app_power_manage.h"

#include "key_event_deal.h"
#include "user_cfg.h"
#include "clock_cfg.h"

#include "app_connected.h"
#include "connected_api.h"
#include "app_broadcast.h"
#include "broadcast_api.h"

#if ((defined TCFG_APP_LIVE_IIS_EN) && TCFG_APP_LIVE_IIS_EN)
static u8 live_iis_idle = 1;
//*----------------------------------------------------------------------------*/
/**@brief    live iis 初始化
   @param    无
   @return
   @note     广播方案需要对模式的广播进行特别的初始化
*/
/*----------------------------------------------------------------------------*/
static void live_iis_app_init(void)
{
    sys_key_event_enable();
    clock_idle(LINEIN_IDLE_CLOCK);
#if (TCFG_BROADCAST_ENABLE || TCFG_CONNECTED_ENABLE)
    live_iis_wireless_audio_codec_interface_register();
    if (!bt_back_mode_en) {
        btstack_init_in_other_mode();
    }
#endif
    live_iis_idle = 0;
}

//*----------------------------------------------------------------------------*/
/**@brief    live iis 提示音播放结束回调函数
   @param    无
   @return
   @note
*/
/*----------------------------------------------------------------------------*/
static void  live_iis_tone_play_end_callback(void *priv, int flag)
{
    u32 index = (u32)priv;
    if (app_next_task) {
        printf("\n-- error: curr task isn't APP_live_iis_TASK!\n");
        return;
    }
    switch (index) {
    case IDEX_TONE_IIS:
        app_task_put_key_msg(KEY_MIC_START, 0);
        break;
    default:
        break;
    }
}

//*----------------------------------------------------------------------------*/
/**@brief   live iis 按键消息入口
   @param    无
   @return   1、消息已经处理，不需要发送到common  0、消息发送到common处理
   @note
*/
/*----------------------------------------------------------------------------*/
static int live_iis_key_msg_deal(struct sys_event *event)
{
    int result;
    int ret = true;
    int err = 0;
    u8 vol;
    int key_event = event->u.key.event;
    int key_value = event->u.key.value;
    switch (key_event) {
    case KEY_MIC_START:
#if TCFG_BROADCAST_ENABLE
        result = app_broadcast_deal(BROADCAST_APP_MODE_ENTER);
        if (result <= 0)
#endif
#if TCFG_CONNECTED_ENABLE
            result = app_connected_deal(CONNECTED_APP_MODE_ENTER);
        if (result <= 0)
#endif
        {
            if (!(!bt_back_mode_en && (le_broadcast_en || le_connected_en))) {  //在上面初始化蓝牙协议栈成功后会自动开广播，从而开启广播音频，不需要此处开启音频打断广播音频
                live_iis_playback_start();
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
/**@brief    live iis 模式活跃状态 所有消息入口
   @param    无
   @return   1、当前消息已经处理，不需要发送comomon 0、当前消息不是linein处理的，发送到common统一处理
   @note
*/
/*----------------------------------------------------------------------------*/
static int live_iis_sys_event_handler(struct sys_event *event)
{
    int ret = TRUE;
    switch (event->type) {
    case SYS_KEY_EVENT:
        return live_iis_key_msg_deal(event);
        break;
    default:
        return false;
    }
    return false;
}

static void live_iis_task_close(void)
{
#if TCFG_BROADCAST_ENABLE
    app_broadcast_deal(BROADCAST_APP_MODE_EXIT);
#endif
#if TCFG_CONNECTED_ENABLE
    app_connected_deal(CONNECTED_APP_MODE_EXIT);
#endif
    if (!bt_back_mode_en) {
        if (le_broadcast_en) {
            app_broadcast_close_in_other_mode();
        }
        if (le_connected_en) {
            app_connected_close_in_other_mode();
        }
        btstack_exit_in_other_mode();
    }
    live_iis_playback_stop();
    tone_play_stop_by_path(tone_table[IDEX_TONE_IIS]);
    live_iis_idle = 1;
}

//*----------------------------------------------------------------------------*/
/**@brief    live iis 任务主体
   @param    无
   @return
   @note
*/
/*----------------------------------------------------------------------------*/
void app_live_iis_task(void)
{
    int res;
    int msg[32];

    live_iis_app_init();
    tone_play_with_callback_by_name(tone_table[IDEX_TONE_IIS], 1, live_iis_tone_play_end_callback, (void *)IDEX_TONE_IIS);

    while (1) {
        app_task_get_msg(msg, ARRAY_SIZE(msg), 1);
        switch (msg[0]) {
        case APP_MSG_SYS_EVENT:
            if (live_iis_sys_event_handler((struct sys_event *)(&msg[1])) == false) {
                app_default_event_deal((struct sys_event *)(&msg[1]));
            }
            break;
        default:
            break;
        }

        if (app_task_exitting()) {
            live_iis_task_close();
            return;
        }
    }
}
static u8 live_iis_idle_query(void)
{
    return live_iis_idle;
}
REGISTER_LP_TARGET(live_iis_lp_target) = {
    .name = "live_iis",
    .is_idle = live_iis_idle_query,
};

#else
void app_live_iis_task(void)
{

}

#endif /* TCFG_APP_LIVE_IIS_EN */


