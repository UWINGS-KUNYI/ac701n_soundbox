
#include "app_config.h"
#include "media/includes.h"
#include "audio_way_dac.h"
#include "audio_config.h"

#ifndef AUDIO_OUT_WAY_TYPE
#error "no defined AUDIO_OUT_WAY_TYPE"
#endif

#if (AUDIO_OUT_WAY_TYPE & AUDIO_WAY_TYPE_DAC)

#if (AUDIO_OUT_WAY_TYPE & AUDIO_WAY_TYPE_DONGLE)
s16 dac_buff[1 * 1024] SEC(.dac_buff);
#else
#if (TCFG_AUDIO_DAC_BIT_WIDTH == DAC_24BIT_MODE)
int dac_buff[8 * 1024] SEC(.dac_buff);
#else
s16 dac_buff[4 * 1024] SEC(.dac_buff);
#endif /* 24bit */
#endif

void *audio_dac_get_buf(int *len)
{
    if (len) {
        *len = sizeof(dac_buff);
    }
    return dac_buff;
}

static int audio_dac_trim_value(struct audio_dac_trim *trim)
{
    int len = syscfg_read(CFG_DAC_TRIM_INFO, (void *)trim, sizeof(struct audio_dac_trim));
    if (len != sizeof(struct audio_dac_trim) || audio_dac_trim_value_check(trim)) {
        return -EINVAL;
    }
    return 0;
}

static void audio_dac_trim_end_handler(struct audio_dac_trim *trim)
{
    syscfg_write(CFG_DAC_TRIM_INFO, (void *)trim, sizeof(struct audio_dac_trim));
}

const struct audio_dac_platform_data sound_dac_data = {
    .output = TCFG_AUDIO_DAC_CONNECT_MODE,
    .mode = TCFG_AUDIO_DAC_MODE,
    .vcm_cap_en = 1,
    .clk_sel = (TCFG_CLOCK_SYS_SRC == SYS_CLOCK_INPUT_PLL_RCL) ? AUD_DIG_CLK : AUD_OSC_CLK,
    .power = {
        .ldo_isel = 3,
        .ldo_fb_isel = 3,
        .lpf_isel   = 0xf,//0x8,
        .vcmo_en = 0,//1,
        .vcmo_always_on = 0,
        .vcm_risetime = 0,
        .trim_poweron_time = 500,
        .trim_value = audio_dac_trim_value,
        .trim_begin = NULL,
        .trim_end   = audio_dac_trim_end_handler,
    },
    .codec = {
        .dsm_clk = DAC_DSM_6MHz,
        .bit_mode = TCFG_AUDIO_DAC_BIT_WIDTH,
    },
    .fade_out_step_min = 20,
};

void audio_way_dac_init(void)
{
    struct sound_pcm_platform_data data;
    data.dma_addr = (void *)dac_buff;
    data.dma_bytes = sizeof(dac_buff);
    data.fifo_bytes = sizeof(dac_buff);
    data.private_data = (void *)&sound_dac_data;
    sound_platform_load("dac", &data);
}

static int audio_dac_buf_frames_fade_out(int frames)
{
    u16 buffered_frames = JL_AUDIO->DAC_LEN - JL_AUDIO->DAC_SWN - 1;
    s16 offset;
    int down_step;
    int value = 16384;
    int i, j;
    if (frames > buffered_frames) {
        frames = buffered_frames;
    }
    if (frames < 1) {
        frames = 1;
    }
    offset = JL_AUDIO->DAC_SWP - frames;
    if (offset < 0) {
        offset += JL_AUDIO->DAC_LEN;
    }
    down_step = (16384 / frames) + 1;
    if (global_bit_wide) {
        s32 *ptr;
        for (i = 0; i < frames; i++) {
            if (value > 0) {
                value -= down_step;
                if (value < 0) {
                    value = 0;
                }
            }
            ptr = (s16 *)JL_AUDIO->DAC_ADR + offset * audio_way_get_channel_num(AUDIO_WAY_TYPE_DAC);
            for (j = 0; j < audio_way_get_channel_num(AUDIO_WAY_TYPE_DAC); j++) {
                int tmp = *(ptr + j);
                *(ptr + j) = ((long long)tmp * (long long)value) / 16384;
            }
            if (++offset >= JL_AUDIO->DAC_LEN) {
                offset = 0;
            }
        }
    } else {
        s16 *ptr;
        for (i = 0; i < frames; i++) {
            if (value > 0) {
                value -= down_step;
                if (value < 0) {
                    value = 0;
                }
            }
            ptr = (s16 *)JL_AUDIO->DAC_ADR + offset * audio_way_get_channel_num(AUDIO_WAY_TYPE_DAC);
            for (j = 0; j < audio_way_get_channel_num(AUDIO_WAY_TYPE_DAC); j++) {
                int tmp = *(ptr + j);
                *(ptr + j) = tmp * value / 16384;
            }
            if (++offset >= JL_AUDIO->DAC_LEN) {
                offset = 0;
            }
        }
    }

    return 0;
}


static void audio_dac_delay_handler(void)
{
    u32 protect_time = 1; //1ms
    int unread_frames = JL_AUDIO->DAC_LEN - JL_AUDIO->DAC_SWN - 1;
    u32 protect_pns = protect_time ? (protect_time * audio_way_get_sample_rate(AUDIO_WAY_TYPE_DAC) / 1000) : 0;
    /*JL_AUDIO->DAC_CON |= BIT(8);// DAC fifo release*/
    /* JL_AUDIO->DAC_PNS = protect_pns ? protect_pns : IRQ_MIN_POINTS; */

    JL_AUDIO->DAC_PNS = protect_pns;
    if (protect_pns) {
        /* putchar('L'); */
        if (unread_frames <= protect_pns) {
            /* putchar('D'); */
            /*DAC缓冲已达到较低水平，很可能会出噪声*/
            audio_dac_buf_frames_fade_out(unread_frames);
        } else {
            /* putchar('C'); */
            JL_AUDIO->DAC_PNS = protect_pns;
            return;
        }
    }
    JL_AUDIO->DAC_CON &= ~BIT(5);
}

__attribute__((weak))
void audio_dac_wakeup_irq_handler(void *priv)
{
    /* putchar('R'); */
    audio_way_resume();
    /* audio_dac_delay_handler(); //数据低于1ms时对数据做淡出 */
}

struct audio_way *audio_way_dac_open(void)
{
    struct audio_way *audio_hdl;
    struct sound_pcm_stream *dac = NULL;
    int err = sound_pcm_create(&dac, "dac", 0);
    if (err) {
        log_e("Create dac sound pcm error.\n");
        return NULL;
    }
    sound_pcm_ctl_ioctl(dac, SNDCTL_IOCTL_SET_BIAS_TRIM, 0);
    sound_pcm_set_irq_handler(dac, NULL, audio_dac_wakeup_irq_handler);
    audio_hdl = zalloc(sizeof(struct audio_way));
    ASSERT(audio_hdl);
    audio_hdl->way_type = AUDIO_WAY_TYPE_DAC;
    audio_hdl->stream = dac;
    audio_hdl->state = AUDIO_WAY_STATE_IDLE;
    switch (sound_dac_data.output) {
    case DAC_OUTPUT_MONO_L:
    case DAC_OUTPUT_MONO_R:
        audio_hdl->out_ch = 1;
        break;
    default :
        audio_hdl->out_ch = 2;
        break;
    }
    return audio_hdl;
}


#endif /*(AUDIO_OUT_WAY_TYPE & AUDIO_WAY_TYPE_DAC)*/

