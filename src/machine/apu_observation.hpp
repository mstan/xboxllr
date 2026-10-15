// Borrowed, synchronous observations of completed audio work. Observers copy
// existing data only: no guest reads, reentry, clocks, processing or ownership.
// Pointers expire on return. Metadata/data use the host's native representation;
// a persistence consumer must declare its encoding. No observer is the default.
#pragma once
#include <cstdint>
#include <functional>

namespace xlle {
enum class ApuObservationKind : uint32_t {
    Source=1, Rate=2, Send=3, Mix=4, GpFifo=5, EpInput=6, EpFifo=7, GpOutput=8
};
struct ApuObservation {
    ApuObservationKind kind;
    uint32_t id;
    const uint32_t* metadata;
    uint32_t metadata_words;
    const void* data;
    uint32_t bytes;
};
using ApuObserver=std::function<void(const ApuObservation&)>;
} // namespace xlle
