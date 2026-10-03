#pragma once

#include <cstddef>
#include <cstdint>

namespace xlle {

// Fixed storage: 31 finite intervals and one explicitly unbounded tail.
// The last interval retains samples for arbitrarily long runs without wrapping
// the instruction clock or allocating on the sampling thread.
struct HostSampleWindows {
  static constexpr size_t count = 32;
  static constexpr uint64_t span = 1500000000ull;

  static constexpr size_t index(uint64_t progress) {
    return progress / span < count - 1
               ? static_cast<size_t>(progress / span) : count - 1;
  }
  static constexpr uint64_t begin(size_t window) { return window * span; }
  static constexpr bool is_tail(size_t window) { return window == count - 1; }
};

// Sampled extrema are inclusive, separate from the half-open nominal interval.
// Track extrema rather than assuming progress cannot move backwards.
struct HostSampleProgressRange {
  bool seen = false;
  uint64_t minimum = 0;
  uint64_t maximum = 0;

  void record(uint64_t progress) {
    if (!seen || progress < minimum) minimum = progress;
    if (!seen || progress > maximum) maximum = progress;
    seen = true;
  }
};

}  // namespace xlle
