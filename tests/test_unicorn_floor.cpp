#include "cpu/unicorn_floor.hpp"

#include <cassert>
#include <iostream>
#include <string>

using namespace xlle;

int main() {
    UnicornFloor cpu;
    assert(cpu.ok());
    assert(cpu.map_mem(0x1000, 0x1000));

    X86Regs regs{};
    regs.eip = 0x1000;
    regs.eflags = 0x2;
    cpu.set_regs(regs);

    cpu.request_stop("sentinel stop");
    std::string error;
    const uint64_t retired = cpu.run(0x1000, 100, error);
    assert(retired == 0);
    assert(error.find("sentinel stop") != std::string::npos);

    std::cout << "unicorn floor tests passed\n";
    return 0;
}
