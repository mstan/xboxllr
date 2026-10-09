// Xbox VP block adapter; the codec is the separately credited dr_wav donor.
#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

// Exactly one 36-byte/channel Xbox block -> 64 interleaved S16 frames.
// Mono/stereo only; output capacity is in samples. No output changes on failure.
// Reserved header bytes are ignored. Signed step-index clamping follows the
// pinned emulator reference; this is not a new silicon measurement.
int xlle_apu_decode_adpcm(const uint8_t* block, size_t bytes, unsigned channels,
                          int16_t* output, size_t samples);

#ifdef __cplusplus
}
#endif
