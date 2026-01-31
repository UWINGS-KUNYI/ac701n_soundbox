
#ifndef __APP_COMMON_H__
#define __APP_COMMON_H__

#include "typedef.h"

u8 is_wireless_trans_tone_busy(void);
int app_wireless_trans_mode_swtch_to(u8 switch_mode);
u8 app_common_key_var_2_event(u32 key_var);

#endif
