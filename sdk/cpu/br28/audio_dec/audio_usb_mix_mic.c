#include "asm/includes.h"
#include "media/includes.h"
#include "system/includes.h"
#include "app_main.h"
#include "app_config.h"
#include "audio_config.h"
#include "device/uac_stream.h"
#include "Resample_api.h"
#include "audio_usb_mix_mic.h"

/////////////////// mix fifo start ////////////////////////
#if (SOUNDCARD_ENABLE || MULTI_AUDIO_UPLOAD_TO_UAC_ENABLE)

#if 1
#define mix_fifo_log	        printf
#define mix_fifo_putchar        putchar
#else
#define mix_fifo_log(...)
#define mix_fifo_putchar(...)
#endif /*log_en*/

#define MIX_FIFO_SYNC               (1) // 1:sync enable  0:sync disable

#define MIX_FIFO_BUF_TEMP_LEN       (128)
#define MIX_FIFO_BUF_POINTS         (128)
#define MIX_FIFO_CH_BUF_POINTS      (2048)//(1000)

#define USB_MIC_MIX_RUN             1
#define USB_MIC_MIX_STOP            2

struct mix_fifo {
    struct list_head fifo_list;
    cbuffer_t cbuffer;
    u8 *cbuffer_ram;
#if MIX_FIFO_SYNC
    RS_STUCT_API *sw_src_api;
    u8 *sw_src_buf;
#endif
    u8 tmp_buf[MIX_FIFO_BUF_TEMP_LEN + 24];
    u8 tmp_buf1[MIX_FIFO_BUF_TEMP_LEN];
    u32 input_sample_rate;
    u32 output_sample_rate;
    u8 channel_num;
    OS_MUTEX mutex;
};

struct mix_fifo_ch {
    struct list_head ch_head;
    cbuffer_t cbuffer;
    u8 *cbuffer_ram;
    struct audio_stream_entry entry;    // 音频流入口
    u8 channel_num;         // output channel_num
    u8 input_channel_num;   // input  channel num
    u8 state;       // 1:on 0:off
};

struct mix_fifo *usb_mix_fifo = NULL;
extern u32 audio_output_channel_num(void);
extern void pcm_dual_to_dual_or_single(u8 ch_type, u8 half_lr, s16 *odata, s16 *idata, int len);
int __mix_fifo_write_update(void *fifo_hdl);
static void usb_mic_mix_task(void *arg)
{
    int msg[16];
    int res;
    u8 pend_taskq = 1;

    while (1) {
        res = os_taskq_pend("taskq", msg, ARRAY_SIZE(msg));

        if (res == OS_TASKQ) {
            switch (msg[1]) {
            case USB_MIC_MIX_RUN:
                __mix_fifo_write_update((void *)msg[2]);
                break;
            }
        }
    }
}

void *mix_fifo_init(u8 channel_num, u32 sample_rate)
{
    mix_fifo_log("mix_fifo_init ch:%d sr:%d\n", channel_num, sample_rate);
    struct mix_fifo *fifo = NULL;
    fifo = zalloc(sizeof(struct mix_fifo));
    if (fifo == NULL) {
        mix_fifo_log("mix_fifo hdl malloc err!\n");
        return NULL;
    }
    fifo->cbuffer_ram = zalloc(MIX_FIFO_CH_BUF_POINTS * channel_num * 2);
    if (fifo->cbuffer_ram == NULL) {
        mix_fifo_log("mix_fifo cbuffer_ram malloc err!\n");
        return NULL;
    }
    cbuf_init(&fifo->cbuffer, fifo->cbuffer_ram, MIX_FIFO_CH_BUF_POINTS * channel_num * 2);
    fifo->channel_num = channel_num;
    fifo->input_sample_rate = sample_rate;
    fifo->output_sample_rate = sample_rate;
    INIT_LIST_HEAD(&fifo->fifo_list);

#if MIX_FIFO_SYNC
    fifo->sw_src_api = get_rs16_context();
    ASSERT(fifo->sw_src_api);
    u32 sw_src_need_buf = fifo->sw_src_api->need_buf();
    fifo->sw_src_buf = zalloc(sw_src_need_buf);
    printf("sw src adr:%x size:%d\n", fifo->sw_src_buf, sw_src_need_buf);

    ASSERT(fifo->sw_src_buf);
    RS_PARA_STRUCT rs_para_obj;
    rs_para_obj.nch = fifo->channel_num;
    rs_para_obj.new_insample = fifo->input_sample_rate;
    rs_para_obj.new_outsample = fifo->output_sample_rate;
    mix_fifo_log("sw src,in = %d,out = %d\n", rs_para_obj.new_insample, rs_para_obj.new_outsample);
    fifo->sw_src_api->open(fifo->sw_src_buf, &rs_para_obj);
#endif
    usb_mix_fifo = fifo;
    os_mutex_create(&fifo->mutex);
    int err = task_create(usb_mic_mix_task, NULL, "uac_mix");
    if (err) {
        printf("usb mix mic create task error.\n");
    }
    //gpio_set_pull_down(IO_PORTG_05,1);
    //gpio_set_pull_up(IO_PORTG_05,0);
    //gpio_direction_output(IO_PORTG_05,0);
    return fifo;
}

int mix_fifo_uninit(void *fifo_hdl)
{
    /* if (sw_src_api) { */
    /* sw_src_api = NULL; */
    /* } */
    /* if (sw_src_buf) { */
    /* free(sw_src_buf); */
    /* sw_src_buf = NULL; */
    /* } */
    return 0;
}

int mix_fifo_ch_state_set(void *ch_hdl, u8 en)
{
    mix_fifo_log("mix_fifo_ch_state_set %x %d\n", ch_hdl, en);
    struct mix_fifo_ch *fifo_ch = (struct mix_fifo_ch *)ch_hdl;
    os_mutex_pend(&usb_mix_fifo->mutex, 0);
    if (fifo_ch == NULL) {
        os_mutex_post(&usb_mix_fifo->mutex);
        return -1;
    }
    fifo_ch->state = en;
    os_mutex_post(&usb_mix_fifo->mutex);
    return 0;
}

static void mix_fifo_output_data_process_len(struct audio_stream_entry *entry,  int len)
{
}

static int mix_fifo_data_handler(struct audio_stream_entry *entry,
                                 struct audio_data_frame *in,
                                 struct audio_data_frame *out)
{
    struct mix_fifo_ch *fifo_ch = container_of(entry, struct mix_fifo_ch, entry);

    out->data = in->data;
    out->data_len = in->data_len;

    if (in->data_len - in->offset > 0) {
        if (fifo_ch) {
            mix_fifo_ch_write(fifo_ch, in->data + in->offset / 2, in->data_len - in->offset);
        }
    }

    return in->data_len;
}

void *mix_fifo_ch_open(void *fifo_hdl, u8 input_channel_num)
{
    mix_fifo_log("mix_fifo_ch_open %x\n", fifo_hdl);
    struct mix_fifo *fifo = (struct mix_fifo *)fifo_hdl;
    struct mix_fifo_ch *fifo_ch = NULL;
    if (fifo == NULL) {
        return NULL;
    }
    fifo_ch = zalloc(sizeof(struct mix_fifo_ch));
    if (fifo_ch == NULL) {
        mix_fifo_log("mix_fifo_ch hdl malloc err!\n");
        return NULL;
    }
    fifo_ch->cbuffer_ram = zalloc(MIX_FIFO_CH_BUF_POINTS * fifo->channel_num * 2);
    if (fifo_ch->cbuffer_ram == NULL) {
        mix_fifo_log("mix_fifo_ch cbuffer_ram malloc err!\n");
        return NULL;
    }
    cbuf_init(&fifo_ch->cbuffer, fifo_ch->cbuffer_ram, MIX_FIFO_CH_BUF_POINTS * fifo->channel_num * 2);
    fifo_ch->channel_num = fifo->channel_num;
    fifo_ch->input_channel_num = input_channel_num;
    INIT_LIST_HEAD(&(fifo_ch->ch_head));
    fifo_ch->entry.data_process_len = mix_fifo_output_data_process_len;
    fifo_ch->entry.data_handler = mix_fifo_data_handler;
    fifo_ch->state = 1;
    list_add(&fifo_ch->ch_head, &fifo->fifo_list);
    return fifo_ch;
}

int mix_fifo_ch_close(void *ch_hdl)
{
    mix_fifo_log("mix_fifo_ch_close %x\n", ch_hdl);
    struct mix_fifo_ch *fifo_ch = (struct mix_fifo_ch *)ch_hdl;
    struct mix_fifo *fifo = (struct mix_fifo *)usb_mix_fifo;
    if (!fifo) {
        return 0;
    }
    os_mutex_pend(&fifo->mutex, 0);
    if (fifo_ch == NULL) {
        os_mutex_post(&fifo->mutex);
        return -1;
    }
    if (fifo_ch->cbuffer_ram != NULL) {
        free(fifo_ch->cbuffer_ram);
        fifo_ch->cbuffer_ram = NULL;
    }

    list_del(&fifo_ch->ch_head);
    free(fifo_ch);
    fifo_ch = NULL;
    os_mutex_post(&fifo->mutex);
    return 0;
}

s16 temp_bbuf[64] = {0};
int mix_fifo_ch_write(void *ch_hdl, s16 *data, u32 len)
{
    u8 channel_num = 0;//
    int wlen = 0;
    os_mutex_pend(&usb_mix_fifo->mutex, 0);
    struct mix_fifo_ch *fifo_ch = (struct mix_fifo_ch *)ch_hdl;
    if (fifo_ch == NULL) {
        os_mutex_post(&usb_mix_fifo->mutex);
        return 0;
    }

    if (len == 0) {
        os_mutex_post(&usb_mix_fifo->mutex);
        return 0;
    }

    channel_num = fifo_ch->input_channel_num;
    if (channel_num == 2) {
        u32 remain_len = len;
        u32 ret_len = 0;
        while (remain_len) {
            if (remain_len > 256) {
                pcm_dual_to_dual_or_single(AUDIO_CH_DIFF, 1, temp_bbuf, data, 256);
                ret_len = cbuf_write(&fifo_ch->cbuffer, temp_bbuf, 128);
                if (ret_len == 0) {
                    break;
                }
                wlen += ret_len;
                remain_len -= 256;
                data += 128;
            } else {
                pcm_dual_to_dual_or_single(AUDIO_CH_DIFF, 1, temp_bbuf, data, remain_len);
                ret_len = cbuf_write(&fifo_ch->cbuffer, temp_bbuf, remain_len / 2);
                wlen += ret_len;
                remain_len = 0;
                break;
            }
        }
    } else {

        wlen = cbuf_write(&fifo_ch->cbuffer, data, len);
    }

    if (wlen * channel_num != len) {
        /* mix_fifo_putchar('c'); */
    }
    os_mutex_post(&usb_mix_fifo->mutex);
    return wlen * channel_num;
}

void *mix_fifo_ch_get_entry(void *ch_hdl)
{
    mix_fifo_log("mix_fifo_ch_get_entry %x\n", ch_hdl);
    struct mix_fifo_ch *fifo_ch = (struct mix_fifo_ch *)ch_hdl;
    os_mutex_pend(&usb_mix_fifo->mutex, 0);
    if (fifo_ch == NULL) {
        os_mutex_post(&usb_mix_fifo->mutex);
        return NULL;
    }
    os_mutex_post(&usb_mix_fifo->mutex);
    return (void *)(&(fifo_ch->entry));
}

int __mix_fifo_write_update(void *fifo_hdl)
{
    //gpio_direction_output(IO_PORTG_05,1);
    struct mix_fifo *fifo = (struct mix_fifo *)fifo_hdl;
    struct mix_fifo_ch *ch = NULL;
    u32 update_len = 0;
    u32 mix_len = 0;
    u32 free_len = 0;
    s16 *data = NULL;
    s16 *data1 = NULL;
    u32 len = 0;
    u32 wlen = 0;

    os_mutex_pend(&fifo->mutex, 0);
    if (fifo == NULL) {
        //gpio_direction_output(IO_PORTG_05,0);
        os_mutex_post(&fifo->mutex);
        return 0;
    }

    while (1) {
        mix_len = 0xFFFF;
        list_for_each_entry(ch, &fifo->fifo_list, ch_head) {
            if (ch->cbuffer.data_len < mix_len) {
                mix_len = ch->cbuffer.data_len;
            }
        }

        if (mix_len == 0xFFFF || mix_len == 0) {
            //gpio_direction_output(IO_PORTG_05,0);
            os_mutex_post(&fifo->mutex);
            return update_len;
        }

        if (mix_len > MIX_FIFO_BUF_TEMP_LEN) {
            mix_len = MIX_FIFO_BUF_TEMP_LEN;
        }

        free_len = fifo->cbuffer.total_len - fifo->cbuffer.data_len;
        if (mix_len > free_len) {
            mix_len = free_len;
            if (mix_len == 0) {
                //gpio_direction_output(IO_PORTG_05,0);
                os_mutex_post(&fifo->mutex);
                return update_len;
            }
        }

        memset(&fifo->tmp_buf, 0x00, MIX_FIFO_BUF_TEMP_LEN);
        memset(&fifo->tmp_buf1, 0x00, MIX_FIFO_BUF_TEMP_LEN);
        list_for_each_entry(ch, &fifo->fifo_list, ch_head) {
            data = &fifo->tmp_buf;
            data1 = &fifo->tmp_buf1;
            len = mix_len / 2;
            cbuf_read(&ch->cbuffer, data, mix_len);
            while (len--) {
                *data1 = data_sat_s16((s32) * data1 + (s32) * data);
                data++;
                data1++;
            }
        }

        data1 = &fifo->tmp_buf1;

#if MIX_FIFO_SYNC
        if (fifo->cbuffer.data_len > fifo->cbuffer.total_len * 3 / 4) {
            fifo->input_sample_rate += 5;
            /* mix_fifo_putchar('>'); */
            if (fifo->input_sample_rate > fifo->output_sample_rate + 100) {
                fifo->input_sample_rate = fifo->output_sample_rate + 100;
                /* mix_fifo_putchar('}'); */
            }
            fifo->sw_src_api->set_sr(fifo->sw_src_buf, fifo->input_sample_rate);
        } else if (fifo->cbuffer.data_len < fifo->cbuffer.total_len * 2 / 4) {
            fifo->input_sample_rate -= 5;
            /* mix_fifo_putchar('<'); */
            if (fifo->input_sample_rate < fifo->output_sample_rate - 100) {
                fifo->input_sample_rate = fifo->output_sample_rate - 100;
                /* mix_fifo_putchar('{'); */
            }
            fifo->sw_src_api->set_sr(fifo->sw_src_buf, fifo->input_sample_rate);
        }

        if ((fifo->sw_src_api != NULL) \
            && (fifo->sw_src_buf != NULL)) {
            u32 outlen = fifo->sw_src_api->run(fifo->sw_src_buf, data1, mix_len >> 1, data);
            outlen <<= 1;
            wlen = cbuf_write(&fifo->cbuffer, data, outlen);
            if (wlen != outlen) {
                /* mix_fifo_putchar('a'); */
            }
        } else {
            printf("mix fifo sw src is NULL\n");
        }
#else
        wlen = cbuf_write(&fifo->cbuffer, data1, mix_len);
        if (wlen != mix_len) {
            mix_fifo_putchar('a');
        }
#endif // #if MIX_FIFO_SYNC

        update_len += mix_len;
    }

}

int mix_fifo_write_update(void *fifo_hdl)
{
    int err = os_taskq_post_msg("uac_mix", 2, USB_MIC_MIX_RUN, (int)fifo_hdl);
    if (err) {
        r_printf("mix fifo write update error\n");
    }

    return err;
}
int mix_fifo_read(void *fifo_hdl, s16 *data, u32 len)
{
    struct mix_fifo *fifo = (struct mix_fifo *)fifo_hdl;
    if (fifo == NULL) {
        return 0;
    }

    u32 rlen = fifo->cbuffer.data_len;
    if (rlen < len) {
        rlen = cbuf_read(&fifo->cbuffer, data, rlen);
    } else {
        rlen = cbuf_read(&fifo->cbuffer, data, len);
    }
    return rlen;
}

/////////////////// mix fifo end ////////////////////////



static int usb_mix_fifo_tx_handler(int event, void *data, int len)
{
    memset(data, 0x00, len);
    mix_fifo_write_update(usb_mix_fifo);
    u32 rlen = mix_fifo_read(usb_mix_fifo, data, len);
    if (rlen != len) {
        mix_fifo_putchar('h');
    }
    return len;
}

int usb_audio_soundcard_mic_open(void *_info)
{
    u32 sample_rate = (u32)_info & 0xFFFFFF;
    u32 channels = (u32)_info >> 24;
    mix_fifo_log("usb_audio_soundcard_mic_open %d %d\n", sample_rate, channels);
    set_uac_mic_tx_handler(NULL, usb_mix_fifo_tx_handler);
    return 0;
}

int usb_audio_soundcard_mic_close(void *arg)
{
    return 0;
}

void usb_audio_soundcard_mic_set_gain(int gain)
{
    return;
}

#endif // #if (SOUNDCARD_ENABLE)


