#include "app_config.h"
#include "system/includes.h"
#include "broadcast_api.h"
#include "connected_api.h"
#include "wireless_params.h"

//*********************************************************************************//
//                                       bt                                        //
//*********************************************************************************//
const int bt_back_mode_en = TCFG_BLUETOOTH_BACK_MODE;
const int bt_emitter_en = TCFG_USER_EMITTER_ENABLE;

//*********************************************************************************//
//                                       tws                                       //
//*********************************************************************************//
const int user_tws_en = TCFG_USER_TWS_ENABLE;
const int virtual_fast_connect_for_emitter = TCFG_VIRTUAL_FAST_CONNECT_FOR_EMITTER;
const int sync_play_phone_ring = BT_SYNC_PHONE_RING;

//*********************************************************************************//
//                                   big(broadcast)                                //
//*********************************************************************************//
const int le_broadcast_en = TCFG_BROADCAST_ENABLE;
const int lea_broadcast_tx_en = LEA_BIG_CTRLER_TX_EN;
const int lea_broadcast_rx_en = LEA_BIG_CTRLER_RX_EN;
const int broadcast_fixed_role = BROADCAST_FIXED_ROLE;
const int broadcast_data_sync_en = BROADCAST_DATA_SYNC_EN;
const int broadcasst_tx_local_dec = BROADCAST_TX_LOCAL_DEC_EN;
const int broadcast_receiver_close_edr = BROADCAST_RECEIVER_CLOSE_EDR_CONN;
const int broadcast_1tn_en = WIRELESS_1tN_EN;
const int broadcast_1t4_en = WIRELESS_1t4_EN;

#if (BROADCAST_TX_CHANNEL_SEPARATION && (JLA_CODING_CHANNEL == 2))
const int broadcast_tx_track_separate = 1;
#else
const int broadcast_tx_track_separate = 0;
#endif

//*********************************************************************************//
//                                   cig(connected)                                //
//*********************************************************************************//
const int le_connected_en = TCFG_CONNECTED_ENABLE;
const int connected_transport_mode = CIG_TRANSPORT_MODE;
const int lea_connected_central_en = LEA_CIG_CENTRAL_EN;
const int lea_connected_perip_en = LEA_CIG_PERIPHERAL_EN;
const int connected_central_close_edr = CONNECTED_CENTRAL_CLOSE_EDR_CONN;
const int connected_perip_close_edr = CONNECTED_PERIPHERAL_CLOSE_EDR_CONN;
const int le_connected_close_edr = connected_central_close_edr + connected_perip_close_edr;
const int connected_key_event_sync_en = CONNECTED_KEY_EVENT_SYNC;
const int connected_tx_local_dec = CONNECTED_TX_LOCAL_DEC_EN;
const int connected_2t1_duplex = WIRELESS_2T1_DUPLEX_EN;
const int connected_role_config = CONNECTED_ROLE_CONFIG;

#if (CONNECTED_TX_CHANNEL_SEPARATION && (JLA_CODING_CHANNEL == 2))
const int connected_tx_track_separate = 1;
#else
const int connected_tx_track_separate = 0;
#endif


