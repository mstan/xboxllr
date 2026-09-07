#include "aot_programs.h"
#include "machine/machine.hpp"

#include <array>
#include <cstddef>
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

static void prepare(XboxMachine& machine, const xlle_aot_region& region = xlle_aot_loop) {
    require(machine.ok() && machine.init_ram(), "machine RAM and floor");
    require(machine.cpu().setup_flat_segments(kGdtBase), "flat protected segments");
    require(machine.cpu().write_mem(region.base, region.code, region.code_size), "code load");
    const uint8_t entry[] = {0xe9, 0xfb, 0, 0, 0};
    require(machine.cpu().write_mem(region.base - 0x100, entry, sizeof(entry)), "entry jump");
    X86Regs r{};
    r.eip = region.base - 0x100;
    r.esp = 0x9000;
    r.eflags = 2;
    machine.cpu().set_regs(r);
}

static void run_exact(XboxMachine& machine, uint32_t pc, uint64_t budget) {
    std::string err;
    const uint64_t n = machine.run(pc, budget, err);
    require(err.empty(), err.c_str());
    require(n == budget, "exact instruction budget");
}

template <size_t N>
static void write_xreg(CpuFloor& cpu, XReg reg, const std::array<uint8_t, N>& value,
                       const char* label) {
    require(cpu.write_reg(reg, value.data(), value.size()), label);
}

template <size_t N>
static std::array<uint8_t, N> read_xreg(CpuFloor& cpu, XReg reg, const char* label) {
    std::array<uint8_t, N> value{};
    require(cpu.read_reg(reg, value.data(), value.size()), label);
    return value;
}

static void write_u16_xreg(CpuFloor& cpu, XReg reg, uint16_t value, const char* label) {
    require(cpu.write_reg(reg, &value, sizeof(value)), label);
}

static uint16_t read_u16_xreg(CpuFloor& cpu, XReg reg, const char* label) {
    uint16_t value = 0;
    require(cpu.read_reg(reg, &value, sizeof(value)), label);
    return value;
}

static void write_u32_xreg(CpuFloor& cpu, XReg reg, uint32_t value, const char* label) {
    require(cpu.write_reg(reg, &value, sizeof(value)), label);
}

static uint32_t read_u32_xreg(CpuFloor& cpu, XReg reg, const char* label) {
    uint32_t value = 0;
    require(cpu.read_reg(reg, &value, sizeof(value)), label);
    return value;
}

static std::array<uint8_t, 16> xmm_pattern(unsigned reg) {
    std::array<uint8_t, 16> v{};
    for (unsigned i = 0; i < v.size(); ++i)
        v[i] = static_cast<uint8_t>(0x20u + reg * 17u + i * 3u);
    return v;
}

static std::array<uint8_t, 10> st_pattern(unsigned reg) {
    // Valid finite 80-bit values: explicit integer bit set, exponent varied.
    std::array<uint8_t, 10> v{};
    const uint64_t significand = UINT64_C(0x8000000000000000) | (uint64_t(reg) << 48);
    const uint16_t exponent = static_cast<uint16_t>(0x3fff + reg);
    for (unsigned i = 0; i < 8; ++i)
        v[i] = static_cast<uint8_t>(significand >> (i * 8));
    v[8] = static_cast<uint8_t>(exponent);
    v[9] = static_cast<uint8_t>(exponent >> 8);
    return v;
}

static void extended_state_survives_native_and_fallback() {
    XboxMachine machine;
    prepare(machine, xlle_aot_machine_mixed);
    const uint8_t fallback[] = {0x43, 0xe9, 0xea, 0xff, 0xff, 0xff};
    require(machine.cpu().write_mem(0x1010, fallback, sizeof(fallback)), "extended-state fallback");

    std::array<std::array<uint8_t, 16>, 8> xmm{};
    std::array<std::array<uint8_t, 10>, 8> st{};
    const XReg xmm_regs[8] = {XReg::XMM0, XReg::XMM1, XReg::XMM2, XReg::XMM3,
                              XReg::XMM4, XReg::XMM5, XReg::XMM6, XReg::XMM7};
    const XReg st_regs[8] = {XReg::ST0, XReg::ST1, XReg::ST2, XReg::ST3,
                             XReg::ST4, XReg::ST5, XReg::ST6, XReg::ST7};
    for (unsigned i = 0; i < 8; ++i) {
        xmm[i] = xmm_pattern(i);
        st[i] = st_pattern(i);
        write_xreg(machine.cpu(), xmm_regs[i], xmm[i], "write XMM state");
        write_xreg(machine.cpu(), st_regs[i], st[i], "write x87 state");
    }
    write_u32_xreg(machine.cpu(), XReg::MXCSR, 0x1f80, "write MXCSR");
    write_u16_xreg(machine.cpu(), XReg::FPCW, 0x037f, "write FPCW");
    write_u16_xreg(machine.cpu(), XReg::FPSW, 0x0000, "write FPSW");
    write_u16_xreg(machine.cpu(), XReg::FPTAG, 0x0000, "write FPTAG");

    require(machine.register_aot_region(xlle_aot_machine_mixed), "register mixed state loop");
    run_exact(machine, xlle_aot_loop.base - 0x100, 101);
    const auto regs = machine.cpu().get_regs();
    require(regs.eax == 25 && regs.ebx == 25 && regs.eip == xlle_aot_loop.base,
            "independent mixed loop state");
    const auto stats = machine.aot_stats();
    require(stats.retired > 0 && stats.fallback_insns > 0, "native and fallback both ran");
    require(stats.retired + stats.fallback_insns == 101, "retirement partition");

    for (unsigned i = 0; i < 8; ++i) {
        require(read_xreg<16>(machine.cpu(), xmm_regs[i], "read XMM state") == xmm[i],
                "XMM state survived");
        require(read_xreg<10>(machine.cpu(), st_regs[i], "read x87 state") == st[i],
                "x87 state survived");
    }
    require(read_u32_xreg(machine.cpu(), XReg::MXCSR, "read MXCSR") == 0x1f80,
            "MXCSR survived");
    require(read_u16_xreg(machine.cpu(), XReg::FPCW, "read FPCW") == 0x037f,
            "FPCW survived");
    require(read_u16_xreg(machine.cpu(), XReg::FPSW, "read FPSW") == 0x0000,
            "FPSW survived");
    require(read_u16_xreg(machine.cpu(), XReg::FPTAG, "read FPTAG") == 0x0000,
            "FPTAG survived");
}

static unsigned g_hook_calls = 0;

static bool accept_hook(const xlle_aot_state& input, xlle_aot_state& output, uint64_t budget) {
    ++g_hook_calls;
    if (budget == 0) return false;
    output = input;
    output.gpr[0] = 0x12345678u;
    output.eip = xlle_aot_loop.base;
    output.retired = input.retired + 1;
    return true;
}

static bool decline_hook(const xlle_aot_state& input, xlle_aot_state& output, uint64_t budget) {
    ++g_hook_calls;
    output = input;
    output.gpr[0] = 0xdeadbeefu;
    output.eip = xlle_aot_loop.base;
    output.retired = input.retired + (budget ? 1 : 0);
    return false;
}

static bool over_budget_hook(const xlle_aot_state& input, xlle_aot_state& output, uint64_t budget) {
    ++g_hook_calls;
    output = input;
    output.gpr[0] = 0xc001d00du;
    output.eip = xlle_aot_loop.base;
    output.retired = input.retired + budget + 1;
    return true;
}

static bool protected_if_hook(const xlle_aot_state& input, xlle_aot_state& output, uint64_t budget) {
    ++g_hook_calls;
    if (budget == 0) return false;
    output = input;
    output.gpr[0] = 0xfacefeedu;
    output.eip = xlle_aot_loop.base;
    output.eflags ^= 0x200u;
    output.retired = input.retired + 1;
    return true;
}

static AotReplacement replacement(bool (*fn)(const xlle_aot_state&, xlle_aot_state&, uint64_t),
                                  uint32_t profile, uint64_t identity) {
    AotReplacement r{};
    r.identity = identity;
    r.profile = profile;
    r.region = &xlle_aot_loop;
    r.propose = fn;
    return r;
}

static void hook_accepts_register_transaction() {
    g_hook_calls = 0;
    XboxMachine machine;
    prepare(machine);
    require(machine.register_aot_region(xlle_aot_loop), "register loop");
    require(machine.register_aot_replacement(replacement(accept_hook, 7, 0xa001)),
            "register accept hook");
    machine.set_aot_profile(7);
    run_exact(machine, xlle_aot_loop.base - 0x100, 2);
    const auto regs = machine.cpu().get_regs();
    require(regs.eax == 0x12345678u && regs.eip == xlle_aot_loop.base,
            "accepted hook committed proposed GPR/EIP");
    const auto stats = machine.aot_stats();
    require(g_hook_calls == 1 && stats.hook_calls == 1 && stats.hook_accepts == 1,
            "accepted hook counters");
    require(stats.retired == 1 && stats.fallback_insns == 1,
            "accepted hook retirement partition");
}

static void hook_decline_discards_proposal() {
    g_hook_calls = 0;
    XboxMachine machine;
    prepare(machine);
    require(machine.register_aot_region(xlle_aot_loop), "register loop");
    require(machine.register_aot_replacement(replacement(decline_hook, 7, 0xa002)),
            "register decline hook");
    machine.set_aot_profile(7);
    run_exact(machine, xlle_aot_loop.base - 0x100, 3);
    const auto regs = machine.cpu().get_regs();
    require(regs.eax == 1 && regs.eip == xlle_aot_loop.base,
            "declined hook proposal discarded and fallback executed");
    const auto stats = machine.aot_stats();
    require(g_hook_calls == 1 && stats.hook_calls == 1 && stats.hook_declines == 1,
            "declined hook counters");
    require(stats.hook_accepts == 0, "declined hook never accepted");
}

static void hook_wrong_or_disabled_profile_is_ineligible(bool set_wrong_profile) {
    g_hook_calls = 0;
    XboxMachine machine;
    prepare(machine);
    require(machine.register_aot_region(xlle_aot_loop), "register loop");
    require(machine.register_aot_replacement(replacement(accept_hook, 7, set_wrong_profile ? 0xa003 : 0xa004)),
            "register profiled hook");
    if (set_wrong_profile) machine.set_aot_profile(9);
    run_exact(machine, xlle_aot_loop.base - 0x100, 3);
    const auto regs = machine.cpu().get_regs();
    require(regs.eax == 1 && regs.eip == xlle_aot_loop.base,
            "ineligible hook did not commit proposal");
    const auto stats = machine.aot_stats();
    require(g_hook_calls == 0 && stats.hook_calls == 0 && stats.hook_accepts == 0,
            "ineligible hook was not called");
    require(stats.hook_ineligible > 0, "ineligible hook counter");
}

static void changed_code_declines_before_hook() {
    g_hook_calls = 0;
    XboxMachine machine;
    prepare(machine);
    require(machine.register_aot_region(xlle_aot_loop), "register loop");
    require(machine.register_aot_replacement(replacement(accept_hook, 7, 0xa005)),
            "register guarded hook");
    machine.set_aot_profile(7);
    const uint8_t nop = 0x90;
    require(machine.cpu().write_mem(xlle_aot_loop.base, &nop, 1), "mutate loop code");
    run_exact(machine, xlle_aot_loop.base - 0x100, 101);
    const auto regs = machine.cpu().get_regs();
    require(regs.eax == 0 && regs.eip == xlle_aot_loop.base,
            "fallback observed changed loop code");
    const auto stats = machine.aot_stats();
    require(g_hook_calls == 0 && stats.hook_calls == 0 && stats.hook_accepts == 0,
            "changed-code guard blocks replacement hook");
    require(stats.retired == 0 && stats.fallback_insns == 101,
            "changed code never retired native instructions");
    require(stats.exits[XLLE_AOT_CODE_CHANGED] > 0, "changed-code exit reported");
}

static void hook_true_over_budget_discards_proposal() {
    g_hook_calls = 0;
    XboxMachine machine;
    prepare(machine);
    require(machine.register_aot_region(xlle_aot_loop), "register loop");
    require(machine.register_aot_replacement(replacement(over_budget_hook, 7, 0xa006)),
            "register over-budget hook");
    machine.set_aot_profile(7);
    run_exact(machine, xlle_aot_loop.base - 0x100, 3);
    const auto regs = machine.cpu().get_regs();
    require(regs.eax == 1 && regs.eip == xlle_aot_loop.base,
            "over-budget replacement proposal discarded");
    const auto stats = machine.aot_stats();
    require(g_hook_calls == 1 && stats.hook_calls == 1 && stats.hook_declines == 1,
            "over-budget hook counted as decline");
    require(stats.hook_accepts == 0, "over-budget hook never accepted");
    require(stats.retired == 2 && stats.fallback_insns == 1,
            "over-budget decline ran generated loop after bootstrap");
}

static void hook_true_protected_if_discards_proposal() {
    g_hook_calls = 0;
    XboxMachine machine;
    prepare(machine);
    require(machine.register_aot_region(xlle_aot_loop), "register loop");
    require(machine.register_aot_replacement(replacement(protected_if_hook, 7, 0xa007)),
            "register protected-IF hook");
    machine.set_aot_profile(7);
    run_exact(machine, xlle_aot_loop.base - 0x100, 3);
    const auto regs = machine.cpu().get_regs();
    require(regs.eax == 1 && regs.eip == xlle_aot_loop.base,
            "protected IF replacement proposal discarded");
    require((regs.eflags & 0x200u) == 0, "protected IF bit unchanged");
    const auto stats = machine.aot_stats();
    require(g_hook_calls == 1 && stats.hook_calls == 1 && stats.hook_declines == 1,
            "protected IF hook counted as decline");
    require(stats.hook_accepts == 0, "protected IF hook never accepted");
    require(stats.retired == 2 && stats.fallback_insns == 1,
            "protected IF decline ran generated loop after bootstrap");
}

static void aot_registry_rejects_invalid_entries() {
    XboxMachine machine;
    prepare(machine);

    xlle_aot_region wrong_version = xlle_aot_loop;
    wrong_version.abi_version = XLLE_AOT_ABI_VERSION + 1;
    require(!machine.register_aot_region(wrong_version), "reject unsupported region ABI version");

    require(machine.register_aot_region(xlle_aot_loop), "register loop for registry tests");
    require(!machine.register_aot_region(xlle_aot_loop), "reject duplicate overlapping region");

    AotReplacement bad_version = replacement(accept_hook, 7, 0xb001);
    bad_version.version = 2;
    require(!machine.register_aot_replacement(bad_version),
            "reject unsupported replacement version");

    require(!machine.register_aot_replacement(replacement(accept_hook, 7, 0)),
            "reject zero replacement identity");

    xlle_aot_region unregistered = xlle_aot_loop;
    unregistered.base = xlle_aot_loop.base + 0x2000;
    AotReplacement unregistered_region = replacement(accept_hook, 7, 0xb002);
    unregistered_region.region = &unregistered;
    require(!machine.register_aot_replacement(unregistered_region),
            "reject replacement for unregistered region");

    require(machine.register_aot_replacement(replacement(accept_hook, 7, 0xb003)),
            "register first replacement identity/profile");
    require(!machine.register_aot_replacement(replacement(decline_hook, 9, 0xb003)),
            "reject duplicate replacement identity");
    require(!machine.register_aot_replacement(replacement(decline_hook, 7, 0xb004)),
            "reject duplicate replacement profile for region");
}

static void write_data_descriptor(uint32_t gdt_base, uint64_t descriptor, XboxMachine& machine) {
    require(machine.cpu().write_mem(gdt_base + 16, &descriptor, sizeof(descriptor)),
            "write data descriptor");
}

static void gdt_change_without_reload_keeps_cached_flat_context() {
    XboxMachine machine;
    prepare(machine);
    write_data_descriptor(kGdtBase, UINT64_C(0x00cf92001000ffff), machine);
    require(machine.register_aot_region(xlle_aot_loop), "register loop");
    run_exact(machine, xlle_aot_loop.base - 0x100, 101);
    const auto stats = machine.aot_stats();
    require(stats.retired > 0 && stats.context_declines == 0,
            "GDT memory write alone does not alter cached DS context");
}

static void gdt_change_with_ds_reload_declines_native_context() {
    XboxMachine machine;
    prepare(machine);
    write_data_descriptor(kGdtBase, UINT64_C(0x00cf92001000ffff), machine);
    write_u32_xreg(machine.cpu(), XReg::DS, 0x10, "reload DS selector");
    require(machine.register_aot_region(xlle_aot_loop), "register loop");
    run_exact(machine, xlle_aot_loop.base - 0x100, 101);
    const auto regs = machine.cpu().get_regs();
    require(regs.eax == 50 && regs.eip == xlle_aot_loop.base,
            "context-declined fallback preserves loop behavior");
    const auto stats = machine.aot_stats();
    require(stats.retired == 0 && stats.context_declines > 0,
            "reloaded nonflat DS declines native execution");
    require(stats.fallback_insns == 101, "context decline falls back for full budget");
}

static void install_irq0_gate_to_loop(XboxMachine& machine) {
    uint8_t gate[8]{};
    const uint32_t handler = xlle_aot_loop.base;
    gate[0] = static_cast<uint8_t>(handler);
    gate[1] = static_cast<uint8_t>(handler >> 8);
    gate[2] = 0x08;
    gate[3] = 0x00;
    gate[4] = 0x00;
    gate[5] = 0x8e;  // present ring-0 32-bit interrupt gate
    gate[6] = static_cast<uint8_t>(handler >> 16);
    gate[7] = static_cast<uint8_t>(handler >> 24);
    require(machine.cpu().write_mem(kIdtBase, gate, sizeof(gate)), "write IRQ0 IDT gate");
    X86DescriptorTable idtr{};
    idtr.base = kIdtBase;
    idtr.limit = 0x7ff;
    require(machine.cpu().write_reg(XReg::IDTR, &idtr, sizeof(idtr)), "load IDTR");
}

static void pending_irq_interrupts_before_native_dispatch() {
    XboxMachine machine;
    require(machine.ok() && machine.init_ram(), "machine RAM and floor");
    machine.init_devices();
    require(machine.cpu().setup_flat_segments(kGdtBase), "flat protected segments");
    require(machine.cpu().write_mem(xlle_aot_loop.base, xlle_aot_loop.code,
                                    xlle_aot_loop.code_size), "code load");
    const uint8_t entry[] = {0xe9, 0xfb, 0, 0, 0};
    require(machine.cpu().write_mem(xlle_aot_loop.base - 0x100, entry, sizeof(entry)),
            "entry jump");
    install_irq0_gate_to_loop(machine);
    X86Regs r{};
    r.eip = xlle_aot_loop.base - 0x100;
    r.esp = 0x9000;
    r.eflags = 0x202;
    machine.cpu().set_regs(r);
    require(machine.register_aot_region(xlle_aot_loop), "register loop");
    run_exact(machine, xlle_aot_loop.base - 0x100, 101);
    const auto warmed = machine.aot_stats();
    const uint64_t injected_before = machine.injected_interrupts();
    const uint64_t pending_before = machine.pending_boundaries();
    require(warmed.retired > 0, "warm native loop before IRQ case");

    X86Regs r2{};
    r2.eip = xlle_aot_loop.base - 0x100;
    r2.esp = 0x9000;
    r2.eflags = 0x202;
    machine.cpu().set_regs(r2);
    require(machine.pic_dbg(), "PIC present");
    // The initial PIT pulse may have delivered IRQ0 during warmup. Complete
    // that interrupt before presenting a new edge at the same priority.
    machine.pic_dbg()->io_write(0x20, 1, 0x20);
    machine.pic_dbg()->raise(0, true);

    std::string err;
    const uint64_t n = machine.run(xlle_aot_loop.base - 0x100, 3, err);
    require(err.empty(), err.c_str());
    require(n == 3, "IRQ/deadline run budget");
    const auto regs = machine.cpu().get_regs();
    require(machine.injected_interrupts() == injected_before + 1, "one pending IRQ injected");
    require(machine.pending_boundaries() > pending_before, "pending IRQ boundary observed");
    require((regs.eflags & 0x200u) == 0, "interrupt gate cleared IF");
    require(regs.esp == 0x9000 - 12, "IRQ frame pushed once");
    require(regs.eip == xlle_aot_loop.base + 1 && regs.eax == 2,
            "loop resumes from IRQ handler before bootstrap jump");
    const auto stats = machine.aot_stats();
    require(stats.retired > warmed.retired &&
                stats.retired + stats.fallback_insns == warmed.retired + warmed.fallback_insns + n,
            "AOT accounting remains disjoint across IRQ boundary");
}

}  // namespace

void test_aot_machine_state() {
    extended_state_survives_native_and_fallback();
    hook_accepts_register_transaction();
    hook_decline_discards_proposal();
    hook_true_over_budget_discards_proposal();
    hook_true_protected_if_discards_proposal();
    aot_registry_rejects_invalid_entries();
    hook_wrong_or_disabled_profile_is_ineligible(false);
    hook_wrong_or_disabled_profile_is_ineligible(true);
    changed_code_declines_before_hook();
    gdt_change_without_reload_keeps_cached_flat_context();
    gdt_change_with_ds_reload_declines_native_context();
    pending_irq_interrupts_before_native_dispatch();
}
