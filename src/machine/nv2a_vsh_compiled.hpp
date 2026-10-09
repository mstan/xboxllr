// xlle — pre-decoded NV2A vertex-shader programs (beads-xbox.1.15.11).
//
// The vendored interpreter (`nv2a_vsh_emulator.c` from xemu's nv2a_vsh_cpu)
// decodes every operand of every step on every vertex: a switch on the operand
// type, an indexed swizzle array, a writemask switch per output half, and a
// walk over an `Nv2aVshStep` that is ~168 bytes wide because it carries the
// disassembler's full symbolic form. `beads-xbox.1.15.10` phase 5 measured what
// that costs on the canonical 12 B replay: 38 337 230 vertices at 118 ns each,
// 4.542 s, 62 % of the `nv2a_vertex` bucket, and NEITHER the parse cache nor
// the batch transform cache touches it.
//
// This header is the pre-decoded form the parse cache is compiled into once per
// distinct program. Every decode that is a pure function of the program is done
// at compile time; what is left per vertex is a linear walk of 32-byte records
// with resolved base pointers, packed swizzles and packed writemasks.
//
// EXACTNESS IS THE WHOLE POINT AND IT IS PRESERVED BY CONSTRUCTION:
//
//  * The arithmetic is NOT reimplemented. `fn` points at the SAME
//    `nv2a_vsh_cpu_*` function the vendored dispatch table points at, compiled
//    from the same source with the same flags, so every IEEE detail and every
//    NV2A special case (RCP/RCC/RSQ/EXP/LOG/LIT, `fix_inf_mult`, the -0 rule in
//    SLT/SGE) is literally the same object code, and no expression here can be
//    contracted into an FMA the vendored build did not already have. A
//    transcribed, inlined form was BUILT AND MEASURED and is slower; see the
//    note on `fn` below and PERF-METHODOLOGY section 3k.
//  * The operand fetch keeps the interpreter's `mult * in[swizzle[i]]` VERBATIM,
//    including the multiply by +1.0f when the operand is not negated. Replacing
//    that with a copy would be bit-identical for every finite value and for both
//    zeros but WOULD differ for a signalling NaN, which the multiply quiets and
//    a copy does not (`beads-xbox.1.15.11`, the sNaN question). The multiply is
//    one instruction; the win here is the removed DECODE, not removed arithmetic,
//    so the question is answered by not depending on the answer.
//  * The MAC/ILU pairing is preserved. `prepare_inputs` fetches BOTH halves of a
//    step before EITHER executes, so an ILU that reads a register its own step's
//    MAC writes sees the OLD value; `paired` marks that pairing and the executor
//    honours it.
//  * Anything the compiler cannot prove it can represent exactly -- an operand
//    or destination index outside the range the vendored code asserts, an opcode
//    outside the dispatch table, a program with no final step -- makes the whole
//    program DECLINE to compile, and the device runs the vendored interpreter for
//    it unchanged. There is no partial or approximate compiled form.
#ifndef XLLE_MACHINE_NV2A_VSH_COMPILED_HPP
#define XLLE_MACHINE_NV2A_VSH_COMPILED_HPP

#include <cmath>
#include <cstdint>
#include <cstring>
#include <math.h>

namespace xlle {

// Index into the base-pointer table handed to the executor. The order is fixed
// and is the ONLY place the mapping lives; the compiler and the executor both
// read it from here.
enum VshBase : uint8_t {
    kVshBaseTemp = 0,     // r0-r11
    kVshBaseOutput = 1,   // o0-o12 (also r12, which aliases oPos)
    kVshBaseInput = 2,    // v0-v15
    kVshBaseContext = 3,  // c0-c191
    kVshBaseAddress = 4,  // a0
    kVshBaseCount = 5,
    kVshBaseNone = 0xFF,  // this output slot is unused
};

// One pre-decoded operation: either the MAC half or the ILU half of a step.
// 32 bytes, so a 60-op program is under 2 kB and stays hot in L1 across the
// tens of millions of vertices a replay runs it for.
struct VshCompiledOp {
    using Fn = void (*)(float* out, const float* inputs);

    // The vendored `nv2a_vsh_cpu_*` implementation, reached by a POINTER and
    // not by a switch. That is a measurement, not a preference: transcribing
    // the twenty bodies into an inlined switch made the canonical 2 B window
    // 0.906 s -> 1.540 s, and the same switch forced out of line 1.346 s.
    // The vendored bodies are small, hot in the instruction cache and reached
    // through one indirect branch whose target sequence is identical on every
    // vertex, which the indirect predictor learns; a nineteen-way switch is a
    // second indirect branch on top of that and buys nothing. See the header
    // note and PERF-METHODOLOGY section 3k.
    Fn fn = nullptr;
    // Element offset of each operand within its base array. For a RELATIVE
    // context operand this holds the RAW context index instead, because the
    // final offset depends on a0 and is computed per fetch with the same
    // uint32_t arithmetic the interpreter uses.
    uint16_t in_offset[3] = {0, 0, 0};
    uint8_t in_base[3] = {kVshBaseNone, kVshBaseNone, kVshBaseNone};
    // Four 2-bit component selectors, x in bits 0-1 through w in bits 6-7.
    uint8_t in_swizzle[3] = {0, 0, 0};
    uint8_t in_negate_mask = 0;    // bit k: operand k is negated
    uint8_t in_relative_mask = 0;  // bit k: operand k is c[a0 + n]
    // Number of LEADING operands the vendored `prepare_inputs` would fetch: it
    // stops at the first NV2ART_NONE and leaves the rest of the input block
    // uninitialised, and so does this.
    uint8_t input_count = 0;
    // The NEXT op in the array is this step's ILU half and its operand must be
    // fetched BEFORE this op executes.
    uint8_t paired = 0;
    uint8_t out_base[2] = {kVshBaseNone, kVshBaseNone};
    uint8_t out_mask[2] = {0, 0};  // low bits: XYZW; bit7: scalar fog, bits5:4: source lane
    uint16_t out_offset[2] = {0, 0};
};

static_assert(sizeof(VshCompiledOp) <= 32,
              "the pre-decoded op must stay at 32 bytes: the whole lever is "
              "that a program's decode fits in L1 for 38 M vertices");

// Fetch one operand into `out[0..3]`. Transcribed from `fetch_value` +
// `set_register` in the vendored nv2a_vsh_emulator.c, with the type switch and
// the swizzle-array loads resolved at compile time and NOTHING else changed.
inline void vsh_fetch_operand(float* out, const VshCompiledOp& op, unsigned k,
                              float* const* bases) {
    const float* in;
    if (op.in_relative_mask & (1u << k)) {
        // The interpreter computes `offset = index; offset += (int)a0[0];` in
        // uint32_t and then indexes `context_regs[offset * 4]`, also in
        // uint32_t. Reproduced exactly, wrap included, rather than in ptrdiff_t.
        const uint32_t offset =
            static_cast<uint32_t>(op.in_offset[k]) +
            static_cast<uint32_t>(
                static_cast<int>(bases[kVshBaseAddress][0]));
        in = bases[kVshBaseContext] + offset * 4u;
    } else {
        in = bases[op.in_base[k]] + op.in_offset[k];
    }
    const unsigned swizzle = op.in_swizzle[k];
    // Kept as a multiply, not a conditional copy. See the sNaN note above.
    const float mult = (op.in_negate_mask & (1u << k)) ? -1.0f : 1.0f;
    out[0] = mult * in[swizzle & 3u];
    out[1] = mult * in[(swizzle >> 2) & 3u];
    out[2] = mult * in[(swizzle >> 4) & 3u];
    out[3] = mult * in[(swizzle >> 6) & 3u];
}

// Write one destination half. Transcribed from the output loop of
// `apply_operation`; a slot the compiler resolved to "no store happens" -- an
// unused output, a write to an input register (which the vendored code skips
// once asserts are off) or a zero writemask (which ARL's address destination
// always has) -- is kVshBaseNone and costs one predictable branch.
inline void vsh_store_operand(const VshCompiledOp& op, unsigned o,
                              float* const* bases, const float* result) {
    const uint8_t base = op.out_base[o];
    if (base == kVshBaseNone) return;
    float* dst = bases[base] + op.out_offset[o];
    const unsigned mask = op.out_mask[o];
    if (mask & 0x80u) {
        dst[0] = result[(mask >> 4) & 3u];
        return;
    }
    if (mask & 8u) dst[0] = result[0];
    if (mask & 4u) dst[1] = result[1];
    if (mask & 2u) dst[2] = result[2];
    if (mask & 1u) dst[3] = result[3];
}

// The result of the read-before-write analysis the compiled form makes
// possible (beads-xbox.1.15.11). The interpreter initializes 13 output
// registers, 12 temporaries and a0 -- 416 bytes -- on EVERY vertex, 38 337 230
// times per canonical run, because any of them can be read before the program
// writes it and their initial value is then observable. A straight-line program
// with no branches makes that question decidable exactly: walk the steps in
// order, and a register lane needs initialization only if an operand reads it
// before any operation writes it. The seven output registers the DEVICE reads back
// afterwards -- position, diffuse, specular and the four texture coordinates --
// are added unconditionally unless the program writes all four of their lanes.
// The scalar fog output is also consumed, requiring its X lane to be defined.
// The device's read is a read like any other.
//
// This is a pure removal of stores whose value is never read. It cannot change
// output, and `--nv2a-vsh-verify` checks that against the interpreter, which
// initializes every register, on the real programs. NVIDIA result-register
// defaults are (0,0,0,1); temporaries and the address register remain zero.
struct VshZeroPlan {
    uint16_t temp_mask = 0xFFF;    // r0-r11
    uint16_t output_mask = 0x1FFF;  // o0-o12
    bool address = true;            // a0
};

// Initialize exactly the registers the plan names. `output` is 13 * 4 floats,
// `temp` is 12 * 4, `address` is 4.
inline void vsh_apply_zero_plan(const VshZeroPlan& plan, float* output,
                                float* temp, float* address) {
    for (unsigned mask = plan.output_mask; mask; mask &= mask - 1u) {
        const unsigned index = static_cast<unsigned>(__builtin_ctz(mask));
        float* dst = output + index * 4;
        dst[0] = 0.0f;
        dst[1] = 0.0f;
        dst[2] = 0.0f;
        dst[3] = 1.0f;
    }
    for (unsigned mask = plan.temp_mask; mask; mask &= mask - 1u) {
        const unsigned index = static_cast<unsigned>(__builtin_ctz(mask));
        float* dst = temp + index * 4;
        dst[0] = 0.0f;
        dst[1] = 0.0f;
        dst[2] = 0.0f;
        dst[3] = 0.0f;
    }
    if (plan.address) {
        address[0] = 0.0f;
        address[1] = 0.0f;
        address[2] = 0.0f;
        address[3] = 0.0f;
    }
}

// The lanes an operand reads, as a 4-bit mask, from its packed swizzle.
inline unsigned vsh_swizzle_lane_mask(uint8_t swizzle) {
    return (1u << (swizzle & 3u)) | (1u << ((swizzle >> 2) & 3u)) |
           (1u << ((swizzle >> 4) & 3u)) | (1u << ((swizzle >> 6) & 3u));
}

// The lanes a destination writes, as a 4-bit mask, from its NV2A writemask
// (X = 8 is lane 0 ... W = 1 is lane 3).
inline unsigned vsh_writemask_lane_mask(uint8_t mask) {
    if (mask & 0x80u) return 1u;
    return ((mask >> 3) & 1u) | (((mask >> 2) & 1u) << 1) |
           (((mask >> 1) & 1u) << 2) | ((mask & 1u) << 3);
}

// Ceiling probe (beads-xbox.1.15.11), KNOWINGLY INCORRECT when armed and
// default off. `--nv2a-vsh-unsafe-probe 1` runs the operand fetch and the
// destination stores but not the operation; `2` runs the operand fetch only.
// It exists to bound, on the real programs and the real workload, how much of
// the per-op cost is the vendored call and how much is the operand traffic
// around it -- which is the number that decides whether an inlined op form is
// worth building. It is never a shipping path and has no --no- spelling.
enum : uint32_t {
    kVshProbeOff = 0,
    kVshProbeSkipOperation = 1,
    kVshProbeFetchOnly = 2,
};

// Run a compiled program. Replaces `nv2a_vsh_emu_execute` for programs the
// compiler accepted; the step walk, the is_final test, the NOP tests, the
// operand type switches and the destination type switches are all gone, and the
// order of every fetch, every call and every store is unchanged.
inline void vsh_execute_compiled(const VshCompiledOp* ops, uint32_t count,
                                 float* const* bases) {
    // Deliberately NOT zero-initialised, exactly like the vendored
    // `float mac_inputs[3 * 4]` / `float ilu_input[4]`: an op reads only the
    // operands its arity declares, and `input_count` is that arity.
    float mac_inputs[3 * 4];
    float ilu_input[4];
    float result[4];
    for (uint32_t i = 0; i < count; ++i) {
        const VshCompiledOp& op = ops[i];
        for (unsigned k = 0; k < op.input_count; ++k)
            vsh_fetch_operand(&mac_inputs[k * 4], op, k, bases);
        if (op.paired) {
            // Both halves of one step. The vendored `prepare_inputs` fetches
            // both operand sets before either operation runs so that an ILU
            // reading a register its own step's MAC writes sees the OLD value.
            const VshCompiledOp& ilu = ops[i + 1];
            vsh_fetch_operand(ilu_input, ilu, 0, bases);
            op.fn(result, mac_inputs);
            vsh_store_operand(op, 0, bases, result);
            vsh_store_operand(op, 1, bases, result);
            ilu.fn(result, ilu_input);
            vsh_store_operand(ilu, 0, bases, result);
            vsh_store_operand(ilu, 1, bases, result);
            ++i;
        } else {
            op.fn(result, mac_inputs);
            vsh_store_operand(op, 0, bases, result);
            vsh_store_operand(op, 1, bases, result);
        }
    }
}

// The armed form of the ceiling probe. A SEPARATE function, dispatched once per
// vertex, because carrying its branch inside the executor loop cost 0.906 ->
// 1.071 s on the canonical 2 B window -- an instrument that changes what it
// measures is not an instrument (PERF-METHODOLOGY section 2 rule 9).
inline void vsh_execute_compiled_probe(const VshCompiledOp* ops, uint32_t count,
                                       float* const* bases, uint32_t probe) {
    float mac_inputs[3 * 4];
    float ilu_input[4];
    for (uint32_t i = 0; i < count; ++i) {
        const VshCompiledOp& op = ops[i];
        for (unsigned k = 0; k < op.input_count; ++k)
            vsh_fetch_operand(&mac_inputs[k * 4], op, k, bases);
        if (probe == kVshProbeSkipOperation) {
            vsh_store_operand(op, 0, bases, mac_inputs);
            vsh_store_operand(op, 1, bases, mac_inputs);
        }
        if (op.paired) {
            const VshCompiledOp& ilu = ops[i + 1];
            vsh_fetch_operand(ilu_input, ilu, 0, bases);
            if (probe == kVshProbeSkipOperation) {
                vsh_store_operand(ilu, 0, bases, ilu_input);
                vsh_store_operand(ilu, 1, bases, ilu_input);
            }
            ++i;
        }
    }
}

}  // namespace xlle

#endif  // XLLE_MACHINE_NV2A_VSH_COMPILED_HPP
