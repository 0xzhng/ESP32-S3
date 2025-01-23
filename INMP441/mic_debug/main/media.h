#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void init_audio_capture();
void audio_loopback();
void init_audio_encoder();
void init_audio_decoder();

#ifdef __cplusplus
}
#endif 