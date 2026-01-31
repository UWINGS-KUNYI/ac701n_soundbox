/*********************************************************************************************
    *   Filename        : connected_api.h

    *   Description     :

    *   Author          : Weixin Liang

    *   Email           : liangweixin@zh-jieli.com

    *   Last modifiled  : 2022-12-15 14:33

    *   Copyright:(c)JIELI  2011-2022  @ , All Rights Reserved.
*********************************************************************************************/
#ifndef _CONNECTED_API_H
#define _CONNECTED_API_H

/*  Include header */
#include "app_config.h"
#include "typedef.h"
#include "system/includes.h"
#include "cig.h"
#include "wireless_params.h"

#ifdef __cplusplus
extern "C" {
#endif

/**************************************************************************************************
  Macros
**************************************************************************************************/

#define CONNECTED_SYNC_CALLBACK_REGISTER(channel) \
    const struct connected_sync_channel channel SEC(.connected_call_sync)

#define list_for_each_cig_sync_channel(p) \
    for (p = connected_sync_channel_begin; p < connected_sync_channel_end; p++)

/**************************************************************************************************
  Data Types
**************************************************************************************************/

/*! \brief CIG角色枚举 */
enum {
    CONNECTED_ROLE_UNKNOW,
    CONNECTED_ROLE_CENTRAL,
    CONNECTED_ROLE_PERIP,
};

/*! \brief CIG同步模块结构体 */
struct connected_sync_hdl {
    struct list_head entry; /*!< 同步模块链表项，用于多同步模块管理 */
    u8 cig_hdl;         /*!< cig句柄，用于cig控制接口 */
    u8 channel_num;
    u16 seqn;
    int sample_rate;
    int connected_sample_rate;
    u32 tx_sync_time;
    OS_SEM sem;
    void *bcsync;
    void *connected_hdl;
};

/*! \brief CIG数据转发和事件同步回调函数接头体 */
struct connected_sync_channel {
    void(*event_handler)(const CIG_EVENT event, void *arg);
    int(*tx_events_suss)(void *data, int len);
    int(*rx_events_suss)(u16 acl_hdl, void *data, int len);
};

/**************************************************************************************************
  Global Variables
**************************************************************************************************/
extern const cig_callback_t cig_central_cb;
extern const cig_callback_t cig_perip_cb;
extern struct connected_sync_channel  connected_sync_channel_begin[];
extern struct connected_sync_channel  connected_sync_channel_end[];

/**************************************************************************************************
  Function Declarations
**************************************************************************************************/

/* --------------------------------------------------------------------------*/
/**
 * @brief CIG主机启动接口
 *
 * @param params:CIG主机启动所需参数
 *
 * @return 分配的cig_hdl
 */
/* ----------------------------------------------------------------------------*/
int connected_central_open(cig_parameter_t *params);

/* --------------------------------------------------------------------------*/
/**
 * @brief CIG从机启动接口
 *
 * @param params:CIG从机启动所需参数
 *
 * @return 分配的cig_hdl
 */
/* ----------------------------------------------------------------------------*/
int connected_perip_open(cig_parameter_t *params);

/* --------------------------------------------------------------------------*/
/**
 * @brief 关闭对应cig_hdl的CIG连接
 *
 * @param cig_hdl:需要关闭的CIG连接的cig_hdl
 */
/* ----------------------------------------------------------------------------*/
void connected_close(u8 cig_hdl);

/* --------------------------------------------------------------------------*/
/**
 * @brief 获取当前CIG是主机还是从机
 *
 * @return CIG角色
 */
/* ----------------------------------------------------------------------------*/
u8 get_connected_role(void);

/* --------------------------------------------------------------------------*/
/**
 * @brief 初始化CIG所需的参数及流程
 *
 * @param role:CIG角色作为主机还是从机
 */
/* ----------------------------------------------------------------------------*/
void connected_init(u8 role);

/* --------------------------------------------------------------------------*/
/**
 * @brief 复位CIG所需的参数及流程
 *
 * @param role:CIG角色作为主机还是从机
 */
/* ----------------------------------------------------------------------------*/
void connected_uninit(u8 role);

/* --------------------------------------------------------------------------*/
/**
 * @brief acl数据发送接口
 *
 * @param acl_hdl:acl数据通道句柄
 * @param data:需要发送的数据
 * @param length:数据长度
 *
 * @return 实际发送出去的数据长度
 */
/* ----------------------------------------------------------------------------*/
int connected_send_acl_data(u16 acl_hdl, void *data, size_t length);

/* --------------------------------------------------------------------------*/
/**
 * @brief CIG主机连接成功处理事件
 *
 * @param priv:连接成功附带的句柄参数
 * @param crc16:校验连接成功事件转发前后的参数是否一致
 * @param mode:当前音源模式
 *
 * @return 是否执行成功 -- 0为成功，其他为失败
 */
/* ----------------------------------------------------------------------------*/
int connected_central_connect_deal(void *priv, int crc16, u8 mode);

/* --------------------------------------------------------------------------*/
/**
 * @brief CIG主机连接断开成功处理接口口
 *
 * @param priv:断链成功附带的句柄参数
 *
 * @return 是否执行成功 -- 0为成功，其他为失败
 */
/* ----------------------------------------------------------------------------*/
int connected_central_disconnect_deal(void *priv);

/* --------------------------------------------------------------------------*/
/**
 * @brief CIG从机连接成功处理事件
 *
 * @param priv:连接成功附带的句柄参数
 * @param crc16:校验连接成功事件转发前后的参数是否一致
 * @param mode:当前音源模式
 *
 * @return 是否执行成功 -- 0为成功，其他为失败
 */
/* ----------------------------------------------------------------------------*/
int connected_perip_connect_deal(void *priv, int crc16, u8 mode);

/* --------------------------------------------------------------------------*/
/**
 * @brief CIG从机连接断开成功处理接口口
 *
 * @param priv:断链成功附带的句柄参数
 *
 * @return 是否执行成功 -- 0为成功，其他为失败
 */
/* ----------------------------------------------------------------------------*/
int connected_perip_disconnect_deal(void *priv);

/* --------------------------------------------------------------------------*/
/**
 * @brief 供外部手动初始化capture模块
 *
 * @param cis_hdl:capture模块所对应的cis_hdl
 */
/* ----------------------------------------------------------------------------*/
void cis_audio_capture_reset(u16 cis_hdl);

/* --------------------------------------------------------------------------*/
/**
 * @brief 供外部手动关闭capture模块
 *
 * @param cis_hdl:capture模块所对应的cis_hdl
 */
/* ----------------------------------------------------------------------------*/
void cis_audio_capture_close(u16 cis_hdl);

/* --------------------------------------------------------------------------*/
/**
 * @brief 多路音源混合时，可通过指定模式来打开需要转发的音频获取模块
 *
 * @param mode:需要获取的音源模式
 *
 * @return true:开启成功 false:开启失败
 */
/* ----------------------------------------------------------------------------*/
int cis_audio_multi_capture_open(u8 mode);

/* --------------------------------------------------------------------------*/
/**
 * @brief 多路音源混合时，可通过指定模式来关闭不需要转发的音频获取模块
 *
 * @param mode:需要移除的音源模式
 *
 * @return true:开启成功 false:开启失败
 */
/* ----------------------------------------------------------------------------*/
int cis_audio_multi_capture_close(u8 mode);

/* --------------------------------------------------------------------------*/
/**
 * @brief 多路音源混合时，设置音源设备上线标志
 *
 * @param mode:对应的音源设备
 */
/* ----------------------------------------------------------------------------*/
void set_cig_audio_dev_online(u8 mode);

/* --------------------------------------------------------------------------*/
/**
 * @brief 多路音源混合时，设置音源设备下线标志
 *
 * @param mode:对应的音源设备
 */
/* ----------------------------------------------------------------------------*/
void set_cig_audio_dev_offline(u8 mode);

#ifdef __cplusplus
};
#endif

#endif //_connected_API_H

