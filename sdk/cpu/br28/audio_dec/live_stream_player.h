#ifndef _LIVE_STREAM_PLAYER_H_
#define _LIVE_STREAM_PLAYER_H_

void *live_stream_player_open(struct audio_path *ipath, struct audio_path *opath);

void live_stream_player_close(void *player);

void live_stream_player_wakeup(void *player);

int live_stream_player_capture(void *player, struct audio_path *path);

void live_stream_player_capture_stop(void *priv);
#endif
