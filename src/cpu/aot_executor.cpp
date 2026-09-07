#include "cpu/aot_executor.hpp"
#include "cpu/unicorn_floor.hpp"
#include "cpu/unicorn_aot_bridge.h"
#include "cpu/unicorn_metrics.h"
#include "machine/perf_excl.hpp"
#include <unicorn/unicorn.h>
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <limits>

namespace xlle {

bool AotExecutor::add_region(const xlle_aot_region& r) {
    if (r.abi_version != XLLE_AOT_ABI_VERSION || !r.code || !r.run ||
        !r.code_size || r.code_size > 65536 ||
        uint64_t(r.base) + r.code_size > UINT32_MAX) return false;
    for (const auto* existing : regions_)
        if (uint64_t(r.base) < uint64_t(existing->base) + existing->code_size &&
            uint64_t(existing->base) < uint64_t(r.base) + r.code_size) return false;
    regions_.push_back(&r);
    AotRegionStats counters{};
    counters.base = r.base;
    region_stats.push_back(counters);
    return true;
}

bool AotExecutor::add_replacement(const AotReplacement& h) {
    if (h.version != 1 || !h.identity || !h.profile || !h.propose ||
        std::find(regions_.begin(), regions_.end(), h.region) == regions_.end())
        return false;
    for (const auto& old : hooks_)
        if (old.identity == h.identity ||
            (old.region == h.region && old.profile == h.profile)) return false;
    hooks_.push_back(h);
    return true;
}

bool AotExecutor::contains(uint32_t pc) const {
    for (const auto* r : regions_)
        if (pc >= r->base && uint64_t(pc) < uint64_t(r->base) + r->code_size)
            return true;
    return false;
}

void* AotExecutor::ram(uint32_t address, size_t size, unsigned access) {
    if (!size || uint64_t(address) + size > (uint64_t(1) << 32)) return nullptr;
    uint8_t* first = nullptr;
    // Certify every page before exposing a span; no MMU fills or guest effects.
    for (size_t offset = 0; offset < size;) {
        uint32_t at = address + static_cast<uint32_t>(offset);
        size_t part = std::min(size - offset, size_t(4096 - (at & 4095)));
        auto* ptr = static_cast<uint8_t*>(
            xlle_unicorn_aot_ram_ptr(cpu_.uc_, at, part, access));
        if (!ptr) return nullptr;
        if (!first) first = ptr;
        else if (reinterpret_cast<uintptr_t>(ptr) !=
                 reinterpret_cast<uintptr_t>(first) + offset) return nullptr;
        bool owned = false;
        for (const auto& m : cpu_.host_ram_) {
            uintptr_t delta = reinterpret_cast<uintptr_t>(ptr) -
                              reinterpret_cast<uintptr_t>(m.host);
            if (delta < m.size && part <= m.size - delta) { owned = true; break; }
        }
        if (!owned) return nullptr;
        offset += part;
    }
    // One registered backing range must own the whole span, with a corresponding
    // physical alias for code invalidation. Adjacent registrations alone cannot
    // certify the complete access.
    for (const auto& m : cpu_.host_ram_) {
        uintptr_t delta = reinterpret_cast<uintptr_t>(first) -
                          reinterpret_cast<uintptr_t>(m.host);
        if (delta < m.size && size <= m.size - delta) return first;
    }
    return nullptr;
}

int AotExecutor::read32(void* opaque, uint32_t address, uint32_t* value) {
    auto& self = *static_cast<AotExecutor*>(opaque);
    auto& access = self.read_access_;
    auto* p = access.host && access.address == address ? access.host :
        static_cast<uint8_t*>(self.ram(address, 4, 0));
    if (!p) { ++self.stats.memory_declines; return 0; }
    access = {address, p};
    *value = uint32_t(p[0]) | (uint32_t(p[1]) << 8) |
             (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
    ++self.stats.memory_reads;
    return 1;
}

int AotExecutor::write32(void* opaque, uint32_t address, uint32_t value) {
    auto& self = *static_cast<AotExecutor*>(opaque);
    auto& access = self.write_access_;
    auto* p = access.host && access.address == address ? access.host :
        static_cast<uint8_t*>(self.ram(address, 4, 1));
    if (!p) { ++self.stats.memory_declines; return 0; }
    access = {address, p};
    const uint8_t bytes[4] = {uint8_t(value), uint8_t(value >> 8),
                             uint8_t(value >> 16), uint8_t(value >> 24)};
    // Snapshot/observer contexts are excluded before entry. Certification above
    // already established write permission, accessed/dirty bits, and ownership
    // of the entire span. Store once to the shared backing, in guest byte order.
    // Keep physical code invalidation for EVERY alias: the copy alone cannot
    // invalidate cached TCG code or expose a synchronous SMC exit request.
    std::memcpy(p, bytes, sizeof(bytes));
    for (const auto& m : self.cpu_.host_ram_) {
        uintptr_t delta = reinterpret_cast<uintptr_t>(p) -
                          reinterpret_cast<uintptr_t>(m.host);
        if (delta < m.size && 4 <= m.size - delta) {
            xlle_unicorn_invalidate_ram(self.cpu_.uc_,
                m.base + static_cast<uint32_t>(delta), 4);
        }
    }
    ++self.stats.memory_writes;
    return 1;
}

uint64_t AotExecutor::execute(uint32_t pc, uint64_t budget) {
    const xlle_aot_region* region = nullptr;
    size_t index = 0;
    for (; index < regions_.size(); ++index) {
        const auto* r = regions_[index];
        if (pc >= r->base && uint64_t(pc) < uint64_t(r->base) + r->code_size) {
            region = r; break;
        }
    }
    if (!region || !budget) return 0;
    auto& counters = region_stats[index];
    perf_excl::Scope timing(perf_excl::kAotExec);
    ++stats.attempts;
    ++counters.attempts;
    const char* rejection = xlle_unicorn_aot_context_rejection(cpu_.uc_);
    if (rejection || cpu_.mem_cb_ || cpu_.retired_cb_) {
        if (!stats.context_declines)
            std::printf("[aot] first-context-decline reason=%s pc=%08x\n",
                        rejection ? rejection : "diagnostic-observer", pc);
        ++stats.context_declines;
        ++counters.context_declines;
        return 0;
    }
    auto* code = static_cast<uint8_t*>(ram(region->base, region->code_size, 2));
    if (!code) { ++stats.code_tlb_declines; ++counters.code_tlb_declines; return 0; }
    X86Regs regs = cpu_.get_regs();
    xlle_aot_state state{{regs.eax, regs.ecx, regs.edx, regs.ebx,
                           regs.esp, regs.ebp, regs.esi, regs.edi},
                          pc, regs.eflags, cpu_.seq_};
    const uint64_t start = state.retired;
    xlle_aot_bus bus{};
    bus.context = XLLE_AOT_CONTEXT_MACHINE_FLAT32_RAM;
    bus.code_base = region->base;
    bus.code = code;
    bus.code_size = region->code_size;
    bus.code_limit = xlle_unicorn_aot_code_limit(cpu_.uc_);
    bus.opaque = this;
    bus.read32 = read32;
    bus.write32 = write32;
    bus.event_pending = cpu_.irq_pending_ && (state.eflags & 0x200u);
    auto result = xlle_aot_check(region, &state, &bus, budget, start);
    if (result != XLLE_AOT_CONTINUE) { ++stats.exits[result]; ++counters.exits[result]; return 0; }
    bool replaced = false;
    for (const auto& h : hooks_) {
        if (h.region != region || pc != region->base) continue;
        if (!profile_ || profile_ != h.profile) { ++stats.hook_ineligible; continue; }
        auto proposed = state;
        ++stats.hook_calls;
        if (h.propose(state, proposed, budget) && proposed.retired > start &&
            proposed.retired - start <= budget &&
            ((proposed.eflags ^ state.eflags) & ~uint32_t(0x8d5)) == 0 &&
            xlle_aot_branch_allowed(&bus, proposed.eip)) {
            state = proposed;
            replaced = true;
            result = XLLE_AOT_EXTERNAL;
            ++stats.hook_accepts;
        } else ++stats.hook_declines;
        break;
    }
    if (!replaced) {
        perf_excl::Scope body_timing(perf_excl::kAotBody);
        // Invalidate at EVERY native entry. Fallback/device execution may have
        // changed mappings or permissions since the previous invocation.
        // Native RAM writes can change page-table bytes but cannot invalidate
        // architectural TLB translations; INVLPG/CR3 writes require fallback.
        read_access_ = {};
        write_access_ = {};
        result = region->run(region, &state, &bus, budget);
    }
    ++stats.invocations;
    ++counters.invocations;
    if (static_cast<unsigned>(result) <= XLLE_AOT_EXTERNAL) {
        ++stats.exits[result];
        ++counters.exits[result];
    }
    if (state.retired < start || state.retired - start > budget) {
        cpu_.request_stop("AOT: generated code violated retirement budget");
        return 0;
    }
    const uint64_t retired = state.retired - start;
    if (!retired) return 0;
    regs.eax = state.gpr[0]; regs.ecx = state.gpr[1];
    regs.edx = state.gpr[2]; regs.ebx = state.gpr[3];
    regs.esp = state.gpr[4]; regs.ebp = state.gpr[5];
    regs.esi = state.gpr[6]; regs.edi = state.gpr[7];
    regs.eip = state.eip; regs.eflags = state.eflags;
    cpu_.set_regs(regs);
    cpu_.seq_ = state.retired;
    xlle_guest_tsc_ticks = cpu_.seq_;
    stats.retired += retired;
    counters.retired += retired;
    return retired;
}

bool UnicornFloor::register_aot_region(const xlle_aot_region& region) {
    if (!aot_) aot_ = std::make_unique<AotExecutor>(*this);
    return aot_->add_region(region);
}
bool UnicornFloor::register_aot_replacement(const AotReplacement& h) {
    return aot_ && aot_->add_replacement(h);
}
void UnicornFloor::set_aot_profile(uint32_t profile) {
    if (aot_) aot_->set_profile(profile);
}
AotExecutionStats UnicornFloor::aot_stats() const {
    return aot_ ? aot_->stats : AotExecutionStats{};
}
std::vector<AotRegionStats> UnicornFloor::aot_region_stats() const {
    return aot_ ? aot_->region_stats : std::vector<AotRegionStats>{};
}

uint64_t UnicornFloor::run(uint32_t start_eip, uint64_t max_insns, std::string& err) {
    // Default execution remains the existing CPU floor. No generated region is
    // installed automatically, and diagnostic observers retain Unicorn events.
    if (!aot_ || insn_mode_ || mem_cb_)
        return run_unicorn(start_eip, max_insns, err);
    const uint64_t before = seq_;
    uint32_t pc = start_eip;
    irq_window_stop_ = false;
    while (seq_ - before < max_insns) {
        if (!stop_reason_.empty()) {
            err = stop_reason_;
            break;
        }
        if (irq_pending_ && interrupts_enabled()) {
            irq_window_stop_ = true;
            break;
        }
        uint64_t remaining = max_insns - (seq_ - before);
        if (aot_->execute(pc, remaining)) {
            pc = get_regs().eip;
            continue;
        }
        if (!stop_reason_.empty()) { err = stop_reason_; break; }
        aot_handoff_ = false;
        aot_bypass_entry_ = true;
        aot_dispatching_ = true;
        aot_fallback_start_ = seq_;
        ++aot_->stats.fallback_runs;
        uint64_t n = run_unicorn(pc, remaining, err);
        aot_dispatching_ = false;
        aot_->stats.fallback_insns += n;
        pc = get_regs().eip;
        if (!n && aot_handoff_) {
            request_stop("AOT: fallback handoff made no progress");
            err = stop_reason_;
            break;
        }
        if (!err.empty() || !aot_handoff_) break;
        ++aot_->stats.handoffs;
    }
    return seq_ - before;
}

}  // namespace xlle
