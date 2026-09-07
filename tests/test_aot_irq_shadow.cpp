#include "aot_programs.h"
#include "machine/machine.hpp"

#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>

using namespace xlle;

namespace {

static void require(bool ok, const char* why) {
    if (!ok) throw std::runtime_error(why);
}

static constexpr uint32_t kGdtBase = 0x8000;
static constexpr uint32_t kIdtBase = 0x7000;
static constexpr uint32_t kEntry = 0x0f00;
static constexpr uint32_t kLoop = 0x1000;
static constexpr uint32_t kHandler = 0x2000;

static uint32_t read_u32(XboxMachine& machine, uint32_t address, const char* label) {
    uint32_t value = 0;
    require(machine.cpu().read_mem(address, &value, sizeof(value)), label);
    return value;
}

static void install_irq0_gate(XboxMachine& machine) {
    uint8_t gate[8]{};
    gate[0] = static_cast<uint8_t>(kHandler);
    gate[1] = static_cast<uint8_t>(kHandler >> 8);
    gate[2] = 0x08;  // flat code selector from setup_flat_segments()
    gate[5] = 0x8e;  // present ring-0 32-bit interrupt gate
    gate[6] = static_cast<uint8_t>(kHandler >> 16);
    gate[7] = static_cast<uint8_t>(kHandler >> 24);
    require(machine.cpu().write_mem(kIdtBase, gate, sizeof(gate)), "write IRQ0 gate");
    X86DescriptorTable idtr{};
    idtr.base = kIdtBase;
    idtr.limit = 0x7ff;
    require(machine.cpu().write_reg(XReg::IDTR, &idtr, sizeof(idtr)), "load IDTR");
}

static void prepare(XboxMachine& machine) {
    require(machine.ok() && machine.init_ram(), "machine RAM and floor");
    machine.init_devices();
    require(machine.cpu().setup_flat_segments(kGdtBase), "flat protected segments");
    const uint8_t entry[] = {
        0xfb,                         // STI delays IRQ recognition until after the next instruction.
        0xe9, 0xfa, 0x00, 0x00, 0x00  // jmp 0x1000, displacement from 0x0f06.
    };
    require(machine.cpu().write_mem(kEntry, entry, sizeof(entry)), "write STI shadow entry");
    require(machine.cpu().write_mem(xlle_aot_loop.base, xlle_aot_loop.code,
                                    xlle_aot_loop.code_size), "write generated loop");
    const uint8_t iret = 0xcf;
    require(machine.cpu().write_mem(kHandler, &iret, sizeof(iret)), "write IRET handler");
    install_irq0_gate(machine);
    X86Regs regs{};
    regs.eip = kEntry;
    regs.esp = 0x9000;
    regs.eflags = 0x00000002u;  // IF clear before STI.
    machine.cpu().set_regs(regs);
    require(machine.pic_dbg(), "PIC present");
    machine.pic_dbg()->raise(0, true);
}

static void assert_shadow_after_two_instruction_window(XboxMachine& machine) {
    std::string err;
    const uint64_t n = machine.run(kEntry, 2, err);
    require(err.empty(), err.c_str());
    require(n == 2, "STI shadow window retires exactly STI and JMP");
    require(machine.injected_interrupts() == 1, "pending IRQ injected after shadowed JMP");
    require(machine.pending_boundaries() > 0, "pending IRQ boundary observed");

    const auto regs = machine.cpu().get_regs();
    require(regs.eip == kHandler, "interrupt delivered to IRET handler after JMP target");
    require(regs.eax == 0, "loop body has not executed before interrupt delivery");
    require((regs.eflags & 0x200u) == 0, "interrupt gate clears IF");
    require(regs.esp == 0x9000 - 12, "hardware IRQ frame pushed once");
    require(read_u32(machine, regs.esp, "saved IRQ EIP") == kLoop,
            "IRQ frame saved post-shadow JMP target EIP");
    require(read_u32(machine, regs.esp + 4, "saved IRQ CS") == 0x08,
            "IRQ frame saved flat CS");
    require((read_u32(machine, regs.esp + 8, "saved IRQ EFLAGS") & 0x200u) != 0,
            "IRQ frame saved IF set by STI");
}

static void assert_iret_then_loop_continues(XboxMachine& machine, bool native_registered) {
    const uint64_t before_injected = machine.injected_interrupts();
    const auto before_stats = machine.aot_stats();
    std::string err;
    const uint64_t n = machine.run(kHandler, 3, err);
    require(err.empty(), err.c_str());
    require(n == 3, "IRET and loop continuation budget");
    require(machine.injected_interrupts() == before_injected,
            "in-service PIC line does not reinject without EOI");
    const auto regs = machine.cpu().get_regs();
    require(regs.esp == 0x9000, "IRET restored original ESP");
    require((regs.eflags & 0x200u) != 0, "IRET restored IF from shadowed frame");
    require(regs.eip == kLoop && regs.eax == 1, "IRET returns to loop and retires one iteration");
    if (native_registered) {
        const auto after_stats = machine.aot_stats();
        require(after_stats.retired > before_stats.retired,
                "registered loop executes native after IRET");
    }
}

static void split_run_sti_shadow(bool native_registered) {
    XboxMachine machine;
    prepare(machine);
    if (native_registered)
        require(machine.register_aot_region(xlle_aot_loop), "register AOT loop");

    std::string err;
    uint64_t n = machine.run(kEntry, 1, err);
    require(err.empty(), err.c_str());
    require(n == 1, "split STI run retires exactly the STI");
    require(machine.injected_interrupts() == 0,
            "STI shadow suppresses interrupt delivery across first run boundary");
    auto regs = machine.cpu().get_regs();
    require(regs.eip == kEntry + 1, "split STI leaves EIP at shadowed instruction");
    require((regs.eflags & 0x200u) != 0, "split STI sets IF");
    require(regs.esp == 0x9000, "split STI does not push an IRQ frame");

    n = machine.run(kEntry + 1, 1, err);
    require(err.empty(), err.c_str());
    require(n == 1, "split shadowed JMP run retires exactly the JMP");
    require(machine.injected_interrupts() == 1,
            "pending IRQ injects after the shadowed instruction in the next run");
    regs = machine.cpu().get_regs();
    require(regs.eip == kHandler, "split IRQ delivered to handler after shadowed JMP");
    require(regs.eax == 0, "split shadow does not execute loop before IRQ delivery");
    require(regs.esp == 0x9000 - 12, "split shadow pushes one IRQ frame");
    require(read_u32(machine, regs.esp, "split saved IRQ EIP") == kLoop,
            "split IRQ frame saves post-shadow target EIP");
}

static void sti_shadow(bool native_registered) {
    XboxMachine machine;
    prepare(machine);
    if (native_registered)
        require(machine.register_aot_region(xlle_aot_loop), "register AOT loop");
    assert_shadow_after_two_instruction_window(machine);
    assert_iret_then_loop_continues(machine, native_registered);
}

static unsigned g_retired_seen = 0;
static uint32_t g_retired_eips[4]{};

static void observer_mode_sti_shadow() {
    g_retired_seen = 0;
    std::memset(g_retired_eips, 0, sizeof(g_retired_eips));
    XboxMachine machine;
    prepare(machine);
    machine.cpu().on_retired([](const RetiredInsn& insn) {
        if (g_retired_seen < 4) g_retired_eips[g_retired_seen] = insn.eip;
        ++g_retired_seen;
    });
    assert_shadow_after_two_instruction_window(machine);
    require(g_retired_seen == 2, "observer saw exactly STI and shadowed JMP before IRQ");
    require(g_retired_eips[0] == kEntry && g_retired_eips[1] == kEntry + 1,
            "observer confirms instruction order inside STI shadow");
}

}  // namespace

void test_aot_irq_shadow() {
    sti_shadow(false);
    sti_shadow(true);
    split_run_sti_shadow(false);
    split_run_sti_shadow(true);
    observer_mode_sti_shadow();
}
