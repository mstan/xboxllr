#pragma once
#include <stdint.h>
#include "audio_backend.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Private reference only. Advance emits exactly requested frames, including
 * zero-source frames, or returns a latched failure with actual state retained.
 * No OS audio device, callback thread, wall-clock provider or synthetic cursor. */
int32_t reference_advance_frames(uint32_t frames);
uint64_t reference_output_frames(void); /* pure actual emitted-frame count */
typedef struct ReferenceAudioSourceState {
 uint64_t lifetime_id,source_frames_fetched;
 uint32_t cbo_frames,cbo_bytes,frequency,alignment,playing,looping,drained;
} ReferenceAudioSourceState;
int32_t reference_source_state(void *buffer,ReferenceAudioSourceState *out);
/* Position observes actual source CBO in bytes; the executed original full-ring
 * rule supplies write=(CBO bytes+32*alignment)%capacity for full-ring playing.
 * A qualified clean partial loop uses the original literal start+safe%span
 * branch while CBO is inside that region. Stopped
 * positions use the saved explicit Stop/Seek cursor. Natural finite completion
 * remains guarded. Source fetch/prefetch is not output retirement. */
typedef struct ReferenceAudioRead {
 uint64_t lifetime_id,output_frame;
 uint32_t source_offset,physical_address,bytes;
} ReferenceAudioRead;
typedef void (*ReferenceAudioReadTap)(void *context,const ReferenceAudioRead *record,const void *actual_bytes);
int32_t reference_set_read_tap(ReferenceAudioReadTap tap,void *context);
/* Read taps execute synchronously inside advance and must not reenter any API. */
int32_t reference_effects_detach(struct PilotDsp *expected);
/* Opt-in diagnostic epoch binding and stopped-owner finalization. These never
 * advance time, source consumption or output. Engine open is not a PIT epoch. */
int32_t reference_capture_epoch(uint64_t ticks);
int32_t reference_capture_finish(void);
#ifdef PILOT_AUDIO_REFERENCE_TEST
uint64_t reference_state_hash(void); /* pure observed RAM/carry/DSP state */
/* Private test observer: copies maintained configuration words without writes.
 * Returns route,format,LBO,EBO,pitch,attenuation0..2; not a runtime owner API. */
int32_t reference_voice_configuration(void *buffer,uint32_t words[8]);
#endif
#ifdef __cplusplus
}
#endif
