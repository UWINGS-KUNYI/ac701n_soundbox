#ifndef _UART_1T2_MASTER_SEND_H_
#define _UART_1T2_MASTER_SEND_H_

#include "system/includes.h"

/**
 * @brief uart 1t2主机消息发送
 *
 * @pragma role 接收角色
 * @pragma buf 数据buf
 * @pragma len 数据长度
 */
void uart_1t2_master_msg_send(u8 role, u8 *buf, u16 len);

/**
 * @brief uart 1t2消息单io发送初始化
 */
void uart_1t2_master_send_init();

/**
 * @brief uart 1t2消息单io发送销毁
 */
void uart_1t2_master_send_exit();

/**
 * @brief uart 1t2消息初始化
 */
void uart_1t2_command_init();

/**
 * @brief uart 1t2关机流程
 */
void uart_1t2_soft_poweroff_enter();
#endif
