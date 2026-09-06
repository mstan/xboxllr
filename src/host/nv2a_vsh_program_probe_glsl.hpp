#pragma once
#include "host/nv2a_vsh_dot_probe_fp64_glsl.hpp"

namespace xlle::vsh_shadow_probe {

// Full decoded-program diagnostic. No program identity or guest memory access.
// Input/cache ownership stays on the CPU; one invocation is one actual execution.
inline const std::string kProgramComputeSource = std::string(R"GLSL(
#version 430
layout(local_size_x = 64) in;
struct Vertex { uvec4 inputs[16]; uvec4 constants[192]; uvec4 meta; };
struct Op { uvec4 header; uvec4 bases; uvec4 offsets; uvec4 swizzles;
            uvec4 destinations; uvec4 masks; };
struct Output { uvec4 values[8]; };
layout(std430, binding = 0) readonly buffer Inputs { Vertex vertices[]; };
layout(std430, binding = 1) readonly buffer Programs { Op operations[]; };
layout(std430, binding = 2) writeonly buffer Outputs { Output outputs[]; };
layout(std430, binding = 3) writeonly buffer Status { uint statuses[]; };
uniform uint u_record_count;
uvec4 temps[12];
uvec4 regs[13];
)GLSL") + std::string(kFp32ArithmeticSource) + R"GLSL(

uint reciprocal32(uint x, inout bool supported) {
    uint sign = x & 0x80000000u;
    uint mag = x & 0x7fffffffu;
    if (nan32(x)) { supported = false; return 0u; }
    if (mag == 0u) return sign | 0x7f800000u;
    if (inf32(x)) return sign;
    precise double result = 1.0lf / decode32(x);
    return round32(result, sign);
}

uint rcc32(uint x, inout bool supported) {
    uint result = reciprocal32(x, supported);
    uint sign = result & 0x80000000u;
    uint mag = result & 0x7fffffffu;
    // Pinned CPU literals are deliberately NOT the W-clamp constants.
    // Its result > 0 branch sends BOTH signed zeros through the negative branch.
    if (mag == 0u) return 0x9f7ffffdu;
    if (mag < 0x1f7ffffdu) return sign | 0x1f7ffffdu;
    if (mag > 0x5f82c2dcu) return sign | 0x5f800000u;
    return result;
}

uint rsq32(uint x, inout bool supported) {
    x &= 0x7fffffffu;
    if (nan32(x)) { supported = false; return 0u; }
    if (x == 0u) return 0x7f800000u;
    if (inf32(x)) return 0u;
    // CPU floor rounds sqrtf before its separate float32 division.
    precise double root = sqrt(decode32(x));
    return reciprocal32(round32(root, 0u), supported);
}

uint order_key(uint x) { return (x & 0x80000000u) != 0u ? ~x : x ^ 0x80000000u; }
bool less32(uint a, uint b) {
    if ((a & 0x7fffffffu) == 0u && (b & 0x7fffffffu) == 0u) return false;
    return order_key(a) < order_key(b);
}

uvec4 fetch_operand(Op op, uint operand, uint vertex, inout bool supported) {
    uint base = op.bases[operand];
    uint reg = op.offsets[operand] / 4u;
    uvec4 value;
    if (base == 0u) value = temps[reg];
    else if (base == 1u) value = regs[reg];
    else if (base == 2u) value = vertices[vertex].inputs[reg];
    else value = vertices[vertex].constants[reg];
    uvec4 result;
    for (uint lane = 0u; lane < 4u; ++lane) {
        uint word = value[(op.swizzles[operand] >> (2u * lane)) & 3u];
        if (nan32(word)) supported = false;
        result[lane] = word ^ (((op.header.w >> operand) & 1u) << 31);
    }
    return result;
}

uvec4 execute_op(Op op, uvec4 a, uvec4 b, uvec4 c, inout bool supported) {
    uint code = op.header.x;
    if (code == 1u) return a;
    if (code == 5u || code == 6u || code == 7u) {
        uint p0 = multiply_fix32(a.x, b.x, supported);
        uint p1 = multiply_fix32(a.y, b.y, supported);
        uint p2 = multiply_fix32(a.z, b.z, supported);
        uint sum = add32(p0, p1, supported);
        sum = add32(sum, p2, supported);
        if (code == 6u) sum = add32(sum, b.w, supported);
        if (code == 7u) {
            uint p3 = multiply_fix32(a.w, b.w, supported);
            sum = add32(sum, p3, supported);
        }
        return uvec4(fix32(sum));
    }
    if (code == 14u) return uvec4(reciprocal32(a.x, supported));
    if (code == 15u) return uvec4(rcc32(a.x, supported));
    if (code == 16u) return uvec4(rsq32(a.x, supported));
    if (code == 8u) return uvec4(0x3f800000u, multiply_fix32(a.y, b.y, supported), a.z, b.w);
    uvec4 result;
    for (uint lane = 0u; lane < 4u; ++lane) {
        if (code == 2u) result[lane] = multiply_fix32(a[lane], b[lane], supported);
        else if (code == 3u) result[lane] = add32(a[lane], b[lane], supported);
        else if (code == 4u) {
            uint product = multiply_fix32(a[lane], b[lane], supported);
            result[lane] = add32(product, c[lane], supported);
        }
        else if (code == 9u) result[lane] = less32(a[lane], b[lane]) ? a[lane] : b[lane];
        else if (code == 10u) result[lane] = less32(b[lane], a[lane]) ? a[lane] : b[lane];
        else {
            // SLT/SGE additionally order negative zero below positive zero.
            bool lt = order_key(a[lane]) < order_key(b[lane]);
            result[lane] = (code == 11u ? lt : !lt) ? 0x3f800000u : 0u;
        }
    }
    return result;
}

void store_result(Op op, uvec4 value) {
    for (uint dest = 0u; dest < 2u; ++dest) {
        uint base = op.destinations[dest];
        if (base == 255u) continue;
        uint reg = op.destinations[dest + 2u] / 4u;
        for (uint lane = 0u; lane < 4u; ++lane) {
            if ((op.masks[dest] & (8u >> lane)) == 0u) continue;
            if (base == 0u) temps[reg][lane] = value[lane];
            else regs[reg][lane] = value[lane];
        }
    }
}

void main() {
    uint index = gl_GlobalInvocationID.x;
    if (index >= u_record_count) return;
    uint begin = vertices[index].meta.x;
    uint count = vertices[index].meta.y;
    // Full-program admission precedes execution. Host separately validates all
    // offsets, lengths, arities and pairing before submitting any SSBO data.
    for (uint i = 0u; i < count; ++i) {
        uint code = operations[begin + i].header.x;
        if (code == 13u || code >= 17u) { statuses[index] = 2u; return; }
    }
    for (uint i = 0u; i < 12u; ++i) temps[i] = uvec4(0u);
    for (uint i = 0u; i < 13u; ++i) regs[i] = uvec4(0u);
    bool supported = true;
    for (uint i = 0u; i < count; ++i) {
        Op op = operations[begin + i];
        uvec4 args[3];
        for (uint k = 0u; k < 3u; ++k) args[k] = uvec4(0u);
        for (uint k = 0u; k < op.header.z; ++k) args[k] = fetch_operand(op, k, index, supported);
        uvec4 ilu_arg = uvec4(0u);
        if (op.header.y != 0u) ilu_arg = fetch_operand(operations[begin + i + 1u], 0u, index, supported);
        uvec4 value = execute_op(op, args[0], args[1], args[2], supported);
        store_result(op, value);
        if (op.header.y != 0u) {
            ++i;
            Op ilu = operations[begin + i];
            value = execute_op(ilu, ilu_arg, uvec4(0u), uvec4(0u), supported);
            store_result(ilu, value);
        }
        if (!supported) { statuses[index] = 1u; return; }
    }
    const uint consumed[7] = uint[7](0u, 3u, 4u, 9u, 10u, 11u, 12u);
    uvec4 position = regs[0];
    position.x = screen32(position.x, supported);
    position.y = screen32(position.y, supported);
    position.w = w32(position.w);
    if (!supported) { statuses[index] = 1u; return; }
    for (uint i = 0u; i < 7u; ++i) outputs[index].values[i] = regs[consumed[i]];
    outputs[index].values[7] = position;
    statuses[index] = 0u;
}
)GLSL";

} // namespace xlle::vsh_shadow_probe
