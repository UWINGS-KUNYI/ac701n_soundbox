#include "mic_stream.h"
#include "app_config.h"
#include "system/includes.h"
#include "audio_splicing.h"
#include "audio_config.h"
#include "asm/dac.h"
#include "audio_enc/audio_enc.h"
#include "audio_dec.h"
#include "media/includes.h"
#include "application/audio_dig_vol.h"
#include "media/pcm_decoder.h"
#include "audio_splicing.h"
#if TCFG_AUDIO_INPUT_IIS
#include "audio_link.h"
#endif // TCFG_AUDIO_INPUT_IIS


#if (TCFG_MIC_EFFECT_ENABLE)

#define MIC_STREAM_TASK_NAME				"mic_stream"

#ifdef SUPPORT_MS_EXTENSIONS
//#pragma bss_seg(".audio_mic_stream_bss")
//#pragma data_seg(".audio_mic_stream_data")
#pragma const_seg(".audio_mic_stream_const")
#pragma code_seg(".audio_mic_stream_code")
#endif


static struct __mic_stream *mic = NULL;
#define __this mic

#define MIC_SIZEOF_ALIN(var,al)     ((((var)+(al)-1)/(al))*(al))

/* extern struct audio_dac_hdl dac_hdl; */
/* extern struct audio_mixer mixer; */

//ADC打开时，清零的数据包数
#define MIC_DUMP_PACKET_NUM   1

static void mic_data_fade_16(struct __mic_stream *stream, s16 *data, int len)
{
    if (stream->dump_packet) {
        stream->dump_packet--;
        memset(data, 0x00, len);
    } else {
        if (stream->fade_en) {
            int tmp_data;
            for (int i = 0; i < len >> 1; i++) {
                tmp_data = data[i];
                data[i] = (tmp_data * stream->fade_gain) >> 14;
                stream->fade_gain += 1;
                if (stream->fade_gain >= 16384) {
                    stream->fade_en = 0;
                    break;
                }
            }
        }
    }
}

static void mic_data_fade_32(struct __mic_stream *stream, int *data, int len)
{
    if (stream->dump_packet) {
        stream->dump_packet--;
        memset(data, 0x00, len);
    } else {
        if (stream->fade_en) {
            s64 tmp_data;
            for (int i = 0; i < len >> 2; i++) {
                tmp_data = data[i];
                data[i] = (tmp_data * stream->fade_gain) >> 14;
                stream->fade_gain += 1;
                if (stream->fade_gain >= 16384) {
                    stream->fade_en = 0;
                    break;
                }
            }
        }
    }
}

/*----------------------------------------------------------------------------*/
/**@brief    唤醒mic数据处理任务
  @param
  @return
  @note
  */
/*----------------------------------------------------------------------------*/
void mic_stream_adc_resume(void *priv)
{
    struct __mic_stream *stream = (struct __mic_stream *)priv;
    if (stream != NULL && (stream->release == 0)) {
        os_sem_set(&stream->sem, 0);
        os_sem_post(&stream->sem);
    }
}
/*----------------------------------------------------------------------------*/
/**@brief    mic数据处理函数
   @param
   @return
   @note
*/
/*----------------------------------------------------------------------------*/

static int mic_stream_effects_run(struct __mic_stream *stream)
{
    int res = os_sem_pend(&stream->sem, 0);
    if (res) {
        return -1;
    }
    if (stream->release) {
        return -1;
    }
    u16 point_offset = 1;
#if defined(TCFG_AUDIO_ADC_BIT_MODE)&&ADC_BIT_WIDTH_24&&(TCFG_AUDIO_ADC_BIT_MODE == ADC_BIT_WIDTH_24)
    point_offset = 2;
#endif

#if (TCFG_AUDIO_OUTPUT_IIS && TCFG_AUDIO_INPUT_IIS)
    while (stream->cbuf->data_len >= ALNK_BUF_POINTS_NUM) {
        u16 rlen = cbuf_read(stream->cbuf, stream->buf, ALNK_BUF_POINTS_NUM);
        if (rlen != ALNK_BUF_POINTS_NUM) {
            printf("cbuf read err %d\n", rlen);
        }

        s16 *read_buf = (s16 *)(stream->buf);
        stream->adc_buf_len = rlen;
        if (stream->out.func) {
            stream->out.func(stream->out.priv, read_buf, NULL, stream->parm->point_unit << point_offset, stream->parm->point_unit * 2 << point_offset);
        }
    }
#else
    s16 *read_buf = (s16 *)(stream->adc_buf);
    u16 read_len = stream->adc_buf_len;
    if (global_bit_wide) {
        mic_data_fade_32(stream, (int *)read_buf, read_len);
    } else {
        mic_data_fade_16(stream, read_buf, read_len);
    }
    if (stream->out.func) {
        stream->out.func(stream->out.priv, read_buf, NULL, stream->parm->point_unit << point_offset, stream->parm->point_unit * 2 << point_offset);
    }
#endif
    return 0;
}
/*----------------------------------------------------------------------------*/
/**@brief    mic数据处理任务
   @param
   @return
   @note
*/
/*----------------------------------------------------------------------------*/
static void mic_stream_task_deal(void *p)
{
    int res = 0;
    struct __mic_stream *stream = (struct __mic_stream *)p;
    stream->busy = 1;
    while (1) {
        res = mic_stream_effects_run(stream);
        if (res) {
            ///等待删除线程
            stream->busy = 0;
            while (1) {
                os_time_dly(10000);
            }
        }
    }
}
/*----------------------------------------------------------------------------*/
/**@brief    创建mic数据流
   @param
   @return
   @note
*/
/*----------------------------------------------------------------------------*/
struct __mic_stream *mic_stream_creat(struct __mic_stream_parm *parm)
{
    int err = 0;
    struct __mic_stream_parm *p = parm;
    if (parm == NULL) {
        printf("%s parm err\n", __FUNCTION__);
        return NULL;
    }
    printf("p->dac_delay = %d\n", p->dac_delay);
    printf("p->point_unit = %d\n", p->point_unit);
    printf("p->sample_rate = %d\n", p->sample_rate);

    u32 offset = 0;
    u32 buf_size = MIC_SIZEOF_ALIN(sizeof(struct __mic_stream), 4);

    u8 *buf = zalloc(buf_size);
    if (buf == NULL) {
        return NULL;
    }

    struct __mic_stream *stream = (struct __mic_stream *)buf;
    offset += MIC_SIZEOF_ALIN(sizeof(struct __mic_stream), 4);

    stream->parm = p;
    os_sem_create(&stream->sem, 0);
    err = task_create(mic_stream_task_deal, (void *)stream, MIC_STREAM_TASK_NAME);
    if (err != OS_NO_ERR) {
        printf("%s creat fail %x\n", __FUNCTION__,  err);
        free(stream);
        return NULL;
    }

    local_irq_disable();
    __this = stream;
    local_irq_enable();

    printf("mic stream creat ok\n");
    return stream;
}
/*----------------------------------------------------------------------------*/
/**@brief    设置mic处理函数的回调处理
   @param
   @return
   @note
*/
/*----------------------------------------------------------------------------*/
void mic_stream_set_output(struct __mic_stream  *stream, void *priv, u32(*func)(void *priv, void *in, void *out, u32 inlen, u32 outlen))
{
    if (stream) {
        stream->out.priv = priv;
        stream->out.func = func;
    }
}

/*----------------------------------------------------------------------------*/
/**@brief    mic中断数据输出回调函数
   @param
   @return
   @note
*/
/*----------------------------------------------------------------------------*/
static void adc_output_to_buf(void *priv, s16 *data, int len)
{
    struct __mic_stream *stream = (struct __mic_stream *)priv;
    int wlen = 0;
    if (stream != NULL && (stream->release == 0)) {
        stream->adc_buf = data;
        stream->adc_buf_len = len;
        os_sem_set(&stream->sem, 0);
        os_sem_post(&stream->sem);

#if 0//RECORDER_MIX_EN
        extern void recorder_mix_update(void);
        recorder_mix_update();
#endif
    }
}

/*----------------------------------------------------------------------------*/
/**@brief    iis中断数据输出回调函数
   @param
   @return
   @note
*/
/*----------------------------------------------------------------------------*/
/*
 * 24bit转32bit，处理符号位
 * bit23如果是1，高8位补1;如果是0，高8位补0
 */
static void audio_data_24bit_to_32bit(void *buf, int npoint)
{
    s32 *data = (s32 *)buf;
    for (int i = 0; i < npoint; i++) {
        if (data[i] & 0x00800000) {
            data[i] = data[i] | 0xff000000;
        } else {
            data[i] = data[i] & 0x00ffffff;
        }
    }
}

#if TCFG_AUDIO_INPUT_IIS
extern ALINK_PARM alink0_platform_data;
extern void *hw_alink;

static void iis_output_to_buf(void *priv, s16 *data, u16 len)
{
    u16 temp_len = len;
    if (alink0_platform_data.bitwide == ALINK_LEN_24BIT) {
        audio_data_24bit_to_32bit(data, len >> 2);
        pcm_dual_to_single_32bit(data, data, len);
    } else {
        pcm_dual_to_single(data, data, len);
    }
    len >>= 1;

#if TCFG_AUDIO_OUTPUT_IIS  //数据写到cbuf，进行拆包处理
    struct __mic_stream *stream = (struct __mic_stream *)priv;
    u16 wlen = cbuf_write(stream->cbuf, data, len);
    if (wlen != len) {
        printf("cbuf write err %d %d\n", wlen, len);
    }
    if (stream != NULL && (stream->release == 0)) {
        os_sem_set(&stream->sem, 0);
        os_sem_post(&stream->sem);
    }
#else
    struct __mic_stream *stream = (struct __mic_stream *)priv;
    if (stream != NULL && (stream->release == 0)) {
        stream->adc_buf = data;
        stream->adc_buf_len = len;
        os_sem_set(&stream->sem, 0);
        os_sem_post(&stream->sem);
    }
#endif
    alink_set_shn(&alink0_platform_data.ch_cfg[1], temp_len / 4);
}

#endif // TCFG_AUDIO_INPUT_IIS

/*----------------------------------------------------------------------------*/
/**@brief    打开mic
   @param
   @return
   @note
*/
/*----------------------------------------------------------------------------*/
bool mic_stream_start(struct __mic_stream  *stream)
{
    if (stream) {
#if TCFG_AUDIO_INPUT_IIS
#if 0 //使用wm8978模块作为输入
        extern u8 WM8978_Init(u8 dacen, u8 adcen);
        WM8978_Init(0, 1);
#endif
#if TCFG_AUDIO_OUTPUT_IIS
        if (!stream->buf) {
            stream->buf = zalloc(ALNK_BUF_POINTS_NUM);
        }
        if (!stream->cbuf) {
            stream->cbuf = zalloc(sizeof(cbuffer_t) + 2 * ALNK_BUF_POINTS_NUM);
            if (stream->cbuf) {
                cbuf_init(stream->cbuf, stream->cbuf + 1, 2 * ALNK_BUF_POINTS_NUM); //初始化拆包用cbuf
            }
        }
        alink_channel_init(hw_alink, 1, ALINK_DIR_RX, stream, iis_output_to_buf); //IIS输入使用通道1
#else // TCFG_AUDIO_OUTPUT_IIS
        hw_alink = alink_init(&alink0_platform_data);
        alink_channel_init(hw_alink, 1, ALINK_DIR_RX, stream, iis_output_to_buf);
        alink_start(hw_alink);
#endif // TCFG_AUDIO_OUTPUT_IIS
        return true;
#else // TCFG_AUDIO_INPUT_IIS

        u8 mic_gain =  stream->parm->mic_gain;
#if defined(AUDIO_ADC_PGA_CONFIG)&&AUDIO_ADC_PGA_CONFIG
        mic_gain = adc_pga.gain_mic;
#endif

        if (audio_mic_open(&stream->mic_ch, stream->parm->sample_rate, mic_gain) == 0) {
            stream->adc_output.handler = adc_output_to_buf;
            stream->adc_output.priv = stream;
            //fade配置
            stream->fade_en = 1;
            stream->fade_gain = 0;
            stream->dump_packet = MIC_DUMP_PACKET_NUM;
            audio_mic_add_output(&stream->adc_output);
            audio_mic_set_gain(mic_gain);
            audio_mic_start(&stream->mic_ch);
#if TCFG_AUDIO_ADC_MIC_CHA & AUDIO_ADC_MIC_0
            audio_adc_mic_0dB_en(adc_pga.MicBoostFlag);//关闭mic前置6dB增益，默认打开
#endif
#if TCFG_AUDIO_ADC_MIC_CHA & AUDIO_ADC_MIC_1
            audio_adc_mic1_0dB_en(adc_pga.MicBoostFlag);//关闭mic前置6dB增益，默认打开
#endif
#if TCFG_AUDIO_ADC_MIC_CHA & AUDIO_ADC_MIC_2
            audio_adc_mic2_0dB_en(adc_pga.MicBoostFlag);//关闭mic前置6dB增益，默认打开
#endif
#if TCFG_AUDIO_ADC_MIC_CHA & AUDIO_ADC_MIC_3
            audio_adc_mic3_0dB_en(adc_pga.MicBoostFlag);//关闭mic前置6dB增益，默认打开
#endif
            log_i("mic_stream_start ok 11\n");
            return true;
        }
#endif//TCFG_AUDIO_INPUT_IIS
    }
    return false;
}
/*----------------------------------------------------------------------------*/
/**@brief    关闭mic数据流
   @param
   @return
   @note
*/
/*----------------------------------------------------------------------------*/
void mic_stream_destroy(struct __mic_stream **hdl)
{
    int err = 0;
    if ((hdl == NULL) || (*hdl == NULL)) {
        return ;
    }

    struct __mic_stream *stream = *hdl;
    stream->release = 1;

    os_sem_set(&stream->sem, 0);
    os_sem_post(&stream->sem);

    while (stream->busy) {
        os_time_dly(5);
    }
#if TCFG_AUDIO_INPUT_IIS
    if (hw_alink) {
#if TCFG_AUDIO_OUTPUT_IIS
        if (stream->buf) {
            free(stream->buf);
            stream->buf = NULL;
        }
        if (stream->cbuf) {
            free(stream->cbuf);
            stream->cbuf = NULL;
        }
        alink_channel_close(&alink0_platform_data.ch_cfg[1]);
#else // TCFG_AUDIO_OUTPUT_IIS
        alink_uninit(hw_alink);
        hw_alink = NULL;
#endif // TCFG_AUDIO_OUTPUT_IIS
    }
#else // TCFG_AUDIO_INPUT_IIS
    audio_mic_close(&stream->mic_ch, &stream->adc_output);
#endif // TCFG_AUDIO_INPUT_IIS
    printf("%s wait busy ok!!!\n", __FUNCTION__);

    err = task_kill(MIC_STREAM_TASK_NAME);
    os_sem_del(&stream->sem, 0);

    local_irq_disable();
    free(*hdl);
    *hdl = NULL;
    __this = NULL;
    local_irq_enable();
}

#endif//TCFG_MIC_EFFECT_ENABLE


