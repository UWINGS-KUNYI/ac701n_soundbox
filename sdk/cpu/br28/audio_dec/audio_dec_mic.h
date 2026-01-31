#ifndef _AUDIO_DEC_MIC_H_
#define _AUDIO_DEC_MIC_H_

#include "asm/includes.h"
#include "media/includes.h"
#include "system/includes.h"
#include "media/audio_decoder.h"

// linein解码释放
void mic_dec_relaese();
// linein解码开始
int mic_dec_start();

// 打开linein解码
int mic_dec_open(u8 source, u32 sample_rate, u8 gain);
// 关闭linein解码
void mic_dec_close(void);
// linein解码重新开始
int mic_dec_restart(int magic);
// 推送linein解码重新开始命令
int mic_dec_push_restart(void);


#endif
