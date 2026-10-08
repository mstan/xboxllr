// Free-running 32-bit audio sample counter. Rate follows the original USB
// driver's 48 samples/ms synchronization and Cxbx's GetAPUTime reference.
// Time comes from the machine's selected clock, never from register reads.
#pragma once
#include <cstdint>
namespace xlle {
inline uint32_t apu_sample_counter(uint64_t ticks,uint64_t frequency)
{
    // The machine uses 733MHz instruction time; the native adapter uses QPC.
    // Integer seconds are reduced to 32 bits before the intentional wrap.
    return static_cast<uint32_t>(ticks/frequency)*48000u+
           static_cast<uint32_t>((ticks%frequency)*48000u/frequency);
}
}
