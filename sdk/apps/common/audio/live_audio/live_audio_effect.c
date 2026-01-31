/*************************************************************************************************/
/*!
*  \file      live_audio_effect.c
*
*  \brief     这里主要存放实时音频下的效果处理
*
*  Copyright (c) 2011-2022 ZhuHai Jieli Technology Co.,Ltd.
*
*/
/*************************************************************************************************/
#include "live_audio.h"
#include "live_audio_effect.h"
#include "wireless_mic_effect.h"
#include "audio_vocal_remove.h"
#include "audio_dec.h"

struct live_music_effect_param {
    u32 sample_rate;
    u8 ch_num;
    u8 bit_width;
    void *output_priv;      /*输出私有参数*/
    int (*output)(void *output_priv, void *data, int len);     /*输出接口*/
};

struct live_music_effect {
    struct audio_stream *stream;			// 音频流
    struct audio_stream_entry entry;		// effect 音频入口
    struct audio_stream_entry output_entry; //effect 数据出口

    struct live_music_effect_param param; //effect 参数

#if LIVE_CAPTURE_VOCAL_REMOVE_EN && AUDIO_VOCAL_REMOVE_EN
    vocal_remove_hdl *vocal_hdl;
    struct channel_switch *vocal_ch_switch;
#endif

    u32 process_len;	// 数据流处理长度
    u8 bypass;

    spinlock_t lock;
    u8 start;
    u32 timestamp;
    /* OS_SEM   sem; */

};

struct live_audio_effect_context {
    u8 nch;
    u8 remain;
    int sample_rate;
    int (*write_frame)(void *path, struct audio_frame *frame);
    void *opath;
    void (*underrun_wakeup)(void *stream);
    void *underrun_wakeup_data;
    void *overrun_wakeup_data;
    void (*overrun_wakeup)(void *);
#if (WIRELESS_MIC_EFFECT_ENABLE && WIRELESS_MASTER_MIC_EFFECT_ENABLE)
    struct live_mic_effect *mic_effect;
#endif
    struct live_music_effect *music_effect;
    //TODO
};

static u8 all_entry_cnt = 0;
static struct audio_stream_entry *entries[8] = {NULL};

#if (WIRELESS_MIC_EFFECT_ENABLE && WIRELESS_MASTER_MIC_EFFECT_ENABLE)
static int live_audio_effect_output(void *priv, void *data, int len)
{
    struct live_audio_effect_context *ctx = (struct live_audio_effect_context *)priv;
    int wlen = 0;
    struct audio_frame frame = {
        .data = data,
        .len = len,
        .nch = ctx->nch,
        .sample_rate = ctx->sample_rate,
    };

    if (ctx->write_frame) {
        wlen = ctx->write_frame(ctx->opath, &frame);
    }
    return wlen;
}
#endif

static int live_audio_music_effect_output(void *priv, void *data, int len)
{
    struct live_audio_effect_context *ctx = (struct live_audio_effect_context *)priv;
    int wlen = 0;
    struct audio_frame frame = {
        .data = data,
        .len = len,
        .nch = ctx->nch,
        .sample_rate = ctx->sample_rate,
    };

    if (ctx->write_frame) {
        wlen = ctx->write_frame(ctx->opath, &frame);
    }
    return wlen;
}

static void live_music_effect_open(struct live_music_effect *effect)
{
#if LIVE_CAPTURE_VOCAL_REMOVE_EN && AUDIO_VOCAL_REMOVE_EN
    if (effect->param.ch_num == 2) {
        printf(">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>open vocal remove");
        effect->vocal_hdl = vocal_remove_open(effect->param.ch_num, effect->param.sample_rate, 100, 200, 13000);
        set_vocal_remove_hdl(effect->vocal_hdl);

        if (JLA_CODING_CHANNEL == 1) {
            printf(">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>open vocal remove channel switch");
            effect->vocal_ch_switch =  channel_switch_open(AUDIO_CH_DIFF, 0);
            if (effect->vocal_ch_switch) {
                channel_switch_set_bit_wide(effect->vocal_ch_switch, global_bit_wide);
                set_vocal_ch_switch_hdl(effect->vocal_ch_switch);
            }
        }
    }
#endif
}

static void live_music_effect_close(struct live_music_effect **music_effect)
{
    struct live_music_effect *effect = *music_effect;

    if (!effect) {
        return;
    }
    spin_lock(&effect->lock);
    effect->start = 0;
    spin_unlock(&effect->lock);

#if LIVE_CAPTURE_VOCAL_REMOVE_EN && AUDIO_VOCAL_REMOVE_EN
    if (effect->vocal_hdl) {
        set_vocal_remove_hdl(0);
        audio_vocal_remove_close(effect->vocal_hdl);
    }
    if (effect->vocal_ch_switch) {
        set_vocal_ch_switch_hdl(0);
        channel_switch_close(&effect->vocal_ch_switch);
    }
#endif

    if (effect->stream) {
        audio_stream_close(effect->stream);
    }

    all_entry_cnt = 0;
    memset(entries, 0, sizeof(entries));

    local_irq_disable();
    free(*music_effect);
    *music_effect = NULL;
    local_irq_enable();
}

static void live_music_effect_data_process_len(struct audio_stream_entry *entry,  int len)
{
    struct live_music_effect *effect = container_of(entry, struct live_music_effect, entry);
    effect->process_len = len;
}

static int live_music_effect_output_data_handler(
    struct audio_stream_entry *entry,
    struct audio_data_frame *in,
    struct audio_data_frame *out)
{
    struct live_music_effect *effect = container_of(entry, struct live_music_effect, output_entry);
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

static int live_music_effect_stream_entry_add(struct live_music_effect *effect, struct audio_stream_entry **entries, int max)
{
    int entry_cnt = 0;

#if LIVE_CAPTURE_VOCAL_REMOVE_EN && AUDIO_VOCAL_REMOVE_EN
    if (effect->vocal_hdl) {
        entries[entry_cnt++] = &effect->vocal_hdl->entry;
        if (effect->vocal_ch_switch) {
            entries[entry_cnt++] = &effect->vocal_ch_switch->entry;
        }
    }
#endif

    return entry_cnt;
}

static void music_effect_stream_resume(void *p)
{
    struct live_music_effect *effect = (struct live_music_effect *)p;
    if (effect) {
        audio_stream_resume(&effect->entry);
    }
}

int live_music_effect_input(void *_effect, void *buff, int len, u32 time)
{
    struct live_music_effect *effect = (struct live_music_effect *)_effect;
    if (!effect) {
        printf("input   !effect");
        return 0;
    }
    if (effect->start) {
        /* printf("live_music_effect_input  %d",len); */
        struct audio_data_frame frame = {0};
        frame.channel = effect->param.ch_num;
        frame.sample_rate = effect->param.sample_rate;
        frame.data_len = len;
        frame.data = buff;
        /* frame.timestamp = time; */
        /* effect->timestamp  = time; */
        /* spin_unlock(&effect->lock); */
        int err = audio_stream_run(&effect->entry, &frame);
        if (err < 0) {
            r_printf("live music effect stream run err!");
            music_effect_stream_resume(effect);
        }
        return	effect->process_len;

    }
    printf("effect->start %d", effect->start);
    return 0;
}

struct live_music_effect *live_music_effect_init(struct live_music_effect_param *param)
{
    struct live_music_effect *effect = (struct live_music_effect *)zalloc(sizeof(struct live_music_effect));
    if (effect == NULL) {
        return NULL;
    }
    memcpy(&effect->param, param, sizeof(struct live_music_effect_param));
    g_printf("live music effect->param.sample_rate %d  ch_num %d\n", effect->param.sample_rate, effect->param.ch_num);

    //打开音效处理
    if (!effect->bypass) {
        live_music_effect_open(effect);
    }

    spin_lock_init(&effect->lock);
    /* os_sem_create(&effect->sem, 1); */

    effect->entry.data_process_len = live_music_effect_data_process_len;
    effect->output_entry.data_handler = live_music_effect_output_data_handler; //数据输出节点

// 数据流串联
    all_entry_cnt = 0;
    entries[all_entry_cnt++] = &effect->entry;

    if (!effect->bypass) {
        //添加音效处理节点
        all_entry_cnt += live_music_effect_stream_entry_add(effect, &entries[all_entry_cnt], ARRAY_SIZE(entries) - all_entry_cnt);
    }
    entries[all_entry_cnt++] = &effect->output_entry;  //音效输出节点，放最后面

    effect->stream = audio_stream_open(effect, music_effect_stream_resume);
    audio_stream_add_list(effect->stream, entries, all_entry_cnt);

    effect->start = 1;
    return effect;
}

void *live_audio_effect_open(struct live_audio_effect_params *params)
{
    struct live_audio_effect_context *ctx = (struct live_audio_effect_context *)zalloc(sizeof(struct live_audio_effect_context));

    if (!ctx) {
        return NULL;
    }
#if (WIRELESS_MIC_EFFECT_ENABLE && WIRELESS_MASTER_MIC_EFFECT_ENABLE)
    struct live_mic_effect_param  param = {
        .effect_config = LIVE_MIC_EFFECT_CONFIG,
        .sample_rate = params->sample_rate,
        .ch_num = params->nch,
        .output_priv = ctx,
        .output = live_audio_effect_output,
        .bit_width = global_bit_wide,
    };
#endif

    ctx->nch = params->nch;
    ctx->sample_rate = params->sample_rate;
    ctx->opath = params->opath;
    ctx->write_frame = params->write_frame;
    ctx->underrun_wakeup_data = params->underrun_wakeup_data;
    ctx->underrun_wakeup = params->underrun_wakeup;
    ctx->overrun_wakeup_data = params->overrun_wakeup_data;
    ctx->overrun_wakeup = params->overrun_wakeup;
#if (WIRELESS_MIC_EFFECT_ENABLE && WIRELESS_MASTER_MIC_EFFECT_ENABLE)
    if (ctx->nch >= 2) {
        printf("channel >= 2,live_audio_capture_mic_effect pass");
        return NULL;
    }
    ctx->mic_effect = live_mic_effect_init(&param);
#else
    struct live_music_effect_param  music_effect_params = {
        .sample_rate = params->sample_rate,
        .ch_num = params->nch,
        .output_priv = ctx,
        .output = live_audio_music_effect_output,
        .bit_width = global_bit_wide,
    };
    ctx->music_effect = live_music_effect_init(&music_effect_params);
#endif
    return ctx;
}

void live_audio_effect_close(void *effect)
{
    struct live_audio_effect_context *ctx = (struct live_audio_effect_context *)effect;
#if (WIRELESS_MIC_EFFECT_ENABLE && WIRELESS_MASTER_MIC_EFFECT_ENABLE)
    if (ctx->mic_effect) {
        live_mic_effect_close(ctx->mic_effect);
    }
#endif

    if (ctx->music_effect) {
        live_music_effect_close(&ctx->music_effect);
    }

    if (ctx) {
        free(ctx);
    }
}

int live_audio_effect_write_frame(void *effect, struct audio_frame *frame)
{
    struct live_audio_effect_context *ctx = (struct live_audio_effect_context *)effect;
    int wlen = 0;

#if (WIRELESS_MIC_EFFECT_ENABLE && WIRELESS_MASTER_MIC_EFFECT_ENABLE)
    wlen = live_mic_effect_input(ctx->mic_effect, frame->data, frame->len, 0);
#elif AUDIO_VOCAL_REMOVE_EN
    wlen = live_music_effect_input(ctx->music_effect, frame->data, frame->len, 0);
#else
    if (!ctx->remain) {
        /*
         * 这里提供两种方式：
         * 一种是直接run，一种是通过audio_stream结构传递信息
         */
        /*
         * 直接运算
         */
        //live_audio_effect_run(frame->data, frame->len);


        /*
         * audio_stream 方式
         * struct audio_data_frame stream_frame = {
         *      .data = frame->data,
         *      .len = frame->len,
         *      .channel = frame->nch,
         *      .sample_rate = frame->sample_rate,
         * };
         * audio_stream_run(&ctx->entry, &stream_frame);
         *
         **/
    }
    if (ctx->write_frame) {
        wlen = ctx->write_frame(ctx->opath, frame);
        frame->offset += wlen;
        ctx->remain = frame->offset == frame->len ? 0 : 1;
    }
#endif

    return wlen;
}

void live_audio_effect_wakeup(void *effect)
{
    struct live_audio_effect_context *ctx = (struct live_audio_effect_context *)effect;

    if (ctx->overrun_wakeup) {
        ctx->overrun_wakeup(ctx->overrun_wakeup_data);
    }
}

