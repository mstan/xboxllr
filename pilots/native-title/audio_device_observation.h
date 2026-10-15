#pragma once
#include "audio_device_pcm_capture.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Elective host observation bound, never simulation input. Bind after both
 * actual consumers and captures are bound, before starting original CPU work.
 * Empty duration disables it; otherwise accept decimal milliseconds 1..3600000.
 * At a serialized work boundary, persist BOTH incomplete captures and exit.
 * This neither closes devices nor certifies original stop/reset/retirement. */
const char *pilot_audio_device_observation_start(const char *milliseconds,
    PilotPcmCapture *pcm,PilotPcmCapture *spdif);
/* Optional diagnostic persistence only; install before start. The callback
 * runs at the serialized observation cutoff and must not enter guest/device
 * services, advance time or release simulation owners. */
const char *pilot_audio_device_observation_on_finish(const char *(*finish)(void));
/* Optional external early cutoff. Configure a nonexistent file before start;
 * creating it requests the same partial persistence as the duration cutoff.
 * Checked at most every250 host ms; never supplies simulated time or success. */
const char *pilot_audio_device_observation_stop_file(const char *path);
#ifdef __cplusplus
}
#endif
