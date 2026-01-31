#ifndef _SHARE_OSC_UART_SINGLE_IO_SEND_H_
#define _SHARE_OSC_UART_SINGLE_IO_SEND_H_

#include "system/includes.h"

/**
 * @brief 共享晶振消息发送
 */
void share_osc_msg_send(u8 *buf, u16 len);

/**
 * @brief 共享晶振消息单io发送初始化
 */
void share_osc_check_single_io_send_init();

/**
 * @brief 共享晶振消息单io发送销毁
 */
void share_osc_check_single_io_send_exit();

#endif
