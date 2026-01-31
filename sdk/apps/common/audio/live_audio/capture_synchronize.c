/****************************************************************/
/* Copyright:(c)JIELI  2011-2022 All Rights Reserved.           */
/****************************************************************/
/*
>file name : capture_synchronize.c
>create time : Fri 10 Jun 2022 04:01:52 PM CST
*****************************************************************/
#include "system/includes.h"
#include "media/drift_compensation.h"
#include "media/delay_compensation.h"
#include "media/audio_syncts.h"
#include "capture_synchronize.h"

#define DMA_INPUT_TO_TASK_MSG                   1

#define LIVE_CAP_SYNC_MSG_DMA_FRAME             0
#define LIVE_CAP_SYNC_MSG_STOP                  1

#define SYNCHRONIZE_TASK                "bcsync"

DEFINE_ATOMIC(task_used);

struct live_capture_synchronize_context {
    u8 task_create;
    u8 new_frame;
    u8 nch;
    u8 task_enable;
    u8 bit_width;
    int delay_time;
    u16 remain;
    int send_sample_rate;
    int sample_rate;
    void *drift_compensation;
    void *delay_compensation;
    void *syncts;
    u32 send_timestamp;
    u8 dma_frames;
    spinlock_t lock;
    void *opath;
    void *ipath;
    int (*opath_frame_handler)(void *opath, struct audio_frame *frame);
    void (*ipath_wakeup)(void *opath);
};

static int audio_capture_drift_compensation_init(struct live_capture_synchronize_context *ctx)
{
    struct pcm_sample_params params;
    params.sample_rate = ctx->sample_rate;
    params.unit = CLOCK_TIME_US;
    params.factor = 1;
    params.period = 5000000; //5秒
    params.nch = ctx->nch;
    ctx->drift_compensation = audio_drift_compensation_open(&params);

    return 0;
}

static void audio_capture_drift_compensation_free(struct live_capture_synchronize_context *ctx)
{
    if (ctx->drift_compensation) {
        audio_drift_compensation_close(ctx->drift_compensation);
        ctx->drift_compensation = NULL;
    }
}

static int audio_stream_syncts_data_handler(void *priv, void *data, int len)
{
    struct live_capture_synchronize_context *ctx = (struct live_capture_synchronize_context *)priv;

    if (ctx->opath_frame_handler) {
        struct audio_frame frame = {
            .nch = ctx->nch,
            .coding_type = AUDIO_CODING_PCM,
            .sample_rate = ctx->send_sample_rate,
            .mode = AUDIO_FRAME_LIVE_STREAM,
            .len = len,
            .data = data,
        };
        return ctx->opath_frame_handler(ctx->opath, &frame);
    }

    return len;
}

static int audio_capture_syncts_init(struct live_capture_synchronize_context *ctx)
{
    struct audio_syncts_params params = {0};

    params.network = AUDIO_NETWORK_LOCAL;
    params.pcm_device = PCM_TRANSMIT;
    params.factor = 1;
    params.nch = ctx->nch;
    params.rin_sample_rate = ctx->send_sample_rate;
    params.rout_sample_rate = ctx->send_sample_rate;
    params.priv = ctx;
    params.output = audio_stream_syncts_data_handler;
    params.bit_mode = ctx->bit_width;
    return audio_syncts_open(&ctx->syncts, &params);
}

static void audio_capture_syncts_free(struct live_capture_synchronize_context *ctx)
{
    if (ctx->syncts) {
        audio_syncts_close(ctx->syncts);
        ctx->syncts = NULL;
    }
}

/***********************************************************
 * audio capture delay compensation init
 * description  : 广播延时控制器的初始
 * arguments    : ctx   -   广播音频同步的总数据结构
 * return       : 0     -   sucess,
 *                非0   -   failed
 * notes        : 应用于广播方案的延时控制策略
 ***********************************************************/
static int audio_capture_delay_compensation_init(struct live_capture_synchronize_context *ctx, u8 frame_mode)
{
    if (frame_mode == AUDIO_FRAME_DMA_STREAM) {
        int delay_time = ctx->delay_time > 1000 ? ctx->delay_time : ctx->delay_time * 1000;
        ctx->delay_compensation = audio_delay_us_compensation_open(delay_time, ctx->sample_rate, PERIOD_DELAY_MODE);
    } else if (frame_mode == AUDIO_FRAME_LIVE_STREAM) {
        ctx->delay_compensation = audio_delay_compensation_open(ctx->delay_time, ctx->sample_rate, AVERAGE_DELAY_MODE);
    }

    return 0;
}

static void audio_capture_delay_compensation_free(struct live_capture_synchronize_context *ctx)
{
    if (ctx->delay_compensation) {
        audio_delay_compensation_close(ctx->delay_compensation);
        ctx->delay_compensation = NULL;
    }
}

static int live_dma_frame_synchronize_handler(struct live_capture_synchronize_context *ctx, struct audio_frame *frame)
{
    int sample_rate = ctx->sample_rate;
    int sample_rate_offset = 0;
    int buffered_time_us = 0;
    int delay_time = 0;
    int ret = frame->len;
    u32  drift_len = (ctx->bit_width ? frame->len >> 1 : frame->len);
    /*如果需要漂移补偿，则在此进行漂移补偿后的采样率获取*/
    if (ctx->drift_compensation) {
        sample_rate = audio_drift_samples_to_reference_clock(ctx->drift_compensation, drift_len, frame->timestamp);
    }

    if (ctx->send_timestamp) {
        spin_lock(&ctx->lock);
        buffered_time_us = ((drift_len >> 1) / frame->nch * 1000000) / frame->sample_rate + sound_buffered_between_syncts_and_device(ctx->syncts, 1);
        delay_time = (buffered_time_us + (ctx->send_timestamp - frame->timestamp));// / 1000;
        if (delay_time && __builtin_abs(delay_time) < 0x1ffffff) { //需要考虑时钟溢出的情况
            sample_rate_offset = audio_delay_compensation_detect(ctx->delay_compensation, delay_time);
            sample_rate += sample_rate_offset;
            if (ctx->sample_rate != sample_rate) {
                /*printf("<%d, %d, %d, %d>\n", delay_time, buffered_time_us, ctx->send_timestamp, frame->timestamp);*/
            }
        }
        spin_unlock(&ctx->lock);
    }
    /*变采样*/
    if (!ctx->syncts) {
        return 0;
    }
    if (sample_rate && (ctx->sample_rate != sample_rate)) {
        /*printf("---->>> sample rate : %d, %d-----\n", ctx->sample_rate, sample_rate);*/
        audio_syncts_update_sample_rate(ctx->syncts, sample_rate);
        ctx->sample_rate = sample_rate;
    }
    int remain_len = frame->len;
    int wlen = 0;
    u8 *data = frame->data;
    while (remain_len) {
        wlen = audio_syncts_frame_filter(ctx->syncts, data, remain_len);
        if (wlen == 0) {
            break;
        }
        data = (u8 *)data + wlen;
        remain_len -= wlen;
    }
    audio_syncts_push_data_out(ctx->syncts);

    spin_lock(&ctx->lock);
    free(frame);
    ctx->dma_frames--;
    spin_unlock(&ctx->lock);

    return ret;
}

static int live_stream_frame_synchronize_handler(struct live_capture_synchronize_context *ctx,
        struct audio_frame *frame)
{
    int sample_rate = ctx->sample_rate;
    int sample_rate_offset = 0;
    int buffered_time_us = 0;
    int delay_time = 0;

    if (frame->mode == AUDIO_FRAME_FILE_STREAM) {
        sample_rate = frame->sample_rate; //文件广播按数据帧的采样率做同步;
    }

    if (!ctx->remain && ctx->delay_compensation) {
        buffered_time_us = frame->delay * 1000 + sound_buffered_between_syncts_and_device(ctx->syncts, 1);
        delay_time = (buffered_time_us + (ctx->send_timestamp - frame->timestamp)) / 1000;
        if (ctx->send_timestamp && __builtin_abs(delay_time) < (0x1ffffff / 1000)) { //需要考虑时钟溢出的情况
            sample_rate_offset = audio_delay_compensation_detect(ctx->delay_compensation, delay_time);
            if (sample_rate_offset) {
                sample_rate = frame->sample_rate + sample_rate_offset;
            }
        }
    }

    if (!ctx->syncts) {
        return 0;
    }

    if (ctx->sample_rate != sample_rate) {
        /*printf("---->>> sample rate : %d, %d-----\n", ctx->sample_rate, sample_rate);*/
        audio_syncts_update_sample_rate(ctx->syncts, sample_rate);
        ctx->sample_rate = sample_rate;
    }

    int len = frame->len - frame->offset;
    int wlen = 0;
    wlen = audio_syncts_frame_filter(ctx->syncts, (u8 *)frame->data + frame->offset, len);
    if (wlen < len) {
        audio_syncts_trigger_resume(ctx->syncts, ctx->ipath, ctx->ipath_wakeup);
    }
    /*printf("0x%x, %d, %d\n", (u32)frame->data, len, wlen);*/
    ctx->remain = wlen == len ? 0 : 1;
    return wlen;
}

static int live_stream_synchronize_frame_handler(void *synchronize, struct audio_frame *frame)
{
    struct live_capture_synchronize_context *ctx = (struct live_capture_synchronize_context *)synchronize;

    if (ctx->sample_rate == 0) {
        ctx->sample_rate = frame->sample_rate;
        audio_syncts_update_sample_rate(ctx->syncts, ctx->sample_rate);
    }
    if (frame->mode == AUDIO_FRAME_DMA_STREAM && !ctx->drift_compensation) {
        audio_capture_drift_compensation_init(ctx);
    }

    if (frame->mode != AUDIO_FRAME_FILE_STREAM && !ctx->delay_compensation) {
        audio_capture_delay_compensation_init(ctx, frame->mode);
    }

    if (frame->mode == AUDIO_FRAME_DMA_STREAM) {
        return live_dma_frame_synchronize_handler(ctx, frame);
    }

    if (frame->mode == AUDIO_FRAME_LIVE_STREAM || frame->mode == AUDIO_FRAME_FILE_STREAM) {
        return live_stream_frame_synchronize_handler(ctx, frame);
    }

    return frame->len;
}

static void capture_synchronize_task(void *arg)
{
    int msg[16];
    int res;
    u8 pend_taskq = 1;

    while (1) {
        res = os_taskq_pend("taskq", msg, ARRAY_SIZE(msg));

        if (res == OS_TASKQ) {
            switch (msg[1]) {
            case LIVE_CAP_SYNC_MSG_DMA_FRAME:
                live_stream_synchronize_frame_handler((void *)msg[2], (struct audio_frame *)msg[3]);
                break;
            case LIVE_CAP_SYNC_MSG_STOP:
                os_sem_post((OS_SEM *)msg[2]);
                break;
            }
        }
    }
}

static void capture_synchronize_task_create(struct live_capture_synchronize_context *ctx)
{
    if (atomic_add_return(1, &task_used) == 1) {
        int err = task_create(capture_synchronize_task, NULL, SYNCHRONIZE_TASK);
        if (err) {
            printf("!!broadcast up audio sync task create failed.\n");
        }
    }
    ctx->task_enable = 1;
}

static void capture_synchronize_task_exit(struct live_capture_synchronize_context *ctx)
{
    if (!ctx->task_enable) {
        return;
    }

    if (atomic_sub_return(1, &task_used) == 0) {
        OS_SEM *sem = (OS_SEM *)malloc(sizeof(OS_SEM));
        os_sem_create(sem, 0);
        os_taskq_post_msg(SYNCHRONIZE_TASK, 2, LIVE_CAP_SYNC_MSG_STOP, (int)sem);
        os_sem_pend(sem, 0);
        free(sem);
        task_kill(SYNCHRONIZE_TASK);
    }

}

void *live_stream_capture_synchronize_open(struct audio_path *path)
{
    struct live_capture_synchronize_context *ctx = (struct live_capture_synchronize_context *)zalloc(sizeof(struct live_capture_synchronize_context));

    ctx->send_sample_rate = path->fmt.sample_rate;
    ctx->nch = path->fmt.channel;
    ctx->opath = path->output.path;
    ctx->opath_frame_handler = path->output.write_frame;
    ctx->ipath = path->input.path;
    ctx->ipath_wakeup = path->input.wakeup;
    ctx->delay_time = path->delay_time;
    ctx->bit_width = path->fmt.bit_width;

    /*audio_capture_drift_compensation_init(ctx);*/

    /*audio_capture_delay_compensation_init(ctx);*/

    audio_capture_syncts_init(ctx);

    spin_lock_init(&ctx->lock);

    return ctx;
}

void live_stream_capture_synchronize_use_frames(void *synchronize, int frames)
{
    struct live_capture_synchronize_context *ctx = (struct live_capture_synchronize_context *)synchronize;

    if (!ctx) {
        return;
    }
    spin_lock(&ctx->lock);
    if (ctx->syncts) {
        sound_pcm_update_frame_num(ctx->syncts, frames);
    }
    spin_unlock(&ctx->lock);
}

void live_stream_capture_send_frames(void *synchronize, int time, u32 timestamp)
{
    struct live_capture_synchronize_context *ctx = (struct live_capture_synchronize_context *)synchronize;

    if (ctx->syncts) {
        spin_lock(&ctx->lock);
        if (ctx->send_timestamp == timestamp) {
            /* spin_unlock(&ctx->lock); */
            putchar('>');
            /* return; */
        }
        int frames = ctx->send_sample_rate * time / 1000;
        sound_pcm_update_frame_num(ctx->syncts, frames);
        ctx->send_timestamp = timestamp;
        spin_unlock(&ctx->lock);
    }
}

int live_stream_capture_synchronize_handler(void *synchronize, struct audio_frame *frame)
{
    struct live_capture_synchronize_context *ctx = (struct live_capture_synchronize_context *)synchronize;

    if (frame->mode == AUDIO_FRAME_DMA_STREAM) {
        if (!ctx->task_enable) {
            capture_synchronize_task_create(ctx);
        }

        spin_lock(&ctx->lock);
        if (ctx->dma_frames >= 5) {
            spin_unlock(&ctx->lock);
            return 0;
        }
        struct audio_frame *synchronize_frame = (struct audio_frame *)zalloc(sizeof(struct audio_frame) + frame->len);
        memcpy(synchronize_frame, frame, sizeof(struct audio_frame));
        synchronize_frame->data = (void *)(synchronize_frame + 1);
        memcpy(synchronize_frame->data, frame->data, frame->len);
        ctx->dma_frames++;
        spin_unlock(&ctx->lock);
        int err = os_taskq_post_msg(SYNCHRONIZE_TASK, 3, LIVE_CAP_SYNC_MSG_DMA_FRAME, (int)ctx, (int)synchronize_frame);
        if (err) {
            r_printf("dma frame input error %d\n", err);
        }
        return frame->len;

    }
    return live_stream_synchronize_frame_handler(ctx, frame);
}


void live_stream_capture_synchronize_close(void *synchronize)
{
    struct live_capture_synchronize_context *ctx = (struct live_capture_synchronize_context *)synchronize;

    capture_synchronize_task_exit(ctx);
    audio_capture_drift_compensation_free(ctx);
    audio_capture_delay_compensation_free(ctx);
    audio_capture_syncts_free(ctx);

    if (ctx) {
        free(ctx);
    }
}

void live_stream_capture_synchronize_wakeup(void *synchronize)
{
    struct live_capture_synchronize_context *ctx = (struct live_capture_synchronize_context *)synchronize;

    if (ctx->ipath_wakeup) {
        ctx->ipath_wakeup(ctx->ipath);
    }
}

