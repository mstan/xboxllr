#include "aot_x87_probe.h"
#include "../src/cpu/unicorn_metrics.h"
#include <unicorn/unicorn.h>
#include <unicorn/x86.h>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

static unsigned failures;
static void check(bool ok, const char* label) {
    if (!ok) { std::fprintf(stderr, "MMX floor: %s\n", label); ++failures; }
}
static void setup_ok(uc_err e) {
    if (e != UC_ERR_OK) { std::fprintf(stderr, "%s\n", uc_strerror(e)); std::exit(1); }
}
static uc_engine* create() {
    uc_engine* uc = nullptr;
    setup_ok(uc_open(UC_ARCH_X86, UC_MODE_32, &uc));
    setup_ok(uc_mem_map(uc, 0x1000, 0x2000, UC_PROT_ALL));
    uint32_t cw = 0x37f, sw = 0, tags = 0xffff, cr0 = 0;
    setup_ok(uc_reg_write(uc, UC_X86_REG_FPCW, &cw));
    setup_ok(uc_reg_write(uc, UC_X86_REG_FPSW, &sw));
    for (unsigned i = 0; i < 8; ++i) {
        std::array<uint8_t, 10> st{};
        for (unsigned b = 0; b < 8; ++b) st[b] = uint8_t(0x80 + i * 8 + b);
        st[8] = uint8_t(0xff - i); st[9] = 0x3f;
        setup_ok(uc_reg_write(uc, UC_X86_REG_ST0 + i, st.data()));
    }
    sw = 0x4100 | (3 << 11);
    setup_ok(uc_reg_write(uc, UC_X86_REG_FPSW, &sw));
    setup_ok(uc_reg_write(uc, UC_X86_REG_FPTAG, &tags));
    setup_ok(uc_reg_read(uc, UC_X86_REG_CR0, &cr0));
    cr0 |= 0x20; // Native x87 exception reporting (#MF).
    setup_ok(uc_reg_write(uc, UC_X86_REG_CR0, &cr0));
    return uc;
}
static xlle_x87_snapshot snapshot(uc_engine* uc) {
    xlle_x87_snapshot s{}; xlle_probe_x87_snapshot(uc, &s); return s;
}
static void equal(const xlle_x87_snapshot& a, const xlle_x87_snapshot& b, const char* why) {
    check(std::memcmp(&a, &b, sizeof(a)) == 0, why);
}
static uc_err run(uc_engine* uc, const std::vector<uint8_t>& code) {
    setup_ok(uc_mem_write(uc, 0x1000, code.data(), code.size()));
    return uc_emu_start(uc, 0x1000, 0x1000 + code.size(), 0, 1);
}
static void enter(xlle_x87_snapshot& s) {
    s.top = 0; for (auto& tag : s.tags) tag = 0;
}
static void interrupt(uc_engine* uc, uint32_t number, void* opaque) {
    *static_cast<uint32_t*>(opaque) = number;
    setup_ok(uc_emu_stop(uc));
}
int main() {
    xlle_guest_tsc_ticks = 0;
    const uint64_t value = 0x0123456789abcdefull;
    for (unsigned reg = 0; reg < 8; ++reg) {
        auto* uc = create(); auto expected = snapshot(uc);
        setup_ok(uc_mem_write(uc, 0x2001, &value, 8));
        check(run(uc, {0x0f, 0x6f, uint8_t(5 + reg * 8), 1, 0x20, 0, 0}) == UC_ERR_OK,
              "unaligned MOVQ load executes");
        enter(expected); expected.significand[reg] = value; expected.exponent[reg] = 0xffff;
        equal(snapshot(uc), expected, "MOVQ load destination exponent, physical register and tags");
        setup_ok(uc_close(uc));
    }
    for (unsigned opcode : {0x6fu, 0x7fu}) {
        auto* uc = create(); auto expected = snapshot(uc);
        // Both encodings move physical MM5 into physical MM2.
        check(run(uc, {0x0f, uint8_t(opcode), uint8_t(opcode == 0x6f ? 0xd5 : 0xea)}) == UC_ERR_OK,
              "MOVQ register executes");
        enter(expected); expected.significand[2] = expected.significand[5]; expected.exponent[2] = 0xffff;
        equal(snapshot(uc), expected, "MOVQ register direction and physical alias");
        setup_ok(uc_close(uc));
    }
    for (unsigned opcode : {0x7fu, 0xe7u}) {
        auto* uc = create(); auto expected = snapshot(uc);
        check(run(uc, {0x0f, uint8_t(opcode), 0x2d, 1, 0x20, 0, 0}) == UC_ERR_OK,
              "MOVQ/MOVNTQ store executes");
        uint64_t stored = 0; setup_ok(uc_mem_read(uc, 0x2001, &stored, 8));
        check(stored == expected.significand[5], "store uses physical MM5");
        enter(expected); equal(snapshot(uc), expected, "store tags/TOP without changing register bits");
        setup_ok(uc_close(uc));
    }
    {
        auto* uc = create(); auto expected = snapshot(uc);
        check(run(uc, {0x0f, 0x77}) == UC_ERR_OK, "EMMS executes");
        for (auto& tag : expected.tags) tag = 1;
        equal(snapshot(uc), expected, "EMMS empties tags and preserves TOP/data/control");
        setup_ok(uc_close(uc));
    }
    for (unsigned opcode : {0x6fu, 0x7fu, 0xe7u}) {
        auto* uc = create(); const auto before = snapshot(uc);
        const auto e = run(uc, {0x0f, uint8_t(opcode), 5, 0, 0x40, 0, 0});
        check(e == (opcode == 0x6f ? UC_ERR_READ_UNMAPPED : UC_ERR_WRITE_UNMAPPED),
              "unmapped transfer reports actual access fault");
        equal(snapshot(uc), before, "faulted transfer preserves all FPU state");
        setup_ok(uc_close(uc));
    }
    for (unsigned opcode : {0x6fu, 0x7fu, 0xe7u}) {
        auto* uc = create(); const auto before = snapshot(uc);
        const uint32_t sentinel = 0x13579bdf;
        setup_ok(uc_mem_write(uc, 0x2ffc, &sentinel, 4));
        // A genuine guest #PF, not a missing physical-bus implementation.
        // The next virtual page is absent; page-table walks must fault before
        // any portion of this ordinary store becomes visible.
        setup_ok(uc_mem_map(uc, 0x3000, 0x7000, UC_PROT_ALL));
        const uint32_t directory = 0x9003, present[] = {0x1003, 0x2003};
        setup_ok(uc_mem_write(uc, 0x8000, &directory, 4));
        setup_ok(uc_mem_write(uc, 0x9004, present, sizeof(present)));
        uint32_t cr3 = 0x8000, cr0 = 0, vector = 0;
        setup_ok(uc_reg_write(uc, UC_X86_REG_CR3, &cr3));
        setup_ok(uc_reg_read(uc, UC_X86_REG_CR0, &cr0));
        cr0 |= 0x80010001; setup_ok(uc_reg_write(uc, UC_X86_REG_CR0, &cr0));
        uc_hook hook; setup_ok(uc_hook_add(uc, &hook, UC_HOOK_INTR, (void*)interrupt, &vector, 1, 0));
        const auto e = run(uc, {0x0f, uint8_t(opcode), 5, 0xfc, 0x2f, 0, 0});
        check(e == UC_ERR_OK && vector == 14, "second-page transfer raises guest #PF");
        equal(snapshot(uc), before, "split-page fault preserves FPU state");
        uint32_t remaining = 0; setup_ok(uc_mem_read(uc, 0x2ffc, &remaining, 4));
        check(remaining == sentinel, "split-page store does not write its first half");
        setup_ok(uc_close(uc));
    }
    {
        auto* uc = create(); const auto before = snapshot(uc);
        check(run(uc, {0x0f, 0xe7, 0xc0}) == UC_ERR_INSN_INVALID, "MOVNTQ register encoding invalid");
        equal(snapshot(uc), before, "invalid encoding preserves FPU state");
        setup_ok(uc_close(uc));
    }
    for (unsigned opcode : {0x6fu, 0x7fu, 0xe7u, 0x77u}) {
        auto* uc = create(); uint32_t status = 0x1881, vector = 0;
        const uint16_t unmasked = 0x37e;
        setup_ok(uc_reg_write(uc, UC_X86_REG_FPCW, &unmasked));
        setup_ok(uc_reg_write(uc, UC_X86_REG_FPSW, &status));
        const auto before = snapshot(uc);
        uc_hook hook; setup_ok(uc_hook_add(uc, &hook, UC_HOOK_INTR, (void*)interrupt, &vector, 1, 0));
        std::vector<uint8_t> code = {0x0f, uint8_t(opcode)};
        if (opcode != 0x77) code.insert(code.end(), {5, 0, 0x40, 0, 0});
        const auto error = run(uc, code);
        check(error == UC_ERR_OK && vector == 16, "pending x87 exception takes priority over memory fault");
        equal(snapshot(uc), before, "pending exception preserves FPU state");
        setup_ok(uc_close(uc));
    }
    if (failures) { std::fprintf(stderr, "MMX floor: %u failed assertions\n", failures); return 1; }
    std::puts("MMX floor: physical register alias, exponents, tags/TOP, fault ordering and pending exceptions pass");
}
