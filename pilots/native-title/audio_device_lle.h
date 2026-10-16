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
/* Raw digital transport words, with the unchanged programmed format tag. */
typedef int (*PilotAudioDeviceSpdif)(void*,const uint8_t*,size_t,uint32_t);
/* Begins at the pinned kernel4983 initialized idle handoff. SECTL/IEN derive from
 * original init/retirement; FE control uses the retained physical idle value.
 * This is not a board cold-reset constructor or a fence-completion grant. */
const char *pilot_audio_device_lle_open(PilotAudioDevicePcm consumer,void *context);
const char *pilot_audio_device_lle_open_outputs(PilotAudioDevicePcm pcm,void *pcm_context,
    PilotAudioDeviceSpdif spdif,void *spdif_context);
/* Close requires a stopped/drained codec and an idle shared engine. Active or
 * paused descriptors, or an engine fault forbidding exact unbind, retain the
 * registered terminal owner until process disposal. No cancellation claimed. */
const char *pilot_audio_device_lle_close(void);
const char *pilot_audio_device_lle_access(uint32_t address,unsigned size,int write,uint32_t *value);
const char *pilot_audio_device_lle_sample(void);
const char *pilot_audio_device_lle_error(void);
/* Optional bounded passive stage journal, selected before device processing.
 * It borrows existing samples/settings, never guest reads or another clock.
 * finish preserves a diagnostic .partial file and leaves device owners live. */
const char *pilot_audio_device_lle_trace_start(const char *path);
/* Separate256MiB/4guest-second window, configured on the fresh owner. Starts
 * only on the journal's completed Stop of a previously played retained object.
 * Copies all concurrent existing stage events, including silent samples. */
const char *pilot_audio_device_lle_retained_trace_start(const char *path);
const char *pilot_audio_device_lle_retained_trace_trigger(uint64_t lifetime,
    uint32_t object,uint64_t prior_play,uint64_t stop);
const char *pilot_audio_device_lle_trace_finish(void);
/* Passive diagnostics remain available after a terminal guard. */
typedef struct PilotAudioDeviceState {
    uint64_t pcm_words,descriptors,clock_ticks,dma_reads,dma_writes;
    uint64_t lifetime_id,epoch_ticks,codec_frame;
    unsigned owned,apu_mapping,aci_mapping,work_binding,gp_held,ep_held;
    uint64_t gp_cycles,ep_cycles;
    unsigned gp_phase,ep_phase;
    uint64_t gp_quanta,ep_events;
    uint64_t spdif_words,spdif_descriptors;
} PilotAudioDeviceState;
void pilot_audio_device_lle_state(PilotAudioDeviceState *state);
uint64_t pilot_audio_device_lle_fences_published(void);
/* Passive lifetime check; no locking, services, clock input or device work. */
int pilot_audio_device_lle_consumer_attached(void *context);
int pilot_audio_device_lle_spdif_consumer_attached(void *context);
#ifdef __cplusplus
}
#endif
#endif
