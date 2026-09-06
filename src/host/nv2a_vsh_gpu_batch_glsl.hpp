#pragma once

#include "host/nv2a_vsh_dot_probe_fp64_glsl.hpp"

#include <string>

namespace xlle::vsh_gpu_batch {

// Shared decoded VSH execution helper. The including shader must define:
//   VSH_LOAD_INPUT(execution, reg)
//   VSH_LOAD_CONSTANT(execution, reg)
// before appending this source, and may undefine them afterwards. This keeps the
// arithmetic/store logic single-sourced without runtime string substitutions.
inline const std::string kVshExecutionCommonSource = std::string(xlle::vsh_shadow_probe::kFp32ArithmeticSource) + R"GLSL(

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
    precise double root = sqrt(decode32(x));
    return reciprocal32(round32(root, 0u), supported);
}

uint order_key(uint x) { return (x & 0x80000000u) != 0u ? ~x : x ^ 0x80000000u; }
bool less32(uint a, uint b) {
    if ((a & 0x7fffffffu) == 0u && (b & 0x7fffffffu) == 0u) return false;
    return order_key(a) < order_key(b);
}

uvec4 fetch_operand(Op op, uint operand, uint execution, inout bool supported) {
    uint base = op.bases[operand];
    uint reg = op.offsets[operand] / 4u;
    uvec4 value;
    if (base == 0u) value = temps[reg];
    else if (base == 1u) value = regs[reg];
    else if (base == 2u) value = VSH_LOAD_INPUT(execution, reg);
    else value = VSH_LOAD_CONSTANT(execution, reg);
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
)GLSL";

inline const std::string kVshBatchComputeSource = std::string(R"GLSL(
#version 430
layout(local_size_x = 64) in;
struct InputRecord { uvec4 inputs[16]; };
struct Op { uvec4 header; uvec4 bases; uvec4 offsets; uvec4 swizzles;
            uvec4 destinations; uvec4 masks; };
struct ProgramOutput { uvec4 values[8]; };
layout(std430, binding = 0) readonly buffer ExecutionInputs { InputRecord execution_inputs[]; };
layout(std430, binding = 1) readonly buffer ProgramOps { Op operations[]; };
layout(std430, binding = 2) writeonly buffer ProgramOutputs { ProgramOutput outputs[]; };
layout(std430, binding = 3) writeonly buffer ProgramStatuses { uint program_status[]; };
layout(std430, binding = 4) readonly buffer ProgramConstants { uvec4 constants[]; };
uniform uint u_execution_count;
uniform uint u_op_count;
uvec4 temps[12];
uvec4 regs[13];
#define VSH_LOAD_INPUT(execution, reg) execution_inputs[execution].inputs[reg]
#define VSH_LOAD_CONSTANT(execution, reg) constants[reg]
)GLSL") + kVshExecutionCommonSource + R"GLSL(
#undef VSH_LOAD_INPUT
#undef VSH_LOAD_CONSTANT

void main() {
    uint index = gl_GlobalInvocationID.x;
    if (index >= u_execution_count) return;
    if (u_op_count == 0u) { program_status[index] = 2u; return; }
    for (uint i = 0u; i < u_op_count; ++i) {
        uint code = operations[i].header.x;
        if (code == 13u || code >= 17u) { program_status[index] = 2u; return; }
    }
    for (uint i = 0u; i < 12u; ++i) temps[i] = uvec4(0u);
    for (uint i = 0u; i < 13u; ++i) regs[i] = uvec4(0u);
    bool supported = true;
    for (uint i = 0u; i < u_op_count; ++i) {
        Op op = operations[i];
        uvec4 args[3];
        for (uint k = 0u; k < 3u; ++k) args[k] = uvec4(0u);
        for (uint k = 0u; k < op.header.z; ++k) args[k] = fetch_operand(op, k, index, supported);
        uvec4 ilu_arg = uvec4(0u);
        if (op.header.y != 0u) {
            if (i + 1u >= u_op_count) { program_status[index] = 2u; return; }
            ilu_arg = fetch_operand(operations[i + 1u], 0u, index, supported);
        }
        uvec4 value = execute_op(op, args[0], args[1], args[2], supported);
        store_result(op, value);
        if (op.header.y != 0u) {
            ++i;
            Op ilu = operations[i];
            value = execute_op(ilu, ilu_arg, uvec4(0u), uvec4(0u), supported);
            store_result(ilu, value);
        }
        if (!supported) { program_status[index] = 1u; return; }
    }
    const uint consumed[7] = uint[7](0u, 3u, 4u, 9u, 10u, 11u, 12u);
    uvec4 position = regs[0];
    position.x = screen32(position.x, supported);
    position.y = screen32(position.y, supported);
    position.w = w32(position.w);
    if (!supported) { program_status[index] = 1u; return; }
    for (uint i = 0u; i < 7u; ++i) outputs[index].values[i] = regs[consumed[i]];
    outputs[index].values[7] = position;
    program_status[index] = 0u;
}
)GLSL";

// Serial ordered pack for the first renderer fidelity bridge. Accepted triangles
// are compacted in source order; empty/culled/degenerate candidates are omitted
// without forcing fallback. control.y carries fallback bits only for unsupported
// program/numeric status, geometry uncertainty, invalid shape, or output overflow.
inline const std::string kResidentPrimaryPackComputeSource = std::string(R"GLSL(
#version 430
layout(local_size_x = 1) in;
struct ProgramOutput { uvec4 values[8]; };
struct ResidentVertex { uvec4 position; uvec4 diffuse; uvec4 texture[4]; };
layout(std430, binding = 2) readonly buffer ProgramOutputs { ProgramOutput vertices[]; };
layout(std430, binding = 3) readonly buffer ProgramStatuses { uint program_status[]; };
layout(std430, binding = 4) readonly buffer References { uint refs[]; };
layout(std430, binding = 5) writeonly buffer TriangleCandidates { uvec4 candidates[]; };
layout(std430, binding = 6) writeonly buffer ResidentVertices { ResidentVertex resident_vertices[]; };
layout(std430, binding = 7) coherent buffer BatchControl { uvec4 control; };
uniform uint u_reference_count;
uniform uint u_execution_count;
uniform uint u_primitive;
uniform uint u_large_target;
uniform uint u_cull_enabled;
uniform uint u_cull_front;
uniform uint u_max_resident_vertices;
)GLSL") + std::string(xlle::vsh_shadow_probe::kFp32ArithmeticSource) + R"GLSL(

const uint BATCH_PROGRAM_DECLINE = 1u;
const uint BATCH_GEOMETRY_DECLINE = 2u;
const uint BATCH_OUTPUT_OVERFLOW = 4u;
const uint BATCH_INVALID_SHAPE = 8u;
const uint TRI_EMPTY_OR_CULLED = 0u;
const uint TRI_ACCEPTED = 1u;
const uint TRI_GEOMETRY_DECLINE = 2u;
const uint TRI_PROGRAM_DECLINE = 3u;
const uint TRI_OUTPUT_OVERFLOW = 4u;

bool finite4(uvec4 v) { return all(lessThan(v & uvec4(0x7fffffffu), uvec4(0x7f800000u))); }

bool valid_vertex(uint i) {
    uvec4 p = vertices[i].values[7];
    return finite4(p) && ((p.w & 0x7fffffffu) != 0u) && finite4(vertices[i].values[1]);
}

dvec2 xy(uint i) {
    uvec4 p = vertices[i].values[7];
    return dvec2(decode32(p.x), decode32(p.y));
}

dvec2 fixed_xy(dvec2 v) { return sign(v) * floor(abs(v * 16.0) + 0.5); }

void write_candidate(uint tid, uvec3 ids, uint status) {
    candidates[tid] = uvec4(ids, status);
}

void write_resident_vertex(uint dst, uint source) {
    resident_vertices[dst].position = vertices[source].values[7];
    resident_vertices[dst].diffuse = vertices[source].values[1];
    resident_vertices[dst].texture[0] = vertices[source].values[3];
    resident_vertices[dst].texture[1] = vertices[source].values[4];
    resident_vertices[dst].texture[2] = vertices[source].values[5];
    resident_vertices[dst].texture[3] = vertices[source].values[6];
}

uint candidate_count() {
    if (u_primitive == 5u) return u_reference_count / 3u;
    if (u_primitive == 6u || u_primitive == 7u) return u_reference_count >= 3u ? u_reference_count - 2u : 0u;
    return 0u;
}

uvec3 submitted_slots(uint tid) {
    if (u_primitive == 5u) return uvec3(tid * 3u, tid * 3u + 1u, tid * 3u + 2u);
    if (u_primitive == 6u) return (tid & 1u) == 0u ?
        uvec3(tid, tid + 1u, tid + 2u) : uvec3(tid + 1u, tid, tid + 2u);
    return uvec3(0u, tid + 1u, tid + 2u);
}

uint evaluate_triangle(uint tid, out uvec3 ids) {
    ids = uvec3(0u);
    if (u_primitive != 5u && u_primitive != 6u && u_primitive != 7u) return TRI_GEOMETRY_DECLINE;
    uvec3 source = submitted_slots(tid);
    ids = uvec3(refs[source.x], refs[source.y], refs[source.z]);
    if (ids.x >= u_execution_count || ids.y >= u_execution_count || ids.z >= u_execution_count)
        return TRI_GEOMETRY_DECLINE;
    if ((program_status[ids.x] | program_status[ids.y] | program_status[ids.z]) != 0u)
        return TRI_PROGRAM_DECLINE;
    if (!valid_vertex(ids.x) || !valid_vertex(ids.y) || !valid_vertex(ids.z))
        return TRI_GEOMETRY_DECLINE;
    bvec3 behind = bvec3((vertices[ids.x].values[7].w & 0x80000000u) != 0u,
        (vertices[ids.y].values[7].w & 0x80000000u) != 0u,
        (vertices[ids.z].values[7].w & 0x80000000u) != 0u);
    if (all(behind)) return TRI_EMPTY_OR_CULLED;
    if (any(behind)) return TRI_GEOMETRY_DECLINE;
    precise dvec2 a = xy(ids.x), b = xy(ids.y), c = xy(ids.z);
    if (u_large_target != 0u) {
        // On this lattice the area numerator is below 2^43, so both the CPU
        // and GPU double products/subtraction are exact, including sign/zero.
        // Values outside the proven domain use whole-batch CPU fallback.
        if (any(greaterThan(abs(a), dvec2(62500.0))) ||
            any(greaterThan(abs(b), dvec2(62500.0))) ||
            any(greaterThan(abs(c), dvec2(62500.0))) ||
            any(notEqual(a * 16.0, trunc(a * 16.0))) ||
            any(notEqual(b * 16.0, trunc(b * 16.0))) ||
            any(notEqual(c * 16.0, trunc(c * 16.0))))
            return TRI_GEOMETRY_DECLINE;
    }
    if (u_large_target == 0u) {
        a = fixed_xy(a); b = fixed_xy(b); c = fixed_xy(c);
        if (any(greaterThan(abs(a), dvec2(1000000.0))) ||
            any(greaterThan(abs(b), dvec2(1000000.0))) ||
            any(greaterThan(abs(c), dvec2(1000000.0))))
            return TRI_GEOMETRY_DECLINE;
    }
    precise double p = (b.x - a.x) * (c.y - a.y);
    precise double q = (b.y - a.y) * (c.x - a.x);
    precise double area = p - q;
    if (area == 0.0) return TRI_EMPTY_OR_CULLED;
    if (u_cull_enabled == 0u) {
        if (area < 0.0) ids = ids.xzy;
    } else if (u_cull_front != 0u) {
        if (area > 0.0) return TRI_EMPTY_OR_CULLED;
        ids = ids.xzy;
    } else if (area < 0.0) {
        return TRI_EMPTY_OR_CULLED;
    }
    return TRI_ACCEPTED;
}

void main() {
    if (gl_GlobalInvocationID.x != 0u) return;
    uint count = candidate_count();
    uint out_count = 0u;
    uint decline_bits = 0u;
    if (u_primitive != 5u && u_primitive != 6u && u_primitive != 7u)
        decline_bits |= BATCH_INVALID_SHAPE;
    for (uint tid = 0u; tid < count; ++tid) {
        uvec3 ids;
        uint status = evaluate_triangle(tid, ids);
        write_candidate(tid, ids, status);
        if (status == TRI_ACCEPTED) {
            if (out_count + 3u > u_max_resident_vertices) {
                write_candidate(tid, ids, TRI_OUTPUT_OVERFLOW);
                decline_bits |= BATCH_OUTPUT_OVERFLOW;
                continue;
            }
            write_resident_vertex(out_count, ids.x);
            write_resident_vertex(out_count + 1u, ids.y);
            write_resident_vertex(out_count + 2u, ids.z);
            out_count += 3u;
        } else if (status == TRI_PROGRAM_DECLINE) {
            decline_bits |= BATCH_PROGRAM_DECLINE;
        } else if (status == TRI_GEOMETRY_DECLINE) {
            decline_bits |= BATCH_GEOMETRY_DECLINE;
        }
    }
    control = uvec4(out_count, decline_bits, count, 0u);
}
)GLSL";

} // namespace xlle::vsh_gpu_batch
