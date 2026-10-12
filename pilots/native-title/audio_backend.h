#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Build-selected audio transport. All failures propagate; no silent clock/output fallback. */
int32_t pilot_audio_open(void);
void pilot_audio_close(void);
int32_t pilot_audio_buffer_create(uint32_t bytes,unsigned channels,unsigned rate,unsigned bits,void **out);
void pilot_audio_buffer_destroy(void *buffer);
int32_t pilot_audio_buffer_copy(void *buffer,uint32_t offset,void *data,uint32_t bytes,int readback);
int32_t pilot_audio_buffer_play(void *buffer,int looping);
#ifdef PILOT_AUDIO_DSP
int32_t pilot_audio_buffer_loop_region(void *buffer,uint32_t start,uint32_t bytes);
int32_t pilot_audio_buffer_mixbins(void *buffer,unsigned count,const uint32_t *bins,const int32_t *volumes);
/* Diagnostic tap of actual mixer input (32 channels) or GP output
 * (6 channels). Caller owns storage until end; production never enables it. */
int32_t pilot_audio_capture_begin(float *samples,uint32_t frames,int after_effects);
uint32_t pilot_audio_capture_end(void);
#endif
int32_t pilot_audio_buffer_stop(void *buffer);
int32_t pilot_audio_buffer_status(void *buffer,uint32_t *status);
int32_t pilot_audio_buffer_position(void *buffer,uint32_t *play,uint32_t *write);
int32_t pilot_audio_buffer_seek(void *buffer,uint32_t position);
int32_t pilot_audio_buffer_volume(void *buffer,int32_t volume);
int32_t pilot_audio_buffer_frequency(void *buffer,uint32_t frequency);
#ifdef PILOT_AUDIO_DSP
struct PilotDsp;
int32_t pilot_audio_effects_install(struct PilotDsp *graph);
#endif
#ifdef __cplusplus
}
#endif
