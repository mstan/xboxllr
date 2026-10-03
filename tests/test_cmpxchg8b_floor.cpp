#include "cpu/unicorn_floor.hpp"
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace xlle;
static unsigned failures;
static void check(bool ok, const char* why) {
    if (!ok) { std::fprintf(stderr, "CMPXCHG8B: %s\n", why); ++failures; }
}
static void fixture(bool locked, bool match, bool split, uint32_t initial_ecx,
                    uint32_t add_flags) {
    UnicornFloor cpu;
    check(cpu.ok() && cpu.map_mem(0x1000, 0x3000), "CPU and RAM");
    check(cpu.setup_flat_segments(0x3000), "flat segments");
    // Authored ADD ECX,ffff; [LOCK] CMPXCHG8B [ESI]; JMP self.
    // The ADD's lazy flag source is an operand, never an EFLAGS image.
    std::vector<uint8_t> code = {0x81, 0xc1, 0xff, 0xff, 0, 0};
    if (locked) code.push_back(0xf0);
    code.insert(code.end(), {0x0f, 0xc7, 0x0e, 0xeb, 0xfe});
    check(cpu.write_mem(0x1000, code.data(), code.size()), "authored program");
    const uint64_t memory = 0x1234567887654321ull;
    check(cpu.write_mem(0x2001, &memory, sizeof(memory)), "unaligned qword");
    X86Regs r{}; r.eip = 0x1000; r.esp = 0x3ff0; r.eflags = 0x602;
    r.esi = 0x2001; r.edx = uint32_t(memory >> 32);
    r.eax = uint32_t(memory) ^ (match ? 0 : 1); r.ecx = initial_ecx; r.ebx = 0x10203040;
    cpu.set_regs(r);
    std::string error;
    uint64_t retired = cpu.run(r.eip, split ? 1 : 2, error);
    if (split && error.empty()) retired += cpu.run(cpu.get_regs().eip, 1, error);
    check(error.empty(), error.c_str());
    check(retired == 2, "both instructions retire exactly");
    const auto result = cpu.get_regs();
    const uint32_t expected_flags = (0x602 | add_flags) & ~0x40u;
    if (result.eflags != (expected_flags | (match ? 0x40 : 0)))
        std::fprintf(stderr, "lock=%u match=%u split=%u ECX=%08x flags=%08x expected=%08x\n",
            locked, match, split, initial_ecx, result.eflags, expected_flags | (match ? 0x40 : 0));
    check(result.eflags == (expected_flags | (match ? 0x40 : 0)),
          "only ZF changes after the independently expected ADD flags");
    check(result.eax == uint32_t(memory) && result.edx == uint32_t(memory >> 32),
          "compare pair is preserved on success and loaded on failure");
    check(result.ecx == initial_ecx + 0xffffu && result.ebx == r.ebx,
          "source pair is preserved");
    uint64_t output = 0;
    check(cpu.read_mem(0x2001, &output, 8), "result qword");
    const uint64_t expected_memory = match ? (uint64_t(result.ecx) << 32) | r.ebx : memory;
    check(output == expected_memory, "all eight memory bytes follow compare result");
}
int main() {
    for (bool locked : {false, true}) for (bool match : {false, true})
        for (bool split : {false, true}) {
            fixture(locked, match, split, 0, 0x04);             // 0 + ffff: PF
            fixture(locked, match, split, 1, 0x14);             // 1 + ffff: AF/PF
            fixture(locked, match, split, 0xffff0001, 0x55);    // wraps to zero: CF/PF/AF/ZF
            fixture(locked, match, split, 0x7fffffff, 0x890);   // signed overflow: OF/SF/AF
        }
    if (failures) { std::fprintf(stderr, "%u failures\n", failures); return 1; }
    std::puts("CMPXCHG8B flags and effects pass");
}
