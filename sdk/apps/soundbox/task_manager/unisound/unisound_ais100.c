/*
 * Copyright 2020 Unisound AI Technology Co., Ltd.
 * Author: Hao Peng
 * All Rights Reserved.
 */

#include "system/includes.h"
#include "unisound/ual_aik_config.h"
#include "unisound/ual_aik_event.h"
#include "unisound/ual_aik_id.h"
#include "unisound/ual_aik_mode.h"
#include "unisound/ual_aik.h"
#include "key_event_deal.h"
#include "avctp_user.h"
#include "tone_player.h"
#include "unisound/unisound_mic.h"
#include "file_operate/file_bs_deal.h"
#include "spi/nor_fs.h"

#if TCFG_UNISOUND_ENABLE

#include "unisound/grammar.h"
#include "unisound/model_370k.h"

#define LOG_TAG_CONST       UNISOUND
#define LOG_TAG             "[UNISOUND]"
#define LOG_ERROR_ENABLE
#define LOG_DEBUG_ENABLE
#define LOG_INFO_ENABLE
/* #define LOG_DUMP_ENABLE */
#define LOG_CLI_ENABLE
#include "debug.h"

#define UNI_MIC_NUM          (2)
#define UNI_AUDIO_CHS        (UNI_MIC_NUM + 1)

const char *kws_unit[] = {
    "prev_",
    "next_",
    "pause_",
    "play_",
    "vol_up_",
    "vol_down_",
    "wakeup_",
    "open_scr_",
    "close_scr_",
    "call_father_",
    "call_mother_",
    "call_",
    "open_video_",
    "close_video_",
    "take_pic_",
    "playback_",
    "open_msg_",
};

#if TCFG_KWS_TEST_ENABLE
static u16 kws_cnt[KWS_NUM] = {0};
static u16 kws_log_timer = 0;
const char *kws_unit_log[] = {
    "上一首",
    "下一首",
    "暂停播放",
    "播放音乐",
    "增加音量",
    "降低音量",
    "手机唤醒词"
};
void clear_kws_cnt(void)
{
    for (u8 i = 0; i < KWS_NUM; i++) {
        kws_cnt[i] = 0;
    }
}
static void kws_log_list(void)
{
    log_info("   unit   |  cnt");
    for (u8 i = 0; i < KWS_NUM; i++) {
        log_info("  %s  |   %d", kws_unit_log[i], kws_cnt[i]);
    }
}
#endif

/* AIS100 配置，应用可结合自身的配置管理模块来设计结构体 */
typedef struct Ais100Cfg {
    SlaveConfig slave;
    KwsConfig kws;      // asr 模块配置
    SspConfig ssp;      // ssp 模块配置
} Ais100Cfg;

#if (TCFG_MIC_REC_ENABLE && TCFG_SSP_DATA_EXPORT)
#define SSP_TASK_NAME  	"ssp_data_export"
extern FILE *mic_file_hdl[2];
#define SSP_CBUF_SIZE	(512*64)
#define SSP_RBUF_SIZE	(512*8)
extern u8 get_rec_file_init_ok(void);
typedef struct {
    u8 rec_init_ok;
    u8 *task_name;			 		//线程名称
    OS_SEM sem_sspdata_export_run;  //激活线程的信号量
    cbuffer_t *sspdata_w_cbuf;  	//cbuf指针
    u8 *sspdata_buf;				//存放mic数据流
    u8 *sspdata_rbuf;			 	//存放从cbuf读取出的数据
} unisound_ssp;

static unisound_ssp info = {
    .rec_init_ok = 0,
    .task_name = SSP_TASK_NAME,
    .sspdata_w_cbuf = NULL,
    .sspdata_buf = NULL,
    .sspdata_rbuf = NULL,
};
#define __this  (&info)

#define APP_IO_DEBUG_0(i,x)       //{JL_PORT##i->DIR &= ~BIT(x), JL_PORT##i->OUT &= ~BIT(x);}
#define APP_IO_DEBUG_1(i,x)       //{JL_PORT##i->DIR &= ~BIT(x), JL_PORT##i->OUT |= BIT(x);}

static void ssp_data_export_task(void *p)
{
    u8 pend;
    int ret, len;

    while (1) {
        //检查导出文件是否初始化完成
        if (!unisound_get_rec_file_init_ok()) {
            log_info("wait file init...");
            break;
        }
        pend = 1;
        ret = cbuf_get_data_size(__this->sspdata_w_cbuf);
        if (ret >= SSP_RBUF_SIZE) {
            len = cbuf_read(__this->sspdata_w_cbuf, __this->sspdata_rbuf, SSP_RBUF_SIZE);
            if (len == SSP_RBUF_SIZE) {
                pend = 0;
                //APP_IO_DEBUG_1(A, 5);
                ret = fwrite(mic_file_hdl[1], __this->sspdata_rbuf, SSP_RBUF_SIZE);
                //APP_IO_DEBUG_0(A, 5);
                if (ret != len) {
                    log_error("ssp data export err\n");
                }
                putchar('~');
            }
        }
        if (pend) {
            os_sem_pend(&(__this->sem_sspdata_export_run), 0);
        }
    }
    while (1) {
        os_time_dly(100);
    }
}

void ssp_data_export_uninit(void)
{
    __this->rec_init_ok = 0;
    if (__this->sspdata_w_cbuf) {
        free(__this->sspdata_w_cbuf);
        __this->sspdata_w_cbuf = NULL;
    }
    if (__this->sspdata_buf) {
        free(__this->sspdata_buf);
        __this->sspdata_buf = NULL;
    }
    if (__this->sspdata_rbuf) {
        free(__this->sspdata_rbuf);
        __this->sspdata_rbuf = NULL;
    }
    task_kill(__this->task_name);
}

u8 ssp_data_export_init(void)
{
    u8 err = 0;
    __this->sspdata_buf = (u8 *)zalloc(SSP_CBUF_SIZE);
    if (__this->sspdata_buf == NULL) {
        log_error("sspdata_buf malloc err!");
        return 1;
    }
    __this->sspdata_w_cbuf = (cbuffer_t *)zalloc(sizeof(cbuffer_t));
    if (__this->sspdata_w_cbuf == NULL) {
        free(__this->sspdata_buf);
        __this->sspdata_buf = NULL;
        log_error("sspdata_buf malloc err!");
        return 1;
    }
    __this->sspdata_rbuf = (u8 *)zalloc(SSP_RBUF_SIZE);
    if (__this->sspdata_rbuf == NULL) {
        free(__this->sspdata_w_cbuf);
        __this->sspdata_w_cbuf = NULL;
        free(__this->sspdata_buf);
        __this->sspdata_buf = NULL;
        log_error("sspdata_rbuf malloc err!");
        return 1;
    }
    cbuf_init(__this->sspdata_w_cbuf, __this->sspdata_buf, SSP_CBUF_SIZE);

    os_sem_create(&(__this->sem_sspdata_export_run), 0);
    err = task_create(ssp_data_export_task, NULL, __this->task_name);
    if (err != OS_NO_ERR) {
        log_error("task create err ");
        ssp_data_export_uninit();
        return 1;
    }
    __this->rec_init_ok = 1;
    return err;
}

static void SspUploader(int32_t index, int32_t doa, void *data, int32_t len)
{
    int wlen = 0;
    static u8 ssp_w_cnt = 0;
    if (unisound_get_rec_file_init_ok() && (__this->rec_init_ok == 1)) {
        if (cbuf_is_write_able(__this->sspdata_w_cbuf, len)) {
            wlen = cbuf_write(__this->sspdata_w_cbuf, (u8 *)data, len);
        }
        if (wlen == 0) {
            log_error("sspdata_w_cbuf full");
            os_sem_set(&(__this->sem_sspdata_export_run), 0);
            os_sem_post(&(__this->sem_sspdata_export_run));
        }
        ssp_w_cnt++;
        if (ssp_w_cnt == 8) {
            ssp_w_cnt = 0;
            os_sem_set(&(__this->sem_sspdata_export_run), 0);
            os_sem_post(&(__this->sem_sspdata_export_run));
        }
    }
}
#else
static void SspUploader(int32_t index, int32_t doa, void *data, int32_t len)
{
    return;
}
#endif

/* 识别词阈值表 */
static KwsFaValue global_cmd_fa[] = {{0.02, 11.8}, {0.05, 10.85}, {0.1, 10.05}, {0.2, -1.69}, {0.3, -2.32}};
static KwsFaTable global_cmd_table = {
    .nums = sizeof(global_cmd_fa) / sizeof(global_cmd_fa[0]),
    .value = global_cmd_fa
};
/* 定制命令词条（当词表中个别词性能不佳时，可以通过该列表单独调整） */
static KwsCustomCmd global_cmd_custom[] = {
    {.cmd = "prev_", .fa = 0.2},
    {.cmd = "next_", .fa = 0.2},
    {.cmd = "pause_", .fa = 0.2},
    {.cmd = "play_", .fa = 0.2},
    {.cmd = "vol_up_", .fa = 0.2},
    {.cmd = "vol_down_", .fa = 0.2},
    {.cmd = "wakeup_", .fa = 0.3}
};
/* 识别功能配置 */
static KwsCmdCfg global_cmd_cfg = {
    .table = &global_cmd_table,
    .fa = 0.2,     // 通用的 FA 一般配置为每十小时 2 次
    .timeout = 10, // 识别超时时间，单位秒，每 10 秒无结果会上报一个 timeout，应用如果不需要超时可以忽略 timeout 事件
    .custom_nums = sizeof(global_cmd_custom) / sizeof(global_cmd_custom[0]),
    .cmd_custom = &global_cmd_custom[0]
};

long uni_get_ms(void)
{
    u32 msec = 0;
    msec = timer_get_ms();
    return msec;
}

static int vad_flag = 0;
static float anker_mic_position[] = {0.0, 0.0, 0.01854f, 0.0};
// static float anker_voice_direction = 170.0f;
static float anker_voice_direction = 0.0f;

// 识别功能配置
static Ais100Cfg global_ais_config = {
    .slave = {
        .chs = UNI_AUDIO_CHS
    },

    /* 降噪模块配置 */
    .ssp = {  							// SSP 相关配置不需要用户修改
        .total_chs = UNI_AUDIO_CHS,     // 输入的总通道数
        .mode = 14,                     // SSP 工作模式
        .mic_orders = {0, 1},           // Mic 数据顺序
        .ref_orders = {2},              // Ref 数据顺序
        .data_uploader = SspUploader
    },// 数据回调，获取降噪后数据

    /* KWS 模块配置 */
    .kws = {
        .am = (char *)acoutstic_model, // 声学模型数据，model_183k.h
        .grammar = (char *)grammar,    // 语法模型数据，grammar.h
        .auto_clear_when_start = 1,    // 启动时重置超时计数
        .auto_clear_when_result = 1,   // 有结果时重置超时计数
        .shared_size = 0,              // 共享内存大小，不需要则配为 0
        .shared_buffer = NULL,         // 共享内存地址指针
        .wkr_cfg = NULL,               // 唤醒功能配置
        .cmd_cfg = &global_cmd_cfg
    }
};

static void tone_play_end(void *priv, int flag)
{
    app_task_put_key_msg((int)priv, 0); //msg:key event ；value:key_value
}

/*----------------------------------------------------------------------------*/
/**@brief    语音识别结果回调函数
  @param
  @return
  @note
 */
/*----------------------------------------------------------------------------*/
static void AisEventCb(AikEvent event, void *args)
{
    static int vad_start, vad_end;
    int err = 0;
    u8 kws_index = 0;
    switch (event) {
    case AIK_EVENT_KWS_COMMAND: {
        AikEventKwsArgs *kws = (AikEventKwsArgs *)args;
        for (kws_index = 0; kws_index < KWS_NUM; kws_index++) {
            if (strncmp(kws_unit[kws_index], kws->word, strlen(kws_unit[kws_index])) == 0) {
#if TCFG_KWS_TEST_ENABLE
                kws_cnt[kws_index]++;
#endif
                /* printf("%s, %s, %d, %d\n", kws->word, kws_unit[kws_index], kws_index, strlen(kws_unit[kws_index])); */
                break;
            }
        }
        switch (kws_index) {
        case PREV_MUSIC:
            app_task_put_key_msg(KEY_MUSIC_PREV, 0);
            break;
        case NEXT_MUSIC:
            app_task_put_key_msg(KEY_MUSIC_NEXT, 0);
            break;
        case PP_MUSIC:
            if (a2dp_get_status() != BT_MUSIC_STATUS_SUSPENDING) {
                app_task_put_key_msg(KEY_MUSIC_PP, 0);
            }
            break;
        case PLAY_MUSIC:
            if (a2dp_get_status() != BT_MUSIC_STATUS_STARTING) {
                app_task_put_key_msg(KEY_MUSIC_PP, 0);
            }
            break;
        case UP_VOL:
            /* err = tone_play_with_callback_by_name(tone_table[IDEX_TONE_UNISOUND], 1, tone_play_end, (void *)KEY_VOL_UP); */
            /* if (err) { */
            app_task_put_key_msg(KEY_VOL_UP, 0);
            /* } */
            break;
        case DOWN_VOL:
            /* err = tone_play_with_callback_by_name(tone_table[IDEX_TONE_UNISOUND], 1, tone_play_end, (void *)KEY_VOL_DOWN); */
            /* if (err) { */
            app_task_put_key_msg(KEY_VOL_DOWN, 0);
            /* } */
            break;
        case SIRI_OPEN:
            app_task_put_key_msg(KEY_OPEN_SIRI, 0);
            break;
        case OPEN_SCR:    //打开屏幕
            r_printf("OPEN_SCR");
            break;
        case CLOSE_SCR:
            r_printf("CLOSE_SCR");
            break;
        case CALL:
            r_printf("CALL");
            break;
        case CALL_FATHER:
            r_printf("CALL_FATHER");
            break;
        case CALL_MOTHER:
            r_printf("CALL_MOTHER");
            break;
        case OPEN_VIDEO:
            r_printf("OPEN_VIDEO");
            break;
        case CLOSE_VIDEO:
            r_printf("CLOSE_VIDEO");
            break;
        case TAKE_PIC:
            r_printf("TAKE_PIC");
            break;
        case PLAYBACK:
            r_printf("PLAYBACK");
            break;
        case OPEN_MSG:
            r_printf("OPEN_MSG");
            break;
        default:
            log_info("Not This Command!\n");
            break;
        }
        log_info("Get ASR result:%s, score:%f, start[%.2fs] to end[%.2fs]\n", kws->word, kws->score, kws->start_ms / 1000.0f, kws->end_ms / 1000.0f);
    }
    break;
    case AIK_EVENT_NONE:
        log_info("AIK_EVENT_NONE\n");
        break;
    case  AIK_EVENT_START:
        log_info("AIK_EVENT_START\n");
        break;
    case  AIK_EVENT_STOP:
        log_info("AIK_EVENT_STOP\n");
        break;
    case  AIK_EVENT_EXIT:
        log_info("AIK_EVENT_EXIT\n");
        break;
    case  AIK_EVENT_KWS_WAKEUP:
        log_info("AIK_EVENT_KWS_WAKEUP\n");
        break;
    case  AIK_EVENT_KWS_TIMEOUT:
        log_info("AIK_EVENT_KWS_TIMEOUT\n");
        break;
    case  AIK_EVENT_HEARTBEAT:
        log_info("AIK_EVENT_HEARTBEAT\n");
        break;
    case  AIK_EVENT_LP_VAD_VOICE:
        log_info("AIK_EVENT_LP_VAD_VOICE\n");
        break;
    default:
        log_info("Nothing\n");
        break;
    }
    return;
}

/*----------------------------------------------------------------------------*/
/**@brief    语音识别相关初始化
  @param
  @return   0:成功， 1:失败
  @note
 */
/*----------------------------------------------------------------------------*/
int uniAsrInit(void)
{
    int err = 0;

    //配置打印等级
    UalAikSetLogLevel(3);

    //配置为从机模式
    UalAikSet(AIK_ID_SLAVE_SET_CONFIG, &global_ais_config.slave);
    //配置SPP模块
    UalAikSet(AIK_ID_SSP_SET_CONFIG, &global_ais_config.ssp);
    //配置KWS模块
    UalAikSet(AIK_ID_KWS_SET_CONFIG, &global_ais_config.kws);

    //注册识别完成的回调函数
    err = UalAikInit(AisEventCb);
    if (err == 0) {
        //配置工作模式为识别模式
        UalAikSet(AIK_ID_KWS_SET_COMMAND, NULL);
        //启动语音识别，识别模式为SSP+KWS(降噪+关键词)
        UalAikStart(AIK_MODE_SSP_KWS);
#if TCFG_KWS_TEST_ENABLE
        clear_kws_cnt();
        if (!kws_log_timer) {
            kws_log_timer = sys_timer_add(NULL, kws_log_list, 1000);
        }
#endif
    }

    return err;
}

/*----------------------------------------------------------------------------*/
/**@brief    mic数据流识别函数
  @param
  @return
  @note	 识别mic采集到的数据
 */
/*----------------------------------------------------------------------------*/
int uniAsrProcess(void *data, int size)
{
    if (get_unisound_online()) {
        UalAikUpdate(data, size);
    } else {
        log_info("wait init ...");
        return -1;
    }
    return 0;
}

/*----------------------------------------------------------------------------*/
/**@brief    关闭语音识别
  @param
  @return
  @note
 */
/*----------------------------------------------------------------------------*/
int uniAsrUninit(void)
{
#if (TCFG_MIC_REC_ENABLE && TCFG_SSP_DATA_EXPORT)
    ssp_data_export_uninit();
#endif

#if TCFG_KWS_TEST_ENABLE
    clear_kws_cnt();
    if (kws_log_timer) {
        sys_timer_del(kws_log_timer);
        kws_log_timer = 0;
    }
#endif
    UalAikStop(AIK_MODE_SSP_KWS);
    UalAikRelease();
    return 0;
}
#endif  //TCFG_UNISOUND_ENABLE
