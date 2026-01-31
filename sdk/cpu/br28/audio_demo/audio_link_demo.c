#include "includes.h"
#include "asm/includes.h"
#include "media/includes.h"
#include "system/includes.h"
#include "audio_sound_dac.h"
#include "audio_way_dac.h"
#include "audio_link.h"

/* 同时测试IIS输入输出
 * 忽略 CONFIG_IN_SEL 和 CONFIG_OUT_SEL 的设置
 * 数据流：SIN IN -> IIS OUT -> IIS IN -> DAC OUT*/
#define ALINK_DEMO_CYCLE_TESE_IIS   0

/*选择输入源*/
#define IN_IIS BIT(0)   //iis输入
#define IN_SIN BIT(1)   //定时器sin输入
#define CONFIG_IN_SEL   IN_IIS//IN_SIN

/*选择输出源*/
#define OUT_IIS BIT(0)  //iis输出
#define OUT_DAC BIT(1)  //dac输出
#define CONFIG_OUT_SEL OUT_DAC//OUT_IIS


//采样率
#define ALINK_DEMO_SAMPLE_RATE  48000//16000
//每帧样点数
#define ALINK_DEMO_FRAME_POINTS_NUM 256
//双声道数据长度
#define ALINK_DEMO_FRAME_SIZE     (ALINK_DEMO_FRAME_POINTS_NUM << 2)
//sin 定时器时间 ms
#define ALINK_DEMO_SIN_TIME     1

#define ALINK_DEMO_TASK_NAME    "alink_demo_task"

struct alink_demo {
    u8 start;
    struct audio_sound_dac *dac_sound_demo;
    u8 buf[ALINK_DEMO_FRAME_SIZE * 3];
    s16 runbuf[ALINK_DEMO_FRAME_POINTS_NUM * 2];
    s16 tmpbuf[ALINK_DEMO_FRAME_POINTS_NUM * 2];
    cbuffer_t cbuf;
    void *hw_alink;
    void *alink_rx_ch;
    void *alink_tx_ch;
    u32 sin_timer;
};
struct alink_demo *alink_demo_hdl = NULL;

ALINK_PARM	alink_demo_config = {
    .module = ALINK0,
    .mclk_io = IO_PORTA_12,
    .sclk_io = IO_PORTA_13,
    .lrclk_io = IO_PORTA_14,
    .ch_cfg[0].data_io = IO_PORTA_15,
    .ch_cfg[1].data_io = IO_PORTA_11,
    /* .mode = ALINK_MD_IIS_LALIGN, */
    .mode = ALINK_MD_IIS,
    /* .role = ALINK_ROLE_SLAVE, */
    .role = ALINK_ROLE_MASTER,
    /* .clk_mode = ALINK_CLK_RAISE_UPDATE_FALL_SAMPLE, */
    .clk_mode = ALINK_CLK_FALL_UPDATE_RAISE_SAMPLE,
    .bitwide = ALINK_LEN_16BIT,
    /* .bitwide = ALINK_LEN_24BIT, */
    /* .sclk_per_frame = ALINK_FRAME_32SCLK, */
    .dma_len = ALINK_DEMO_FRAME_POINTS_NUM * 2 * 2,
    .sample_rate = ALINK_DEMO_SAMPLE_RATE,
    /* .sample_rate = 44100, */
    /* .sample_rate = TCFG_IIS_SAMPLE_RATE, */
    .buf_mode = ALINK_BUF_CIRCLE,
    /* .buf_mode = ALINK_BUF_DUAL, */
};

static short const tsin_441k[441] = {
    0x0000, 0x122d, 0x23fb, 0x350f, 0x450f, 0x53aa, 0x6092, 0x6b85, 0x744b, 0x7ab5, 0x7ea2, 0x7fff, 0x7ec3, 0x7af6, 0x74ab, 0x6c03,
    0x612a, 0x545a, 0x45d4, 0x35e3, 0x24db, 0x1314, 0x00e9, 0xeeba, 0xdce5, 0xcbc6, 0xbbb6, 0xad08, 0xa008, 0x94fa, 0x8c18, 0x858f,
    0x8181, 0x8003, 0x811d, 0x84ca, 0x8af5, 0x9380, 0x9e3e, 0xaaf7, 0xb969, 0xc94a, 0xda46, 0xec06, 0xfe2d, 0x105e, 0x223a, 0x3365,
    0x4385, 0x5246, 0x5f5d, 0x6a85, 0x7384, 0x7a2d, 0x7e5b, 0x7ffa, 0x7f01, 0x7b75, 0x7568, 0x6cfb, 0x6258, 0x55b7, 0x4759, 0x3789,
    0x2699, 0x14e1, 0x02bc, 0xf089, 0xdea7, 0xcd71, 0xbd42, 0xae6d, 0xa13f, 0x95fd, 0x8ce1, 0x861a, 0x81cb, 0x800b, 0x80e3, 0x844e,
    0x8a3c, 0x928c, 0x9d13, 0xa99c, 0xb7e6, 0xc7a5, 0xd889, 0xea39, 0xfc5a, 0x0e8f, 0x2077, 0x31b8, 0x41f6, 0x50de, 0x5e23, 0x697f,
    0x72b8, 0x799e, 0x7e0d, 0x7fee, 0x7f37, 0x7bed, 0x761f, 0x6ded, 0x6380, 0x570f, 0x48db, 0x392c, 0x2855, 0x16ad, 0x048f, 0xf259,
    0xe06b, 0xcf20, 0xbed2, 0xafd7, 0xa27c, 0x9705, 0x8db0, 0x86ab, 0x821c, 0x801a, 0x80b0, 0x83da, 0x8988, 0x919c, 0x9bee, 0xa846,
    0xb666, 0xc603, 0xd6ce, 0xe86e, 0xfa88, 0x0cbf, 0x1eb3, 0x3008, 0x4064, 0x4f73, 0x5ce4, 0x6874, 0x71e6, 0x790a, 0x7db9, 0x7fdc,
    0x7f68, 0x7c5e, 0x76d0, 0x6ed9, 0x64a3, 0x5863, 0x4a59, 0x3acc, 0x2a0f, 0x1878, 0x0661, 0xf42a, 0xe230, 0xd0d0, 0xc066, 0xb145,
    0xa3bd, 0x9813, 0x8e85, 0x8743, 0x8274, 0x8030, 0x8083, 0x836b, 0x88da, 0x90b3, 0x9acd, 0xa6f5, 0xb4ea, 0xc465, 0xd515, 0xe6a3,
    0xf8b6, 0x0aee, 0x1ced, 0x2e56, 0x3ecf, 0x4e02, 0x5ba1, 0x6764, 0x710e, 0x786f, 0x7d5e, 0x7fc3, 0x7f91, 0x7cc9, 0x777a, 0x6fc0,
    0x65c1, 0x59b3, 0x4bd3, 0x3c6a, 0x2bc7, 0x1a41, 0x0833, 0xf5fb, 0xe3f6, 0xd283, 0xc1fc, 0xb2b7, 0xa503, 0x9926, 0x8f60, 0x87e1,
    0x82d2, 0x804c, 0x805d, 0x8303, 0x8833, 0x8fcf, 0x99b2, 0xa5a8, 0xb372, 0xc2c9, 0xd35e, 0xe4da, 0xf6e4, 0x091c, 0x1b26, 0x2ca2,
    0x3d37, 0x4c8e, 0x5a58, 0x664e, 0x7031, 0x77cd, 0x7cfd, 0x7fa3, 0x7fb4, 0x7d2e, 0x781f, 0x70a0, 0x66da, 0x5afd, 0x4d49, 0x3e04,
    0x2d7d, 0x1c0a, 0x0a05, 0xf7cd, 0xe5bf, 0xd439, 0xc396, 0xb42d, 0xa64d, 0x9a3f, 0x9040, 0x8886, 0x8337, 0x806f, 0x803d, 0x82a2,
    0x8791, 0x8ef2, 0x989c, 0xa45f, 0xb1fe, 0xc131, 0xd1aa, 0xe313, 0xf512, 0x074a, 0x195d, 0x2aeb, 0x3b9b, 0x4b16, 0x590b, 0x6533,
    0x6f4d, 0x7726, 0x7c95, 0x7f7d, 0x7fd0, 0x7d8c, 0x78bd, 0x717b, 0x67ed, 0x5c43, 0x4ebb, 0x3f9a, 0x2f30, 0x1dd0, 0x0bd6, 0xf99f,
    0xe788, 0xd5f1, 0xc534, 0xb5a7, 0xa79d, 0x9b5d, 0x9127, 0x8930, 0x83a2, 0x8098, 0x8024, 0x8247, 0x86f6, 0x8e1a, 0x978c, 0xa31c,
    0xb08d, 0xbf9c, 0xcff8, 0xe14d, 0xf341, 0x0578, 0x1792, 0x2932, 0x39fd, 0x499a, 0x57ba, 0x6412, 0x6e64, 0x7678, 0x7c26, 0x7f50,
    0x7fe6, 0x7de4, 0x7955, 0x7250, 0x68fb, 0x5d84, 0x5029, 0x412e, 0x30e0, 0x1f95, 0x0da7, 0xfb71, 0xe953, 0xd7ab, 0xc6d4, 0xb725,
    0xa8f1, 0x9c80, 0x9213, 0x89e1, 0x8413, 0x80c9, 0x8012, 0x81f3, 0x8662, 0x8d48, 0x9681, 0xa1dd, 0xaf22, 0xbe0a, 0xce48, 0xdf89,
    0xf171, 0x03a6, 0x15c7, 0x2777, 0x385b, 0x481a, 0x5664, 0x62ed, 0x6d74, 0x75c4, 0x7bb2, 0x7f1d, 0x7ff5, 0x7e35, 0x79e6, 0x731f,
    0x6a03, 0x5ec1, 0x5193, 0x42be, 0x328f, 0x2159, 0x0f77, 0xfd44, 0xeb1f, 0xd967, 0xc877, 0xb8a7, 0xaa49, 0x9da8, 0x9305, 0x8a98,
    0x848b, 0x80ff, 0x8006, 0x81a5, 0x85d3, 0x8c7c, 0x957b, 0xa0a3, 0xadba, 0xbc7b, 0xcc9b, 0xddc6, 0xefa2, 0x01d3, 0x13fa, 0x25ba,
    0x36b6, 0x4697, 0x5509, 0x61c2, 0x6c80, 0x750b, 0x7b36, 0x7ee3, 0x7ffd, 0x7e7f, 0x7a71, 0x73e8, 0x6b06, 0x5ff8, 0x52f8, 0x444a,
    0x343a, 0x231b, 0x1146, 0xff17, 0xecec, 0xdb25, 0xca1d, 0xba2c, 0xaba6, 0x9ed6, 0x93fd, 0x8b55, 0x850a, 0x813d, 0x8001, 0x815e,
    0x854b, 0x8bb5, 0x947b, 0x9f6e, 0xac56, 0xbaf1, 0xcaf1, 0xdc05, 0xedd3
};

static short const tsin_16k[16] = {
    0x0000, 0x30fd, 0x5a83, 0x7641, 0x7fff, 0x7642, 0x5a82, 0x30fc, 0x0000, 0xcf04, 0xa57d, 0x89be, 0x8000, 0x89be, 0xa57e, 0xcf05,
};
static u16 tx_s_cnt = 0;
static int get_sine_data(u16 *s_cnt, s16 *data, u16 points, u8 ch)
{
    while (points--) {
        if (*s_cnt >= 441) {
            *s_cnt = 0;
        }
        *data++ = tsin_441k[*s_cnt];
        if (ch == 2) {
            *data++ = tsin_441k[*s_cnt];
        }
        (*s_cnt)++;
    }
    return 0;
}

static int get_sine_16k_data(u16 *s_cnt, s16 *data, u16 points, u8 ch)
{
    while (points--) {
        if (*s_cnt >= 16) {
            *s_cnt = 0;
        }
        *data++ = tsin_16k[*s_cnt];
        if (ch == 2) {
            *data++ = tsin_16k[*s_cnt];
        }
        (*s_cnt)++;
    }
    return 0;
}

static void pcm_LR_to_mono(s16 *pcm_lr, s16 *pcm_mono, int points_len)
{
    s16 pcm_L;
    s16 pcm_R;
    int i = 0;

    for (i = 0; i < points_len; i++, pcm_lr += 2) {
        pcm_L = *pcm_lr;
        pcm_R = *(pcm_lr + 1);
        *pcm_mono++ = (s16)(((int)pcm_L + pcm_R) >> 1);
    }
}

//暂不可用
int audio_alink_demo_write(s16 *data, int len)
{
    int wlen = 0;
    u32 tx_addr, tx_shn, tx_swptr, dma_len;
    int offset = 0;
    if (alink_demo_hdl) {
        tx_addr = alink_get_addr(alink_demo_hdl->alink_tx_ch);
        tx_shn = alink_get_shn(alink_demo_hdl->alink_tx_ch);
        tx_swptr = alink_get_swptr(alink_demo_hdl->alink_tx_ch);
        printf("addr %x, shn %d, swptr %d", tx_addr, tx_shn, tx_swptr);
        int buf_len =  tx_shn * 4;
        if (buf_len > len) {
            wlen = len;
        } else {
            wlen = buf_len;
        }
        offset = tx_swptr + wlen / 4;
        if (offset > dma_len) {
            memcpy(tx_addr + tx_swptr * 4, data, (dma_len - tx_swptr) * 4);
            data = (s16 *)data + ((dma_len - tx_swptr) * 2);
            offset = 0;
        }
        memcpy(tx_addr + offset * 4, data, (tx_swptr + wlen / 4 - dma_len) * 4);
        alink_set_shn(alink_demo_hdl->alink_tx_ch, wlen / 4);
    }
    printf("iis wlen %d", wlen);
    return wlen;
}


void audio_link_demo_post_run(s16 *data, int len)
{
    int ret = 0;
    int wlen = 0;
    if (alink_demo_hdl && alink_demo_hdl->start) {
        wlen = cbuf_write(&alink_demo_hdl->cbuf, data, len);
        if (wlen != len) {
            printf("[alink_demo] cbuf fill !!!");
        }
        ret = os_taskq_post_msg(ALINK_DEMO_TASK_NAME, 2, (int)data, len);
    }
}

void audio_sin_timer(void *param)
{
    putchar('s');
    int len = (ALINK_DEMO_SAMPLE_RATE * ALINK_DEMO_SIN_TIME / 1000) * 4;
    if (alink_demo_hdl && alink_demo_hdl->start) {
        if (ALINK_DEMO_SAMPLE_RATE == 44100) {
            get_sine_data(&tx_s_cnt, alink_demo_hdl->tmpbuf, len / 2 / 2, 2);
        } else {
            get_sine_16k_data(&tx_s_cnt, alink_demo_hdl->tmpbuf, len / 2 / 2, 2);
        }
        audio_link_demo_post_run(alink_demo_hdl->tmpbuf, len);
    }
}


void alink_demo_tx_irq_handle(void *priv, void *buf, u16 len)
{
    putchar('t');
#if ALINK_DEMO_CYCLE_TESE_IIS
    if (ALINK_DEMO_SAMPLE_RATE == 44100) {
        get_sine_data(&tx_s_cnt, alink_demo_hdl->tmpbuf, len / 2 / 2, 2);
    } else {
        get_sine_16k_data(&tx_s_cnt, alink_demo_hdl->tmpbuf, len / 2 / 2, 2);
    }
#else
    if (alink_demo_hdl) {
        if (cbuf_get_data_len(&alink_demo_hdl->cbuf) >= len) {
            cbuf_read(&alink_demo_hdl->cbuf, buf, len);
        } else {
            printf("err %d", cbuf_get_data_len(&alink_demo_hdl->cbuf));
        }
    }
    alink_set_shn(alink_demo_hdl->alink_tx_ch, len / 4);
#endif
}

void alink_demo_rx_irq_handle(void *priv, void *buf, u16 len)
{
    putchar('r');
    if (alink_demo_hdl) {
        /* get_sine_data(&tx_s_cnt, buf, len / 2 / 2, 2); */
        /* get_sine_16k_data(&tx_s_cnt, buf, len / 2 / 2, 2); */
        audio_link_demo_post_run(buf, len);
    }
    alink_set_shn(alink_demo_hdl->alink_rx_ch, len / 4);

}


void audio_link_demo_task(void *p)
{
    int res;
    int msg[16];
    int wlen = 0;
    while (1) {
        res = os_taskq_pend("taskq", msg, ARRAY_SIZE(msg));
        s16 *data = (int)msg[1];
        int len = msg[2];
        int rlen = 0;
        if (cbuf_get_data_len(&alink_demo_hdl->cbuf) >= ALINK_DEMO_FRAME_SIZE) {
#if (CONFIG_OUT_SEL == OUT_DAC) || (ALINK_DEMO_CYCLE_TESE_IIS == 1)
            cbuf_read(&alink_demo_hdl->cbuf, alink_demo_hdl->runbuf, ALINK_DEMO_FRAME_SIZE);
            struct audio_way *dac_channel = alink_demo_hdl->dac_sound_demo->way;
            if (dac_channel && (dac_channel->out_ch ==  2)) {
                /*双声道*/
                putchar('2');
                wlen = audio_sound_dac_write(alink_demo_hdl->dac_sound_demo, alink_demo_hdl->runbuf, ALINK_DEMO_FRAME_SIZE);
                if (wlen != ALINK_DEMO_FRAME_SIZE) {
                    printf("[alink_demo] DAC write fail !!!");
                }
            } else {
                /*单声道*/
                /*双声道数据转单声道*/
                putchar('1');
                pcm_LR_to_mono(alink_demo_hdl->runbuf, alink_demo_hdl->runbuf, ALINK_DEMO_FRAME_POINTS_NUM);
                wlen = audio_sound_dac_write(alink_demo_hdl->dac_sound_demo, alink_demo_hdl->runbuf, ALINK_DEMO_FRAME_SIZE / 2);
                if (wlen != ALINK_DEMO_FRAME_SIZE / 2) {
                    printf("[alink_demo] DAC write fail !!!");
                }
            }
            if (wlen != ALINK_DEMO_FRAME_SIZE) {
                printf("[alink_demo] DAC write fail !!!");
            }
#else
#endif
        } else {
            /* printf("cbuf data len %d", cbuf_get_data_len(&alink_demo_hdl->cbuf)); */
        }
    }
}

void audio_link_demo_open()
{
    int err = 0;
    printf("audio_link_demo_open");
    err = task_create(audio_link_demo_task, NULL, ALINK_DEMO_TASK_NAME);
    alink_demo_hdl = zalloc(sizeof(struct alink_demo));
    if (alink_demo_hdl == NULL) {
        printf("alink_demo_hdl malloc fail !!!");
        return;
    }
    cbuf_init(&alink_demo_hdl->cbuf, alink_demo_hdl->buf, sizeof(alink_demo_hdl->buf));

#if (CONFIG_OUT_SEL == OUT_IIS) || (CONFIG_IN_SEL == IN_IIS) || (ALINK_DEMO_CYCLE_TESE_IIS == 1)
    /*iis初始化*/
    alink_demo_hdl->hw_alink = alink_init(&alink_demo_config);
#endif

#if (CONFIG_IN_SEL == IN_SIN) && (!ALINK_DEMO_CYCLE_TESE_IIS)
    /*定时器做sin输入源*/
    printf("sin_timer : %d ms", ALINK_DEMO_SIN_TIME);
    alink_demo_hdl->sin_timer = sys_hi_timer_add(NULL, audio_sin_timer, ALINK_DEMO_SIN_TIME);
#endif
#if (CONFIG_IN_SEL == IN_IIS) || (ALINK_DEMO_CYCLE_TESE_IIS == 1)
    /*iis做输入源*/
    alink_demo_hdl->alink_rx_ch =  alink_channel_init(alink_demo_hdl->hw_alink, 1, ALINK_DIR_RX, NULL, alink_demo_rx_irq_handle);
    alink_set_swptr(alink_demo_hdl->alink_rx_ch, 0);
    alink_set_hwptr(alink_demo_hdl->alink_rx_ch, 0);
#endif

#if (CONFIG_OUT_SEL == OUT_DAC) || (ALINK_DEMO_CYCLE_TESE_IIS == 1)
    /*dac初始化*/
    audio_sound_dac_init();

    struct dac_param demo_dac_param = {0};
    demo_dac_param.sample_rate = ALINK_DEMO_SAMPLE_RATE;
    demo_dac_param.delay_ms = 50;
    alink_demo_hdl->dac_sound_demo = audio_sound_dac_open(&demo_dac_param);
    if (alink_demo_hdl->dac_sound_demo == NULL) {
        goto _error;
    }
    err = audio_sound_dac_start(alink_demo_hdl->dac_sound_demo);
    if (err) {
        goto _error;
    }
    audio_sound_dac_set_analog_gain(alink_demo_hdl->dac_sound_demo, 0xF, 3);
    audio_sound_dac_set_digital_gain(alink_demo_hdl->dac_sound_demo, 0xF, 16384);
#endif
#if (CONFIG_OUT_SEL == OUT_IIS) || (ALINK_DEMO_CYCLE_TESE_IIS == 1)
    /*iis输出初始化*/
    alink_demo_hdl->alink_tx_ch =  alink_channel_init(alink_demo_hdl->hw_alink, 0, ALINK_DIR_TX, NULL, alink_demo_tx_irq_handle);
#endif

#if (CONFIG_OUT_SEL == OUT_IIS) || (CONFIG_IN_SEL == IN_IIS) || (ALINK_DEMO_CYCLE_TESE_IIS == 1)
    /*打开iis模块*/
    alink_start(alink_demo_hdl->hw_alink);
#endif
    alink_demo_hdl->start = 1;

    /* while(1) { */
    /*     os_time_dly(10); */
    /* } */

_error :
    audio_sound_dac_stop(alink_demo_hdl->dac_sound_demo);
    audio_sound_dac_close(alink_demo_hdl->dac_sound_demo);
    audio_sound_dac_init();

}

void audio_link_demo_close()
{
    if (alink_demo_hdl) {

        task_kill(ALINK_DEMO_TASK_NAME);
#if (CONFIG_OUT_SEL == OUT_IIS) || (CONFIG_IN_SEL == IN_IIS) || (ALINK_DEMO_CYCLE_TESE_IIS == 1)
        alink_uninit(alink_demo_hdl->hw_alink);
#endif

#if (CONFIG_IN_SEL == IN_SIN) && (!ALINK_DEMO_CYCLE_TESE_IIS)
        if (alink_demo_hdl->sin_timer) {
            sys_hi_timer_del(alink_demo_hdl->sin_timer);
        }
#endif
#if (CONFIG_OUT_SEL == OUT_DAC) || (ALINK_DEMO_CYCLE_TESE_IIS == 1)
        audio_sound_dac_stop(alink_demo_hdl->dac_sound_demo);
        audio_sound_dac_close(alink_demo_hdl->dac_sound_demo);
        audio_sound_dac_init();
#endif
        free(alink_demo_hdl);
        alink_demo_hdl = NULL;
    }
}

