#pragma once

#include <string_view>
#include <string>

namespace xlle::vsh_shadow_probe {

// Diagnostic only. Keep binary32 intermediates as integers, so GPU float FTZ
// cannot erase subnormals. Binary64 arithmetic is tested on the actual device;
// GLSL 4.30 does not promise full IEEE binary64 operation accuracy everywhere.
// NaN operands and invalid operations explicitly decline the entire record.
inline constexpr std::string_view kFp32ArithmeticSource = R"GLSL(
bool nan32(uint x) { return (x & 0x7fffffffu) > 0x7f800000u; }
bool inf32(uint x) { return (x & 0x7fffffffu) == 0x7f800000u; }

double decode32(uint x) {
    uint e = (x >> 23) & 255u;
    uint m = x & 0x7fffffu;
    // All nonzero binary32 values become normal binary64 values.
    precise double value = e == 0u ? ldexp(double(m), -149)
        : ldexp(double(m | 0x800000u), int(e) - 150);
    return (x & 0x80000000u) != 0u ? -value : value;
}

uint round_even_integer(double x) {
    double base = floor(x);
    uint n = uint(base); // caller scales to at most 24 significant bits
    double tail = x - base;
    return n + uint(tail > 0.5lf || (tail == 0.5lf && (n & 1u) != 0u));
}

uint round32(double value, uint zero_sign) {
    if (value == 0.0lf) return zero_sign;
    uint sign = unpackDouble2x32(value).y & 0x80000000u;
    double magnitude = abs(value);
    int e = int((unpackDouble2x32(magnitude).y >> 20) & 2047u) - 1023;
    if (e < -126) {
        // Includes rounding up to the smallest normal and down to signed zero.
        return sign | round_even_integer(ldexp(magnitude, 149));
    }
    if (e > 127) return sign | 0x7f800000u;
    uint m = round_even_integer(ldexp(magnitude, 23 - e));
    if (m == 0x1000000u) { m >>= 1; ++e; }
    if (e > 127) return sign | 0x7f800000u;
    return sign | (uint(e + 127) << 23) | (m & 0x7fffffu);
}

uint multiply32(uint a, uint b, inout bool supported) {
    uint sign = (a ^ b) & 0x80000000u;
    if (nan32(a) || nan32(b) ||
        (inf32(a) && (b & 0x7fffffffu) == 0u) ||
        (inf32(b) && (a & 0x7fffffffu) == 0u)) {
        supported = false;
        return 0u;
    }
    if (inf32(a) || inf32(b)) return sign | 0x7f800000u;
    precise double product = decode32(a) * decode32(b);
    return round32(product, sign);
}

uint multiply_fix32(uint a, uint b, inout bool supported) {
    uint p = multiply32(a, b, supported);
    if (inf32(p) && !inf32(a) && !inf32(b))
        return (p & 0x80000000u) | 0x7f7fffffu;
    return p;
}

uint add32(uint a, uint b, inout bool supported) {
    if (nan32(a) || nan32(b) || (inf32(a) && inf32(b) && a != b)) {
        supported = false;
        return 0u;
    }
    if (inf32(a)) return a;
    if (inf32(b)) return b;
    precise double sum = decode32(a) + decode32(b);
    // Under RN-even, exact cancellation is +0; only -0 + -0 is -0.
    uint zero_sign = a == 0x80000000u && b == 0x80000000u ? 0x80000000u : 0u;
    return round32(sum, zero_sign);
}

uint fix32(uint x) {
    return inf32(x) ? (x & 0x80000000u) | 0x7f7fffffu : x;
}

uint screen32(uint value, inout bool supported) {
    uint scaled = multiply32(value, 0x41800000u, supported); // float32 * 16
    if (inf32(scaled)) return scaled;
    uint sign = scaled & 0x80000000u;
    int e = int((scaled >> 23) & 255u) - 127;
    if (e < 0) scaled = sign;
    else if (e < 23) scaled &= ~((1u << uint(23 - e)) - 1u);
    return multiply32(scaled, 0x3d800000u, supported); // /16, exact power of two
}

uint w32(uint x) {
    uint magnitude = x & 0x7fffffffu;
    return (x & 0x80000000u) | clamp(magnitude, 0x1f800000u, 0x5f800000u);
}
)GLSL";

inline const std::string kFp64ComputeSource = std::string(R"GLSL(
#version 430
layout(local_size_x = 64) in;
struct DotInput { uvec4 a; uvec4 b; };
layout(std430, binding = 0) readonly buffer DotInputs { DotInput records[]; };
layout(std430, binding = 1) writeonly buffer DotOutputs { uvec4 results[]; };
layout(std430, binding = 2) writeonly buffer DotStatus { uint status[]; };
uniform uint u_record_count;
)GLSL") + std::string(kFp32ArithmeticSource) + R"GLSL(

void main() {
    uint index = gl_GlobalInvocationID.x;
    if (index >= u_record_count) return;
    DotInput input_value = records[index];
    bool supported = true;
    uvec4 products;
    for (int lane = 0; lane < 4; ++lane) {
        // +1 operand fetch is an identity for non-NaN binary32 inputs.
        products[lane] = multiply_fix32(input_value.a[lane], input_value.b[lane], supported);
    }
    uint sum01 = add32(products.x, products.y, supported);
    uint sum012 = add32(sum01, products.z, supported);
    uint sum0123 = add32(sum012, products.w, supported);
    uint dp3 = fix32(sum012);
    uint dp4 = fix32(sum0123);
    uint screen = screen32(dp4, supported);
    status[index] = supported ? 0u : 1u;
    if (!supported) return; // leave output sentinel intact, not an accepted value
    results[index] = uvec4(dp3, dp4, screen, w32(dp4));
}
)GLSL";

} // namespace xlle::vsh_shadow_probe
