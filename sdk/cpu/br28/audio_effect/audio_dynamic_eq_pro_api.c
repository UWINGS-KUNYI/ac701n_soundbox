#include "media/dynamic_eq_pro.h"
#include "audio_eff_default_parm.h"
/* 动态eq 精度是precision的话，两段，peak 44100 是30M(不包含信号检测) */

#if defined(TCFG_DYNAMIC_EQ_PRO_ENABLE) && TCFG_DYNAMIC_EQ_PRO_ENABLE


DynamicEQProParam_TOOL_SET  dynamic_eq_pro[mode_add];


struct dynamic_eq_pro *audio_dynamic_eq_pro_open_api(u32 dynamic_eq_pro_name, u32 sample_rate, u8 channel)
{

    u8 tar = 0;
    if (dynamic_eq_pro_name == AEID_AUX_DYNAMIC_EQ) {
        tar = 1;
    }
    struct dynamic_eq_pro *hdl = dynamic_eq_pro_open(dynamic_eq_pro_name, sample_rate, channel, &dynamic_eq_pro[tar]);
#if defined(TCFG_DYNAMIC_EQ_PRO_SPILT_ENABLE)&&TCFG_DYNAMIC_EQ_PRO_SPILT_ENABLE
    struct dynamic_eq_pro_fixparm par = {0};
    par.spilt = 1; //使能独立处理某个声道的效果
    par.start_point = 1;//相应声道起始起始点(右声道)
    dynamic_eq_pro_set_info(hdl, &par);
#endif

    return hdl;
}

void audio_dynamic_eq_pro_close_api(struct dynamic_eq_pro *hdl)
{
    dynamic_eq_pro_close(hdl);
}

void audio_dynamic_eq_pro_update_parm_api(u32 dynamic_eq_pro_name, DynamicEQProParam_TOOL_SET  *parm)
{
    dynamic_eq_pro_update(dynamic_eq_pro_name, parm);
    dynamic_eq_pro_bypass(dynamic_eq_pro_name, parm->is_bypass);
}

#endif
