#include "aot_programs.h"
#include "cpu/unicorn_floor.hpp"
#include "machine/machine.hpp"
#include <cstdio>
#include <cstdlib>
#include <string>

using namespace xlle;
static void require(bool ok, const char* reason) {
    if (!ok) { std::fprintf(stderr, "native-required: %s\n", reason); std::exit(1); }
}
static UnicornFloor& floor(XboxMachine& m) {
    auto* cpu = dynamic_cast<UnicornFloor*>(&m.cpu());
    require(cpu != nullptr, "Unicorn adapter");
    return *cpu;
}
static void prepare(XboxMachine& m, const xlle_aot_region& r, bool native) {
    require(m.ok() && m.init_ram(), "machine");
    require(m.cpu().setup_flat_segments(0x8000), "segments");
    require(m.cpu().write_mem(r.base, r.code, r.code_size), "code");
    const uint8_t jump[] = {0xe9, 0xfb, 0, 0, 0};
    require(m.cpu().write_mem(r.base - 0x100, jump, sizeof(jump)), "bootstrap");
    X86Regs regs{};
    regs.eip = r.base - 0x100; regs.esp = 0x9000; regs.eflags = 2;
    m.cpu().set_regs(regs);
    if (native) require(m.register_aot_region(r), "registration");
}
static void run(XboxMachine& m, uint32_t pc, uint64_t count) {
    std::string err;
    auto n = m.run(pc, count, err);
    require(err.empty(), err.c_str());
    require(n == count, "exact clock");
}

int main() {
    // No registry, diagnostic execution, initial CPU stop and a cold code mapping must never
    // silently bypass strict validation through run_unicorn's fast path.
    for (unsigned mode = 0; mode < 4; ++mode) {
        XboxMachine m;
        prepare(m, xlle_aot_loop, mode != 0);
        if (mode == 1) floor(m).force_insn_scheduling();
        if (mode == 2) {
            auto regs = m.cpu().get_regs();
            regs.eip = xlle_aot_loop.base;
            m.cpu().set_regs(regs);
        }
        if (mode == 3) {
            // Settle the CPU's initial stop/bootstrap on another code page.
            // The registered page at 0x1000 remains untranslated, so this
            // exercises cold code admission independently of startup state.
            const uint8_t jump[] = {0xe9, 0xfb, 0, 0, 0};
            require(m.cpu().write_mem(0x3000, xlle_aot_loop.code, xlle_aot_loop.code_size), "warm other page");
            require(m.cpu().write_mem(0x2f00, jump, sizeof(jump)), "warm other bootstrap");
            run(m, 0x2f00, 101);
            auto regs = m.cpu().get_regs();
            regs.eip = xlle_aot_loop.base;
            m.cpu().set_regs(regs);
        }
        floor(m).require_native_execution(true);
        const auto before = floor(m).block_stats();
        const auto aot_before = m.aot_stats();
        const auto regs = m.cpu().get_regs();
        std::string err;
        require(m.run(regs.eip, 10, err) == 0, "decline before retirement");
        require(err.find("native-required fallback refused") != std::string::npos, "explicit failure");
        // A freshly constructed Unicorn has a pending CPU stop before its
        // first run. Context certification rejects it before querying the TLB.
        const char* reason[] = {"no-native-regions", "diagnostic-observer",
                                "cpu-stop-or-interrupt", "code-translation"};
        require(err.find(reason[mode]) != std::string::npos, "correct decline reason");
        require(m.cpu().get_regs().eip == regs.eip, "no PC effects");
        const auto after = floor(m).block_stats();
        require(after.tb_entries == before.tb_entries && after.insn_mode_runs == before.insn_mode_runs &&
                after.block_mode_runs == before.block_mode_runs, "no Unicorn execution");
        require(m.aot_stats().fallback_runs == aot_before.fallback_runs &&
                m.aot_stats().fallback_insns == aot_before.fallback_insns,
                "refused fallback is not a run");
    }
    // A declared warm scope succeeds, including fixed RAM helpers; warming is
    // outside the scope and remains visible in the cumulative fallback count.
    for (bool memory : {false, true}) {
        XboxMachine m;
        prepare(m, memory ? xlle_aot_machine_memory : xlle_aot_loop, true);
        run(m, 0xf00, 101);
        const auto before = m.aot_stats();
        floor(m).require_native_execution(true);
        run(m, m.cpu().get_regs().eip, 100);
        const auto after = m.aot_stats();
        require(after.retired - before.retired == 100, "native work in strict scope");
        require(after.fallback_insns == before.fallback_insns && after.fallback_runs == before.fallback_runs,
                "scope has no hidden fallback");
        if (memory) require(after.memory_reads > before.memory_reads && after.memory_writes > before.memory_writes,
                            "fixed memory helpers execute inside strict scope");
    }
    // Execute the new general integer sequence through the real machine in a
    // strict scope. Compare each of 121 instruction boundaries with the floor;
    // warming precedes the scope and cannot conceal execution inside it.
    {
        XboxMachine native,reference;
        prepare(native,xlle_aot_integer_cycle,true);
        prepare(reference,xlle_aot_integer_cycle,false);
        for(auto* machine:{&native,&reference}) {
            auto regs=machine->cpu().get_regs();
            regs.eax=0x12345678;regs.ebx=0xa5a55a5a;regs.ecx=0x80000021;
            regs.edx=0x10203040;regs.esi=0x87654321;regs.edi=0xfeedbeef;
            regs.eflags=0x00203cd7; // Preserved DF/IOPL/ID, IF remains clear.
            machine->cpu().set_regs(regs);
            run(*machine,0xf00,45);
        }
        const auto before=native.aot_stats();
        const auto blocks=floor(native).block_stats();
        floor(native).require_native_execution(true);
        for(unsigned i=0;i<121;++i) {
            run(native,native.cpu().get_regs().eip,1);
            run(reference,reference.cpu().get_regs().eip,1);
            const auto a=native.cpu().get_regs(), b=reference.cpu().get_regs();
            require(a.eax==b.eax && a.ebx==b.ebx && a.ecx==b.ecx && a.edx==b.edx &&
                    a.esi==b.esi && a.edi==b.edi && a.esp==b.esp && a.ebp==b.ebp &&
                    a.eip==b.eip && ((a.eflags^b.eflags)&~0x10u)==0,"integer machine boundary state");
        }
        const auto after=native.aot_stats();
        const auto blocks_after=floor(native).block_stats();
        require(after.retired-before.retired==121 && after.fallback_insns==before.fallback_insns &&
                after.fallback_runs==before.fallback_runs,"integer scope executes only native code");
        require(blocks_after.tb_entries==blocks.tb_entries &&
                blocks_after.block_mode_runs==blocks.block_mode_runs &&
                blocks_after.insn_mode_runs==blocks.insn_mode_runs,"integer scope has no hidden CPU execution");
    }
    // Warm a real native prefix followed by deliberately unregistered BSWAP.
    {
        XboxMachine m;
        prepare(m, xlle_aot_machine_mixed, true);
        const uint8_t tail[] = {0x0f, 0xc8, 0xe9, 0xe9, 0xff, 0xff, 0xff};
        require(m.cpu().write_mem(0x1010, tail, sizeof(tail)), "unsupported tail");
        run(m, 0xf00, 81);
        const auto before = m.aot_stats();
        floor(m).require_native_execution(true);
        std::string err;
        require(m.run(0x1000, 20, err) == 2, "native prefix retires before first missing target");
        require(err.find("pc=0x00001010") != std::string::npos &&
                err.find("unregistered-code") != std::string::npos, "first missing location/reason");
        require(m.aot_stats().fallback_insns == before.fallback_insns &&
                m.aot_stats().fallback_runs == before.fallback_runs, "BSWAP never executes");
    }
    std::puts("native-required: missing/cold/observer refusal, exact native scope, RAM helpers and first fallback passed");
}
