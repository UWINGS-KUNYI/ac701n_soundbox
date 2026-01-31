#ifndef _PC_H_
#define _PC_H_

#include "system/event.h"
#include "music/music_player.h"

extern int pc_device_event_handler(struct sys_event *event);
extern void app_pc_tone_play_start(u8 mix);
extern void usb_start();


#endif
