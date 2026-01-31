/*************************************************************************************************/
/*!
*  \file      audio_effect_task.h
*
*  \brief
*
*  Copyright (c) 2011-2023 ZhuHai Jieli Technology Co.,Ltd.
*
*/
/*************************************************************************************************/
#ifndef _AUDIO_EFFECT_TASK_H_
#define _AUDIO_EFFECT_TASK_H_
#include "audio_stream.h"

struct audio_stream_entry *audio_effect_task_open(void);

void audio_effect_task_close(struct audio_stream_entry *entry);

struct audio_stream_entry *audio_effect_task_get_output_entry(struct audio_stream_entry *entry);

#endif
