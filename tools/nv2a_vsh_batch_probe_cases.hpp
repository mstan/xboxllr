#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <limits>
#include <vector>

#include "machine/nv2a_vsh_batch_capture.hpp"
#include "nv2a_vsh_capture_oracle.hpp"
#include "nv2a_vsh_triangle_oracle.hpp"

namespace xlle::vsh_shadow_probe {

namespace detail {

inline uint32_t bits(float value) {
    uint32_t out = 0;
    std::memcpy(&out, &value, sizeof(out));
    return out;
}

inline float from_bits(uint32_t word) {
    float out = 0.0f;
    std::memcpy(&out, &word, sizeof(out));
    return out;
}

inline float clamp_away_zero_inf(float value) {
    const float minimum = std::ldexp(1.0f, -64);
    const float maximum = std::ldexp(1.0f, 64);
    uint32_t word = 0;
    std::memcpy(&word, &value, sizeof(word));
    if (value > 0.0f || word == 0u)
        return std::clamp(value, minimum, maximum);
    return std::clamp(value, -maximum, -minimum);
}

inline float round_screen_coordinate(float value) {
    return std::trunc(value * 16.0f) / 16.0f;
}

inline vsh_capture::Op mov_input_to_output(unsigned input_reg, unsigned output_reg,
                                           uint32_t mask) {
    vsh_capture::Op op{};
    op.words[0] = 1u;
    op.words[2] = 1u;
    op.words[4] = kVshBaseInput;
    op.words[5] = kVshBaseNone;
    op.words[6] = kVshBaseNone;
    op.words[8] = input_reg * 4u;
    op.words[12] = 0xE4u;
    op.words[16] = kVshBaseOutput;
    op.words[17] = kVshBaseNone;
    op.words[18] = output_reg * 4u;
    op.words[20] = mask;
    return op;
}

inline std::vector<vsh_capture::Op> mov_program() {
    return {mov_input_to_output(0, 0, 0xFu), mov_input_to_output(3, 3, 0xFu),
            mov_input_to_output(4, 4, 0xFu), mov_input_to_output(9, 9, 0xFu),
            mov_input_to_output(10, 10, 0xFu), mov_input_to_output(11, 11, 0xFu),
            mov_input_to_output(12, 12, 0xFu)};
}

inline void set_input(std::array<uint32_t, 64>& inputs, unsigned reg,
                      std::array<float, 4> values) {
    for (unsigned lane = 0; lane < 4; ++lane)
        inputs[reg * 4u + lane] = bits(values[lane]);
}

inline vsh_batch_capture::Execution make_execution(float x, float y, float z,
                                                   float w,
                                                   std::array<float, 4> diffuse =
                                                       {1.0f, 0.5f, 0.25f, 1.0f}) {
    vsh_capture::Vertex vertex;
    vertex.ordinal = 1;
    vertex.ops = mov_program();
    set_input(vertex.inputs, 0, {x, y, z, w});
    set_input(vertex.inputs, 3, diffuse);
    set_input(vertex.inputs, 4, {0.0f, 0.0f, 0.0f, 1.0f});
    set_input(vertex.inputs, 9, {0.0f, 0.0f, 0.0f, 1.0f});
    set_input(vertex.inputs, 10, {0.0f, 0.0f, 0.0f, 1.0f});
    set_input(vertex.inputs, 11, {0.0f, 0.0f, 0.0f, 1.0f});
    set_input(vertex.inputs, 12, {0.0f, 0.0f, 0.0f, 1.0f});

    vsh_batch_capture::Execution execution;
    execution.inputs = vertex.inputs;
    const auto raw = vsh_capture_oracle::validate_and_execute(vertex);
    std::copy(raw.begin(), raw.end(), execution.expected.begin());
    execution.expected[28] = bits(round_screen_coordinate(from_bits(raw[0])));
    execution.expected[29] = bits(round_screen_coordinate(from_bits(raw[1])));
    execution.expected[30] = raw[2];
    execution.expected[31] = bits(clamp_away_zero_inf(from_bits(raw[3])));
    return execution;
}

inline vsh_batch_capture::Batch make_uncached(uint64_t sequence, uint32_t primitive_mode,
                                              std::vector<vsh_batch_capture::Execution> executions,
                                              std::vector<uint32_t> expected_indices) {
    vsh_batch_capture::Batch batch;
    batch.sequence = sequence;
    batch.fingerprint = 0;
    batch.primitive_mode = primitive_mode;
    batch.ops = mov_program();
    batch.executions = std::move(executions);
    for (uint32_t i = 0; i < batch.executions.size(); ++i) {
        batch.source_indices.push_back(1000u + i * 7u);
        batch.references.push_back(i);
    }
    batch.expected_indices = std::move(expected_indices);
    return batch;
}

inline void verify_expected(const vsh_batch_capture::Batch& batch) {
    const auto expanded = vsh_triangle_oracle::expand_resident_primary(batch);
    if (expanded.declined != batch.expected_decline ||
        expanded.indices != batch.expected_indices) {
        throw std::runtime_error("Synthetic VSH batch probe case expectation mismatch");
    }
}

inline void add_case(std::vector<vsh_batch_capture::Batch>& cases,
                     vsh_batch_capture::Batch batch) {
    batch.sequence = static_cast<uint64_t>(cases.size() + 1u);
    verify_expected(batch);
    vsh_batch_capture::detail::validate_batch(batch);
    cases.push_back(std::move(batch));
}

}  // namespace detail

inline std::vector<vsh_batch_capture::Batch> make_batch_probe_cases() {
    std::vector<vsh_batch_capture::Batch> cases;
    cases.reserve(16);

    detail::add_case(cases, detail::make_uncached(
        1, 5, {detail::make_execution(0.0f, 0.0f, 0.0f, 1.0f),
               detail::make_execution(1.0f, 0.0f, 0.0f, 1.0f),
               detail::make_execution(0.0f, 1.0f, 0.0f, 1.0f),
               detail::make_execution(2.0f, 0.0f, 0.0f, 1.0f),
               detail::make_execution(3.0f, 0.0f, 0.0f, 1.0f),
               detail::make_execution(2.0f, 1.0f, 0.0f, 1.0f)},
        {0, 1, 2, 3, 4, 5}));

    detail::add_case(cases, detail::make_uncached(
        2, 6, {detail::make_execution(0.0f, 0.0f, 0.0f, 1.0f),
               detail::make_execution(1.0f, 0.0f, 0.0f, 1.0f),
               detail::make_execution(0.0f, 1.0f, 0.0f, 1.0f),
               detail::make_execution(1.0f, 1.0f, 0.0f, 1.0f)},
        {0, 1, 2, 2, 1, 3}));

    detail::add_case(cases, detail::make_uncached(
        3, 7, {detail::make_execution(0.0f, 0.0f, 0.0f, 1.0f),
               detail::make_execution(1.0f, 0.0f, 0.0f, 1.0f),
               detail::make_execution(0.0f, 1.0f, 0.0f, 1.0f),
               detail::make_execution(-1.0f, 0.0f, 0.0f, 1.0f)},
        {0, 1, 2, 0, 2, 3}));

    detail::add_case(cases, detail::make_uncached(
        4, 5, {detail::make_execution(0.0f, 0.0f, 0.0f, 1.0f),
               detail::make_execution(0.0f, 1.0f, 0.0f, 1.0f),
               detail::make_execution(1.0f, 0.0f, 0.0f, 1.0f)},
        {0, 2, 1}));

    auto back_culled = detail::make_uncached(
        5, 5, {detail::make_execution(0.0f, 0.0f, 0.0f, 1.0f),
               detail::make_execution(0.0f, 1.0f, 0.0f, 1.0f),
               detail::make_execution(1.0f, 0.0f, 0.0f, 1.0f)},
        {});
    back_culled.cull_enabled = true;
    detail::add_case(cases, std::move(back_culled));

    auto front_culled = detail::make_uncached(
        6, 5, {detail::make_execution(0.0f, 0.0f, 0.0f, 1.0f),
               detail::make_execution(1.0f, 0.0f, 0.0f, 1.0f),
               detail::make_execution(0.0f, 1.0f, 0.0f, 1.0f)},
        {});
    front_culled.cull_enabled = true;
    front_culled.cull_front = true;
    detail::add_case(cases, std::move(front_culled));

    detail::add_case(cases, detail::make_uncached(
        7, 5, {detail::make_execution(0.0f, 0.0f, 0.0f, 1.0f),
               detail::make_execution(1.0f, 0.0f, 0.0f, 1.0f),
               detail::make_execution(2.0f, 0.0f, 0.0f, 1.0f)},
        {}));

    detail::add_case(cases, detail::make_uncached(
        8, 5, {detail::make_execution(0.0f, 0.0f, 0.0f, -1.0f),
               detail::make_execution(1.0f, 0.0f, 0.0f, -2.0f),
               detail::make_execution(0.0f, 1.0f, 0.0f, -3.0f)},
        {}));

    auto cross_w = detail::make_uncached(
        9, 5, {detail::make_execution(0.0f, 0.0f, 0.0f, -1.0f),
               detail::make_execution(1.0f, 0.0f, 0.0f, 1.0f),
               detail::make_execution(0.0f, 1.0f, 0.0f, 1.0f)},
        {});
    cross_w.expected_decline = true;
    detail::add_case(cases, std::move(cross_w));

    auto nan_diffuse = detail::make_uncached(
        10, 5, {detail::make_execution(0.0f, 0.0f, 0.0f, 1.0f,
                                      {std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f, 1.0f}),
                detail::make_execution(1.0f, 0.0f, 0.0f, 1.0f),
                detail::make_execution(0.0f, 1.0f, 0.0f, 1.0f)},
        {});
    nan_diffuse.expected_decline = true;
    detail::add_case(cases, std::move(nan_diffuse));

    auto unconsumed_nan = detail::make_uncached(
        11, 5, {detail::make_execution(0.0f, 0.0f, 0.0f, 1.0f),
                detail::make_execution(1.0f, 0.0f, 0.0f, 1.0f),
                detail::make_execution(0.0f, 1.0f, 0.0f, 1.0f)},
        {0, 1, 2});
    unconsumed_nan.executions[0].inputs[9u * 4u] =
        detail::bits(std::numeric_limits<float>::quiet_NaN());
    vsh_capture::Vertex unconsumed_vertex;
    unconsumed_vertex.ordinal = 1;
    unconsumed_vertex.ops = detail::mov_program();
    unconsumed_vertex.inputs = unconsumed_nan.executions[0].inputs;
    const auto raw = vsh_capture_oracle::validate_and_execute(unconsumed_vertex);
    std::copy(raw.begin(), raw.end(), unconsumed_nan.executions[0].expected.begin());
    unconsumed_nan.executions[0].expected[28] =
        detail::bits(detail::round_screen_coordinate(detail::from_bits(raw[0])));
    unconsumed_nan.executions[0].expected[29] =
        detail::bits(detail::round_screen_coordinate(detail::from_bits(raw[1])));
    unconsumed_nan.executions[0].expected[30] = raw[2];
    unconsumed_nan.executions[0].expected[31] =
        detail::bits(detail::clamp_away_zero_inf(detail::from_bits(raw[3])));
    detail::add_case(cases, std::move(unconsumed_nan));

    detail::add_case(cases, detail::make_uncached(
        12, 5, {detail::make_execution(62500.0f, 0.0f, 0.0f, 1.0f),
                detail::make_execution(62500.0f, 1.0f, 0.0f, 1.0f),
                detail::make_execution(62499.0f, 0.0f, 0.0f, 1.0f)},
        {0, 1, 2}));

    auto fixed_decline = detail::make_uncached(
        13, 5, {detail::make_execution(62500.1f, 0.0f, 0.0f, 1.0f),
                detail::make_execution(62500.1f, 1.0f, 0.0f, 1.0f),
                detail::make_execution(62499.0f, 0.0f, 0.0f, 1.0f)},
        {});
    fixed_decline.expected_decline = true;
    detail::add_case(cases, std::move(fixed_decline));

    auto large_area = detail::make_uncached(
        14, 5, {detail::make_execution(62500.1f, 0.0f, 0.0f, 1.0f),
                detail::make_execution(62500.1f, 1.0f, 0.0f, 1.0f),
                detail::make_execution(62499.0f, 0.0f, 0.0f, 1.0f)},
        {0, 1, 2});
    large_area.large_target = true;
    detail::add_case(cases, std::move(large_area));

    vsh_batch_capture::Batch collision;
    collision.sequence = 15;
    collision.primitive_mode = 5;
    collision.cache_enabled = true;
    collision.ops = detail::mov_program();
    const auto source70 = detail::make_execution(0.0f, 0.0f, 0.0f, 1.0f);
    collision.executions = {
        source70,                                           // source 70 first miss
        detail::make_execution(1.0f, 0.0f, 0.0f, 1.0f),      // source 1024, same cache slot
        detail::make_execution(0.0f, 1.0f, 0.0f, 1.0f),      // source 5
        source70,                                           // source 70 re-executes after replacement
        detail::make_execution(2.0f, 0.0f, 0.0f, 1.0f),      // source 1
        detail::make_execution(2.0f, 1.0f, 0.0f, 1.0f),      // source 2
    };
    collision.source_indices = {70u, 1024u, 5u, 70u, 1u, 2u, 1u, 2u, 70u};
    collision.references = {0u, 1u, 2u, 3u, 4u, 5u, 4u, 5u, 3u};
    collision.expected_indices = {0u, 1u, 2u, 3u, 4u, 5u, 4u, 5u, 3u};
    detail::add_case(cases, std::move(collision));

    vsh_batch_capture::Batch tail;
    tail.sequence = 16;
    tail.primitive_mode = 5;
    tail.cache_enabled = true;
    tail.ops = detail::mov_program();
    for (uint32_t i = 0; i < 198u; ++i) {
        const float base = static_cast<float>((i / 3u) * 4u);
        const uint32_t corner = i % 3u;
        const float x = corner == 1u ? base + 1.0f : base;
        const float y = corner == 2u ? 1.0f : 0.0f;
        tail.executions.push_back(detail::make_execution(x, y, 0.0f, 1.0f));
        tail.source_indices.push_back(i);
        tail.references.push_back(i);
        tail.expected_indices.push_back(i);
    }
    detail::add_case(cases, std::move(tail));

    // Signed zero reaches opposite postprocessed W clamps in the CPU floor.
    // Check the second dispatch's classification, not just the VSH words.
    auto positive_zero_w = detail::make_uncached(
        17, 5, {detail::make_execution(0.0f, 0.0f, 0.0f, 0.0f),
                detail::make_execution(1.0f, 0.0f, 0.0f, 0.0f),
                detail::make_execution(0.0f, 1.0f, 0.0f, 0.0f)}, {0, 1, 2});
    detail::add_case(cases, std::move(positive_zero_w));
    auto negative_zero_w = detail::make_uncached(
        18, 5, {detail::make_execution(0.0f, 0.0f, 0.0f, -0.0f),
                detail::make_execution(1.0f, 0.0f, 0.0f, -0.0f),
                detail::make_execution(0.0f, 1.0f, 0.0f, -0.0f)}, {});
    detail::add_case(cases, std::move(negative_zero_w));
    auto mixed_zero_w = detail::make_uncached(
        19, 5, {detail::make_execution(0.0f, 0.0f, 0.0f, -0.0f),
                detail::make_execution(1.0f, 0.0f, 0.0f, 0.0f),
                detail::make_execution(0.0f, 1.0f, 0.0f, 0.0f)}, {});
    mixed_zero_w.expected_decline = true;
    detail::add_case(cases, std::move(mixed_zero_w));

    if (cases.size() > vsh_batch_capture::kMaxBatches)
        throw std::runtime_error("Too many synthetic VSH batch probe cases");
    return cases;
}

}  // namespace xlle::vsh_shadow_probe
