/*************************************************************************************************/
/*!
*  \file      live_stream_encoder.h
*
*  \brief
*
*  Copyright (c) 2011-2022 ZhuHai Jieli Technology Co.,Ltd.
*
*/
/*************************************************************************************************/
#ifndef _LIVE_STREAM_ENCODER_H_
#define _LIVE_STREAM_ENCODER_H_
#include "audio_path.h"

void *live_stream_encoder_open(struct audio_path *path);

void live_stream_encoder_close(void *priv);

void live_stream_encoder_stop(void *encoder);

void live_stream_encoder_wakeup(void *priv);

void live_stream_encode_bitrate_updata(void *priv, u32 bit_rate);

void live_stream_encoder_capacity_dump(void *priv);
#endif
