/*************************************************************************************************/
/*!
*  \file      pcm_capture_buffer.h
*
*  \brief
*
*  Copyright (c) 2011-2023 ZhuHai Jieli Technology Co.,Ltd.
*
*/
/*************************************************************************************************/
#ifndef _LIVE_AUDIO_PCM_CAPTURE_BUFFER_H_
#define _LIVE_AUDIO_PCM_CAPTURE_BUFFER_H_
#include "live_audio_buffer.h"

void *live_audio_pcm_capture_buf_open(int size,
                                      void *wakeup_data,
                                      void (*wakeup)(void *),
                                      void *path,
                                      int (*write_frame)(void *path, struct audio_frame *frame));

void live_audio_pcm_capture_buf_close(void *buffer);

void live_audio_pcm_capture_buf_wakeup(void *buffer);

int live_audio_pcm_capture_write_frame(void *buffer, struct audio_frame *frame);

int live_audio_pcm_capture_buf_read(void *buffer, void *data, int len);
#endif
