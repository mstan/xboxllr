#pragma once
#include "io_device_lle.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Private reference composition: one stopped controller owns this state.
 * The external driver defines input cadence, not retired CPU instructions or
 * hardware cycles. Audio advances actual frames before publishing PIT input.
 * Observers and consumer attach/detach never advance time. */
typedef const char *(*PilotReferenceAudioAdvance)(void *context,uint32_t frames);
typedef struct PilotReferenceClock {
    PilotIoClock input;
    PilotIoClockStep drive;
    void *input_context;
    PilotReferenceAudioAdvance audio;
    void *audio_context;
    uint64_t origin,ticks,frames,audio_frame,pending;
    uint32_t max_step;
    uint64_t work_blocks;
    uint32_t work_blocks_per_ms,work_fraction;
    const char *failure;
    int initialized,busy;
} PilotReferenceClock;
/* Initialize zeroed storage. Reinitialization requires retiring the old binding. */
const char *pilot_reference_clock_init(PilotReferenceClock *clock,PilotIoClock input,
    PilotIoClockStep drive,void *context);
const char *pilot_reference_clock_audio(PilotReferenceClock *clock,
    PilotReferenceAudioAdvance advance,void *context);
/* Bound publication per stopped CPU opportunity. Zero preserves unbounded
 * admission. A proposed input interval is retained and drained exactly before
 * asking the driver for another interval; reads never drain it. This controls
 * CPU/device interleaving, not oscillator rate, pulse width or CPU cycles.
 * Change only with no interval pending and no advance in progress. */
const char *pilot_reference_clock_quantum(PilotReferenceClock *clock,uint32_t max_ticks);
/* Optional functional work limit for a quantum-bounded composition. Each new
 * observed block permits1125/blocks_per_ms ticks, retaining only the fractional
 * remainder. Whole credits never accumulate across stopped opportunities:
 * extra zero-work import boundaries cannot drain old input debt. Capped work
 * is discarded, so arbitrary workload chunking need not produce equal times.
 * This is an interleaving policy, not hardware CPU-cycle timing. Configure only
 * stopped/drained; the explicit cumulative baseline starts a new fraction.
 * Zero blocks_per_ms disables this additional limit. */
const char *pilot_reference_clock_work(PilotReferenceClock *clock,
    uint32_t blocks_per_ms,uint64_t initial_blocks);
const char *pilot_reference_clock_observe(void *context,uint64_t *ticks);
/* Pure nominal733333333-Hz counter on the same absolute PIT epoch. This
 * eliminates host-time leakage; it does not qualify physical CPU cycle timing. */
const char *pilot_reference_clock_tsc(void *context,uint64_t *counter);
const char *pilot_reference_clock_step(void *context,uint64_t block_entries);
#ifdef __cplusplus
}
#endif
