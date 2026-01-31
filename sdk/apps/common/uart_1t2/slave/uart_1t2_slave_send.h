#ifndef _UART_1T2_SLAVE_SEND_H_
#define _UART_1T2_SLAVE_SEND_H_

#include "system/includes.h"

/**
 * @brief uart 1t2从机消息发送
 */
void uart_1t2_slave_msg_send(u8 *buf, u16 len);

/**
 * @brief uart 1t2消息单io发送初始化
 */
void uart_1t2_slave_send_init();

#endif
