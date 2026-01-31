#include "common/app_common.h"
#include "app_task.h"
#include "app_main.h"
#include "app_cfg.h"
#include "key_event_deal.h"
#include "music/music.h"
#include "pc/pc.h"
#include "record/record.h"
#include "linein/linein.h"
#include "fm/fm.h"
#include "btstack/avctp_user.h"
#include "app_power_manage.h"
#include "app_chargestore.h"
#include "usb/otg.h"
#include "usb/host/usb_host.h"
#include "usb/device/usb_stack.h"
#include <stdlib.h>
#include "bt/bt_tws.h"
#include "audio_config.h"
#include "common/power_off.h"
#include "common/user_msg.h"
#include "audio_config.h"
#include "audio_enc.h"
#include "ui/ui_api.h"
#include "fm_emitter/fm_emitter_manage.h"
#include "common/fm_emitter_led7_ui.h"
#if TCFG_CHARGE_ENABLE
#include "app_charge.h"
#endif
#include "dev_multiplex_api.h"
#include "chgbox_ctrl.h"
#include "device/chargebox.h"
#include "app_online_cfg.h"
#include "soundcard/soundcard.h"
#include "rcsp_bt_manage.h"
#include "bt.h"
#include "common/dev_status.h"
#include "tone_player.h"
#include "ui_manage.h"
#include "soundbox.h"
#include "bt_emitter.h"
#include "broadcast_api.h"
#include "sound_device_driver.h"
#include "app_broadcast.h"
#include "app_connected.h"
#include "connected_api.h"
#include "user_api/app_status_api.h"
#include "audio_mode.h"
#include "audio_recorder_mix.h"

#if WIRELESS_MIC_EFFECT_ENABLE
#include "wireless_mic_effect.h"
#endif
#if TCFG_UNISOUND_ENABLE
#include "unisound/unisound_mic.h"
#endif

#define LOG_TAG_CONST       APP_ACTION
#define LOG_TAG             "[APP_ACTION]"
#define LOG_ERROR_ENABLE
#define LOG_DEBUG_ENABLE
#define LOG_INFO_ENABLE
/* #define LOG_DUMP_ENABLE */
#define LOG_CLI_ENABLE
#include "debug.h"

#include "clock_cfg.h"

#if SYS_DIGVOL_GROUP_EN
#include "application/audio_dig_vol.h"
extern void *sys_digvol_group;
#endif

int JL_rcsp_event_handler(struct rcsp_event *rcsp);
int bt_background_event_handler(struct sys_event *event);
extern u32 timer_get_ms(void);
extern int alarm_sys_event_handler(struct sys_event *event);
extern void bt_tws_sync_volume();
extern int jl_kws_voice_event_handle(struct sys_event *event);

static const u8 wireless_trans_mode_list[] = {
    WIRELESS_TRANS_BT_MODE,

#if TCFG_USER_TWS_ENABLE
    WIRELESS_TRANS_TWS_MODE,
#endif

#if TCFG_BROADCAST_ENABLE
    WIRELESS_TRANS_BIG_MODE,
#endif

#if TCFG_CONNECTED_ENABLE
    WIRELESS_TRANS_CIG_MODE,
#endif
};
static u8 wireless_trans_tone_busy = 0;
static u32 input_number = 0;
static u16 input_number_timer = 0;
static void input_number_timeout(void *p)
{
    input_number_timer = 0;
    printf("input_number = %d\n", input_number);
    if (app_get_curr_task() == APP_MUSIC_TASK) {
        app_task_put_key_msg(KEY_MUSIC_PLAYE_BY_DEV_FILENUM, (int)input_number);
    }
    input_number = 0;
}
static void input_number_deal(u32 num)
{
    input_number = input_number * 10 + num;
    if (input_number > 9999) {
        input_number = num;
    }
    printf("num = %d, input_number = %d, input_number_timer = %d\n", num, input_number, input_number_timer);
    if (input_number_timer == 0) {
        input_number_timer = sys_timeout_add(NULL, input_number_timeout, 1000);
    } else {
        sys_timer_modify(input_number_timer, 1000);
    }
    UI_SHOW_MENU(MENU_FILENUM, 4 * 1000, input_number, NULL);
}

u8 is_wireless_trans_tone_busy(void)
{
    return wireless_trans_tone_busy;
}

static void  wireless_trans_tone_play_end_callback(void *priv, int flag)
{
    u32 index = (u32)priv;

    switch (index) {
    case IDEX_TONE_BT_COMMON:
    case IDEX_TONE_TWS_OPEN:
    case IDEX_TONE_BROADCAST_OPEN:
        wireless_trans_tone_busy = 0;
        break;
    default:
        break;
    }
}

static int app_wireless_trans_mode_close(u8 mode)
{
    int ret = 0;
    switch (mode) {
    case WIRELESS_TRANS_BT_MODE:
        break;
    case WIRELESS_TRANS_TWS_MODE:
        if (user_tws_en) {
            bt_tws_dynamic_onoff(0);
        }
        break;
    case WIRELESS_TRANS_BIG_MODE:
        if (le_broadcast_en) {
            app_broadcast_close(APP_BROADCAST_STATUS_STOP);
        }
        break;
    case WIRELESS_TRANS_CIG_MODE:
        if (le_connected_en) {
            app_connected_close_all(APP_CONNECTED_STATUS_STOP);
        }
        break;
    default:
        log_error("%s mode err", __FUNCTION__);
        ret = -EINVAL;
        break;
    }

    return ret;
}

int app_wireless_trans_mode_swtch_to(u8 switch_mode)
{
    int ret = 0;
    int wireless_trans_cur_mode = get_cur_wireless_trans_mode();

    if (app_get_curr_task() != APP_BT_TASK) {
        return -EPERM;
    }

    if (wireless_trans_cur_mode == switch_mode) {
        return -EPERM;
    }

    if ((app_get_curr_task() == APP_BT_TASK) &&
        (get_call_status() != BT_CALL_HANGUP)) {
        return -EPERM;
    }

    ret = app_wireless_trans_mode_close(wireless_trans_cur_mode);
    if (ret) {
        return ret;
    }

    switch (switch_mode) {
    case WIRELESS_TRANS_BT_MODE:
        wireless_trans_tone_busy = 1;
        tone_play_with_callback_by_name(tone_table[IDEX_TONE_BT_COMMON], 1, wireless_trans_tone_play_end_callback, (void *)IDEX_TONE_BT_COMMON);
        set_cur_wireless_trans_mode(WIRELESS_TRANS_BT_MODE);
        break;
    case WIRELESS_TRANS_TWS_MODE:
        if (user_tws_en) {
            wireless_trans_tone_busy = 1;
            tone_play_with_callback_by_name(tone_table[IDEX_TONE_TWS_OPEN], 1, wireless_trans_tone_play_end_callback, (void *)IDEX_TONE_TWS_OPEN);
            set_cur_wireless_trans_mode(WIRELESS_TRANS_TWS_MODE);
            bt_tws_dynamic_onoff(1);
        }
        break;
    case WIRELESS_TRANS_BIG_MODE:
        if (le_broadcast_en) {
            wireless_trans_tone_busy = 1;
            tone_play_with_callback_by_name(tone_table[IDEX_TONE_BROADCAST_OPEN], 1, wireless_trans_tone_play_end_callback, (void *)IDEX_TONE_BROADCAST_OPEN);
            set_cur_wireless_trans_mode(WIRELESS_TRANS_BIG_MODE);
            app_broadcast_open();
            app_broadcast_deal(BROADCAST_APP_MODE_ENTER);
        }
        break;
    case WIRELESS_TRANS_CIG_MODE:
        if (le_connected_en) {
            set_cur_wireless_trans_mode(WIRELESS_TRANS_CIG_MODE);
            app_connected_open(0);
            app_connected_deal(CONNECTED_APP_MODE_ENTER);
        }
        break;
    default:
        log_error("bt mode err");
        ret = -EINVAL;
        break;
    }

    /* if (!ret) { */
    /*     set_cur_wireless_trans_mode(switch_mode); */
    /* } */

    return ret;
}

static int app_wireless_trans_mode_swtch_next(void)
{
    int i = 0;

    if ((app_get_curr_task() == APP_BT_TASK) &&
        (get_call_status() != BT_CALL_HANGUP)) {
        return -EPERM;
    }

    for (i = 0; i < ARRAY_SIZE(wireless_trans_mode_list); i++) {
        if (get_cur_wireless_trans_mode() == wireless_trans_mode_list[i]) {
            break;
        }
    }

    if (++i >= ARRAY_SIZE(wireless_trans_mode_list)) {
        i = 0;
    }

    return app_wireless_trans_mode_swtch_to(wireless_trans_mode_list[i]);
}

int app_common_key_msg_deal(struct sys_event *event)
{
    int result = 0;
    int ret = false;
    struct key_event *key = &event->u.key;
    int key_event = event->u.key.event;
    int key_value = event->u.key.value;

    if (key_event == KEY_NULL) {
        return false;
    }

    if (key_is_ui_takeover()) {
        ui_key_msg_post(key_event);
        return false;
    }

#if (TCFG_UI_ENABLE && TCFG_APP_FM_EMITTER_EN)
    if (!ui_fm_emitter_common_key_msg(key_event)) {
        return false;
    }
#endif

    log_info("common_key_event:%d\n", key_event);

#if (RCSP_MODE)
    extern bool rcsp_key_event_filter_before(int key_event);
    if (rcsp_key_event_filter_before(key_event)) {
        return true;
    }
#endif

    switch (key_event) {
#if TCFG_APP_BT_EN

#if TCFG_USER_TWS_ENABLE
    case KEY_TWS_DISCONN:
        log_info("    KEY_TWS_DISCONN \n");
        if (((u32)event->arg == SYS_BT_EVENT_FROM_TWS)) {
            break;
        }
        bt_disconnect_tws_conn();
        break;
    case KEY_TWS_REMOVE_PAIR:
        log_info("    KEY_TWS_REMOVE_PAIR \n");
        bt_tws_remove_tws_pair();
        break;
    case KEY_TWS_SEARCH_PAIR:
        log_info("    KEY_TWS_SEARCH_PAIR \n");
        bt_tws_start_search_and_pair();
        break;
    case KEY_TWS_SEARCH_REMOVE_PAIR:
        log_info("    KEY_TWS_SEARCH_REMOVE_PAIR \n");
        bt_tws_search_or_remove_pair();
        break;
#endif

    case KEY_BT_DIRECT_INIT:
#if TCFG_UNISOUND_ENABLE
        if (!get_unisound_online()) {
            break;
        }
#if (TCFG_MIC_REC_ENABLE)
        if (!unisound_get_rec_file_init_ok()) {
#if (TCFG_RAW_DATA_EXPORT)
            result = unisound_multiple_mic_rec_init();
            if (result != 0) {
                break;
            }
#endif
#if (TCFG_SSP_DATA_EXPORT)
            if (ssp_data_export_init()) {
                log_error("ssp data export init err\n");
                break;
            }
#endif
        } else {
#if (TCFG_RAW_DATA_EXPORT)
            unisound_multiple_mic_rec_uninit();
#endif
#if (TCFG_SSP_DATA_EXPORT)
            ssp_data_export_uninit();
#endif
        }
#endif
        break;
#endif
        bt_direct_init();
        break;
    case KEY_BT_DIRECT_CLOSE:
        bt_direct_close();
        break;
#endif

    case  KEY_POWEROFF:
    case  KEY_POWEROFF_HOLD:
        power_off_deal(event, key_event - KEY_POWEROFF);
        break;

    case KEY_IR_NUM_0:
    case KEY_IR_NUM_1:
    case KEY_IR_NUM_2:
    case KEY_IR_NUM_3:
    case KEY_IR_NUM_4:
    case KEY_IR_NUM_5:
    case KEY_IR_NUM_6:
    case KEY_IR_NUM_7:
    case KEY_IR_NUM_8:
    case KEY_IR_NUM_9:
        input_number_deal(key_event - KEY_IR_NUM_0);
        break;
    /* void audio_tunning_test(u8 flag); */
    /* audio_tunning_test(key_event - KEY_IR_NUM_0); */
    /* break; */
    case KEY_CHANGE_MODE:
#if TCFG_MULTIPLE_MIC_REC_ENABLE
        extern u8 get_wfil_ok(void);
        extern int multiple_mic_rec_uninit();
        extern int multiple_mic_rec_init();
        if (get_wfil_ok()) {
            multiple_mic_rec_uninit();
        } else {
            multiple_mic_rec_init();
        }
        break;
#endif

#if TCFG_UNISOUND_ENABLE
        if (get_unisound_online()) {
            unisound_set_online(0);
        } else {
            unisound_set_online(1);
        }
        break;
#endif

#if TCFG_CONNECTED_ENABLE
        if (((u32)event->arg == KEY_EVENT_FROM_CIG)) {
            break;
        }
        if ((app_get_connected_role() & APP_CONNECTED_ROLE_RECEIVER) == APP_CONNECTED_ROLE_RECEIVER) {
            break;
        }
#endif

#if TCFG_USER_TWS_ENABLE
        if (((u32)event->arg == SYS_BT_EVENT_FROM_TWS)) {
            break;
        }
#endif

#if TWFG_APP_POWERON_IGNORE_DEV
        if ((timer_get_ms() - app_var.start_time) > TWFG_APP_POWERON_IGNORE_DEV)
#endif//TWFG_APP_POWERON_IGNORE_DEV

        {
            printf("KEY_CHANGE_MODE\n");
            app_task_switch_next();
        }
        break;

    case KEY_VOL_UP:
        log_info("COMMON KEY_VOL_UP\n");
        if (!tone_get_status()) {
            app_audio_volume_up(1);
            printf("common vol+: %d", app_audio_get_volume(APP_AUDIO_CURRENT_STATE));
        }
        if (app_audio_get_volume(APP_AUDIO_CURRENT_STATE) == app_audio_get_max_volume()) {
            if (tone_get_status() == 0) {
#if TCFG_MAX_VOL_PROMPT
                tone_play_by_path(tone_table[IDEX_TONE_MAX_VOL], 0);
#endif
            }
        }

#if (TCFG_DEC2TWS_ENABLE)
        bt_tws_sync_volume();
#endif
        UI_SHOW_MENU(MENU_MAIN_VOL, 1000, app_audio_get_volume(APP_AUDIO_CURRENT_STATE), NULL);
        break;

    case KEY_VOL_DOWN:
        log_info("COMMON KEY_VOL_DOWN\n");
        app_audio_volume_down(1);
        printf("common vol-: %d", app_audio_get_volume(APP_AUDIO_CURRENT_STATE));
#if (TCFG_DEC2TWS_ENABLE)
        bt_tws_sync_volume();
#endif
        UI_SHOW_MENU(MENU_MAIN_VOL, 1000, app_audio_get_volume(APP_AUDIO_CURRENT_STATE), NULL);
        break;

    case  KEY_EQ_MODE:
#if(TCFG_EQ_ENABLE == 1)
        eq_mode_sw();
#endif
        break;
#if AUDIO_OUTPUT_INCLUDE_BT


    case KEY_BT_EMITTER_RECEIVER_SW:
        printf(" KEY_BT_EMITTER_RECEIVER_SW\n");
        bt_emitter_receiver_sw();
        break;

    case KEY_BT_EMITTER_PAUSE:
        r_printf(" KEY_BT_EMITTER_PAUSE\n");
        bt_emitter_pp(0);
        break;

    case KEY_BT_EMITTER_PLAY:
        r_printf(" KEY_BT_EMITTER_PLAY\n");
        bt_emitter_pp(1);
        break;

    case KEY_BT_EMITTER_SW:
        r_printf("KEY_BT_EMITTER_SW\n");
        {
            extern u8 bt_emitter_stu_sw(void);

            if (bt_emitter_stu_sw()) {
                printf("bt emitter start \n");
            } else {
                printf("bt emitter stop \n");
            }
        }
        break;
#endif


#if (TCFG_CHARGE_BOX_ENABLE)
    case  KEY_BOX_POWER_CLICK:
    case  KEY_BOX_POWER_LONG:
    case  KEY_BOX_POWER_HOLD:
    case  KEY_BOX_POWER_UP:
    case  KEY_BOX_POWER_DOUBLE:
    case  KEY_BOX_POWER_THREE:
    case  KEY_BOX_POWER_FOUR:
    case  KEY_BOX_POWER_FIVE:
        charge_box_key_event_handler(key_event);
        break;
#endif
#if (TCFG_MIC_EFFECT_ENABLE)
    case KEY_REVERB_OPEN:
        if (mic_effect_get_status()) {
            mic_effect_stop();
        } else {
            printf("get_call_status() %d\n", get_call_status());
            if (get_call_status() == BT_CALL_INCOMING) {
                //来电响铃声过程，不允许打开混响，
                log_i("phone ringring, not alloc open mic_effect\n");
            } else {
                mic_effect_start();
            }
        }
        ret = true;
        break;
#endif  // #if (TCFG_MIC_EFFECT_ENABLE)

#if TCFG_LINEIN_ENABLE
    case KEY_LINEIN_START:
        printf("soundcard linein pp\n");
        extern int linein_volume_pp(void);
        linein_volume_pp();
        ret = true;
        break;
#endif // #if TCFG_LINEIN_ENABLE

    case KEY_ENC_START:
#if (RECORDER_MIX_EN)
        if (recorder_mix_get_status()) {
            g_printf("recorder_encode_stop\n");
            recorder_mix_stop();
        } else {
            g_printf("recorder_encode_start\n");
            recorder_mix_start();
        }
#endif/*RECORDER_MIX_EN*/
        break;

    case KEY_TONE_PLAY:
        /* extern void linein_tone_play(u8 index, u8 preemption); */
        //下面是叠加播放的
        tone_play_by_path(TONE_NORMAL, 0);   //播放一段正弦波
        /* tone_play_by_path(TONE_BT_CONN, 0); */  //播放一个文件提示音
        /* linein_tone_play(IDEX_TONE_NORMAL,0);//模拟linein模式下提示音的时候用这个接口,播正弦波 */
        /* linein_tone_play(IDEX_TONE_BT_CONN,0);//模拟linein模式下提示音的时候用这个接口 ,播提示音文件 */
        //下面是打断播放的
        /* tone_play_by_path(TONE_NORMAL, 1); */   //播放一段正弦波
        /* tone_play_by_path(TONE_BT_CONN, 1); */  //播放一个文件提示音
        /* linein_tone_play(IDEX_TONE_NORMAL,1);//模拟linein模式下播提示音的时候要用这个接口,播正弦波 */
        /* linein_tone_play(IDEX_TONE_BT_CONN,1);//模拟linein模式下播提示音的时候要用这个接口,播提示音文件 */
        break;
    case KEY_TEST_DEMO_0: {
#if defined(SOUND_TRACK_2_P_X_CH_CONFIG) &&SOUND_TRACK_2_P_X_CH_CONFIG
        extern void sound_track_bass_vol_test_demo(u8 up_down);
        puts("bass vol ++\n");
        sound_track_bass_vol_test_demo(1);//+
#endif/*SOUND_TRACK_2_P_X_CH_CONFIG*/
    }
    break;
    case KEY_TEST_DEMO_1: {
#if defined(SOUND_TRACK_2_P_X_CH_CONFIG) &&SOUND_TRACK_2_P_X_CH_CONFIG
        extern void sound_track_bass_vol_test_demo(u8 up_down);
        puts("bass vol --\n");
        sound_track_bass_vol_test_demo(0);//-
#endif/*SOUND_TRACK_2_P_X_CH_CONFIG*/

    }
    break;

    case KEY_WIRELESS_TRANS_MODE_SW:
        if (user_tws_en && ((u32)event->arg == SYS_BT_EVENT_FROM_TWS)) {
            break;
        }
        if (le_connected_en && ((u32)event->arg == KEY_EVENT_FROM_CIG)) {
            break;
        }
        app_wireless_trans_mode_swtch_next();
        break;

#if TCFG_BROADCAST_ENABLE
    case KEY_BROADCAST_SW:
        result = app_broadcast_switch();
        break;
    case KEY_WIRELESS_AUTH_ENTER:
        app_broadcast_enter_pair(0);
        break;
    case KEY_WIRELESS_AUTH_EXIT:
        app_broadcast_exit_pair(0);
        break;
#endif
#if TCFG_CONNECTED_ENABLE
    case KEY_CONNECTED_SW:
        if (((u32)event->arg == KEY_EVENT_FROM_CIG)) {
            break;
        }
        app_connected_switch();
        break;
    case KEY_WIRELESS_PAIR_INFO_REMOVE:
        log_info("KEY_WIRELESS_PAIR_INFO_REMOVE\n");
        app_connected_remove_pairs_addr();
        break;
    case KEY_CONNECTED_ROLE_SW:
        log_info("KEY_CONNECTED_ROLE_SW\n");
        app_connected_role_switch();
        break;

#endif

#if SYS_DIGVOL_GROUP_EN
    case KEY_DIG_GROUP_MIC_PP:
        if (audio_dig_vol_group_hdl_get(sys_digvol_group, "music_mic")) {
            static bool mic_pp = false;
            mic_pp = !mic_pp;
            u8 volume = mic_pp ? 0 : app_var.music_volume;
            printf("dig vol group set mic %d", volume);
            audio_dig_vol_group_vol_set(sys_digvol_group, "music_mic", AUDIO_DIG_VOL_ALL_CH, volume);
        }
        break;
    case KEY_DIG_GROUP_LINEIN_PP:
        if (audio_dig_vol_group_hdl_get(sys_digvol_group, "music_linein")) {
            static bool linein_pp = false;
            linein_pp = !linein_pp;
            u8 volume = linein_pp ? 0 : app_var.music_volume;
            printf("dig vol group set linein %d", volume);
            audio_dig_vol_group_vol_set(sys_digvol_group, "music_linein", AUDIO_DIG_VOL_ALL_CH, volume);
        }
        break;
    case KEY_DIG_GROUP_IIS_IN_PP:
        if (audio_dig_vol_group_hdl_get(sys_digvol_group, "music_iis_in")) {
            static bool iis_pp = false;
            iis_pp = !iis_pp;
            u8 volume = iis_pp ? 0 : app_var.music_volume;
            printf("dig vol group set iis_in %d", volume);
            audio_dig_vol_group_vol_set(sys_digvol_group, "music_iis_in", AUDIO_DIG_VOL_ALL_CH, volume);
        }
        break;
    case KEY_DIG_GROUP_PC_PP:
        if (audio_dig_vol_group_hdl_get(sys_digvol_group, "music_pc")) {
            static bool pc_pp = false;
            pc_pp = !pc_pp;
            u8 volume = pc_pp ? 0 : app_var.music_volume;
            printf("dig vol group set pc %d", volume);
            audio_dig_vol_group_vol_set(sys_digvol_group, "music_pc", AUDIO_DIG_VOL_ALL_CH, volume);
        }
        break;
    case KEY_DIG_GROUP_LIVE_PLAYER_PP:
        if (audio_dig_vol_group_hdl_get(sys_digvol_group, "music_live_player")) {
            static bool live_paler_pp = false;
            live_paler_pp = !live_paler_pp;
            u8 volume = live_paler_pp ? 0 : app_var.music_volume;
            printf("dig vol group set music_live_player %d", volume);
            audio_dig_vol_group_vol_set(sys_digvol_group, "music_live_player", AUDIO_DIG_VOL_ALL_CH, volume);
        }
        break;
    case KEY_DIG_GROUP_LIVE_CAPTURE_PP:
        if (audio_dig_vol_group_hdl_get(sys_digvol_group, "music_live_capture")) {
            static bool live_capture_pp = false;
            live_capture_pp = !live_capture_pp;
            u8 volume = live_capture_pp ? 0 : app_var.music_volume;
            printf("dig vol group set music_live_capture %d", volume);
            audio_dig_vol_group_vol_set(sys_digvol_group, "music_live_capture", AUDIO_DIG_VOL_ALL_CH, volume);
        }
        break;
#endif

#if WIRELESS_2T1_DUPLEX_EN
    case KEY_MULTI_CAPTURE_MIC_PP:
        static u8 on_off = 1;
        on_off = on_off ? 0 : 1;
        extern void mic_multiple_capture_play_pause(void *capture, u8 on_off);
        mic_multiple_capture_play_pause(NULL, on_off);
        break;
#endif


#if AUDIO_VOCAL_REMOVE_EN
    case KEY_VOCAL_REMOVE_SWITCH:
        u8 vocal_remove_on_off = 1;
        ret = syscfg_read(CFG_VOCAL_REMOVE_SW, &vocal_remove_on_off, sizeof(vocal_remove_on_off));
        if (ret < 0) {
            vocal_remove_on_off = 1;
        }
        vocal_remove_on_off = vocal_remove_on_off ? 0 : 1;
        syscfg_write(CFG_VOCAL_REMOVE_SW, &vocal_remove_on_off, sizeof(vocal_remove_on_off));
        extern int vocal_remove_switch(u8 sw);
        printf("KEY_VOCAL_REMOVE_SWITCH %d", vocal_remove_on_off ? 0 : 1);
        vocal_remove_switch(vocal_remove_on_off);
        break;
#endif

#if TCFG_MIC_EFFECT_ENABLE

    case KEY_MIC_EFFECT_REVERB_ONOFF:
        printf("KEY_MIC_EFFECT_REVERB_ONOFF");
        if (mic_effect_get_status()) {
            static u8 on_off = 1;
            on_off = on_off ? 0 : 1;
            printf("KEY_MIC_EFFECT_REVERB_ONOFF %d", on_off ? 0 : 1);
            mic_effect_plate_reverb_bypass(on_off);
#if WIRELESS_MIC_EFFECT_ENABLE
            live_mic_plate_reverb_bypass(on_off);
#endif
        }
        break;


    case KEY_MIC_EFFECT_ECHO_ONOFF:
        printf("KEY_MIC_EFFECT_ECHO_ONOFF");
        if (mic_effect_get_status()) {
            static u8 on_off = 1;
            on_off = on_off ? 0 : 1;
            printf("KEY_MIC_EFFECT_ECHO_ONOFF %d", on_off ? 0 : 1);
            mic_effect_echo_bypass(on_off);
#if WIRELESS_MIC_EFFECT_ENABLE
            live_mic_echo_bypass(on_off);
#endif
        }
        break;

    case KEY_MIC_EFFECT_HOWLING_ONOFF:
        printf("KEY_MIC_EFFECT_HOWLING_ONOFF");
        if (mic_effect_get_status()) {
            static u8 on_off = 1;
            on_off = on_off ? 0 : 1;
            printf("KEY_MIC_EFFECT_HOWLING_ONOFF %d", on_off ? 0 : 1);
            mic_effect_howling_pitch_shift_bypass(on_off);
            mic_effect_notchhowline_bypass(on_off);
#if WIRELESS_MIC_EFFECT_ENABLE
            live_mic_howling_pitch_shift_bypass(on_off);
            live_mic_notchhowline_bypass(on_off);
#endif
        }
        break;


#if MIC_EFFECT_LLNS
    case KEY_MIC_EFFECT_LLNS_ONOFF:
        printf("KEY_MIC_EFFECT_LLNS_ONOFF");
        if (mic_effect_get_status()) {
            static u8 on_off = 0;
            on_off = on_off ? 0 : 1;
            printf("KEY_MIC_EFFECT_LLNS_ONOFF %d", on_off);
#if WIRELESS_MIC_EFFECT_ENABLE
            live_mic_llns_bypass(on_off);
#endif
        }
        break;
#endif

#if TCFG_MIC_VOICE_CHANGER_ENABLE
    case KEY_MIC_EFFECT_VOICE_CHANGER_LOOP:
        mic_voicechange_loop();
        break;
    case KEY_MIC_EFFECT_VOICE_CHANGER_ONOFF:
        printf("KEY_MIC_EFFECT_VOICE_CHANGER_ONOFF");
        if (mic_effect_get_status()) {
            static u8 on_off = 1;
            on_off = on_off ? 0 : 1;
            printf("KEY_MIC_EFFECT_VOICE_CHANGER_ONOFF %d", on_off ? 0 : 1);
            mic_effect_voice_changer_bypass(on_off);
#if WIRELESS_MIC_EFFECT_ENABLE
            live_mic_voice_changer_bypass(on_off);
#endif
        }
        break;
#endif

#endif

#if AUDIO_VBASS_CONFIG
    case KEY_AUDIO_VBASS_ONOFF:
        u8 vbass_on_off = 1;
        ret = syscfg_read(CFG_AUDIO_VBASS_SW, &vbass_on_off, sizeof(vbass_on_off));
        if (ret < 0) {
            vbass_on_off = 1;
        }
        vbass_on_off = vbass_on_off ? 0 : 1;
        syscfg_write(CFG_AUDIO_VBASS_SW, &vbass_on_off, sizeof(vbass_on_off));
        extern void audio_vbass_set_bypass(u32 vbass_name, u32 bypass);
        printf("KEY_AUDIO_VBASS_ONOFF %d", vbass_on_off ? 0 : 1);
        audio_vbass_set_bypass(AEID_MUSIC_VBASS, vbass_on_off);
        break;
#endif

    default:
        ui_key_msg_post(key_event);
        ui_simple_key_msg_post(key_event, key_value);
#if 0 //(SOUNDCARD_ENABLE)
        soundcard_event_deal(event);
#endif
        break;

    }
#if (RCSP_MODE)
    extern int rcsp_common_key_event_deal(int key_event, int ret);
    ret = rcsp_common_key_event_deal(key_event, ret);
#endif


    return ret;
}

int app_power_user_event_handler(struct device_event *dev)
{
#if(TCFG_SYS_LVD_EN == 1)
    switch (dev->event) {
    case POWER_EVENT_POWER_WARNING:
        ui_update_status(STATUS_LOWPOWER);
        tone_play_by_path(tone_table[IDEX_TONE_LOW_POWER], 1);
        /* return 0; */
    }
#if 0 //(SOUNDCARD_ENABLE)
    soundcard_power_event(dev);
#endif
#endif
    return app_power_event_handler(dev);
}

static void app_common_device_event_handler(struct sys_event *event)
{
    int ret = 0;
    const char *logo = NULL;
    const char *usb_msg = NULL;
    u8 app  = 0xff ;
    u8 alarm_flag = 0;
    switch ((u32)event->arg) {
#if (TCFG_CFG_TOOL_ENABLE || TCFG_ONLINE_ENABLE)
    case DEVICE_EVENT_FROM_CFG_TOOL:
        extern int app_cfg_tool_event_handler(struct cfg_tool_event * cfg_tool_dev);
        app_cfg_tool_event_handler(&event->u.cfg_tool);
        break;
#endif
#if TCFG_CHARGE_ENABLE
    case DEVICE_EVENT_FROM_CHARGE:
        app_charge_event_handler(&event->u.dev);
        break;
#endif//TCFG_CHARGE_ENABLE

#if (TCFG_ONLINE_ENABLE || TCFG_CFG_TOOL_ENABLE)
    case DEVICE_EVENT_FROM_CI_UART:
        ci_data_rx_handler(CI_UART);
        break;

#if TCFG_USER_TWS_ENABLE
    case DEVICE_EVENT_FROM_CI_TWS:
        ci_data_rx_handler(CI_TWS);
        break;
#endif//TCFG_USER_TWS_ENABLE
#endif//TCFG_ONLINE_ENABLE

    case DEVICE_EVENT_FROM_POWER:
        app_power_user_event_handler(&event->u.dev);
        break;

#if TCFG_CHARGESTORE_ENABLE || TCFG_TEST_BOX_ENABLE
    case DEVICE_EVENT_CHARGE_STORE:
        app_chargestore_event_handler(&event->u.chargestore);
        break;
#endif//TCFG_CHARGESTORE_ENABLE || TCFG_TEST_BOX_ENABLE

#if(TCFG_CHARGE_BOX_ENABLE)
    case DEVICE_EVENT_FROM_CHARGEBOX:
        charge_box_ctrl_event_handler(&event->u.chargebox);
        break;
#endif


    case DEVICE_EVENT_FROM_OTG:
        ///先分析OTG设备类型
        usb_msg = (const char *)event->u.dev.value;
        if (usb_msg[0] == 's') {
            ///是从机
#if (SOUNDCARD_ENABLE || MULTI_AUDIO_UPLOAD_TO_UAC_ENABLE)
            ret = pc_device_event_handler(event);
            printf("======================== ret %d\n", ret);
            // 在蓝牙初始化完成前不能枚举usb设备
            switch (event->u.dev.event) {
            case DEVICE_EVENT_IN:
                clock_idle(PC_IDLE_CLOCK);
                if (connected_2t1_duplex && ((connected_role_config == CONNECTED_ROLE_CENTRAL) ||
                                             ((connected_role_config == CONNECTED_ROLE_PERIP) && CIG_PERIP_UAC_ENABLE))) {
                    printf("usb capture open\n");
                    set_cig_audio_dev_online(LIVE_USB_CAPTURE_MODE);
                    cis_audio_multi_capture_open(LIVE_USB_CAPTURE_MODE);
                }
                break;
            case DEVICE_EVENT_OUT:   // usb 拔出
                if (connected_2t1_duplex && ((connected_role_config == CONNECTED_ROLE_CENTRAL) ||
                                             ((connected_role_config == CONNECTED_ROLE_PERIP) && CIG_PERIP_UAC_ENABLE))) {
                    printf("usb capture close\n");
                    set_cig_audio_dev_offline(LIVE_USB_CAPTURE_MODE);
                    cis_audio_multi_capture_close(LIVE_USB_CAPTURE_MODE);
                }
                break;
            }
#else
#if (TCFG_PC_ENABLE || TCFG_USB_CDC_BACKGROUND_RUN)
            ret = pc_device_event_handler(event);
            if (ret == 1) {
                app = APP_PC_TASK;
            }
#endif
#endif
            break;
        } else if (usb_msg[0] == 'h') {
            ///是主机, 统一于SD卡等响应主机处理，这里不break
        } else {
            log_e("unknow otg devcie !!!\n");
            break;
        }
    case DRIVER_EVENT_FROM_SD0:
    case DRIVER_EVENT_FROM_SD1:
    case DRIVER_EVENT_FROM_SD2:
#if TCFG_APP_MUSIC_EN
        ret = dev_status_event_filter(event);///解码设备上下线， 设备挂载等处理
        if (ret == true) {
            if (event->u.dev.event == DEVICE_EVENT_IN) {
                ///设备上线， 非解码模式切换到解码模式播放
                if (app_get_curr_task() != APP_MUSIC_TASK) {
                    app = APP_MUSIC_TASK;
                }
            }
            if (event->u.dev.event == DEVICE_EVENT_OUT) {
                ///设备上线， 非解码模式切换到解码模式播放
                if (app_get_curr_task() == APP_MUSIC_TASK) {
                    if (dev_manager_get_total(1) == 0) {
                        app_status_handler(APP_STATUS_MUSIC_QUIT);
                        app_task_put_key_msg(KEY_MUSIC_PLAYER_QUIT, 0);///没有设备在线， 退出音乐模式
                    } else {
                        app_task_put_key_msg(KEY_MUSIC_AUTO_NEXT_DEV, 0);///切换设备
                    }
                }
            }
        }
#endif
        break;

#if TCFG_LINEIN_ENABLE
    case DEVICE_EVENT_FROM_LINEIN:
        ret = linein_device_event_handler(event);
        if (ret == true) {
            app = APP_LINEIN_TASK;
#if TCFG_LINEIN_ENERGY_DETECT
            app_task_switch_to(app);
            return;
#endif
        }
        break;
#endif//TCFG_LINEIN_ENABLE

#if TCFG_APP_RTC_EN
    case DEVICE_EVENT_FROM_ALM:
        ret = alarm_sys_event_handler(event);
#if	(defined(SMARTBOX_ALARM_EX) && (SMARTBOX_ALARM_EX))
        extern void rtc_ring_sw_deal();
        rtc_ring_sw_deal();
#endif
        if (ret == true) {
            alarm_flag = 1;
            app = APP_RTC_TASK;
        }
        break;
#endif//TCFG_APP_RTC_EN

    default:
        /* printf("unknow SYS_DEVICE_EVENT!!, %x\n", (u32)event->arg); */
        break;
    }

#if (RCSP_MODE)
    extern void rcsp_update_dev_state(u32 event);
    rcsp_update_dev_state((u32)event->arg);
#endif

    if (app != 0xff) {
        //PC 不响应因为设备上线引发的模式切换
        if ((true != app_check_curr_task(APP_PC_TASK)) || alarm_flag) {
            //闹钟响起直接切到rtc模式
            if (alarm_flag) {
                app_task_switch_to(app);
                return;
            }

#if (TCFG_CHARGE_ENABLE && (!TCFG_CHARGE_POWERON_ENABLE))
            extern u8 get_charge_online_flag(void);
            if (get_charge_online_flag()) {
                return;
            }
#endif

#if TWFG_APP_POWERON_IGNORE_DEV
            if ((timer_get_ms() - app_var.start_time) > TWFG_APP_POWERON_IGNORE_DEV)
#endif//TWFG_APP_POWERON_IGNORE_DEV
            {
#if defined(TCFG_CONNECTED_ENABLE) && (TCFG_CONNECTED_ENABLE)
                if ((app_get_connected_role() & APP_CONNECTED_ROLE_RECEIVER) != APP_CONNECTED_ROLE_RECEIVER) {
                    app_task_switch_to(app);
                }
#else
                app_task_switch_to(app);
#endif
            } else {
#if TCFG_PC_ENABLE
                if (app == APP_PC_TASK && get_usb_detect_mode() == 0) {
                    //如果要切入PC模式，但是又因为开机时间小于
                    //TWFG_APP_POWERON_IGNORE_DEV导致不切模式，且当usb检测方式
                    //是DP唤醒检测，需要恢复DP唤醒检测使能，否则插拔检测失效
                    usb_offline_check_without_otg_disable(get_usb_check_wkup_index());
                }
#endif
            }
        }
    }
}

static void wireless_dev_connction_status_event_handler(struct sys_event *event)
{
    switch ((u32)event->arg) {
    case SYS_BT_EVENT_FROM_BIG:
#if TCFG_BROADCAST_ENABLE
        app_broadcast_conn_status_event_handler(&event->u.wireless_trans);
#endif
        break;
    case SYS_BT_EVENT_FROM_CIG:
#if TCFG_CONNECTED_ENABLE
        app_connected_conn_status_event_handler(&event->u.wireless_trans);
#endif
        break;
    default:
        break;
    }
}
/* SYS_EVENT_HANDLER(SYS_BT_EVENT, wireless_dev_connction_status_event_handler, 3); */

///公共事件处理， 各自模式没有处理的事件， 会统一在这里处理
void app_default_event_deal(struct sys_event *event)
{
    int ret;
    struct key_event *key;
    SYS_EVENT_HANDLER_SPECIFIC(event);
    switch (event->type) {
    case SYS_DEVICE_EVENT:
        /*默认公共设备事件处理*/
        /* printf(">>>>>>>>>>>>>%s %d \n", __FUNCTION__, __LINE__); */
        app_common_device_event_handler(event);
        break;
#if TCFG_APP_BT_EN
    case SYS_BT_EVENT:
        if (true != app_check_curr_task(APP_BT_TASK)) {
            /*默认公共BT事件处理*/
            bt_background_event_handler(event);
        }
#if (TCFG_BROADCAST_ENABLE || TCFG_CONNECTED_ENABLE)
        wireless_dev_connction_status_event_handler(event);
#endif
        break;
#endif
    case SYS_KEY_EVENT:
        key = &event->u.key;
        if (key->value == KEY_DRIVER_TYPE_VOICE) {
            /* 语音消息 */
            ret = jl_kws_voice_event_handle(event);
            break;
        }
        app_common_key_msg_deal(event);
        break;
    default:
        printf("unknow event\n");
        break;
    }
}

#if 0
extern int key_event_remap(struct sys_event *e);
extern const u16 bt_key_ad_table[KEY_AD_NUM_MAX][KEY_EVENT_MAX];
u8 app_common_key_var_2_event(u32 key_var)
{
    u8 key_event = 0;
    u8 key_value = 0;
    struct sys_event e = {0};
#if TCFG_ADKEY_ENABLE
    for (; key_value < KEY_AD_NUM_MAX; key_value++) {
        for (key_event = 0; key_event < KEY_EVENT_MAX; key_event++) {
            if (bt_key_ad_table[key_value][key_event] == key_var) {
                e.type = SYS_KEY_EVENT;
                e.u.key.type = KEY_DRIVER_TYPE_AD;
                e.u.key.event = key_event;
                e.u.key.value = key_value;
                /* e.u.key.tmr = timer_get_ms(); */
                e.arg  = (void *)DEVICE_EVENT_FROM_KEY;
                /* printf("key2event:%d %d %d\n", key_var, key_value, key_event); */
                if (key_event_remap(&e)) {
                    sys_event_notify(&e);
                    return true;
                }
            }
        }
    }
#endif
    return false;
}

#else

u8 app_common_key_var_2_event(u32 key_var)
{
    struct sys_event e = {0};
    e.type = SYS_KEY_EVENT;
    e.u.key.type = KEY_DRIVER_TYPE_SOFTKEY;
    e.u.key.event = key_var;
    e.arg  = (void *)DEVICE_EVENT_FROM_KEY;
    sys_event_notify(&e);
    return true;
}
#endif

