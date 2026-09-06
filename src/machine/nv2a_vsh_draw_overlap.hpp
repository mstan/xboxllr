#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace xlle {

enum class VshDrawOverlapOutcome : uint8_t {
    kUnknown = 0,
    kCpuWitnessed,
    kGpuWitnessed,
    kWitnessedEmpty,
    kGeneratedGpu,
    kGeneratedEmpty,
    kGenericResidentGpu,
    kGenericSnapshotGpu,
    kGenericEmpty,
    kGenericCpu,
    kNoRaster,
    kFault,
    kCount,
};

inline constexpr size_t kVshDrawOverlapOutcomeCount =
    static_cast<size_t>(VshDrawOverlapOutcome::kCount);

inline const char* vsh_draw_overlap_outcome_name(
    VshDrawOverlapOutcome outcome) {
    switch (outcome) {
    case VshDrawOverlapOutcome::kUnknown:        return "unknown";
    case VshDrawOverlapOutcome::kCpuWitnessed:   return "cpu_witnessed";
    case VshDrawOverlapOutcome::kGpuWitnessed:   return "gpu_witnessed";
    case VshDrawOverlapOutcome::kWitnessedEmpty: return "witnessed_empty";
    case VshDrawOverlapOutcome::kGeneratedGpu:   return "generated_gpu";
    case VshDrawOverlapOutcome::kGeneratedEmpty: return "generated_empty";
    case VshDrawOverlapOutcome::kGenericResidentGpu:
        return "generic_resident_gpu";
    case VshDrawOverlapOutcome::kGenericSnapshotGpu:
        return "generic_snapshot_gpu";
    case VshDrawOverlapOutcome::kGenericEmpty:   return "generic_empty";
    case VshDrawOverlapOutcome::kGenericCpu:     return "generic_cpu";
    case VshDrawOverlapOutcome::kNoRaster:       return "no_raster";
    case VshDrawOverlapOutcome::kFault:          return "fault";
    case VshDrawOverlapOutcome::kCount:          break;
    }
    return "invalid";
}

struct VshDrawOverlapProgramIdentity {
    bool known = false;
    uint64_t fingerprint = 0;
    uint32_t start_slot = 0;
    uint32_t slots = 0;
};

inline bool vsh_draw_overlap_same_program(
    const VshDrawOverlapProgramIdentity& a,
    const VshDrawOverlapProgramIdentity& b) {
    return a.known == b.known && a.fingerprint == b.fingerprint &&
           a.start_slot == b.start_slot && a.slots == b.slots;
}

struct VshDrawOverlapKey {
    VshDrawOverlapProgramIdentity program{};
    VshDrawOverlapOutcome outcome = VshDrawOverlapOutcome::kUnknown;
    uint32_t transform_mode = 0;
    bool fixed_function = false;
    bool programmable = false;
    bool compiled = false;
    bool writes_context = false;
    uint32_t fixture = 0; // CPU proposal before backend admission; not a success flag.
};

inline bool vsh_draw_overlap_same_key(const VshDrawOverlapKey& a,
                                      const VshDrawOverlapKey& b) {
    return vsh_draw_overlap_same_program(a.program, b.program) &&
           a.outcome == b.outcome && a.transform_mode == b.transform_mode &&
           a.fixed_function == b.fixed_function &&
           a.programmable == b.programmable && a.compiled == b.compiled &&
           a.writes_context == b.writes_context && a.fixture == b.fixture;
}

struct VshDrawOverlapCounters {
    uint64_t batches = 0;
    uint64_t submitted_vertices = 0;
    uint64_t transformed_vertices = 0;
    uint64_t executed_vsh_vertices = 0;
    uint64_t index_cache_hits = 0;

    std::array<uint64_t, kVshDrawOverlapOutcomeCount> outcomes{};

    std::array<uint64_t, 4> transform_mode_batches{};
    uint64_t transform_mode_other_batches = 0;
    uint64_t fixed_function_batches = 0;
    uint64_t programmable_batches = 0;
    uint64_t interpreted_batches = 0;
    uint64_t compiled_batches = 0;
    uint64_t context_writing_batches = 0;

    // Timed only around the first generated-shader collection pass. Verifier
    // reruns and post-decline software fallback passes are excluded by design.
    uint64_t generated_setup_calls = 0;
    uint64_t generated_setup_raw_ticks = 0;
    uint64_t generated_setup_compensated_ticks = 0;
    uint64_t fixture_setup_calls = 0;
    uint64_t fixture_setup_raw_ticks = 0;
    uint64_t fixture_setup_compensated_ticks = 0;
    uint64_t fixture_input_vertices = 0;
    uint64_t fixture_output_vertices = 0;
    uint64_t discarded_deltas = 0;

    uint64_t primitive_input_triangles = 0;
    uint64_t primitive_collected_triangles = 0;
    uint64_t primitive_refused_batches = 0;
    uint64_t primitive_nonfinite_batches = 0;
};

struct VshDrawOverlapRow {
    bool used = false;
    VshDrawOverlapKey key{};
    VshDrawOverlapCounters totals{};
};

struct VshDrawOverlapJob {
    bool valid = false;
    bool counted = false;
    VshDrawOverlapProgramIdentity program{};
    VshDrawOverlapOutcome outcome = VshDrawOverlapOutcome::kUnknown;

    uint64_t submitted_vertices = 0;
    uint64_t transformed_vertices = 0;
    uint64_t executed_vsh_vertices = 0;
    uint64_t index_cache_hits = 0;

    uint32_t transform_mode = 0;
    bool fixed_function = false;
    bool programmable = false;
    bool compiled = false;
    bool writes_context = false;
    uint32_t fixture = 0;

    uint64_t generated_setup_calls = 0;
    uint64_t generated_setup_raw_ticks = 0;
    uint64_t generated_setup_compensated_ticks = 0;
    uint64_t fixture_setup_calls = 0;
    uint64_t fixture_setup_raw_ticks = 0;
    uint64_t fixture_setup_compensated_ticks = 0;
    uint64_t fixture_input_vertices = 0;
    uint64_t fixture_output_vertices = 0;
    uint64_t discarded_deltas = 0;

    uint64_t primitive_input_triangles = 0;
    uint64_t primitive_collected_triangles = 0;
    uint64_t primitive_refused_batches = 0;
    uint64_t primitive_nonfinite_batches = 0;
};

struct VshDrawOverlapCensus {
    static constexpr size_t kRows = 256;
    bool enabled = false;
    VshDrawOverlapCounters totals{};
    std::array<VshDrawOverlapRow, kRows> rows{};
    size_t row_count = 0;
    VshDrawOverlapRow overflow{};
    VshDrawOverlapCounters unknown_program{};  // Supplemental, not exclusive.
};

inline VshDrawOverlapKey vsh_draw_overlap_key(const VshDrawOverlapJob& job) {
    VshDrawOverlapKey key{};
    key.program = job.program;
    key.outcome = job.outcome;
    key.transform_mode = job.transform_mode;
    key.fixed_function = job.fixed_function;
    key.programmable = job.programmable;
    key.compiled = job.programmable && job.compiled;
    key.writes_context = job.writes_context;
    key.fixture = job.fixture;
    return key;
}

inline void vsh_draw_overlap_increment_outcome(
    VshDrawOverlapCounters& counters, VshDrawOverlapOutcome outcome) {
    const size_t slot = static_cast<size_t>(outcome);
    if (slot < counters.outcomes.size()) ++counters.outcomes[slot];
}

inline void vsh_draw_overlap_add(VshDrawOverlapCounters& counters,
                                 const VshDrawOverlapJob& job) {
    ++counters.batches;
    counters.submitted_vertices += job.submitted_vertices;
    counters.transformed_vertices += job.transformed_vertices;
    counters.executed_vsh_vertices += job.executed_vsh_vertices;
    counters.index_cache_hits += job.index_cache_hits;
    vsh_draw_overlap_increment_outcome(counters, job.outcome);
    if (job.transform_mode < counters.transform_mode_batches.size())
        ++counters.transform_mode_batches[job.transform_mode];
    else
        ++counters.transform_mode_other_batches;
    if (job.fixed_function) ++counters.fixed_function_batches;
    if (job.programmable) ++counters.programmable_batches;
    if (job.programmable && job.compiled) ++counters.compiled_batches;
    if (job.programmable && !job.compiled) ++counters.interpreted_batches;
    if (job.writes_context) ++counters.context_writing_batches;

    counters.generated_setup_calls += job.generated_setup_calls;
    counters.generated_setup_raw_ticks += job.generated_setup_raw_ticks;
    counters.generated_setup_compensated_ticks +=
        job.generated_setup_compensated_ticks;
    counters.fixture_setup_calls += job.fixture_setup_calls;
    counters.fixture_setup_raw_ticks += job.fixture_setup_raw_ticks;
    counters.fixture_setup_compensated_ticks += job.fixture_setup_compensated_ticks;
    counters.fixture_input_vertices += job.fixture_input_vertices;
    counters.fixture_output_vertices += job.fixture_output_vertices;
    counters.discarded_deltas += job.discarded_deltas;

    counters.primitive_input_triangles += job.primitive_input_triangles;
    counters.primitive_collected_triangles += job.primitive_collected_triangles;
    counters.primitive_refused_batches += job.primitive_refused_batches;
    counters.primitive_nonfinite_batches += job.primitive_nonfinite_batches;
}

inline VshDrawOverlapRow& vsh_draw_overlap_row(VshDrawOverlapCensus& census,
                                                const VshDrawOverlapJob& job) {
    const VshDrawOverlapKey key = vsh_draw_overlap_key(job);
    for (size_t i = 0; i < census.row_count; ++i) {
        if (vsh_draw_overlap_same_key(census.rows[i].key, key))
            return census.rows[i];
    }
    if (census.row_count < census.rows.size()) {
        VshDrawOverlapRow& row = census.rows[census.row_count++];
        row.used = true;
        row.key = key;
        return row;
    }
    census.overflow.used = true;
    return census.overflow;
}

inline bool vsh_draw_overlap_record(VshDrawOverlapCensus& census,
                                    VshDrawOverlapJob& job) {
    if (!census.enabled || !job.valid || job.counted) return false;
    vsh_draw_overlap_add(census.totals, job);
    VshDrawOverlapRow& row = vsh_draw_overlap_row(census, job);
    vsh_draw_overlap_add(row.totals, job);
    if (!job.program.known) vsh_draw_overlap_add(census.unknown_program, job);
    job.counted = true;
    return true;
}

}  // namespace xlle
