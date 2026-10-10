// Authored descriptor byte/range boundaries and physical depth dependencies.
// These test the declared local software contract, not page/access silicon.
#include "machine/nv2a_surface_dma.hpp"
#include <array>
#include <cstdint>
#include <cstdio>
#include <limits>

namespace dma = xlle::nv2a_surface_dma;
static unsigned failures, cases;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

static std::array<uint8_t, 32> descriptor(uint32_t flags, uint32_t limit,
                                          uint32_t frame) {
    std::array<uint8_t, 32> bytes{};
    const uint32_t words[] = {flags, limit, frame};
    for (unsigned word = 0; word < 3; ++word)
        for (unsigned lane = 0; lane < 4; ++lane)
            bytes[16 + word * 4 + lane] = uint8_t(words[word] >> (lane * 8));
    return bytes;
}

static void range_cases() {
    using S = dma::RangeStatus;
    struct Case {
        uint32_t flags, limit, frame, offset;
        uint64_t bytes;
        S status;
        uint32_t address;
    };
    // Expected translated addresses and inclusive endpoint results are authored
    // constants. Several change a single boundary of a passing descriptor.
    const Case fixtures[] = {
        {0x3d, 0xff, 0x10000, 0, 256, S::kReady, 0x10000},
        {0x3d, 0xfe, 0x10000, 0, 256, S::kDescriptorLimit, 0},
        {0x3d, 0x1f, 0x10000, 0x10, 16, S::kReady, 0x10010},
        {0x3d, 0x1e, 0x10000, 0x10, 16, S::kDescriptorLimit, 0},
        {0x3d, 0x3ffffff, 0, 0x10000, 256, S::kReady, 0x10000},
        {0x3d, 0x3ffffff, 0, 0, 256, S::kProtectedZeroDestination, 0},
        {0x3d, 0xff, 0x10003, 0, 256, S::kReady, 0x10000},
        {0x3d, 0xff, 0x80010000, 0x20, 32, S::kReady, 0x10020},
        {0x1230003d, 0xff, 0x21000000, 0x45, 32, S::kReady, 0x1000168},
        {0xb03d, 0xff, 0x10000, 0x20, 32, S::kReady, 0x10020},
        {0, 0, 0, 0, 1, S::kUnsupportedClass, 0},
        {0x97, 0xff, 0x10000, 0, 32, S::kUnsupportedClass, 0},
        {0x02, 0xff, 0x10000, 0, 32, S::kUnsupportedClass, 0},
        {0x03, 0xff, 0x10000, 0, 32, S::kUnsupportedClass, 0},
        {0x1003d, 0xff, 0x10000, 0, 32, S::kUnsupportedTarget, 0},
        {0x2003d, 0xff, 0x10000, 0, 32, S::kUnsupportedTarget, 0},
        {0x3003d, 0xff, 0x10000, 0, 32, S::kUnsupportedTarget, 0},
        {0x3d, UINT32_MAX, 0, 0xfffffff0, 32, S::kOffsetOverflow, 0},
        {0x3d, UINT32_MAX, 0, 0, UINT64_MAX, S::kOffsetOverflow, 0},
        {0x3d, 0xfff, 0x07fff000, 0xf00, 256, S::kReady, 0x07ffff00},
        {0x3d, 0xffff, 0x07fff000, 0xff0, 32, S::kPhysicalWrap, 0x07fffff0},
        {0x3d, UINT32_MAX, 0, 0xfffffff0, 16, S::kReady, 0x07fffff0},
    };
    for (const auto& f : fixtures) {
        const auto storage = descriptor(f.flags, f.limit, f.frame);
        const auto original = storage;
        const auto result = dma::resolve_destination(storage.data(), storage.size(),
                                                      true, 16, f.offset, f.bytes);
        ++cases;
        CHECK(result.status == f.status);
        CHECK(result.address == f.address);
        CHECK(result.flags == f.flags && result.limit == f.limit && result.frame == f.frame);
        CHECK(storage == original);
    }
    const auto storage = descriptor(0x3d, 255, 0x10000);
    CHECK(dma::resolve_destination(nullptr, 0, false, UINT32_MAX, UINT32_MAX,
                                   UINT64_MAX).status == S::kUnbound); ++cases;
    CHECK(dma::resolve_destination(nullptr, 32, true, 16, 0, 1).status ==
          S::kMalformedInstance); ++cases;
    CHECK(dma::resolve_destination(storage.data(), 11, true, 0, 0, 1).status ==
          S::kMalformedInstance); ++cases;
    CHECK(dma::resolve_destination(storage.data(), 27, true, 16, 0, 1).status ==
          S::kMalformedInstance); ++cases;
    CHECK(dma::resolve_destination(storage.data(), 28, true, 16, 0, 1).status ==
          S::kReady); ++cases;
    CHECK(dma::resolve_destination(storage.data(), storage.size(), true, 17, 0, 1).status ==
          S::kMalformedInstance); ++cases;
    CHECK(dma::resolve_destination(storage.data(), storage.size(), true, 16, 0, 0).status ==
          S::kEmptyRange); ++cases;
}

static void depth_cases() {
    using L = dma::DepthLayout;
    using S = dma::RangeStatus;
    using D = dma::DepthDisposition;
    struct Case {
        bool test; uint32_t function; bool write, stencil, bound;
        L layout; S range; D result;
    };
    const Case fixtures[] = {
        {false, 0x201, false, false, true, L::kUnsupported, S::kUnsupportedClass, D::kUnused},
        {true, 0x207, false, false, false, L::kAbsent, S::kUnbound, D::kUnused},
        {true, 0x201, false, false, false, L::kAbsent, S::kUnbound, D::kReject},
        {true, 0x201, false, false, true, L::kD24S8, S::kReady, D::kUseStorage},
        {true, 0x200, false, false, true, L::kD24S8, S::kReady, D::kUseStorage},
        {true, 0x207, true, false, true, L::kD24S8, S::kProtectedZeroDestination, D::kReject},
        {true, 0x207, true, false, true, L::kD24S8, S::kReady, D::kUseStorage},
        {false, 0x201, true, false, true, L::kD24S8, S::kReady, D::kUseStorage},
        {false, 0x201, true, false, false, L::kD24S8, S::kUnbound, D::kAbsentOptionalWriteOnly},
        {false, 0x201, true, false, true, L::kAbsent, S::kUnbound, D::kAbsentOptionalWriteOnly},
        {false, 0x201, true, false, true, L::kD24S8, S::kProtectedZeroDestination, D::kProtectedZeroWriteOnly},
        // The bound invalid-layout row retains its unresolved range status;
        // the contrasting unbound row below must not mistake it for absence.
        {false, 0x201, true, false, true, L::kUnsupported, S::kUnbound, D::kReject},
        {false, 0x201, true, false, true, L::kD24S8, S::kDescriptorLimit, D::kReject},
        {false, 0x201, true, false, true, L::kD24S8, S::kUnsupportedClass, D::kReject},
        {true, 0x201, true, false, true, L::kD24S8, S::kPhysicalWrap, D::kReject},
        {false, 0x207, false, true, false, L::kD24S8, S::kUnbound, D::kReject},
        {false, 0x207, false, true, true, L::kD24S8, S::kProtectedZeroDestination, D::kReject},
        {false, 0x207, false, true, true, L::kUnsupported, S::kReady, D::kReject},
        {false, 0x207, false, true, true, L::kD24S8, S::kReady, D::kUseStorage},
        {false, 0x201, true, false, false, L::kUnsupported, S::kUnbound, D::kAbsentOptionalWriteOnly},
    };
    for (const auto& f : fixtures) {
        ++cases;
        CHECK(dma::depth_disposition(f.test, f.function, f.write, f.stencil,
                                     f.bound, f.layout, f.range) == f.result);
    }
}

int main() {
    range_cases();
    depth_cases();
    std::printf("NV2A surface DMA: %u authored cases, %u failures\n", cases, failures);
    return failures ? 1 : 0;
}
