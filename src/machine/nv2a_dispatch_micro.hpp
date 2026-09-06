// xlle -- TEMPORARY per-word micro-partition of the NV2A pushbuffer/dispatch path
// (beads-xbox.1.15.1 step 1, 2026-09-05).
//
// WHY THIS EXISTS. docs/PERF-METHODOLOGY.md section 5b row 6 puts
// `nv2a_dispatch` 4.774 s + `nv2a_pfifo` 2.811 s = 7.585 s on 82.7 M dispatched
// method words: 13.0 % of the phase-7 instrumented wall and the third-largest
// bucket in the run. perf_excl.hpp splits that pair three ways (pfifo / dispatch
// / recorder) and no further, so "57.7 ns/word of dispatch" is a number with no
// addresses in it. Before anything is changed, this module says WHERE the
// nanoseconds are: the DMA translate, the guest word fetch, the CACHE1 deque,
// the drain funnel, the recorder's three pieces, the class histograms and the
// handler body, each with its own denominator.
//
// COMPILE-TIME OPTIONAL, AND OFF IN THE SHIPPING BINARY. Every probe below is
// inside `#if XLLE_DISPATCH_MICRO`. With the option off (the default) this
// header contributes no code, no branch and no global to the hot loop, so the
// binary that runs the decision A/B is the binary that ships. A runtime
// `if (enabled)` per probe would have cost ~12 predictable branches per word on
// the very loop under measurement -- comparable to the effect being measured.
// Configure the measurement build with `-DXLLE_DISPATCH_MICRO=ON` into its own
// build directory.
//
// ONE WORD CLASS AT A TIME, AND WHY. dispatch_method's handler for
// SET_BEGIN_END (0x17FC) runs the whole draw -- vertex assembly, raster, GL --
// which perf_excl carves into its own buckets through nested regions. This
// module has no such nesting, so an unfiltered handler slot would report the
// rasterizer as dispatch cost (measured: 343 ns/word against a 341 ns envelope,
// i.e. the number is the draw, not the dispatch). The sample predicate is
// therefore keyed on the METHOD: by default only ARRAY_ELEMENT16 (0x1800), which
// is 76.9 % of the run's method words and whose handler is two vector appends,
// so every slot in the report is genuinely per-word overhead. Set
// XLLE_DISPATCH_MICRO_METHOD to another method (hex, e.g. 1808) or to `all` to
// widen it, and read the handler slot accordingly.
//
// SAMPLED, AND WHY THAT IS SOUND HERE. perf_excl.hpp rejects sampling, for a
// reason that does not apply to this module: there, NV2A_DISPATCH is a hot CHILD
// of the hot parent NV2A_PFIFO, so an unsampled entry leaves the child's time
// inside the parent while a scaled child total also covers it, and the two
// overlap. The slots here are SIBLINGS within one word -- they never nest -- and
// every slot is sampled on the SAME words by the same predicate, so each slot's
// mean is an independent estimator of the same population. Sampling buys the
// thing exactness would destroy: ~13 rdtsc reads on a word whose true cost is
// ~60 ns is a multiple-x perturbation, and paying it on every word would change
// what is being measured. At the default period only 1 matching word in 64 is
// probed.
//
// THE FLOOR IS MEASURED, NOT ASSUMED. Slot `null` is a mark taken immediately
// after the previous mark with NO WORK between them, so its mean is this
// instrument's own in-situ cost per boundary -- the floor under which a slot's
// reading carries no information. It is not the same as
// perf_excl::g_overhead_ticks (the minimum over back-to-back reads in a tight
// calibration loop, already subtracted by accrue()): the residual measured here
// is what a read costs when it sits between real work in a real loop. Every slot
// is reported both raw and as `excess`, its reading minus the floor, and a slot
// at the floor is reported as ~0 rather than as 7 ns of imaginary work.
//
// THE ENVELOPE IS MEASURED TOO. Sampled words alternate between two modes: in
// ENVELOPE mode two reads bracket the whole word and nothing inside is probed;
// in BREAKDOWN mode the slots are chained. Both see the same population, so
// `sum(excess)` against `envelope - floor` is a check on whether the parts add
// up to the whole, printed rather than assumed.
//
// NOT ARM-THEN-CAPTURE (PRINCIPLES section 7). These are free-running
// accumulators from process start; the report is a query against them at each
// checkpoint and at shutdown. Nothing is armed, nothing is stepped.
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "perf_excl.hpp"

namespace xlle::dispatch_micro {

// One slot per piece of the per-word path, in the order a data word visits them.
// The comment names the code the slot closes at, so a probe that moves shows up
// as a renamed slot rather than as a silently different number.
enum Slot : uint8_t {
    // No work at all: two marks back to back. This module's own in-situ cost per
    // boundary, and the floor under every other slot below.
    kNull = 0,
    // Nv2a::process_pushbuffer: dma_object_address() for this word's GET.
    kPfDma,
    // Nv2a::process_pushbuffer: mem_read_() -- the std::function into
    // UnicornFloor::read_mem / uc_mem_read -- plus the GET advance.
    kPfFetch,
    // Nv2a::process_pushbuffer: the `if (ch.method_count)` arm's CACHE1 work --
    // the PushMethod aggregate, pull_queue_.push_back, record_pfifo_queue_push,
    // the method / count bookkeeping.
    kPfQueue,
    // Nv2a::drain_pull_queue: the funnel prologue -- the stall predicate, the
    // perf_excl scope, the optional<ScopedAdd>, the loop test, front() /
    // pop_front() and record_pfifo_queue_pop.
    kDrainEnter,
    // Nv2a::dispatch_method: entry through the SET_OBJECT test and the
    // object_class_ read, up to the recorder region.
    kDmPrologue,
    // The recorder's first piece: push.seq = methods_.total() and methods_.push.
    kRecRing,
    // The recorder's second piece: the clear-trace capture deques.
    kRecTrace,
    // The recorder's third piece: ++method_counts_[method >> 2].
    kRecCounts,
    // Nv2a::dispatch_method: ++class_method_counts_[...] and the class-0x62 /
    // 0x9F / 0x44 class2d_method_stats_ block, plus the three class compares
    // that stand between a class-0x97 word and its table lookup.
    kDmClass,
    // The kNv097Table lookup, the switch, the handler body, and the call and
    // return around dispatch_method.
    kDmHandler,
    // Nv2a::drain_pull_queue: everything after the handler returns -- the loop
    // re-test, the scope destructors, the return into process_pushbuffer.
    kDrainExit,
    kSlotCount
};

inline constexpr const char* kSlotNames[kSlotCount] = {
    "null",      "pf_dma",     "pf_fetch",  "pf_queue",
    "drain_enter", "dm_prologue", "rec_ring", "rec_trace",
    "rec_counts", "dm_class",  "dm_handler", "drain_exit",
};

// Sample every method word, not one class. Not the default: see the header note.
inline constexpr uint32_t kMethodAll = 0xFFFFFFFFu;

#if XLLE_DISPATCH_MICRO

// Sample one matching data word in g_period.
inline uint32_t g_period = 64;
// The method whose word class is under measurement, or kMethodAll.
inline uint32_t g_method_filter = 0x1800;

// Matching data words seen, sampled or not: the denominator that scales a slot
// mean up to the whole run.
inline uint64_t g_words = 0;
// Every data word, matching or not, for context.
inline uint64_t g_all_words = 0;
// Every non-data (header / jump / call / return) word.
inline uint64_t g_header_words = 0;

inline uint64_t g_ticks[kSlotCount] = {};
inline uint64_t g_counts[kSlotCount] = {};
inline uint64_t g_envelope_ticks = 0;
inline uint64_t g_envelope_words = 0;
inline uint64_t g_header_ticks = 0;
inline uint64_t g_header_sampled = 0;
inline uint64_t g_reads = 0;
inline uint64_t g_discarded = 0;

// 0 = this word is not sampled; 1 = envelope mode; 2 = breakdown mode.
// A global because the marks live in three functions across two regions of
// nv2a.cpp.
inline uint8_t g_mode = 0;
inline uint64_t g_last = 0;
inline uint64_t g_word_start = 0;

// SERIALIZED tick read. perf_excl now also fences its clock; this experimental
// micro probe retains its own read/calibration pair. The first
// version of this module chained bare reads across a ~41 ns word and the twelve
// deltas summed to 12.2 ns -- 36 % of the whole -- because the reads float to
// the head of the reorder window and collapse toward each other while the real
// work retires behind them. `lfence` on both sides pins each read between the
// work before it and the work after it. It costs more per read; the floor slot
// below measures exactly how much, and every number is reported net of it.
inline uint64_t now() {
#if defined(XLLE_PERF_EXCL_TSC)
    __builtin_ia32_lfence();
    const uint64_t t = __builtin_ia32_rdtsc();
    __builtin_ia32_lfence();
    return t;
#else
    return perf_excl::tick_now();
#endif
}

// Call once, next to perf_excl::init(), before the replay starts. The knobs are
// environment variables rather than CLI flags on purpose: this module is
// temporary measurement scaffolding and must not add surface to the shipping
// argument parser.
inline void init_from_env() {
    if (const char* p = std::getenv("XLLE_DISPATCH_MICRO_PERIOD")) {
        const unsigned long v = std::strtoul(p, nullptr, 10);
        if (v >= 1 && v <= 1000000) g_period = static_cast<uint32_t>(v);
    }
    if (const char* m = std::getenv("XLLE_DISPATCH_MICRO_METHOD")) {
        if (m[0] == 'a' || m[0] == 'A') {
            g_method_filter = kMethodAll;
        } else {
            const unsigned long v = std::strtoul(m, nullptr, 16);
            g_method_filter = static_cast<uint32_t>(v);
        }
    }
}

// RAW accrual. Nothing is subtracted here, deliberately:
// perf_excl::g_overhead_ticks is the minimum cost of a BARE back-to-back rdtsc
// pair, and the reads above are lfence-fenced, so that constant is the wrong
// one and would silently under-compensate. The instrument's cost is removed at
// report time instead, by subtracting the `null` slot -- a mark taken with no
// work in front of it, measured in situ with these same fenced reads. An
// implausible interval is still dropped rather than allowed to poison a slot.
inline void accrue(uint8_t slot, uint64_t delta) {
    if (delta > perf_excl::g_max_plausible_ticks) {
        ++g_discarded;
        return;
    }
    g_ticks[slot] += delta;
    ++g_counts[slot];
}

inline void mark(Slot slot) {
    if (g_mode != 2) return;
    const uint64_t t = now();
    ++g_reads;
    accrue(static_cast<uint8_t>(slot), t - g_last);
    g_last = t;
}

// Open a data word carrying `method`.
inline void begin_word(uint32_t method) {
    ++g_all_words;
    if (g_method_filter != kMethodAll && method != g_method_filter) {
        g_mode = 0;
        return;
    }
    const uint64_t n = ++g_words;
    if (n % g_period) {
        g_mode = 0;
        return;
    }
    // Alternate so both modes see the same population.
    g_mode = ((n / g_period) & 1u) ? 2u : 1u;
    const uint64_t t = now();
    ++g_reads;
    g_word_start = t;
    g_last = t;
    // The floor: one mark with nothing between it and the open.
    mark(kNull);
}

inline void end_word() {
    if (!g_mode) return;
    const uint64_t t = now();
    ++g_reads;
    if (g_mode == 1) {
        const uint64_t d = t - g_word_start;
        if (d <= perf_excl::g_max_plausible_ticks) {
            g_envelope_ticks += d;
            ++g_envelope_words;
        } else {
            ++g_discarded;
        }
    }
    g_mode = 0;
}

// ---- the RUN path (beads-xbox.1.15.1 stages (c)/(d)) -----------------------
//
// Once run batching is on, 98.6 % of method data words never reach begin_word()
// above -- they are consumed inside Nv2a::drain_method_run. The AFTER number
// therefore needs its own probe, and a thirteen-read chain inside a loop whose
// body is two vector appends would be pure instrument. So the run path is
// measured as an ENVELOPE ONLY: two fenced reads around the whole loop, divided
// by the words it consumed. With the method filter left at ARRAY_ELEMENT16 the
// handler does no draw work, so run_ns_per_word is directly comparable with the
// `word_ns` the per-word path reports for the same word class.
inline uint64_t g_run_ticks = 0;
inline uint64_t g_run_words = 0;
inline uint64_t g_run_samples = 0;
// Runs seen, matching the filter: the run path's own sample denominator. Kept
// separate from g_words, which counts per-word-path WORDS.
inline uint64_t g_run_seen = 0;

inline uint64_t begin_run(uint32_t method) {
    if (g_method_filter != kMethodAll && method != g_method_filter) return 0;
    if (++g_run_seen % g_period) return 0;
    ++g_reads;
    const uint64_t t = now();
    return t ? t : 1u;
}

inline void end_run(uint64_t start, uint32_t words) {
    if (!start || !words) return;
    ++g_reads;
    const uint64_t d = now() - start;
    if (d > perf_excl::g_max_plausible_ticks) {
        ++g_discarded;
        return;
    }
    g_run_ticks += d;
    g_run_words += words;
    ++g_run_samples;
}

// A non-data word: the PFIFO header decode arm. Two reads, the same period, its
// own denominator. Never filtered by method -- a header word has none yet.
inline uint64_t begin_header_word() {
    const uint64_t n = ++g_header_words;
    if (n % g_period) return 0;
    ++g_reads;
    const uint64_t t = now();
    return t ? t : 1u;
}

inline void end_header_word(uint64_t start) {
    if (!start) return;
    ++g_reads;
    const uint64_t d = now() - start;
    if (d > perf_excl::g_max_plausible_ticks) {
        ++g_discarded;
        return;
    }
    g_header_ticks += d;
    ++g_header_sampled;
}

// One report line, queried at a checkpoint or at shutdown. Every slot carries
// its own denominator (docs/PERF-METHODOLOGY.md section 2 rule 9), and the
// module's own read count, its measured floor and the parts-versus-whole check
// are on the same line.
inline std::string format_report(const char* scope, unsigned long long deadline) {
    const double ns_per_tick = perf_excl::g_ns_per_tick;
    auto mean_ns = [&](std::size_t i) {
        return g_counts[i] ? static_cast<double>(g_ticks[i]) * ns_per_tick /
                                 static_cast<double>(g_counts[i])
                           : 0.0;
    };

    const double floor_ns = mean_ns(kNull);
    // The deltas TELESCOPE: the twelve slot means sum to the span from the read
    // in begin_word() to the read in the last mark, so sum(raw) is one
    // breakdown word measured end to end. Model a word in each mode as
    //
    //     breakdown span = W + kSlotCount * R
    //     envelope  span = W + 1          * R
    //
    // where W is the word's real work and R the marginal cost of one fenced
    // read where it actually sits. Two modes, two equations, so R is SOLVED
    // rather than assumed -- which matters, because the obvious estimator (the
    // `null` slot) is not R: it also carries begin_word()'s own bookkeeping,
    // including two 64-bit divisions by the runtime period, and using it
    // over-subtracts by that much from every slot.
    double sum_raw = 0.0;
    for (std::size_t i = 0; i < static_cast<std::size_t>(kSlotCount); ++i)
        sum_raw += mean_ns(i);
    const double env_ns = g_envelope_words
                              ? static_cast<double>(g_envelope_ticks) * ns_per_tick /
                                    static_cast<double>(g_envelope_words)
                              : 0.0;
    const double read_ns =
        (sum_raw > 0.0 && env_ns > 0.0)
            ? (sum_raw - env_ns) / static_cast<double>(kSlotCount - 1)
            : 0.0;
    // W from the envelope, which pays the instrument only once.
    const double word_ns = env_ns - read_ns;
    // Sum of the emulator-side slots, i.e. everything but `null` (which is this
    // module's own per-word bookkeeping and belongs to no emulator work).
    double sum_net = 0.0;
    for (std::size_t i = kNull + 1; i < static_cast<std::size_t>(kSlotCount); ++i)
        sum_net += mean_ns(i) - read_ns;
    const double sum_excess = sum_net;
    const double header_raw_ns =
        g_header_sampled ? static_cast<double>(g_header_ticks) * ns_per_tick /
                               static_cast<double>(g_header_sampled)
                         : 0.0;
    // A header word is a two-read bracket, so it pays the instrument exactly
    // once, the same as an envelope word.
    const double header_ns = header_raw_ns - read_ns;

    char head[768];
    std::snprintf(head, sizeof(head),
                  "[dispatch-micro] scope=%s deadline=%llu period=%u method=0x%x "
                  "matching_words=%llu all_data_words=%llu header_words=%llu "
                  "sampled_breakdown=%llu sampled_envelope=%llu sampled_header=%llu "
                  "clock_reads=%llu discarded=%llu ns_per_tick=%.6f floor_ns=%.2f "
                  "read_ns=%.2f sum_raw_ns=%.2f envelope_ns=%.2f word_ns=%.2f "
                  "sum_net_ns=%.2f parts_over_whole=%.3f word_run_s=%.3f "
                  "sum_net_run_s=%.3f header_ns=%.2f header_run_s=%.3f "
                  "run_samples=%llu run_sampled_words=%llu run_ns_per_word=%.2f",
                  scope, deadline, g_period, g_method_filter,
                  (unsigned long long)g_words, (unsigned long long)g_all_words,
                  (unsigned long long)g_header_words,
                  (unsigned long long)g_counts[kNull],
                  (unsigned long long)g_envelope_words,
                  (unsigned long long)g_header_sampled, (unsigned long long)g_reads,
                  (unsigned long long)g_discarded, ns_per_tick, floor_ns, read_ns,
                  sum_raw, env_ns, word_ns, sum_excess,
                  word_ns > 0.0 ? sum_excess / word_ns : 0.0,
                  word_ns * static_cast<double>(g_words) / 1e9,
                  sum_excess * static_cast<double>(g_words) / 1e9, header_ns,
                  header_ns * static_cast<double>(g_header_words) / 1e9,
                  (unsigned long long)g_run_samples,
                  (unsigned long long)g_run_words,
                  // One bracket per RUN, so the instrument is paid once per
                  // sample and amortised over the words the run consumed.
                  g_run_words ? (static_cast<double>(g_run_ticks) * ns_per_tick -
                                 read_ns * static_cast<double>(g_run_samples)) /
                                    static_cast<double>(g_run_words)
                              : 0.0);
    std::string out(head);
    for (std::size_t i = 0; i < static_cast<std::size_t>(kSlotCount); ++i) {
        const double raw = mean_ns(i);
        const double net = raw - read_ns;
        char field[224];
        std::snprintf(field, sizeof(field),
                      " %s_ns=%.2f %s_n=%llu %s_net_ns=%.2f %s_run_s=%.3f",
                      kSlotNames[i], raw, kSlotNames[i], (unsigned long long)g_counts[i],
                      kSlotNames[i], net, kSlotNames[i],
                      (net > 0.0 ? net : 0.0) * static_cast<double>(g_words) / 1e9);
        out += field;
    }
    return out;
}

#else  // !XLLE_DISPATCH_MICRO

// Off: every probe is an empty inline with no global behind it, so the loop the
// module measures is compiled exactly as it would be without this header.
inline void init_from_env() {}
inline void begin_word(uint32_t) {}
inline void mark(Slot) {}
inline void end_word() {}
inline uint64_t begin_run(uint32_t) { return 0; }
inline void end_run(uint64_t, uint32_t) {}
inline uint64_t begin_header_word() { return 0; }
inline void end_header_word(uint64_t) {}
inline std::string format_report(const char*, unsigned long long) { return {}; }

#endif  // XLLE_DISPATCH_MICRO

}  // namespace xlle::dispatch_micro
