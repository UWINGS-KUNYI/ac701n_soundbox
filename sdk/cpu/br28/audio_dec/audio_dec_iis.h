#ifndef _AUDIO_DEC_IIS_H_
#define _AUDIO_DEC_IIS_H_

#include "asm/includes.h"
#include "media/includes.h"
#include "system/includes.h"
#include "media/audio_decoder.h"

// iis_in解码释放
void iis_in_dec_relaese();
// iis_in解码开始
int iis_in_dec_start();

// 打开iis_in解码
int iis_in_dec_open(u32 sample_rate);
// 关闭iis_in解码
void iis_in_dec_close(void);
// iis_in解码重新开始
int iis_in_dec_restart(int magic);
// 推送iis_in解码重新开始命令
int iis_in_dec_push_restart(void);

// 输出一路iis原始数据
void set_iis_in_capture_handler(void *priv, void (*handler)(void *, void *, int));

/***********************iis_in pcm enc******************************/
// iis_in录音停止
void iis_in_pcm_enc_stop(void);
// iis_in录音开始
int iis_in_pcm_enc_start(void);
// 检测iis_in是否在录音
bool iis_in_pcm_enc_check();

#endif

