// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "xboot.h"
#include <cstdint>
#include <vector>

struct BootNativeAudioResult {
    std::vector<int16_t> pcm;
    uint32_t blocks=0, note_starts=0, idle_acknowledgements=0;
    uint64_t cutoff_modulation_omitted_frames=0;
    int last_omitted_cutoff_scale=0;
};

// Owns private sample/voice/notifier RAM and the source-derived mono idle
// consumer. No console, original kernel execution, save or HDD is involved.
// Errors propagate; no legacy VP or silence is substituted on failure.
BootNativeAudioResult boot_native_audio(xb_state *state,uint32_t frames);
