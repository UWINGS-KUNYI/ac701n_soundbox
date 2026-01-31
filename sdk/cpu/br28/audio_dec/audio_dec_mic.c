/*
 ****************************************************************
 *File : audio_dc_mic.c
 *Note :
 *
 ****************************************************************
 */


#include "asm/includes.h"
#include "media/includes.h"
#include "media/pcm_decoder.h"
#include "system/includes.h"
#include "effectrs_sync.h"
#include "app_config.h"
#include "audio_config.h"
#include "audio_dec.h"
#include "app_config.h"
#include "app_main.h"
#include "audio_enc/audio_enc.h"
#include "clock_cfg.h"
#include "dev_manager.h"
#include "media/convert_data.h"
#include "audio_effect/audio_dynamic_eq_demo.h"
#include "media/effects_adj.h"
#include "audio_enc_recoder.h"
#include "audio_effect/audio_eff_default_parm.h"
#include "wireless_mic_effect.h"
#include "audio_dec_mic.h"
#include "board_config.h"
//////////////////////////////////////////////////////////////////////////////

#if TCFG_MIC_EFFECT_ENABLE
#if (RECORDER_MIX_EN)
#define LADC_MIC_IRQ_POINTS     160
#else
#if (TCFG_MIC_EFFECT_SEL == MIC_EFFECT_REVERB)
#define LADC_MIC_IRQ_POINTS    REVERB_LADC_IRQ_POINTS
#else
#define LADC_MIC_IRQ_POINTS     ((MIC_EFFECT_SAMPLERATE/1000)*4)
#endif
#endif/*RECORDER_MIX_EN*/
#elif (WIRELESS_MIC_EFFECT_ENABLE && MIC_EFFECT_LLNS) /*无线mic 低延时*/
#define LADC_MIC_IRQ_POINTS     ((JLA_CODING_SAMPLERATE/1000)*5)
#else
#define LADC_MIC_IRQ_POINTS     256
#endif



#if (TCFG_MIC_EFFECT_ENABLE && WIRELESS_MIC_EFFECT_ENABLE)
struct local_mic_effect_hdl {
    struct audio_stream_entry entry;
    struct live_mic_effect *effect;
    s16 used_len;
};
#endif

struct mic_dec_hdl {
    struct audio_stream *stream;	// 音频流
    struct pcm_decoder pcm_dec;		// pcm解码句柄
    struct audio_res_wait wait;		// 资源等待句柄
    struct audio_mixer_ch mix_ch;	// 叠加句柄
#if (RECORDER_MIX_EN)
    void *rec_mix_ch;	// 叠加句柄
#endif/*RECORDER_MIX_EN*/
#if (TCFG_MIC_EFFECT_ENABLE && WIRELESS_MIC_EFFECT_ENABLE)
    struct local_mic_effect_hdl mic_effect;
#endif
    u8 nch;
    int sample_rate;

    struct convert_data *convert_16_to_32;

    u32 id;				// 唯一标识符，随机值
    u32 start : 1;		// 正在解码
    u32 source : 8;		// mic音频源
    u8  gain;
    void *mic;		// 底层驱动句柄

#if WIRELESS_MIC_EFFECT_ASYNC_EN

    s16 *pcm_buf;
    cbuffer_t cbuf;
    u8 wait_resume;
#endif
};

spinlock_t mic_dec_lock;

//////////////////////////////////////////////////////////////////////////////

struct mic_dec_hdl *mic_dec = NULL;	// linein解码句柄

//////////////////////////////////////////////////////////////////////////////
int linein_sample_size(void *hdl);
int linein_sample_total(void *hdl);


//////////////////////////////////////////////////////////////////////////////

/*----------------------------------------------------------------------------*/
/**@brief    linein解码释放
   @param
   @return
   @note
*/
/*----------------------------------------------------------------------------*/
void mic_dec_relaese()
{
    if (mic_dec) {
        audio_decoder_task_del_wait(&decode_task, &mic_dec->wait);
        clock_remove(AUDIO_CODING_PCM);
        spin_lock(&mic_dec_lock);
        free(mic_dec);
        mic_dec = NULL;
        spin_unlock(&mic_dec_lock);
    }
}

/*----------------------------------------------------------------------------*/
/**@brief    linein解码事件处理
   @param    *decoder: 解码器句柄
   @param    argc: 参数个数
   @param    *argv: 参数
   @return
   @note
*/
/*----------------------------------------------------------------------------*/
static void mic_dec_event_handler(struct audio_decoder *decoder, int argc, int *argv)
{
    switch (argv[0]) {
    case AUDIO_DEC_EVENT_END:
        if (!mic_dec) {
            log_i("mic_dec handle err ");
            break;
        }

        if (mic_dec->id != argv[1]) {
            log_w("mic_dec id err : 0x%x, 0x%x \n", mic_dec->id, argv[1]);
            break;
        }

        mic_dec_close();
        //audio_decoder_resume_all(&decode_task);
        break;
    }
}


/*----------------------------------------------------------------------------*/
/**@brief    linein解码数据输出
   @param    *entry: 音频流句柄
   @param    *in: 输入信息
   @param    *out: 输出信息
   @return   输出长度
   @note     *out未使用
*/
/*----------------------------------------------------------------------------*/
static int mic_dec_data_handler(struct audio_stream_entry *entry,
                                struct audio_data_frame *in,
                                struct audio_data_frame *out)
{
    struct audio_decoder *decoder = container_of(entry, struct audio_decoder, entry);
    struct pcm_decoder *pcm_dec = container_of(decoder, struct pcm_decoder, decoder);
    struct mic_dec_hdl *dec = container_of(pcm_dec, struct mic_dec_hdl, pcm_dec);
    if (!dec->start) {
        return 0;
    }
    audio_stream_run(&decoder->entry, in);
    return decoder->process_len;
}

/*----------------------------------------------------------------------------*/
/**@brief    linein解码数据流激活
   @param    *p: 私有句柄
   @return
   @note
*/
/*----------------------------------------------------------------------------*/
static void mic_dec_out_stream_resume(void *p)
{
    struct mic_dec_hdl *dec = p;
    audio_decoder_resume(&dec->pcm_dec.decoder);
}

/*----------------------------------------------------------------------------*/
/**@brief    linein解码激活
   @param
   @return
   @note
*/
/*----------------------------------------------------------------------------*/
static void mic_dec_resume(void)
{
    if (mic_dec) {
        /* printf("mic_dec_resume"); */
        audio_decoder_resume(&mic_dec->pcm_dec.decoder);
    }
}

#if WIRELESS_MIC_EFFECT_ASYNC_EN
static int mic_dec_write(void *hdl, void *data, int len)
{
    struct mic_dec_hdl *mic = (struct mic_dec_hdl *)hdl;
    spin_lock(&mic_dec_lock);
    if (!mic) {
        spin_unlock(&mic_dec_lock);
        return 0;
    }
    int wlen = 0;
    if (mic->start) {
        wlen = cbuf_write(&mic->cbuf, data, len * mic->pcm_dec.ch_num);
        if (wlen != len * mic->pcm_dec.ch_num) {
            putchar('W');
            //r_printf(">> wlen = %d, cbuf_len = %d", wlen, cbuf_get_data_size(&linein->cbuf));
            cbuf_clear(&mic->cbuf);
            wlen = cbuf_write(&mic->cbuf, data, len * mic->pcm_dec.ch_num);

            if (mic->wait_resume) {
                mic->wait_resume = 0;
            }
            mic_dec_resume();
            spin_unlock(&mic_dec_lock);
            return wlen;
        }

    }
    if (mic->wait_resume) {
        mic->wait_resume = 0;
        mic_dec_resume();
    }
    spin_unlock(&mic_dec_lock);
    return wlen;
}


static int mic_dec_read(void *hdl, void *data, int len)
{
    struct mic_dec_hdl *mic = (struct mic_dec_hdl *)hdl;
    spin_lock(&mic_dec_lock);
    if (!mic) {
        spin_unlock(&mic_dec_lock);
        return 0;
    }
    int rlen = 0;
    if (mic->start) {
        u32 tlen = LADC_MIC_IRQ_POINTS  * 2;
        if (tlen > len) {
            tlen = len;
        }
        rlen = cbuf_read(&mic->cbuf, data, tlen);
        if (rlen < tlen) {
            mic->wait_resume = 1;
        }
    }
    spin_unlock(&mic_dec_lock);
    return rlen;
}
#endif



#if (TCFG_MIC_EFFECT_ENABLE && WIRELESS_MIC_EFFECT_ENABLE)
static int local_mic_effect_output_handler(void *priv, void *data, int len)
{
    struct local_mic_effect_hdl *mic_effect = (struct local_mic_effect_hdl *)priv;

    struct audio_data_frame frame = {
        .data = data,
        .data_len = len,
        .channel = mic_effect->effect->param.ch_num,
        .sample_rate = mic_effect->effect->param.sample_rate,
    };

    audio_stream_run(&mic_effect->entry, &frame);

    return mic_effect->used_len;

}


static void local_mic_effect_data_process_len(struct audio_stream_entry *entry, int len)
{
    struct local_mic_effect_hdl *mic_effect = container_of(entry, struct local_mic_effect_hdl, entry);

    mic_effect->used_len = len;
}

static int local_mic_effect_data_handler(struct audio_stream_entry *entry,  struct audio_data_frame *in, struct audio_data_frame *out)
{
    struct local_mic_effect_hdl *mic_effect = container_of(entry, struct local_mic_effect_hdl, entry);

    if (in->data_len) {
        out->no_subsequent = 1;  //数据流节点自己调用audio_stream_run,需要置1，不跑数据流节点的递归调用;
    }
    // 设置运行句柄
    if (mic_effect->effect) {
        int wlen = live_mic_effect_input(mic_effect->effect, in->data, in->data_len, 0);
        if (wlen < in->data_len) {
            /* printf("local mic_effect write err \n"); */
        }
        return wlen;
    }

    return in->data_len;
}

static void local_mic_effect_close(struct mic_dec_hdl *mic)
{
    if (mic->mic_effect.effect) {
        live_mic_effect_close(mic->mic_effect.effect);
        mic->mic_effect.effect = NULL;
    }
}

static void local_mic_effect_open(struct mic_dec_hdl *mic)
{

    struct live_mic_effect_param  param = {
        .effect_config = LIVE_MIC_EFFECT_CONFIG,
        .sample_rate = mic->sample_rate,
        .ch_num = mic->pcm_dec.ch_num,


#if WIRELESS_MIC_EFFECT_ASYNC_EN
        .output_priv = mic_dec,
        .output = mic_dec_write,
#else
        .output_priv = &mic->mic_effect,
        .output = local_mic_effect_output_handler,
#endif
    };

    mic->mic_effect.effect = live_mic_effect_init(&param);

#if !WIRELESS_MIC_EFFECT_ASYNC_EN
    mic->mic_effect.entry.data_handler = local_mic_effect_data_handler;
    mic->mic_effect.entry.data_process_len = local_mic_effect_data_process_len;
#endif
}
#endif
/*----------------------------------------------------------------------------*/
/**@brief    linein解码开始
   @param
   @return   0：成功
   @return   非0：失败
   @note
*/
/*----------------------------------------------------------------------------*/
int mic_dec_start()
{
    int err;
    struct mic_dec_hdl *dec = mic_dec;
    struct audio_mixer *p_mixer = &mixer;

    if (!mic_dec) {
        return -EINVAL;
    }

#if (TCFG_AUDIO_ADC_BIT_MODE == ADC_BIT_WIDTH_24)&& (!TCFG_ADC_BIT_24_TO_16)
    dec->pcm_dec.bit_wide = global_bit_wide;
#endif
    err = pcm_decoder_open(&dec->pcm_dec, &decode_task);
    if (err) {
        goto __err1;
    }


#if (TCFG_MIC_EFFECT_ENABLE && WIRELESS_MIC_EFFECT_ENABLE)
    local_mic_effect_open(dec); //mic音效处理节点
#endif

#if !WIRELESS_MIC_EFFECT_ASYNC_EN
    struct mic_sample_params  params = {
        .sample_rate = dec->sample_rate,
        .gain =  dec->gain,
        .channel_num = dec->pcm_dec.ch_num,

#if 0//WIRELESS_MIC_EFFECT_ASYNC_EN
        .output_priv  = dec->mic_effect.effect,
        .output_handler = live_mic_effect_async_input,
#endif
    };

    dec->mic = mic_sample_open(&params);
    mic_sample_set_resume_handler(dec->mic, mic_dec_resume);
#endif

    pcm_decoder_set_event_handler(&dec->pcm_dec, mic_dec_event_handler, dec->id);
#if WIRELESS_MIC_EFFECT_ASYNC_EN
    pcm_decoder_set_read_data(&dec->pcm_dec, mic_dec_read, dec);
#else
    pcm_decoder_set_read_data(&dec->pcm_dec, mic_sample_read, dec->mic);
#endif
    pcm_decoder_set_data_handler(&dec->pcm_dec, mic_dec_data_handler);

    audio_mixer_ch_open_head(&dec->mix_ch, p_mixer);
    audio_mixer_ch_set_sample_rate(&dec->mix_ch, dec->sample_rate);

#if (TCFG_AUDIO_ADC_BIT_MODE == ADC_BIT_WIDTH_24)&& (!TCFG_ADC_BIT_24_TO_16)
#else
    if (global_bit_wide) {
        dec->convert_16_to_32 = convet_data_open(1 << 4 | 1, 2048);
    }
#endif

#if (RECORDER_MIX_EN)
    dec->rec_mix_ch = rec_mix_fifo_ch_open(dec->pcm_dec.output_ch_num);
#endif

#if (!SOUNDCARD_ENABLE && !RECORDER_MIX_EN)
    audio_mixer_ch_set_no_wait(&dec->mix_ch, 1, 10); // 超时自动丢数
#endif

#if (SOUNDCARD_ENABLE)
    audio_mixer_ch_set_aud_ch_out(&dec->mix_ch, 0, BIT(0) | BIT(1));
    audio_mixer_ch_set_aud_ch_out(&dec->mix_ch, 1, BIT(0) | BIT(1));
#endif


    // 设置叠加功能


    // 数据流串联
    struct audio_stream_entry *entries[10] = {NULL};
    u8 entry_cnt = 0;
    entries[entry_cnt++] = &dec->pcm_dec.decoder.entry;
    // 添加自定义数据流节点等

    if (dec->convert_16_to_32) {
        entries[entry_cnt++] = &dec->convert_16_to_32->entry;
    }

#if !WIRELESS_MIC_EFFECT_ASYNC_EN
#if (TCFG_MIC_EFFECT_ENABLE && WIRELESS_MIC_EFFECT_ENABLE)
    entries[entry_cnt++] = &dec->mic_effect.entry;
#endif
#endif

#if SYS_DIGVOL_GROUP_EN
    void *dvol_entry = sys_digvol_group_ch_open("music_mic", -1, NULL);
    entries[entry_cnt++] = dvol_entry;
#endif // SYS_DIGVOL_GROUP_EN

#if (RECORDER_MIX_EN)
    if (dec->rec_mix_ch) {
        entries[entry_cnt++] = rec_mix_fifo_ch_get_entry(dec->rec_mix_ch);
    }
#endif/*RECORDER_MIX_EN*/

    // 最后输出到mix数据流节点
    entries[entry_cnt++] = &dec->mix_ch.entry;
    // 创建数据流，把所有节点连接起来
    dec->stream = audio_stream_open(dec, mic_dec_out_stream_resume);
    if (!dec->stream) {
        goto __err3;
    }
    audio_stream_add_list(dec->stream, entries, entry_cnt);


    // 设置音频输出类型
    audio_output_set_start_volume(APP_AUDIO_STATE_MUSIC);


    dec->start = 1;
    err = audio_decoder_start(&dec->pcm_dec.decoder);
    if (err) {
        goto __err3;
    }

    printf("audio_dec_mic start successful !!!!!!!!!");
    return 0;

__err3:
    dec->start = 0;
    mic_dec_close();
    return err;
__err1:
    mic_dec_relaese();
    return err;
}

/*----------------------------------------------------------------------------*/
/**@brief    fm解码关闭
   @param
   @return
   @note
*/
/*----------------------------------------------------------------------------*/
static void __mic_dec_close(void)
{
    if (mic_dec && mic_dec->start) {
        mic_dec->start = 0;
#if WIRELESS_MIC_EFFECT_ASYNC_EN
        mic_sample_close(mic_dec->mic);
#if (TCFG_MIC_EFFECT_ENABLE && WIRELESS_MIC_EFFECT_ENABLE)
        local_mic_effect_close(mic_dec);
#endif

        pcm_decoder_close(&mic_dec->pcm_dec);

#else
        pcm_decoder_close(&mic_dec->pcm_dec);
        mic_sample_close(mic_dec->mic);

#if (TCFG_MIC_EFFECT_ENABLE && WIRELESS_MIC_EFFECT_ENABLE)
        local_mic_effect_close(mic_dec);
#endif
#endif // WIRELESS_MIC_EFFECT_ASYNC_EN

        mic_dec->mic = NULL;

        if (mic_dec->convert_16_to_32) {
            convet_data_close(mic_dec->convert_16_to_32);
        }

#if SYS_DIGVOL_GROUP_EN
        sys_digvol_group_ch_close("music_mic");
#endif // SYS_DIGVOL_GROUP_EN

        audio_mixer_ch_close(&mic_dec->mix_ch);

#if (RECORDER_MIX_EN)
        if (mic_dec->rec_mix_ch != NULL) {
            void *entry = rec_mix_fifo_ch_get_entry(mic_dec->rec_mix_ch);
            if (entry != NULL) {
                audio_stream_del_entry(entry);
            }
        }
        rec_mix_fifo_ch_close(mic_dec->rec_mix_ch);
        mic_dec->rec_mix_ch = NULL;
#endif
        // 先关闭各个节点，最后才close数据流
        if (mic_dec->stream) {
            audio_stream_close(mic_dec->stream);
            mic_dec->stream = NULL;
        }

    }
}

/*----------------------------------------------------------------------------*/
/**@brief    linein解码资源等待
   @param    *wait: 句柄
   @param    event: 事件
   @return   0：成功
   @note     用于多解码打断处理
*/
/*----------------------------------------------------------------------------*/
static int mic_wait_res_handler(struct audio_res_wait *wait, int event)
{
    int err = 0;
    log_i("mic_wait_res_handler, event:%d\n", event);
    if (event == AUDIO_RES_GET) {
        // 启动解码
        err = mic_dec_start();
    } else if (event == AUDIO_RES_PUT) {
        // 被打断
        __mic_dec_close();
    }

    return err;
}

/*----------------------------------------------------------------------------*/
/**@brief    打开linein解码
   @param    source: 音频源
   @param    sample_rate: 采样率
   @return   0：成功
   @return   非0：失败
   @note
*/
/*----------------------------------------------------------------------------*/
int mic_dec_open(u8 source, u32 sample_rate, u8 gain)
{
    int err;
    struct mic_dec_hdl *dec;
    dec = zalloc(sizeof(struct mic_dec_hdl));
    if (!dec) {
        return -ENOMEM;
    }
    dec->id = rand32();

    dec->pcm_dec.ch_num = 0;
    if (source & AUDIO_ADC_MIC_0) {
        printf("mic_dec_open mic 0");
        dec->pcm_dec.ch_num++;
    }
    if (source & AUDIO_ADC_MIC_1) {
        printf("mic_dec_open mic 1");
        dec->pcm_dec.ch_num++;
    }
    if (source & AUDIO_ADC_MIC_2) {
        printf("mic_dec_open mic 2");
        dec->pcm_dec.ch_num++;
    }
    if (source & AUDIO_ADC_MIC_3) {
        printf("mic_dec_open mic 3");
        dec->pcm_dec.ch_num++;
    }

    ASSERT(dec->pcm_dec.ch_num <= 2, "%d \n", dec->pcm_dec.ch_num);
    dec->source = source;

#if TCFG_MIC_EFFECT_ENABLE

#if WIRELESS_MIC_EFFECT_ASYNC_EN
    dec->pcm_dec.ch_num = 1;
    dec->pcm_dec.output_ch_num = audio_output_channel_num();
#else
    dec->pcm_dec.ch_num = 1;
    dec->pcm_dec.output_ch_num = 1;
#endif

#else
    dec->pcm_dec.output_ch_num = audio_output_channel_num();
#endif

    dec->pcm_dec.output_ch_type = audio_output_channel_type();

#if TCFG_MIC_EFFECT_ENABLE
    dec->pcm_dec.sample_rate = MIC_EFFECT_SAMPLERATE;
#else
    dec->pcm_dec.sample_rate = sample_rate;
#endif

    dec->gain = gain;
    dec->sample_rate = dec->pcm_dec.sample_rate;
    dec->nch = dec->pcm_dec.output_ch_num;
    dec->wait.priority = 2;
    dec->wait.preemption = 0;
    dec->wait.snatch_same_prio = 1;
#if (SOUNDCARD_ENABLE || (WIRELESS_2T1_DUPLEX_ROLE == 2))
    dec->wait.protect = 1;
#else
    dec->wait.protect = 0;
#endif


#if WIRELESS_MIC_EFFECT_ASYNC_EN
    dec->pcm_buf = zalloc(LADC_MIC_IRQ_POINTS  * 2 * 2 * 6);
    if (!dec->pcm_buf) {
        printf("mic_dec->pcm_buf malloc error !!!!!!");
        free(dec);
        mic_dec = NULL;
        return -ENOMEM;
    }
    cbuf_init(&dec->cbuf, dec->pcm_buf, LADC_MIC_IRQ_POINTS  * 2 * 2 * 6);
    spin_lock_init(&mic_dec_lock);
#endif

    mic_dec = dec;

    dec->wait.handler = mic_wait_res_handler;
    clock_add(AUDIO_CODING_PCM);

    err = audio_decoder_task_add_wait(&decode_task, &dec->wait);
    return err;
}

/*----------------------------------------------------------------------------*/
/**@brief    关闭linein解码
   @param
   @return
   @note
*/
/*----------------------------------------------------------------------------*/
void mic_dec_close(void)
{

    if (!mic_dec) {
        printf("mic_dec_ have been closed");
        return;
    }

    __mic_dec_close();
    mic_dec_relaese();
    clock_set_cur();
    log_i("mic dec close \n\n ");
}

/*----------------------------------------------------------------------------*/
/**@brief    linein解码重新开始
   @param    id: 文件解码id
   @return   0：成功
   @return   非0：失败
   @note
*/
/*----------------------------------------------------------------------------*/
int mic_dec_restart(int id)
{
    if ((!mic_dec) || (id != mic_dec->id)) {
        return -1;
    }
    u8 source = mic_dec->source;
    u32 sample_rate = mic_dec->pcm_dec.sample_rate;
    u8 gain = mic_dec->gain;
    mic_dec_close();
    int err = mic_dec_open(source, sample_rate, gain);
    return err;
}

/*----------------------------------------------------------------------------*/
/**@brief    推送linein解码重新开始命令
   @param
   @return   true：成功
   @return   false：失败
   @note
*/
/*----------------------------------------------------------------------------*/
int mic_dec_push_restart(void)
{
    if (!mic_dec) {
        return false;
    }
    int argv[3];
    argv[0] = (int)mic_dec_restart;
    argv[1] = 1;
    argv[2] = (int)mic_dec->id;
    os_taskq_post_type(os_current_task(), Q_CALLBACK, ARRAY_SIZE(argv), argv);
    return true;
}
