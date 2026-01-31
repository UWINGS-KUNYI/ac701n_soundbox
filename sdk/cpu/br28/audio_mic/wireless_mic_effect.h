#ifndef _WIRELESS_MIC_EFFCT_H_
#define _WIRELESS_MIC_EFFCT_H_

#include "application/audio_bfilt.h"
#include "media/audio_eq_drc_apply.h"
#include "application/audio_echo_src.h"
#include "application/audio_energy_detect.h"
#include "media/audio_stream.h"
#include "stream_entry.h"
#include "mic_effect.h"
#include "audio_effect/audio_eff_default_parm.h"
#include "audio_effect/audio_autotune_demo.h"
#include "media/convert_data.h"
#include "media/effects_adj.h"
#include "audio_effect/audio_eq_drc_demo.h"
#include "media/audio_echo_reverb.h"
#include "mic_stream.h"
#include "mic_stream_demo.h"

// #if MIC_EFFECT_LLNS
// #include "audio_llns.h"
// #endif


#define LIVE_MIC_EFFECT_CONFIG 	  	0\
								| BIT(MIC_EFFECT_CONFIG_NOISEGATE) \
								| BIT(MIC_EFFECT_CONFIG_EQ) \
								| BIT(MIC_EFFECT_CONFIG_HOWLING) \
								| BIT(MIC_EFFECT_CONFIG_HOWLING_TRAP) \
/* | BIT(MIC_EFFECT_CONFIG_ENERGY_DETECT) \ */
/* | BIT(MIC_EFFECT_CONFIG_LINEIN) \ */
/* | BIT(MIC_EFFECT_CONFIG_DVOL) \ */

#define	WIRELESS_MIC_CONFIG_ECHO			    0
#define	WIRELESS_MIC_CONFIG_REVERB			    0
#define	WIRELESS_MIC_CONFIG_LLNS			    0
#define	WIRELESS_MIC_CONFIG_NOISEGATE		    0
#define	WIRELESS_MIC_CONFIG_EQ					0
#define	WIRELESS_MIC_CONFIG_HOWLING			    0
#define	WIRELESS_MIC_CONFIG_HOWLING_TRAP	    0
#define	WIRELESS_MIC_CONFIG_VOICE_CHANGE	    0
#define	WIRELESS_MIC_CONFIG_DEMO				0


enum {
    WIRELESS_AUDIO_DATA,
};

struct live_mic_effect_param {
    int effect_config;   //音效支持的功能;
    u32 sample_rate;
    u8 ch_num;
    u8 bit_width;
    void *output_priv;      /*输出私有参数*/
    int (*output)(void *output_priv, void *data, int len);     /*输出接口*/
};

struct live_mic_effect {
    struct audio_stream *stream;			// 音频流
    struct audio_stream_entry entry;		// effect 音频入口
    struct audio_stream_entry output_entry; //effect 数据出口

    mic_stream 						*mic_stream;
    int out_len;
    struct live_mic_effect_param param; //effect 参数

    struct audio_eq             *mic_eq0;    //eq 句柄
    struct audio_drc            *mic_drc0;   //eq 句柄
    struct convert_data         *convert0;   //32-》16

    struct audio_eq             *mic_eq4;    //eq drc句柄
    struct audio_drc            *mic_drc4;    //eq drc句柄
    struct convert_data         *convert4;//32-》16

    NOISEGATE_API_STRUCT	*noisegate;
    HOWLING_API_STRUCT 		*howling_ps;
    HOWLING_API_STRUCT 		*notch_howling;
    voice_changer_hdl       *voice_changer;
    struct __stream_llns    *llns;
    struct __stream_demo_hdl *demo;

    struct audio_eq     	*mic_eq3;    //eq drc句柄
    struct audio_drc    	*mic_drc3;    //eq drc句柄

    //分支0节点
    PLATE_REVERB_API_STRUCT		*sub_0_plate_reverb_hdl;
    struct audio_eq     		*mic_eq1;    //eq drc句柄
    struct audio_drc     		*mic_drc1;    //eq drc句柄

    //分支1节点
    ECHO_API_STRUCT 			*sub_1_echo_hdl;
    struct audio_eq     		*mic_eq2;    //eq drc句柄
    struct audio_drc     		*mic_drc2;    //eq drc句柄

    struct channel_switch       *ch_switch;
    //分支1 2 3的总处理
    struct aud_reverb_process *aud_reverb;

    void *dvol_entry;

    u32 process_len;	// 数据流处理长度
    u8 bypass;

    spinlock_t lock;
    u8 start;
    u32 timestamp;
    OS_SEM   sem;

};




struct live_mic_effect *live_mic_effect_init(struct live_mic_effect_param *param);

void live_mic_effect_open(struct live_mic_effect *effect);

int live_mic_effect_stream_entry_add(struct live_mic_effect *effect, struct audio_stream_entry **entries, int max);

int live_mic_effect_input(void *_effect, void *buff, int len, u32 time);

void live_mic_effect_close(struct live_mic_effect *effect);


u8 live_mic_effect_status_get();

void live_mic_plate_reverb_update_parm(void *parm, int bypass);

void live_mic_plate_reverb_bypass(int bypass);

void live_mic_echo_updata_parm(void *parm, int bypass);

void live_mic_echo_bypass(int bypass);

void live_mic_howling_pitch_shift_update_parm(void *parm, int bypass);

void live_mic_howling_pitch_shift_bypass(int bypass);

void live_mic_notchhowline_update_parm(void *parm, int bypass);

void live_mic_notchhowline_bypass(int bypass);

void live_mic_llns_bypass(int bypass);

void live_mic_voice_changer_bypass(int bypass);

int live_mic_effect_add_output(struct audio_stream_entry *output);

int live_mic_effect_del_output(struct audio_stream_entry *output);

//int live_mic_effect_async_input(void *effect, void *data, int len);
int live_mic_effect_async_input(void *effect, void *in, void *out, u32 inlen, u32 outlen);
#endif

