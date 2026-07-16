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
        return v;
    }();
    return c;
}

}  // namespace xlle
