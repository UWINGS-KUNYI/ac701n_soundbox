/*********************************************************************************************
    *   Filename        : big_params.c

    *   Description     :

    *   Author          : Weixin Liang

    *   Email           : liangweixin@zh-jieli.com

    *   Last modifiled  : 2022-12-15 14:17

    *   Copyright:(c)JIELI  2011-2022  @ , All Rights Reserved.
*********************************************************************************************/
#include "app_config.h"
#include "app_cfg.h"
#include "app_task.h"
#include "audio_base.h"
#include "broadcast_api.h"
#include "wireless_params.h"
#include "live_audio.h"

/**************************************************************************************************
  Macros
**************************************************************************************************/

#ifndef BIG_BT_SDU_PERIOD_MS
#define BIG_BT_SDU_PERIOD_MS        20
#endif

#ifndef BIG_AUX_SDU_PERIOD_MS
#define BIG_AUX_SDU_PERIOD_MS       20
#endif

#ifndef BIG_MUSIC_SDU_PERIOD_MS
#define BIG_MUSIC_SDU_PERIOD_MS     20
#endif

#ifndef BIG_MIC_SDU_PERIOD_MS
#define BIG_MIC_SDU_PERIOD_MS       20
#endif

#ifndef BIG_IIS_SDU_PERIOD_MS
#define BIG_IIS_SDU_PERIOD_MS       20
#endif



#if TCFG_LIVE_AUDIO_LOW_LATENCY_EN
#undef  BIG_AUX_SDU_PERIOD_MS
#define BIG_AUX_SDU_PERIOD_MS       5
#endif

/*! \brief 配对名 */
#define BIG_PAIR_NAME           "br28_big_soundbox"

/*! \brief Broadcast Code */
#define BIG_BROADCAST_CODE      "JL_BROADCAST"

/*! \配置广播通道数，不同通道可发送不同数据，例如多声道音频  */
#ifndef BIG_TX_USED_BIS_NUM
#define BIG_TX_USED_BIS_NUM     1
#endif
#ifndef BIG_RX_USED_BIS_NUM
#define BIG_RX_USED_BIS_NUM     1
#endif

#define SCAN_WINDOW_SLOT                16
#define SCAN_INTERVAL_SLOT              28
#define PRIMARY_ADV_INTERVAL_SLOT       192

#ifndef BIG_TX_PARAM_FROM
#define BIG_TX_PARAM_FROM               0
#endif

#ifndef BIG_TX_PARAM_TX_DELAY
#define BIG_TX_PARAM_TX_DELAY           3500
#endif

#ifndef BIG_RX_BIS_CHANNEL
#define BIG_RX_BIS_CHANNEL              1
#endif


#ifndef BIG_RX_PSYNC_TIME
#define BIG_RX_PSYNC_TIME               0
#endif

#ifndef BIG_RX_BSYNC_TIME
#define BIG_RX_BSYNC_TIME               2000
#endif
/**************************************************************************************************
  Global Variables
**************************************************************************************************/
static struct jla_codec_params big_bt_codec_params = {
    .sdu_period_ms = BIG_BT_SDU_PERIOD_MS,
    .sound_input = LIVE_SOUND_A2DP,
    .nch = JLA_CODING_CHANNEL,
    .coding_type = AUDIO_CODING_JLA,
    .sample_rate = JLA_CODING_SAMPLERATE,
    .frame_size = JLA_CODING_FRAME_LEN,
    .bit_rate = JLA_CODING_BIT_RATE,
    .bit_width = TCFG_AUDIO_DAC_BIT_WIDTH,
};

static struct jla_codec_params big_music_codec_params = {
    .sdu_period_ms = BIG_MUSIC_SDU_PERIOD_MS,
    .sound_input = LIVE_SOUND_FILE,
    .nch = JLA_CODING_CHANNEL,
    .coding_type = AUDIO_CODING_JLA,
    .sample_rate = JLA_CODING_SAMPLERATE,
    .frame_size = JLA_CODING_FRAME_LEN,
    .bit_rate = JLA_CODING_BIT_RATE,
    .bit_width = TCFG_AUDIO_DAC_BIT_WIDTH,
};

static struct jla_codec_params big_aux_codec_params = {
    .sdu_period_ms = BIG_AUX_SDU_PERIOD_MS,
    .sound_input = LIVE_SOUDN_AUX,
    .nch = JLA_CODING_CHANNEL,
    .coding_type = AUDIO_CODING_JLA,
    .sample_rate = JLA_CODING_SAMPLERATE,
    .frame_size = JLA_CODING_FRAME_LEN,
    .bit_rate = JLA_CODING_BIT_RATE,
    .bit_width = TCFG_AUDIO_DAC_BIT_WIDTH,
};

static struct jla_codec_params big_mic_codec_params = {
    .sdu_period_ms = BIG_MIC_SDU_PERIOD_MS,
    .sound_input = LIVE_SOUND_MIC,
    .nch = JLA_CODING_CHANNEL,
    .coding_type = AUDIO_CODING_JLA,
    .sample_rate = JLA_CODING_SAMPLERATE,
    .frame_size = JLA_CODING_FRAME_LEN,
    .bit_rate = JLA_CODING_BIT_RATE,
    .bit_width = TCFG_AUDIO_DAC_BIT_WIDTH,
};

static struct jla_codec_params big_iis_codec_params = {
#if (TCFG_AUDIO_INPUT_IIS || TCFG_AUDIO_OUTPUT_IIS)
    .sdu_period_ms = BIG_IIS_SDU_PERIOD_MS,
    .sound_input = LIVE_SOUND_IIS,
    .nch = JLA_CODING_CHANNEL,
    .coding_type = AUDIO_CODING_JLA,
    .sample_rate = JLA_CODING_SAMPLERATE,
    .frame_size = JLA_CODING_FRAME_LEN,
    .bit_rate = JLA_CODING_BIT_RATE,
    .bit_width = TCFG_AUDIO_DAC_BIT_WIDTH,
#endif
};

static struct jla_codec_params big_receiver_codec_params = {
    .nch = JLA_CODING_CHANNEL,
    .coding_type = AUDIO_CODING_JLA,
    .sample_rate = JLA_CODING_SAMPLERATE,
    .frame_size = JLA_CODING_FRAME_LEN,
    .bit_rate = JLA_CODING_BIT_RATE,
    .bit_width = TCFG_AUDIO_DAC_BIT_WIDTH,
};


static big_parameter_t big_tx_param = {
    .pair_name = BIG_PAIR_NAME,
    .cb        = &big_tx_cb,
    .num_bis   = BIG_TX_USED_BIS_NUM,
    .ext_phy   = 1,
    .tx.eadv_int_ms = PRIMARY_ADV_INTERVAL_SLOT,
    .enc       = 0,
    .bc        = BIG_BROADCAST_CODE,
    .form      = BIG_TX_PARAM_FROM,

    .tx = {
        .phy            = BIT(1),
        .aux_phy        = 2,
        .vdr = {
            .tx_delay   = BIG_TX_PARAM_TX_DELAY,
        },
    },
};

static big_parameter_t big_rx_param = {
    .pair_name = BIG_PAIR_NAME,
    .num_bis   = BIG_RX_USED_BIS_NUM,
    .cb        = &big_rx_cb,
    .ext_phy   = 1,
    .rx.ext_scan_int = SCAN_WINDOW_SLOT,
    .rx.ext_scan_win = SCAN_INTERVAL_SLOT,
    .enc       = 0,
    .bc        = BIG_BROADCAST_CODE,

    .rx = {
        .bis            = {BIG_RX_BIS_CHANNEL},
        .psync_to_ms    =  BIG_RX_PSYNC_TIME,
        .bsync_to_ms    =  BIG_RX_BSYNC_TIME,
        .psync_keep     =  BROADCAST_DATA_SYNC_EN,
    },
};


static big_parameter_t *tx_params = NULL;
static big_parameter_t *rx_params = NULL;
static struct jla_codec_params *codec_params = NULL;
static u16 big_transmit_data_len = 0;
static u32 enc_output_frame_len = 0;
static u32 dec_input_buf_len = 0;
static u32 enc_output_buf_len = 0;

/**************************************************************************************************
  Function Declarations
**************************************************************************************************/

/*! \brief 每包编码数据长度 */
/* (int)((JLA_CODING_FRAME_LEN / 10) * (JLA_CODING_BIT_RATE / 1000 / 8) + 2) */
/* 如果码率超过96K,即帧长超过122,就需要将每次传输数据大小 修改为一帧编码长度 */
static u32 calcul_big_enc_output_frame_len(u16 frame_len, u32 bit_rate)
{
    return (frame_len * bit_rate / 1000 / 8 / 10 + 2);
}

u32 get_big_enc_output_frame_len(void)
{
    ASSERT(enc_output_frame_len, "enc_output_frame_len is 0");
    return enc_output_frame_len;
}

static u16 calcul_big_transmit_data_len(u32 encode_output_frame_len, u16 period, u16 codec_frame_len)
{
    return (encode_output_frame_len * (period * 10 / codec_frame_len));
}

u16 get_big_transmit_data_len(void)
{
    ASSERT(big_transmit_data_len, "big_transmit_data_len is 0");
    return big_transmit_data_len;
}

static u32 calcul_big_enc_output_buf_len(u32 transmit_data_len)
{
    return (transmit_data_len * 2);
}

u32 get_big_enc_output_buf_len(void)
{
    ASSERT(enc_output_buf_len, "enc_output_buf_len is 0");
    return enc_output_buf_len;
}

static u32 calcul_big_dec_input_buf_len(u32 transmit_data_len)
{
    return (transmit_data_len * 10);
}

u32 get_big_dec_input_buf_len(void)
{
    ASSERT(dec_input_buf_len, "dec_input_buf_len is 0");
    return dec_input_buf_len;
}

u32 get_big_sdu_period_ms(void)
{
    return codec_params->sdu_period_ms;
}

struct jla_codec_params *get_big_codec_params_hdl(void)
{
    return codec_params;
}

u32 get_big_mtl_time(void)
{
    return tx_params->tx.mtl;
}

u8 get_bis_num(u8 role)
{
    u8 num = 0;
    if ((role == BROADCAST_ROLE_TRANSMITTER) && tx_params) {
        num = tx_params->num_bis;
    } else if ((role == BROADCAST_ROLE_RECEIVER) && rx_params) {
        num = rx_params->num_bis;
    }
    return num;
}

void set_big_hdl(u8 role, u8 big_hdl)
{
    if ((role == BROADCAST_ROLE_TRANSMITTER) && tx_params) {
        tx_params->big_hdl = big_hdl;
    } else if ((role == BROADCAST_ROLE_RECEIVER) && rx_params) {
        rx_params->big_hdl = big_hdl;
    }
}

int get_big_audio_coding_bit_rate(void)
{
    if (codec_params) {
        return codec_params->bit_rate;
    }
    return 0;
}

int get_big_audio_coding_frame_duration(void)
{
    if (codec_params) {
        return codec_params->frame_size;
    }

    return 0;
}

int get_big_tx_rtn(void)
{
    if (tx_params) {
        return tx_params->tx.rtn;
    }

    return 0;
}

int get_big_tx_delay(void)
{
    if (tx_params) {
        return tx_params->tx.vdr.tx_delay;
    }

    return 0;
}

void update_receiver_big_codec_params(BROADCAST_SYNC_INFO sync_data)
{
    struct broadcast_sync_info *data_sync = (struct broadcast_sync_info *)sync_data;
    if (codec_params) {
        codec_params->sound_input = data_sync->sound_input;
        codec_params->nch = data_sync->nch;
        codec_params->coding_type = data_sync->coding_type;
        codec_params->sample_rate = data_sync->sample_rate;
        codec_params->frame_size = data_sync->frame_size;
        codec_params->bit_rate = data_sync->bit_rate;
        enc_output_frame_len = calcul_big_enc_output_frame_len(codec_params->frame_size, codec_params->bit_rate);
        big_transmit_data_len = calcul_big_transmit_data_len(enc_output_frame_len, codec_params->sdu_period_ms, codec_params->frame_size);
        dec_input_buf_len = calcul_big_dec_input_buf_len(big_transmit_data_len);
    }
}

big_parameter_t *set_big_params(u8 app_task, u8 role, u8 big_hdl)
{
    u32 pair_code;
    int ret;
    if (role == BROADCAST_ROLE_TRANSMITTER) {
        switch (app_task) {
        case APP_BT_TASK:
            tx_params = &big_tx_param;
            codec_params = &big_bt_codec_params;
            break;
        case APP_MUSIC_TASK:
            tx_params = &big_tx_param;
            codec_params = &big_music_codec_params;
            break;
        case APP_LINEIN_TASK:
            tx_params = &big_tx_param;
            codec_params = &big_aux_codec_params;
            break;
        case APP_LIVE_MIC_TASK:
            tx_params = &big_tx_param;
            codec_params = &big_mic_codec_params;
            break;
        case APP_LIVE_IIS_TASK:
            tx_params = &big_tx_param;
            codec_params = &big_iis_codec_params;
            break;
        default:
            tx_params = NULL;
            codec_params = NULL;
            break;
        }
        enc_output_frame_len = calcul_big_enc_output_frame_len(codec_params->frame_size, codec_params->bit_rate);
        big_transmit_data_len = calcul_big_transmit_data_len(enc_output_frame_len, codec_params->sdu_period_ms, codec_params->frame_size);
        dec_input_buf_len = calcul_big_dec_input_buf_len(big_transmit_data_len);
        enc_output_buf_len = calcul_big_enc_output_buf_len(big_transmit_data_len);
        if (tx_params) {
            tx_params->big_hdl = big_hdl;
            if (codec_params->bit_rate > 96000) {
                tx_params->tx.mtl = codec_params->sdu_period_ms * 1;
                if (broadcast_1tn_en || broadcast_1t4_en) {
                    tx_params->tx.rtn = 3;
                } else {
                    tx_params->tx.rtn = 2;
                }
            } else {
                tx_params->tx.mtl = codec_params->sdu_period_ms * 2;
                tx_params->tx.rtn = 4;
            }
            if (broadcast_1t4_en) {
                tx_params->tx.mtl = 0;
            }
#if TCFG_LIVE_AUDIO_LOW_LATENCY_EN
            if (app_task == APP_LINEIN_TASK) {
                tx_params->tx.mtl = 0;
            }
#endif
            tx_params->tx.sdu_int_us = codec_params->sdu_period_ms * 1000L;
#if (CONNECTED_TX_CHANNEL_SEPARATION && (JLA_CODING_CHANNEL == 2))
            u8 packet_num;
            u16 single_ch_trans_data_len;
            packet_num = big_transmit_data_len / enc_output_frame_len;
            single_ch_trans_data_len = big_transmit_data_len / 2 + packet_num;
            tx_params->tx.max_sdu = single_ch_trans_data_len;
#else
            tx_params->tx.max_sdu = big_transmit_data_len;
#endif
            if (big_transmit_data_len > 251) {
                tx_params->tx.vdr.max_pdu = enc_output_frame_len;
            }
        }
        ret = syscfg_read(VM_WIRELESS_PAIR_CODE0, &pair_code, sizeof(u32));
        if (ret <= 0) {
            if (broadcast_1tn_en) {
                pair_code = 0xFFFFFFFE;
            } else {
                pair_code = 0;
            }
        }
        tx_params->pri_ch = pair_code;
        g_printf("wireless_pair_code:0x%x", pair_code);
        return tx_params;
    }

    if (role == BROADCAST_ROLE_RECEIVER) {
        rx_params = &big_rx_param;
        codec_params = &big_receiver_codec_params;
        if (app_task == APP_BT_TASK) {
            codec_params->sdu_period_ms = BIG_BT_SDU_PERIOD_MS;
        } else if (app_task == APP_MUSIC_TASK) {
            codec_params->sdu_period_ms = BIG_MUSIC_SDU_PERIOD_MS;
        } else if (app_task == APP_LINEIN_TASK) {
            codec_params->sdu_period_ms = BIG_AUX_SDU_PERIOD_MS;
        } else if (app_task == APP_LIVE_MIC_TASK) {
            codec_params->sdu_period_ms = BIG_MIC_SDU_PERIOD_MS;
        }
        enc_output_frame_len = calcul_big_enc_output_frame_len(codec_params->frame_size, codec_params->bit_rate);
        big_transmit_data_len = calcul_big_transmit_data_len(enc_output_frame_len, codec_params->sdu_period_ms, codec_params->frame_size);
        dec_input_buf_len = calcul_big_dec_input_buf_len(big_transmit_data_len);
        rx_params->big_hdl = big_hdl;
        ret = syscfg_read(VM_WIRELESS_PAIR_CODE0, &pair_code, sizeof(u32));
        if (ret <= 0) {
            if (broadcast_1tn_en) {
                pair_code = 0xFFFFFFFF;
            } else {
                pair_code = 0;
            }
        }
        rx_params->pri_ch = pair_code;
        g_printf("wireless_pair_code:0x%x", pair_code);
        return rx_params;
    }

    return NULL;
}

