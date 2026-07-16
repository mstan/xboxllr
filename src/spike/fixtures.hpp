// xlle — x86 CPU-floor fixture corpus for the A0 spike.
//
// Each fixture is a tiny, hand-encoded IA-32 program with a known-correct final state.
// Flag checking honors provenance (PRINCIPLES §6): `eflags_mask` names ONLY the flags the
// instruction architecturally defines. Flags left undefined by the ISA (e.g. AF after a
// logical op) are excluded from the compare rather than canonicalized — a deliberate,
// documented policy, not a convenience.
#pragma once
#include <cstdint>
#include <vector>

#include "xlle/cpu_floor.hpp"

namespace xlle {

// Bit positions of the arithmetic EFLAGS we validate at A0.
enum : uint32_t {
    F_CF = 1u << 0,
    F_PF = 1u << 2,
    F_AF = 1u << 4,
    F_ZF = 1u << 6,
    F_SF = 1u << 7,
    F_OF = 1u << 11,
};

// Which GPR/EIP fields to compare.
enum : uint32_t {
    R_EAX = 1u << 0, R_ECX = 1u << 1, R_EDX = 1u << 2, R_EBX = 1u << 3,
    R_ESP = 1u << 4, R_EBP = 1u << 5, R_ESI = 1u << 6, R_EDI = 1u << 7,
    R_EIP = 1u << 8,
};

// Byte-level value of a wide/control register (for SSE/x87/CR setup + checks).
struct RegBytes {
    XReg reg;
    std::vector<uint8_t> bytes;  // little-endian, as the hardware stores it
};

struct Fixture {
    const char* name;
    std::vector<uint8_t> code;  // loaded at kCodeBase
    X86Regs init;
    uint64_t insns;             // instructions to execute
    X86Regs expect;             // expected final register state
    uint32_t reg_mask;          // which GPR/EIP fields to compare
    uint32_t eflags_mask;       // which EFLAGS bits are defined & compared

    // Optional single 32-bit memory check (exercises the mem ring).
    bool has_mem = false;
    uint32_t mem_addr = 0;
    uint32_t mem_expect = 0;

    // Wide/control registers written before run (e.g. CR4.OSFXSR, initial XMM/ST) and
    // checked after (independently-derived expected bytes — not echoed from the engine).
    std::vector<RegBytes> ext_init;
    std::vector<RegBytes> ext_check;
};

// Fixed layout for the spike's toy machine.
constexpr uint32_t kCodeBase = 0x1000;
constexpr uint32_t kDataBase = 0x2000;
constexpr uint32_t kMapSize = 0x2000;  // covers code + data

const std::vector<Fixture>& corpus();

}  // namespace xlle
