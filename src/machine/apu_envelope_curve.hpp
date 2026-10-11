// Declared nominal envelope policy, not measured MCPX curve/rounding behavior.
// A nonzero rate describes 16*rate output quanta. Exponential decay reaches
// -60 dB over that interval; the endpoint is explicitly zero/sustain. Keeping
// policy here lets later measurements replace coefficients and quantization
// without changing source DMA, resampling, transport or completion ownership.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace xlle::apu_envelope_curve {
struct ReleaseHistory {
    bool active = false;
    uint8_t initial_level = 0;
    uint16_t initial_count = 0, expected_count = 0, rate_count = 0;
};
inline uint8_t exponential_level(uint8_t initial, unsigned remaining,
                                  unsigned total) {
    if (!remaining || !total || !initial) return 0;
    if (remaining >= total) return initial;
    const double elapsed = double(total - remaining) / total;
    return static_cast<uint8_t>(std::floor(initial *
        std::exp(-std::log(1000.0) * elapsed)));
}
inline uint8_t decay_level(uint8_t sustain, unsigned remaining,
                           unsigned total) {
    return std::max(sustain, exponential_level(255, remaining, total));
}
} // namespace xlle::apu_envelope_curve
