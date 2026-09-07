#include "aot_programs.h"
#include "machine/machine.hpp"

#include <cstdint>
#include <stdexcept>
#include <string>

using namespace xlle;

namespace {

static void require(bool ok, const char* why) {
    if (!ok) throw std::runtime_error(why);
}

static constexpr uint32_t kGdtBase = 0x8000;
static constexpr uint32_t kEntry = 0x0f00;
static constexpr uint32_t kInitialEax = 0x11223344u;

static void prepare(XboxMachine& machine) {
    require(machine.ok() && machine.init_ram(), "machine RAM and floor");
    require(machine.cpu().setup_flat_segments(kGdtBase), "flat protected segments");
    require(machine.cpu().write_mem(xlle_aot_machine_store_fault.base,
                                    xlle_aot_machine_store_fault.code,
                                    xlle_aot_machine_store_fault.code_size),
            "store-fault code load");
    const uint8_t entry[] = {0xe9, 0xfb, 0, 0, 0};
    require(machine.cpu().write_mem(kEntry, entry, sizeof(entry)), "entry jump");
    X86Regs regs{};
    regs.eip = kEntry;
    regs.esp = 0x9000;
    regs.eflags = 2;
    regs.eax = kInitialEax;
    machine.cpu().set_regs(regs);
}

static void unmapped_store_fault(bool native_registered) {
    XboxMachine machine;
    prepare(machine);
    if (native_registered) {
        require(machine.register_aot_region(xlle_aot_machine_store_fault),
                "register store-fault native region");
    }

    std::string err;
    const uint64_t retired = machine.run(kEntry, 10, err);
    const auto regs = machine.cpu().get_regs();

    require(!err.empty(), "unmapped store faults loudly");
    require(retired == 2 && machine.total_insns() == 2,
            "bootstrap JMP and preceding INC retire, faulting store does not");
    require(regs.eip == xlle_aot_machine_store_fault.base + 1,
            "fault EIP points at A3 store, not TB entry or suffix");
    require(regs.eax == kInitialEax, "faulting store leaves EAX source unchanged");
    require(regs.ebx == 1, "preceding INC EBX effect is preserved exactly once");

    uint32_t target = 0;
    require(!machine.cpu().read_mem(0x0e000000u, &target, sizeof(target)),
            "unmapped store target remains unmapped");

    if (native_registered) {
        const auto stats = machine.aot_stats();
        require(stats.retired <= retired, "native store-fault retirement bounded by machine clock");
        require(stats.memory_declines > 0, "native path reports no-effect memory decline");
        require(stats.retired + stats.fallback_insns == retired,
                "native and fallback retirement partition faulting slice");
    }
}

}  // namespace

void test_aot_store_fault() {
    unmapped_store_fault(false);
    unmapped_store_fault(true);
}
