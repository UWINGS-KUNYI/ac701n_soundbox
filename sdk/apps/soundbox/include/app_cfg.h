#ifndef APP_CFG_H
#define APP_CFG_H

//*********************************************************************************//
//                                       bt                                        //
//*********************************************************************************//
extern const int bt_back_mode_en;
extern const int bt_emitter_en;

//*********************************************************************************//
//                                       tws                                       //
//*********************************************************************************//
extern const int user_tws_en;
extern const int virtual_fast_connect_for_emitter;
extern const int sync_play_phone_ring;

//*********************************************************************************//
//                                   big(broadcast)                                //
//*********************************************************************************//
extern const int le_broadcast_en;
extern const int lea_broadcast_tx_en;
extern const int lea_broadcast_rx_en;
extern const int broadcast_fixed_role;
extern const int broadcast_data_sync_en;
extern const int broadcasst_tx_local_dec;
extern const int broadcast_receiver_close_edr;
extern const int broadcast_1tn_en;
extern const int broadcast_1t4_en;
extern const int broadcast_tx_track_separate;

//*********************************************************************************//
//                                   cig(connected)                                //
//*********************************************************************************//
extern const int le_connected_en;
extern const int le_connected_close_edr;
extern const int connected_central_close_edr;
extern const int connected_perip_close_edr;
extern const int connected_key_event_sync_en;
extern const int connected_tx_local_dec;
extern const int connected_tx_track_separate;
extern const int connected_2t1_duplex;
extern const int connected_role_config;
extern const int connected_transport_mode;
extern const int lea_connected_central_en;
extern const int lea_connected_perip_en;

#endif

