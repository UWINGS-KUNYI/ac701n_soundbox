/*************************************************************************************************/
/*!
*  \file      audio_effect_develop.h
*
*  \brief
*
*  Copyright (c) 2011-2023 ZhuHai Jieli Technology Co.,Ltd.
*
*/
/*************************************************************************************************/
#ifndef _AUDIO_EFFECT_DEVELOP_H_
#define _AUDIO_EFFECT_DEVELOP_H_
#include "audio_stream.h"

/*********************************************************************** 
 * 音效算法开发打开 
 * Input    :  sample_rate      - 采样率 
               nch              - 声道数 
               bit_width        - 位宽(通常默认为16bit)
 * Output   :  audio stream节点入口
 * Notes    : 
 * History  : 
 *=====================================================================*/
struct audio_stream_entry *audio_effect_develop_open(int sample_rate, u8 nch, u8 bit_width);

/*********************************************************************** 
 * 音效算法开发关闭 
 * Input    :  entry - audio stream节点入口 
 * Output   :   
 * Notes    :   
 * History  : 
 *=====================================================================*/
void audio_effect_develop_close(struct audio_stream_entry *entry);


#endif
