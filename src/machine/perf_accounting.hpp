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
//                         stack, so some pushbuffer/raster work overlaps this bucket.
//                         Device draining can also rasterize after uc_emu_start returns,
//                         so none of these buckets may be subtracted to derive a pure-CPU
//                         number without interval-aware exclusive accounting.
//   nv2a_pushbuffer    -- wall time inside Nv2a::process_pushbuffer (PFIFO word fetch +
//                         method dispatch for one doorbell kick). It can overlap raster,
//                         but raster can also run later from drain_pull_queue(), so neither
//                         bucket is a strict subset of the other.
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
#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
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
inline std::atomic<uint64_t> g_ns_generic_texture_setup{0};
inline std::atomic<uint64_t> g_ns_generic_software_raster{0};
inline std::atomic<uint64_t> g_ns_generic_flush{0};
inline std::atomic<uint64_t> g_calls_generic_texture_setup{0};
inline std::atomic<uint64_t> g_calls_generic_software_raster{0};
inline std::atomic<uint64_t> g_calls_generic_flush{0};

inline std::atomic<uint64_t> g_calls_cpu{0};
inline std::atomic<uint64_t> g_calls_nv2a_pushbuffer{0};
inline std::atomic<uint64_t> g_calls_raster{0};
inline std::atomic<uint64_t> g_calls_host_poll_devices{0};

// Optional renderer-backend split for resident OpenGL paths. These are API-wall-time
// buckets, not GPU timer-query results; they are meant to identify host staging and
// synchronization pressure before attempting broader renderer changes.
inline std::atomic<uint64_t> g_ns_renderer_resident_ensure{0};
inline std::atomic<uint64_t> g_ns_renderer_texture_stage{0};
inline std::atomic<uint64_t> g_ns_renderer_vertex_upload{0};
inline std::atomic<uint64_t> g_ns_renderer_draw{0};
inline std::atomic<uint64_t> g_ns_renderer_resolve{0};
inline std::atomic<uint64_t> g_calls_renderer_resident_ensure{0};
inline std::atomic<uint64_t> g_calls_renderer_texture_stage{0};
inline std::atomic<uint64_t> g_calls_renderer_vertex_upload{0};
inline std::atomic<uint64_t> g_calls_renderer_draw{0};
inline std::atomic<uint64_t> g_calls_renderer_resolve{0};
inline std::atomic<uint64_t> g_renderer_dirty_clip_pixels{0};
inline std::atomic<uint64_t> g_renderer_dirty_surface_pixels{0};
inline std::atomic<uint64_t> g_renderer_resolve_pixels{0};

inline constexpr size_t kWitnessedProfileCount = 16;
inline constexpr std::array<const char*, kWitnessedProfileCount> kWitnessedProfileNames{{
    "none",
    "pitch-z16-depth",
    "swizzled-radial-color",
    "swizzled-textured-color",
    "pitch-textured-color",
    "swizzled-projected-color",
    "pitch-projected-color",
    "pitch-multi-texture-color",
    "pitch-three-texture-color",
    "pitch-tex5-projected-color",
    "pitch-tex5-diffuse-color",
    "inline-linear-blit-color",
    "pitch-single-texture-color",
    "tex7-fan-color",
    "texf-projected-fan-color",
    "pitch-additive-color",
}};
inline std::array<std::atomic<uint64_t>, kWitnessedProfileCount>
    g_ns_witnessed_profile{};
inline std::array<std::atomic<uint64_t>, kWitnessedProfileCount>
    g_calls_witnessed_profile{};

// Allocation-churn accounting for the late stock-dashboard scene. PagedRasterSurface
// owns one small allocation per resident 4 KiB guest page, while the generic combiner
// reloads referenced texture images into temporary vectors once per batch. These
// counters distinguish a genuinely large live raster working set from a small live set
// whose enormous cumulative allocation churn is retained by the host allocator.
inline std::atomic<uint64_t> g_paged_surface_instances{0};
inline std::atomic<uint64_t> g_paged_surface_direct_instances{0};
inline std::atomic<uint64_t> g_paged_surface_direct_bytes{0};
inline std::atomic<uint64_t> g_paged_surface_pages_loaded{0};
inline std::atomic<uint64_t> g_paged_surface_bytes_loaded{0};
inline std::atomic<uint64_t> g_paged_surface_bytes_live{0};
inline std::atomic<uint64_t> g_paged_surface_bytes_peak{0};
inline std::atomic<uint64_t> g_paged_surface_logical_bytes_peak{0};
inline std::atomic<uint64_t> g_generic_texture_allocations{0};
inline std::atomic<uint64_t> g_generic_texture_bytes_loaded{0};
inline std::atomic<uint64_t> g_generic_texture_batch_bytes_peak{0};
inline std::atomic<uint64_t> g_pfifo_queue_entries_live{0};
inline std::atomic<uint64_t> g_pfifo_queue_entries_peak{0};
inline std::atomic<uint64_t> g_pfifo_queue_pushes{0};

inline void update_peak(std::atomic<uint64_t>& sink, uint64_t value) {
    uint64_t previous = sink.load(std::memory_order_relaxed);
    while (previous < value &&
           !sink.compare_exchange_weak(previous, value, std::memory_order_relaxed)) {}
}

inline void record_paged_surface(size_t logical_bytes) {
    if (!g_enabled) return;
    g_paged_surface_instances.fetch_add(1, std::memory_order_relaxed);
    update_peak(g_paged_surface_logical_bytes_peak,
                static_cast<uint64_t>(logical_bytes));
}

inline void record_paged_surface_direct(size_t logical_bytes) {
    if (!g_enabled) return;
    g_paged_surface_direct_instances.fetch_add(1, std::memory_order_relaxed);
    g_paged_surface_direct_bytes.fetch_add(static_cast<uint64_t>(logical_bytes),
                                           std::memory_order_relaxed);
}

inline void record_paged_surface_page(size_t bytes) {
    if (!g_enabled) return;
    const uint64_t amount = static_cast<uint64_t>(bytes);
    g_paged_surface_pages_loaded.fetch_add(1, std::memory_order_relaxed);
    g_paged_surface_bytes_loaded.fetch_add(amount, std::memory_order_relaxed);
    const uint64_t live =
        g_paged_surface_bytes_live.fetch_add(amount, std::memory_order_relaxed) + amount;
    update_peak(g_paged_surface_bytes_peak, live);
}

inline void release_paged_surface_bytes(size_t bytes) {
    if (!g_enabled || !bytes) return;
    g_paged_surface_bytes_live.fetch_sub(static_cast<uint64_t>(bytes),
                                         std::memory_order_relaxed);
}

inline void record_generic_texture_allocation(size_t bytes,
                                              size_t batch_bytes_live) {
    if (!g_enabled) return;
    g_generic_texture_allocations.fetch_add(1, std::memory_order_relaxed);
    g_generic_texture_bytes_loaded.fetch_add(static_cast<uint64_t>(bytes),
                                              std::memory_order_relaxed);
    update_peak(g_generic_texture_batch_bytes_peak,
                static_cast<uint64_t>(batch_bytes_live));
}

inline void record_renderer_dirty_area(uint64_t clip_pixels,
                                       uint64_t surface_pixels) {
    if (!g_enabled) return;
    g_renderer_dirty_clip_pixels.fetch_add(clip_pixels, std::memory_order_relaxed);
    g_renderer_dirty_surface_pixels.fetch_add(surface_pixels,
                                              std::memory_order_relaxed);
}

inline void record_renderer_resolve_pixels(uint64_t pixels) {
    if (!g_enabled) return;
    g_renderer_resolve_pixels.fetch_add(pixels, std::memory_order_relaxed);
}

inline void record_pfifo_queue_push() {
    if (!g_enabled) return;
    g_pfifo_queue_pushes.fetch_add(1, std::memory_order_relaxed);
    const uint64_t live =
        g_pfifo_queue_entries_live.fetch_add(1, std::memory_order_relaxed) + 1;
    update_peak(g_pfifo_queue_entries_peak, live);
}

inline void record_pfifo_queue_pop() {
    if (!g_enabled) return;
    g_pfifo_queue_entries_live.fetch_sub(1, std::memory_order_relaxed);
}

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
        "-- note: nv2a_pushbuffer and raster can overlap cpu and each other, but raster "
        "can also run outside process_pushbuffer; "
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

inline std::string format_memory_report() {
    if (!g_enabled) return {};
    char buf[1024];
    std::snprintf(
        buf, sizeof(buf),
        "[perf-memory] paged-surface-creations=%llu direct-surfaces=%llu "
        "direct-surface-bytes=%llu pages-loaded=%llu "
        "page-payload-bytes-cumulative=%llu page-payload-bytes-live=%llu "
        "page-payload-bytes-peak=%llu logical-surface-bytes-peak=%llu "
        "generic-texture-allocations=%llu generic-texture-bytes=%llu "
        "generic-texture-batch-peak=%llu pfifo-queue-pushes=%llu "
        "pfifo-queue-entries-live=%llu pfifo-queue-entries-peak=%llu",
        (unsigned long long)g_paged_surface_instances.load(),
        (unsigned long long)g_paged_surface_direct_instances.load(),
        (unsigned long long)g_paged_surface_direct_bytes.load(),
        (unsigned long long)g_paged_surface_pages_loaded.load(),
        (unsigned long long)g_paged_surface_bytes_loaded.load(),
        (unsigned long long)g_paged_surface_bytes_live.load(),
        (unsigned long long)g_paged_surface_bytes_peak.load(),
        (unsigned long long)g_paged_surface_logical_bytes_peak.load(),
        (unsigned long long)g_generic_texture_allocations.load(),
        (unsigned long long)g_generic_texture_bytes_loaded.load(),
        (unsigned long long)g_generic_texture_batch_bytes_peak.load(),
        (unsigned long long)g_pfifo_queue_pushes.load(),
        (unsigned long long)g_pfifo_queue_entries_live.load(),
        (unsigned long long)g_pfifo_queue_entries_peak.load());
    return std::string(buf);
}

inline std::string format_generic_split_report() {
    if (!g_enabled) return {};
    char buf[512];
    std::snprintf(
        buf, sizeof(buf),
        "[perf-generic-split] texture-setup-ns=%llu (n=%llu) "
        "software-raster-ns=%llu (n=%llu) flush-ns=%llu (n=%llu)",
        (unsigned long long)g_ns_generic_texture_setup.load(),
        (unsigned long long)g_calls_generic_texture_setup.load(),
        (unsigned long long)g_ns_generic_software_raster.load(),
        (unsigned long long)g_calls_generic_software_raster.load(),
        (unsigned long long)g_ns_generic_flush.load(),
        (unsigned long long)g_calls_generic_flush.load());
    return std::string(buf);
}

inline std::string format_renderer_report() {
    if (!g_enabled) return {};
    char buf[768];
    std::snprintf(
        buf, sizeof(buf),
        "[perf-renderer] resident-ensure-ns=%llu (n=%llu) "
        "texture-stage-ns=%llu (n=%llu) vertex-upload-ns=%llu (n=%llu) "
        "draw-ns=%llu (n=%llu) resolve-ns=%llu (n=%llu) "
        "dirty-clip-pixels=%llu dirty-surface-pixels=%llu "
        "resolve-pixels=%llu",
        (unsigned long long)g_ns_renderer_resident_ensure.load(),
        (unsigned long long)g_calls_renderer_resident_ensure.load(),
        (unsigned long long)g_ns_renderer_texture_stage.load(),
        (unsigned long long)g_calls_renderer_texture_stage.load(),
        (unsigned long long)g_ns_renderer_vertex_upload.load(),
        (unsigned long long)g_calls_renderer_vertex_upload.load(),
        (unsigned long long)g_ns_renderer_draw.load(),
        (unsigned long long)g_calls_renderer_draw.load(),
        (unsigned long long)g_ns_renderer_resolve.load(),
        (unsigned long long)g_calls_renderer_resolve.load(),
        (unsigned long long)g_renderer_dirty_clip_pixels.load(),
        (unsigned long long)g_renderer_dirty_surface_pixels.load(),
        (unsigned long long)g_renderer_resolve_pixels.load());
    return std::string(buf);
}

inline std::string format_witnessed_profile_report() {
    if (!g_enabled) return {};
    std::string out = "[perf-witnessed-profile] top=";
    struct Entry {
        uint64_t ns = 0;
        uint64_t calls = 0;
        size_t index = 0;
    };
    std::array<Entry, kWitnessedProfileCount> entries{};
    for (size_t index = 0; index < kWitnessedProfileCount; ++index) {
        entries[index] = {g_ns_witnessed_profile[index].load(),
                          g_calls_witnessed_profile[index].load(), index};
    }
    std::sort(entries.begin(), entries.end(),
              [](const Entry& a, const Entry& b) { return a.ns > b.ns; });
    bool any = false;
    for (const Entry& entry : entries) {
        if (entry.ns == 0 && entry.calls == 0) continue;
        if (any) out += " ";
        char buf[160];
        std::snprintf(buf, sizeof(buf), "%s:ns%llu/n%llu",
                      kWitnessedProfileNames[entry.index],
                      (unsigned long long)entry.ns,
                      (unsigned long long)entry.calls);
        out += buf;
        any = true;
    }
    if (!any) out += "none";
    return out;
}

}  // namespace xlle::perf
