#ifndef PILOT_AUDIO_DEVICE_LLE_H
#define PILOT_AUDIO_DEVICE_LLE_H
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Sole-engine serialized device transport. Callbacks borrow the engine's
 * synchronous DMA capability; they must never reenter CPU/services or hold
 * the USB lock. A consumer accepts an entire actual little-endian PCM chunk. */
typedef int (*PilotAudioDevicePcm)(void*,const uint8_t*,size_t);
const char *pilot_audio_device_lle_open(PilotAudioDevicePcm consumer,void *context);
/* Close requires a stopped/drained codec and an idle shared engine. Active or
 * paused descriptors, or an engine fault forbidding exact unbind, retain the
 * registered terminal owner until process disposal. No cancellation claimed. */
const char *pilot_audio_device_lle_close(void);
const char *pilot_audio_device_lle_access(uint32_t address,unsigned size,int write,uint32_t *value);
const char *pilot_audio_device_lle_sample(void);
const char *pilot_audio_device_lle_error(void);
/* Passive diagnostics remain available after a terminal guard. */
typedef struct PilotAudioDeviceState {
    uint64_t pcm_words,descriptors,clock_ticks,dma_reads,dma_writes;
    unsigned owned,apu_mapping,aci_mapping,work_binding,gp_held,ep_held;
} PilotAudioDeviceState;
void pilot_audio_device_lle_state(PilotAudioDeviceState *state);
#ifdef __cplusplus
}
#endif
#endif
