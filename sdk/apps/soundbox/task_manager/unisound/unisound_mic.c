#include "encode/encode_write.h"
#include "file_operate/file_bs_deal.h"
#include "audio_enc.h"
#include "media/audio_base.h"
#include "dev_manager.h"
#include "app_config.h"
#include "spi/nor_fs.h"
#include "rec_nor/nor_interface.h"
#include "asm/includes.h"
#include "media/includes.h"
#include "system/includes.h"
#include "asm/audio_src.h"
#include "audio_config.h"
#include "app_main.h"
#include "tone_player.h"
#include "avctp_user.h"

#if TCFG_UNISOUND_ENABLE

#include "unisound/unisound_mic.h"

#define LOG_TAG_CONST       UNISOUND
#define LOG_TAG             "[UNISOUND]"
#define LOG_ERROR_ENABLE
#define LOG_DEBUG_ENABLE
#define LOG_INFO_ENABLE
/* #define LOG_DUMP_ENABLE */
#define LOG_CLI_ENABLE
#include "debug.h"

#define THIS_TASK_NAME  				"unisound"
#define MULTIPLE_MIC_NUM    			3//固定	双麦+回采麦
#define LADC_BUF_NUM       				2
#define LADC_IRQ_POINTS     			256//512//256	/*中断点数*/ //固定
#define LADC_BUFS_SIZE      			(MULTIPLE_MIC_NUM * LADC_BUF_NUM * LADC_IRQ_POINTS)
#define MIC_DATA_TOTAL      			5//收到连续mic的n包数据再后激活线程进行处理，防止频繁进行系统调度
#define CBUF_SIZE 						(512 * MULTIPLE_MIC_NUM * MIC_DATA_TOTAL * 4)//存储mic采集到的数据流
#define TEMP_BUF_SIZE 					(512 * MULTIPLE_MIC_NUM)//存放读取cbuf的数据
#define MIC_SAMPLE_RATE     			16000
#define MIC_GAIN            			15

#if TCFG_MIC_REC_ENABLE
#define SD_PCM_BUF_SIZE					4096//每次写的数据长度
#define RAW_TASK_NAME        			"raw_data_export"
#define RAW_MIC_REC_FOLDER_NAME			"MIC_REC"
#endif

#define ASR_VERIFY
#ifdef ASR_VERIFY
void *_calloc(size_t nmemb, size_t size)
{
    void *p = malloc(nmemb * size);
    memset(p, 0, nmemb * size);
    return p;
}
void *_calloc_r(void *r, size_t a, size_t b)
{
    return _calloc(a, b);
}
#endif

extern struct audio_adc_hdl adc_hdl;
typedef struct {
    struct audio_adc_output_hdl output;
    struct adc_linein_ch linein_ch;
    struct adc_mic_ch mic_ch;
    s16 adc_buf[LADC_BUFS_SIZE];    //align 4Bytes
    s16 temp_buf[LADC_IRQ_POINTS * MULTIPLE_MIC_NUM];
} audio_adc_t;
static audio_adc_t *ladc_var = NULL;

typedef struct {
    u8 *task_name;			 //线程名称
    OS_SEM sem_unisound_run; //激活线程的信号量
    cbuffer_t *data_w_cbuf;  //cbuf指针
    u8 *data_buf;			 //存放mic数据流
    u8 *mic_rbuf;			 //存放从cbuf读取出的数据
    volatile u8 init_ok;
    u8 unisound_online;		 //unisound状态
    u8 siri_online;			 //siri状态
    u8 esco_online;			 //通话状态
} unisound_t;

static unisound_t info = {
    .task_name = THIS_TASK_NAME,
    .data_w_cbuf = NULL,
    .data_buf = NULL,
    .mic_rbuf = NULL,
    .esco_online = 0,
    .siri_online = 0,
    .init_ok = 0,
};
#define __this  (&info)

#if TCFG_MIC_REC_ENABLE
struct multiple_mic_rec {
    volatile u32 init_ok : 1;
        volatile u32 wait_idle : 1;
        volatile u32 start : 1;
        volatile u32 wait_stop : 1;
        OS_SEM sem_task_run;
        void *set_head_hdl;
        int (*set_head)(void *, char **head);
    };
    typedef struct {
    struct device *sd_hdl;
    u32 sd_write_sector;
    OS_SEM sd_sem;
    u8 sd_wbuf[SD_PCM_BUF_SIZE];			// 数据导出buf
    s16 ch_buf[LADC_IRQ_POINTS * 64 * 3];
    cbuffer_t *ch_cb;
} multiple_mic_t;

static multiple_mic_t dm;
static struct multiple_mic_rec *wfil_hdl = NULL;

#if TCFG_SSP_DATA_EXPORT
FILE *mic_file_hdl[1 + 1] = {0};
#else
FILE *mic_file_hdl[1] = {0};
#endif

void multiple_mic_write_file_stop(void *hdl, u32 delay_ms);

static u32 get_creat_path_len(char *root_path, const char *folder, const char *filename)
{
    return (strlen(root_path) + strlen(folder) + strlen(filename) + strlen("/") + 1);
}

static char *create_path(char *path, char *root_path, const char *folder, const char *filename)
{
    strcat(path, root_path);
    /* strcat(path, folder); */
    /* strcat(path, "/"); */
    strcat(path, filename);
#if TCFG_NOR_REC
#elif FLASH_INSIDE_REC_ENABLE
    int index = 0;
    int last_num = -1;
    char *file_num_str;

    index = sdfile_rec_scan_ex();
    last_num = index + 1;
    file_num_str = strrchr(path, '.') - 4;

    if (last_num > 9999) {
        last_num = 0;
    }
    file_num_str[0] = last_num / 1000 + '0';
    file_num_str[1] = last_num % 1000 / 100 + '0';
    file_num_str[2] = last_num % 100 / 10 + '0';
    file_num_str[3] = last_num % 10 + '0';
#endif
    printf(">>>[test]:create path =%s\n", path);
    return path;
}

static int raw_data_export_init()
{
    dm.ch_cb = (cbuffer_t *)zalloc(sizeof(cbuffer_t));
    cbuf_init(dm.ch_cb, dm.ch_buf, sizeof(dm.ch_buf));
    return 0;
}

static int raw_data_export_run(s16 *dat, int len)
{
    int wlen = 0;
    static u32 len_total = 0;
    if (cbuf_is_write_able(dm.ch_cb, len) > len) {
        wlen = cbuf_write(dm.ch_cb, (u8 *)dat, len);
    } else {
        printf("%s, cbuf is full.\n", __func__);
    }
    if (cbuf_get_data_size(dm.ch_cb) >= 4096) {
        os_sem_post(&wfil_hdl->sem_task_run);
    }
    return 0;
}
#endif

/*----------------------------------------------------------------------------*/
/**@brief    mic初始化
  @param
  @return
  @note
 */
/*----------------------------------------------------------------------------*/
extern void adc_mic_output_handler(void *priv, s16 *data, int len);
static void unisound_multiple_mic_deal_output_demo(void *priv, s16 *data, int len);
static int unisound_open_multiple_mic_init_demo(void)
{
    u16 ladc_sr = MIC_SAMPLE_RATE;
    u8 mic_gain = MIC_GAIN;
    log_info("audio_adc_open_demo,sr:%d,mic_gain:%d", ladc_sr, mic_gain);
    if (ladc_var) {
        log_error("ladc already open");
        return -1;
    }
    ladc_var = zalloc(sizeof(audio_adc_t));
    if (ladc_var) {
        audio_adc_mic_open(&ladc_var->mic_ch, TCFG_UNISOUND_ADC_MIC_CHA, &adc_hdl);
        audio_adc_mic_set_gain(&ladc_var->mic_ch, mic_gain);
        audio_adc_mic1_open(&ladc_var->mic_ch, TCFG_UNISOUND_ADC_MIC_CHA, &adc_hdl);
        audio_adc_mic1_set_gain(&ladc_var->mic_ch, mic_gain);
        audio_adc_mic2_open(&ladc_var->mic_ch, TCFG_UNISOUND_ADC_MIC_CHA, &adc_hdl);
        audio_adc_mic2_set_gain(&ladc_var->mic_ch, 3);
        audio_adc_mic_set_sample_rate(&ladc_var->mic_ch, ladc_sr);
        audio_adc_mic_set_buffs(&ladc_var->mic_ch, ladc_var->adc_buf,  LADC_IRQ_POINTS * 2, LADC_BUF_NUM);
        ladc_var->output.handler = unisound_multiple_mic_deal_output_demo;
        ladc_var->output.priv = &adc_hdl;
        audio_adc_add_output_handler(&adc_hdl, &ladc_var->output);
        audio_adc_mic_start(&ladc_var->mic_ch);

        SFR(JL_ADDA->DAA_CON1, 0, 3, 7);
        SFR(JL_ADDA->DAA_CON1, 4, 3, 7);
        return 0;
    } else {
        log_error("mic zalloc error");
        return -1;
    }
}

/*----------------------------------------------------------------------------*/
/**@brief
  @param
  @return
  @note	 将mic采集到的数据写入cbuf，并激活unisound对数据进行处理
 */
/*----------------------------------------------------------------------------*/
static int unisound_mic_deal_run(s16 *dat, int len)
{
    int wlen = 0;
    static u8 write_cnt = 0;
    if (cbuf_is_write_able(__this->data_w_cbuf, len)) {
        wlen = cbuf_write(__this->data_w_cbuf, dat, len);
        write_cnt ++;
    }
    if (wlen == 0) {
        log_error("data_w_cbuf_full");
    }
    if (write_cnt == MIC_DATA_TOTAL) {
        write_cnt = 0;
        os_sem_post(&(__this->sem_unisound_run));
    }
    return 0;
}

/*----------------------------------------------------------------------------*/
/**@brief    mic中断回调处理函数
  @param
  @return
  @note
 */
/*----------------------------------------------------------------------------*/
s16 mic0[LADC_IRQ_POINTS];
static void unisound_multiple_mic_deal_output_demo(void *priv, s16 *data, int len)
{
    if (ladc_var == NULL) {
        return;
    }

#if (TCFG_MIC_REC_ENABLE && TCFG_RAW_DATA_EXPORT)
    if (wfil_hdl->start) {
        raw_data_export_run(data, len * MULTIPLE_MIC_NUM);
    }
#endif

    if (__this->esco_online || __this->siri_online) {
        for (u16 i = 0; i < (len >> 1); i++) {
            mic0[i] = data[i * 3];
        }
        adc_mic_output_handler(NULL, mic0, len);
        putchar('^');
    }

    if (__this->unisound_online) {
        unisound_mic_deal_run(data, len * MULTIPLE_MIC_NUM);//线程处理数据
        //log_info("len=%d\n",len);
        putchar('*');
    }
}

/*----------------------------------------------------------------------------*/
/**@brief    关闭麦
  @param
  @return
  @note
 */
/*----------------------------------------------------------------------------*/
static void unisound_multiple_mic_close_demo()
{
    if (ladc_var) {
        audio_adc_del_output_handler(&adc_hdl, &ladc_var->output);
        extern int audio_adc_mic_close(struct adc_mic_ch * mic);
        audio_adc_mic_close(&ladc_var->mic_ch);
        free(ladc_var);
        ladc_var = NULL;
    }
}

/*----------------------------------------------------------------------------*/
/**@brief    关闭unisound模块
  @param
  @return
  @note	 关闭mic，关闭语音识别，释放相关资源
 */
/*----------------------------------------------------------------------------*/
void unisound_uninit(void)
{
    log_info(">>>>>>>>>>>>>>>unisound_uninit<<<<<<<<<<<<<<<<<");

    __this->init_ok = 0;
    //kill unisound
    task_kill(__this->task_name);

    //关mic
    unisound_multiple_mic_close_demo();

#if TCFG_MIC_REC_ENABLE
    unisound_multiple_mic_rec_uninit();
#endif

    //关闭语音识别
    uniAsrUninit();

    if (__this->data_w_cbuf) {
        free(__this->data_w_cbuf);
        __this->data_w_cbuf = NULL;
    }
    if (__this->data_buf) {
        free(__this->data_buf);
        __this->data_buf = NULL;
    }
    if (__this->mic_rbuf) {
        free(__this->mic_rbuf);
        __this->mic_rbuf = NULL;
    }
    /* mem_stats(); */
}

/*----------------------------------------------------------------------------*/
/**@brief    unisound线程入口函数
  @param
  @return
  @note	 对mic采集到的数据进行语音算法处理
 */
/*----------------------------------------------------------------------------*/
static void unisound_mic_data_deal_task(void *p)
{
    u8 pend;
    int ret;
    float process_time = 0;
    u32 process_begin, process_end = 0;

    while (1) {
        pend = 1;
        for (u8 i = 0; i < MIC_DATA_TOTAL; i++) {
            ret = cbuf_get_data_size(__this->data_w_cbuf);
            if (ret >= TEMP_BUF_SIZE) {
                memset(__this->mic_rbuf, 0x0, TEMP_BUF_SIZE);
                ret = cbuf_read(__this->data_w_cbuf, __this->mic_rbuf, TEMP_BUF_SIZE);
                process_begin = timer_get_ms();
                uniAsrProcess(__this->mic_rbuf, TEMP_BUF_SIZE);//语音数据处理
                process_end = timer_get_ms();
                /* log_info("uniAsrProcess time %f", (float)(process_end - process_begin) / 16); */
            }
        }

        if (pend) {
            os_sem_pend(&(__this->sem_unisound_run), 0);
        }
    }
    while (1) {
        os_time_dly(100);
    }
}

/*----------------------------------------------------------------------------*/
/**@brief    unisound资源申请函数
  @param
  @return   0:成功， 1:失败
  @note
 */
/*----------------------------------------------------------------------------*/
static int unisound_buf_init()
{
    __this->data_buf = (u8 *)zalloc(CBUF_SIZE);
    if (__this->data_buf == NULL) {
        log_error("data_buf malloc error");
        return 1;
    }

    __this->data_w_cbuf = (cbuffer_t *)zalloc(sizeof(cbuffer_t));
    if (__this->data_w_cbuf == NULL) {
        free(__this->data_buf);
        __this->data_buf = NULL;
        log_error("data_w_cbuf malloc err!");
        return 1;
    }

    __this->mic_rbuf = (u8 *)zalloc(TEMP_BUF_SIZE);
    if (__this->mic_rbuf == NULL) {
        free(__this->data_buf);
        __this->data_buf = NULL;
        free(__this->data_w_cbuf);
        __this->data_w_cbuf = NULL;
        log_error("mic_rbuf malloc err!");
        return 1;
    }

    cbuf_init(__this->data_w_cbuf, __this->data_buf, CBUF_SIZE);
    log_info("unisound buf init success");
    return 0;
}

/*----------------------------------------------------------------------------*/
/**@brief    unisound模块初始化
  @param
  @return   0:成功， 1:失败
  @note
 */
/*----------------------------------------------------------------------------*/
int unisound_init(void)
{
    /* mem_stats(); */
    log_info(">>>>>>>>>>>>>>>unisound_init<<<<<<<<<<<<<<<<<");

    //打开语音识别，将音乐暂停
    user_send_cmd_prepare(USER_CTRL_AVCTP_OPID_PAUSE, 0, NULL);

    //初始化cbuf
    if (unisound_buf_init()) {
        log_info("unisound buf init error");
        return 1;
    }

    //语音识别初始化
    if (uniAsrInit()) {
        goto _exit;
    }

    //创建信号量
    os_sem_create(&(__this->sem_unisound_run), 0);

    //创建unisound算法线程
    int err = task_create(unisound_mic_data_deal_task, NULL, __this->task_name);
    if (err != OS_NO_ERR) {
        log_error("unisound task create error");
        goto _exit;
    }

    //mic初始化
    int ret = unisound_open_multiple_mic_init_demo();
    if (ret < 0) {
        log_error("unisound mic open error");
        goto _exit;
    }
    __this->init_ok = 1;
    return 0;

_exit:
    unisound_uninit();
    log_error("unidound init error");
    return 1;
}

u8 unisound_set_online(u8 online)
{
    if (__this->init_ok) {
        __this->unisound_online = online;
    } else {
        __this->unisound_online = 0;
        log_info("unisound start error");
    }
    return 0;
}

u8 get_unisound_online(void)
{
    return (__this->unisound_online);
}

u8 unisound_set_esco_online(u8 online)
{
    __this->esco_online = online;
    return 0;
}

u8 unisound_set_siri_online(u8 online)
{
    __this->siri_online = online;
    return 0;
}

#if TCFG_MIC_REC_ENABLE
#define APP_IO_DEBUG_0(i,x)       //{JL_PORT##i->DIR &= ~BIT(x), JL_PORT##i->OUT &= ~BIT(x);}
#define APP_IO_DEBUG_1(i,x)       //{JL_PORT##i->DIR &= ~BIT(x), JL_PORT##i->OUT |= BIT(x);}
static void multiple_mic_rec_task(void *p)
{
    u8 i;
    u8 pend = 1;
    int ret, len;
    struct multiple_mic_rec *wfil = p;

    while (1) {
        if (pend) {
            os_sem_pend(&wfil->sem_task_run, 0);
        }
        pend = 1;
        if (!wfil->init_ok) {
            wfil->wait_idle = 0;
            wfil->start = 0;
            wfil->wait_stop = 0;
            break;
        }
        if (!wfil->start) {
            continue;
        }
        ret = cbuf_get_data_size(dm.ch_cb);
        if (ret >= sizeof(dm.sd_wbuf)) {
            len = cbuf_read(dm.ch_cb, dm.sd_wbuf, sizeof(dm.sd_wbuf));
            if (len == sizeof(dm.sd_wbuf)) {
                pend = 0;
                putchar(',');
                APP_IO_DEBUG_1(A, 5);
                ret = fwrite(mic_file_hdl[0], dm.sd_wbuf, len);
                APP_IO_DEBUG_0(A, 5);
                putchar('.');
                if (ret != len) {
                    printf("mic_file_hdl write err\n");
                }
            }
        }
    }
    while (1) {
        os_time_dly(100);
    }
}

FILE *multiple_mic_rec_file_open(char *logo, const char *folder, const char *filename)
{
    if (!logo || !folder || !filename) {
        return NULL;
    }

    struct __dev *dev = dev_manager_find_spec(logo, 0);
    if (!dev) {
        return NULL;
    }

    char *root_path = dev_manager_get_root_path(dev);
    char *temp_filename = zalloc(strlen(filename) + 5);
    if (temp_filename == NULL) {
        return NULL;
    }
    strcat(temp_filename, filename);
    strcat(temp_filename, ".raw");
    printf("temp_filename:%s\n", temp_filename);

    u32 path_len = get_creat_path_len(root_path, folder, temp_filename);
    char *path = zalloc(path_len);
    if (path == NULL) {
        free(temp_filename);
        return NULL;
    }
    create_path(path, root_path, folder, temp_filename);
    printf("path:%s\n", path);
    FILE *wfile_hdl = fopen(path, "w+");
    free(temp_filename);
    free(path);
    if (!wfile_hdl) {
        return NULL;
    }

    return wfile_hdl;
}

int unisound_multiple_mic_rec_uninit()
{
    u8 i;
    int f_len;
    struct vfs_attr attr = {0};
    log_info(">>>>>>>>>>>multiple_mic_rec_uninit<<<<<<<<<<\n");

    multiple_mic_write_file_stop(wfil_hdl, 1000);

    if (wfil_hdl->init_ok) {
        wfil_hdl->wait_idle = 1;
        wfil_hdl->init_ok = 0;
        os_sem_set(&wfil_hdl->sem_task_run, 0);
        os_sem_post(&wfil_hdl->sem_task_run);
        while (wfil_hdl->wait_idle) {
            os_time_dly(1);
        }
        task_kill(RAW_TASK_NAME);
    }

#if TCFG_SSP_DATA_EXPORT
    for (i = 0; i < 1 + 1; i++) {
#else
    for (i = 0; i < 1; i++) {
#endif
        if (mic_file_hdl[i]) {
            f_len = fpos(mic_file_hdl[i]);
            if (wfil_hdl->set_head) {
                char *head;
                int len = wfil_hdl->set_head(wfil_hdl->set_head_hdl, &head);
                if (f_len <= len) {
                    fdelete(mic_file_hdl[i]);
                    mic_file_hdl[i] = NULL;
                    continue;
                }
                if (len) {
                    fseek(mic_file_hdl[i], 0, SEEK_SET);
                    fwrite(mic_file_hdl[i], head, len);
                }
            }
            fseek(mic_file_hdl[i], f_len, SEEK_SET);
            /* fget_attrs(mic_file_hdl[i], &attr); */
            fclose(mic_file_hdl[i]);
            mic_file_hdl[i] = NULL;
        }
    }

    if (wfil_hdl) {
        free(wfil_hdl);
        wfil_hdl = NULL;
    }
#if TCFG_SSP_DATA_EXPORT
    for (i = 0; i < 1 + 1; i++) {
#else
    for (i = 0; i < 1; i++) {
#endif
        log_info("mic%d_file_hdl:0x%x\n", i, mic_file_hdl[i]);
    }

    if (dm.ch_cb) {
        free(dm.ch_cb);
        dm.ch_cb = NULL;
    }
    return 0;
}

int unisound_multiple_mic_rec_init()
{
    char folder[] = {RAW_MIC_REC_FOLDER_NAME};         //录音文件夹名称
    char mic_filename[] = {"mic****"};     //录音文件名，不需要加后缀，录音接口会根据编码格式添加后缀
    char *logo = dev_manager_get_phy_logo(dev_manager_find_active(0));//普通设备录音，获取最后活动设备
    if (wfil_hdl) {
        return -1;
    }

    wfil_hdl = zalloc(sizeof(struct multiple_mic_rec));
    if (!wfil_hdl) {
        return -1;
    }

    //打开mic*录音文件句柄
#if TCFG_SSP_DATA_EXPORT
    for (u8 i = 0; i < 1 + 1; i++) {
#else
    for (u8 i = 0; i < 1; i++) {
#endif
        mic_file_hdl[i] = multiple_mic_rec_file_open(logo, folder, mic_filename);
        if (!mic_file_hdl[i]) {
            log_info("mic%d rec hdl open fail\n", i);
            goto _exit;
        }
    }

    raw_data_export_init();

    os_sem_create(&wfil_hdl->sem_task_run, 0);
    int err;
    err = task_create(multiple_mic_rec_task, wfil_hdl, RAW_TASK_NAME);
    if (err != OS_NO_ERR) {
        log_error("task create err ");
        goto _exit;
    }
    wfil_hdl->init_ok = 1;

    //启动写卡
    multiple_mic_write_file_start(wfil_hdl);

    return 0;

_exit:
    unisound_multiple_mic_rec_uninit();
    printf("multiple mic rec init fail\n");
    return -1;
}

u8 unisound_get_rec_file_init_ok(void)
{
    return (wfil_hdl->init_ok);
}

int multiple_mic_write_file_resume(void *hdl)
{
    struct multiple_mic_rec *wfil = hdl;
    if (wfil->start) {
        os_sem_set(&wfil->sem_task_run, 0);
        os_sem_post(&wfil->sem_task_run);
        return 0;
    }
    return -1;
}

int multiple_mic_write_file_start(void *hdl)
{
    struct multiple_mic_rec *wfil = hdl;

    if (wfil->init_ok) {
        wfil->start = 1;
        return 0;
    }
    return -1;
}

void multiple_mic_write_file_stop(void *hdl, u32 delay_ms)
{
    struct multiple_mic_rec *wfil = hdl;

    if (wfil->start) {
        wfil->wait_stop = 1;
        u32 dly = 0;
        do {
            if (!wfil->wait_stop) {
                break;
            }
            os_time_dly(1);
            dly += 10;
        } while (dly < delay_ms);
        wfil->start = 0;
    }
}
#endif // TCFG_MIC_REC_ENABLE

#endif // TCFG_UNISOUND_ENABLE

#if 0
void rgb_led_status_set(u8 red, u8 green, u8 blue)
{
    gpio_direction_output(TCFG_COLORLED_RED_PIN, red);
    gpio_direction_output(TCFG_COLORLED_GREEN_PIN, green);
    gpio_direction_output(TCFG_COLORLED_BLUE_PIN, blue);
}

void unisound_ui_show(u8 tone_flag)
{
    rgb_led_status_set(0, 1, 0);
    if (tone_flag) {
        tone_play_with_callback_by_name(tone_table[IDEX_TONE_UNISOUND], 1, NULL, NULL);
    }
}
#endif
