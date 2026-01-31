#include "asm/includes.h"
#include "media/includes.h"
#include "system/includes.h"
#include "audio_enc.h"
#include "audio_dec.h"
#include "app_main.h"
#include "audio_config.h"
#include "live_stream_encoder.h"


#if (TCFG_BROADCAST_ENABLE || TCFG_CONNECTED_ENABLE)

/* 资源互斥方式配置 */
#if 0
#define LIVE_STREAM_ENC_LOCK_INIT()
#define LIVE_STREAM_ENC_LOCK()  local_irq_disable()
#define LIVE_STREAM_ENC_UNLOCK()   local_irq_enable()
#else
#define LIVE_STREAM_ENC_LOCK_INIT()   spin_lock_init(&ctx->lock)
#define LIVE_STREAM_ENC_LOCK()  spin_lock(&ctx->lock)
#define LIVE_STREAM_ENC_UNLOCK()   spin_unlock(&ctx->lock)
#endif

#define LIVE_STREAM_FRAME_LEN		(2048)  //一定要大于编码一帧的数据长度

struct live_encoder_dynamic_analysis {
    u8 encoding;
    u32 min_enc_time;
    u32 enc_start_time;
    u32 enc_interval[10];
    u32 total_frames;
    void *time;
    u32(*current_time)(void *priv, u8 type);
};

struct live_stream_encode_context {
    struct audio_encoder encoder;   				//编码句柄
    volatile u8 start;                              //编码运行状态
    s16 output_frame[LIVE_STREAM_FRAME_LEN / 2];	//align 4Bytes
    int pcm_frame[LIVE_STREAM_FRAME_LEN / 4];   	//align 4Bytes
    void *ipath;
    int (*read_frame)(void *ipath, struct audio_frame *frame, int len);
    void *opath;
    int (*write_frame)(void *opath, struct audio_frame *frame);
    spinlock_t lock;
    struct live_encoder_dynamic_analysis *analysis_hdl;
};

extern const int CONFIG_LIVE_AUDIO_DYNAMIC_ANALYSIS;
extern struct audio_encoder_task *encode_task;

/*----------------------------------------------------------------------------*/
/**@brief   编码更新码率接口
   @param   *priv:私有句柄
   @note
*/
/*----------------------------------------------------------------------------*/
void live_stream_encode_bitrate_updata(void *priv, u32 bit_rate)
{
    struct live_stream_encode_context *ctx = (struct live_stream_encode_context *)priv;
    if (bit_rate < 64000) {
        bit_rate = 64000;
    } else if (bit_rate > 320000) {
        bit_rate = 320000;
    }
    audio_encoder_ioctrl(&ctx->encoder, 2, AUDIO_ENCODER_IOCTRL_CMD_UPDATE_BITRATE, bit_rate);
}
/*----------------------------------------------------------------------------*/
/**@brief   外部激活编码接口
   @param   *priv:私有句柄
   @note
*/
/*----------------------------------------------------------------------------*/
void live_stream_encoder_wakeup(void *priv)
{
    struct live_stream_encode_context *ctx = (struct live_stream_encode_context *)priv;

    if (!ctx) {
        return;
    }
    LIVE_STREAM_ENC_LOCK();
    if (ctx->start) {
        audio_encoder_resume(&ctx->encoder);
    }
    LIVE_STREAM_ENC_UNLOCK();
}

static u32 live_stream_encoder_current_time(struct live_encoder_dynamic_analysis *hdl)
{
    if (!hdl->current_time) {
        return 0;
    }

    return hdl->current_time(hdl->time, 0);
}
static void live_stream_encoder_dynamic_analysis(struct live_encoder_dynamic_analysis *hdl)
{
    if (CONFIG_LIVE_AUDIO_DYNAMIC_ANALYSIS != 1) {
        return;
    }
    if (!hdl) {
        return;
    }
    hdl->encoding = 0;
    u32 time = live_stream_encoder_current_time(hdl);
    int enc_time_ms = ((time - hdl->enc_start_time) & 0xfffffff);

    if (!hdl->min_enc_time || enc_time_ms < hdl->min_enc_time) {
        hdl->min_enc_time = enc_time_ms;
    }
    enc_time_ms /= 100;
    if ((enc_time_ms % 10) < 5) {
        enc_time_ms = enc_time_ms / 10 - 1;
        if (enc_time_ms < 0) {
            enc_time_ms = 0;
        }
    } else {
        enc_time_ms = enc_time_ms / 10;
    }
    ASSERT(enc_time_ms >= 0);
    if (enc_time_ms >= 10) {
        enc_time_ms = 9;
    }
    hdl->enc_interval[enc_time_ms]++;
    hdl->total_frames++;
}
/*----------------------------------------------------------------------------*/
/**@brief    broadcast编码数据获取接口
   @param    *encoder: 编码器句柄
   @return
   @note
*/
/*----------------------------------------------------------------------------*/
static int live_stream_enc_pcm_get(struct audio_encoder *encoder, s16 **frame, u16 frame_len)
{
    struct live_stream_encode_context *ctx = container_of(encoder, struct live_stream_encode_context, encoder);
    int rlen = 0;
    if (ctx == NULL) {
        return 0;
    }

    struct audio_frame pcm_frame = {
        .len = 0,
        .data = ctx->pcm_frame,
    };
    rlen = ctx->read_frame(ctx->ipath, &pcm_frame, frame_len);

    if (rlen == frame_len) {
        /*编码读取数据正常*/
        if (CONFIG_LIVE_AUDIO_DYNAMIC_ANALYSIS == 1) {
            ctx->analysis_hdl->enc_start_time = live_stream_encoder_current_time(ctx->analysis_hdl);
            ctx->analysis_hdl->encoding = 1;
        }
    } else if (rlen == 0) {
        /*编码读不到数据会挂起解码，由前级输出数据后激活解码*/
        return 0;
    } else {
        printf("audio_enc end:%d\n", rlen);
        rlen = 0;
    }

    *frame = (s16 *)ctx->pcm_frame;
    return rlen;
}

static void live_stream_enc_pcm_put(struct audio_encoder *encoder, s16 *frame)
{
}

static const struct audio_enc_input live_stream_encode_input = {
    .fget = live_stream_enc_pcm_get,
    .fput = live_stream_enc_pcm_put,
};

/*----------------------------------------------------------------------------*/
/**@brief    broadcast编码预处理
   @param    *encoder: 编码器句柄
   @return
   @note
*/
/*----------------------------------------------------------------------------*/
static int live_stream_enc_probe_handler(struct audio_encoder *encoder)
{
    struct live_stream_encode_context *ctx = container_of(encoder, struct live_stream_encode_context, encoder);
    return 0;
}

/*----------------------------------------------------------------------------*/
/**@brief    broadcast编码数据输出接口
   @param    *encoder: 编码器句柄
   @param    frame:    编码数据
   @param    len: 	   数据长度
   @return
   @note
*/
/*----------------------------------------------------------------------------*/
static int live_stream_enc_output_handler(struct audio_encoder *encoder, u8 *frame, int len)
{
    struct live_stream_encode_context *ctx = container_of(encoder, struct live_stream_encode_context, encoder);
    int wlen = 0;

    if (!ctx || !ctx->start) {
        putchar('X');
        return 0;
    }

    live_stream_encoder_dynamic_analysis(ctx->analysis_hdl);
    /*putchar('+');*/
    struct audio_frame stream_frame = {
        .data = frame,
        .len = len,
    };
    /*printf("-enc : 0x%x, %d-\n", (u32)frame, len);*/
    wlen = ctx->write_frame(ctx->opath, &stream_frame);

    return wlen;
}

const static struct audio_enc_handler live_stream_encode_handler = {
    .enc_probe = live_stream_enc_probe_handler,
    .enc_output = live_stream_enc_output_handler,
};

/*----------------------------------------------------------------------------*/
/**@brief    broadcast编码事件处理
   @param    *decoder: 编码器句柄
   @param    argc: 参数个数
   @param    *argv: 参数
   @return
   @note
*/
/*----------------------------------------------------------------------------*/
static void broadcast_enc_event_handler(struct audio_encoder *encoder, int argc, int *argv)
{
    printf("broadcast_enc_event_handler:0x%x,%d\n", argv[0], argv[0]);
    switch (argv[0]) {
    case AUDIO_ENC_EVENT_END:
        puts("AUDIO_ENC_EVENT_END\n");
        break;
    }
}

/*----------------------------------------------------------------------------*/
/**@brief    broadcast编码流激活
   @param    *priv: 私有句柄
   @return
   @note
*/
/*----------------------------------------------------------------------------*/
static void broadcast_enc_resume(void *priv)
{
    struct live_stream_encode_context *ctx = (struct live_stream_encode_context *)priv;
    if (ctx) {
        audio_encoder_resume(&ctx->encoder);
    }
}

/*----------------------------------------------------------------------------*/
/**@brief    打开实时流编码
   @param  	 编码参数
   @return   编码私有参数
   @note
*/
/*----------------------------------------------------------------------------*/
extern int audio_encoder_task_open(void);
extern void audio_encoder_task_close(void);
void *live_stream_encoder_open(struct audio_path *path)
{
    int ret;
    struct audio_fmt fmt = {0};
    memcpy(&fmt, &path->fmt, sizeof(fmt));
    printf("live stream encoder type : 0x%x\n", fmt.coding_type);

    struct live_stream_encode_context *ctx = zalloc(sizeof(struct live_stream_encode_context));
    ASSERT(ctx);
    ctx->ipath = path->input.path;
    ctx->opath = path->output.path;
    ctx->write_frame = path->output.write_frame;
    ctx->read_frame = path->input.read_frame;
    LIVE_STREAM_ENC_LOCK_INIT();

    if (CONFIG_LIVE_AUDIO_DYNAMIC_ANALYSIS == 1) {
        ctx->analysis_hdl = (struct live_encoder_dynamic_analysis *)zalloc(sizeof(struct live_encoder_dynamic_analysis));
        ctx->analysis_hdl->time = path->time.reference_clock;
        ctx->analysis_hdl->current_time = path->time.request;
    }
    audio_encoder_task_open();

    audio_encoder_open(&ctx->encoder, &live_stream_encode_input, encode_task);
    audio_encoder_set_handler(&ctx->encoder, &live_stream_encode_handler);
    audio_encoder_set_fmt(&ctx->encoder, &fmt);
    audio_encoder_set_event_handler(&ctx->encoder, broadcast_enc_event_handler, 0);
    audio_encoder_set_output_buffs(&ctx->encoder, ctx->output_frame,
                                   sizeof(ctx->output_frame), 1);
    //配置数据输入输出接口
    if (!ctx->encoder.enc_priv) {
        log_e("encoder err, maybe coding(0x%x) disable \n", fmt.coding_type);
        audio_encoder_close(&ctx->encoder);
        free(ctx);
        return NULL;
    }
    clock_add_set(BROADCAST_ENC_CLK);

    ctx->start = 1;
    audio_encoder_start(&ctx->encoder);
    printf("----  audio_encoder_start succ \n");

    return ctx;
}

void live_stream_encoder_stop(void *encoder)
{
    struct live_stream_encode_context *ctx = (struct live_stream_encode_context *)encoder;

    LIVE_STREAM_ENC_LOCK();
    if (ctx->start) {
        ctx->start = 0;
        LIVE_STREAM_ENC_UNLOCK();
        audio_encoder_close(&ctx->encoder);
        return;
    }
    LIVE_STREAM_ENC_UNLOCK();
}


void live_stream_encoder_capacity_dump(void *priv)
{
    struct live_stream_encode_context *ctx = (struct live_stream_encode_context *)priv;
    struct live_encoder_dynamic_analysis *hdl = ctx->analysis_hdl;

    if (CONFIG_LIVE_AUDIO_DYNAMIC_ANALYSIS != 1 || !hdl) {
        return;
    }
#define PERCENT_CALCULATE(n) \
    (hdl->enc_interval[n] * 100 / hdl->total_frames)

    printf("encoder frames : %d\n"
           "[1ms] : %%%d\n"
           "[2ms] : %%%d\n"
           "[3ms] : %%%d\n"
           "[4ms] : %%%d\n"
           "[5ms] : %%%d\n"
           "[6ms] : %%%d\n"
           "[7ms] : %%%d\n"
           "[8ms] : %%%d\n"
           "[>9ms] : %%%d\n"
           "min encode us : %dus\n", hdl->total_frames, PERCENT_CALCULATE(0), PERCENT_CALCULATE(1), PERCENT_CALCULATE(2), PERCENT_CALCULATE(3),
           PERCENT_CALCULATE(4), PERCENT_CALCULATE(5), PERCENT_CALCULATE(6), PERCENT_CALCULATE(7), PERCENT_CALCULATE(8) + PERCENT_CALCULATE(9), hdl->min_enc_time);
}

/*----------------------------------------------------------------------------*/
/**@brief   关闭 broadcast 编码
   @param   私有参数
   @return
   @note
*/
/*----------------------------------------------------------------------------*/
void live_stream_encoder_close(void *priv)
{
    struct live_stream_encode_context *ctx = (struct live_stream_encode_context *)priv;
    u8 encoder_close = 0;
    if (!ctx) {
        return ;
    }
    LIVE_STREAM_ENC_LOCK();
    if (ctx->start) {
        ctx->start = 0;
        encoder_close = 1;
    }
    LIVE_STREAM_ENC_UNLOCK();
    if (encoder_close) {
        audio_encoder_close(&ctx->encoder);
    }
    audio_encoder_task_close();
    local_irq_disable();
    if (CONFIG_LIVE_AUDIO_DYNAMIC_ANALYSIS == 1) {
        if (ctx->analysis_hdl) {
            free(ctx->analysis_hdl);
        }
    }
    free(ctx);
    local_irq_enable();
    clock_remove_set(BROADCAST_ENC_CLK);
}

#endif/*TCFG_BROADCAST_ENABLE*/
