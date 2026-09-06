#pragma once

#include "machine/nv2a_renderer.hpp"
#include "machine/perf_excl.hpp"

#include <cstddef>
#include <cstdint>

namespace xlle {

// Diagnostic-only phase attribution for the generated-combiner submit path.
// This is intentionally inclusive of child work inside each contiguous segment:
// entry may flush pending witnessed work, and ensure may resolve/upload resident
// surfaces. It is not another exclusive partition and should be used only to
// choose the next bounded measurement.
// A shared boundary read closes one segment and starts the next. Recorder
// bookkeeping after that read is charged to the next segment. Subtracting the
// calibrated read floor does not remove bookkeeping or fence perturbation;
// compare a companion run with this probe disabled before interpreting costs.
class Nv2aGeneratedSubmitPhaseRecorder {
public:
    using ClockFn = uint64_t (*)();

    enum Phase : std::size_t {
        kEntry = 0,
        kEnsure,
        kTextures,
        kRecords,
        kBuffer,
        kUniforms,
        kState,
        kDraw,
        kBarrier,
        kCleanup,
        kError,
        kFinish,
        kPhaseCount
    };

    Nv2aGeneratedSubmitPhaseRecorder(GeneratedSubmitCpuStats& stats,
                                     bool active,
                                     ClockFn clock = perf_excl::tick_now,
                                     uint64_t overhead_ticks =
                                         perf_excl::g_overhead_ticks,
                                     uint64_t max_plausible_ticks =
                                         perf_excl::g_max_plausible_ticks)
        : stats_(stats),
          clock_(clock),
          overhead_ticks_(overhead_ticks),
          max_plausible_ticks_(max_plausible_ticks),
          active_(active && clock != nullptr) {
        if (!active_) return;
        ++stats_.submits;
        current_ = kEntry;
        last_ = clock_();
    }

    ~Nv2aGeneratedSubmitPhaseRecorder() { close(); }

    Nv2aGeneratedSubmitPhaseRecorder(
        const Nv2aGeneratedSubmitPhaseRecorder&) = delete;
    Nv2aGeneratedSubmitPhaseRecorder& operator=(
        const Nv2aGeneratedSubmitPhaseRecorder&) = delete;

    void enter(Phase phase) {
        if (!active_) return;
        const uint64_t now = clock_();
        accrue(current_, now - last_);
        current_ = phase;
        last_ = now;
    }

    void add_record_bytes(std::size_t bytes) {
        if (!active_) return;
        stats_.record_bytes += static_cast<uint64_t>(bytes);
    }

    bool active() const { return active_; }

private:
    void close() {
        if (!active_) return;
        const uint64_t now = clock_();
        accrue(current_, now - last_);
        active_ = false;
    }

    void accrue(Phase phase, uint64_t delta) {
        const std::size_t index = static_cast<std::size_t>(phase);
        if (index >= GeneratedSubmitCpuStats::kPhaseCount ||
            delta > max_plausible_ticks_) {
            ++stats_.discarded_deltas;
            return;
        }
        const uint64_t removed =
            delta > overhead_ticks_ ? overhead_ticks_ : delta;
        stats_.raw_ticks[index] += delta;
        stats_.compensated_ticks[index] += delta - removed;
        ++stats_.calls[index];
    }

    GeneratedSubmitCpuStats& stats_;
    ClockFn clock_;
    uint64_t overhead_ticks_ = 0;
    uint64_t max_plausible_ticks_ = 0;
    uint64_t last_ = 0;
    Phase current_ = kEntry;
    bool active_ = false;
};

static_assert(
    static_cast<std::size_t>(Nv2aGeneratedSubmitPhaseRecorder::kPhaseCount) ==
        GeneratedSubmitCpuStats::kPhaseCount,
    "generated-submit phase count must match the public report layout");

}  // namespace xlle
