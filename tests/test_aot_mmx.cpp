#include "aot_programs.h"
#include "cpu/unicorn_floor.hpp"
#include "machine/machine.hpp"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

using namespace xlle;
static void require(bool ok, const char* why) {
    if (!ok) { std::fprintf(stderr, "AOT MMX: %s\n", why); std::exit(1); }
}
static UnicornFloor& floor(XboxMachine& m) { return dynamic_cast<UnicornFloor&>(m.cpu()); }
static void run(XboxMachine& m, uint32_t pc, uint64_t count) {
    std::string error;
    const auto n = m.run(pc, count, error);
    require(error.empty(), error.c_str()); require(n == count, "exact retirement");
}
template<class T> static T get(CpuFloor& cpu, XReg reg) {
    T value{}; require(cpu.read_reg(reg, &value, sizeof(value)), "read extended register"); return value;
}
template<class T> static void put(CpuFloor& cpu, XReg reg, const T& value) {
    require(cpu.write_reg(reg, &value, sizeof(value)), "write extended register");
}
static constexpr XReg st_regs[] = {XReg::ST0, XReg::ST1, XReg::ST2, XReg::ST3,
                                   XReg::ST4, XReg::ST5, XReg::ST6, XReg::ST7};
static constexpr uint64_t input = 0x0123456789abcdefull;
struct Snapshot {
    std::array<std::array<uint8_t, 10>, 8> st;
    uint16_t control, status, tag;
    bool operator==(const Snapshot& b) const {
        return st == b.st && control == b.control && status == b.status && tag == b.tag;
    }
};
static Snapshot snapshot(CpuFloor& cpu) {
    Snapshot s;
    for (unsigned i = 0; i < 8; ++i) s.st[i] = get<std::array<uint8_t, 10>>(cpu, st_regs[i]);
    s.control = get<uint16_t>(cpu, XReg::FPCW); s.status = get<uint16_t>(cpu, XReg::FPSW);
    s.tag = get<uint16_t>(cpu, XReg::FPTAG); return s;
}
static void seed_fpu(CpuFloor& cpu) {
    put(cpu, XReg::FPSW, uint16_t(0));
    for (unsigned i = 0; i < 8; ++i) {
        std::array<uint8_t, 10> value{};
        for (unsigned b = 0; b < 8; ++b) value[b] = uint8_t(0x80 + i * 8 + b);
        value[8] = uint8_t(0xff - i); value[9] = 0x3f;
        put(cpu, st_regs[i], value);
    }
    put(cpu, XReg::FPCW, uint16_t(0x37f));
    put(cpu, XReg::FPSW, uint16_t(0x5900)); // Nonzero TOP and condition codes.
    put(cpu, XReg::FPTAG, uint16_t(0xffff));
}
static void prepare(XboxMachine& m, const xlle_aot_region& r, bool native,
                    uint32_t source = 0x2001, uint32_t destination = 0x3001) {
    require(m.ok() && m.init_ram(), "machine RAM");
    if (!native) floor(m).force_insn_scheduling();
    require(m.cpu().setup_flat_segments(0x8000), "flat segments");
    require(m.cpu().write_mem(r.base, r.code, r.code_size), "guest program");
    // Warm only code and data translations outside the declared native scope.
    // Both dwords are touched, including the second page of an unaligned qword.
    const uint8_t warm[] = {0x8b,6, 0x8b,0x46,4, 0x89,7, 0x89,0x47,4, 0xeb,0xfe};
    require(m.cpu().write_mem(0x1800, warm, sizeof(warm)), "warm program");
    X86Regs regs{}; regs.eip = 0x1800; regs.esp = 0x9000; regs.eflags = 2;
    regs.esi = source; regs.edi = destination; m.cpu().set_regs(regs);
    run(m, regs.eip, 25);
    seed_fpu(m.cpu());
    require(m.cpu().write_mem(source, &input, 8), "source pattern");
    const uint64_t output = 0xfedcba9876543210ull;
    require(m.cpu().write_mem(destination, &output, 8), "destination pattern");
    regs.eip = r.base; regs.eax = 0x12345678; regs.eflags = 0x8d7;
    m.cpu().set_regs(regs);
    if (native) {
        require(m.register_aot_region(r), "native registration");
        floor(m).require_native_execution(true);
    }
}
static void compare(const xlle_aot_region& r, uint64_t count, bool cross_page = false) {
    XboxMachine a, b;
    const uint32_t source = cross_page ? 0x2ffc : 0x2001;
    const uint32_t destination = cross_page ? 0x4ffc : 0x3001;
    prepare(a, r, true, source, destination); prepare(b, r, false, source, destination);
    const auto old = a.aot_stats(); const auto old_blocks = floor(a).block_stats();
    run(a, r.base, count); run(b, r.base, count);
    require(snapshot(a.cpu()) == snapshot(b.cpu()), "extended state matches corrected floor");
    auto ar = a.cpu().get_regs(), br = b.cpu().get_regs();
    require(ar.eip == br.eip && ar.eax == br.eax && ar.eflags == br.eflags &&
            ar.esi == br.esi && ar.edi == br.edi && ar.esp == br.esp, "integer state/flags preserved");
    uint64_t am = 0, bm = 0;
    require(a.cpu().read_mem(destination, &am, 8) && b.cpu().read_mem(destination, &bm, 8), "output read");
    require(am == bm, "all eight output bytes match");
    if (&r == &xlle_aot_mmx_cycle && count >= 3) require(am == input, "independent transferred qword");
    if (&r == &xlle_aot_mmx_load || (&r == &xlle_aot_mmx_cycle && count <= 3)) {
        const auto st = get<std::array<uint8_t, 10>>(a.cpu(), XReg::ST0);
        uint64_t low = 0; std::memcpy(&low, st.data(), 8);
        require(low == input && st[8] == 0xff && st[9] == 0xff, "independent MMX destination and exponent");
    }
    const auto now = a.aot_stats(); const auto blocks = floor(a).block_stats();
    require(now.retired - old.retired == count && now.fallback_insns == old.fallback_insns &&
            now.fallback_runs == old.fallback_runs, "strict scope executes every instruction natively");
    require(blocks.tb_entries == old_blocks.tb_entries && blocks.insn_mode_runs == old_blocks.insn_mode_runs &&
            blocks.block_mode_runs == old_blocks.block_mode_runs, "no hidden guest execution in helpers");
}
static void refusal(const xlle_aot_region& r, unsigned reason) {
    XboxMachine m;
    prepare(m, r, true, reason == 1 ? 0x2ff8 : 0x2001, reason == 1 ? 0x4ff8 : 0x3001);
    auto regs = m.cpu().get_regs();
    if (reason == 0) { regs.esi = regs.edi = 0x0e000000; m.cpu().set_regs(regs); }
    if (reason == 1) { regs.esi = 0x2ffc; regs.edi = 0x4ffc; m.cpu().set_regs(regs); } // first page warm, second cold
    if (reason == 2) put(m.cpu(), XReg::CR0, get<uint32_t>(m.cpu(), XReg::CR0) | 8u);
    if (reason == 3) put(m.cpu(), XReg::CR0, get<uint32_t>(m.cpu(), XReg::CR0) | 4u);
    if (reason == 4) {
        put(m.cpu(), XReg::FPCW, uint16_t(0x37e));
        put(m.cpu(), XReg::FPSW, uint16_t(0x1881));
    }
    const auto before = snapshot(m.cpu()); const auto old = m.aot_stats();
    const auto old_blocks = floor(m).block_stats();
    std::array<uint8_t, 16> bytes{}, after{};
    require(m.cpu().read_mem(0x4ff8, bytes.data(), bytes.size()), "cold span before");
    std::string error;
    require(m.run(r.base, 1, error) == 0 && error.find("native-required fallback refused") != std::string::npos,
            "unsupported memory/context refuses before retirement");
    require(snapshot(m.cpu()) == before, "decline preserves all inspected extended state");
    require(m.cpu().get_regs().eip == r.base, "decline preserves precise EIP");
    require(m.cpu().read_mem(0x4ff8, after.data(), after.size()) && bytes == after,
            "decline does not partially store qword");
    require(m.aot_stats().invocations > old.invocations, "decline reaches generated MMX operation");
    require(error.find(reason < 2 ? "reason=memory" : "reason=context") != std::string::npos,
            "specific MMX memory/availability rejection");
    require(m.aot_stats().fallback_insns == old.fallback_insns &&
            floor(m).block_stats().tb_entries == old_blocks.tb_entries, "refusal executes no guest fallback");
}

static void prefetch_contract() {
    for (uint32_t address : {0x2001u, 0x0e000000u, 0xffffffffu}) {
        XboxMachine m; prepare(m, xlle_aot_prefetch, true);
        auto regs = m.cpu().get_regs(); regs.esi = address; m.cpu().set_regs(regs);
        // Cache hints do not use the FPU, even if an x87 exception is pending.
        put(m.cpu(), XReg::FPCW, uint16_t(0x37e));
        put(m.cpu(), XReg::FPSW, uint16_t(0x1881));
        const auto fp = snapshot(m.cpu()); const auto old = m.aot_stats();
        const auto blocks = floor(m).block_stats();
        run(m, xlle_aot_prefetch.base, 4);
        require(snapshot(m.cpu()) == fp && m.cpu().get_regs().eflags == regs.eflags,
                "cache hints preserve FPU and flags");
        require(m.aot_stats().retired - old.retired == 4 &&
                m.aot_stats().fallback_insns == old.fallback_insns &&
                floor(m).block_stats().tb_entries == blocks.tb_entries,
                "all four locality hints are static operations");
        require(m.aot_stats().memory_reads == old.memory_reads &&
                m.aot_stats().memory_writes == old.memory_writes &&
                m.aot_stats().memory_declines == old.memory_declines,
                "prefetch does not read guest data or invent access faults");
    }
}

static void changed_code() {
    XboxMachine m;
    // Warm a physical alias of the code page without overwriting the region.
    prepare(m, xlle_aot_mmx_smc, true, 0x2001, 0x04001010);
    auto regs = m.cpu().get_regs(); regs.edi = 0x04001000; m.cpu().set_regs(regs);
    const auto old = m.aot_stats();
    std::string error;
    require(m.run(regs.eip, 2, error) == 1, "MMX code-alias store retires exactly once");
    require(error.find("reason=code-changed") != std::string::npos,
            "changed code cannot continue invisibly");
    uint64_t bytes = 0; require(m.cpu().read_mem(0x1000, &bytes, 8), "code alias readback");
    require(bytes == 0x8786858483828180ull, "full qword visible through physical code alias");
    require(m.aot_stats().fallback_insns == old.fallback_insns, "no stale fallback executes after code write");
}
int main() {
    for (const auto* r : {&xlle_aot_mmx_load, &xlle_aot_mmx_store, &xlle_aot_mmx_store_nt,
                          &xlle_aot_mmx_move, &xlle_aot_mmx_move_reverse, &xlle_aot_mmx_emms})
        compare(*r, 1);
    for (uint64_t prefix = 1; prefix <= 10; ++prefix) compare(xlle_aot_mmx_cycle, prefix);
    compare(xlle_aot_mmx_cycle, 100, true);
    for (const auto* r : {&xlle_aot_mmx_load, &xlle_aot_mmx_store, &xlle_aot_mmx_store_nt})
        for (unsigned reason = 0; reason < 5; ++reason) refusal(*r, reason);
    for (const auto* r : {&xlle_aot_mmx_move, &xlle_aot_mmx_emms})
        for (unsigned reason = 2; reason < 5; ++reason) refusal(*r, reason);
    prefetch_contract();
    changed_code();
    std::puts("AOT MMX: every prefix, qword/page spans, x87 alias state and precise declines pass with zero scope fallback");
}
