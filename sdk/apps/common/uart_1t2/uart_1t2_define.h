#ifndef __UART_1T2_DEFINE_H__
#define __UART_1T2_DEFINE_H__

#include "typedef.h"
#include "system/event.h"
#include "system/includes.h"

#define UART_1T2_SINGLE_IO_SEND_ID 0X32

#define UART_1T2_SUB_OP_CHECK_SINGLE_IO_SEND_BUF				0x00000045	//查询单io命令
#define UART_1T2_SUB_OP_CHECK_SINGLE_IO_SEND_BUF_NO_DATA		0x00000046	//查询单io但无数据命令
#define UART_1T2_SUB_OP_USER_DATA								0x00000047	//用户可通过这个命令发送自定义数据
#define UART_1T2_SUB_OP_POWER_OFF								0x00000048	//用户可通过这个命令发送自定义数据

#endif
