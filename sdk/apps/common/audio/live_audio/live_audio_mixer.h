/*************************************************************************************************/
/*!
*  \file      live_audio_mixer.h
*
*  \brief
*
*  Copyright (c) 2011-2023 ZhuHai Jieli Technology Co.,Ltd.
*
*/
/*************************************************************************************************/

#ifndef _LIVE_AUDIO_MIXER_H_
#define _LIVE_AUDIO_MIXER_H_
#include "audio_stream.h"

enum {
    AUDIO_MIXER_EVENT_CH_OPEN,
    AUDIO_MIXER_EVENT_CH_CLOSE,
    AUDIO_MIXER_EVENT_CH_START,
    AUDIO_MIXER_EVENT_CH_STOP,
};

struct live_audio_mixer_config {
    u8 nch;
    int frame_size;
    int sample_rate;
    void *opath;
    int (*write_frame)(void *, struct audio_frame *);
};

struct live_audio_mixer_ch_params {
    u8 nch;
    u8 ch_map[4];
    void *wakeup_data;
    void (*wakeup)(void *priv);
    void *priv;
    void (*event_handler)(void *priv, int event);
};

int live_audio_mixer_register(const char *name, struct live_audio_mixer_config *config);

int live_audio_mixer_unregister(const char *name);

void *live_audio_mix_ch_open(const char *name, struct live_audio_mixer_ch_params *params);

int live_audio_mix_ch_write_frame(void *mix_ch, struct audio_frame *frame);

int live_audio_mix_ch_stop(void *mix_ch);

void live_audio_mixer_wakeup(void *mixer_name);

int live_audio_mix_ch_close(void *mix_ch);

int live_audio_mixer_sample_rate(void *mix_ch);

void live_audio_mixer_use_pcm_frames(void *mix_ch, int frames);

int live_audio_buffered_frames_from_mixer(void *mix_ch);

void live_audio_mix_ch_debug(void *mix_ch);
#endif
