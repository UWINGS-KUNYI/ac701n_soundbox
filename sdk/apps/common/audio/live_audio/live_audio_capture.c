/*************************************************************************************************/
/*!
*  \file      live_audio_capture.c
*
*
*  \brief
*
*  Copyright (c) 2011-2022 ZhuHai Jieli Technology Co.,Ltd.
*
*/
/*************************************************************************************************/
#include "app_config.h"
#include "live_audio.h"
#include "live_audio_buffer.h"
#include "capture_synchronize.h"
#include "live_stream_encoder.h"
#include "live_audio_effect.h"
#include "live_audio_mixer.h"
#include "live_audio_capture.h"
#include "pcm_capture_buffer.h"
#include "os/os_api.h"

#define LIVE_AUDIO_CAPTURE_SYNCHRONIZE_ENABLE       1

#if WIRELESS_MIC_EFFECT_ASYNC_EN

#define LIVE_AUDIO_CAPTURE_EFFECT_ENABLE            0//开启WIRELESS_MIC_EFFECT_ASYNC_EN宏会开启异步线程同时输出做了mic音效处理的mic数据到本地DAC以及capture数据流。若需要本地DAC的mic数据与到capture数据流分开做mic音效处理请关闭WIRELESS_MIC_EFFECT_ASYNC_EN

#else

#define LIVE_AUDIO_CAPTURE_EFFECT_ENABLE            0//central角色默认关闭需要开启请前往板级设置将mic_effect,WIRELESS_MIC_ECHO_ENABLE等相关宏打开

#endif

extern const int global_bit_wide;

struct live_audio_capture_context {
    void *source;
    void *encoder;
#if LIVE_AUDIO_CAPTURE_EFFECT_ENABLE
    void *effect;
#endif
#if LIVE_AUDIO_CAPTURE_SYNCHRONIZE_ENABLE
    void *synchronize;
#endif
    void *audio_buffer;
    void *pcm_buffer;
    void *mix_ch;
    void *multi_capture;
    void *pcm_capture_buffer;
    struct live_audio_mode_ops *audio_ops;
    struct list_head stream_head;
    spinlock_t lock;
};

#define MAX_CAPTURE_NUM         6
struct live_audio_multi_analysis {
    struct list_head capture_list;
    void *mix_ch_array[MAX_CAPTURE_NUM];
    u32 underrun_stats[MAX_CAPTURE_NUM];
    u32 overrun_stats[MAX_CAPTURE_NUM];
    u32 stats_run_num;
    u32 output_timeout;
    const char *task_name;
};

struct live_audio_multi_capture {
    struct list_head entry;
    struct list_head stream_head;
    u8 audio_num;
    spinlock_t lock;
    void *pcm_buffer;
    void *encoder;
    void *audio_buffer;
    char name[16];
    struct live_audio_multi_analysis *analysis_hdl;
};

static LIST_HEAD(g_audio_mixer_head);

#define live_stream_enter_critial(x)        if (x) {local_irq_disable();}//spin_lock(x);}
#define live_stream_exit_critial(x)         if (x) {local_irq_enable();}//spin_unlock(x);}

const int CONFIG_LIVE_AUDIO_DYNAMIC_ANALYSIS = 0;
const int CONFIG_LIVE_AUDIO_TX_FRAMES = 480;
const int CONFIG_LIVE_AUDIO_UNDERRUN_FRAMES = 128;

static int live_audio_multi_capture_buffer_len(void *multi_capture);
static int live_audio_multi_capture_read_data(void *multi_capture, void *buf, int len);
static void *live_audio_multi_capture_init(const char *name, struct audio_path *path);
static void live_audio_multi_capture_uninit(void *multi_capture);
static void live_audio_multi_capture_analysis_init(struct live_audio_multi_capture *capture);
static void live_audio_multi_capture_analysis_close(struct live_audio_multi_capture *capture);
static void live_audio_multi_capture_analysis_run(struct live_audio_multi_capture *capture, u8 underrun);
static void live_audio_capture_analysis_setup(struct live_audio_capture_context *ctx);
static void live_audio_capture_analysis_close(struct live_audio_capture_context *ctx);

static inline struct audio_stream_node *new_stream_node(struct list_head *head, spinlock_t *lock)
{
    struct audio_stream_node *node = (struct audio_stream_node *)zalloc(sizeof(struct audio_stream_node));

    if (node) {
        node->lock = lock;
        node->head = head;
    }

    return node;
}

static inline void free_stream_node(struct audio_stream_node *node)
{
    if (node) {
        free(node);
    }
}

static int live_audio_capture_write_frame(void *priv, struct audio_frame *frame)
{
    struct audio_stream_node *node = (struct audio_stream_node *)priv;
    struct audio_stream_node *next_node;
    int wlen = 0;

    live_stream_enter_critial(node->lock);
    if (node->stop) {
        live_stream_exit_critial(node->lock);
        return 0;
    }

    next_node = list_first_entry(&node->entry, struct audio_stream_node, entry);
    live_stream_exit_critial(node->lock);

    /*printf("next : 0x%x\n", (u32)next_node);*/
    if (next_node->write_frame) {
        node->state |= AUDIO_STREAM_WRITING_FRAME;
        wlen = next_node->write_frame(next_node->stream, frame);
        live_stream_enter_critial(node->lock);
        if (wlen < (frame->len - frame->offset)) {
            node->state |= AUDIO_STREAM_OVERRUN;
        }
        node->state &= ~AUDIO_STREAM_WRITING_FRAME;

        if (next_node->state & AUDIO_STREAM_UNDERRUN) {
            next_node->state &= ~AUDIO_STREAM_UNDERRUN;
            if (next_node->wakeup) {
                next_node->wakeup(next_node->stream);
            }
        }
        live_stream_exit_critial(node->lock);
    }
    return wlen;
}

static int live_audio_capture_read_frame(void *priv, struct audio_frame *frame, int len)
{
    struct audio_stream_node *node = (struct audio_stream_node *)priv;
    struct audio_stream_node *prev_node;
    int rlen = 0;

    live_stream_enter_critial(node->lock);
    if (node->stop) {
        live_stream_exit_critial(node->lock);
        return 0;
    }

    prev_node = list_entry(node->entry.prev, struct audio_stream_node, entry);
    live_stream_exit_critial(node->lock);

    if (prev_node->read_frame) {
        node->state |= AUDIO_STREAM_READING_FRAME;
        rlen = prev_node->read_frame(prev_node->stream, frame, len);
        live_stream_enter_critial(node->lock);
        if (rlen < len) {
            node->state |= AUDIO_STREAM_UNDERRUN;
        }

        node->state &= ~AUDIO_STREAM_READING_FRAME;
        if (prev_node->state & AUDIO_STREAM_OVERRUN) {
            prev_node->state &= ~AUDIO_STREAM_OVERRUN;
            if (prev_node->wakeup) {
                prev_node->wakeup(prev_node->stream);
            }
        }
        live_stream_exit_critial(node->lock);
    }

    return rlen;
}

static void live_audio_capture_underrun_wakeup(void *priv)
{
    struct audio_stream_node *node = (struct audio_stream_node *)priv;
    struct audio_stream_node *next_node = NULL;

    live_stream_enter_critial(node->lock);
    if (node->stop) {
        live_stream_exit_critial(node->lock);
        return;
    }

    if (node->entry.next != node->head) {
        next_node = list_entry(node->entry.next, struct audio_stream_node, entry);
    }
    live_stream_exit_critial(node->lock);

    if (next_node) {
        if ((next_node->state & AUDIO_STREAM_UNDERRUN) || (next_node->state & AUDIO_STREAM_READING_FRAME)) {
            if (next_node->wakeup) {
                next_node->wakeup(next_node->stream);
            }
            next_node->state &= ~AUDIO_STREAM_UNDERRUN;
        }
    }
    /* live_stream_exit_critial(node->lock); */
}

static void live_audio_capture_overrun_wakeup(void *priv)
{
    struct audio_stream_node *node = (struct audio_stream_node *)priv;
    struct audio_stream_node *prev_node = NULL;

    live_stream_enter_critial(node->lock);
    if (node->stop) {
        live_stream_exit_critial(node->lock);
        return;
    }
    if (node->entry.prev != node->head) {
        prev_node = list_entry(node->entry.prev, struct audio_stream_node, entry);
    }
    live_stream_exit_critial(node->lock);

    if (prev_node) {
        if ((prev_node->state & AUDIO_STREAM_OVERRUN) || (prev_node->state & AUDIO_STREAM_WRITING_FRAME)) {
            prev_node->state &= ~AUDIO_STREAM_OVERRUN;
            if (prev_node->wakeup) {
                prev_node->wakeup(prev_node->stream);
            }
        }
    }

    /* live_stream_exit_critial(node->lock); */
}

static struct audio_stream_node *live_audio_encoder_node_init(struct list_head *head, spinlock_t *lock, struct audio_path *path)
{
    struct audio_stream_node *node = new_stream_node(head, lock);
    if (!node) {
        return NULL;
    }
    struct audio_path encode_path = {
        .input = {
            .path = node,
            .read_frame = live_audio_capture_read_frame,
        },
        .output = {
            .path = node,
            .write_frame = live_audio_capture_write_frame,
        },
    };

    memcpy(&encode_path.time, &path->time, sizeof(encode_path.time));
    memcpy(&encode_path.fmt, &path->fmt, sizeof(encode_path.fmt));
    node->wakeup = (void (*)(void *))live_stream_encoder_wakeup;
    node->coding_type = path->fmt.coding_type;
    list_add_tail(&node->entry, head);
    node->stream = live_stream_encoder_open(&encode_path);

    printf("live audio stream encoder init 0x%x.\n", (u32)node);
    return node;
}
static int live_audio_encoder_setup(struct live_audio_capture_context *ctx,
                                    struct audio_path *ipath,
                                    struct audio_path *opath)
{
    if (ipath->fmt.coding_type == opath->fmt.coding_type) {
        return 0;
    }
    struct audio_stream_node *node = live_audio_encoder_node_init(&ctx->stream_head, &ctx->lock, opath);
    if (node) {
        ctx->encoder = node->stream;
    }

    return 0;
}


static void live_audio_encoder_stop(struct live_audio_capture_context *ctx)
{
    if (ctx->encoder) {
        live_stream_encoder_stop(ctx->encoder);
    }
}

static int live_audio_encoder_close(struct live_audio_capture_context *ctx)
{
    if (ctx->encoder) {
        live_stream_encoder_close(ctx->encoder);
    }

    return 0;
}

static int live_audio_multi_capture_encoder_setup(struct live_audio_multi_capture *capture, struct audio_path *path)
{
    struct audio_stream_node *node = live_audio_encoder_node_init(&capture->stream_head, &capture->lock, path);
    if (node) {
        capture->encoder = node->stream;
    }

    return 0;
}

static void live_audio_multi_capture_encoder_stop(struct live_audio_multi_capture *capture)
{
    if (capture->encoder) {
        live_stream_encoder_stop(capture->encoder);
    }
}

static void live_audio_multi_capture_encoder_close(struct live_audio_multi_capture *capture)
{
    if (capture->encoder) {
        live_stream_encoder_close(capture->encoder);
    }
}

static int live_audio_capture_synchronize_setup(struct live_audio_capture_context *ctx,
        struct audio_path *ipath,
        struct audio_path *opath)
{
#if LIVE_AUDIO_CAPTURE_SYNCHRONIZE_ENABLE
    if ((int)ipath->fmt.priv == LIVE_SOUND_SYNCHRONIZE_STREAM) {
        return 0;
    }
    struct audio_stream_node *node = new_stream_node(&ctx->stream_head, &ctx->lock);
    if (!node) {
        return -EINVAL;
    }

    struct audio_path synchronize_path = {
        .fmt = {
            .channel = opath->fmt.channel,
            .sample_rate = opath->fmt.sample_rate,
            .bit_width = global_bit_wide,
        },
        .input = {
            .path = node,
            .wakeup = live_audio_capture_overrun_wakeup,
        },
        .output = {
            .path = node,
            .write_frame = live_audio_capture_write_frame,
        },
        .delay_time = ipath->delay_time + opath->delay_time,
    };

#if LIVE_CAPTURE_VOCAL_REMOVE_EN && AUDIO_VOCAL_REMOVE_EN
    synchronize_path.fmt.channel = 2;
#endif

    ctx->synchronize = live_stream_capture_synchronize_open(&synchronize_path);

    node->stream = ctx->synchronize;
    node->write_frame = live_stream_capture_synchronize_handler;
    node->coding_type = opath->fmt.coding_type;
    node->wakeup = live_stream_capture_synchronize_wakeup;
    list_add_tail(&node->entry, &ctx->stream_head);
    printf("live audio capture synchronize init 0x%x.\n", (u32)node);
#endif
    return 0;
}

static int live_audio_capture_synchronize_close(struct live_audio_capture_context *ctx)
{
    if (ctx->synchronize) {
        live_stream_capture_synchronize_close(ctx->synchronize);
        ctx->synchronize = NULL;
    }

    return 0;
}

static void live_audio_mixer_event_handler(void *priv, int event)
{
    struct live_audio_capture_context *ctx = (struct live_audio_capture_context *)priv;
    switch (event) {
    case AUDIO_MIXER_EVENT_CH_START:
        spin_lock(&ctx->lock);
        if (ctx->synchronize) {
            int frames = live_audio_buffered_frames_from_mixer(ctx->mix_ch);
            printf("Mix channel start, buffered frames : %d\n", frames);
            live_stream_capture_synchronize_use_frames(ctx->synchronize, -frames);
        }
        spin_unlock(&ctx->lock);
        break;
    default:
        printf("-----%s----%d  %d", __FUNCTION__, __LINE__, event);
        break;
    }
}

static int live_audio_capture_mixer_setup(struct live_audio_capture_context *ctx, const char *mixer_name, struct audio_path *path)
{
    struct audio_stream_node *node = new_stream_node(&ctx->stream_head, &ctx->lock);

    struct live_audio_mixer_ch_params params = {
        .wakeup_data = node,
        .wakeup = live_audio_capture_overrun_wakeup,
        .nch = path->fmt.channel,
        .priv = ctx,
        .event_handler = live_audio_mixer_event_handler,
    };
    if (path->output.ch_map) {
        memcpy(params.ch_map, path->output.ch_map, params.nch);
    }

    node->coding_type = AUDIO_CODING_PCM;
    node->write_frame = live_audio_mix_ch_write_frame;
    list_add_tail(&node->entry, &ctx->stream_head);

    node->stream = live_audio_mix_ch_open(mixer_name, &params);
    ctx->mix_ch = node->stream;
    return 0;
}

static void live_audio_capture_mixer_stop(struct live_audio_capture_context *ctx)
{
    printf("live_audio_capture_mixer_stop");
    if (ctx->mix_ch) {
        live_audio_mix_ch_stop(ctx->mix_ch);
    }
}

static void live_audio_capture_mixer_close(struct live_audio_capture_context *ctx)
{
    if (ctx->mix_ch) {
        live_audio_mix_ch_close(ctx->mix_ch);
    }
}

static struct audio_stream_node *live_audio_pcm_buffer_node_init(struct list_head *head, spinlock_t *lock, struct audio_path *path)
{
    struct audio_stream_node *node = new_stream_node(head, lock);
    if (!node) {
        return NULL;
    }

    int buffer_size = path->fmt.sample_rate * path->fmt.channel * 2 * path->delay_time / 1000;
    if (buffer_size > 20 * 1024) {
        buffer_size = 20 * 1024;
    }
    if (buffer_size < 4 * 1024) {
        buffer_size = 4 * 1024;
    }

    struct live_audio_buffer_params buf_params = {
        .overrun_wakeup_data = node,
        .underrun_wakeup_data = node,
        .overrun_wakeup = live_audio_capture_overrun_wakeup,
        .underrun_wakeup = live_audio_capture_underrun_wakeup,
        .size = buffer_size,
    };

    node->coding_type = AUDIO_CODING_PCM;
    node->write_frame = live_audio_buffer_push_frame;
    node->read_frame = live_audio_buffer_read_frame;


    list_add_tail(&node->entry, head);
    node->stream = live_audio_buffer_init(&buf_params);
    printf("live audio capture pcm buffer init 0x%x, %d. buffer 0x%x\n", (u32)node, buffer_size, node->stream);

    return node;
}

static int live_audio_pcm_buffer_setup(struct live_audio_capture_context *ctx, struct audio_path *path)
{
    struct audio_stream_node *node = live_audio_pcm_buffer_node_init(&ctx->stream_head, &ctx->lock, path);
    if (node) {
        ctx->pcm_buffer = node->stream;
    }
    return 0;
}

static void live_audio_pcm_buffer_close(struct live_audio_capture_context *ctx)
{
    if (ctx->pcm_buffer) {
        live_audio_buffer_close(ctx->pcm_buffer);
    }
}

static struct audio_stream_node *live_audio_pcm_capture_buf_node_init(struct list_head *head, spinlock_t *lock, struct audio_path *path)
{
    struct audio_stream_node *node = new_stream_node(head, lock);

    if (!node) {
        return NULL;
    }

    u8 point_offset = path->fmt.bit_width ? 4 : 2;

    int delay_time = path->delay_time > 1000 ? path->delay_time / 1000 : path->delay_time;
    int buffer_size = path->fmt.sample_rate * path->fmt.channel * point_offset * delay_time / 1000 * 3;
    if (path->fmt.bit_width) {
        if (buffer_size > 40 * 1024) {
            buffer_size = 40 * 1024;
        }
    } else {
        if (buffer_size > 20 * 1024) {
            buffer_size = 20 * 1024;
        }
    }

    node->coding_type = AUDIO_CODING_PCM;
    node->write_frame = live_audio_pcm_capture_write_frame;
    node->wakeup = live_audio_pcm_capture_buf_wakeup;
    list_add_tail(&node->entry, head);
    node->stream = live_audio_pcm_capture_buf_open(buffer_size,
                   node,
                   live_audio_capture_overrun_wakeup,
                   node,
                   live_audio_capture_write_frame);

    return node;
}

static int live_audio_pcm_capture_buffer_setup(struct live_audio_capture_context *ctx, struct audio_path *path)
{
    struct audio_stream_node *node = live_audio_pcm_capture_buf_node_init(&ctx->stream_head, &ctx->lock, path);

    if (node) {
        ctx->pcm_capture_buffer = node->stream;
    }
    return 0;
}

static void live_audio_pcm_capture_buffer_close(struct live_audio_capture_context *ctx)
{
    if (ctx->pcm_capture_buffer) {
        live_audio_pcm_capture_buf_close(ctx->pcm_capture_buffer);
    }
}

static int live_audio_multi_capture_pcm_buffer_setup(struct live_audio_multi_capture *capture, struct audio_path *path)
{
    struct audio_stream_node *node = live_audio_pcm_buffer_node_init(&capture->stream_head, &capture->lock, path);
    if (node) {
        capture->pcm_buffer = node->stream;
    }
    return 0;
}

static void live_audio_multi_capture_pcm_buffer_close(struct live_audio_multi_capture *capture)
{
    if (capture->pcm_buffer) {
        live_audio_buffer_close(capture->pcm_buffer);
    }
}


static int live_audio_source_setup(struct live_audio_capture_context *ctx, struct audio_path *path)
{
    ctx->audio_ops = (struct live_audio_mode_ops *)path->input.path;

    if (!ctx->audio_ops) {
        return -EINVAL;
    }

    if (!ctx->audio_ops->capture_open) {
        printf("!ctx->audio_ops->capture_open");
        return -EINVAL;
    }

    struct audio_stream_node *node = new_stream_node(&ctx->stream_head, &ctx->lock);
    if (!node) {
        return -EINVAL;
    }

    struct audio_path opath = {
        .fmt = {
            .channel = path->fmt.channel,
            .sample_rate = path->fmt.sample_rate,
            .priv = path->fmt.priv,
        },
        .input = {
            .priv = path->input.priv,
        },
        .output = {
            .path = node,
            .write_frame = live_audio_capture_write_frame,
        },
        .time = {
            .reference_clock = path->time.reference_clock,
            .request = path->time.request,
        },
        .delay_time = path->delay_time,
    };
#if LIVE_CAPTURE_VOCAL_REMOVE_EN && AUDIO_VOCAL_REMOVE_EN
    opath.fmt.channel = 2;
#endif
    node->coding_type = path->fmt.coding_type;
    list_add(&node->entry, &ctx->stream_head);

    if (ctx->audio_ops->capture_wakeup) {
        node->wakeup = ctx->audio_ops->capture_wakeup;
    }
    node->stream = ctx->audio_ops->capture_open(&opath);
    ctx->source = node->stream;

    if (ctx->audio_ops->capture_start) {
        ctx->audio_ops->capture_start(ctx->source);
    }
    printf("live audio source init 0x%x.\n", (u32)node);
    return 0;
}

static int live_audio_source_close(struct live_audio_capture_context *ctx)
{
    if (!ctx->audio_ops) {
        return 0;
    }

    ctx->audio_ops->capture_close(ctx->source);
    return 0;
}

static int live_audio_capture_effect_setup(struct live_audio_capture_context *ctx, struct audio_path *path)
{
#if LIVE_AUDIO_CAPTURE_EFFECT_ENABLE
    struct audio_stream_node *node = new_stream_node(&ctx->stream_head, &ctx->lock);
    if (!node) {
        return -EINVAL;
    }

    node->coding_type = AUDIO_CODING_PCM;
    node->write_frame = live_audio_effect_write_frame;
    struct live_audio_effect_params effect_params = {
        .nch = path->fmt.channel,
        .sample_rate = path->fmt.sample_rate,
        .opath = node,
        .write_frame = live_audio_capture_write_frame,
        .underrun_wakeup = live_audio_capture_underrun_wakeup,
        .underrun_wakeup_data = node,
        .overrun_wakeup = live_audio_capture_overrun_wakeup,
        .overrun_wakeup_data = node,
        .bit_width = path->fmt.bit_width,
    };
#if LIVE_CAPTURE_VOCAL_REMOVE_EN && AUDIO_VOCAL_REMOVE_EN
    effect_params.nch = 2;
#endif
    void *stream_tmp = live_audio_effect_open(&effect_params);
    if (stream_tmp) {
        node->stream = stream_tmp;
        ctx->effect = node->stream;
        list_add_tail(&node->entry, &ctx->stream_head);
        node->wakeup = live_audio_effect_wakeup;
        printf("live_audio_capture_effect_setup success 0x%x", node);
    } else {
        printf("live_audio_capture_effect_setup error");
    }
#endif
    return 0;
}

static void live_audio_capture_effect_close(struct live_audio_capture_context *ctx)
{
#if LIVE_AUDIO_CAPTURE_EFFECT_ENABLE
    if (ctx->effect) {
        live_audio_effect_close(ctx->effect);
    }
#endif
}

static struct audio_stream_node *live_audio_stream_buffer_node_init(struct list_head *head, spinlock_t *lock, struct audio_path *path)
{
    struct audio_stream_node *node = new_stream_node(head, lock);
    if (!node) {
        return NULL;
    }

    struct live_audio_buffer_params buf_params = {
        .underrun_wakeup_data = node,
        .overrun_wakeup_data = node,
        .overrun_wakeup = live_audio_capture_overrun_wakeup,
        .underrun_wakeup = live_audio_capture_underrun_wakeup,
        .size = 1024,
    };

    node->coding_type = path->fmt.coding_type;
    node->write_frame = live_audio_buffer_push_frame;

    list_add_tail(&node->entry, head);
    node->stream = live_audio_buffer_init(&buf_params);

    printf("live audio stream buffer init 0x%x. buffer 0x%x\n", (u32)node, node->stream);
    return node;
}

static int live_audio_stream_buffer_setup(struct live_audio_capture_context *ctx, struct audio_path *path)
{
    struct audio_stream_node *node = live_audio_stream_buffer_node_init(&ctx->stream_head, &ctx->lock, path);
    if (node) {
        ctx->audio_buffer = node->stream;
    }
    return 0;
}


static void live_audio_stream_buffer_close(struct live_audio_capture_context *ctx)
{
    if (ctx->audio_buffer) {
        live_audio_buffer_close(ctx->audio_buffer);
    }
}

static int live_audio_multi_capture_stream_buf_setup(struct live_audio_multi_capture *capture, struct audio_path *path)
{
    struct audio_stream_node *node = live_audio_stream_buffer_node_init(&capture->stream_head, &capture->lock, path);

    if (node) {
        capture->audio_buffer = node->stream;
    }
    return 0;
}

static void live_audio_multi_capture_stream_buf_close(struct live_audio_multi_capture *capture)
{
    if (capture->audio_buffer) {
        live_audio_buffer_close(capture->audio_buffer);
    }
}

static void live_audio_stream_node_close(struct list_head *head)
{
    while (!list_empty(head)) {
        struct audio_stream_node *node = list_first_entry(head, struct audio_stream_node, entry);
        if (node) {
            list_del(&node->entry);
            free_stream_node(node);
        }
    }
}

int live_audio_capture_buffer_len(void *capture)
{
    struct live_audio_capture_context *ctx = (struct live_audio_capture_context *)capture;

    if (ctx == NULL) {
        return 0;
    }

    if (ctx->multi_capture) {
        return live_audio_multi_capture_buffer_len(ctx->multi_capture);
    }

    if (ctx->audio_buffer) {
        return live_audio_buffer_len(ctx->audio_buffer);
    }

    return 0;
}

int live_audio_capture_read_data(void *capture, void *buf, int len)
{
    struct live_audio_capture_context *ctx = (struct live_audio_capture_context *)capture;

    if (ctx == NULL) {
        return 0;
    }

    int rlen = 0;

    if (ctx->multi_capture) {
        return live_audio_multi_capture_read_data(ctx->multi_capture, buf, len);
    }
    if (ctx->audio_buffer) {
        if (live_audio_buffer_len(ctx->audio_buffer) < len) {
            /*printf("stream : %d, pcm buffer : %d\n", live_audio_buffer_len(ctx->audio_buffer), live_audio_buffer_len(ctx->pcm_buffer));*/
            return 0;
        }
        rlen = live_audio_buffer_read(ctx->audio_buffer, buf, len);
    }

    return rlen;
}

int live_audio_capture_send_update(void *capture, int time, u32 send_timestamp)
{
    struct live_audio_capture_context *ctx = (struct live_audio_capture_context *)capture;

    spin_lock(&ctx->lock);
    if (ctx->synchronize) {
        live_stream_capture_send_frames(ctx->synchronize, time, send_timestamp);
    }

    if (ctx->mix_ch) {
        int frames = live_audio_mixer_sample_rate(ctx->mix_ch) * time / 1000;
        live_audio_mixer_use_pcm_frames(ctx->mix_ch, frames);
    }
    spin_unlock(&ctx->lock);

    return 0;
}

void live_audio_capture_send_debug(void *capture)
{
    struct live_audio_capture_context *ctx = (struct live_audio_capture_context *)capture;

    if (ctx->mix_ch) {
        live_audio_mix_ch_debug(ctx->mix_ch);
    }
}

int live_audio_capture_read_pcm_data(void *capture, void *data, int len)
{
    struct live_audio_capture_context *ctx = (struct live_audio_capture_context *)capture;

    if (ctx->pcm_capture_buffer) {
        return live_audio_pcm_capture_buf_read(ctx->pcm_capture_buffer, data, len);
    }

    return 0;
}

void *__live_audio_capture_open(struct audio_path *ipath, struct audio_path *opath, u8 dual_fmt)
{
    struct live_audio_capture_context *ctx = (struct live_audio_capture_context *)zalloc(sizeof(struct live_audio_capture_context));

    if (!ctx) {
        return NULL;
    }
    INIT_LIST_HEAD(&ctx->stream_head);
    spin_lock_init(&ctx->lock);

    if (!opath->time.request) {
        opath->time.request = ipath->time.request;
        opath->time.reference_clock = ipath->time.reference_clock;
    }
    if (ipath->output.path) {
        const char *mixer_name = ipath->output.path;
        if (strncmp(mixer_name, "capture_mixer", strlen("capture_mixer")) == 0) {
            ctx->multi_capture = live_audio_multi_capture_init(mixer_name, opath);
            live_audio_capture_synchronize_setup(ctx, ipath, opath);
            live_audio_capture_effect_setup(ctx, opath);
            //多音源的最后一个输出节点为mixer
            live_audio_capture_mixer_setup(ctx, mixer_name, ipath);
            live_audio_capture_analysis_setup(ctx);
            goto source_open;
        }
    }
    live_audio_capture_synchronize_setup(ctx, ipath, opath);
    live_audio_capture_effect_setup(ctx, opath);
    if (dual_fmt) {
        live_audio_pcm_capture_buffer_setup(ctx, opath);
    }
    live_audio_pcm_buffer_setup(ctx, opath);
    live_audio_encoder_setup(ctx, ipath, opath);
    live_audio_stream_buffer_setup(ctx, opath);

source_open:
    live_audio_source_setup(ctx, ipath);

    return ctx;
}

void *live_audio_capture_open(struct audio_path *ipath, struct audio_path *opath)
{
    return __live_audio_capture_open(ipath, opath, 0);
}

void *live_audio_capture_dual_fmt_open(struct audio_path *ipath, struct audio_path *opath)
{
    return __live_audio_capture_open(ipath, opath, 1);
}


static void live_audio_capture_stop(void *capture)
{

    struct live_audio_capture_context *ctx = (struct live_audio_capture_context *)capture;

    if (!ctx) {
        return;
    }
    printf("live_audio_capture_stop");
    struct audio_stream_node *node = NULL;
    list_for_each_entry(node, &ctx->stream_head, entry) {
        if (node) {
            node->stop = 1;
        }
    }

}

static void live_audio_multi_capture_stop(void *capture)
{

    struct live_audio_multi_capture *ctx = (struct live_audio_multi_capture *)capture;

    if (!ctx) {
        return;
    }
    printf("live_audio_multi_capture_stop");


    struct audio_stream_node *node = NULL;
    list_for_each_entry(node, &ctx->stream_head, entry) {
        if (node) {
            node->stop = 1;
        }
    }
}

void live_audio_capture_close(void *capture)
{
    struct live_audio_capture_context *ctx = (struct live_audio_capture_context *)capture;

    if (!ctx) {
        return;
    }
    printf("live_audio_capture_close");
    live_stream_enter_critial(&ctx->lock);
    live_audio_capture_stop(ctx);
    live_stream_exit_critial(&ctx->lock);

    if (ctx->multi_capture) {
        live_audio_capture_mixer_stop(ctx);
    } else {
        live_audio_encoder_stop(ctx);
    }

    live_audio_source_close(ctx);
    live_audio_capture_synchronize_close(ctx);
    live_audio_capture_effect_close(ctx);

    if (ctx->multi_capture) {
        live_audio_capture_analysis_close(ctx);
        live_audio_capture_mixer_close(ctx);
        live_stream_enter_critial(&ctx->lock);
        live_audio_stream_node_close(&ctx->stream_head);
        live_stream_exit_critial(&ctx->lock);
        live_audio_multi_capture_uninit(ctx->multi_capture);
    } else {
        live_audio_pcm_capture_buffer_close(ctx);
        live_audio_pcm_buffer_close(ctx);
        live_audio_encoder_close(ctx);
        live_audio_stream_buffer_close(ctx);
        live_stream_enter_critial(&ctx->lock);
        live_audio_stream_node_close(&ctx->stream_head);
        live_stream_exit_critial(&ctx->lock);
    }

    free(ctx);
}

int live_audio_capture_add_path(void *capture, struct audio_path *opath)
{
    struct live_audio_capture_context *ctx = (struct live_audio_capture_context *)capture;

    return 0;
}

static int live_audio_capture_mixer_register(struct live_audio_multi_capture *capture, struct audio_path *path)
{
    struct audio_stream_node *node = new_stream_node(&capture->stream_head, &capture->lock);
    struct live_audio_mixer_config config = {
        .sample_rate = path->fmt.sample_rate,
        .nch = path->fmt.channel,
        .opath = node,
        .write_frame = live_audio_capture_write_frame,
    };

    if (path->fmt.coding_type == AUDIO_CODING_JLA) {
        config.frame_size = path->fmt.frame_len * path->fmt.sample_rate / 10000 * path->fmt.channel * 2;
    }

    int err = live_audio_mixer_register(capture->name, &config);
    if (err) {
        printf("live_audio_mixer_register error\n");
    }
    node->stream = (void *)capture->name;
    node->coding_type = AUDIO_CODING_PCM;
    node->wakeup = live_audio_mixer_wakeup;
    list_add_tail(&node->entry, &capture->stream_head);
    return 0;
}

static void *live_audio_multi_capture_init(const char *name, struct audio_path *path)
{
    struct live_audio_multi_capture *capture;

    list_for_each_entry(capture, &g_audio_mixer_head, entry) {
        if (strcmp(name, capture->name) == 0) {
            capture->audio_num++;
            return capture;
        }
    }

    capture = (struct live_audio_multi_capture *)zalloc(sizeof(struct live_audio_multi_capture));

    if (!capture) {
        return NULL;
    }

    INIT_LIST_HEAD(&capture->stream_head);
    spin_lock_init(&capture->lock);
    strcpy(capture->name, name);
    live_audio_capture_mixer_register(capture, path);
    live_audio_multi_capture_pcm_buffer_setup(capture, path);
    live_audio_multi_capture_encoder_setup(capture, path);
    live_audio_multi_capture_stream_buf_setup(capture, path);
    live_audio_multi_capture_analysis_init(capture);
    capture->audio_num = 1;
    list_add(&capture->entry, &g_audio_mixer_head);
    return capture;
}

static void live_audio_multi_capture_uninit(void *multi_capture)
{
    struct live_audio_multi_capture *capture = (struct live_audio_multi_capture *)multi_capture;

    if (!capture) {
        return;
    }
    if (--capture->audio_num > 0) {
        return;
    }

    live_stream_enter_critial(&capture->lock);
    live_audio_multi_capture_stop(capture);
    live_stream_exit_critial(&capture->lock);

    live_audio_multi_capture_encoder_stop(capture);

    live_audio_multi_capture_pcm_buffer_close(capture);
    live_audio_multi_capture_encoder_close(capture);
    live_audio_multi_capture_stream_buf_close(capture);
    live_stream_enter_critial(&capture->lock);
    live_audio_stream_node_close(&capture->stream_head);
    live_stream_exit_critial(&capture->lock);

    live_audio_multi_capture_analysis_close(capture);
    live_audio_mixer_unregister(capture->name);

    live_stream_enter_critial(&capture->lock);
    list_del(&capture->entry);
    live_stream_exit_critial(&capture->lock);
    free(capture);
}

static int live_audio_multi_capture_buffer_len(void *multi_capture)
{
    struct live_audio_multi_capture *capture = (struct live_audio_multi_capture *)multi_capture;

    if (capture->audio_buffer) {
        return live_audio_buffer_len(capture->audio_buffer);
    }

    return 0;
}

static int live_audio_multi_capture_use_frames(void *multi_capture)
{
    struct live_audio_multi_capture *capture = (struct live_audio_multi_capture *)multi_capture;

    return 0;
}

static int live_audio_multi_capture_read_data(void *multi_capture, void *buf, int len)
{
    struct live_audio_multi_capture *capture = (struct live_audio_multi_capture *)multi_capture;
    int rlen = 0;

    if (capture->audio_buffer) {
        if (live_audio_buffer_len(capture->audio_buffer) < len) {
            /* printf("stream : %d\n", live_audio_buffer_len(capture->audio_buffer)); */
            return 0;
        }
        rlen = live_audio_buffer_read(capture->audio_buffer, buf, len);
        live_audio_multi_capture_analysis_run(capture, rlen < len ? 1 : 0);
    }

    return rlen;
}

static void live_audio_capture_analysis_setup(struct live_audio_capture_context *ctx)
{
    if (CONFIG_LIVE_AUDIO_DYNAMIC_ANALYSIS == 1) {
        if (!ctx->multi_capture) {
            return;
        }
        struct live_audio_multi_capture *capture = (struct live_audio_multi_capture *)ctx->multi_capture;
        struct live_audio_multi_analysis *hdl = capture->analysis_hdl;
        int i = 0;

        for (i = 0; i < capture->audio_num; i++) {
            if (hdl->mix_ch_array[i]) {
                continue;
            }
            hdl->mix_ch_array[i] = ctx->mix_ch;
            break;
        }
    }
}

static void live_audio_capture_analysis_close(struct live_audio_capture_context *ctx)
{
    if (CONFIG_LIVE_AUDIO_DYNAMIC_ANALYSIS == 1) {
        if (!ctx->multi_capture) {
            return;
        }
        struct live_audio_multi_capture *capture = (struct live_audio_multi_capture *)ctx->multi_capture;
        struct live_audio_multi_analysis *hdl = capture->analysis_hdl;
        int i = 0;

        for (i = 0; i < capture->audio_num; i++) {
            if (hdl->mix_ch_array[i] == ctx->mix_ch) {
                hdl->mix_ch_array[i] = NULL;
                break;
            }
        }
    }
}

static void live_audio_multi_capture_analysis_init(struct live_audio_multi_capture *capture)
{
    if (CONFIG_LIVE_AUDIO_DYNAMIC_ANALYSIS == 1) {
        struct live_audio_multi_analysis *hdl = (struct live_audio_multi_analysis *)zalloc(sizeof(struct live_audio_multi_analysis));
        hdl->task_name = os_current_task();
        capture->analysis_hdl = hdl;
    }
}

static void live_audio_multi_capture_analysis_close(struct live_audio_multi_capture *capture)
{
    if (CONFIG_LIVE_AUDIO_DYNAMIC_ANALYSIS == 1) {
        if (capture->analysis_hdl) {
            free(capture->analysis_hdl);
            capture->analysis_hdl = NULL;
        }
    }
}

static void live_audio_multi_analysis_print(void *arg)
{
    struct live_audio_multi_capture *capture = (struct live_audio_multi_capture *)arg;
    struct live_audio_multi_analysis *hdl = capture->analysis_hdl;
    int i = 0;

    printf("stats run : %d\n", hdl->stats_run_num);
    for (i = 0; i < capture->audio_num; i++) {
        printf("[ch%d] underrun %d, overrun %d\n", i, hdl->underrun_stats[i], hdl->overrun_stats[i]);
    }

    if (capture->encoder) {
        live_stream_encoder_capacity_dump(capture->encoder);
    }
}

static void live_audio_multi_capture_analysis_run(struct live_audio_multi_capture *capture, u8 underrun)
{
    if (CONFIG_LIVE_AUDIO_DYNAMIC_ANALYSIS == 1) {
        if (underrun) {

        }
        struct live_audio_multi_analysis *hdl = capture->analysis_hdl;
        void *mix_ch = NULL;
        int i = 0, buffered_frames = 0, mixer_buffered_frames = 0;

        for (i = 0; i < capture->audio_num; i++) {
            mix_ch = hdl->mix_ch_array[i];
            if (!mix_ch) {
                continue;
            }
            mixer_buffered_frames = live_audio_buffered_frames_from_mixer(mix_ch);
            if (mixer_buffered_frames == 0) {
                /* r_printf("mix_channel 0x%x is no start",mix_ch);  */
            }
            buffered_frames =  mixer_buffered_frames - underrun ? 0 : CONFIG_LIVE_AUDIO_TX_FRAMES;
            if (buffered_frames <= CONFIG_LIVE_AUDIO_UNDERRUN_FRAMES) {
                hdl->underrun_stats[i]++;
            } else {
                hdl->overrun_stats[i]++;
            }
        }

        hdl->stats_run_num++;
        if (time_after(jiffies, hdl->output_timeout)) {
            int argv[4];
            argv[0] = (int)live_audio_multi_analysis_print;
            argv[1] = 1;
            argv[2] = (int)capture;
            os_taskq_post_type(hdl->task_name, Q_CALLBACK, 3, argv);
            hdl->output_timeout = jiffies + msecs_to_jiffies(5000);
        }
    }
}
