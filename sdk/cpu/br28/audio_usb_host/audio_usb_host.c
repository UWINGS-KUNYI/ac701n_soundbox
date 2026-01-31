#include "audio_usb_host.h"
#include "system/app_core.h"
#include "system/includes.h"
#include "server/server_core.h"
#include "app_config.h"
#include "audio_config.h"
#include "audio_enc.h"
#include "audio_dec.h"
#include "usb/usb_config.h"
#include "audio_src.h"
#include "circular_buf.h"
#include "media/mixer.h"
#include "media/pcm_decoder.h"
#include "system/includes.h"
#include "audio_enc.h"
#include "application/audio_dig_vol.h"
#include "uac_host.h"

#define LOG_TAG_CONST       USB
#define LOG_TAG             "[AUDIO_USB_HOST]"
#define LOG_ERROR_ENABLE
#define LOG_DEBUG_ENABLE
#define LOG_INFO_ENABLE
#define LOG_CLI_ENABLE
#include "debug.h"

#if TCFG_HOST_AUDIO_ENABLE

#define AUDIO_USB_FADE_IN_STEP  0.001f	//淡入的步进，决定淡入的快慢
#define AUDIO_USB_FADE_IN_DROP_CNT 100	//淡入前丢掉的数据数

struct __usbSpk_stream {
    struct audio_stream_entry entry;	// 数据流节点
    void *spk_dig_vol_hdl;
    void *priv;							// resume私有参数
};

enum audio_usb_host_state {
    USB_AUDIO_STATE_OFFLINE = 0x00,
    USB_AUDIO_STATE_ONLINE,
};

#define USB_HOST_SRC_CBUF_LEN (1024 * 2)

/* usb host Mic -> pcm dec */
struct __usbMic_dec_hdl {
    struct audio_stream *stream;	// 音频流
    struct pcm_decoder pcm_dec;		// pcm解码句柄
    struct audio_res_wait wait;		// 资源等待句柄
    struct audio_mixer_ch mix_ch;	// 叠加句柄
    u32 id;
    int sr;
    u8 start;	//正在解码标识
    u8 need_resume;
};

/* usb host Spk */
struct __usbSpk_hdl {
    struct __usbSpk_stream usbSpk_stream;	//USB master spk数据流
    struct audio_src_handle hw_src;
    cbuffer_t hw_src_cbuffer;	// src cbuf
    OS_SEM hw_src_sem;			// src 的信号量
    int in_sr;
    int out_sr;
    int row_out_sr;
    u8 src_read_buf[USB_HOST_SRC_CBUF_LEN];	//存放读取 src cbuf 的数据
    u8 *src_buf;				// src 使用到
    u8 hw_src_init_flag;
    u8 src_start_flag;
    u8 busy;
    u8 fade_in_flag;		//淡入操作
    u8 fade_in_drop_frame;
    float fade_in_coefficient;
};

struct __mic_stream_out {
    void *priv;
    u32(*func)(void *priv, void *data, int len);
};

/* usb host Mic */
struct __usbMic_hdl {
    struct __mic_stream_out out;		//注册输出回调函数
    struct audio_src_handle hw_src;		//src
    cbuffer_t src_cbuffer;	// src cbuf
    OS_SEM mic_sem;
    u32 buf_size;
    u32 mic_sr;
    u32 out_sr;
    u8 ch;	//声道数
    u8 *mic_row_buf;
    u8 *src_buf;
    volatile u8 start;
};

struct __usb_host_config {
    u32 spk_sr;
    u16 spk_ch;
    u16 spk_bit;		//bit
    u32 mic_sr;
    u16 mic_ch;
    u16 mic_bit;
};

struct __usb_master_device {
    u32 usb_id;
    u8 get_usb_host_info;
    u8 spk_open_flag;
    u8 mic_open_flag;
    u8 state;

    struct __usb_host_config host_config;

    /* mic 使用的: mic -> pcm 解码(测试用，无实际应用) */
    struct __usbMic_dec_hdl *usbMic_dec_hdl;

    /* mic 使用的：打开mic，然后注册回调接口，单独使用usb mic */
    struct __usbMic_hdl *usbMic;

    /* usb Spk 使用到的结构体, 因为是数据流的形式，因此用静态内存的方式，不采用动态申请的方式 */
    struct __usbSpk_hdl usbSpk;
};

extern struct audio_mixer mixer;

struct __usb_master_device host_dev = {0};

/* 将数据写进变采样模块 */
static int audio_usb_spk_src_resample_write(void *indata, int len)
{
    int wlen = 0;
    if (host_dev.usbSpk.hw_src_init_flag == 1 && host_dev.spk_open_flag == 1) {
        wlen = audio_src_resample_write(&host_dev.usbSpk.hw_src, indata, len);
    }
    return wlen;
}

static int usb_host_spk_data_handler(struct audio_stream_entry *entry,
                                     struct audio_data_frame *in,
                                     struct audio_data_frame *out)
{
    struct __usbSpk_stream *hdl = container_of(entry, struct __usbSpk_stream, entry);

    int in_sr = audio_mixer_get_sample_rate(&mixer);

    if (host_dev.state == USB_AUDIO_STATE_OFFLINE) {
        goto __exit;
    }
    if (host_dev.state == USB_AUDIO_STATE_ONLINE && host_dev.spk_open_flag == 0) {
        //此时说明设备插入, 打开uac 和 src，state -> START
        host_dev.usbSpk.in_sr = in_sr;
        host_dev.usbSpk.row_out_sr = host_dev.host_config.spk_sr;
        audio_usb_host_spk_start(in_sr, host_dev.host_config.spk_sr);
        /* audio_host_spk_fade_in(); */
    }

    if (host_dev.spk_open_flag == 1) {
        //如果采样率发生了变化，则直接改变采样率
        if (in_sr != host_dev.usbSpk.in_sr && in_sr != 0) {
            y_printf("%s, input_sr change : %d -> %d\n", __func__, host_dev.usbSpk.in_sr, in_sr);
            host_dev.usbSpk.in_sr = in_sr;
            audio_hw_src_set_rate(&host_dev.usbSpk.hw_src, host_dev.usbSpk.in_sr, host_dev.usbSpk.out_sr);
            goto __exit;
        }

        /* 3 - 输入输出异步空间的形式 */
        int wlen = cbuf_write(&host_dev.usbSpk.hw_src_cbuffer, in->data, in->data_len);

        if (wlen != in->data_len) {
            putchar('B');
        }
        os_sem_set(&host_dev.usbSpk.hw_src_sem, 0);
        os_sem_post(&host_dev.usbSpk.hw_src_sem);
    }

__exit:
    return in->data_len;
}

// 后级使用了多少数据
static void usb_host_spk_data_process_len(struct audio_stream_entry *entry,  int len)
{
    struct __usbSpk_stream *hdl = container_of(entry, struct __usbSpk_stream, entry);
}

void *get_usb_host_spk_stream_entry(void)
{
    return (void *)(&(host_dev.usbSpk.usbSpk_stream.entry));
}

void audio_usb_spk_set_volume(s8 vol)
{
    g_printf("set usb SPK Volum : %d\n", vol);
    if (host_dev.host_config.spk_ch == 2) {
        if (host_dev.usbSpk.usbSpk_stream.spk_dig_vol_hdl) {
            audio_dig_vol_set(host_dev.usbSpk.usbSpk_stream.spk_dig_vol_hdl, 1, vol);
            audio_dig_vol_set(host_dev.usbSpk.usbSpk_stream.spk_dig_vol_hdl, 2, vol);
        } else {
            log_error("%s, %d, usb SPK set_dig_vol failed\n", __func__, __LINE__);
        }
    } else {
        //host_dev.host_config.spk_ch==1
        if (host_dev.usbSpk.usbSpk_stream.spk_dig_vol_hdl) {
            audio_dig_vol_set(host_dev.usbSpk.usbSpk_stream.spk_dig_vol_hdl, 1, vol);
        } else {
            log_error("%s, %d, usb SPK set_dig_vol failed\n", __func__, __LINE__);
        }
    }
}

/* audio spk 淡入操作 */
void audio_host_spk_fade_in(void)
{
    if (host_dev.state == USB_AUDIO_STATE_ONLINE && host_dev.spk_open_flag == 1) {
        r_printf("\nSPK need Fade In!\n");
        if (host_dev.usbSpk.fade_in_flag == 0) {
            host_dev.usbSpk.fade_in_flag = 1;
        }
        host_dev.usbSpk.src_start_flag = 0;
        host_dev.usbSpk.fade_in_drop_frame = AUDIO_USB_FADE_IN_DROP_CNT;
        host_dev.usbSpk.fade_in_coefficient = 0.01f;
    }
}


/* usb spk 减小音量 */
void audio_usb_spk_volume_down(void)
{
    if (host_dev.host_config.spk_ch == 2) {
        if (host_dev.usbSpk.usbSpk_stream.spk_dig_vol_hdl) {
            int lcur_vol = audio_dig_vol_get(host_dev.usbSpk.usbSpk_stream.spk_dig_vol_hdl, 1);
            int rcur_vol = audio_dig_vol_get(host_dev.usbSpk.usbSpk_stream.spk_dig_vol_hdl, 2);
            if (lcur_vol && rcur_vol) {
                lcur_vol--;
                rcur_vol--;
            }
            audio_dig_vol_set(host_dev.usbSpk.usbSpk_stream.spk_dig_vol_hdl, 1, lcur_vol);
            audio_dig_vol_set(host_dev.usbSpk.usbSpk_stream.spk_dig_vol_hdl, 2, lcur_vol);
        } else {
            log_error("%s, %d, usb SPK set_dig_vol failed\n", __func__, __LINE__);
        }
    } else {
        //host_dev.host_config.spk_ch==1

        if (host_dev.usbSpk.usbSpk_stream.spk_dig_vol_hdl) {
            int cur_vol = audio_dig_vol_get(host_dev.usbSpk.usbSpk_stream.spk_dig_vol_hdl, 1);
            if (cur_vol) {
                cur_vol--;
            }
            audio_dig_vol_set(host_dev.usbSpk.usbSpk_stream.spk_dig_vol_hdl, 1, cur_vol);
        } else {
            log_error("%s, %d, usb SPK set_dig_vol failed\n", __func__, __LINE__);
        }
    }
}
/* usb spk 增加音量 */
void audio_usb_spk_volume_up(void)
{
    if (host_dev.host_config.spk_ch == 2) {
        if (host_dev.usbSpk.usbSpk_stream.spk_dig_vol_hdl) {
            int lcur_vol = audio_dig_vol_get(host_dev.usbSpk.usbSpk_stream.spk_dig_vol_hdl, 1);
            int rcur_vol = audio_dig_vol_get(host_dev.usbSpk.usbSpk_stream.spk_dig_vol_hdl, 2);
            lcur_vol++;
            rcur_vol++;
            audio_dig_vol_set(host_dev.usbSpk.usbSpk_stream.spk_dig_vol_hdl, 1, lcur_vol);
            audio_dig_vol_set(host_dev.usbSpk.usbSpk_stream.spk_dig_vol_hdl, 2, lcur_vol);
        } else {
            log_error("%s, %d, usb SPK set_dig_vol failed\n", __func__, __LINE__);
        }
    } else {
        //host_dev.host_config.spk_ch==1
        if (host_dev.usbSpk.usbSpk_stream.spk_dig_vol_hdl) {
            int cur_vol = audio_dig_vol_get(host_dev.usbSpk.usbSpk_stream.spk_dig_vol_hdl, 1);
            cur_vol++;
            audio_dig_vol_set(host_dev.usbSpk.usbSpk_stream.spk_dig_vol_hdl, 1, cur_vol);
        } else {
            log_error("%s, %d, usb SPK set_dig_vol failed\n", __func__, __LINE__);
        }
    }
}

/* 申请一次后，不释放掉 usbSpk */
void audio_usb_host_spk_stream_init(void *dig_vol_hdl)
{
    y_printf("=== init Usb Master SPK audio stream ===");
    if (dig_vol_hdl) {
        host_dev.usbSpk.usbSpk_stream.spk_dig_vol_hdl = (void *)dig_vol_hdl;
    }
    host_dev.usbSpk.usbSpk_stream.entry.data_handler = usb_host_spk_data_handler;
    host_dev.usbSpk.usbSpk_stream.entry.data_process_len = usb_host_spk_data_process_len;
}


/* 做同步，根据uac buf的大小来调节采样率 */
static void audio_usb_host_spk_sync_deal(void)
{
    if (host_dev.usbSpk.hw_src_init_flag == 1) {
        static u32 dir = 1;
        static u32 last_dir = 1;
        u32 buf_size = uac_host_get_spk_cbuf_size();
        u32 spk_cbuf_len = uac_host_get_spk_cbuf_len();
        int o_sr = host_dev.usbSpk.out_sr;
        int last_o_sr = o_sr;

        if (host_dev.usbSpk.src_start_flag == 0) {
            if (spk_cbuf_len >= buf_size * 2 / 20) {
                host_dev.usbSpk.src_start_flag = 1;
            } else {
                o_sr = host_dev.usbSpk.row_out_sr;
                host_dev.usbSpk.out_sr = o_sr;
            }
        } else {
            if (spk_cbuf_len >= (buf_size * 8 / 20)) {
                dir = 0;	//down
                if (dir != last_dir) {
                    /* putchar('-'); */
                    o_sr = host_dev.usbSpk.row_out_sr;
                }
                if (spk_cbuf_len >= (buf_size * 12 / 20)) {
                    o_sr -= 1;
                }
                o_sr -= 1;
                host_dev.usbSpk.out_sr = o_sr;
            } else if (spk_cbuf_len <= (buf_size * 6 / 20)) {
                dir = 1;	//up
                if (dir != last_dir) {
                    /* putchar('+'); */
                    o_sr = host_dev.usbSpk.row_out_sr;
                }
                if (spk_cbuf_len <= (buf_size * 4 / 20)) {
                    o_sr += 1;
                }
                o_sr += 1;
                host_dev.usbSpk.out_sr = o_sr;
            } else {
                /* putchar('='); */
            }
        }

        /* printf(">> spk_cbuf_len : %d\n", spk_cbuf_len); */

        last_dir = dir;
        if (last_o_sr != o_sr) {
            audio_hw_src_set_rate(&host_dev.usbSpk.hw_src, host_dev.usbSpk.in_sr, o_sr);
        }
    }
}

static int audio_usb_spk_src_output(void *priv, s16 *data, int len)
{
    if (host_dev.state == USB_AUDIO_STATE_ONLINE) {
#if 1
        /* 看是否需要淡入操作 */
        if (host_dev.usbSpk.fade_in_flag == 1) {
            /* static float fade_in_step = 0.01f; */

            static u8 printf_flag = 0;
            if (host_dev.usbSpk.fade_in_drop_frame) {
                host_dev.usbSpk.fade_in_drop_frame--;
                putchar('D');
                if (printf_flag <= 3) {
                    printf_flag++;
                    printf("\n>>>> in_sr:%d, out_sr:%d\n", host_dev.usbSpk.in_sr, host_dev.usbSpk.row_out_sr);
                }
                audio_hw_src_set_rate(&host_dev.usbSpk.hw_src, host_dev.usbSpk.in_sr, host_dev.usbSpk.row_out_sr);
                return len;
            }
            for (int i = 0; i < len / 2; i++) {
                data[i]	= (s16)((float)data[i] * host_dev.usbSpk.fade_in_coefficient);
            }
            host_dev.usbSpk.fade_in_coefficient += AUDIO_USB_FADE_IN_STEP;
            if (host_dev.usbSpk.fade_in_coefficient >= 1.0f) {
                // 淡入完成
                g_printf("\nUSB Fade in Success!\n");
                printf_flag = 0;
                host_dev.usbSpk.fade_in_flag = 0;
            }
        }


        /* 1 - 直接写给 USB */
        int wlen = uac_host_write_spk_cbuf((u8 *)data, len);

        // 做同步用
        audio_usb_host_spk_sync_deal();
        int remain = len - wlen;
        if (remain) {
            putchar('K');
        }

        return len;
#endif
    }
    return 0;
}


/* 创建一个任务，用来读取变采样的数据 */
static void audio_host_spk_task_deal(void *priv)
{
    y_printf("=== Enter task : %s === \n", __func__);
    while (1) {
        os_sem_pend(&host_dev.usbSpk.hw_src_sem, 0);
        if (host_dev.state == USB_AUDIO_STATE_ONLINE) {
            int cbuf_len = cbuf_get_data_len(&host_dev.usbSpk.hw_src_cbuffer);
            int read_len = 0;
            int rlen = 0;
            int wlen = 0;

            if (cbuf_len >= 512) {
                read_len = 512;
            } else if (cbuf_len > 0) {
                read_len = cbuf_len;
            }
            if (read_len) {
                rlen = cbuf_read(&host_dev.usbSpk.hw_src_cbuffer, host_dev.usbSpk.src_read_buf, read_len);
                wlen = audio_usb_spk_src_resample_write(host_dev.usbSpk.src_read_buf, rlen);
                /* audio_src_base_data_flush_out(&host_dev.usbSpk.hw_src); */
            }

            static u8 rewrite_cnt = 0;
            int remain = rlen - wlen;
            if (remain) {
                host_dev.usbSpk.busy = 1;
                rewrite_cnt = 0;
__spk_src_rewrite:
                rewrite_cnt++;
                wlen = audio_usb_spk_src_resample_write(&(host_dev.usbSpk.src_read_buf[rlen - remain]), remain);
                remain -= wlen;
                if (remain && rewrite_cnt <= 50) {
                    goto __spk_src_rewrite;
                }
                if (wlen != 0) {
                    /* putchar('P'); */
                }
                if (remain) {
                    putchar('S');
                    r_printf("remain: %d\n\n", remain);
                }
                host_dev.usbSpk.busy = 0;
            }
        } else if (host_dev.state == USB_AUDIO_STATE_OFFLINE) {
            printf("Wait %s task del......\n", __func__);
            while (1) {
                os_time_dly(10000);
            }
        }
    }
}

/* 初始化变采样模块 */
static void audio_usb_spk_hw_src_init(int in_sr, int out_sr)
{
    u8 in_channel = 2;
    u8 output_channel = host_dev.host_config.spk_ch;

    /* 初始化 hw src 使用到的cbuf */
    host_dev.usbSpk.src_buf = malloc(USB_HOST_SRC_CBUF_LEN);
    cbuf_init(&host_dev.usbSpk.hw_src_cbuffer, host_dev.usbSpk.src_buf, USB_HOST_SRC_CBUF_LEN);

    /* 打开 src 模块 */
    audio_hw_src_open(&host_dev.usbSpk.hw_src, in_channel, output_channel);
    audio_hw_src_set_rate(&host_dev.usbSpk.hw_src, in_sr, out_sr);
    audio_src_set_output_handler(&host_dev.usbSpk.hw_src, NULL, (int (*)(void *, void *, int))audio_usb_spk_src_output);

    host_dev.usbSpk.hw_src_init_flag = 1;
}

/* 停止并关闭 变采样模块 */
static void audio_usb_spk_hw_src_close(void)
{
    if (host_dev.usbSpk.hw_src_init_flag == 1) {
        audio_hw_src_stop(&host_dev.usbSpk.hw_src);
        audio_hw_src_close(&host_dev.usbSpk.hw_src);
        if (host_dev.usbSpk.src_buf) {
            free(host_dev.usbSpk.src_buf);
            host_dev.usbSpk.src_buf = NULL;
        }
        host_dev.usbSpk.hw_src_init_flag = 0;
    }
}


/*
 * 打开 src 变采样，开始传输数据(spk)
 */
void audio_usb_host_spk_start(int in_sr, int out_sr)
{
    if (host_dev.state == USB_AUDIO_STATE_ONLINE && host_dev.spk_open_flag == 0) {
        /* 打开变采样 */
        host_dev.usbSpk.in_sr = in_sr;
        host_dev.usbSpk.out_sr = out_sr;
        audio_usb_spk_hw_src_init(in_sr, out_sr);

        /* 初始化信号量 */
        os_sem_create(&host_dev.usbSpk.hw_src_sem, 0);
        task_create(audio_host_spk_task_deal, NULL, "spk_task");
        int err = uac_host_open_spk(host_dev.usb_id, host_dev.host_config.spk_ch, (u32)out_sr, host_dev.host_config.spk_bit);
        if (err) {
            r_printf("%s, %d, UAC SPK open failed!\n", __func__, __LINE__);
            task_kill("spk_task");
            os_sem_del(&host_dev.usbSpk.hw_src_sem, 0);
            audio_usb_spk_hw_src_close();
            return;
        }
        host_dev.spk_open_flag = 1;
    }
}

// 初始化usb audio 配置, 成功返回0, 其它返回值表示失败
static int audio_usb_host_config_init(void)
{
    //这些table 是配置的候选值, 排前面的优先级高
    u32 sr_table[] = {44100, 48000, 16000, 32000, 8000, 22050, 96000};
    u32 ch_table[] = {2, 1};
    u32 bit_table[] = {16, 8, 32};
    int sr_table_cnt = sizeof(sr_table) / sizeof(sr_table[0]);
    int ch_table_cnt = sizeof(ch_table) / sizeof(ch_table[0]);
    int bit_table_cnt = sizeof(bit_table) / sizeof(bit_table[0]);

    int ret = 0;
    // 获取 spk 支持的的配置
    for (int i = 0; i < sr_table_cnt; i++) {
        for (int j = 0; j < ch_table_cnt; j++) {
            for (int k = 0; k < bit_table_cnt; k++) {
                ret = uac_host_query_spk_info(ch_table[j], sr_table[i], bit_table[k]);
                if (ret == 0) {
                    //获取成功
                    host_dev.host_config.spk_sr = sr_table[i];
                    host_dev.host_config.spk_ch = ch_table[j];
                    host_dev.host_config.spk_bit = bit_table[k];
                    y_printf(">> %s, SPK CONFIG: sr:%d, ch:%d, bit:%d\n", __func__, sr_table[i], ch_table[j], bit_table[k]);
                    goto __exit1;
                }
            }
        }
    }
    r_printf(">> %s, %d, get spk config failed!\n");
    return -1;

__exit1:
    // 获取 mic 支持的配置

    for (int i = 0; i < sr_table_cnt; i++) {
        for (int j = 0; j < ch_table_cnt; j++) {
            for (int k = 0; k < bit_table_cnt; k++) {
                ret = uac_host_query_mic_info(ch_table[j], sr_table[i], bit_table[k]);
                if (ret == 0) {
                    //获取成功
                    host_dev.host_config.mic_sr = sr_table[i];
                    host_dev.host_config.mic_ch = ch_table[j];
                    host_dev.host_config.mic_bit = bit_table[k];
                    y_printf(">> %s, MIC CONFIG: sr:%d, ch:%d, bit:%d\n", __func__, sr_table[i], ch_table[j], bit_table[k]);
                    goto __exit2;
                }
            }
        }
    }
    r_printf(">> %s, %d, get mic config failed!\n");
    return -1;

__exit2:
    //获取 spk 和 mic的参数成功
    return 0;
}

/*
 * 打开usb master，获取usb id，初始化数据流，创建usb spk任务
 */
int audio_usb_host_init(const char *audio)
{
    g_printf("\n----- USB Master ONLINE -----\n");

    /* 获取 usb_id */
    if (host_dev.get_usb_host_info == 0) {
        host_dev.usb_id = (audio[5] - '0');	// usb id
        int ret = audio_usb_host_config_init();
        if (ret != 0) {
            log_error("%s, %d, audio_usb_host_config_init failed!\n");
            host_dev.state = USB_AUDIO_STATE_OFFLINE;
            return -1;
        }
        host_dev.get_usb_host_info = 1;
    }

    host_dev.state = USB_AUDIO_STATE_ONLINE;
    return 0;
}


void usbMic_resume(void *priv)
{
    if (host_dev.usbMic_dec_hdl && host_dev.usbMic_dec_hdl->start == 1) {
        if (host_dev.usbMic_dec_hdl->need_resume) {
            audio_decoder_resume(&host_dev.usbMic_dec_hdl->pcm_dec.decoder);
            host_dev.usbMic_dec_hdl->need_resume = 0;
        }
    } else if (host_dev.usbMic && host_dev.usbMic->start == 1) {
        os_sem_post(&host_dev.usbMic->mic_sem);
    }
}
//******************* ************************ *********************//
//*******************  usb Mic 单独打开和关闭  *********************//
//******************* ************************ *********************//

// usb Mic 拿到数据后变采样，给外部注册进来的函数拿去使用

extern u32 uac_host_read_mic_cbuf(u8 *buf, u32 len);

/* 如果设置了回调，那么mic的数据会给到回调函数，
   如果没有设置回调，那么mic的数据会写到cbuf中，需外部自己获取
   */
static int audio_usb_mic_src_output(void *priv, s16 *data, int len)
{
    u32 wlen = 0;
    if (host_dev.state == USB_AUDIO_STATE_ONLINE && host_dev.usbMic->start == 1) {
        // 如果有注册了回调，则调用回调函数
        // 如果没有注册回调函数，则写到cbuf里
        if (host_dev.usbMic->out.func) {
            wlen = host_dev.usbMic->out.func(host_dev.usbMic->out.priv, data, len);
        } else {
            wlen = cbuf_write(&host_dev.usbMic->src_cbuffer, data, len);
        }
    }
    return len;
}


/* 初始化变采样模块 */
static void audio_usb_mic_hw_src_init(int in_sr, int out_sr)
{
    u8 in_channel = 1;		//usb mic 已变为单声道
    u8 output_channel = 2;

    /* 打开 src 模块 */
    audio_hw_src_open(&host_dev.usbMic->hw_src, in_channel, output_channel);
    audio_hw_src_set_rate(&host_dev.usbMic->hw_src, in_sr, out_sr);
    audio_src_set_output_handler(&host_dev.usbMic->hw_src, NULL, (int (*)(void *, void *, int))audio_usb_mic_src_output);
}

/* 停止并关闭 变采样模块 */
static void audio_usb_mic_hw_src_close(void)
{
    if (host_dev.usbMic) {
        audio_hw_src_stop(&host_dev.usbMic->hw_src);
        audio_hw_src_close(&host_dev.usbMic->hw_src);
    }
}

static void audio_host_mic_task_deal(void *priv)
{
    y_printf("=== Enter task : %s === \n", __func__);
    while (1) {
        os_sem_pend(&host_dev.usbMic->mic_sem, 0);
        if (host_dev.state == USB_AUDIO_STATE_ONLINE && host_dev.usbMic->start == 1 && host_dev.mic_open_flag == 1) {
            int cbuf_len = (int)uac_host_get_mic_cbuf_len();
            u32 rlen = 0;
            u32 wlen = 0;
            int read_len = 0;
            if (cbuf_len >= 256) {
                read_len = 256;
            } else if (cbuf_len > 0) {
                read_len = cbuf_len;
            }
            if (read_len) {
                /* putchar(); */
                rlen = uac_host_read_mic_cbuf(host_dev.usbMic->mic_row_buf, read_len);
                // 如果双声道数据，则转为单声道, 暂时不需要双声道的情况
                if (host_dev.usbMic->ch == 2) {
                    s16 *data = (s16 *)(host_dev.usbMic->mic_row_buf);
                    for (int i = 0; i < rlen / 2; i += 2) {
                        data[i / 2] = (data[i] / 2) + (data[i + 1] / 2);
                    }
                    rlen = rlen / 2;
                }
                wlen = audio_src_resample_write(&host_dev.usbMic->hw_src, host_dev.usbMic->mic_row_buf, rlen);
            }
            /* audio_src_resample_write(&host_dev.usbMic->hw_src, host_dev.usbMic->mic_row_buf, 0); */
            /* audio_src_base_data_flush_out(&host_dev.usbMic->hw_src); */
            if (rlen != wlen) {
                /* __mic_src_rewrite: */
                wlen = audio_src_resample_write(&host_dev.usbMic->hw_src, host_dev.usbMic->mic_row_buf, rlen);
                /* audio_src_resample_write(&host_dev.usbMic->hw_src, host_dev.usbMic->mic_row_buf, 0); */
                /* audio_src_base_data_flush_out(&host_dev.usbMic->hw_src); */
                if (rlen != wlen) {
                    /* goto __mic_src_rewrite; */
                    r_printf("rlen : %d, wlen : %d\n", rlen, wlen);
                }
            }
        } else {
            printf("Wait %s task del......\n", __func__);
            while (1) {
                os_time_dly(10000);
            }
        }
    }
}

void audio_usb_mic_set_handler(void *priv, u32(*func)(void *priv, void *data, int len))
{
    if (host_dev.usbMic) {
        host_dev.usbMic->out.priv = priv;
        host_dev.usbMic->out.func = func;
    } else {
        log_error("%s, %d, usbMic is NULL!\n", __func__, __LINE__);
    }
}

// 读取 USB MIC SRC 写到cbuf里的数据, 在没有设置回调的情况下 */
int audio_usb_mic_src_buf_read(s16 *buf, u32 len)
{
    int rlen = 0;
    if (host_dev.usbMic && host_dev.usbMic->src_buf) {
        rlen = cbuf_read(&host_dev.usbMic->src_cbuffer, buf, len);
    }
    return rlen;
}

// 开始进行mic 的传输, 打开成功返回0
int audio_usb_mic_start(int out_sr)
{
    int ret = -1;
    if (host_dev.state == USB_AUDIO_STATE_ONLINE) {
        if (host_dev.usbMic && host_dev.usbMic->start == 0 && host_dev.mic_open_flag == 1) {
            host_dev.usbMic->start = 1;

            host_dev.usbMic->mic_sr = host_dev.host_config.mic_sr;
            host_dev.usbMic->out_sr = out_sr;
            if (host_dev.usbMic->out.func == NULL) {
                //没有设置回调，则将输出的数据写到cbuf里, 初始化cbuf
                host_dev.usbMic->src_buf = malloc(USB_HOST_SRC_CBUF_LEN);
                if (!host_dev.usbMic->src_buf) {
                    r_printf("%s, %d, ptr is NULL! UAC MIC open failed!\n", __func__, __LINE__);
                    host_dev.usbMic->start = 0;
                    ret = -1;
                    goto __exit;
                }
                cbuf_init(&host_dev.usbMic->src_cbuffer, host_dev.usbMic->src_buf, USB_HOST_SRC_CBUF_LEN);
            }

            audio_usb_mic_hw_src_init(host_dev.usbMic->mic_sr, host_dev.usbMic->out_sr);
            uac_host_set_mic_hanlder(usbMic_resume, NULL);
            host_dev.usbMic->ch = host_dev.host_config.mic_ch;
            int err = uac_host_open_mic(host_dev.usb_id, host_dev.host_config.mic_ch, host_dev.usbMic->mic_sr, host_dev.host_config.mic_bit);
            if (err) {
                //mic 打开失败
                r_printf("%s, %d, UAC MIC open failed!\n", __func__, __LINE__);
                ret = -1;
                audio_usb_mic_hw_src_close();
                if (host_dev.usbMic->src_buf) {
                    free(host_dev.usbMic->src_buf);
                    host_dev.usbMic->src_buf = NULL;
                }
                host_dev.usbMic->start = 0;
                goto __exit;
            }
            ret = 0;
            g_printf(">>> %s, open success!\n", __func__);
        }
    }
__exit:
    return ret;
}

int audio_usb_mic_init(void)
{
    int ret = -1;
    if (host_dev.state == USB_AUDIO_STATE_ONLINE) {
        if (host_dev.mic_open_flag == 0 && !host_dev.usbMic) {
            struct __usbMic_hdl *mic = zalloc(sizeof(*mic));
            if (!mic) {
                log_error("%s, %d, ptr is NULL!\n", __func__, __LINE__);
                ret = -1;
                goto __err1;
            }
            host_dev.usbMic = mic;

            // 申请buf 空间
            mic->buf_size = uac_host_get_mic_cbuf_size();
            mic->mic_row_buf = malloc(mic->buf_size);
            if (!mic->mic_row_buf) {
                log_error("%s, %d, ptr is NULL!\n", __func__, __LINE__);
                ret = -1;
                goto __err2;
            } else {
                g_printf("## %s, %d, malloc success!\n", __func__, __LINE__);
            }

            // 初始化信号量
            os_sem_create(&mic->mic_sem, 0);

            // 创建任务
            task_create(audio_host_mic_task_deal, NULL, "usbMic_task");

            host_dev.mic_open_flag = 1;
            ret = 0;
            return ret;
        }
__err2:
        if (host_dev.usbMic && host_dev.usbMic->mic_row_buf) {
            free(host_dev.usbMic->mic_row_buf);
            host_dev.usbMic->mic_row_buf = NULL;
        }
        if (host_dev.usbMic) {
            free(host_dev.usbMic);
            host_dev.usbMic = NULL;
        }
    }
__err1:
    return ret;
}

void audio_usb_mic_stop(void)
{
    if (host_dev.usbMic && host_dev.usbMic->start == 1) {
        uac_host_close_mic(host_dev.usb_id);
        audio_usb_mic_hw_src_close();
        host_dev.usbMic->start = 0;
    }
}

void audio_usb_mic_release(void)
{
    if (host_dev.usbMic && host_dev.mic_open_flag == 1) {
        if (host_dev.usbMic->start == 1) {
            audio_usb_mic_stop();
        }

        host_dev.mic_open_flag = 0;

        //删除任务
        os_sem_set(&host_dev.usbMic->mic_sem, 0);
        os_sem_post(&host_dev.usbMic->mic_sem);
        os_time_dly(5);
        task_kill("usbMic_task");

        if (host_dev.usbMic->mic_row_buf) {
            free(host_dev.usbMic->mic_row_buf);
            host_dev.usbMic->mic_row_buf = NULL;
        }
        if (host_dev.usbMic->src_buf) {
            free(host_dev.usbMic->src_buf);
            host_dev.usbMic->src_buf = NULL;
        }

        os_sem_del(&host_dev.usbMic->mic_sem, 0);

        local_irq_disable();
        free(host_dev.usbMic);
        host_dev.usbMic = NULL;
        local_irq_enable();
    }
}



//******************* ************************ *********************//
//*******************  usb Mic 单独 dec->pcm   *********************//
//******************* 仅供测试，无实际应用场景 *********************//
//******************* ************************ *********************//

/* usb Mic -> pcm dec 只用于测试Mic 是否正常，正常情况下的应用，是用于通话 */

void usbMic_dec_close(void)
{
    if (host_dev.usbMic_dec_hdl) {
        if (host_dev.usbMic_dec_hdl && host_dev.usbMic_dec_hdl->start) {
            host_dev.usbMic_dec_hdl->start = 0;
            pcm_decoder_close(&host_dev.usbMic_dec_hdl->pcm_dec);
            uac_host_close_mic(host_dev.usb_id);
            audio_mixer_ch_close(&host_dev.usbMic_dec_hdl->mix_ch);
            if (host_dev.usbMic_dec_hdl->stream) {
                audio_stream_close(host_dev.usbMic_dec_hdl->stream);
                host_dev.usbMic_dec_hdl->stream = NULL;
            }
        }
    }
}

void usbMic_dec_relaese(void)
{
    if (host_dev.usbMic_dec_hdl && host_dev.usbMic_dec_hdl->start == 1) {
        usbMic_dec_close();
    }
    if (host_dev.usbMic_dec_hdl) {
        host_dev.mic_open_flag = 0;
        audio_decoder_task_del_wait(&decode_task, &host_dev.usbMic_dec_hdl->wait);
        clock_remove(AUDIO_CODING_PCM);
        local_irq_disable();
        free(host_dev.usbMic_dec_hdl);
        host_dev.usbMic_dec_hdl = NULL;
        local_irq_enable();
    }
}


static void usbMic_dec_event_handler(struct audio_decoder *decoder, int argc, int *argv)
{
    switch (argv[0]) {
    case AUDIO_DEC_EVENT_END:
        if (!host_dev.usbMic_dec_hdl) {
            break;
        }

        if (host_dev.usbMic_dec_hdl->id != argv[1]) {
            log_w("linein_dec id err : 0x%x, 0x%x \n", host_dev.usbMic_dec_hdl->id, argv[1]);
            break;
        }

        linein_dec_close();
        break;
    }
}

static int usbMic_dec_sample_read(void *hdl, void *data, int len)
{
    struct __usbMic_dec_hdl *dec = (struct __usbMic_dec_hdl *)hdl;
    int rlen = 0;
    int mic_cbuf_len = 0;
    if (dec && dec->start == 1) {
        mic_cbuf_len = uac_host_get_mic_cbuf_len();
        if (len > mic_cbuf_len) {
            len = mic_cbuf_len;
        }
        rlen = (int)uac_host_read_mic_cbuf(data, len);
    }
    if (rlen == 0) {
        dec->need_resume = 1;
    }
    return rlen;
}

static int usbMic_dec_data_handler(struct audio_stream_entry *entry,
                                   struct audio_data_frame *in,
                                   struct audio_data_frame *out)
{
    struct audio_decoder *decoder = container_of(entry, struct audio_decoder, entry);
    struct pcm_decoder *pcm_dec = container_of(decoder, struct pcm_decoder, decoder);
    struct __usbMic_dec_hdl *dec = container_of(pcm_dec, struct __usbMic_dec_hdl, pcm_dec);
    if (!dec->start) {
        return 0;
    }
    audio_stream_run(&decoder->entry, in);
    return decoder->process_len;
}

static void usbMic_dec_out_stream_resume(void *p)
{
    struct __usbMic_dec_hdl *dec = p;
    audio_decoder_resume(&dec->pcm_dec.decoder);
}

static int usbMic_dec_start(void)
{
    int err = 0;
    struct __usbMic_dec_hdl *dec = host_dev.usbMic_dec_hdl;
    struct audio_mixer *p_mixer = &mixer;

    if (!dec) {
        return -EINVAL;
    }
    if (dec->start == 1) {
        return 0;
    }

    err = pcm_decoder_open(&dec->pcm_dec, &decode_task);
    if (err) {
        goto __err1;
    }

    /* 打开 usb MIC */
    uac_host_set_mic_hanlder(usbMic_resume, NULL);
    err = uac_host_open_mic(host_dev.usb_id, host_dev.host_config.mic_ch, dec->sr, host_dev.host_config.mic_bit);
    if (err) {
        //mic 打开失败
        r_printf("%s, %d, UAC MIC open failed!\n", __func__, __LINE__);
        goto __err1;
    }

    pcm_decoder_set_event_handler(&dec->pcm_dec, usbMic_dec_event_handler, dec->id);
    pcm_decoder_set_read_data(&dec->pcm_dec, usbMic_dec_sample_read, dec);
    pcm_decoder_set_data_handler(&dec->pcm_dec, usbMic_dec_data_handler);

    // 设置叠加功能
    audio_mixer_ch_open_head(&dec->mix_ch, p_mixer);
    audio_mixer_ch_set_no_wait(&dec->mix_ch, 1, 10); // 超时自动丢数
    audio_mixer_ch_set_sample_rate(&dec->mix_ch, dec->pcm_dec.sample_rate);

    /* audio_mixer_ch_follow_resample_enable(&dec->mix_ch, dec, audio_linein_input_sample_rate); */

    // 数据流串联
    struct audio_stream_entry *entries[32] = {NULL};
    u8 entry_cnt = 0;
    entries[entry_cnt++] = &dec->pcm_dec.decoder.entry;
    entries[entry_cnt++] = &dec->mix_ch.entry;

    // 创建数据流，把所有节点连接起来
    dec->stream = audio_stream_open(dec, usbMic_dec_out_stream_resume);
    audio_stream_add_list(dec->stream, entries, entry_cnt);

    // 设置音频输出音量
    audio_output_set_start_volume(APP_AUDIO_STATE_MUSIC);

    // 开始解码
    dec->start = 1;
    err = audio_decoder_start(&dec->pcm_dec.decoder);
    if (err) {
        goto __err2;
    }
    clock_set_cur();

    return 0;

__err2:
    usbMic_dec_close();

__err1:
    usbMic_dec_relaese();
    return err;
}

/* usb mic 资源等待 */
static int usbMic_wait_res_handler(struct audio_res_wait *wait, int event)
{
    int err = 0;
    if (event == AUDIO_RES_GET) {
        // 启动解码
        err = usbMic_dec_start();
    } else if (event == AUDIO_RES_PUT) {
        // 被打断
        usbMic_dec_close();
    }

    return err;
}


/*
 * usb mic -> pcm dec 走pcm解码的方式
 * usb mic 以单独解码的形式打开
 */
int audio_usbMic_dec_open(u16 ch_num, int sr)
{
    int err = 0;
    if (host_dev.state == USB_AUDIO_STATE_ONLINE) {
        if (host_dev.usbMic_dec_hdl) {
            log_error("%s, %d, usb mic had open!\n", __func__, __LINE__);
            return -ENOMEM;
        }
        struct __usbMic_dec_hdl *dec = NULL;
        dec = zalloc(sizeof(*dec));
        if (!dec) {
            log_error("%s, %d\n", __func__, __LINE__);
            return -ENOMEM;
        }
        host_dev.usbMic_dec_hdl = dec;
        dec->id = rand32();

        dec->pcm_dec.ch_num = ch_num;

        dec->pcm_dec.output_ch_num = audio_output_channel_num();
        dec->pcm_dec.output_ch_type = audio_output_channel_type();
        dec->pcm_dec.sample_rate = sr;
        dec->sr = sr;

        dec->wait.priority = 1;
        dec->wait.preemption = 0;
        dec->wait.snatch_same_prio = 1;
        dec->wait.protect = 1;
        dec->wait.handler = usbMic_wait_res_handler;
        clock_add(AUDIO_CODING_PCM);
        err = audio_decoder_task_add_wait(&decode_task, &dec->wait);

        host_dev.mic_open_flag = 1;

        g_printf("---> %s open success!\n", __func__);

        return err;
    }
    return -1;
}


/* 这个是拔掉设备才会调用的 */
void audio_usb_host_release(void)
{
    /* g_printf("\n--- USB Master Exit ---\n"); */
    host_dev.state = USB_AUDIO_STATE_OFFLINE;

    if (host_dev.spk_open_flag) {
        uac_host_close_spk(host_dev.usb_id);
        // 将采样率设回44100
        audio_hw_src_set_rate(&host_dev.usbSpk.hw_src, host_dev.usbSpk.in_sr, host_dev.usbSpk.in_sr);

        while (host_dev.usbSpk.busy == 1) {
            os_sem_post(&host_dev.usbSpk.hw_src_sem);
            os_time_dly(2);
        }
        os_sem_set(&host_dev.usbSpk.hw_src_sem, 0);
        os_sem_post(&host_dev.usbSpk.hw_src_sem);
        os_time_dly(5);
        task_kill("spk_task");
        audio_usb_spk_hw_src_close();
        os_sem_del(&host_dev.usbSpk.hw_src_sem, 0);
        host_dev.spk_open_flag = 0;
    }
    /* uac_host_close_mic(host_dev.usb_id); */
    if (host_dev.mic_open_flag) {
        // 1 - usb Mic 是解码的方式打开的
        if (host_dev.usbMic_dec_hdl) {
            usbMic_dec_relaese();
        }

        /* 2 - usb Mic 是单独打开的 */
        if (host_dev.usbMic) {
            audio_usb_mic_release();
        }
        host_dev.mic_open_flag = 0;
    }
    host_dev.get_usb_host_info = 0;	//下次插入USB会再获取
}

#endif

