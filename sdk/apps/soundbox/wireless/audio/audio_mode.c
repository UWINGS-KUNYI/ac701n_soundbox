/*************************************************************************************************/
/*!
*  \file      audio_mode.c
*
*  \brief   用于处于无线传输的模式设置和参数
*
*  Copyright (c) 2011-2022 ZhuHai Jieli Technology Co.,Ltd.
*
*/
/*************************************************************************************************/
#include "app_config.h"
#include "live_audio.h"
#include "wireless_params.h"
#include "audio_mode.h"
#include "app_cfg.h"

#if (TCFG_CONNECTED_ENABLE && CIG_TRANSPORT_MODE == CIG_MODE_DUPLEX)

#if ((defined TCFG_LIVE_AUDIO_LOW_LATENCY_EN) && TCFG_LIVE_AUDIO_LOW_LATENCY_EN)||ADC_CAPTURE_LOW_LATENCY
#define CAPTURE_DELAY_TIME      1 //这里按照64 sample@48000Hz一次中断计算
#else

#if (WIRELESS_2T1_DUPLEX_EN && (CONNECTED_ROLE_CONFIG == ROLE_CENTRAL))
#define CAPTURE_DELAY_TIME      5 //这里按照256 sample@48000Hz一次中断计算
#define ENCODE_DITHER_TIME      10 //编码最大的抖动时间(超过sdu_period的时间)
#elif((WIRELESS_2T1_DUPLEX_EN && (CONNECTED_ROLE_CONFIG == ROLE_PERIP))&&CIG_PERIP_MULTI_CAPTURE_EN)
#define CAPTURE_DELAY_TIME      13 //与其他数据叠加需要增大延时
#define ENCODE_DITHER_TIME      3 //编码最大的抖动时间(超过sdu_period的时间)
#else
#define CAPTURE_DELAY_TIME      5 //这里按照256 sample@48000Hz一次中断计算
#define ENCODE_DITHER_TIME      3 //编码最大的抖动时间(超过sdu_period的时间)
#endif /*defined(CIG_MULTIPLE_CAPTURE_EN) && (CIG_MULTIPLE_CAPTURE_EN)*/

#endif /*((defined TCFG_LIVE_AUDIO_LOW_LATENCY_EN) && TCFG_LIVE_AUDIO_LOW_LATENCY_EN)*/

#else

#define ENCODE_DITHER_TIME      1 //编码最大的抖动时间(超过sdu_period的时间)
#if ((defined TCFG_LIVE_AUDIO_LOW_LATENCY_EN) && TCFG_LIVE_AUDIO_LOW_LATENCY_EN)||ADC_CAPTURE_LOW_LATENCY
#define CAPTURE_DELAY_TIME      1 //这里按照64 sample@48000Hz一次中断计算
#else
#define CAPTURE_DELAY_TIME      5 //这里按照256 sample@48000Hz一次中断计算
#endif

#endif /*TCFG_CONNECTED_ENABLE && CIG_TRANSPORT_MODE == CIG_MODE_DUPLEX*/
struct live_audio_mode_context {
    u8 mode;
    u32 source_type;
    u32 sample_rate;
    struct live_audio_mode_ops *ops;
};

static struct live_audio_mode_context g_live_audio_mode[LIVE_AUDIO_CAPTURE_MAX_MODE] = {0};

extern int CONFIG_A2DP_DELAY_TIME;

#if 0
extern void a2dp_dec_close();
static int live_a2dp_play_open(struct audio_path *path)
{
    //TODO
    return NULL;
}

static int live_a2dp_play_close(void *player)
{
    return a2dp_dec_close();
}

const struct live_audio_mode_ops audio_mode_ops[] = {
    {
        .capture_open = live_aux_capture_open,
        .capture_close = live_aux_capture_close,
        .capture_start = live_aux_capture_start,
        .capture_suspend = live_aux_capture_stop,

        .play_open = live_aux_play_open,
        .play_close = live_aux_play_close,
    },

    {
        .capture_open = live_a2dp_capture_open,
        .capture_close = live_a2dp_capture_close,
        .capture_start = live_a2dp_capture_start,
        .capture_suspend = live_a2dp_capture_suspend,

        .play_open = live_a2dp_play_open,
        .play_close = live_a2dp_play_close,
    },
};
#endif

int live_audio_mode_delay_time(u8 mode, u8 cig, int period_ms, int bit_rate, int frame_duration, int rtn)
{
    int delay_time = CAPTURE_DELAY_TIME;
    int sync_delay = 0;
    int tx_align = 1500L; /*us*/
    int cig_channel = LEA_CIG_CONNECTION_NUM;

    if (cig) {
        tx_align = get_cig_tx_delay();
    } else {
        tx_align = get_big_tx_delay();
    }

#if (TCFG_CONNECTED_ENABLE && CIG_TRANSPORT_MODE == CIG_MODE_DUPLEX)
    cig_channel = 2 * LEA_CIG_CONNECTION_NUM;
#endif
    /*
     * CIS : (((Bitrate * frame_duration + (2+4) * 8) * 0.5 + 150 + 44) * (RTN + 1)) * 单双工 * 通道数
     * BIS : (Bitrate * frame_duration * 0.5 + 150) * (RTN + 1)
     */
    if (cig) {
        sync_delay = (((bit_rate / 1000) * frame_duration + (2 + 4) * 80) / 2 + 1500 + 440) * (rtn + 1) * cig_channel / 10; /*us*/
    } else {
        sync_delay = ((bit_rate / 1000) * frame_duration / 2 + 1500) * (rtn + 1) / 10; /*us*/;
    }

    /*
     * TX->RX Delay计算：
     * |Sample      | Enc                   | Tx align | Sync delay |
     *                                                 ^            ^
     * |<-        min >= sdu period       ->|          |            |
     *                                                 TX           RX
     *
     */
#if ((defined TCFG_LIVE_AUDIO_LOW_LATENCY_EN) && TCFG_LIVE_AUDIO_LOW_LATENCY_EN)||ADC_CAPTURE_LOW_LATENCY
    int enc_time = frame_duration * 100 / 3 + (ENCODE_DITHER_TIME * 1000 / 4);
#else
    int enc_time = frame_duration * 100;//普通模式编码时间较长/ 2;
#endif
    int sample_time = CAPTURE_DELAY_TIME * 1000;
    delay_time = (sample_time + enc_time + period_ms * 1000 + sync_delay + tx_align);

    switch (mode) {
    case LIVE_A2DP_CAPTURE_MODE:
        delay_time = CONFIG_A2DP_DELAY_TIME;
        break;
    case LIVE_FILE_CAPTURE_MODE:
        delay_time = 30;
        break;
    case LIVE_IIS_CAPTURE_MODE:
#ifdef TCFG_IIS_CAPTURE_SAMPLE_PERIOD
        delay_time = TCFG_IIS_CAPTURE_SAMPLE_PERIOD + enc_time + period_ms * 1000 + sync_delay + tx_align;
#endif
        /*delay_time = CAPTURE_DELAY_TIME;*/
        break;
    }

    printf("[%s mode%d], sdu period : %dms, sync_delay : %dus, tx_align : %dus, Tx->Rx : %dus\n",
           cig ? "CIS" : "BIS", mode, period_ms, sync_delay, tx_align, delay_time);
    return delay_time;
}

int live_audio_mode_setup(u8 mode, u32 sample_rate, struct live_audio_mode_ops *ops)
{
    g_live_audio_mode[mode].mode = mode;
    g_live_audio_mode[mode].sample_rate = sample_rate;
    g_live_audio_mode[mode].ops = ops;

    return 0;
}

int live_audio_mode_play_stop(u8 mode)
{
    if (g_live_audio_mode[mode].ops && g_live_audio_mode[mode].ops->play_close) {
        g_live_audio_mode[mode].ops->play_close(NULL);
    }

    return 0;
}

int live_audio_mode_play_start(u8 mode)
{
    if (g_live_audio_mode[mode].ops && g_live_audio_mode[mode].ops->play_open) {
        g_live_audio_mode[mode].ops->play_open(NULL);
    }

    return 0;
}

int live_audio_mode_play_status(u8 mode)
{
    if (g_live_audio_mode[mode].ops &&
        g_live_audio_mode[mode].ops->play_status) {
        return g_live_audio_mode[mode].ops->play_status();
    }

    return 0;
}

int live_audio_mode_get_capture_params(u8 mode, struct audio_path *path)
{
    memset(path, 0x0, sizeof(struct audio_path));
    path->fmt.coding_type = AUDIO_CODING_PCM;
    path->fmt.sample_rate = g_live_audio_mode[mode].sample_rate;
    path->fmt.channel = JLA_CODING_CHANNEL;
    path->fmt.bit_width = TCFG_AUDIO_DAC_BIT_WIDTH;
    path->fmt.priv = (void *)g_live_audio_mode[mode].source_type;
    path->delay_time = 0;
    path->input.path = (void *)g_live_audio_mode[mode].ops;//&audio_mode_ops[g_live_audio_mode];

    if (((mode == LIVE_REMOTE_DEV1_CAPTURE_MODE) || (mode == LIVE_REMOTE_DEV0_CAPTURE_MODE)) && connected_tx_track_separate) {
        path->fmt.channel = 1;
    }


    return 0;
}

int live_audio_mode_get_broadcast_params(u8 mode, struct audio_path *path)
{
    memset(path, 0x0, sizeof(struct audio_path));

    int delay_time = live_audio_mode_delay_time(mode,
                     0,
                     get_big_sdu_period_ms(),
                     get_big_audio_coding_bit_rate(),
                     get_big_audio_coding_frame_duration(),
                     get_big_tx_rtn());
    path->delay_time = (delay_time > 1000) ? (delay_time + get_big_mtl_time() * 1000L) : (delay_time + get_big_mtl_time()); //<1000 - ms, > 1000 - us
    path->fmt.coding_type = AUDIO_CODING_JLA;
    path->fmt.channel = JLA_CODING_CHANNEL;
    path->fmt.sample_rate = JLA_CODING_SAMPLERATE;
    path->fmt.frame_len = JLA_CODING_FRAME_LEN;
    path->fmt.bit_rate = JLA_CODING_BIT_RATE;
    path->fmt.bit_width = TCFG_AUDIO_DAC_BIT_WIDTH;
    return 0;
}

int live_audio_mode_get_cis_params(u8 mode, struct audio_path *path)
{
    memset(path, 0x0, sizeof(struct audio_path));

    int delay_time = live_audio_mode_delay_time(mode,
                     1,
                     get_cig_sdu_period_ms(),
                     get_cig_audio_coding_bit_rate(),
                     get_cig_audio_coding_frame_duration(),
                     get_cig_tx_rtn());
    printf("live audio cis delay : %d, %d, %d\n", delay_time, get_cig_sdu_period_ms(), get_cig_mtl_time());

    path->delay_time = (delay_time > 1000) ? (delay_time + get_cig_mtl_time() * 1000L) : (delay_time + get_cig_mtl_time());
    path->fmt.coding_type = AUDIO_CODING_JLA;
    path->fmt.channel = JLA_CODING_CHANNEL;
    path->fmt.sample_rate = JLA_CODING_SAMPLERATE;
    path->fmt.frame_len = JLA_CODING_FRAME_LEN;
    path->fmt.bit_rate = JLA_CODING_BIT_RATE;
    return 0;
}
