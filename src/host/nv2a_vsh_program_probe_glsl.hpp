#pragma once
#include "host/nv2a_vsh_gpu_batch_glsl.hpp"

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
#define VSH_LOAD_INPUT(execution, reg) vertices[execution].inputs[reg]
#define VSH_LOAD_CONSTANT(execution, reg) vertices[execution].constants[reg]
)GLSL") + xlle::vsh_gpu_batch::kVshExecutionCommonSource + R"GLSL(
#undef VSH_LOAD_INPUT
#undef VSH_LOAD_CONSTANT

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
