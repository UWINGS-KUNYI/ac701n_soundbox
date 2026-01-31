#ifndef SPECTRUM_API_H
#define SPECTRUM_API_H


#include "media/audio_eq.h"
#include "media/audio_stream.h"
#include "media/follow.h"

struct spectrum_filter {
    int freq;   //中心频点值 Hz
    float q;    //q值(0.7~30)，Q越大，计算精度越大
};
struct spectrum_open_parm {
    FollowParam param;
    u16 bit_width;             //当前输入数据的位宽配置  0:16bit  1:32bit,(32bit位宽时，内部会转成16bit再计算dB值)
    u16 series_filter;         //串联叠加相同滤波器的个数(1~3)
    int section;              //最大10段
    struct spectrum_filter filter[16];
};

struct spectrum_base_hdl {
    void *workbuf;
    struct audio_eq *eq;
    struct eq_seg_info seg[3];
};

struct spectrum_hdl {
    struct spectrum_base_hdl *base;
    struct spectrum_open_parm parm;
    struct audio_stream_entry entry;	// 音频流入口
    s16 *tmp;
    s16 len;
};


struct spectrum_hdl *audio_spectrum_open(struct spectrum_open_parm *parm);
void audio_stpectrum_run(struct spectrum_hdl *hdl, s16 *data, u16 len);//非节点数据流时直接调用run处理
int audio_spectrum_close(struct spectrum_hdl *hdl);
/*
 *获取目标频点的能量值
 **hdl:audio_spectrum_open接口的返回值
 *center_freq:audio_spectrum_open时，配置的频点值
 *return 返回能量值存储地址, 立体声时，L:dB[0]  R:dB[1], 单声道时：dB[0], 返回NULL时无效
 * */
float *audio_spectrum_get_dB(struct spectrum_hdl *hdl, u32 center_freq);//获取能量值

#if 0
//应用例子
/*
 *打开
 * */
struct spectrum_filter sfilter[] = {//配置目标频点,Q值30
    {100, 30}, {500, 30}, {1000, 30}, {2000, 30}, {4000, 30},
    {8000, 30}, {10000, 30}, {12000, 30}, {14000, 30}, {16000, 30}
};
struct spectrum_open_parm sparm = {0};
sparm.bit_width = 1;//当前输入数据的位宽配置  0:16bit  1:32bit,(32bit位宽时，内部会转成16bit再计算dB值)
sparm.series_filter = 3;//叠加滤波器的个数
sparm.section = ARRAY_SIZE(sfilter);
memcpy(sparm.filter, sfilter, sizeof(struct spectrum_filter)*sparm.section);
sparm.param.SampleRate = sample_rate;//采样率
sparm.param.channel = ch_num;//通道数
sparm.param.attackTime  = 10;
sparm.param.releaseTime  = 500;
sparm.param.algorithm = PEAK;
sparm.param.mode = TWOPOINT;
struct spectrum_hdl *spectrum = audio_spectrum_open(&sparm);
/*
 *接入数据流
 * */
if (spectrum)
{
    entries[entry_cnt++] = &spectrum->entry;
}

/*
 *获取100Hz目标频点的能量值
 * */
float *dB = audio_spectrum_get_dB(spectrum, 100);
if (dB)
{
    printf("L: %d, R:%d\n", (int)dB[0], (int)dB[1]);
}

/*
 *使用完成，关闭模块
 * */
audio_spectrum_close(spectrum);
spectrum = NULL;
#endif


#endif
