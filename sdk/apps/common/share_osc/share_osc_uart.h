#ifndef __SHARE_OSC_UART_H__
#define __SHARE_OSC_UART_H__

#include "typedef.h"
#include "system/event.h"
#include "system/includes.h"

#define SHARE_OSC_READ_LIT_U32(a)   (*((u8*)(a))  + (*((u8*)(a)+1)<<8) + (*((u8*)(a)+2)<<16) + (*((u8*)(a)+3)<<24))
#define SHARE_OSC_WRITE_LIT_U32(a,src)   {*((u8*)(a)+3) = (u8)((src)>>24);  *((u8*)(a)+2) = (u8)(((src)>>16)&0xff);*((u8*)(a)+1) = (u8)(((src)>>8)&0xff);*((u8*)(a)+0) = (u8)((src)&0xff);}

/**
 * @brief 共晶振串口初始化
 */
void share_osc_uart_init(void);

/**
 * @brief 共晶振串口关闭
 */
void share_osc_uart_close(void);

/**
 * @brief 串口发送数据
 */
void share_osc_uart_send_data(u8 *buf, u32 len);

/**
 * @brief 发送共晶振协议内容
 *
 * @pragma buf 数据buf
 * @pragma len 数据长度
 */
void all_assemble_package_send_for_share_osc(u8 *buf, u32 len);

struct share_osc_interface {
    u8 id;
    void (*share_osc_message_deal)(u8 *buf, u32 len);
};

#define REGISTER_SHARE_OSC_DETECT_TARGET(interface) \
	const struct share_osc_interface interface sec(.share_osc_interface)

extern const struct share_osc_interface share_osc_interface_begin[];
extern const struct share_osc_interface share_osc_interface_end[];

#define list_for_each_share_osc_interface(p) \
	for (p = share_osc_interface_begin; p < share_osc_interface_end; p++)

#endif

