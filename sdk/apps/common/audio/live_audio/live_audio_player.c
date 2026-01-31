/*************************************************************************************************/
/*!
*  \file      live_audio_player.c
*
*  \brief
*
*  Copyright (c) 2011-2022 ZhuHai Jieli Technology Co.,Ltd.
*
*/
/*************************************************************************************************/
#include "live_audio_player.h"
#include "live_audio_buffer.h"
#include "live_stream_player.h"
#include "audio_stream.h"

struct live_audio_player_context {
    u8 state;
    u8 nch;
    int sample_rate;
    void *buffer;
    void *player;
    spinlock_t lock;
};

void live_audio_player_wakeup(void *priv)
{
    struct live_audio_player_context *ctx = (struct live_audio_player_context *)priv;

    spin_lock(&ctx->lock);

    live_stream_player_wakeup(ctx->player);

    spin_unlock(&ctx->lock);
}

int live_audio_player_buffer_setup(struct live_audio_player_context *ctx, struct audio_path *path)
{
    int buffer_size = path->fmt.bit_rate / 8 / 10;
    u8 point_offset = path->fmt.bit_width ? 4 : 2;

    if (path->fmt.coding_type == AUDIO_CODING_PCM) {
        buffer_size = path->fmt.sample_rate * path->fmt.channel * point_offset * 60 / 1000;
    }

    if (path->fmt.bit_width) {
        if (buffer_size < 1024) {
            buffer_size = 1024;
        }
    } else {
        if (buffer_size < 512) {
            buffer_size = 512;
        }
    }

    struct live_audio_buffer_params buf_params = {
        .underrun_wakeup = live_audio_player_wakeup,
        .underrun_wakeup_data = ctx,
        .size = buffer_size,
    };

    ctx->buffer = live_audio_buffer_init(&buf_params);

    return 0;
}

static void live_audio_player_buffer_close(struct live_audio_player_context *ctx)
{
    live_audio_buffer_close(ctx->buffer);
    ctx->buffer = NULL;
}

static int live_audio_player_setup(struct live_audio_player_context *ctx, struct audio_path *ipath, struct audio_path *opath)
{
    ipath->input.path = ctx->buffer;
    ipath->input.pull_frame = live_audio_buffer_pull_frame;
    ipath->input.free_frame = live_audio_buffer_free_frame;

    ctx->player = live_stream_player_open(ipath, opath);

    return 0;
}

void *live_audio_player_open(struct audio_path *ipath, struct audio_path *opath)
{
    struct live_audio_player_context *ctx = (struct live_audio_player_context *)zalloc(sizeof(struct live_audio_player_context));

    spin_lock_init(&ctx->lock);

    live_audio_player_buffer_setup(ctx, ipath);

    live_audio_player_setup(ctx, ipath, opath);

    ctx->sample_rate = ipath->fmt.sample_rate;
    ctx->nch = ipath->fmt.channel;
    return ctx;
}

int live_audio_capture_from_player(void *player, struct audio_path *path)
{
    struct live_audio_player_context *ctx = (struct live_audio_player_context *)player;

    printf("live audio capture from player.\n");
    return live_stream_player_capture(ctx->player, path);
}

void live_audio_capture_from_player_close(void *player)
{
    printf("live audio capture from player.\n");
    struct live_audio_player_context *ctx = (struct live_audio_player_context *)player;
    live_stream_player_capture_stop(ctx->player);
}

static void __live_audio_player_close(struct live_audio_player_context *ctx)
{
    live_stream_player_close(ctx->player);
    ctx->player = NULL;
}

void live_audio_player_close(void *player)
{
    struct live_audio_player_context *ctx = (struct live_audio_player_context *)player;

    __live_audio_player_close(ctx);

    live_audio_player_buffer_close(ctx);

    free(ctx);
}

int live_audio_player_push_data(void *player, void *data, int len, u32 timestamp)
{
    struct live_audio_player_context *ctx = (struct live_audio_player_context *)player;

    if (!ctx->buffer) {
        return 0;
    }

    struct audio_frame frame = {
        .sample_rate = ctx->sample_rate,
        .nch = ctx->nch,
        .data = data,
        .len = len,
        .timestamp = timestamp,
    };

    int wlen = live_audio_buffer_push_frame(ctx->buffer, &frame);
    if (wlen < len) {
        putchar('f');
    }
    return wlen;
}
