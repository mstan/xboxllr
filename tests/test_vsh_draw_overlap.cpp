#include "machine/nv2a_vsh_draw_overlap.hpp"

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>

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

using xlle::VshDrawOverlapCensus;
using xlle::VshDrawOverlapJob;
using xlle::VshDrawOverlapOutcome;
using xlle::VshDrawOverlapProgramIdentity;

constexpr std::size_t outcome_slot(VshDrawOverlapOutcome outcome) {
    return static_cast<std::size_t>(outcome);
}

VshDrawOverlapProgramIdentity program(uint64_t fp, uint32_t start = 0,
                                      uint32_t slots = 8) {
    VshDrawOverlapProgramIdentity out;
    out.known = true;
    out.fingerprint = fp;
    out.start_slot = start;
    out.slots = slots;
    return out;
}

VshDrawOverlapJob job(uint64_t fp, VshDrawOverlapOutcome outcome) {
    VshDrawOverlapJob out;
    out.valid = true;
    out.program = program(fp);
    out.outcome = outcome;
    out.transform_mode = 2;
    out.programmable = true;
    out.compiled = true;
    out.submitted_vertices = 9;
    out.transformed_vertices = 7;
    out.executed_vsh_vertices = 5;
    out.index_cache_hits = 4;
    return out;
}

uint64_t retained_batches(const VshDrawOverlapCensus& census) {
    uint64_t total = 0;
    for (std::size_t i = 0; i < census.row_count; ++i)
        total += census.rows[i].totals.batches;
    total += census.overflow.totals.batches;
    return total;
}

void test_outcome_names_are_stable() {
    CHECK(xlle::kVshDrawOverlapOutcomeCount == 12);
    CHECK(std::string(xlle::vsh_draw_overlap_outcome_name(
              VshDrawOverlapOutcome::kUnknown)) == "unknown");
    CHECK(std::string(xlle::vsh_draw_overlap_outcome_name(
              VshDrawOverlapOutcome::kCpuWitnessed)) == "cpu_witnessed");
    CHECK(std::string(xlle::vsh_draw_overlap_outcome_name(
              VshDrawOverlapOutcome::kGpuWitnessed)) == "gpu_witnessed");
    CHECK(std::string(xlle::vsh_draw_overlap_outcome_name(
              VshDrawOverlapOutcome::kWitnessedEmpty)) == "witnessed_empty");
    CHECK(std::string(xlle::vsh_draw_overlap_outcome_name(
              VshDrawOverlapOutcome::kGeneratedGpu)) == "generated_gpu");
    CHECK(std::string(xlle::vsh_draw_overlap_outcome_name(
              VshDrawOverlapOutcome::kGeneratedEmpty)) == "generated_empty");
    CHECK(std::string(xlle::vsh_draw_overlap_outcome_name(
              VshDrawOverlapOutcome::kGenericResidentGpu)) == "generic_resident_gpu");
    CHECK(std::string(xlle::vsh_draw_overlap_outcome_name(
              VshDrawOverlapOutcome::kGenericSnapshotGpu)) == "generic_snapshot_gpu");
    CHECK(std::string(xlle::vsh_draw_overlap_outcome_name(
              VshDrawOverlapOutcome::kGenericEmpty)) == "generic_empty");
    CHECK(std::string(xlle::vsh_draw_overlap_outcome_name(
              VshDrawOverlapOutcome::kGenericCpu)) == "generic_cpu");
    CHECK(std::string(xlle::vsh_draw_overlap_outcome_name(
              VshDrawOverlapOutcome::kNoRaster)) == "no_raster");
    CHECK(std::string(xlle::vsh_draw_overlap_outcome_name(
              VshDrawOverlapOutcome::kFault)) == "fault");
}

void test_disabled_invalid_and_counted_jobs_do_not_record() {
    VshDrawOverlapCensus census;
    VshDrawOverlapJob valid = job(1, VshDrawOverlapOutcome::kGeneratedGpu);
    CHECK(!xlle::vsh_draw_overlap_record(census, valid));
    CHECK(census.totals.batches == 0);
    CHECK(!valid.counted);

    census.enabled = true;
    VshDrawOverlapJob invalid;
    CHECK(!xlle::vsh_draw_overlap_record(census, invalid));
    CHECK(census.totals.batches == 0);

    CHECK(xlle::vsh_draw_overlap_record(census, valid));
    CHECK(!xlle::vsh_draw_overlap_record(census, valid));
    CHECK(census.totals.batches == 1);
    CHECK(census.row_count == 1);
    CHECK(retained_batches(census) == census.totals.batches);
}

void test_full_key_splits_same_program_by_route() {
    VshDrawOverlapCensus census;
    census.enabled = true;

    VshDrawOverlapJob generated = job(0xABC, VshDrawOverlapOutcome::kGeneratedGpu);
    generated.generated_setup_calls = 1;
    generated.generated_setup_raw_ticks = 50;
    generated.generated_setup_compensated_ticks = 40;
    generated.primitive_input_triangles = 3;
    generated.primitive_collected_triangles = 2;
    generated.primitive_refused_batches = 1;
    CHECK(xlle::vsh_draw_overlap_record(census, generated));

    VshDrawOverlapJob generic = job(0xABC, VshDrawOverlapOutcome::kGenericCpu);
    generic.executed_vsh_vertices = 11;
    generic.compiled = false;
    CHECK(xlle::vsh_draw_overlap_record(census, generic));

    VshDrawOverlapJob context = job(0xABC, VshDrawOverlapOutcome::kCpuWitnessed);
    context.writes_context = true;
    CHECK(xlle::vsh_draw_overlap_record(census, context));

    CHECK(census.totals.batches == 3);
    CHECK(census.row_count == 3);
    CHECK(census.totals.outcomes[outcome_slot(VshDrawOverlapOutcome::kGeneratedGpu)] == 1);
    CHECK(census.totals.outcomes[outcome_slot(VshDrawOverlapOutcome::kGenericCpu)] == 1);
    CHECK(census.totals.outcomes[outcome_slot(VshDrawOverlapOutcome::kCpuWitnessed)] == 1);
    CHECK(census.totals.executed_vsh_vertices == 21);
    CHECK(census.totals.index_cache_hits == 12);
    CHECK(census.totals.compiled_batches == 2);
    CHECK(census.totals.interpreted_batches == 1);
    CHECK(census.totals.context_writing_batches == 1);
    CHECK(census.totals.generated_setup_calls == 1);
    CHECK(census.totals.generated_setup_raw_ticks == 50);
    CHECK(census.totals.generated_setup_compensated_ticks == 40);
    CHECK(census.totals.primitive_input_triangles == 3);
    CHECK(census.totals.primitive_collected_triangles == 2);
    CHECK(census.totals.primitive_refused_batches == 1);
    CHECK(retained_batches(census) == census.totals.batches);
}

void test_unknown_outcome_and_unknown_program_stay_visible() {
    VshDrawOverlapCensus census;
    census.enabled = true;
    VshDrawOverlapJob unknown;
    unknown.valid = true;
    unknown.outcome = VshDrawOverlapOutcome::kUnknown;
    unknown.transform_mode = 0;
    unknown.fixed_function = true;
    unknown.submitted_vertices = 6;
    unknown.transformed_vertices = 6;

    CHECK(xlle::vsh_draw_overlap_record(census, unknown));
    CHECK(unknown.counted);
    CHECK(census.totals.batches == 1);
    CHECK(census.row_count == 1);
    CHECK(census.rows[0].key.outcome == VshDrawOverlapOutcome::kUnknown);
    CHECK(!census.rows[0].key.program.known);
    CHECK(census.totals.outcomes[outcome_slot(VshDrawOverlapOutcome::kUnknown)] == 1);
    CHECK(census.unknown_program.batches == 1);
    CHECK(census.unknown_program.fixed_function_batches == 1);
    CHECK(retained_batches(census) == census.totals.batches);
}

void test_row_saturation_keeps_overflow_conservation() {
    VshDrawOverlapCensus census;
    census.enabled = true;
    for (std::size_t i = 0; i < VshDrawOverlapCensus::kRows + 3; ++i) {
        VshDrawOverlapJob item = job(0x1000 + i,
                                    VshDrawOverlapOutcome::kGpuWitnessed);
        item.submitted_vertices = static_cast<uint64_t>(i + 1);
        CHECK(xlle::vsh_draw_overlap_record(census, item));
    }
    CHECK(census.row_count == VshDrawOverlapCensus::kRows);
    CHECK(census.overflow.used);
    CHECK(census.overflow.totals.batches == 3);
    CHECK(census.totals.batches == VshDrawOverlapCensus::kRows + 3);
    CHECK(retained_batches(census) == census.totals.batches);
}

void test_program_identity_includes_start_and_slots() {
    VshDrawOverlapCensus census;
    census.enabled = true;
    VshDrawOverlapJob a = job(0xCAFE, VshDrawOverlapOutcome::kGeneratedGpu);
    VshDrawOverlapJob b = a;
    VshDrawOverlapJob c = a;
    b.program.start_slot = a.program.start_slot + 1;
    c.program.slots = a.program.slots + 1;
    CHECK(xlle::vsh_draw_overlap_record(census, a));
    CHECK(xlle::vsh_draw_overlap_record(census, b));
    CHECK(xlle::vsh_draw_overlap_record(census, c));
    CHECK(census.row_count == 3);
    CHECK(retained_batches(census) == census.totals.batches);
}

void test_fixture_population_and_setup_counts_stay_separate() {
    VshDrawOverlapCensus census;
    census.enabled = true;
    auto first = job(0xAA, VshDrawOverlapOutcome::kGenericResidentGpu);
    first.fixture = 4;
    first.fixture_setup_calls = 1;
    first.fixture_setup_raw_ticks = 100;
    first.fixture_setup_compensated_ticks = 90;
    first.fixture_input_vertices = 9;
    first.fixture_output_vertices = 6;
    auto second = first;
    second.fixture = 6;
    second.fixture_setup_raw_ticks = 200;
    second.fixture_setup_compensated_ticks = 190;
    CHECK(xlle::vsh_draw_overlap_record(census, first));
    CHECK(xlle::vsh_draw_overlap_record(census, second));
    CHECK(census.row_count == 2);
    CHECK(census.rows[0].key.fixture == 4);
    CHECK(census.rows[1].key.fixture == 6);
    CHECK(census.rows[0].totals.fixture_setup_raw_ticks == 100);
    CHECK(census.rows[1].totals.fixture_setup_raw_ticks == 200);
    CHECK(census.totals.fixture_setup_calls == 2);
    CHECK(census.totals.fixture_setup_compensated_ticks == 280);
    CHECK(census.totals.fixture_input_vertices == 18);
    CHECK(census.totals.fixture_output_vertices == 12);
    CHECK(census.totals.generated_setup_calls == 0);
}

}  // namespace

int main() {
    test_outcome_names_are_stable();
    test_disabled_invalid_and_counted_jobs_do_not_record();
    test_full_key_splits_same_program_by_route();
    test_unknown_outcome_and_unknown_program_stay_visible();
    test_row_saturation_keeps_overflow_conservation();
    test_program_identity_includes_start_and_slots();
    test_fixture_population_and_setup_counts_stay_separate();
    if (g_failures) {
        std::cerr << g_failures << " failures\n";
        return 1;
    }
    return 0;
}
