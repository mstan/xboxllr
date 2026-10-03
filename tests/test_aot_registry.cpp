#include "registry.h"
#include "cpu/aot_executor.hpp"
#include "cpu/unicorn_floor.hpp"
#include "machine/machine.hpp"
#include <cstdio>
#include <cstdlib>
#include <string>

using namespace xlle;
static void require(bool okay, const char* message) {
    if (!okay) { std::fprintf(stderr, "AOT registry: %s\n", message); std::exit(1); }
}
static constexpr size_t count = 257;
int main() {
    XboxMachine machine;
    require(machine.ok() && machine.init_ram(), "initialize machine");
    require(machine.cpu().setup_flat_segments(0x8000), "flat segments");
    auto* cpu = dynamic_cast<UnicornFloor*>(&machine.cpu());
    require(cpu != nullptr, "CPU adapter");
    AotExecutor index(*cpu);
    for (size_t i = 0; i < count; ++i) {
        const auto* region = registry_regions[(i * 73) % count]; // shuffled address order
        require(index.add_region(*region), "shuffled index insertion");
        require(machine.cpu().write_mem(region->base, region->code, region->code_size), "load authored code");
    }
    require(!index.contains(0) && !index.contains(0xfffffff0), "before first / after last");
    for (size_t i = 0; i < count; ++i) {
        const auto* r = registry_regions[i];
        require(index.contains(r->base) && index.contains(r->base + r->code_size - 1), "range inclusive start/end byte");
        require(!index.contains(r->base + r->code_size), "exclusive range end and gap");
    }
    const auto* middle = registry_regions[128];
    auto rejected = *middle;
    require(!index.add_region(rejected), "reject duplicate");
    rejected.base -= 1;
    require(!index.add_region(rejected), "reject overlap with next range");
    rejected.base = middle->base + 1;
    require(!index.add_region(rejected), "reject overlap with previous range");
    rejected.base = 0xfffffffcu;
    require(!index.add_region(rejected), "reject wrapping range");
    rejected = *middle; rejected.code_size = 0;
    require(!index.add_region(rejected), "reject empty range");
    require(index.region_stats.size() == count, "failed registrations do not alter counters");
    for (size_t i = 0; i < count; ++i)
        require(index.region_stats[i].base == registry_regions[(i * 73) % count]->base,
                "stable statistics registration order");

    const uint8_t bootstrap[] = {0xe9, 0xfb, 0, 0, 0};
    require(machine.cpu().write_mem(0xff00, bootstrap, sizeof(bootstrap)), "bootstrap jump");
    X86Regs state{};
    state.eip = 0xff00; state.esp = 0x9000; state.eflags = 2;
    machine.cpu().set_regs(state);
    std::string error;
    // Settle bootstrap and warm both code pages outside the explicitly strict scope.
    require(machine.run(0xff00, 1 + 2 * count, error) == 1 + 2 * count && error.empty(), "warm authored cycle");
    require(machine.cpu().get_regs().eip == 0x10000, "warm cycle returns to start");
    for (size_t i = 0; i < count; ++i)
        require(machine.register_aot_region(*registry_regions[(i * 73) % count]), "register machine regions");
    const auto before = machine.aot_stats();
    const auto blocks = cpu->block_stats();
    cpu->require_native_execution(true);
    require(machine.run(0x10000, 2 * count, error) == 2 * count && error.empty(), "strict cross-region cycle");
    const auto after = machine.aot_stats();
    require(after.retired - before.retired == 2 * count, "all cycle work native");
    require(after.fallback_runs == before.fallback_runs && after.fallback_insns == before.fallback_insns,
            "zero fallback across 257 regions");
    require(cpu->block_stats().tb_entries == blocks.tb_entries, "no hidden JIT execution");
    state = machine.cpu().get_regs();
    require(state.eax == 256 && state.eip == 0x10000 && state.esp == 0x9000 && state.eflags == 2,
            "independent cycle result");
    for (const auto& r : machine.aot_region_stats()) require(r.retired == 2, "each stable counter receives its own work");
    // The fast address index is not code identity. Changed bytes must still stop.
    const auto* last = registry_regions[count - 1];
    const uint8_t changed = 0x90;
    require(machine.cpu().write_mem(last->base, &changed, 1), "mutate authored code");
    require(machine.run(last->base, 1, error) == 0, "stale compiled body cannot retire");
    require(error.find("native-required fallback refused") != std::string::npos &&
            error.find("code-changed") != std::string::npos, "identity rejection survives indexed lookup");
    require(machine.aot_stats().fallback_runs == after.fallback_runs &&
            cpu->block_stats().tb_entries == blocks.tb_entries, "stale-code refusal never enters JIT");
    std::puts("AOT registry: 257 shuffled regions, strict branch cycle, bounds, counters and stale-code refusal passed");
}
