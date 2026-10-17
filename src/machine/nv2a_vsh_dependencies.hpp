#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "nv2a_vsh_cpu.h"
#include "nv2a_vsh_disassembler.h"

namespace xlle {

// Data dependencies of finite, decoded Kelvin programs with direct operands.
// This is analysis, not execution, numeric simplification, or a
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
    if (op == NV2AOP_MOV) return 1;
    if (scalar) {
        switch (op) {
        case NV2AOP_RCP: case NV2AOP_RCC: case NV2AOP_RSQ:
        case NV2AOP_EXP: case NV2AOP_LOG: case NV2AOP_LIT: return 1;
        default: break;
        }
    } else {
        switch (op) {
        case NV2AOP_MUL: case NV2AOP_ADD:
        case NV2AOP_DP3: case NV2AOP_DPH: case NV2AOP_DP4:
        case NV2AOP_DST: case NV2AOP_MIN: case NV2AOP_MAX:
        case NV2AOP_SLT: case NV2AOP_SGE: return 2;
        case NV2AOP_MAD: return 3;
        default: break;
        }
    }
    return 4; // unsupported, distinct from every legal operand count
}

inline bool validate(const Nv2aVshOperation& op, bool scalar, bool paired,
                     const char*& error, bool indexed = false) {
    const bool address = indexed && !scalar && op.opcode == NV2AOP_ARL;
    const unsigned count = address ? 1 : arity(op.opcode, scalar);
    if (count == 4) {
        error = "VSH dependency opcode is unsupported for this execution unit";
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
        if (in.type == NV2ART_CONTEXT && in.is_relative && (!indexed || address)) {
            error = "VSH dependency relative context addressing is unsupported"; return false;
        }
        for (uint8_t lane : in.swizzle) if (lane > 3) {
            error = "VSH dependency swizzle is invalid"; return false;
        }
    }
    if (address) {
        // The reached ARLs read direct vertex/context values. Computed address
        // sources need their own value/provenance evaluation, not a guess.
        if ((op.inputs[0].type != NV2ART_INPUT && op.inputs[0].type != NV2ART_CONTEXT) ||
            op.outputs[0].type != NV2ART_ADDRESS || op.outputs[0].index != 0 ||
            op.outputs[0].writemask != 0 || op.outputs[1].type != NV2ART_NONE) {
            error = "VSH dependency ARL source/destination is unsupported"; return false;
        }
        return true;
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

inline bool validate_program(const Nv2aVshStep* steps, size_t slots, bool indexed,
                             size_t& count, const char*& error) {
    error = nullptr;
    if (!steps || !slots || slots > 136) {
        error = "VSH dependency program extent is invalid"; return false;
    }
    for (count = 0; count < slots; ++count) {
        const auto& step = steps[count];
        if (!validate(step.mac, false, false, error, indexed) ||
            !validate(step.ilu, true, step.mac.opcode != NV2AOP_NOP, error, indexed)) return false;
        if (step.is_final) { ++count; return true; }
    }
    error = "VSH dependency program has no FINAL"; return false;
}

struct Live {
    std::array<uint8_t, 12> temps{};
    std::array<uint8_t, 13> outputs{};
};

// Operand lanes BEFORE mapping through its swizzle. These are structural
// dependencies of the maintained arithmetic, independent of operand values.
// Keep branches conservative (notably LIT.z), but do not require inputs for
// literal result components such as DST.x or EXP/LOG/LIT.w.
inline uint8_t operand_lanes(Nv2aVshOpcode op, unsigned input, uint8_t needed) {
    if (!needed) return 0;
    switch (op) {
    case NV2AOP_DP3: return 7;
    case NV2AOP_DPH: return input == 0 ? 7 : 15;
    case NV2AOP_DP4: return 15;
    case NV2AOP_DST: return uint8_t(needed & (input == 0 ? 6 : 10));
    case NV2AOP_RCP: case NV2AOP_RCC: case NV2AOP_RSQ: return 1;
    case NV2AOP_EXP: case NV2AOP_LOG: return (needed & 7) ? 1 : 0;
    case NV2AOP_LIT: return uint8_t(((needed & 2) ? 1 : 0) |
                                 ((needed & 4) ? 11 : 0));
    default: return needed; // lane-wise MAC arithmetic and MAC/ILU MOV
    }
}

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
        const uint8_t source = operand_lanes(op.opcode, i, needed);
        for (unsigned lane = 0; lane < 4; ++lane) if (source & (1u << lane))
            lanes |= uint8_t(1u << in.swizzle[lane]);
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
    if (!validate_program(steps, slots, false, count, error)) return false;
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

// Classification only; either analysis entry point still validates the entire
// executed prefix. Ignore unused descriptors (especially NOP) and trailing data.
inline bool vsh_dependencies_need_vertex(const Nv2aVshStep* steps, size_t slots) {
    if (!steps || !slots || slots > 136) return false;
    for (size_t n = 0; n < slots; ++n) {
        const auto& step = steps[n];
        if (step.mac.opcode == NV2AOP_ARL) return true;
        for (unsigned unit = 0; unit < 2; ++unit) {
            const auto& op = unit ? step.ilu : step.mac;
            const unsigned count = vsh_dependency_detail::arity(op.opcode, unit != 0);
            if (count > 3) continue;
            for (unsigned i = 0; i < count; ++i)
                if (op.inputs[i].type == NV2ART_CONTEXT && op.inputs[i].is_relative) return true;
        }
        if (step.is_final) break;
    }
    return false;
}

// Resolve the reached direct-source ARL form from this vertex's actual,
// producer-known values. Only the private ANALYSIS copy gets direct indices;
// the renderer still executes the original program with its normal arithmetic.
// This is not a substitute numeric shader interpreter or a context-state write.
// Unknown data may feed dead values, but every physically fetched relative
// index is checked, and all address-defining source lanes remain dependencies.
// Relative reads before a produced ARL and computed/relative ARL sources remain
// guarded. Address initialization/precision beyond this contract is not claimed.
inline bool vsh_dependencies_for_vertex(const Nv2aVshStep* steps, size_t slots,
        const std::array<uint8_t, 13>& observable,
        const std::array<std::array<float, 4>, 16>& inputs,
        const std::array<uint8_t, 16>& input_known,
        const std::array<std::array<float, 4>, 192>& constants,
        const std::array<uint8_t, 192>& constant_known,
        VshDependencies& result, const char*& error) {
    using namespace vsh_dependency_detail;
    size_t count = 0;
    // Validate pairing on the original steps, before an ARL can become NOP.
    if (!validate_program(steps, slots, true, count, error)) return false;
    std::array<Nv2aVshStep, 136> direct;
    std::memcpy(direct.data(), steps, count * sizeof(*steps));
    int32_t address = 0;
    bool address_known = false;
    VshDependencies address_sources;
    for (size_t n = 0; n < count; ++n) {
        auto& step = direct[n];
        // Both halves fetch from PRE-step a0, even when MAC is a new ARL.
        for (unsigned unit = 0; unit < 2; ++unit) {
            auto& op = unit ? step.ilu : step.mac;
            const unsigned operands = op.opcode == NV2AOP_ARL ? 1 : arity(op.opcode, unit != 0);
            for (unsigned i = 0; i < operands; ++i) {
                auto& in = op.inputs[i];
                if (in.type != NV2ART_CONTEXT || !in.is_relative) continue;
                if (!address_known) {
                    error = "VSH dependency relative read before produced ARL"; return false;
                }
                const uint32_t index = uint32_t(in.index) + uint32_t(address);
                if (index >= constants.size()) {
                    error = "VSH dependency relative context index is out of range"; return false;
                }
                in.index = index; in.is_relative = false;
            }
        }
        if (step.mac.opcode == NV2AOP_ARL) {
            const auto& in = step.mac.inputs[0];
            const unsigned lane = in.swizzle[0];
            const uint8_t bit = uint8_t(1u << lane);
            const bool from_input = in.type == NV2ART_INPUT;
            const uint8_t known = from_input ? input_known[in.index] : constant_known[in.index];
            if (!(known & bit)) {
                error = "VSH dependency ARL source lane has no known producer"; return false;
            }
            const float value = from_input ? inputs[in.index][lane] : constants[in.index][lane];
            // Same operand multiply and SAME arithmetic function as both
            // maintained executors, including the donor's near-integer bias.
            const float operand[4] = { (in.is_negated ? -1.0f : 1.0f) * value, 0, 0, 0 };
            float computed[4]; nv2a_vsh_cpu_arl(computed, operand);
            const double widened = computed[0];
            if (!std::isfinite(widened) || widened < -2147483648.0 || widened >= 2147483648.0) {
                error = "VSH dependency ARL result is outside signed32 range"; return false;
            }
            address = static_cast<int32_t>(computed[0]); address_known = true;
            if (from_input) address_sources.inputs[in.index] |= bit;
            else address_sources.constants[in.index] |= bit;
            step.mac = {}; // analysis only; original ARL is still executed
        }
    }
    VshDependencies staged;
    if (!vsh_dependencies(direct.data(), count, observable, staged, error)) return false;
    for (size_t i = 0; i < staged.inputs.size(); ++i) staged.inputs[i] |= address_sources.inputs[i];
    for (size_t i = 0; i < staged.constants.size(); ++i) staged.constants[i] |= address_sources.constants[i];
    result = staged;
    return true;
}

} // namespace xlle
