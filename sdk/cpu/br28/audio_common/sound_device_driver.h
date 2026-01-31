#ifndef SOUND_DEVICE_DRIVER_H_
#define SOUND_DEVICE_DRIVER_H_
#include "audio_path.h"

void *live_aux_capture_open(struct audio_path *path);

void live_aux_capture_close(void *capture);

void live_aux_capture_play_pause(void *capture, u8 on_off);

void *live_mic_capture_open(struct audio_path *path);

void live_mic_capture_close(void *capture);

void live_mic_capture_play_pause(void *capture, u8 on_off);

void *sound_iis_capture_open(struct audio_path *path);

void sound_iis_capture_close(void *capture);

#endif

