
#ifndef _AUDIO_WAY_DAC_H_
#define _AUDIO_WAY_DAC_H_

#include "audio_way.h"

void audio_way_dac_init(void);

struct audio_way *audio_way_dac_open(void);


/*************************************************************************
 * DAC IO中断初始化接口
 *
 * Input    :  private_data     - 私有数据
 *             codec_direction  - CODEC(采样/模拟化)方向选择
 * Output   :  0 - 成功，非0 - 失败.
 * Notes    :  非外部使用接口，仅DAC内部使用.
 * History  :  2023/08/30 by Mashihao.
 *=======================================================================*/
int audio_dac_io_irq_init(void *private_data, u8 codec_direction);

/*************************************************************************
 * DAC IO模式初始化
 *
 * Input    :  l_state  - DACL_IO初始状态(0/1)
 *             r_state  - DACR_IO初始状态(0/1)
 * Output   :  void
 * Notes    :
 * History  :  2023/08/30 by Mashihao.
 *=======================================================================*/
void audio_dac_io_init(u8 l_state, u8 r_state);

/*************************************************************************
 * DAC IO状态设置
 *
 * Input    :  ch  - DAC_IO通道(BIT(0):DACL BIT(1):DACR)
 *             val  - DACR_IO状态(0:低电平 1:高电平)
 * Output   :  void
 * Notes    :
 * History  :  2023/08/30 by Mashihao.
 *=======================================================================*/
void audio_dac_io_set(u8 ch, u8 val);

/*************************************************************************
 * DAC IO关闭
 *
 * Input    :  void
 * Output   :  void
 * Notes    :
 * History  :  2023/08/30 by Mashihao.
 *=======================================================================*/
void audio_dac_io_close(void);


#endif /*_AUDIO_WAY_DAC_H_*/

