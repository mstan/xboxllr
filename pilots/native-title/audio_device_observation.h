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
#ifdef __cplusplus
}
#endif
