#include "asm/includes.h"
#include "media/includes.h"
#include "system/includes.h"
#include "typedef.h"
#include "app_config.h"
#include "app_cfg.h"
#include "asm/audio_src.h"
#include "media/pcm_decoder.h"
#include "audio_link.h"
#include "audio_dec.h"
#include "effectrs_sync.h"
#include "clock_cfg.h"
#include "audio_effect/audio_dynamic_eq_demo.h"
#include "audio_effect/audio_eff_default_parm.h"
#include "audio_config.h"
#include "audio_track.h"
#include "connected_api.h"

#if TCFG_AUDIO_INPUT_IIS

#if (SOUNDCARD_ENABLE)
#include "audio_usb_mix_mic.h"
extern void *usb_mix_fifo;
void *iis_mix_fifo_ch = NULL;
#endif

#define IIS_IN_STORE_PCM_SIZE   (4 * 1024)

struct iis_in_sample_hdl {
    OS_SEM sem;
    s16 *store_pcm_buf;
    cbuffer_t cbuf;
    void (*resume)(void);
    u8 channel_num;
    volatile u8 wait_resume;
    u16 output_fade_in_gain;
    u8 output_fade_in;
    u16 sample_rate;
    void *audio_track;
    void *hw_alink;
    void *alink_ch;
};

struct iis_in_dec_hdl {
    struct audio_stream *stream;	// 音频流
    struct pcm_decoder pcm_dec;		// pcm解码句柄
    struct audio_res_wait wait;		// 资源等待句柄
    struct audio_mixer_ch mix_ch;	// 叠加句柄
#if (RECORDER_MIX_EN)
    void *rec_mix_ch;	// 叠加句柄
#endif/*RECORDER_MIX_EN*/

#if AUDIO_SURROUND_CONFIG
    surround_hdl *surround;         //环绕音效句柄
#endif


#if AUDIO_VBASS_CONFIG
    struct aud_gain_process *vbass_prev_gain;
    NOISEGATE_API_STRUCT *ns_gate;
    vbass_hdl *vbass;               //虚拟低音句柄
#endif
    struct convert_data *convert_16_to_32;
    struct convert_data *convert_32_to_16;

#if defined(TCFG_MUSIC_HARMONIC_EXCITER_ENABLE) &&TCFG_MUSIC_HARMONIC_EXCITER_ENABLE
    struct audio_harmonic_exciter *harmonic_exciter;
#endif

#if TCFG_EQ_ENABLE && TCFG_AUDIO_OUT_EQ_ENABLE
    struct audio_eq  *high_bass;
    struct audio_drc *hb_drc;//高低音后的drc
#if defined(TCFG_DRC_ENABLE) && TCFG_DRC_ENABLE
    struct convert_data *hb_convert;
#endif
#endif

    struct audio_eq *eq;    //eq drc句柄
    struct audio_drc *drc;    // drc句柄
#if (defined(TCFG_DRC_SPILT_ENABLE) && TCFG_DRC_SPILT_ENABLE)
    struct audio_drc *drc_fr;    // drc句柄
#endif
#if defined(TCFG_DRC_ENABLE) && TCFG_DRC_ENABLE
    struct convert_data *convert;
#endif
#if defined(MUSIC_EXT_EQ_AFTER_DRC) && MUSIC_EXT_EQ_AFTER_DRC
    struct audio_eq *ext_eq;    //eq drc句柄 扩展eq
#endif
#if defined(TCFG_DYNAMIC_EQ_ENABLE) && TCFG_DYNAMIC_EQ_ENABLE
    struct audio_eq *eq2;    //eq drc句柄
    struct dynamic_eq_hdl *dy_eq;
    struct dynamic_eq_pro *dy_eq_pro;
    struct audio_drc *last_drc;
    struct convert_data *convert2;
#endif
#if defined(TCFG_PHASER_GAIN_AND_CH_SWAP_ENABLE) && TCFG_PHASER_GAIN_AND_CH_SWAP_ENABLE
    struct aud_gain_process *gain;
#endif
#if SOUND_TRACK_1_P_1_CH_CONFIG && (EFFECT_PROCESS_POITION_CONFIG == EFFECT_BEFORE_STEREO_MIX)
    struct channel_switch *fl_fr_ch_2to1;//声道变换
#endif

#if TCFG_EQ_DIVIDE_ENABLE&& !WIRELESS_SOUND_TRACK_2_P_X_ENABLE
    struct audio_eq *eq_rl_rr;    //eq drc句柄
    struct audio_drc *drc_rl_rr;    // drc句柄
#if (defined(TCFG_DRC_SPILT_ENABLE) && TCFG_DRC_SPILT_ENABLE)
    struct audio_drc *drc_rr;    // drc句柄
#endif

    struct convert_data *convert_rl_rr;
#if defined(TCFG_PHASER_GAIN_AND_CH_SWAP_ENABLE) && TCFG_PHASER_GAIN_AND_CH_SWAP_ENABLE
    struct aud_gain_process *gain_rl_rr;
#endif
    struct audio_vocal_tract vocal_tract;//声道合并目标句柄
    struct audio_vocal_tract_ch synthesis_ch_fl_fr;//声道合并句柄
    struct audio_vocal_tract_ch synthesis_ch_rl_rr;//声道合并句柄
    struct channel_switch *ch_switch;//声道变换
#if SOUND_TRACK_1_P_1_CH_CONFIG && (EFFECT_PROCESS_POITION_CONFIG == EFFECT_BEFORE_STEREO_MIX)
    struct channel_switch *rl_rr_ch_2to1;//声道变换
#endif

#if defined(MUSIC_EXT_EQ_AFTER_DRC) && MUSIC_EXT_EQ_AFTER_DRC
    struct audio_eq *ext_eq2;    //eq drc句柄 扩展eq
#endif
#endif

    u32 id;				// 唯一标识符，随机值
    u32 start : 1;		// 正在解码
    void *iis_in;		// 底层驱动句柄
};

//////////////////////////////////////////////////////////////////////////////

extern ALINK_PARM alink0_platform_data;

struct iis_in_dec_hdl *iis_in_dec = NULL;	// iis_in解码句柄
static void (*iis_in_capture_handler)(void *, void *, int) = NULL;

//////////////////////////////////////////////////////////////////////////////

void set_iis_in_capture_handler(void *priv, void (*handler)(void *, void *, int))
{
    iis_in_capture_handler = handler;
}

static void iis_in_dma_data_handler(void *priv, void *_data, int len)
{
    struct iis_in_sample_hdl *iis_in = (struct iis_in_sample_hdl *)priv;
    s16 *data = (s16 *)_data;
    u16 temp_len = len;

    if (iis_in->audio_track) {
        audio_local_sample_track_in_period(iis_in->audio_track, (len >> 1));
    }


    if (iis_in->output_fade_in) {
        s32 tmp_data;
        //printf("fade:%d\n",aec_hdl->output_fade_in_gain);
        for (int i = 0; i < len / 2; i++) {
            tmp_data = data[i];
            data[i] = tmp_data * iis_in->output_fade_in_gain >> 7;

        } //end of for

        iis_in->output_fade_in_gain += 2 ;
        if (iis_in->output_fade_in_gain >= 128) {
            iis_in->output_fade_in = 0;
        }

    }

    int wlen = cbuf_write(&iis_in->cbuf, data, len);

    if (connected_2t1_duplex && (connected_role_config == CONNECTED_ROLE_CENTRAL)) {
        if (iis_in_capture_handler) {
            iis_in_capture_handler(NULL, _data, len);
        }
    }

    /* os_sem_post(&iis_in->sem); */
    if (wlen != len) {
        putchar('W');
    }
    if (iis_in->resume) {
        local_irq_disable();
        if (iis_in->wait_resume) {
            iis_in->wait_resume = 0;
            iis_in->resume();
        }
        local_irq_enable();
    }
    alink_set_shn(&alink0_platform_data.ch_cfg[1], temp_len / 4);
}

static int iis_in_sample_read(void *hdl, void *data, int len)
{
    struct iis_in_sample_hdl *iis_in = (struct iis_in_sample_hdl *)hdl;
    int rlen = cbuf_get_data_size(&iis_in->cbuf);
    if (rlen) {
        rlen = rlen > len ? len : rlen;
        rlen = cbuf_read(&iis_in->cbuf, data, rlen);
        if (!rlen) {
            putchar('$');
        }
    }

    local_irq_disable();
    if (rlen == 0) {
        iis_in->wait_resume = 1;
    }
    local_irq_enable();

    return rlen;
}

static int iis_in_stream_sample_rate(void *hdl)
{
    struct iis_in_sample_hdl *iis_in = (struct iis_in_sample_hdl *)hdl;

    if (iis_in->audio_track) {
        int sr =  audio_local_sample_track_rate(iis_in->audio_track);
        if ((sr < (iis_in->sample_rate + 200)) && (sr > iis_in->sample_rate - 200)) {
            return sr;
        }
        printf("iis_in audio_track reset \n");
        local_irq_disable();
        audio_local_sample_track_close(iis_in->audio_track);
        iis_in->audio_track = audio_local_sample_track_open(iis_in->channel_num, iis_in->sample_rate, 1000);
        local_irq_enable();
    }

    return iis_in->sample_rate;
}

static int iis_in_sample_size(void *hdl)
{
    struct iis_in_sample_hdl *iis_in = (struct iis_in_sample_hdl *)hdl;
    return cbuf_get_data_size(&iis_in->cbuf);
}

static int iis_in_sample_total(void *hdl)
{
    struct iis_in_sample_hdl *iis_in = (struct iis_in_sample_hdl *)hdl;
    return iis_in->cbuf.total_len;
}

static void *iis_in_open(u16 sample_rate)
{
    struct iis_in_sample_hdl *iis_in = NULL;

    iis_in = zalloc(sizeof(struct iis_in_sample_hdl));
    if (!iis_in) {
        return NULL;
    }

    memset(iis_in, 0x0, sizeof(struct iis_in_sample_hdl));
    iis_in->store_pcm_buf = malloc(IIS_IN_STORE_PCM_SIZE);
    if (!iis_in->store_pcm_buf) {
        return NULL;
    }

    iis_in->output_fade_in_gain = 0;
    iis_in->output_fade_in = 1;
    iis_in->channel_num = 2;

    cbuf_init(&iis_in->cbuf, iis_in->store_pcm_buf, IIS_IN_STORE_PCM_SIZE);

    iis_in->hw_alink = alink_init(&alink0_platform_data);
    iis_in->alink_ch = alink_channel_init(iis_in->hw_alink, 1, ALINK_DIR_RX, (void *)iis_in, iis_in_dma_data_handler);
    alink_start(iis_in->hw_alink);

    iis_in->sample_rate = sample_rate;
    iis_in->audio_track = audio_local_sample_track_open(iis_in->channel_num, sample_rate, 1000);

    return iis_in;
}

static void iis_in_close(void *hdl)
{
    struct iis_in_sample_hdl *iis_in = (struct iis_in_sample_hdl *)hdl;
    if (!iis_in) {
        return;
    }
    alink_uninit(iis_in->hw_alink);
    free(iis_in->store_pcm_buf);
    free(iis_in);
}

//////////////////////////////////////////////////////////////////////////////

/*----------------------------------------------------------------------------*/
/**@brief    iis_in解码释放
   @param
   @return
   @note
*/
/*----------------------------------------------------------------------------*/
void iis_in_dec_relaese()
{
    if (iis_in_dec) {
        audio_decoder_task_del_wait(&decode_task, &iis_in_dec->wait);
        clock_remove(AUDIO_CODING_PCM);
        local_irq_disable();
        free(iis_in_dec);
        iis_in_dec = NULL;
        local_irq_enable();
    }
}

/*----------------------------------------------------------------------------*/
/**@brief    iis_in解码事件处理
   @param    *decoder: 解码器句柄
   @param    argc: 参数个数
   @param    *argv: 参数
   @return
   @note
*/
/*----------------------------------------------------------------------------*/
static void iis_in_dec_event_handler(struct audio_decoder *decoder, int argc, int *argv)
{
    switch (argv[0]) {
    case AUDIO_DEC_EVENT_END:
        if (!iis_in_dec) {
            log_i("iis_in_dec handle err ");
            break;
        }

        if (iis_in_dec->id != argv[1]) {
            log_w("iis_in_dec id err : 0x%x, 0x%x \n", iis_in_dec->id, argv[1]);
            break;
        }

        iis_in_dec_close();
        //audio_decoder_resume_all(&decode_task);
        break;
    }
}


/*----------------------------------------------------------------------------*/
/**@brief    iis_in解码数据输出
   @param    *entry: 音频流句柄
   @param    *in: 输入信息
   @param    *out: 输出信息
   @return   输出长度
   @note     *out未使用
*/
/*----------------------------------------------------------------------------*/
static int iis_in_dec_data_handler(struct audio_stream_entry *entry,
                                   struct audio_data_frame *in,
                                   struct audio_data_frame *out)
{
    struct audio_decoder *decoder = container_of(entry, struct audio_decoder, entry);
    struct pcm_decoder *pcm_dec = container_of(decoder, struct pcm_decoder, decoder);
    struct iis_in_dec_hdl *dec = container_of(pcm_dec, struct iis_in_dec_hdl, pcm_dec);
    if (!dec->start) {
        return 0;
    }
    audio_stream_run(&decoder->entry, in);

    return decoder->process_len;
}

/*----------------------------------------------------------------------------*/
/**@brief    iis_in解码数据流激活
   @param    *p: 私有句柄
   @return
   @note
*/
/*----------------------------------------------------------------------------*/
static void iis_in_dec_out_stream_resume(void *p)
{
    struct iis_in_dec_hdl *dec = p;
    audio_decoder_resume(&dec->pcm_dec.decoder);
}

/*----------------------------------------------------------------------------*/
/**@brief    iis_in解码激活
   @param
   @return
   @note
*/
/*----------------------------------------------------------------------------*/
static void iis_in_dec_resume(void)
{
    if (iis_in_dec) {
        audio_decoder_resume(&iis_in_dec->pcm_dec.decoder);
    }
}

/*----------------------------------------------------------------------------*/
/**@brief    计算iis_in输入采样率
   @param
   @return   采样率
   @note
*/
/*----------------------------------------------------------------------------*/
static int audio_iis_in_input_sample_rate(void *priv)
{
    struct iis_in_dec_hdl *dec = (struct iis_in_dec_hdl *)priv;
    int sample_rate = iis_in_stream_sample_rate(dec->iis_in);
    int buf_size = iis_in_sample_size(dec->iis_in);
    /* if (dec->pcm_dec.sample_rate == 44100 && audio_mixer_get_sample_rate(&mixer) == 44100) { */
    if ((dec->pcm_dec.sample_rate == 44100) && (audio_mixer_get_original_sample_rate_by_type(&mixer, MIXER_SR_SPEC) == 44100)) {
        sample_rate = 44100 + (sample_rate - 44118);
    }
    if (buf_size >= (iis_in_sample_total(dec->iis_in) * 3 / 5)) {
        sample_rate += (sample_rate * 6 / 10000);
    }
    if (buf_size <= (iis_in_sample_total(dec->iis_in) * 2 / 5)) {
        sample_rate -= (sample_rate * 6 / 10000);
    }
    return sample_rate;
}

static void iis_in_set_resume_handler(void *priv, void (*resume)(void))
{
    struct iis_in_sample_hdl *iis_in = (struct iis_in_sample_hdl *)priv;

    if (iis_in) {
        iis_in->resume = resume;
    }
}

/*----------------------------------------------------------------------------*/
/**@brief    iis_in解码开始
   @param
   @return   0：成功
   @return   非0：失败
   @note
*/
/*----------------------------------------------------------------------------*/
int iis_in_dec_start()
{
    int err;
    struct iis_in_dec_hdl *dec = iis_in_dec;
    struct audio_mixer *p_mixer = &mixer;

    if (!iis_in_dec) {
        return -EINVAL;
    }

    err = pcm_decoder_open(&dec->pcm_dec, &decode_task);
    if (err) {
        goto __err1;
    }

    // 打开iis_in驱动
    dec->iis_in = iis_in_open(dec->pcm_dec.sample_rate);
    iis_in_set_resume_handler(dec->iis_in, iis_in_dec_resume);

    pcm_decoder_set_event_handler(&dec->pcm_dec, iis_in_dec_event_handler, dec->id);
    pcm_decoder_set_read_data(&dec->pcm_dec, iis_in_sample_read, dec->iis_in);
    pcm_decoder_set_data_handler(&dec->pcm_dec, iis_in_dec_data_handler);

#if (SOUNDCARD_ENABLE)
    iis_mix_fifo_ch = mix_fifo_ch_open(usb_mix_fifo, 2);
#endif


    if (!dec->pcm_dec.dec_no_out_sound) {
        audio_mode_main_dec_open(AUDIO_MODE_MAIN_STATE_DEC_IIS);
    }

    // 设置叠加功能
    audio_mixer_ch_open_head(&dec->mix_ch, p_mixer);
#if (!SOUNDCARD_ENABLE && !RECORDER_MIX_EN)
    audio_mixer_ch_set_no_wait(&dec->mix_ch, 1, 10); // 超时自动丢数
#endif

#if (RECORDER_MIX_EN)
    dec->rec_mix_ch = rec_mix_fifo_ch_open(dec->pcm_dec.output_ch_num);
#endif/*RECORDER_MIX_EN*/

#if 0
    if (dec->pcm_dec.dec_no_out_sound) {
        // 自动变采样
        audio_mixer_ch_set_src(&dec->mix_ch, 1, 0);
    } else {
        // 根据buf数据量动态变采样
        struct audio_mixer_ch_sync_info info = {0};
        info.priv = dec->iis_in;
        info.get_total = iis_in_sample_total;
        info.get_size = iis_in_sample_size;
#if (!SOUNDCARD_ENABLE)
        audio_mixer_ch_set_sync(&dec->mix_ch, &info, 1, 1);
#endif/*SOUNDCARD_ENABLE*/
    }
#else /*0*/

    if (dec->pcm_dec.dec_no_out_sound) {
        audio_mixer_ch_follow_resample_enable(&dec->mix_ch, dec, audio_iis_in_input_sample_rate);
    } else {
#if (!SOUNDCARD_ENABLE)
        audio_mixer_ch_set_sample_rate(&dec->mix_ch, dec->pcm_dec.sample_rate);
        if ((dec->pcm_dec.sample_rate == audio_mixer_get_sample_rate(p_mixer)) && (AUDIO_OUT_WAY_TYPE & AUDIO_WAY_TYPE_DAC)) {
        } else {
            audio_mixer_ch_follow_resample_enable(&dec->mix_ch, dec, audio_iis_in_input_sample_rate);
        }
#endif/*SOUNDCARD_ENABLE*/
    }
#endif  /*0*/

    if (global_bit_wide) {
        dec->convert_16_to_32 = convet_data_open(1 << 4 | 1, 2048);
    } else {
#if defined(AUDIO_VBASS_32BIT_OUT_EN)&&AUDIO_VBASS_32BIT_OUT_EN
        dec->convert_32_to_16 = convet_data_open(0, 512);
#endif

    }



#if AUDIO_SURROUND_CONFIG
    //环绕音效
    dec->surround = surround_open_demo(AEID_MUSIC_SURROUND, dec->pcm_dec.output_ch_type);
#endif


#if AUDIO_VBASS_CONFIG
    dec->vbass_prev_gain = audio_gain_open_demo(AEID_MUSIC_VBASS_PREV_GAIN, dec->pcm_dec.output_ch_num);
    dec->ns_gate = audio_noisegate_open_demo(AEID_MUSIC_NS_GATE, dec->pcm_dec.sample_rate, dec->pcm_dec.output_ch_num);
    //虚拟低音
    dec->vbass = audio_vbass_open_demo(AEID_MUSIC_VBASS, dec->pcm_dec.sample_rate, dec->pcm_dec.output_ch_num);
#endif
#if defined(TCFG_MUSIC_HARMONIC_EXCITER_ENABLE) &&TCFG_MUSIC_HARMONIC_EXCITER_ENABLE
    dec->harmonic_exciter = audio_harmonic_exciter_open_api(AEID_MUSIC_HARMONIC_EXCITER, dec->pcm_dec.sample_rate, dec->pcm_dec.output_ch_num);
#endif


#if TCFG_EQ_ENABLE && TCFG_AUDIO_OUT_EQ_ENABLE
    dec->high_bass = high_bass_eq_open(dec->pcm_dec.sample_rate, dec->pcm_dec.output_ch_num);
    dec->hb_drc = high_bass_drc_open(dec->pcm_dec.sample_rate, dec->pcm_dec.output_ch_num);
    if (!global_bit_wide) {
#if !AUDIO_VBASS_32BIT_OUT_EN
#if defined(TCFG_DRC_ENABLE) && TCFG_DRC_ENABLE
        if (dec->hb_drc && dec->hb_drc->run32bit) {
            dec->hb_convert = convet_data_open(0, 512);
        }
#endif
#endif
    }
#endif

#if TCFG_EQ_ENABLE && TCFG_IIS_IN_MODE_EQ_ENABLE
    dec->eq = music_eq_open(dec->pcm_dec.sample_rate, dec->pcm_dec.output_ch_num);// eq
#if TCFG_DRC_ENABLE && TCFG_IIS_IN_MODE_DRC_ENABLE
    dec->drc = music_drc_open(dec->pcm_dec.sample_rate, dec->pcm_dec.output_ch_num);//drc
#if (defined(TCFG_DRC_SPILT_ENABLE) && TCFG_DRC_SPILT_ENABLE)
    dec->drc_fr = music_drc_fr_open(dec->pcm_dec.sample_rate, dec->pcm_dec.output_ch_num);//drc
#endif
#endif/*TCFG_IIS_IN_MODE_DRC_ENABLE*/
    if (!global_bit_wide) {
#if !AUDIO_VBASS_32BIT_OUT_EN
#if defined(TCFG_DRC_ENABLE) && TCFG_DRC_ENABLE
        if (dec->eq && dec->eq->out_32bit) {
            dec->convert = convet_data_open(0, 512);
        }
#endif
#endif
    }
#if defined(MUSIC_EXT_EQ_AFTER_DRC) && MUSIC_EXT_EQ_AFTER_DRC
    dec->ext_eq = music_ext_eq_open(dec->pcm_dec.sample_rate, dec->pcm_dec.output_ch_num);
#endif

#if defined(TCFG_DYNAMIC_EQ_ENABLE) && TCFG_DYNAMIC_EQ_ENABLE
    dec->eq2 = music_eq2_open(dec->pcm_dec.sample_rate, dec->pcm_dec.output_ch_num);// eq
#if defined(TCFG_DYNAMIC_EQ_PRO_ENABLE) && !TCFG_DYNAMIC_EQ_PRO_ENABLE
    dec->dy_eq = audio_dynamic_eq_ctrl_open(AEID_MUSIC_DYNAMIC_EQ, dec->pcm_dec.sample_rate, dec->pcm_dec.output_ch_num);//动态eq
#else
    dec->dy_eq_pro = audio_dynamic_eq_pro_open_api(AEID_MUSIC_DYNAMIC_EQ, dec->pcm_dec.sample_rate, dec->pcm_dec.output_ch_num);//动态eq
#endif
    /* dec->last_drc = music_last_drc_open(dec->pcm_dec.sample_rate, dec->pcm_dec.output_ch_num); */
    if (!global_bit_wide) {
#if !AUDIO_VBASS_32BIT_OUT_EN
        dec->convert2 = convet_data_open(0, 512);
#endif
    }
#endif/*TCFG_DYNAMIC_EQ_ENABLE*/
    u16 gain_name =  AEID_MUSIC_GAIN;
    u16 swap_name = AEID_MUSIC_CH_SWAP;
#if defined(TCFG_PHASER_GAIN_AND_CH_SWAP_ENABLE) && TCFG_PHASER_GAIN_AND_CH_SWAP_ENABLE
    dec->gain = audio_gain_open_demo(gain_name, dec->pcm_dec.output_ch_num);
#endif
#endif/*TCFG_IIS_IN_MODE_EQ_ENABLE*/



#if TCFG_EQ_DIVIDE_ENABLE&& !WIRELESS_SOUND_TRACK_2_P_X_ENABLE

#if TCFG_EQ_ENABLE && TCFG_IIS_IN_MODE_EQ_ENABLE
    dec->eq_rl_rr = music_eq_rl_rr_open(dec->pcm_dec.sample_rate, dec->pcm_dec.output_ch_num);// eq
#if TCFG_DRC_ENABLE && TCFG_IIS_IN_MODE_DRC_ENABLE
    dec->drc_rl_rr = music_drc_rl_rr_open(dec->pcm_dec.sample_rate, dec->pcm_dec.output_ch_num);//drc
#if (defined(TCFG_DRC_SPILT_ENABLE) && TCFG_DRC_SPILT_ENABLE)
    dec->drc_rr = music_drc_rr_open(dec->pcm_dec.sample_rate, dec->pcm_dec.output_ch_num);//drc
#endif
#endif
    if (!global_bit_wide) {
        if ((dec->eq_rl_rr && dec->eq_rl_rr->out_32bit) || AUDIO_VBASS_32BIT_OUT_EN) {
            dec->convert_rl_rr = convet_data_open(0, 512);
        }
    }
#endif

#if defined(TCFG_PHASER_GAIN_AND_CH_SWAP_ENABLE) && TCFG_PHASER_GAIN_AND_CH_SWAP_ENABLE
    dec->gain_rl_rr = audio_gain_open_demo(AEID_MUSIC_RL_GAIN, dec->pcm_dec.output_ch_num);
#endif
    if (dec->eq_rl_rr) {
        audio_vocal_tract_open(&dec->vocal_tract, AUDIO_SYNTHESIS_LEN);
        {
            struct vocal_track_parm par = {0};
#if SOUND_TRACK_1_P_1_CH_CONFIG
            par.ch_num = 2;
#else
            par.ch_num = 4;
#endif
            par.bit_width = global_bit_wide;
            par.len = AUDIO_SYNTHESIS_LEN;
            audio_vocal_tract_set_info(&dec->vocal_tract, par);

            u8 entry_cnt = 0;
            struct audio_stream_entry *entries[8] = {NULL};
            entries[entry_cnt++] = &dec->vocal_tract.entry;
            entries[entry_cnt++] = &dec->mix_ch.entry;
            dec->vocal_tract.stream = audio_stream_open(&dec->vocal_tract, audio_vocal_tract_stream_resume);
            if (!dec->vocal_tract.stream) {
                goto __err3;
            }
            audio_stream_add_list(dec->vocal_tract.stream, entries, entry_cnt);
        }
        audio_vocal_tract_synthesis_open(&dec->synthesis_ch_fl_fr, &dec->vocal_tract, FL_FR);
        audio_vocal_tract_synthesis_open(&dec->synthesis_ch_rl_rr, &dec->vocal_tract, RL_RR);
    } else {
        dec->ch_switch = channel_switch_open(AUDIO_CH_QUAD, AUDIO_SYNTHESIS_LEN / 2);
        channel_switch_set_bit_wide(dec->ch_switch, global_bit_wide);
    }
#if defined(MUSIC_EXT_EQ2_AFTER_DRC) && MUSIC_EXT_EQ2_AFTER_DRC
    dec->ext_eq2 = music_ext_eq2_open(dec->pcm_dec.sample_rate, dec->pcm_dec.output_ch_num);
#endif
#ifdef CONFIG_MIXER_CYCLIC
    audio_mixer_ch_set_aud_ch_out(&dec->mix_ch, 0, BIT(0));
    audio_mixer_ch_set_aud_ch_out(&dec->mix_ch, 1, BIT(1));
#if !SOUND_TRACK_1_P_1_CH_CONFIG
    audio_mixer_ch_set_aud_ch_out(&dec->mix_ch, 2, BIT(2));
    audio_mixer_ch_set_aud_ch_out(&dec->mix_ch, 3, BIT(3));
#endif
#endif
#endif

#if (SOUNDCARD_ENABLE)
    audio_mixer_ch_set_aud_ch_out(&dec->mix_ch, 0, BIT(0) | BIT(1));
    audio_mixer_ch_set_aud_ch_out(&dec->mix_ch, 1, BIT(0) | BIT(1));
#endif

#if SOUND_TRACK_1_P_1_CH_CONFIG && (EFFECT_PROCESS_POITION_CONFIG == EFFECT_BEFORE_STEREO_MIX)
    dec->fl_fr_ch_2to1 = channel_switch_open(AUDIO_CH_DIFF, AUDIO_SYNTHESIS_LEN);
    channel_switch_set_bit_wide(dec->fl_fr_ch_2to1, global_bit_wide);
    dec->rl_rr_ch_2to1 = channel_switch_open(AUDIO_CH_DIFF, AUDIO_SYNTHESIS_LEN);
    channel_switch_set_bit_wide(dec->rl_rr_ch_2to1, global_bit_wide);

#endif

    // 数据流串联
    struct audio_stream_entry *entries[32] = {NULL};
    u8 entry_cnt = 0;
    u8 rl_rr_entry_start = 0;
    entries[entry_cnt++] = &dec->pcm_dec.decoder.entry;

    if (dec->convert_16_to_32) {
        entries[entry_cnt++] = &dec->convert_16_to_32->entry;
    }

#if SYS_DIGVOL_GROUP_EN
    void *dvol_entry = sys_digvol_group_ch_open("music_iis_in", -1, NULL);
    entries[entry_cnt++] = dvol_entry;
#endif // SYS_DIGVOL_GROUP_EN

#if SOUND_TRACK_1_P_1_CH_CONFIG
#if defined(TCFG_DYNAMIC_EQ_ENABLE) && TCFG_DYNAMIC_EQ_ENABLE
    //1.1声道，EQ2作为分流前的preEQ
    if (dec->eq2) {
        entries[entry_cnt++] = &dec->eq2->entry;
    }
#endif
#endif

#if !(SOUND_TRACK_1_P_1_CH_CONFIG && (EFFECT_PROCESS_POITION_CONFIG == EFFECT_BEFORE_STEREO_MIX))
#if AUDIO_VBASS_CONFIG
    if (dec->vbass_prev_gain) {
        entries[entry_cnt++] = &dec->vbass_prev_gain->entry;
    }
    if (dec->vbass) {
        entries[entry_cnt++] = &dec->vbass->entry;
    }
    if (dec->ns_gate) {
        entries[entry_cnt++] = &dec->ns_gate->entry;
    }
#endif
#endif

#if defined(TCFG_MUSIC_HARMONIC_EXCITER_ENABLE) &&TCFG_MUSIC_HARMONIC_EXCITER_ENABLE
    if (dec->harmonic_exciter) {
        entries[entry_cnt++] = &dec->harmonic_exciter->entry;
    }
#endif
#if !(SOUND_TRACK_1_P_1_CH_CONFIG && (EFFECT_PROCESS_POITION_CONFIG == EFFECT_BEFORE_STEREO_MIX))
#if defined(TCFG_DYNAMIC_EQ_ENABLE) && TCFG_DYNAMIC_EQ_ENABLE
    if (dec->dy_eq && dec->dy_eq->dy_eq) {
        entries[entry_cnt++] = &dec->dy_eq->dy_eq->entry;
    } else if (dec->dy_eq_pro && dec->dy_eq_pro) {
        entries[entry_cnt++] = &dec->dy_eq_pro->entry;
    }
#endif
#endif


#if TCFG_EQ_ENABLE && TCFG_AUDIO_OUT_EQ_ENABLE
    if (dec->high_bass) { //高低音
        entries[entry_cnt++] = &dec->high_bass->entry;
    }
    if (dec->hb_drc) { //高低音后drc
        entries[entry_cnt++] = &dec->hb_drc->entry;
#if defined(TCFG_DRC_ENABLE) && TCFG_DRC_ENABLE
        if (dec->hb_convert) {
            entries[entry_cnt++] = &dec->hb_convert->entry;
        }
#endif
    }
#endif

    rl_rr_entry_start = entry_cnt - 1;//记录eq的上一个节点
#if defined(TCFG_PHASER_GAIN_AND_CH_SWAP_ENABLE) && TCFG_PHASER_GAIN_AND_CH_SWAP_ENABLE
    if (dec->gain) {
        entries[entry_cnt++] = &dec->gain->entry;
    }
#endif
#if AUDIO_SURROUND_CONFIG
    if (dec->surround) {
        entries[entry_cnt++] = &dec->surround->entry;
    }
#endif
#if TCFG_EQ_ENABLE && TCFG_IIS_IN_MODE_EQ_ENABLE
    if (dec->eq) {
        entries[entry_cnt++] = &dec->eq->entry;
        if (dec->drc) {
            entries[entry_cnt++] = &dec->drc->entry;
        }
#if (defined(TCFG_DRC_SPILT_ENABLE) && TCFG_DRC_SPILT_ENABLE)
        if (dec->drc_fr) {
            entries[entry_cnt++] = &dec->drc_fr->entry;
        }
#endif
#if defined(TCFG_DRC_ENABLE) && TCFG_DRC_ENABLE
        if (dec->convert) {
            entries[entry_cnt++] = &dec->convert->entry;
        }
#endif
#if defined(MUSIC_EXT_EQ_AFTER_DRC) && MUSIC_EXT_EQ_AFTER_DRC
        if (dec->ext_eq) {
            entries[entry_cnt++] = &dec->ext_eq->entry;
        }
#endif
#if defined(TCFG_DYNAMIC_EQ_ENABLE) && TCFG_DYNAMIC_EQ_ENABLE
#if !SOUND_TRACK_1_P_1_CH_CONFIG
        if (dec->eq2) {
            entries[entry_cnt++] = &dec->eq2->entry;
        }
#endif
        if (dec->convert2) {
            entries[entry_cnt++] = &dec->convert2->entry;
        }
#endif
    }
#endif

    if (dec->convert_32_to_16) {
        entries[entry_cnt++] = &dec->convert_32_to_16->entry;
    }



#if TCFG_EQ_DIVIDE_ENABLE&& !WIRELESS_SOUND_TRACK_2_P_X_ENABLE
#if SOUND_TRACK_1_P_1_CH_CONFIG && (EFFECT_PROCESS_POITION_CONFIG == EFFECT_BEFORE_STEREO_MIX)
    if (dec->fl_fr_ch_2to1) {
        entries[entry_cnt++] = &dec->fl_fr_ch_2to1->entry;
    }
#endif

    if (dec->eq_rl_rr) {
        entries[entry_cnt++] = &dec->synthesis_ch_fl_fr.entry;//四声道eq独立时，该节点后不接节点
    } else {
        if (dec->ch_switch) {
            entries[entry_cnt++] = &dec->ch_switch->entry;
        }
        entries[entry_cnt++] = &dec->mix_ch.entry;
    }
#else

#if (SOUNDCARD_ENABLE)
    if (iis_mix_fifo_ch) {
        entries[entry_cnt++] = mix_fifo_ch_get_entry(iis_mix_fifo_ch);
    }
#endif

#if (RECORDER_MIX_EN)
    if (dec->rec_mix_ch) {
        entries[entry_cnt++] = rec_mix_fifo_ch_get_entry(dec->rec_mix_ch);
    }
#endif

    entries[entry_cnt++] = &dec->mix_ch.entry;
#endif

    // 创建数据流，把所有节点连接起来
    dec->stream = audio_stream_open(dec, iis_in_dec_out_stream_resume);
    if (!dec->stream) {
        goto __err3;
    }
    audio_stream_add_list(dec->stream, entries, entry_cnt);
#if TCFG_EQ_DIVIDE_ENABLE&& !WIRELESS_SOUND_TRACK_2_P_X_ENABLE
    struct audio_stream_entry *rl_rr_entries[20] = {NULL};
    entry_cnt = 0;
    if (dec->eq_rl_rr) { //接在eq_drc的上一个节点
        rl_rr_entries[entry_cnt++] = entries[rl_rr_entry_start];
#if defined(TCFG_PHASER_GAIN_AND_CH_SWAP_ENABLE) && TCFG_PHASER_GAIN_AND_CH_SWAP_ENABLE
        if (dec->gain_rl_rr) {
            rl_rr_entries[entry_cnt++] = &dec->gain_rl_rr->entry;
        }
#endif
#if AUDIO_VBASS_CONFIG
        if (dec->vbass_prev_gain) {
            rl_rr_entries[entry_cnt++] = &dec->vbass_prev_gain->entry;
        }
        if (dec->vbass) {
            rl_rr_entries[entry_cnt++] = &dec->vbass->entry;
        }
        if (dec->ns_gate) {
            rl_rr_entries[entry_cnt++] = &dec->ns_gate->entry;
        }
#endif
#if defined(TCFG_DYNAMIC_EQ_ENABLE) && TCFG_DYNAMIC_EQ_ENABLE
        if (dec->dy_eq && dec->dy_eq->dy_eq) {
            rl_rr_entries[entry_cnt++] = &dec->dy_eq->dy_eq->entry;
        } else if (dec->dy_eq_pro && dec->dy_eq_pro) {
            rl_rr_entries[entry_cnt++] = &dec->dy_eq_pro->entry;
        }
#endif
        rl_rr_entries[entry_cnt++] = &dec->eq_rl_rr->entry;
        if (dec->drc_rl_rr) {
            rl_rr_entries[entry_cnt++] = &dec->drc_rl_rr->entry;
        }
#if (defined(TCFG_DRC_SPILT_ENABLE) && TCFG_DRC_SPILT_ENABLE)
        if (dec->drc_rr) {
            rl_rr_entries[entry_cnt++] = &dec->drc_rr->entry;
        }
#endif
        if (dec->convert_rl_rr) {
            rl_rr_entries[entry_cnt++] = &dec->convert_rl_rr->entry;
        }
#if defined(MUSIC_EXT_EQ2_AFTER_DRC) && MUSIC_EXT_EQ2_AFTER_DR
        if (dec->ext_eq2) {
            rl_rr_entries[entry_cnt++] = &dec->ext_eq2->entry;
        }
#endif

#if SOUND_TRACK_1_P_1_CH_CONFIG && (EFFECT_PROCESS_POITION_CONFIG == EFFECT_BEFORE_STEREO_MIX)
        if (dec->rl_rr_ch_2to1) {
            rl_rr_entries[entry_cnt++] = &dec->rl_rr_ch_2to1->entry;
        }
#endif

        rl_rr_entries[entry_cnt++] = &dec->synthesis_ch_rl_rr.entry;//必须是最后一个
        for (int i = 0; i < (entry_cnt - 1); i++) {
            audio_stream_add_entry(rl_rr_entries[i], rl_rr_entries[i + 1]);
        }
    }
#endif


    // 设置音频输出音量
    audio_output_set_start_volume(APP_AUDIO_STATE_MUSIC);

    // 开始解码
    dec->start = 1;
    err = audio_decoder_start(&dec->pcm_dec.decoder);
    if (err) {
        goto __err3;
    }
    clock_set_cur();
    return 0;
__err3:
    dec->start = 0;

#if AUDIO_SURROUND_CONFIG
    surround_close_demo(dec->surround);
#endif


#if AUDIO_VBASS_CONFIG
    audio_gain_close_demo(dec->vbass_prev_gain);
    audio_noisegate_close_demo(dec->ns_gate);
    audio_vbass_close_demo(dec->vbass);
#endif

    convet_data_close(dec->convert_16_to_32);
    convet_data_close(dec->convert_32_to_16);
#if defined(TCFG_MUSIC_HARMONIC_EXCITER_ENABLE) &&TCFG_MUSIC_HARMONIC_EXCITER_ENABLE
    audio_harmonic_exciter_close_api(dec->harmonic_exciter);
#endif


#if TCFG_EQ_ENABLE && TCFG_AUDIO_OUT_EQ_ENABLE
    high_bass_eq_close(dec->high_bass);
    high_bass_drc_close(dec->hb_drc);
#if defined(TCFG_DRC_ENABLE) && TCFG_DRC_ENABLE
    convet_data_close(dec->hb_convert);
#endif
#endif

#if TCFG_EQ_ENABLE && TCFG_IIS_IN_MODE_EQ_ENABLE
    music_eq_close(dec->eq);
#if TCFG_DRC_ENABLE && TCFG_IIS_IN_MODE_DRC_ENABLE
    music_drc_close(dec->drc);
#if (defined(TCFG_DRC_SPILT_ENABLE) && TCFG_DRC_SPILT_ENABLE)
    music_drc_fr_close(dec->drc_fr);
#endif
#endif
#if defined(TCFG_DRC_ENABLE) && TCFG_DRC_ENABLE
    convet_data_close(dec->convert);
#endif

#if defined(MUSIC_EXT_EQ_AFTER_DRC) && MUSIC_EXT_EQ_AFTER_DRC
    music_ext_eq_close(dec->ext_eq);
#endif
#if defined(TCFG_DYNAMIC_EQ_ENABLE) && TCFG_DYNAMIC_EQ_ENABLE
    music_eq2_close(dec->eq2);
#if defined(TCFG_DYNAMIC_EQ_PRO_ENABLE) && !TCFG_DYNAMIC_EQ_PRO_ENABLE
    audio_dynamic_eq_ctrl_close(dec->dy_eq);
#else
    audio_dynamic_eq_pro_close_api(dec->dy_eq_pro);
#endif
    music_last_drc_close(dec->last_drc);
    convet_data_close(dec->convert2);
#endif
#if defined(TCFG_PHASER_GAIN_AND_CH_SWAP_ENABLE) && TCFG_PHASER_GAIN_AND_CH_SWAP_ENABLE
    audio_gain_close_demo(dec->gain);
#endif
#endif

#if TCFG_EQ_DIVIDE_ENABLE&& !WIRELESS_SOUND_TRACK_2_P_X_ENABLE
    music_eq_rl_rr_close(dec->eq_rl_rr);
    music_drc_rl_rr_close(dec->drc_rl_rr);
#if (defined(TCFG_DRC_SPILT_ENABLE) && TCFG_DRC_SPILT_ENABLE)
    music_drc_rr_close(dec->drc_rr);
#endif
    convet_data_close(dec->convert_rl_rr);
#if defined(TCFG_PHASER_GAIN_AND_CH_SWAP_ENABLE) && TCFG_PHASER_GAIN_AND_CH_SWAP_ENABLE
    audio_gain_close_demo(dec->gain_rl_rr);
#endif

    audio_vocal_tract_synthesis_close(&dec->synthesis_ch_fl_fr);
    audio_vocal_tract_synthesis_close(&dec->synthesis_ch_rl_rr);
    audio_vocal_tract_close(&dec->vocal_tract);
    channel_switch_close(&dec->ch_switch);
#if defined(MUSIC_EXT_EQ2_AFTER_DRC) && MUSIC_EXT_EQ2_AFTER_DRC
    music_ext_eq2_close(dec->ext_eq2);
#endif
#endif
#if SOUND_TRACK_1_P_1_CH_CONFIG && (EFFECT_PROCESS_POITION_CONFIG == EFFECT_BEFORE_STEREO_MIX)
    channel_switch_close(&dec->fl_fr_ch_2to1);
    channel_switch_close(&dec->rl_rr_ch_2to1);
#endif

    if (dec->iis_in) {
        iis_in_close(dec->iis_in);
        dec->iis_in = NULL;
    }
    audio_mixer_ch_close(&dec->mix_ch);
#if (RECORDER_MIX_EN)
    if (dec->rec_mix_ch != NULL) {
        void *entry = rec_mix_fifo_ch_get_entry(dec->rec_mix_ch);
        if (entry != NULL) {
            audio_stream_del_entry(entry);
        }
    }
    rec_mix_fifo_ch_close(dec->rec_mix_ch);
    dec->rec_mix_ch = NULL;
#endif

#if SYS_DIGVOL_GROUP_EN
    sys_digvol_group_ch_close("music_iis_in");
#endif // SYS_DIGVOL_GROUP_EN

#if (SOUNDCARD_ENABLE)
    if (iis_mix_fifo_ch != NULL) {
        void *entry = mix_fifo_ch_get_entry(iis_mix_fifo_ch);
        if (entry != NULL) {
            audio_stream_del_entry(entry);
        }
    }
    mix_fifo_ch_close(iis_mix_fifo_ch);
    iis_mix_fifo_ch = NULL;
#endif

    if (dec->stream) {
        audio_stream_close(dec->stream);
        dec->stream = NULL;
    }

    pcm_decoder_close(&dec->pcm_dec);
__err1:
    iis_in_dec_relaese();
    return err;
}

/*----------------------------------------------------------------------------*/
/**@brief    fm解码关闭
   @param
   @return
   @note
*/
/*----------------------------------------------------------------------------*/
static void __iis_in_dec_close(void)
{
    if (iis_in_dec && iis_in_dec->start) {
        iis_in_dec->start = 0;

        pcm_decoder_close(&iis_in_dec->pcm_dec);
        iis_in_close(iis_in_dec->iis_in);
        iis_in_dec->iis_in = NULL;

#if AUDIO_SURROUND_CONFIG
        surround_close_demo(iis_in_dec->surround);
#endif



#if AUDIO_VBASS_CONFIG
        audio_gain_close_demo(iis_in_dec->vbass_prev_gain);
        audio_noisegate_close_demo(iis_in_dec->ns_gate);
        audio_vbass_close_demo(iis_in_dec->vbass);
#endif
        convet_data_close(iis_in_dec->convert_16_to_32);
        convet_data_close(iis_in_dec->convert_32_to_16);
#if defined(TCFG_MUSIC_HARMONIC_EXCITER_ENABLE) &&TCFG_MUSIC_HARMONIC_EXCITER_ENABLE
        audio_harmonic_exciter_close_api(iis_in_dec->harmonic_exciter);
#endif

#if TCFG_EQ_ENABLE && TCFG_AUDIO_OUT_EQ_ENABLE
        high_bass_eq_close(iis_in_dec->high_bass);
        high_bass_drc_close(iis_in_dec->hb_drc);
#if defined(TCFG_DRC_ENABLE) && TCFG_DRC_ENABLE
        convet_data_close(iis_in_dec->hb_convert);
#endif
#endif

#if TCFG_EQ_ENABLE && TCFG_IIS_IN_MODE_EQ_ENABLE
        music_eq_close(iis_in_dec->eq);
#if TCFG_DRC_ENABLE && TCFG_IIS_IN_MODE_DRC_ENABLE
        music_drc_close(iis_in_dec->drc);
#if (defined(TCFG_DRC_SPILT_ENABLE) && TCFG_DRC_SPILT_ENABLE)
        music_drc_fr_close(iis_in_dec->drc_fr);
#endif

#endif
#if defined(TCFG_DRC_ENABLE) && TCFG_DRC_ENABLE
        convet_data_close(iis_in_dec->convert);
#endif
#if defined(MUSIC_EXT_EQ_AFTER_DRC) && MUSIC_EXT_EQ_AFTER_DRC
        music_ext_eq_close(iis_in_dec->ext_eq);
#endif

#if defined(TCFG_DYNAMIC_EQ_ENABLE) && TCFG_DYNAMIC_EQ_ENABLE
        music_eq2_close(iis_in_dec->eq2);
#if defined(TCFG_DYNAMIC_EQ_PRO_ENABLE) && !TCFG_DYNAMIC_EQ_PRO_ENABLE
        audio_dynamic_eq_ctrl_close(iis_in_dec->dy_eq);
#else
        audio_dynamic_eq_pro_close_api(iis_in_dec->dy_eq_pro);
#endif
        music_last_drc_close(iis_in_dec->last_drc);
        convet_data_close(iis_in_dec->convert2);
#endif
#if defined(TCFG_PHASER_GAIN_AND_CH_SWAP_ENABLE) && TCFG_PHASER_GAIN_AND_CH_SWAP_ENABLE
        audio_gain_close_demo(iis_in_dec->gain);
#endif
#endif

#if TCFG_EQ_DIVIDE_ENABLE&& !WIRELESS_SOUND_TRACK_2_P_X_ENABLE
        music_eq_rl_rr_close(iis_in_dec->eq_rl_rr);
        music_drc_rl_rr_close(iis_in_dec->drc_rl_rr);
#if (defined(TCFG_DRC_SPILT_ENABLE) && TCFG_DRC_SPILT_ENABLE)
        music_drc_rr_close(iis_in_dec->drc_rr);
#endif
        convet_data_close(iis_in_dec->convert_rl_rr);
#if defined(TCFG_PHASER_GAIN_AND_CH_SWAP_ENABLE) && TCFG_PHASER_GAIN_AND_CH_SWAP_ENABLE
        audio_gain_close_demo(iis_in_dec->gain_rl_rr);
#endif

        audio_vocal_tract_synthesis_close(&iis_in_dec->synthesis_ch_fl_fr);
        audio_vocal_tract_synthesis_close(&iis_in_dec->synthesis_ch_rl_rr);
        audio_vocal_tract_close(&iis_in_dec->vocal_tract);
        channel_switch_close(&iis_in_dec->ch_switch);
#if defined(MUSIC_EXT_EQ2_AFTER_DRC) && MUSIC_EXT_EQ2_AFTER_DRC
        music_ext_eq2_close(iis_in_dec->ext_eq2);
#endif
#endif
#if SOUND_TRACK_1_P_1_CH_CONFIG && (EFFECT_PROCESS_POITION_CONFIG == EFFECT_BEFORE_STEREO_MIX)
        channel_switch_close(&iis_in_dec->fl_fr_ch_2to1);
        channel_switch_close(&iis_in_dec->rl_rr_ch_2to1);
#endif

        audio_mixer_ch_close(&iis_in_dec->mix_ch);
#if (RECORDER_MIX_EN)
        if (dec->rec_mix_ch != NULL) {
            void *entry = rec_mix_fifo_ch_get_entry(dec->rec_mix_ch);
            if (entry != NULL) {
                audio_stream_del_entry(entry);
            }
        }
        rec_mix_fifo_ch_close(dec->rec_mix_ch);
        dec->rec_mix_ch = NULL;
#endif
#if SYS_DIGVOL_GROUP_EN
        sys_digvol_group_ch_close("music_iis_in");
#endif // SYS_DIGVOL_GROUP_EN

#if (SOUNDCARD_ENABLE)
        if (iis_mix_fifo_ch != NULL) {
            void *entry = mix_fifo_ch_get_entry(iis_mix_fifo_ch);
            if (entry != NULL) {
                audio_stream_del_entry(entry);
            }
        }
        mix_fifo_ch_close(iis_mix_fifo_ch);
        iis_mix_fifo_ch = NULL;
#endif

        // 先关闭各个节点，最后才close数据流
        if (iis_in_dec->stream) {
            audio_stream_close(iis_in_dec->stream);
            iis_in_dec->stream = NULL;
        }

    }
}

/*----------------------------------------------------------------------------*/
/**@brief    iis_in解码资源等待
   @param    *wait: 句柄
   @param    event: 事件
   @return   0：成功
   @note     用于多解码打断处理
*/
/*----------------------------------------------------------------------------*/
static int iis_in_wait_res_handler(struct audio_res_wait *wait, int event)
{
    int err = 0;
    log_i("iis_in_wait_res_handler, event:%d\n", event);
    if (event == AUDIO_RES_GET) {
        // 启动解码
        err = iis_in_dec_start();
    } else if (event == AUDIO_RES_PUT) {
        // 被打断
        __iis_in_dec_close();
    }

    return err;
}

/*----------------------------------------------------------------------------*/
/**@brief    打开iis_in解码
   @param    sample_rate: 采样率
   @return   0：成功
   @return   非0：失败
   @note
*/
/*----------------------------------------------------------------------------*/
int iis_in_dec_open(u32 sample_rate)
{
    int err;
    struct iis_in_dec_hdl *dec;
    dec = zalloc(sizeof(*dec));
    if (!dec) {
        return -ENOMEM;
    }
    iis_in_dec = dec;
    dec->id = rand32();

    dec->pcm_dec.output_ch_num = audio_output_channel_num();
    dec->pcm_dec.output_ch_type = audio_output_channel_type();
    dec->pcm_dec.sample_rate = TCFG_IIS_SR;
    dec->wait.priority = 2;
    dec->wait.preemption = 0;
    dec->wait.snatch_same_prio = 1;
#if (SOUNDCARD_ENABLE||(WIRELESS_2T1_DUPLEX_ROLE == 1))
    dec->wait.protect = 1;
#endif
    dec->wait.handler = iis_in_wait_res_handler;
    clock_add(AUDIO_CODING_PCM);
    clock_add(DEC_IIS_CLK);
    clock_set_cur();

    err = audio_decoder_task_add_wait(&decode_task, &dec->wait);
    return err;
}

/*----------------------------------------------------------------------------*/
/**@brief    关闭iis_in解码
   @param
   @return
   @note
*/
/*----------------------------------------------------------------------------*/
void iis_in_dec_close(void)
{
    if (!iis_in_dec) {
        return;
    }

    __iis_in_dec_close();
    iis_in_dec_relaese();
    clock_remove(DEC_IIS_CLK);
    clock_set_cur();
    log_i("iis_in dec close \n\n ");
}

/*----------------------------------------------------------------------------*/
/**@brief    iis_in解码重新开始
   @param    id: 文件解码id
   @return   0：成功
   @return   非0：失败
   @note
*/
/*----------------------------------------------------------------------------*/
int iis_in_dec_restart(int id)
{
    if ((!iis_in_dec) || (id != iis_in_dec->id)) {
        return -1;
    }
    u32 sample_rate = iis_in_dec->pcm_dec.sample_rate;
    iis_in_dec_close();
    int err = iis_in_dec_open(sample_rate);
    return err;
}

/*----------------------------------------------------------------------------*/
/**@brief    推送iis_in解码重新开始命令
   @param
   @return   true：成功
   @return   false：失败
   @note
*/
/*----------------------------------------------------------------------------*/
int iis_in_dec_push_restart(void)
{
    if (!iis_in_dec) {
        return false;
    }
    int argv[3];
    argv[0] = (int)iis_in_dec_restart;
    argv[1] = 1;
    argv[2] = (int)iis_in_dec->id;
    os_taskq_post_type(os_current_task(), Q_CALLBACK, ARRAY_SIZE(argv), argv);
    return true;
}
#endif

