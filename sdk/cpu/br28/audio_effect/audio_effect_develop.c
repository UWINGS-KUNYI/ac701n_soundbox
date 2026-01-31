/*************************************************************************************************/
/*!
*  \file      audio_effect_develop.c
*
*  \brief     第三方音效算法添加
*
*  Copyright (c) 2011-2023 ZhuHai Jieli Technology Co.,Ltd.
*
*/
/*************************************************************************************************/
#include "app_config.h"
#include "audio_effect_develop.h"

#if ((defined TCFG_EFFECT_DEVELOP_ENABLE) && TCFG_EFFECT_DEVELOP_ENABLE)
struct audio_effect_develop_handle {
    int sample_rate;
    u8 nch;
    u8 bit_width;
    struct audio_stream_entry entry;
};

static int audio_effect_develop_data_handler(struct audio_stream_entry *entry,
        struct audio_data_frame *in,
        struct audio_data_frame *out);
static void audio_effect_develop_process_len(struct audio_stream_entry *entry, int len);

struct audio_stream_entry *audio_effect_develop_open(int sample_rate, u8 nch, u8 bit_width)
{
    struct audio_effect_develop_handle *hdl = (struct audio_effect_develop_handle *)zalloc(sizeof(struct audio_effect_develop_handle));

    if (!hdl) {
        return NULL;
    }

    hdl->sample_rate = sample_rate;
    hdl->nch = nch;
    hdl->bit_width = bit_width;
    hdl->entry.data_handler = audio_effect_develop_data_handler;
    hdl->entry.data_process_len = audio_effect_develop_process_len;


    //TODO : 打开算法模块

    return &hdl->entry;
}

void audio_effect_develop_close(struct audio_stream_entry *entry)
{
    if (!entry) {
        return;
    }
    struct audio_effect_develop_handle *hdl = container_of(entry, struct audio_effect_develop_handle, entry);

    audio_stream_del_entry(&hdl->entry);
    //TODO : 关闭算法模块


    if (hdl) {
        free(hdl);
    }
}

static void audio_effect_develop_process_len(struct audio_stream_entry *entry, int len)
{

}

static int audio_effect_develop_data_handler(struct audio_stream_entry *entry,
        struct audio_data_frame *in,
        struct audio_data_frame *out)
{
    struct audio_effect_develop_handle *hdl = container_of(entry, struct audio_effect_develop_handle, entry);

    out->data = in->data;
    out->data_len = in->data_len;
    //TODO : 音效运行
    // in->data : PCM数据地址, in->data_len : PCM数据byte长度
    // demo : audio_effect_run(hdl->effect, in->data, in->data_len);


    return in->data_len;
}

#endif
