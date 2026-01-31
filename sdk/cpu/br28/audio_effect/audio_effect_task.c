/*************************************************************************************************/
/*!
*  \file      audio_effect_task.c
*
*  \brief
*
*  Copyright (c) 2011-2023 ZhuHai Jieli Technology Co.,Ltd.
*
*/
/*************************************************************************************************/
#include "audio_effect_task.h"
#include "os/os_api.h"
#include "system/spinlock.h"
#include "system/task.h"
#include "generic/atomic.h"

#define AUDIO_EFFECT_TASK_RUN       0
#define AUDIO_EFFECT_TASK_FLUSH     1
#define AUDIO_EFFECT_TASK_STOP      2

#define AUDIO_EFFECT_BUF_SIZE       (4 * 1024)
#define AUDIO_EFFECT_FRAME_SIZE     (1 * 1024)
struct audio_effect_task_context {
    const char *name;
    u8 nch;
    u8 stream_pend;
    int sample_rate;
    void *input_buf;
    void *output_buf;
    cbuffer_t input_cbuf;
    cbuffer_t output_cbuf;
    struct audio_stream_entry input_entry;
    struct audio_stream_entry output_entry;
    s16 input_remain;
    s16 input_process_len;
    s16 input_offset;
    s16 output_remain;
    s16 output_process_len;
    s16 output_offset;
    u8 *input_frame_data;
    u8 *output_frame_data;
    spinlock_t lock;
};

static atomic_t task_used = {0};

static int audio_effect_task_input_data_handler(struct audio_stream_entry *entry,
        struct audio_data_frame *in,
        struct audio_data_frame *out);
static int audio_effect_task_output_data_handler(struct audio_stream_entry *entry,
        struct audio_data_frame *in,
        struct audio_data_frame *out);
static void audio_effect_task_input_process_len(struct audio_stream_entry *entry, int len);
static void audio_effect_task_output_process_len(struct audio_stream_entry *entry, int len);

static int audio_effect_task_data_handler(void *priv)
{
    struct audio_effect_task_context *ctx = (struct audio_effect_task_context *)priv;

    struct audio_data_frame in = {0};
    do {
        if (ctx->input_remain) {
            in.data = (s16 *)(ctx->input_frame_data + ctx->input_offset);
            in.data_len = ctx->input_remain;
            in.sample_rate = ctx->sample_rate;
            in.channel = ctx->nch;
            in.offset = 0;
            audio_stream_run(&ctx->input_entry, &in);
            ctx->input_remain -= ctx->input_process_len;
            ctx->input_offset += ctx->input_process_len;
            if (ctx->input_remain) {
                break;
            }
            cbuf_read_updata(&ctx->input_cbuf, ctx->input_offset);
        }

        spin_lock(&ctx->lock);
        if (ctx->stream_pend) {
            ctx->stream_pend = 0;
            audio_stream_resume(&ctx->input_entry);
        }
        spin_unlock(&ctx->lock);

        if (cbuf_get_data_len(&ctx->input_cbuf) <= AUDIO_EFFECT_FRAME_SIZE) {
            break;
        }

        ctx->input_frame_data = cbuf_get_readptr(&ctx->input_cbuf);
        ctx->input_remain = AUDIO_EFFECT_FRAME_SIZE;
        ctx->input_offset = 0;
    } while (1);

    return 0;
}

static void audio_effect_task(void *arg)
{
    u8 pend = 1;
    int msg[16];
    int res;
    while (1) {
        if (pend) {
            res = os_taskq_pend("taskq", msg, ARRAY_SIZE(msg));
        } else {
            res = os_taskq_accept(ARRAY_SIZE(msg), msg);
        }

        if (res == OS_TASKQ) {
            switch (msg[1]) {
            case AUDIO_EFFECT_TASK_RUN:
                audio_effect_task_data_handler((void *)msg[2]);
                break;
            case AUDIO_EFFECT_TASK_FLUSH:
                /*audio_effect_task_data_flush((void *)msg[2]);*/
                break;
            case AUDIO_EFFECT_TASK_STOP:
                os_sem_post((OS_SEM *)msg[2]);
                break;
            default:
                break;
            }
        }
    }
}

struct audio_stream_entry *audio_effect_task_open(void)
{
    struct audio_effect_task_context *ctx = (struct audio_effect_task_context *)zalloc(sizeof(struct audio_effect_task_context));

    if (!ctx) {
        return NULL;
    }

    ctx->input_buf = zalloc(AUDIO_EFFECT_BUF_SIZE);
    if (ctx->input_buf) {
        cbuf_init(&ctx->input_cbuf, ctx->input_buf, AUDIO_EFFECT_BUF_SIZE);
    }
    ctx->output_buf = zalloc(AUDIO_EFFECT_BUF_SIZE);
    if (ctx->output_buf) {
        cbuf_init(&ctx->output_cbuf, ctx->output_buf, AUDIO_EFFECT_BUF_SIZE);
    }

    spin_lock_init(&ctx->lock);

    ctx->input_entry.data_handler = audio_effect_task_input_data_handler;
    ctx->input_entry.data_process_len = audio_effect_task_input_process_len;

    if (atomic_inc_return(&task_used) == 1) {
        task_create(audio_effect_task, NULL, "aud_effect");
    }
    return &ctx->input_entry;
}

void audio_effect_task_close(struct audio_stream_entry *entry)
{
    struct audio_effect_task_context *ctx = container_of(entry, struct audio_effect_task_context, input_entry);

    OS_SEM *sem = (OS_SEM *)malloc(sizeof(OS_SEM));
    os_sem_create(sem, 0);
    while (os_taskq_post_msg("aud_effect", 2, AUDIO_EFFECT_TASK_STOP, (int)sem)) {
        os_time_dly(2);
    }
    os_sem_pend(sem, 0);
    free(sem);
    if (atomic_dec_return(&task_used) == 0) {
        task_kill("aud_effect");
    }

    audio_stream_del_entry(&ctx->input_entry);

    if (ctx->output_entry.input) {
        audio_stream_del_entry(&ctx->output_entry);
    }
    if (ctx->input_buf) {
        free(ctx->input_buf);
    }

    if (ctx->output_buf) {
        free(ctx->output_buf);
    }

    free(ctx);
}

struct audio_stream_entry *audio_effect_task_get_output_entry(struct audio_stream_entry *entry)
{
    struct audio_effect_task_context *ctx = container_of(entry, struct audio_effect_task_context, input_entry);

    ctx->output_entry.data_handler = audio_effect_task_output_data_handler;
    ctx->output_entry.data_process_len = audio_effect_task_output_process_len;

    return &ctx->output_entry;
}


static int audio_effect_task_input_data_handler(struct audio_stream_entry *entry,
        struct audio_data_frame *in,
        struct audio_data_frame *out)
{
    struct audio_effect_task_context *ctx = container_of(entry, struct audio_effect_task_context, input_entry);

    out->no_subsequent = 1;

    ctx->nch = in->channel;
    ctx->sample_rate = in->sample_rate;
    while (1) {
        if (!ctx->output_remain) {
            if (cbuf_get_data_len(&ctx->output_cbuf) <  AUDIO_EFFECT_FRAME_SIZE) {
                break;
            }
            ctx->output_frame_data = cbuf_get_readptr(&ctx->output_cbuf);
            ctx->output_offset = 0;
            ctx->output_remain = AUDIO_EFFECT_FRAME_SIZE;
        }
        struct audio_data_frame frame = {
            .channel = ctx->nch,
            .stop = in->stop,
            .sample_rate = ctx->sample_rate,
            .data = (s16 *)(ctx->output_frame_data + ctx->output_offset),
            .data_len = ctx->output_remain,
        };
        audio_stream_run(&ctx->output_entry, &frame);
        ctx->output_remain -= ctx->output_process_len;
        ctx->output_offset += ctx->output_process_len;
        if (ctx->output_remain) {
            break;
        }
        cbuf_read_updata(&ctx->output_cbuf, ctx->output_offset);
    }

    u32 free_size = 0;
    cbuf_write_alloc(&ctx->input_cbuf, &free_size);
    int wlen = (in->data_len - in->offset) > free_size ? free_size : (in->data_len - in->offset);
    wlen = cbuf_write(&ctx->input_cbuf, (u8 *)in->data + in->offset, wlen);
    spin_lock(&ctx->lock);
    if (wlen < (in->data_len - in->offset)) {
        ctx->stream_pend = 1;
    }
    spin_unlock(&ctx->lock);
    os_taskq_post_msg("aud_effect", 2, AUDIO_EFFECT_TASK_RUN, (int)ctx);
    if (!ctx->output_remain) {
        if (cbuf_get_data_len(&ctx->output_cbuf) >= AUDIO_EFFECT_FRAME_SIZE) {
            ctx->output_frame_data = cbuf_get_readptr(&ctx->output_cbuf);
            ctx->output_offset = 0;
            ctx->output_remain = AUDIO_EFFECT_FRAME_SIZE;
        }
    }

    return wlen;
}

static void audio_effect_task_input_process_len(struct audio_stream_entry *entry, int len)
{
    struct audio_effect_task_context *ctx = container_of(entry, struct audio_effect_task_context, input_entry);

    ctx->input_process_len = len;
}

static int audio_effect_task_output_data_handler(struct audio_stream_entry *entry,
        struct audio_data_frame *in,
        struct audio_data_frame *out)
{
    struct audio_effect_task_context *ctx = container_of(entry, struct audio_effect_task_context, output_entry);

    out->no_subsequent = 1;

    u32 free_size = 0;
    cbuf_write_alloc(&ctx->output_cbuf, &free_size);
    int wlen = (in->data_len - in->offset) > free_size ? free_size : (in->data_len - in->offset);

    wlen = cbuf_write(&ctx->output_cbuf, (u8 *)in->data + in->offset, wlen);
    if (wlen < (in->data_len - in->offset)) {

    }

#if 0
    if (ctx->output_remain) {
        return wlen;
    }
    if (cbuf_get_data_len(&ctx->output_cbuf) >= AUDIO_EFFECT_FRAME_SIZE) {
        ctx->output_frame_data = cbuf_get_readptr(&ctx->output_cbuf);
        ctx->output_offset = 0;
        ctx->output_remain = AUDIO_EFFECT_FRAME_SIZE;
    }
#endif
    return wlen;
}

static void audio_effect_task_output_process_len(struct audio_stream_entry *entry, int len)
{
    struct audio_effect_task_context *ctx = container_of(entry, struct audio_effect_task_context, output_entry);

    ctx->output_process_len = len;
}
