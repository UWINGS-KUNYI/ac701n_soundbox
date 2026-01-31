/*************************************************************************************************/
/*!
*  \file      live_audio_effect.h
*
*  \brief
*
*  Copyright (c) 2011-2022 ZhuHai Jieli Technology Co.,Ltd.
*
*/
/*************************************************************************************************/
#ifndef _LIVE_AUDIO_EFFECT_H_
#define _LIVE_AUDIO_EFFECT_H_

struct live_audio_effect_params {
    u8 nch;
    u8 bit_width;
    int sample_rate;
    int (*write_frame)(void *path, struct audio_frame *frame);
    void *opath;
    void (*underrun_wakeup)(void *stream);
    void *underrun_wakeup_data;
    void *overrun_wakeup_data;
    void (*overrun_wakeup)(void *);
};

void *live_audio_effect_open(struct live_audio_effect_params *params);

void live_audio_effect_close(void *effect);

int live_audio_effect_write_frame(void *effect, struct audio_frame *frame);

void live_audio_effect_wakeup(void *effect);
#endif
