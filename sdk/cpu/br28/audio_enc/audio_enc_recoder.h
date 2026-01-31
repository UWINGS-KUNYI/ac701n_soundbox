
#ifndef _AUDIO_ENC_RECODER_H_
#define _AUDIO_ENC_RECODER_H_

#include "media/audio_encoder.h"

struct mic_sample_params {
    u16 sample_rate;    //采样率
    u8 gain;			//增益
    u8 channel_num;     //需要linein输出数据的声道数
    void *output_priv;  //数据接口私有参数
    int (*output_handler)(void *output_priv, s16 *data, int len);//输出接口

};



int audio_adc_mic_init(u16 sr);
void audio_adc_mic_exit(void);

void mic_sample_set_resume_handler(void *priv, void (*resume)(void));
int mic_sample_read(void *hdl, void *data, int len);
int mic_sample_size(void *hdl);
int mic_sample_total(void *hdl);
void *mic_sample_open(struct mic_sample_params *mic_params);
void mic_sample_close(void *hdl);

void linein_sample_set_resume_handler(void *priv, void (*resume)(void));
void fm_inside_output_handler(void *priv, s16 *data, int len);
int linein_sample_read(void *hdl, void *data, int len);
int linein_sample_size(void *hdl);
int linein_sample_total(void *hdl);
int linein_stream_sample_rate(void *hdl);
void *linein_sample_open(u8 source, u16 sample_rate);
void linein_sample_close(void *hdl);
void *fm_sample_open(u8 source, u16 sample_rate);
void fm_sample_close(void *hdl, u8 source);

////>>>>>>>>>>>>>>record_player api录音接口<<<<<<<<<<<<<<<<<<<<<///
void recorder_encode_stop(void);
u32 recorder_get_encoding_time();
///检查录音是否正在进行
int recorder_is_encoding(void);
void recorder_device_offline_check(char *logo);

void set_mic_cbuf_hdl(cbuffer_t *mic_cbuf);
void set_mic_resume_hdl(void (*resume)(void *priv), void *priv);
void set_ladc_cbuf_hdl(cbuffer_t *ladc_cbuf);
void set_ladc_resume_hdl(void (*resume)(void *priv), void *priv);

#endif

