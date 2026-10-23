// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <limits>
#include <stdexcept>

// The original animation consumes elapsed millisecond ticks, independently of
// the number of pictures the host manages to present. Keep the 60 Hz deadline
// only as a pacing ceiling; a late frame must use the observed time and retire
// missed deadlines rather than slowing the animation behind the audio stream.
// Offline captures deliberately use frame_tick() for reproducible pictures.
class BootTimeline {
  public:
    static std::uint64_t frame_tick(std::uint64_t frame) {
        return (frame * 1000 + 59) / 60;
    }

    std::uint64_t next_deadline_ms() const {
        return frame_tick(next_frame_);
    }

    std::uint32_t observe(std::uint64_t elapsed_ms) {
        if (elapsed_ms < last_ms_ || elapsed_ms > std::numeric_limits<std::uint32_t>::max())
            throw std::runtime_error("Invalid startup presentation clock");
        last_ms_ = elapsed_ms;
        next_frame_ = elapsed_ms * 60 / 1000 + 1;
        return static_cast<std::uint32_t>(elapsed_ms);
    }

  private:
    std::uint64_t last_ms_ = 0;
    std::uint64_t next_frame_ = 1;
};
