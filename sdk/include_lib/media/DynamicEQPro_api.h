#ifndef DYNAMICEQ_PRO_API_H
#define DYNAMICEQ_PRO_API_H

#include "AudioEffect_DataType.h"
#include "asm/hw_eq.h"

#ifdef WIN32

#define AT_DYNAMICEQ_Pro(x)
#define AT_DYNAMICEQ_Pro_CODE
#define AT_DYNAMICEQ_Pro_CONST
#define AT_DYNAMICEQ_Pro_SPARSE_CODE
#define AT_DYNAMICEQ_Pro_SPARSE_CONST

#else

#define AT_DYNAMICEQ_Pro(x)				__attribute((section(#x)))
#define AT_DYNAMICEQ_Pro_CODE			AT_DYNAMICEQ_Pro(.dynamic_eq_pro_code)
#define AT_DYNAMICEQ_Pro_CONST			AT_DYNAMICEQ_Pro(.dynamic_eq_pro_const)
#define AT_DYNAMICEQ_Pro_SPARSE_CODE	AT_DYNAMICEQ_Pro(.dynamic_eq_pro_sparse_code)
#define AT_DYNAMICEQ_Pro_SPARSE_CONST	AT_DYNAMICEQ_Pro(.dynamic_eq_pro_sparse_const)

#endif

typedef enum {
    EQ_IIR_TYPE_PEAKING = EQ_IIR_TYPE_BAND_PASS,
    /* EQ_IIR_TYPE_HIGH_SHELF, */
    /* EQ_IIR_TYPE_LOW_SHELF */
} DYNAMIC_EQ_PRO_IIR_TYPE;


typedef struct _DynamicEQProEffectParam {
    int fc;
    float Q;
    float gain;
    int attackTime : 16;
    int releaseTime : 16;
    float low_thr;//小于等于low_thr,输出幅度增加gain(dB),
    float high_thr;//大于等于high_thr,输出幅度增加0(dB), 等于low_thr且小于high_thr,输出幅度按照实际斜率控制
    char type;
    char Enable;
    char reserved[2];
} DynamicEQProEffectParam;

typedef struct _DynamicEQProParam {
    int nSection;
    int channel;
    int SampleRate;
    int DetectdataInc;
    int DetectdataBit;
    af_DataType pcm_info;
} DynamicEQProParam;

int getDynamicEQProBuf(DynamicEQProEffectParam *effectParam, DynamicEQProParam *param); //bufsize 与nSection rmsTime algorithm channel SampleRate 有关
int DynamicEQProInit(void *WorkBuf, DynamicEQProEffectParam *effectParam, DynamicEQProParam *param);
int DynamicEQProUpdate(void *WorkBuf, DynamicEQProEffectParam *effectParam, DynamicEQProParam *param);
int DynamicEQProRun(void *WorkBuf, int *detectdata, int *indata, int *outdata, int per_channel_npoint);

#endif // !DYNAMICEQ_API_H


