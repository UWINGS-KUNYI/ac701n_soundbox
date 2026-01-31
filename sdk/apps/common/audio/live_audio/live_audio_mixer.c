/*************************************************************************************************/
/*!
*  \file      live_audio_mixer.c
*
*  \brief
*
*  Copyright (c) 2011-2023 ZhuHai Jieli Technology Co.,Ltd.
*
*/
/*************************************************************************************************/
#include "os/os_api.h"
#include "live_audio_mixer.h"

struct live_audio_mixer {
    char name[16];
    struct list_head entry;
    struct list_head channel_list;
    int sample_rate;
    u8 remain;
    u8 nch;
    u8 frame_num;
    u8 run_ch_num;
    u8 buff_num;
    s16 frame_size; //byte
    s16 buff_size;  //byte
    s16 offset;     //sample
    s16 buff_pcm_frames;
    u8 *buff;
    struct audio_frame output_frame;
    spinlock_t lock;
    void *opath;
    int (*write_frame)(void *path, struct audio_frame *frame);
    OS_MUTEX mutex;
    u8 need_resume;
};

struct live_audio_mix_channel {
    struct live_audio_mixer *mixer;     //混音器
    struct list_head entry;
    u8 state;               //通道状态
    u8 nch;                 //通道的音频声道数
    u8 remapping;           //声道重映射使能
    u8 ch_map[2];           //声道映射到mixer buffer上的配置
    s16 offset;             //sample(采样周期)
    s16 mixed_bytes;        //在mixer的buffer上混入的长度
    s16 buffered_frames;    //数据从通道写入到输出的缓冲采样周期数量
    void *priv;             //事件的私有数据
    void (*event_handler)(void *priv, int event); //事件的callback
    void *wakeup_data;      //唤醒callback参数私有数据
    void (*wakeup)(void *priv); //唤醒callbck
};

#define MIX_CH_IDLE         0x0
#define MIX_CH_START        0x1
#define MIX_CH_OVERRUN      0x4

static struct list_head g_mixer_list = LIST_HEAD_INIT(g_mixer_list);
static OS_MUTEX mixer_mutex;

int live_audio_mixer_register(const char *name, struct live_audio_mixer_config *config)
{
    struct live_audio_mixer *mixer = NULL;

    list_for_each_entry(mixer, &g_mixer_list, entry) {
        if (strcmp(mixer->name, name) == 0) {
            printf("Mixer %s has been registered.\n", name);
            return 0;
        }
    }

    mixer = (struct live_audio_mixer *)zalloc(sizeof(struct live_audio_mixer));

    if (!mixer) {
        return -ENOMEM;
    }

    ASSERT(config->sample_rate);
    ASSERT(config->nch);
    ASSERT(config->frame_size);
    mixer->sample_rate = config->sample_rate;
    mixer->nch = config->nch;
    mixer->frame_size = config->frame_size;
    mixer->buff_num = 3;
    mixer->buff_size = config->frame_size * mixer->buff_num;
    mixer->buff = zalloc(mixer->buff_size);
    mixer->buff_pcm_frames = mixer->buff_size / 2 / mixer->nch;
    mixer->opath = config->opath;
    mixer->write_frame = config->write_frame;
    strcpy(mixer->name, name);
    spin_lock_init(&mixer->lock);
    os_mutex_create(&mixer->mutex);
    INIT_LIST_HEAD(&mixer->channel_list);
    list_add(&mixer->entry, &g_mixer_list);
    printf("%s register : %d, %d, %d\n", mixer->name, mixer->sample_rate, mixer->nch, mixer->frame_size);
    return 0;
}

int live_audio_mixer_unregister(const char *name)
{
    struct live_audio_mixer *mixer;

    list_for_each_entry(mixer, &g_mixer_list, entry) {
        if (strcmp(mixer->name, name) == 0) {
            goto unregister;
        }
    }

    return 0;

unregister:
    if (mixer->buff) {
        free(mixer->buff);
    }
    printf("mixer %s unregister\n", mixer->name);

    list_del(&mixer->entry);
    free(mixer);
    return 0;
}

void *live_audio_mix_ch_open(const char *name, struct live_audio_mixer_ch_params *params)
{
    struct live_audio_mix_channel *ch;
    struct live_audio_mixer *mixer;
    int i = 0;

    list_for_each_entry(mixer, &g_mixer_list, entry) {
        if (strcmp(mixer->name, name) == 0) {
            goto found_mixer;
        }
    }

    return NULL;

found_mixer:
    ch = (struct live_audio_mix_channel *)zalloc(sizeof(struct live_audio_mix_channel));

    ch->mixer = mixer;
    ch->nch = params->nch;
    ch->wakeup_data = params->wakeup_data;
    ch->wakeup = params->wakeup;
    ch->priv = params->priv;
    ch->event_handler = params->event_handler;

    if (ch->nch == mixer->nch) {
        for (i = 0; i < ch->nch; i++) {
            ch->ch_map[i] = BIT(i);
        }
    } else {
        ch->remapping = 1;
        printf("audio mix different channel : %d, %d\n", ch->nch, mixer->nch);
        memcpy(ch->ch_map, params->ch_map, ch->nch);
    }
    /*printf("live audio mixer ch open : 0x%x\n", (u32)ch);*/
    spin_lock(&mixer->lock);
    list_add(&ch->entry, &mixer->channel_list);
    spin_unlock(&mixer->lock);
    return ch;
}

static int live_audio_mixer_output(struct live_audio_mixer *mixer, struct audio_frame *frame)
{
    struct live_audio_mix_channel *p;
    int wlen = 0;
    wlen = mixer->write_frame(mixer->opath, frame);

    spin_lock(&mixer->lock);
    mixer->remain = wlen < (frame->len - frame->offset) ? 1 : 0;

    if (!mixer->remain) {
        memset(frame->data, 0x0, frame->len);
    }

    list_for_each_entry(p, &mixer->channel_list, entry) {
        if (!(p->state & MIX_CH_START)) {
            continue;
        }
        if (mixer->remain) {
            if (p->mixed_bytes >= mixer->buff_size) {
                p->state |= MIX_CH_OVERRUN;
            }
        } else {
            p->mixed_bytes -= mixer->frame_size;
        }
    }
    spin_unlock(&mixer->lock);
    if (mixer->need_resume) {
        live_audio_mixer_wakeup(mixer->name);
        mixer->need_resume = 0;
    }
    return wlen;
}

static int audio_pcm_data_16bit_mix(s16 *input, u8 input_nch, s16 *output, u8 output_nch, u8 *ch_map, int frames)
{
    int temp = 0;
    int i, ich, och, idata;
    for (i = 0; i < frames; i++) {
        for (ich = 0; ich < input_nch; ich++) {
            idata = input[ich];
            for (och = 0; och < output_nch; och++) {
                if (!(ch_map[ich] & BIT(och))) {
                    continue;
                }
                temp = (int)output[och] + idata;
                output[och] = data_sat_s16(temp);
            }
        }
        input += input_nch;
        output += output_nch;
    }

    return 0;
}

/***********************************************************
 *      mixer audio frame write
 * description  : 向混音器中混入通道的音频帧
 * arguments    : ch                -   混音通道结构
 *                frame             -   混音的音频帧
 * return       : 实际音频帧混入缓冲的长度.
 * notes        : None.
 ***********************************************************/
static int mixer_audio_frame_write(struct live_audio_mix_channel *ch, struct audio_frame *frame)
{
    struct live_audio_mix_channel *p;
    struct live_audio_mixer *mixer = ch->mixer;
    int write_pcm_frames = 0;
    int ch_free_pcm_frames = 0;

    if (mixer->remain) {
        live_audio_mixer_output(mixer, &mixer->output_frame);
    }

    int pcm_frames = ((frame->len - frame->offset) >> 1) / ch->nch;
    s16 *pcm_data, *buff;
    int ch_to_end_pcm_frames;

    pcm_data = (s16 *)(frame->data + frame->offset);

mix_begin:
    ch_free_pcm_frames = mixer->buff_pcm_frames - (ch->mixed_bytes >> 1) / mixer->nch;
    buff = (s16 *)mixer->buff + ch->offset * mixer->nch;

    if (pcm_frames > ch_free_pcm_frames) {
        pcm_frames = ch_free_pcm_frames;
    }
    write_pcm_frames += pcm_frames;
    spin_lock(&mixer->lock);
    ch->mixed_bytes += (pcm_frames << 1) * mixer->nch;
    ch->buffered_frames += pcm_frames;
    spin_unlock(&mixer->lock);

    /*
     * 检查可混合到buffer的pcm帧点是否越过buffer的尾地址，
     * 如果越过需要先混合到尾地址的数据，再绕回buffer开始进行混合，
     * 这种混合主要是对于mixer需要多帧buffer的存储提升前级运转效率的需求
     */
    if (ch->offset + pcm_frames > mixer->buff_pcm_frames) {
        ch_to_end_pcm_frames = mixer->buff_pcm_frames - ch->offset;
        audio_pcm_data_16bit_mix(pcm_data, ch->nch, buff, mixer->nch, ch->ch_map, ch_to_end_pcm_frames);
        buff = (s16 *)mixer->buff;
        pcm_data += ch_to_end_pcm_frames * ch->nch;
        pcm_frames -= ch_to_end_pcm_frames;
        ch->offset = 0;
    }

    audio_pcm_data_16bit_mix(pcm_data, ch->nch, buff, mixer->nch, ch->ch_map, pcm_frames);
    ch->offset += pcm_frames;
    if (ch->offset >= mixer->buff_pcm_frames) {
        ch->offset -= mixer->buff_pcm_frames;
    }

    int frame_num = 0;
    int min_bytes = ch->mixed_bytes;

    spin_lock(&mixer->lock);
    list_for_each_entry(p, &mixer->channel_list, entry) {
        if (!(p->state & MIX_CH_START)) {
            continue;
        }

        if (p->mixed_bytes < mixer->frame_size) {
            spin_unlock(&mixer->lock);
            goto mix_exit;
        }
        if (p->mixed_bytes <= min_bytes) {
            min_bytes = p->mixed_bytes;
        }
    }
    spin_unlock(&mixer->lock);

    frame_num = min_bytes / mixer->frame_size;

    /*
     * 已经混合完一帧，且mixer前一帧数据已经输出，则继续输出
     */
    if (!mixer->remain) {
        while (!mixer->remain && frame_num--) {
            struct audio_frame *mixer_frame = &mixer->output_frame;
            mixer_frame->coding_type = frame->coding_type;
            mixer_frame->data = mixer->buff + mixer->offset;
            mixer_frame->len = mixer->frame_size;
            mixer_frame->nch = mixer->nch;
            mixer_frame->sample_rate = mixer->sample_rate;
            mixer_frame->offset = 0;

            mixer->offset += mixer->frame_size;
            if (mixer->offset >= mixer->buff_size) {
                mixer->offset = 0;
            }
            live_audio_mixer_output(mixer, &mixer->output_frame);
        }
        pcm_data += pcm_frames * ch->nch;
        pcm_frames = ((frame->len - frame->offset) >> 1) / ch->nch - write_pcm_frames;
        if (pcm_frames) {
            goto mix_begin;
        }
    }

mix_exit:
    int write_bytes = (write_pcm_frames << 1) * ch->nch;
    spin_lock(&mixer->lock);
    if (write_bytes < (frame->len - frame->offset)) {
        ch->state |= MIX_CH_OVERRUN;
    }
    spin_unlock(&mixer->lock);
    return write_bytes;
}

int live_audio_mix_ch_write_frame(void *mix_ch, struct audio_frame *frame)
{
    struct live_audio_mix_channel *ch = (struct live_audio_mix_channel *)mix_ch;
    struct live_audio_mixer *mixer = ch->mixer;
    int wlen = 0;

    int max = 0;
    struct live_audio_mix_channel *max_ch = NULL;

    os_mutex_pend(&mixer->mutex, 0);
    spin_lock(&mixer->lock);
    if (!(ch->state & MIX_CH_START)) {
        ch->state |= MIX_CH_START;
        mixer->run_ch_num++;
        struct live_audio_mix_channel *p;
        /*获取mixer已输出的缓冲*/
        list_for_each_entry(p, &mixer->channel_list, entry) {
            if (ch == p || (!(p->state & MIX_CH_START))) {
                continue;
            }
            if (max <= p->mixed_bytes) {
                max = p->mixed_bytes;
                max_ch = p;
            }

        }
        if (max_ch) {
            ch->buffered_frames = max_ch->buffered_frames;
            ch->offset = max_ch->offset;
            ch->mixed_bytes = max_ch->mixed_bytes;
        }
        spin_unlock(&mixer->lock);
        /*printf("----mixer ch start, buffered_frames : %d----\n", ch->buffered_frames);*/
        if (ch->event_handler) {
            ch->event_handler(ch->priv, AUDIO_MIXER_EVENT_CH_START);
        }
        spin_lock(&mixer->lock);
    }
    spin_unlock(&mixer->lock);

    if (!ch->remapping && mixer->run_ch_num == 1) {
        wlen = mixer->write_frame(mixer->opath, frame);
        spin_lock(&mixer->lock);
        ch->buffered_frames += ((wlen >> 1) / ch->nch);
        spin_unlock(&mixer->lock);
        os_mutex_post(&mixer->mutex);
        if (wlen < (frame->len - frame->offset)) {
            ch->state |= MIX_CH_OVERRUN;
            live_audio_mixer_wakeup(mixer->name);//直通时直接激活
            /* mixer->need_resume = 1; */
        }
        return wlen;
    }

    wlen = mixer_audio_frame_write(ch, frame);
    os_mutex_post(&mixer->mutex);
    if (wlen < (frame->len - frame->offset)) {
        /* live_audio_mixer_wakeup(mixer->name); */
        ch->state |= MIX_CH_OVERRUN;
        mixer->need_resume = 1;
    }
    return wlen;
}

int live_audio_mix_ch_stop(void *mix_ch)
{
    struct live_audio_mix_channel *ch = (struct live_audio_mix_channel *)mix_ch;
    struct live_audio_mixer *mixer = ch->mixer;

    os_mutex_pend(&mixer->mutex, 0);
    if (ch->state & MIX_CH_START) {
        ch->state &= ~MIX_CH_START;
        if (ch->event_handler) {
            ch->event_handler(ch->priv, AUDIO_MIXER_EVENT_CH_STOP);
        }
        mixer->run_ch_num--;
    }

    os_mutex_post(&mixer->mutex);
    return 0;
}

int live_audio_buffered_frames_from_mixer(void *mix_ch)
{
    struct live_audio_mix_channel *ch = (struct live_audio_mix_channel *)mix_ch;
    struct live_audio_mixer *mixer = ch->mixer;
    int pcm_frames = 0;

    spin_lock(&mixer->lock);
    if (ch->state & MIX_CH_START) {
        pcm_frames = ch->buffered_frames;
    }
    spin_unlock(&mixer->lock);
    return pcm_frames;
}

int live_audio_mixer_sample_rate(void *mix_ch)
{
    struct live_audio_mix_channel *ch = (struct live_audio_mix_channel *)mix_ch;
    struct live_audio_mixer *mixer = ch->mixer;

    return mixer->sample_rate;
}

void live_audio_mixer_use_pcm_frames(void *mix_ch, int frames)
{
    struct live_audio_mix_channel *ch = (struct live_audio_mix_channel *)mix_ch;
    struct live_audio_mixer *mixer = ch->mixer;
    spin_lock(&mixer->lock);
    if (ch->state & MIX_CH_START) {
        ch->buffered_frames -= frames;
    }
    spin_unlock(&mixer->lock);
}

void live_audio_mix_ch_debug(void *mix_ch)
{
    struct live_audio_mix_channel *ch = (struct live_audio_mix_channel *)mix_ch;

    printf("mixer ch [%d, %d]\n", ch->mixed_bytes, ch->buffered_frames);
}

void live_audio_mixer_wakeup(void *mixer_name)
{
    const char *name = mixer_name;
    struct live_audio_mix_channel *ch;
    struct live_audio_mixer *mixer = NULL;

    list_for_each_entry(mixer, &g_mixer_list, entry) {
        if (strcmp(mixer->name, name) == 0) {
            goto wakeup;
        }
    }

    return;

wakeup:
    spin_lock(&mixer->lock);
    list_for_each_entry(ch, &mixer->channel_list, entry) {
        if (!(ch->state & MIX_CH_START) || !(ch->state & MIX_CH_OVERRUN)) {
            continue;
        }
        if (ch->wakeup) {
            ch->wakeup(ch->wakeup_data);
        }
    }
    spin_unlock(&mixer->lock);
}

int live_audio_mix_ch_close(void *mix_ch)
{
    struct live_audio_mix_channel *ch = (struct live_audio_mix_channel *)mix_ch;
    struct live_audio_mixer *mixer = NULL;

    struct live_audio_mixer *n = NULL;

    live_audio_mix_ch_stop(ch);
    if (ch->event_handler) {
        ch->event_handler(ch->priv, AUDIO_MIXER_EVENT_CH_CLOSE);
    }

    list_for_each_entry_safe(mixer, n, &g_mixer_list, entry) {
        if (strcmp(mixer->name, ch->mixer->name) == 0) {
            os_mutex_pend(&mixer->mutex, 0);
            list_del(&ch->entry);
            free(ch);
            ch = NULL;
            os_mutex_post(&mixer->mutex);
        }
    }
    return 0;
}


