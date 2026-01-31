#ifndef __DYNAMIC_EQ_PRO_H__
#define __DYNAMIC_EQ_PRO_H__

#include "system/includes.h"
#include "media/audio_stream.h"
#include "media/DynamicEQPro_api.h"
#include "media/drc_api.h"
#include "media/convert_data.h"

#define RUN_NORMAL  0
#define RUN_BYPASS  1

//动态EQ DynamicEQPro:
typedef struct dynamic_eq_pro_tool_set {
    int is_bypass;          // 1-> byass 0 -> no bypass
    int nSection;					//段数
    DynamicEQProEffectParam  effectParam[4];
} DynamicEQProParam_TOOL_SET; //实际发送这个结构体

struct dynamic_eq_pro_fixparm {
    u8 start_point;                    //起始点
    u8 spilt;                          //是否使能只处理其中一个声道
};

struct dynamic_eq_pro {
    struct list_head hentry;                         //
    struct audio_stream_entry entry;	//音频流入口
    void *workbuf;                      //算法运行buf
    DynamicEQProEffectParam *effectParam;
    DynamicEQProParam parm;                //算法相关配置参数
    int dynamic_eq_pro_name;
    struct dynamic_eq_pro_fixparm fixparm;
    u8 channel;
    u8 status;                          //内部运行状态机
    u8 update;                          //设置参数更新标志
};
/*
*********************************************************************
*            dynamic_eq_pro_open
* Description: 动态eq打开
* Arguments  :*parm 检测模块相关参数、若有2段，则parm[0], parm[1],参数连续存方
*             nesection:动态eq检测模块支持的段数
*             channel:输入数据通道数
*             sample_rate:输入数据采样率
* Return	 : 模块句柄.
* Note(s)    : None.
*********************************************************************
*/
struct dynamic_eq_pro *dynamic_eq_pro_open(int dynamic_eq_pro_name, u32 sample_rate, u8 channel, DynamicEQProParam_TOOL_SET *dparam);


/*
*********************************************************************
*            dynamic_eq_pro_run
* Description: 动态eq模块数据处理
* Arguments  :*hdl:模块句柄
*             data:输入数据地址，32bit位宽
*             len:输入数据长度，byte
* Return	 : None.
* Note(s)    : None.
*********************************************************************
*/
int dynamic_eq_pro_run(struct dynamic_eq_pro *hdl, int *data, int len);

/*
*********************************************************************
*            dynamic_eq_pro_bypass
* Description: 动态eq模块设置直通、正常处理
* Arguments  :*hdl:模块句柄
*             bypass:设置直通(RUN_BYPASS)、正常处理(RUN_NORMAL)
* Return	 : None.
* Note(s)    : None.
*********************************************************************
*/
void dynamic_eq_pro_bypass(int dynamic_eq_pro_name, u8 bypass);

/*
*********************************************************************
*            dynamic_eq_bypass
* Description: 动态eq模块设置直通、正常处理
* Arguments  :*hdl:模块句柄
*             bypass:设置直通(RUN_BYPASS)、正常处理(RUN_NORMAL)
* Return	 : None.
* Note(s)    : None.
*********************************************************************
*/
void dynamic_eq_pro_update(int dynamic_eq_pro_name, DynamicEQProParam_TOOL_SET *dparam);


/*
*********************************************************************
*            dynamic_eq_close
* Description: 动态eq检块关闭
* Arguments  :*hdl:模块句柄
* Return	 : None.
* Note(s)    : None.
*********************************************************************
*/
void dynamic_eq_pro_close(struct dynamic_eq_pro *hdl);


struct dynamic_eq_pro *get_cur_dynamic_eq_pro_hdl_by_name(u32 dynamic_eq_pro_name);


void dynamic_eq_pro_set_info(struct dynamic_eq_pro *hdl, struct dynamic_eq_pro_fixparm *parm);



#endif/*__DYNAMIC_EQ_H__*/
