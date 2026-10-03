#include "cpu/aot_executor.hpp"
#include "cpu/unicorn_floor.hpp"
#include "cpu/unicorn_aot_bridge.h"
#include "cpu/unicorn_metrics.h"
#include "machine/perf_excl.hpp"
#include <unicorn/unicorn.h>
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <iterator>
#include <limits>

namespace xlle {

static const char* aot_exit_name(xlle_aot_exit exit) {
    static const char* const names[] = {"continue", "complete", "budget", "unsupported",
        "code-changed", "memory", "invalid-entry", "context", "event", "external"};
    const auto index = static_cast<unsigned>(exit);
    return index < sizeof(names) / sizeof(names[0]) ? names[index] : "invalid-exit";
}

bool AotExecutor::add_region(const xlle_aot_region& r) {
    if (r.abi_version != XLLE_AOT_ABI_VERSION || !r.code || !r.run ||
        !r.code_size || r.code_size > 65536 ||
        uint64_t(r.base) + r.code_size > UINT32_MAX) return false;
    const auto next = region_by_base_.lower_bound(r.base);
    if (next != region_by_base_.end() && uint64_t(r.base) + r.code_size > next->first)
        return false;
    if (next != region_by_base_.begin()) {
        const auto* previous = regions_[std::prev(next)->second];
        if (uint64_t(previous->base) + previous->code_size > r.base) return false;
    }
    region_by_base_.emplace_hint(next, r.base, regions_.size());
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

size_t AotExecutor::region_index(uint32_t pc) const {
    auto candidate = region_by_base_.upper_bound(pc);
    if (candidate == region_by_base_.begin()) return regions_.size();
    --candidate;
    const auto* region = regions_[candidate->second];
    return uint64_t(pc) < uint64_t(region->base) + region->code_size
        ? candidate->second : regions_.size();
}

bool AotExecutor::contains(uint32_t pc) const {
    return region_index(pc) != regions_.size();
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

int AotExecutor::read_narrow(void* opaque, uint32_t address, unsigned size, uint32_t* value) {
    auto& self = *static_cast<AotExecutor*>(opaque);
    if ((size != 1 && size != 2) || !value) return 0;
    // Do not reuse the four-byte read cache for a different access width.
    const auto* p = static_cast<const uint8_t*>(self.ram(address, size, XLLE_UNICORN_AOT_ACCESS_READ));
    if (!p) { ++self.stats.memory_declines; return 0; }
    *value = uint32_t(p[0]) | (size == 2 ? uint32_t(p[1]) << 8 : 0);
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
    self.commit_ram(p, value, 4);
    return 1;
}

void AotExecutor::commit_ram(uint8_t* p, uint64_t value, size_t size) {
    uint8_t bytes[8];
    for (size_t i = 0; i < size; ++i) bytes[i] = uint8_t(value >> (8 * i));
    // Snapshot/observer contexts are excluded before entry. Certification above
    // already established write permission, accessed/dirty bits, and ownership
    // of the entire span. Store once to the shared backing, in guest byte order.
    // Keep physical code invalidation for EVERY alias: the copy alone cannot
    // invalidate cached TCG code or expose a synchronous SMC exit request.
    std::memcpy(p, bytes, size);
    for (const auto& m : cpu_.host_ram_) {
        uintptr_t delta = reinterpret_cast<uintptr_t>(p) -
                          reinterpret_cast<uintptr_t>(m.host);
        if (delta < m.size && size <= m.size - delta) {
            xlle_unicorn_invalidate_ram(cpu_.uc_,
                m.base + static_cast<uint32_t>(delta), size);
        }
    }
    ++stats.memory_writes;
}

xlle_aot_exit AotExecutor::x87_memory(void* opaque, unsigned op, uint32_t address,
                                     uint32_t pc, unsigned opcode, unsigned segment) {
    auto& self = *static_cast<AotExecutor*>(opaque);
    if (op > XLLE_AOT_X87_STORE_POP64 || opcode > 0x7ff || segment > 1) return XLLE_AOT_UNSUPPORTED;
    const bool load = op < XLLE_AOT_X87_STORE32, push = op < XLLE_AOT_X87_MUL32;
    const bool wide = (op & 1) != 0;
    if (!xlle_unicorn_aot_x87_available(self.cpu_.uc_, push ? 0 : 1, push)) return XLLE_AOT_CONTEXT;
    const size_t size = wide ? 8 : 4;
    auto* p = static_cast<uint8_t*>(self.ram(address, size, load ?
        XLLE_UNICORN_AOT_ACCESS_READ : XLLE_UNICORN_AOT_ACCESS_WRITE));
    if (!p) { ++self.stats.memory_declines; return XLLE_AOT_MEMORY; }
    if (load) {
        uint64_t value = 0;
        for (size_t i = 0; i < size; ++i) value |= uint64_t(p[i]) << (8 * i);
        xlle_unicorn_aot_x87_load(self.cpu_.uc_, value, wide, !push);
        ++self.stats.memory_reads;
    } else {
        // Complete permission/span certification precedes all FP effects. The
        // coherent RAM commit cannot fault or invoke a guest device callback.
        const uint64_t value = xlle_unicorn_aot_x87_store(self.cpu_.uc_, wide);
        self.commit_ram(p, value, size);
        if (op >= XLLE_AOT_X87_STORE_POP32) xlle_unicorn_aot_x87_pop(self.cpu_.uc_);
    }
    xlle_unicorn_aot_x87_record(self.cpu_.uc_, pc, opcode, address, segment);
    return XLLE_AOT_CONTINUE;
}

xlle_aot_exit AotExecutor::x87_register(void* opaque, unsigned op, unsigned reg,
                                       uint32_t pc, unsigned opcode) {
    auto& self = *static_cast<AotExecutor*>(opaque);
    if (op > XLLE_AOT_X87_FREE_REG || reg > 7 || opcode > 0x7ff) return XLLE_AOT_UNSUPPORTED;
    const unsigned reads = op == XLLE_AOT_X87_FREE_REG ? 0 :
        op == XLLE_AOT_X87_LOAD_REG ? 1u << reg :
        op == XLLE_AOT_X87_STORE_POP_REG ? 1u : (1u | (1u << reg));
    if (!xlle_unicorn_aot_x87_available(self.cpu_.uc_, reads, op == XLLE_AOT_X87_LOAD_REG))
        return XLLE_AOT_CONTEXT;
    xlle_unicorn_aot_x87_register(self.cpu_.uc_, op, reg);
    xlle_unicorn_aot_x87_record(self.cpu_.uc_, pc, opcode, 0, -1);
    return XLLE_AOT_CONTINUE;
}

xlle_aot_exit AotExecutor::mmx_load64(void* opaque, unsigned reg, uint32_t address) {
    auto& self = *static_cast<AotExecutor*>(opaque);
    if (reg >= 8 || !xlle_unicorn_aot_mmx_available(self.cpu_.uc_, 0)) return XLLE_AOT_CONTEXT;
    const auto* p = static_cast<const uint8_t*>(self.ram(address, 8, XLLE_UNICORN_AOT_ACCESS_READ));
    if (!p) { ++self.stats.memory_declines; return XLLE_AOT_MEMORY; }
    uint64_t value = 0;
    for (unsigned i = 0; i < 8; ++i) value |= uint64_t(p[i]) << (8 * i);
    xlle_unicorn_aot_mmx_commit(self.cpu_.uc_, int(reg), value);
    ++self.stats.memory_reads;
    return XLLE_AOT_CONTINUE;
}

xlle_aot_exit AotExecutor::mmx_store64(void* opaque, unsigned reg, uint32_t address, unsigned non_temporal) {
    auto& self = *static_cast<AotExecutor*>(opaque);
    if (reg >= 8 || non_temporal > 1 || !xlle_unicorn_aot_mmx_available(self.cpu_.uc_, non_temporal))
        return XLLE_AOT_CONTEXT;
    auto* p = static_cast<uint8_t*>(self.ram(address, 8, XLLE_UNICORN_AOT_ACCESS_WRITE));
    if (!p) { ++self.stats.memory_declines; return XLLE_AOT_MEMORY; }
    const uint64_t value = xlle_unicorn_aot_mmx_peek(self.cpu_.uc_, reg);
    self.commit_ram(p, value, 8);
    xlle_unicorn_aot_mmx_commit(self.cpu_.uc_, -1, 0);
    return XLLE_AOT_CONTINUE;
}

xlle_aot_exit AotExecutor::mmx_move(void* opaque, unsigned destination, unsigned source) {
    auto& self = *static_cast<AotExecutor*>(opaque);
    if (destination >= 8 || source >= 8 || !xlle_unicorn_aot_mmx_available(self.cpu_.uc_, 0))
        return XLLE_AOT_CONTEXT;
    const uint64_t value = xlle_unicorn_aot_mmx_peek(self.cpu_.uc_, source);
    xlle_unicorn_aot_mmx_commit(self.cpu_.uc_, int(destination), value);
    return XLLE_AOT_CONTINUE;
}

xlle_aot_exit AotExecutor::mmx_emms(void* opaque) {
    auto& self = *static_cast<AotExecutor*>(opaque);
    if (!xlle_unicorn_aot_mmx_available(self.cpu_.uc_, 0)) return XLLE_AOT_CONTEXT;
    xlle_unicorn_aot_mmx_emms(self.cpu_.uc_);
    return XLLE_AOT_CONTINUE;
}

xlle_aot_exit AotExecutor::prefetch(void* opaque, uint32_t address, unsigned locality) {
    auto& self = *static_cast<AotExecutor*>(opaque);
    if (locality > 3 || !xlle_unicorn_aot_sse_available(self.cpu_.uc_)) return XLLE_AOT_CONTEXT;
    // Intel SDM PREFETCHh permits discarding the hint. Unlike an ordinary load,
    // an unavailable translation is not a fault and must not service MMIO.
    // Guest cache-cycle timing is not modeled by the CPU floor. Where available,
    // translate the hint to a host prefetch of already-certified RAM only.
#if defined(__GNUC__) || defined(__clang__)
    if (const void* p = self.ram(address, 1, XLLE_UNICORN_AOT_ACCESS_READ)) {
        switch (locality) {
        case 0: __builtin_prefetch(p, 0, 0); break;
        case 1: __builtin_prefetch(p, 0, 3); break;
        case 2: __builtin_prefetch(p, 0, 2); break;
        case 3: __builtin_prefetch(p, 0, 1); break;
        }
    }
#else
    (void)address;
#endif
    return XLLE_AOT_CONTINUE;
}

uint64_t AotExecutor::execute(uint32_t pc, uint64_t budget) {
    decline_reason_ = "unregistered-code";
    const size_t index = region_index(pc);
    if (index == regions_.size() || !budget) return 0;
    const auto* region = regions_[index];
    auto& counters = region_stats[index];
    perf_excl::Scope timing(perf_excl::kAotExec);
    ++stats.attempts;
    ++counters.attempts;
    const char* rejection = xlle_unicorn_aot_context_rejection(cpu_.uc_);
    if (rejection || cpu_.mem_cb_ || cpu_.retired_cb_) {
        decline_reason_ = rejection ? rejection : "diagnostic-observer";
        if (!stats.context_declines)
            std::printf("[aot] first-context-decline reason=%s pc=%08x\n",
                        rejection ? rejection : "diagnostic-observer", pc);
        ++stats.context_declines;
        ++counters.context_declines;
        return 0;
    }
    auto* code = static_cast<uint8_t*>(ram(region->base, region->code_size, 2));
    if (!code) { decline_reason_ = "code-translation"; ++stats.code_tlb_declines; ++counters.code_tlb_declines; return 0; }
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
    bus.read_narrow = read_narrow;
    bus.write32 = write32;
    bus.mmx_load64 = mmx_load64;
    bus.mmx_store64 = mmx_store64;
    bus.mmx_move = mmx_move;
    bus.mmx_emms = mmx_emms;
    bus.prefetch = prefetch;
    bus.x87_memory = x87_memory;
    bus.x87_register = x87_register;
    bus.event_pending = cpu_.irq_pending_ && (state.eflags & 0x200u);
    auto result = xlle_aot_check(region, &state, &bus, budget, start);
    if (result != XLLE_AOT_CONTINUE) { decline_reason_ = aot_exit_name(result); ++stats.exits[result]; ++counters.exits[result]; return 0; }
    if (cpu_.native_required_ && profile_) {
        decline_reason_ = "replacement-profile";
        return 0;
    }
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
    decline_reason_ = aot_exit_name(result);
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
