/*************************************************************************************************/
/*!
*  \file      live_audio_capture.h
*
*  \brief
*
*  Copyright (c) 2011-2022 ZhuHai Jieli Technology Co.,Ltd.
*
*/
/*************************************************************************************************/
#ifndef _LIVE_AUDIO_CAPTURE_H_
#define _LIVE_AUDIO_CAPTURE_H_
#include "live_audio.h"

void *live_audio_capture_open(struct audio_path *ipath, struct audio_path *opath);

void *live_audio_capture_dual_fmt_open(struct audio_path *ipath, struct audio_path *opath);

void live_audio_capture_close(void *capture);

int live_audio_capture_add_path(void *capture, struct audio_path *opath);

int live_audio_capture_buffer_len(void *capture);

int live_audio_capture_send_update(void *capture, int time, u32 send_timestamp);

int live_audio_capture_read_data(void *capture, void *buf, int len);

void live_audio_capture_send_debug(void *capture);

int live_audio_capture_read_pcm_data(void *capture, void *data, int len);
#endif
