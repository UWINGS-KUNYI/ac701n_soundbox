/*************************************************************************************************/
/*!
*  \file      pcm_capture_buffer.c
*
*  \brief
*
*  Copyright (c) 2011-2023 ZhuHai Jieli Technology Co.,Ltd.
*
*/
/*************************************************************************************************/
#include "pcm_capture_buffer.h"

extern const int global_bit_wide;
struct live_audio_capture_buffer {
    u8 remain;
    void *audio_buffer;
    void *wakeup_data;
    void (*wakeup)(void *);
    void *path;
    int (*write_frame)(void *path, struct audio_frame *frame);
};

void *live_audio_pcm_capture_buf_open(int size,
                                      void *wakeup_data,
                                      void (*wakeup)(void *),
                                      void *path,
                                      int (*write_frame)(void *path, struct audio_frame *frame))
{
    struct live_audio_capture_buffer *capture_buf = (struct live_audio_capture_buffer *)zalloc(sizeof(struct live_audio_capture_buffer));

    struct live_audio_buffer_params buf_params = {
        .overrun_wakeup_data = capture_buf,
        .overrun_wakeup = live_audio_pcm_capture_buf_wakeup,
        .size = size,
    };

    capture_buf->audio_buffer = live_audio_buffer_init(&buf_params);
    capture_buf->wakeup_data = wakeup_data;
    capture_buf->wakeup = wakeup;
    capture_buf->path = path;
    capture_buf->write_frame = write_frame;

    printf("live_audio_pcm_capture_buf_open buffer 0x%x \n", capture_buf->audio_buffer);
    return capture_buf;
}

void live_audio_pcm_capture_buf_close(void *buffer)
{
    struct live_audio_capture_buffer *capture_buf = (struct live_audio_capture_buffer *)buffer;

    if (!capture_buf) {
        return;
    }
    if (capture_buf->audio_buffer) {
        live_audio_buffer_close(capture_buf->audio_buffer);
    }

    free(capture_buf);
}

void live_audio_pcm_capture_buf_wakeup(void *buffer)
{
    struct live_audio_capture_buffer *capture_buf = (struct live_audio_capture_buffer *)buffer;

    if (capture_buf->wakeup) {
        capture_buf->wakeup(capture_buf->wakeup_data);
    }
}

int live_audio_pcm_capture_write_frame(void *buffer, struct audio_frame *frame)
{
    struct live_audio_capture_buffer *capture_buf = (struct live_audio_capture_buffer *)buffer;
    int wlen = 0;

    if (!capture_buf->remain) {
        wlen = live_audio_buffer_push_frame(capture_buf->audio_buffer, frame);
        if (frame->offset + wlen < frame->len) {
            return wlen;
        }
    }

    if (capture_buf->write_frame) {
        if (global_bit_wide) {
            //jla编码一路的数据做24转16bit给mixer，capture_mixer节点适配24bit后去掉
            extern void audio_convert_data_32bit_to_16bit_round(s32 * in, s16 * out, u32 npoint);
            audio_convert_data_32bit_to_16bit_round((s32 *)frame->data, (s16 *)frame->data, frame->len >> 2);
            frame->len >>= 1;
        }
        wlen = capture_buf->write_frame(capture_buf->path, frame);
        frame->offset += wlen;
        capture_buf->remain = frame->offset == frame->len ? 0 : 1;
        wlen = global_bit_wide ? wlen * 2 : wlen;
    }

    return wlen;
}

int live_audio_pcm_capture_buf_read(void *buffer, void *data, int len)
{
    struct live_audio_capture_buffer *capture_buf = (struct live_audio_capture_buffer *)buffer;

    if (capture_buf->audio_buffer) {
        return live_audio_buffer_read(capture_buf->audio_buffer, data, len);
    }
    return 0;
}
