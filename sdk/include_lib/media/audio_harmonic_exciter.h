#ifndef _AUDIO_HARMONIC_EXCITER_API_H_
#define _AUDIO_HARMONIC_EXCITER_API_H_
#include "system/includes.h"
#include "media/audio_stream.h"
#include "media/excitor_api.h"

typedef struct _HarmonicExciterUdateParam {//与EXCITER_PARM关联
    unsigned int flow_cut;
    unsigned int fhigh_cut;
    int wetgain;               //0-100
    int drygain;
    int excitType;
} HarmonicExciterUdateParam;


typedef struct _HarmonicExciter_TOOL_SET {
    int is_bypass; // 1-> byass 0 -> no bypass
    HarmonicExciterUdateParam parm;
} harmonic_exciter_param_tool_set;

struct harmonic_exciter_param {
    EXCITER_PARM param;
    u32 samplerate;
    u32 ch_num;
    u32 exciter_name;
};

//谐波增强
struct audio_harmonic_exciter {
    void *workbuf;           //harmonic_exciter 运行句柄及buf
    EXCITE_FUNC_API *ops;
    struct harmonic_exciter_param parm;
    struct audio_stream_entry entry;	// 音频流入口
    struct list_head hentry;                        //
    u8 status;                           //内部运行状态机
    u8 update;                           //设置参数更新标志
};


int audio_harmonic_exciter_run(struct audio_harmonic_exciter *hdl, void *datain, void *dataout, u32 len);
struct audio_harmonic_exciter *audio_harmonic_exciter_open(struct harmonic_exciter_param *parm);
int audio_harmonic_exciter_close(struct audio_harmonic_exciter *hdl);
void audio_harmonic_exciter_set_bit_wide(int exciter_name, af_DataType dataTypeobj);

int audio_harmonic_exciter_update_parm(int exciter_name, HarmonicExciterUdateParam *parm);
void audio_harmonic_exciter_bypass(int exciter_name, u8 bypass);

#ifndef RUN_NORMAL
#define RUN_NORMAL  0
#endif

#ifndef RUN_BYPASS
#define RUN_BYPASS  1
#endif

#endif

