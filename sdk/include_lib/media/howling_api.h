#ifndef HOWLING_API_H
#define HOWLING_API_H

#include "media/howling_pitchshifter_api.h"
#include "media/notch_howling_api.h"
#include "pemafrow_api.h"
#include "media/audio_stream.h"

//啸叫抑制 NotchHowling:
struct NotchHowling_update_Param {
    float Q;    	//Q值
    float gain;		//增益
    int fade_n;		//启动释放时间
    float threshold;
};



typedef struct _HOWLING_API_STRUCT_ {
    void				*ptr;    //运算buf指针
    HOWLING_PITCHSHIFT_PARM 	parm_2;  //移频参数
    HOWLING_PITCHSHIFT_FUNC_API *func_api;           //移频函数指针
    NotchHowlingParam 	parm;  //陷波参数
    struct audio_stream_entry entry;	// 音频流入口
    s16 *pre_buf;
    u32 sample_rate;
    u8 run_en;
    u8 mode;
    u8 update;
} HOWLING_API_STRUCT;

#endif
