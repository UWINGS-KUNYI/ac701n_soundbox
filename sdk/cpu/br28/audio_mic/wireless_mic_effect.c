#include "wireless_mic_effect.h"
#include "audio_splicing.h"
#include "stream_llns.h"
#include "audio_voice_changer_demo.h"
#include "mic_stream.h"
#include "audio_dec.h"
#include "board_config.h"
#include "wireless_dev_manager.h"
#include "system/init.h"
#if WIRELESS_MIC_EFFECT_ENABLE

#define DEMO_TASK_NAME   "wireless_mic_effect"

#if WIRELESS_MIC_EFFECT_ASYNC_EN
#define MIC_EFFECT_ASYNC_EN        1//1:异步处理, 会增加一个处理单元的延时， 0:同步处理， 不增加延时， 但是要求在一个cpu核要可以做完算法处理
#else
#define MIC_EFFECT_ASYNC_EN        0//1:异步处理, 会增加一个处理单元的延时， 0:同步处理， 不增加延时， 但是要求在一个cpu核要可以做完算法处理
#endif

extern void *mic_eq_open(u32 sample_rate, u8 ch_num, u8 eq_name);
extern void *mic_drc_open(u32 sample_rate, u8 ch_num, u8 eq_name);
extern void mic_eq_close(void *eq);
extern void mic_drc_close(void  *drc);

static void mic_effect_stream_resume(void *p);
static struct audio_stream_entry *capture_output = NULL;

struct aud_reverb_process {
    struct audio_stream_entry entry;	// 音频流入口
    s16 *tmpbuf[3];
    u16 tmpbuf_len[3];
    void *eff;//struct __mic_effect
    s16 *out_buf;
    u8 bit_wide;//eq是否输出32bit位宽
    u8 in_ch;
};
static OS_MUTEX  wireless_mic_effect_mutex;
#define WIRELESS_MIC_EFFECT_INIT_CRITICAL()          os_mutex_create(&wireless_mic_effect_mutex)
#define WIRELESS_MIC_EFFECT_ENTER_CRITICAL()         os_mutex_pend(&wireless_mic_effect_mutex,0)
#define WIRELESS_MIC_EFFECT_EXIT_CRITICAL()          os_mutex_post(&wireless_mic_effect_mutex)


static struct live_mic_effect *g_mic_effect = NULL;
static u8 all_entry_cnt = 0;
static struct audio_stream_entry *entries[20] = {NULL};
static output_channel_cnt = 0;
static struct audio_stream_entry *added_output[4] = {NULL};
static u8 temp_data[512];

#if WIRELESS_MIC_CONFIG_ECHO
struct echo_update_parm echo_parm = {
    .delay = 120,//200,				//回声的延时时间 0-300ms
    .decayval = 35,//50,				// 0-70%
    .filt_enable = 1,			//发散滤波器使能
    .wetgain = 2048,			////湿声增益：[0:4096]
    .drygain = 4096,				////干声增益: [0:4096]
    .lpf_cutoff = 5000,
};
EF_REVERB_FIX_PARM echo_fix_parm = {
    .sr = 48000,		////采样率
    .max_ms = 200,				////所需要的最大延时，影响 need_buf 大小
};
#endif

#if WIRELESS_MIC_CONFIG_LLNS
struct __stream_llns_parm llns_parm = {
    .gainfloor = 0.1f,
    .suppress_level = 1.0f,
    .frame_len = 100,
};
#endif

#if WIRELESS_MIC_CONFIG_REVERB
struct  plate_reverb_update_parm reverb_parm = {
    .pre_delay = 0,
    .highcutoff = 12200,
    .diffusion = 43,
    .decayfactor = 70,
    .highfrequencydamping = 26,
    .dry = 80,
    .wet = 40,
    .modulate = 1,
    .roomsize = 100,
};
#endif

/*----------------------------------------------------------------------------*/
/**@brief   数据输入接口，驱动mic 效数据处理运行
   @param
   @return
   @note
*/
/*----------------------------------------------------------------------------*/
int live_mic_effect_input(void *_effect, void *buff, int len, u32 time)
{
    struct live_mic_effect *effect = (struct live_mic_effect *)_effect;
    if (!effect) {
        printf("input   !effect");
        return 0;
    }
    /* spin_lock(&effect->lock); */
    WIRELESS_MIC_EFFECT_ENTER_CRITICAL();
    if (effect->start) {
        /* printf("live_mic_effect_input  %d",len); */
        struct audio_data_frame frame = {0};
        frame.channel = effect->param.ch_num;
        frame.sample_rate = effect->param.sample_rate;
        frame.data_len = len;
        frame.data = buff;
        /* frame.timestamp = time; */
        effect->timestamp  = time;
        /* spin_unlock(&effect->lock); */
        int err = audio_stream_run(&effect->entry, &frame);
        if (err < 0) {
            printf("mic input   resume");
            mic_effect_stream_resume(effect);
        }
        WIRELESS_MIC_EFFECT_EXIT_CRITICAL();
        /* printf("live_mic_effect_input  %d",effect->process_len); */
        return	effect->process_len;

    }
    /* spin_unlock(&effect->lock); */
    WIRELESS_MIC_EFFECT_EXIT_CRITICAL();
    printf("effect->start %d", effect->start);
    return 0;

}

/*----------------------------------------------------------------------------*/
/**@brief   数据输出节点
   @param
   @return
   @note
*/
/*----------------------------------------------------------------------------*/
static int live_mic_effect_output_data_handler(
    struct audio_stream_entry *entry,
    struct audio_data_frame *in,
    struct audio_data_frame *out)
{
    struct live_mic_effect *effect = container_of(entry, struct live_mic_effect, output_entry);
    int wlen = 0;
    /* int wlen2 = 0; */
    if (!effect) {
        printf("output   !effect");
        return 0;
    }
    if (effect->start) {
        if (effect->param.output) {

            /* spin_lock(&effect->lock); */
            wlen = effect->param.output(effect->param.output_priv, in->data, in->data_len);
            /* printf("mic_effect output %d",wlen); */
            /* spin_unlock(&effect->lock); */

            if (wlen != in->data_len) {
                /* putchar('B'); */
            }
        }
        return wlen;
    }
    printf("output  !effect->start");
    return 0;
}

int live_mic_effect_add_output(struct audio_stream_entry *output)
{

    if ((g_mic_effect == NULL) || (all_entry_cnt == 0)) {
        printf("%s %d hdl is NULL err %x  %d!\n", __func__, __LINE__, g_mic_effect, all_entry_cnt);
        capture_output = output;
        return -1;
    }
    for (int i = 0; i < 4; i++) {
        if (added_output[i] == output) {
            printf("output entry have added");
            return 0;
        }
    }


    printf("live_mic_effect_add_output");

    spin_lock(&g_mic_effect->lock);
    g_mic_effect->start = 0;
    struct audio_stream_entry *entries_start = entries[all_entry_cnt - 2];
    u8 entry_cnt = 0;
    struct audio_stream_entry *add_entries[4] = {NULL};
    add_entries[entry_cnt++] = entries_start;
    add_entries[entry_cnt++] = output;
    for (int i = 0; i < entry_cnt - 1; i++) {
        audio_stream_add_entry(add_entries[i], add_entries[i + 1]);
    }

    g_mic_effect->start = 1;
    spin_unlock(&g_mic_effect->lock);

    output_channel_cnt++;
    added_output[output_channel_cnt] = output;
    return 0;
}


int live_mic_effect_del_output(struct audio_stream_entry *output)
{
    printf("live_mic_effect_del_output");
    if ((g_mic_effect == NULL) || (all_entry_cnt == 0)) {
        printf("%s %d hdl is NULL err !\n", __func__, __LINE__);
        return -1;
    }

    if (output_channel_cnt) {
        spin_lock(&g_mic_effect->lock);
        g_mic_effect->start = 0;
        /* audio_stream_del_entry(output); */

        for (int i = 0; i < 4; i++) {
            if (added_output[i] == output) {
                added_output[i] = NULL;
            }
        }
        struct audio_stream_entry *entries_start = entries[all_entry_cnt - 2];


        u8 entry_cnt = 0;
        struct audio_stream_entry *del_entries[4] = {NULL};
        del_entries[entry_cnt++] = entries_start;
        del_entries[entry_cnt++] = output;
        /* for (int i = 0; i < entry_cnt - 1; i++) { */
        /*     audio_stream_add_entry(add_entries[i], add_entries[i + 1]); */
        /* } */
        audio_stream_del_list(del_entries, entry_cnt);

        g_mic_effect->start = 1;
        spin_unlock(&g_mic_effect->lock);
        output_channel_cnt--;
        printf("live_mic_effect_del_output success");
        return 0;
    } else {
        return -2;
    }
}

/*----------------------------------------------------------------------------*/
/**@brief   mic 音效数据流激活
   @param
   @return
   @note
*/
/*----------------------------------------------------------------------------*/
static void mic_effect_stream_resume(void *p)
{
    struct live_mic_effect *effect = (struct live_mic_effect *)p;
    if (effect) {
        audio_stream_resume(&effect->entry);
    }
}

/*----------------------------------------------------------------------------*/
/**@brief  echo  reverb 效果处理
   @param
   @return
   @note
*/
/*----------------------------------------------------------------------------*/
static s16 *aud_reverb_process_run(struct aud_reverb_process *hdl, s16 *data, int len)
{
    struct live_mic_effect  *eff = hdl->eff;
    u8 ch_num = eff->param.ch_num;
    /* printf("len = %d\n",len); */
    if (eff->sub_0_plate_reverb_hdl) {
        //ch0
        if (hdl->in_ch == 2) {
            if (hdl->tmpbuf[0]) {
                if (hdl->tmpbuf_len[0] < len) {
                    free(hdl->tmpbuf[0]);
                    hdl->tmpbuf[0] = NULL;
                }
            }
            if (!hdl->tmpbuf[0]) {
                hdl->tmpbuf[0] = malloc(len);//2ch 16 bit buf
                hdl->tmpbuf_len[0] = len;
            }
            u8 *tmp = (u8 *)hdl->tmpbuf[0];
            s16 *tar = (s16 *)&tmp[len / 2];
            pcm_dual_to_single((void *)tar, (void *)data, len);
            run_plate_reverb(eff->sub_0_plate_reverb_hdl, tar, hdl->tmpbuf[0], len / 2); //内部单变双
        } else {
            if (!hdl->bit_wide) { //16 bit
                if (hdl->tmpbuf[0]) {
                    if (hdl->tmpbuf_len[0] < len * 2) {
                        free(hdl->tmpbuf[0]);
                        hdl->tmpbuf[0] = NULL;
                    }
                }
                if (!hdl->tmpbuf[0]) {
                    hdl->tmpbuf[0] = malloc(len * 2);//2ch 16 bit buf
                    hdl->tmpbuf_len[0] = len * 2;
                }
                run_plate_reverb(eff->sub_0_plate_reverb_hdl, data, hdl->tmpbuf[0], len); //16bit 内部单变双
                audio_dec_eq_run(eff->mic_eq1, hdl->tmpbuf[0], hdl->tmpbuf[0], len * 2);// 16bit 2ch
                audio_dec_drc_run(eff->mic_drc1, hdl->tmpbuf[0], len * 2); //16bit 2ch
            } else {
#define OFFSET_POINTS  128  //eq输入输出位宽不一致时，每次计算是32个点，如需复用输入buf，输入buf需比输出buf滞后32个点,以防buf追尾
                if (hdl->tmpbuf[0]) {
                    if (hdl->tmpbuf_len[0] < len * 2 * 2 + OFFSET_POINTS) {
                        free(hdl->tmpbuf[0]);
                        hdl->tmpbuf[0] = NULL;
                    }
                }

                if (!hdl->tmpbuf[0]) {
                    hdl->tmpbuf[0] = malloc(len * 2 * 2 + OFFSET_POINTS);//2ch 32 bit buf
                    hdl->tmpbuf_len[0] = len * 2 * 2 + OFFSET_POINTS;
                }
                u8 *tmp = (u8 *)hdl->tmpbuf[0];
                s16 *tar = (s16 *)&tmp[OFFSET_POINTS + len * 2 * 2 - 2 * len]; //放在最后 16bit 2ch
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
                if (hdl->tmpbuf[1]) {
                    if (hdl->tmpbuf_len[1] < len * 2) {
                        free(hdl->tmpbuf[1]);
                        hdl->tmpbuf[1] = NULL;
                    }
                }
                if (!hdl->tmpbuf[1]) {
                    hdl->tmpbuf[1] = malloc(len * 2);//2ch 16 bit buf
                    hdl->tmpbuf_len[1] = len * 2;
                }
                //ch1
                tmp = (u8 *)hdl->tmpbuf[1];
                tar = (s16 *)&tmp[len];
                run_echo(eff->sub_1_echo_hdl, data, tar, len);
                audio_dec_eq_run(eff->mic_eq2, tar, NULL, len); //1ch
                audio_dec_drc_run(eff->mic_drc2, tar, len); //16bit 1ch
                pcm_single_to_dual((void *)hdl->tmpbuf[1], (void *)tar,  len);//16bit 2ch
            }

            if (hdl->tmpbuf[2]) {
                if (hdl->tmpbuf_len[2] < len * 2) {
                    free(hdl->tmpbuf[2]);
                    hdl->tmpbuf[2] = NULL;
                }
            }
            if (!hdl->tmpbuf[2]) {
                hdl->tmpbuf[2] = malloc(len * 2);//2ch 16 bit buf
                hdl->tmpbuf_len[2] = len * 2;
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
                if (hdl->tmpbuf[1]) {
                    if (hdl->tmpbuf_len[1] < len * 2 * 2) {
                        free(hdl->tmpbuf[1]);
                        hdl->tmpbuf[1] = NULL;
                    }
                }
                if (!hdl->tmpbuf[1]) {
                    hdl->tmpbuf[1] = malloc(len * 2 * 2); //2ch 32 bit buf
                    hdl->tmpbuf_len[1] = len * 2 * 2;
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
            if (hdl->tmpbuf[2]) {
                if (hdl->tmpbuf_len[2] < len * 2 * 2) {
                    free(hdl->tmpbuf[2]);
                    hdl->tmpbuf[2] = NULL;
                }
            }
            if (!hdl->tmpbuf[2]) {
                hdl->tmpbuf[2] = malloc(len * 2 * 2); //2ch 32 bit buf
                hdl->tmpbuf_len[2] = len * 2 * 2;
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
                if (hdl->tmpbuf[1]) {
                    if (hdl->tmpbuf_len[1] < len) {
                        free(hdl->tmpbuf[1]);
                        hdl->tmpbuf[1] = NULL;
                    }
                }
                if (!hdl->tmpbuf[1]) {
                    hdl->tmpbuf[1] = malloc(len);//2ch 16 bit buf
                    hdl->tmpbuf_len[1] = len;
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
            if (hdl->tmpbuf[2]) {
                if (hdl->tmpbuf_len[2] < len) {
                    free(hdl->tmpbuf[2]);
                    hdl->tmpbuf[2] = NULL;
                }
            }
            if (!hdl->tmpbuf[2]) {
                hdl->tmpbuf[2] = malloc(len);//2ch 16bit buf
                hdl->tmpbuf_len[2] = len ;
            }
            audio_dec_eq_run(eff->mic_eq3, data, hdl->tmpbuf[2], len);
            audio_dec_drc_run(eff->mic_drc3, hdl->tmpbuf[2], len); //16bit 2ch
            points = len / 2;
        } else {

            u8 *tmp = NULL;
            s16 *tar = NULL;
            if (eff->sub_1_echo_hdl) {
                if (hdl->tmpbuf[1]) {
                    if (hdl->tmpbuf_len[1] < len * 2) {
                        free(hdl->tmpbuf[1]);
                        hdl->tmpbuf[1] = NULL;
                    }
                }
                if (!hdl->tmpbuf[1]) {
                    hdl->tmpbuf[1] = malloc(len * 2); //2ch 32 bit buf
                    hdl->tmpbuf_len[1] = len * 2;
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
            if (hdl->tmpbuf[2]) {
                if (hdl->tmpbuf_len[2] < len * 2 * 2) {
                    free(hdl->tmpbuf[2]);
                    hdl->tmpbuf[2] = NULL;
                }
            }
            if (!hdl->tmpbuf[2]) {
                hdl->tmpbuf[2] = malloc(len * 2 * 2); //2ch 32 bit buf
                hdl->tmpbuf_len[2] = len * 2 * 2;
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
static void audio_reverb_process_output_data_process_len(struct audio_stream_entry *entry,  int len)
{
    struct aud_reverb_process *hdl = container_of(entry, struct aud_reverb_process, entry);
}


/*----------------------------------------------------------------------------*/
/**@brief  echo  reverb 效果处理 节点
   @param
   @return
   @note
*/
/*----------------------------------------------------------------------------*/
static int audio_reverb_process_data_handler(struct audio_stream_entry *entry,
        struct audio_data_frame *in,
        struct audio_data_frame *out)
{
    struct aud_reverb_process *hdl = container_of(entry, struct aud_reverb_process, entry);
    /* if (in->data_len != 304) { */
    /* printf("in->data_len %d\n", in->data_len); */
    /* } */

    out->data_sync = in->data_sync;
    out->channel = in->channel;
    out->data_len = out->channel * in->data_len;
    hdl->in_ch = in->channel;

    out->data = aud_reverb_process_run(hdl, (short *)((int)in->data + in->offset), (in->data_len - in->offset)); //默认输出2ch
    if (out->channel == 1) {
        pcm_dual_to_single((void *)out->data, (void *)out->data, in->data_len * 2);
    }

    return in->data_len;
}

static void live_mic_effect_data_process_len(struct audio_stream_entry *entry,  int len)
{
    struct live_mic_effect *effect = container_of(entry, struct live_mic_effect, entry);
    effect->process_len = len;
}

/*----------------------------------------------------------------------------*/
/**@brief  打开echo  reverb 效果处理
   @param
   @return
   @note
*/
/*----------------------------------------------------------------------------*/
static struct aud_reverb_process *aud_reverb_open(struct live_mic_effect *effect, u8 bit_wide)
{
    struct aud_reverb_process *hdl = zalloc(sizeof(struct aud_reverb_process));
    hdl->eff = effect;
    hdl->bit_wide = bit_wide;
    hdl->entry.data_process_len = audio_reverb_process_output_data_process_len;
    hdl->entry.data_handler = audio_reverb_process_data_handler;

    return hdl;
}

/*----------------------------------------------------------------------------*/
/**@brief  关闭echo  reverb 效果处理
   @param
   @return
   @note
*/
/*----------------------------------------------------------------------------*/
static void aud_reverb_close(struct aud_reverb_process *hdl)
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

/*----------------------------------------------------------------------------*/
/**@brief  打开mic 音效处理
   @param
   @return
   @note
*/
/*----------------------------------------------------------------------------*/
void live_mic_effect_open(struct live_mic_effect *effect)
{

    u8 mode = get_mic_eff_mode();
    struct eff_parm *mic_eff = &eff_mode[mode];

    u8 ch_num = effect->param.ch_num;

//降噪节点
#if MIC_EFFECT_LLNS && WIRELESS_MIC_CONFIG_LLNS
    effect->llns = stream_llns_open(&llns_parm, effect->param.sample_rate, 1);
#endif

//噪声门限节点
#if WIRELESS_MIC_CONFIG_NOISEGATE
    effect->noisegate = audio_noisegate_open_demo(AEID_MIC_NS_GATE, effect->param.sample_rate, effect->param.ch_num);
#endif

//EQ节点
#if WIRELESS_MIC_CONFIG_EQ
    effect->mic_eq0 = mic_eq_open(effect->param.sample_rate, effect->param.ch_num, AEID_MIC_EQ0);
    if (effect->mic_eq0 && effect->mic_eq0->out_32bit) {
        effect->mic_drc0 = mic_drc_open(effect->param.sample_rate, effect->param.ch_num, AEID_MIC_DRC0);
        effect->convert0 = convet_data_open(0,  0);
    }
#endif

//啸叫抑制

#if WIRELESS_MIC_CONFIG_HOWLING
    printf("open_howling\n\n\n");
    HOWLING_PITCHSHIFT_PARM parm = {0};
    parm.effect_v = mic_eff->howlingps_parm.parm.effect_v;
    parm.ps_parm = mic_eff->howlingps_parm.parm.ps_parm;
    parm.fe_parm = mic_eff->howlingps_parm.parm.fe_parm;
    parm.dataTypeobj.IndataBit = global_bit_wide ? DATA_INT_32BIT : DATA_INT_16BIT;
    parm.dataTypeobj.OutdataBit = global_bit_wide ? DATA_INT_32BIT : DATA_INT_16BIT;
    parm.dataTypeobj.IndataInc = 1;
    parm.dataTypeobj.OutdataInc = 1;
    parm.dataTypeobj.Qval = global_bit_wide ? 23 : 15;
    effect->howling_ps = open_howling(&parm, effect->param.sample_rate, 0, 1);//以频
#endif
#if WIRELESS_MIC_CONFIG_HOWLING_TRAP
    printf("open_howling_trap\n\n\n");
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
    effect->notch_howling = open_howling(&howling_param, effect->param.sample_rate, 0, 0);//陷波
#endif

// 变声节点
#if TCFG_MIC_VOICE_CHANGER_ENABLE && WIRELESS_MIC_CONFIG_VOICE_CHANGE
    effect->voice_changer = audio_voice_changer_open_demo(AEID_MIC_VOICE_CHANGER, effect->param.sample_rate);
#endif

// ECHO节点
#if WIRELESS_MIC_CONFIG_ECHO
    ECHO_PARM_SET e_parm = {0};
    memcpy(&e_parm, &echo_parm, sizeof(struct echo_update_parm));
    e_parm.dataTypeobj.IndataBit = 0;
    e_parm.dataTypeobj.OutdataBit = 0;
    e_parm.dataTypeobj.IndataInc = 1;
    e_parm.dataTypeobj.OutdataInc = 1;
    e_parm.dataTypeobj.Qval = 15;
    effect->sub_1_echo_hdl = open_echo((ECHO_PARM_SET *)&e_parm, (EF_REVERB_FIX_PARM *)&effect_echo_fix_parm_default);//(&echo_parm, &echo_fix_parm);
#endif

// REVERB节点
#if WIRELESS_MIC_CONFIG_REVERB
    Plate_reverb_parm r_parm = {0};
    memcpy(&r_parm, &reverb_parm, sizeof(struct  plate_reverb_update_parm));
    r_parm.dataTypeobj.IndataBit = 0;
    r_parm.dataTypeobj.OutdataBit = 0;
    r_parm.dataTypeobj.IndataInc = 1;
    r_parm.dataTypeobj.OutdataInc = 2;
    r_parm.dataTypeobj.Qval = 15;
    effect->sub_0_plate_reverb_hdl = open_plate_reverb((Plate_reverb_parm *)&r_parm, effect->param.sample_rate);//(&reverb_parm, effect->param.sample_rate);
    if (ch_num == 1) {
        effect->ch_switch = channel_switch_open(AUDIO_CH_DIFF, 512);
    }
#endif

#if WIRELESS_MIC_CONFIG_DEMO
    effect->demo = adapter_stream_demo_open();
#endif

#if SYS_DIGVOL_GROUP_EN&&WIRELESS_MASTER_MIC_EFFECT_ENABLE
    extern void *sys_digvol_group_ch_open(char *logo, int vol_start, audio_dig_vol_param * parm);
    effect->dvol_entry = sys_digvol_group_ch_open("music_live_capture", -1, NULL);
#endif
}

/*----------------------------------------------------------------------------*/
/**@brief  添加mic 音效处理节点
   @param
   @return
   @note
*/
/*----------------------------------------------------------------------------*/
int live_mic_effect_stream_entry_add(struct live_mic_effect *effect, struct audio_stream_entry **entries, int max)
{
    int entry_cnt = 0;

#if WIRELESS_MIC_CONFIG_NOISEGATE
    if (effect->noisegate) {
        entries[entry_cnt++] = &effect->noisegate->entry;
    }
#endif

//EQ节点
#if WIRELESS_MIC_CONFIG_EQ
    if (effect->mic_eq0) {
        entries[entry_cnt++] = &effect->mic_eq0->entry;
        if (effect->mic_eq0->out_32bit) {
            if (effect->mic_drc0) {
                entries[entry_cnt++] = &effect->mic_drc0->entry;
            }
            if (effect->convert0) {
                entries[entry_cnt++] = &effect->convert0->entry;
            }
        }
    }
#endif

#if WIRELESS_MIC_CONFIG_HOWLING
    if (effect->howling_ps) {
        entries[entry_cnt++] = &effect->howling_ps->entry;
    }
#endif

#if WIRELESS_MIC_CONFIG_HOWLING_TRAP
    if (effect->notch_howling) {
        entries[entry_cnt++] = &effect->notch_howling->entry;
    }
#endif

#if TCFG_MIC_VOICE_CHANGER_ENABLE && WIRELESS_MIC_CONFIG_VOICE_CHANGE
    if (effect->voice_changer) {
        entries[entry_cnt++] = &effect->voice_changer->entry;
    }
#endif


#if MIC_EFFECT_LLNS && WIRELESS_MIC_CONFIG_LLNS
    if (effect->llns) {
        entries[entry_cnt++] = &effect->llns->entry;
    }
#endif


#if WIRELESS_MIC_CONFIG_ECHO
    if (effect->sub_1_echo_hdl) {
        entries[entry_cnt++] = &effect->sub_1_echo_hdl->entry;
    }
#endif


#if WIRELESS_MIC_CONFIG_REVERB
    if (effect->sub_0_plate_reverb_hdl) {
        entries[entry_cnt++] = &effect->sub_0_plate_reverb_hdl->entry;
    }
    if (effect->ch_switch) {
        entries[entry_cnt++] = &effect->ch_switch->entry;
    }
#endif

    /* if (effect->aud_reverb) { */
    /*     entries[entry_cnt++] = &effect->aud_reverb->entry; */
    /* } */

    /* if (effect->mic_eq4) { */
    /*     entries[entry_cnt++] = &effect->mic_eq4->entry; */
    /*     if (effect->mic_eq4->out_32bit) { */
    /*         if (effect->mic_drc4) { */
    /*             entries[entry_cnt++] = &effect->mic_drc4->entry; */
    /*         } */
    /*         if (effect->convert4) { */
    /*             entries[entry_cnt++] = &effect->convert4->entry; */
    /*         } */
    /*     } */
    /* } */
#if WIRELESS_MIC_CONFIG_DEMO
    if (effect->demo) {
        entries[entry_cnt++] = &effect->demo->stream->entry;
    }
#endif

#if SYS_DIGVOL_GROUP_EN&&WIRELESS_MASTER_MIC_EFFECT_ENABLE
    entries[entry_cnt++] = effect->dvol_entry;
#endif
    return entry_cnt;
}

#if 0
static void wireless_mic_effect_task(void *priv)
{
    int msg[16];
    int res;
    while (1) {
        res = os_taskq_pend("taskq", msg, ARRAY_SIZE(msg));
        if (res == OS_TASKQ) {
            switch (msg[1]) {
            case WIRELESS_AUDIO_DATA:
                WIRELESS_MIC_EFFECT_ENTER_CRITICAL();
                live_mic_effect_input((void *)msg[2], (void *)msg[3], (u32)msg[4], msg[5]);
                WIRELESS_MIC_EFFECT_EXIT_CRITICAL();
                break;
            }
        }
    }


}
#endif
/*----------------------------------------------------------------------------*/
/**@brief  mic 音效处理节点初始化
   @param   mic 音效处理参数
   @return
   @note
*/
/*----------------------------------------------------------------------------*/

static int live_mic_effect_early_init()
{
    WIRELESS_MIC_EFFECT_INIT_CRITICAL();
    return 0;
}
module_initcall(live_mic_effect_early_init);

struct live_mic_effect *live_mic_effect_init(struct live_mic_effect_param *param)
{
    struct live_mic_effect *effect = (struct live_mic_effect *)zalloc(sizeof(struct live_mic_effect));
    if (effect == NULL) {
        return NULL;
    }
    g_mic_effect = effect;
    memcpy(&effect->param, param, sizeof(struct live_mic_effect_param));
    printf("live mic effect->param.sample_rate %d  ch_num %d\n", effect->param.sample_rate, effect->param.ch_num);

    if (effect->param.ch_num > 1) {
        printf("[error] --- data channel number not 1 , mic effect set bypass !!!\n");
        effect->bypass = 1;
    }
    //打开音效处理
    if (!effect->bypass) {
        live_mic_effect_open(effect);
    }

    spin_lock_init(&effect->lock);
    os_sem_create(&effect->sem, 1);

    effect->entry.data_process_len = live_mic_effect_data_process_len;
    effect->output_entry.data_handler = live_mic_effect_output_data_handler; //数据输出节点

// 数据流串联
    all_entry_cnt = 0;
    entries[all_entry_cnt++] = &effect->entry;

    if (!effect->bypass) {
        //添加音效处理节点
        all_entry_cnt += live_mic_effect_stream_entry_add(effect, &entries[all_entry_cnt], ARRAY_SIZE(entries) - all_entry_cnt);
    }
    entries[all_entry_cnt++] = &effect->output_entry;  //音效输出节点，放最后面

    effect->stream = audio_stream_open(effect, mic_effect_stream_resume);
    audio_stream_add_list(effect->stream, entries, all_entry_cnt);

    if (capture_output) {
        live_mic_effect_add_output(capture_output);
        capture_output == NULL;
    }
#if MIC_EFFECT_ASYNC_EN
#if 0//MIC_EFFECT_ASYNC_EN
    printf("-----------------create asyns task---------------");
    int err = task_create(wireless_mic_effect_task, NULL, "wireless_mic_effect");
    if (err) {
        printf("wireless_mic_effect_task create fail err %d", err);
        free(effect);
        return NULL;
    }
#else
    struct __mic_stream_parm *mic_parm = (struct __mic_stream_parm *)&effect_mic_stream_parm_default;
    effect->mic_stream = mic_stream_creat(mic_parm);
    if (effect->mic_stream == NULL) {
        printf("wireless_mic_effect mic_stream_creat err ");
        return NULL;
    }
    mic_stream_set_output(effect->mic_stream, (void *)effect, live_mic_effect_async_input);
    mic_stream_start(effect->mic_stream);
#endif
#endif
    effect->start = 1;


#if 0//数据流启动后以下音效节点bypass，需要手动开启
    live_mic_plate_reverb_bypass(1);

    live_mic_echo_bypass(1);

    live_mic_howling_pitch_shift_bypass(1);

    live_mic_notchhowline_bypass(1);

    live_mic_llns_bypass(0);

    live_mic_voice_changer_bypass(1);
#endif
    return effect;
}


#if 0//MIC_EFFECT_ASYNC_EN
int live_mic_effect_async_input(void *effect, void *data, int len)
{
    struct live_mic_effect *ctx = (struct live_mic_effect *)effect;

    u32 time = 0;
    if (!ctx) {
        printf("effect = 0");
        return 0;
    }

    WIRELESS_MIC_EFFECT_ENTER_CRITICAL();
    if (!ctx->start) {
        printf("effect->start = 0");
        return 0;
    }
    memcpy(temp_data, data, len);
#if TCFG_BROADCAST_ENABLE
    wireless_dev_get_cur_clk("big_tx", &time);
#elif TCFG_CONNECTED_ENABLE
#if (WIRELESS_2T1_DUPLEX_ROLE == ROLE_PERIP)
    wireless_dev_get_cur_clk("cig_perip", &time);
#else
    wireless_dev_get_cur_clk("cig_central", &time);
#endif
#endif
    int ret = os_taskq_post_msg("wireless_mic_effect", 5, WIRELESS_AUDIO_DATA, ctx, temp_data, len, time);
    if (ret) {
        r_printf("dma input error\n");
    }
    WIRELESS_MIC_EFFECT_EXIT_CRITICAL();
    return len;
}
#else

int live_mic_effect_async_input(void *effect, void *in, void *out, u32 inlen, u32 outlen)
{
    struct live_mic_effect *ctx = (struct live_mic_effect *)effect;

    u32 time = 0;
    if (!g_mic_effect) {
        printf("effect = 0");
        return 0;
    }

    WIRELESS_MIC_EFFECT_ENTER_CRITICAL();
    if (!g_mic_effect->start) {
        printf("effect->start = 0");
        return 0;
    }
#if TCFG_BROADCAST_ENABLE
    wireless_dev_get_cur_clk("big_tx", &time);
#elif TCFG_CONNECTED_ENABLE
#if (WIRELESS_2T1_DUPLEX_ROLE == ROLE_PERIP)
    wireless_dev_get_cur_clk("cig_perip", &time);
#else
    wireless_dev_get_cur_clk("cig_central", &time);
#endif
#endif

    struct audio_data_frame frame = {0};
    frame.channel = ctx->param.ch_num;
    frame.sample_rate = ctx->param.sample_rate;
    frame.data_len = inlen;
    frame.data = in;
    frame.timestamp = time;
    ctx->out_len = 0;
    ctx->process_len = inlen;
    /* printf("in_len = %d",inlen); */
    audio_stream_run(&ctx->entry, &frame);

    WIRELESS_MIC_EFFECT_EXIT_CRITICAL();
    return	ctx->process_len;


}

#endif
/************广播mic音效模块参数更新接口(调试工具/模式切换会使用勿修改)************/
//获取mic effect 状态
u8 live_mic_effect_status_get()
{
    return ((g_mic_effect) ? 1 : 0);
}


void live_mic_plate_reverb_update_parm(void *parm, int bypass)
{
    if (g_mic_effect && g_mic_effect->sub_0_plate_reverb_hdl) {
        update_plate_reverb_parm(g_mic_effect->sub_0_plate_reverb_hdl, parm);
        plate_reverb_update_bypass(g_mic_effect->sub_0_plate_reverb_hdl, bypass);
    }
}


void live_mic_plate_reverb_bypass(int bypass)
{
    if (g_mic_effect && g_mic_effect->sub_0_plate_reverb_hdl) {
        plate_reverb_update_bypass(g_mic_effect->sub_0_plate_reverb_hdl, bypass);
    }
}

void live_mic_echo_updata_parm(void *parm, int bypass)
{
    if (g_mic_effect && g_mic_effect->sub_1_echo_hdl) {
        update_echo_parm(g_mic_effect->sub_1_echo_hdl, parm);
        echo_update_bypass(g_mic_effect->sub_1_echo_hdl, bypass);
    }
}

void live_mic_echo_bypass(int bypass)
{
    if (g_mic_effect && g_mic_effect->sub_1_echo_hdl) {
        echo_update_bypass(g_mic_effect->sub_1_echo_hdl, bypass);
    }
}

void live_mic_howling_pitch_shift_update_parm(void *parm, int bypass)
{
    if (g_mic_effect && g_mic_effect->howling_ps) {
        update_howling_parm(g_mic_effect->howling_ps, parm);
        howling_update_bypass(g_mic_effect->howling_ps, bypass);
    }
}

void live_mic_howling_pitch_shift_bypass(int bypass)
{
    if (g_mic_effect && g_mic_effect->howling_ps) {
        howling_update_bypass(g_mic_effect->howling_ps, bypass);
    }
}

void live_mic_notchhowline_update_parm(void *parm, int bypass)
{
    if (g_mic_effect && g_mic_effect->notch_howling) {
        update_howling_parm(g_mic_effect->notch_howling, parm);
        howling_update_bypass(g_mic_effect->notch_howling, bypass);
    }
}

void live_mic_notchhowline_bypass(int bypass)
{
    if (g_mic_effect && g_mic_effect->notch_howling) {
        howling_update_bypass(g_mic_effect->notch_howling, bypass);
    }
}

void live_mic_llns_bypass(int bypass)
{
    if (g_mic_effect && g_mic_effect->llns) {
        stream_llns_switch(g_mic_effect->llns, bypass);
    }
}


void live_mic_voice_changer_bypass(int bypass)
{
    if (g_mic_effect && g_mic_effect->voice_changer) {
        audio_voice_changer_bypass(AEID_MIC_VOICE_CHANGER, bypass);
    }

}


/*----------------------------------------------------------------------------*/
/**@brief  关闭mic 音效处理
   @param
   @return
   @note
*/
/*----------------------------------------------------------------------------*/
void live_mic_effect_close(struct live_mic_effect *effect)
{
    if (!effect) {
        return;
    }

    WIRELESS_MIC_EFFECT_ENTER_CRITICAL();
    effect->start = 0;
    capture_output = 0;

#if MIC_EFFECT_ASYNC_EN
    mic_stream_destroy(&effect->mic_stream);
#endif
#if SYS_DIGVOL_GROUP_EN&&WIRELESS_MASTER_MIC_EFFECT_ENABLE
    extern  int sys_digvol_group_ch_close(char *logo);
    sys_digvol_group_ch_close("music_live_capture");
#endif // SYS_DIGVOL_GROUP_EN

    if (effect->demo) {
        adapter_stream_demo_close(&effect->demo);
    }
#if MIC_EFFECT_LLNS && WIRELESS_MIC_CONFIG_LLNS
    if (effect->llns) {
        stream_llns_close(&effect->llns);
    }
#endif

    if (effect->noisegate) {
        audio_noisegate_close(effect->noisegate);
    }

    if (effect->mic_eq0) {
        mic_eq_close(effect->mic_eq0);
        if (effect->mic_drc0) {
            mic_drc_close(effect->mic_drc0);
        }
        if (effect->convert0) {
            convet_data_close(effect->convert0);
        }
    }

    if (effect->mic_eq1) {
        mic_eq_close(effect->mic_eq1);
        if (effect->mic_drc1) {
            mic_drc_close(effect->mic_drc1);
        }
    }

    if (effect->mic_eq2) {
        mic_eq_close(effect->mic_eq2);
        if (effect->mic_drc2) {
            mic_drc_close(effect->mic_drc2);
        }

    }
    if (effect->mic_eq3) {
        mic_eq_close(effect->mic_eq3);
        if (effect->mic_drc3) {
            mic_drc_close(effect->mic_drc3);
        }
    }

    if (effect->howling_ps) {
        close_howling(effect->howling_ps);
    }
    if (effect->notch_howling) {
        close_howling(effect->notch_howling);
    }

#if TCFG_MIC_VOICE_CHANGER_ENABLE && WIRELESS_MIC_CONFIG_VOICE_CHANGE
    if (effect->voice_changer) {
        audio_voice_changer_close_demo(effect->voice_changer);
    }
#endif

    if (effect->sub_1_echo_hdl) {
        close_echo(effect->sub_1_echo_hdl);
    }
    if (effect->sub_0_plate_reverb_hdl) {
        close_plate_reverb(effect->sub_0_plate_reverb_hdl);
    }

    if (effect->ch_switch) {
        channel_switch_close(&effect->ch_switch);
    }
    /* if (effect->aud_reverb) { */
    /*     aud_reverb_close(effect->aud_reverb); */
    /* } */

    /* if (effect->mic_eq4) { */
    /*     mic_eq_close(effect->mic_eq4); */
    /*     if (effect->mic_drc4) { */
    /*         mic_drc_close(effect->mic_drc4); */
    /*     } */
    /*     if (effect->convert4) { */
    /*         convet_data_close(effect->convert4); */
    /*     } */
    /* } */
    if (effect->stream) {
        audio_stream_close(effect->stream);
    }

    all_entry_cnt = 0;
    memset(entries, 0, sizeof(entries));
    output_channel_cnt = 0;
    memset(added_output, 0, sizeof(added_output));

#if 0//MIC_EFFECT_ASYNC_EN
    task_kill("wireless_mic_effect");
#endif

    free(effect);
    effect = NULL;
    g_mic_effect = NULL;

    WIRELESS_MIC_EFFECT_EXIT_CRITICAL();
}

#endif//WIRELESS_MIC_EFFECT_ENABLE

