#pragma once

#include "host/nv2a_vsh_gpu_batch_glsl.hpp"
#include "machine/nv2a_vsh_gpu_batch.hpp"

namespace xlle::vsh_gpu_batch {

// Bound driver compilation and cache storage. Larger/unsupported programs keep
// the generic GPU interpreter and its real CPU fallback. Constants and vertex
// inputs remain dynamic; every decoded operation word is part of the identity.
constexpr size_t kSpecializeMaxOps = 64;
constexpr size_t kSpecializeCacheSize = 32;

inline bool same_program(const std::vector<Op>& a, const std::vector<Op>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (a[i].words != b[i].words) return false;
    return true;
}

inline std::string specialized_vsh_source(const std::vector<Op>& ops) {
    if (ops.size() > kSpecializeMaxOps || !supported_program_layout(ops)) return {};
    for (const auto& op : ops)
        if (op.words[0] == 13 || op.words[0] >= 17) return {};

    std::string source = kVshBatchPreamble + R"GLSL(
void main() {
    uint index = gl_GlobalInvocationID.x;
    if (index >= u_execution_count) return;
    for (uint i = 0u; i < 12u; ++i) temps[i] = uvec4(0u);
    for (uint i = 0u; i < 13u; ++i) regs[i] = uvec4(0u, 0u, 0u, 0x3f800000u);
    bool supported = true;
)GLSL";
    const auto declare = [&](const char* name, const Op& op) {
        source += std::string("const Op ") + name + " = Op(";
        for (unsigned v = 0; v < 6; ++v) {
            if (v) source += ",";
            source += "uvec4(";
            for (unsigned lane = 0; lane < 4; ++lane) {
                if (lane) source += ",";
                source += std::to_string(op.words[v * 4 + lane]) + "u";
            }
            source += ")";
        }
        source += ");\n";
    };
    for (size_t i = 0; i < ops.size(); ++i) {
        const auto& op = ops[i];
        source += "{\n";
        declare("op", op);
        for (unsigned arg = 0; arg < 3; ++arg) {
            source += "uvec4 a" + std::to_string(arg) + " = ";
            source += arg < op.words[2]
                ? "fetch_operand(op," + std::to_string(arg) + "u,index,supported);\n"
                : "uvec4(0u);\n";
        }
        // Paired MAC and ILU read their inputs before either writes. Keep the
        // exact helper arithmetic and post-pair decline point of the interpreter.
        if (op.words[1]) {
            declare("ilu", ops[i + 1]);
            source += "uvec4 ilu_arg = fetch_operand(ilu,0u,index,supported);\n";
        }
        source += "store_result(op,execute_op(op,a0,a1,a2,supported));\n";
        if (op.words[1]) {
            source += "store_result(ilu,execute_op(ilu,ilu_arg,uvec4(0u),uvec4(0u),supported));\n";
            ++i;
        }
        source += "if (!supported) { program_status[index] = 1u; return; }\n}\n";
    }
    return source + kVshBatchFinish;
}

} // namespace xlle::vsh_gpu_batch
