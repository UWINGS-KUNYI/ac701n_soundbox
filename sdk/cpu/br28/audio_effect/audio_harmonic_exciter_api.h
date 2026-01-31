#ifndef AUDIO_HARMONIC_EXCITER_API_H
#define AUDIO_HARMONIC_EXCITER_API_H
#include "media/audio_harmonic_exciter.h"
#include "app_config.h"
#include "audio_effect/audio_eff_default_parm.h"


struct audio_harmonic_exciter *audio_harmonic_exciter_open_api(int exciter_name, u32 sample_rate, u32 ch_num);
void audio_harmonic_exciter_close_api(struct audio_harmonic_exciter *hdl);
void audio_harmonic_exciter_update_parm_api(int exciter_name, HarmonicExciterUdateParam *parm, u8 bypass);
#endif
