/*************************************************************************************************/
/*!
*  \file      audio_mode.h
*
*  \brief
*
*  Copyright (c) 2011-2022 ZhuHai Jieli Technology Co.,Ltd.
*
*/
/*************************************************************************************************/
#ifndef _LIVE_AUDIO_MODE_H_
#define _LIVE_AUDIO_MODE_H_
#include "live_audio.h"

enum live_audio_capture_mode {
    LIVE_A2DP_CAPTURE_MODE,
    LIVE_AUX_CAPTURE_MODE,
    LIVE_MIC_CAPTURE_MODE,
    LIVE_FILE_CAPTURE_MODE,
    LIVE_IIS_CAPTURE_MODE,
    LIVE_USB_CAPTURE_MODE,
    LIVE_REMOTE_DEV0_CAPTURE_MODE,
    LIVE_REMOTE_DEV1_CAPTURE_MODE,
    LIVE_AUDIO_CAPTURE_MAX_MODE,
};

/*! \brief 本地音源播放器状态 */
enum {
    LOCAL_AUDIO_PLAYER_STATUS_ERR,
    LOCAL_AUDIO_PLAYER_STATUS_PLAY,
    LOCAL_AUDIO_PLAYER_STATUS_STOP,
};

int live_audio_mode_setup(u8 mode, u32 sample_rate, struct live_audio_mode_ops *ops);

int live_audio_mode_play_stop(u8 mode);

int live_audio_mode_play_start(u8 mode);

int live_audio_mode_play_status(u8 mode);

int live_audio_mode_get_capture_params(u8 mode, struct audio_path *path);

int live_audio_mode_get_broadcast_params(u8 mode, struct audio_path *path);

int live_audio_mode_get_cis_params(u8 mode, struct audio_path *path);
#endif
