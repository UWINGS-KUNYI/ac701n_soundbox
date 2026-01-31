/****************************************************************/
/* Copyright:(c)JIELI  2011-2022 All Rights Reserved.           */
/****************************************************************/
/*
>file name : capture_synchronize.h
>create time : Sat 11 Jun 2022 03:44:33 PM CST
*****************************************************************/
#ifndef _CAPTURE_SYNCHRONIZE_H_
#define _CAPTURE_SYNCHRONIZE_H_
#include "typedef.h"

#include "media/audio_path.h"
/***********************************************************
 *      live_stream_capture_synchronize_open
 * description  : 实时音频流捕捉同步的打开
 * arguments    : path      -   路径结构参数
 * return       : NULL      -   failed,
 *                非NULL    -   private_data
 * notes        : None.
 ***********************************************************/
void *live_stream_capture_synchronize_open(struct audio_path *path);

/***********************************************************
 *      live_stream_capture_synchronize_handler
 * description  : 实时音频流捕捉同步音频帧输入
 * arguments    : synchronize       -   音频同步的私有结构
 *                frame             -   音频帧数据
 * return       : 实际的处理长度.
 * notes        : None.
 ***********************************************************/
int live_stream_capture_synchronize_handler(void *synchronize, struct audio_frame *frame);

/***********************************************************
 *      live_stream_capture_send_frames
 * description  : 实时流端发送(采样)的pcm采样数
 * arguments    : synchronize       -   音频同步的私有结构
 *                time              -   发送的采样时间
 *                timestamp         -   发送的时间戳
 * return       : None.
 * notes        : None.
 ***********************************************************/
void live_stream_capture_send_frames(void *synchronize, int time, u32 timestamp);

/***********************************************************
 *      live_stream_capture_synchronize_close
 * description  : 实时音频流捕捉同步关闭
 * arguments    : synchronize   -   音频同步的私有结构
 * return       : None.
 * notes        : None.
 ***********************************************************/
void live_stream_capture_synchronize_close(void *priv);

/***********************************************************
 *      live_stream_capture_synchronize_wakeup
 * description  : 实时音频流捕捉同步的唤醒
 * arguments    : synchronize           -   音频同步的私有结构
 * return       : None.
 * notes        : None.
 *                .
 ***********************************************************/
void live_stream_capture_synchronize_wakeup(void *synchronize);

/***********************************************************
 *      live_stream_capture_synchronize_use_frames
 * description  : 更新使用模块输出的pcm采样数
 * arguments    : synchronize       -   音频同步的私有结构
 *                frames            -   pcm的采样周期数值
 * return       : None.
 * notes        : None.
 ***********************************************************/
void live_stream_capture_synchronize_use_frames(void *synchronize, int frames);
#endif
