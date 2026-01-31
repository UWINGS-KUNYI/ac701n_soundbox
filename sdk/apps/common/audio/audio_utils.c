/*
 ************************************************************
 *					Audio Utils
 *Brief:数字信号处理常用模块合集
 *Notes:
 ************************************************************
 */

#include "audio_utils.h"

/*
*********************************************************************
*                  Audio Digital Phase Inverter
* Description: 数字反相器，用来反转数字音频信号的相位
* Arguments  : dat  数据buf地址
*			   len	数据长度(unit:byte)
* Return	 : None.
* Note(s)    : None.
*********************************************************************
*/
void digital_phase_inverter_s16(s16 *dat, int len)
{
    int data_size = len / 2;
    for (int i = 0; i < data_size; i++) {
        dat[i] = (dat[i] == -32768) ? 32767 : -dat[i];
    }
}
void digital_phase_inverter_s24(s32 *dat, int len)
{
    s32 *data32 = (s32 *)dat;
    int data_size = len / 4;
    for (int i = 0; i < data_size; i++) {
        data32[i] = (data32[i] == -8388608) ? 8388607 : -data32[i];
    }
}

