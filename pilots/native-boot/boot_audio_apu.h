// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "xboot.h"
#include "machine/apu.hpp"
#include <array>
#include <functional>
#include <string>

// Thin native VP transport. The caller owns the real shared APU, RAM/SGEs,
// allocation handles, signal policy and original idle/IRQ consumer. This class
// owns none of those lifetimes and never guesses an idle acknowledgement.
class BootApuVoiceAdapter {
  public:
    struct Source { uint32_t byte_offset=0,frames=0; };
    using Resolve=std::function<bool(uint32_t table,uint32_t frames,Source&)>;
    using ReadWord=std::function<bool(uint16_t voice,uint32_t field,uint32_t&)>;
    enum class Status { Produced, NeedsIdleService, Failed };
    using Bins=std::array<std::array<int32_t,XB_AUDIO_BLOCK>,XB_AUDIO_BINS>;
    BootApuVoiceAdapter(xlle::McpxApu &apu,const std::array<uint16_t,XB_AUDIO_CHANNELS>& handles,
                        Resolve source,ReadWord read);
    bool submit(const xb_audio_native_event &event);
    bool feedback(uint32_t actual_library_idle_ack,uint32_t &hardware_active,uint32_t &library_idle_ack);
    Status quantum(Bins &bins);
    const std::string &error() const { return error_; }
  private:
    xlle::McpxApu &apu_;
    const std::array<uint16_t,XB_AUDIO_CHANNELS> handles_;
    Resolve source_;
    ReadWord read_;
    uint32_t allocated_=0;
    std::string error_;
    bool fail(const char *message);
    bool method(uint32_t address,uint32_t value);
};
