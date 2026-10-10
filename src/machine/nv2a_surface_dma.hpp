// Generic software surface destinations: explicit range and depth disposition.
// This is a bounded local, flat mapping contract, not complete DMA silicon
// semantics. Target/page/access/coherency extensions require their own mapper.
#pragma once

#include <cstddef>
#include <cstdint>

namespace xlle::nv2a_surface_dma {

enum class RangeStatus {
    kReady,
    kUnbound,
    kEmptyRange,
    kMalformedInstance,
    kUnsupportedClass,
    kUnsupportedTarget,
    kOffsetOverflow,
    kDescriptorLimit,
    kPhysicalWrap,
    kProtectedZeroDestination,
};

struct DestinationRange {
    RangeStatus status = RangeStatus::kUnbound;
    uint32_t address = 0;
    uint32_t flags = 0, limit = 0, frame = 0;
    uint32_t dma_class = 0, dma_target = 0;
};

inline uint32_t read_le32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) |
        (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

inline DestinationRange resolve_destination(const uint8_t* pramin,
        size_t pramin_size, bool bound, uint32_t instance, uint32_t offset,
        uint64_t bytes) {
    DestinationRange result;
    const auto stop = [&](RangeStatus status) {
        result.status = status;
        return result;
    };
    if (!bound) return result;
    if (!bytes) return stop(RangeStatus::kEmptyRange);
    // RAMHT represents instances in 16-byte units. Check before pointer math.
    if (!pramin || (instance & 15) || pramin_size < 12 ||
        instance > pramin_size - 12)
        return stop(RangeStatus::kMalformedInstance);
    result.flags = read_le32(pramin + instance);
    result.limit = read_le32(pramin + instance + 4);
    result.frame = read_le32(pramin + instance + 8);
    result.dma_class = result.flags & 0xfff;
    result.dma_target = (result.flags >> 16) & 3;
    // The surface contract requires IN_MEMORY; other DMA users are unchanged.
    if (result.dma_class != 0x3d) return stop(RangeStatus::kUnsupportedClass);
    if (result.dma_target != 0) return stop(RangeStatus::kUnsupportedTarget);
    // Retain raw page/access/coherency flags without assigning new semantics.
    if (bytes - 1 > uint64_t(UINT32_MAX) - offset)
        return stop(RangeStatus::kOffsetOverflow);
    const uint64_t last_offset = uint64_t(offset) + bytes - 1;
    if (last_offset > result.limit) return stop(RangeStatus::kDescriptorLimit);
    const uint32_t base = (result.frame & 0xfffff000u) |
        ((result.flags & 0xfff00000u) >> 20);
    // Preserve the existing 27-bit local address fold and in-aperture aliases.
    result.address = uint32_t((uint64_t(base) + offset) & 0x07ffffffu);
    if (bytes - 1 > uint64_t(0x07ffffffu) - result.address)
        return stop(RangeStatus::kPhysicalWrap);
    // A valid-looking identity descriptor cannot establish intentional zero
    // destination use. Keep boot low-memory protection as explicit safety debt.
    if (!result.address) return stop(RangeStatus::kProtectedZeroDestination);
    return stop(RangeStatus::kReady);
}

enum class DepthLayout { kD24S8, kAbsent, kUnsupported };
enum class DepthDisposition {
    kUnused,
    kUseStorage,
    kAbsentOptionalWriteOnly,
    kProtectedZeroWriteOnly,
    kReject,
};

inline DepthDisposition depth_disposition(bool test, uint32_t function,
        bool write, bool stencil, bool bound, DepthLayout layout,
        RangeStatus range) {
    // ALWAYS with writes/stencil off has no physical zeta dependency. This
    // does not alter geometry/depth clipping, which is independent of storage.
    const bool compare_reads = test && function != 0x0207;
    if (!compare_reads && !write && !stencil) return DepthDisposition::kUnused;
    // Enabled comparison, enabled test with writes, and stencil are strict.
    if (test || stencil) {
        return bound && layout == DepthLayout::kD24S8 && range == RangeStatus::kReady
            ? DepthDisposition::kUseStorage : DepthDisposition::kReject;
    }
    // These two compatibility cases do not claim a completed depth write.
    // Actual binding is independent of range resolution: a bound unsupported
    // layout may never resolve a range, so RangeStatus alone cannot mean absent.
    if (layout == DepthLayout::kAbsent || !bound)
        return DepthDisposition::kAbsentOptionalWriteOnly;
    if (layout != DepthLayout::kD24S8) return DepthDisposition::kReject;
    if (range == RangeStatus::kProtectedZeroDestination)
        return DepthDisposition::kProtectedZeroWriteOnly;
    return range == RangeStatus::kReady ? DepthDisposition::kUseStorage
                                       : DepthDisposition::kReject;
}

inline const char* range_status_name(RangeStatus status) {
    switch (status) {
    case RangeStatus::kReady: return "ready";
    case RangeStatus::kUnbound: return "unbound";
    case RangeStatus::kEmptyRange: return "empty_range";
    case RangeStatus::kMalformedInstance: return "malformed_instance";
    case RangeStatus::kUnsupportedClass: return "unsupported_class";
    case RangeStatus::kUnsupportedTarget: return "unsupported_target";
    case RangeStatus::kOffsetOverflow: return "offset_overflow";
    case RangeStatus::kDescriptorLimit: return "descriptor_limit";
    case RangeStatus::kPhysicalWrap: return "physical_wrap";
    case RangeStatus::kProtectedZeroDestination: return "protected_zero_destination";
    }
    return "unknown";
}

inline const char* depth_disposition_name(DepthDisposition disposition) {
    switch (disposition) {
    case DepthDisposition::kUnused: return "unused";
    case DepthDisposition::kUseStorage: return "use_storage";
    case DepthDisposition::kAbsentOptionalWriteOnly: return "absent_optional_write_only";
    case DepthDisposition::kProtectedZeroWriteOnly: return "protected_zero_write_only";
    case DepthDisposition::kReject: return "reject";
    }
    return "unknown";
}

} // namespace xlle::nv2a_surface_dma
