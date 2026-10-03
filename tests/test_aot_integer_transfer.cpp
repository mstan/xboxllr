#include "aot_programs.h"
#include "cpu/unicorn_floor.hpp"
#include "cpu/unicorn_aot_bridge.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace xlle;
static void check(bool ok, const char* why) {
    if (!ok) { std::fprintf(stderr, "AOT integer transfer: %s\n", why); std::exit(1); }
}

struct Result {
    X86Regs regs;
    AotExecutionStats aot;
    std::vector<uint8_t> memory;
};

static Result run(unsigned tier, bool carry, uint64_t budget) {
    Result result;
    result.memory.resize(0x10000);
    UnicornFloor cpu;
    check(cpu.ok() && cpu.map_mem_ptr(0, result.memory.size(), result.memory.data()), "owned RAM");
    check(cpu.setup_flat_segments(0x8000), "protected flat context");
    const auto& region = xlle_aot_machine_integer_handoff;
    check(cpu.write_mem(region.base, region.code, region.code_size), "native code");
    // Fallback ADD produces lazy arithmetic flags. Native INC/DEC must preserve
    // its carry; JNC chooses whether EDX changes. Then fallback PUSHFD/ADC must
    // observe native flags and the exact outgoing PC before re-entering native.
    const uint8_t before[] = {0x01, 0xf8, 0xe9, 0xf9, 0, 0, 0}; // ADD EAX,EDI; JMP 1000
    const uint8_t after[] = {0x9c, 0x5d, 0x83, 0xd6, 0, 0xe9, 0xe6, 0xfe, 0xff, 0xff};
    check(cpu.write_mem(0xf00, before, sizeof(before)), "incoming fallback");
    check(cpu.write_mem(0x1010, after, sizeof(after)), "outgoing fallback");
    const X86Regs seed{carry ? 0xffffffffu : 0x7fffffffu, 0x7fffffffu,
                       0x55555555u, 0x80000000u, 0x9000u, 0x11111111u,
                       0x12340000u, 1u, 0xf00u, 0x00203402u}; // DF/IOPL/ID
    cpu.set_regs(seed);
    if (tier) {
        check(cpu.register_aot_region(region), "register artifact");
        cpu.set_aot_bulk_registers(tier == 2);
    }
    std::string error;
    check(cpu.run(0xf00, budget, error) == budget && error.empty(), "exact prefix retirement");
    result.regs = cpu.get_regs();
    result.aot = cpu.aot_stats();
    if (tier) check(result.aot.retired + result.aot.fallback_insns == budget, "tier partition");
    check((result.regs.eflags & 0x00203400u) == 0x00203400u, "DF/IOPL/ID preserved");
    return result;
}

static std::array<uint32_t, 10> words(const X86Regs& r) {
    return {r.eax, r.ecx, r.edx, r.ebx, r.esp, r.ebp, r.esi, r.edi, r.eip, r.eflags};
}

int main() {
    uint32_t unchanged[8] = {1,2,3,4,5,6,7,8}, pc = 9, flags = 10;
    check(!xlle_unicorn_aot_read_integer(nullptr, unchanged, &pc, &flags), "reject absent engine read");
    check(!xlle_unicorn_aot_write_integer(nullptr, unchanged, pc, flags), "reject absent engine write");
    check(unchanged[0] == 1 && unchanged[7] == 8 && pc == 9 && flags == 10, "failed read has no output effects");
    uint64_t public_native = 0, bulk_native = 0;
    for (bool carry : {false, true}) {
        for (uint64_t budget = 1; budget <= 32; ++budget) {
            const auto floor = run(0, carry, budget);
            const auto api = run(1, carry, budget);
            const auto bulk = run(2, carry, budget);
            check(words(floor.regs) == words(api.regs), "reference API matches instruction floor");
            check(words(floor.regs) == words(bulk.regs), "bulk transfer matches instruction floor");
            check(floor.memory == api.memory && floor.memory == bulk.memory, "stack and memory effects match");
            check(api.aot.retired == bulk.aot.retired && api.aot.handoffs == bulk.aot.handoffs &&
                  api.aot.fallback_insns == bulk.aot.fallback_insns, "transfer mode preserves guest execution");
            public_native += api.aot.retired;
            bulk_native += bulk.aot.retired;
            // Independent first-cycle checks, before the next incoming ADD.
            if (budget == (carry ? 11u : 10u)) {
                check(bulk.regs.eip == 0xf00 && bulk.regs.esp == 0x9000, "completed cycle and restored stack");
                check(bulk.regs.ecx == 0x80000000 && bulk.regs.ebx == 0x7fffffff, "native INC/DEC results");
                check(bulk.regs.edx == (carry ? 0x12345678u : 0x55555555u), "incoming lazy carry chooses branch");
                check((bulk.regs.ebp & 1u) == unsigned(carry), "PUSHFD sees outgoing carry");
                check(bulk.regs.esi == 0x12340000u + unsigned(carry), "fallback ADC consumes outgoing carry");
            }
        }
    }
    check(public_native > 0 && public_native == bulk_native, "both actual native transfer paths executed");
    std::puts("AOT integer transfer: floor/API/bulk prefixes and lazy-flag handoffs passed");
}
