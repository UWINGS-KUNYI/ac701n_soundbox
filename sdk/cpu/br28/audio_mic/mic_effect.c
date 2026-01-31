#include "audio_splicing.h"
#include "application/audio_bfilt.h"
#include "media/audio_eq_drc_apply.h"
#include "application/audio_echo_src.h"
#include "application/audio_energy_detect.h"
#include "clock_cfg.h"
#include "media/audio_stream.h"
#include "media/includes.h"
#include "mic_effect.h"
#include "asm/dac.h"
#include "audio_enc/audio_enc.h"
#include "audio_dec.h"
#include "stream_entry.h"
#include "effect_linein.h"
#include "audio_recorder_mix.h"
#include "app_task.h"
#include "amplitude_statistic.h"
#include "media/effects_adj.h"
#include "audio_effect/audio_eff_default_parm.h"
#include "audio_effect/audio_autotune_demo.h"
#include "audio_effect/audio_harmonic_exciter_api.h"
#include "application/audio_data_viewer.h"
#include "media/convert_data.h"
#include "simpleAGC.h"
#include "asm/math_fast_function.h"
#if MIC_EFFECT_LLNS
#include "audio_llns.h"
#endif

#if (SOUNDCARD_ENABLE)
#include "audio_usb_mix_mic.h"
extern void *usb_mix_fifo;
void *mic_mix_fifo_ch = NULL;
#endif

#if WIRELESS_MIC_EFFECT_ENABLE
#include "wireless_mic_effect.h"
#endif

#define LOG_TAG     "[APP-REVERB]"
#define LOG_ERROR_ENABLE
#define LOG_INFO_ENABLE
#define LOG_DUMP_ENABLE
#define LOG_DEBUG_ENABLE
#include "debug.h"

#define REVERB_ECHO_RUN_32BIT 1

#ifdef SUPPORT_MS_EXTENSIONS
//#pragma bss_seg(".audio_mic_stream_bss")
//#pragma data_seg(".audio_mic_stream_data")
#pragma const_seg(".audio_mic_effect_const")
#pragma code_seg(".audio_mic_effect_code")
#endif

#if defined(TCFG_MIC_EFFECT_ENABLE) && TCFG_MIC_EFFECT_ENABLE

#define  LOUDNESS_DEBUG_ENABLE 1//分贝指示器
#if LOUDNESS_DEBUG_ENABLE
#define PRINTF_LOUNDNESS_TIMER   (100)  //MS  100ms打印一次最大值
#define LOUDNESS_THREAD_dB       (-20)  //100ms内大于 该值就打印一次
#endif

//reverb申请的buf大小控制，1->尾音短，2->尾音长
const int PLATERE_RMAX_SIZE = 2;

//reverb模式选择
const int plate_reverb_mode = REVERB_ORINGIN_MODE;

//变声模式选择
const int voice_changer_mode = V_CHANGE_ORINGIN_MODE;

struct aud_reverb_process {
    struct audio_stream_entry entry;	// 音频流入口
    s16 *tmpbuf[3];
    void *eff;//struct __mic_effect
    s16 *out_buf;
    u8 bit_wide;//eq是否输出32bit位宽
    u8 in_ch;
};
struct peak_dB {
    float max_dB;
    u16 cnt;
    u16 index;//0:adc 1:后级
};


struct __mic_effect {
    OS_MUTEX				 		mutex;
    struct __mic_effect_parm     	parm;
    mic_stream 						*mic;
    struct audio_eq             *mic_eq0;    //eq 句柄
#if TCFG_EQ_ENABLE && TCFG_MIC_BASS_AND_TREBLE_ENABLE
    struct audio_eq             *mic_tunning_eq;    //eq 句柄
#endif
    struct audio_drc             *mic_drc0;    //eq 句柄
    struct convert_data             *convert0;//32-》16

    struct audio_eq             *mic_eq4;    //eq drc句柄
    struct audio_drc             *mic_drc4;    //eq drc句柄
    struct convert_data             *convert4;//32-》16
    struct aud_gain_process     *gain;//最后一级 的增益调节

    struct audio_stream *stream;		// 音频流
    struct audio_stream_entry entry;	// effect 音频入口
    int out_len;
    int process_len;
    u8 input_ch_num;                      //mic输入给混响数据流的源声道数

    NOISEGATE_API_STRUCT	*noisegate;
    struct audio_stream_entry *dvol_entry;//数字音量节点
    void            		*d_vol;
    HOWLING_API_STRUCT 		*howling_ps;
    HOWLING_API_STRUCT 		*notch_howling;
    voice_changer_hdl       *voice_changer;
    struct audio_harmonic_exciter *harmonic_exciter;
#if defined(TCFG_MIC_AUTOTUNE_ENABLE) && TCFG_MIC_AUTOTUNE_ENABLE
    autotune_hdl       *autotune;
#endif

    struct channel_switch   *channel_zoom;
#if (RECORDER_MIX_EN)
    void *rec_hdl;
#endif
#if (TCFG_USB_MIC_DATA_FROM_MICEFFECT||TCFG_USB_MIC_DATA_FROM_DAC)
    struct __stream_entry 	*usbmic_hdl;
    u8    usbmic_start;
#endif
    void *energy_hdl;     //能量检测的hdl
    u8 dodge_en;          //能量检测运行过程闪避是否使能
    u8 dodge_ch_num;   //能量检测的总通道数

    struct __effect_linein *linein;
    struct audio_drc       *drc;


    u8  pause_mark;
    struct peak_dB loudness_adc;
    struct peak_dB loudness_drc_out;
    struct __stream_entry 	*loudness_debug_hdl;

    /* struct audio_src_handle *src_hdl; */

    struct audio_eq     *mic_eq3;    //eq drc句柄
    struct audio_drc     *mic_drc3;    //eq drc句柄

    u8                      main_pause;
    //分支0节点
    PLATE_REVERB_API_STRUCT		*sub_0_plate_reverb_hdl;
    struct audio_eq     *mic_eq1;    //eq drc句柄
    struct audio_drc     *mic_drc1;    //eq drc句柄

    //分支1节点
    ECHO_API_STRUCT 		*sub_1_echo_hdl;
    struct audio_eq     *mic_eq2;    //eq drc句柄
    struct audio_drc     *mic_drc2;    //eq drc句柄


    //分支1 2 3的总处理
    struct aud_reverb_process *aud_reverb;


    /*
     *启动淡入处理
     * */
    s16 fade_trigger;
    s16 fade_step;
    s32 fade_volume;

};

struct __mic_stream_parm *g_mic_parm = NULL;
static struct __mic_effect *p_effect = NULL;
#define __this  p_effect
#define R_ALIN(var,al)     ((((var)+(al)-1)/(al))*(al))

void *mic_eq_open(u32 sample_rate, u8 ch_num, u8 eq_name);
void *mic_drc_open(u32 sample_rate, u8 ch_num, u8 eq_name);
void mic_eq_close(void *eq);
void mic_drc_close(void  *drc);
void mic_effect_echo_parm_parintf(struct echo_update_parm *parm);
void *mic_energy_detect_open(u32 sr, u8 ch_num);
void mic_energy_detect_close(void *hdl);
void *mic_tunning_eq_open(u32 sample_rate, u8 ch_num);
void mic_tunning_eq_close(void *eq);

#if MIC_EFFECT_LLNS
static int mic_effect_ns_init(int sr, float gainfloor, float suppress_level);
static int mic_effect_ns_run(s16 *data, int len);
static int mic_effect_ns_exit(void);
#endif // MIC_EFFECT_LLNS

#define MIC_DIGITAL_OdB_VOLUME             441000
static void reverb_data_fade_in(struct __mic_effect *dec, s16 *data, int len, int channels)
{
    if (global_bit_wide) {
        if (dec->fade_trigger) {
            int tmp = 0;
            s32 *p = (s32 *)data;
            u16 frames = (len >> 2) / channels;
            for (int i = 0; i < frames; i++) {
                for (int j = 0; j < channels; j++) {
                    tmp = *p;
                    *p++ = (long long)dec->fade_volume * (long long)tmp / MIC_DIGITAL_OdB_VOLUME;
                }
                dec->fade_volume += dec->fade_step;
                if (dec->fade_volume > MIC_DIGITAL_OdB_VOLUME) {
                    dec->fade_volume = MIC_DIGITAL_OdB_VOLUME;
                    break;
                }
            }
            if (dec->fade_volume == MIC_DIGITAL_OdB_VOLUME) {
                dec->fade_trigger = 0;
            }
        }
    } else {
        if (dec->fade_trigger) {
            int tmp = 0;
            s16 *p = data;
            u16 frames = (len >> 1) / channels;
            for (int i = 0; i < frames; i++) {
                for (int j = 0; j < channels; j++) {
                    tmp = *p;
                    *p++ = (long long)dec->fade_volume * (long long)tmp / MIC_DIGITAL_OdB_VOLUME;
                }
                dec->fade_volume += dec->fade_step;
                if (dec->fade_volume > MIC_DIGITAL_OdB_VOLUME) {
                    dec->fade_volume = MIC_DIGITAL_OdB_VOLUME;
                    break;
                }
            }
            if (dec->fade_volume == MIC_DIGITAL_OdB_VOLUME) {
                dec->fade_trigger = 0;
            }
        }
    }
}


#if LOUDNESS_DEBUG_ENABLE
static int peak_calc(short *mono_pcm, int npoint)
{
    int abs_tmp;
    int peak;
    peak = __builtin_abs(*mono_pcm);
    mono_pcm++;
    for (int i = 1; i < npoint; i++) {
        abs_tmp = __builtin_abs(*mono_pcm);
        if (peak < abs_tmp) {
            peak = abs_tmp;
        }
        mono_pcm++;
    }
    return peak; // db = 20*log10(peak/2^(nbit-1))
}

static int peak_calc_32bit(int *mono_pcm, int npoint)
{
    int abs_tmp;
    int peak;
    peak = __builtin_abs(*mono_pcm);
    mono_pcm++;
    for (int i = 1; i < npoint; i++) {
        abs_tmp = __builtin_abs(*mono_pcm);
        if (peak < abs_tmp) {
            peak = abs_tmp;
        }
        mono_pcm++;
    }
    return peak; // db = 20*log10(peak/2^(nbit-1))
}

extern void put_float(double fv);
static float peak_dB_cal(void *data, int len, u8 bit_wide)
{
    u16 point_offset = 1;
    if (bit_wide) {
        point_offset = 2;
    }
    u32 npoint_per_ch = len >> point_offset;
    void *mono_pcm = data;
    int peak = bit_wide ? peak_calc_32bit((int *)mono_pcm, npoint_per_ch) + 1 : peak_calc(mono_pcm, npoint_per_ch) + 1;

    int nbit = bit_wide ? 24 : 16;
    float dB = 20 * log10_float(peak) - 20 * log10_float((float)(1 << (nbit - 1)));

    return dB;
}


static void peak_dB_cal_ctrl(struct peak_dB *hdl, void *data, int len, int ch_num)
{
    void *mono_pcm = NULL;
    float dB = 0;
    if (ch_num == 2) {
        mono_pcm = malloc(len / 2);
        if (!mono_pcm) {
            return;
        }
        if (global_bit_wide) {
            pcm_dual_to_single_32bit(mono_pcm, data, len);
        } else {
            pcm_dual_to_single(mono_pcm, data, len);
        }
        dB = peak_dB_cal(mono_pcm, len / 2, global_bit_wide);
        free(mono_pcm);
    } else {
        mono_pcm = data;
        dB = peak_dB_cal(mono_pcm, len, global_bit_wide);
    }


    if (dB > hdl->max_dB) {
        hdl->max_dB = dB;
    }
    u16 thread_cnt = PRINTF_LOUNDNESS_TIMER * MIC_EFFECT_SAMPLERATE / (1000 * 256);
    if (hdl->cnt++ > thread_cnt) {
        hdl->cnt = 0;
        if (hdl->max_dB > LOUDNESS_THREAD_dB) {
            if (hdl->index) {
                printf("MIC_EFF_OUT maxdB: %d\n", (int)hdl->max_dB);
            } else {
                printf("ADC_OUT maxdB: %d\n", (int)hdl->max_dB);
            }
        }
        hdl->max_dB = -138;
    }
}
#endif

#if (TCFG_AUDIO_DAC_CONNECT_MODE == DAC_OUTPUT_MONO_LR_DIFF)
void *mic_data_viewer = NULL;
void *mic_data_viewer_entry = NULL;

static void pcm_LR_to_DUAL_DIFF(s16 *pcm_lr, s16 *pcm_dual_diff, int points_per_ch)
{
    s16 pcm_L;
    s16 pcm_R;
    s16 pcm_diff;
    int i = 0;

    for (i = 0; i < points_per_ch; i++, pcm_lr += 2) {
        pcm_L = *pcm_lr;
        pcm_R = *(pcm_lr + 1);
        pcm_diff = (s16)(((int)pcm_L + pcm_R) >> 1);
        *pcm_dual_diff++ = pcm_diff;
        *pcm_dual_diff++ = (pcm_diff == -32768) ? (32767) : (-pcm_diff);
    }
}
static void pcm_LR_to_DUAL_DIFF_32(s32 *pcm_lr, s32 *pcm_dual_diff, int points_per_ch)
{
    s32 pcm_L;
    s32 pcm_R;
    s32 pcm_diff;
    int i = 0;

    for (i = 0; i < points_per_ch; i++, pcm_lr += 2) {
        pcm_L = *pcm_lr;
        pcm_R = *(pcm_lr + 1);
        pcm_diff = (s32)(((int)pcm_L + pcm_R) >> 1);
        *pcm_dual_diff++ = pcm_diff;
        *pcm_dual_diff++ = (pcm_diff == -8388608) ? (8388607) : (-pcm_diff);;
    }
}

static void mic_data_viewer_callback(u8 ch, s16 *data, u32 len)
{
    if (global_bit_wide) {
        pcm_LR_to_DUAL_DIFF_32((s32 *)data, (s32 *)data, len / 2 / 2 / 2);
    } else {
        pcm_LR_to_DUAL_DIFF(data, data, len / 2 / 2);
    }
}

#endif


s16 *aud_reverb_process_run(struct aud_reverb_process *hdl, s16 *data, int len);
s16 *aud_reverb_process_run32(struct aud_reverb_process *hdl, s16 *data, int len);
static void audio_reverb_process_output_data_process_len(struct audio_stream_entry *entry,  int len)
{
    struct aud_reverb_process *hdl = container_of(entry, struct aud_reverb_process, entry);
}
static int audio_reverb_process_data_handler(struct audio_stream_entry *entry,
        struct audio_data_frame *in,
        struct audio_data_frame *out)
{
    struct aud_reverb_process *hdl = container_of(entry, struct aud_reverb_process, entry);
    /* if (in->data_len != 304) { */
    /* printf("in->data_len %d\n", in->data_len); */
    /* } */

    out->data_sync = in->data_sync;

    struct __mic_effect  *eff = hdl->eff;
    if (eff->sub_0_plate_reverb_hdl) {
        out->channel = 2;//in->channel;
    } else {
        out->channel = in->channel;
    }
    out->data_len = out->channel * in->data_len;
    hdl->in_ch = in->channel;
    if (global_bit_wide) {
        out->data = aud_reverb_process_run32(hdl, (short *)((int)in->data + in->offset), (in->data_len - in->offset)); //默认输出2ch
        reverb_data_fade_in(hdl->eff, out->data, out->data_len, out->channel);
        if (out->channel == 1) {
            pcm_dual_to_single_32bit((void *)out->data, (void *)out->data, in->data_len * 2);
        }
    } else {
        out->data = aud_reverb_process_run(hdl, (short *)((int)in->data + in->offset), (in->data_len - in->offset)); //默认输出2ch
        reverb_data_fade_in(hdl->eff, out->data, out->data_len, out->channel);
        if (out->channel == 1) {
            pcm_dual_to_single((void *)out->data, (void *)out->data, in->data_len * 2);
        }
    }
    return in->data_len;
}


struct aud_reverb_process *aud_reverb_open(struct __mic_effect *eff, u8 bit_wide)
{
    struct aud_reverb_process *hdl = zalloc(sizeof(struct aud_reverb_process));
    hdl->eff = eff;
    hdl->bit_wide = bit_wide;
    hdl->entry.data_process_len = audio_reverb_process_output_data_process_len;
    hdl->entry.data_handler = audio_reverb_process_data_handler;

    return hdl;
}

void aud_reverb_close(struct aud_reverb_process *hdl)
{
    if (!hdl) {
        return;
    }
    local_irq_disable();
    audio_stream_del_entry(&hdl->entry);
    local_irq_enable();
    for (int i = 0; i < 3; i++) {
        if (hdl->tmpbuf[i]) {
            free(hdl->tmpbuf[i]);
            hdl->tmpbuf[i] = NULL;
        }
    }
    free(hdl);
    hdl = NULL;
}


s16 *aud_reverb_process_run32(struct aud_reverb_process *hdl, s16 *data, int len)
{
    struct __mic_effect  *eff = hdl->eff;
    u8 ch_num = eff->input_ch_num;
#if MIC_EFFECT_LLNS
    mic_effect_ns_run(data, len);
#endif // MIC_EFFECT_LLNS
    //data 32 ,
#if 0
    //for test
    if (!hdl->tmpbuf[0]) {
        hdl->tmpbuf[0] = malloc(len * 2);//2ch 32 bit buf
    }
    pcm_single_to_dual_32bit((void *)hdl->tmpbuf[0], (void *)data,  len);//32bit 2ch len->len*2
    return hdl->tmpbuf[0];
#endif

    s16 *bk_buf = malloc(len * 2);//2 ch 32bit
    if (!bk_buf) {
        printf("mic_eff bk_buf alloc err\n");
        return data;
    }
    if (eff->sub_0_plate_reverb_hdl) {//in 32bit 1ch
        //ch0
        if (!hdl->tmpbuf[0]) {
            hdl->tmpbuf[0] = malloc(len * 2);//2ch 32 bit buf
        }
#if 0
        pcm_single_to_dual_32bit((void *)hdl->tmpbuf[0], (void *)data,  len);//32bit 2ch len->len*2
#else
#if REVERB_ECHO_RUN_32BIT
        run_plate_reverb(eff->sub_0_plate_reverb_hdl, data, hdl->tmpbuf[0], len); //32bit 内部单变双 len->len*2
        audio_dec_eq_run(eff->mic_eq1, hdl->tmpbuf[0], hdl->tmpbuf[0], len * 2); // 32bit 2ch  len*2->len*2
        audio_dec_drc_run(eff->mic_drc1, hdl->tmpbuf[0], len * 2); //32bit 2ch
#else
        audio_convert_data_32bit_to_16bit_round(data, bk_buf, len / 4);//32->16  len->len/2
        run_plate_reverb(eff->sub_0_plate_reverb_hdl, bk_buf, hdl->tmpbuf[0], len / 2); //16bit 内部单变双 len/2->len
        audio_dec_eq_run(eff->mic_eq1, hdl->tmpbuf[0], bk_buf, len);// 16bit 2ch  len->len
        audio_dec_drc_run(eff->mic_drc1, hdl->tmpbuf[0], len); //16bit 2ch
        audio_convert_data_16bit_to_32bit_round(bk_buf, hdl->tmpbuf[0], (len * 2) / 4);
#endif
#endif
    }

    //ch1  in 32bit 1ch
    u32 points = 0;
    if (eff->sub_1_echo_hdl) {
        if (!hdl->tmpbuf[1]) {
            hdl->tmpbuf[1] = malloc(len * 2);//2ch 32 bit buf
        }
#if 0
        pcm_single_to_dual_32bit((void *)hdl->tmpbuf[1], (void *)data,  len);//32bit 2ch len->len*2
#else
        //ch1
#if REVERB_ECHO_RUN_32BIT
        run_echo(eff->sub_1_echo_hdl, data, bk_buf, len);  //1ch 32bit len
        audio_dec_eq_run(eff->mic_eq2, bk_buf, NULL, len);   //1ch 32bit len
        audio_dec_drc_run(eff->mic_drc2, bk_buf, len); //32bit 1ch
        pcm_single_to_dual_32bit((void *)hdl->tmpbuf[1], (void *)bk_buf,  len); //16bit 2ch len/2->len
#else
        audio_convert_data_32bit_to_16bit_round(data, bk_buf, len / 4);//1ch 32->16 len->len/2
        run_echo(eff->sub_1_echo_hdl, bk_buf, hdl->tmpbuf[1], len / 2);  //1ch 16bit len/2
        audio_dec_eq_run(eff->mic_eq2, hdl->tmpbuf[1], NULL, len / 2);   //1ch 16bit len/2
        audio_dec_drc_run(eff->mic_drc2, hdl->tmpbuf[1], len / 2); //16bit 2ch
        pcm_single_to_dual((void *)bk_buf, (void *)hdl->tmpbuf[1],  len / 2); //16bit 2ch len/2->len
        audio_convert_data_16bit_to_32bit_round(bk_buf, hdl->tmpbuf[1], (len * 2) / 4); //16->32  len->len*2
#endif
#endif
    }
    free(bk_buf);

    if (!hdl->tmpbuf[2]) {
        hdl->tmpbuf[2] = malloc(len * 2);//2ch 16 bit buf
    }
    //ch2 in 32bit 1ch
    audio_dec_eq_run(eff->mic_eq3, data, NULL, len);//32bit 1ch
    audio_dec_drc_run(eff->mic_drc3, data, len); //32bit 1ch
    pcm_single_to_dual_32bit((void *)hdl->tmpbuf[2], (void *)data,  len);//32bit 2ch len->len*2
    points = (len * 2) / 4;


    MixParam mix0  = {0};
    MixParam mix1  = {0};
    MixParam mix2  = {0};

    u8 mode = get_mic_eff_mode();
    Mix_TOOL_SET *mix_gain = &eff_mode[mode].mix_gain;
    mix0.data = hdl->tmpbuf[0];
    mix0.gain = mix_gain->gain1;
    mix1.data = hdl->tmpbuf[1];
    mix1.gain = mix_gain->gain2;
    mix2.data = hdl->tmpbuf[2];
    mix2.gain = mix_gain->gain3;
    u8 out_ch_num = 2;

    if (eff->sub_1_echo_hdl && eff->sub_0_plate_reverb_hdl) {
        Mix32to32(&mix0, &mix1, &mix2, hdl->tmpbuf[0], out_ch_num, 3, points / out_ch_num);
    } else if (eff->sub_0_plate_reverb_hdl) {
        Mix32to32(&mix0, &mix2, NULL, hdl->tmpbuf[0], out_ch_num, 2, points / out_ch_num);
    } else {
        Mix32to32(&mix1, &mix2, NULL, hdl->tmpbuf[1], out_ch_num, 2, points / out_ch_num);
        return hdl->tmpbuf[1];
    }
    return hdl->tmpbuf[0];
}

s16 *aud_reverb_process_run(struct aud_reverb_process *hdl, s16 *data, int len)
{
    struct __mic_effect  *eff = hdl->eff;
    u8 ch_num = eff->input_ch_num;
#if MIC_EFFECT_LLNS
    mic_effect_ns_run(data, len);
#endif // MIC_EFFECT_LLNS
    if (eff->sub_0_plate_reverb_hdl) {
        //ch0
        if (hdl->in_ch == 2) {
            if (!hdl->tmpbuf[0]) {
                hdl->tmpbuf[0] = malloc(len);//2ch 16 bit buf
            }
            u8 *tmp = (u8 *)hdl->tmpbuf[0];
            s16 *tar = (s16 *)&tmp[len / 2];
            pcm_dual_to_single((void *)tar, (void *)data, len);
            run_plate_reverb(eff->sub_0_plate_reverb_hdl, tar, hdl->tmpbuf[0], len / 2); //内部单变双
        } else {
            if (!hdl->bit_wide) { //16 bit
                if (!hdl->tmpbuf[0]) {
                    hdl->tmpbuf[0] = malloc(len * 2);//2ch 16 bit buf
                }
                run_plate_reverb(eff->sub_0_plate_reverb_hdl, data, hdl->tmpbuf[0], len); //16bit 内部单变双
                audio_dec_eq_run(eff->mic_eq1, hdl->tmpbuf[0], hdl->tmpbuf[0], len * 2);// 16bit 2ch
                audio_dec_drc_run(eff->mic_drc1, hdl->tmpbuf[0], len * 2); //16bit 2ch
            } else {
#define OFFSET_POINTS  128  //eq输入输出位宽不一致时，每次计算是32个点，如需复用输入buf，输入buf需比输出buf滞后32个点,以防buf追尾
                if (!hdl->tmpbuf[0]) {
                    hdl->tmpbuf[0] = malloc(len * 2 * 2 + OFFSET_POINTS);//2ch 32 bit buf
                }
                u8 *tmp = (u8 *)hdl->tmpbuf[0];

                s16 *tar = (s16 *)&tmp[OFFSET_POINTS + len * 2 * 2 - 2 * len]; //放在最后 16bit 2ch,防止buf追尾，reverb输出位置往后偏128byte
                run_plate_reverb(eff->sub_0_plate_reverb_hdl, data, tar, len); //16 bit 内部单变双
                audio_dec_eq_run(eff->mic_eq1, tar, hdl->tmpbuf[0], len * 2);// 32bit 2ch out
                audio_dec_drc_run(eff->mic_drc1, hdl->tmpbuf[0], len * 2 * 2); //32bit 2ch
            }
        }
    }

    //ch1
    u32 points = 0;
    if (hdl->in_ch == 1) {
        if (!hdl->bit_wide) { //16 bit

            u8 *tmp = NULL;
            s16 *tar = NULL;
            if (eff->sub_1_echo_hdl) {
                if (!hdl->tmpbuf[1]) {
                    hdl->tmpbuf[1] = malloc(len * 2);//2ch 16 bit buf
                }
                //ch1
                tmp = (u8 *)hdl->tmpbuf[1];
                tar = (s16 *)&tmp[len];
                run_echo(eff->sub_1_echo_hdl, data, tar, len);
                audio_dec_eq_run(eff->mic_eq2, tar, NULL, len); //1ch
                audio_dec_drc_run(eff->mic_drc2, tar, len); //16bit 1ch
                pcm_single_to_dual((void *)hdl->tmpbuf[1], (void *)tar,  len);//16bit 2ch
            }

            if (!hdl->tmpbuf[2]) {
                hdl->tmpbuf[2] = malloc(len * 2);//2ch 16 bit buf
            }
            //ch2
            audio_dec_eq_run(eff->mic_eq3, data, NULL, len);//1ch
            audio_dec_drc_run(eff->mic_drc3, data, len); //16bit 1ch
            pcm_single_to_dual((void *)hdl->tmpbuf[2], (void *)data,  len);//16bit 2ch
            points = (len * 2) / 2;
        } else {
            u8 *tmp = NULL;
            s16 *tar = NULL;
            if (eff->sub_1_echo_hdl) {
                if (!hdl->tmpbuf[1]) {
                    hdl->tmpbuf[1] = malloc(len * 2 * 2); //2ch 32 bit buf
                }
                //ch1
                tmp = (u8 *)hdl->tmpbuf[1];
                tar = (s16 *)&tmp[len * 2 * 2 - len]; //放在最后 16bit 1ch
                s16 *tar2 = (s16 *)&tmp[len * 2]; //buf中间位置
                run_echo(eff->sub_1_echo_hdl, data, tar, len);
                audio_dec_eq_run(eff->mic_eq2, tar, tar2, len); //16 ->32 1ch
                audio_dec_drc_run(eff->mic_drc2, tar2, len * 2); //32bit 1ch
                pcm_single_to_dual_32bit(hdl->tmpbuf[1], tar2, len * 2); //32bit 2ch out
            }

            //ch2
            if (!hdl->tmpbuf[2]) {
                hdl->tmpbuf[2] = malloc(len * 2 * 2); //2ch 32 bit buf
            }
            tmp = (u8 *)hdl->tmpbuf[2];
            tar = (s16 *)&tmp[len * 2]; //buf中间位置
            audio_dec_eq_run(eff->mic_eq3, data, tar, len);//16->32bit 1ch
            audio_dec_drc_run(eff->mic_drc3, tar, len * 2); //32bit 1ch
            pcm_single_to_dual_32bit(hdl->tmpbuf[2], tar,  len * 2); //32bit 2ch

            points = (len * 2 * 2) / 4;
        }
    } else {
        if (!hdl->bit_wide) { //16 bit

            u8 *tmp = NULL;
            s16 *tar = NULL;
            if (eff->sub_1_echo_hdl) {
                if (!hdl->tmpbuf[1]) {
                    hdl->tmpbuf[1] = malloc(len);//2ch 16 bit buf
                }
                tmp = (u8 *)hdl->tmpbuf[1];
                tar = (s16 *)&tmp[len / 2];
                pcm_dual_to_single((void *)tar, (void *)data, len);
                run_echo(eff->sub_1_echo_hdl, tar, tar, len / 2);
                pcm_single_to_dual((void *)tmp, (void *)tar,  len / 2);
                audio_dec_eq_run(eff->mic_eq2, (s16 *)tmp, hdl->tmpbuf[1], len); //立体声处理2ch
                audio_dec_drc_run(eff->mic_drc2, hdl->tmpbuf[1], len); //16bit 2ch
            }

            //ch2
            if (!hdl->tmpbuf[2]) {
                hdl->tmpbuf[2] = malloc(len);//2ch 16bit buf
            }
            audio_dec_eq_run(eff->mic_eq3, data, hdl->tmpbuf[2], len);
            audio_dec_drc_run(eff->mic_drc3, hdl->tmpbuf[2], len); //16bit 2ch
            points = len / 2;
        } else {

            u8 *tmp = NULL;
            s16 *tar = NULL;
            if (eff->sub_1_echo_hdl) {
                if (!hdl->tmpbuf[1]) {
                    hdl->tmpbuf[1] = malloc(len * 2); //2ch 32 bit buf
                }
                //ch1
                tmp = (u8 *)hdl->tmpbuf[1];
                tar = (s16 *)&tmp[len * 2 - len / 2];
                pcm_dual_to_single((void *)tar, (void *)data, len);
                run_echo(eff->sub_1_echo_hdl, tar, tar, len / 2);
                pcm_single_to_dual((void *)tmp, (void *)tar,  len / 2);
                audio_dec_eq_run(eff->mic_eq2, (s16 *)tmp, hdl->tmpbuf[1], len); //立体声处理2ch
                audio_dec_drc_run(eff->mic_drc2, hdl->tmpbuf[1], len * 2); //32bit 2ch
            }

            //ch2
            if (!hdl->tmpbuf[2]) {
                hdl->tmpbuf[2] = malloc(len * 2 * 2); //2ch 32 bit buf
            }
            audio_dec_eq_run(eff->mic_eq3, data, hdl->tmpbuf[2], len);
            audio_dec_drc_run(eff->mic_drc3, hdl->tmpbuf[2], len * 2); //32bit 2ch
            points = (len * 2) / 4;
        }
    }
    MixParam mix0  = {0};
    MixParam mix1  = {0};
    MixParam mix2  = {0};

    u8 mode = get_mic_eff_mode();
    Mix_TOOL_SET *mix_gain = &eff_mode[mode].mix_gain;
    mix0.data = hdl->tmpbuf[0];
    mix0.gain = mix_gain->gain1;
    mix1.data = hdl->tmpbuf[1];
    mix1.gain = mix_gain->gain2;
    mix2.data = hdl->tmpbuf[2];
    mix2.gain = mix_gain->gain3;
    u8 out_ch_num = 2;
    if (!hdl->bit_wide) { //16 bit
        if (eff->sub_1_echo_hdl && eff->sub_0_plate_reverb_hdl) {
            Mix16to16(&mix0, &mix1, &mix2, hdl->tmpbuf[0], out_ch_num, 3, points / out_ch_num);
        } else if (eff->sub_0_plate_reverb_hdl) {
            Mix16to16(&mix0, &mix2, NULL, hdl->tmpbuf[0], out_ch_num, 2, points / out_ch_num);
        } else {
            Mix16to16(&mix1, &mix2, NULL, hdl->tmpbuf[1], out_ch_num, 2, points / out_ch_num);
            return hdl->tmpbuf[1];
        }
        return hdl->tmpbuf[0];
    } else {
        if (eff->sub_1_echo_hdl && eff->sub_0_plate_reverb_hdl) {
            Mix32to16(&mix0, &mix1, &mix2, hdl->tmpbuf[0], out_ch_num, 3, points / out_ch_num);
        } else if (eff->sub_0_plate_reverb_hdl) {
            Mix32to16(&mix0, &mix2, NULL, hdl->tmpbuf[0], out_ch_num, 2, points / out_ch_num);
        } else {
            Mix32to16(&mix1, &mix2, NULL, hdl->tmpbuf[1], out_ch_num, 2, points / out_ch_num);
            return hdl->tmpbuf[1];
        }
    }
    return hdl->tmpbuf[0];
}


/*----------------------------------------------------------------------------*/
/**@brief    mic数据流串接入口
  @param
  @return
  @note
*/
/*----------------------------------------------------------------------------*/
static u32 mic_effect_effect_run(void *priv, void *in, void *out, u32 inlen, u32 outlen)
{
    struct __mic_effect *effect = (struct __mic_effect *)priv;
    if (effect == NULL) {
        return 0;
    }
    struct audio_data_frame frame = {0};
    frame.channel = effect->input_ch_num;
    frame.sample_rate = effect->parm.sample_rate;
    frame.data_len = inlen;
    frame.data = in;
    effect->out_len = 0;
    effect->process_len = inlen;

    if (effect->pause_mark) {
        memset(in, 0, inlen);
        return inlen;
    } else {

    }
#if LOUDNESS_DEBUG_ENABLE
    peak_dB_cal_ctrl(&effect->loudness_adc, in, inlen, effect->input_ch_num);
#endif
    while (1) {
        audio_stream_run(&effect->entry, &frame);
        if (effect->out_len >= effect->process_len) {
            break;
        }
        frame.data = (s16 *)((u8 *)in + effect->out_len);
        frame.data_len = inlen - effect->out_len;
    }
    return outlen;
}

/*----------------------------------------------------------------------------*/
/**@brief   释放mic数据流资源
   @param
   @return
   @note
*/
/*----------------------------------------------------------------------------*/
static void mic_effect_destroy(struct __mic_effect **hdl)
{
    if (hdl == NULL || *hdl == NULL) {
        return ;
    }
#if MIC_EFFECT_LLNS
    mic_effect_ns_exit();
#endif // MIC_EFFECT_LLNS
    struct __mic_effect *effect = *hdl;
    if (effect->mic) {
        log_i("mic_stream_destroy\n\n\n");
        mic_stream_destroy(&effect->mic);
    }

#if TCFG_MIC_DODGE_EN
    if (effect->energy_hdl) {
        mic_energy_detect_close(effect->energy_hdl);
    }
#endif

    if (effect->noisegate) {
        log_i("close_noisegate\n\n\n");
        audio_noisegate_close(effect->noisegate);
    }


    if (effect->howling_ps) {
        log_i("close_howling\n\n\n");
        close_howling(effect->howling_ps);
    }
    if (effect->notch_howling) {
        log_i("close_howling\n\n\n");
        close_howling(effect->notch_howling);
    }
#if defined(TCFG_MIC_VOICE_CHANGER_ENABLE) && TCFG_MIC_VOICE_CHANGER_ENABLE
    if (effect->voice_changer) {
        audio_voice_changer_close_demo(effect->voice_changer);
    }
#endif
#if defined(TCFG_MIC_HARMONIC_EXCITER_ENABLE) &&TCFG_MIC_HARMONIC_EXCITER_ENABLE
    if (effect->harmonic_exciter) {
        audio_harmonic_exciter_close_api(effect->harmonic_exciter);
    }
#endif

#if defined(TCFG_MIC_AUTOTUNE_ENABLE) && TCFG_MIC_AUTOTUNE_ENABLE
    if (effect->autotune) {
        audio_autotune_close_demo(effect->autotune);
    }
#endif
    if (effect->mic_eq0) {
        log_i("mic_eq0_close\n\n\n");
        mic_eq_close(effect->mic_eq0);
    }
#if TCFG_EQ_ENABLE && TCFG_MIC_BASS_AND_TREBLE_ENABLE
    if (effect->mic_tunning_eq) {
        mic_tunning_eq_close(effect->mic_tunning_eq);
    }
#endif
    if (effect->mic_drc0) {
        log_i("mic_drc0_close\n\n\n");
        mic_drc_close(effect->mic_drc0);
    }
    if (effect->convert0) {
        log_i("convet0_data_close\n\n\n");
        convet_data_close(effect->convert0);
    }

    if (effect->mic_eq1) {
        log_i("mic_eq1_close\n\n\n");
        mic_eq_close(effect->mic_eq1);
    }
    if (effect->mic_drc1) {
        log_i("mic_drc1_close\n\n\n");
        mic_drc_close(effect->mic_drc1);
    }

    if (effect->mic_eq2) {
        log_i("mic_eq2_close\n\n\n");
        mic_eq_close(effect->mic_eq2);
    }
    if (effect->mic_drc2) {
        log_i("mic_drc2_close\n\n\n");
        mic_drc_close(effect->mic_drc2);
    }

    if (effect->mic_eq3) {
        log_i("mic_eq3_close\n\n\n");
        mic_eq_close(effect->mic_eq3);
    }
    if (effect->mic_drc3) {
        log_i("mic_drc3_close\n\n\n");
        mic_drc_close(effect->mic_drc3);
    }

    if (effect->mic_eq4) {
        log_i("mic_eq4_close\n\n\n");
        mic_eq_close(effect->mic_eq4);
    }
    if (effect->mic_drc4) {
        log_i("mic_drc4_close\n\n\n");
        mic_drc_close(effect->mic_drc4);
    }

    if (effect->convert4) {
        log_i("convet4_data_close\n\n\n");
        convet_data_close(effect->convert4);
    }
    /* if (effect->gain) { */
    /* log_i("audio_gain_close_demo\n\n\n"); */
    /* audio_gain_close_demo(effect->gain); */
    /* effect->gain = NULL; */
    /* } */
    if (effect->sub_1_echo_hdl) {
        close_echo(effect->sub_1_echo_hdl);
    }
    if (effect->sub_0_plate_reverb_hdl) {
        close_plate_reverb(effect->sub_0_plate_reverb_hdl);
    }
    if (effect->d_vol) {
        audio_stream_del_entry(audio_dig_vol_entry_get(effect->d_vol));
#if defined(SYS_DIGVOL_GROUP_EN) && SYS_DIGVOL_GROUP_EN
        sys_digvol_group_ch_close("mic_mic");
#else
        audio_dig_vol_close(effect->d_vol);
#endif/*SYS_DIGVOL_GROUP_EN*/
    }
    if (effect->linein) {
        effect_linein_close(&effect->linein);
    }

#if (SOUNDCARD_ENABLE)
    if (mic_mix_fifo_ch != NULL) {
        void *entry = mix_fifo_ch_get_entry(mic_mix_fifo_ch);
        if (entry != NULL) {
            audio_stream_del_entry(entry);
        }
    }
    mix_fifo_ch_close(mic_mix_fifo_ch);
#endif

#if (RECORDER_MIX_EN)
    if (effect->rec_hdl != NULL) {
        void *entry = rec_mix_fifo_ch_get_entry(effect->rec_hdl);
        if (entry != NULL) {
            audio_stream_del_entry(entry);
        }
    }
    rec_mix_fifo_ch_close(effect->rec_hdl);
#endif

    if (effect->channel_zoom) {
        channel_switch_close(&effect->channel_zoom);
        /*effect->channel_zoom = NULL;*/
    }
    if (effect->loudness_debug_hdl) {
        stream_entry_close(&effect->loudness_debug_hdl);
    }

#if (TCFG_USB_MIC_DATA_FROM_MICEFFECT||TCFG_USB_MIC_DATA_FROM_DAC)
    if (effect->usbmic_hdl) {
        stream_entry_close(&effect->usbmic_hdl);
    }
#endif
    if (effect->aud_reverb) {
        aud_reverb_close(effect->aud_reverb);
    }

    audio_reverb_stream_dac_uninit();
    if (effect->stream) {
        audio_stream_close(effect->stream);
    }
    local_irq_disable();
    free(effect);
    *hdl = NULL;
    local_irq_enable();

    mem_stats();
    clock_remove_set(REVERB_CLK);
}
/*----------------------------------------------------------------------------*/
/**@brief    串流唤醒
   @param
   @return
   @note 暂未使用
*/
/*----------------------------------------------------------------------------*/
static void mic_stream_resume(void *p)
{
    struct __mic_effect *effect = (struct __mic_effect *)p;
    /* audio_decoder_resume_all(&decode_task); */
}

/*----------------------------------------------------------------------------*/
/**@brief    串流数据处理长度回调
   @param
   @return
   @note
*/
/*----------------------------------------------------------------------------*/
static void mic_effect_data_process_len(struct audio_stream_entry *entry, int len)
{

    struct __mic_effect *effect = container_of(entry, struct __mic_effect, entry);
    effect->out_len += len;
    /* printf("out len[%d]",effect->out_len); */
}

extern int usb_audio_mic_write_do(void *data, u16 len);
static int mic_effect_otherout_stream_callback(void *priv, struct audio_data_frame *in)
{
    struct __mic_effect *effect = (struct __mic_effect *)priv;
    s16 *data = in->data;
    u32 len = in->data_len;

#if ((TCFG_USB_MIC_DATA_FROM_MICEFFECT||TCFG_USB_MIC_DATA_FROM_DAC))
    if (effect->usbmic_start) {
        if (len) {
            usb_audio_mic_write_do(data, len);
        }
    } else {
    }
#endif
    return len;
}

void mic_effect_to_usbmic_onoff(u8 mark)
{
#if (TCFG_USB_MIC_DATA_FROM_MICEFFECT||TCFG_USB_MIC_DATA_FROM_DAC)
    if (__this) {
        __this->usbmic_start = mark ? 1 : 0;
    }
#endif
}

static void  inline rl_rr_mix_to_rl_rr(short *data, int len)
{
    s32 tmp32_1;
    s32 tmp32_2;
    s16 *inbuf = data;
    inbuf = inbuf + 2;  //定位到第三通道
    len >>= 3;
    __asm__ volatile(
        "1:                      \n\t"
        "rep %0 {                \n\t"
        "  %2 = h[%1 ++= 2](s)     \n\t"  //取第三通道值，并地址偏移两个字节指向第四通道数据
        " %3 = h[%1 ++= -2](s)   \n\t"   //取第四通道值，并地址偏移两个字节指向第三通道数据
        " %2 = %2 + %3           \n\t"
        " %2 = sat16(%2)(s)      \n\t"  //饱和处理
        " h[%1 ++= 2] = %2      \n\t"  //存取第三通道数据，并地址偏移两个字节指向第四通道数据
        " h[%1 ++= 6] = %2      \n\t"  //存取第四通道数据，并地址偏移六个字节指向第三通道相邻的数据
        "}                      \n\t"
        "if(%0 != 0) goto 1b    \n\t"
        :
        "=&r"(len),
        "=&r"(inbuf),
        "=&r"(tmp32_1),
        "=&r"(tmp32_2)
        :
        "0"(len),
        "1"(inbuf),
        "2"(tmp32_1),
        "3"(tmp32_2)
        :
    );
}
static int effect_to_dac_data_pro_handle(struct audio_stream_entry *entry,  struct audio_data_frame *in)
{
    return 0;
}

#if TCFG_APP_RECORD_EN
static int prob_handler_to_record(struct audio_stream_entry *entry,  struct audio_data_frame *in)
{
    if (app_get_curr_task() != APP_RECORD_TASK) {
        return 0;
    }
    if (in->data_len == 0) {
        return 0;
    }
    if (recorder_is_encoding() == 0) {
        return 0;
    }
    int  wlen = recorder_userdata_to_enc(in->data, in->data_len);
    if (wlen != in->data_len) {
        putchar('N');
    }
    return 0;
}
#endif
int audio_data_check_cb(void *priv,  struct audio_data_frame *in)
{
    if (__this) {
        peak_dB_cal_ctrl(&__this->loudness_drc_out, in->data, in->data_len, in->channel);
    }
    return in->data_len;
}

#if (TCFG_AUDIO_DAC_CONNECT_MODE == DAC_OUTPUT_LR || TCFG_AUDIO_DAC_CONNECT_MODE == DAC_OUTPUT_MONO_LR_DIFF)
#define DAC_OUTPUT_CHANNELS     2
#elif (TCFG_AUDIO_DAC_CONNECT_MODE == DAC_OUTPUT_MONO_L || TCFG_AUDIO_DAC_CONNECT_MODE == DAC_OUTPUT_MONO_R || TCFG_AUDIO_DAC_CONNECT_MODE == DAC_OUTPUT_LR_DIFF)
#define DAC_OUTPUT_CHANNELS     1
#elif (TCFG_AUDIO_DAC_CONNECT_MODE == DAC_OUTPUT_FRONT_LR_REAR_LR)
#define DAC_OUTPUT_CHANNELS     4
#else
#define DAC_OUTPUT_CHANNELS     3
#endif

static u8 reverb_mode = 0;
static u8 voicechange_mode = 0;
void switch_mic_effect_mode(void)
{
    reverb_mode++;
    if (reverb_mode > 2) {
        reverb_mode = 0;
    }
    printf("\n--func=%s [%d] \n", __FUNCTION__, reverb_mode);

    set_mic_reverb_mode_by_id(reverb_mode);
}
/*----------------------------------------------------------------------------*/
/**@brief    (mic数据流)混响打开接口
   @param
   @return
   @note
*/
/*----------------------------------------------------------------------------*/
bool mic_effect_start(void)
{
    bool ret = false;
    mem_stats();
    printf("\n--func=%s\n", __FUNCTION__);
    if (__this) {
        log_e("reverb is already start \n");
        return ret;
    }

#if WIRELESS_MIC_EFFECT_ENABLE
    if (live_mic_effect_status_get()) {
        log_e("live_mic_effect starting \n");
        return ret;
    }
#endif

    struct __mic_effect *effect = (struct __mic_effect *)zalloc(sizeof(struct __mic_effect));
    if (effect == NULL) {
        return false;
    }
    u8 mode = get_mic_eff_mode();
    struct eff_parm *mic_eff = &eff_mode[mode];

    clock_add_set(REVERB_CLK);
    os_mutex_create(&effect->mutex);
    memcpy(&effect->parm, &effect_parm_default, sizeof(struct __mic_effect_parm));
    struct __mic_stream_parm *mic_parm = (struct __mic_stream_parm *)&effect_mic_stream_parm_default;
    if (g_mic_parm) {
        mic_parm = g_mic_parm;
    }

#if LOUDNESS_DEBUG_ENABLE
    effect->loudness_adc.index = 0;//标识类型
    effect->loudness_drc_out.index = 1;
    effect->loudness_debug_hdl = stream_entry_open(effect, audio_data_check_cb, 0);// 能量计算及打印节点
#endif
#if MIC_EFFECT_LLNS
    mic_effect_ns_init(MIC_EFFECT_SAMPLERATE, 0.05f, 1.0f);
#endif // MIC_EFFECT_LLNS

    /* if ((TCFG_MIC_EFFECT_SEL & MIC_EFFECT_ECHO)
        && (TCFG_MIC_EFFECT_SEL & MIC_EFFECT_REVERB)) {
        log_e("effect config err ?? !!!, cann't support echo && reverb at the same time\n");
        mic_effect_destroy(&effect);
        return false;
    } */
    effect->fade_trigger = 1;
    effect->fade_volume = 0;
    u16 fade_time = 2000;//ms
    effect->fade_step = MIC_DIGITAL_OdB_VOLUME / (fade_time * effect->parm.sample_rate / 1000);

    u16 irq_points = mic_parm->point_unit;
    u8 ch_num = 1; //??
    effect->input_ch_num = ch_num;
#if TCFG_MIC_DODGE_EN
    if (effect->parm.effect_config & BIT(MIC_EFFECT_CONFIG_ENERGY_DETECT)) {
        effect->energy_hdl = mic_energy_detect_open(effect->parm.sample_rate, ch_num);
        effect->dodge_en = 0;//默认关闭， 需要通过按键触发打开
        effect->dodge_ch_num = ch_num;
    }
#endif
    ///声音门限初始化
    if (effect->parm.effect_config & BIT(MIC_EFFECT_CONFIG_NOISEGATE)) {
        effect->noisegate = audio_noisegate_open_demo(AEID_MIC_NS_GATE, MIC_EFFECT_SAMPLERATE, ch_num);
    }

///啸叫抑制初始化
    if (effect->parm.effect_config & BIT(MIC_EFFECT_CONFIG_HOWLING)) {
        log_i("open_howling\n\n\n");
        HOWLING_PITCHSHIFT_PARM parm = {0};
        parm.effect_v = mic_eff->howlingps_parm.parm.effect_v;
        parm.ps_parm = mic_eff->howlingps_parm.parm.ps_parm;
        parm.fe_parm = mic_eff->howlingps_parm.parm.fe_parm;
        parm.dataTypeobj.IndataBit = global_bit_wide ? DATA_INT_32BIT : DATA_INT_16BIT;
        parm.dataTypeobj.OutdataBit = global_bit_wide ? DATA_INT_32BIT : DATA_INT_16BIT;
        parm.dataTypeobj.IndataInc = 1;
        parm.dataTypeobj.OutdataInc = 1;
        parm.dataTypeobj.Qval = global_bit_wide ? 23 : 15;
        effect->howling_ps = open_howling(&parm, effect->parm.sample_rate, 0, 1);//以频
        howling_update_bypass(effect->howling_ps, mic_eff->howlingps_parm.is_bypass);

    }

    if (effect->parm.effect_config & BIT(MIC_EFFECT_CONFIG_HOWLING_TRAP)) {
        log_i("open_howling\n\n\n");

        NotchHowlingParam howling_param = {
            .threshold = mic_eff->notchhowling_parm.parm.threshold,
            .fade_time = mic_eff->notchhowling_parm.parm.fade_n,
            .notch_Q = mic_eff->notchhowling_parm.parm.Q,
            .notch_gain = mic_eff->notchhowling_parm.parm.gain,
            .SampleRate = MIC_EFFECT_SAMPLERATE,
            .pcm_info.IndataBit  = global_bit_wide ? DATA_INT_32BIT : DATA_INT_16BIT,
            .pcm_info.OutdataBit = global_bit_wide ? DATA_INT_32BIT : DATA_INT_16BIT,
            .pcm_info.IndataInc  = 1,
            .pcm_info.OutdataInc = 1,
            .pcm_info.Qval = global_bit_wide ? 23 : 15,
        };
        effect->notch_howling = open_howling(&howling_param, effect->parm.sample_rate, 0, 0);//陷波
        howling_update_bypass(effect->notch_howling, mic_eff->notchhowling_parm.is_bypass);
    }

#if defined(TCFG_MIC_VOICE_CHANGER_ENABLE) && TCFG_MIC_VOICE_CHANGER_ENABLE
    effect->voice_changer = audio_voice_changer_open_demo(AEID_MIC_VOICE_CHANGER, effect->parm.sample_rate);
    if (!voicechange_mode) {
        int ret = syscfg_read(CFG_VOICE_CHANGER_MODE, &voicechange_mode, sizeof(voicechange_mode));
        if (ret >= 0) {
            audio_voice_changer_mode_switch(AEID_MIC_VOICE_CHANGER, voicechange_mode);
        }

    }
#endif

#if defined(TCFG_MIC_HARMONIC_EXCITER_ENABLE) &&TCFG_MIC_HARMONIC_EXCITER_ENABLE
    effect->harmonic_exciter = audio_harmonic_exciter_open_api(AEID_MIC_HARMONIC_EXCITER, effect->parm.sample_rate, ch_num);
#endif

#if defined(TCFG_MIC_AUTOTUNE_ENABLE) && TCFG_MIC_AUTOTUNE_ENABLE
    effect->autotune = audio_autotune_open_demo(AEID_MIC_AUTOTUNE, effect->parm.sample_rate);
#endif
    log_i("effect->parm.sample_rate %d\n", effect->parm.sample_rate);
    effect->mic_eq0 = mic_eq_open(effect->parm.sample_rate, ch_num, AEID_MIC_EQ0);
    if (global_bit_wide) {
#if TCFG_EQ_ENABLE && TCFG_MIC_BASS_AND_TREBLE_ENABLE
        effect->mic_tunning_eq = mic_tunning_eq_open(effect->parm.sample_rate, ch_num);
#endif
        effect->mic_drc0 = mic_drc_open(effect->parm.sample_rate, ch_num, AEID_MIC_DRC0);
    } else {
        if (effect->mic_eq0 && effect->mic_eq0->out_32bit) {
#if TCFG_EQ_ENABLE && TCFG_MIC_BASS_AND_TREBLE_ENABLE
            effect->mic_tunning_eq = mic_tunning_eq_open(effect->parm.sample_rate, ch_num);
#endif
            effect->mic_drc0 = mic_drc_open(effect->parm.sample_rate, ch_num, AEID_MIC_DRC0);
            if (!global_bit_wide) {
                effect->convert0 = convet_data_open(0, (irq_points << 1) * ch_num);
            }
        }
    }

    effect->mic_eq3 = mic_eq_open(effect->parm.sample_rate, ch_num, AEID_MIC_EQ3);
    if (effect->mic_eq3 && effect->mic_eq3->out_32bit) {
        if (!global_bit_wide) {
            effect->mic_drc3 = mic_drc_open(effect->parm.sample_rate, ch_num, AEID_MIC_DRC3);
        }
    }

// sub 0 分流节点
    if (TCFG_MIC_EFFECT_SEL & MIC_EFFECT_ECHO) {
        ECHO_PARM_SET parm = {0};
        memcpy(&parm, &mic_eff->echo_parm.parm, sizeof(struct echo_update_parm));
#if REVERB_ECHO_RUN_32BIT
        parm.dataTypeobj.IndataBit = global_bit_wide;
        parm.dataTypeobj.OutdataBit = global_bit_wide;
        parm.dataTypeobj.IndataInc = 1;
        parm.dataTypeobj.OutdataInc = 1;
        parm.dataTypeobj.Qval = global_bit_wide ? 23 : 15;
#else
        parm.dataTypeobj.IndataBit = 0;
        parm.dataTypeobj.OutdataBit = 0;
        parm.dataTypeobj.IndataInc = 1;
        parm.dataTypeobj.OutdataInc = 1;
        parm.dataTypeobj.Qval = 15;
#endif
        effect->sub_1_echo_hdl = open_echo((ECHO_PARM_SET *)&parm, (EF_REVERB_FIX_PARM *)&effect_echo_fix_parm_default);

        if (effect->sub_1_echo_hdl) {
            echo_update_bypass(effect->sub_1_echo_hdl, mic_eff->echo_parm.is_bypass);
            effect->mic_eq2 = mic_eq_open(effect->parm.sample_rate, ch_num, AEID_MIC_EQ2);
            if (effect->mic_eq2 && effect->mic_eq2->out_32bit) {
                effect->mic_drc2 = mic_drc_open(effect->parm.sample_rate, ch_num, AEID_MIC_DRC2);
            }
        }
    }

    if (TCFG_MIC_EFFECT_SEL & MIC_EFFECT_REVERB) {
        Plate_reverb_parm parm = {0};
        memcpy(&parm, &mic_eff->plate_reverb_parm.parm, sizeof(struct  plate_reverb_update_parm));
#if REVERB_ECHO_RUN_32BIT
        parm.dataTypeobj.IndataBit = global_bit_wide;
        parm.dataTypeobj.OutdataBit = global_bit_wide;
        parm.dataTypeobj.IndataInc = 1;
        parm.dataTypeobj.OutdataInc = 2;
        parm.dataTypeobj.Qval = global_bit_wide ? 23 : 15;
#else
        parm.dataTypeobj.IndataBit = 0;
        parm.dataTypeobj.OutdataBit = 0;
        parm.dataTypeobj.IndataInc = 1;
        parm.dataTypeobj.OutdataInc = 2;
        parm.dataTypeobj.Qval = 15;
#endif
        effect->sub_0_plate_reverb_hdl = open_plate_reverb((Plate_reverb_parm *)&parm, effect->parm.sample_rate);
        if (effect->sub_0_plate_reverb_hdl) {
            plate_reverb_update_bypass(effect->sub_0_plate_reverb_hdl, mic_eff->plate_reverb_parm.is_bypass);
            ch_num = 2;
        }

        effect->mic_eq1 = mic_eq_open(effect->parm.sample_rate, ch_num, AEID_MIC_EQ1);
        if (effect->mic_eq1 && effect->mic_eq1->out_32bit) {
            effect->mic_drc1 = mic_drc_open(effect->parm.sample_rate, ch_num, AEID_MIC_DRC1);
        }
    }

    //多路混合后 EQ drc
    effect->mic_eq4 = mic_eq_open(effect->parm.sample_rate, ch_num, AEID_MIC_EQ4);
    if (global_bit_wide) {
        effect->mic_drc4 = mic_drc_open(effect->parm.sample_rate, ch_num, AEID_MIC_DRC4);
    } else {
        if (effect->mic_eq4 && effect->mic_eq4->out_32bit) {
            effect->mic_drc4 = mic_drc_open(effect->parm.sample_rate, ch_num, AEID_MIC_DRC4);
            if (!global_bit_wide) {
                effect->convert4 = convet_data_open(0, (irq_points << 1) * ch_num);
            }
        }
    }
    /* effect->gain = audio_gain_open_demo(AEID_MIC_GAIN, ch_num); */

    ///初始化数字音量
    if (effect->parm.effect_config & BIT(MIC_EFFECT_CONFIG_DVOL)) {
        effect_dvol_default_parm.ch_total = ch_num;
#if defined(SYS_DIGVOL_GROUP_EN)&&SYS_DIGVOL_GROUP_EN
        effect->dvol_entry = sys_digvol_group_ch_open("mic_mic", -1, &effect_dvol_default_parm);
        effect->d_vol = audio_dig_vol_group_hdl_get(sys_digvol_group, "mic_mic");
#else
        effect->d_vol = audio_dig_vol_open((audio_dig_vol_param *)&effect_dvol_default_parm);
        effect->dvol_entry = audio_dig_vol_entry_get(effect->d_vol);
#endif /*SYS_DIGVOL_GROUP_EN*/
    }

#if (SOUNDCARD_ENABLE)
    mic_mix_fifo_ch = mix_fifo_ch_open(usb_mix_fifo, audio_output_channel_num());
#endif

#if (RECORDER_MIX_EN)
    effect->rec_hdl = rec_mix_fifo_ch_open(audio_output_channel_num());
#endif

    //打开混响变采样
    /* if (effect->parm.effect_config & BIT(MIC_EFFECT_CONFIG_SOFT_SRC)) { */
    /* u32 out_sr = audio_output_nor_rate(); */

    /* effect->src_hdl = zalloc(sizeof(struct audio_src_handle)); */
    /* audio_hw_src_open(effect->src_hdl, ch_num, SRC_TYPE_RESAMPLE); */
    /* audio_hw_src_set_rate(effect->src_hdl, effect->parm.sample_rate, out_sr); */
    /* } */

    //混响通路混合linein
    if (effect->parm.effect_config & BIT(MIC_EFFECT_CONFIG_LINEIN)) {
        effect->linein = effect_linein_open();
    }

    u8 output_channels = DAC_OUTPUT_CHANNELS;
    if (output_channels != ch_num) {
        u32 points_num = REVERB_LADC_IRQ_POINTS * 4;
        effect->channel_zoom = channel_switch_open(output_channels == 2 ? AUDIO_CH_LR : (output_channels == 4 ? AUDIO_CH_QUAD : AUDIO_CH_DIFF), output_channels == 4 ? (points_num * 2 + 128) : 1024);
        if (global_bit_wide) {
            if (effect->channel_zoom) {
                channel_switch_set_bit_wide(effect->channel_zoom, 1);
            }
        }
        //effect->channel_zoom = channel_switch_open(AUDIO_CH_LR, 1024);
    }

#if (TCFG_AUDIO_DAC_CONNECT_MODE == DAC_OUTPUT_MONO_LR_DIFF)
    mic_data_viewer = audio_data_viewer_open(mic_data_viewer_callback);
    mic_data_viewer_entry = audio_data_viewer_entry_get(mic_data_viewer);
#endif

    effect->entry.data_process_len = mic_effect_data_process_len;

#if (TCFG_USB_MIC_DATA_FROM_MICEFFECT||TCFG_USB_MIC_DATA_FROM_DAC)
    effect->usbmic_hdl = stream_entry_open(effect, mic_effect_otherout_stream_callback, 1);
#endif

    u8 bit_wide = 0;
    if (effect->mic_eq1 && effect->mic_eq1->out_32bit) {
        bit_wide = 1;
    }
#if(TCFG_MIC_EFFECT_SEL != MIC_EFFECT_MEGAPHONE)
    effect->aud_reverb = aud_reverb_open(effect, bit_wide);
#endif

// 数据流串联
    struct audio_stream_entry *entries[26] = {NULL};
    u8 entry_cnt = 0;
    entries[entry_cnt++] = &effect->entry;

    if (effect->dvol_entry) {
        entries[entry_cnt++] = effect->dvol_entry;
    }
#if TCFG_MIC_DODGE_EN
    if (effect->energy_hdl) {
        entries[entry_cnt++] = audio_energy_detect_entry_get(effect->energy_hdl);
    }
#endif
    if (effect->noisegate) {
        entries[entry_cnt++] = &effect->noisegate->entry;
    }

    if (effect->mic_eq0) {
        entries[entry_cnt++] = &effect->mic_eq0->entry;
#if TCFG_EQ_ENABLE && TCFG_MIC_BASS_AND_TREBLE_ENABLE
        if (effect->mic_tunning_eq) {
            entries[entry_cnt++] = &effect->mic_tunning_eq->entry;
        }
#endif
        if (effect->mic_drc0) {
            entries[entry_cnt++] = &effect->mic_drc0->entry;
        }

        if (effect->mic_eq0->out_32bit) {

            if (effect->convert0) {
                entries[entry_cnt++] = &effect->convert0->entry;
            }
        }
    }

    if (effect->howling_ps) {
        entries[entry_cnt++] = &effect->howling_ps->entry;
    }
    if (effect->notch_howling) {
        entries[entry_cnt++] = &effect->notch_howling->entry;
    }
    if (effect->voice_changer) {
        entries[entry_cnt++] = &effect->voice_changer->entry;
    }
    if (effect->harmonic_exciter) {
        entries[entry_cnt++] = &effect->harmonic_exciter->entry;
    }
#if defined(TCFG_MIC_AUTOTUNE_ENABLE) && TCFG_MIC_AUTOTUNE_ENABLE
    if (effect->autotune) {
        entries[entry_cnt++] = &effect->autotune->entry;
    }
#endif
    if (effect->aud_reverb) {
        entries[entry_cnt++] = &effect->aud_reverb->entry;
    }

    if (effect->mic_drc4) {
        entries[entry_cnt++] = &effect->mic_drc4->entry;
    }
    if (effect->mic_eq4) {
        entries[entry_cnt++] = &effect->mic_eq4->entry;
        if (effect->mic_eq4->out_32bit) {
            if (effect->convert4) {
                entries[entry_cnt++] = &effect->convert4->entry;
            }
        }
    }

    /* if (effect->gain) { */
    /* entries[entry_cnt++] = &effect->gain->entry; */
    /* } */
    if (effect->loudness_debug_hdl) {
        entries[entry_cnt++] = &effect->loudness_debug_hdl->entry;
    }

    if (effect->channel_zoom) {
        entries[entry_cnt++] = &effect->channel_zoom->entry;
    }

#if (TCFG_AUDIO_DAC_CONNECT_MODE == DAC_OUTPUT_MONO_LR_DIFF)
    if (mic_data_viewer_entry) {
        entries[entry_cnt++] = mic_data_viewer_entry;
    }
#endif

#if (SOUNDCARD_ENABLE)
    entries[entry_cnt++] = mix_fifo_ch_get_entry(mic_mix_fifo_ch);
#endif

#if (RECORDER_MIX_EN)
    entries[entry_cnt++] = rec_mix_fifo_ch_get_entry(effect->rec_hdl);
#endif

    audio_reverb_stream_dac_init();
    entries[entry_cnt++] = audio_reverb_stream_dac_get_entry();

    effect->stream = audio_stream_open(effect, mic_stream_resume);
    audio_stream_add_list(effect->stream, entries, entry_cnt);

    /* effect->main_pause = 2; */
    ///mic 数据流初始化
    effect->mic = mic_stream_creat(mic_parm);
    if (effect->mic == NULL) {
        mic_effect_destroy(&effect);
        return false;
    }
    mic_stream_set_output(effect->mic, (void *)effect, mic_effect_effect_run);
    mic_stream_start(effect->mic);

    clock_set_cur();
    __this = effect;


    log_info("--------------------------effect start ok\n");
    mem_stats();

    //mic_effect_change_mode(mode);

    return true;
}

/*----------------------------------------------------------------------------*/
/**@brief    mic增益调节
   @param
   @return
   @note
*/
/*----------------------------------------------------------------------------*/
/* void mic_effect_mic_gain_parm_fill(EFFECTS_MIC_GAIN_PARM *parm, u8 fade, u8 online) */
/* { */
/* if (__this == NULL || parm == NULL) { */
/* return ; */
/* } */
/* audio_mic_set_gain(parm->gain); */
/* } */
/*----------------------------------------------------------------------------*/
/**@brief    mic效果模式切换（数据流音效组合切换）
   @param
   @return
   @note 使用效果配置文件时生效
*/
/*----------------------------------------------------------------------------*/
void mic_effect_change_mode(u16 mode)
{
    set_mic_reverb_mode_by_id(mode);
}
/*----------------------------------------------------------------------------*/
/**@brief    获取mic效果模式（数据流音效组合）
   @param
   @return
   @note 使用效果配置文件时生效
*/
/*----------------------------------------------------------------------------*/
u16 mic_effect_get_cur_mode(void)
{
    return get_mic_eff_mode();
}

#if TCFG_EQ_ENABLE && TCFG_MIC_BASS_AND_TREBLE_ENABLE
/*
 *混响mic tunning_eq 打开
 * */
struct eq_tool mic_tunning_eq_parm = {0};
void *mic_tunning_eq_open(u32 sample_rate, u8 ch_num)
{
    u8 mode = get_mic_eff_mode();
    struct eff_parm *mic_eff = &eff_mode[mode];
    u8 seg_num = 3;
    mic_tunning_eq_parm.seg_num = seg_num;
    mic_tunning_eq_parm.global_gain = 0;
    for (int i = 0; i < seg_num; i++) {
        mic_tunning_eq_parm.seg[i].index    = i;
        mic_tunning_eq_parm.seg[i].iir_type = mic_eff->tunning_eq_parm.seg[i].filt_type;
        mic_tunning_eq_parm.seg[i].freq     = mic_eff->tunning_eq_parm.seg[i].center_freq;
        mic_tunning_eq_parm.seg[i].q        = mic_eff->tunning_eq_parm.seg[i].q;
    }
    struct eq_parm eparm = {0};
    eparm.in_mode = DATI_INT;
    eparm.out_mode = DATO_INT;
    eparm.run_mode = NORMAL;
    eparm.data_in_mode = SEQUENCE_DAT_IN;
    eparm.data_out_mode = SEQUENCE_DAT_OUT;
    struct audio_eq_param parm = {0};
    parm.parm = &eparm;
    parm.channels = ch_num;
    parm.cb = eq_get_filter_info;
    parm.sr = sample_rate;
    parm.eq_name = AEID_MIC_TUNNING_EQ;
    parm.max_nsection = mic_tunning_eq_parm.seg_num;
    parm.nsection = mic_tunning_eq_parm.seg_num;
    parm.seg = mic_tunning_eq_parm.seg;
    parm.global_gain = mic_tunning_eq_parm.global_gain;

    parm.fade = 1;//高低音增益更新差异大，会引入哒哒音，此处使能系数淡入
    parm.fade_step = 0.4f;//淡入步进（0.1f~1.0f）

    log_d("=====mic tunning eq_name %d\n", parm.eq_name);
    struct audio_eq *eq = audio_dec_eq_open(&parm);
    return eq;
}
/*
 *混响mic tunning_eq 关闭
 * */
void mic_tunning_eq_close(void *eq)
{
    if (eq) {
        audio_dec_eq_close(eq);
    }
}
#endif

/*
 *index:0 low, 1 mid, 2 high
 *gain:增益（-12~12）
 * */
void mic_tunning_eq_update(u8 index, float gain)
{
#if TCFG_EQ_ENABLE && TCFG_MIC_BASS_AND_TREBLE_ENABLE
    u8 mode = get_mic_eff_mode();
    struct eff_parm *mic_eff = &eff_mode[mode];
    //printf("max %d\n", (int)mic_eff->tunning_eq_parm.seg[index].max_gain);
    //printf("min %d\n", (int)mic_eff->tunning_eq_parm.seg[index].min_gain);
    if (gain > mic_eff->tunning_eq_parm.seg[index].max_gain) {
        gain = 	mic_eff->tunning_eq_parm.seg[index].max_gain;
    } else if (gain < mic_eff->tunning_eq_parm.seg[index].min_gain) {
        gain = 	mic_eff->tunning_eq_parm.seg[index].min_gain;
    }
    mic_tunning_eq_parm.seg[index].gain = gain;
    /* log_d("mic tunning eq index %d, %d\n", index, (int)gain); */
    struct audio_eq *hdl = get_cur_eq_hdl_by_name(AEID_MIC_TUNNING_EQ);
    if (hdl) {
        cur_eq_set_update(AEID_MIC_TUNNING_EQ, &mic_tunning_eq_parm.seg[index], mic_tunning_eq_parm.seg_num, 0);
    }
#endif
}
/*
*混响切换模式时更新高低音参数
*/
void mic_tunning_eq_update_parm(u8 mode)
{
#if TCFG_EQ_ENABLE && TCFG_MIC_BASS_AND_TREBLE_ENABLE
    u8 seg_num = 3;
    struct eff_parm *mic_eff = &eff_mode[mode];
    struct audio_eq *hdl = get_cur_eq_hdl_by_name(AEID_MIC_TUNNING_EQ);
    for (int i = 0; i < seg_num; i++) {
        mic_tunning_eq_parm.seg[i].index    = i;
        mic_tunning_eq_parm.seg[i].iir_type = mic_eff->tunning_eq_parm.seg[i].filt_type;
        mic_tunning_eq_parm.seg[i].freq     = mic_eff->tunning_eq_parm.seg[i].center_freq;
        mic_tunning_eq_parm.seg[i].q        = mic_eff->tunning_eq_parm.seg[i].q;
        if (hdl) {
            cur_eq_set_update(AEID_MIC_TUNNING_EQ, &mic_tunning_eq_parm.seg[i], mic_tunning_eq_parm.seg_num, 0);
        }
    }
#endif
}





#if TCFG_MIC_DODGE_EN
void mic_e_det_handler(u8 event, u8 ch)
{
    //printf(">>>> ch:%d %s\n", ch, event ? ("MUTE") : ("UNMUTE"));
    struct __mic_effect *effect = (struct __mic_effect *)__this;
#if SYS_DIGVOL_GROUP_EN
    //printf("effect_dvol_default_parm.ch_total %d effect->dodge_en %d\n", effect_dvol_default_parm.ch_total, effect->dodge_en);
    if (ch == effect->dodge_ch_num) {
        if (effect->dodge_en) {
            if (effect && effect->d_vol) {
                if (event) { //推出闪避
                    audio_dig_vol_group_dodge(sys_digvol_group, "mic_mic", 100, 100);
                } else { //启动闪避
                    audio_dig_vol_group_dodge(sys_digvol_group, "mic_mic", 100, 0);
                }
            }
        }
    }
#endif
}
/*----------------------------------------------------------------------------*/
/**@brief    打开mic 能量检测
   @param
   @return
   @note
*/
/*----------------------------------------------------------------------------*/
void *mic_energy_detect_open(u32 sr, u8 ch_num)
{
    audio_energy_detect_param e_det_param = {0};
    e_det_param.mute_energy = dodge_parm.dodge_out_thread;//人声能量小于mute_energy 退出闪避
    e_det_param.unmute_energy = dodge_parm.dodge_in_thread;//人声能量大于 100触发闪避
    e_det_param.mute_time_ms = dodge_parm.dodge_out_time_ms;
    e_det_param.unmute_time_ms = dodge_parm.dodge_in_time_ms;
    e_det_param.count_cycle_ms = 2;
    e_det_param.sample_rate = sr;
    e_det_param.event_handler = mic_e_det_handler;
    e_det_param.ch_total = ch_num;
    e_det_param.dcc = 1;
    e_det_param.bit_wide = global_bit_wide;
    void *audio_e_det_hdl = audio_energy_detect_open(&e_det_param);
    return audio_e_det_hdl;
}
/*----------------------------------------------------------------------------*/
/**@brief    关闭mic 能量检测
   @param
   @return
   @note
*/
/*----------------------------------------------------------------------------*/
void mic_energy_detect_close(void *hdl)
{
    if (hdl) {
        audio_stream_del_entry(audio_energy_detect_entry_get(hdl));
#if SYS_DIGVOL_GROUP_EN
        struct __mic_effect *effect = (struct __mic_effect *)__this;
        if (effect->d_vol) {
            audio_dig_vol_group_dodge(sys_digvol_group, "mic_mic", 100, 100);         // undodge
        }
#endif

        audio_energy_detect_close(hdl);
    }
}

/*----------------------------------------------------------------------------*/
/**@brief    能量检测运行过程，是否触发闪避
   @param
   @return
   @note
*/
/*----------------------------------------------------------------------------*/
void mic_dodge_ctr(void)
{
    struct __mic_effect *effect = (struct __mic_effect *)__this;
    if (effect) {
        effect->dodge_en = !effect->dodge_en;
    }
}
u8 mic_dodge_get_status(void)
{
    struct __mic_effect *effect = (struct __mic_effect *)__this;
    if (effect) {
        return effect->dodge_en;
    }
    return 0;
}
#endif

/*----------------------------------------------------------------------------*/
/**@brief    (mic数据流)混响关闭接口
   @param
   @return
   @note
*/
/*----------------------------------------------------------------------------*/
void mic_effect_stop(void)
{
    mic_effect_destroy(&__this);
}
/*----------------------------------------------------------------------------*/
/**@brief    (mic数据流)混响暂停接口(整个数据流不运行)
   @param
   @return
   @note
*/
/*----------------------------------------------------------------------------*/
void mic_effect_pause(u8 mark)
{
    if (__this) {
        __this->pause_mark = mark ? 1 : 0;
    }
}
/*----------------------------------------------------------------------------*/
/**@brief    (mic数据流)混响状态获取接口
   @param
   @return
   @note
*/
/*----------------------------------------------------------------------------*/
u8 mic_effect_get_status(void)
{
#if WIRELESS_MIC_EFFECT_ENABLE
    if (live_mic_effect_status_get()) {
        return 1;
    }
#endif
    return ((__this) ? 1 : 0);
}
/*----------------------------------------------------------------------------*/
/**@brief    数字音量调节接口
   @param
   @return
   @note
*/
/*----------------------------------------------------------------------------*/
void mic_effect_set_dvol(u8 vol)
{
    if (__this && __this->d_vol) {
        audio_dig_vol_set(__this->d_vol, 3, vol);
    }
}
u8 mic_effect_get_dvol(void)
{
    if (__this && __this->d_vol) {
        return audio_dig_vol_get(__this->d_vol, 1);
    }
    return 0;
}
/*----------------------------------------------------------------------------*/
/**@brief    获取mic增益接口
   @param
   @return
   @note
*/
/*----------------------------------------------------------------------------*/
u8 mic_effect_get_micgain(void)
{
    //动态调的需实时记录
    return effect_mic_stream_parm_default.mic_gain;
}


/*----------------------------------------------------------------------------*/
/**@brief    reverb 效果声增益调节接口
  @param     wet增益系数%[0,300];dry增益系数%[0,200]
  @return
  @note
  */
/*----------------------------------------------------------------------------*/
void mic_effect_set_reverb_wet(int wet)
{
    if (__this == NULL || __this->sub_0_plate_reverb_hdl == NULL) {
        return ;
    }
    os_mutex_pend(&__this->mutex, 0);
    struct  plate_reverb_update_parm parm;
    memcpy(&parm, & __this->sub_0_plate_reverb_hdl->reverb_parm_obj, sizeof(struct  plate_reverb_update_parm));
    parm.wet = wet;
    update_plate_reverb_parm(__this->sub_0_plate_reverb_hdl, &parm);
    os_mutex_post(&__this->mutex);
}

int mic_effect_get_reverb_wet(void)
{
    if (__this && __this->sub_0_plate_reverb_hdl) {
        return __this->sub_0_plate_reverb_hdl->reverb_parm_obj.wet;
    }
    return 0;
}
void mic_effect_set_reverb_dry(int dry)
{
    if (__this == NULL || __this->sub_0_plate_reverb_hdl == NULL) {
        return ;
    }
    os_mutex_pend(&__this->mutex, 0);
    struct  plate_reverb_update_parm parm;
    memcpy(&parm, & __this->sub_0_plate_reverb_hdl->reverb_parm_obj, sizeof(struct  plate_reverb_update_parm));
    parm.dry = dry;
    update_plate_reverb_parm(__this->sub_0_plate_reverb_hdl, &parm);
    os_mutex_post(&__this->mutex);
}

int mic_effect_get_reverb_dry(void)
{
    if (__this && __this->sub_0_plate_reverb_hdl) {
        return __this->sub_0_plate_reverb_hdl->reverb_parm_obj.dry;
    }
    return 0;
}

/*----------------------------------------------------------------------------*/
/**@brief    echo 回声延时调节接口
   @param   delay 回声延时[0,max_ms]
   @return
   @note
*/
/*----------------------------------------------------------------------------*/
void mic_effect_set_echo_delay(u32 delay)
{
    if (__this == NULL || __this->sub_1_echo_hdl == NULL) {
        return ;
    }
    os_mutex_pend(&__this->mutex, 0);
    struct echo_update_parm parm;
    memcpy(&parm, &__this->sub_1_echo_hdl->echo_parm_obj, sizeof(struct echo_update_parm));
    parm.delay = delay;
    update_echo_parm(__this->sub_1_echo_hdl, &parm);
    os_mutex_post(&__this->mutex);
}
u32 mic_effect_get_echo_delay(void)
{
    if (__this && __this->sub_1_echo_hdl) {
        return __this->sub_1_echo_hdl->echo_parm_obj.delay;
    }
    return 0;
}
/*----------------------------------------------------------------------------*/
/**@brief    echo 回声衰减系数调节接口
   @param    decay 衰减系数值%[0,90]
   @return
   @note
*/
/*----------------------------------------------------------------------------*/
void mic_effect_set_echo_decay(u32 decay)
{
    if (__this == NULL || __this->sub_1_echo_hdl == NULL) {
        return ;
    }
    os_mutex_pend(&__this->mutex, 0);
    struct echo_update_parm parm;
    memcpy(&parm, &__this->sub_1_echo_hdl->echo_parm_obj, sizeof(struct echo_update_parm));
    parm.decayval = decay;
    update_echo_parm(__this->sub_1_echo_hdl, &parm);
    os_mutex_post(&__this->mutex);
}

u32 mic_effect_get_echo_decay(void)
{
    if (__this && __this->sub_1_echo_hdl) {
        return __this->sub_1_echo_hdl->echo_parm_obj.decayval;
    }
    return 0;
}
/*----------------------------------------------------------------------------*/
/**@brief    echo 回声wetgain调节接口
   @param    wetgain 湿声增益系数%[0,200]
   @return
   @note
*/
/*----------------------------------------------------------------------------*/
void mic_effect_set_echo_wetgain(u32 wetgain)
{
    if (__this == NULL || __this->sub_1_echo_hdl == NULL) {
        return ;
    }
    os_mutex_pend(&__this->mutex, 0);
    struct echo_update_parm parm;
    memcpy(&parm, &__this->sub_1_echo_hdl->echo_parm_obj, sizeof(struct echo_update_parm));
    parm.wetgain = wetgain;
    update_echo_parm(__this->sub_1_echo_hdl, &parm);
    os_mutex_post(&__this->mutex);
}

u32 mic_effect_get_echo_wetgain(void)
{
    if (__this && __this->sub_1_echo_hdl) {
        return __this->sub_1_echo_hdl->echo_parm_obj.wetgain;
    }
    return 0;
}
/*----------------------------------------------------------------------------*/
/**@brief    echo 回声drygain调节接口
  @param    干声增益系数%[0,100]
  @return
  @note
  */
/*----------------------------------------------------------------------------*/
void mic_effect_set_echo_drygain(u32 drygain)
{
    if (__this == NULL || __this->sub_1_echo_hdl == NULL) {
        return ;
    }
    os_mutex_pend(&__this->mutex, 0);
    struct echo_update_parm parm;
    memcpy(&parm, &__this->sub_1_echo_hdl->echo_parm_obj, sizeof(struct echo_update_parm));
    parm.drygain = drygain;
    update_echo_parm(__this->sub_1_echo_hdl, &parm);
    os_mutex_post(&__this->mutex);
}

u32 mic_effect_get_echo_drygain(void)
{
    if (__this && __this->sub_1_echo_hdl) {
        return __this->sub_1_echo_hdl->echo_parm_obj.drygain;
    }
    return 0;
}
//*********************变声音效切换例程**********************************//

/* VOICE_CHANGER_NONE,//原声 */
/* VOICE_CHANGER_UNCLE,//大叔 */
/* VOICE_CHANGER_GODDESS,//女神 */
/* VOICE_CHANGER_BABY,//娃娃音 */
/* VOICE_CHANGER_MAGIC,//魔音女声 */
/* VOICE_CHANGER_MONSTER,//怪兽音 */
/* VOICE_CHANGER_DONALD_DUCK,//唐老鸭 */
/* VOICE_CHANGER_MINIONS,//小黄人 */
/* VOICE_CHANGER_ROBOT,//机器音 */
/* VOICE_CHANGER_WHISPER,//气音 */
/* VOICE_CHANGER_MELODY,//固定旋律音 */
/* VOICE_CHANGER_FEEDBACK,//调制音 */

/*----------------------------------------------------------------------------*/
/**@brief    变声音效循环切换
   @param    NULL
   @return
   @note
*/
/*----------------------------------------------------------------------------*/
void mic_voicechange_loop(void)
{
    voicechange_mode++;
    if (voicechange_mode >= VOICE_CHANGER_MAX) {
        voicechange_mode = VOICE_CHANGER_NONE;
    }
    syscfg_write(CFG_VOICE_CHANGER_MODE, &voicechange_mode, sizeof(voicechange_mode));
    mic_voicechange_switch(voicechange_mode);
}

/*----------------------------------------------------------------------------*/
/**@brief    变声音效指定模式切换
   @param    eff_mode 模式索引
   @return
   @note
*/
/*----------------------------------------------------------------------------*/
void mic_voicechange_switch(u8 eff_mode)
{
#if TCFG_MIC_VOICE_CHANGER_ENABLE
    if (!mic_effect_get_status()) {
#if WIRELESS_MIC_EFFECT_ENABLE
        if (!live_mic_effect_status_get()) {
            mic_effect_start();
        }
#else
        mic_effect_start();
#endif
        /* return; */
    }
    switch (eff_mode) {
    case VOICE_CHANGER_NONE://原声,录音棚
        /* tone_play_index(IDEX_TONE_MIC_OST, 1); */
        break;
    case VOICE_CHANGER_UNCLE://大叔
        /* tone_play_index(IDEX_TONE_UNCLE, 1); */
        break;
    case VOICE_CHANGER_GODDESS://女神
        /* tone_play_index(IDEX_TONE_GODNESS, 1); */
        break;
    case VOICE_CHANGER_BABY://娃娃音
        /* tone_play_index(IDEX_TONE_BABY, 1); */
        break;
    case VOICE_CHANGER_MAGIC://魔音女声
        /* tone_play_index(IDEX_TONE_BABY, 1); */
        break;
    case VOICE_CHANGER_MONSTER://怪兽音
        /* tone_play_index(IDEX_TONE_BABY, 1); */
        break;
    case VOICE_CHANGER_DONALD_DUCK://唐老鸭
        /* tone_play_index(IDEX_TONE_BABY, 1); */
        break;
    case VOICE_CHANGER_MINIONS://小黄人
        /* tone_play_index(IDEX_TONE_BABY, 1); */
        break;
    case VOICE_CHANGER_ROBOT://机器音
        /* tone_play_index(IDEX_TONE_BABY, 1); */
        break;
    case VOICE_CHANGER_WHISPER://气音
        /* tone_play_index(IDEX_TONE_BABY, 1); */
        break;
    case VOICE_CHANGER_MELODY://固定旋律音
        /* tone_play_index(IDEX_TONE_BABY, 1); */
        break;
    case VOICE_CHANGER_FEEDBACK://调制音
        /* tone_play_index(IDEX_TONE_BABY, 1); */
        break;
    default:
        puts("mic_ERROR\n");
        /* mic_effect_stop(); */
        break;
    }
    audio_voice_changer_mode_switch(AEID_MIC_VOICE_CHANGER, eff_mode);
#endif//TCFG_MIC_VOICE_CHANGER_ENABLE
}

/************mic effect音效模块参数更新接口(调试工具/模式切换会使用勿修改)************/
void mic_effect_plate_reverb_update_parm(void *parm, int bypass)
{
    if (__this && __this->sub_0_plate_reverb_hdl) {
        update_plate_reverb_parm(__this->sub_0_plate_reverb_hdl, parm);
        plate_reverb_update_bypass(__this->sub_0_plate_reverb_hdl, bypass);
    }
}

void mic_effect_plate_reverb_bypass(int bypass)
{
    if (__this && __this->sub_0_plate_reverb_hdl) {
        plate_reverb_update_bypass(__this->sub_0_plate_reverb_hdl, bypass);
    }
}

void mic_effect_echo_updata_parm(void *parm, int bypass)
{
    if (__this && __this->sub_1_echo_hdl) {
        update_echo_parm(__this->sub_1_echo_hdl, parm);
        echo_update_bypass(__this->sub_1_echo_hdl, bypass);
    }
}


void mic_effect_echo_bypass(int bypass)
{
    if (__this && __this->sub_1_echo_hdl) {
        echo_update_bypass(__this->sub_1_echo_hdl, bypass);
    }
}

void mic_effect_howling_pitch_shift_update_parm(void *parm, int bypass)
{
    if (__this && __this->howling_ps) {
        update_howling_parm(__this->howling_ps, parm);
        howling_update_bypass(__this->howling_ps, bypass);
    }
}

void mic_effect_howling_pitch_shift_bypass(int bypass)
{
    if (__this && __this->howling_ps) {
        howling_update_bypass(__this->howling_ps, bypass);
    }
}

void mic_effect_notchhowline_update_parm(void *parm, int bypass)
{
    if (__this && __this->notch_howling) {
        update_howling_parm(__this->notch_howling, parm);
        howling_update_bypass(__this->notch_howling, bypass);
    }
}

void mic_effect_notchhowline_bypass(int bypass)
{
    if (__this && __this->notch_howling) {
        howling_update_bypass(__this->notch_howling, bypass);
    }
}


void mic_effect_voice_changer_bypass(int bypass)
{
    if (__this && __this->voice_changer) {
        audio_voice_changer_bypass(AEID_MIC_VOICE_CHANGER, bypass);
    }

}

#endif//defined(TCFG_MIC_EFFECT_ENABLE) && TCFG_MIC_EFFECT_ENABLE


#if (defined(TCFG_MIC_EFFECT_ENABLE) && TCFG_MIC_EFFECT_ENABLE || (defined(WIRELESS_MIC_EFFECT_ENABLE) && WIRELESS_MIC_EFFECT_ENABLE))
/**************************各个音效模块参数设置接口(调试工具/模式切换会使用勿修改)************************************/
void plate_reverb_update_parm(void *parm, int bypass)
{
#if WIRELESS_MIC_EFFECT_ENABLE
    if (live_mic_effect_status_get()) {
        live_mic_plate_reverb_update_parm(parm, bypass);
    }
#endif//WIRELESS_MIC_EFFECT_ENABLE
#if TCFG_MIC_EFFECT_ENABLE
    if (mic_effect_get_status()) {
        mic_effect_plate_reverb_update_parm(parm, bypass);
    }
#endif//TCFG_MIC_EFFECT_ENABLE
}

void echo_updata_parm(void *parm, int bypass)
{
#if WIRELESS_MIC_EFFECT_ENABLE
    if (live_mic_effect_status_get()) {
        live_mic_echo_updata_parm(parm, bypass);
        return;
    }
#endif
#if TCFG_MIC_EFFECT_ENABLE
    if (mic_effect_get_status()) {
        mic_effect_echo_updata_parm(parm, bypass);
    }
#endif//TCFG_MIC_EFFECT_ENABLE
}

void noisegate_update_parm(void *parm, int bypass)
{
    audio_noisegate_update(AEID_MIC_NS_GATE, parm);
    audio_noisegate_bypass(AEID_MIC_NS_GATE, bypass);
}

void howling_pitch_shift_update_parm(void *parm, int bypass)
{
#if WIRELESS_MIC_EFFECT_ENABLE
    if (live_mic_effect_status_get()) {
        live_mic_howling_pitch_shift_update_parm(parm, bypass);
    }
#endif
#if TCFG_MIC_EFFECT_ENABLE
    if (mic_effect_get_status()) {
        mic_effect_howling_pitch_shift_update_parm(parm, bypass);
    }
#endif//TCFG_MIC_EFFECT_ENABLE
}

void notchhowline_update_parm(void *parm, int bypass)
{
#if WIRELESS_MIC_EFFECT_ENABLE
    if (live_mic_effect_status_get()) {
        live_mic_notchhowline_update_parm(parm, bypass);
        return;
    }
#endif
#if TCFG_MIC_EFFECT_ENABLE
    if (mic_effect_get_status()) {
        mic_effect_notchhowline_update_parm(parm, bypass);
    }
#endif//TCFG_MIC_EFFECT_ENABLE
}
/**************************各个音效模块参数设置接口 END************************************/

/**************************降噪llns相关函数接口************************************/
#if MIC_EFFECT_LLNS
typedef struct {
    char *private_buf;
    char *share_buf;
} llns_hdl_t;
static llns_hdl_t *llns_hdl = NULL;
static int mic_effect_ns_exit(void);

/* _NOINLINE_ */
static int mic_effect_ns_init(int sr, float gainfloor, float suppress_level)
{
    if (llns_hdl) {
        mic_effect_ns_exit();
    }

    llns_hdl = zalloc(sizeof(llns_hdl_t));
    if (llns_hdl == NULL) {
        printf("llns_hdl zalloc err !!!!\n");
        return -1;
    }

    int private_heap_size, share_heap_size;
    audio_llns_heap_query(&share_heap_size, &private_heap_size,  sr);
    printf("private_heap_size:%d,share_heap_size:%d\n", private_heap_size, share_heap_size);
    llns_hdl->private_buf = (char *)zalloc(private_heap_size);
    llns_hdl->share_buf = (char *)zalloc(share_heap_size);

    printf("sr: %d, gainfloor: %d/100\n", sr, (int)(gainfloor * 100));
    int ret = audio_llns_init(llns_hdl->private_buf, private_heap_size, llns_hdl->share_buf, share_heap_size, sr, gainfloor, suppress_level);
    if (ret != 0) {
        printf("mic effect ns init fail !!!\n");
        return ret;
    }
    printf("mic effect ns init ok\n");
    return 0;
}

static int mic_effect_ns_run(s16 *data, int len)
{
    int llns_outsize = 0;
    if (llns_hdl) {
        llns_outsize = audio_llns_run(data, len, data);
    }
    return llns_outsize << 1;
}

static int mic_effect_ns_exit(void)
{
    if (llns_hdl) {
        audio_llns_close();
        if (llns_hdl->private_buf) {
            free(llns_hdl->private_buf);
            llns_hdl->private_buf = NULL;
        }
        if (llns_hdl->share_buf) {
            free(llns_hdl->share_buf);
            llns_hdl->share_buf = NULL;
        }
        free(llns_hdl);
        llns_hdl = NULL;
    }
    return 0;
}
#endif // MIC_EFFECT_LLNS
/**************************降噪llns相关函数接口 END************************************/


struct eq_tool mic_eq_parm[5];
void *mic_eq_open(u32 sample_rate, u8 ch_num, u8 eq_name)
{
#if TCFG_EQ_ENABLE

#if(TCFG_MIC_EFFECT_SEL == MIC_EFFECT_MEGAPHONE)
    if ((eq_name == AEID_MIC_EQ1) || (eq_name == AEID_MIC_EQ2) || (eq_name == AEID_MIC_EQ3)) {
        return NULL;
    }
#endif


    u8 mode = get_mic_eff_mode();

    struct eq_parm eparm = {0};
    struct audio_eq_param parm = {0};
    if (global_bit_wide) {
#if REVERB_ECHO_RUN_32BIT
        eparm.in_mode = DATI_INT;
        eparm.out_mode = DATO_INT;
        eparm.run_mode = NORMAL;
        eparm.data_in_mode = SEQUENCE_DAT_IN;
        eparm.data_out_mode = SEQUENCE_DAT_OUT;
        parm.parm = &eparm;
#else
        if ((eq_name == AEID_MIC_EQ0) || (eq_name == AEID_MIC_EQ3) || (eq_name == AEID_MIC_EQ4)) {
            eparm.in_mode = DATI_INT;
            eparm.out_mode = DATO_INT;
            eparm.run_mode = NORMAL;
            eparm.data_in_mode = SEQUENCE_DAT_IN;
            eparm.data_out_mode = SEQUENCE_DAT_OUT;
            parm.parm = &eparm;
        }
#endif
    } else {
#if (TCFG_MIC_EFFECT_SEL == MIC_EFFECT_REVERB_ECHO_ADVANCE)
        parm.out_32bit = 1;//32bit位宽输出
#else
        if ((eq_name == AEID_MIC_EQ0) || (eq_name == AEID_MIC_EQ4)) {
            parm.out_32bit = 1;//32bit位宽输出
        }
#endif
    }
    parm.channels = ch_num;
    parm.cb = eq_get_filter_info;
    parm.sr = sample_rate;
    parm.eq_name = eq_name;

    u8 index = get_eq_module_index(eq_name);
    log_d("index %d\n", index);
    if (sizeof(mic_eq_parm) != sizeof(eff_mode[mode].eq_parm)) {
        log_e("need check eq num\n");//检查mic_eq_parm结构是否与eff_mode[mode].eq_parm结构长度一致
        log_e("mic_eq open err\n");
        return NULL;
    }
    memcpy(mic_eq_parm, eff_mode[mode].eq_parm, sizeof(eff_mode[mode].eq_parm));

    parm.max_nsection  = mic_eq_parm[index].seg_num;
    parm.nsection      = mic_eq_parm[index].seg_num;
    parm.seg           = mic_eq_parm[index].seg;
    parm.global_gain   = mic_eq_parm[index].global_gain;
    log_d("=====mic eq_name %d\n", eq_name);
    struct audio_eq *eq = audio_dec_eq_open(&parm);
    return eq;

#else
    return NULL;
#endif//TCFG_EQ_ENABLE
}

#if TCFG_DRC_ENABLE
static wdrc_struct_TOOL_SET mic_drc_parm[5];
#if ((TCFG_MIC_EFFECT_SEL == MIC_EFFECT_REVERB_ECHO_ADVANCE) || TCFG_MIC_EFFECT_MDRC)
static CrossOverParam_TOOL_SET crossover_parm;
static wdrc_struct_TOOL_SET last_wdrc_parm[4];//[0]low  [1]mid [2]high [3]多带之后附加的全带
#endif
#endif
void *mic_drc_open(u32 sample_rate, u8 ch_num, u8 drc_name)
{
#if TCFG_DRC_ENABLE
#if ((TCFG_MIC_EFFECT_SEL == MIC_EFFECT_REVERB_ECHO_ADVANCE) || TCFG_MIC_EFFECT_MDRC)
    if (drc_name  == AEID_MIC_DRC4) {
        struct audio_drc_param parm = {0};
        parm.channels = ch_num;
        parm.sr = sample_rate;
        parm.out_32bit = global_bit_wide ? 1 : 0;
        parm.cb = drc_get_filter_info;
        parm.drc_name = drc_name;
        parm.nband = CROSSOVER_EN | MORE_BAND_EN;

        u8 mode = get_mic_eff_mode();
        memcpy(&crossover_parm, &mic_last_drc_parm[mode].crossover, sizeof(CrossOverParam_TOOL_SET));
        memcpy(&last_wdrc_parm, &mic_last_drc_parm[mode].wdrc_parm, sizeof(last_wdrc_parm));
        parm.crossover = &crossover_parm;
        parm.wdrc = &last_wdrc_parm;
        struct audio_drc *drc = audio_dec_drc_open(&parm);
        return drc;
    }
#endif

    u8 mode = get_mic_eff_mode();
    log_i("sample_rate %d %d\n", sample_rate, ch_num);
    struct audio_drc_param parm = {0};
    parm.channels = ch_num;
    parm.sr = sample_rate;
    if (drc_name == AEID_MIC_DRC4) {
        parm.out_32bit = global_bit_wide ? 1 : 0;;
    } else {
        parm.out_32bit = 1;
    }
    parm.cb = drc_get_filter_info;
    parm.drc_name = drc_name;
    u8 index = get_drc_module_index(drc_name);
    memcpy(&mic_drc_parm[index], &eff_mode[mode].drc_parm[index], sizeof(wdrc_struct_TOOL_SET));
    parm.wdrc = &mic_drc_parm[index];
    log_d("=====drc_name %d\n", drc_name);
    struct audio_drc *drc = audio_dec_drc_open(&parm);
    clock_add(EQ_CLK);
    return drc;
#else
    return NULL;
#endif//TCFG_DRC_ENABLE

}

void mic_eq_close(void *eq)
{
#if TCFG_EQ_ENABLE
    if (eq) {
        audio_dec_eq_close(eq);
        clock_remove(EQ_CLK);
    }
#endif
    return;
}

void mic_drc_close(void *drc)
{
#if TCFG_DRC_ENABLE
    if (drc) {
        audio_dec_drc_close(drc);
        drc = NULL;
        clock_remove(EQ_CLK);
    }
#endif
    return;
}

#endif//(defined(TCFG_MIC_EFFECT_ENABLE) && TCFG_MIC_EFFECT_ENABLE || (defined(WIRELESS_MIC_EFFECT_ENABLE) && WIRELESS_MIC_EFFECT_ENABLE))



