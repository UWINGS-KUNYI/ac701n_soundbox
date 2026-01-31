#include "audio_harmonic_exciter_api.h"
#include "app_config.h"
#include "audio_effect/audio_eff_default_parm.h"


#if defined(TCFG_MUSIC_HARMONIC_EXCITER_ENABLE) &&TCFG_MUSIC_HARMONIC_EXCITER_ENABLE ||defined(TCFG_MIC_HARMONIC_EXCITER_ENABLE) &&TCFG_MIC_HARMONIC_EXCITER_ENABLE


#if defined(TCFG_MUSIC_HARMONIC_EXCITER_ENABLE) &&TCFG_MUSIC_HARMONIC_EXCITER_ENABLE
harmonic_exciter_param_tool_set music_exciter_parm;
#endif

struct audio_harmonic_exciter *audio_harmonic_exciter_open_api(int exciter_name, u32 sample_rate, u32 ch_num)
{
    harmonic_exciter_param_tool_set *parm = NULL;
    struct harmonic_exciter_param param = {0};
    if (exciter_name == AEID_MIC_HARMONIC_EXCITER) {
#if defined(TCFG_MIC_EFFECT_ENABLE) && TCFG_MIC_EFFECT_ENABLE
#if defined(TCFG_MIC_HARMONIC_EXCITER_ENABLE) &&TCFG_MIC_HARMONIC_EXCITER_ENABLE
        u8 mode = get_mic_eff_mode();
        parm = &eff_mode[mode].exciter_parm;
#endif
#endif
    } else if (exciter_name == AEID_MUSIC_HARMONIC_EXCITER) {
#if defined(TCFG_MUSIC_HARMONIC_EXCITER_ENABLE) &&TCFG_MUSIC_HARMONIC_EXCITER_ENABLE
        parm = &music_exciter_parm;
#endif
    }
    if (!parm) {
        return NULL;
    }
    /* printf("sr %d, ch_num %d\n", sample_rate, ch_num); */
    /* printf("cfg.is_bypass %d, flow_cut %d, fhigh_cut:%d, wetgain %d drygain %d, excitType %d\n", */
    /* parm->is_bypass, parm->parm.flow_cut, parm->parm.fhigh_cut, parm->parm.wetgain, parm->parm.drygain, parm->parm.excitType); */
    if (global_bit_wide) {
        param.param.dataTypeobj.IndataBit = global_bit_wide;
        param.param.dataTypeobj.OutdataBit = global_bit_wide;
    } else if (exciter_name == AEID_MUSIC_HARMONIC_EXCITER) {
#if defined(AUDIO_VBASS_32BIT_OUT_EN) && AUDIO_VBASS_32BIT_OUT_EN
        param.param.dataTypeobj.IndataBit = 1;
        param.param.dataTypeobj.OutdataBit = 1;
#else
        param.param.dataTypeobj.IndataBit = 0;
        param.param.dataTypeobj.OutdataBit = 0;
#endif
    } else {
        param.param.dataTypeobj.IndataBit = 0;
        param.param.dataTypeobj.OutdataBit = 0;
    }

    param.exciter_name = exciter_name;
    param.ch_num = ch_num;
    param.samplerate = sample_rate;
    param.param.flow_cut  = parm->parm.flow_cut;
    param.param.fhigh_cut = parm->parm.fhigh_cut;
    param.param.wetgain   = parm->parm.wetgain;
    param.param.drygain   = parm->parm.drygain;
    param.param.excitType = parm->parm.excitType;

    param.param.dataTypeobj.IndataInc = (ch_num == 2) ? 2 : 1;
    param.param.dataTypeobj.OutdataInc = (ch_num == 2) ? 2 : 1;
    param.param.dataTypeobj.Qval = global_bit_wide ? 23 : 15;
    struct audio_harmonic_exciter *harmonic_exciter = audio_harmonic_exciter_open(&param);
    audio_harmonic_exciter_bypass(param.exciter_name, parm->is_bypass);
    return harmonic_exciter;
}

void audio_harmonic_exciter_close_api(struct audio_harmonic_exciter *hdl)
{
    audio_harmonic_exciter_close(hdl);
}

void audio_harmonic_exciter_update_parm_api(int exciter_name, HarmonicExciterUdateParam *parm, u8 bypass)
{
    audio_harmonic_exciter_update_parm(exciter_name, parm);
    audio_harmonic_exciter_bypass(exciter_name, bypass);
}
#endif
