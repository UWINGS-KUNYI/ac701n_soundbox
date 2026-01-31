#include "app_config.h"
#include "app_connected.h"
#include "connected_api.h"
#include "app_broadcast.h"
#include "broadcast_api.h"
#include "sound_device_driver.h"
#include "audio_mode.h"
#include "audio_link.h"
#include "audio_config.h"
#include "asm/audio_adc.h"
#include "audio_enc/audio_enc.h"
#include "audio_splicing.h"
#include "audio_path.h"
#include "uac_stream.h"
#include "board_config.h"
#include "wireless_mic_effect.h"

#if WIRELESS_2T1_DUPLEX_EN

#define UAC_STREAM_PACKET_NUM       (5)
#define UAC_SAMPLE_POINT            (SPK_CHANNEL * SPK_AUDIO_RES / 8)
#define UAC_STREAM_IRQ_LEN          (SPK_AUDIO_RATE * UAC_SAMPLE_POINT / 1000 + UAC_SAMPLE_POINT)
#define UAC_STREAM_TEMP_BUFF_LEN    ((SPK_AUDIO_RATE * UAC_SAMPLE_POINT / 1000 + UAC_SAMPLE_POINT) * UAC_STREAM_PACKET_NUM)

struct adc_opr {
    void *rec_dev;
    u8 onoff;
    u8 audio_state; /*判断linein模式使用模拟音量还是数字音量*/
    void *capture;
};

struct iis_opr {
    u8 state;
    void *capture;
    void *alink_ch;
};

struct usb_opr {
    u8 state;
    u8 streamon;
    u8 pp_event;
    u8 channel;
    u16 stream_detect_timer;
    int sample_rate;
    void *capture;
    void *priv;
};

struct adc_multiple_capture_context {
    struct audio_adc_output_hdl adc_output; //adc 输出
    struct adc_linein_ch aux_ch;      	    //aux 通道
    struct adc_mic_ch mic_ch;               //mic 通道
    int sample_rate;   					    //采样率
    u8 gain;								//增益
    u8 input_nch;						    //打开的输入通道数
    u8 nch;                                 //输出pcm数据声道数
    u8 start;								//aux 状态
    u8 on_off;
    s16 sample_buffer[256 * 4];             //256 是中断点数
    void *output_path;                      //数据接口私有参数
    int (*write_frame)(void *path, struct audio_frame *frame);//输出接口
    void *clock;
    u32(*capture_time)(void *clock, u8 type);
};

struct iis_multiple_capture_context {
    u8 start;
    int sample_rate;
    u8 nch;                                 //输出pcm数据声道数
    s16 sample_buffer[256 * 3];             //256 是中断点数
    void *output_path;                      //数据接口私有参数
    int (*write_frame)(void *path, struct audio_frame *frame);//输出接口
    void *clock;
    u32(*capture_time)(void *clock, u8 type);
};

struct usb_multiple_capture_context {
    int sample_rate;
    u8 nch;                                 //输出pcm数据声道数
    s16 sample_buffer[UAC_STREAM_IRQ_LEN * 3];
    void *output_path;                      //数据接口私有参数
    int (*write_frame)(void *path, struct audio_frame *frame);//输出接口
    void *clock;
    u32(*capture_time)(void *clock, u8 type);
};

static void uac_streamon_handler(struct usb_opr *usb);
static void *wireless_aux_capture_start(struct audio_path *path);
static void wireless_aux_capture_stop(void *capture);
static void *wireless_mic_capture_start(struct audio_path *path);
static void wireless_mic_capture_stop(void *capture);
static void *wireless_usb_capture_start(struct audio_path *path);
static void wireless_usb_capture_stop(void *capture);
static void *wireless_iis_capture_start(struct audio_path *path);
static void wireless_iis_capture_stop(void *capture);

static struct adc_opr mic_hdl = {0};
static struct adc_opr aux_hdl = {0};
static struct iis_opr iis_hdl = {0};
static struct usb_opr usb_hdl = {0};
static struct adc_multiple_capture_context *aux_priv = 0;
static struct adc_multiple_capture_context *mic_priv = 0;
static struct iis_multiple_capture_context *iis_priv = 0;
static struct usb_multiple_capture_context *usb_priv = 0;
static u8 uac_stream_temp_buf[UAC_STREAM_TEMP_BUFF_LEN];
static DEFINE_SPINLOCK(aux_multiple_capture_lock);
static DEFINE_SPINLOCK(mic_multiple_capture_lock);
static DEFINE_SPINLOCK(iis_multiple_capture_lock);
static DEFINE_SPINLOCK(usb_multiple_capture_lock);

extern ALINK_PARM alink0_platform_data;

#if TCFG_LINEIN_ENABLE
static struct live_audio_mode_ops wireless_aux_mode_ops = {
    .capture_open = wireless_aux_capture_start,
    .capture_close = wireless_aux_capture_stop,
};
#endif

static struct live_audio_mode_ops wireless_mic_mode_ops = {
    .capture_open = wireless_mic_capture_start,
    .capture_close = wireless_mic_capture_stop,
};

#if TCFG_PC_ENABLE
static struct live_audio_mode_ops wireless_usb_mode_ops = {
    .capture_open = wireless_usb_capture_start,
    .capture_close = wireless_usb_capture_stop,
};
#endif

#if TCFG_AUDIO_INPUT_IIS
static struct live_audio_mode_ops wireless_iis_mode_ops = {
    .capture_open = wireless_iis_capture_start,
    .capture_close = wireless_iis_capture_stop,
};
#endif

static int adc_multiple_capture_write_frame(struct adc_multiple_capture_context *adc, void *data, int len)
{
    if (!adc->write_frame) {
        return len;
    }

    struct audio_frame frame = {
        .mode = AUDIO_FRAME_DMA_STREAM,
        .nch = adc->nch,
        .sample_rate = adc->sample_rate,
        .len = len,
        .data = data,
        .timestamp = adc->capture_time ? adc->capture_time(adc->clock, 0) : 0,
    };

    return adc->write_frame(adc->output_path, &frame);
}

//*********************************************************************************//
//                                    AUX                                          //
//*********************************************************************************//

#if TCFG_LINEIN_ENABLE
#if 0
void aux_multiple_capture_data_handler(void *priv, s16 *data, int len)
{
    struct adc_multiple_capture_context *aux = (struct adc_multiple_capture_context *)aux_priv;

    spin_lock(&aux_multiple_capture_lock);
    if (!aux || !aux->start) {
        spin_unlock(&aux_multiple_capture_lock);
        return ;
    }
    if (!aux->on_off) {
        memset(data, 0, len * aux->input_nch);
    }
    int wlen = 0;
    if (aux->input_nch >= 2) {
        /*输出两个linein的数据,默认输出第一个和第二个采样通道的数据*/
        if (aux->nch >= 2) {
            wlen = adc_multiple_capture_write_frame(aux, data, len * 2);
            if (wlen < len * 2) {
                putchar('L');
            }
        } else { //硬件配置双声道,需要输出单声道,这种情况最好硬件也配成单声道避免资源浪费.
            pcm_dual_to_single(aux->sample_buffer, data, len * 2);
            wlen = adc_multiple_capture_write_frame(aux, aux->sample_buffer, len);
            if (wlen < len) {
                putchar('L');
            }
        }

    } else {
        if (aux->nch >= 2) { //硬件配置单声道,需要输出双声道数据,
            pcm_single_to_dual(aux->sample_buffer, data, len);
            wlen = adc_multiple_capture_write_frame(aux, aux->sample_buffer, len * 2);
            if (wlen < len * 2) {
                putchar('L');
            }
        } else {
            wlen = adc_multiple_capture_write_frame(aux, data, len);
            if (wlen < len) {
                putchar('L');
            }

        }
    }
    spin_unlock(&aux_multiple_capture_lock);
}

static void *aux_multiple_capture_open(struct audio_path *path)
{
    /* if (aux_priv){ */
    /*     spin_lock(&aux_multiple_capture_lock); */
    /*     if (!aux_priv->start){ */
    /*         aux_priv->start = 1; */
    /*     } */
    /*     spin_unlock(&aux_multiple_capture_lock); */
    /*     return  aux_priv; */
    /* } */
    aux_priv = zalloc(sizeof(struct adc_multiple_capture_context));
    ASSERT(aux_priv);
    aux_priv->sample_rate = path->fmt.sample_rate;
    aux_priv->gain = 3;//params->gain;
    aux_priv->nch = path->fmt.channel;
    aux_priv->output_path = path->output.path;
    aux_priv->write_frame = path->output.write_frame;
    aux_priv->clock = path->time.reference_clock;
    aux_priv->capture_time = path->time.request;

    if (audio_linein_open(&aux_priv->aux_ch, aux_priv->sample_rate, aux_priv->gain) == 0) {
        aux_priv->adc_output.handler = aux_multiple_capture_data_handler;
        aux_priv->adc_output.priv = aux_priv;
        aux_priv->input_nch = get_audio_linein_ch_num();
        printf("-------------linein->channel_num:%d  output_num:%d----------\n", aux_priv->input_nch, aux_priv->nch);
        audio_linein_add_output(&aux_priv->adc_output);
        audio_linein_start(&aux_priv->aux_ch);
        aux_priv->start = 1;
    }
    aux_priv->on_off = 1;
    printf("aux multiple capture start succ\n");
    return aux_priv;

}

static void aux_multiple_capture_close(void *capture)
{
    struct adc_multiple_capture_context *aux = (struct adc_multiple_capture_context *)capture;
    printf("aux multiple capture close\n");
    if (!aux) {
        return;
    }
    /* audio_linein_close(NULL, &aux->adc_output); */
    spin_lock(&aux_multiple_capture_lock);
    aux->start = 0;
    /* free(aux); */
    /* aux = NULL; */
    spin_unlock(&aux_multiple_capture_lock);
}

void aux_multiple_capture_play_pause(void *capture, u8 on_off)
{
    struct adc_multiple_capture_context *ctx = (struct adc_multiple_capture_context *)capture;

    ctx->on_off = on_off;
}
#else

struct aux_pcm_capture_hdl {
    struct audio_stream_entry entry;
} aux_capture_node;


static void aux_pcm_capture_process_len(struct audio_stream_entry *entry,  int len)
{
}
// 解码输出数据流节点数据处理回调
static int aux_pcm_capture_data_handler(struct audio_stream_entry *entry,
                                        struct audio_data_frame *in,
                                        struct audio_data_frame *out)
{
    struct aux_pcm_capture_hdl *hdl = container_of(entry, struct aux_pcm_capture_hdl, entry);
    int wlen = 0;
    /* putchar('D'); */

    spin_lock(&aux_multiple_capture_lock);
    if (!aux_priv) {
        spin_unlock(&aux_multiple_capture_lock);
        return in->data_len;
    }

    if (!aux_priv->start || !aux_priv->write_frame) {
        spin_unlock(&aux_multiple_capture_lock);
        return in->data_len;
    }
    spin_unlock(&aux_multiple_capture_lock);
    if (!aux_priv->on_off) {
        memset(in->data, 0, in->data_len);
    }
    if (in->channel >= 2) {
        /*输出两个linein的数据,默认输出第一个和第二个采样通道的数据*/
        if (aux_priv->nch >= 2) {
            wlen = adc_multiple_capture_write_frame(aux_priv, in->data, in->data_len);
            if (wlen < in->data_len) {
                putchar('L');
            }
            return wlen;
        } else { //硬件配置双声道,需要输出单声道,这种情况最好硬件也配成单声道避免资源浪费.
            pcm_dual_to_single(aux_priv->sample_buffer, in->data, in->data_len);
            wlen = adc_multiple_capture_write_frame(aux_priv, aux_priv->sample_buffer, in->data_len / 2);
            if (wlen <  in->data_len / 2) {
                putchar('L');
            }
            return wlen * 2;
        }

    } else {
        if (aux_priv->nch >= 2) { //硬件配置单声道,需要输出双声道数据,
            pcm_single_to_dual(aux_priv->sample_buffer, in->data, in->data_len);
            wlen = adc_multiple_capture_write_frame(aux_priv, aux_priv->sample_buffer, in->data_len * 2);
            if (wlen < in->data_len * 2) {
                putchar('L');
            }
            return wlen / 2;
        } else {
            wlen = adc_multiple_capture_write_frame(aux_priv, in->data, in->data_len);
            if (wlen < in->data_len) {
                putchar('L');
            }
            return wlen;
        }
    }

    return in->data_len;
}


void audio_dec_linein_add_capture_output(void)
{
    aux_capture_node.entry.data_process_len = aux_pcm_capture_process_len;
    aux_capture_node.entry.data_handler = aux_pcm_capture_data_handler;

    extern int audio_dec_linein_add_output(struct audio_stream_entry * output);
    audio_dec_linein_add_output(&aux_capture_node.entry);
}

static void *aux_multiple_capture_open(struct audio_path *path)
{
    printf("aux_multiple_capture_open");
    if (aux_priv) {
        spin_lock(&aux_multiple_capture_lock);
        aux_priv->sample_rate = path->fmt.sample_rate;
        aux_priv->gain = 10;
        aux_priv->nch = path->fmt.channel;
        aux_priv->output_path = path->output.path;
        aux_priv->write_frame = path->output.write_frame;
        aux_priv->clock = path->time.reference_clock;
        aux_priv->capture_time = path->time.request;

        aux_priv->start = 1;
        aux_priv->on_off = 1;
        spin_unlock(&aux_multiple_capture_lock);
        return  aux_priv;
    }
    aux_priv = zalloc(sizeof(struct adc_multiple_capture_context));
    ASSERT(aux_priv);
    aux_priv->sample_rate = path->fmt.sample_rate;
    aux_priv->gain = 10;
    aux_priv->nch = path->fmt.channel;
    aux_priv->output_path = path->output.path;
    aux_priv->write_frame = path->output.write_frame;
    aux_priv->clock = path->time.reference_clock;
    aux_priv->capture_time = path->time.request;

    aux_priv->start = 1;
    aux_priv->on_off = 1;

    audio_dec_linein_add_capture_output();

    return aux_priv;
}

void aux_multiple_capture_play_pause(void *capture, u8 on_off)
{
    struct adc_multiple_capture_context *ctx = (struct adc_multiple_capture_context *)capture;
    if (aux_priv) {
        aux_priv->on_off = on_off;
        printf("aux_multiple_capture_play_pause  on_off %d", on_off);
    }
}


static void aux_multiple_capture_close(void *capture)
{
    struct adc_multiple_capture_context *aux = (struct adc_multiple_capture_context *)capture;
    printf("aux multiple capture close\n");
    if (!aux_priv) {
        return;
    }

    spin_lock(&aux_multiple_capture_lock);

    aux_priv->start = 0;
    /* if (mic_capture_node.entry.data_handler){ */
    /*     live_mic_effect_del_output(&mic_capture_node.entry); */
    /*     mic_capture_node.entry.data_handler = NULL; */
    /*     mic_capture_node.entry.data_process_len = NULL; */
    /* } */

    /* free(mic_priv); */
    /* mic_priv = NULL; */

    spin_unlock(&aux_multiple_capture_lock);
}



#endif
#endif

//*********************************************************************************//
//                                    MIC                                          //
//*********************************************************************************//
#if WIRELESS_MIC_EFFECT_ASYNC_EN

struct mic_pcm_capture_hdl {
    struct audio_stream_entry entry;
} mic_capture_node;


static void mic_pcm_capture_process_len(struct audio_stream_entry *entry,  int len)
{
}
// 解码输出数据流节点数据处理回调
static int mic_pcm_capture_data_handler(struct audio_stream_entry *entry,
                                        struct audio_data_frame *in,
                                        struct audio_data_frame *out)
{
    struct mic_pcm_capture_hdl *hdl = container_of(entry, struct mic_pcm_capture_hdl, entry);
    int wlen = 0;
    /* putchar('D'); */

    /* spin_lock(&mic_multiple_capture_lock); */
    if (!mic_priv) {
        return in->data_len;
    }

    if (!mic_priv->start || !mic_priv->write_frame) {
        return in->data_len;
    }

    if (!mic_priv->on_off) {
        /* spin_unlock(&mic_multiple_capture_lock); */
        memset(in->data, 0, in->data_len);
        /* spin_lock(&mic_multiple_capture_lock); */
    }
    /* extern struct live_mic_effect *g_mic_effect; */
    if (!live_mic_effect_status_get()) {
        return in->data_len;
    }
    if (in->channel >= 2) {
        /*输出两个linein的数据,默认输出第一个和第二个采样通道的数据*/
        if (mic_priv->nch >= 2) {
            wlen = adc_multiple_capture_write_frame(mic_priv, in->data, in->data_len);
            if (wlen < in->data_len) {
                putchar('L');
            }
            return wlen;
        } else { //硬件配置双声道,需要输出单声道,这种情况最好硬件也配成单声道避免资源浪费.
            pcm_dual_to_single(mic_priv->sample_buffer, in->data, in->data_len);
            wlen = adc_multiple_capture_write_frame(mic_priv, mic_priv->sample_buffer, in->data_len / 2);
            if (wlen <  in->data_len / 2) {
                putchar('l');
            }
            return wlen * 2;
        }

    } else {
        if (mic_priv->nch >= 2) { //硬件配置单声道,需要输出双声道数据,
            pcm_single_to_dual(mic_priv->sample_buffer, in->data, in->data_len);
            wlen = adc_multiple_capture_write_frame(mic_priv, mic_priv->sample_buffer, in->data_len * 2);
            if (wlen < in->data_len * 2) {
                putchar('L');
            }
            return wlen / 2;
        } else {
            wlen = adc_multiple_capture_write_frame(mic_priv, in->data, in->data_len);
            if (wlen < in->data_len) {
                putchar('l');
            }
            return wlen;
        }
    }
    /* spin_unlock(&mic_multiple_capture_lock); */
    return in->data_len;
}


void async_live_mic_effect_add_output(void)
{
    mic_capture_node.entry.data_process_len = mic_pcm_capture_process_len;
    mic_capture_node.entry.data_handler = mic_pcm_capture_data_handler;
    live_mic_effect_add_output(&mic_capture_node.entry);
}

static void *mic_multiple_capture_open(struct audio_path *path)
{
    printf("mic_multiple_capture_open");
    if (mic_priv) {
        spin_lock(&mic_multiple_capture_lock);
        mic_priv->sample_rate = path->fmt.sample_rate;
        mic_priv->gain = 10;
        mic_priv->nch = path->fmt.channel;
        mic_priv->output_path = path->output.path;
        mic_priv->write_frame = path->output.write_frame;
        mic_priv->clock = path->time.reference_clock;
        mic_priv->capture_time = path->time.request;

        mic_priv->start = 1;
        mic_priv->on_off = 1;
        spin_unlock(&mic_multiple_capture_lock);
        return  mic_priv;
    }
    mic_priv = zalloc(sizeof(struct adc_multiple_capture_context));
    ASSERT(mic_priv);
    mic_priv->sample_rate = path->fmt.sample_rate;
    mic_priv->gain = 10;
    mic_priv->nch = path->fmt.channel;
    mic_priv->output_path = path->output.path;
    mic_priv->write_frame = path->output.write_frame;
    mic_priv->clock = path->time.reference_clock;
    mic_priv->capture_time = path->time.request;

    mic_priv->start = 1;
    mic_priv->on_off = 1;

    async_live_mic_effect_add_output();

    return mic_priv;
}

void mic_multiple_capture_play_pause(void *capture, u8 on_off)
{
    struct adc_multiple_capture_context *ctx = (struct adc_multiple_capture_context *)capture;
    if (mic_priv) {
        mic_priv->on_off = on_off;
        printf("mic_multiple_capture_play_pause  on_off %d", on_off);
    }
}


static void mic_multiple_capture_close(void *capture)
{
    struct adc_multiple_capture_context *mic = (struct adc_multiple_capture_context *)capture;
    printf("mic multiple capture close\n");
    if (!mic_priv) {
        return;
    }

    spin_lock(&mic_multiple_capture_lock);

    mic_priv->start = 0;
    /* if (mic_capture_node.entry.data_handler){ */
    /*     live_mic_effect_del_output(&mic_capture_node.entry); */
    /*     mic_capture_node.entry.data_handler = NULL; */
    /*     mic_capture_node.entry.data_process_len = NULL; */
    /* } */

    /* free(mic_priv); */
    /* mic_priv = NULL; */

    spin_unlock(&mic_multiple_capture_lock);
}

#else
void mic_multiple_capture_data_handler(void *priv, s16 *data, int len)
{
    struct adc_multiple_capture_context *mic = (struct adc_multiple_capture_context *)mic_priv;

    spin_lock(&mic_multiple_capture_lock);
    if (!mic || !mic->start) {
        spin_unlock(&mic_multiple_capture_lock);
        return ;
    }
    if (!mic->on_off) {
        memset(data, 0, len * mic->input_nch);
    }
    int wlen = 0;
    if (mic->nch >= 2) { //硬件配置单声道,需要输出双声道数据,
        pcm_single_to_dual(mic->sample_buffer, data, len);
        wlen = adc_multiple_capture_write_frame(mic, mic->sample_buffer, len * 2);
        /* app_audio_output_write(mic->sample_buffer, len * 2); */

        if (wlen < len * 2) {
            putchar('M');
        }
    } else {
        wlen = adc_multiple_capture_write_frame(mic, data, len);
        /* app_audio_output_write(data, len); */
        if (wlen < len) {
            putchar('m');
        }
    }
    spin_unlock(&mic_multiple_capture_lock);
}

void mic_multiple_capture_play_pause(void *capture, u8 on_off)
{
    struct adc_multiple_capture_context *ctx = (struct adc_multiple_capture_context *)capture;

    if (mic_priv) {
        mic_priv->on_off = on_off;
        printf("mic_multiple_capture_play_pause  on_off %d", on_off);
    }
    /* ctx->on_off = on_off; */
}

static void *mic_multiple_capture_open(struct audio_path *path)
{
    mic_priv = zalloc(sizeof(struct adc_multiple_capture_context));
    ASSERT(mic_priv);
    mic_priv->sample_rate = path->fmt.sample_rate;
    mic_priv->gain = 10;
    mic_priv->nch = path->fmt.channel;
    mic_priv->output_path = path->output.path;
    mic_priv->write_frame = path->output.write_frame;
    mic_priv->clock = path->time.reference_clock;
    mic_priv->capture_time = path->time.request;

    if (audio_mic_open(&mic_priv->mic_ch, mic_priv->sample_rate, mic_priv->gain) == 0) {
        mic_priv->adc_output.handler = mic_multiple_capture_data_handler;
        mic_priv->adc_output.priv = mic_priv;
        audio_mic_add_output(&mic_priv->adc_output);
        audio_mic_start(&mic_priv->mic_ch);
        mic_priv->start = 1;
    }
    mic_priv->on_off = 1;
    printf("mic multiple capture start succ\n");
    return mic_priv;
}

static void mic_multiple_capture_close(void *capture)
{
    struct adc_multiple_capture_context *mic = (struct adc_multiple_capture_context *)capture;
    printf("mic multiple capture close\n");
    if (!mic) {
        return;
    }

    audio_mic_close(&mic->mic_ch, &mic->adc_output);
    spin_lock(&mic_multiple_capture_lock);
    mic->start = 0;
    free(mic);
    mic = NULL;
    spin_unlock(&mic_multiple_capture_lock);
}
#endif
//*********************************************************************************//
//                                    IIS                                          //
//*********************************************************************************//

#if TCFG_AUDIO_INPUT_IIS
static int iis_multiple_capture_write_frame(struct iis_multiple_capture_context *iis, void *data, int len)
{
    if (!iis->write_frame) {
        return len;
    }

    struct audio_frame frame = {
        .mode = AUDIO_FRAME_DMA_STREAM,
        .nch = iis->nch,
        .sample_rate = iis->sample_rate,
        .len = len,
        .data = data,
        .timestamp = iis->capture_time ? iis->capture_time(iis->clock, 0) : 0,
    };

    return iis->write_frame(iis->output_path, &frame);
}

void iis_multiple_caputre_dma_data_handler(void *priv, void *data, int len)
{
    struct iis_multiple_capture_context *iis = (struct iis_multiple_capture_context *)iis_priv;

    spin_lock(&iis_multiple_capture_lock);
    if (!iis || !iis->start) {
        spin_unlock(&iis_multiple_capture_lock);
        return;
    }
    //TODO : channel mapping

    int wlen = 0;
    if (iis->nch >= 2) {
        wlen = iis_multiple_capture_write_frame(iis, data, len);
        if (wlen < len) {
            putchar('I');
        }
    } else {
        pcm_dual_to_single(iis->sample_buffer, data, len);
        wlen = iis_multiple_capture_write_frame(iis, iis->sample_buffer, len / 2);
        if (wlen < len / 2) {
            putchar('i');
        }
    }
    spin_unlock(&iis_multiple_capture_lock);
}

static void *iis_multiple_capture_open(struct audio_path *path)
{
    iis_priv = (struct iis_multiple_capture_context *)zalloc(sizeof(struct iis_multiple_capture_context));

    ASSERT(iis_priv);

    iis_priv->sample_rate = TCFG_IIS_SR;//path->sample_rate;
    iis_priv->nch = path->fmt.channel;
    iis_priv->output_path = path->output.path;
    iis_priv->write_frame = path->output.write_frame;
    iis_priv->clock = path->time.reference_clock;
    iis_priv->capture_time = path->time.request;
    iis_priv->start = 1;

    set_iis_in_capture_handler(NULL, iis_multiple_caputre_dma_data_handler);

    return iis_priv;
}

static void iis_multiple_capture_close(void *capture)
{
    struct iis_multiple_capture_context *iis = (struct iis_multiple_capture_context *)capture;

    if (!iis) {
        return;
    }

    spin_lock(&iis_multiple_capture_lock);
    iis->start = 0;
    free(iis);
    iis_priv = NULL;
    spin_unlock(&iis_multiple_capture_lock);
}
#endif

#if 0
static void file_muliple_capture_open(struct audio_path *path)
{


    log_i("KEY_MUSIC_PLAYER_START !!\n");
    app_status_handler(APP_STATUS_MUSIC_PLAY);
    ///断点播放活动设备
    logo = dev_manager_get_logo(dev_manager_find_active(1));
    if (music_player_get_play_status() == FILE_DEC_STATUS_PLAY) {
        if (music_player_get_dev_cur() && logo) {
            ///播放的设备跟当前活动的设备是同一个设备，不处理
            if (0 == strcmp(logo, music_player_get_dev_cur())) {
                log_w("the same dev!!\n");
                break;
            }
        }
    }

    if (true == breakpoint_vm_read(breakpoint, logo)) {
        err = music_player_play_by_breakpoint(logo, breakpoint);
    } else {
        err = music_player_play_first_file(logo);
    }
    ///错误处理
    music_player_err_deal(err);

}
#endif
//*********************************************************************************//
//                                    USB                                          //
//*********************************************************************************//
#if TCFG_PC_ENABLE
static int usb_capture_write_frame(struct usb_multiple_capture_context *usb, void *data, int len)
{
    if (!usb->write_frame) {
        return len;
    }

    struct audio_frame frame = {
        .mode = AUDIO_FRAME_DMA_STREAM,
        .nch = usb->nch,
        .sample_rate = usb->sample_rate,
        .len = len,
        .data = data,
        .timestamp = usb->capture_time ? usb->capture_time(usb->clock, 0) : 0,
    };

    return usb->write_frame(usb->output_path, &frame);
}


static u8 packet_num = 0;
static u32 offset = 0;

static void uac_speaker_stream_rx_capture_handler(int event, void *data, int len)
{
    struct usb_multiple_capture_context *usb = (struct usb_multiple_capture_context *)usb_priv;

    usb_hdl.streamon = 1;
    uac_streamon_handler(&usb_hdl);

    spin_lock(&usb_multiple_capture_lock);
    if (!usb) {
        spin_unlock(&usb_multiple_capture_lock);
        return;
    }

    packet_num++;
    u8 *temp_data = uac_stream_temp_buf + offset;
    memcpy(temp_data, data, len);
    offset += len;
    if (packet_num == UAC_STREAM_PACKET_NUM) {
        packet_num = 0;
    } else {
        spin_unlock(&usb_multiple_capture_lock);
        return;
    }

    int wlen = 0;
    if (usb->nch >= 2) {
        wlen = usb_capture_write_frame(usb, uac_stream_temp_buf, offset);
        if (wlen < offset) {
            putchar('S');
        }
    } else {
        pcm_dual_to_single(usb->sample_buffer, uac_stream_temp_buf, offset);
        wlen = usb_capture_write_frame(usb, usb->sample_buffer, offset / 2);
        if (wlen < offset / 2) {
            putchar('s');
        }
    }
    offset = 0;
    spin_unlock(&usb_multiple_capture_lock);
}

static void uac_streamon_handler(struct usb_opr *usb)
{
    if (usb->pp_event == 1) {
        usb->pp_event = 0;
        spin_lock(&usb_multiple_capture_lock);
        offset = 0;
        packet_num = 0;
        spin_unlock(&usb_multiple_capture_lock);
        /* memset(uac_stream_temp_buf, 0, UAC_STREAM_TEMP_BUFF_LEN); */
        struct sys_event event;
        event.type = SYS_DEVICE_EVENT;
        event.arg = (void *)DEVICE_EVENT_FROM_UAC;
        event.u.dev.event = USB_AUDIO_PLAY_RESUME;
        event.u.dev.value = (int)((usb->channel << 24) | usb->sample_rate);
        sys_event_notify(&event);
    }
}
/*
 * USB数据流的PP事件检测功能
 */
static void uac_stream_detect_timer(void *arg)
{
    if (!usb_hdl.streamon) {
        if (usb_hdl.pp_event == 0) {
            usb_hdl.pp_event = 1;
            struct sys_event event;
            event.type = SYS_DEVICE_EVENT;
            event.arg = (void *)DEVICE_EVENT_FROM_UAC;
            event.u.dev.event = USB_AUDIO_PLAY_SUSPEND;
            event.u.dev.value = (int)0;
            sys_event_notify(&event);
        }
        return;
    }

    uac_streamon_handler(&usb_hdl);
    usb_hdl.streamon = 0;
}

void usb_isr_capture_timer_creat(void)
{
    if (!usb_hdl.stream_detect_timer) {
        /*检测音频流PP处理*/
        usb_hdl.streamon = 0;
        usb_hdl.pp_event = 0;
        usb_hdl.stream_detect_timer = sys_hi_timer_add(NULL, uac_stream_detect_timer, 6);
    }
}

void usb_isr_capture_timer_destroy(void)
{
    if (usb_hdl.stream_detect_timer) {
        sys_hi_timer_del(usb_hdl.stream_detect_timer);
        usb_hdl.stream_detect_timer = 0;
    }
}

void *sound_usb_capture_open(struct audio_path *path)
{
    usb_priv = (struct usb_multiple_capture_context *)zalloc(sizeof(struct usb_multiple_capture_context));
    ASSERT(usb_priv);

    usb_priv->sample_rate = path->fmt.sample_rate;
    usb_priv->nch = path->fmt.channel;
    usb_priv->output_path = path->output.path;
    usb_priv->write_frame = path->output.write_frame;
    usb_priv->clock = path->time.reference_clock;
    usb_priv->capture_time = path->time.request;

    spin_lock(&usb_multiple_capture_lock);
    offset = 0;
    packet_num = 0;
    spin_unlock(&usb_multiple_capture_lock);
    extern void set_uac_speaker_rx_capture_handler(void *priv, void (*rx_handler)(int, void *, int));
    set_uac_speaker_rx_capture_handler(NULL, uac_speaker_stream_rx_capture_handler);

    return usb_priv;
}

void sound_usb_capture_close(void *capture)
{
    struct usb_multiple_capture_context *usb = (struct usb_multiple_capture_context *)capture;

    if (!usb) {
        return;
    }

    spin_lock(&usb_multiple_capture_lock);
    // 拔掉usb需要将这个中断函数也设为NULL,注意set的时候要与usb中断互斥
    /* set_uac_speaker_rx_capture_handler(NULL, NULL); */
    offset = 0;
    packet_num = 0;
    free(usb);
    usb_priv = NULL;
    spin_unlock(&usb_multiple_capture_lock);
}
#endif
//*********************************************************************************//
//                          capture start & stop                                   //
//*********************************************************************************//

#if TCFG_LINEIN_ENABLE
static void *wireless_aux_capture_start(struct audio_path *path)
{
    if (aux_hdl.onoff == 1) {
        printf("linein is aleady start\n");
        return aux_hdl.capture;
    }

    aux_hdl.onoff = 1;
    aux_hdl.capture = aux_multiple_capture_open(path);
    aux_multiple_capture_play_pause(aux_hdl.capture, aux_hdl.onoff);
    return aux_hdl.capture;
}

static void wireless_aux_capture_stop(void *capture)
{
    aux_multiple_capture_close(capture);
    aux_hdl.capture = NULL;
    aux_hdl.onoff = 0;
}
#endif

//*********************************************************************************//
//                         mic capture start & stop                                //
//*********************************************************************************//

static void *wireless_mic_capture_start(struct audio_path *path)
{
    if (mic_hdl.onoff == 1) {
        printf("mic is aleady start\n");
        return mic_hdl.capture;
    }

    mic_hdl.onoff = 1;
    mic_hdl.capture = mic_multiple_capture_open(path);
    mic_multiple_capture_play_pause(mic_hdl.capture, mic_hdl.onoff);
    return mic_hdl.capture;
}

static void wireless_mic_capture_stop(void *capture)
{
    mic_multiple_capture_close(capture);
    mic_hdl.capture = NULL;
    mic_hdl.onoff = 0;
}

//*********************************************************************************//
//                         iis capture start & stop                                //
//*********************************************************************************//

#if TCFG_AUDIO_INPUT_IIS
static void *wireless_iis_capture_start(struct audio_path *path)
{
    iis_hdl.capture = iis_multiple_capture_open(path);
    iis_hdl.state = 1;

    return iis_hdl.capture;
}

static void wireless_iis_capture_stop(void *capture)
{
    iis_multiple_capture_close(capture);
    iis_hdl.capture = NULL;
    iis_hdl.state = 0;
}
#endif

//*********************************************************************************//
//                         usb capture start & stop                                //
//*********************************************************************************//

#if TCFG_PC_ENABLE
static void *wireless_usb_capture_start(struct audio_path *path)
{
    extern void *sound_usb_capture_open(struct audio_path * path);
    usb_hdl.capture = sound_usb_capture_open(path);
    usb_hdl.state = 1;
    usb_hdl.channel = path->fmt.channel;
    usb_hdl.sample_rate = path->fmt.sample_rate;

    return usb_hdl.capture;
}

static void wireless_usb_capture_stop(void *capture)
{
    extern void sound_usb_capture_close(void *capture);
    sound_usb_capture_close(capture);
    usb_hdl.capture = NULL;
    usb_hdl.state = 0;

}
#endif

void wireless_multiple_audio_codec_interface_register(void)
{
#if TCFG_LINEIN_ENABLE
    live_audio_mode_setup(LIVE_AUX_CAPTURE_MODE, JLA_CODING_SAMPLERATE, &wireless_aux_mode_ops);
    set_cig_audio_dev_online(LIVE_AUX_CAPTURE_MODE);
#endif

#if TCFG_PC_ENABLE
    live_audio_mode_setup(LIVE_USB_CAPTURE_MODE, SPK_AUDIO_RATE, &wireless_usb_mode_ops);
#endif

#if (CONNECTED_ROLE_CONFIG == ROLE_PERIP)
    live_audio_mode_setup(LIVE_MIC_CAPTURE_MODE, JLA_CODING_SAMPLERATE, &wireless_mic_mode_ops);
    set_cig_audio_dev_online(LIVE_MIC_CAPTURE_MODE);
#endif

#if TCFG_AUDIO_INPUT_IIS
    live_audio_mode_setup(LIVE_IIS_CAPTURE_MODE, TCFG_IIS_SR, &wireless_iis_mode_ops);
    set_cig_audio_dev_online(LIVE_IIS_CAPTURE_MODE);
#endif
}

#endif  //#if WIRELESS_2T1_DUPLEX_EN

