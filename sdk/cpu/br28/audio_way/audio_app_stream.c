
#include "app_config.h"
#include "media/includes.h"
#include "audio_config.h"
#include "audio_way.h"
#include "audio_app_stream.h"


#ifndef AUDIO_OUT_WAY_TYPE
#error "no defined AUDIO_OUT_WAY_TYPE"
#endif


struct audio_app_stream {
    struct audio_stream_entry entry;
    u32 out_way;
    u32 cur_sr;
    u8  start;
};

static struct audio_app_stream 	_audio_stream;

void audio_way_resume(void)
{
    /* putchar('r'); */
    if (_audio_stream.start) {
        audio_stream_resume(&_audio_stream.entry);
    }
}

static int audio_app_stream_data_handler(struct audio_stream_entry *entry,
        struct audio_data_frame *in,
        struct audio_data_frame *out)
{
    static u8 first_start_flag = 1;
    struct audio_app_stream *stream = container_of(entry, struct audio_app_stream, entry);
    AUDIO_WAY_PEND();
    if (in->stop) {
        if (stream->start) {
            stream->start = 0;
#if defined(TCFG_AUDIO_DAC_PA_PORT) && (TCFG_AUDIO_DAC_PA_PORT != NO_CONFIG_PORT)
            if (_audio_stream.out_way & AUDIO_WAY_TYPE_DAC) {
                gpio_direction_output(TCFG_AUDIO_DAC_PA_PORT, 0);
            }
#endif /* #if (TCFG_AUDIO_DAC_PA_PORT != NO_CONFIG_PORT) */
            audio_way_stop(_audio_stream.out_way);
            /* audio_way_ioctrl(_audio_stream.out_way, SNDCTL_IOCTL_POWER_OFF, NULL); */
            /* audio_way_close(_audio_stream.out_way); */
        }
        AUDIO_WAY_POST();
        return 0;
    } else {
        if (stream->start == 0) {
            stream->start = 1;
            stream->cur_sr = in->sample_rate;
            /* audio_way_open(_audio_stream.out_way); */
            audio_way_set_sample_rate(_audio_stream.out_way, stream->cur_sr);
#if (AUDIO_OUT_WAY_TYPE & AUDIO_WAY_TYPE_DAC)
            /* audio_way_substream_set_underrun_frames(AUDIO_WAY_TYPE_DAC, 1); */
#endif
            audio_way_ioctrl(_audio_stream.out_way, SNDCTL_IOCTL_POWER_ON, NULL);
            u8 volume = app_audio_get_volume(APP_AUDIO_CURRENT_STATE);

#if !SOUNDCARD_ENABLE	//28声卡不进行音量调整
#if !PC_VOL_INDEPENDENT_EN
            app_audio_set_volume(APP_AUDIO_CURRENT_STATE, volume, 1);
#else
            u8 volume_r = app_audio_get_volume_r(APP_AUDIO_CURRENT_STATE);
            app_audio_set_volume_each_channel(APP_AUDIO_CURRENT_STATE, volume, volume_r, 1);
#endif
#endif
            /* audio_way_set_gain(_audio_stream.out_way, volume); */
            audio_way_start(_audio_stream.out_way);
#if defined(TCFG_AUDIO_DAC_PA_PORT) && (TCFG_AUDIO_DAC_PA_PORT != NO_CONFIG_PORT)
            if (_audio_stream.out_way & AUDIO_WAY_TYPE_DAC) {
                if (first_start_flag) {
                    os_time_dly(50);//等到dac稳定，再开功放
                    first_start_flag = 0;
                }
                gpio_direction_output(TCFG_AUDIO_DAC_PA_PORT, 1);
            }
#endif /* #if (TCFG_AUDIO_DAC_PA_PORT != NO_CONFIG_PORT) */
        }
        if (stream->cur_sr != in->sample_rate) {
            stream->cur_sr = in->sample_rate;
            audio_way_set_sample_rate(_audio_stream.out_way, stream->cur_sr);
        }
    }
    if (in->data_len == 0) {
        AUDIO_WAY_POST();
        return 0;
    }
#if TCFG_USER_EMITTER_ENABLE && (AUDIO_OUT_WAY_TYPE & AUDIO_WAY_TYPE_BT)
    if (_audio_stream.out_way & AUDIO_WAY_TYPE_DAC) {
        extern u8 bt_emitter_audio_get_mute();
        if (bt_emitter_audio_get_mute()) {
            memset(in->data, 0, in->data_len);
        }
    }
#endif /* #if (AUDIO_OUT_WAY_TYPE & AUDIO_WAY_TYPE_BT) */
    int wlen = audio_way_output_write(in->data, in->data_len);
    AUDIO_WAY_POST();
    return wlen;
}

int audio_app_stream_switch_way(u32 close_way, u32 open_way)
{
    AUDIO_WAY_PEND();
    if (close_way) {
        audio_way_close(close_way);
        _audio_stream.out_way &= (~close_way);
#if defined(TCFG_AUDIO_DAC_PA_PORT) && (TCFG_AUDIO_DAC_PA_PORT != NO_CONFIG_PORT)
        if (close_way & AUDIO_WAY_TYPE_DAC) {
            gpio_direction_output(TCFG_AUDIO_DAC_PA_PORT, 0);
        }
#endif /* #if (TCFG_AUDIO_DAC_PA_PORT != NO_CONFIG_PORT) */
    }
    if (open_way) {
        audio_way_open(open_way);
        _audio_stream.out_way |= open_way;
    }
    _audio_stream.start = 0;
    audio_stream_resume(&_audio_stream.entry);
    AUDIO_WAY_POST();
    y_printf("switch way:0x%x \n", _audio_stream.out_way);
    return 0;
}

void audio_app_stream_init(void)
{
    memset(&_audio_stream, 0, sizeof(struct audio_app_stream));
    _audio_stream.entry.data_handler = audio_app_stream_data_handler;
    _audio_stream.out_way = AUDIO_OUT_WAY_TYPE;
    /* audio_way_open(AUDIO_OUT_WAY_TYPE); */
}

struct audio_stream_entry *audio_app_stream_get_entry(void)
{
    return &_audio_stream.entry;
}

/*****************************************************************************/
struct sound_pcm_stream *reverb_dac = NULL;
#if (AUDIO_OUT_WAY_TYPE & AUDIO_WAY_TYPE_IIS)
struct sound_pcm_stream *reverb_iis = NULL;
#endif
static struct audio_app_stream 	_audio_stream_dac;
static int audio_app_stream_dac_data_handler(struct audio_stream_entry *entry,
        struct audio_data_frame *in,
        struct audio_data_frame *out)
{
    struct audio_app_stream *stream = container_of(entry, struct audio_app_stream, entry);

    if (in->data_len) {
#if (AUDIO_OUT_WAY_TYPE & AUDIO_WAY_TYPE_DAC)
        sound_pcm_write(reverb_dac, in->data, in->data_len);
#endif
#if (AUDIO_OUT_WAY_TYPE & AUDIO_WAY_TYPE_IIS)
        sound_pcm_write(reverb_iis, in->data, in->data_len);
#endif
    }
    return in->data_len;
}

void audio_reverb_stream_dac_init(void)
{
    memset(&_audio_stream_dac, 0, sizeof(struct audio_app_stream));


    _audio_stream_dac.entry.data_handler = audio_app_stream_dac_data_handler;
    _audio_stream_dac.out_way = AUDIO_OUT_WAY_TYPE;

    int err;
#if (AUDIO_OUT_WAY_TYPE & AUDIO_WAY_TYPE_DAC)
    err = sound_pcm_create(&reverb_dac, "dac", 0);
#endif

#if (AUDIO_OUT_WAY_TYPE & AUDIO_WAY_TYPE_IIS)
    err = sound_pcm_create(&reverb_iis, "iis", 0);
#endif

    if (_audio_stream_dac.start == 0) {
        _audio_stream_dac.start = 1;

        struct sound_volume volume = {
            .chmap = SOUND_CHMAP_FL | SOUND_CHMAP_FR,
        };
        // 模拟音量
        u8 gain = app_audio_get_volume(APP_AUDIO_CURRENT_STATE);

#if (SYS_VOL_TYPE ==VOL_TYPE_DIGGROUP)//br28 最大模拟音量
        if (gain < 3) {
            gain = 3;
        }
#endif
        volume.volume[0] = gain;
        volume.volume[1] = gain;
#if (AUDIO_OUT_WAY_TYPE & AUDIO_WAY_TYPE_DAC)
        sound_pcm_ctl_ioctl(reverb_dac, SNDCTL_IOCTL_SET_ANA_GAIN, &volume);
        // 数字音量
        volume.volume[0] = 16384;
        volume.volume[1] = 16384;
        sound_pcm_ctl_ioctl(reverb_dac, SNDCTL_IOCTL_SET_DIG_GAIN, &volume);
        /* printf("JL_AUDIO->DAC_VL0 : 0x%x, \n", JL_AUDIO->DAC_VL0); */
        //sample_rate
        sound_pcm_prepare(reverb_dac, MIC_EFFECT_SAMPLERATE, 10, WRITE_MODE_FORCE);
        sound_pcm_ctl_ioctl(reverb_dac, SNDCTL_IOCTL_POWER_ON, NULL);
        sound_pcm_start(reverb_dac);
#endif

#if (AUDIO_OUT_WAY_TYPE & AUDIO_WAY_TYPE_IIS)
        sound_pcm_ctl_ioctl(reverb_iis, SNDCTL_IOCTL_SET_ANA_GAIN, &volume);
        // 数字音量
        volume.volume[0] = 16384;
        volume.volume[1] = 16384;
        sound_pcm_ctl_ioctl(reverb_iis, SNDCTL_IOCTL_SET_DIG_GAIN, &volume);
        sound_pcm_prepare(reverb_iis, MIC_EFFECT_SAMPLERATE, 10, WRITE_MODE_FORCE);
        sound_pcm_ctl_ioctl(reverb_iis, SNDCTL_IOCTL_POWER_ON, NULL);
        sound_pcm_start(reverb_iis);
#endif

        printf("-----------sound dac/iis start\n");
    }

}



struct audio_stream_entry *audio_reverb_stream_dac_get_entry(void)
{
    return &_audio_stream_dac.entry;
}

void audio_reverb_stream_dac_uninit(void)
{
    //printf("audio_reverb_stream_dac_uninit\n");
    if (reverb_dac) {
        if (_audio_stream_dac.start) {
            sound_pcm_stop(reverb_dac);
            /*混响关闭，不用关闭DAC电源，避免反复开关DAC有杂音*/
            //sound_pcm_ctl_ioctl(reverb_dac, SNDCTL_IOCTL_POWER_OFF, NULL);
        }
        sound_pcm_free(reverb_dac);
        reverb_dac = NULL;
    } else {
        printf("reverb_dac NULL\n");
    }

#if (AUDIO_OUT_WAY_TYPE & AUDIO_WAY_TYPE_IIS)
    if (reverb_iis) {
        sound_pcm_stop(reverb_iis);
        sound_pcm_ctl_ioctl(reverb_iis, SNDCTL_IOCTL_POWER_OFF, NULL);
        sound_pcm_free(reverb_iis);
    } else {
        printf("reverb_iis NULL\n");
    }
#endif

    audio_stream_del_entry(&_audio_stream_dac.entry);
}

