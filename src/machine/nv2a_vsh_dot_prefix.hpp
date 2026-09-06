#ifndef XLLE_MACHINE_NV2A_VSH_DOT_PREFIX_HPP
#define XLLE_MACHINE_NV2A_VSH_DOT_PREFIX_HPP

#include <array>
#include <cstdint>
#include <cstring>

#include "machine/nv2a_vsh_compiled.hpp"

namespace xlle {

#if defined(_MSC_VER)
#define XLLE_VSH_DOT_PREFIX_NOINLINE __declspec(noinline)
#else
#define XLLE_VSH_DOT_PREFIX_NOINLINE __attribute__((noinline))
#endif

struct VshDotPrefixStats {
    uint64_t matched_programs = 0;
    uint64_t planned_ops = 0;
    uint64_t executed_vertices = 0;
    uint64_t executed_ops = 0;
};

struct VshDotPrefixDispatch {
    VshCompiledOp::Fn dp3 = nullptr;
    VshCompiledOp::Fn dp4 = nullptr;
};

struct VshDotPrefixPlan {
    static constexpr uint32_t kMaxOps = 32;
    static constexpr uint32_t kMaxSources = kMaxOps * 2;
    static constexpr uint8_t kIdentitySwizzle = 0xE4;

    struct Source {
        uint8_t base = kVshBaseNone;
        uint16_t offset = 0;
    };

    struct Op {
        VshCompiledOp::Fn fn = nullptr;
        uint8_t source[2] = {0, 0};
        uint8_t out_base = kVshBaseNone;
        uint8_t out_lane = 0;
        uint16_t out_offset = 0;
    };

    std::array<Source, kMaxSources> sources{};
    std::array<Op, kMaxOps> ops{};
    uint32_t source_count = 0;
    uint32_t op_count = 0;

    void clear() {
        source_count = 0;
        op_count = 0;
    }

    bool active() const { return op_count >= 2; }
};

inline float vsh_dot_prefix_positive_one() {
    static const volatile float one = 1.0f;
    return one;
}

inline bool vsh_dot_prefix_single_lane_mask(uint8_t mask) {
    return mask == 8u || mask == 4u || mask == 2u || mask == 1u;
}

inline uint32_t vsh_dot_prefix_lane_from_mask(uint8_t mask) {
    switch (mask) {
        case 8u: return 0;
        case 4u: return 1;
        case 2u: return 2;
        default: return 3;
    }
}

inline bool vsh_dot_prefix_valid_source(uint8_t base, uint16_t offset) {
    if ((offset & 3u) != 0) return false;
    if (base == kVshBaseInput) return offset <= 15u * 4u;
    if (base == kVshBaseContext) return offset <= 191u * 4u;
    return false;
}

inline bool vsh_dot_prefix_valid_destination(uint8_t base, uint16_t offset) {
    if ((offset & 3u) != 0) return false;
    if (base == kVshBaseTemp) return offset <= 11u * 4u;
    if (base == kVshBaseOutput) return offset <= 12u * 4u;
    return false;
}

inline bool vsh_dot_prefix_accept_op(const VshCompiledOp& op,
                                     const VshDotPrefixDispatch& dispatch) {
    if (op.fn != dispatch.dp3 && op.fn != dispatch.dp4) return false;
    if (op.input_count != 2u) return false;
    if (op.paired != 0u) return false;
    if (op.in_negate_mask != 0u || op.in_relative_mask != 0u) return false;
    if (op.in_base[0] != kVshBaseInput || op.in_base[1] != kVshBaseContext)
        return false;
    if (op.in_swizzle[0] != VshDotPrefixPlan::kIdentitySwizzle ||
        op.in_swizzle[1] != VshDotPrefixPlan::kIdentitySwizzle)
        return false;
    if (!vsh_dot_prefix_valid_source(op.in_base[0], op.in_offset[0]) ||
        !vsh_dot_prefix_valid_source(op.in_base[1], op.in_offset[1]))
        return false;
    if (op.out_base[1] != kVshBaseNone) return false;
    if (!vsh_dot_prefix_single_lane_mask(op.out_mask[0])) return false;
    if (!vsh_dot_prefix_valid_destination(op.out_base[0], op.out_offset[0]))
        return false;
    return true;
}

inline uint8_t vsh_dot_prefix_source_index(VshDotPrefixPlan& plan,
                                           uint8_t base, uint16_t offset) {
    for (uint32_t i = 0; i < plan.source_count; ++i) {
        if (plan.sources[i].base == base && plan.sources[i].offset == offset)
            return static_cast<uint8_t>(i);
    }
    const uint32_t index = plan.source_count++;
    plan.sources[index].base = base;
    plan.sources[index].offset = offset;
    return static_cast<uint8_t>(index);
}

inline bool vsh_build_dot_prefix_plan(const VshCompiledOp* ops, uint32_t count,
                                      const VshDotPrefixDispatch& dispatch,
                                      VshDotPrefixPlan& plan) {
    plan.clear();
    if (!ops || !dispatch.dp3 || !dispatch.dp4) return false;
    for (uint32_t i = 0; i < count && plan.op_count < VshDotPrefixPlan::kMaxOps;
         ++i) {
        const VshCompiledOp& op = ops[i];
        if (!vsh_dot_prefix_accept_op(op, dispatch)) break;
        VshDotPrefixPlan::Op& planned = plan.ops[plan.op_count++];
        planned.fn = op.fn;
        planned.source[0] = vsh_dot_prefix_source_index(plan, op.in_base[0],
                                                        op.in_offset[0]);
        planned.source[1] = vsh_dot_prefix_source_index(plan, op.in_base[1],
                                                        op.in_offset[1]);
        planned.out_base = op.out_base[0];
        planned.out_lane =
            static_cast<uint8_t>(vsh_dot_prefix_lane_from_mask(op.out_mask[0]));
        planned.out_offset = op.out_offset[0];
    }
    if (!plan.active()) {
        plan.clear();
        return false;
    }
    return true;
}

// Accepted prefix operands can only read the input and context register files,
// and accepted destinations can only write temp/output. Those register files
// are distinct in the compiled executor's base table, so prefetching unique
// input/context vectors before the prefix stores cannot change dependencies.
inline XLLE_VSH_DOT_PREFIX_NOINLINE uint32_t
vsh_execute_dot_prefix(const VshDotPrefixPlan& plan, float* const* bases) {
    float source_values[VshDotPrefixPlan::kMaxSources][4];
    const float one = vsh_dot_prefix_positive_one();
    for (uint32_t i = 0; i < plan.source_count; ++i) {
        const auto& source = plan.sources[i];
        const float* src = bases[source.base] + source.offset;
        source_values[i][0] = one * src[0];
        source_values[i][1] = one * src[1];
        source_values[i][2] = one * src[2];
        source_values[i][3] = one * src[3];
    }

    float inputs[3 * 4];
    float result[4];
    for (uint32_t i = 0; i < plan.op_count; ++i) {
        const auto& op = plan.ops[i];
        std::memcpy(&inputs[0], source_values[op.source[0]], 4 * sizeof(float));
        std::memcpy(&inputs[4], source_values[op.source[1]], 4 * sizeof(float));
        op.fn(result, inputs);
        float* dst = bases[op.out_base] + op.out_offset;
        dst[op.out_lane] = result[op.out_lane];
    }
    return plan.op_count;
}

#undef XLLE_VSH_DOT_PREFIX_NOINLINE

}  // namespace xlle

#endif  // XLLE_MACHINE_NV2A_VSH_DOT_PREFIX_HPP
