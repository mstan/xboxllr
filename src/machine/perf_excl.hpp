// xlle — EXCLUSIVE host-time attribution for a replay run (beads-xbox.1.14.3).
//
// WHY THIS EXISTS. src/machine/perf_accounting.hpp accumulates INCLUSIVE wall
// time per region, and those regions nest:
//
//     cpu ⊃ nv2a_pushbuffer ⊃ dispatch_method ⊃ submit_vertex
//                                             ⊃ {vertex_transform,
//                                                raster ⊃ {raster_generic
//                                                        | witnessed[16]}}
//
// Its own header (perf_accounting.hpp:11-29) states the buckets are not a
// partition and may not be subtracted, so no combination of them yields the
// number the throughput investigation actually needs: how much of the run is
// translated guest code versus the device path. compare_xbox_reports.py
// subtracted anyway until 2026-09-03, when those fields were relabelled
// INVALID_ESTIMATE_*. What survived was a band: pure-TCG time somewhere in
// [0, 100] s of a 251 s run, narrowed by the idle census to <= ~67 s, leaving
// "68-78 % of wall is the device path" as an INFERENCE (docs/PERF-METHODOLOGY.md
// §5, docs/PERF-AUDIT-2026-09-02.md §6).
//
// This module measures that split instead of inferring it. It is modelled on
// F:\Projects\ndsrecomp\ndsrecomp\runner\src\emu_profile.h, whose design notes
// explain every decision reused here.
//
// THE PARTITION. Exactly one bucket accrues at any instant. Opening a child
// region stops the parent's clock; closing it resumes the parent. Therefore
//
//     sum(bucket_ns) <= wall_ns
//
// and the shortfall is PUBLISHED as unaccounted_ns rather than being folded
// into any bucket. TCG_EXEC is the SELF time of the uc_emu_start region: the
// translated code, Unicorn's internals and the per-instruction UC_HOOK_CODE
// trampoline, with every MMIO / I/O-port / interrupt-delivery excursion carved
// out into its own bucket by the regions those funnels open. It is measured,
// never derived by subtraction, so an estimator defect shows up as a residual
// against wall time rather than hiding inside a named bucket.
//
// EXACT, NOT SAMPLED — and why. The obvious cost worry is NV2A_DISPATCH, whose
// region is entered ~83 M times per canonical run. Two alternatives were
// considered and rejected:
//
//   * "Time the drain loop instead of each method." No saving: Nv2a::
//     process_pushbuffer calls drain_pull_queue() once per METHOD WORD
//     (nv2a.cpp, inside the `if (ch.method_count)` arm of the parse loop), so
//     the drain population and the method population are the same population.
//     The region is nevertheless placed at drain_pull_queue's DEFINITION,
//     because that is the single funnel every queued method passes through and
//     a new dispatch call site therefore cannot bypass it.
//
//   * "Sample 1-in-N and scale." This breaks the partition. NV2A_DISPATCH is a
//     hot child of the hot parent NV2A_PFIFO; on an unsampled entry its time
//     stays inside the parent while the scaled child total also covers it, so
//     the two overlap by (1 - 1/N) x dispatch time. Removing that overlap needs
//     a subtraction between overlapping buckets, which is precisely the defect
//     this bead exists to delete. (emu_profile.h documents the same hazard as
//     "KNOWN LIMIT OF THE HYBRID" and tolerates it only on a path worth ~0 % of
//     a frame; here it would be the majority of the run.)
//
// So every region is exact, and the cost is bounded, compensated and EXPORTED:
// two rdtsc reads per boundary pair at ~6-10 ns each replaces today's two
// steady_clock::now() reads (QueryPerformanceCounter, ~20-25 ns each) plus two
// atomic read-modify-writes, i.e. roughly 15-20 ns per method word against
// ~70 ns today. `clock_reads` and `overhead_ticks_per_read` ship in every
// report line so the instrumentation's own share is checkable from a log
// instead of argued about (docs/PERF-METHODOLOGY.md §2 rule 9).
//
// If a measured run ever puts this over the 1 % gate, the exact fallback is one
// line: have Nv2a::drain_pull_queue open kNv2aPfifo instead of kNv2aDispatch.
// The partition stays exact, it just stops separating PFIFO parse from method
// semantics. It is a granularity knob, never an estimate.
//
// CLOCK CHOICE: rdtsc. The regions here run from ~50 ns (a PFIFO method) to
// milliseconds (a GL resolve). On this MinGW/Windows target std::chrono::
// steady_clock::now() lowers to QueryPerformanceCounter, ~20-25 ns per read;
// at the boundary counts above that is seconds of perturbation per run and it
// is a large fraction of the smallest regions' own value. __rdtsc is ~6-10 ns,
// and — more importantly for compensation — its cost is far more repeatable, so
// the measured minimum we subtract per boundary is close to the real cost
// rather than an optimistic floor under a noisy distribution. ns/tick is
// calibrated once against steady_clock and sanity-bounded to 0.1-10 ns/tick
// (100 MHz - 10 GHz); outside that band the calibration is discarded and ticks
// are treated as nanoseconds rather than publishing a wild scale factor. A
// non-x86 build falls back to steady_clock automatically.
//
// OVERHEAD IS COMPENSATED, NOT ASSUMED. Every boundary is exactly one tick read
// and closes exactly one region, so subtracting one measured clock-pair cost
// (g_overhead_ticks, the MINIMUM over 4096 back-to-back reads — the minimum is
// the un-preempted cost, whereas a mean would fold in scheduler noise and
// over-subtract) from the region being closed removes the instrumentation from
// the accumulated numbers. The residual perturbation is what `clock_reads`
// exports.
//
// A LEAKED REGION IS A FAT BUCKET, NEVER A WRONG TOTAL. Every region is opened
// through the RAII Scope/ScopeIf types, so an early return unwinds it. The one
// construct that can bypass a destructor here is Unicorn's internal
// sigsetjmp/siglongjmp: if QEMU ever longjmps out of an MMIO or I/O callback,
// that callback's Scope destructor does not run and g_cur stays on the inner
// bucket. The stack still self-heals at the next enclosing boundary, because
// exit_region() restores the `prev` captured when THAT region opened rather than
// popping a stack: the leaked interval is charged to the inner bucket and the
// outer one resumes correctly. So a leak inflates one bucket; it can never
// desynchronise the partition or corrupt the total.
//
// THREADING. Single accumulator set, plain (non-atomic) counters, on purpose:
// every region in this partition is opened on the emulation thread, which is
// the only thread that runs the CPU, the device models and the GL backend.
// perf_accounting.hpp's atomics cost a locked RMW per scope on a path taken
// 83 M times; that cost bought nothing, because the data was never produced by
// more than one thread. HostSampleProfiler runs on another thread but touches
// none of this.
//
// GATING. Off by default and free when off: enter() early-outs on one bool
// load, taking no clock read and touching no counter. --perf-accounting turns
// it on. It deliberately does NOT turn on NV2A detailed telemetry any more
// (main.cpp now has --nv2a-detailed-telemetry for that), so an instrumented run
// measures the same hot-loop code an uninstrumented one runs.
#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>

#if defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86)
#define XLLE_PERF_EXCL_TSC 1
#if defined(_MSC_VER)
#include <intrin.h>
#endif
#endif

namespace xlle::perf_excl {

// The partition. Every bucket is EXCLUSIVE of every other: time spent in a
// nested region is charged to the nested region only. The comment on each
// bucket names the function whose definition opens it; tests/
// test_perf_excl_guards.py pins those placements.
enum Bucket : uint8_t {
    // Self time of uc_emu_start: translated code, Unicorn internals and the
    // UC_HOOK_CODE trampoline. UnicornFloor::run.
    kTcgExec = 0,
    // MMIO router (find_mmio's linear scan + the observability callback) plus
    // the NV2A register body, exclusive of the NV2A work that opens its own
    // regions. XboxMachine::mmio_read / XboxMachine::mmio_write.
    kMmioDispatch,
    // Guest IN/OUT: the port router plus the PIC / PIT / IDE / SMBus / PM / RTC
    // register bodies behind it. XboxMachine::io_in / XboxMachine::io_out.
    kIoPort,
    // IDT walk, stack-frame push and register writes for one delivered vector,
    // whether from the chunk boundary or from a guest INT n.
    // UnicornFloor::inject_interrupt.
    kIntrDelivery,
    // MMIO device bodies that are not the NV2A: OHCI, APU, ACI, NvNet. Opened
    // around the device call inside XboxMachine::mmio_read / mmio_write.
    kOtherDevice,
    // PFIFO word fetch, DMA-object translate, command parse and CACHE1 push.
    // Nv2a::process_pushbuffer.
    kNv2aPfifo,
    // Method semantics for queued PGRAPH methods, exclusive of the vertex,
    // raster, clear, texture and GL work they reach. Nv2a::drain_pull_queue.
    kNv2aDispatch,
    // The per-method RECORDER block at the top of Nv2a::dispatch_method, before
    // any handler runs: methods_.push into the ring, the clear-trace capture
    // deques, and the method_counts_ histogram bump. Split out of kNv2aDispatch
    // because beads-xbox.1.15.1 replaced a 126-arm if/else chain with a jump
    // table and its result is UNINTERPRETABLE without this: the design doc notes
    // 209 ns/word is far more than ~124 predicted compares can explain, so a flat
    // A/B could mean either "the table did nothing" or "the table worked and the
    // recorder is the bucket". Nested inside kNv2aDispatch, so kNv2aDispatch
    // remains the handler bodies plus the table lookup, exclusive of this.
    kNv2aRecorder,
    // Vertex fetch, transform and batch assembly, exclusive of raster.
    // Nv2a::submit_vertex_batch.
    kNv2aVertex,
    // Witnessed-profile fingerprint match and per-pixel execution.
    // Nv2a::rasterize_witnessed_batch.
    kNv2aRasterWitnessed,
    // Generic-combiner decode and software raster, the strict fallback taken
    // when no witnessed profile matches. Nv2a::rasterize_generic_combiner_batch.
    kNv2aRasterGenericSw,
    // Texture surface resolve, DMA read, DXT decode and swizzle-table build for
    // one generic-combiner batch. The texture-setup funnel inside
    // Nv2a::rasterize_generic_combiner_batch.
    kNv2aTexture,
    // Host GL submit / upload wall time (driver-side, not GPU timer queries).
    // The OpenGL backend's submit_generic_combiner,
    // submit_resident_generic_combiner and upload_resident_color_rows.
    kNv2aGl,
    // Resident-surface readback (glReadPixels) plus the guest-memory writeback
    // that makes it coherent. Nv2a::resolve_renderer_color_overlap /
    // Nv2a::resolve_renderer_depth_overlap.
    kNv2aGlResolve,
    // CLEAR_SURFACE plane fills. Nv2a::clear_surface.
    kNv2aClear,
    // Scanout capture: guest framebuffer read and RGBA conversion.
    // Nv2a::capture_scanout.
    kNv2aScanout,
    // XboxMachine::run's chunk-loop body outside cpu_->run(): the PIC pending
    // check, PIT/NV2A/APU/ACI/OHCI frame-clock advances, interrupt-injection
    // bookkeeping, get_regs() and the idle census. One region per slice,
    // replacing the three separate host_poll_devices scopes that used to
    // measure their own clock reads (71.7 ns/scope, audit §6).
    kSliceOverhead,
    // The 1 kHz host-service callback: SDL window pump, input, network.
    // The host_poll_cb_ call inside XboxMachine::run.
    kHostPoll,
    // --frame-series checkpoint work: scanout capture, PPM write, report
    // formatting and printing. Diagnostics are not emulation; giving them a
    // named bucket keeps them out of HOST_POLL, which is where they would
    // otherwise land because capture_frame_series() runs inside host_poll_cb_.
    kDiagCheckpoint,
    kBucketCount,

    // Sentinel: no region is open. A gap charged to this is discarded.
    kNone = 0xFFu,
    // Sentinel returned by enter() when this entry opened no region — either
    // the module is disabled, or the requested bucket was already the current
    // one (see the re-entrancy note on enter()). It must be distinct from
    // kNone, which is a legitimate "restore to nothing open".
    kInactive = 0xFEu
};

// One name per bucket, in enum order. tests/test_perf_excl_guards.py asserts
// the counts match: a mismatch would silently shift every label in the report.
inline constexpr const char* kBucketNames[kBucketCount] = {
    "tcg_exec",
    "mmio_dispatch",
    "io_port",
    "intr_delivery",
    "other_device",
    "nv2a_pfifo",
    "nv2a_dispatch",
    "nv2a_recorder",
    "nv2a_vertex",
    "nv2a_raster_witnessed",
    "nv2a_raster_generic_sw",
    "nv2a_texture",
    "nv2a_gl",
    "nv2a_gl_resolve",
    "nv2a_clear",
    "nv2a_scanout",
    "slice_overhead",
    "host_poll",
    "diag_checkpoint",
};

struct Profile {
    // Accumulated in TICKS, not ns: a boundary must not do a float multiply.
    // Converted once, at report time.
    uint64_t ticks[kBucketCount] = {};
    // Measured region openings. This is the denominator for ticks[] beside it,
    // and it is NOT a call count: an entry elided by the re-entrancy check
    // below opens no region and is not counted, because it did not start a new
    // measured interval.
    uint64_t entries[kBucketCount] = {};
    // Tick reads taken. reads * overhead_ticks is the instrumentation's own
    // measured cost, already subtracted from the buckets above; exported so the
    // residual perturbation is checkable rather than assumed.
    uint64_t reads = 0;
    // Intervals rejected by the plausibility clamp in accrue(). The TSC is not
    // guaranteed monotonic across every migration/power event, and the delta is
    // computed unsigned: one backwards read would otherwise wrap to ~2^64 ticks
    // and poison a bucket permanently, with no way to tell from the log. A
    // non-zero value here means the clock misbehaved and the affected bucket is
    // under-counted by the discarded intervals -- a finding, not a silent fault.
    uint64_t discarded_deltas = 0;
};

inline Profile g_profile;
// Snapshot behind the last interval report, so a checkpoint can print deltas.
inline Profile g_prev;

// Set once from --perf-accounting before the replay starts; never toggled
// mid-run. Separate from xlle::perf::g_enabled so this header has no dependency
// on perf_accounting.hpp; main.cpp sets both from the same flag.
inline bool g_enabled = false;

// Bucket currently accruing, and the tick at which it started accruing.
inline uint8_t g_cur = kNone;
inline uint64_t g_last = 0;

inline double g_ns_per_tick = 1.0;
inline uint64_t g_overhead_ticks = 0;
inline uint64_t g_wall_start_ticks = 0;
// Upper bound on a single credible region interval, in ticks. Set by init() to
// kMaxPlausibleSeconds worth of ticks; the conservative default assumes 1 ns per
// tick so the clamp is still finite if a bucket is somehow accrued before init().
inline constexpr double kMaxPlausibleSeconds = 10.0;
inline uint64_t g_max_plausible_ticks = 10ull * 1000ull * 1000ull * 1000ull;
inline uint64_t g_prev_wall_ticks = 0;
inline bool g_calibrated = false;

// Monotonic tick source. See the CLOCK CHOICE note at the top of this file.
inline uint64_t tick_now() {
#if defined(XLLE_PERF_EXCL_TSC)
#if defined(_MSC_VER)
    return __rdtsc();
#else
    return __builtin_ia32_rdtsc();
#endif
#else
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
#endif
}

// Cost of one tick_now() pair, in ticks, as the MINIMUM over many back-to-back
// reads: the minimum is the un-preempted cost, whereas a mean would fold in
// scheduler noise and over-subtract from real regions.
inline uint64_t measure_overhead_ticks() {
    uint64_t best = ~uint64_t{0};
    for (int i = 0; i < 4096; ++i) {
        const uint64_t a = tick_now();
        const uint64_t b = tick_now();
        const uint64_t d = b - a;
        if (d < best) best = d;
    }
    return best == ~uint64_t{0} ? 0u : best;
}

// Nanoseconds per tick against the steady clock. On a TSC-less build tick_now()
// already returns nanoseconds, so the ratio is 1.
inline double measure_ns_per_tick() {
#if defined(XLLE_PERF_EXCL_TSC)
    using WallClock = std::chrono::steady_clock;
    double best = 0.0;
    uint64_t best_wall = 0;
    for (int pass = 0; pass < 2; ++pass) {
        const WallClock::time_point w0 = WallClock::now();
        const uint64_t t0 = tick_now();
        // Spin rather than sleep: a sleep can be rescheduled onto a different
        // core, and on some hosts that invalidates the pairing.
        while (std::chrono::duration_cast<std::chrono::microseconds>(
                   WallClock::now() - w0)
                   .count() < 2000) {
        }
        const uint64_t t1 = tick_now();
        const uint64_t wall_ns = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(WallClock::now() - w0)
                .count());
        const uint64_t ticks = t1 - t0;
        if (ticks && wall_ns > best_wall) {
            best_wall = wall_ns;
            best = static_cast<double>(wall_ns) / static_cast<double>(ticks);
        }
    }
    // A plausible invariant TSC is 0.1 .. 10 ns/tick. Outside that the
    // calibration is untrustworthy; treat ticks as nanoseconds rather than
    // publishing a wild scale factor.
    if (best < 0.1 || best > 10.0) return 1.0;
    return best;
#else
    return 1.0;
#endif
}

// Calibrate and start the wall-time window. Call once, before g_enabled is set,
// so the first measured region is not the one paying for calibration. Safe to
// call more than once.
inline void init() {
    if (g_calibrated) return;
    g_calibrated = true;
    g_ns_per_tick = measure_ns_per_tick();
    g_overhead_ticks = measure_overhead_ticks();
    g_max_plausible_ticks = static_cast<uint64_t>(
        kMaxPlausibleSeconds * 1e9 / (g_ns_per_tick > 0.0 ? g_ns_per_tick : 1.0));
    g_wall_start_ticks = tick_now();
    g_prev_wall_ticks = g_wall_start_ticks;
}

inline uint64_t ticks_to_ns(uint64_t ticks) {
    return static_cast<uint64_t>(static_cast<double>(ticks) * g_ns_per_tick);
}

// The single place a measured interval is credited to a bucket.
//
// `delta` is unsigned by construction (t - g_last). If the TSC ever steps
// backwards -- migration between cores whose counters are not perfectly synced,
// a suspend/resume, a hypervisor -- the subtraction wraps and credits ~2^64
// ticks, which at 0.21 ns/tick is ~124 years and would swamp every real
// measurement in that bucket for the rest of the run while looking like an
// ordinary large number in the log. Discard anything above a physically
// impossible single-region duration and count it, so the failure is visible on
// the report line instead of silently becoming the answer.
inline void accrue(uint8_t bucket, uint64_t delta) {
    if (delta > g_max_plausible_ticks) {
        ++g_profile.discarded_deltas;
        return;
    }
    g_profile.ticks[bucket] +=
        delta > g_overhead_ticks ? delta - g_overhead_ticks : 0u;
}

// Open a region. Returns the bucket to restore, or kInactive when this entry
// opened nothing.
//
// The single tick read closes the parent's accrual and opens this bucket's. One
// read, one region closed: subtracting one clock-pair overhead from the region
// being closed compensates the instrumentation exactly.
//
// RE-ENTRANCY ELISION. Entering the bucket that is already current would cost
// two reads to measure an interval that is already being measured, and would
// inflate `entries` with openings that started nothing. One compare removes
// both. It matters because several funnels are reachable from inside their own
// bucket (drain_pull_queue from resume_pushbuffers and from process_pushbuffer;
// clear_surface_plane from clear_surface).
inline uint8_t enter(Bucket bucket) {
    if (!g_enabled) return kInactive;
    if (g_cur == static_cast<uint8_t>(bucket)) return kInactive;
    const uint64_t t = tick_now();
    ++g_profile.reads;
    const uint8_t prev = g_cur;
    if (prev != kNone) accrue(prev, t - g_last);
    ++g_profile.entries[bucket];
    g_cur = static_cast<uint8_t>(bucket);
    g_last = t;
    return prev;
}

// Close the region opened by the matching enter(). `prev` carries whether that
// entry opened anything at all, so a disabled module, an elided re-entry and a
// real region all unwind correctly.
inline void exit_region(uint8_t prev) {
    if (prev == kInactive) return;
    const uint64_t t = tick_now();
    ++g_profile.reads;
    accrue(g_cur, t - g_last);
    g_cur = prev;
    g_last = t;
}

// Bring the currently-open region's accrual up to date without closing it, and
// return the tick it was brought up to date at. Used at report time so the
// numbers include the interval in progress and so wall time is sampled from the
// same clock read.
inline uint64_t flush() {
    const uint64_t t = tick_now();
    ++g_profile.reads;
    if (g_cur != kNone) {
        accrue(g_cur, t - g_last);
        g_last = t;
    }
    return t;
}

// RAII form. Every call site uses this; there are no bare enter/exit pairs.
class Scope {
public:
    explicit Scope(Bucket bucket) : prev_(enter(bucket)) {}
    ~Scope() { exit_region(prev_); }
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;

private:
    uint8_t prev_;
};

// A conditionally-opened region, for a funnel that can cheaply see it will do
// no work. Timing the no-work case would put two reads on a hot path for a
// bucket that stays zero.
class ScopeIf {
public:
    ScopeIf(Bucket bucket, bool open)
        : prev_(open ? enter(bucket) : static_cast<uint8_t>(kInactive)) {}
    ~ScopeIf() { exit_region(prev_); }
    ScopeIf(const ScopeIf&) = delete;
    ScopeIf& operator=(const ScopeIf&) = delete;

private:
    uint8_t prev_;
};

// One report line. `interval` prints deltas since the previous interval report
// and re-bases the snapshot; otherwise it prints run totals. Returns an empty
// string when accounting was never enabled, so call sites can print
// unconditionally.
//
// unaccounted_ns is SIGNED and published as a number. It can be slightly
// negative: the compensation subtracts the minimum clock-pair cost, and a read
// placed between real work costs marginally more than that floor, so the
// buckets can over-attribute by a fraction of a percent. A large positive
// residual means real time is escaping the partition (work outside every
// region), which is a finding about the instrument, not something to bury in a
// bucket.
inline std::string format_report(const char* scope, unsigned long long deadline,
                                 bool interval) {
    if (!g_enabled) return {};
    const uint64_t now_ticks = flush();

    const uint64_t wall_base = interval ? g_prev_wall_ticks : g_wall_start_ticks;
    const uint64_t wall_ns = ticks_to_ns(now_ticks - wall_base);

    uint64_t bucket_ns[kBucketCount] = {};
    uint64_t bucket_n[kBucketCount] = {};
    uint64_t accounted_ns = 0;
    for (std::size_t i = 0; i < static_cast<std::size_t>(kBucketCount); ++i) {
        const uint64_t ticks =
            interval ? g_profile.ticks[i] - g_prev.ticks[i] : g_profile.ticks[i];
        bucket_ns[i] = ticks_to_ns(ticks);
        bucket_n[i] =
            interval ? g_profile.entries[i] - g_prev.entries[i] : g_profile.entries[i];
        accounted_ns += bucket_ns[i];
    }
    const uint64_t reads = interval ? g_profile.reads - g_prev.reads : g_profile.reads;
    const uint64_t discarded = interval
                                   ? g_profile.discarded_deltas - g_prev.discarded_deltas
                                   : g_profile.discarded_deltas;
    const int64_t unaccounted_ns =
        static_cast<int64_t>(wall_ns) - static_cast<int64_t>(accounted_ns);
    const double unaccounted_pct =
        wall_ns ? 100.0 * static_cast<double>(unaccounted_ns) / static_cast<double>(wall_ns)
                : 0.0;
    // The instrumentation's own compensated cost, in ns, for this window.
    const uint64_t compensated_ns = ticks_to_ns(reads * g_overhead_ticks);

    char head[512];
    std::snprintf(head, sizeof(head),
                  "[perf-excl] scope=%s deadline=%llu wall_ns=%llu accounted_ns=%llu "
                  "unaccounted_ns=%lld unaccounted_pct=%.3f clock_reads=%llu "
                  "overhead_ticks_per_read=%llu ns_per_tick=%.6f compensated_ns=%llu "
                  "discarded_deltas=%llu",
                  scope, deadline, (unsigned long long)wall_ns,
                  (unsigned long long)accounted_ns, (long long)unaccounted_ns,
                  unaccounted_pct, (unsigned long long)reads,
                  (unsigned long long)g_overhead_ticks, g_ns_per_tick,
                  (unsigned long long)compensated_ns,
                  (unsigned long long)discarded);
    std::string out(head);
    for (std::size_t i = 0; i < static_cast<std::size_t>(kBucketCount); ++i) {
        char field[128];
        std::snprintf(field, sizeof(field), " %s_ns=%llu %s_n=%llu", kBucketNames[i],
                      (unsigned long long)bucket_ns[i], kBucketNames[i],
                      (unsigned long long)bucket_n[i]);
        out += field;
    }
    if (interval) {
        g_prev = g_profile;
        g_prev_wall_ticks = now_ticks;
    }
    return out;
}

}  // namespace xlle::perf_excl
