/*********************************************************************************************
    *   Filename        : wireless_params.h

    *   Description     :

    *   Author          : Weixin Liang

    *   Email           : liangweixin@zh-jieli.com

    *   Last modifiled  : 2022-12-15 14:26

    *   Copyright:(c)JIELI  2011-2022  @ , All Rights Reserved.
*********************************************************************************************/
#ifndef __WIRELESS_PARAMS_H
#define __WIRELESS_PARAMS_H

/*  Include header */
#include "big.h"
#include "cig.h"
#include "board_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/**************************************************************************************************
  Macros
**************************************************************************************************/
#define CIG_MODE_CToP           0   /*!< 主发从通讯 */
#define CIG_MODE_PToC           1   /*!< 从发主通讯 */
#define CIG_MODE_DUPLEX         2   /*!< 双工通讯 */

/*! \brief CIG通讯方式配置 */
#ifndef CIG_TRANSPORT_MODE
#define CIG_TRANSPORT_MODE      CIG_MODE_PToC
#endif
/**************************************************************************************************
  Data Types
**************************************************************************************************/
typedef BROADCAST_SYNC_INFO(void *);

/*! \brief jla编解码流程参数接口结构体，实体由外部流程定义 */
struct jla_codec_params  {
    u8 sdu_period_ms;
    u8 sound_input;
    u8 nch;
    u8 bit_width;
    int coding_type;
    int sample_rate;
    int frame_size;
    int bit_rate;
};

/**************************************************************************************************
  Function Declarations
**************************************************************************************************/
u32 get_big_enc_output_frame_len(void);
u16 get_big_transmit_data_len(void);
u32 get_big_enc_output_buf_len(void);
u32 get_big_dec_input_buf_len(void);
u32 get_big_sdu_period_ms(void);
struct jla_codec_params *get_big_codec_params_hdl(void);
u32 get_big_mtl_time(void);
u8 get_bis_num(u8 role);
void set_big_hdl(u8 role, u8 big_hdl);
void update_receiver_big_codec_params(BROADCAST_SYNC_INFO sync_data);
big_parameter_t *set_big_params(u8 app_task, u8 role, u8 big_hdl);
int get_big_audio_coding_bit_rate(void);
int get_big_audio_coding_frame_duration(void);
int get_big_tx_rtn(void);
int get_big_tx_delay(void);

u32 get_cig_enc_output_frame_len(void);
u16 get_cig_transmit_data_len(void);
u32 get_cig_enc_output_buf_len(void);
u32 get_cig_dec_input_buf_len(void);
u32 get_cig_sdu_period_ms(void);
struct jla_codec_params *get_cig_codec_params_hdl(void);
u32 get_cig_mtl_time(void);
void set_cig_hdl(u8 role, u8 cig_hdl);
cig_parameter_t *set_cig_params(u8 app_task, u8 role, u8 cig_hdl);
int get_cig_audio_coding_bit_rate(void);
int get_cig_audio_coding_frame_duration(void);
int get_cig_tx_rtn(void);
int get_cig_tx_delay(void);
void reset_cig_params(void);

int get_big_audio_coding_bit_rate(void);
int get_big_audio_coding_frame_duration(void);
int get_big_tx_rtn(void);

int get_cig_audio_coding_bit_rate(void);
int get_cig_audio_coding_frame_duration(void);
int get_cig_tx_rtn(void);
void reset_cig_params(void);

#ifdef __cplusplus
};
#endif

#endif

