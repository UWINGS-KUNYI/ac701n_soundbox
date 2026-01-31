#ifndef __UART_1T2_H__
#define __UART_1T2_H__

#include "typedef.h"
#include "system/event.h"
#include "system/includes.h"

#define UART_1T2_READ_LIT_U32(a)   (*((u8*)(a))  + (*((u8*)(a)+1)<<8) + (*((u8*)(a)+2)<<16) + (*((u8*)(a)+3)<<24))
#define UART_1T2_WRITE_LIT_U32(a,src)   {*((u8*)(a)+3) = (u8)((src)>>24);  *((u8*)(a)+2) = (u8)(((src)>>16)&0xff);*((u8*)(a)+1) = (u8)(((src)>>8)&0xff);*((u8*)(a)+0) = (u8)((src)&0xff);}

/**
 * @brief 串口1t2初始化
 */
void uart_1t2_init(void);

/**
 * @brief 串口1t2关闭
 */
void uart_1t2_close(void);

/**
 * @brief 串口发送数据
 */
void uart_1t2_send_data(u8 *buf, u32 len);

/**
 * @brief 发送uart 1t2协议内容
 *
 * @pragma msg_to 接收角色
 * @pragma buf 数据buf
 * @pragma len 数据长度
 */
void all_assemble_package_send_for_uart_1t2(u8 msg_to, u8 *buf, u32 len);

struct uart_1t2_interface {
    u8 id;
    void (*uart_1t2_msg_deal)(u8 *msg_from, u8 *buf, u32 len);
};

#define REGISTER_UART_1T2_DETECT_TARGET(interface) \
	const struct uart_1t2_interface interface sec(.uart_1t2_interface)

extern const struct uart_1t2_interface uart_1t2_interface_begin[];
extern const struct uart_1t2_interface uart_1t2_interface_end[];

#define list_for_each_uart_1t2_interface(p) \
	for (p = uart_1t2_interface_begin; p < uart_1t2_interface_end; p++)

#endif

