#pragma once

#include <string_view>

namespace xlle::vsh_shadow_probe {

// Diagnostic GLSL 4.30 compute shader for DP3/DP4 numeric differential
// probing. This shader intentionally does not use native dot(), FMA, or guest
// program identity. It mirrors the vendored nv2a_vsh_cpu.c DP3/DP4 helper shape:
// operand fetch +1 multiplication, per-product finite-overflow clamp, sequential
// left-associated sum, final infinity clamp. The explicit bit operations and
// precise intermediates reduce accidental reassociation, but they do not prove
// IEEE, denormal, NaN payload, or signaling-NaN equivalence across drivers.
//
// Input ABI: one std430 record per invocation, 32 bytes each:
//   a: uvec4 raw IEEE-754 bit patterns
//   b: uvec4 raw IEEE-754 bit patterns
// Output ABI: one std430 uvec4 per invocation, 16 bytes each:
//   x = raw DP3 scalar result bits (CPU result is broadcast)
//   y = raw DP4 scalar result bits (CPU result is broadcast)
//   z = round_screen_coordinate(DP4 scalar) bits
//   w = clamp_away_zero_inf(DP4 scalar) bits
inline constexpr std::string_view kComputeSource = R"GLSL(
#version 430

layout(local_size_x = 64) in;

struct DotInput {
    uvec4 a;
    uvec4 b;
};

layout(std430, binding = 0) readonly buffer DotInputs {
    DotInput records[];
};

layout(std430, binding = 1) writeonly buffer DotOutputs {
    uvec4 results[];
};

uniform uint u_record_count;

float multiply_positive_one(float value) {
    precise float one = uintBitsToFloat(0x3F800000u);
    precise float result = value * one;
    return result;
}

float fix_inf(float value) {
    if (!isinf(value)) {
        return value;
    }
    uint bits = floatBitsToUint(value);
    uint fixed_bits = (bits & 0xFF000000u) + 0x007FFFFFu;
    return uintBitsToFloat(fixed_bits);
}

float fix_inf_mult(float a, float b) {
    precise float product = a * b;
    if (!isinf(product) || isinf(a) || isinf(b)) {
        return product;
    }
    uint bits = floatBitsToUint(product);
    uint fixed_bits = (bits & 0xFF000000u) + 0x007FFFFFu;
    return uintBitsToFloat(fixed_bits);
}

float nv2a_dp3(vec4 a, vec4 b) {
    precise float p0 = fix_inf_mult(a.x, b.x);
    precise float p1 = fix_inf_mult(a.y, b.y);
    precise float p2 = fix_inf_mult(a.z, b.z);
    precise float s01 = p0 + p1;
    precise float sum = s01 + p2;
    return fix_inf(sum);
}

float nv2a_dp4(vec4 a, vec4 b) {
    precise float p0 = fix_inf_mult(a.x, b.x);
    precise float p1 = fix_inf_mult(a.y, b.y);
    precise float p2 = fix_inf_mult(a.z, b.z);
    precise float p3 = fix_inf_mult(a.w, b.w);
    precise float s01 = p0 + p1;
    precise float s012 = s01 + p2;
    precise float sum = s012 + p3;
    return fix_inf(sum);
}

float round_screen_coordinate(float value) {
    precise float scaled = value * 16.0;
    precise float rounded = trunc(scaled) / 16.0;
    return rounded;
}

float clamp_away_zero_inf(float value) {
    const float minimum = 5.42101086242752217003726400434970855712890625e-20; // 2^-64
    const float maximum = 18446744073709551616.0; // 2^64
    uint bits = floatBitsToUint(value);
    if (value > 0.0 || bits == 0u) {
        if (value < minimum) {
            return minimum;
        }
        if (value > maximum) {
            return maximum;
        }
        return value;
    }
    if (value < -maximum) {
        return -maximum;
    }
    if (value > -minimum) {
        return -minimum;
    }
    return value;
}

void main() {
    uint index = gl_GlobalInvocationID.x;
    if (index >= u_record_count) {
        return;
    }

    DotInput record = records[index];
    vec4 a = vec4(
        multiply_positive_one(uintBitsToFloat(record.a.x)),
        multiply_positive_one(uintBitsToFloat(record.a.y)),
        multiply_positive_one(uintBitsToFloat(record.a.z)),
        multiply_positive_one(uintBitsToFloat(record.a.w)));
    vec4 b = vec4(
        multiply_positive_one(uintBitsToFloat(record.b.x)),
        multiply_positive_one(uintBitsToFloat(record.b.y)),
        multiply_positive_one(uintBitsToFloat(record.b.z)),
        multiply_positive_one(uintBitsToFloat(record.b.w)));

    precise float dp3_result = nv2a_dp3(a, b);
    precise float dp4_result = nv2a_dp4(a, b);
    precise float screen = round_screen_coordinate(dp4_result);
    precise float w_clamped = clamp_away_zero_inf(dp4_result);

    results[index] = uvec4(
        floatBitsToUint(dp3_result),
        floatBitsToUint(dp4_result),
        floatBitsToUint(screen),
        floatBitsToUint(w_clamped));
}
)GLSL";

}  // namespace xlle::vsh_shadow_probe
