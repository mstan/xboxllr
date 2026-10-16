#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "nv2a_vsh_disassembler.h"

namespace xlle {

// Data dependencies of the finite, decoded Kelvin programs used by the first
// Blinx draws. This is analysis, not execution, numeric simplification, or a
// replacement decoder. All masks here use X=1,Y=2,Z=4,W=8 (NOT NV2AWM_X=8).
struct VshDependencies {
    std::array<uint8_t, 16> inputs{};
    std::array<uint8_t, 192> constants{};
    // Expose read-before-write lanes instead of assuming their initial values.
    // The caller can use its separately qualified per-vertex initialization.
    std::array<uint8_t, 12> initial_temps{};
    std::array<uint8_t, 13> initial_outputs{};
    size_t steps = 0;
};

namespace vsh_dependency_detail {

inline unsigned arity(Nv2aVshOpcode op, bool scalar) {
    if (op == NV2AOP_NOP) return 0;
    if (op == NV2AOP_MOV || (scalar && op == NV2AOP_RCP)) return 1;
    if (!scalar && (op == NV2AOP_MUL || op == NV2AOP_ADD)) return 2;
    if (!scalar && op == NV2AOP_MAD) return 3;
    return 4; // unsupported, distinct from every legal operand count
}

inline bool validate(const Nv2aVshOperation& op, bool scalar, bool paired,
                     const char*& error) {
    const unsigned count = arity(op.opcode, scalar);
    if (count == 4) {
        error = "VSH dependency opcode is outside MOV/MUL/ADD/MAD/RCP";
        return false;
    }
    // The decoder may leave unused descriptors uninitialized on a NOP. The
    // executor ignores the entire operation, so do not inspect those fields.
    if (!count) return true;
    for (unsigned i = 0; i < 3; ++i) {
        const auto& in = op.inputs[i];
        if (i >= count) {
            if (in.type != NV2ART_NONE) {
                error = "VSH dependency operand arity is invalid"; return false;
            }
            continue;
        }
        if (!((in.type == NV2ART_INPUT && in.index < 16) ||
              (in.type == NV2ART_CONTEXT && in.index < 192) ||
              (in.type == NV2ART_TEMPORARY && in.index <= 12))) {
            error = "VSH dependency input/addressing is unsupported"; return false;
        }
        // The decoder initializes is_relative only for CONTEXT operands;
        // other register types do not use it and it may be indeterminate.
        if (in.type == NV2ART_CONTEXT && in.is_relative) {
            error = "VSH dependency relative context addressing is unsupported"; return false;
        }
        for (uint8_t lane : in.swizzle) if (lane > 3) {
            error = "VSH dependency swizzle is invalid"; return false;
        }
    }
    for (const auto& out : op.outputs) {
        if (out.type == NV2ART_NONE) continue;
        // Context/address/input writes are rejected even when their result
        // would be dead. They are not covered by this side-effect-free scope.
        if (!((out.type == NV2ART_OUTPUT && out.index < 13) ||
              (out.type == NV2ART_TEMPORARY && out.index < 12)) ||
            static_cast<unsigned>(out.writemask) > 15) {
            error = "VSH dependency destination/state write is unsupported";
            return false;
        }
        // The vendored decoder resolves a paired ILU temp destination to R1.
        if (scalar && paired && out.type == NV2ART_TEMPORARY && out.index != 1) {
            error = "VSH dependency paired ILU destination is not R1"; return false;
        }
    }
    return true;
}

struct Live {
    std::array<uint8_t, 12> temps{};
    std::array<uint8_t, 13> outputs{};
};

// Remove stores in reverse application order and return the result lanes
// needed by those stores. The patched maintained executor treats oFog as
// scalar: its first masked result lane writes o5.x, irrespective of that lane.
inline uint8_t remove_stores(const Nv2aVshOperation& op, Live& live) {
    if (op.opcode == NV2AOP_NOP) return 0;
    uint8_t needed = 0;
    for (unsigned n = 2; n; --n) {
        const auto& out = op.outputs[n - 1];
        if (out.type == NV2ART_NONE) continue;
        auto& mask = out.type == NV2ART_OUTPUT ? live.outputs[out.index]
                                             : live.temps[out.index];
        const unsigned write = static_cast<unsigned>(out.writemask);
        if (out.type == NV2ART_OUTPUT && out.index == 5) {
            if (write && (mask & 1)) {
                for (unsigned lane = 0; lane < 4; ++lane) if (write & (8u >> lane)) {
                    needed |= uint8_t(1u << lane); break;
                }
                mask &= uint8_t(~1u);
            }
        } else {
            for (unsigned lane = 0; lane < 4; ++lane) if (write & (8u >> lane)) {
                const uint8_t bit = uint8_t(1u << lane);
                needed |= uint8_t(mask & bit);
                mask &= uint8_t(~bit);
            }
        }
    }
    return needed;
}

inline void add_inputs(const Nv2aVshOperation& op, uint8_t needed, bool scalar,
                       Live& live, VshDependencies& result) {
    if (!needed) return;
    const unsigned count = arity(op.opcode, scalar);
    for (unsigned i = 0; i < count; ++i) {
        const auto& in = op.inputs[i];
        uint8_t lanes = 0;
        if (op.opcode == NV2AOP_RCP) {
            // RCP broadcasts the reciprocal of operand X to every result lane.
            lanes = uint8_t(1u << in.swizzle[0]);
        } else {
            for (unsigned lane = 0; lane < 4; ++lane) if (needed & (1u << lane))
                lanes |= uint8_t(1u << in.swizzle[lane]);
        }
        // Negation affects values, not which source components are needed.
        switch (in.type) {
        case NV2ART_INPUT: result.inputs[in.index] |= lanes; break;
        case NV2ART_CONTEXT: result.constants[in.index] |= lanes; break;
        case NV2ART_TEMPORARY:
            // Only source R12 aliases oPos. TEMPORARY12 stores are rejected,
            // matching the maintained compiler/interpreter destination contract.
            if (in.index == 12) live.outputs[0] |= lanes;
            else live.temps[in.index] |= lanes;
            break;
        default: break; // validation excludes all other types
        }
    }
}

} // namespace vsh_dependency_detail

// Analyze the prefix through its first FINAL, bounded by the supplied decoded
// storage and Kelvin's136 slots. Failure leaves result unchanged. Unsupported
// operations/addressing/state writes reject the whole executed prefix, even if
// a requested output would not depend on them. Unexecuted trailing slots are
// ignored, just as the existing decoder/executor ignores them.
//
// MAC and ILU read the PRE-step state, then MAC stores followed by ILU stores.
// Kill both halves' stores before adding either half's source dependencies.
// This preserves parallel reads and the ILU's later-store priority on overlap.
// Caller-selected outputs must include every subsequently observable component;
// this analysis does not select raster state, prove numeric domains, suppress
// execution, supply unknown values, or authorize a different processor path.
inline bool vsh_dependencies(const Nv2aVshStep* steps, size_t slots,
                             const std::array<uint8_t, 13>& observable,
                             VshDependencies& result, const char*& error) {
    using namespace vsh_dependency_detail;
    error = nullptr;
    if (!steps || !slots || slots > 136) {
        error = "VSH dependency program extent is invalid"; return false;
    }
    for (size_t i = 0; i < observable.size(); ++i) {
        if ((observable[i] & ~15u) || (i == 5 && (observable[i] & ~1u))) {
            error = "VSH dependency observable component mask is invalid"; return false;
        }
    }
    size_t count = 0;
    bool final = false;
    for (; count < slots; ++count) {
        const auto& step = steps[count];
        if (!validate(step.mac, false, false, error) ||
            !validate(step.ilu, true, step.mac.opcode != NV2AOP_NOP, error)) return false;
        if (step.is_final) { ++count; final = true; break; }
    }
    if (!final) { error = "VSH dependency program has no FINAL"; return false; }
    Live live;
    live.outputs = observable;
    VshDependencies staged;
    staged.steps = count;
    while (count) {
        const auto& step = steps[--count];
        const uint8_t ilu = remove_stores(step.ilu, live);
        const uint8_t mac = remove_stores(step.mac, live);
        add_inputs(step.mac, mac, false, live, staged);
        add_inputs(step.ilu, ilu, true, live, staged);
    }
    staged.initial_temps = live.temps;
    staged.initial_outputs = live.outputs;
    result = staged;
    return true;
}

} // namespace xlle
