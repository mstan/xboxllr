#include "machine/nv2a_generated_submit_perf.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <initializer_list>
#include <iostream>

namespace {

int g_failures = 0;

#define CHECK(expr)                                                            \
    do {                                                                       \
        if (!(expr)) {                                                         \
            std::cerr << "CHECK failed at " << __FILE__ << ":" << __LINE__    \
                      << ": " #expr << "\n";                                  \
            ++g_failures;                                                      \
        }                                                                      \
    } while (0)

std::array<uint64_t, 32> g_ticks{};
std::size_t g_tick_count = 0;
std::size_t g_tick_pos = 0;

uint64_t fake_clock() {
    if (g_tick_pos >= g_tick_count) {
        ++g_failures;
        return g_ticks[g_tick_count ? g_tick_count - 1 : 0];
    }
    return g_ticks[g_tick_pos++];
}

void set_ticks(std::initializer_list<uint64_t> values) {
    g_ticks = {};
    g_tick_count = values.size();
    g_tick_pos = 0;
    std::size_t i = 0;
    for (uint64_t value : values) g_ticks[i++] = value;
}

using Recorder = xlle::Nv2aGeneratedSubmitPhaseRecorder;
using Stats = xlle::GeneratedSubmitCpuStats;

uint64_t sum_array(const std::array<uint64_t, Stats::kPhaseCount>& values) {
    uint64_t sum = 0;
    for (uint64_t value : values) sum += value;
    return sum;
}

void test_fixed_phase_order_matches_report_contract() {
    CHECK(Stats::kPhaseCount == 12);
    CHECK(Recorder::kEntry == 0);
    CHECK(Recorder::kEnsure == 1);
    CHECK(Recorder::kTextures == 2);
    CHECK(Recorder::kRecords == 3);
    CHECK(Recorder::kBuffer == 4);
    CHECK(Recorder::kUniforms == 5);
    CHECK(Recorder::kState == 6);
    CHECK(Recorder::kDraw == 7);
    CHECK(Recorder::kBarrier == 8);
    CHECK(Recorder::kCleanup == 9);
    CHECK(Recorder::kError == 10);
    CHECK(Recorder::kFinish == 11);
}

void test_phase_attribution_and_conservation() {
    Stats stats;
    stats.available = true;
    stats.enabled = true;
    set_ticks({100, 150, 175, 225});
    {
        Recorder recorder(stats, true, fake_clock, 10, 1000);
        recorder.enter(Recorder::kEnsure);
        recorder.enter(Recorder::kTextures);
    }

    CHECK(g_tick_pos == 4);
    CHECK(stats.submits == 1);
    CHECK(stats.discarded_deltas == 0);
    CHECK(stats.calls[Recorder::kEntry] == 1);
    CHECK(stats.calls[Recorder::kEnsure] == 1);
    CHECK(stats.calls[Recorder::kTextures] == 1);
    CHECK(stats.raw_ticks[Recorder::kEntry] == 50);
    CHECK(stats.raw_ticks[Recorder::kEnsure] == 25);
    CHECK(stats.raw_ticks[Recorder::kTextures] == 50);
    CHECK(stats.compensated_ticks[Recorder::kEntry] == 40);
    CHECK(stats.compensated_ticks[Recorder::kEnsure] == 15);
    CHECK(stats.compensated_ticks[Recorder::kTextures] == 40);
    CHECK(sum_array(stats.raw_ticks) == 125);
    CHECK(sum_array(stats.compensated_ticks) == 95);
}

void test_early_unwind_closes_current_phase() {
    Stats stats;
    set_ticks({10, 40, 70});
    {
        Recorder recorder(stats, true, fake_clock, 5, 1000);
        recorder.enter(Recorder::kError);
    }

    CHECK(g_tick_pos == 3);
    CHECK(stats.submits == 1);
    CHECK(stats.calls[Recorder::kEntry] == 1);
    CHECK(stats.calls[Recorder::kError] == 1);
    CHECK(stats.raw_ticks[Recorder::kEntry] == 30);
    CHECK(stats.raw_ticks[Recorder::kError] == 30);
    CHECK(stats.compensated_ticks[Recorder::kEntry] == 25);
    CHECK(stats.compensated_ticks[Recorder::kError] == 25);
}

void test_disabled_takes_no_clock_reads_or_counters() {
    Stats stats;
    set_ticks({1, 2, 3});
    {
        Recorder recorder(stats, false, fake_clock, 1, 1000);
        recorder.enter(Recorder::kRecords);
        recorder.add_record_bytes(448);
    }

    CHECK(g_tick_pos == 0);
    CHECK(stats.submits == 0);
    CHECK(stats.record_bytes == 0);
    CHECK(sum_array(stats.raw_ticks) == 0);
    CHECK(sum_array(stats.compensated_ticks) == 0);
    CHECK(sum_array(stats.calls) == 0);
}

void test_short_interval_compensation_clamps_to_zero() {
    Stats stats;
    set_ticks({100, 105, 108});
    {
        Recorder recorder(stats, true, fake_clock, 10, 1000);
        recorder.enter(Recorder::kBuffer);
    }

    CHECK(stats.raw_ticks[Recorder::kEntry] == 5);
    CHECK(stats.raw_ticks[Recorder::kBuffer] == 3);
    CHECK(stats.compensated_ticks[Recorder::kEntry] == 0);
    CHECK(stats.compensated_ticks[Recorder::kBuffer] == 0);
    CHECK(stats.calls[Recorder::kEntry] == 1);
    CHECK(stats.calls[Recorder::kBuffer] == 1);
}

void test_invalid_and_backward_intervals_are_discarded() {
    Stats stats;
    set_ticks({100, 1200, 1190, 1210});
    {
        Recorder recorder(stats, true, fake_clock, 10, 1000);
        recorder.enter(Recorder::kDraw);
        recorder.enter(Recorder::kFinish);
    }

    CHECK(stats.submits == 1);
    CHECK(stats.discarded_deltas == 2);
    CHECK(stats.calls[Recorder::kEntry] == 0);
    CHECK(stats.calls[Recorder::kDraw] == 0);
    CHECK(stats.calls[Recorder::kFinish] == 1);
    CHECK(stats.raw_ticks[Recorder::kFinish] == 20);
    CHECK(stats.compensated_ticks[Recorder::kFinish] == 10);
}

void test_record_bytes_count_only_when_active() {
    Stats stats;
    set_ticks({5, 15});
    {
        Recorder recorder(stats, true, fake_clock, 1, 1000);
        recorder.add_record_bytes(448);
        recorder.add_record_bytes(896);
    }

    CHECK(stats.record_bytes == 1344);
    CHECK(stats.submits == 1);
}

}  // namespace

int main() {
    test_fixed_phase_order_matches_report_contract();
    test_phase_attribution_and_conservation();
    test_early_unwind_closes_current_phase();
    test_disabled_takes_no_clock_reads_or_counters();
    test_short_interval_compensation_clamps_to_zero();
    test_invalid_and_backward_intervals_are_discarded();
    test_record_bytes_count_only_when_active();

    if (g_failures) {
        std::cerr << "test_generated_submit_perf: " << g_failures
                  << " checks failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "test_generated_submit_perf: all checks passed\n";
    return EXIT_SUCCESS;
}
