#include "asm/includes.h"
#include "media/includes.h"
#include "system/includes.h"
#include "audio_enc/audio_enc.h"
#include "app_main.h"
#include "app_config.h"
#include "audio_splicing.h"
#include "media/audio_echo_reverb.h"
#include "audio_config.h"
#include "media/24bit_convert.h"
#include "media/audio_def.h"

/*USB MIC上行数据同步及变采样模块使能*/
#define USB_MIC_SRC_ENABLE     1

#if TCFG_USB_MIC_CVP_ENABLE
#include "aec_user.h"
#endif/*TCFG_USB_MIC_CVP_ENABLE*/

#if (TCFG_PC_ENABLE)
#include "device/uac_stream.h"

#if USB_MIC_SRC_ENABLE
#ifdef CONFIG_MEDIA_DEVELOP_ENABLE
#include "audio_track.h"
#endif
#include "Resample_api.h"
#endif/*USB_MIC_SRC_ENABLE*/

#define PCM_ENC2USB_OUTBUF_LEN		(4 * 1024)

#if TCFG_PC_FOUR_MIC
#define USB_MIC_CH_NUM         4
#elif TCFG_AUDIO_DUAL_MIC_ENABLE
#define USB_MIC_CH_NUM         2
#else /*single mic*/
#define USB_MIC_CH_NUM         1
#endif

#define USB_MIC_BUF_NUM        3
#define USB_MIC_IRQ_POINTS     256
#define USB_MIC_BUFS_SIZE      (USB_MIC_CH_NUM * USB_MIC_BUF_NUM * USB_MIC_IRQ_POINTS)

#define USB_MIC_STOP  0x00
#define USB_MIC_START 0x01

/*数据输出开头丢掉的数据包数*/
#define ADC_OUT_DUMP_PACKET    2

extern struct audio_adc_hdl adc_hdl;
extern u16 uac_get_mic_vol(const u8 usb_id);
extern int usb_output_sample_rate();
extern int usb_audio_mic_write_base(void *data, u16 len);

#if USB_MIC_SRC_ENABLE
typedef struct {
    u8 start;
    u8 busy;
    u16 in_sample_rate;
    u32 *runbuf;
    s16 output[320 * 3 + 64];
    RS_STUCT_API *ops;
    void *audio_track;
    u8 input_ch;
} usb_mic_sw_src_t;
static usb_mic_sw_src_t *usb_mic_src = NULL;
#endif/*USB_MIC_SRC_ENABLE*/

struct _usb_mic_hdl {
    struct audio_adc_output_hdl adc_output;
    struct adc_mic_ch    mic_ch;
    //struct adc_linein_ch linein_ch;
    enum enc_source source;

    cbuffer_t output_cbuf;
    u8 *output_buf;//[PCM_ENC2USB_OUTBUF_LEN];
    u8 rec_tx_channels;
    u8 mic_data_ok;/*mic数据等到一定长度再开始发送*/
    u8 status;
    u8 mic_busy;
#if	TCFG_USB_MIC_ECHO_ENABLE
    ECHO_API_STRUCT *p_echo_hdl;
#endif
#if ((USB_MIC_CH_NUM == 2) || TCFG_CVP_REF_IN_ADC_ENABLE)
    s16 tmp_buf[USB_MIC_IRQ_POINTS];
#endif/*USB_MIC_CH_NUM*/
    u8 fade_in;
    u32 fade_in_gain;
    u32 dump_packet;
    u8 *buf_16;
#if (defined TCFG_AUDIO_ADC_BIT_MODE) && (TCFG_AUDIO_ADC_BIT_MODE == ADC_BIT_WIDTH_24)
    void *mic_buf;
    int mic_buf_size;
#endif /*(defined TCFG_AUDIO_ADC_BIT_MODE) && (TCFG_AUDIO_ADC_BIT_MODE == ADC_BIT_WIDTH_24)*/
};

#if TCFG_PC_FOUR_MIC
typedef struct {
    struct audio_adc_output_hdl output;
    struct adc_mic_ch mic_ch;
    adc_type_t adc_buf[USB_MIC_BUFS_SIZE];    //align 4Bytes
} audio_adc_t;
audio_adc_t *ladc_var = NULL;
#endif
static struct _usb_mic_hdl *usb_mic_hdl = NULL;
#if TCFG_USB_MIC_ECHO_ENABLE
struct echo_update_parm usbmic_echo_parm_default = {
    .delay = 200,				//回声的延时时间 0-300ms
    .decayval = 50,				// 0-70%
    .direct_sound_enable = 1,	//直达声使能  0/1
    .filt_enable = 1,			//发散滤波器使能
};
EF_REVERB_FIX_PARM usbmic_echo_fix_parm_default = {
    .wetgain = 2048,			////湿声增益：[0:4096]
    .drygain = 4096,				////干声增益: [0:4096]
    .sr = MIC_EFFECT_SAMPLERATE,		////采样率
    .max_ms = 200,				////所需要的最大延时，影响 need_buf 大小
};
#endif

#if (TCFG_AUDIO_INPUT_IIS && TCFG_IIS_MIC_ENABLE)
#include "audio_link.h"
#include "Resample_api.h"
static struct _usb_iis_mic_hdl {
    u32 in_sample_rate;
    u32 out_sample_rate;
    u32 sr_cal_timer;
    u32 points_total;
    RS_STUCT_API *sw_src_api;
    u8 *sw_src_buf;
    u32 alink_cur_hwp;
    u32 alink_last_hwp;
    u32 alink_points_cnt;
};
static struct _usb_iis_mic_hdl *usb_iis_mic_hdl = NULL;
static s16 temp_iis_input_data[ALNK_BUF_POINTS_NUM * 3] = {0};
extern ALINK_PARM alink0_platform_data;
extern void *hw_alink;
static void iis_output_to_cbuf(void *priv, s16 *data, u16 len)
{
    if (usb_mic_hdl == NULL) {
        return ;
    }
    if (usb_mic_hdl->status == USB_MIC_STOP) {
        return ;
    }

    u16 temp_len = len;

#if TCFG_IIS_MODE       //slave
    usb_iis_mic_hdl->alink_cur_hwp = alink_get_hwptr(&alink0_platform_data.ch_cfg[1]);
    if (usb_iis_mic_hdl->alink_cur_hwp < usb_iis_mic_hdl->alink_last_hwp) {
        usb_iis_mic_hdl->alink_points_cnt += usb_iis_mic_hdl->alink_cur_hwp + JL_ALNK0->LEN - usb_iis_mic_hdl->alink_last_hwp;
    } else {
        usb_iis_mic_hdl->alink_points_cnt += usb_iis_mic_hdl->alink_cur_hwp - usb_iis_mic_hdl->alink_last_hwp;
    }
    usb_iis_mic_hdl->alink_last_hwp = usb_iis_mic_hdl->alink_cur_hwp;
#endif

    for (int i = 0; i < len / 2 / 2; i++) {
        data[i] = data[2 * i];
    }
    len >>= 1;

    switch (usb_mic_hdl->source) {
    case ENCODE_SOURCE_MIC:
    case ENCODE_SOURCE_LINE0_LR:
    case ENCODE_SOURCE_LINE1_LR:
    case ENCODE_SOURCE_LINE2_LR: {
#if TCFG_USB_MIC_CVP_ENABLE
        audio_aec_inbuf(data, len);
#else
#if	TCFG_USB_MIC_ECHO_ENABLE
        if (usb_mic_hdl->p_echo_hdl) {
            run_echo(usb_mic_hdl->p_echo_hdl, data, data, len);
        }
#endif
#if TCFG_PC_FOUR_MIC
        usb_audio_mic_write_base(data, len * 4);

#else
        usb_audio_mic_write_base(data, len);
#endif
#endif
    }
    break;
    default:
        break;
    }
    alink_set_shn(&alink0_platform_data.ch_cfg[1], temp_len / 4);
}
static void audio_usb_iis_sr_cal_timer(void *priv)
{
    if (usb_iis_mic_hdl) {
        usb_iis_mic_hdl->alink_cur_hwp = alink_get_hwptr(&alink0_platform_data.ch_cfg[1]);
        if (usb_iis_mic_hdl->alink_cur_hwp < usb_iis_mic_hdl->alink_last_hwp) {
            usb_iis_mic_hdl->alink_points_cnt += usb_iis_mic_hdl->alink_cur_hwp + JL_ALNK0->LEN - usb_iis_mic_hdl->alink_last_hwp;
        } else {
            usb_iis_mic_hdl->alink_points_cnt += usb_iis_mic_hdl->alink_cur_hwp - usb_iis_mic_hdl->alink_last_hwp;
        }
        usb_iis_mic_hdl->alink_last_hwp = usb_iis_mic_hdl->alink_cur_hwp;
        usb_iis_mic_hdl->in_sample_rate = usb_iis_mic_hdl->alink_points_cnt;
        usb_iis_mic_hdl->alink_points_cnt = 0;
        /* printf("in_sr : %d, len %d\n", usb_iis_mic_hdl->in_sample_rate, usb_mic_hdl->output_cbuf.data_len); */
        if (usb_mic_hdl->output_cbuf.data_len >= PCM_ENC2USB_OUTBUF_LEN * 3 / 4) {
            usb_iis_mic_hdl->in_sample_rate += usb_iis_mic_hdl->in_sample_rate * 5 / 10000;
            /* putchar('+'); */
        } else if (usb_mic_hdl->output_cbuf.data_len <= PCM_ENC2USB_OUTBUF_LEN / 4) {
            usb_iis_mic_hdl->in_sample_rate -= usb_iis_mic_hdl->in_sample_rate * 5 / 10000;
            /* putchar('-'); */
        }
        if (usb_iis_mic_hdl->sw_src_api && usb_iis_mic_hdl->sw_src_buf) {
            usb_iis_mic_hdl->sw_src_api->set_sr(usb_iis_mic_hdl->sw_src_buf, usb_iis_mic_hdl->in_sample_rate);
        }
    }
}
#endif //TCFG_AUDIO_INPUT_IIS && TCFG_IIS_MIC_ENABLE

static int usb_audio_mic_sync(u32 data_size)
{
#if 0
    int change_point = 0;

    if (data_size > __this->rec_top_size) {
        change_point = -1;
    } else if (data_size < __this->rec_bottom_size) {
        change_point = 1;
    }

    if (change_point) {
        struct audio_pcm_src src;
        src.resample = 0;
        src.ratio_i = (1024) * 2;
        src.ratio_o = (1024 + change_point) * 2;
        src.convert = 1;
        dev_ioctl(__this->rec_dev, AUDIOC_PCM_RATE_CTL, (u32)&src);
    }
#endif

    return 0;
}
static int usb_audio_mic_tx_handler(int event, void *data, int len)
{
    if (usb_mic_hdl == NULL) {
        return 0;
    }
    if (usb_mic_hdl->status == USB_MIC_STOP) {
        return 0;
    }

    int i = 0;
    int r_len = 0;
    u8 ch = 0;
    u8 double_read = 0;

    int rlen = 0;

    if (usb_mic_hdl->mic_data_ok == 0) {
        if (usb_mic_hdl->output_cbuf.data_len > (PCM_ENC2USB_OUTBUF_LEN / 4)) {
            usb_mic_hdl->mic_data_ok = 1;
        } else {
            //y_printf("mic_tx NULL\n");
            memset(data, 0, len);
            return 0;
        }
    }

    /* usb_audio_mic_sync(size); */
#if (MIC_AUDIO_RES == 24)
    if (usb_mic_hdl->rec_tx_channels == 2) {
        rlen = cbuf_get_data_size(&usb_mic_hdl->output_cbuf);
        len = len * 2 / 3;
        if (!usb_mic_hdl->buf_16) {
            usb_mic_hdl->buf_16 = zalloc(len);
        }
        if (rlen) {
            rlen = rlen > (len / 2) ? (len / 2) : rlen;
            rlen = cbuf_read(&usb_mic_hdl->output_cbuf, usb_mic_hdl->buf_16, rlen);
        } else {
            /* printf("uac read err1\n"); */
            usb_mic_hdl->mic_data_ok = 0;
            return 0;
        }

        len = rlen * 2;
        s16 *tx_pcm = (s16 *)usb_mic_hdl->buf_16;
        int cnt = len / 2;
        for (cnt = len / 2; cnt >= 2;) {
            tx_pcm[cnt - 1] = tx_pcm[cnt / 2 - 1];
            tx_pcm[cnt - 2] = tx_pcm[cnt / 2 - 1];
            cnt -= 2;
        }
        rlen = len;
    } else {
        rlen = cbuf_get_data_size(&usb_mic_hdl->output_cbuf);
        len = len * 2 / 3;
        if (!usb_mic_hdl->buf_16) {
            usb_mic_hdl->buf_16 = zalloc(len);
        }
        if (rlen) {
            rlen = rlen > len ? len : rlen;
            rlen = cbuf_read(&usb_mic_hdl->output_cbuf, usb_mic_hdl->buf_16, rlen);
        } else {
            /* printf("uac read err2\n"); */
            usb_mic_hdl->mic_data_ok = 0;
            return 0;
        }
    }
    _16bit_to_threebyte_24bit((short *)usb_mic_hdl->buf_16, (int *)data, rlen / 2);
    return rlen * 3 / 2;
#else
    if (usb_mic_hdl->rec_tx_channels == 2) {
        rlen = cbuf_get_data_size(&usb_mic_hdl->output_cbuf);
        if (rlen) {
            rlen = rlen > (len / 2) ? (len / 2) : rlen;
            rlen = cbuf_read(&usb_mic_hdl->output_cbuf, data, rlen);
        } else {
            /* printf("uac read err1\n"); */
            usb_mic_hdl->mic_data_ok = 0;
            return 0;
        }

        len = rlen * 2;
        s16 *tx_pcm = (s16 *)data;
        int cnt = len / 2;
        for (cnt = len / 2; cnt >= 2;) {
            tx_pcm[cnt - 1] = tx_pcm[cnt / 2 - 1];
            tx_pcm[cnt - 2] = tx_pcm[cnt / 2 - 1];
            cnt -= 2;
        }
        rlen = len;
    } else {
        rlen = cbuf_get_data_size(&usb_mic_hdl->output_cbuf);
        if (rlen) {
            rlen = rlen > len ? len : rlen;
            rlen = cbuf_read(&usb_mic_hdl->output_cbuf, data, rlen);
        } else {
            /* printf("uac read err2\n"); */
            usb_mic_hdl->mic_data_ok = 0;
            return 0;
        }
    }
    return rlen;
#endif
}



int usb_audio_mic_write_base(void *data, u16 len)
{
    int outlen = len;
    s16 *obuf = data;

#if (TCFG_AUDIO_INPUT_IIS && TCFG_IIS_MIC_ENABLE)
    if (usb_iis_mic_hdl->sw_src_api && usb_iis_mic_hdl->sw_src_buf) {
        outlen = usb_iis_mic_hdl->sw_src_api->run(usb_iis_mic_hdl->sw_src_buf, data, len >> 1, temp_iis_input_data);
        outlen = outlen << 1;
        obuf = temp_iis_input_data;
    }
#else
#if USB_MIC_SRC_ENABLE
    if (usb_mic_src && usb_mic_src->start) {
        usb_mic_src->busy = 1;
#ifdef CONFIG_MEDIA_DEVELOP_ENABLE
        u32 sr = usb_output_sample_rate();
        usb_mic_src->ops->set_sr(usb_mic_src->runbuf, sr);
#endif
        outlen = usb_mic_src->ops->run(usb_mic_src->runbuf, data, len >> 1, usb_mic_src->output);
        usb_mic_src->busy = 0;
        ASSERT(outlen <= (sizeof(usb_mic_src->output) >> 1));
        /* printf("16->48k:%d,%d,%d\n",len >> 1,outlen,sizeof(sw_src->output)); */
        obuf = usb_mic_src->output;
        outlen = outlen << 1;
    }

#endif/*USB_MIC_SRC_ENABLE*/
#endif
    //丢掉前几包且做淡入操作，解决录音文件开头pop声问题
    if (usb_mic_hdl->dump_packet) {
        usb_mic_hdl->dump_packet--;
        memset(obuf, 0, outlen);
    } else {
        if (usb_mic_hdl->fade_in) {
            int tmp_data;
            int samples = len >> 1;
            for (int i = 0; i < samples; i++) {
                tmp_data = obuf[i];
                obuf[i] = (tmp_data * usb_mic_hdl->fade_in_gain) >> 14;
                usb_mic_hdl->fade_in_gain += 1;
                if (usb_mic_hdl->fade_in_gain >= 16384) {
                    usb_mic_hdl->fade_in = 0;
                    break;
                }
            }
        }
    }

    int wlen = cbuf_write(&usb_mic_hdl->output_cbuf, obuf, outlen);
    if (wlen != (outlen)) {
        putchar('L');
        //r_printf("usb_mic write full:%d-%d\n", wlen, len);
    }
    return wlen;

}

int usb_audio_mic_write_do(void *data, u16 len)
{
    int wlen = len;
    if (usb_mic_hdl && usb_mic_hdl->status == USB_MIC_START) {
        usb_mic_hdl->mic_busy = 1;

        wlen = usb_audio_mic_write_base(data, len);

        usb_mic_hdl->mic_busy = 0;
    }
    return wlen;
}



int usb_audio_mic_write(void *data, u16 len)
{
    pcm_dual_to_single(data, data, len);
    int wlen = usb_audio_mic_write_do(data, len / 2);
    return wlen;
}

int audio_data_24bit_to_16bit(struct _usb_mic_hdl *hdl, void *data, int len)
{
#if (defined TCFG_AUDIO_ADC_BIT_MODE) && (TCFG_AUDIO_ADC_BIT_MODE == ADC_BIT_WIDTH_24)
    if (hdl->mic_buf_size != len && hdl->mic_buf) {
        free(hdl->mic_buf);
        hdl->mic_buf = 0;
    }

    if (!hdl->mic_buf) {
        hdl->mic_buf = malloc(len);
        hdl->mic_buf_size = len;
    }

    memcpy(hdl->mic_buf, data, len);
    int *pcm_24bit = (int *)hdl->mic_buf;
    s16 *pcm_16bit = (s16 *)hdl->mic_buf;
    int i = 0, pcms = len >> 2;
    for (i = 0; i < pcms; i++) {
        pcm_16bit[i] = pcm_24bit[i] >> 8;
    }
#endif /*(defined TCFG_AUDIO_ADC_BIT_MODE) && (TCFG_AUDIO_ADC_BIT_MODE == ADC_BIT_WIDTH_24)*/
    return len >> 1;
}

static void adc_output_to_cbuf(void *priv, s16 *mic_data, int len)
{
    s16 *data = mic_data;
    if (usb_mic_hdl == NULL) {
        return ;
    }
    if (usb_mic_hdl->status == USB_MIC_STOP) {
        return ;
    }

    switch (usb_mic_hdl->source) {
    case ENCODE_SOURCE_MIC:
    case ENCODE_SOURCE_LINE0_LR:
    case ENCODE_SOURCE_LINE1_LR:
    case ENCODE_SOURCE_LINE2_LR: {
#if (defined TCFG_AUDIO_ADC_BIT_MODE) && (TCFG_AUDIO_ADC_BIT_MODE == ADC_BIT_WIDTH_24)
        audio_data_24bit_to_16bit(usb_mic_hdl, data, len);
        data = usb_mic_hdl->mic_buf;
        len >>= 1;
#endif /*(defined TCFG_AUDIO_ADC_BIT_MODE) && (TCFG_AUDIO_ADC_BIT_MODE == ADC_BIT_WIDTH_24)*/

#if USB_MIC_SRC_ENABLE
#ifdef CONFIG_MEDIA_DEVELOP_ENABLE
        if (usb_mic_src && usb_mic_src->audio_track) {
            audio_local_sample_track_in_period(usb_mic_src->audio_track, (len >> 1) / usb_mic_src->input_ch);
        }
#endif
#endif/*USB_MIC_SRC_ENABLE*/

#if TCFG_USB_MIC_CVP_ENABLE

#if TCFG_CVP_REF_IN_ADC_ENABLE
        u8 *ch_num = (u8 *)priv;
        s16 *mic0_data = data;
        s16 *ref0_data = usb_mic_hdl->tmp_buf;
        s16 *ref0_data_pos = data + (len >> 1);

        if (*ch_num == 2) {
            for (u16 i = 0; i < (len >> 1); i++) {
                mic0_data[i] = data[i * 2 + 0];
                ref0_data[i] = data[i * 2 + 1];
            }
        } else if (*ch_num == 3) {
            for (u16 i = 0; i < (len >> 1); i++) {
                mic0_data[i] = data[i * 3 + 0];
                ref0_data[i] = (short)(((int)data[i * 3 + 1] + (int)data[i * 3 + 2]) >> 1);
            }
        }

        memcpy(ref0_data_pos, ref0_data, len);
        audio_aec_in_refbuf(ref0_data_pos, len);
        audio_aec_inbuf(mic0_data, len);

#else /*TCFG_CVP_REF_IN_ADC_ENABLE == 0*/
#if (USB_MIC_CH_NUM == 2)/*DualMic*/
        s16 *mic0_data = data;
        s16 *mic1_data = usb_mic_hdl->tmp_buf;
        s16 *mic1_data_pos = data + (len / 2);
        //printf("mic_data:%x,%x,%d\n",data,mic1_data_pos,len);
        for (u16 i = 0; i < (len >> 1); i++) {
            mic0_data[i] = data[i * 2];
            mic1_data[i] = data[i * 2 + 1];
        }
        memcpy(mic1_data_pos, mic1_data, len);

#if 0 /*debug*/
        static u16 mic_cnt = 0;
        if (mic_cnt++ > 300) {
            putchar('1');
            audio_aec_inbuf(mic1_data_pos, len);
            if (mic_cnt > 600) {
                mic_cnt = 0;
            }
        } else {
            putchar('0');
            audio_aec_inbuf(mic0_data, len);
        }
        return;
#endif/*debug end*/
#if (TCFG_AUDIO_DMS_MIC_MANAGE == DMS_MASTER_MIC0)
        audio_aec_inbuf_ref(mic1_data_pos, len);
        audio_aec_inbuf(data, len);
#else
        audio_aec_inbuf_ref(data, len);
        audio_aec_inbuf(mic1_data_pos, len);
#endif/*TCFG_AUDIO_DMS_MIC_MANAGE*/
#else/*SingleMic*/
        audio_aec_inbuf(data, len);
#endif/*USB_MIC_CH_NUM*/
#endif /*TCFG_CVP_REF_IN_ADC_ENABLE*/

#else /*TCFG_USB_MIC_CVP_ENABLE*/
#if	TCFG_USB_MIC_ECHO_ENABLE
        if (usb_mic_hdl->p_echo_hdl) {
            run_echo(usb_mic_hdl->p_echo_hdl, data, data, len);
        }
#endif
#if TCFG_PC_FOUR_MIC
        usb_audio_mic_write_base(data, len * 4);

#else
        usb_audio_mic_write_base(data, len);
#endif
#endif
    }
    break;
    default:
        break;
    }
}

#if USB_MIC_SRC_ENABLE
static int sw_src_init(u32 in_sr, u32 out_sr)
{
    usb_mic_src = zalloc(sizeof(usb_mic_sw_src_t));
    ASSERT(usb_mic_src);
    /* usb_mic_src->ops = get_rs16_context(); */
    usb_mic_src->ops = get_rsfast_context();
    ASSERT(usb_mic_src->ops);
    u32 need_buf = usb_mic_src->ops->need_buf();
    printf("sw_src need_buf:%d\n", need_buf);
    usb_mic_src->runbuf = zalloc(need_buf);
    ASSERT(usb_mic_src->runbuf);

    RS_PARA_STRUCT rs_para_obj;
    rs_para_obj.nch = 1;
    rs_para_obj.new_insample = in_sr;
    rs_para_obj.new_outsample = out_sr;
    printf("sw src,in = %d,out = %d\n", rs_para_obj.new_insample, rs_para_obj.new_outsample);
    usb_mic_src->ops->open(usb_mic_src->runbuf, &rs_para_obj);

#ifdef CONFIG_MEDIA_DEVELOP_ENABLE
    usb_mic_src->input_ch = rs_para_obj.nch;
    usb_mic_src->in_sample_rate = in_sr;
    usb_mic_src->audio_track = audio_local_sample_track_open(usb_mic_src->input_ch, in_sr, 1000);
#endif

    usb_mic_src->start = 1;
    return 0;
}

static int sw_src_exit(void)
{
    if (usb_mic_src) {
        usb_mic_src->start = 0;
        while (usb_mic_src->busy) {
            putchar('w');
            os_time_dly(2);
        }
#ifdef CONFIG_MEDIA_DEVELOP_ENABLE
        audio_local_sample_track_close(usb_mic_src->audio_track);
        usb_mic_src->audio_track = NULL;
#endif

        local_irq_disable();
        if (usb_mic_src->runbuf) {
            free(usb_mic_src->runbuf);
            usb_mic_src->runbuf = 0;
        }
        free(usb_mic_src);
        usb_mic_src = NULL;
        local_irq_enable();
        printf("sw_src_exit\n");
    }
    printf("sw_src_exit ok\n");
    return 0;
}
#endif/*USB_MIC_SRC_ENABLE*/

#if TCFG_USB_MIC_CVP_ENABLE
static int usb_mic_aec_output(s16 *data, u16 len)
{
    //putchar('k');
    if (usb_mic_hdl == NULL) {
        return len ;
    }
    if (usb_mic_hdl->status == USB_MIC_STOP) {
        return len;
    }

    u16 olen = len;
    s16 *obuf = data;

    usb_audio_mic_write_do(obuf, olen);
    return len;
}
#endif /*TCFG_USB_MIC_CVP_ENABLE*/

int uac_mic_vol_switch(int vol)
{
    return vol * 14 / 100;
}

int usb_audio_mic_open(void *_info)
{
    printf("usb_audio_mic_open -----------------------------------------------------\n");
    if (usb_mic_hdl) {
        return 0;
    }
    struct _usb_mic_hdl *hdl = NULL;
    hdl = zalloc(sizeof(struct _usb_mic_hdl));
    if (!hdl) {
        return -EFAULT;
    }

    local_irq_disable();
    usb_mic_hdl = hdl;
    local_irq_enable();

    hdl->status = USB_MIC_STOP;
#if 0
    hdl->adc_buf = zalloc(USB_MIC_BUFS_SIZE * 2);
    if (!hdl->adc_buf) {
        printf("hdl->adc_buf NULL\n");
        if (hdl) {
            free(hdl);
            hdl = NULL;
        }
        return -EFAULT;
    }
#endif
    hdl->output_buf = zalloc(PCM_ENC2USB_OUTBUF_LEN);
    if (!hdl->output_buf) {
        printf("hdl->output_buf NULL\n");
        /* if (hdl->adc_buf) { */
        /* free(hdl->adc_buf); */
        /* } */
        if (hdl) {
            free(hdl);
            hdl = NULL;
        }
        return -EFAULT;
    }

    u32 sample_rate = (u32)_info & 0xFFFFFF;
    u32 output_rate = sample_rate;
    hdl->rec_tx_channels = (u32)_info >> 24;
    hdl->source = ENCODE_SOURCE_MIC;
    printf("usb mic sr:%d ch:%d\n", sample_rate, hdl->rec_tx_channels);

    hdl->fade_in = 1;
    hdl->fade_in_gain = 0;
    hdl->dump_packet = ADC_OUT_DUMP_PACKET;

#if TCFG_USB_MIC_CVP_ENABLE
    printf("usb mic sr[aec]:%d\n", sample_rate);
    sample_rate = 16000;
    //audio_aec_init(sample_rate);
    audio_aec_open(sample_rate, TCFG_USB_MIC_CVP_MODE, usb_mic_aec_output);
#endif/*TCFG_USB_MIC_CVP_ENABLE*/

#if (!TCFG_AUDIO_INPUT_IIS || !TCFG_IIS_MIC_ENABLE)
#if USB_MIC_SRC_ENABLE
    sw_src_init(sample_rate, output_rate);
#endif/*USB_MIC_SRC_ENABLE*/
#endif

    cbuf_init(&hdl->output_cbuf, hdl->output_buf, PCM_ENC2USB_OUTBUF_LEN);

#if TCFG_MIC_EFFECT_ENABLE
    app_var.usb_mic_gain = mic_effect_get_micgain();
#else
    app_var.usb_mic_gain = uac_mic_vol_switch(uac_get_mic_vol(0));
#endif//TCFG_MIC_EFFECT_ENABLE

#if (TCFG_AUDIO_INPUT_IIS && TCFG_IIS_MIC_ENABLE)
    if (!usb_iis_mic_hdl) {
        usb_iis_mic_hdl = zalloc(sizeof(struct _usb_iis_mic_hdl));
    }
#if TCFG_IIS_MODE   //slave
    usb_iis_mic_hdl->alink_last_hwp = 0;
    usb_iis_mic_hdl->alink_cur_hwp = 0;
    usb_iis_mic_hdl->alink_points_cnt = 0;

    usb_iis_mic_hdl->in_sample_rate = TCFG_IIS_INPUT_SR;
    usb_iis_mic_hdl->out_sample_rate = output_rate;
    usb_iis_mic_hdl->sr_cal_timer = sys_hi_timer_add(NULL, audio_usb_iis_sr_cal_timer, 1000);
#else               //master
    usb_iis_mic_hdl->in_sample_rate = TCFG_IIS_SR / 1000 * 625;
    usb_iis_mic_hdl->out_sample_rate = output_rate / 1000 * 624;
#endif //TCFG_IIS_MODE
    extern RS_STUCT_API *get_rs16_context();
    usb_iis_mic_hdl->sw_src_api = get_rs16_context();
    printf("sw_src_api:0x%x\n", usb_iis_mic_hdl->sw_src_api);
    ASSERT(usb_iis_mic_hdl->sw_src_api);
    int sw_src_need_buf = usb_iis_mic_hdl->sw_src_api->need_buf();
    printf("sw_src_buf:%d\n", sw_src_need_buf);
    usb_iis_mic_hdl->sw_src_buf = zalloc(sw_src_need_buf);
    ASSERT(usb_iis_mic_hdl->sw_src_buf);
    RS_PARA_STRUCT rs_para_obj;
    rs_para_obj.nch = 1;
    rs_para_obj.new_insample = usb_iis_mic_hdl->in_sample_rate;
    rs_para_obj.new_outsample = usb_iis_mic_hdl->out_sample_rate;

    printf("sw src,in = %d,out = %d\n", rs_para_obj.new_insample, rs_para_obj.new_outsample);
    usb_iis_mic_hdl->sw_src_api->open(usb_iis_mic_hdl->sw_src_buf, &rs_para_obj);
#if 0 //使用wm8978模块作为输入
    extern u8 WM8978_Init(u8 dacen, u8 adcen);
    WM8978_Init(0, 1);
#endif
#if TCFG_AUDIO_OUTPUT_IIS
    alink_channel_init(hw_alink, 1, ALINK_DIR_RX, NULL, iis_output_to_cbuf); //IIS输入使用通道1
#else // TCFG_AUDIO_OUTPUT_IIS
    hw_alink = alink_init(&alink0_platform_data);
    alink_channel_init(hw_alink, 1, ALINK_DIR_RX, NULL, iis_output_to_cbuf); //IIS输入使用通道1
    alink_start(hw_alink);
#endif // TCFG_AUDIO_OUTPUT_IIS
#else //TCFG_AUDIO_INPUT_IIS && TCFG_IIS_MIC_ENABLE
#if (TCFG_USB_MIC_DATA_FROM_MICEFFECT)
    mic_effect_to_usbmic_onoff(1);
#else
#if  TCFG_PC_FOUR_MIC
    ladc_var = zalloc(sizeof(audio_adc_t));
    if (ladc_var == NULL) {
        printf("ladc_var malloc fail");
        return -1;
    }
    audio_adc_mic_open(&ladc_var->mic_ch, TCFG_AUDIO_ADC_MIC_CHA, &adc_hdl);
    audio_adc_mic1_open(&ladc_var->mic_ch, TCFG_AUDIO_ADC_MIC_CHA, &adc_hdl);
    audio_adc_mic2_open(&ladc_var->mic_ch, TCFG_AUDIO_ADC_MIC_CHA, &adc_hdl);
    audio_adc_mic3_open(&ladc_var->mic_ch, TCFG_AUDIO_ADC_MIC_CHA, &adc_hdl);

    audio_adc_mic_set_sample_rate(&ladc_var->mic_ch, sample_rate);
    audio_adc_mic_set_gain(&ladc_var->mic_ch, app_var.usb_mic_gain);
    audio_adc_mic1_set_gain(&ladc_var->mic_ch, app_var.usb_mic_gain);
    audio_adc_mic2_set_gain(&ladc_var->mic_ch, app_var.usb_mic_gain);
    audio_adc_mic3_set_gain(&ladc_var->mic_ch, app_var.usb_mic_gain);

    audio_adc_mic_set_buffs(&ladc_var->mic_ch, ladc_var->adc_buf, USB_MIC_IRQ_POINTS * 2, USB_MIC_BUF_NUM);
    ladc_var->output.handler = adc_output_to_cbuf;
    audio_adc_add_output_handler(&adc_hdl, &ladc_var->output);
    audio_adc_mic_start(&ladc_var->mic_ch);
#else /*TCFG_PC_FOUR_MIC == 0*/
#if TCFG_CVP_REF_IN_ADC_ENABLE
    int audio_adc_cvp_ref_open(u8 ch, u16 sr, u8 gain, void (*output_handler)(void *priv, s16 * data, int len));
    audio_adc_cvp_ref_open(TCFG_CVP_REF_IN_ADC_CH, sample_rate, app_var.aec_mic_gain, adc_output_to_cbuf);
#else /*TCFG_CVP_REF_IN_ADC_ENABLE == 0*/
    audio_mic_open(&hdl->mic_ch, sample_rate, app_var.usb_mic_gain);
    hdl->adc_output.handler = adc_output_to_cbuf;
    audio_mic_add_output(&hdl->adc_output);
    audio_mic_start(&hdl->mic_ch);
#endif /*TCFG_CVP_REF_IN_ADC_ENABLE*/
#endif /*TCFG_PC_FOUR_MIC*/
#endif//TCFG_USB_MIC_DATA_FROM_MICEFFECT
#endif //TCFG_AUDIO_INPUT_IIS && TCFG_IIS_MIC_ENABLE
#if TCFG_USB_MIC_ECHO_ENABLE
    hdl->p_echo_hdl = open_echo(&usbmic_echo_parm_default, &usbmic_echo_fix_parm_default);
#endif
    set_uac_mic_tx_handler(NULL, usb_audio_mic_tx_handler);

    hdl->status = USB_MIC_START;
    hdl->mic_busy = 0;

    /* __this->rec_begin = 0; */
    return 0;
}

u32 usb_mic_is_running()
{
    if (usb_mic_hdl) {
        return SPK_AUDIO_RATE;
    }
    return 0;
}

/*
 *************************************************************
 *
 *	usb mic gain save
 *
 *************************************************************
 */
static int usb_mic_gain_save_timer;
static u8  usb_mic_gain_save_cnt;
static void usb_audio_mic_gain_save_do(void *priv)
{
    //printf(" usb_audio_mic_gain_save_do %d\n", usb_mic_gain_save_cnt);
    local_irq_disable();
    if (++usb_mic_gain_save_cnt >= 5) {
        sys_hi_timer_del(usb_mic_gain_save_timer);
        usb_mic_gain_save_timer = 0;
        usb_mic_gain_save_cnt = 0;
        local_irq_enable();
        printf("USB_GAIN_SAVE\n");
        syscfg_write(VM_USB_MIC_GAIN, &app_var.usb_mic_gain, 1);
        return;
    }
    local_irq_enable();
}

static void usb_audio_mic_gain_change(u8 gain)
{
    local_irq_disable();
    app_var.usb_mic_gain = gain;
    usb_mic_gain_save_cnt = 0;
    if (usb_mic_gain_save_timer == 0) {
        usb_mic_gain_save_timer = sys_timer_add(NULL, usb_audio_mic_gain_save_do, 1000);
    }
    local_irq_enable();
}

int usb_audio_mic_get_gain(void)
{
    return app_var.usb_mic_gain;
}

void usb_audio_mic_set_gain(int gain)
{
    if (usb_mic_hdl == NULL) {
        r_printf("usb_mic_hdl NULL gain");
        return;
    }
    gain = uac_mic_vol_switch(gain);
    audio_adc_mic_set_gain(&usb_mic_hdl->mic_ch, gain);
    usb_audio_mic_gain_change(gain);
}
int usb_audio_mic_close(void *arg)
{
    if (usb_mic_hdl == NULL) {
        r_printf("usb_mic_hdl NULL close");
        return 0;
    }
    printf("usb_mic_hdl->status %x\n", usb_mic_hdl->status);
    if (usb_mic_hdl && usb_mic_hdl->status == USB_MIC_START) {
        printf("usb_audio_mic_close in\n");
        usb_mic_hdl->status = USB_MIC_STOP;
#if TCFG_USB_MIC_CVP_ENABLE
        audio_aec_close();
#endif/*TCFG_USB_MIC_CVP_ENABLE*/

#if (!TCFG_AUDIO_INPUT_IIS || !TCFG_IIS_MIC_ENABLE)
#if USB_MIC_SRC_ENABLE
        sw_src_exit();
#endif/*USB_MIC_SRC_ENABLE*/
#endif

#if (TCFG_USB_MIC_DATA_FROM_MICEFFECT)
        mic_effect_to_usbmic_onoff(0);
#else
#if (TCFG_AUDIO_INPUT_IIS && TCFG_IIS_MIC_ENABLE)
        if (hw_alink) {
#if TCFG_AUDIO_OUTPUT_IIS
            alink_channel_close(&alink0_platform_data.ch_cfg[1]);
#else // TCFG_AUDIO_OUTPUT_IIS
            alink_uninit(hw_alink);
            hw_alink = NULL;
#endif // TCFG_AUDIO_OUTPUT_IIS
        }
        if (usb_iis_mic_hdl->sr_cal_timer) {
            sys_hi_timer_del(usb_iis_mic_hdl->sr_cal_timer);
        }
        if (usb_iis_mic_hdl->sw_src_api) {
            usb_iis_mic_hdl->sw_src_api = NULL;
        }
        if (usb_iis_mic_hdl->sw_src_buf) {
            free(usb_iis_mic_hdl->sw_src_buf);
            usb_iis_mic_hdl->sw_src_buf = NULL;
        }
        if (usb_iis_mic_hdl) {
            free(usb_iis_mic_hdl);
            usb_iis_mic_hdl = NULL;
        }
#else // TCFG_AUDIO_INPUT_IIS && TCFG_IIS_MIC_ENABLE
#if TCFG_PC_FOUR_MIC
        audio_adc_mic_close(&ladc_var->mic_ch);
        audio_adc_del_output_handler(&adc_hdl, &ladc_var->output);
#else /*TCFG_PC_FOUR_MIC == 0*/
#if TCFG_CVP_REF_IN_ADC_ENABLE
        void audio_adc_cvp_ref_close(void);
        audio_adc_cvp_ref_close();
#else /*TCFG_CVP_REF_IN_ADC_ENABLE ==0*/
        audio_mic_close(&usb_mic_hdl->mic_ch, &usb_mic_hdl->adc_output);
#endif /*TCFG_CVP_REF_IN_ADC_ENABLE*/
#endif /*TCFG_PC_FOUR_MIC*/
#endif // TCFG_AUDIO_INPUT_IIS && TCFG_IIS_MIC_ENABLE
#endif//TCFG_USB_MIC_DATA_FROM_MICEFFECT
#if (defined TCFG_AUDIO_ADC_BIT_MODE) && (TCFG_AUDIO_ADC_BIT_MODE == ADC_BIT_WIDTH_24)
        if (usb_mic_hdl->mic_buf) {
            free(usb_mic_hdl->mic_buf);
            usb_mic_hdl->mic_buf = NULL;
        }
#endif /*(defined TCFG_AUDIO_ADC_BIT_MODE) && (TCFG_AUDIO_ADC_BIT_MODE == ADC_BIT_WIDTH_24)*/

#if TCFG_USB_MIC_ECHO_ENABLE
        if (usb_mic_hdl->p_echo_hdl) {
            close_echo(usb_mic_hdl->p_echo_hdl);
        }
#endif
        cbuf_clear(&usb_mic_hdl->output_cbuf);
        if (usb_mic_hdl) {
            while (usb_mic_hdl->mic_busy) {
                os_time_dly(3);
            }

            local_irq_disable();

            if (usb_mic_hdl->output_buf) {
                free(usb_mic_hdl->output_buf);
                usb_mic_hdl->output_buf = NULL;
            }
            if (usb_mic_hdl->buf_16) {
                free(usb_mic_hdl->buf_16);
                usb_mic_hdl->buf_16 = NULL;
            }
            free(usb_mic_hdl);
            usb_mic_hdl = NULL;
            local_irq_enable();
        }
    }
    printf("usb_audio_mic_close out\n");

    return 0;
}

int usb_mic_stream_sample_rate(void)
{
#if USB_MIC_SRC_ENABLE
#ifdef CONFIG_MEDIA_DEVELOP_ENABLE
    if (usb_mic_src && usb_mic_src->audio_track) {
        int sr = audio_local_sample_track_rate(usb_mic_src->audio_track);
        if ((sr < (usb_mic_src->in_sample_rate + 500)) && (sr > (usb_mic_src->in_sample_rate - 500))) {
            return sr;
        }
        /* printf("uac audio_track reset \n"); */
        local_irq_disable();
        audio_local_sample_track_close(usb_mic_src->audio_track);
        usb_mic_src->audio_track = audio_local_sample_track_open(SPK_CHANNEL, usb_mic_src->in_sample_rate, 1000);
        local_irq_enable();
        return usb_mic_src->in_sample_rate;
    }
#endif
#endif /*USB_MIC_SRC_ENABLE*/

    return 0;
}

u32 usb_mic_stream_size()
{
    if (!usb_mic_hdl) {
        return 0;
    }
    if (usb_mic_hdl->status == USB_MIC_START) {
        if (usb_mic_hdl) {
            return cbuf_get_data_size(&usb_mic_hdl->output_cbuf);
        }
    }

    return 0;
}

u32 usb_mic_stream_length()
{
    return PCM_ENC2USB_OUTBUF_LEN;
}

int usb_output_sample_rate()
{
    int sample_rate = usb_mic_stream_sample_rate();
    int buf_size = usb_mic_stream_size();
    /* printf("sample_rate %d\n", sample_rate); */
    /* printf("buf_size %d %d\n", buf_size, usb_mic_stream_length()); */

    if (buf_size >= (usb_mic_stream_length() * 3 / 4)) {
        sample_rate += (sample_rate * 5 / 10000);
        /* putchar('+'); */
    }
    if (buf_size <= (usb_mic_stream_length() / 4)) {
        sample_rate -= (sample_rate * 5 / 10000);
        /* putchar('-'); */
    }
    return sample_rate;
}

#endif
