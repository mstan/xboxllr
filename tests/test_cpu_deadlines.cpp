#include "cpu/unicorn_floor.hpp"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

using namespace xlle;
static void require(bool ok, const char* why) {
    if (!ok) { std::fprintf(stderr, "CPU deadline: %s\n", why); std::exit(1); }
}
static void prepare(UnicornFloor& cpu) {
    require(cpu.ok() && cpu.map_mem(0x1000, 0x2000), "mapped CPU");
    require(cpu.setup_flat_segments(0x2000), "flat segments");
    // 64 INC EAX instructions and a backward JMP: exactly 65 instructions
    // per iteration. No timing-sensitive input or harvested guest bytes.
    std::vector<uint8_t> code(64, 0x40);
    code.insert(code.end(), {0xe9, 0xbb, 0xff, 0xff, 0xff});
    require(cpu.write_mem(0x1000, code.data(), code.size()), "authored program");
    X86Regs r{}; r.eip = 0x1000; r.esp = 0x2ff0; r.eflags = 2;
    cpu.set_regs(r);
}
static void check(UnicornFloor& cpu, uint64_t budget, uint64_t total) {
    std::string error;
    const auto n = cpu.run(cpu.get_regs().eip, budget, error);
    require(error.empty(), error.c_str());
    if (n != budget) std::fprintf(stderr, "budget=%llu retired=%llu\n",
        (unsigned long long)budget, (unsigned long long)n);
    require(n == budget, "fallback must retire the exact requested prefix");
    const auto r = cpu.get_regs();
    require(r.eax == total / 65 * 64 + total % 65, "all completed INC effects");
    require(r.eip == 0x1000 + total % 65, "first unexecuted instruction");
    require(cpu.block_stats().tb_insns == total, "exact block retirement accounting");
    require(cpu.block_stats().insn_mode_runs == 0, "no instruction-hook execution");
    uint64_t code = 0, count = 0, block = 0;
    cpu.hook_region_counts(code, count, block);
    require(code == 0 && count == 0 && block > 0, "no hidden per-instruction counter");
}
#ifdef XLLE_CPU_BLOCK_CENSUS
static void census_collisions(const char* path) {
    UnicornFloor cpu;
    require(cpu.ok() && cpu.map_mem(0x3000, 0x1000) &&
            cpu.setup_flat_segments(0x3000), "census machine");
    const uint8_t code[] = {0x90, 0xeb, 0xfd}; // NOP; JMP back
    for (unsigned i = 0; i < 12; ++i) {
        // Same low 22 bits: these identities collide in the bounded census
        // table, whose hash takes 18 bits after a four-bit shift.
        const uint32_t pc = 0x1000 + i * 0x400000;
        require(cpu.map_mem(pc, 0x1000) && cpu.write_mem(pc, code, sizeof(code)), "census code");
        X86Regs r{}; r.eip = pc; r.esp = 0x3ff0; r.eflags = 2; cpu.set_regs(r);
        std::string error;
        require(cpu.run(pc, 2, error) == 2 && error.empty(), "census collision execution");
    }
    require(cpu.write_block_census(path), "census snapshot");
    std::ifstream file(path);
    std::string line; std::getline(file, line);
    require(line.find("overflow=0 ") != std::string::npos, "census collisions cannot drop counts");
    std::getline(file, line); // column names
    uint64_t sum = 0; unsigned rows = 0;
    while (std::getline(file, line)) {
        unsigned pc = 0, bytes = 0; unsigned long long entries = 0, charged = 0, unretired = 0;
        require(std::sscanf(line.c_str(), "0x%x,%u,%llu,%llu,%llu",
                &pc, &bytes, &entries, &charged, &unretired) == 5, "census row");
        sum += charged - unretired; ++rows;
    }
    require(rows == 12 && sum == 24, "all colliding rows reconcile to actual retirement");
    std::ifstream pack(std::string(path) + ".blockpack", std::ios::binary | std::ios::ate);
    require(pack && pack.tellg() == std::streampos(8 + 12 * (8 + 3)),
            "all colliding first-code records are retained");
}
#endif
int main(int argc, char** argv) {
    // Include a cold first block: no bootstrap overshoot exemption.
    for (uint64_t prefix = 1; prefix <= 70; ++prefix) {
        UnicornFloor cpu; prepare(cpu); check(cpu, prefix, prefix);
    }
    // Alternate cached full and partial blocks, including one-instruction tails.
    UnicornFloor cpu; prepare(cpu);
    uint64_t total = 0;
    for (uint64_t budget : {1, 7, 64, 65, 66, 4096, 3, 8192, 1, 1, 65}) {
        total += budget; check(cpu, budget, total);
    }
    // Many translated lengths at the same PC must not lose the instruction
    // count of a still-cached full block. Exercise primary-table collisions.
    UnicornFloor variants; prepare(variants);
    check(variants, 65, 65);
    for (uint64_t length = 1; length <= 64; ++length) {
        for (uint64_t budget : {length, uint64_t(65)}) {
            auto r = variants.get_regs(); r.eip = 0x1000; r.eax = 0;
            variants.set_regs(r);
            std::string error;
            const auto n = variants.run(r.eip, budget, error);
            require(error.empty(), error.c_str());
            require(n == budget, "cached length variant exact retirement");
            require(variants.get_regs().eax == (budget == 65 ? 64 : budget),
                    "cached length variant exact effects");
        }
    }
    require(variants.block_stats().icount_evictions > 0, "fixture exercises count-table spill");
    require(variants.block_stats().icount_misses == 0, "cached blocks retain their exact count");
    // The shortened TB may fault too: retirement must remove the faulting
    // instruction instead of charging the requested tail unconditionally.
    UnicornFloor fault; prepare(fault);
    const uint8_t failed_load[] = {0xa1, 0, 0, 0, 0x0e, 0xeb, 0xfe};
    require(fault.write_mem(0x1040, failed_load, sizeof(failed_load)), "tail fault program");
    std::string error;
    require(fault.run(0x1000, 65, error) == 64, "faulting tail instruction does not retire");
    require(error.find("unmapped read addr=0x0e000000") != std::string::npos,
            "tail fault preserves the actual fault address");
    require(fault.get_regs().eax == 64 && fault.get_regs().eip == 0x1040,
            "tail fault preserves completed state and fault EIP");
    require(fault.block_stats().tb_insns == 64 && fault.block_stats().fault_unretired == 1 &&
            fault.block_stats().deadline_splits == 1, "tail fault repairs exact accounting");
#ifdef XLLE_CPU_BLOCK_CENSUS
    require(argc == 2, "private census output argument");
    census_collisions(argv[1]);
#else
    (void)argc; (void)argv;
#endif
    std::puts("CPU exact deadlines pass");
}
