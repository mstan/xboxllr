#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "nv2a_vsh_cpu.h"

namespace xlle::vsh_shadow_probe {

// Synthetic input ABI for host/nv2a_vsh_dot_probe_glsl.hpp.
// Link users against the vendored nv2a_vsh_cpu.c implementation; this header
// intentionally calls nv2a_vsh_cpu_dp3/dp4 instead of reimplementing those
// functions. It prepares operands using the same +1.0f fetch multiply that the
// emulator uses for non-negated operands.
struct DotInput {
    std::array<uint32_t, 4> a{};
    std::array<uint32_t, 4> b{};
};

struct DotOutput {
    // x=dp3 bits, y=dp4 bits, z=round_screen_coordinate(dp4) bits,
    // w=clamp_away_zero_inf(dp4) bits.
    std::array<uint32_t, 4> words{};
};

struct TestCase {
    DotInput input{};
    std::string category;
};

static_assert(sizeof(DotInput) == 32, "DotInput must stay std430-compatible");
static_assert(sizeof(DotOutput) == 16, "DotOutput must stay std430-compatible");

inline float bits_to_float(uint32_t bits) {
    float value = 0.0f;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

inline uint32_t float_to_bits(float value) {
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

inline float multiply_positive_one_for_fetch(float value) {
    static volatile float positive_one = 1.0f;
    return value * positive_one;
}

inline float round_screen_coordinate(float value) {
    return std::trunc(value * 16.0f) / 16.0f;
}

inline float clamp_away_zero_inf(float value) {
    const float minimum = std::ldexp(1.0f, -64);
    const float maximum = std::ldexp(1.0f, 64);
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    if (value > 0.0f || bits == 0) {
        return std::clamp(value, minimum, maximum);
    }
    return std::clamp(value, -maximum, -minimum);
}

inline DotOutput cpu_oracle(const DotInput& input) {
    float vendored_inputs[8]{};
    for (size_t lane = 0; lane < 4; ++lane) {
        vendored_inputs[lane] = multiply_positive_one_for_fetch(bits_to_float(input.a[lane]));
        vendored_inputs[4 + lane] = multiply_positive_one_for_fetch(bits_to_float(input.b[lane]));
    }

    float dp3_out[4]{};
    float dp4_out[4]{};
    nv2a_vsh_cpu_dp3(dp3_out, vendored_inputs);
    nv2a_vsh_cpu_dp4(dp4_out, vendored_inputs);

    const float dp3_scalar = dp3_out[0];
    const float dp4_scalar = dp4_out[0];
    DotOutput output{};
    output.words[0] = float_to_bits(dp3_scalar);
    output.words[1] = float_to_bits(dp4_scalar);
    output.words[2] = float_to_bits(round_screen_coordinate(dp4_scalar));
    output.words[3] = float_to_bits(clamp_away_zero_inf(dp4_scalar));
    return output;
}

inline std::vector<DotOutput> cpu_oracle(const std::vector<TestCase>& cases) {
    std::vector<DotOutput> outputs;
    outputs.reserve(cases.size());
    for (const auto& test_case : cases) {
        outputs.push_back(cpu_oracle(test_case.input));
    }
    return outputs;
}

inline DotInput make_input(std::array<uint32_t, 4> a,
                           std::array<uint32_t, 4> b) {
    DotInput input{};
    input.a = a;
    input.b = b;
    return input;
}

inline uint32_t next_xorshift32(uint32_t& state) {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

inline uint32_t make_random_normal_finite_bits(uint32_t& state) {
    const uint32_t mantissa = next_xorshift32(state) & 0x007FFFFFu;
    const uint32_t exponent = 1u + (next_xorshift32(state) % 254u);
    const uint32_t sign = next_xorshift32(state) & 0x80000000u;
    return sign | (exponent << 23) | mantissa;
}

inline std::vector<TestCase> make_cases(size_t random_normal_count = 128) {
    constexpr uint32_t kPositiveZero = 0x00000000u;
    constexpr uint32_t kNegativeZero = 0x80000000u;
    constexpr uint32_t kPositiveOne = 0x3F800000u;
    constexpr uint32_t kNegativeOne = 0xBF800000u;
    constexpr uint32_t kPositiveTwo = 0x40000000u;
    constexpr uint32_t kLargePositive = 0x60AD78ECu; // finite ~1e20f
    constexpr uint32_t kLargeNegative = 0xE0AD78ECu;
    constexpr uint32_t kMaxFinite = 0x7F7FFFFFu;
    constexpr uint32_t kPositiveInfinity = 0x7F800000u;
    constexpr uint32_t kNegativeInfinity = 0xFF800000u;
    constexpr uint32_t kQuietNan = 0x7FC00001u;
    constexpr uint32_t kNegativeQuietNan = 0xFFC00001u;
    constexpr uint32_t kSignalingNan = 0x7FA00001u;
    constexpr uint32_t kNegativeSignalingNan = 0xFFA00001u;
    constexpr uint32_t kSmallestSubnormal = 0x00000001u;
    constexpr uint32_t kNegativeSmallestSubnormal = 0x80000001u;
    constexpr uint32_t kLargestSubnormal = 0x007FFFFFu;

    std::vector<TestCase> cases;
    cases.reserve(16 + random_normal_count);

    cases.push_back({make_input(
                         {kPositiveZero, kNegativeZero, kPositiveOne, kNegativeOne},
                         {kPositiveOne, kPositiveOne, kPositiveOne, kPositiveOne}),
                     "signed_zero"});
    cases.push_back({make_input(
                         {kNegativeZero, kNegativeZero, kNegativeZero, kNegativeZero},
                         {kPositiveOne, kPositiveOne, kPositiveOne, kPositiveOne}),
                     "all_negative_zero"});

    cases.push_back({make_input(
                         {kLargePositive, kLargeNegative, kPositiveOne, kNegativeOne},
                         {kPositiveOne, kPositiveOne, kPositiveOne, kPositiveOne}),
                     "cancellation"});

    cases.push_back({make_input(
                         {kMaxFinite, kMaxFinite, kMaxFinite, kMaxFinite},
                         {kPositiveTwo, kNegativeOne, kPositiveOne, kPositiveTwo}),
                     "finite_overflow"});

    cases.push_back({make_input(
                         {kPositiveInfinity, kNegativeInfinity, kPositiveOne, kNegativeOne},
                         {kPositiveOne, kPositiveOne, kPositiveInfinity, kNegativeInfinity}),
                     "infinity"});

    cases.push_back({make_input(
                         {kQuietNan, kNegativeQuietNan, kSignalingNan, kNegativeSignalingNan},
                         {kPositiveOne, kPositiveOne, kPositiveOne, kPositiveOne}),
                     "nan_payloads"});

    cases.push_back({make_input(
                         {kSmallestSubnormal, kNegativeSmallestSubnormal, kLargestSubnormal, kPositiveOne},
                         {kPositiveOne, kPositiveOne, kPositiveOne, kSmallestSubnormal}),
                     "subnormal"});

    cases.push_back({make_input(
                         {0x3EAAAAABu, 0xBEAAAAABu, 0x3EAAAAABu, 0xBEAAAAABu},
                         {0x40400000u, 0x40400000u, 0xC0400000u, 0xC0400000u}),
                     "fractional_cancellation"});

    cases.push_back({make_input(
                         {0x3DCCCCCDu, 0xBDCCCCCDu, 0x3F000000u, kPositiveZero},
                         {0x41200000u, 0x41200000u, 0xC0000000u, kNegativeZero}),
                     "screen_round_and_w_zero"});

    // Frozen synthetic counterexample: all inputs are normal finite values,
    // yet native GPU underflow changed the sign of the postprocessed W.
    cases.push_back({make_input(
                         {0x85DA8C81u, 0x2674C47Cu, 0x8ED51717u, 0x9E431DECu},
                         {0x32A9D579u, 0x0C61F074u, 0x905E0A82u, 0x1075CB63u}),
                     "normal_input_w_sign"});

    uint32_t rng = 0x584C4C45u; // "XLLE"
    for (size_t i = 0; i < random_normal_count; ++i) {
        DotInput input{};
        for (size_t lane = 0; lane < 4; ++lane) {
            input.a[lane] = make_random_normal_finite_bits(rng);
            input.b[lane] = make_random_normal_finite_bits(rng);
        }
        cases.push_back({input, "random_normal_finite"});
    }

    return cases;
}

inline uint32_t negate_bits(uint32_t value) {
    return value ^ 0x80000000u;
}

inline uint32_t make_random_raw_finite_bits(uint32_t& state) {
    uint32_t bits = next_xorshift32(state);
    if (((bits >> 23) & 0xFFu) == 0xFFu) {
        bits &= 0xFF7FFFFFu; // map Inf/NaN exponent 255 to finite exponent 254
    }
    const uint32_t selector = next_xorshift32(state) & 31u;
    if (selector == 0u) {
        return bits & 0x80000000u; // signed zero
    }
    if (selector <= 6u) {
        return (bits & 0x807FFFFFu); // signed subnormal, including zero
    }
    if (((bits >> 23) & 0xFFu) == 0u) {
        bits |= 0x00800000u; // keep most random cases normal finite
    }
    return bits;
}

inline void append_rounding_case(std::vector<TestCase>& cases,
                                 std::array<uint32_t, 4> products,
                                 const std::string& category) {
    cases.push_back({make_input(products,
                                {0x3F800000u, 0x3F800000u, 0x3F800000u, 0x3F800000u}),
                     category});
}

inline void append_product_rounding_case(std::vector<TestCase>& cases,
                                         std::array<uint32_t, 4> a,
                                         std::array<uint32_t, 4> b,
                                         const std::string& category) {
    cases.push_back({make_input(a, b), category});
}

inline void append_rounding_cases(std::vector<TestCase>& cases) {
    constexpr uint32_t kPosZero = 0x00000000u;
    constexpr uint32_t kNegZero = 0x80000000u;
    constexpr uint32_t kPosOne = 0x3F800000u;
    constexpr uint32_t kNegOne = 0xBF800000u;
    constexpr uint32_t kOneOdd = 0x3F800001u;
    constexpr uint32_t kNegOneOdd = 0xBF800001u;
    constexpr uint32_t kHalfUlpAtOne = 0x33800000u; // 2^-24
    constexpr uint32_t kNegHalfUlpAtOne = 0xB3800000u;
    constexpr uint32_t kMinSub = 0x00000001u;
    constexpr uint32_t kNegMinSub = 0x80000001u;
    constexpr uint32_t kMaxSub = 0x007FFFFFu;
    constexpr uint32_t kNegMaxSub = 0x807FFFFFu;
    constexpr uint32_t kMinNormal = 0x00800000u;
    constexpr uint32_t kNegMinNormal = 0x80800000u;
    constexpr uint32_t kMaxFinite = 0x7F7FFFFFu;
    constexpr uint32_t kNegMaxFinite = 0xFF7FFFFFu;
    constexpr uint32_t kTwo = 0x40000000u;
    constexpr uint32_t kNegTwo = 0xC0000000u;
    constexpr uint32_t kPow64 = 0x5F800000u;
    constexpr uint32_t kBelowPow64 = 0x5F7FFFFFu;
    constexpr uint32_t kNegPow64 = 0xDF800000u;
    constexpr uint32_t kHalfOverflowUlp = 0x73000000u; // 2^103 at max exponent
    constexpr uint32_t kBelowHalfOverflowUlp = 0x72FFFFFFu;
    constexpr uint32_t kAboveHalfOverflowUlp = 0x73000001u;
    constexpr uint32_t kPosInf = 0x7F800000u;
    constexpr uint32_t kNegInf = 0xFF800000u;

    cases.reserve(cases.size() + 104000);

    // Directed normal add ties. Products are injected via b=1 so the DP sum
    // exercises float32 addition rounding, not product rounding.
    append_rounding_case(cases,
                         {kPosOne, kHalfUlpAtOne, kPosZero, kPosZero},
                         "round_tie_add_even_positive");
    append_rounding_case(cases,
                         {kOneOdd, kHalfUlpAtOne, kPosZero, kPosZero},
                         "round_tie_add_odd_positive");
    append_rounding_case(cases,
                         {kNegOne, kNegHalfUlpAtOne, kNegZero, kPosZero},
                         "round_tie_add_even_negative");
    append_rounding_case(cases,
                         {kNegOneOdd, kNegHalfUlpAtOne, kNegZero, kPosZero},
                         "round_tie_add_odd_negative");

    // Overflow threshold through sequential sums.
    append_rounding_case(cases,
                         {kMaxFinite, kBelowHalfOverflowUlp, kNegMaxFinite, kPosZero},
                         "sum_overflow_below_threshold");
    append_rounding_case(cases,
                         {kMaxFinite, kHalfOverflowUlp, kNegMaxFinite, kPosZero},
                         "sum_overflow_tie_threshold");
    append_rounding_case(cases,
                         {kMaxFinite, kAboveHalfOverflowUlp, kNegMaxFinite, kPosZero},
                         "sum_overflow_above_threshold");
    append_rounding_case(cases,
                         {kNegMaxFinite, negate_bits(kBelowHalfOverflowUlp), kMaxFinite, kPosZero},
                         "sum_negative_overflow_below_threshold");
    append_rounding_case(cases,
                         {kNegMaxFinite, negate_bits(kHalfOverflowUlp), kMaxFinite, kPosZero},
                         "sum_negative_overflow_tie_threshold");
    append_rounding_case(cases,
                         {kNegMaxFinite, negate_bits(kAboveHalfOverflowUlp), kMaxFinite, kPosZero},
                         "sum_negative_overflow_above_threshold");

    // Product overflow and finite-overflow clamp behavior in fix_inf_mult.
    append_product_rounding_case(cases,
                                 {kMaxFinite, kBelowPow64, kPow64, kNegPow64},
                                 {kPosOne, kBelowPow64, kPow64, kPow64},
                                 "product_overflow_grid_positive_negative");
    append_product_rounding_case(cases,
                                 {kMaxFinite, kMaxFinite, kNegMaxFinite, kNegMaxFinite},
                                 {kTwo, kNegTwo, kTwo, kNegTwo},
                                 "product_maxfinite_times_two_signs");

    // Sequential overflow followed by opposing infinity is intentionally an
    // unsupported/decline stress for the FP64 fallback status path.
    append_rounding_case(cases,
                         {kMaxFinite, kMaxFinite, kNegInf, kPosZero},
                         "decline_sequential_pos_overflow_then_neginf");
    append_rounding_case(cases,
                         {kNegMaxFinite, kNegMaxFinite, kPosInf, kNegZero},
                         "decline_sequential_neg_overflow_then_posinf");

    // Signed zero and product sign propagation.
    append_product_rounding_case(cases,
                                 {kPosZero, kNegZero, kPosZero, kNegZero},
                                 {kNegOne, kPosOne, kPosOne, kNegOne},
                                 "signed_zero_products");
    append_rounding_case(cases,
                         {kNegZero, kNegZero, kPosZero, kNegZero},
                         "signed_zero_sum_chain");

    // Normal/subnormal boundaries and broad subnormal products.
    append_rounding_case(cases,
                         {kMaxSub, kMinSub, kNegMinSub, kMinNormal},
                         "subnormal_to_normal_sum_boundary");
    append_rounding_case(cases,
                         {kNegMaxSub, kNegMinSub, kMinSub, kNegMinNormal},
                         "negative_subnormal_to_normal_sum_boundary");
    append_product_rounding_case(cases,
                                 {kMinNormal, kMinNormal, kMaxSub, kNegMaxSub},
                                 {kMinNormal, kNegMinNormal, kMaxSub, kMaxSub},
                                 "broad_subnormal_products");
    append_product_rounding_case(cases,
                                 {0x01000000u, 0x01800000u, 0x02000000u, 0x02800000u},
                                 {0x01000000u, 0x01800000u, 0x82000000u, 0x82800000u},
                                 "normal_products_underflow_to_subnormal_or_zero");

    // Cancellation and screen/W postprocess boundaries.
    append_product_rounding_case(cases,
                                 {0x3F800001u, kNegOne, kPosZero, kPosZero},
                                 {0x3F7FFFFEu, kPosOne, kPosOne, kPosOne},
                                 "product_must_round_before_cancellation");
    append_product_rounding_case(cases,
                                 {0x3F800001u, kPosZero, kPosZero, kPosZero},
                                 {0x3FC00000u, kPosOne, kPosOne, kPosOne},
                                 "product_normal_halfway_odd");
    append_product_rounding_case(cases,
                                 {kMinSub, kNegZero, kNegZero, kNegZero},
                                 {0x3F000000u, kPosOne, kPosOne, kPosOne},
                                 "product_half_min_subnormal_even_zero");
    append_product_rounding_case(cases,
                                 {0x00000003u, kPosZero, kPosZero, kPosZero},
                                 {0x3F000000u, kPosOne, kPosOne, kPosOne},
                                 "product_subnormal_halfway_odd");
    append_product_rounding_case(cases,
                                 {kMinNormal, kPosZero, kPosZero, kPosZero},
                                 {0x3F7FFFFFu, kPosOne, kPosOne, kPosOne},
                                 "product_halfway_at_min_normal");
    append_rounding_case(cases, {kPosInf, kNegOne, kPosZero, kPosZero},
                         "valid_infinity_clamped");
    append_product_rounding_case(cases,
                                 {kPosInf, kPosZero, kPosZero, kPosZero},
                                 {kNegZero, kPosOne, kPosOne, kPosOne},
                                 "decline_infinity_times_zero");
    append_rounding_case(cases,
                         {0x4B7FFFFFu, 0xCB7FFFFFu, kHalfUlpAtOne, kNegHalfUlpAtOne},
                         "large_exact_cancellation_then_tiny_terms");
    append_rounding_case(cases,
                         {0x3D7FFFFFu, kMinSub, kNegZero, kPosZero},
                         "screen_trunc_below_one_sixteenth");
    append_rounding_case(cases,
                         {0x3D800000u, kNegZero, kPosZero, kPosZero},
                         "screen_trunc_exact_one_sixteenth");
    append_rounding_case(cases,
                         {0xBD7FFFFFu, kNegZero, kPosZero, kPosZero},
                         "screen_trunc_negative_below_one_sixteenth");
    append_rounding_case(cases,
                         {0x1F7FFFFFu, kNegZero, kPosZero, kPosZero},
                         "w_clamp_just_below_min");
    append_rounding_case(cases,
                         {0x1F800000u, kNegZero, kPosZero, kPosZero},
                         "w_clamp_exact_min");
    append_rounding_case(cases,
                         {0x5F800001u, kNegZero, kPosZero, kPosZero},
                         "w_clamp_above_max");
    append_rounding_case(cases,
                         {0xDF800001u, kNegZero, kPosZero, kPosZero},
                         "w_clamp_negative_above_max_magnitude");

    const std::array<uint32_t, 36> edge_values = {
        kPosZero, kNegZero, kMinSub, kNegMinSub, 0x00000002u, 0x80000002u,
        0x00000100u, 0x80000100u, kMaxSub, kNegMaxSub, kMinNormal, kNegMinNormal,
        0x00800001u, 0x80800001u, 0x00FFFFFFu, 0x80FFFFFFu, kHalfUlpAtOne,
        kNegHalfUlpAtOne, kPosOne, kNegOne, kOneOdd, kNegOneOdd, kTwo, kNegTwo,
        0x3F7FFFFFu, 0xBF7FFFFFu, 0x3F000000u, 0xBF000000u, kBelowHalfOverflowUlp,
        negate_bits(kBelowHalfOverflowUlp), kHalfOverflowUlp, negate_bits(kHalfOverflowUlp),
        kAboveHalfOverflowUlp, negate_bits(kAboveHalfOverflowUlp), kMaxFinite, kNegMaxFinite};

    for (size_t i = 0; i < edge_values.size(); ++i) {
        for (size_t j = 0; j < edge_values.size(); ++j) {
            const uint32_t a = edge_values[i];
            const uint32_t b = edge_values[j];
            append_product_rounding_case(cases,
                                         {a, b, negate_bits(a), negate_bits(b)},
                                         {kPosOne, kPosOne, kNegOne, kNegOne},
                                         "structured_edge_sum_grid");
            append_product_rounding_case(cases,
                                         {a, b, edge_values[(i + j) % edge_values.size()],
                                          edge_values[(i * 7 + j * 11) % edge_values.size()]},
                                         {b, a, kPosOne, kNegOne},
                                         "structured_edge_product_grid");
        }
    }

    uint32_t rng = 0x4450524Eu; // "DPRN"
    constexpr size_t kRandomRawFiniteCases = 100000;
    for (size_t i = 0; i < kRandomRawFiniteCases; ++i) {
        DotInput input{};
        for (size_t lane = 0; lane < 4; ++lane) {
            input.a[lane] = make_random_raw_finite_bits(rng);
            input.b[lane] = make_random_raw_finite_bits(rng);
        }
        cases.push_back({input, "random_raw_finite_with_zero_subnormal"});
    }
}


inline void append_native_fp32_boundary_cases(std::vector<TestCase>& cases) {
    constexpr uint32_t one = 0x3f800000u;
    append_rounding_case(cases, {0x0c000001u, 0x8c000000u, 0u, 0u},
                         "native_fp32_min_normal_cancellation");
    append_rounding_case(cases, {0x0b800001u, 0x8b800000u, 0u, 0u},
                         "native_fp32_subnormal_cancellation_fallback");
    append_product_rounding_case(cases, {0x20000000u, 0u, 0u, 0u},
                                 {0x20000000u, one, one, one},
                                 "native_fp32_min_normal_product");
    append_product_rounding_case(cases, {0x20000000u, 0u, 0u, 0u},
                                 {0x1f800000u, one, one, one},
                                 "native_fp32_subnormal_product_fallback");
    // Straddle both bounds of each arithmetic admission predicate, including
    // cancellation between adjacent mantissas and every sign combination.
    constexpr std::array<uint32_t, 17> exponents{
        1, 23, 24, 25, 63, 64, 65, 126, 127, 128, 188, 189, 190, 251, 252, 253, 254};
    constexpr std::array<uint32_t, 3> mantissas{0, 1, 0x7fffff};
    for (uint32_t ea : exponents) for (uint32_t eb : exponents)
        for (uint32_t ma : mantissas) for (uint32_t mb : mantissas)
            for (uint32_t signs = 0; signs < 4; ++signs) {
                const uint32_t a = (ea << 23) | ma | ((signs & 1u) << 31);
                const uint32_t b = (eb << 23) | mb | ((signs & 2u) << 30);
                append_product_rounding_case(cases, {a, 0u, 0u, 0u}, {b, one, one, one},
                                             "native_fp32_product_bounds");
                append_rounding_case(cases, {a, b, 0u, 0u}, "native_fp32_add_bounds");
            }
}

}  // namespace xlle::vsh_shadow_probe
