
#ifndef FOLLOW_H
#define FOLLOW_H

#include "drc_api.h"
/* enum { */
// PEAK = 0,
// RMS
// };

// enum {
// PERPOINT = 0,
// TWOPOINT
/* }; */

typedef struct _FollowParam {
    int SampleRate;
    int channel;
    int attackTime;//10
    int releaseTime;//500
    int rmsTime;    //1~25 ms
    short algorithm;//RMS 、PEAK
    short mode;     //PREPOINT、TWOPOINT
} FollowParam;

typedef struct _FollowProcess {
    void(*Process)(void *workBuf, short in, int per_channel_npoint);
} FollowProcess;

typedef struct _FollowRunProcess {
    int (*Run)(void *workBuf, short *in, int per_channel_npoint);
} FollowRunProcess;

typedef struct _Follow {
    FollowProcess process;
    FollowRunProcess run;
    float attFactor;
    float relFactor;
    int channel;
    int SampleRate;
    int rmsLen;
    short algorithm;
    short mode;

    long long rmsSum[2];
    int rmsIdx[2];
    float follow[2];
    float db[2];
    short *rmsbuf[2];
    short mempool[0];
} Follow;

int getFollowBuf(FollowParam *param);
int FollowInit(void *workbuf, FollowParam *param);
int FollowRun(void *workbuf, short *in, int per_channel_npoint);
float *getFollowDB(void *workbuf);

#endif // !FOLLOW_H
