#ifndef _AUDIO_RECORDER_MIX_H_
#define _AUDIO_RECORDER_MIX_H_

#include "system/includes.h"
#include "media/includes.h"

int recorder_mix_start(void);
void recorder_mix_stop(void);
int recorder_mix_get_status(void);
u16 recorder_mix_get_samplerate(void);
u32 recorder_mix_get_coding_type(void);
void recorder_mix_bt_status_event(struct bt_event *e);
void *rec_mix_fifo_init(u8 channel_num, u32 sample_rate);
int rec_mix_fifo_uninit(void *fifo_hdl);
int rec_mix_fifo_ch_state_set(void *ch_hdl, u8 en);
void *rec_mix_fifo_ch_open(u8 input_channel_num);
int rec_mix_fifo_ch_close(void *ch_hdl);
int rec_mix_fifo_ch_write(void *ch_hdl, s16 *data, u32 len);
void *rec_mix_fifo_ch_get_entry(void *ch_hdl);
int rec_mix_fifo_write_update(void *fifo_hdl);
int rec_mix_fifo_read(void *fifo_hdl, s16 *data, u32 len);

#endif//_AUDIO_RECORDER_MIX_H_

