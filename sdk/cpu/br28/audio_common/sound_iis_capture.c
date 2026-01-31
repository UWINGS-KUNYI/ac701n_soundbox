/*************************************************************************************************/
/*!
*  \file      sound_iis_capture.c
*
*  \brief
*
*  Copyright (c) 2011-2022 ZhuHai Jieli Technology Co.,Ltd.
*
*/
/*************************************************************************************************/
#include "board_config.h"
#include "typedef.h"
#include "sound_device_driver.h"
#include "audio_splicing.h"
#include "audio_link.h"
#include "audio_path.h"

/* #define TCFG_SOUND_PCM_DELAY_TEST   1 */

#if ((defined TCFG_AUDIO_INPUT_IIS) && TCFG_AUDIO_INPUT_IIS)
extern ALINK_PARM alink0_platform_data;

struct sound_iis_capture_context {
    void *hw_alink;
    void *alink_ch;
    int sample_rate;
    u8 nch;                                 //输出pcm数据声道数
    s16 sample_buffer[256 * 3];             //256 是中断点数
    void *output_path;                      //数据接口私有参数
    int (*write_frame)(void *path, struct audio_frame *frame);//输出接口
    void *clock;
    u32(*capture_time)(void *clock, u8 type);
};

static int iis_capture_write_frame(struct sound_iis_capture_context *iis, void *data, int len)
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

#if (defined TCFG_SOUND_PCM_DELAY_TEST && TCFG_SOUND_PCM_DELAY_TEST)
static int test_count = 0;
static u16 sin_data_offset = 0;
const s16 sin_48k[48] = {
    0, 2139, 4240, 6270, 8192, 9974, 11585, 12998,
    14189, 15137, 15826, 16244, 16384, 16244, 15826, 15137,
    14189, 12998, 11585, 9974, 8192, 6270, 4240, 2139,
    0, -2139, -4240, -6270, -8192, -9974, -11585, -12998,
    -14189, -15137, -15826, -16244, -16384, -16244, -15826, -15137,
    -14189, -12998, -11585, -9974, -8192, -6270, -4240, -2139
};
static int get_sine_data(s16 *data, u16 points, u8 ch)
{
    while (points--) {
        if (sin_data_offset >= ARRAY_SIZE(sin_48k)) {
            sin_data_offset = 0;
        }
        *data++ = sin_48k[sin_data_offset];
        if (ch == 2) {
            *data++ = sin_48k[sin_data_offset];
        }
        sin_data_offset++;
    }
    return 0;
}
#endif

static void iis_caputre_dma_data_handler(void *priv, void *data, int len)
{
    struct sound_iis_capture_context *iis = (struct sound_iis_capture_context *)priv;

    //TODO : channel mapping

    int wlen = 0;
#if (defined TCFG_SOUND_PCM_DELAY_TEST && TCFG_SOUND_PCM_DELAY_TEST)
    /* if ((++test_count % 200) == 0) { */
    /* gpio_direction_output(IO_PORTB_02, 1); */
    get_sine_data(data, len / 4, 2);
    /* gpio_direction_output(IO_PORTB_02, 0); */
    /* } else { */
    /* memset(data, 0x0, len); */
    /* } */
#endif
    if (iis->nch >= 2) {
        wlen = iis_capture_write_frame(iis, data, len);
        if (wlen < len) {
            putchar('L');
        }
    } else {
        pcm_dual_to_single(iis->sample_buffer, data, len);
        wlen = iis_capture_write_frame(iis, iis->sample_buffer, len / 2);
        if (wlen < len / 2) {
            putchar('L');
        }
    }

    alink_set_shn(iis->alink_ch, len / 4);
}

void *sound_iis_capture_open(struct audio_path *path)
{
    struct sound_iis_capture_context *iis = (struct sound_iis_capture_context *)zalloc(sizeof(struct sound_iis_capture_context));

    ASSERT(iis);

    iis->sample_rate = path->fmt.sample_rate;
    iis->nch = path->fmt.channel;
    iis->output_path = path->output.path;
    iis->write_frame = path->output.write_frame;
    iis->clock = path->time.reference_clock;
    iis->capture_time = path->time.request;

    iis->hw_alink = alink_init(&alink0_platform_data);
    iis->alink_ch = alink_channel_init(iis->hw_alink, 1, ALINK_DIR_RX, (void *)iis, iis_caputre_dma_data_handler);
    alink_start(iis->hw_alink);

    return iis;
}

void sound_iis_capture_close(void *capture)
{
    struct sound_iis_capture_context *iis = (struct sound_iis_capture_context *)capture;

    if (!iis) {
        return;
    }

    alink_uninit(iis->hw_alink);

    free(iis);
}
#endif
