/*************************************************************************************************/
/*!
*  \file      live_audio_player.h
*
*  \brief
*
*  Copyright (c) 2011-2022 ZhuHai Jieli Technology Co.,Ltd.
*
*/
/*************************************************************************************************/
#ifndef _LIVE_AUDIO_PLAYER_H_
#define _LIVE_AUDIO_PLAYER_H_

#include "audio_syncts.h"
#include "audio_path.h"

void *live_audio_player_open(struct audio_path *ipath, struct audio_path *opath);

void live_audio_player_close(void *player);

int live_audio_player_push_data(void *player, void *data, int len, u32 timestamp);

int live_audio_capture_from_player(void *player, struct audio_path *path);

void live_audio_capture_from_player_close(void *player);
#endif
