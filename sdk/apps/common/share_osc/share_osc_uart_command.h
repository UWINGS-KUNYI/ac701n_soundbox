#ifndef _SHARE_OSC_UART_COMMAND_H_
#define _SHARE_OSC_UART_COMMAND_H_

#include "system/includes.h"

#define SHARE_OSC_SUB_OP_POWER_OFF					0x00000044	//从机关机

/**
 * @brief share_osc命令相关初始化
 */
void share_osc_command_init();

/**
 * @brief share_osc命令执行共晶振关机流程
 */
void share_osc_soft_poweroff_enter_post();

#endif
