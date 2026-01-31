/*************************************************************************************************/
/*!
*  \file      live_audio_buffer.c
*
*  \brief
*
*  Copyright (c) 2011-2022 ZhuHai Jieli Technology Co.,Ltd.
*
*/
/*************************************************************************************************/
#include "audio_stream.h"
#include "live_audio_buffer.h"

#define AUDIO_BUFFER_UNDERRUN   0x1
#define AUDIO_BUFFER_OVERRUN    0x2
struct live_audio_frame {
    struct list_head entry;
    u8 deleted;
    struct audio_frame frame;
};

struct live_audio_buffer {
    volatile u8 state;
    void *overrun_wakeup_data;
    void *underrun_wakeup_data;
    void (*overrun_wakeup)(void *);
    void (*underrun_wakeup)(void *);
    int max_size;
    int buffered_len;
    struct list_head frame_head;
    spinlock_t lock;
};

void *live_audio_buffer_init(struct live_audio_buffer_params *params)
{
    struct live_audio_buffer *audio_buf = (struct live_audio_buffer *)zalloc(sizeof(struct live_audio_buffer));

    INIT_LIST_HEAD(&audio_buf->frame_head);
    spin_lock_init(&audio_buf->lock);
    audio_buf->max_size = params->size;
    audio_buf->overrun_wakeup_data = params->overrun_wakeup_data;
    audio_buf->underrun_wakeup_data = params->underrun_wakeup_data;
    audio_buf->overrun_wakeup = params->overrun_wakeup;
    audio_buf->underrun_wakeup = params->underrun_wakeup;


    return audio_buf;
}

void live_audio_buffer_close(void *audio_buffer)
{
    struct live_audio_buffer *audio_buf = (struct live_audio_buffer *)audio_buffer;

    if (!audio_buf) {
        return;
    }

    struct live_audio_frame *frame, *n;

    spin_lock(&audio_buf->lock);
    list_for_each_entry_safe(frame, n, &audio_buf->frame_head, entry) {
        __list_del_entry(&frame->entry);
        free(frame);
    }
    spin_unlock(&audio_buf->lock);

    free(audio_buf);
}

static struct live_audio_frame *new_live_audio_frame(struct live_audio_buffer *audio_buf, int size)
{
    if (audio_buf->buffered_len + size > audio_buf->max_size) {
        return NULL;
    }

    struct live_audio_frame *frame = (struct live_audio_frame *)malloc(sizeof(struct live_audio_frame) + size);
    if (!frame) {
        return NULL;
    }

    frame->frame.data = (void *)(frame + 1);
    frame->frame.len = size;
    frame->frame.offset = 0;
    list_add_tail(&frame->entry, &audio_buf->frame_head);

    return frame;
}

int live_audio_buffer_read(void *audio_buffer, void *data, int len)
{
    struct live_audio_buffer *audio_buf = (struct live_audio_buffer *)audio_buffer;
    struct live_audio_frame *frame, *n;
    int offset = 0;

    spin_lock(&audio_buf->lock);
    list_for_each_entry_safe(frame, n, &audio_buf->frame_head, entry) {
        int rlen = frame->frame.len - frame->frame.offset;
        if (rlen > len) {
            rlen = len;
        }
        memcpy((u8 *)data + offset, (u8 *)frame->frame.data + frame->frame.offset, rlen);
        frame->frame.offset += rlen;
        audio_buf->buffered_len -= rlen;
        if (frame->frame.offset == frame->frame.len) {
            __list_del_entry(&frame->entry);
            free(frame);
        }
        offset += rlen;
        len -= rlen;
        if (len == 0) {
            break;
        }
    }
    if (offset < len) {
        audio_buf->state |= AUDIO_BUFFER_UNDERRUN;
    }

    if ((audio_buf->state & AUDIO_BUFFER_OVERRUN) && audio_buf->overrun_wakeup) {
        audio_buf->state &= ~AUDIO_BUFFER_OVERRUN;
        audio_buf->overrun_wakeup(audio_buf->overrun_wakeup_data);
    }
    spin_unlock(&audio_buf->lock);

    return offset;
}

int live_audio_buffer_read_frame(void *audio_buffer, struct audio_frame *frame, int len)
{
    struct live_audio_buffer *audio_buf = (struct live_audio_buffer *)audio_buffer;
    int rlen = 0;

    /*printf("-read : %d-\n", len);*/
    spin_lock(&audio_buf->lock);
    if (audio_buf->buffered_len < len) {
        audio_buf->state |= AUDIO_BUFFER_UNDERRUN;
        spin_unlock(&audio_buf->lock);
        frame->len = 0;
        return 0;
    }
    spin_unlock(&audio_buf->lock);
    rlen = live_audio_buffer_read(audio_buf, frame->data, len);
    return rlen;
}

int live_audio_buffer_write(void *audio_buffer, void *data, int len)
{
    struct live_audio_buffer *audio_buf = (struct live_audio_buffer *)audio_buffer;
    struct live_audio_frame *frame = NULL;

    spin_lock(&audio_buf->lock);
    frame = new_live_audio_frame(audio_buf, len);
    if (!frame) {
        audio_buf->state |= AUDIO_BUFFER_OVERRUN;
        spin_unlock(&audio_buf->lock);
        return 0;
    }
    memcpy(frame->frame.data, data, len);
    audio_buf->buffered_len += len;
    if ((audio_buf->state & AUDIO_BUFFER_UNDERRUN) && audio_buf->underrun_wakeup) {
        audio_buf->state &= ~AUDIO_BUFFER_UNDERRUN;
        audio_buf->underrun_wakeup(audio_buf->underrun_wakeup_data);
    }
    spin_unlock(&audio_buf->lock);

    return len;
}

int live_audio_buffer_push_frame(void *audio_buffer, struct audio_frame *audio_frame)
{
    struct live_audio_buffer *audio_buf = (struct live_audio_buffer *)audio_buffer;
    struct live_audio_frame *frame = NULL;

    /*printf("push : 0x%x, %d\n", (u32)audio_frame->data, audio_frame->len);*/
    spin_lock(&audio_buf->lock);
    frame = new_live_audio_frame(audio_buf, audio_frame->len);
    if (!frame) {
        audio_buf->state |= AUDIO_BUFFER_OVERRUN;
        spin_unlock(&audio_buf->lock);
        return 0;
    }
    memcpy(&frame->frame, audio_frame, sizeof(struct audio_frame));
    frame->frame.data = (void *)(frame + 1);
    memcpy(frame->frame.data, audio_frame->data, audio_frame->len);
    if ((audio_buf->state & AUDIO_BUFFER_UNDERRUN) && audio_buf->underrun_wakeup) {
        audio_buf->state &= ~AUDIO_BUFFER_UNDERRUN;
        audio_buf->underrun_wakeup(audio_buf->underrun_wakeup_data);
    }
    audio_buf->buffered_len += audio_frame->len;
    spin_unlock(&audio_buf->lock);

    /*printf("audio buf : %d\n", audio_frame->len - audio_frame->offset);*/
    return audio_frame->len - audio_frame->offset;
}


static struct audio_frame *__live_audio_buffer_pull_frame(void *audio_buffer, u8 delete)
{
    struct live_audio_buffer *audio_buf = (struct live_audio_buffer *)audio_buffer;

    struct live_audio_frame *frame = NULL;

    spin_lock(&audio_buf->lock);

    if (list_empty(&audio_buf->frame_head)) {
        audio_buf->state |= AUDIO_BUFFER_UNDERRUN;
        spin_unlock(&audio_buf->lock);
        return NULL;
    }
    frame = list_first_entry(&audio_buf->frame_head, struct live_audio_frame, entry);
    if (delete) {
        list_del(&frame->entry);
        frame->deleted = 1;
    }

    if ((audio_buf->state & AUDIO_BUFFER_OVERRUN) && audio_buf->overrun_wakeup) {
        audio_buf->state &= ~AUDIO_BUFFER_OVERRUN;
        audio_buf->overrun_wakeup(audio_buf->overrun_wakeup_data);
    }
    spin_unlock(&audio_buf->lock);

    return &frame->frame;
}

int live_audio_buffer_push_timestamp(void *audio_buffer, u32 timestamp)
{
    struct live_audio_buffer *audio_buf = (struct live_audio_buffer *)audio_buffer;

    struct live_audio_frame *frame = NULL;

    spin_lock(&audio_buf->lock);

    if (list_empty(&audio_buf->frame_head)) {
        spin_unlock(&audio_buf->lock);
        return -EINVAL;
    }

    frame = list_entry(&audio_buf->frame_head.prev, struct live_audio_frame, entry);
    frame->frame.timestamp = timestamp;

    spin_unlock(&audio_buf->lock);

    return 0;
}

struct audio_frame *live_audio_buffer_pull_frame(void *audio_buffer)
{
    return __live_audio_buffer_pull_frame(audio_buffer, 1);
}

struct audio_frame *live_audio_buffer_pop_frame(void *audio_buffer)
{
    return __live_audio_buffer_pull_frame(audio_buffer, 0);
}

int live_audio_buffer_len(void *audio_buffer)
{
    struct live_audio_buffer *audio_buf = (struct live_audio_buffer *)audio_buffer;
    int len = 0;

    spin_lock(&audio_buf->lock);
    len = audio_buf->buffered_len;
    spin_unlock(&audio_buf->lock);

    return len;
}

void live_audio_buffer_free_frame(void *audio_buffer, struct audio_frame *audio_frame)
{
    struct live_audio_buffer *audio_buf = (struct live_audio_buffer *)audio_buffer;

    struct live_audio_frame *frame = container_of(audio_frame, struct live_audio_frame, frame);

    spin_lock(&audio_buf->lock);
    if (!frame->deleted) {
        list_del(&frame->entry);
    }
    audio_buf->buffered_len -= audio_frame->len;
    free(frame);
    spin_unlock(&audio_buf->lock);
}

