/*************************************************************************************************/
/*!
*  \file      live_audio_buffer.h
*
*  \brief
*
*  Copyright (c) 2011-2022 ZhuHai Jieli Technology Co.,Ltd.
*
*/
/*************************************************************************************************/
#ifndef _LIVE_AUDIO_BUFFER_H_
#define _LIVE_AUDIO_BUFFER_H_
#include "audio_stream.h"

struct live_audio_buffer_params {
    void *overrun_wakeup_data;
    void *underrun_wakeup_data;
    void (*overrun_wakeup)(void *);
    void (*underrun_wakeup)(void *);
    int size;
};

void *live_audio_buffer_init(struct live_audio_buffer_params *params);

void live_audio_buffer_close(void *audio_buffer);

int live_audio_buffer_read(void *audio_buffer, void *data, int len);

int live_audio_buffer_read_frame(void *audio_buffer, struct audio_frame *frame, int len);

int live_audio_buffer_push_timestamp(void *audio_buffer, u32 timestamp);

int live_audio_buffer_write(void *audio_buffer, void *data, int len);

int live_audio_buffer_push_frame(void *audio_buffer, struct audio_frame *audio_frame);

struct audio_frame *live_audio_buffer_pull_frame(void *audio_buffer);

struct audio_frame *live_audio_buffer_pop_frame(void *audio_buffer);

void live_audio_buffer_free_frame(void *audio_buffer, struct audio_frame *audio_frame);

int live_audio_buffer_len(void *audio_buffer);
#endif
