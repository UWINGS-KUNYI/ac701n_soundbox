#include "media/includes.h"
#include "audio_recorder_mix.h"
#include "audio_enc.h"
#include "audio_dec.h"
#include "media/pcm_decoder.h"
#include "btstack/btstack_task.h"
#include "btstack/avctp_user.h"
#include "audio_splicing.h"
#include "app_main.h"
#include "mic_effect.h"
#include "app_task.h"
#include "Resample_api.h"
#include "board_config.h"
#include "media/24bit_convert.h"

#if (RECORDER_MIX_EN)

#if (RECORDER_INPUT_BIT_WIDTH != RECORDER_INPUT_16BIT)
#define RECORDER_MIX_READ_DATA_ONCE		     (640) * 2
#else
#define RECORDER_MIX_READ_DATA_ONCE		     (640)
#endif

#ifndef RECORDER_MIX_SAMPLERATE
#define	RECORDER_MIX_SAMPLERATE				 (48000L)
#endif

#ifndef RECORDER_MIX_CHANNEL
#define RECORDER_MIX_CHANNEL                 1
#endif

#define REC_ALIN(var,al)     ((((var)+(al)-1)/(al))*(al))

#define MIX_FIFO_SYNC               (1) // 1:sync enable  0:sync disable

#define MIX_FIFO_BUF_TEMP_LEN       (128)
#define MIX_FIFO_BUF_POINTS         (128)
#define MIX_FIFO_CH_BUF_POINTS      (2048)//(1000)

#define REC_MIX_RUN                 1
#define REC_MIX_STOP                2

struct mix_fifo {
    struct list_head fifo_list;
    cbuffer_t cbuffer;
    u8 *cbuffer_ram;
#if MIX_FIFO_SYNC
    RS_STUCT_API *sw_src_api;
    u8 *sw_src_buf;
#endif
    u8 tmp_buf[MIX_FIFO_BUF_TEMP_LEN + 24];
    u8 tmp_buf1[MIX_FIFO_BUF_TEMP_LEN];
    u32 input_sample_rate;
    u32 output_sample_rate;
    u8 channel_num;
    OS_MUTEX mutex;
};

struct mix_fifo_ch {
    struct list_head ch_head;
    cbuffer_t cbuffer;
    u8 *cbuffer_ram;
    struct audio_stream_entry entry;    // 音频流入口
    u8 channel_num;         // output channel_num
    u8 input_channel_num;   // input  channel num
    u8 state;       // 1:on 0:off
    u8 bit_width;
};

/* static s16 recorder_mix_buf[320]; */
static s16 *recorder_mix_buf;
static s16 temp_buffer[256] = {0};
static OS_MUTEX rec_mix_mutex;
struct mix_fifo *rec_mix_fifo = NULL;
extern u32 audio_output_channel_num(void);
extern void pcm_dual_to_dual_or_single(u8 ch_type, u8 half_lr, s16 *odata, s16 *idata, int len);
int __rec_mix_fifo_write_update(void *fifo_hdl);

//混合录音总控制句柄
struct __recorder_mix {
    u16								timer;
    u8                              start;
};

struct __recorder_mix *recorder_hdl = NULL;
#define __this	recorder_hdl

int recorder_mix_stream_data_handler(void *buf, u16 len)
{
    s16 *data = buf;
    u16 data_len = len;

    if (data_len >= RECORDER_MIX_READ_DATA_ONCE) {
        data_len = RECORDER_MIX_READ_DATA_ONCE;
    }
    int wlen = recorder_userdata_to_enc(data, data_len);
    if (wlen != data_len) {
        putchar('s');
    }
    return wlen;
}

static void recorder_mix_err_callback(void)
{
    recorder_mix_stop();
}

static void recorder_mix_destroy(struct __recorder_mix **hdl)
{
    if (hdl == NULL || (*hdl == NULL)) {
        return ;
    }
    struct __recorder_mix *recorder = *hdl;

    local_irq_disable();
    free(*hdl);
    *hdl = NULL;
    local_irq_enable();
}

static struct __recorder_mix *recorder_mix_creat(void)
{
    struct __recorder_mix *recorder = (struct __recorder_mix *)zalloc(sizeof(struct __recorder_mix));
    return recorder;
}

static int __recorder_mix_start(struct __recorder_mix *recorder)
{
    struct record_file_fmt fmt = {0};
    /* char logo[] = {"sd0"}; */		//可以指定设备
    char folder[] = {REC_FOLDER_NAME};         //录音文件夹名称
    char filename[] = {"AC69****"};     //录音文件名，不需要加后缀，录音接口会根据编码格式添加后缀

#if (TCFG_NOR_REC)
    char logo[] = {"rec_nor"};		//外挂flash录音
#elif (FLASH_INSIDE_REC_ENABLE)
    char logo[] = {"rec_sdfile"};		//内置flash录音
#else
    char *logo = dev_manager_get_phy_logo(dev_manager_find_active(0));//普通设备录音，获取最后活动设备
#endif

    fmt.dev = logo;
    fmt.folder = folder;
    fmt.filename = filename;
    fmt.channel = RECORDER_MIX_CHANNEL;//1;//audio_output_channel_num();//跟mix通道数一致
    fmt.coding_type = (!RECORDER_CODING_TYPE ? AUDIO_CODING_MP3 : ((RECORDER_CODING_TYPE == 1) ? AUDIO_CODING_WAV : AUDIO_CODING_PCM));
    fmt.sample_rate = RECORDER_MIX_SAMPLERATE;

    printf("[%s], fmt.sample_rate = %d\n", __FUNCTION__, fmt.sample_rate);
    fmt.cut_head_time = 300;            //录音文件去头时间,单位ms
    fmt.cut_tail_time = 300;            //录音文件去尾时间,单位ms
    fmt.limit_size = 3000;              //录音文件大小最小限制， 单位byte
    fmt.source = ENCODE_SOURCE_MIX;     //录音输入源
    fmt.err_callback = recorder_mix_err_callback;
    /* fmt.bit_mode = TCFG_AUDIO_ADC_BIT_MODE; */
    fmt.bit_mode = RECORDER_INPUT_BIT_WIDTH | RECORDER_OUTPUT_BIT_WIDTH;//这里的位宽选择是方便让编码的buff大小变化的

    int ret = recorder_encode_start(&fmt);
    if (ret) {
        log_e("[%s] fail !!, dev = %s\n", __FUNCTION__, logo);
    } else {
        log_e("[%s] succ !!, dev = %s\n", __FUNCTION__, logo);
    }
    return ret;
}

static void __recorder_mix_stop(struct __recorder_mix *recorder)
{
    recorder_encode_stop();
}

static void __recorder_mix_timer(void *priv)
{
    u32 sec = recorder_get_encoding_time();
    printf("%d\n", sec);
}

//*----------------------------------------------------------------------------*/
/**@brief    混合录音开始
   @param
   @return   0成功， 非0失败
   @note
   			混合录音支持录制内容：
				BT sbc(高级音频）
				BT sco（蓝牙通话）
				FM（内置FM）
				Linein(外部音源输入)
			录音参数配置：
				请在__recorder_mix_start函数内部修改参数
				1、支持设备选择, 如：sd0、udisk0等
				2、修改文件名称及文件夹名称, 默认文件夹名称为JL_REC，文件名AC69****
				3、编码格式(资源受限，通话支持adpcm wav)
				4、支持砍头砍尾处理
			说明：
				1、录音允许打断配置, 通过RECORDER_MIX_BREAK_EN来配置
					1）录音过程中， 蓝牙音乐播放与通话切换过程， 自动打断， 如需继续录音需要手动启动
						A、该配置支持AEC回声消除，因为回声消除占用cpu及ram资源比较多，所以录音会被打断
						B、编码类型可选， SDK默认是除通话情况下使用wav格式，其他使用mp3
						C、采样率随当前dac的采样率
					2) 录音过程中， 蓝牙音乐播放与通话切换过程， 不允许打断， 录音继续
						A、该配置不支持AEC回声消除,因为该过程固定了编码采样率， 需要较大的ram及cpu资源
						B、编码类型可以选， 开混响情况下，只可以选择WAV， 不开混响可选MP3
						C、编码采样率固定，SDK默认配置采样率为32000, 不建议高于此采样率
						D、录制混响时，会录制混响+背景音乐
				2、混合录音支持蓝牙、FM、LINEIN模式, 其他模式不支持
*/
/*----------------------------------------------------------------------------*/
int recorder_mix_start(void)
{
    if (__this) {
        recorder_mix_stop();
    }

    struct __recorder_mix *recorder = recorder_mix_creat();
    if (recorder == NULL) {
        return -1;
    }

    int ret = __recorder_mix_start(recorder);
    if (ret) {
        recorder_mix_destroy(&recorder);
        return -1;
    }

    recorder_mix_buf = zalloc(RECORDER_MIX_READ_DATA_ONCE);
    ASSERT(recorder_mix_buf);

    local_irq_disable();
    __this = recorder;
    local_irq_enable();

    __this->timer = sys_timer_add((void *)__this, __recorder_mix_timer, 1000);

    printf("[%s] start ok\n", __FUNCTION__);
    __this->start = 1;
    mem_stats();

    return 0;
}

//*----------------------------------------------------------------------------*/
/**@brief    混合录音停止
   @param
   @return
   @note
*/
/*----------------------------------------------------------------------------*/
void recorder_mix_stop(void)
{
    if (__this && __this->timer) {
        sys_timer_del(__this->timer);
    }
    if (__this && __this->start) {
        __this->start = 0;
        os_mutex_pend(&rec_mix_mutex, 0);
        __recorder_mix_stop(__this);
        recorder_mix_destroy(&__this);
        if (recorder_mix_buf) {
            free(recorder_mix_buf);
            recorder_mix_buf = 0;
        }
        os_mutex_post(&rec_mix_mutex);
    }
    printf("[%s] stop ok\n", __FUNCTION__);

    mem_stats();
}
//*----------------------------------------------------------------------------*/
/**@brief    获取混合录音状态
   @param
   @return
  			1:正在录音状态
			0:录音停止状态
   @note
*/
/*----------------------------------------------------------------------------*/
int recorder_mix_get_status(void)
{
    return (__this ? 1 : 0);
}

u16 recorder_mix_get_samplerate(void)
{
    return RECORDER_MIX_SAMPLERATE;
}

u32 recorder_mix_get_coding_type(void)
{
    return (!RECORDER_CODING_TYPE ? AUDIO_CODING_MP3 : ((RECORDER_CODING_TYPE == 1) ? AUDIO_CODING_WAV : AUDIO_CODING_PCM));
}

void recorder_mix_bt_status_event(struct bt_event *e)
{
    switch (e->event) {
    case BT_STATUS_PHONE_INCOME:
        break;
    case BT_STATUS_PHONE_OUT:
        break;
    case BT_STATUS_PHONE_ACTIVE:
        break;
    case BT_STATUS_PHONE_HANGUP:
        recorder_mix_stop();
        break;
    case BT_STATUS_PHONE_NUMBER:
        break;
    case BT_STATUS_SCO_STATUS_CHANGE:
        if (e->value != 0xff) {
            //电话激活， 先停止当前录音
            recorder_mix_stop();
        } else {

        }
        break;
    case BT_STATUS_VOICE_RECOGNITION:
        if (e->value) { //如果是siri语音状态，停止录音
            recorder_mix_stop();
        }
        break;
    default:
        break;
    }
}

static void rec_mix_task(void *arg)
{
    int msg[16];
    int res;
    u8 pend_taskq = 1;

    while (1) {
        res = os_taskq_pend("taskq", msg, ARRAY_SIZE(msg));

        if (!__this) {
            continue;
        }

        if (res == OS_TASKQ) {
            switch (msg[1]) {
            case REC_MIX_RUN:
                __rec_mix_fifo_write_update((void *)msg[2]);
                os_mutex_pend(&rec_mix_mutex, 0);
                if (!recorder_mix_buf) {
                    os_mutex_post(&rec_mix_mutex);
                    break;
                }
                while (1) {
                    u32 rlen = rec_mix_fifo_read(rec_mix_fifo, recorder_mix_buf, RECORDER_MIX_READ_DATA_ONCE);
                    if (rlen == 0) {
                        /* putchar('h'); */
                        break;
                    } else {
                        int wlen = recorder_mix_stream_data_handler(recorder_mix_buf, rlen);
                        memset(recorder_mix_buf, 0x00, RECORDER_MIX_READ_DATA_ONCE);
                    }
                }
                os_mutex_post(&rec_mix_mutex);
                break;
            }
        }
    }
}

void *rec_mix_fifo_init(u8 channel_num, u32 sample_rate)
{
    printf("mix_fifo_init ch:%d sr:%d\n", channel_num, sample_rate);
    struct mix_fifo *fifo = NULL;
    fifo = zalloc(sizeof(struct mix_fifo));
    if (fifo == NULL) {
        printf("mix_fifo hdl malloc err!\n");
        return NULL;
    }
#if (RECORDER_INPUT_BIT_WIDTH != RECORDER_INPUT_16BIT)
    fifo->cbuffer_ram = zalloc(MIX_FIFO_CH_BUF_POINTS * channel_num * 4);
#else
    fifo->cbuffer_ram = zalloc(MIX_FIFO_CH_BUF_POINTS * channel_num * 2);
#endif
    if (fifo->cbuffer_ram == NULL) {
        printf("mix_fifo cbuffer_ram malloc err!\n");
        return NULL;
    }
#if (RECORDER_INPUT_BIT_WIDTH != RECORDER_INPUT_16BIT)
    cbuf_init(&fifo->cbuffer, fifo->cbuffer_ram, MIX_FIFO_CH_BUF_POINTS * channel_num * 4);
#else
    cbuf_init(&fifo->cbuffer, fifo->cbuffer_ram, MIX_FIFO_CH_BUF_POINTS * channel_num * 2);
#endif
    fifo->channel_num = channel_num;
    fifo->input_sample_rate = sample_rate;
    fifo->output_sample_rate = sample_rate;
    INIT_LIST_HEAD(&fifo->fifo_list);

#if MIX_FIFO_SYNC
    fifo->sw_src_api = get_rs16_context();
    ASSERT(fifo->sw_src_api);
    u32 sw_src_need_buf = fifo->sw_src_api->need_buf();
    fifo->sw_src_buf = zalloc(sw_src_need_buf);
    printf("sw src adr:%x size:%d\n", fifo->sw_src_buf, sw_src_need_buf);

    ASSERT(fifo->sw_src_buf);
    RS_PARA_STRUCT rs_para_obj;
    rs_para_obj.nch = fifo->channel_num;
    rs_para_obj.new_insample = fifo->input_sample_rate;
    rs_para_obj.new_outsample = fifo->output_sample_rate;
    printf("sw src,in = %d,out = %d\n", rs_para_obj.new_insample, rs_para_obj.new_outsample);
    fifo->sw_src_api->open(fifo->sw_src_buf, &rs_para_obj);
#endif
    rec_mix_fifo = fifo;
    os_mutex_create(&fifo->mutex);
    os_mutex_create(&rec_mix_mutex);
    int err = task_create(rec_mix_task, NULL, "rec_mix");
    if (err) {
        printf("usb mix mic create task error.\n");
    }
    return fifo;
}

int rec_mix_fifo_uninit(void *fifo_hdl)
{
    /* if (sw_src_api) { */
    /* sw_src_api = NULL; */
    /* } */
    /* if (sw_src_buf) { */
    /* free(sw_src_buf); */
    /* sw_src_buf = NULL; */
    /* } */
    return 0;
}

int rec_mix_fifo_ch_state_set(void *ch_hdl, u8 en)
{
    printf("rec_mix_fifo_ch_state_set %x %d\n", ch_hdl, en);
    struct mix_fifo_ch *fifo_ch = (struct mix_fifo_ch *)ch_hdl;
    os_mutex_pend(&rec_mix_fifo->mutex, 0);
    if (fifo_ch == NULL) {
        os_mutex_post(&rec_mix_fifo->mutex);
        return -1;
    }
    fifo_ch->state = en;
    os_mutex_post(&rec_mix_fifo->mutex);
    return 0;
}

static void rec_mix_fifo_output_data_process_len(struct audio_stream_entry *entry,  int len)
{
}

static int rec_mix_fifo_data_handler(struct audio_stream_entry *entry,
                                     struct audio_data_frame *in,
                                     struct audio_data_frame *out)
{
    struct mix_fifo_ch *fifo_ch = container_of(entry, struct mix_fifo_ch, entry);

    out->data = in->data;
    out->data_len = in->data_len;

    if (in->data_len - in->offset > 0) {
        if (fifo_ch) {
            rec_mix_fifo_ch_write(fifo_ch, in->data + in->offset / 2, in->data_len - in->offset);
            if (__this && __this->start) {
                rec_mix_fifo_write_update(rec_mix_fifo);
            }
        }
    }

    return in->data_len;
}

void *rec_mix_fifo_ch_open(u8 input_channel_num)
{
    struct mix_fifo *fifo = rec_mix_fifo;
    struct mix_fifo_ch *fifo_ch = NULL;
    if (fifo == NULL) {
        return NULL;
    }
    fifo_ch = zalloc(sizeof(struct mix_fifo_ch));
    if (fifo_ch == NULL) {
        printf("mix_fifo_ch hdl malloc err!\n");
        return NULL;
    }
    y_printf(">>>>%s   %d  %d", __FUNCTION__, input_channel_num, fifo->channel_num);
    if (0) { //(global_bit_wide) {
        fifo_ch->cbuffer_ram = zalloc(MIX_FIFO_CH_BUF_POINTS * fifo->channel_num * 4);
    } else {
        fifo_ch->cbuffer_ram = zalloc(MIX_FIFO_CH_BUF_POINTS * fifo->channel_num * 2);
    }
    if (fifo_ch->cbuffer_ram == NULL) {
        printf("mix_fifo_ch cbuffer_ram malloc err!\n");
        return NULL;
    }
    if (0) { //(global_bit_wide) {
        cbuf_init(&fifo_ch->cbuffer, fifo_ch->cbuffer_ram, MIX_FIFO_CH_BUF_POINTS * fifo->channel_num * 4);
    } else {
        cbuf_init(&fifo_ch->cbuffer, fifo_ch->cbuffer_ram, MIX_FIFO_CH_BUF_POINTS * fifo->channel_num * 2);
    }
    fifo_ch->channel_num = fifo->channel_num;
    fifo_ch->input_channel_num = input_channel_num;
    INIT_LIST_HEAD(&(fifo_ch->ch_head));
    fifo_ch->entry.data_process_len = rec_mix_fifo_output_data_process_len;
    fifo_ch->entry.data_handler = rec_mix_fifo_data_handler;
    fifo_ch->state = 1;
    fifo_ch->bit_width = global_bit_wide;
    list_add(&fifo_ch->ch_head, &fifo->fifo_list);
    return fifo_ch;
}

int rec_mix_fifo_ch_close(void *ch_hdl)
{
    printf("rec_mix_fifo_ch_close %x\n", ch_hdl);
    struct mix_fifo_ch *fifo_ch = (struct mix_fifo_ch *)ch_hdl;
    struct mix_fifo *fifo = (struct mix_fifo *)rec_mix_fifo;
    if (!fifo) {
        return 0;
    }
    os_mutex_pend(&fifo->mutex, 0);
    if (fifo_ch == NULL) {
        os_mutex_post(&fifo->mutex);
        return -1;
    }
    if (fifo_ch->cbuffer_ram != NULL) {
        free(fifo_ch->cbuffer_ram);
        fifo_ch->cbuffer_ram = NULL;
    }

    list_del(&fifo_ch->ch_head);
    free(fifo_ch);
    fifo_ch = NULL;
    os_mutex_post(&fifo->mutex);
    return 0;
}

int rec_mix_fifo_ch_write(void *ch_hdl, s16 *data, u32 len)
{
    u8 channel_num = 0;//
    int wlen = 0;
    s16 *temp_data = NULL;
    os_mutex_pend(&rec_mix_fifo->mutex, 0);
    struct mix_fifo_ch *fifo_ch = (struct mix_fifo_ch *)ch_hdl;
    if (fifo_ch == NULL) {
        os_mutex_post(&rec_mix_fifo->mutex);
        return 0;
    }

    if (len == 0) {
        os_mutex_post(&rec_mix_fifo->mutex);
        return 0;
    }
    if (fifo_ch->bit_width) {
        temp_data = zalloc(REC_ALIN(len / 2, 4));
        if (!temp_data) {
            os_mutex_post(&rec_mix_fifo->mutex);
            return 0;
        }
        audio_convert_data_32bit_to_16bit_round((s32 *)data, temp_data, len  / 4);
        len = len >> 1;
        data = temp_data;
    }
    channel_num = fifo_ch->input_channel_num;
    if (channel_num == 2) {
        if (rec_mix_fifo->channel_num == 1) {
            u32 remain_len = len;
            u32 ret_len = 0;
            while (remain_len) {
                if (remain_len > 256) {
                    pcm_dual_to_dual_or_single(AUDIO_CH_DIFF, 1, temp_buffer, data, 256);
                    ret_len = cbuf_write(&fifo_ch->cbuffer, temp_buffer, 128);
                    if (ret_len == 0) {
                        break;
                    }
                    wlen += ret_len;
                    remain_len -= 256;
                    data += 128;
                } else {
                    pcm_dual_to_dual_or_single(AUDIO_CH_DIFF, 1, temp_buffer, data, remain_len);
                    ret_len = cbuf_write(&fifo_ch->cbuffer, temp_buffer, remain_len / 2);
                    wlen += ret_len;
                    remain_len = 0;
                    break;
                }
            }
            if (wlen * channel_num != len) {
                /* putchar('c'); */
            }
            if (temp_data) {
                free(temp_data);
            }
            os_mutex_post(&rec_mix_fifo->mutex);
            return wlen * channel_num;
        } else if (rec_mix_fifo->channel_num == 2) {
            wlen = cbuf_write(&fifo_ch->cbuffer, data, len);
            if (wlen != len) {
                /* putchar('c'); */
            }
            if (temp_data) {
                free(temp_data);
            }
            os_mutex_post(&rec_mix_fifo->mutex);
            return wlen;
        }
    } else if (channel_num == 1) {

        if (rec_mix_fifo->channel_num == 1) {
            wlen = cbuf_write(&fifo_ch->cbuffer, data, len);
            if (wlen  != len) {
                /* putchar('c'); */
            }
            if (temp_data) {
                free(temp_data);
            }
            os_mutex_post(&rec_mix_fifo->mutex);
            return wlen;
        } else if (rec_mix_fifo->channel_num == 2) {

            u32 remain_len = len;
            u32 ret_len = 0;
            while (remain_len) {
                if (remain_len > 256) {
                    pcm_single_to_dual(temp_buffer, data, 256);
                    ret_len = cbuf_write(&fifo_ch->cbuffer, temp_buffer, 512);
                    if (ret_len == 0) {
                        break;
                    }
                    wlen += ret_len;
                    remain_len -= 256;
                    data += 128;
                } else {
                    pcm_single_to_dual(temp_buffer, data, remain_len);
                    ret_len = cbuf_write(&fifo_ch->cbuffer, temp_buffer, remain_len * 2);
                    wlen += ret_len;
                    remain_len = 0;
                    break;
                }
            }
            if (wlen / channel_num != len) {
                /* putchar('c'); */
            }
            if (temp_data) {
                free(temp_data);
            }
            os_mutex_post(&rec_mix_fifo->mutex);
            return wlen / channel_num;
        }
    }
    return len;
}

void *rec_mix_fifo_ch_get_entry(void *ch_hdl)
{
    printf("rec_mix_fifo_ch_get_entry %x\n", ch_hdl);
    struct mix_fifo_ch *fifo_ch = (struct mix_fifo_ch *)ch_hdl;
    os_mutex_pend(&rec_mix_fifo->mutex, 0);
    if (fifo_ch == NULL) {
        os_mutex_post(&rec_mix_fifo->mutex);
        return NULL;
    }
    os_mutex_post(&rec_mix_fifo->mutex);
    return (void *)(&(fifo_ch->entry));
}

int __rec_mix_fifo_write_update(void *fifo_hdl)
{
    //gpio_direction_output(IO_PORTG_05,1);
    struct mix_fifo *fifo = (struct mix_fifo *)fifo_hdl;
    struct mix_fifo_ch *ch = NULL;
    u32 update_len = 0;
    u32 mix_len = 0;
    u32 free_len = 0;
    s16 *data = NULL;
    s16 *data1 = NULL;
    u32 len = 0;
    u32 wlen = 0;

    os_mutex_pend(&fifo->mutex, 0);
    if (fifo == NULL) {
        //gpio_direction_output(IO_PORTG_05,0);
        os_mutex_post(&fifo->mutex);
        return 0;
    }

    while (1) {
        mix_len = 0xFFFF;
        list_for_each_entry(ch, &fifo->fifo_list, ch_head) {
            if (ch->cbuffer.data_len < mix_len) {
                mix_len = ch->cbuffer.data_len;
            }
        }

        if (mix_len == 0xFFFF || mix_len == 0) {
            //gpio_direction_output(IO_PORTG_05,0);
            os_mutex_post(&fifo->mutex);
            return update_len;
        }

        if (mix_len > MIX_FIFO_BUF_TEMP_LEN) {
            mix_len = MIX_FIFO_BUF_TEMP_LEN;
        }

        free_len = fifo->cbuffer.total_len - fifo->cbuffer.data_len;
        if (mix_len > free_len) {
            mix_len = free_len;
            if (mix_len == 0) {
                //gpio_direction_output(IO_PORTG_05,0);
                os_mutex_post(&fifo->mutex);
                return update_len;
            }
        }

        memset(&fifo->tmp_buf, 0x00, MIX_FIFO_BUF_TEMP_LEN + 24);
        memset(&fifo->tmp_buf1, 0x00, MIX_FIFO_BUF_TEMP_LEN);
        list_for_each_entry(ch, &fifo->fifo_list, ch_head) {
            data = &fifo->tmp_buf;
            data1 = &fifo->tmp_buf1;
            len = mix_len / 2;
            cbuf_read(&ch->cbuffer, data, mix_len);
            while (len--) {
                *data1 = data_sat_s16((s32) * data1 + (s32) * data);
                data++;
                data1++;
            }
        }

        data1 = &fifo->tmp_buf1;

#if MIX_FIFO_SYNC
        if (fifo->cbuffer.data_len > fifo->cbuffer.total_len * 3 / 4) {
            fifo->input_sample_rate += 5;
            /* putchar('>'); */
            if (fifo->input_sample_rate > fifo->output_sample_rate + 100) {
                fifo->input_sample_rate = fifo->output_sample_rate + 100;
                /* putchar('}'); */
            }
            fifo->sw_src_api->set_sr(fifo->sw_src_buf, fifo->input_sample_rate);
        } else if (fifo->cbuffer.data_len < fifo->cbuffer.total_len * 2 / 4) {
            fifo->input_sample_rate -= 5;
            /* putchar('<'); */
            if (fifo->input_sample_rate < fifo->output_sample_rate - 100) {
                fifo->input_sample_rate = fifo->output_sample_rate - 100;
                /* putchar('{'); */
            }
            fifo->sw_src_api->set_sr(fifo->sw_src_buf, fifo->input_sample_rate);
        }

        if ((fifo->sw_src_api != NULL) \
            && (fifo->sw_src_buf != NULL)) {
            u32 outlen = fifo->sw_src_api->run(fifo->sw_src_buf, data1, mix_len >> 1, data);
            outlen <<= 1;
#if (RECORDER_INPUT_BIT_WIDTH != RECORDER_INPUT_16BIT)
            s32 *temp_data = zalloc(REC_ALIN(outlen * 2, 4));
            if (!temp_data) {
                return 0;
            }
            audio_convert_data_16bit_to_32bit_round(data, temp_data, outlen / 2);
            outlen <<= 1;
            wlen = cbuf_write(&fifo->cbuffer, temp_data, outlen);
            if (wlen != outlen) {
                /* putchar('a'); */
            }
            free(temp_data);
#else
            wlen = cbuf_write(&fifo->cbuffer, data, outlen);
            if (wlen != outlen) {
                /* putchar('a'); */
            }
#endif
        } else {
            printf("mix fifo sw src is NULL\n");
        }
#else

#if (RECORDER_INPUT_BIT_WIDTH != RECORDER_INPUT_16BIT)
        s32 *temp_data = zalloc(REC_ALIN(mix_len * 2, 4));
        if (!temp_data) {
            return 0;
        }
        audio_convert_data_16bit_to_32bit_round(data, temp_data, mix_len / 2);
        u32 outlen = mix_len * 2;
        wlen = cbuf_write(&fifo->cbuffer, temp_data, outlen);
        if (wlen != outlen) {
            /* putchar('a'); */
        }
        free(temp_data);
#else
        wlen = cbuf_write(&fifo->cbuffer, data1, mix_len);
        if (wlen != mix_len) {
            putchar('a');
        }
#endif
#endif // #if MIX_FIFO_SYNC

        update_len += mix_len;
    }

}

int rec_mix_fifo_write_update(void *fifo_hdl)
{
    int err = os_taskq_post_msg("rec_mix", 2, REC_MIX_RUN, (int)fifo_hdl);
    if (err) {
        r_printf("rec_mix fifo write update error, err:0x%x\n", err);
    }

    return err;
}
int rec_mix_fifo_read(void *fifo_hdl, s16 *data, u32 len)
{
    struct mix_fifo *fifo = (struct mix_fifo *)fifo_hdl;
    if (fifo == NULL) {
        return 0;
    }

    u32 rlen = fifo->cbuffer.data_len;
    if (rlen < len) {
        rlen = cbuf_read(&fifo->cbuffer, data, rlen);
    } else {
        rlen = cbuf_read(&fifo->cbuffer, data, len);
    }
    return rlen;
}

void recorder_mix_update(void)
{
    rec_mix_fifo_write_update(rec_mix_fifo);
}

#endif//RECORDER_MIX_EN


