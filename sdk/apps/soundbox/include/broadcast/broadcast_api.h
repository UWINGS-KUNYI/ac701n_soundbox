/*********************************************************************************************
    *   Filename        : broadcast_api.h

    *   Description     :

    *   Author          : Weixin Liang

    *   Email           : liangweixin@zh-jieli.com

    *   Last modifiled  : 2022-07-07 14:37

    *   Copyright:(c)JIELI  2011-2022  @ , All Rights Reserved.
*********************************************************************************************/
#ifndef _BROADCAST_API_H
#define _BROADCAST_API_H

/*  Include header */
#include "system/includes.h"
#include "typedef.h"
#include "wireless_params.h"

#ifdef __cplusplus
extern "C" {
#endif

/**************************************************************************************************
  Data Types
**************************************************************************************************/
/*! \brief 广播角色枚举 */
enum {
    BROADCAST_ROLE_UNKNOW,
    BROADCAST_ROLE_TRANSMITTER,
    BROADCAST_ROLE_RECEIVER,
};

//time 类型
enum {
    CURRENT_TIME,
    PACKET_RX_TIME,
};

/*! \brief 广播同步的参数 */
struct broadcast_sync_info {
    // 状态同步
    u8 volume;
    u16 softoff;
    // 解码参数同步
    u8 sound_input;
    u8 nch;
    u16 frame_size;
    int coding_type;
    int sample_rate;
    int bit_rate;
};

/*! \brief 广播同步模块结构体 */
struct broadcast_sync_hdl {
    struct list_head entry; /*!< 同步模块链表项，用于多同步模块管理 */
    u8 big_hdl;         /*!< big句柄，用于big控制接口 */
    u16 seqn;
    u32 tx_sync_time;
    OS_SEM sem;
    void *bcsync;
    void *broadcast_hdl;
};

/**************************************************************************************************
  Global Variables
**************************************************************************************************/
extern const big_callback_t big_tx_cb;
extern const big_callback_t big_rx_cb;

/**************************************************************************************************
  Function Declarations
**************************************************************************************************/

/* ***************************************************************************/
/**
 * @brief open broadcast as transmitter
 *
 * @return err:-1, success:available_big_hdl
 */
/* *****************************************************************************/
int broadcast_transmitter(big_parameter_t *params);

/* ***************************************************************************/
/**
 * @brief open broadcast as receiver
 *
 * @return err:-1, success:available_big_hdl
 */
/* *****************************************************************************/
int broadcast_receiver(big_parameter_t *params);

/* ***************************************************************************/
/**
 * @brief close broadcast function
 *
 * @param big_hdl:need closed of big_hdl
 */
/* *****************************************************************************/
void broadcast_close(u8 big_hdl);

/* --------------------------------------------------------------------------*/
/**
 * @brief get current broadcast role
 *
 * @return broadcast role
 */
/* ----------------------------------------------------------------------------*/
u8 get_broadcast_role(void);

/* --------------------------------------------------------------------------*/
/**
 * @brief init broadcast params
 */
/* ----------------------------------------------------------------------------*/
void broadcast_init(void);

/* --------------------------------------------------------------------------*/
/**
 * @brief uninit broadcast params
 */
/* ----------------------------------------------------------------------------*/
void broadcast_uninit(void);

/* --------------------------------------------------------------------------*/
/**
 * @brief 设置需要同步的状态数据
 *
 * @param big_hdl:big句柄
 * @param data:数据buffer
 * @param length:数据长度
 *
 * @return -1:fail，0:success
 */
/* ----------------------------------------------------------------------------*/
int broadcast_set_sync_data(u8 big_hdl, void *data, size_t length);

/* --------------------------------------------------------------------------*/
/**
 * @brief 初始化同步的状态数据的内容
 *
 * @param data:用来同步的数据
 */
/* ----------------------------------------------------------------------------*/
void broadcast_sync_data_init(struct broadcast_sync_info *data);

int broadcast_enter_pair(u8 role, void *pair_event_cb, u32 user_pair_code);

int broadcast_exit_pair(u8 role);

int broadcast_transmitter_connect_deal(void *priv, int crc16, u8 mode);

int broadcast_receiver_connect_deal(void *priv, int crc16);

int broadcast_receiver_disconnect_deal(void *priv);

/* --------------------------------------------------------------------------*/
/**
 * @brief 供外部手动初始化capture模块
 *
 * @param big_hdl:capture模块所对应的big_hdl
 */
/* ----------------------------------------------------------------------------*/
void broadcast_audio_capture_reset(u16 big_hdl);

/* --------------------------------------------------------------------------*/
/**
 * @brief 供外部手动关闭capture模块
 *
 * @param big_hdl:capture模块所对应的big_hdl
 */
/* ----------------------------------------------------------------------------*/
void broadcast_audio_capture_close(u16 big_hdl);

#ifdef __cplusplus
};
#endif

#endif //_BROADCASE_API_H

