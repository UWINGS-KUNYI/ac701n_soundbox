#ifndef __AUDIO_USB_HOST_H
#define __AUDIO_USB_HOST_H

#include "cpu.h"


void *get_usb_host_spk_stream_entry(void);
void audio_usb_host_spk_stream_init(void *dig_vol_hdl);
int audio_usb_host_init(const char *audio);
void audio_usb_host_release(void);
void audio_usb_host_spk_start(int in_sr, int out_sr);
void audio_usb_spk_volume_up(void);
void audio_usb_spk_volume_down(void);
void audio_usb_spk_set_volume(s8 vol);

void audio_usb_mic_set_handler(void *priv, u32(*func)(void *priv, void *data, int len));
int audio_usb_mic_init(void);
int audio_usb_mic_start(int out_sr);
int audio_usb_mic_src_buf_read(s16 *buf, u32 len);
void audio_usb_mic_release(void);
void audio_host_spk_fade_in(void);


extern u32 uac_host_get_spk_cbuf_len();
extern u32 uac_host_get_spk_cbuf_size();
extern u32 uac_host_get_mic_cbuf_len();
extern u32 uac_host_get_mic_cbuf_size();


#endif

