#include "machine/perf_excl.hpp"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <unordered_map>

namespace {

int g_failures = 0;

#define CHECK(cond)                                                                 \
    do {                                                                            \
        if (!(cond)) {                                                              \
            std::cerr << "CHECK failed at " << __FILE__ << ":" << __LINE__ << ": " \
                      << #cond << "\n";                                            \
            ++g_failures;                                                           \
        }                                                                           \
    } while (0)

using namespace xlle;

void reset_perf_excl_state() {
    perf_excl::g_profile = {};
    perf_excl::g_prev = {};
    perf_excl::g_enabled = false;
    perf_excl::g_cur = perf_excl::kNone;
    perf_excl::g_last = 0;
    perf_excl::g_ns_per_tick = 1.0;
    perf_excl::g_overhead_ticks = 0;
    perf_excl::g_wall_start_ticks = 0;
    perf_excl::g_max_plausible_ticks = 10ull * 1000ull * 1000ull * 1000ull;
    perf_excl::g_prev_wall_ticks = 0;
    perf_excl::g_calibrated = false;
}

std::unordered_map<std::string, std::string> parse_report(const std::string& report) {
    std::unordered_map<std::string, std::string> fields;
    std::size_t pos = 0;
    while (pos < report.size()) {
        while (pos < report.size() && report[pos] == ' ') ++pos;
        const std::size_t end = report.find(' ', pos);
        const std::string token =
            report.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
        const std::size_t eq = token.find('=');
        if (eq != std::string::npos) {
            fields.emplace(token.substr(0, eq), token.substr(eq + 1));
        }
        if (end == std::string::npos) break;
        pos = end + 1;
    }
    return fields;
}

uint64_t u64_field(const std::unordered_map<std::string, std::string>& fields,
                   const char* name) {
    const auto it = fields.find(name);
    CHECK(it != fields.end());
    return it == fields.end() ? 0 : std::stoull(it->second);
}

int64_t i64_field(const std::unordered_map<std::string, std::string>& fields,
                  const char* name) {
    const auto it = fields.find(name);
    CHECK(it != fields.end());
    return it == fields.end() ? 0 : std::stoll(it->second);
}

void test_compensated_delta_and_accrue() {
    reset_perf_excl_state();
    perf_excl::g_overhead_ticks = 10;
    perf_excl::g_max_plausible_ticks = 1000;

    uint64_t discarded = 0;
    CHECK(perf_excl::compensated_delta(50, discarded) == 40);
    CHECK(discarded == 0);
    CHECK(perf_excl::compensated_delta(10, discarded) == 0);
    CHECK(discarded == 0);
    CHECK(perf_excl::compensated_delta(5, discarded) == 0);
    CHECK(discarded == 0);
    CHECK(perf_excl::compensated_delta(1001, discarded) == 0);
    CHECK(discarded == 1);
    CHECK(perf_excl::compensated_delta(~uint64_t{0}, discarded) == 0);
    CHECK(discarded == 2);

    perf_excl::accrue(perf_excl::kHostPoll, 50);
    CHECK(perf_excl::g_profile.ticks[perf_excl::kHostPoll] == 40);
    CHECK(perf_excl::g_profile.compensated_ticks == 10);
    CHECK(perf_excl::g_profile.discarded_deltas == 0);

    perf_excl::accrue(perf_excl::kHostPoll, 10);
    CHECK(perf_excl::g_profile.ticks[perf_excl::kHostPoll] == 40);
    CHECK(perf_excl::g_profile.compensated_ticks == 20);
    CHECK(perf_excl::g_profile.discarded_deltas == 0);

    perf_excl::accrue(perf_excl::kHostPoll, 5);
    CHECK(perf_excl::g_profile.ticks[perf_excl::kHostPoll] == 40);
    CHECK(perf_excl::g_profile.compensated_ticks == 25);
    CHECK(perf_excl::g_profile.discarded_deltas == 0);

    perf_excl::accrue(perf_excl::kHostPoll, 1001);
    CHECK(perf_excl::g_profile.ticks[perf_excl::kHostPoll] == 40);
    CHECK(perf_excl::g_profile.compensated_ticks == 25);
    CHECK(perf_excl::g_profile.discarded_deltas == 1);
}

void test_disabled_elided_and_nested_counters() {
    reset_perf_excl_state();
    perf_excl::g_overhead_ticks = 0;
    perf_excl::g_max_plausible_ticks = ~uint64_t{0};

    CHECK(perf_excl::enter(perf_excl::kHostPoll) == perf_excl::kInactive);
    CHECK(perf_excl::g_profile.reads == 0);
    CHECK(perf_excl::g_profile.entries[perf_excl::kHostPoll] == 0);

    perf_excl::g_enabled = true;
    const uint8_t outer_prev = perf_excl::enter(perf_excl::kHostPoll);
    CHECK(outer_prev == perf_excl::kNone);
    CHECK(perf_excl::g_cur == perf_excl::kHostPoll);
    CHECK(perf_excl::g_profile.reads == 1);
    CHECK(perf_excl::g_profile.entries[perf_excl::kHostPoll] == 1);

    CHECK(perf_excl::enter(perf_excl::kHostPoll) == perf_excl::kInactive);
    CHECK(perf_excl::g_profile.reads == 1);
    CHECK(perf_excl::g_profile.entries[perf_excl::kHostPoll] == 1);

    perf_excl::exit_region(perf_excl::kInactive);
    CHECK(perf_excl::g_profile.reads == 1);
    CHECK(perf_excl::g_cur == perf_excl::kHostPoll);

    const uint8_t inner_prev = perf_excl::enter(perf_excl::kDiagCheckpoint);
    CHECK(inner_prev == perf_excl::kHostPoll);
    CHECK(perf_excl::g_cur == perf_excl::kDiagCheckpoint);
    CHECK(perf_excl::g_profile.reads == 2);
    CHECK(perf_excl::g_profile.entries[perf_excl::kDiagCheckpoint] == 1);

    perf_excl::exit_region(inner_prev);
    CHECK(perf_excl::g_profile.reads == 3);
    CHECK(perf_excl::g_cur == perf_excl::kHostPoll);

    perf_excl::exit_region(outer_prev);
    CHECK(perf_excl::g_profile.reads == 4);
    CHECK(perf_excl::g_cur == perf_excl::kNone);
}

void test_format_report_cumulative_raw_and_compensated_fields() {
    reset_perf_excl_state();
    perf_excl::g_enabled = true;
    perf_excl::g_ns_per_tick = 1.0;
    perf_excl::g_overhead_ticks = 10;
    perf_excl::g_max_plausible_ticks = 1000;
    perf_excl::g_cur = perf_excl::kNone;
    perf_excl::g_wall_start_ticks = perf_excl::tick_now();
    perf_excl::g_prev_wall_ticks = perf_excl::g_wall_start_ticks;
    perf_excl::g_profile.ticks[perf_excl::kHostPoll] = 40;
    perf_excl::g_profile.entries[perf_excl::kHostPoll] = 3;
    perf_excl::g_profile.reads = 7;
    perf_excl::g_profile.discarded_deltas = 2;
    perf_excl::g_profile.compensated_ticks = 25;

    const auto fields =
        parse_report(perf_excl::format_report("cumulative-test", 123, false));
    const uint64_t wall_ns = u64_field(fields, "wall_ns");
    const uint64_t accounted_ns = u64_field(fields, "accounted_ns");
    const uint64_t compensated_ns = u64_field(fields, "compensated_ns");

    CHECK(u64_field(fields, "clock_reads") == 8);
    CHECK(accounted_ns == 40);
    CHECK(compensated_ns == 25);
    CHECK(u64_field(fields, "discarded_deltas") == 2);
    CHECK(u64_field(fields, "host_poll_ns") == 40);
    CHECK(u64_field(fields, "host_poll_n") == 3);
    CHECK(i64_field(fields, "unaccounted_ns") ==
          static_cast<int64_t>(wall_ns) - static_cast<int64_t>(accounted_ns));
    CHECK(i64_field(fields, "residual_ns") ==
          static_cast<int64_t>(wall_ns) - static_cast<int64_t>(accounted_ns) -
              static_cast<int64_t>(compensated_ns));
    CHECK(perf_excl::g_profile.compensated_ticks == 25);
}

void test_format_report_interval_deltas_and_snapshot() {
    reset_perf_excl_state();
    perf_excl::g_enabled = true;
    perf_excl::g_ns_per_tick = 1.0;
    perf_excl::g_overhead_ticks = 10;
    perf_excl::g_max_plausible_ticks = 1000;
    perf_excl::g_cur = perf_excl::kNone;
    perf_excl::g_wall_start_ticks = perf_excl::tick_now();
    perf_excl::g_prev_wall_ticks = perf_excl::g_wall_start_ticks;

    perf_excl::g_prev.ticks[perf_excl::kHostPoll] = 10;
    perf_excl::g_prev.entries[perf_excl::kHostPoll] = 1;
    perf_excl::g_prev.reads = 2;
    perf_excl::g_prev.discarded_deltas = 1;
    perf_excl::g_prev.compensated_ticks = 5;

    perf_excl::g_profile.ticks[perf_excl::kHostPoll] = 40;
    perf_excl::g_profile.entries[perf_excl::kHostPoll] = 3;
    perf_excl::g_profile.reads = 7;
    perf_excl::g_profile.discarded_deltas = 2;
    perf_excl::g_profile.compensated_ticks = 25;

    const auto fields = parse_report(perf_excl::format_report("interval-test", 456, true));
    const uint64_t wall_ns = u64_field(fields, "wall_ns");
    const uint64_t accounted_ns = u64_field(fields, "accounted_ns");
    const uint64_t compensated_ns = u64_field(fields, "compensated_ns");

    CHECK(u64_field(fields, "clock_reads") == 6);
    CHECK(accounted_ns == 30);
    CHECK(compensated_ns == 20);
    CHECK(u64_field(fields, "discarded_deltas") == 1);
    CHECK(u64_field(fields, "host_poll_ns") == 30);
    CHECK(u64_field(fields, "host_poll_n") == 2);
    CHECK(i64_field(fields, "unaccounted_ns") ==
          static_cast<int64_t>(wall_ns) - static_cast<int64_t>(accounted_ns));
    CHECK(i64_field(fields, "residual_ns") ==
          static_cast<int64_t>(wall_ns) - static_cast<int64_t>(accounted_ns) -
              static_cast<int64_t>(compensated_ns));
    CHECK(perf_excl::g_prev.reads == perf_excl::g_profile.reads);
    CHECK(perf_excl::g_prev.compensated_ticks == perf_excl::g_profile.compensated_ticks);
    CHECK(perf_excl::g_prev_wall_ticks != perf_excl::g_wall_start_ticks ||
          wall_ns == 0);
}

void test_real_clock_smoke() {
    reset_perf_excl_state();
    const uint64_t a = perf_excl::tick_now();
    const uint64_t b = perf_excl::tick_now();
    CHECK(b >= a);

    perf_excl::init();
    CHECK(perf_excl::g_calibrated);
    CHECK(perf_excl::g_ns_per_tick > 0.0);
    CHECK(perf_excl::g_max_plausible_ticks > 0);
    CHECK(perf_excl::g_prev_wall_ticks == perf_excl::g_wall_start_ticks);
}

}  // namespace

int main() {
    test_compensated_delta_and_accrue();
    test_disabled_elided_and_nested_counters();
    test_format_report_cumulative_raw_and_compensated_fields();
    test_format_report_interval_deltas_and_snapshot();
    test_real_clock_smoke();

    if (g_failures) {
        std::cerr << "test_perf_excl: " << g_failures << " failure(s)\n";
        return EXIT_FAILURE;
    }
    std::cout << "test_perf_excl: all checks passed\n";
    return EXIT_SUCCESS;
}
