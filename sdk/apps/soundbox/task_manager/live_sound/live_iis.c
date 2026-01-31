/*************************************************************************************************/
/*!
*  \file      live_iis.c
*
*  \brief
*
*  Copyright (c) 2011-2022 ZhuHai Jieli Technology Co.,Ltd.
*
*/
/*************************************************************************************************/
#include "system/includes.h"
#include "app_config.h"
#include "app_task.h"
#include "media/includes.h"
#include "key_event_deal.h"
#include "user_cfg.h"
#include "clock_cfg.h"
#include "app_main.h"
#include "audio_config.h"
#include "audio_dec.h"
#include "audio_link.h"
#include "app_connected.h"
#include "connected_api.h"
#include "app_broadcast.h"
#include "broadcast_api.h"
#include "sound_device_driver.h"
#include "audio_mode.h"
#if ((defined TCFG_APP_LIVE_IIS_EN) && TCFG_APP_LIVE_IIS_EN)

struct live_iis_playback_context {
    u8 state;
    void *capture;
    void *hw_alink;
    void *alink_ch;
    void *priv;
};

static struct live_iis_playback_context g_live_iis;
#define __this 	(&g_live_iis)

extern ALINK_PARM alink0_platform_data;

int live_iis_playback_start(void)
{
    iis_in_dec_open(TCFG_IIS_SR);
    __this->state = 1;
    return 0;
}

void live_iis_playback_stop(void)
{
    iis_in_dec_close();
    __this->state = 0;
}

#if (TCFG_BROADCAST_ENABLE || TCFG_CONNECTED_ENABLE)
static u8 live_iis_get_status(void);
static void *wireless_iis_play_start(struct audio_path *path);
static void wireless_iis_play_stop(void *player);
static int wireless_iis_capture_start(struct audio_path *path);
static void wireless_iis_capture_stop(void *capture);

static struct live_audio_mode_ops wireless_iis_mode_ops = {
    .capture_open = wireless_iis_capture_start,
    .capture_close = wireless_iis_capture_stop,
    .play_open = wireless_iis_play_start,
    .play_close = wireless_iis_play_stop,
    .play_status = live_iis_get_status,
};

void live_iis_wireless_audio_codec_interface_register(void)
{
    live_audio_mode_setup(LIVE_IIS_CAPTURE_MODE, TCFG_IIS_SR, &wireless_iis_mode_ops);
}

static u8 live_iis_get_status(void)
{
    if (__this->state == 1) {
        return LOCAL_AUDIO_PLAYER_STATUS_PLAY;
    }

    return LOCAL_AUDIO_PLAYER_STATUS_STOP;
}

static void *wireless_iis_play_start(struct audio_path *path)
{
    live_iis_playback_start();
    return __this;
}

static void wireless_iis_play_stop(void *player)
{
    live_iis_playback_stop();
}

static int wireless_iis_capture_start(struct audio_path *path)
{
    __this->capture = sound_iis_capture_open(path);
    __this->state = 1;

    return __this->capture;
}

static void wireless_iis_capture_stop(void *capture)
{
    sound_iis_capture_close(capture);
    __this->capture = NULL;
    __this->state = 0;
}

#endif  /* TCFG_BROADCAST_ENABLE */
#endif /* TCFG_APP_IIS_LIVE_EN */

