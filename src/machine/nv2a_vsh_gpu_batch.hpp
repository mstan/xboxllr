#pragma once

#include "machine/nv2a_vsh_capture.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace xlle::vsh_gpu_batch {

// Source-independent GPU batch ABI for a resident-primary VSH/triangle handoff.
// It carries decoded operations, shared constants, fetched vertex inputs and
// submitted-reference mapping; it never uses guest bytecode bytes, addresses, or
// a program fingerprint as an admission key.
using Op = vsh_capture::Op;
static_assert(sizeof(Op) == 96);

constexpr uint32_t kWorkgroupSize = 64;
constexpr uint32_t kMaxOps = vsh_capture::kMaxOps;
constexpr uint32_t kMaxExecutions = 16384;
constexpr uint32_t kMaxReferences = 16384;
constexpr uint32_t kMaxResidentVertices = kMaxReferences * 3u;

constexpr uint32_t kPrimitiveTriangles = 5;
constexpr uint32_t kPrimitiveTriangleStrip = 6;
constexpr uint32_t kPrimitiveTriangleFan = 7;

struct ExecutionInput {
    std::array<uint32_t, 64> words{}; // 16 raw uvec4 input registers.
};
static_assert(sizeof(ExecutionInput) == 256);

struct ProgramConstants {
    std::array<uint32_t, 768> words{}; // 192 raw uvec4 constant registers.
};
static_assert(sizeof(ProgramConstants) == 3072);

struct ProgramOutput128B {
    // values[0..6] are the consumed output registers in vsh_capture order:
    // oPos, oD0, oD1, oT0, oT1, oT2, oT3. values[7] is postprocessed screen
    // position, matching the CPU latch path used by resident-primary setup.
    std::array<std::array<uint32_t, 4>, 8> values{};
};
static_assert(sizeof(ProgramOutput128B) == 128);

struct TriangleCandidate16B {
    // x/y/z = execution ids after CPU-compatible winding choice; w = status.
    std::array<uint32_t, 4> words{};
};
static_assert(sizeof(TriangleCandidate16B) == 16);

struct ResidentVertex96B {
    // Exact final resident VBO element order consumed by the current GL resident
    // primary path: position, diffuse, then texture registers 0..3.
    std::array<uint32_t, 4> position{};
    std::array<uint32_t, 4> diffuse{};
    std::array<std::array<uint32_t, 4>, 4> texture{};
};
static_assert(sizeof(ResidentVertex96B) == 96);

struct BatchControl16B {
    // output_vertex_count, decline bits, candidate triangle count, reserved.
    std::array<uint32_t, 4> words{};
};
static_assert(sizeof(BatchControl16B) == 16);

struct BatchShape {
    uint32_t primitive_mode = 0;
    bool large_target = false;
    bool cull_enabled = false;
    bool cull_front = false;
    uint32_t op_count = 0;
    uint32_t execution_count = 0;
    uint32_t reference_count = 0;
};

struct Batch {
    BatchShape shape{};
    ProgramConstants constants{};
    std::vector<Op> ops;
    std::vector<ExecutionInput> executions;
    std::vector<uint32_t> references; // submitted vertex -> actual execution id.
};

struct BindingLayout {
    // VSH pass: bindings 0..4.
    static constexpr uint32_t kExecutionInputs = 0;
    static constexpr uint32_t kOps = 1;
    static constexpr uint32_t kProgramOutputs = 2;
    static constexpr uint32_t kProgramStatus = 3;
    static constexpr uint32_t kConstants = 4;

    // Pack pass reuses bindings 2..7, staying within GL 4.3's minimum of 8
    // shader-storage binding points.
    static constexpr uint32_t kReferences = 4;
    static constexpr uint32_t kTriangleCandidates = 5;
    static constexpr uint32_t kResidentVertices = 6;
    static constexpr uint32_t kControl = 7;
};

struct ProgramStatus {
    static constexpr uint32_t kOk = 0;
    static constexpr uint32_t kNumericDecline = 1;
    static constexpr uint32_t kUnsupportedProgram = 2;
};

struct TriangleStatus {
    static constexpr uint32_t kEmptyOrCulled = 0;
    static constexpr uint32_t kAccepted = 1;
    static constexpr uint32_t kGeometryDecline = 2;
    static constexpr uint32_t kProgramDecline = 3;
    static constexpr uint32_t kOutputOverflow = 4;
};

struct BatchStatusBits {
    static constexpr uint32_t kProgramDecline = 1u << 0;
    static constexpr uint32_t kGeometryDecline = 1u << 1;
    static constexpr uint32_t kOutputOverflow = 1u << 2;
    static constexpr uint32_t kInvalidShape = 1u << 3;
};

constexpr bool supported_primitive(uint32_t primitive_mode) {
    return primitive_mode == kPrimitiveTriangles ||
           primitive_mode == kPrimitiveTriangleStrip ||
           primitive_mode == kPrimitiveTriangleFan;
}

constexpr uint32_t triangle_count(uint32_t primitive_mode, uint32_t references) {
    if (primitive_mode == kPrimitiveTriangles) return references / 3u;
    if (primitive_mode == kPrimitiveTriangleStrip || primitive_mode == kPrimitiveTriangleFan)
        return references >= 3u ? references - 2u : 0u;
    return 0u;
}

constexpr uint32_t resident_vertices_for_full_accept(uint32_t primitive_mode,
                                                     uint32_t references) {
    return triangle_count(primitive_mode, references) * 3u;
}

inline bool validate_shape(const BatchShape& shape) {
    if (!supported_primitive(shape.primitive_mode)) return false;
    if (shape.op_count == 0 || shape.op_count > kMaxOps) return false;
    if (shape.execution_count > kMaxExecutions) return false;
    if (shape.reference_count > kMaxReferences) return false;
    return resident_vertices_for_full_accept(shape.primitive_mode, shape.reference_count) <=
           kMaxResidentVertices;
}

inline bool validate_batch_vectors(const Batch& batch) {
    if (batch.shape.op_count != batch.ops.size() ||
        batch.shape.execution_count != batch.executions.size() ||
        batch.shape.reference_count != batch.references.size() ||
        !validate_shape(batch.shape)) {
        return false;
    }
    for (uint32_t ref : batch.references) {
        if (ref >= batch.shape.execution_count) return false;
    }
    return true;
}

// Validate decoder fields before they become shader array indices. Address and
// relative operands remain on the CPU even when their program is otherwise legal.
inline bool supported_program_layout(const std::vector<Op>& ops) {
    const auto arity = [](uint32_t code) -> uint32_t {
        if (code == 4) return 3;
        if (code == 1 || (code >= 13 && code <= 19)) return 1;
        return code >= 2 && code <= 12 ? 2 : 0;
    };
    constexpr std::array<uint32_t, 4> register_counts{12, 13, 16, 192};
    for (size_t i = 0; i < ops.size(); ++i) {
        const auto& w = ops[i].words;
        const uint32_t inputs = arity(w[0]);
        if (!inputs || w[2] != inputs || w[1] > 1 || w[7] ||
            (w[3] & ~((1u << inputs) - 1u)) || w[11] || w[15] || w[22] || w[23]) return false;
        if (w[1] && (i + 1 >= ops.size() || ops[i + 1].words[1] ||
                     arity(ops[i + 1].words[0]) != 1)) return false;
        for (uint32_t k = 0; k < 3; ++k) {
            const uint32_t base = w[4 + k], offset = w[8 + k], swizzle = w[12 + k];
            if (k >= inputs) {
                if (base != 255 || offset || swizzle) return false;
            } else if (base >= register_counts.size() || offset % 4 ||
                       offset / 4 >= register_counts[base] || swizzle > 255) return false;
        }
        for (uint32_t k = 0; k < 2; ++k) {
            const uint32_t base = w[16 + k], offset = w[18 + k], mask = w[20 + k];
            if (base == 255) { if (offset || mask) return false; }
            else if (base > 1 || offset % 4 || offset / 4 >= register_counts[base] || !mask || mask > 15)
                return false;
        }
    }
    return !ops.empty() && ops.size() <= kMaxOps;
}

} // namespace xlle::vsh_gpu_batch
