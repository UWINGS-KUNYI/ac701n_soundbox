#ifndef _LIVE_MIC_H_
#define _LIVE_MIC_H_

void mic_start(u8 source, u32 sample_rate, u8 gain);
void mic_stop(void);
void mic_wireless_audio_codec_interface_register(void);

#endif

