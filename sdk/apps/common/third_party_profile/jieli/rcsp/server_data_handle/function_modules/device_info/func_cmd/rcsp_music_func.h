#ifndef __RCSP_MUSIC_FUNC_H__
#define __RCSP_MUSIC_FUNC_H__
#include "typedef.h"
#include "app_config.h"

/**
 * @abstract 获取固件播放器信息
 */
u32 music_func_get(void *priv, u8 *buf, u16 buf_size, u32 mask);
/**
 * @abstract 设置播放器行为
 */
bool music_func_set(void *priv, u8 *data, u16 len);
/**
 *	@abstract 音乐功能暂停
 */
void music_func_stop(void);

#endif
