#pragma once
#include "io_device_lle.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Private reference composition: one stopped controller owns this state.
 * The external driver defines input cadence; this module defines no CPU/TSC
 * model. Audio advances actual frames before publishing the new PIT input.
 * Observers and consumer attach/detach never advance time. */
typedef const char *(*PilotReferenceAudioAdvance)(void *context,uint32_t frames);
typedef struct PilotReferenceClock {
    PilotIoClock input;
    PilotIoClockStep drive;
    void *input_context;
    PilotReferenceAudioAdvance audio;
    void *audio_context;
    uint64_t origin,ticks,frames,audio_frame;
    const char *failure;
    int initialized,busy;
} PilotReferenceClock;
/* Initialize zeroed storage. Reinitialization requires retiring the old binding. */
const char *pilot_reference_clock_init(PilotReferenceClock *clock,PilotIoClock input,
    PilotIoClockStep drive,void *context);
const char *pilot_reference_clock_audio(PilotReferenceClock *clock,
    PilotReferenceAudioAdvance advance,void *context);
const char *pilot_reference_clock_observe(void *context,uint64_t *ticks);
const char *pilot_reference_clock_step(void *context,uint64_t block_entries);
#ifdef __cplusplus
}
#endif
