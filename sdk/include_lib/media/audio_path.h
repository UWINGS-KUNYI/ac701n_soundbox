/*************************************************************************************************/
/*!
*  \file      audio_path.h
*
*  \brief
*
*  Copyright (c) 2011-2022 ZhuHai Jieli Technology Co.,Ltd.
*
*/
/*************************************************************************************************/
#ifndef _AUDIO_PATH_H_
#define _AUDIO_PATH_H_

#include "audio_stream.h"

struct audio_stream_input {
    void *path;
    union {
        int (*read_frame)(void *path, struct audio_frame *frame, int len);
        struct audio_frame *(*pull_frame)(void *path);
        void *priv;
    };
    void (*free_frame)(void *path, struct audio_frame *frame);
    void (*wakeup)(void *path);
};

struct audio_stream_output {
    void *path;
    union {
        int (*write_frame)(void *path, struct audio_frame *frame);
        void *ch_map;
    };
    void (*wakeup)(void *path);
};

struct audio_reference_time {
    void *reference_clock;
    u32(*request)(void *clock, u8 type);
};
/*
 * 音频路径参数：
 * 一般用于打开音频的接口参数配置
 */
struct audio_path {
    u8 network;
    u8 device;
    int delay_time;
    struct audio_fmt fmt;
    struct audio_stream_input input;
    struct audio_stream_output output;
    struct audio_reference_time time;
};

#endif
