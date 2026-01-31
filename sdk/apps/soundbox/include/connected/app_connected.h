/*********************************************************************************************
    *   Filename        : app_connected.h

    *   Description     :

    *   Author          : Weixin Liang

    *   Email           : liangweixin@zh-jieli.com

    *   Last modifiled  : 2022-12-15 14:34

    *   Copyright:(c)JIELI  2011-2022  @ , All Rights Reserved.
*********************************************************************************************/
#ifndef _APP_CONNECTED_H
#define _APP_CONNECTED_H

/*  Include header */
#include "typedef.h"

#ifdef __cplusplus
extern "C" {
#endif

/**************************************************************************************************
  Macros
**************************************************************************************************/
// 设备标识，用于区分不同设备，与实际声道没有直接的关联
#define CONNECTED_DEVICE_IDENTIFICATION_M  BIT(0)   //主机(central)
#define CONNECTED_DEVICE_IDENTIFICATION_L  BIT(1)   //从机(perip)左声道
#define CONNECTED_DEVICE_IDENTIFICATION_R  BIT(2)   //从机(perip)右声道

#define CONNECTED_SYNC_CALL_REGISTER(sync_call) \
    static const struct connected_sync_call sync_call sec(.connected_sync_call_func)

#define list_for_each_connected_sync_call(p) \
    for (p = conn_sync_call_func_begin; p < conn_sync_call_func_end; p++)

#define CONN_DATA_TRANS_STUB_REGISTER(stub) \
    static const struct conn_data_trans_stub stub sec(.conn_data_trans_stub)

#define list_for_each_connected_data_trans(p) \
    for (p = conn_data_trans_stub_begin; p < conn_data_trans_stub_end; p++)

#define CONNECTED_SYNC_CALL_TX                1
#define CONNECTED_SYNC_CALL_RX                2

#define CIG_FUNC_ID(a, b, c, d)               (((a) << 24) | ((b) << 16) | ((c) << 8) | (d))

#define CONNECTED_IDTF_SYNC_FUNC_ID           CIG_FUNC_ID('I', 'D', 'T', 'F')
#define CONNECTED_VOLS_SYNC_FUNC_ID           CIG_FUNC_ID('V', 'O', 'L', 'S')

/**************************************************************************************************
  Data Types
**************************************************************************************************/
enum {
    APP_CONNECTED_STATUS_STOP,
    APP_CONNECTED_STATUS_START,
    APP_CONNECTED_STATUS_SUSPEND,
    APP_CONNECTED_STATUS_CONNECT,
    APP_CONNECTED_STATUS_DISCONNECT,
};

enum {
    CONNECTED_APP_MODE_ENTER,
    CONNECTED_APP_MODE_EXIT,
    CONNECTED_A2DP_START,
    CONNECTED_A2DP_STOP,
    CONNECTED_PHONE_START,
    CONNECTED_PHONE_STOP,
    CONNECTED_EDR_DISCONN,
    CONNECTED_MUSIC_START,
    CONNECTED_MUSIC_STOP,
};

enum {
    CONNECTED_EVENT_SYNC_TYPE = 1,
    CONNECTED_KEY_SYNC_TYPE,
    CONNECTED_DATA_TRANSMIT_TYPE,
};

enum {
    APP_CONNECTED_ROLE_UNKNOW,
    APP_CONNECTED_ROLE_TRANSMITTER,
    APP_CONNECTED_ROLE_RECEIVER,
    APP_CONNECTED_ROLE_DUPLEX,
};

struct connected_sync_call {
    int uuid;
    void (*func)(int priv, int sync_call_role);
    const char *task_name;
};

struct conn_data_trans_stub {
    int uuid;
    void (*func)(u16 acl_hdl, u8 arg_num, int *argv);
    const char *task_name;
};

/**************************************************************************************************
  Global Variables
**************************************************************************************************/
extern const struct connected_sync_call  conn_sync_call_func_begin[];
extern const struct connected_sync_call  conn_sync_call_func_end[];
extern const struct conn_data_trans_stub conn_data_trans_stub_begin[];
extern const struct conn_data_trans_stub conn_data_trans_stub_end[];

/**************************************************************************************************
  Function Declarations
**************************************************************************************************/

/* --------------------------------------------------------------------------*/
/**
 * @brief 开启CIG
 *
 * @param pair_without_addr:不匹配配对地址，重新自由配对
 */
/* ----------------------------------------------------------------------------*/
void app_connected_open(u8 pair_without_addr);

/* --------------------------------------------------------------------------*/
/**
 * @brief 关闭对应cig_hdl的CIG连接
 *
 * @param cig_hdl:需要关闭的CIG连接对应的hdl
 * @param status:关闭后CIG进入的suspend还是stop状态
 */
/* ----------------------------------------------------------------------------*/
void app_connected_close(u8 cig_hdl, u8 status);

/* --------------------------------------------------------------------------*/
/**
 * @brief 关闭所有cig_hdl的CIG连接
 *
 * @param status:关闭后CIG进入的suspend还是stop状态
 */
/* ----------------------------------------------------------------------------*/
void app_connected_close_all(u8 status);

/* --------------------------------------------------------------------------*/
/**
 * @brief CIG开关切换
 *
 * @return -1:切换失败，0:切换成功
 */
/* ----------------------------------------------------------------------------*/
int app_connected_switch(void);

/* --------------------------------------------------------------------------*/
/**
 * @brief CIG开启情况下，不同场景的处理流程
 *
 * @param switch_mode:当前系统状态
 *
 * @return -1:无需处理，0:处理事件但不拦截后续流程，1:处理事件并拦截后续流程
 */
/* ----------------------------------------------------------------------------*/
int app_connected_deal(int switch_mode);

/* --------------------------------------------------------------------------*/
/**
 * @brief 获取当前是否在退出模式的状态
 *
 * @return 1；是，0：否
 */
/* ----------------------------------------------------------------------------*/
u8 get_connected_app_mode_exit_flag(void);

/* --------------------------------------------------------------------------*/
/**
 * @brief CIG资源初始化
 */
/* ----------------------------------------------------------------------------*/
void app_connected_init(void);

/* --------------------------------------------------------------------------*/
/**
 * @brief CIG资源卸载
 */
/* ----------------------------------------------------------------------------*/
void app_connected_uninit(void);

/* --------------------------------------------------------------------------*/
/**
 * @brief ACL数据发送接口
 *
 * @param device_channel:发送给远端设备的标识
 * @param data:需要发送的数据
 * @param length:数据长度
 *
 * @return slen:实际发送数据的总长度
 */
/* ----------------------------------------------------------------------------*/
int app_connected_send_acl_data(u8 device_channel, void *data, size_t length);

/* --------------------------------------------------------------------------*/
/**
 * @brief 判断对应设备标识的CIS当前是否处于连接状态
 *
 * @param device_channel:远端设备标识
 *
 * @return connected_status:连接状态，按bit判断
 */
/* ----------------------------------------------------------------------------*/
u8 app_cis_is_connected(u8 device_channel);

/* --------------------------------------------------------------------------*/
/**
 * @brief CIG连接状态事件处理函数
 *
 * @param wireless_trans:连接事件附带的返回参数，参数类型是cig_hdl_t
 */
/* ----------------------------------------------------------------------------*/
void app_connected_conn_status_event_handler(struct wireless_trans_event *wireless_trans);

/* --------------------------------------------------------------------------*/
/**
 * @brief 非蓝牙后台情况下，在其他音源模式开启CIG，前提要先开蓝牙协议栈
 */
/* ----------------------------------------------------------------------------*/
void app_connected_open_in_other_mode();

/* --------------------------------------------------------------------------*/
/**
 * @brief
 * @brief 非蓝牙后台情况下，在其他音源模式关闭CIG
 */
/* ----------------------------------------------------------------------------*/
void app_connected_close_in_other_mode();

/* --------------------------------------------------------------------------*/
/**
 * @brief CIG连接角色重定义
 *
 * @return 设备角色为发送设备还是接收设备
 */
/* ----------------------------------------------------------------------------*/
u8 app_get_connected_role(void);

/* --------------------------------------------------------------------------*/
/**
 * @brief 清除配对信息，并重新配对
 */
/* ----------------------------------------------------------------------------*/
void app_connected_remove_pairs_addr(void);

/* --------------------------------------------------------------------------*/
/**
 * @brief 任务同步调用接口，约定事件双方同时执行对应的事件任务
 *
 * @param uuid:注册回调时配置的ID
 * @param priv:需要同步执行的事件
 * @param delay_ms:约定的延时时间
 * @param device_channel:需要跟本机同步执行的远端设备的标识
 *
 * @return len:实际发出去的数据长度
 */
/* ----------------------------------------------------------------------------*/
int connected_api_sync_call_by_uuid(int uuid, int priv, int delay_ms, u8 device_channel);

/* --------------------------------------------------------------------------*/
/**
 * @brief 发送数据给远端设备
 *
 * @param uuid:注册回调时配置的ID
 * @param data:需要发送的数据
 * @param len:发送数据的长度
 * @param device_channel:远端设备的标识
 *
 * @return 实际发送的数据长度
 */
/* ----------------------------------------------------------------------------*/
int connected_send_data_to_sibling(int uuid, void *data, u16 len, u8 device_channel);

/* --------------------------------------------------------------------------*/
/**
 * @brief CIG连接成功后central发起音量同步
 */
/* ----------------------------------------------------------------------------*/
void app_connected_sync_volume(u8 remote_dev_identification);



bool get_connected_on_off(void);

int app_connected_role_switch(void);
void app_connected_open_with_role(u8 role_in, u8 pair_without_addr);

#ifdef __cplusplus
};
#endif

#endif

