#include "audio_vbass_demo.h"
#include "app_config.h"
#include "audio_eff_default_parm.h"
//24M mips
#if AUDIO_VBASS_CONFIG
VirtualBass_TOOL_SET vbass_parm[mode_add];

vbass_hdl *audio_vbass_open_demo(u32 vbass_name, u32 sample_rate, u8 ch_num)
{
    VirtualBassParam parm = {0};
    u8 tar = 0;
#if defined(LINEIN_MODE_SOLE_EQ_EN) && LINEIN_MODE_SOLE_EQ_EN
    if (vbass_name == AEID_AUX_VBASS) {
        tar = 1;
    }
#endif

    u8 bypass  = vbass_parm[tar].is_bypass;
    parm.ratio = vbass_parm[tar].parm.ratio;
    parm.boost = vbass_parm[tar].parm.boost;
    parm.fc    = vbass_parm[tar].parm.fc;
    parm.channel = ch_num;
    parm.SampleRate = sample_rate;
    if (global_bit_wide) {
        parm.pcm_info.IndataBit = DATA_INT_32BIT;
        parm.pcm_info.OutdataBit = DATA_INT_32BIT;
    } else {
        parm.pcm_info.IndataBit = DATA_INT_16BIT;
#if defined(AUDIO_VBASS_32BIT_OUT_EN)&&AUDIO_VBASS_32BIT_OUT_EN
        parm.pcm_info.OutdataBit = DATA_INT_32BIT;
#else
        parm.pcm_info.OutdataBit = DATA_INT_16BIT;
#endif
    }
    parm.pcm_info.IndataInc = (ch_num == 2) ? 2 : 1;
    parm.pcm_info.OutdataInc = (ch_num == 2) ? 2 : 1;
    parm.pcm_info.Qval = global_bit_wide ? 23 : 15;
    //printf("vbass ratio %d, boost %d, fc %d, channel %d, SampleRate %d\n", parm.ratio, parm.boost, parm.fc,parm.channel, parm.SampleRate);
    vbass_hdl *vbass = audio_vbass_open(vbass_name, &parm);
    vbass->fade = 1;                //参数淡入使能
    vbass->fade_step = 1;           //ratio淡入步进
    vbass->fade_ratio = parm.ratio;  //设置淡入目标初值
    vbass->lowfreq_en = 1;          //1:保留低频；0:不保留

#if defined(TCFG_VBASS_SPILT_ENABLE)&&TCFG_VBASS_SPILT_ENABLE
    struct virtual_bass_fixparm fixparm = {0};
    fixparm.spilt = 1;
    fixparm.start_point = 1;//右声道
    audio_vbass_set_info(vbass, &fixparm);
#endif

#if 0
    int ret = syscfg_read(CFG_AUDIO_VBASS_SW, &bypass, sizeof(bypass));
    if (ret < 0) {
        bypass  = vbass_parm[tar].is_bypass;
    }
    printf("---------%s bypass %d", __FUNCTION__, bypass);
#endif
    audio_vbass_bypass(vbass_name, bypass);
    clock_add(DEC_VBASS_CLK);
    return vbass;
}


void audio_vbass_close_demo(vbass_hdl *vbass)
{
    if (vbass) {
        audio_vbass_close(vbass);
        vbass = NULL;
    }
    clock_remove(DEC_VBASS_CLK);
}



void audio_vbass_update_demo(u32 vbass_name, VirtualBassUdateParam *parm, u32 bypass)
{
    audio_vbass_parm_update(vbass_name, parm);
    audio_vbass_bypass(vbass_name, bypass);
}


void audio_vbass_set_bypass(u32 vbass_name, u32 bypass)
{
#if 0
    audio_vbass_bypass(vbass_name, bypass);
#endif
}

#if AUDIO_VBASS_LINK_VOLUME

static VirtualBassUdateParam v_list[] = {
    //ratio    boost   fc
    {40,      1,      100},
    {30,      1,      100},
    {20,      1,      100},
    {10,      1,      100},
};
void audio_vbass_link_volume(u32 vbass_name, u8 max_vol, u8 cur_vol)
{
    VirtualBassParam parm = {0};
    u8 tar = (vbass_name == AEID_AUX_VBASS) ? 1 : 0;
    u8 group;
    if ((0 <= cur_vol) && (cur_vol <= (max_vol / 4))) {
        group = 0;
    } else if (((max_vol / 4) < cur_vol) && (cur_vol <= (max_vol * 2 / 4))) {
        group = 1;
    } else if (((max_vol * 2 / 4) < cur_vol) && (cur_vol <= (max_vol * 3 / 4))) {
        group = 2;
    } else if (((max_vol * 3 / 4) < cur_vol) && (cur_vol <= max_vol)) {
        group = 3;
    } else {
        group = 0;
    }
    VirtualBassUdateParam list = v_list[group];
    vbass_parm[tar].parm.ratio = list.ratio;
    vbass_parm[tar].parm.boost = list.boost;
    vbass_parm[tar].parm.fc = list.fc;
    printf("cur_vol %d, max_vol %d, group %d, ratio %d, boost %d, fc %d\n", cur_vol, max_vol, group, vbass_parm[tar].parm.ratio, vbass_parm[tar].parm.boost, vbass_parm[tar].parm.fc);
    audio_vbass_parm_update(vbass_name, &vbass_parm[tar].parm);
}
#endif

#endif
