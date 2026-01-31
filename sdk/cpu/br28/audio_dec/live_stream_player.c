#include "asm/includes.h"
#include "system/includes.h"
#include "app_main.h"
#include "audio_dec.h"
#include "audio_decoder.h"
#include "audio_config.h"
#include "asm/audio_adc.h"
#include "mixer.h"
#include "debug.h"
#include "audio_syncts.h"
#include "audio_way.h"
#include "asm/dac.h"
#include "audio_splicing.h"
#include "wireless_mic_effect.h"
#include "audio_recorder_mix.h"
#include "audio_effect/audio_effect_task.h"
#if ((defined MULTI_AUDIO_UPLOAD_TO_UAC_ENABLE) && MULTI_AUDIO_UPLOAD_TO_UAC_ENABLE)
#include "audio_usb_mix_mic.h"
extern void *usb_mix_fifo;
#endif

#if (TCFG_BROADCAST_ENABLE || TCFG_CONNECTED_ENABLE)
#include "live_audio.h"

#define LOG_TAG         "[LIVE_PLAYER]"
#define LOG_INFO_ENABLE
#define LOG_DEBUG_ENABLE
#define LOG_DUMP_ENABLE
#define LOG_ERROR_ENABLE
#define LOG_WARN_ENABLE

#if TCFG_APP_LIVE_MIC_EN
#define ENABLE_HOWLING           1      //该宏是使能啸叫抑制算法的宏，相当于广播mic啸叫抑制的总开关
#define ENABLE_HOWLING_PS     	 1		//移频
#define ENABLE_HOWLING_TRAP		 1	    //馅波
#else
#define ENABLE_HOWLING           0      //该宏是使能啸叫抑制算法的宏，相当于广播mic啸叫抑制的总开关
#define ENABLE_HOWLING_PS     	 0		//移频
#define ENABLE_HOWLING_TRAP		 0	    //馅波
#endif


#define LIVE_PLAYER_STATE_OPEN             1
#define LIVE_PLAYER_STATE_START            2
#define LIVE_PLAYER_STATE_STOP             3
#define LIVE_PLAYER_STATE_CLOSING          4
#define LIVE_PLAYER_STATE_CLOSE            5


#if (WIRELESS_MIC_EFFECT_ENABLE && WIRELESS_MASTER_MIC_EFFECT_ENABLE)
#define LIVE_PLAYER_MIC_EFFECT_ENABLE      0
#elif (WIRELESS_MIC_EFFECT_ENABLE && WIRELESS_SLAVE_MIC_EFFECT_ENABLE)
#define LIVE_PLAYER_MIC_EFFECT_ENABLE      1
#else
#define LIVE_PLAYER_MIC_EFFECT_ENABLE      0
#endif

#ifndef TCFG_JLA_STREAM_CUSTOM_ADD_DELAY
#define TCFG_JLA_STREAM_CUSTOM_ADD_DELAY   0
#endif
/*下行播放延时设置*/
#define LIVE_STREAM_RX_LATENCY                  (500L)

#if (TCFG_BROADCAST_MODE_EQ_ENABLE || TCFG_BROADCAST_MODE_DRC_ENABLE)
#if (TCFG_MIC_EFFECT_ENABLE)
#define LIVE_STREAM_EFFECT_LATENCY              (8 * 1000L)
#else
#define LIVE_STREAM_EFFECT_LATENCY              (3 * 1000L)
#endif
#else
#define LIVE_STREAM_EFFECT_LATENCY              (1 * 1000L)
#endif /*(TCFG_BROADCAST_MODE_EQ_ENABLE || TCFG_BROADCAST_MODE_DRC_ENABLE)*/

#define LIVE_JLA_STREAM_DECODING_LATENCY(frame_len, nch)    \
    (((frame_len > 50 ? 2 : 1) * 1000L) + (nch * 1000L / 2))

#if (defined(WIRELESS_SOUND_TRACK_2_P_X_ENABLE)&&WIRELESS_SOUND_TRACK_2_P_X_ENABLE) || (defined(TCFG_DYNAMIC_EQ_ENABLE) && TCFG_DYNAMIC_EQ_ENABLE)
//若音效模块过多可能会导致从机卡顿，需要在此处酌情增加延时
#define LIVE_STREAM_PLAY_LATENCY(dec_latency)             (40 * 1000L)//无线2.x声道时，增加延时
#define LIVE_JLA_STREAM_PLAY_LATENCY(dec_latency, frame_len)         (40 * 1000L)
#define LIVE_STREAM_LOW_LATENCY             0

#else

#define LIVE_STREAM_PLAY_LATENCY(dec_latency)       (LIVE_STREAM_RX_LATENCY + LIVE_STREAM_EFFECT_LATENCY + dec_latency)

#if ((defined TCFG_LIVE_AUDIO_LOW_LATENCY_EN) && TCFG_LIVE_AUDIO_LOW_LATENCY_EN)
#define LIVE_JLA_STREAM_PLAY_LATENCY(dec_latency, frame_len)  (LIVE_STREAM_PLAY_LATENCY(dec_latency))
#define LIVE_STREAM_LOW_LATENCY             1
#else
#define LIVE_JLA_STREAM_PLAY_LATENCY(dec_latency, frame_len)  (LIVE_STREAM_PLAY_LATENCY(dec_latency) + (((frame_len / 10)/2) * 1000L) + TCFG_JLA_STREAM_CUSTOM_ADD_DELAY)
#define LIVE_STREAM_LOW_LATENCY             0
#endif /*((defined TCFG_LIVE_AUDIO_LOW_LATENCY_EN) && TCFG_LIVE_AUDIO_LOW_LATENCY_EN)*/

#endif

#define LIVE_STREAM_MULTI_PLAYER_ENABLE         1

extern struct audio_decoder_task decode_task;
extern struct audio_mixer mixer;
extern struct audio_dac_hdl dac_hdl;
extern const int JLA_2CH_L_OR_R;
extern const int JLA_CODEC_SOFT_DECISION_ENABLE;

extern void bt_audio_sync_nettime_select(u8 base);

#define LIVE_PLAY_DECODE_TASK       &decode_task
#define LIVE_PLAY_MIXER             &mixer
#define LIVE_PLAY_DAC_HDL           &dac_hdl

/* 资源互斥方式配置 */
#if 0
#define LIVE_PLAYER_CRITICAL_INIT()
#define LIVE_PLAYER_ENTER_CRITICAL()  local_irq_disable()
#define LIVE_PLAYER_EXIT_CRITICAL()   local_irq_enable()
#else
#define LIVE_PLAYER_CRITICAL_INIT()   spin_lock_init(&ctx->lock)
#define LIVE_PLAYER_ENTER_CRITICAL()  spin_lock(&ctx->lock)
#define LIVE_PLAYER_EXIT_CRITICAL()   spin_unlock(&ctx->lock)
#endif

struct live_player_effect {
    struct audio_stream_entry entry;
    u8 nch;
    u8 first_ts;
    s16 used_len;
    int sample_rate;
    void *syncts;
    void *eq;
    void *drc;
    u32 base_time;
};

struct live_player_pcm_capture {
    struct audio_stream_entry entry;
    void *path;
    int (*write_frame)(void *path, struct audio_frame *frame);
    u8 enable;
    u8 wait_enable;
    u8 nch;                                 //输出pcm数据声道数
    s16 sample_buffer[256 * 3];             //256 是中断点数
    OS_MUTEX mutex;
};

struct live_player_mic_effect {
    struct audio_stream_entry entry;
    struct live_mic_effect *effect;
    s16 used_len;
};
struct live_player_music_effect {

#if AUDIO_SURROUND_CONFIG
    surround_hdl *surround;         //环绕音效句柄
#endif
#if AUDIO_VBASS_CONFIG
    struct aud_gain_process *vbass_prev_gain;
    NOISEGATE_API_STRUCT *ns_gate;
    vbass_hdl *vbass;               //虚拟低音句柄
#endif

#if AUDIO_VOCAL_REMOVE_EN && LIVE_PLAYER_VOCAL_REMOVE_EN
    vocal_remove_hdl *vocal_hdl;
#endif

#if TCFG_EQ_ENABLE && TCFG_AUDIO_OUT_EQ_ENABLE
    struct audio_eq  *high_bass;
    struct audio_drc *hb_drc;//高低音后的drc
    struct convert_data *hb_convert;
#endif

    struct audio_eq *eq;    //eq drc句柄
    struct audio_drc *drc;    // drc句柄
#if (defined(TCFG_DRC_SPILT_ENABLE) && TCFG_DRC_SPILT_ENABLE)
    struct audio_drc *drc_fr;    // drc fr句柄
#endif
#if defined(TCFG_DRC_ENABLE) && TCFG_DRC_ENABLE
    struct convert_data *convert;
#endif
#if defined(MUSIC_EXT_EQ_AFTER_DRC) && MUSIC_EXT_EQ_AFTER_DRC
    struct audio_eq *ext_eq;    //eq drc句柄 扩展eq
#endif
#if defined(TCFG_DYNAMIC_EQ_ENABLE) && TCFG_DYNAMIC_EQ_ENABLE
    struct audio_eq *eq2;    //eq drc句柄
    struct dynamic_eq_hdl *dy_eq;
    struct dynamic_eq_pro *dy_eq_pro;
#if defined(TCFG_BROADCAST_MODE_LAST_DRC_ENABLE)&&TCFG_BROADCAST_MODE_LAST_DRC_ENABLE
    struct audio_drc *last_drc;
#endif
    struct convert_data *convert2;
#endif
#if defined(TCFG_PHASER_GAIN_AND_CH_SWAP_ENABLE) && TCFG_PHASER_GAIN_AND_CH_SWAP_ENABLE
    struct aud_gain_process *gain;
#endif

#if TCFG_EQ_DIVIDE_ENABLE
    struct audio_eq *eq_rl_rr;    //eq rl_rr句柄
    struct audio_drc *drc_rl_rr;  // drc rl句柄
#if (defined(TCFG_DRC_SPILT_ENABLE) && TCFG_DRC_SPILT_ENABLE)
    struct audio_drc *drc_rr;     // drc rr句柄
#endif
    struct convert_data *convert_rl_rr;//位宽转换(32->16)

#if defined(TCFG_PHASER_GAIN_AND_CH_SWAP_ENABLE) && TCFG_PHASER_GAIN_AND_CH_SWAP_ENABLE
    struct aud_gain_process *gain_rl_rr;//rl rr左右声道合并、相位控制
#endif

#if defined(MUSIC_EXT_EQ_AFTER_DRC) && MUSIC_EXT_EQ_AFTER_DRC
    struct audio_eq *ext_eq2;    //eq drc句柄 扩展eq
#endif

#endif
    struct convert_data *convert_32_to_16;//位宽转换(32->16)
#if TCFG_AUDIO_EFFECT_TASK_ENABLE
    struct audio_stream_entry *effect_task_entry;
#endif

#if SOUND_TRACK_1_P_1_CH_CONFIG && (EFFECT_PROCESS_POITION_CONFIG == EFFECT_BEFORE_STEREO_MIX)
    struct audio_vocal_tract vocal_tract;//声道合并目标句柄
    struct audio_vocal_tract_ch synthesis_ch_fl_fr;//声道合并句柄
    struct audio_vocal_tract_ch synthesis_ch_rl_rr;//声道合并句柄
    struct channel_switch *fl_fr_ch_2to1;//声道变换
    struct channel_switch *rl_rr_ch_2to1;//声道变换
#endif

    u8 bass_sel;                        //低音通路音效控制

};

struct live_stream_player_context {
    struct audio_decoder decoder;       //解码句柄
    struct audio_res_wait res_wait;		//解码资源等待
    struct audio_mixer_ch mix_ch;   	//叠加句柄
#if (RECORDER_MIX_EN)
    void *rec_mix_ch;	// 叠加句柄
#endif/*RECORDER_MIX_EN*/
    struct audio_stream *stream;		// 音频流
    struct audio_fmt in_fmt;        	// 输入数据参数
    struct audio_dec_input dec_input;
    struct audio_frame *frame;
    void *ipath;
    struct audio_frame *(*get_frame)(void *path);
    void (*free_frame)(void *path, struct audio_frame *frame);
    void *clock;
    u32(*clock_time)(void *clock, u8 type, struct reference_time *time);
    struct live_player_effect effect; //音频音效处理
    struct live_player_music_effect music_effect; //music音效处理
    struct live_player_mic_effect mic_effect; //mic音效处理
    struct live_player_pcm_capture capture;
    volatile u8 state;                  //解码状态
    u32 timer;							//数据释放timer
    u8 dec_out_ch_mode; 				//解码输出声道模式
    u8 dec_out_ch_num; 				    //解码输出声道数  解码输出给后级的声道数
    u8 device;
    u8 network;
#if ((defined MULTI_AUDIO_UPLOAD_TO_UAC_ENABLE) && MULTI_AUDIO_UPLOAD_TO_UAC_ENABLE)
    void *uac_mix_ch;
#endif
    int decoding_latency;
    int play_latency;

    spinlock_t lock;

};

static u8 live_player_num = 0;
void live_stream_player_close(void *priv);
static void live_stream_stop_drain(struct live_stream_player_context *ctx);
/*----------------------------------------------------------------------------*/
/**@brief   外部激活解码接口
   @param   *priv:私有句柄
   @note
*/
/*----------------------------------------------------------------------------*/
void live_stream_player_wakeup(void *priv)
{
    struct live_stream_player_context *ctx = (struct live_stream_player_context *)priv;
    LIVE_PLAYER_ENTER_CRITICAL();
    if (ctx->state == LIVE_PLAYER_STATE_START) {
        audio_decoder_resume(&ctx->decoder);
    }
    LIVE_PLAYER_EXIT_CRITICAL();
}

/*----------------------------------------------------------------------------*/
/**@brief    读取解码数据
   @param    *decoder: 解码器句柄
   @param    *buf: 数据
   @param    len: 数据长度
   @return   >=0：读到的数据长度
   @return   <0：错误
   @note
*/
/*----------------------------------------------------------------------------*/
static int live_stream_read(struct audio_decoder *decoder, void *buf, u32 len)
{
    struct live_stream_player_context *ctx = container_of(decoder, struct live_stream_player_context, decoder);
    int rlen = 0;

    if (!ctx->get_frame) {
        return -1;
    }

    if (!ctx->frame) {
        ctx->frame = ctx->get_frame(ctx->ipath);
        if (!ctx->frame) {
            return -1;
        }
        /*printf("%d\n", ctx->frame->timestamp);*/
    }
    struct audio_frame *frame = ctx->frame;
    if (frame->offset == 0) {
        if (ctx->effect.syncts) {
            int latency = ctx->play_latency;
            u32 timestamp = (((frame->timestamp & 0xfffffff) + latency) & 0xfffffff) | (frame->timestamp & 0xf0000000);
            if (ctx->effect.first_ts) { //获取到的第一个时间戳，和当前时间相差较小时，说明数据同步时间不够，将数据丢掉.
                int time_diff = timestamp - ctx->clock_time(ctx->clock, PLAY_SYNCHRONIZE_CURRENT_TIME, NULL);
                if (time_diff < latency * 2 / 3) {
                    ctx->free_frame(ctx->ipath, frame);
                    ctx->frame = NULL;
                    audio_decoder_resume(&ctx->decoder); //丢掉数据，返回挂起，要自己激活解码.
                    return -1;
                }
            }

            /*printf("-timestamp : %d, %d-\n", timestamp, ctx->clock_time(ctx->clock, PLAY_SYNCHRONIZE_CURRENT_TIME, NULL));*/
            audio_syncts_next_pts(ctx->effect.syncts, timestamp);
            if (ctx->effect.first_ts) {
                ctx->effect.first_ts = 0;
                ctx->effect.base_time = timestamp;
            }
        }
    }

    rlen = frame->len - frame->offset;
    if (rlen > len) {
        rlen = len;
    }
    memcpy(buf, frame->data + frame->offset, rlen);
    frame->offset += rlen;
    if (frame->offset == frame->len) {
        ctx->free_frame(ctx->ipath, frame);
        ctx->frame = NULL;
    }

    if (rlen == 0) {
        /*数据存在残缺，需要直接唤醒读取下一帧*/
        audio_decoder_resume(&ctx->decoder);
        return -1;
    }
    return rlen;

}

static int live_stream_get_frame(struct audio_decoder *decoder, void **frame)
{
    struct live_stream_player_context *ctx = container_of(decoder, struct live_stream_player_context, decoder);

    if (!ctx->get_frame) {
        return -1;
    }

    if (!ctx->frame) {
        ctx->frame = ctx->get_frame(ctx->ipath);
        if (!ctx->frame) {
            return -1;
        }
    }

    if (ctx->effect.syncts) {
        int latency = ctx->play_latency;
        u32 timestamp = (((ctx->frame->timestamp & 0xfffffff) + latency) & 0xfffffff) | (ctx->frame->timestamp & 0xf0000000);
        if (ctx->effect.first_ts) { //获取到的第一个时间戳，和当前时间相差较小时，说明数据同步时间不够，将数据丢掉.
            int time_diff = timestamp - ctx->clock_time(ctx->clock, PLAY_SYNCHRONIZE_CURRENT_TIME, NULL);
            if (time_diff < latency / 2) {
                ctx->free_frame(ctx->ipath, ctx->frame);
                ctx->frame = NULL;
                audio_decoder_resume(&ctx->decoder); //丢掉数据，返回挂起，要自己激活解码.
                return -1;
            }
        }

        /*printf("-timestamp : %d, %d-\n", timestamp, ctx->clock_time(ctx->clock, PLAY_SYNCHRONIZE_CURRENT_TIME, NULL));*/
        audio_syncts_next_pts(ctx->effect.syncts, timestamp);
        if (ctx->effect.first_ts) {
            ctx->effect.first_ts = 0;
            ctx->effect.base_time = timestamp;
        }
    }

    *frame = ctx->frame->data;
    return ctx->frame->len;
}

static void live_stream_free_frame(struct audio_decoder *decoder, void *frame)
{
    struct live_stream_player_context *ctx = container_of(decoder, struct live_stream_player_context, decoder);

    if (ctx->frame) {
        ctx->free_frame(ctx->ipath, ctx->frame);
        ctx->frame = NULL;
    }
}

/*----------------------------------------------------------------------------*/
/**@brief    文件指针定位
   @param    *decoder: 解码器句柄
   @param    offset: 定位偏移
   @param    seek_mode: 定位类型
   @return   0：成功
   @return   非0：错误
   @note
*/
/*----------------------------------------------------------------------------*/
static int live_stream_seek(struct audio_decoder *decoder, u32 offset, int seek_mode)
{
    struct live_stream_player_context *ctx = container_of(decoder, struct live_stream_player_context, decoder);


    return 0;
}

/*----------------------------------------------------------------------------*/
/**@brief    读取文件长度
   @param    *decoder: 解码器句柄
   @return   文件长度
   @note
*/
/*----------------------------------------------------------------------------*/
static int live_stream_len(struct audio_decoder *decoder)
{
    struct live_stream_player_context *ctx = container_of(decoder, struct live_stream_player_context, decoder);
    /*数据流返回最大长度*/
    return 0xffffffff;
}

static const struct audio_dec_input live_stream_dec_input = {
    .coding_type = AUDIO_CODING_JLA,
    .data_type   = AUDIO_INPUT_FILE,
    .ops = {
        .file = {
            .fread = live_stream_read,
            .fseek = live_stream_seek,
            .flen  = live_stream_len,
        }
    }
};



/*----------------------------------------------------------------------------*/
/**@brief    解码预处理
   @param    *decoder: 解码器句柄
   @return   0：成功
   @note
*/
/*----------------------------------------------------------------------------*/
static int live_stream_dec_probe_handler(struct audio_decoder *decoder)
{
    return 0;
}

/*----------------------------------------------------------------------------*/
/**@brief    文件解码后处理
   @param    *decoder: 解码器句柄
   @return   0：成功
   @note
*/
/*----------------------------------------------------------------------------*/
static int live_stream_dec_post_handler(struct audio_decoder *decoder)
{
    return 0;
}

static const struct audio_dec_handler live_stream_dec_handler = {
    .dec_probe  = live_stream_dec_probe_handler,
    .dec_post   = live_stream_dec_post_handler,

};

/*----------------------------------------------------------------------------*/
/**@brief    broadcast解码事件处理
   @param    *encoder: 解码器句柄
   @param    argc: 参数个数
   @param    *argv: 参数
   @return
   @note
*/
/*----------------------------------------------------------------------------*/
static void live_stream_dec_event_handler(struct audio_decoder *decoder, int arg, int *argv)
{
    struct live_stream_player_context *ctx = container_of(decoder, struct live_stream_player_context, decoder);

    switch (argv[0]) {
    case AUDIO_DEC_EVENT_END:
    case AUDIO_DEC_EVENT_ERR:
        live_stream_player_close(ctx);
        break;
    default:
        break;
    }
}


static void live_player_syncts_mix_ch_event_handler(void *priv, int event, int param)
{
    struct live_stream_player_context *ctx = (struct live_stream_player_context *)priv;
    int time_diff = 0;
    int slience_frames = 0;
    switch (event) {
    case MIXER_EVENT_CH_OPEN:
        sound_pcm_device_mount_syncts(AUDIO_OUT_WAY_TYPE, ctx->effect.syncts);
        if (ctx->clock_time) {
            time_diff = ctx->effect.base_time - ctx->clock_time(ctx->clock, PLAY_SYNCHRONIZE_CURRENT_TIME, NULL);
        }
        if (time_diff > 0 && time_diff < 500000) {
            slience_frames = (u64)time_diff * ctx->effect.sample_rate / 1000000 - sound_pcm_device_buffered_len((void *)AUDIO_OUT_WAY_TYPE);
            if (slience_frames <= 0) {
                break;
            }
            log_info("=====slience_frames %d=====\n", slience_frames);
            audio_mixer_ch_add_slience_samples(&ctx->mix_ch, slience_frames * audio_output_channel_num());//因使用mix做声道变换，此处计算使用mix的实际输出声道数
            sound_pcm_update_frame_num(ctx->effect.syncts, -slience_frames);

        }
        break;
    case MIXER_EVENT_CH_CLOSE:
        sound_pcm_device_unmount_syncts(AUDIO_OUT_WAY_TYPE, ctx->effect.syncts);
        break;
    }
}

/*----------------------------------------------------------------------------*/
/**@brief    broadcast解码数据流激活
   @param    *priv: 私有句柄
   @return
   @note
*/
/*----------------------------------------------------------------------------*/
static void live_player_resume_from_stream(void *priv)
{
    struct live_stream_player_context *ctx = (struct live_stream_player_context *)priv;

    audio_decoder_resume(&ctx->decoder);
}


static int audio_syncts_output_handler(void *priv, void *data, int len)
{
    struct live_player_effect *effect = (struct live_player_effect *)priv;

    struct audio_data_frame frame = {
        .data = data,
        .data_len = len,
        .channel = effect->nch,
        .sample_rate = effect->sample_rate,
    };

    audio_stream_run(&effect->entry, &frame);

    return effect->used_len;
}

static int live_player_effect_data_handler(struct audio_stream_entry *entry,  struct audio_data_frame *in, struct audio_data_frame *out)
{
    struct live_player_effect *effect = container_of(entry, struct live_player_effect, entry);

    if (in->data_len) {
        out->no_subsequent = 1;  //数据流节点自己调用audio_stream_run,需要置1，不跑数据流节点的递归调用;
    }
    if (effect->syncts) {
        int wlen = audio_syncts_frame_filter(effect->syncts, in->data, in->data_len);
        if (wlen < in->data_len) {
            audio_syncts_trigger_resume(effect->syncts, (void *)entry, (void (*)(void *))audio_stream_resume);
        } else {
#if ((defined TCFG_LIVE_AUDIO_LOW_LATENCY_EN) && TCFG_LIVE_AUDIO_LOW_LATENCY_EN)
            audio_syncts_push_data_out(effect->syncts);
#endif
        }
        return wlen;
    }

    return in->data_len;
}

static void live_player_effect_data_process_len(struct audio_stream_entry *entry, int len)
{
    struct live_player_effect *effect = container_of(entry, struct live_player_effect, entry);

    effect->used_len = len;
}

static int audio_syncts_latch_reference_time(void *priv, int cmd, void *param)
{
    struct live_stream_player_context *ctx = (struct live_stream_player_context *)priv;
    int err = 0;

    switch (cmd) {
    case BLE_LATCH_REFERENCE_TIME:
        if (ctx->clock_time) {
            err = ctx->clock_time(ctx->clock, PLAY_SYNCHRONIZE_LATCH_TIME, NULL);
        }
        break;
    case BLE_GET_REFERENCE_TIME_US:
        if (ctx->clock_time) {
            if (!param) {
                break;
            }
            err = ctx->clock_time(ctx->clock, PLAY_SYNCHRONIZE_GET_TIME, param);
        }
        break;
    default:
        break;
    }

    return err;
}

static int live_player_effect_open(struct live_stream_player_context *ctx)
{
    ctx->effect.nch = ctx->dec_out_ch_num;
    ctx->effect.sample_rate = audio_mixer_get_sample_rate(LIVE_PLAY_MIXER);

    /*音效处理之同步模块初始化*/
    struct audio_syncts_params params = {0};
    params.nch = ctx->effect.nch;
#if TCFG_AUDIO_OUTPUT_IIS
    params.pcm_device = PCM_OUTSIDE_DAC;
#else
    params.pcm_device = PCM_INSIDE_DAC;
#endif /*TCFG_AUDIO_OUTPUT_IIS*/
    params.network = AUDIO_NETWORK_BLE;
    params.rin_sample_rate = ctx->in_fmt.sample_rate;
    params.rout_sample_rate = ctx->effect.sample_rate;
    params.priv = &ctx->effect;
    params.output = audio_syncts_output_handler;
    params.reference_clock = ctx;
    params.reference_time_handler = audio_syncts_latch_reference_time;
    params.bit_mode = global_bit_wide;
    bt_audio_sync_nettime_select(2);//0 - a2dp主机，1 - tws, 2 - BLE
    int err = audio_syncts_open(&ctx->effect.syncts, &params);
    if (ctx->effect.syncts) {
        audio_mixer_ch_set_event_handler(&ctx->mix_ch, (void *)ctx, live_player_syncts_mix_ch_event_handler);
    }
    /*TODO : 后续的音效处理可以在这里接入*/
    ctx->effect.first_ts = 1;

    ctx->effect.entry.data_handler = live_player_effect_data_handler;
    ctx->effect.entry.data_process_len = live_player_effect_data_process_len;
    return err;
}

static void live_player_effect_close(struct live_stream_player_context *ctx)
{
    audio_stream_del_entry(&ctx->effect.entry);

    if (ctx->effect.syncts) {
        audio_syncts_close(ctx->effect.syncts);
    }
}

static int live_player_pcm_capture_handler(struct audio_stream_entry *entry, struct audio_data_frame *in, struct audio_data_frame *out)
{
    struct live_player_pcm_capture *capture = container_of(entry, struct live_player_pcm_capture, entry);
    out->data_len = in->data_len;
    out->data = in->data;

    os_mutex_pend(&capture->mutex, 0);
    if (in->offset == 0) {
        if (!capture->enable) {
            os_mutex_post(&capture->mutex);
            return in->data_len;
        }
        struct audio_frame frame = {0};
        frame.data = in->data;
        frame.len = in->data_len;
        frame.sample_rate = in->sample_rate;
        frame.nch = in->channel;
        frame.coding_type = AUDIO_CODING_PCM;
        frame.mode = AUDIO_FRAME_LIVE_STREAM;
        if (in->channel >= 2) {
            /*输出两个linein的数据,默认输出第一个和第二个采样通道的数据*/
            if (capture->nch >= 2) {
                if (capture->write_frame) {
                    int wlen = capture->write_frame(capture->path, &frame);
                    if (wlen < in->data_len) {
                        putchar('C');
                    }
                    /* return wlen; */
                }
            } else { //硬件配置双声道,需要输出单声道,这种情况最好硬件也配成单声道避免资源浪费.
                pcm_dual_to_single(capture->sample_buffer, in->data, in->data_len);
                if (capture->write_frame) {
                    frame.data = capture->sample_buffer;
                    frame.len = in->data_len / 2;
                    int wlen = capture->write_frame(capture->path, &frame);
                    if (wlen < in->data_len / 2) {
                        putchar('c');
                    }
                    /* return wlen * 2; */
                }
            }

        } else {
            if (capture->nch >= 2) {
                pcm_single_to_dual(capture->sample_buffer, in->data, in->data_len);
                if (capture->write_frame) {
                    frame.data = capture->sample_buffer;
                    frame.len = in->data_len * 2;
                    int wlen = capture->write_frame(capture->path, &frame);
                    if (wlen < in->data_len * 2) {
                        putchar('c');
                    }
                    /* return wlen / 2; */
                }
            } else {
                if (capture->write_frame) {
                    int wlen = capture->write_frame(capture->path, &frame);
                    if (wlen < in->data_len) {
                        putchar('C');
                    }
                    /* return wlen; */
                }
            }
        }
    }
    os_mutex_post(&capture->mutex);
    return in->data_len;
}

static void live_player_pcm_capture_process_len(struct audio_stream_entry *entry, int len)
{
}

static int live_player_pcm_capture_open(struct live_stream_player_context *ctx)
{
    ctx->capture.enable = 0;
    if (ctx->capture.wait_enable) {
        ctx->capture.enable = 1;
    }
    os_mutex_create(&ctx->capture.mutex);
    ctx->capture.entry.data_handler = live_player_pcm_capture_handler;
    ctx->capture.entry.data_process_len = live_player_pcm_capture_process_len;

    return 0;
}

static void live_player_pcm_capture_close(struct live_stream_player_context *ctx)
{
    os_mutex_pend(&ctx->capture.mutex, 0);
    audio_stream_del_entry(&ctx->capture.entry);
    os_mutex_post(&ctx->capture.mutex);
}

//mic 音效处理节点
static int live_player_mic_effect_output_handler(void *priv, void *data, int len)
{
    struct live_player_mic_effect *mic_effect = (struct live_player_mic_effect *)priv;

    struct audio_data_frame frame = {
        .data = data,
        .data_len = len,
        .channel = mic_effect->effect->param.ch_num,
        .sample_rate = mic_effect->effect->param.sample_rate,
    };

    audio_stream_run(&mic_effect->entry, &frame);

    return mic_effect->used_len;

}

static void live_mic_effect_data_process_len(struct audio_stream_entry *entry, int len)
{
    struct live_player_mic_effect *mic_effect = container_of(entry, struct live_player_mic_effect, entry);

    mic_effect->used_len = len;
}

static int live_mic_effect_data_handler(struct audio_stream_entry *entry,  struct audio_data_frame *in, struct audio_data_frame *out)
{
    struct live_player_mic_effect *mic_effect = container_of(entry, struct live_player_mic_effect, entry);

    if (in->data_len) {
        out->no_subsequent = 1;  //数据流节点自己调用audio_stream_run,需要置1，不跑数据流节点的递归调用;
    }
    if (mic_effect->effect) {
        int wlen = live_mic_effect_input(mic_effect->effect, in->data, in->data_len, 0);
        if (wlen < in->data_len) {
            printf("mic_effect write err \n");
        }
        return wlen;
    }

    return in->data_len;
}
static void live_player_mic_effect_open(struct live_stream_player_context *ctx)
{

    struct live_mic_effect_param  param = {
        .effect_config = LIVE_MIC_EFFECT_CONFIG,
        .sample_rate = ctx->effect.sample_rate,
        .ch_num = ctx->effect.nch,
        .output_priv = &ctx->mic_effect,
        .output = live_player_mic_effect_output_handler,
    };

    ctx->mic_effect.effect = live_mic_effect_init(&param);

    ctx->mic_effect.entry.data_handler = live_mic_effect_data_handler;
    ctx->mic_effect.entry.data_process_len = live_mic_effect_data_process_len;

}

static void live_player_mic_effect_close(struct live_stream_player_context *ctx)
{
    if (ctx->mic_effect.effect) {
        live_mic_effect_close(ctx->mic_effect.effect);
    }
}

//music 音效处理
static void live_player_music_effect_open(struct live_stream_player_context *ctx)
{
    ctx->music_effect.bass_sel = 0;

#if defined(WIRELESS_SOUND_TRACK_2_P_X_ENABLE)&&WIRELESS_SOUND_TRACK_2_P_X_ENABLE
#if (TWO_POINT_X_CH == WIRELESS_SCENE_ONE)
    if (get_broadcast_role() == BROADCAST_ROLE_RECEIVER) {	//接收端,做低音
        ctx->music_effect.bass_sel = 1;
    }
#elif (TWO_POINT_X_CH == WIRELESS_SCENE_TWO)
    if (get_broadcast_role() == BROADCAST_ROLE_TRANSMITTER) {	//发射端,做低音
        ctx->music_effect.bass_sel = 1;
    }
#endif
#endif/*WIRELESS_SOUND_TRACK_2_P_X_ENABLE*/

    log_info("---------bass_sel %d\n", ctx->music_effect.bass_sel);
    if (ctx->music_effect.bass_sel) {	//低音通路
#if TCFG_EQ_DIVIDE_ENABLE&& WIRELESS_SOUND_TRACK_2_P_X_ENABLE
#if TCFG_EQ_ENABLE && TCFG_BROADCAST_MODE_EQ_ENABLE
        ctx->music_effect.eq_rl_rr = music_eq_rl_rr_open(ctx->effect.sample_rate, ctx->effect.nch);// eq
#if TCFG_DRC_ENABLE && TCFG_BROADCAST_MODE_DRC_ENABLE
        ctx->music_effect.drc_rl_rr = music_drc_rl_rr_open(ctx->effect.sample_rate, ctx->effect.nch);//drc
#endif
        if (!global_bit_wide) {
            if (ctx->music_effect.eq_rl_rr && ctx->music_effect.eq_rl_rr->out_32bit) {
                ctx->music_effect.convert_rl_rr = convet_data_open(0, 512);
            }
        }
#endif
        ctx->music_effect.gain_rl_rr = audio_gain_open_demo(AEID_MUSIC_RL_GAIN, ctx->effect.nch);
#if defined(MUSIC_EXT_EQ2_AFTER_DRC) && MUSIC_EXT_EQ2_AFTER_DRC
        ctx->music_effect.ext_eq2 = music_ext_eq2_open(ctx->effect.sample_rate, ctx->effect.nch);
#endif
#endif
    } else {

#if AUDIO_SURROUND_CONFIG
        //环绕音效
        u8 ch_type = 0;
        ch_type = ctx->dec_out_ch_mode;
        if ((ctx->effect.nch == 1) && ctx->dec_out_ch_mode == AUDIO_CH_LR) {//此情况时左箱体与右箱体无法确认
            ch_type = 0xff;//not support surround effect
        }
        ctx->music_effect.surround = surround_open_demo(AEID_MUSIC_SURROUND, ch_type);
#endif

#if SOUND_TRACK_1_P_1_CH_CONFIG && (EFFECT_PROCESS_POITION_CONFIG == EFFECT_AFTER_STEREO_MIX)
        ctx->music_effect.music_1p1_gain = audio_gain_open_demo(AEID_MUSIC_1P1_GAIN, ctx->effect.nch);
#endif
#if AUDIO_VOCAL_REMOVE_EN && LIVE_PLAYER_VOCAL_REMOVE_EN
        ctx->music_effect.vocal_hdl = vocal_remove_open(ctx->effect.nch, ctx->effect.sample_rate, 100, 200, 13000);
        set_vocal_remove_hdl(ctx->music_effect.vocal_hdl);
#endif

#if AUDIO_VBASS_CONFIG
        ctx->music_effect.vbass_prev_gain = audio_gain_open_demo(AEID_MUSIC_VBASS_PREV_GAIN, ctx->effect.nch);
        ctx->music_effect.ns_gate = audio_noisegate_open_demo(AEID_MUSIC_NS_GATE, ctx->effect.sample_rate, ctx->effect.nch);
        //虚拟低音
        ctx->music_effect.vbass = audio_vbass_open_demo(AEID_MUSIC_VBASS, ctx->effect.sample_rate, ctx->effect.nch);
#endif

#if defined(TCFG_MUSIC_HARMONIC_EXCITER_ENABLE) &&TCFG_MUSIC_HARMONIC_EXCITER_ENABLE
        ctx->music_effect.harmonic_exciter = audio_harmonic_exciter_open_api(AEID_MUSIC_HARMONIC_EXCITER, ctx->effect.sample_rate, ctx->effect.nch);
#endif

#if TCFG_EQ_ENABLE && TCFG_AUDIO_OUT_EQ_ENABLE
        ctx->music_effect.high_bass = high_bass_eq_open(ctx->effect.sample_rate, ctx->effect.nch);
        ctx->music_effect.hb_drc = high_bass_drc_open(ctx->effect.sample_rate, ctx->effect.nch);
        if (!global_bit_wide) {
            if (ctx->music_effect.hb_drc && ctx->music_effect.hb_drc->run32bit) {
#if defined(TCFG_DRC_ENABLE) && TCFG_DRC_ENABLE
                ctx->music_effect.hb_convert = convet_data_open(0, 512);
#endif
            }
        }
#endif/*TCFG_EQ_ENABLE && TCFG_AUDIO_OUT_EQ_ENABLE */

#if TCFG_EQ_ENABLE && TCFG_BROADCAST_MODE_EQ_ENABLE
        ctx->music_effect.eq = live_music_eq_open(ctx->effect.sample_rate, ctx->effect.nch, LIVE_STREAM_LOW_LATENCY);// eq
#if TCFG_DRC_ENABLE && TCFG_BROADCAST_MODE_DRC_ENABLE
        ctx->music_effect.drc = music_drc_open(ctx->effect.sample_rate, ctx->effect.nch);//drc
#if (defined(TCFG_DRC_SPILT_ENABLE) && TCFG_DRC_SPILT_ENABLE)
        ctx->music_effect.drc_fr = music_drc_fr_open(ctx->effect.sample_rate, ctx->effect.nch);//drc
#endif/*TCFG_BROADCAST_MODE_DRC_ENABLE*/
#endif/*TCFG_BROADCAST_MODE_DRC_ENABLE*/

        if (!global_bit_wide) {
#if !AUDIO_VBASS_32BIT_OUT_EN
#if defined(TCFG_DRC_ENABLE) && TCFG_DRC_ENABLE
            if (ctx->music_effect.eq && ctx->music_effect.eq->out_32bit) {
                ctx->music_effect.convert = convet_data_open(0, 512);
            }
#endif
#endif/* TCFG_DYNAMIC_EQ_ENABLE */
        }

#if defined(MUSIC_EXT_EQ_AFTER_DRC) && MUSIC_EXT_EQ_AFTER_DRC
        ctx->music_effect.ext_eq = music_ext_eq_open(ctx->effect.sample_rate, ctx->effect.nch);
#endif/*MUSIC_EXT_EQ_AFTER_DRC*/

#if defined(TCFG_DYNAMIC_EQ_ENABLE) && TCFG_DYNAMIC_EQ_ENABLE
        ctx->music_effect.eq2 = music_eq2_open(ctx->effect.sample_rate, ctx->effect.nch);// eq
#if defined(TCFG_DYNAMIC_EQ_PRO_ENABLE) && !TCFG_DYNAMIC_EQ_PRO_ENABLE
        ctx->music_effect.dy_eq = audio_dynamic_eq_ctrl_open(AEID_MUSIC_DYNAMIC_EQ, ctx->effect.sample_rate, ctx->effect.nch);//动态eq
#else
        ctx->music_effect.dy_eq_pro = audio_dynamic_eq_pro_open_api(AEID_MUSIC_DYNAMIC_EQ, ctx->effect.sample_rate, ctx->effect.nch);//动态eq
#endif
#if defined(TCFG_BROADCAST_MODE_LAST_DRC_ENABLE)&&TCFG_BROADCAST_MODE_LAST_DRC_ENABLE
        ctx->music_effect.last_drc = music_last_drc_open(ctx->effect.sample_rate, ctx->effect.nch);//广播音箱默认关闭dynamic_eq后的drc处理
#endif
        if (!global_bit_wide) {
#if !AUDIO_VBASS_32BIT_OUT_EN
            ctx->music_effect.convert2 = convet_data_open(0, 512);
#endif
        }
#endif/*TCFG_DYNAMIC_EQ_ENABLE*/

#if defined(TCFG_PHASER_GAIN_AND_CH_SWAP_ENABLE) && TCFG_PHASER_GAIN_AND_CH_SWAP_ENABLE
        ctx->music_effect.gain = audio_gain_open_demo(AEID_MUSIC_GAIN, ctx->effect.nch);
#endif

#endif/*TCFG_BROADCAST_MODE_EQ_ENABLE*/
        if (!global_bit_wide) {
#if defined(AUDIO_VBASS_32BIT_OUT_EN)&&AUDIO_VBASS_32BIT_OUT_EN
            ctx->music_effect.convert_32_to_16 = convet_data_open(0, 512);
#endif
        }

#if SOUND_TRACK_1_P_1_CH_CONFIG && (EFFECT_PROCESS_POITION_CONFIG == EFFECT_BEFORE_STEREO_MIX)
#if TCFG_EQ_ENABLE && TCFG_BROADCAST_MODE_EQ_ENABLE
        ctx->music_effect.eq_rl_rr = music_eq_rl_rr_open(ctx->effect.sample_rate, ctx->effect.nch);// eq
#if TCFG_DRC_ENABLE && TCFG_BROADCAST_MODE_DRC_ENABLE
        ctx->music_effect.drc_rl_rr = music_drc_rl_rr_open(ctx->effect.sample_rate, ctx->effect.nch);//drc
#if (defined(TCFG_DRC_SPILT_ENABLE) && TCFG_DRC_SPILT_ENABLE)
        ctx->music_effect.drc_rr = music_drc_rr_open(ctx->effect.sample_rate, ctx->effect.nch);//drc
#endif
#endif
        if (!global_bit_wide) {
            if ((ctx->music_effect.eq_rl_rr && ctx->music_effect.eq_rl_rr->out_32bit) || AUDIO_VBASS_32BIT_OUT_EN) {
                ctx->music_effect.convert_rl_rr = convet_data_open(0, 512);
            }
        }
#endif
#if defined(TCFG_PHASER_GAIN_AND_CH_SWAP_ENABLE) && TCFG_PHASER_GAIN_AND_CH_SWAP_ENABLE
        ctx->music_effect.gain_rl_rr = audio_gain_open_demo(AEID_MUSIC_RL_GAIN, ctx->effect.nch);
#endif
        ctx->music_effect.fl_fr_ch_2to1 = channel_switch_open(AUDIO_CH_DIFF, AUDIO_SYNTHESIS_LEN);
        channel_switch_set_bit_wide(ctx->music_effect.fl_fr_ch_2to1, global_bit_wide);
        ctx->music_effect.rl_rr_ch_2to1 = channel_switch_open(AUDIO_CH_DIFF, AUDIO_SYNTHESIS_LEN);
        channel_switch_set_bit_wide(ctx->music_effect.rl_rr_ch_2to1, global_bit_wide);

        audio_vocal_tract_open(&ctx->music_effect.vocal_tract, AUDIO_SYNTHESIS_LEN);
        struct vocal_track_parm par = {0};
        par.ch_num = 2;
        par.bit_width = global_bit_wide;
        par.len = AUDIO_SYNTHESIS_LEN;
        audio_vocal_tract_set_info(&ctx->music_effect.vocal_tract, par);

        u8 entry_cnt = 0;
        struct audio_stream_entry *entries[8] = {NULL};
        entries[entry_cnt++] = &ctx->music_effect.vocal_tract.entry;

#if TCFG_AUDIO_EFFECT_TASK_ENABLE
        ctx->music_effect.effect_task_entry = audio_effect_task_open();
        if (ctx->music_effect.effect_task_entry) {
            entries[entry_cnt++] = audio_effect_task_get_output_entry(ctx->music_effect.effect_task_entry);
        }
#endif
        entries[entry_cnt++] = &ctx->mix_ch.entry;

        ctx->music_effect.vocal_tract.stream = audio_stream_open(&ctx->music_effect.vocal_tract, audio_vocal_tract_stream_resume);
        audio_stream_add_list(ctx->music_effect.vocal_tract.stream, entries, entry_cnt);
        audio_vocal_tract_synthesis_open(&ctx->music_effect.synthesis_ch_fl_fr, &ctx->music_effect.vocal_tract, FL_FR);
        audio_vocal_tract_synthesis_open(&ctx->music_effect.synthesis_ch_rl_rr, &ctx->music_effect.vocal_tract, RL_RR);
#if defined(MUSIC_EXT_EQ2_AFTER_DRC) && MUSIC_EXT_EQ2_AFTER_DRC
        ctx->music_effect.ext_eq2 = music_ext_eq2_open(ctx->effect.sample_rate, ctx->effect.nch);
#endif
#ifdef CONFIG_MIXER_CYCLIC
        audio_mixer_ch_set_aud_ch_out(&ctx->mix_ch, 0, BIT(0));
        audio_mixer_ch_set_aud_ch_out(&ctx->mix_ch, 1, BIT(1));
#endif
#endif
    }

}

static int live_player_music_effect_stream_entry_add(struct live_stream_player_context *ctx, struct audio_stream_entry **entries, int max)
{
    int entry_cnt = 0;

    if (ctx->music_effect.bass_sel) {	//接收端
#if TCFG_EQ_DIVIDE_ENABLE&& WIRELESS_SOUND_TRACK_2_P_X_ENABLE
        if (ctx->music_effect.eq_rl_rr) {
            if (ctx->music_effect.gain_rl_rr) {
                entries[entry_cnt++] = &ctx->music_effect.gain_rl_rr->entry;
            }
            entries[entry_cnt++] = &ctx->music_effect.eq_rl_rr->entry;
            if (ctx->music_effect.drc_rl_rr) {
                entries[entry_cnt++] = &ctx->music_effect.drc_rl_rr->entry;
            }
            if (ctx->music_effect.convert_rl_rr) {
                entries[entry_cnt++] = &ctx->music_effect.convert_rl_rr->entry;
            }

#if defined(MUSIC_EXT_EQ2_AFTER_DRC) && MUSIC_EXT_EQ2_AFTER_DRC
            if (ctx->music_effect.ext_eq2) {
                entries[entry_cnt++] = &ctx->music_effect.ext_eq2->entry;
            }
#endif
        }
#endif
    } else {

#if AUDIO_VOCAL_REMOVE_EN && LIVE_PLAYER_VOCAL_REMOVE_EN
        if (ctx->music_effect.vocal_hdl) {
            entries[entry_cnt++] = &ctx->music_effect.vocal_hdl->entry;
        }
#endif

#if !(SOUND_TRACK_1_P_1_CH_CONFIG && (EFFECT_PROCESS_POITION_CONFIG == EFFECT_BEFORE_STEREO_MIX))
#if AUDIO_VBASS_CONFIG
        if (ctx->music_effect.vbass_prev_gain) {
            entries[entry_cnt++] = &ctx->music_effect.vbass_prev_gain->entry;
        }
        if (ctx->music_effect.ns_gate) {
            entries[entry_cnt++] = &ctx->music_effect.ns_gate->entry;
        }
        if (ctx->music_effect.vbass) {
            entries[entry_cnt++] = &ctx->music_effect.vbass->entry;
        }
#endif
#endif


#if defined(TCFG_MUSIC_HARMONIC_EXCITER_ENABLE) &&TCFG_MUSIC_HARMONIC_EXCITER_ENABLE
        if (ctx->music_effect.harmonic_exciter) {
            entries[entry_cnt++] = &ctx->music_effect.harmonic_exciter->entry;
        }
#endif

#if !(SOUND_TRACK_1_P_1_CH_CONFIG && (EFFECT_PROCESS_POITION_CONFIG == EFFECT_BEFORE_STEREO_MIX))
#if defined(TCFG_DYNAMIC_EQ_ENABLE) && TCFG_DYNAMIC_EQ_ENABLE
        if (ctx->music_effect.dy_eq && ctx->music_effect.dy_eq->dy_eq) {
            entries[entry_cnt++] = &ctx->music_effect.dy_eq->dy_eq->entry;
        } else if (ctx->music_effect.dy_eq_pro && ctx->music_effect.dy_eq_pro) {
            entries[entry_cnt++] = &ctx->music_effect.dy_eq_pro->entry;
        }
#endif
#endif

#if TCFG_EQ_ENABLE && TCFG_AUDIO_OUT_EQ_ENABLE
        if (ctx->music_effect.high_bass) { //高低音
            entries[entry_cnt++] = &ctx->music_effect.high_bass->entry;
        }
        if (ctx->music_effect.hb_drc) { //高低音后drc
            entries[entry_cnt++] = &ctx->music_effect.hb_drc->entry;
#if defined(TCFG_DRC_ENABLE) && TCFG_DRC_ENABLE
            if (ctx->music_effect.hb_convert) {
                entries[entry_cnt++] = &ctx->music_effect.hb_convert->entry;
            }
#endif
        }
#endif/* TCFG_EQ_ENABLE && TCFG_AUDIO_OUT_EQ_ENABLE */

#if defined(TCFG_PHASER_GAIN_AND_CH_SWAP_ENABLE) && TCFG_PHASER_GAIN_AND_CH_SWAP_ENABLE
        if (ctx->music_effect.gain) {
            entries[entry_cnt++] = &ctx->music_effect.gain->entry;
        }
#endif

#if AUDIO_SURROUND_CONFIG
        if (ctx->music_effect.surround) {
            entries[entry_cnt++] = &ctx->music_effect.surround->entry;
        }
#endif

#if TCFG_EQ_ENABLE && TCFG_BROADCAST_MODE_EQ_ENABLE
        if (ctx->music_effect.eq) {
            entries[entry_cnt++] = &ctx->music_effect.eq->entry;
#if TCFG_DRC_ENABLE && TCFG_BROADCAST_MODE_DRC_ENABLE
            if (ctx->music_effect.drc) {
                entries[entry_cnt++] = &ctx->music_effect.drc->entry;
            }
#if (defined(TCFG_DRC_SPILT_ENABLE) && TCFG_DRC_SPILT_ENABLE)
            if (ctx->music_effect.drc_fr) {
                entries[entry_cnt++] = &ctx->music_effect.drc_fr->entry;
            }
#endif
#endif/*TCFG_BROADCAST_MODE_DRC_ENABLE*/
#if defined(TCFG_DRC_ENABLE) && TCFG_DRC_ENABLE
            if (ctx->music_effect.convert) {
                entries[entry_cnt++] = &ctx->music_effect.convert->entry;
            }
#endif
#if defined(MUSIC_EXT_EQ_AFTER_DRC) && MUSIC_EXT_EQ_AFTER_DRC
            if (ctx->music_effect.ext_eq) {
                entries[entry_cnt++] = &ctx->music_effect.ext_eq->entry;
            }
#endif
#if defined(TCFG_DYNAMIC_EQ_ENABLE) && TCFG_DYNAMIC_EQ_ENABLE
#if !(SOUND_TRACK_1_P_1_CH_CONFIG && (EFFECT_PROCESS_POITION_CONFIG == EFFECT_BEFORE_STEREO_MIX))
            if (ctx->music_effect.eq2) {
                entries[entry_cnt++] = &ctx->music_effect.eq2->entry;
            }
#endif
#if defined(TCFG_BROADCAST_MODE_LAST_DRC_ENABLE)&&TCFG_BROADCAST_MODE_LAST_DRC_ENABLE
            if (ctx->music_effect.last_drc) {
                entries[entry_cnt++] = &ctx->music_effect.last_drc->entry;
            }
#endif
            if (ctx->music_effect.convert2) {
                entries[entry_cnt++] = &ctx->music_effect.convert2->entry;
            }
#endif
        }
#endif/* TCFG_EQ_ENABLE && TCFG_BROADCAST_MODE_EQ_ENABLE */

        if (ctx->music_effect.convert_32_to_16) {
            entries[entry_cnt++] = &ctx->music_effect.convert_32_to_16->entry;
        }

#if SOUND_TRACK_1_P_1_CH_CONFIG && (EFFECT_PROCESS_POITION_CONFIG == EFFECT_BEFORE_STEREO_MIX)
        if (ctx->music_effect.fl_fr_ch_2to1) {
            entries[entry_cnt++] = &ctx->music_effect.fl_fr_ch_2to1->entry;
        }
        entries[entry_cnt++] = &ctx->music_effect.synthesis_ch_fl_fr.entry;//四声道eq独立时，该节点后不接节点
#endif
    }


    return entry_cnt;
}

static int live_player_music_effect_stream_rl_rr_entry_add(struct live_stream_player_context *ctx, struct audio_stream_entry **rl_rr_entries, int max)
{
    int entry_cnt = 0;
#if SOUND_TRACK_1_P_1_CH_CONFIG && (EFFECT_PROCESS_POITION_CONFIG == EFFECT_BEFORE_STEREO_MIX)
#if defined(TCFG_PHASER_GAIN_AND_CH_SWAP_ENABLE) && TCFG_PHASER_GAIN_AND_CH_SWAP_ENABLE
    if (ctx->music_effect.gain_rl_rr) {
        rl_rr_entries[entry_cnt++] = &ctx->music_effect.gain_rl_rr->entry;
    }
#endif

#if AUDIO_VBASS_CONFIG
    if (ctx->music_effect.vbass) {
        rl_rr_entries[entry_cnt++] = &ctx->music_effect.vbass->entry;
    }
#endif
#if defined(TCFG_DYNAMIC_EQ_ENABLE) && TCFG_DYNAMIC_EQ_ENABLE
    if (ctx->music_effect.dy_eq) {
        rl_rr_entries[entry_cnt++] = &ctx->music_effect.dy_eq->dy_eq->entry;
    } else if (ctx->music_effect.dy_eq_pro) {
        rl_rr_entries[entry_cnt++] = &ctx->music_effect.dy_eq_pro->entry;
    }
#endif
#if TCFG_EQ_DIVIDE_ENABLE
    if (ctx->music_effect.eq_rl_rr) {
        rl_rr_entries[entry_cnt++] = &ctx->music_effect.eq_rl_rr->entry;
    }
    if (ctx->music_effect.drc_rl_rr) {
        rl_rr_entries[entry_cnt++] = &ctx->music_effect.drc_rl_rr->entry;
    }
    if (ctx->music_effect.convert_rl_rr) {
        rl_rr_entries[entry_cnt++] = &ctx->music_effect.convert_rl_rr->entry;
    }
    if (ctx->music_effect.ext_eq2) {
        rl_rr_entries[entry_cnt++] = &ctx->music_effect.ext_eq2->entry;
    }
#endif

    if (ctx->music_effect.rl_rr_ch_2to1) {
        rl_rr_entries[entry_cnt++] = &ctx->music_effect.rl_rr_ch_2to1->entry;
    }
    rl_rr_entries[entry_cnt++] = &ctx->music_effect.synthesis_ch_rl_rr.entry;//必须是最后一个
#endif
    return entry_cnt;
}

static void live_player_music_effect_close(struct live_stream_player_context *ctx)
{
#if TCFG_AUDIO_EFFECT_TASK_ENABLE
    if (ctx->music_effect.effect_task_entry) {
        audio_effect_task_close(ctx->music_effect.effect_task_entry);
    }
#endif
#if SOUND_TRACK_1_P_1_CH_CONFIG && (EFFECT_PROCESS_POITION_CONFIG == EFFECT_AFTER_STEREO_MIX)
    audio_gain_close_demo(ctx->music_effect.music_1p1_gain);
#endif
    convet_data_close(ctx->music_effect.convert_32_to_16);
#if defined(TCFG_MUSIC_HARMONIC_EXCITER_ENABLE) &&TCFG_MUSIC_HARMONIC_EXCITER_ENABLE
    audio_harmonic_exciter_close_api(ctx->music_effect.harmonic_exciter);
#endif

#if AUDIO_VBASS_CONFIG
    audio_gain_close_demo(ctx->music_effect.vbass_prev_gain);
    audio_noisegate_close_demo(ctx->music_effect.ns_gate);
    audio_vbass_close_demo(ctx->music_effect.vbass);
#endif

#if TCFG_EQ_ENABLE && TCFG_AUDIO_OUT_EQ_ENABLE
    high_bass_eq_close(ctx->music_effect.high_bass);
    high_bass_drc_close(ctx->music_effect.hb_drc);
#if defined(TCFG_DRC_ENABLE) && TCFG_DRC_ENABLE
    convet_data_close(ctx->music_effect.hb_convert);
#endif
#endif/*TCFG_EQ_ENABLE && TCFG_AUDIO_OUT_EQ_ENABLE*/

#if AUDIO_SURROUND_CONFIG
    surround_close_demo(ctx->music_effect.surround);
#endif

#if AUDIO_VOCAL_REMOVE_EN && LIVE_PLAYER_VOCAL_REMOVE_EN
    set_vocal_remove_hdl(0);
    audio_vocal_remove_close(ctx->music_effect.vocal_hdl);
#endif

#if TCFG_EQ_ENABLE && TCFG_BROADCAST_MODE_EQ_ENABLE
    music_eq_close(ctx->music_effect.eq);
#if TCFG_DRC_ENABLE && TCFG_BROADCAST_MODE_DRC_ENABLE
    music_drc_close(ctx->music_effect.drc);
#if (defined(TCFG_DRC_SPILT_ENABLE) && TCFG_DRC_SPILT_ENABLE)
    music_drc_fr_close(ctx->music_effect.drc_fr);
#endif
#endif/*TCFG_BROADCAST_MODE_DRC_ENABLE*/
#if defined(TCFG_DRC_ENABLE) && TCFG_DRC_ENABLE
    convet_data_close(ctx->music_effect.convert);
#endif

#if defined(MUSIC_EXT_EQ_AFTER_DRC) && MUSIC_EXT_EQ_AFTER_DRC
    music_ext_eq_close(ctx->music_effect.ext_eq);
#endif
#if defined(TCFG_DYNAMIC_EQ_ENABLE) && TCFG_DYNAMIC_EQ_ENABLE
    music_eq2_close(ctx->music_effect.eq2);
#if defined(TCFG_DYNAMIC_EQ_PRO_ENABLE) && !TCFG_DYNAMIC_EQ_PRO_ENABLE
    audio_dynamic_eq_ctrl_close(ctx->music_effect.dy_eq);
#else
    audio_dynamic_eq_pro_close_api(ctx->music_effect.dy_eq_pro);
#endif
#if defined(TCFG_BROADCAST_MODE_LAST_DRC_ENABLE)&&TCFG_BROADCAST_MODE_LAST_DRC_ENABLE
    music_last_drc_close(ctx->music_effect.last_drc);
#endif
    convet_data_close(ctx->music_effect.convert2);
#endif/*TCFG_DYNAMIC_EQ_ENABLE*/
#if defined(TCFG_PHASER_GAIN_AND_CH_SWAP_ENABLE) && TCFG_PHASER_GAIN_AND_CH_SWAP_ENABLE
    audio_gain_close_demo(ctx->music_effect.gain);
#endif
#endif /*TCFG_BROADCAST_MODE_EQ_ENABLE*/
#if TCFG_EQ_DIVIDE_ENABLE
    music_eq_rl_rr_close(ctx->music_effect.eq_rl_rr);
    music_drc_rl_rr_close(ctx->music_effect.drc_rl_rr);
#if (defined(TCFG_DRC_SPILT_ENABLE) && TCFG_DRC_SPILT_ENABLE)
    music_drc_rr_close(ctx->music_effect.drc_rr);
#endif
    convet_data_close(ctx->music_effect.convert_rl_rr);
    audio_gain_close_demo(ctx->music_effect.gain_rl_rr);

    audio_vocal_tract_synthesis_close(&ctx->music_effect.synthesis_ch_fl_fr);
    audio_vocal_tract_synthesis_close(&ctx->music_effect.synthesis_ch_rl_rr);
    audio_vocal_tract_close(&ctx->music_effect.vocal_tract);
#if defined(MUSIC_EXT_EQ2_AFTER_DRC) && MUSIC_EXT_EQ2_AFTER_DRC
    music_ext_eq2_close(ctx->music_effect.ext_eq2);
#endif
    if (ctx->music_effect.vocal_tract.stream) {
        audio_stream_close(ctx->music_effect.vocal_tract.stream);
        ctx->music_effect.vocal_tract.stream = NULL;
    }
#endif
#if SOUND_TRACK_1_P_1_CH_CONFIG && (EFFECT_PROCESS_POITION_CONFIG == EFFECT_BEFORE_STEREO_MIX)
    channel_switch_close(&ctx->music_effect.fl_fr_ch_2to1);
    channel_switch_close(&ctx->music_effect.rl_rr_ch_2to1);
#endif

}

#if ((defined MULTI_AUDIO_UPLOAD_TO_UAC_ENABLE) && MULTI_AUDIO_UPLOAD_TO_UAC_ENABLE)
static struct audio_stream_entry *live_player_to_uac_entry_open(struct live_stream_player_context *ctx)
{
    ctx->uac_mix_ch = mix_fifo_ch_open(usb_mix_fifo, ctx->dec_out_ch_num);
    if (ctx->uac_mix_ch) {
        return mix_fifo_ch_get_entry(ctx->uac_mix_ch);
    }
    return NULL;
}

static void live_player_to_uac_entry_close(struct live_stream_player_context *ctx)
{
    if (!ctx->uac_mix_ch) {
        return;
    }

    struct audio_stream_entry *entry = mix_fifo_ch_get_entry(ctx->uac_mix_ch);
    if (entry) {
        audio_stream_del_entry(entry);
    }
    mix_fifo_ch_close(ctx->uac_mix_ch);
    ctx->uac_mix_ch = NULL;
}
#endif

/*----------------------------------------------------------------------------*/
/**@brief    broadcast 解码开始
   @param    解码私有参数
   @return   0：成功
   @return   非0：失败
   @note
*/
/*----------------------------------------------------------------------------*/
static int live_stream_player_start(struct live_stream_player_context *ctx)
{
    int err = 0;
    struct audio_fmt f = {0};

    if (!ctx) {
        return -EINVAL;
    }
    printf("live_stream_player_start: in\n");

    memcpy(&ctx->dec_input, &live_stream_dec_input, sizeof(struct audio_dec_input));
    if (ctx->in_fmt.coding_type == AUDIO_CODING_JLA && JLA_CODEC_SOFT_DECISION_ENABLE) {
        ctx->dec_input.data_type = AUDIO_INPUT_FRAME;
        ctx->dec_input.ops.frame.fget = live_stream_get_frame;
        ctx->dec_input.ops.frame.fput = live_stream_free_frame;
    }
    ctx->dec_input.coding_type = ctx->in_fmt.coding_type;

    // 打开broadcast_dec解码
    err = audio_decoder_open(&ctx->decoder, &ctx->dec_input, LIVE_PLAY_DECODE_TASK);
    if (err) {
        goto __err1;
    }
    // 设置运行句柄
    audio_decoder_set_handler(&ctx->decoder, &live_stream_dec_handler);
    f.coding_type = ctx->in_fmt.coding_type;
    f.sample_rate = ctx->in_fmt.sample_rate;
    f.bit_rate = ctx->in_fmt.bit_rate;
    f.frame_len = ctx->in_fmt.frame_len;
    f.bit_width = ctx->in_fmt.bit_width;
    /* memcpy(&ctx->decoder.fmt,&f,sizeof(struct audio_fmt)); */
    ctx->decoder.fmt.sample_rate = ctx->in_fmt.sample_rate;
    ctx->decoder.fmt.channel = ctx->in_fmt.channel;
    ctx->decoder.fmt.bit_rate = ctx->in_fmt.bit_rate;
    ctx->decoder.fmt.frame_len = ctx->in_fmt.frame_len;
    ctx->decoder.fmt.bit_width = ctx->in_fmt.bit_width;
    if (ctx->in_fmt.channel == 2) {
        if (ctx->dec_out_ch_mode == AUDIO_CH_L || ctx->dec_out_ch_mode == AUDIO_CH_R) {
            ctx->decoder.fmt.channel = JLA_2CH_L_OR_R; //jla 解码只解一个声道;
        }
    } else if (ctx->in_fmt.channel == 1) {
        ctx->dec_out_ch_mode = AUDIO_CH_DIFF;
    }
    err = audio_decoder_set_fmt(&ctx->decoder, &f);
    if (err) {
        goto __err2;
    }
    // 使能事件回调
    audio_decoder_set_event_handler(&ctx->decoder, live_stream_dec_event_handler, 0);
    // 设置解码输出声道类型  会根据配置更新实际输出的声道数到格式里;
    audio_decoder_set_output_channel(&ctx->decoder, ctx->dec_out_ch_mode);
    //解码的源数据是单声道数据时，解码声道类型配置无效，输出单声道数据，
    ctx->dec_out_ch_num = audio_decoder_get_channel(&ctx->decoder);
    // 配置mixer通道参数
    audio_mixer_ch_open_head(&ctx->mix_ch, LIVE_PLAY_MIXER); // 挂载到mixer最前面
    audio_mixer_ch_set_sample_rate(&ctx->mix_ch, f.sample_rate);

#if (RECORDER_MIX_EN)
    ctx->rec_mix_ch = rec_mix_fifo_ch_open(ctx->decoder.fmt.channel);
#endif

    live_player_effect_open(ctx);  //同步节点
    live_player_pcm_capture_open(ctx);

#if (!WIRELESS_2T1_DUPLEX_EN)
    if (ctx->device == LIVE_SOUND_MIC) {
#if LIVE_PLAYER_MIC_EFFECT_ENABLE
        live_player_mic_effect_open(ctx); //mic音效处理节点
#endif
    } else {
        live_player_music_effect_open(ctx); //music 音效处理节点
    }
#endif

#ifdef CONFIG_MIXER_CYCLIC
    //解码按配置输出数据， 通过mixer做声道变换
    u8 ch_num = audio_output_channel_num(); //获取输出设备的声道数
    if (ctx->dec_out_ch_num != ch_num) {
        if (ctx->dec_out_ch_num == 1) {
            audio_mixer_ch_set_aud_ch_out(&ctx->mix_ch, 0, BIT(0) | BIT(1)); //单变双
        } else {
            audio_mixer_ch_set_aud_ch_out(&ctx->mix_ch, 0, BIT(0));       //双变单
            audio_mixer_ch_set_aud_ch_out(&ctx->mix_ch, 1, BIT(0));
        }
    }
#endif

    // 数据流串联
    struct audio_stream_entry *entries[32] = {NULL};
    u8 entry_cnt = 0;
    u8 rl_rr_entry_start = 0;
    entries[entry_cnt++] = &ctx->decoder.entry;
    // 添加自定义数据流节点等
    entries[entry_cnt++] = &ctx->capture.entry;
    entries[entry_cnt++] = &ctx->effect.entry;

    //1.1声道需要使用音效独立异步任务
#if SOUND_TRACK_1_P_1_CH_CONFIG && (EFFECT_PROCESS_POITION_CONFIG == EFFECT_BEFORE_STEREO_MIX)
#if TCFG_AUDIO_EFFECT_TASK_ENABLE
    if (ctx->music_effect.effect_task_entry) {
        entries[entry_cnt++] = ctx->music_effect.effect_task_entry;
    }
#endif
#endif

#if SYS_DIGVOL_GROUP_EN
    void *dvol_entry = sys_digvol_group_ch_open("music_live_player", -1, NULL);
    entries[entry_cnt++] = dvol_entry;
#endif // SYS_DIGVOL_GROUP_EN

#if (!WIRELESS_2T1_DUPLEX_EN)
    if (ctx->device == LIVE_SOUND_MIC) {
#if LIVE_PLAYER_MIC_EFFECT_ENABLE
        entries[entry_cnt++] = &ctx->mic_effect.entry;
#endif
    } else {
#if SOUND_TRACK_1_P_1_CH_CONFIG && (EFFECT_PROCESS_POITION_CONFIG == EFFECT_BEFORE_STEREO_MIX)
#if defined(TCFG_DYNAMIC_EQ_ENABLE) && TCFG_DYNAMIC_EQ_ENABLE
        //1.1声道，EQ2作为分流前的preEQ
        if (ctx->music_effect.eq2) {
            entries[entry_cnt++] = &ctx->music_effect.eq2->entry;
        }
        //记录分流节点
        rl_rr_entry_start = entry_cnt - 1;
#endif
#endif
        entry_cnt += live_player_music_effect_stream_entry_add(ctx, &entries[entry_cnt], ARRAY_SIZE(entries) - entry_cnt);
    }
#endif

    //若1.1声道使用该功能，需要将节点挪到music_effect_open中的mix_ch节点之前
#if ((defined MULTI_AUDIO_UPLOAD_TO_UAC_ENABLE) && MULTI_AUDIO_UPLOAD_TO_UAC_ENABLE)
    struct audio_stream_entry *upload_to_uac_entry = live_player_to_uac_entry_open(ctx);
    if (upload_to_uac_entry) {
        entries[entry_cnt++] = upload_to_uac_entry;
    }
#endif


    //若1.1声道使用该功能，需要将节点挪到music_effect_open中的mix_ch节点之前
#if (RECORDER_MIX_EN)
    entries[entry_cnt++] = rec_mix_fifo_ch_get_entry(ctx->rec_mix_ch);
#endif
    // 最后输出到mix数据流节点
#if !(SOUND_TRACK_1_P_1_CH_CONFIG && (EFFECT_PROCESS_POITION_CONFIG == EFFECT_BEFORE_STEREO_MIX))
    //1.1声道分流操作，不直接串mix_ch
    entries[entry_cnt++] = &ctx->mix_ch.entry;
#endif
    // 创建数据流，把所有节点连接起来
    ctx->stream = audio_stream_open(ctx, live_player_resume_from_stream);
    audio_stream_add_list(ctx->stream, entries, entry_cnt);

#if SOUND_TRACK_1_P_1_CH_CONFIG && (EFFECT_PROCESS_POITION_CONFIG == EFFECT_BEFORE_STEREO_MIX)
    if (ctx->device != LIVE_SOUND_MIC) {
        struct audio_stream_entry *rl_rr_entries[20] = {NULL};
        entry_cnt = 0;
        rl_rr_entries[entry_cnt++] = entries[rl_rr_entry_start];
        entry_cnt += live_player_music_effect_stream_rl_rr_entry_add(ctx, &rl_rr_entries[entry_cnt], ARRAY_SIZE(rl_rr_entries) - entry_cnt);
        // 创建数据流，把所有节点连接起来
        for (int i = 0; i < (entry_cnt - 1); i++) {
            audio_stream_add_entry(rl_rr_entries[i], rl_rr_entries[i + 1]);
        }
    }
#endif

    // 设置音频输出类型
    audio_output_set_start_volume(APP_AUDIO_STATE_MUSIC);

    clock_add_set(BROADCAST_DEC_CLK);

    ctx->state = LIVE_PLAYER_STATE_START;
    live_stream_stop_drain(ctx);
    err = audio_decoder_start(&ctx->decoder);
    printf("live_stream_player_start succ\n");
    if (err) {
        goto __err3;
    }
    live_player_num++;

    return 0;
__err3:
    ctx->state = LIVE_PLAYER_STATE_CLOSE;

#if (!WIRELESS_2T1_DUPLEX_EN)
    if (ctx->device == LIVE_SOUND_MIC) {
#if LIVE_PLAYER_MIC_EFFECT_ENABLE
        live_player_mic_effect_close(ctx);
#endif
    } else {
        live_player_music_effect_close(ctx);
    }
#endif
    live_player_effect_close(ctx);
    audio_mixer_ch_close(&ctx->mix_ch);

    // 先关闭各个节点，最后才close数据流
    if (ctx->stream) {
        audio_stream_close(ctx->stream);
        ctx->stream = NULL;
    }
__err2:
    audio_decoder_close(&ctx->decoder);
__err1:
    return err;
}

/*----------------------------------------------------------------------------*/
/**@brief    broadcast解码停止
   @param    私有参数
   @return   0：成功
   @return   非0：失败
   @note
*/
/*----------------------------------------------------------------------------*/

static int live_stream_player_stop(struct live_stream_player_context *ctx)
{
    LIVE_PLAYER_ENTER_CRITICAL();
    ctx->state = LIVE_PLAYER_STATE_STOP;
    LIVE_PLAYER_EXIT_CRITICAL();

    audio_decoder_close(&ctx->decoder);

    if (ctx->frame) {
        ctx->free_frame(ctx->ipath, ctx->frame);
        ctx->frame = NULL;
    }

#if SYS_DIGVOL_GROUP_EN
    sys_digvol_group_ch_close("music_live_player");
#endif // SYS_DIGVOL_GROUP_EN

#if (!WIRELESS_2T1_DUPLEX_EN)
    if (ctx->device == LIVE_SOUND_MIC) {
#if LIVE_PLAYER_MIC_EFFECT_ENABLE
        live_player_mic_effect_close(ctx);
#endif
    } else {
        live_player_music_effect_close(ctx);
    }
#endif

    audio_mixer_ch_close(&ctx->mix_ch);

#if (RECORDER_MIX_EN)
    if (ctx->rec_mix_ch != NULL) {
        void *entry = rec_mix_fifo_ch_get_entry(ctx->rec_mix_ch);
        if (entry != NULL) {
            audio_stream_del_entry(entry);
        }
    }
    rec_mix_fifo_ch_close(ctx->rec_mix_ch);
    ctx->rec_mix_ch = NULL;
#endif

#if ((defined MULTI_AUDIO_UPLOAD_TO_UAC_ENABLE) && MULTI_AUDIO_UPLOAD_TO_UAC_ENABLE)
    live_player_to_uac_entry_close(ctx);
#endif
    live_player_pcm_capture_close(ctx);
    live_player_effect_close(ctx);
    // 先关闭各个节点，最后才close数据流
    if (ctx->stream) {
        audio_stream_close(ctx->stream);
        ctx->stream = NULL;
    }

    clock_remove_set(BROADCAST_DEC_CLK);

    app_audio_state_exit(APP_AUDIO_STATE_MUSIC);

    live_player_num--;
    return 0;
}


/*----------------------------------------------------------------------------*/
/**@brief    解码释放
   @param    解码私有参数
   @return
   @note
*/
/*----------------------------------------------------------------------------*/
static void live_stream_player_free(struct live_stream_player_context *ctx)
{
    // 解码释放
    audio_decoder_task_del_wait(LIVE_PLAY_DECODE_TASK, &ctx->res_wait);

    local_irq_disable();
    ctx->state = LIVE_PLAYER_STATE_CLOSE;
    free(ctx);
    ctx = NULL;
    local_irq_enable();
}

/*----------------------------------------------------------------------------*/
/**@brief    解码打断的数据处理
   @param    解码私有参数
   @return
   @note
*/
/*----------------------------------------------------------------------------*/
static void __live_stream_drain(void *priv)
{
    struct live_stream_player_context *ctx = (struct live_stream_player_context *)priv;
    int rlen;
    struct audio_frame *frame;
    if (ctx->state == LIVE_PLAYER_STATE_START) {
        return;
    }

    while (1) {
        frame = ctx->get_frame(ctx->ipath);
        if (!frame) {
            break;
        }
        ctx->free_frame(ctx->ipath, frame);
    }
}

static void live_stream_auto_drain(struct live_stream_player_context *ctx)
{
    LIVE_PLAYER_ENTER_CRITICAL();
    if (!ctx->timer) {
        ctx->timer = sys_timer_add(ctx, __live_stream_drain, 30);
    }
    LIVE_PLAYER_EXIT_CRITICAL();
}

static void live_stream_stop_drain(struct live_stream_player_context *ctx)
{
    LIVE_PLAYER_ENTER_CRITICAL();
    if (ctx->timer) {
        sys_timer_del(ctx->timer);
        ctx->timer = 0;
    }
    LIVE_PLAYER_EXIT_CRITICAL();
}

/*----------------------------------------------------------------------------*/
/**@brief    broadcast 解码资源等待
   @param    *wait: 句柄
   @param    event: 事件
   @return   0：成功
   @note     用于多解码打断处理。被打断时关闭解码
*/
/*----------------------------------------------------------------------------*/
static int live_player_wait_res_handler(struct audio_res_wait *wait, int event)
{
    struct live_stream_player_context *ctx = container_of(wait, struct live_stream_player_context, res_wait);
    int err = 0;

    log_debug("live player res handler : %d", event);

    if (event == AUDIO_RES_GET) {
        if (ctx->state == LIVE_PLAYER_STATE_START) {
            return 0;
        }
        err = live_stream_player_start(ctx);
        if (err) {
            live_stream_player_free(ctx);
        }
    } else if (event == AUDIO_RES_PUT) {
        if (ctx->state == LIVE_PLAYER_STATE_START) {
            live_stream_auto_drain(ctx);
            live_stream_player_stop(ctx);
        }
    }

    return err;
}

/*----------------------------------------------------------------------------*/
/**@brief    打开 broadcast 解码
   @param    解码参数
   @return   0：成功
   @return   非0：失败
   @note
*/
/*----------------------------------------------------------------------------*/
void *live_stream_player_open(struct audio_path *ipath, struct audio_path *opath)
{
    struct live_stream_player_context *ctx;

    if (strcmp(os_current_task(), "app_core") != 0) {
        log_error("live palyer open in task : %s\n", os_current_task());
    }

    printf("live player dec open !!\n");
    ctx = zalloc(sizeof(*ctx));
    if (!ctx) {
        return NULL;
    }
    LIVE_PLAYER_CRITICAL_INIT();

    ctx->device = ipath->device;
    ctx->network = ipath->network;
    ctx->state = LIVE_PLAYER_STATE_OPEN;
    ctx->in_fmt.coding_type = ipath->fmt.coding_type;
    ctx->in_fmt.sample_rate = ipath->fmt.sample_rate;
    ctx->in_fmt.channel = ipath->fmt.channel;
    ctx->in_fmt.bit_width = ipath->fmt.bit_width;
    ctx->dec_out_ch_mode = opath->fmt.channel;

    ctx->ipath = ipath->input.path;
    ctx->get_frame = ipath->input.pull_frame;
    ctx->free_frame = ipath->input.free_frame;
    ctx->clock = ipath->time.reference_clock;
    ctx->clock_time = (u32(*)(void *, u8, struct reference_time *))ipath->time.request;

    switch (ipath->fmt.coding_type) {
#if TCFG_DEC_JLA_ENABLE
    case AUDIO_CODING_JLA:
        ctx->in_fmt.bit_rate = ipath->fmt.bit_rate;
        ctx->in_fmt.frame_len = ipath->fmt.frame_len;
#if (defined(WIRELESS_SOUND_TRACK_2_P_X_ENABLE)&&WIRELESS_SOUND_TRACK_2_P_X_ENABLE) || (defined(TCFG_DYNAMIC_EQ_ENABLE) && TCFG_DYNAMIC_EQ_ENABLE)
        ctx->decoding_latency = LIVE_JLA_STREAM_DECODING_LATENCY(ipath->fmt.frame_len, ipath->fmt.channel);
        ctx->play_latency = LIVE_JLA_STREAM_PLAY_LATENCY(ctx->decoding_latency, ipath->fmt.frame_len);
#else
        ctx->decoding_latency = LIVE_JLA_STREAM_DECODING_LATENCY(ipath->fmt.frame_len, ipath->fmt.channel);
        ctx->play_latency = LIVE_JLA_STREAM_PLAY_LATENCY(ctx->decoding_latency, ipath->fmt.frame_len);
#endif
        y_printf("========   ctx->play_latency = %d\n", ctx->play_latency);
        break;
#endif

#if TCFG_DEC_PCM_ENABLE
    case AUDIO_CODING_PCM:
#if 1 //PCM格式与远端的JLA数据延迟对齐
#if (defined(WIRELESS_SOUND_TRACK_2_P_X_ENABLE)&&WIRELESS_SOUND_TRACK_2_P_X_ENABLE) || (defined(TCFG_DYNAMIC_EQ_ENABLE) && TCFG_DYNAMIC_EQ_ENABLE)
        ctx->decoding_latency = LIVE_JLA_STREAM_DECODING_LATENCY(ipath->fmt.frame_len, ipath->fmt.channel);
        ctx->play_latency = LIVE_JLA_STREAM_PLAY_LATENCY(ctx->decoding_latency, ipath->fmt.frame_len);
#else
        ctx->decoding_latency = LIVE_JLA_STREAM_DECODING_LATENCY(ipath->fmt.frame_len, ipath->fmt.channel);
        ctx->play_latency = LIVE_JLA_STREAM_PLAY_LATENCY(ctx->decoding_latency, ipath->fmt.frame_len);
#endif
#else
        ctx->play_latency = LIVE_STREAM_PLAY_LATENCY(0);
#endif
        break;
#endif

    default:
        printf("do not support this type !!!\n");
        return NULL;
    }

    ctx->res_wait.priority = 1;

#if (WIRELESS_2T1_DUPLEX_ROLE == 2)
    ctx->res_wait.protect = 1;
#else
    ctx->res_wait.protect = 0;
#endif

    ctx->res_wait.preemption = 0;
    ctx->res_wait.handler = live_player_wait_res_handler;
    audio_decoder_task_add_wait(LIVE_PLAY_DECODE_TASK, &ctx->res_wait);
    if (ctx && (ctx->state != LIVE_PLAYER_STATE_START)) {
        live_stream_auto_drain(ctx);
#if LIVE_STREAM_MULTI_PLAYER_ENABLE
        if (live_player_num > 0) {
            int err = live_stream_player_start(ctx);
            if (err) {
                live_stream_player_free(ctx);
            }
        }
#endif
    }

    return ctx;
}

int live_stream_player_capture(void *priv, struct audio_path *path)
{
    struct live_stream_player_context *ctx = (struct live_stream_player_context *)priv;

    if (!ctx->capture.entry.data_handler) {
        ctx->capture.wait_enable = 1;
    }
    ctx->capture.path = path->output.path;
    ctx->capture.write_frame = path->output.write_frame;
    ctx->capture.enable = 1;
    ctx->capture.nch = path->fmt.channel;
    return 0;
}

void live_stream_player_capture_stop(void *priv)
{
    struct live_stream_player_context *ctx = (struct live_stream_player_context *)priv;
    os_mutex_pend(&ctx->capture.mutex, 0);
    ctx->capture.enable = 0;
    os_mutex_post(&ctx->capture.mutex);
}
/*----------------------------------------------------------------------------*/
/**@brief    关闭broadcast解码
   @param    *priv: 私有句柄
   @return
   @note
*/
/*----------------------------------------------------------------------------*/
void live_stream_player_close(void *priv)
{
    struct live_stream_player_context *ctx = (struct live_stream_player_context *)priv;
    if (!ctx) {
        return;
    }
    printf(">>> %s   ");
    live_stream_stop_drain(ctx);
    if (ctx->state == LIVE_PLAYER_STATE_START) {
        live_stream_player_stop(ctx);
    }

    live_stream_player_free(ctx);
}

#endif/*TCFG_BROADCAST_ENABLE*/


