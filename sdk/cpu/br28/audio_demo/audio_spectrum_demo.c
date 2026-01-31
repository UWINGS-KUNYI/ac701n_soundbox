/*
 ****************************************************************************
 *							Audio Spectrum Demo
 *
 *Description	: Spectrum 频谱 使用范例
 *Notes			: 调用 audio_mic_spectrum_demo_open()即可使用此demo，
 *				  使用定时器 spectrum_get 每隔一秒打印频谱信息
 ****************************************************************************
 */
#include "audio_demo.h"
#include "media/includes.h"
#include "system/includes.h"
#include "app_main.h"
#include "audio_config.h"
#include "asm/audio_adc.h"
#include "circular_buf.h"
#include "audio_spectrum.h"
#include "spectrum/spectrum_fft.h"
#include "audio_enc/audio_enc.h"

extern struct audio_adc_hdl adc_hdl;
#define MIC_SPECTRUM_CH_NUM        	4	/*支持的最大采样通道(max = 4)*/
#define MIC_SPECTRUM_BUF_NUM        2	/*采样buf数*/
#define MIC_SPECTRUM_IRQ_POINTS     512	/*采样中断点数*/
#define MIC_SPECTRUM_BUFS_SIZE      (MIC_SPECTRUM_CH_NUM * MIC_SPECTRUM_BUF_NUM * MIC_SPECTRUM_IRQ_POINTS)
#define MIC_SPECTRUM_CBUF_SIZE      2048/*cbuf size*/
#define SPECTRUM_FFT_POINTS_PER_CH  512/*频谱计算的点数*/

struct mic_spectrum_hdl {
    struct audio_adc_output_hdl adc_output;
    struct adc_mic_ch mic_ch;
    adc_type_t adc_buf[MIC_SPECTRUM_BUFS_SIZE];
    cbuffer_t cbuf;
    s16 *mic_spectrum_buf;
    OS_SEM sem;
    u16 timer_ret;
};
static struct mic_spectrum_hdl *mic_spectrum = NULL;
static spectrum_fft_hdl *spectrum_fft = NULL;

static void mic_spectrum_output_handler(void *priv, s16 *data, int len)
{
    if (mic_spectrum) {
        int wlen = cbuf_write(&mic_spectrum->cbuf, data, len);
        os_sem_post(&mic_spectrum->sem);
        if (wlen != len) {
            printf("cbuf_write_err %d\n", wlen);
        }
    }
    return;
}

static void audio_mic_spectrum_task()
{
    while (1) {
        os_sem_pend(&mic_spectrum->sem, 0);
        if (mic_spectrum->cbuf.data_len >= SPECTRUM_FFT_POINTS_PER_CH * 2) {
            int rlen = cbuf_read(&mic_spectrum->cbuf, mic_spectrum->mic_spectrum_buf, SPECTRUM_FFT_POINTS_PER_CH * 2);
            if (rlen != SPECTRUM_FFT_POINTS_PER_CH * 2) {
                printf("cbuf_read_err %d\n", rlen);
            }
            SpectrumShowRun(spectrum_fft->work_buf, mic_spectrum->mic_spectrum_buf, SPECTRUM_FFT_POINTS_PER_CH);
        }
    }
}

static void spectrum_get(void *p)
{
    spectrum_fft_hdl *hdl = p;
    if (hdl) {
        u8 db_num = audio_spectrum_fft_get_num(hdl);//获取频谱个数
        short *db_data = audio_spectrum_fft_get_val(hdl);//获取存储频谱值得地址
        float *centerfreq = audio_spectrum_get_centerfreq(hdl);
        if (!db_data) {
            return;
        }
        for (int i = 0; i < db_num; i++) {
            //输出db_num个 db值
            printf("centerfreq[%02d] %05d Hz,  db_val[%02d] %d dB\n", i, (int)centerfreq[i], i, db_data[i]);
        }
    }
}

void audio_mic_spectrum_demo_open(u8 gain, u16 sr)
{
    mic_spectrum = zalloc(sizeof(struct mic_spectrum_hdl));
    if (!mic_spectrum) {
        puts("mic_spectrum_zalloc_err!\n");
        goto __err;
    }
    mic_spectrum->mic_spectrum_buf = zalloc(MIC_SPECTRUM_CBUF_SIZE);
    if (!mic_spectrum->mic_spectrum_buf) {
        puts("mic_spectrum_buf_zalloc_err!\n");
        goto __err;
    }
    cbuf_init(&mic_spectrum->cbuf, mic_spectrum->mic_spectrum_buf, MIC_SPECTRUM_CBUF_SIZE);
    os_sem_create(&mic_spectrum->sem, 0);
    os_task_create(audio_mic_spectrum_task, NULL, 3, 1024, 128, "spectrum");

    spectrum_fft = zalloc(sizeof(spectrum_fft_hdl));
    if (!spectrum_fft) {
        puts("spectrum_fft_zalloc_err!\n");
        goto __err;
    }
    spectrum_fft->work_buf = zalloc(getSpectrumShowBuf());
    if (!spectrum_fft->work_buf) {
        puts("spectrum_fft_workbuf_zalloc_err!\n");
        goto __err;
    }
    spectrum_fft->parm.sr = sr;
    spectrum_fft->parm.channel = 1; //单声道
    spectrum_fft->parm.attackFactor = 0.9;
    spectrum_fft->parm.releaseFactor = 0.2;
    spectrum_fft->parm.mode = 0;    //模式0 计算第一声道
    spectrum_fft->parm.pcm_info.IndataBit = 0;
    spectrum_fft->parm.pcm_info.OutdataBit = 0;
    spectrum_fft->parm.pcm_info.IndataInc = (spectrum_fft->parm.channel == 2) ? 2 : 1;
    spectrum_fft->parm.pcm_info.OutdataInc = (spectrum_fft->parm.channel == 2) ? 2 : 1;
    spectrum_fft->parm.pcm_info.Qval = 15;//global_bit_wide?23:15;


    SpectrumShowInit(spectrum_fft->work_buf, &spectrum_fft->parm);
    mic_spectrum->timer_ret = sys_timer_add(spectrum_fft, spectrum_get, 1000);//频谱值获取测试
    //adc
    audio_adc_mic_open(&mic_spectrum->mic_ch, AUDIO_ADC_MIC_0, &adc_hdl);
    audio_adc_mic_set_gain(&mic_spectrum->mic_ch, gain);
    audio_adc_mic_0dB_en(1);
    audio_adc_mic_set_sample_rate(&mic_spectrum->mic_ch, sr);
    audio_adc_mic_set_buffs(&mic_spectrum->mic_ch, mic_spectrum->adc_buf, MIC_SPECTRUM_IRQ_POINTS * 2, MIC_SPECTRUM_BUF_NUM);
    mic_spectrum->adc_output.handler = mic_spectrum_output_handler;
    audio_adc_add_output_handler(&adc_hdl, &mic_spectrum->adc_output);
    audio_adc_mic_start(&mic_spectrum->mic_ch);
    return;

__err:
    if (mic_spectrum) {
        free(mic_spectrum);
        mic_spectrum = NULL;
    }
    if (mic_spectrum->mic_spectrum_buf) {
        free(mic_spectrum->mic_spectrum_buf);
        mic_spectrum->mic_spectrum_buf = NULL;
    }
    if (spectrum_fft->work_buf) {
        free(spectrum_fft->work_buf);
        spectrum_fft->work_buf = NULL;
    }
    if (spectrum_fft) {
        free(spectrum_fft);
        spectrum_fft = NULL;
    }
    return;
}

void audio_mic_spectrum_demo_close()
{
    task_kill("spectrum");
    if (mic_spectrum->mic_spectrum_buf) {
        free(mic_spectrum->mic_spectrum_buf);
        mic_spectrum->mic_spectrum_buf = NULL;
    }
    if (spectrum_fft->work_buf) {
        free(spectrum_fft->work_buf);
        spectrum_fft->work_buf = NULL;
    }
    if (spectrum_fft) {
        free(spectrum_fft);
        spectrum_fft = NULL;
    }
    if (mic_spectrum) {
        sys_timer_del(mic_spectrum->timer_ret);
        audio_adc_del_output_handler(&adc_hdl, &mic_spectrum->adc_output);
        audio_adc_mic_close(&mic_spectrum->mic_ch);
        free(mic_spectrum);
        mic_spectrum = NULL;
    }
}
