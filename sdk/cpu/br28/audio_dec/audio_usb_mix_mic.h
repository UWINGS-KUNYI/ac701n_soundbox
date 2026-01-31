#ifndef _AUDIO_USB_MIX_MIC_H_
#define _AUDIO_USB_MIX_MIC_H_

#include "app_config.h"
#include "asm/includes.h"

extern int usb_audio_soundcard_mic_open(void *_info);
extern int usb_audio_soundcard_mic_close(void *arg);
extern void usb_audio_soundcard_mic_set_gain(int gain);

/////////////////// mix fifo    ////////////////////////
extern void *mix_fifo_init(u8 channel_num, u32 sample_rate);
extern int mix_fifo_uninit(void *fifo_hdl);
extern int mix_fifo_ch_state_set(void *ch_hdl, u8 en);
extern void *mix_fifo_ch_open(void *fifo_hdl, u8 input_channel_num);
extern int mix_fifo_ch_close(void *ch_hdl);
extern int mix_fifo_ch_write(void *ch_hdl, s16 *data, u32 len);
extern void *mix_fifo_ch_get_entry(void *ch_hdl);
extern int mix_fifo_write_update(void *fifo_hdl);
extern int mix_fifo_read(void *fifo_hdl, s16 *data, u32 len);

#endif  //#ifndef _AUDIO_USB_MIX_MIC_H_
