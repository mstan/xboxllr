#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <vector>

#include "machine/nv2a_vsh_capture.hpp"
#include "machine/nv2a_vsh_compiled.hpp"
#include "nv2a_vsh_cpu.h"

namespace xlle::vsh_capture_oracle {

namespace detail {

inline VshCompiledOp::Fn opcode_fn(uint32_t opcode) {
    switch (opcode) {
        case 1: return nv2a_vsh_cpu_mov;
        case 2: return nv2a_vsh_cpu_mul;
        case 3: return nv2a_vsh_cpu_add;
        case 4: return nv2a_vsh_cpu_mad;
        case 5: return nv2a_vsh_cpu_dp3;
        case 6: return nv2a_vsh_cpu_dph;
        case 7: return nv2a_vsh_cpu_dp4;
        case 8: return nv2a_vsh_cpu_dst;
        case 9: return nv2a_vsh_cpu_min;
        case 10: return nv2a_vsh_cpu_max;
        case 11: return nv2a_vsh_cpu_slt;
        case 12: return nv2a_vsh_cpu_sge;
        case 13: return nv2a_vsh_cpu_arl;
        case 14: return nv2a_vsh_cpu_rcp;
        case 15: return nv2a_vsh_cpu_rcc;
        case 16: return nv2a_vsh_cpu_rsq;
        case 17: return nv2a_vsh_cpu_exp;
        case 18: return nv2a_vsh_cpu_log;
        case 19: return nv2a_vsh_cpu_lit;
        default: return nullptr;
    }
}

inline uint8_t opcode_arity(uint32_t opcode) {
    switch (opcode) {
        case 1:   // MOV
        case 13:  // ARL
        case 14:  // RCP
        case 15:  // RCC
        case 16:  // RSQ
        case 17:  // EXP
        case 18:  // LOG
        case 19:  // LIT
            return 1;
        case 2:   // MUL
        case 3:   // ADD
        case 5:   // DP3
        case 6:   // DPH
        case 7:   // DP4
        case 8:   // DST
        case 9:   // MIN
        case 10:  // MAX
        case 11:  // SLT
        case 12:  // SGE
            return 2;
        case 4:  // MAD
            return 3;
        default:
            return 0;
    }
}

inline bool unary_opcode(uint32_t opcode) { return opcode_arity(opcode) == 1; }

inline bool vector_offset_valid(uint8_t base, uint32_t offset) {
    if (offset % 4u) return false;
    switch (base) {
        case kVshBaseTemp: return offset <= (12u - 1u) * 4u;
        case kVshBaseOutput: return offset <= (13u - 1u) * 4u;
        case kVshBaseInput: return offset <= (16u - 1u) * 4u;
        case kVshBaseContext: return offset <= (192u - 1u) * 4u;
        case kVshBaseAddress: return offset == 0;
        default: return false;
    }
}

inline bool source_base_supported(uint8_t base) {
    return base == kVshBaseTemp || base == kVshBaseOutput ||
           base == kVshBaseInput || base == kVshBaseContext;
}

inline bool destination_base_supported(uint8_t base) {
    return base == kVshBaseTemp || base == kVshBaseOutput;
}

inline void validate_source_slot(const vsh_capture::Op& wire, unsigned slot,
                                 unsigned input_count) {
    const uint32_t base = wire.words[4 + slot];
    const uint32_t offset = wire.words[8 + slot];
    const uint32_t swizzle = wire.words[12 + slot];
    const bool used = slot < input_count;
    if (!used) {
        if (base != kVshBaseNone || offset != 0 || swizzle != 0)
            throw std::runtime_error("VSH capture has nonzero unused operand fields");
        return;
    }
    if (base > 0xFFu || !source_base_supported(static_cast<uint8_t>(base)) ||
        !vector_offset_valid(static_cast<uint8_t>(base), offset))
        throw std::runtime_error("VSH capture operand base or offset is unsupported");
    if (swizzle > 0xFFu)
        throw std::runtime_error("VSH capture operand swizzle is out of range");
}

inline void validate_destination_slot(const vsh_capture::Op& wire, unsigned slot) {
    const uint32_t base = wire.words[16 + slot];
    const uint32_t offset = wire.words[18 + slot];
    const uint32_t mask = wire.words[20 + slot];
    if (mask > 0xFu)
        throw std::runtime_error("VSH capture destination mask is out of range");
    if (base == kVshBaseNone) {
        if (offset != 0 || mask != 0)
            throw std::runtime_error("VSH capture disabled destination is not zeroed");
        return;
    }
    if (!mask)
        throw std::runtime_error("VSH capture enabled destination has zero mask");
    if (base > 0xFFu || !destination_base_supported(static_cast<uint8_t>(base)) ||
        !vector_offset_valid(static_cast<uint8_t>(base), offset))
        throw std::runtime_error("VSH capture destination base or offset is unsupported");
}

inline VshCompiledOp reconstruct_op(const vsh_capture::Op& wire,
                                    const vsh_capture::Op* next,
                                    std::size_t index, std::size_t op_count) {
    if (wire.words[11] || wire.words[15] || wire.words[22] || wire.words[23])
        throw std::runtime_error("VSH capture reserved fields are nonzero");

    const uint32_t opcode = wire.words[0];
    const uint32_t paired = wire.words[1];
    const uint32_t input_count = wire.words[2];
    const uint32_t negate_mask = wire.words[3];
    const uint32_t relative_mask = wire.words[7];
    const uint8_t arity = opcode_arity(opcode);
    VshCompiledOp::Fn fn = opcode_fn(opcode);
    if (!fn || !arity)
        throw std::runtime_error("VSH capture opcode is unsupported");
    if (paired > 1u)
        throw std::runtime_error("VSH capture paired flag is invalid");
    if (input_count != arity)
        throw std::runtime_error("VSH capture opcode arity does not match inputs");
    if (negate_mask & ~((1u << input_count) - 1u))
        throw std::runtime_error("VSH capture negate mask names unused operands");
    if (relative_mask)
        throw std::runtime_error("VSH capture relative operands are outside oracle scope");
    if (paired) {
        if (index + 1 >= op_count)
            throw std::runtime_error("VSH capture paired operation is missing its ILU half");
        if (!next || next->words[1] != 0 || !unary_opcode(next->words[0]) ||
            next->words[2] != 1)
            throw std::runtime_error("VSH capture paired operation has invalid ILU half");
    }

    for (unsigned slot = 0; slot < 3; ++slot)
        validate_source_slot(wire, slot, static_cast<unsigned>(input_count));
    validate_destination_slot(wire, 0);
    validate_destination_slot(wire, 1);

    VshCompiledOp op{};
    op.fn = fn;
    op.paired = static_cast<uint8_t>(paired);
    op.input_count = static_cast<uint8_t>(input_count);
    op.in_negate_mask = static_cast<uint8_t>(negate_mask);
    op.in_relative_mask = 0;
    for (unsigned slot = 0; slot < 3; ++slot) {
        op.in_base[slot] = static_cast<uint8_t>(wire.words[4 + slot]);
        op.in_offset[slot] = static_cast<uint16_t>(wire.words[8 + slot]);
        op.in_swizzle[slot] = static_cast<uint8_t>(wire.words[12 + slot]);
    }
    for (unsigned slot = 0; slot < 2; ++slot) {
        op.out_base[slot] = static_cast<uint8_t>(wire.words[16 + slot]);
        op.out_offset[slot] = static_cast<uint16_t>(wire.words[18 + slot]);
        op.out_mask[slot] = static_cast<uint8_t>(wire.words[20 + slot]);
    }
    return op;
}

inline void words_to_floats(float* dst, const uint32_t* src, std::size_t count) {
    std::memcpy(dst, src, count * sizeof(uint32_t));
}

inline bool any_nan(const float* values, unsigned count) {
    for (unsigned i = 0; i < count; ++i)
        if (std::isnan(values[i])) return true;
    return false;
}

inline std::array<uint32_t, 28> collect_consumed_outputs(const float* output) {
    std::array<uint32_t, 28> out{};
    std::size_t dst = 0;
    for (unsigned reg : vsh_capture::kConsumedOutputs) {
        std::memcpy(out.data() + dst, output + reg * 4u, 4u * sizeof(uint32_t));
        dst += 4;
    }
    return out;
}

inline uint32_t float_bits(float value) {
    uint32_t out = 0;
    std::memcpy(&out, &value, sizeof(out));
    return out;
}

inline vsh_capture::Op make_wire_op(uint32_t opcode, uint32_t arity,
                                    uint32_t out_base, uint32_t out_reg,
                                    uint32_t mask) {
    vsh_capture::Op op{};
    op.words[0] = opcode;
    op.words[2] = arity;
    op.words[4] = kVshBaseInput;
    op.words[5] = arity > 1 ? kVshBaseContext : kVshBaseNone;
    op.words[6] = arity > 2 ? kVshBaseTemp : kVshBaseNone;
    op.words[8] = 0;
    op.words[9] = 0;
    op.words[10] = 0;
    op.words[12] = 0xE4;
    op.words[13] = arity > 1 ? 0xE4 : 0;
    op.words[14] = arity > 2 ? 0xE4 : 0;
    op.words[16] = out_base;
    op.words[17] = kVshBaseNone;
    op.words[18] = out_reg * 4u;
    op.words[20] = mask;
    return op;
}

inline void set_word(std::array<uint32_t, 64>& regs, unsigned reg,
                     unsigned lane, uint32_t word) {
    regs[reg * 4u + lane] = word;
}

inline void set_word(std::array<uint32_t, 768>& regs, unsigned reg,
                     unsigned lane, uint32_t word) {
    regs[reg * 4u + lane] = word;
}

}  // namespace detail

struct NumericDeclineTrace {
    bool numeric_decline = false;
    std::array<uint32_t, 28> outputs{};
};

inline std::vector<VshCompiledOp> validate_and_compile(const vsh_capture::Vertex& vertex) {
    if (vertex.ops.empty() || vertex.ops.size() > vsh_capture::kMaxOps)
        throw std::runtime_error("VSH capture vertex has invalid op count");
    std::vector<VshCompiledOp> ops;
    ops.reserve(vertex.ops.size());
    for (std::size_t i = 0; i < vertex.ops.size(); ++i) {
        ops.push_back(detail::reconstruct_op(
            vertex.ops[i], i + 1 < vertex.ops.size() ? &vertex.ops[i + 1] : nullptr,
            i, vertex.ops.size()));
    }
    return ops;
}

inline NumericDeclineTrace trace_numeric_decline(const vsh_capture::Vertex& vertex) {
    std::vector<VshCompiledOp> ops = validate_and_compile(vertex);

    float input[16 * 4]{};
    float context[192 * 4]{};
    float output[13 * 4]{};
    float temp[12 * 4]{};
    float address[4]{};
    detail::words_to_floats(input, vertex.inputs.data(), vertex.inputs.size());
    detail::words_to_floats(context, vertex.constants.data(), vertex.constants.size());
    float* bases[kVshBaseCount] = {temp, output, input, context, address};

    bool numeric_decline = false;
    float mac_inputs[3 * 4];
    float ilu_input[4];
    float result[4];
    for (uint32_t i = 0; i < ops.size(); ++i) {
        const VshCompiledOp& op = ops[i];
        for (unsigned k = 0; k < op.input_count; ++k) {
            vsh_fetch_operand(&mac_inputs[k * 4], op, k, bases);
            numeric_decline |= detail::any_nan(&mac_inputs[k * 4], 4);
        }
        if (op.paired) {
            const VshCompiledOp& ilu = ops[i + 1];
            vsh_fetch_operand(ilu_input, ilu, 0, bases);
            numeric_decline |= detail::any_nan(ilu_input, 4);
            op.fn(result, mac_inputs);
            numeric_decline |= detail::any_nan(result, 4);
            vsh_store_operand(op, 0, bases, result);
            vsh_store_operand(op, 1, bases, result);
            ilu.fn(result, ilu_input);
            numeric_decline |= detail::any_nan(result, 4);
            vsh_store_operand(ilu, 0, bases, result);
            vsh_store_operand(ilu, 1, bases, result);
            ++i;
        } else {
            op.fn(result, mac_inputs);
            numeric_decline |= detail::any_nan(result, 4);
            vsh_store_operand(op, 0, bases, result);
            vsh_store_operand(op, 1, bases, result);
        }
    }
    return {numeric_decline, detail::collect_consumed_outputs(output)};
}

inline std::array<uint32_t, 28> validate_and_execute(const vsh_capture::Vertex& vertex) {
    const auto ops = validate_and_compile(vertex);
    float input[16 * 4]{};
    float context[192 * 4]{};
    float output[13 * 4]{};
    float temp[12 * 4]{};
    float address[4]{};
    detail::words_to_floats(input, vertex.inputs.data(), vertex.inputs.size());
    detail::words_to_floats(context, vertex.constants.data(), vertex.constants.size());
    float* bases[kVshBaseCount] = {temp, output, input, context, address};
    // Keep the actual executor independent of the instrumented trace walk.
    vsh_execute_compiled(ops.data(), static_cast<uint32_t>(ops.size()), bases);
    return detail::collect_consumed_outputs(output);
}

namespace detail {

inline void finalize_probe_case(vsh_capture::Vertex& vertex) {
    vertex.expected = validate_and_execute(vertex);
}

}  // namespace detail

inline std::vector<vsh_capture::Vertex> make_program_probe_cases() {
    std::vector<vsh_capture::Vertex> cases;

    {
        vsh_capture::Vertex vertex;
        vertex.ordinal = 1;
        vsh_capture::Op seed = detail::make_wire_op(7, 2, kVshBaseTemp, 0, 1);
        vsh_capture::Op mac = detail::make_wire_op(7, 2, kVshBaseTemp, 0, 1);
        mac.words[1] = 1;
        mac.words[8] = 1u * 4u;
        mac.words[9] = 1u * 4u;
        vsh_capture::Op ilu = detail::make_wire_op(16, 1, kVshBaseOutput, 0, 0xFu);
        ilu.words[4] = kVshBaseTemp;
        ilu.words[8] = 0;
        ilu.words[12] = 0xFF;
        vertex.ops = {seed, mac, ilu};
        for (unsigned lane = 0; lane < 4; ++lane) {
            detail::set_word(vertex.inputs, 0, lane, detail::float_bits(1.0f));
            detail::set_word(vertex.constants, 0, lane, detail::float_bits(1.0f));
            detail::set_word(vertex.inputs, 1, lane, detail::float_bits(2.0f));
            detail::set_word(vertex.constants, 1, lane, detail::float_bits(2.0f));
        }
        detail::finalize_probe_case(vertex);
        cases.push_back(vertex);
    }

    {
        vsh_capture::Vertex vertex;
        vertex.ordinal = 2;
        vsh_capture::Op mov = detail::make_wire_op(1, 1, kVshBaseOutput, 0, 0xFu);
        vsh_capture::Op mul = detail::make_wire_op(2, 2, kVshBaseOutput, 0, 0xEu);
        mul.words[4] = kVshBaseOutput;
        mul.words[5] = kVshBaseContext;
        mul.words[8] = 0;
        mul.words[9] = 2u * 4u;
        vertex.ops = {mov, mul};
        detail::set_word(vertex.inputs, 0, 0, detail::float_bits(1.5f));
        detail::set_word(vertex.inputs, 0, 1, detail::float_bits(-2.0f));
        detail::set_word(vertex.inputs, 0, 2, detail::float_bits(3.0f));
        detail::set_word(vertex.inputs, 0, 3, detail::float_bits(4.0f));
        for (unsigned lane = 0; lane < 4; ++lane)
            detail::set_word(vertex.constants, 2, lane, detail::float_bits(2.0f));
        detail::finalize_probe_case(vertex);
        cases.push_back(vertex);
    }

    {
        vsh_capture::Vertex vertex;
        vertex.ordinal = 3;
        vsh_capture::Op mul = detail::make_wire_op(2, 2, kVshBaseOutput, 3, 0xAu);
        mul.words[3] = 1;
        mul.words[8] = 4u * 4u;
        mul.words[9] = 5u * 4u;
        mul.words[12] = 0x1B;
        mul.words[13] = 0x00;
        vertex.ops = {mul};
        detail::set_word(vertex.inputs, 4, 0, detail::float_bits(1.0f));
        detail::set_word(vertex.inputs, 4, 1, detail::float_bits(2.0f));
        detail::set_word(vertex.inputs, 4, 2, detail::float_bits(3.0f));
        detail::set_word(vertex.inputs, 4, 3, detail::float_bits(4.0f));
        detail::set_word(vertex.constants, 5, 0, detail::float_bits(0.5f));
        detail::finalize_probe_case(vertex);
        cases.push_back(vertex);
    }

    {
        vsh_capture::Vertex vertex;
        vertex.ordinal = 4;
        vsh_capture::Op rsq = detail::make_wire_op(16, 1, kVshBaseOutput, 0, 0xFu);
        vsh_capture::Op rcc = detail::make_wire_op(15, 1, kVshBaseOutput, 3, 0xFu);
        rcc.words[8] = 1u * 4u;
        vertex.ops = {rsq, rcc};
        detail::set_word(vertex.inputs, 0, 0, detail::float_bits(4.0f));
        detail::set_word(vertex.inputs, 1, 0, 0x80000000u);
        detail::finalize_probe_case(vertex);
        cases.push_back(vertex);
    }

    {
        vsh_capture::Vertex vertex;
        vertex.ordinal = 5;
        vsh_capture::Op op = detail::make_wire_op(1, 1, kVshBaseOutput, 3, 8);
        op.words[17] = kVshBaseOutput;
        op.words[19] = 4u * 4u;
        op.words[21] = 4;
        vertex.ops = {op};
        detail::set_word(vertex.inputs, 0, 0, detail::float_bits(9.0f));
        detail::set_word(vertex.inputs, 0, 1, detail::float_bits(10.0f));
        detail::finalize_probe_case(vertex);
        cases.push_back(vertex);
    }

    {
        vsh_capture::Vertex vertex;
        vertex.ordinal = 6;
        vsh_capture::Op mac = detail::make_wire_op(1, 1, kVshBaseOutput, 0, 8);
        mac.words[1] = 1;
        vsh_capture::Op ilu = detail::make_wire_op(14, 1, kVshBaseOutput, 0, 8);
        vertex.ops = {mac, ilu};
        detail::set_word(vertex.inputs, 0, 0, detail::float_bits(4.0f));
        detail::finalize_probe_case(vertex);
        cases.push_back(vertex);
    }

    const auto add_minmax_case = [&](uint64_t ordinal, uint32_t opcode,
                                     uint32_t a, uint32_t b) {
        vsh_capture::Vertex vertex;
        vertex.ordinal = ordinal;
        vertex.ops = {detail::make_wire_op(opcode, 2, kVshBaseOutput, 3, 0xFu)};
        for (unsigned lane = 0; lane < 4; ++lane) {
            detail::set_word(vertex.inputs, 0, lane, a);
            detail::set_word(vertex.constants, 0, lane, b);
        }
        detail::finalize_probe_case(vertex);
        cases.push_back(vertex);
    };
    add_minmax_case(7, 9, 0x80000000u, 0u);
    add_minmax_case(8, 9, 0u, 0x80000000u);
    add_minmax_case(9, 10, 0x80000000u, 0u);
    add_minmax_case(10, 10, 0u, 0x80000000u);

    const auto add_compare_case = [&](uint64_t ordinal, uint32_t opcode,
                                      uint32_t a, uint32_t b) {
        vsh_capture::Vertex vertex;
        vertex.ordinal = ordinal;
        vertex.ops = {detail::make_wire_op(opcode, 2, kVshBaseOutput, 3, 0xFu)};
        for (unsigned lane = 0; lane < 4; ++lane) {
            detail::set_word(vertex.inputs, 0, lane, a);
            detail::set_word(vertex.constants, 0, lane, b);
        }
        detail::finalize_probe_case(vertex);
        cases.push_back(vertex);
    };
    add_compare_case(11, 11, 0x80000000u, 0u);
    add_compare_case(12, 11, 0u, 0x80000000u);
    add_compare_case(13, 12, 0x80000000u, 0u);
    add_compare_case(14, 12, 0u, 0x80000000u);

    {
        vsh_capture::Vertex vertex;
        vertex.ordinal = 15;
        vertex.ops = {detail::make_wire_op(6, 2, kVshBaseOutput, 3, 0xFu)};
        detail::set_word(vertex.inputs, 0, 0, detail::float_bits(2.0f));
        detail::set_word(vertex.inputs, 0, 1, detail::float_bits(3.0f));
        detail::set_word(vertex.inputs, 0, 2, detail::float_bits(4.0f));
        detail::set_word(vertex.inputs, 0, 3, detail::float_bits(99.0f));
        detail::set_word(vertex.constants, 0, 0, detail::float_bits(5.0f));
        detail::set_word(vertex.constants, 0, 1, detail::float_bits(6.0f));
        detail::set_word(vertex.constants, 0, 2, detail::float_bits(7.0f));
        detail::set_word(vertex.constants, 0, 3, detail::float_bits(8.0f));
        detail::finalize_probe_case(vertex);
        cases.push_back(vertex);
    }

    {
        vsh_capture::Vertex vertex;
        vertex.ordinal = 16;
        vertex.ops = {detail::make_wire_op(8, 2, kVshBaseOutput, 3, 0xFu)};
        detail::set_word(vertex.inputs, 0, 0, detail::float_bits(99.0f));
        detail::set_word(vertex.inputs, 0, 1, detail::float_bits(3.0f));
        detail::set_word(vertex.inputs, 0, 2, detail::float_bits(7.0f));
        detail::set_word(vertex.inputs, 0, 3, detail::float_bits(11.0f));
        detail::set_word(vertex.constants, 0, 0, detail::float_bits(13.0f));
        detail::set_word(vertex.constants, 0, 1, detail::float_bits(5.0f));
        detail::set_word(vertex.constants, 0, 2, detail::float_bits(17.0f));
        detail::set_word(vertex.constants, 0, 3, detail::float_bits(19.0f));
        detail::finalize_probe_case(vertex);
        cases.push_back(vertex);
    }

    {
        vsh_capture::Vertex vertex;
        vertex.ordinal = 17;
        vertex.ops = {detail::make_wire_op(4, 3, kVshBaseOutput, 3, 0xFu)};
        for (unsigned lane = 0; lane < 4; ++lane) {
            detail::set_word(vertex.inputs, 0, lane, 0x7f7fffffu);
            detail::set_word(vertex.constants, 0, lane, detail::float_bits(2.0f));
        }
        vertex.ops[0].words[6] = kVshBaseContext;
        vertex.ops[0].words[10] = 1u * 4u;
        vertex.ops[0].words[14] = 0xE4;
        for (unsigned lane = 0; lane < 4; ++lane)
            detail::set_word(vertex.constants, 1, lane, 0xff7fffffu);
        detail::finalize_probe_case(vertex);
        cases.push_back(vertex);
    }

    {
        vsh_capture::Vertex vertex;
        vertex.ordinal = 18;
        vertex.ops = {detail::make_wire_op(3, 2, kVshBaseOutput, 3, 0xFu)};
        detail::set_word(vertex.inputs, 0, 0, 0x7f800000u);
        detail::set_word(vertex.constants, 0, 0, 0xff800000u);
        detail::finalize_probe_case(vertex);
        cases.push_back(vertex);
    }

    {
        vsh_capture::Vertex vertex;
        vertex.ordinal = 19;
        vertex.ops = {detail::make_wire_op(19, 1, kVshBaseOutput, 3, 0xFu)};
        detail::set_word(vertex.inputs, 0, 0, detail::float_bits(2.0f));
        detail::set_word(vertex.inputs, 0, 1, detail::float_bits(4.0f));
        detail::set_word(vertex.inputs, 0, 2, detail::float_bits(0.0f));
        detail::set_word(vertex.inputs, 0, 3, detail::float_bits(3.0f));
        detail::finalize_probe_case(vertex);
        cases.push_back(vertex);
    }

    return cases;
}

}  // namespace xlle::vsh_capture_oracle
