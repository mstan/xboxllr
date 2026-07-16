// xlle — the A0 fixture corpus. Expected values computed by hand from the Intel SDM;
// each is a case a from-scratch interpreter would also have to satisfy, so the same corpus
// cross-validates whichever CPU floor the spike selects.
#include "spike/fixtures.hpp"

namespace xlle {

static Fixture mk(const char* name, std::vector<uint8_t> code, X86Regs init, uint64_t insns,
                  X86Regs expect, uint32_t reg_mask, uint32_t eflags_mask) {
    init.eip = kCodeBase;
    Fixture f;
    f.name = name;
    f.code = std::move(code);
    f.init = init;
    f.insns = insns;
    f.expect = expect;
    f.reg_mask = reg_mask;
    f.eflags_mask = eflags_mask;
    return f;
}

const std::vector<Fixture>& corpus() {
    static const std::vector<Fixture> c = [] {
        std::vector<Fixture> v;
        const uint32_t ALL6 = F_CF | F_PF | F_AF | F_ZF | F_SF | F_OF;

        // mov eax, 0x11223344      — no flags touched
        {
            X86Regs init{}; init.eflags = 0x2;
            X86Regs exp{}; exp.eax = 0x11223344; exp.eip = kCodeBase + 5;
            v.push_back(mk("mov_eax_imm", {0xB8, 0x44, 0x33, 0x22, 0x11}, init, 1, exp,
                          R_EAX | R_EIP, 0));
        }
        // add eax, ebx  (5 + 7 = 12) — PF set, rest clear
        {
            X86Regs init{}; init.eax = 5; init.ebx = 7; init.eflags = 0x2;
            X86Regs exp{}; exp.eax = 12; exp.eip = kCodeBase + 2; exp.eflags = F_PF;
            v.push_back(mk("add_eax_ebx", {0x01, 0xD8}, init, 1, exp, R_EAX | R_EIP, ALL6));
        }
        // sub eax, ebx  (5 - 7 = -2) — CF, AF, SF set
        {
            X86Regs init{}; init.eax = 5; init.ebx = 7; init.eflags = 0x2;
            X86Regs exp{}; exp.eax = 0xFFFFFFFE; exp.eip = kCodeBase + 2;
            exp.eflags = F_CF | F_AF | F_SF;
            v.push_back(mk("sub_eax_ebx", {0x29, 0xD8}, init, 1, exp, R_EAX | R_EIP, ALL6));
        }
        // xor eax, eax  — result 0; AF is UNDEFINED after XOR, so excluded from the mask
        {
            X86Regs init{}; init.eax = 0x1234; init.eflags = 0x2;
            X86Regs exp{}; exp.eax = 0; exp.eip = kCodeBase + 2; exp.eflags = F_ZF | F_PF;
            v.push_back(mk("xor_eax_eax", {0x31, 0xC0}, init, 1, exp, R_EAX | R_EIP,
                          ALL6 & ~F_AF));  // AF undefined -> not compared
        }
        // inc eax  (0xFF -> 0x100) — INC does NOT affect CF, so CF excluded
        {
            X86Regs init{}; init.eax = 0xFF; init.eflags = 0x2;
            X86Regs exp{}; exp.eax = 0x100; exp.eip = kCodeBase + 1; exp.eflags = F_AF | F_PF;
            v.push_back(mk("inc_eax", {0x40}, init, 1, exp, R_EAX | R_EIP, ALL6 & ~F_CF));
        }
        // mov ecx,3 ; add eax,ecx  (10 + 3 = 13) — two instructions, all flags clear
        {
            X86Regs init{}; init.eax = 10; init.eflags = 0x2;
            X86Regs exp{}; exp.eax = 13; exp.ecx = 3; exp.eip = kCodeBase + 7; exp.eflags = 0;
            v.push_back(mk("mov_add_two", {0xB9, 0x03, 0x00, 0x00, 0x00, 0x01, 0xC8}, init, 2,
                          exp, R_EAX | R_ECX | R_EIP, ALL6));
        }
        // mov [0x2000], eax  — exercises the memory-write ring
        {
            X86Regs init{}; init.eax = 0xDEADBEEF; init.eflags = 0x2;
            X86Regs exp{}; exp.eip = kCodeBase + 5;
            Fixture f = mk("mov_store_mem", {0xA3, 0x00, 0x20, 0x00, 0x00}, init, 1, exp,
                           R_EIP, 0);
            f.has_mem = true; f.mem_addr = kDataBase; f.mem_expect = 0xDEADBEEF;
            v.push_back(f);
        }

        // --- flag provenance & control flow ---
        // and eax, 0x0F0F  — AND clears CF/OF; AF is UNDEFINED (excluded from the mask)
        {
            X86Regs init{}; init.eax = 0xFFFF; init.eflags = 0x2;
            X86Regs exp{}; exp.eax = 0x0F0F; exp.eip = kCodeBase + 5; exp.eflags = F_PF;
            v.push_back(mk("and_eax_imm", {0x25, 0x0F, 0x0F, 0x00, 0x00}, init, 1, exp,
                          R_EAX | R_EIP, ALL6 & ~F_AF));
        }
        // shl eax, 1  (0x40000000 -> 0x80000000) — OF = MSB^CF = 1; AF UNDEFINED (excluded)
        {
            X86Regs init{}; init.eax = 0x40000000; init.eflags = 0x2;
            X86Regs exp{}; exp.eax = 0x80000000; exp.eip = kCodeBase + 2;
            exp.eflags = F_OF | F_SF | F_PF;
            v.push_back(mk("shl_eax_1", {0xD1, 0xE0}, init, 1, exp, R_EAX | R_EIP, ALL6 & ~F_AF));
        }
        // mul ebx  (0x10000 * 0x10000 = 0x1_00000000) — only CF/OF defined; SF/ZF/AF/PF UNDEF
        {
            X86Regs init{}; init.eax = 0x10000; init.ebx = 0x10000; init.eflags = 0x2;
            X86Regs exp{}; exp.eax = 0; exp.edx = 1; exp.eip = kCodeBase + 2;
            exp.eflags = F_CF | F_OF;
            v.push_back(mk("mul_ebx", {0xF7, 0xE3}, init, 1, exp, R_EAX | R_EDX | R_EIP,
                          F_CF | F_OF));
        }
        // cmp eax,eax ; je +5 (taken) — flags drive control flow; the mov is skipped
        {
            X86Regs init{}; init.eax = 0x1234; init.eflags = 0x2;
            X86Regs exp{}; exp.eax = 0x1234; exp.eip = kCodeBase + 9; exp.eflags = F_ZF | F_PF;
            v.push_back(mk("cmp_je_taken",
                          {0x39, 0xC0, 0x74, 0x05, 0xB8, 0xEF, 0xBE, 0xAD, 0xDE}, init, 2, exp,
                          R_EAX | R_EIP, ALL6));
        }
        // push eax ; pop ebx — stack + mem ring (a write then a read)
        {
            X86Regs init{}; init.eax = 0xCAFE; init.esp = 0x2800; init.eflags = 0x2;
            X86Regs exp{}; exp.ebx = 0xCAFE; exp.esp = 0x2800; exp.eip = kCodeBase + 2;
            v.push_back(mk("push_pop", {0x50, 0x5B}, init, 2, exp, R_EBX | R_ESP | R_EIP, 0));
        }
        // --- SSE: addps xmm0,xmm1  ({1,1,1,1}+{2,2,2,2}={3,3,3,3}) ---
        {
            X86Regs init{}; init.eflags = 0x2;
            X86Regs exp{}; exp.eip = kCodeBase + 3;
            Fixture f = mk("sse_addps", {0x0F, 0x58, 0xC1}, init, 1, exp, R_EIP, 0);
            f.ext_init = {
                {XReg::CR0, {0x11, 0x00, 0x00, 0x00}},   // PE|ET, EM=TS=0
                {XReg::CR4, {0x00, 0x06, 0x00, 0x00}},   // OSFXSR|OSXMMEXCPT
                {XReg::XMM0, {0,0,0x80,0x3F, 0,0,0x80,0x3F, 0,0,0x80,0x3F, 0,0,0x80,0x3F}},  // 1.0f
                {XReg::XMM1, {0,0,0,0x40, 0,0,0,0x40, 0,0,0,0x40, 0,0,0,0x40}},              // 2.0f
            };
            f.ext_check = {
                {XReg::XMM0, {0,0,0x40,0x40, 0,0,0x40,0x40, 0,0,0x40,0x40, 0,0,0x40,0x40}},  // 3.0f
            };
            v.push_back(f);
        }
        // --- x87: FLD1 -> ST0 = 1.0 (80-bit extended: mantissa 0x8..0, exp 0x3FFF) ---
        {
            X86Regs init{}; init.eflags = 0x2;
            X86Regs exp{}; exp.eip = kCodeBase + 2;
            Fixture f = mk("x87_fld1", {0xD9, 0xE8}, init, 1, exp, R_EIP, 0);
            f.ext_init = {{XReg::CR0, {0x11, 0x00, 0x00, 0x00}}};  // EM=0
            f.ext_check = {{XReg::ST0, {0,0,0,0,0,0,0,0x80, 0xFF,0x3F}}};  // 1.0
            v.push_back(f);
        }
        // --- x87: FLD1; FLD1; FADDP -> ST0 = 2.0 (exp 0x4000) ---
        {
            X86Regs init{}; init.eflags = 0x2;
            X86Regs exp{}; exp.eip = kCodeBase + 6;
            Fixture f = mk("x87_fadd_2", {0xD9, 0xE8, 0xD9, 0xE8, 0xDE, 0xC1}, init, 3, exp,
                           R_EIP, 0);
            f.ext_init = {{XReg::CR0, {0x11, 0x00, 0x00, 0x00}}};
            f.ext_check = {{XReg::ST0, {0,0,0,0,0,0,0,0x80, 0x00,0x40}}};  // 2.0
            v.push_back(f);
        }
        return v;
    }();
    return c;
}

}  // namespace xlle
