// xlle — coarse subsystem wall-time accounting behind --perf-accounting (beads-xbox.1.13).
//
// Purpose: attribute host wall time across the handful of places the throughput
// investigation (beads-xbox.1.13) needs numbers for, without requiring a working
// sampling profiler (this box's MinGW/DWARF toolchain has no PDB, so Windows
// Performance Analyzer cannot symbolicate stack samples out of the box — see the
// perf-plan writeup). Default replays pay nothing: `g_enabled` stays false and every
// ScopedAdd degenerates to a single bool load in its ctor/dtor, no clock read, no
// atomic add. Only --perf-accounting flips it on.
//
// Buckets (NOT a mutually-exclusive partition — see notes):
//   cpu                -- wall time inside uc_emu_start (UnicornFloor::run). Unicorn
//                         invokes MMIO/IO callbacks synchronously on this same call
//                         stack, so nv2a_pushbuffer and raster below are SUBSETS of
//                         this bucket, not siblings of it. "cpu minus nv2a_pushbuffer"
//                         is the part of this bucket that is genuine guest instruction
//                         dispatch/translation.
//   nv2a_pushbuffer    -- wall time inside Nv2a::process_pushbuffer (PFIFO word fetch +
//                         method dispatch for one doorbell kick). Inclusive of `raster`.
//   raster             -- wall time inside the single rasterize_witnessed_batch() call
//                         site (every witnessed fast-path Profile plus the generic-
//                         combiner fallback, i.e. all per-pixel work).
//   host_poll_devices  -- wall time in the XboxMachine::run() chunk-loop body OUTSIDE
//                         cpu_->run(): PIC pending-interrupt checks, PIT/NV2A/APU/ACI/
//                         OHCI frame-clock advances, the host_poll_cb_ callback (SDL
//                         window pump + input), and interrupt-injection bookkeeping.
//                         This bucket IS disjoint from the other three.
#pragma once
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>

namespace xlle::perf {

// Set once from --perf-accounting before the replay starts; never toggled mid-run.
inline bool g_enabled = false;

inline std::atomic<uint64_t> g_ns_cpu{0};
inline std::atomic<uint64_t> g_ns_nv2a_pushbuffer{0};
inline std::atomic<uint64_t> g_ns_raster{0};
inline std::atomic<uint64_t> g_ns_host_poll_devices{0};
// Split of g_ns_raster (beads-xbox.1.13 Phase 1 attribution): g_ns_raster_generic is
// wall time spent specifically inside rasterize_generic_combiner_batch, called from the
// single strict-fallback site in rasterize_witnessed_batch. It is a SUBSET of
// g_ns_raster (nested, single-threaded, sequential ScopedAdds), never a sibling.
// raster_witnessed = g_ns_raster - g_ns_raster_generic.
inline std::atomic<uint64_t> g_ns_raster_generic{0};
inline std::atomic<uint64_t> g_calls_raster_generic{0};

inline std::atomic<uint64_t> g_calls_cpu{0};
inline std::atomic<uint64_t> g_calls_nv2a_pushbuffer{0};
inline std::atomic<uint64_t> g_calls_raster{0};
inline std::atomic<uint64_t> g_calls_host_poll_devices{0};

// RAII scope timer. Construct at the top of the region to measure; destructor commits
// the elapsed time (and a call count) to the given sinks. No-op unless g_enabled.
class ScopedAdd {
public:
    ScopedAdd(std::atomic<uint64_t>& ns_sink, std::atomic<uint64_t>& call_sink)
        : ns_sink_(ns_sink), call_sink_(call_sink), active_(g_enabled) {
        if (active_) start_ = std::chrono::steady_clock::now();
    }
    ~ScopedAdd() {
        if (!active_) return;
        const auto elapsed = std::chrono::steady_clock::now() - start_;
        const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count();
        ns_sink_.fetch_add(static_cast<uint64_t>(ns), std::memory_order_relaxed);
        call_sink_.fetch_add(1, std::memory_order_relaxed);
    }
    ScopedAdd(const ScopedAdd&) = delete;
    ScopedAdd& operator=(const ScopedAdd&) = delete;

private:
    std::atomic<uint64_t>& ns_sink_;
    std::atomic<uint64_t>& call_sink_;
    bool active_;
    std::chrono::steady_clock::time_point start_;
};

// Formats the four buckets as one JSON-ish line for the end-of-run telemetry. Returns
// an empty string when accounting was never enabled, so call sites can print
// unconditionally without their own `if (perf-accounting)` branch.
inline std::string format_report() {
    if (!g_enabled) return {};
    char buf[768];
    const uint64_t raster_ns = g_ns_raster.load();
    const uint64_t raster_generic_ns = g_ns_raster_generic.load();
    const uint64_t raster_witnessed_ns =
        raster_ns >= raster_generic_ns ? raster_ns - raster_generic_ns : 0;
    std::snprintf(buf, sizeof(buf),
        "[perf] cpu_ns=%llu (n=%llu) nv2a_pushbuffer_ns=%llu (n=%llu) raster_ns=%llu "
        "(n=%llu) raster_witnessed_ns=%llu raster_generic_ns=%llu (n=%llu) "
        "host_poll_devices_ns=%llu (n=%llu) "
        "-- note: nv2a_pushbuffer and raster are SUBSETS of cpu, not disjoint from it; "
        "raster_witnessed/raster_generic are a SPLIT of raster (generic measured "
        "directly, witnessed derived as raster - generic); "
        "host_poll_devices is disjoint from all three",
        (unsigned long long)g_ns_cpu.load(), (unsigned long long)g_calls_cpu.load(),
        (unsigned long long)g_ns_nv2a_pushbuffer.load(),
        (unsigned long long)g_calls_nv2a_pushbuffer.load(),
        (unsigned long long)raster_ns, (unsigned long long)g_calls_raster.load(),
        (unsigned long long)raster_witnessed_ns,
        (unsigned long long)raster_generic_ns,
        (unsigned long long)g_calls_raster_generic.load(),
        (unsigned long long)g_ns_host_poll_devices.load(),
        (unsigned long long)g_calls_host_poll_devices.load());
    return std::string(buf);
}

}  // namespace xlle::perf
