#ifndef NOTCH_HOWLING_API_H
#define NOTCH_HOWLING_API_H

#include "AudioEffect_DataType.h"

#ifdef WIN32

#define AT_NOTCHHOWLING(x)
#define AT_NOTCHHOWLING_CODE
#define AT_NOTCHHOWLING_CONST
#define AT_NOTCHHOWLING_SPARSE_CODE
#define AT_NOTCHHOWLING_SPARSE_CONST

#else
#define AT_NOTCHHOWLING(x)           __attribute((section(#x)))
#define AT_NOTCHHOWLING_CODE         AT_NOTCHHOWLING(.notchhowling_code)
#define AT_NOTCHHOWLING_CONST        AT_NOTCHHOWLING(.notchhowling_const)
#define AT_NOTCHHOWLING_SPARSE_CODE  AT_NOTCHHOWLING(.notchhowling_sparse_code)
#define AT_NOTCHHOWLING_SPARSE_CONST AT_NOTCHHOWLING(.notchhowling_sparse_const)
#endif

typedef struct _NotchHowlingParam {
    int fade_time;
    float notch_gain;
    float notch_Q;
    float threshold;
    int SampleRate;
    af_DataType pcm_info;
} NotchHowlingParam;

int get_howling_buf(NotchHowlingParam *param);
int howling_init(void *workbuf, NotchHowlingParam *param);
int howling_run(void *workbuf, void *in, void *out, int len);
void SetHowlingDetection(void *workbuf, int OnlyDetection);
float *getHowlingFreq(void *workbuf, int *num);


#endif // !NOTCH_HOWLING_API_H

