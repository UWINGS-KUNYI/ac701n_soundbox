#ifndef _UART_1T2_SERIAL_SEND_H_
#define _UART_1T2_SERIAL_SEND_H_

#include "system/includes.h"

/**
 * @brief 初始化串行发送
 */
void uart_1t2_serial_send_init(uart_bus_t *udev);

typedef enum {
    Uart1t2SendResultSuccess             	= 0, //发送成功
    Uart1t2SendResultFailMaxCount			= 1, //发送失败，超过链表设计长度
    Uart1t2SendResultMallocFail	        = 2, //malloc失败
} Uart1t2SendResult;

/**
 * @brief 添加发送消息到串行发送链表
 *
 * @return ShareOscResult
 */
Uart1t2SendResult uart_1t2_send_buf_list_add(char *buf, u16 len);

/**
 * @brief 收到从机的消息
 */
void uart_1t2_recieve_msg_from_slave();

/**
 * @brief 清除链表内容
 */
void uart_1t2_send_list_flush();

#endif

