/*************************************************************************************************/
/*!
*  \file      live_audio.h
*
*  \brief
*
*  Copyright (c) 2011-2022 ZhuHai Jieli Technology Co.,Ltd.
*
*/
/*************************************************************************************************/
#ifndef _LIVE_AUDIO_H_
#define _LIVE_AUDIO_H_

#include "media/audio_path.h"
#define PLAY_SYNCHRONIZE_CURRENT_TIME       0x0
#define PLAY_SYNCHRONIZE_LATCH_TIME         0x1
#define PLAY_SYNCHRONIZE_GET_TIME           0x2

enum live_sound_type {
    LIVE_SOUND_A2DP = 0x1,
    LIVE_SOUND_FILE,
    LIVE_SOUDN_AUX,
    LIVE_SOUND_MIC,
    LIVE_SOUND_IIS,
    LIVE_SOUND_SYNCHRONIZE_STREAM,
};


struct live_audio_mode_ops {
    void *(*capture_open)(struct audio_path *path); //音频源数据捕捉打开
    void (*capture_close)(void *capture);           //音频源数据捕捉关闭
    void (*capture_wakeup)(void *capture);          //音频源睡眠后唤醒
    int (*capture_start)(void *capture);            //音频源数据捕捉启动
    int (*capture_stop)(void *capture);             //音频源数据捕捉停止

    void *(*play_open)(struct audio_path *path);    //音频播放打开
    void (*play_close)(void *player);               //音频播放关闭
    int (*play_start)(void *player);                //音频播放启动
    int (*play_stop)(void *player);                 //音频播放停止
    int(*play_status)(void);                        //获取当前音频播放状态
};

#endif
