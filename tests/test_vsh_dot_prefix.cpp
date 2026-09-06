#include "machine/nv2a_vsh_dot_prefix.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

#include "nv2a_vsh_cpu.h"

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

using xlle::VshCompiledOp;
using xlle::VshDotPrefixDispatch;
using xlle::VshDotPrefixPlan;

constexpr uint8_t kIdentity = VshDotPrefixPlan::kIdentitySwizzle;

void set_bits(float& value, uint32_t bits) {
    std::memcpy(&value, &bits, sizeof(bits));
}

uint32_t bits(float value) {
    uint32_t out = 0;
    std::memcpy(&out, &value, sizeof(out));
    return out;
}

VshDotPrefixDispatch dispatch() {
    return {nv2a_vsh_cpu_dp3, nv2a_vsh_cpu_dp4};
}

VshCompiledOp dot_op(VshCompiledOp::Fn fn, uint16_t input_index,
                    uint16_t context_index, uint8_t out_base,
                    uint16_t out_index, uint8_t out_mask) {
    VshCompiledOp op{};
    op.fn = fn;
    op.in_offset[0] = static_cast<uint16_t>(input_index * 4u);
    op.in_offset[1] = static_cast<uint16_t>(context_index * 4u);
    op.in_base[0] = xlle::kVshBaseInput;
    op.in_base[1] = xlle::kVshBaseContext;
    op.in_swizzle[0] = kIdentity;
    op.in_swizzle[1] = kIdentity;
    op.input_count = 2;
    op.out_base[0] = out_base;
    op.out_offset[0] = static_cast<uint16_t>(out_index * 4u);
    op.out_mask[0] = out_mask;
    op.out_base[1] = xlle::kVshBaseNone;
    return op;
}

VshCompiledOp suffix_mov_temp_to_output() {
    VshCompiledOp op{};
    op.fn = nv2a_vsh_cpu_mov;
    op.in_base[0] = xlle::kVshBaseTemp;
    op.in_offset[0] = 0;
    op.in_swizzle[0] = kIdentity;
    op.input_count = 1;
    op.out_base[0] = xlle::kVshBaseOutput;
    op.out_offset[0] = 4u * 4u;
    op.out_mask[0] = 2u;
    op.out_base[1] = xlle::kVshBaseNone;
    return op;
}

struct State {
    std::array<float, 16 * 4> input{};
    std::array<float, 13 * 4> output{};
    std::array<float, 12 * 4> temp{};
    std::array<float, 192 * 4> context{};
    std::array<float, 4> address{};

    State() {
        for (uint32_t i = 0; i < input.size(); ++i)
            set_bits(input[i], 0x3F000000u + i);
        for (uint32_t i = 0; i < output.size(); ++i)
            set_bits(output[i], 0x40000000u + i);
        for (uint32_t i = 0; i < temp.size(); ++i)
            set_bits(temp[i], 0x41000000u + i);
        for (uint32_t i = 0; i < context.size(); ++i)
            set_bits(context[i], 0x3E800000u + i);
        for (uint32_t i = 0; i < address.size(); ++i)
            set_bits(address[i], 0x42000000u + i);
    }

    float* const* bases() {
        base_ptrs[0] = temp.data();
        base_ptrs[1] = output.data();
        base_ptrs[2] = input.data();
        base_ptrs[3] = context.data();
        base_ptrs[4] = address.data();
        return base_ptrs.data();
    }

private:
    std::array<float*, xlle::kVshBaseCount> base_ptrs{};
};

bool same_state(const State& a, const State& b) {
    return std::memcmp(a.input.data(), b.input.data(), a.input.size() * sizeof(float)) == 0 &&
           std::memcmp(a.output.data(), b.output.data(), a.output.size() * sizeof(float)) == 0 &&
           std::memcmp(a.temp.data(), b.temp.data(), a.temp.size() * sizeof(float)) == 0 &&
           std::memcmp(a.context.data(), b.context.data(), a.context.size() * sizeof(float)) == 0 &&
           std::memcmp(a.address.data(), b.address.data(), a.address.size() * sizeof(float)) == 0;
}

void run_compiled(State& state, const std::vector<VshCompiledOp>& ops) {
    xlle::vsh_execute_compiled(ops.data(), static_cast<uint32_t>(ops.size()),
                              state.bases());
}

void run_prefix_then_suffix(State& state, const std::vector<VshCompiledOp>& ops,
                            const VshDotPrefixPlan& plan) {
    const uint32_t suffix_start = xlle::vsh_execute_dot_prefix(plan, state.bases());
    xlle::vsh_execute_compiled(ops.data() + suffix_start,
                              static_cast<uint32_t>(ops.size()) - suffix_start,
                              state.bases());
}

void expect_plan(const std::vector<VshCompiledOp>& ops, uint32_t expected_ops,
                 uint32_t expected_sources) {
    VshDotPrefixPlan plan;
    CHECK(xlle::vsh_build_dot_prefix_plan(ops.data(), static_cast<uint32_t>(ops.size()),
                                          dispatch(), plan));
    CHECK(plan.op_count == expected_ops);
    CHECK(plan.source_count == expected_sources);
}

void expect_decline(std::vector<VshCompiledOp> ops) {
    VshDotPrefixPlan plan;
    CHECK(!xlle::vsh_build_dot_prefix_plan(ops.data(), static_cast<uint32_t>(ops.size()),
                                           dispatch(), plan));
    CHECK(plan.op_count == 0);
    CHECK(plan.source_count == 0);
}

void test_declines_before_two_ops() {
    expect_decline({dot_op(nv2a_vsh_cpu_dp3, 0, 0, xlle::kVshBaseTemp, 0, 8)});
    expect_decline({suffix_mov_temp_to_output(),
                    dot_op(nv2a_vsh_cpu_dp3, 0, 0, xlle::kVshBaseTemp, 0, 8)});
}

void test_rejects_unsupported_prefix_shapes() {
    VshCompiledOp paired = dot_op(nv2a_vsh_cpu_dp3, 0, 0, xlle::kVshBaseTemp, 0, 8);
    paired.paired = 1;
    expect_decline({paired, dot_op(nv2a_vsh_cpu_dp4, 1, 1, xlle::kVshBaseTemp, 1, 4)});

    VshCompiledOp double_output = dot_op(nv2a_vsh_cpu_dp3, 0, 0, xlle::kVshBaseTemp, 0, 8);
    double_output.out_base[1] = xlle::kVshBaseOutput;
    expect_decline({double_output, dot_op(nv2a_vsh_cpu_dp4, 1, 1, xlle::kVshBaseTemp, 1, 4)});

    VshCompiledOp full_mask = dot_op(nv2a_vsh_cpu_dp3, 0, 0, xlle::kVshBaseTemp, 0, 0xF);
    expect_decline({full_mask, dot_op(nv2a_vsh_cpu_dp4, 1, 1, xlle::kVshBaseTemp, 1, 4)});

    VshCompiledOp zero_mask = dot_op(nv2a_vsh_cpu_dp3, 0, 0, xlle::kVshBaseTemp, 0, 0);
    expect_decline({zero_mask, dot_op(nv2a_vsh_cpu_dp4, 1, 1, xlle::kVshBaseTemp, 1, 4)});

    VshCompiledOp negated = dot_op(nv2a_vsh_cpu_dp3, 0, 0, xlle::kVshBaseTemp, 0, 8);
    negated.in_negate_mask = 1;
    expect_decline({negated, dot_op(nv2a_vsh_cpu_dp4, 1, 1, xlle::kVshBaseTemp, 1, 4)});

    VshCompiledOp swizzled = dot_op(nv2a_vsh_cpu_dp3, 0, 0, xlle::kVshBaseTemp, 0, 8);
    swizzled.in_swizzle[0] = 0;
    expect_decline({swizzled, dot_op(nv2a_vsh_cpu_dp4, 1, 1, xlle::kVshBaseTemp, 1, 4)});

    VshCompiledOp relative = dot_op(nv2a_vsh_cpu_dp3, 0, 0, xlle::kVshBaseTemp, 0, 8);
    relative.in_relative_mask = 2;
    expect_decline({relative, dot_op(nv2a_vsh_cpu_dp4, 1, 1, xlle::kVshBaseTemp, 1, 4)});

    VshCompiledOp bad_input_range = dot_op(nv2a_vsh_cpu_dp3, 16, 0, xlle::kVshBaseTemp, 0, 8);
    expect_decline({bad_input_range, dot_op(nv2a_vsh_cpu_dp4, 1, 1, xlle::kVshBaseTemp, 1, 4)});

    VshCompiledOp bad_context_range = dot_op(nv2a_vsh_cpu_dp3, 0, 192, xlle::kVshBaseTemp, 0, 8);
    expect_decline({bad_context_range, dot_op(nv2a_vsh_cpu_dp4, 1, 1, xlle::kVshBaseTemp, 1, 4)});

    VshCompiledOp bad_output_range = dot_op(nv2a_vsh_cpu_dp3, 0, 0, xlle::kVshBaseOutput, 13, 8);
    expect_decline({bad_output_range, dot_op(nv2a_vsh_cpu_dp4, 1, 1, xlle::kVshBaseTemp, 1, 4)});

    VshCompiledOp bad_temp_range = dot_op(nv2a_vsh_cpu_dp3, 0, 0, xlle::kVshBaseTemp, 12, 8);
    expect_decline({bad_temp_range, dot_op(nv2a_vsh_cpu_dp4, 1, 1, xlle::kVshBaseTemp, 1, 4)});

    VshCompiledOp misaligned_input = dot_op(nv2a_vsh_cpu_dp3, 0, 0, xlle::kVshBaseTemp, 0, 8);
    misaligned_input.in_offset[0] = 1;
    expect_decline({misaligned_input, dot_op(nv2a_vsh_cpu_dp4, 1, 1, xlle::kVshBaseTemp, 1, 4)});

    VshCompiledOp misaligned_context = dot_op(nv2a_vsh_cpu_dp3, 0, 0, xlle::kVshBaseTemp, 0, 8);
    misaligned_context.in_offset[1] = 2;
    expect_decline({misaligned_context, dot_op(nv2a_vsh_cpu_dp4, 1, 1, xlle::kVshBaseTemp, 1, 4)});

    VshCompiledOp misaligned_output = dot_op(nv2a_vsh_cpu_dp3, 0, 0, xlle::kVshBaseTemp, 0, 8);
    misaligned_output.out_offset[0] = 3;
    expect_decline({misaligned_output, dot_op(nv2a_vsh_cpu_dp4, 1, 1, xlle::kVshBaseTemp, 1, 4)});
}

void test_accepts_upper_bounds() {
    std::vector<VshCompiledOp> ops = {
        dot_op(nv2a_vsh_cpu_dp3, 15, 191, xlle::kVshBaseTemp, 11, 8),
        dot_op(nv2a_vsh_cpu_dp4, 15, 191, xlle::kVshBaseOutput, 12, 1),
    };
    expect_plan(ops, 2, 2);
}

void seed_special_values(State& state) {
    const uint32_t values[] = {
        0x00000000u, 0x80000000u, 0x00000001u, 0x80000001u,
        0x7F800000u, 0xFF800000u, 0x7F800001u, 0x7FC00001u,
        0x7F7FFFFFu, 0xFF7FFFFFu, 0x3F800000u, 0xBF800000u,
    };
    for (uint32_t i = 0; i < 16; ++i) {
        for (uint32_t lane = 0; lane < 4; ++lane)
            set_bits(state.input[i * 4 + lane], values[(i + lane) % 12]);
    }
    for (uint32_t i = 0; i < 192; ++i) {
        for (uint32_t lane = 0; lane < 4; ++lane)
            set_bits(state.context[i * 4 + lane], values[(i * 3 + lane + 5) % 12]);
    }
}

void test_prefix_suffix_matches_compiled_executor_for_special_values() {
    std::vector<VshCompiledOp> ops = {
        dot_op(nv2a_vsh_cpu_dp3, 0, 0, xlle::kVshBaseTemp, 0, 8),
        dot_op(nv2a_vsh_cpu_dp4, 1, 1, xlle::kVshBaseOutput, 3, 4),
        suffix_mov_temp_to_output(),
    };
    VshDotPrefixPlan plan;
    CHECK(xlle::vsh_build_dot_prefix_plan(ops.data(), static_cast<uint32_t>(ops.size()),
                                          dispatch(), plan));
    CHECK(plan.op_count == 2);

    State compiled;
    State prefix;
    seed_special_values(compiled);
    seed_special_values(prefix);
    run_compiled(compiled, ops);
    run_prefix_then_suffix(prefix, ops, plan);
    CHECK(same_state(compiled, prefix));
}

void fill_finite_pattern(State& state, uint32_t seed) {
    for (uint32_t i = 0; i < state.input.size(); ++i) {
        uint32_t bits = 0x3E000000u + ((seed * 977u + i * 131u) & 0x003FFFFFu);
        if ((i + seed) & 1u) bits |= 0x80000000u;
        set_bits(state.input[i], bits);
    }
    for (uint32_t i = 0; i < state.context.size(); ++i) {
        uint32_t bits = 0x3F000000u + ((seed * 313u + i * 17u) & 0x003FFFFFu);
        if ((i + seed) & 2u) bits |= 0x80000000u;
        set_bits(state.context[i], bits);
    }
}

void test_finite_pattern_sweep_matches_compiled_executor() {
    std::vector<VshCompiledOp> ops = {
        dot_op(nv2a_vsh_cpu_dp3, 0, 3, xlle::kVshBaseTemp, 0, 8),
        dot_op(nv2a_vsh_cpu_dp4, 4, 9, xlle::kVshBaseOutput, 3, 4),
        dot_op(nv2a_vsh_cpu_dp3, 15, 191, xlle::kVshBaseTemp, 11, 2),
        suffix_mov_temp_to_output(),
    };
    VshDotPrefixPlan plan;
    CHECK(xlle::vsh_build_dot_prefix_plan(ops.data(), static_cast<uint32_t>(ops.size()),
                                          dispatch(), plan));
    CHECK(plan.op_count == 3);

    for (uint32_t seed = 0; seed < 2048; ++seed) {
        State compiled;
        State prefix;
        fill_finite_pattern(compiled, seed);
        fill_finite_pattern(prefix, seed);
        run_compiled(compiled, ops);
        run_prefix_then_suffix(prefix, ops, plan);
        CHECK(same_state(compiled, prefix));
    }
}

void test_isolated_finite_overflow_matches_compiled_executor() {
    std::vector<VshCompiledOp> ops = {
        dot_op(nv2a_vsh_cpu_dp3, 0, 0, xlle::kVshBaseTemp, 0, 8),
        dot_op(nv2a_vsh_cpu_dp4, 1, 1, xlle::kVshBaseOutput, 3, 4),
    };
    VshDotPrefixPlan plan;
    CHECK(xlle::vsh_build_dot_prefix_plan(ops.data(), static_cast<uint32_t>(ops.size()),
                                          dispatch(), plan));

    State compiled;
    State prefix;
    for (float& value : compiled.input) value = 0.0f;
    for (float& value : compiled.context) value = 0.0f;
    prefix.input = compiled.input;
    prefix.context = compiled.context;
    set_bits(compiled.input[0], 0x7F7FFFFFu);
    set_bits(compiled.context[0], 0x40000000u);
    set_bits(prefix.input[0], 0x7F7FFFFFu);
    set_bits(prefix.context[0], 0x40000000u);

    run_compiled(compiled, ops);
    run_prefix_then_suffix(prefix, ops, plan);
    CHECK(same_state(compiled, prefix));
}

void test_input_and_context_values_are_dynamic_not_baked() {
    std::vector<VshCompiledOp> ops = {
        dot_op(nv2a_vsh_cpu_dp3, 2, 7, xlle::kVshBaseTemp, 2, 1),
        dot_op(nv2a_vsh_cpu_dp4, 2, 7, xlle::kVshBaseOutput, 9, 8),
        suffix_mov_temp_to_output(),
    };
    VshDotPrefixPlan plan;
    CHECK(xlle::vsh_build_dot_prefix_plan(ops.data(), static_cast<uint32_t>(ops.size()),
                                          dispatch(), plan));
    CHECK(plan.source_count == 2);

    State compiled;
    State prefix;
    set_bits(compiled.input[2 * 4 + 0], 0x40400000u);
    set_bits(compiled.context[7 * 4 + 0], 0x40800000u);
    set_bits(prefix.input[2 * 4 + 0], 0x40400000u);
    set_bits(prefix.context[7 * 4 + 0], 0x40800000u);
    run_compiled(compiled, ops);
    run_prefix_then_suffix(prefix, ops, plan);
    CHECK(same_state(compiled, prefix));

    State changed_compiled;
    State changed_prefix;
    set_bits(changed_compiled.input[2 * 4 + 0], 0xC1200000u);
    set_bits(changed_compiled.context[7 * 4 + 0], 0x40A00000u);
    set_bits(changed_prefix.input[2 * 4 + 0], 0xC1200000u);
    set_bits(changed_prefix.context[7 * 4 + 0], 0x40A00000u);
    run_compiled(changed_compiled, ops);
    run_prefix_then_suffix(changed_prefix, ops, plan);
    CHECK(same_state(changed_compiled, changed_prefix));
    CHECK(bits(compiled.output[9 * 4]) != bits(changed_compiled.output[9 * 4]));
}

void test_unsupported_op_after_valid_prefix_becomes_suffix() {
    std::vector<VshCompiledOp> ops = {
        dot_op(nv2a_vsh_cpu_dp3, 0, 0, xlle::kVshBaseTemp, 0, 8),
        dot_op(nv2a_vsh_cpu_dp4, 1, 1, xlle::kVshBaseOutput, 3, 4),
        suffix_mov_temp_to_output(),
    };
    expect_plan(ops, 2, 4);
}

void test_paired_suffix_after_prefix_preserves_compiled_pairing() {
    VshCompiledOp paired_mac = suffix_mov_temp_to_output();
    paired_mac.out_base[0] = xlle::kVshBaseTemp;
    paired_mac.out_offset[0] = 1u * 4u;
    paired_mac.out_mask[0] = 8u;
    paired_mac.paired = 1u;

    VshCompiledOp paired_ilu = suffix_mov_temp_to_output();
    paired_ilu.in_base[0] = xlle::kVshBaseTemp;
    paired_ilu.in_offset[0] = 1u * 4u;
    paired_ilu.out_base[0] = xlle::kVshBaseOutput;
    paired_ilu.out_offset[0] = 5u * 4u;
    paired_ilu.out_mask[0] = 8u;

    std::vector<VshCompiledOp> ops = {
        dot_op(nv2a_vsh_cpu_dp3, 0, 0, xlle::kVshBaseTemp, 0, 8),
        dot_op(nv2a_vsh_cpu_dp4, 1, 1, xlle::kVshBaseOutput, 3, 4),
        paired_mac,
        paired_ilu,
    };
    VshDotPrefixPlan plan;
    CHECK(xlle::vsh_build_dot_prefix_plan(ops.data(), static_cast<uint32_t>(ops.size()),
                                          dispatch(), plan));
    CHECK(plan.op_count == 2);

    State compiled;
    State prefix;
    set_bits(compiled.temp[1u * 4u], 0x3F123456u);
    set_bits(prefix.temp[1u * 4u], 0x3F123456u);
    run_compiled(compiled, ops);
    run_prefix_then_suffix(prefix, ops, plan);
    CHECK(same_state(compiled, prefix));
    CHECK(bits(prefix.output[5u * 4u]) == 0x3F123456u);
}

void test_prefix_cap_continues_with_suffix() {
    std::vector<VshCompiledOp> ops;
    for (uint32_t i = 0; i < VshDotPrefixPlan::kMaxOps + 1; ++i) {
        ops.push_back(dot_op((i & 1u) ? nv2a_vsh_cpu_dp4 : nv2a_vsh_cpu_dp3,
                             static_cast<uint16_t>(i % 16u),
                             static_cast<uint16_t>(i % 32u),
                             (i & 1u) ? xlle::kVshBaseOutput : xlle::kVshBaseTemp,
                             static_cast<uint16_t>((i & 1u) ? (i % 13u) : (i % 12u)),
                             static_cast<uint8_t>(1u << (i % 4u))));
    }
    ops.push_back(suffix_mov_temp_to_output());

    VshDotPrefixPlan plan;
    CHECK(xlle::vsh_build_dot_prefix_plan(ops.data(), static_cast<uint32_t>(ops.size()),
                                          dispatch(), plan));
    CHECK(plan.op_count == VshDotPrefixPlan::kMaxOps);

    State compiled;
    State prefix;
    seed_special_values(compiled);
    seed_special_values(prefix);
    run_compiled(compiled, ops);
    run_prefix_then_suffix(prefix, ops, plan);
    CHECK(same_state(compiled, prefix));
}

void test_reused_plan_rebuild_and_decline_lifecycle() {
    VshDotPrefixPlan plan;
    std::vector<VshCompiledOp> valid = {
        dot_op(nv2a_vsh_cpu_dp3, 0, 0, xlle::kVshBaseTemp, 0, 8),
        dot_op(nv2a_vsh_cpu_dp4, 1, 1, xlle::kVshBaseOutput, 3, 4),
    };
    CHECK(xlle::vsh_build_dot_prefix_plan(valid.data(), static_cast<uint32_t>(valid.size()),
                                          dispatch(), plan));
    CHECK(plan.op_count == 2);

    std::vector<VshCompiledOp> mutated_valid = valid;
    mutated_valid[0].in_offset[0] = 2u * 4u;
    mutated_valid[1].out_offset[0] = 10u * 4u;
    CHECK(xlle::vsh_build_dot_prefix_plan(mutated_valid.data(),
                                          static_cast<uint32_t>(mutated_valid.size()),
                                          dispatch(), plan));
    CHECK(plan.op_count == 2);
    CHECK(plan.ops[0].source[0] == 0);
    CHECK(plan.sources[0].offset == 2u * 4u);
    CHECK(plan.ops[1].out_offset == 10u * 4u);
    State changed_reference;
    State changed_candidate;
    run_compiled(changed_reference, mutated_valid);
    run_prefix_then_suffix(changed_candidate, mutated_valid, plan);
    CHECK(same_state(changed_reference, changed_candidate));

    std::vector<VshCompiledOp> declined = mutated_valid;
    declined[0].in_base[0] = xlle::kVshBaseTemp;
    CHECK(!xlle::vsh_build_dot_prefix_plan(declined.data(),
                                           static_cast<uint32_t>(declined.size()),
                                           dispatch(), plan));
    CHECK(plan.op_count == 0);
    CHECK(plan.source_count == 0);
}

}  // namespace

int main() {
    test_declines_before_two_ops();
    test_rejects_unsupported_prefix_shapes();
    test_accepts_upper_bounds();
    test_prefix_suffix_matches_compiled_executor_for_special_values();
    test_finite_pattern_sweep_matches_compiled_executor();
    test_isolated_finite_overflow_matches_compiled_executor();
    test_input_and_context_values_are_dynamic_not_baked();
    test_unsupported_op_after_valid_prefix_becomes_suffix();
    test_paired_suffix_after_prefix_preserves_compiled_pairing();
    test_prefix_cap_continues_with_suffix();
    test_reused_plan_rebuild_and_decline_lifecycle();

    if (g_failures) {
        std::cerr << "test_vsh_dot_prefix: " << g_failures << " checks failed\n";
        return 1;
    }
    std::cout << "test_vsh_dot_prefix: ok\n";
    return 0;
}
