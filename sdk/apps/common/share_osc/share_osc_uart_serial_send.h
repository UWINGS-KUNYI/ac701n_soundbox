#ifndef _SHARE_OSC_UART_SERIAL_SEND_H_
#define _SHARE_OSC_UART_SERIAL_SEND_H_

#include "system/includes.h"

/**
 * @brief 初始化串行发送
 */
void share_osc_uart_serial_send_init(uart_bus_t *udev);

typedef enum {
    ShareOscSendResultSuccess             	= 0, //发送成功
    ShareOscSendResultFailMaxCount			= 1, //发送失败，超过链表设计长度
    ShareOscSendResultMallocFail	        = 2, //malloc失败
} ShareOscSendResult;

/**
 * @brief 添加发送消息到串行发送链表
 *
 * @return ShareOscResult
 */
ShareOscSendResult share_osc_send_buf_list_add(char *buf, u16 len);

/**
 * @brief 收到从机的消息
 */
void share_osc_recieve_msg_from_slave();

/**
 * @brief 清除链表内容
 */
void share_osc_uart_send_list_flush();

#endif

