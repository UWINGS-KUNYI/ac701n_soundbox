#include "audio_online_debug.h"
#include "system/includes.h"
#include "generic/list.h"
#include "aud_mic_dut.h"
#include "board_config.h"

typedef struct {
    struct list_head parser_head;

} aud_online_ctx_t;
aud_online_ctx_t *aud_online_ctx = NULL;

int audio_online_debug_init(void)
{
    //aud_online_ctx = zalloc(sizeof(aud_online_ctx_t));
    if (aud_online_ctx) {
        //INIT_LIST_HEAD(&aud_online_ctx->parser_head);
    }
    return 0;

}

int audio_online_debug_open()
{
    /* 麦克风测试 */
#if (defined(TCFG_AUDIO_MIC_DUT_ENABLE) && (TCFG_AUDIO_MIC_DUT_ENABLE == 1))
    aud_mic_dut_open();
#endif/*TCFG_AUDIO_MIC_DUT_ENABLE*/

    return 0;
}
__initcall(audio_online_debug_open);

int audio_online_debug_close()
{
    if (aud_online_ctx) {

    }
    return 0;
}

