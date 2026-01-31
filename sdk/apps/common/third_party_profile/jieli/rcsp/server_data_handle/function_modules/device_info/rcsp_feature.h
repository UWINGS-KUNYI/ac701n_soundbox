#ifndef __RCSP_FEATURE_H__
#define __RCSP_FEATURE_H__

#include "typedef.h"
#include "app_config.h"

enum {
    FEATURE_ATTR_TYPE_PROTOCOL_VERSION = 0,
    FEATURE_ATTR_TYPE_SYS_INFO         = 1,
    FEATURE_ATTR_TYPE_EDR_ADDR         = 2,
    FEATURE_ATTR_TYPE_PLATFORM         = 3,
    FEATURE_ATTR_TYPE_FUNCTION_INFO    = 4,
    FEATURE_ATTR_TYPE_DEV_VERSION      = 5,
    FEATURE_ATTR_TYPE_SDK_TYPE         = 6,
    FEATURE_ATTR_TYPE_UBOOT_VERSION    = 7,
    FEATURE_ATTR_TYPE_DOUBLE_PARITION  = 8,
    FEATURE_ATTR_TYPE_UPDATE_STATUS    = 9,
    FEATURE_ATTR_TYPE_DEV_VID_PID      = 10,
    FEATURE_ATTR_TYPE_DEV_AUTHKEY      = 11,
    FEATURE_ATTR_TYPE_DEV_PROCODE      = 12,
    FEATURE_ATTR_TYPE_DEV_MAX_MTU      = 13,
    FEATURE_ATTR_TYPE_CONNECT_BLE_ONLY = 17,
    FEATURE_ATTR_TYPE_BT_EMITTER_INFO  = 18,
    FEATURE_ATTR_TYPE_MD5_GAME_SUPPORT = 19,
    FEATURE_ATTR_TYPE_FILE_TRANSFER_INFO = 21,
    FEATURE_ATTR_TYPE_MAX,
};

// le aduio mode
typedef enum {
    JL_LeAudioModeNone             			= 0x00,
    JL_LeAudioModeBig						= 0x01,
    JL_LeAudioModeCig						= 0x02,
} JL_LeAudioMode;
void setLeAudioModeMode(JL_LeAudioMode mode);
JL_LeAudioMode getLeAudioModeMode();

u32 target_feature_parse_packet(void *priv, u8 *buf, u16 buf_size, u32 mask);

#endif//__RCSP_FEATURE_H__

