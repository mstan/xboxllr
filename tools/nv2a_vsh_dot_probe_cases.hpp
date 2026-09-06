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

}  // namespace xlle::vsh_shadow_probe
