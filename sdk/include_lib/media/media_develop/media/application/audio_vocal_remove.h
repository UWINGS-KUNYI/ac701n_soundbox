
#ifndef _AUDIO_VOCAL_REMOVE_API_H_
#define _AUDIO_VOCAL_REMOVE_API_H_

#include "system/includes.h"
#include "media/includes.h"
#include "media/audio_eq.h"

#define LOW_CUT_ENABLE  BIT(1)
#define HIGH_CUT_ENABLE BIT(2)

typedef struct _vocal_remove_open_parm {
    u8 channel;//输入音频声道数
    u32 samplerate;
    u8 freq_cut_enable;
    u8 remove_ratio;
    u16 low_freq;
    u16 high_freq;
    u8 bit_wide;
} vocal_remove_open_parm;


typedef struct _vocal_remove_hdl {
    u32 vocal_remove_dis: 1;
    vocal_remove_open_parm o_parm;
    struct audio_stream_entry entry;	// 音频流入口
    int *vocal_low_cut_buf;
    int *vocal_high_cut_buf;
    u16 vocal_buf_len;
    struct audio_eq *low_cut_eq;
    struct audio_eq *high_cut_eq;
    struct audio_eq *pre_vocal_eq;
    struct eq_seg_info low_cut_eq_tab[5];
    struct eq_seg_info high_cut_eq_tab[5];
    struct eq_seg_info pre_vocal_eq_tab[10];
} vocal_remove_hdl;
/*----------------------------------------------------------------------------*/
/**@brief   audio_vocal_remove_open  人声消除打开
   @param    *_parm: 始化参数，详见结构体vocal_remove_open_parm
   @return   句柄
   @note
*/
/*----------------------------------------------------------------------------*/
vocal_remove_hdl *audio_vocal_remove_open(vocal_remove_open_parm *_parm);

/*----------------------------------------------------------------------------*/
/**@brief    audio_vocal_remove_close 人声关闭处理
   @param    _hdl:句柄
   @return  0:成功  -1：失败
   @note
*/
/*----------------------------------------------------------------------------*/
int audio_vocal_remove_close(vocal_remove_hdl *_hdl);


/*----------------------------------------------------------------------------*/
/**@brief    audio_vocal_remove_sw 运行过程开关处理
   @param    _hdl:句柄
   @param    dis: 0：人声消除有效  1:人声消除无效
   @return  0:成功  -1：失败
   @note
*/
/*----------------------------------------------------------------------------*/
int audio_vocal_remove_sw(vocal_remove_hdl *_hdl, u32 dis);

void vocal_remove_samplerate_set(vocal_remove_hdl *_hdl, u32 sr);

#endif

