// Differential qualification of the guest i386 interpreter (xlle_x86_interp)
// against the framework's pinned Unicorn x86 core.
//
// Every case runs one instruction from an identical randomized state in both
// engines and compares GPRs, defined EFLAGS, EIP, data/stack memory and the
// register file the instruction class owns. The interpreter executes at a
// nonzero host base (guest address 0 is NOT host address 0), so a missed
// guest->host translation in the core shows up as a mismatch or a crash.
//
// Known, documented model differences are excluded from comparison:
//  * x87 registers are doubles (as in generated native-title code): PC=64
//    arithmetic is not compared bit-exactly; PC=24/53 is.
//  * x87/MMX exception status flags and MXCSR sticky flags are reported, not
//    required (neither generated code nor the interpreter models them fully).
//  * MMX registers are separate from the x87 file (as in generated code).
//
// Built with XLR_X86_INTERP_REPLAY_ONLY (no Unicorn), the same deterministic
// case generator runs the interpreter alone and checks per-template digests of
// its outputs against tests/fixtures/x86_interp_digests.txt, which the
// Unicorn-qualified build writes. That proves every host compiler build
// (MSVC, GCC, Clang) of the interpreter is bit-identical on the corpus.
#include "xlle/x86_interp.h"
#ifndef XLR_X86_INTERP_REPLAY_ONLY
#include "../src/cpu/unicorn_metrics.h"
#include <unicorn/unicorn.h>
#include <unicorn/x86.h>
#endif
#include <fstream>
#include <sstream>

#include <cinttypes>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <vector>

namespace {

// Unicorn's x87 register image: 64-bit significand then 16-bit sign/exponent.
struct uc_x86_float80 { uint64_t mantissa; uint16_t exponent; };
#ifndef XLR_X86_INTERP_REPLAY_ONLY
void fp_write(uc_engine* uc, int i, const uc_x86_float80& f)
{
    uint8_t raw[10]; std::memcpy(raw, &f.mantissa, 8); std::memcpy(raw + 8, &f.exponent, 2);
    uc_reg_write(uc, UC_X86_REG_FP0 + i, raw);
}
uc_x86_float80 fp_read(uc_engine* uc, int i)
{
    uint8_t raw[10] = {}; uc_reg_read(uc, UC_X86_REG_FP0 + i, raw);
    uc_x86_float80 f; std::memcpy(&f.mantissa, raw, 8); std::memcpy(&f.exponent, raw + 8, 2); return f;
}
typedef uint8_t uc_x86_xmm[16];
#endif

constexpr uint32_t kCode = 0x00010000;
constexpr uint32_t kData = 0x00100000;
constexpr uint32_t kDataSize = 0x200;
constexpr uint32_t kStackTop = 0x00080000;
constexpr uint32_t kStackSize = 0x200;
constexpr uint32_t kGuestSize = 0x00200000;
constexpr uint32_t kRet = 0x00012000;  // address the code never reaches

enum Cls { INT, X87, MMX, SSE };
enum Flags : uint32_t { CF = 1, PF = 4, AF = 0x10, ZF = 0x40, SF = 0x80, OF = 0x800,
                        ALL = CF | PF | AF | ZF | SF | OF };

struct Tpl {
    const char* name;
    std::vector<uint8_t> op;
    char form;          // 'R' reg/reg, 'M' memory [ebx+disp8], 'N' no ModRM
    int digit;          // fixed ModRM.reg for group opcodes, -1 = random
    int imm;            // immediate bytes
    uint32_t undef;     // EFLAGS bits architecturally undefined after the op
    Cls cls;
    int pc = -1;        // x87: required precision control (-1 = any of 24/53)
    int fp_need = 0;    // x87: valid stack entries the op reads
    int fp_push = 0;    // x87: entries the op pushes
    int align = 4;      // memory operand alignment
    bool str = false;   // string op: ESI/EDI into data, small ECX
    uint16_t fsw = 0;   // x87 status bits defined after the op (C0-C3 are undefined after arithmetic; C1 round-up is not modelled)
};

std::vector<Tpl> templates()
{
    std::vector<Tpl> t;
    auto alu = [&](const char* n, uint8_t base) {
        t.push_back({n, {uint8_t(base + 1)}, 'R', -1, 0, 0, INT});
        t.push_back({n, {uint8_t(base + 1)}, 'M', -1, 0, 0, INT});
        t.push_back({n, {uint8_t(base + 3)}, 'M', -1, 0, 0, INT});
        t.push_back({n, {uint8_t(base + 0)}, 'R', -1, 0, 0, INT});
        t.push_back({n, {uint8_t(base + 2)}, 'M', -1, 0, 0, INT});
        t.push_back({n, {0x66, uint8_t(base + 1)}, 'R', -1, 0, 0, INT});
    };
    alu("add", 0x00); alu("or", 0x08); alu("adc", 0x10); alu("sbb", 0x18);
    alu("and", 0x20); alu("sub", 0x28); alu("xor", 0x30); alu("cmp", 0x38);
    for (int d = 0; d < 8; ++d) {
        t.push_back({"grp1 imm32", {0x81}, 'R', d, 4, 0, INT});
        t.push_back({"grp1 imm8", {0x83}, 'M', d, 1, 0, INT});
        t.push_back({"grp1 r8 imm8", {0x80}, 'R', d, 1, 0, INT});
    }
    t.push_back({"test", {0x85}, 'R', -1, 0, AF, INT});
    t.push_back({"test m8", {0x84}, 'M', -1, 0, AF, INT});
    t.push_back({"xchg", {0x87}, 'M', -1, 0, 0, INT});
    t.push_back({"mov", {0x89}, 'M', -1, 0, 0, INT});
    t.push_back({"mov r", {0x8B}, 'M', -1, 0, 0, INT});
    t.push_back({"mov r8", {0x8A}, 'M', -1, 0, 0, INT});
    t.push_back({"mov m16", {0x66, 0x89}, 'M', -1, 0, 0, INT});
    t.push_back({"lea", {0x8D}, 'M', -1, 0, 0, INT});
    t.push_back({"inc", {0xFF}, 'R', 0, 0, 0, INT});
    t.push_back({"dec m", {0xFF}, 'M', 1, 0, 0, INT});
    t.push_back({"not", {0xF7}, 'R', 2, 0, 0, INT});
    t.push_back({"neg", {0xF7}, 'M', 3, 0, 0, INT});
    t.push_back({"mul", {0xF7}, 'R', 4, 0, SF | ZF | AF | PF, INT});
    t.push_back({"imul1", {0xF7}, 'M', 5, 0, SF | ZF | AF | PF, INT});
    t.push_back({"imul2", {0x0F, 0xAF}, 'R', -1, 0, SF | ZF | AF | PF, INT});
    t.push_back({"imul3", {0x69}, 'R', -1, 4, SF | ZF | AF | PF, INT});
    t.push_back({"imul3b", {0x6B}, 'M', -1, 1, SF | ZF | AF | PF, INT});
    t.push_back({"mul8", {0xF6}, 'R', 4, 0, SF | ZF | AF | PF, INT});
    for (int d : {0, 1, 2, 3, 4, 5, 7}) {
        t.push_back({"shift imm", {0xC1}, 'R', d, 1, OF | AF, INT});
        t.push_back({"shift cl", {0xD3}, 'M', d, 0, OF | AF, INT});
        t.push_back({"shift 1", {0xD1}, 'R', d, 0, AF, INT});
        t.push_back({"shift8 cl", {0xD2}, 'R', d, 0, OF | AF, INT});
    }
    t.push_back({"shld imm", {0x0F, 0xA4}, 'R', -1, 1, OF | AF, INT});
    t.push_back({"shrd cl", {0x0F, 0xAD}, 'M', -1, 0, OF | AF, INT});
    t.push_back({"bt", {0x0F, 0xA3}, 'R', -1, 0, OF | SF | AF | PF | ZF, INT});
    t.push_back({"bts m", {0x0F, 0xAB}, 'M', -1, 0, OF | SF | AF | PF | ZF, INT});
    t.push_back({"btr imm", {0x0F, 0xBA}, 'R', 6, 1, OF | SF | AF | PF | ZF, INT});
    t.push_back({"btc imm", {0x0F, 0xBA}, 'M', 7, 1, OF | SF | AF | PF | ZF, INT});
    t.push_back({"bsf", {0x0F, 0xBC}, 'R', -1, 0, ALL & ~ZF, INT});
    t.push_back({"bsr", {0x0F, 0xBD}, 'M', -1, 0, ALL & ~ZF, INT});
    t.push_back({"movzx8", {0x0F, 0xB6}, 'R', -1, 0, 0, INT});
    t.push_back({"movzx16", {0x0F, 0xB7}, 'M', -1, 0, 0, INT});
    t.push_back({"movsx8", {0x0F, 0xBE}, 'M', -1, 0, 0, INT});
    t.push_back({"movsx16", {0x0F, 0xBF}, 'R', -1, 0, 0, INT});
    for (int c = 0; c < 16; ++c) {
        t.push_back({"setcc", {0x0F, uint8_t(0x90 + c)}, 'R', 0, 0, 0, INT});
        t.push_back({"cmovcc", {0x0F, uint8_t(0x40 + c)}, 'M', -1, 0, 0, INT});
        t.push_back({"jcc8", {uint8_t(0x70 + c)}, 'N', -1, 1, 0, INT});
    }
    t.push_back({"cmpxchg", {0x0F, 0xB1}, 'M', -1, 0, 0, INT});
    t.push_back({"lock cmpxchg", {0xF0, 0x0F, 0xB1}, 'M', -1, 0, 0, INT});
    t.push_back({"lock xadd", {0xF0, 0x0F, 0xC1}, 'M', -1, 0, 0, INT});
    t.push_back({"lock add", {0xF0, 0x01}, 'M', -1, 0, 0, INT});
    t.push_back({"lock dec", {0xF0, 0xFF}, 'M', 1, 0, 0, INT});
    t.push_back({"lock cmpxchg8b", {0xF0, 0x0F, 0xC7}, 'M', 1, 0, 0, INT});
    t.push_back({"bswap", {0x0F, 0xC8}, 'N', -1, 0, 0, INT});
    t.push_back({"cdq", {0x99}, 'N', -1, 0, 0, INT});
    t.push_back({"cwde", {0x98}, 'N', -1, 0, 0, INT});
    t.push_back({"lahf", {0x9F}, 'N', -1, 0, 0, INT});
    t.push_back({"sahf", {0x9E}, 'N', -1, 0, 0, INT});
    t.push_back({"push", {0x50}, 'N', -1, 0, 0, INT});
    t.push_back({"pop", {0x59}, 'N', -1, 0, 0, INT});
    t.push_back({"push m", {0xFF}, 'M', 6, 0, 0, INT});
    t.push_back({"pushad", {0x60}, 'N', -1, 0, 0, INT});
    t.push_back({"popad", {0x61}, 'N', -1, 0, 0, INT});
    t.push_back({"pushfd", {0x9C}, 'N', -1, 0, 0, INT});
    t.push_back({"leave", {0xC9}, 'N', -1, 0, 0, INT});
    t.push_back({"call rel", {0xE8}, 'N', -1, 4, 0, INT});
    t.push_back({"ret", {0xC3}, 'N', -1, 0, 0, INT});
    t.push_back({"ret imm", {0xC2}, 'N', -1, 2, 0, INT});
    t.push_back({"jmp rel8", {0xEB}, 'N', -1, 1, 0, INT});
    t.push_back({"div", {0xF7}, 'R', 6, 0, ALL, INT});
    t.push_back({"idiv m", {0xF7}, 'M', 7, 0, ALL, INT});
    t.push_back({"rep movsb", {0xF3, 0xA4}, 'N', -1, 0, 0, INT, -1, 0, 0, 4, true});
    t.push_back({"rep movsd", {0xF3, 0xA5}, 'N', -1, 0, 0, INT, -1, 0, 0, 4, true});
    t.push_back({"rep stosd", {0xF3, 0xAB}, 'N', -1, 0, 0, INT, -1, 0, 0, 4, true});
    t.push_back({"repe cmpsb", {0xF3, 0xA6}, 'N', -1, 0, 0, INT, -1, 0, 0, 4, true});
    t.push_back({"repne scasb", {0xF2, 0xAE}, 'N', -1, 0, 0, INT, -1, 0, 0, 4, true});
    t.push_back({"lodsd", {0xAD}, 'N', -1, 0, 0, INT, -1, 0, 0, 4, true});
    t.push_back({"movs m16", {0x66, 0xA5}, 'N', -1, 0, 0, INT, -1, 0, 0, 4, true});
    t.push_back({"xlat", {0xD7}, 'N', -1, 0, 0, INT});
    t.push_back({"mov moffs", {0xA1}, 'N', -1, 4, 0, INT});
    t.push_back({"mov moffs st", {0xA3}, 'N', -1, 4, 0, INT});

    // x87 (PC 24/53: results must match bit-exactly as doubles).
    auto fp = [&](const char* n, std::vector<uint8_t> op, char form, int digit, int need, int push, int align = 4, uint16_t fsw = 0) {
        Tpl x{n, op, form, digit, 0, 0, X87};
        x.fp_need = need; x.fp_push = push; x.align = align; x.fsw = fsw;
        // D8/DA/DC /2 /3 are FCOM/FCOMP/FICOM/FICOMP: C0, C2, C3 are the result.
        if ((op[0] == 0xD8 || op[0] == 0xDA || op[0] == 0xDC) && (digit == 2 || digit == 3)) x.fsw = 0x4500;
        t.push_back(x);
    };
    for (int d = 0; d < 8; ++d) {
        fp("fop m32", {0xD8}, 'M', d, 1, 0);
        fp("fop m64", {0xDC}, 'M', d, 1, 0, 8);
        fp("fop st(i)", {0xD8}, 'R', d, 8, 0);
        fp("fop m32int", {0xDA}, 'M', d, 1, 0);
    }
    fp("faddp", {0xDE}, 'R', 0, 8, 0);
    fp("fmulp", {0xDE}, 'R', 1, 8, 0);
    fp("fsubrp", {0xDE}, 'R', 4, 8, 0);
    fp("fdivp", {0xDE}, 'R', 7, 8, 0);
    fp("fld m32", {0xD9}, 'M', 0, 0, 1);
    fp("fld m64", {0xDD}, 'M', 0, 0, 1, 8);
    fp("fst m32", {0xD9}, 'M', 2, 1, 0);
    fp("fstp m64", {0xDD}, 'M', 3, 1, 0, 8);
    fp("fild m32", {0xDB}, 'M', 0, 0, 1);
    fp("fild m16", {0xDF}, 'M', 0, 0, 1);
    fp("fild m64", {0xDF}, 'M', 5, 0, 1, 8);
    fp("fistp m32", {0xDB}, 'M', 3, 1, 0);
    fp("fist m16", {0xDF}, 'M', 2, 1, 0);
    fp("fistp m64", {0xDF}, 'M', 7, 1, 0, 8);
    fp("fld st(i)", {0xD9}, 'R', 0, 8, 1);
    fp("fxch", {0xD9}, 'R', 1, 8, 0);
    fp("fstp st(i)", {0xDD}, 'R', 3, 8, 0);
    fp("fcom st(i)", {0xD8}, 'R', 2, 8, 0, 4, 0x4500);
    fp("fucompp", {0xDA, 0xE9}, 'N', -1, 2, 0, 4, 0x4500);
    fp("fcomi", {0xDB}, 'R', 6, 8, 0);
    fp("fucomip", {0xDF}, 'R', 5, 8, 0);
    fp("fchs", {0xD9, 0xE0}, 'N', -1, 1, 0);
    fp("fabs", {0xD9, 0xE1}, 'N', -1, 1, 0);
    fp("ftst", {0xD9, 0xE4}, 'N', -1, 1, 0, 4, 0x4500);
    fp("fld1", {0xD9, 0xE8}, 'N', -1, 0, 1);
    fp("fldz", {0xD9, 0xEE}, 'N', -1, 0, 1);
    fp("fsqrt", {0xD9, 0xFA}, 'N', -1, 1, 0);
    fp("frndint", {0xD9, 0xFC}, 'N', -1, 1, 0);
    fp("fscale", {0xD9, 0xFD}, 'N', -1, 2, 0);
    fp("fprem", {0xD9, 0xF8}, 'N', -1, 2, 0, 4, 0x4700);
    fp("fprem1", {0xD9, 0xF5}, 'N', -1, 2, 0, 4, 0x4700);
    fp("fnstsw ax", {0xDF, 0xE0}, 'N', -1, 0, 0);
    fp("fnstcw", {0xD9}, 'M', 7, 0, 0);
    fp("fldcw", {0xD9}, 'M', 5, 0, 0);
    // FCOMI/FUCOMIP: QEMU preserves OF/SF/AF, box64 clears them (as Bochs does).
    for (auto& x : t)
        if (x.cls == X87 && (!std::strcmp(x.name, "fcomi") || !std::strcmp(x.name, "fucomip"))) x.undef = OF | SF | AF;

    // MMX.
    auto mmx = [&](const char* n, std::vector<uint8_t> op, char form, int digit = -1, int imm = 0) {
        Tpl x{n, op, form, digit, imm, 0, MMX}; x.align = 8; t.push_back(x);
    };
    const uint8_t mmx_ops[] = {0xFC, 0xFD, 0xFE, 0xEC, 0xED, 0xDC, 0xDD, 0xF8, 0xF9, 0xFA, 0xE8, 0xE9,
                               0xD8, 0xD9, 0xD5, 0xE5, 0xF5, 0xDB, 0xDF, 0xEB, 0xEF, 0x74, 0x75, 0x76,
                               0x64, 0x65, 0x66, 0x63, 0x6B, 0x67, 0x60, 0x61, 0x62, 0x68, 0x69, 0x6A,
                               0xF1, 0xF2, 0xF3, 0xD1, 0xD2, 0xD3, 0xE1, 0xE2, 0xE0, 0xE3, 0xDA, 0xDE,
                               0xEA, 0xEE, 0xE4, 0xF6, 0xF7};
    for (uint8_t o : mmx_ops) {
        if (o == 0xF7) { mmx("maskmovq-skip", {0x0F, 0x6F}, 'R'); continue; }
        mmx("mmx op", {0x0F, o}, 'R');
        mmx("mmx op m", {0x0F, o}, 'M');
    }
    for (int d : {2, 4, 6}) {
        mmx("psxw imm", {0x0F, 0x71}, 'R', d, 1);
        mmx("psxd imm", {0x0F, 0x72}, 'R', d, 1);
    }
    mmx("psxq imm", {0x0F, 0x73}, 'R', 2, 1);
    mmx("psllq imm", {0x0F, 0x73}, 'R', 6, 1);
    mmx("pshufw", {0x0F, 0x70}, 'R', -1, 1);
    mmx("movq", {0x0F, 0x6F}, 'M');
    mmx("movq st", {0x0F, 0x7F}, 'M');
    mmx("movd", {0x0F, 0x6E}, 'M');
    mmx("movd st", {0x0F, 0x7E}, 'M');
    mmx("pinsrw", {0x0F, 0xC4}, 'M', -1, 1);
    mmx("pmovmskb", {0x0F, 0xD7}, 'R');
    mmx("pextrw", {0x0F, 0xC5}, 'R', -1, 1);
    mmx("cvttps2pi", {0x0F, 0x2C}, 'R');
    mmx("cvtps2pi", {0x0F, 0x2D}, 'R');

    // SSE (Pentium III set only).
    auto sse = [&](const char* n, std::vector<uint8_t> op, char form, int imm = 0, int align = 16) {
        Tpl x{n, op, form, -1, imm, 0, SSE}; x.align = align; t.push_back(x);
    };
    const uint8_t ps_ops[] = {0x58, 0x59, 0x5C, 0x5E, 0x5D, 0x5F, 0x51, 0x54, 0x55, 0x56, 0x57, 0x14, 0x15};
    for (uint8_t o : ps_ops) {
        sse("ps op", {0x0F, o}, 'R');
        sse("ps op m", {0x0F, o}, 'M');
        if ((o >= 0x54 && o <= 0x57) || o == 0x14 || o == 0x15) continue;  // no scalar form exists
        sse("ss op", {0xF3, 0x0F, o}, 'R');
        sse("ss op m", {0xF3, 0x0F, o}, 'M', 0, 4);
    }
    sse("movaps", {0x0F, 0x28}, 'M');
    sse("movaps st", {0x0F, 0x29}, 'M');
    sse("movups", {0x0F, 0x10}, 'M', 0, 4);
    sse("movss st", {0xF3, 0x0F, 0x11}, 'M', 0, 4);
    sse("movhlps", {0x0F, 0x12}, 'R');
    sse("movlps", {0x0F, 0x12}, 'M', 0, 8);
    sse("movhps st", {0x0F, 0x17}, 'M', 0, 8);
    sse("shufps", {0x0F, 0xC6}, 'R', 1);
    sse("cmpps", {0x0F, 0xC2}, 'M', 1);
    sse("cmpss", {0xF3, 0x0F, 0xC2}, 'R', 1);
    sse("comiss", {0x0F, 0x2F}, 'R');
    sse("ucomiss", {0x0F, 0x2E}, 'M', 0, 4);
    sse("cvtsi2ss", {0xF3, 0x0F, 0x2A}, 'M', 0, 4);
    sse("cvttss2si", {0xF3, 0x0F, 0x2C}, 'R');
    sse("cvtss2si", {0xF3, 0x0F, 0x2D}, 'M', 0, 4);
    sse("cvtpi2ps", {0x0F, 0x2A}, 'M', 0, 8);
    sse("movmskps", {0x0F, 0x50}, 'R');
    sse("stmxcsr", {0x0F, 0xAE}, 'M', 0, 4);
    for (auto& x : t)
        if (x.cls == SSE && !std::strcmp(x.name, "stmxcsr")) x.digit = 3;
    return t;
}

uint32_t pick32(std::mt19937& g)
{
    static const uint32_t special[] = {0, 1, 2, 0x7F, 0x80, 0xFF, 0x7FFF, 0x8000, 0xFFFF,
                                       0x7FFFFFFF, 0x80000000, 0xFFFFFFFF, 0xFFFFFFFE, 31, 32, 33};
    switch (g() % 4) {
    case 0: return special[g() % (sizeof(special) / sizeof(special[0]))];
    case 1: return g() & 0xFF;
    default: return g();
    }
}

double pickfp(std::mt19937& g, bool as_float)
{
    static const double special[] = {0.0, -0.0, 1.0, -1.0, 0.5, 2.0, 3.0, 1e10, -1e-10,
                                     INFINITY, -INFINITY, 1e30, 123456.75};
    double v;
    switch (g() % 6) {
    case 0: v = special[g() % (sizeof(special) / sizeof(special[0]))]; break;
    case 1: { uint64_t q = 0x7FF8000000000000ull; std::memcpy(&v, &q, 8); break; }  // default QNaN, independent of the C++ library
    // One RNG draw per statement: operand evaluation order is unspecified and
    // differs between compilers, which would change the generated corpus.
    case 2: { uint32_t a = g(); uint32_t b = g(); v = double(int32_t(a)) / double(1 + (b & 0xFFFF)); break; }
    default: { uint32_t a = g(); uint32_t b = g(); uint32_t c = g();
               v = std::ldexp(double(a) / 4294967296.0 + 0.5, int(b % 80) - 40) * ((c & 1) ? -1 : 1); }
    }
    return as_float ? double(float(v)) : v;
}

uc_x86_float80 to80(double d)
{
    uc_x86_float80 r{};
    uint64_t q;
    std::memcpy(&q, &d, 8);
    uint16_t sign = uint16_t((q >> 63) << 15);
    int e = int((q >> 52) & 0x7FF);
    uint64_t m = q & ((1ull << 52) - 1);
    if (e == 0 && m == 0) { r.exponent = sign; r.mantissa = 0; return r; }
    if (e == 0x7FF) { r.exponent = uint16_t(sign | 0x7FFF); r.mantissa = (1ull << 63) | (m << 11); return r; }
    if (e == 0) {  // binary64 subnormal -> normal extended
        int shift = 0;
        while (!(m & (1ull << 52))) { m <<= 1; ++shift; }
        e = 1 - shift;
    }
    r.exponent = uint16_t(sign | uint16_t(e - 1023 + 16383));
    r.mantissa = (m << 11) | (1ull << 63);
    return r;
}

double from80(const uc_x86_float80& f)
{
    int e = f.exponent & 0x7FFF;
    bool neg = (f.exponent & 0x8000) != 0;
    if (e == 0 && f.mantissa == 0) return neg ? -0.0 : 0.0;
    if (e == 0x7FFF) {
        if ((f.mantissa << 1) == 0) return neg ? -INFINITY : INFINITY;
        uint64_t q = (neg ? (1ull << 63) : 0) | (0x7FFull << 52) | ((f.mantissa >> 11) & ((1ull << 52) - 1));
        q |= 1ull << 51;
        double d; std::memcpy(&d, &q, 8); return d;
    }
    double d = std::ldexp(double(f.mantissa), e - 16383 - 63);
    return neg ? -d : d;
}

bool same_fp(double a, double b)
{
    if (std::isnan(a) && std::isnan(b)) return true;
    uint64_t qa, qb; std::memcpy(&qa, &a, 8); std::memcpy(&qb, &b, 8);
    return qa == qb;
}

#ifndef XLR_X86_INTERP_REPLAY_ONLY
void uc_ok(uc_err e, const char* what)
{
    if (e != UC_ERR_OK) { std::fprintf(stderr, "unicorn %s: %s\n", what, uc_strerror(e)); std::exit(2); }
}
#endif

void cpuid_hook(void*, uint32_t leaf, uint32_t, uint32_t out[4])
{
    // Coppermine Pentium III as the Xbox reports it.
    if (leaf == 0) { out[0] = 2; out[1] = 0x756E6547; out[2] = 0x6C65746E; out[3] = 0x49656E69; }
    else if (leaf == 1) { out[0] = 0x683; out[1] = 0; out[2] = 0; out[3] = 0x0383F9FF; }
    else { out[0] = out[1] = out[2] = out[3] = 0; }
}
uint64_t tsc_hook(void*) { return 733333333ull; }

struct Engines {
#ifndef XLR_X86_INTERP_REPLAY_ONLY
    uc_engine* uc = nullptr;
#endif
    std::vector<uint8_t> mem;  // interpreter guest space
    uintptr_t base = 0;
};

struct Stats { unsigned cases = 0, fails = 0, soft = 0; };

// FNV-1a over the interpreter's observable outputs, folded per template.
struct Digest {
    uint64_t h = 1469598103934665603ull;
    void add(const void* p, size_t n)
    {
        const uint8_t* b = static_cast<const uint8_t*>(p);
        for (size_t i = 0; i < n; ++i) { h ^= b[i]; h *= 1099511628211ull; }
    }
    template <class T> void put(const T& v) { add(&v, sizeof v); }
};
std::vector<Digest> g_digest;
size_t g_current = 0;
long g_dump = -1;  // --dump-template=N

std::string hex(const std::vector<uint8_t>& v)
{
    std::string s; char b[4];
    for (uint8_t x : v) { std::snprintf(b, sizeof b, "%02X ", x); s += b; }
    return s;
}

bool run_case(Engines& e, const Tpl& t, std::mt19937& g, Stats& st, bool verbose)
{
    // ---- encode ---------------------------------------------------------
    std::vector<uint8_t> code = t.op;
    int reg = t.digit >= 0 ? t.digit : int(g() % 8);
    if (t.form == 'R') {
        int rm = int(g() % 8);
        if (t.cls == INT && t.op.back() != 0x80 && t.op.back() != 0x84 && t.op.back() != 0x8A &&
            !(t.op.size() >= 2 && (t.op[t.op.size() - 2] == 0x0F) && (t.op.back() == 0xB6 || t.op.back() == 0xBE || (t.op.back() >= 0x90 && t.op.back() <= 0x9F)))) {
            if (rm == 4) rm = 1;
            if (t.digit < 0 && reg == 4) reg = 2;
        }
        if (t.cls == X87 && t.digit >= 0) rm = int(g() % 8);
        code.push_back(uint8_t(0xC0 | (reg << 3) | rm));
    } else if (t.form == 'M') {
        if (t.cls == INT && t.digit < 0 && reg == 4) reg = 2;
        if (t.cls == INT && t.digit < 0 && reg == 3) reg = 6;  // keep EBX as the base
        int disp = int(g() % (0x80 / t.align)) * t.align;
        code.push_back(uint8_t(0x40 | (reg << 3) | 3));
        code.push_back(uint8_t(disp));
    } else if (!std::strcmp(t.name, "bswap") || !std::strcmp(t.name, "push") || !std::strcmp(t.name, "pop")) {
        int r = int(g() % 8); if (r == 4) r = 0;
        code.back() = uint8_t(code.back() + r);
    }
    uint32_t immv = g();
    if (!std::strcmp(t.name, "mov moffs") || !std::strcmp(t.name, "mov moffs st"))
        immv = kData + (g() % 0x40) * 4;
    if (!std::strcmp(t.name, "call rel")) immv = 0x40;
    if (!std::strcmp(t.name, "cmpps") || !std::strcmp(t.name, "cmpss")) immv &= 7;
    if (!std::strcmp(t.name, "ret imm")) immv = (g() % 4) * 4;
    for (int i = 0; i < t.imm; ++i) code.push_back(uint8_t(immv >> (8 * i)));
    code.push_back(0x90);

    // ---- state -----------------------------------------------------------
    XlleX86State s{};
    for (int i = 0; i < 8; ++i) s.gpr[i] = pick32(g);
    s.gpr[XLLE_X86_EBX] = kData;
    s.gpr[XLLE_X86_ESP] = kStackTop - 0x100 + (g() % 0x20) * 4;
    s.gpr[XLLE_X86_EBP] = kStackTop - 0x80;
    if (t.str) {
        s.gpr[XLLE_X86_ESI] = kData + (g() % 0x10) * 4;
        s.gpr[XLLE_X86_EDI] = kData + 0x100 + (g() % 0x10) * 4;
        s.gpr[XLLE_X86_ECX] = g() % 17;
    }
    if (!std::strcmp(t.name, "xlat")) s.gpr[XLLE_X86_EBX] = kData;
    // BT/BTS/BTR/BTC with a register bit offset address +-256 MiB around the
    // operand; keep the offset inside the data block (both engines would fault
    // identically outside it, but the host buffer here is only the test space).
    if (t.form == 'M' && t.digit < 0 && t.op.size() >= 2 && t.op[t.op.size() - 2] == 0x0F &&
        (t.op.back() == 0xA3 || t.op.back() == 0xAB || t.op.back() == 0xB3 || t.op.back() == 0xBB))
        s.gpr[reg] = g() % 1024;
    if (!std::strcmp(t.name, "div") || !std::strcmp(t.name, "idiv m")) {
        s.gpr[XLLE_X86_EDX] = g() % 4;  // keep the quotient representable most of the time
    }
    s.eflags = 0x202 | (g() & (CF | PF | AF | ZF | SF | OF));
    s.eip = kCode;
    s.fs_base = 0;
    s.fpu_cw = 0x037F;
    s.mxcsr = 0x1F80 | ((g() % 4) << 13);
    bool pc24 = (g() & 1) != 0;
    if (t.cls == X87) {
        s.fpu_cw = uint16_t(pc24 ? 0x007F : 0x027F) | uint16_t((g() % 4) << 10);
        if (!std::strcmp(t.name, "frndint") || !std::strcmp(t.name, "fistp m32") ||
            !std::strcmp(t.name, "fist m16") || !std::strcmp(t.name, "fistp m64")) { /* RC matters: keep random */ }
        int depth = t.fp_need == 8 ? 8 - t.fp_push : t.fp_need + int(g() % (8 - t.fp_need - t.fp_push + 1));
        s.fp_top = g() % 8;
        s.fpu_tag = 0;
        for (int i = 0; i < 8; ++i) s.fp[i] = 0.0;
        for (int i = 0; i < depth; ++i) {
            unsigned phys = (s.fp_top + unsigned(i)) & 7u;
            s.fp[phys] = pickfp(g, pc24);
            s.fpu_tag |= uint16_t(1u << phys);
        }
    }
    if (t.cls == X87 && !std::strncmp(t.name, "fprem", 5)) {
        // Unicorn 2.1.1's FPREM is QEMU 5's double approximation (x - y*q):
        // exact only where y*q is exact. Small quarter-integers keep it exact.
        for (int i = 0; i < 2; ++i) {
            unsigned phys = (s.fp_top + unsigned(i)) & 7u;
            double v = double(int32_t(g() % (i ? 2048 : 2000000)) - (i ? 1024 : 1000000)) / 4.0;
            if (i && v == 0.0) v = 0.75;
            s.fp[phys] = v;
        }
    }
    for (int i = 0; i < 8; ++i) {
        for (int k = 0; k < 4; ++k) {
            float f = float(pickfp(g, true));
            uint32_t u; std::memcpy(&u, &f, 4);
            if (g() % 8 == 0) u = g();
            std::memcpy(&s.xmm[i][k * 4], &u, 4);
        }
        uint64_t hi = g(); uint64_t lo = g();
        s.mm[i] = (hi << 32) | lo;
    }

    std::vector<uint8_t> data(kDataSize), stack(kStackSize);
    for (auto& b : data) b = uint8_t(g());
    if (t.cls == X87 || t.cls == SSE) {
        for (uint32_t off = 0; off + 8 <= kDataSize; off += 8) {
            if (g() & 1) { double d = pickfp(g, pc24); std::memcpy(&data[off], &d, 8); }
            else { float f = float(pickfp(g, true)); std::memcpy(&data[off], &f, 4); }
        }
        if (!std::strcmp(t.name, "fldcw")) {
            for (uint32_t off = 0; off < kDataSize; off += 2) {
                uint16_t cw = uint16_t((pc24 ? 0x007F : 0x027F) | ((g() % 4) << 10));
                std::memcpy(&data[off], &cw, 2);
            }
        }
    }
    for (auto& b : stack) b = uint8_t(g());
    if (!std::strcmp(t.name, "ret") || !std::strcmp(t.name, "ret imm") || !std::strcmp(t.name, "leave")) {
        uint32_t ra = kCode + 0x100;
        uint32_t esp = !std::strcmp(t.name, "leave") ? s.gpr[XLLE_X86_EBP] + 4 : s.gpr[XLLE_X86_ESP];
        std::memcpy(&stack[esp - (kStackTop - kStackSize)], &ra, 4);
    }
    if (!std::strcmp(t.name, "popad")) {
        // keep the popped ESP slot irrelevant (POPAD discards it) - nothing to do
    }

    // ---- load both -------------------------------------------------------
    std::fill(e.mem.begin(), e.mem.end(), 0);
    std::memcpy(&e.mem[kCode], code.data(), code.size());
    std::memcpy(&e.mem[kData], data.data(), data.size());
    std::memcpy(&e.mem[kStackTop - kStackSize], stack.data(), stack.size());
#ifndef XLR_X86_INTERP_REPLAY_ONLY
    uc_ok(uc_mem_write(e.uc, kCode, code.data(), code.size()), "code");
    // Unicorn keeps translated blocks across uc_mem_write; drop the stale code block.
    uc_ok(uc_ctl_remove_cache(e.uc, kCode, kCode + 0x100), "remove cache");
    uc_ok(uc_mem_write(e.uc, kData, data.data(), data.size()), "data");
    uc_ok(uc_mem_write(e.uc, kStackTop - kStackSize, stack.data(), stack.size()), "stack");
    static const int gpr_ids[8] = {UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_EBX,
                                   UC_X86_REG_ESP, UC_X86_REG_EBP, UC_X86_REG_ESI, UC_X86_REG_EDI};
    for (int i = 0; i < 8; ++i) uc_ok(uc_reg_write(e.uc, gpr_ids[i], &s.gpr[i]), "gpr");
    uc_ok(uc_reg_write(e.uc, UC_X86_REG_EFLAGS, &s.eflags), "eflags");
    uint16_t cw = s.fpu_cw, sw = uint16_t((s.fp_top & 7u) << 11), tag = 0;
    for (int i = 0; i < 8; ++i) if (!((s.fpu_tag >> i) & 1)) tag = uint16_t(tag | (3u << (2 * i)));
    uc_ok(uc_reg_write(e.uc, UC_X86_REG_FPCW, &cw), "fpcw");
    uc_ok(uc_reg_write(e.uc, UC_X86_REG_FPSW, &sw), "fpsw");
    for (int i = 0; i < 8; ++i) {
        uc_x86_float80 f = to80(s.fp[i]);
        if (t.cls == MMX) { f.mantissa = s.mm[i]; f.exponent = 0xFFFF; }
        fp_write(e.uc, i, f);
    }
    if (t.cls == MMX) tag = 0;
    uc_ok(uc_reg_write(e.uc, UC_X86_REG_FPTAG, &tag), "fptag");
    for (int i = 0; i < 8; ++i) {
        uc_x86_xmm x; std::memcpy(x, s.xmm[i], 16);
        uc_ok(uc_reg_write(e.uc, UC_X86_REG_XMM0 + i, x), "xmm");
    }
    uc_ok(uc_reg_write(e.uc, UC_X86_REG_MXCSR, &s.mxcsr), "mxcsr");

    // ---- run both --------------------------------------------------------
    XlleX86State ref_in = s;
    // QEMU retires each REP iteration separately: run REP forms to their end address.
    bool rep = t.str && (t.op[0] == 0xF3 || t.op[0] == 0xF2);
    uc_err ue = rep ? uc_emu_start(e.uc, kCode, kCode + uint32_t(code.size() - 1), 0, 0)
                    : uc_emu_start(e.uc, kCode, kRet, 0, 1);
#else
    XlleX86State ref_in = s;
#endif
    XlleX86Hooks h{nullptr, nullptr, cpuid_hook, tsc_hook};
    XlleX86Result r = xlle_x86_run(&s, &h, e.base, 0, 0xFFFFFFFFu, 1);
    ++st.cases;
    {
        Digest& d = g_digest[g_current];
        uint32_t stop = uint32_t(r.stop), fault = uint32_t(r.fault);
        d.put(stop); d.put(fault); d.put(r.retired); d.put(r.fault_eip);
        d.add(s.gpr, sizeof s.gpr); d.put(s.eip); d.put(s.eflags); d.put(s.fs_base);
        d.add(s.fp, sizeof s.fp); d.put(s.fp_top); d.put(s.fpu_cw); d.put(s.fpu_sw); d.put(s.fpu_tag);
        d.add(s.xmm, sizeof s.xmm); d.put(s.mxcsr); d.add(s.mm, sizeof s.mm);
        d.add(&e.mem[kData], kDataSize); d.add(&e.mem[kStackTop - kStackSize], kStackSize);
        if (g_dump == long(g_current)) {
            // One line per case: compare two builds' dumps to locate a divergence.
            std::printf("case %u [%s] cw=%04X mx=%08X in_st0=%a in_st1=%a out_st0=%a sw=%04X fault=%d edata=%016llx\n",
                        st.cases, hex(code).c_str(), ref_in.fpu_cw, ref_in.mxcsr, ref_in.fp[ref_in.fp_top & 7],
                        ref_in.fp[(ref_in.fp_top + 1) & 7], s.fp[s.fp_top & 7], s.fpu_sw, r.fault,
                        (unsigned long long)d.h);
        }
    }
#ifdef XLR_X86_INTERP_REPLAY_ONLY
    (void)verbose; (void)ref_in;
    return true;
#else

    bool ref_fault = ue != UC_ERR_OK;
    bool our_fault = r.stop == XLLE_X86_STOP_FAULT;
    if (!ref_fault && our_fault) {
        // With an instruction-count limit Unicorn can swallow an exception and
        // return OK with EIP still on the faulting instruction.
        uint32_t ueip = 0; uc_reg_read(e.uc, UC_X86_REG_EIP, &ueip);
        if (ueip == kCode) ref_fault = true;
    }
    std::string why;
    if (ref_fault != our_fault) {
        why = std::string("fault mismatch: unicorn=") + uc_strerror(ue) + " interp=" +
              (our_fault ? std::to_string(r.fault) : std::string("none"));
    }
    if (why.empty() && !ref_fault) {
        if (r.stop != XLLE_X86_STOP_LIMIT || r.retired != 1) why = "interp did not retire exactly one instruction";
        uint32_t v;
        for (int i = 0; i < 8 && why.empty(); ++i) {
            uc_reg_read(e.uc, gpr_ids[i], &v);
            if (v != s.gpr[i]) {
                char b[96]; std::snprintf(b, sizeof b, "gpr%d unicorn=%08X interp=%08X", i, v, s.gpr[i]); why = b;
            }
        }
        uc_reg_read(e.uc, UC_X86_REG_EIP, &v);
        if (why.empty() && v != s.eip) {
            char b[96]; std::snprintf(b, sizeof b, "eip unicorn=%08X interp=%08X", v, s.eip); why = b;
        }
        uc_reg_read(e.uc, UC_X86_REG_EFLAGS, &v);
        uint32_t mask = ALL & ~t.undef;
        if (why.empty() && (v & mask) != (s.eflags & mask)) {
            char b[96]; std::snprintf(b, sizeof b, "eflags unicorn=%08X interp=%08X mask=%03X", v, s.eflags, mask); why = b;
        }
        std::vector<uint8_t> a(kDataSize);
        uc_mem_read(e.uc, kData, a.data(), a.size());
        if (why.empty() && std::memcmp(a.data(), &e.mem[kData], kDataSize)) {
            for (uint32_t i = 0; i < kDataSize; ++i)
                if (a[i] != e.mem[kData + i]) {
                    char b[96]; std::snprintf(b, sizeof b, "data[%03X] unicorn=%02X interp=%02X", i, a[i], e.mem[kData + i]); why = b; break;
                }
        }
        std::vector<uint8_t> sk(kStackSize);
        uc_mem_read(e.uc, kStackTop - kStackSize, sk.data(), sk.size());
        if (why.empty() && std::memcmp(sk.data(), &e.mem[kStackTop - kStackSize], kStackSize)) why = "stack memory differs";
        if (why.empty() && t.cls == X87) {
            uint16_t usw = 0, utag = 0; uc_reg_read(e.uc, UC_X86_REG_FPSW, &usw); uc_reg_read(e.uc, UC_X86_REG_FPTAG, &utag);
            if (((usw >> 11) & 7) != s.fp_top) why = "fp TOP differs";
            if (why.empty() && (usw & t.fsw) != (s.fpu_sw & t.fsw)) {
                char b[96]; std::snprintf(b, sizeof b, "fpsw C-bits unicorn=%04X interp=%04X", usw, s.fpu_sw); why = b;
            }
            if ((usw & 0x3F) != (s.fpu_sw & 0x3F)) ++st.soft;
            uint16_t ucw = 0; uc_reg_read(e.uc, UC_X86_REG_FPCW, &ucw);
            if (why.empty() && (ucw & 0xFFFF) != s.fpu_cw) why = "fpcw differs";
            for (int i = 0; i < 8 && why.empty(); ++i) {
                bool uvalid = ((utag >> (2 * i)) & 3) != 3;
                bool ivalid = (s.fpu_tag >> i) & 1;
                if (uvalid != ivalid) { why = "fp tag differs"; break; }
                if (!uvalid) continue;
                uc_x86_float80 f = fp_read(e.uc, i);
                double ud = from80(f);
                int ue2 = f.exponent & 0x7FFF;
                if (ue2 != 0x7FFF && ue2 != 0 && (ue2 > 16383 + 1023 || ue2 < 16383 - 1074)) {
                    ++st.soft;  // extended-range value: not representable in the double-backed model
                    continue;
                }
                if (!same_fp(ud, s.fp[i]) && !std::isnan(ud)) {
                    uc_x86_float80 back = to80(ud);
                    bool exact = back.mantissa == f.mantissa && back.exponent == f.exponent;
                    if (!exact && (s.fp[i] == std::nextafter(ud, INFINITY) || s.fp[i] == std::nextafter(ud, -INFINITY) ||
                                   (ud == 0.0 && std::fabs(s.fp[i]) <= 4.9406564584124654e-324))) {
                        ++st.soft;  // the exact extended value has more precision than a double holds
                        continue;
                    }
                }
                if (ud == 0.0 && s.fp[i] == 0.0 && !std::strncmp(t.name, "fprem", 5)) {
                    ++st.soft;  // SDM: a zero remainder takes the dividend's sign; QEMU 5 returns +0
                    continue;
                }
                if (!same_fp(ud, s.fp[i])) {
                    char b[128]; std::snprintf(b, sizeof b, "fp%d unicorn=%a interp=%a", i, ud, s.fp[i]); why = b;
                }
            }
        }
        if (why.empty() && t.cls == MMX) {
            for (int i = 0; i < 8 && why.empty(); ++i) {
                uc_x86_float80 f = fp_read(e.uc, i);
                if (f.mantissa != s.mm[i]) {
                    char b[96]; std::snprintf(b, sizeof b, "mm%d unicorn=%016" PRIX64 " interp=%016" PRIX64, i, f.mantissa, s.mm[i]); why = b;
                }
            }
        }
        if (why.empty() && (t.cls == SSE || t.cls == MMX)) {
            for (int i = 0; i < 8 && why.empty(); ++i) {
                uc_x86_xmm x = {}; uc_reg_read(e.uc, UC_X86_REG_XMM0 + i, x);
                if (std::memcmp(x, s.xmm[i], 16) && t.cls == SSE) {
                    // QEMU 5 propagates the larger-significand NaN (the x87 rule)
                    // for SSE; hardware returns the first source operand's NaN.
                    uint32_t a[4], c[4]; std::memcpy(a, x, 16); std::memcpy(c, s.xmm[i], 16);
                    bool only_nan = true;
                    for (int k = 0; k < 4; ++k)
                        if (a[k] != c[k] && !(((a[k] & 0x7F800000u) == 0x7F800000u && (a[k] & 0x7FFFFFu)) &&
                                              ((c[k] & 0x7F800000u) == 0x7F800000u && (c[k] & 0x7FFFFFu)))) only_nan = false;
                    if (only_nan) { ++st.soft; continue; }
                }
                if (std::memcmp(x, s.xmm[i], 16)) {
                    uint32_t a[4], c[4], in[4]; std::memcpy(a, x, 16); std::memcpy(c, s.xmm[i], 16); std::memcpy(in, ref_in.xmm[i], 16);
                    char b[200]; std::snprintf(b, sizeof b, "xmm%d unicorn=%08X %08X %08X %08X interp=%08X %08X %08X %08X in=%08X %08X %08X %08X",
                        i, a[0], a[1], a[2], a[3], c[0], c[1], c[2], c[3], in[0], in[1], in[2], in[3]); why = b;
                }
            }
            uint32_t umx = 0; uc_reg_read(e.uc, UC_X86_REG_MXCSR, &umx);
            if (why.empty() && (umx & 0xFFC0) != (s.mxcsr & 0xFFC0)) why = "mxcsr control differs";
            if ((umx & 0x3F) != (s.mxcsr & 0x3F)) ++st.soft;
        }
    }
    if (!why.empty()) {
        ++st.fails;
        if (verbose)
            std::fprintf(stderr, "FAIL %-14s [%s] eflags_in=%08X cw=%04X: %s\n", t.name, hex(code).c_str(),
                         ref_in.eflags, ref_in.fpu_cw, why.c_str());
        return false;
    }
    return true;
#endif
}

// Directed checks that the differential harness cannot express.
int directed(Engines& e)
{
    int fails = 0;
    auto check = [&](bool ok, const char* what) { if (!ok) { std::fprintf(stderr, "FAIL directed: %s\n", what); ++fails; } };
    XlleX86Hooks h{nullptr, nullptr, cpuid_hook, tsc_hook};

    // FS-relative access reads guest fs_base + offset.
    {
        std::fill(e.mem.begin(), e.mem.end(), 0);
        const uint8_t code[] = {0x64, 0xA1, 0x10, 0, 0, 0, 0x64, 0x8B, 0x0D, 0x18, 0, 0, 0, 0x90};
        std::memcpy(&e.mem[kCode], code, sizeof code);
        uint32_t a = 0x11223344, b = 0x55667788;
        std::memcpy(&e.mem[kData + 0x10], &a, 4); std::memcpy(&e.mem[kData + 0x18], &b, 4);
        XlleX86State s{}; s.eip = kCode; s.eflags = 0x202; s.fs_base = kData; s.gpr[XLLE_X86_ESP] = kStackTop - 0x40;
        s.fpu_cw = 0x37F; s.mxcsr = 0x1F80;
        XlleX86Result r = xlle_x86_run(&s, &h, e.base, 0, 0xFFFFFFFFu, 2);
        check(r.stop == XLLE_X86_STOP_LIMIT && s.gpr[0] == a && s.gpr[1] == b, "fs-relative loads use fs_base");
    }
    // CPUID and RDTSC go to the hooks.
    {
        std::fill(e.mem.begin(), e.mem.end(), 0);
        const uint8_t code[] = {0x0F, 0xA2, 0x89, 0xC6, 0x0F, 0x31, 0x90};
        std::memcpy(&e.mem[kCode], code, sizeof code);
        XlleX86State s{}; s.eip = kCode; s.eflags = 0x202; s.gpr[0] = 1; s.gpr[XLLE_X86_ESP] = kStackTop - 0x40;
        s.fpu_cw = 0x37F; s.mxcsr = 0x1F80;
        XlleX86Result r = xlle_x86_run(&s, &h, e.base, 0, 0xFFFFFFFFu, 3);
        check(r.stop == XLLE_X86_STOP_LIMIT && s.gpr[XLLE_X86_ESI] == 0x683 && s.gpr[0] == 733333333u && s.gpr[2] == 0,
              "cpuid/rdtsc hooks");
    }
    // RETURNED stops once the entry frame pops its return address; LEAVE stops at a hooked EIP.
    {
        std::fill(e.mem.begin(), e.mem.end(), 0);
        // call +5 (to 0x1000A); ret ; 0x1000A: mov eax,7 ; ret
        const uint8_t code[] = {0xE8, 0x05, 0, 0, 0, 0xC3, 0x90, 0x90, 0x90, 0x90, 0xB8, 7, 0, 0, 0, 0xC3};
        std::memcpy(&e.mem[kCode], code, sizeof code);
        uint32_t ra = 0xCAFE0000; uint32_t esp = kStackTop - 0x40;
        std::memcpy(&e.mem[esp], &ra, 4);
        XlleX86State s{}; s.eip = kCode; s.eflags = 0x202; s.gpr[XLLE_X86_ESP] = esp; s.fpu_cw = 0x37F; s.mxcsr = 0x1F80;
        XlleX86Result r = xlle_x86_run(&s, &h, e.base, 0, esp, 0);
        check(r.stop == XLLE_X86_STOP_RETURNED && s.eip == ra && s.gpr[0] == 7 && s.gpr[XLLE_X86_ESP] == esp + 4 && r.retired == 4,
              "return_esp stop after the entry frame returns");
        struct Leave { static int at(void* u, uint32_t eip) { return eip == *(uint32_t*)u; } };
        uint32_t target = kCode + 10;
        XlleX86Hooks lh{&target, Leave::at, cpuid_hook, tsc_hook};
        std::memcpy(&e.mem[esp], &ra, 4);
        XlleX86State s2{}; s2.eip = kCode; s2.eflags = 0x202; s2.gpr[XLLE_X86_ESP] = esp; s2.fpu_cw = 0x37F; s2.mxcsr = 0x1F80;
        r = xlle_x86_run(&s2, &lh, e.base, 0, esp, 0);
        check(r.stop == XLLE_X86_STOP_LEAVE && s2.eip == target && s2.gpr[XLLE_X86_ESP] == esp - 4 &&
              *(uint32_t*)&e.mem[esp - 4] == kCode + 5, "leave_at stops before a hooked call target");
        // Resuming at the hooked EIP does not immediately stop again.
        r = xlle_x86_run(&s2, &lh, e.base, 0, esp, 0);
        check(r.stop == XLLE_X86_STOP_RETURNED && s2.eip == ra && s2.gpr[0] == 7, "resume at a leave_at address");
    }
    // Faults are reported, not delivered: UD2, INT3, privileged HLT, divide error, P3-absent RDTSCP.
    {
        struct F { std::vector<uint8_t> code; int fault; const char* what; };
        const F faults[] = {
            {{0x0F, 0x0B}, XLLE_X86_FAULT_UD, "ud2"},
            {{0xCC}, XLLE_X86_FAULT_BP, "int3"},
            {{0xF4}, XLLE_X86_FAULT_GP, "hlt"},
            {{0x31, 0xC9, 0xF7, 0xF1}, XLLE_X86_FAULT_DE, "div by zero"},
            {{0xC4, 0xE2, 0x79, 0x18, 0xC0}, XLLE_X86_FAULT_UD, "vex"},
        };
        for (const F& f : faults) {
            std::fill(e.mem.begin(), e.mem.end(), 0);
            std::memcpy(&e.mem[kCode], f.code.data(), f.code.size());
            XlleX86State s{}; s.eip = kCode; s.eflags = 0x202; s.gpr[XLLE_X86_ESP] = kStackTop - 0x40;
            s.fpu_cw = 0x37F; s.mxcsr = 0x1F80;
            XlleX86Result r = xlle_x86_run(&s, &h, e.base, 0, 0xFFFFFFFFu, 4);
            if (!(r.stop == XLLE_X86_STOP_FAULT && r.fault == f.fault))
                std::fprintf(stderr, "  %s: stop=%d fault=%d eip=%08X\n", f.what, r.stop, r.fault, r.fault_eip);
            check(r.stop == XLLE_X86_STOP_FAULT && r.fault == f.fault, f.what);
        }
    }
    return fails;
}

}  // namespace

int main(int argc, char** argv)
{
#ifndef XLR_X86_INTERP_REPLAY_ONLY
    xlle_guest_tsc_ticks = 0;
#endif
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::setvbuf(stderr, nullptr, _IONBF, 0);
    std::vector<std::string> pos;
    std::string write_digests, check_digests;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a.rfind("--write-digests=", 0) == 0) write_digests = a.substr(16);
        else if (a.rfind("--check-digests=", 0) == 0) check_digests = a.substr(16);
        else if (a.rfind("--dump-template=", 0) == 0) g_dump = std::strtol(a.c_str() + 16, nullptr, 10);
        else pos.push_back(a);
    }
    unsigned iterations = pos.size() > 0 ? unsigned(std::strtoul(pos[0].c_str(), nullptr, 10)) : 200;
    unsigned seed = pos.size() > 1 ? unsigned(std::strtoul(pos[1].c_str(), nullptr, 10)) : 20261117u;
    const bool trace = std::getenv("X86_INTERP_TRACE") != nullptr || (pos.size() > 2 && pos[2] == "trace");
    bool verbose = true;

    Engines e;
    e.mem.assign(kGuestSize, 0);
    e.base = reinterpret_cast<uintptr_t>(e.mem.data());
#ifndef XLR_X86_INTERP_REPLAY_ONLY
    uc_ok(uc_open(UC_ARCH_X86, UC_MODE_32, &e.uc), "open");
    uc_ok(uc_mem_map(e.uc, 0, kGuestSize, UC_PROT_ALL), "map");
    // Enable SSE in Unicorn (CR4.OSFXSR, CR0.EM=0/MP=1) as the Xbox kernel does.
    uint64_t cr0 = 0x00000033, cr4 = 0x00000200;
    uc_reg_write(e.uc, UC_X86_REG_CR0, &cr0);
    uc_reg_write(e.uc, UC_X86_REG_CR4, &cr4);
#endif

    if (trace) std::fprintf(stderr, "directed\n");
    int fails = directed(e);
    std::printf("directed: %s\n", fails ? "FAIL" : "pass");

    std::mt19937 g(seed);
    auto tpls = templates();
    Stats st;
    std::vector<unsigned> per(tpls.size(), 0);
    g_digest.assign(tpls.size(), Digest{});
    for (unsigned it = 0; it < iterations; ++it) {
        for (size_t k = 0; k < tpls.size(); ++k) {
            g_current = k;
            if (trace) std::fprintf(stderr, "case %u.%zu %s\n", it, k, tpls[k].name);
            if (!std::strstr(tpls[k].name, "-skip") && !run_case(e, tpls[k], g, st, verbose && per[k] < 3))
                ++per[k];
        }
    }
    std::printf("differential: %u cases, %u mismatches, %u soft (status-flag-only) differences, %zu templates, seed %u\n",
                st.cases, st.fails, st.soft, tpls.size(), seed);
#ifndef XLR_X86_INTERP_REPLAY_ONLY
    uc_close(e.uc);
#endif
    int digest_fails = 0;
    if (!write_digests.empty()) {
        std::ofstream out(write_digests, std::ios::binary);
        out << "# xlle_x86_interp output digests: iterations " << iterations << " seed " << seed
            << " core " << xlle_x86_interp_identity() << "\n";
        for (size_t k = 0; k < tpls.size(); ++k) {
            char b[32]; std::snprintf(b, sizeof b, "%016llx", (unsigned long long)g_digest[k].h);
            out << k << " " << b << " " << tpls[k].name << "\n";
        }
        std::printf("digests written: %s\n", write_digests.c_str());
    }
    if (!check_digests.empty()) {
        std::ifstream in(check_digests, std::ios::binary);
        if (!in) { std::fprintf(stderr, "cannot read %s\n", check_digests.c_str()); return 2; }
        std::string line;
        size_t seen = 0;
        const std::string want_hdr = "iterations " + std::to_string(iterations) + " seed " + std::to_string(seed) + " ";
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty()) continue;
            if (line[0] == '#') {
                if (line.find(want_hdr) == std::string::npos) {
                    std::fprintf(stderr, "digest file was recorded with different iterations/seed: %s\n", line.c_str());
                    return 2;
                }
                continue;
            }
            std::istringstream ls(line);
            size_t k = 0;
            std::string hx;
            ls >> k >> hx;
            if (k >= tpls.size()) { ++digest_fails; continue; }
            ++seen;
            unsigned long long want = std::strtoull(hx.c_str(), nullptr, 16);
            if (want != g_digest[k].h) {
                ++digest_fails;
                if (digest_fails <= 20) std::fprintf(stderr, "DIGEST MISMATCH template %zu %s\n", k, tpls[k].name);
            }
        }
        if (seen != tpls.size()) {
            std::fprintf(stderr, "digest file covers %zu of %zu templates\n", seen, tpls.size());
            ++digest_fails;
        }
        std::printf("digest check: %s (%zu templates)\n", digest_fails ? "FAIL" : "pass", seen);
    }
    return (fails || st.fails || digest_fails) ? 1 : 0;
}
