#include "system/includes.h"
#include "app_config.h"
#include "app_task.h"
#include "media/includes.h"
#include "asm/audio_adc.h"
#include "key_event_deal.h"
#include "user_cfg.h"
#include "clock_cfg.h"
#include "app_main.h"
#include "audio_config.h"
#include "audio_enc/audio_enc.h"
#include "tone_player.h"
#ifndef CONFIG_MEDIA_NEW_ENABLE
#include "application/audio_output_dac.h"
#endif

#include "app_connected.h"
#include "connected_api.h"
#include "app_broadcast.h"
#include "broadcast_api.h"
#include "sound_device_driver.h"
#include "audio_mode.h"
#include "audio_dec_mic.h"
/*************************************************************
   此文件函数主要是app mic实现api
**************************************************************/

#if TCFG_APP_LIVE_MIC_EN

#define _debug  printf
#define log_info r_printf

struct mic_opr {
    void *rec_dev;
    u8	volume;
    u8 onoff;
    u8 audio_state;
    void *capture;
};
static struct mic_opr app_mic_hdl = {0};
#define __this 	(&app_mic_hdl)


void mic_start(u8 source, u32 sample_rate, u8 gain)
{
    mic_dec_open(source, sample_rate, gain);
    __this->onoff = 1;
}

void mic_stop(void)
{
    mic_dec_close();
    __this->onoff = 0;
}

#if (TCFG_BROADCAST_ENABLE || TCFG_CONNECTED_ENABLE)
u8 live_mic_get_status(void);
static void *wireless_mic_capture_start(struct audio_path *path);
static void wireless_mic_capture_stop(void *capture);
static void wireless_mic_capture_play_pause(void);
static void *wireless_mic_play_start(struct audio_path *path);
static void wireless_mic_play_stop(void *player);

static struct live_audio_mode_ops wireless_mic_mode_ops = {
    .capture_open = wireless_mic_capture_start,
    .capture_close = wireless_mic_capture_stop,
    .play_open = wireless_mic_play_start,
    .play_close = wireless_mic_play_stop,
    .play_status = live_mic_get_status,
};

void mic_wireless_audio_codec_interface_register(void)
{
    live_audio_mode_setup(LIVE_MIC_CAPTURE_MODE, 48000, &wireless_mic_mode_ops);
}

static void *wireless_mic_play_start(struct audio_path *path)
{
    mic_start(TCFG_AUDIO_ADC_MIC_CHA, JLA_CODING_SAMPLERATE, 6);
    return __this;
}

static void wireless_mic_play_stop(void *player)
{
    mic_stop();
}

u8 live_mic_get_status(void)
{
    if (__this->onoff) {
        return LOCAL_AUDIO_PLAYER_STATUS_PLAY;
    } else {
        return LOCAL_AUDIO_PLAYER_STATUS_STOP;
    }
}

static void *wireless_mic_capture_start(struct audio_path *path)
{
    if (__this->onoff == 1) {
        log_info("mic is aleady start\n");
        return __this->capture;
    }

#if (TCFG_LINEIN_INPUT_WAY == LINEIN_INPUT_WAY_ADC)
    __this->audio_state = APP_AUDIO_STATE_MUSIC;
#elif (TCFG_LINEIN_INPUT_WAY == LINEIN_INPUT_WAY_ANALOG)
    __this->audio_state = APP_AUDIO_STATE_LINEIN;
#elif (TCFG_LINEIN_INPUT_WAY == LINEIN_INPUT_WAY_DAC)
    __this->audio_state = APP_AUDIO_STATE_LINEIN;
#endif

    if (tone_get_status() == 0) {
        app_audio_state_switch(__this->audio_state, get_max_sys_vol());
    }

    __this->capture = live_mic_capture_open(path);
    __this->volume = app_audio_get_volume(__this->audio_state);
    __this->onoff = 1;
    live_mic_capture_play_pause(__this->capture, __this->onoff);

    return __this->capture;
}

static void wireless_mic_capture_stop(void *capture)
{
    live_mic_capture_close(capture);
    __this->capture = NULL;
    app_audio_set_volume(APP_AUDIO_STATE_MUSIC, __this->volume, 1);
    __this->onoff = 0;
}

static void wireless_mic_capture_play_pause(void)
{
    if (__this->onoff == 0) {
        __this->onoff = 1;
        live_mic_capture_play_pause(__this->capture, __this->onoff);
#if TCFG_BROADCAST_ENABLE
        app_broadcast_deal(BROADCAST_MUSIC_START);
#endif
#if TCFG_CONNECTED_ENABLE
        app_connected_deal(CONNECTED_MUSIC_START);
#endif
    } else {
        /* wireless_mic_capture_stop(); */
        __this->onoff = 0;
        live_mic_capture_play_pause(__this->capture, __this->onoff);
#if TCFG_BROADCAST_ENABLE
        app_broadcast_deal(BROADCAST_MUSIC_STOP);
#endif
#if TCFG_CONNECTED_ENABLE
        app_connected_deal(CONNECTED_MUSIC_STOP);
#endif
    }
}

#endif  //#if (TCFG_BROADCAST_ENABLE || TCFG_CONNECTED_ENABLE)
#endif  /* TCFG_BC_MIC_EN */


