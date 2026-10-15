#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Fixed reference input policy for the original-audio build. The original
 * kernel must already have programmed PIT. A block opportunity is explicit
 * simulation input, not a hardware CPU-cycle estimate or host elapsed time.
 * The input observer receives milliseconds since this timeline's origin. */
typedef uint32_t (*PilotAudioDeviceInputTime)(uint64_t milliseconds);
const char *pilot_audio_device_timeline_start(unsigned blocks_per_ms,
    PilotAudioDeviceInputTime input_time);
/* Pure shared nominal TSC, also bound to the sole original CPU's RDTSC. */
const char *pilot_audio_device_timeline_counter(uint64_t *counter);
#ifdef __cplusplus
}
#endif
