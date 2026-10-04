// xlle — UnicornFloor implementation.
//
// Scheduling granularity (beads-xbox.1.4.6). The floor has two paths:
//
//  * BLOCK path (canonical). No per-instruction hook exists. UC_HOOK_BLOCK fires once per
//    translation block, before `gen_tb_start` (qemu/accel/tcg/translator.c:79), so a
//    `uc_emu_stop` from it exits through the block's exit-request branch having retired
//    ZERO instructions of that block. UC_HOOK_EDGE_GENERATED (qemu/accel/tcg/cpu-exec.c:266)
//    hands over `uc_tb{pc, icount, size}` for every block at generation time; the block hook
//    looks the icount up and charges it whole. Guest time therefore remains exactly the
//    retired instruction count. `uc_emu_start` is called with count=0 (so Unicorn adds no
//    count hook, uc.c:1064-1088) and timeout=0 (the timeout parameter reads the HOST clock
//    and is forbidden by the determinism contract).
//
//    Faults and self-modifying-code restarts can interrupt a charged block. The
//    pinned QEMU restore helper reports the exact completed prefix on that cold
//    path, and restore_clock removes the unexecuted suffix before another block
//    or device event can observe the clock. Ordinary blocks need no extra call.
//
//  * INSTRUCTION path. Installed only when a caller asks for a retired-instruction observer
//    (`on_retired`) or explicitly selects diagnostic instruction scheduling. It
//    adds UC_HOOK_CODE but still passes count=0 and timeout=0. Its hook owns the
//    clock and budget; it is never used speculatively to bootstrap normal runs.
//
// The pinned build reports every new TB, including the first (null predecessor).
// A block crossing a deadline is stopped before execution and resumed with
// QEMU's instruction-count translation limit. Only this tail needs another
// uc_emu_start; there is no first-block overshoot or deferred bootstrap charge.
//
// A0 note on fidelity (PRINCIPLES §6): on the instruction path UC_HOOK_CODE fires once per
// instruction *before* execution. Its observer records dispatch; a subsequent
// fault repairs the clock but cannot retract an already-delivered observer event.
#include "cpu/unicorn_floor.hpp"
#include "cpu/unicorn_metrics.h"
#include "cpu/aot_executor.hpp"
#include "cpu/unicorn_aot_bridge.h"

#include <unicorn/unicorn.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#ifdef XLLE_CPU_BLOCK_CENSUS
#include <random>
#endif

#include "machine/perf_accounting.hpp"
#include "machine/perf_excl.hpp"

namespace xlle {

static uint64_t to_hook(void* p) { return reinterpret_cast<uint64_t>(p); }
static void* from_hook(uc_hook h) { return reinterpret_cast<void*>(static_cast<uintptr_t>(h)); }

UnicornFloor::UnicornFloor() {
    uc_engine* uc = nullptr;
    if (uc_open(UC_ARCH_X86, UC_MODE_32, &uc) != UC_ERR_OK) {
        uc_ = nullptr;
        return;
    }
    uc_ = reinterpret_cast<uc_struct*>(uc);

    icount_ = std::make_unique<IcountSlot[]>(kIcountSlots);
    block_pc_ring_ = std::make_unique<uint32_t[]>(kBlockPcRing);
#ifdef XLLE_CPU_BLOCK_CENSUS
    census_ = std::make_unique<CensusRow[]>(kIcountSlots);
    // Random identity is diagnostic metadata only; it never affects guest state.
    // Rejecting cross-run snapshot subtraction needs more than matching counters.
    std::random_device random;
    char census_id[33];
    for (unsigned i = 0; i < 4; ++i)
        std::snprintf(census_id + 8 * i, 9, "%08x", static_cast<unsigned>(random()));
    census_run_id_ = census_id;
#endif

    // Per-translation-block entry hook. This is the canonical scheduling point: instruction
    // accounting, the slice/device deadline, the interrupt window and the always-on block-PC
    // ring all live here. No UC_HOOK_CODE is installed; see the file comment.
    uc_hook hb = 0;
    uc_hook_add(uc, &hb, UC_HOOK_BLOCK,
                reinterpret_cast<void*>(&UnicornFloor::block_trampoline), this, 1, 0);
    hook_block_ = from_hook(hb);

    // Translation-time report of each new block's (pc, size, icount). Not generated into TCG
    // code -- it is called from tb_find, so it costs nothing per executed instruction.
    uc_hook he = 0;
    uc_hook_add(uc, &he, UC_HOOK_EDGE_GENERATED,
                reinterpret_cast<void*>(&UnicornFloor::edge_trampoline), this, 1, 0);
    hook_edge_ = from_hook(he);

    // Capture the address of any unmapped access so a fault-stop can name it (PRINCIPLES §3).
    uc_hook hu = 0;
    uc_hook_add(uc, &hu,
                UC_HOOK_MEM_READ_UNMAPPED | UC_HOOK_MEM_WRITE_UNMAPPED |
                    UC_HOOK_MEM_FETCH_UNMAPPED,
                reinterpret_cast<void*>(&UnicornFloor::unmapped_trampoline), this, 1, 0);
    hook_unmapped_ = from_hook(hu);

    // Guest-raised vectors (INT n / INT3 / CPU exceptions). Unicorn's patched cpu-exec removes
    // QEMU's native IDT delivery entirely and funnels every vector to this hook (no hook ->
    // UC_ERR_EXCEPTION). We put the architectural behavior back: deliver through the guest IDT.
    uc_hook hi = 0;
    uc_hook_add(uc, &hi, UC_HOOK_INTR, reinterpret_cast<void*>(&UnicornFloor::intr_trampoline),
                this, 1, 0);
    hook_intr_ = from_hook(hi);
}

UnicornFloor::~UnicornFloor() {
    if (uc_) uc_close(reinterpret_cast<uc_engine*>(uc_));
}

void UnicornFloor::hook_region_counts(uint64_t& code_regions, uint64_t& count_regions,
                                      uint64_t& block_regions) const {
    xlle_unicorn_hook_region_counts(uc_, hook_code_, hook_block_, &code_regions,
                                    &count_regions, &block_regions);
}

UnicornFloor::BlockStats UnicornFloor::block_stats() const { return stats_; }

#ifdef XLLE_CPU_BLOCK_CENSUS
UnicornFloor::CensusRow* UnicornFloor::census_row(uint32_t pc, uint32_t size) {
    if (census_last_ && census_last_->pc == pc && census_last_->size == size)
        return census_last_;
    const size_t hash = (icount_hash(pc) ^ size) & (kIcountSlots - 1);
    CensusRow* found = nullptr;
    for (size_t i = 0; i < kIcountProbes; ++i) {
        auto& row = census_[(hash + i) & (kIcountSlots - 1)];
        if (!row.size || (row.pc == pc && row.size == size)) {
            found = &row;
            break;
        }
    }
    // Do not drop executed work when the fast table's probe window fills.
    // unordered_map preserves references across rehashes, including census_last_.
    if (!found) found = &census_spill_[(uint64_t(pc) << 32) | size];
    auto& row = *found;
    if (!row.size) {
        row.pc = pc; row.size = size;
        row.first_code.resize(size);
        // Capture fetched bytes at first execution, before boot-only pages can
        // be freed/reused. Read cached fetch mappings only, with no MMU effects.
        for (size_t offset = 0; offset < size;) {
            const uint32_t at = pc + static_cast<uint32_t>(offset);
            const size_t part = std::min(size - offset, size_t(4096 - (at & 4095)));
            const auto* code = xlle_unicorn_aot_ram_ptr(uc_, at, part, 2);
            if (!code) { row.first_code.clear(); break; }
            std::memcpy(row.first_code.data() + offset, code, part);
            offset += part;
        }
    }
    census_last_ = found;
    return found;
}
void UnicornFloor::census_charge(uint32_t pc, uint32_t size, uint64_t insns, uint64_t entries) {
    if (auto* row = census_row(pc, size)) {
        row->entries += entries;
        row->charged += insns;
    }
}
#endif

bool UnicornFloor::write_block_census(const char* path) const {
#ifdef XLLE_CPU_BLOCK_CENSUS
    if (!path || !census_ || insn_mode_) return false;
    std::vector<const CensusRow*> rows;
    for (size_t i = 0; i < kIcountSlots; ++i)
        if (census_[i].size) rows.push_back(&census_[i]);
    for (const auto& item : census_spill_) rows.push_back(&item.second);
    std::sort(rows.begin(), rows.end(), [](const CensusRow* a, const CensusRow* b) {
        return a->pc < b->pc || (a->pc == b->pc && a->size < b->size);
    });
    auto* file = std::fopen(path, "wb");
    if (!file) return false;
    std::fprintf(file, "# schema=xboxlle.block-census.v1 total_insns=%llu fallback_insns=%llu "
        "idle_skipped=%llu native_insns=%llu overflow=%llu run_id=%s\n",
        (unsigned long long)seq_, (unsigned long long)stats_.tb_insns,
        (unsigned long long)stats_.skipped_insns, (unsigned long long)aot_stats().retired,
        (unsigned long long)census_overflow_, census_run_id_.c_str());
    std::fprintf(file, "pc,size,entries,charged_insns,unretired_insns\n");
    for (const auto* item : rows) {
        const auto& row = *item;
        std::fprintf(file, "0x%08x,%u,%llu,%llu,%llu\n", row.pc, row.size,
            (unsigned long long)row.entries, (unsigned long long)row.charged,
            (unsigned long long)row.unretired);
    }
    bool okay = !std::ferror(file);
    if (std::fclose(file) != 0 || !okay) return false;
    // Private binary artifact; the repository publication guard rejects this
    // suffix. Counts aggregate versions, while these bytes are FIRST execution.
    const std::string code_path = std::string(path) + ".blockpack";
    file = std::fopen(code_path.c_str(), "wb");
    if (!file) return false;
    const uint8_t header[8] = {'X', 'L', 'L', 'E', 'B', 'C', '1', 0};
    std::fwrite(header, 1, sizeof(header), file);
    uint64_t captured = 0, missing = 0;
    for (const auto* item : rows) {
        const auto& row = *item;
        if (row.first_code.size() != row.size) { ++missing; continue; }
        const uint32_t values[2] = {row.pc, row.size};
        for (const uint32_t value : values) {
            const uint8_t bytes[4] = {uint8_t(value), uint8_t(value >> 8),
                                    uint8_t(value >> 16), uint8_t(value >> 24)};
            std::fwrite(bytes, 1, 4, file);
        }
        std::fwrite(row.first_code.data(), 1, row.size, file);
        ++captured;
    }
    std::printf("[cpu-census] first-code captured=%llu missing=%llu\n",
                (unsigned long long)captured, (unsigned long long)missing);
    okay = !std::ferror(file);
    return std::fclose(file) == 0 && okay;
#else
    (void)path;
    return false;
#endif
}

void UnicornFloor::icount_insert(uint32_t pc, uint16_t size, uint16_t icount) {
    if (icount == 0) return;  // cannot be represented; a real block has >= 1 instruction
    icount_spill_.erase((uint64_t(pc) << 16) | size);
    const size_t h = icount_hash(pc);
    for (size_t i = 0; i < kIcountProbes; ++i) {
        IcountSlot& slot = icount_[(h + i) & (kIcountSlots - 1)];
        if (slot.icount == 0) {
            slot.pc = pc;
            slot.size = size;
            slot.icount = icount;
            stats_.icount_entries++;
            return;
        }
        if (slot.pc == pc && slot.size == size) {
            slot.icount = icount;  // same identity retranslated; refresh in place
            if (pc == last_pc_ && size == last_size_) last_icount_ = icount;
            return;
        }
    }
    // QEMU can re-enter a cached TB without a generation callback. Displacing
    // an identity must retain its exact count, not depend on a later re-insert.
    IcountSlot& slot = icount_[h];
    icount_spill_[(uint64_t(slot.pc) << 16) | slot.size] = slot.icount;
    slot.pc = pc;
    slot.size = size;
    slot.icount = icount;
    stats_.icount_evictions++;
    // The displaced identity may be the one sitting in the inline cache; invalidate it so a
    // later lookup goes back to the table instead of reusing a stale icount.
    last_pc_ = 0;
    last_size_ = 0;
    last_icount_ = 0;
}

uint32_t UnicornFloor::icount_lookup(uint32_t pc, uint16_t size) const {
    const size_t h = icount_hash(pc);
    for (size_t i = 0; i < kIcountProbes; ++i) {
        const IcountSlot& slot = icount_[(h + i) & (kIcountSlots - 1)];
        if (slot.icount == 0) break;
        if (slot.pc == pc && slot.size == size) return slot.icount;
    }
    const auto spilled = icount_spill_.find((uint64_t(pc) << 16) | size);
    return spilled == icount_spill_.end() ? 0 : spilled->second;
}

void UnicornFloor::on_retired(RetiredCb cb) {
    retired_cb_ = std::move(cb);
    // The per-instruction hook exists only for requested instruction diagnostics.
    // Installing it is what makes a run slow, so it is never installed speculatively.
    if (retired_cb_) set_insn_mode(true);
}

bool UnicornFloor::observe_entry(uint32_t address, std::function<void()> callback) {
    if (!uc_ || !callback) return false;
    auto observer = std::make_unique<EntryObserver>(EntryObserver{address, std::move(callback)});
    entry_observer_min_ = std::min(entry_observer_min_, address);
    entry_observer_max_ = std::max(entry_observer_max_, address);
    entry_observers_.push_back(std::move(observer));
    if (aot_) aot_->observe_entry(address);
    return true;
}

void UnicornFloor::notify_entry(uint32_t pc) {
    for (const auto& entry : entry_observers_) if (entry->address == pc) {
        try { entry->callback(); }
        catch (const std::exception& e) { request_stop(e.what()); }
    }
}

// One-way switch, called only from on_retired(). It is never reversed: uc_hook_del would
// have to sweep the hook's whole hooked_regions set, and nothing needs the per-instruction
// hook to go away once a caller has asked for per-instruction events.
void UnicornFloor::set_insn_mode(bool on) {
    if (!on || insn_mode_) return;
    uc_engine* uc = reinterpret_cast<uc_engine*>(uc_);
    if (!hook_code_) {
        uc_hook hc = 0;
        if (uc_hook_add(uc, &hc, UC_HOOK_CODE,
                        reinterpret_cast<void*>(&UnicornFloor::code_trampoline), this, 1,
                        0) != UC_ERR_OK)
            return;
        hook_code_ = from_hook(hc);
    }
    insn_mode_ = true;
}

bool UnicornFloor::map_mem(uint32_t addr, size_t size) {
    return uc_mem_map(reinterpret_cast<uc_engine*>(uc_), addr, size, UC_PROT_ALL) == UC_ERR_OK;
}
bool UnicornFloor::map_mem_ptr(uint32_t addr, size_t size, void* host) {
    const uintptr_t begin = reinterpret_cast<uintptr_t>(host);
    if (!host || !size || size > UINTPTR_MAX - begin) return false;
    const HostRamMapping* alias_of = nullptr;
    for (const auto& m : host_ram_) {
        const uintptr_t old_begin = reinterpret_cast<uintptr_t>(m.host);
        if (begin < old_begin + m.size && old_begin < begin + size) {
            if (m.host != host || m.size != size) return false;
            alias_of = &m;
        }
    }
    // RAM identity is fixed before any translated code can refer to it.
    if (alias_of && (stats_.tb_entries || stats_.insn_mode_runs)) return false;
    if (uc_mem_map_ptr(reinterpret_cast<uc_engine*>(uc_), addr, size, UC_PROT_ALL, host) !=
        UC_ERR_OK) return false;
    if (alias_of && !xlle_unicorn_share_ram_identity(uc_, alias_of->base, addr)) return false;
    host_ram_.push_back({addr, size, static_cast<uint8_t*>(host)});
    return true;
}
bool UnicornFloor::write_mem(uint32_t addr, const void* data, size_t size) {
    auto* uc = reinterpret_cast<uc_engine*>(uc_);
    if (uc_mem_write(uc, addr, data, size) != UC_ERR_OK) return false;
    xlle_unicorn_invalidate_ram(uc_, addr, size);
    // Host/DMA writes bypass translated stores. Xbox maps one host buffer twice.
    // Invalidate every physical alias so fallback TCG cannot execute stale code.
    for (const auto& source : host_ram_) {
        const uint64_t offset = uint64_t(addr) - source.base;
        if (offset >= source.size || size > source.size - offset) continue;
        const uintptr_t host = reinterpret_cast<uintptr_t>(source.host) + offset;
        for (const auto& alias : host_ram_) {
            const uintptr_t delta = host - reinterpret_cast<uintptr_t>(alias.host);
            if (delta >= alias.size || size > alias.size - delta) continue;
            const uint32_t physical = alias.base + static_cast<uint32_t>(delta);
            if (physical != addr) xlle_unicorn_invalidate_ram(uc_, physical, size);
        }
        break;
    }
    return true;
}
bool UnicornFloor::read_mem(uint32_t addr, void* data, size_t size) {
    return uc_mem_read(reinterpret_cast<uc_engine*>(uc_), addr, data, size) == UC_ERR_OK;
}

void UnicornFloor::on_mem(MemCb cb) {
    mem_cb_ = std::move(cb);
    if (!mem_cb_ || hook_mem_ || !uc_) return;

    // Normal boot replay has no RAM observer. Avoid crossing the C callback boundary on every
    // guest load/store unless a diagnostic client (currently the fixture runner) requests it.
    // Once installed, retain the hook for the UnicornFloor lifetime just like the other hooks.
    uc_hook h = 0;
    if (uc_hook_add(reinterpret_cast<uc_engine*>(uc_), &h,
                    UC_HOOK_MEM_READ | UC_HOOK_MEM_WRITE,
                    reinterpret_cast<void*>(&UnicornFloor::mem_trampoline), this, 1, 0) ==
        UC_ERR_OK)
        hook_mem_ = from_hook(h);
}

void UnicornFloor::set_regs(const X86Regs& r) {
    uc_engine* uc = reinterpret_cast<uc_engine*>(uc_);
    struct { int id; uint32_t v; } regs[] = {
        {UC_X86_REG_EAX, r.eax}, {UC_X86_REG_ECX, r.ecx}, {UC_X86_REG_EDX, r.edx},
        {UC_X86_REG_EBX, r.ebx}, {UC_X86_REG_ESP, r.esp}, {UC_X86_REG_EBP, r.ebp},
        {UC_X86_REG_ESI, r.esi}, {UC_X86_REG_EDI, r.edi}, {UC_X86_REG_EIP, r.eip},
        {UC_X86_REG_EFLAGS, r.eflags},
    };
    for (auto& reg : regs) {
        uint32_t v = reg.v;
        uc_reg_write(uc, reg.id, &v);
    }
}

X86Regs UnicornFloor::get_regs() {
    uc_engine* uc = reinterpret_cast<uc_engine*>(uc_);
    X86Regs r;
    struct { int id; uint32_t* out; } regs[] = {
        {UC_X86_REG_EAX, &r.eax}, {UC_X86_REG_ECX, &r.ecx}, {UC_X86_REG_EDX, &r.edx},
        {UC_X86_REG_EBX, &r.ebx}, {UC_X86_REG_ESP, &r.esp}, {UC_X86_REG_EBP, &r.ebp},
        {UC_X86_REG_ESI, &r.esi}, {UC_X86_REG_EDI, &r.edi}, {UC_X86_REG_EIP, &r.eip},
        {UC_X86_REG_EFLAGS, &r.eflags},
    };
    for (auto& reg : regs) uc_reg_read(uc, reg.id, reg.out);
    return r;
}

static int xreg_to_uc(XReg r) {
    switch (r) {
        case XReg::CR0: return UC_X86_REG_CR0;
        case XReg::CR4: return UC_X86_REG_CR4;
        case XReg::MXCSR: return UC_X86_REG_MXCSR;
        case XReg::XMM0: return UC_X86_REG_XMM0;
        case XReg::XMM1: return UC_X86_REG_XMM1;
        case XReg::XMM2: return UC_X86_REG_XMM2;
        case XReg::ST0: return UC_X86_REG_ST0;
        case XReg::ST1: return UC_X86_REG_ST1;
        case XReg::MM0: return UC_X86_REG_MM0;
        case XReg::CR3: return UC_X86_REG_CR3;
        case XReg::XMM3: return UC_X86_REG_XMM3;
        case XReg::XMM4: return UC_X86_REG_XMM4;
        case XReg::XMM5: return UC_X86_REG_XMM5;
        case XReg::XMM6: return UC_X86_REG_XMM6;
        case XReg::XMM7: return UC_X86_REG_XMM7;
        case XReg::ST2: return UC_X86_REG_ST2;
        case XReg::ST3: return UC_X86_REG_ST3;
        case XReg::ST4: return UC_X86_REG_ST4;
        case XReg::ST5: return UC_X86_REG_ST5;
        case XReg::ST6: return UC_X86_REG_ST6;
        case XReg::ST7: return UC_X86_REG_ST7;
        case XReg::FPCW: return UC_X86_REG_FPCW;
        case XReg::FPSW: return UC_X86_REG_FPSW;
        case XReg::FPTAG: return UC_X86_REG_FPTAG;
        case XReg::CS: return UC_X86_REG_CS;
        case XReg::DS: return UC_X86_REG_DS;
        case XReg::SS: return UC_X86_REG_SS;
        case XReg::IDTR: return UC_X86_REG_IDTR;
        case XReg::DR7: return UC_X86_REG_DR7;
        case XReg::FIP: return UC_X86_REG_FIP;
        case XReg::FDP: return UC_X86_REG_FDP;
        case XReg::FCS: return UC_X86_REG_FCS;
        case XReg::FDS: return UC_X86_REG_FDS;
        case XReg::FOP: return UC_X86_REG_FOP;
    }
    return -1;
}

static size_t xreg_size(XReg r) {
    switch (r) {
        case XReg::XMM0: case XReg::XMM1: case XReg::XMM2: case XReg::XMM3:
        case XReg::XMM4: case XReg::XMM5: case XReg::XMM6: case XReg::XMM7: return 16;
        case XReg::ST0: case XReg::ST1: case XReg::ST2: case XReg::ST3:
        case XReg::ST4: case XReg::ST5: case XReg::ST6: case XReg::ST7: return 10;
        case XReg::MM0: case XReg::FIP: case XReg::FDP: return 8;
        case XReg::FCS: case XReg::FDS: case XReg::FOP: return 2;
        case XReg::FPCW: case XReg::FPSW: case XReg::FPTAG: return 2;
        case XReg::IDTR: return sizeof(uc_x86_mmr);
        default: return 4;
    }
}
static_assert(sizeof(X86DescriptorTable) == sizeof(uc_x86_mmr), "descriptor bridge layout");
static_assert(offsetof(X86DescriptorTable, base) == offsetof(uc_x86_mmr, base), "descriptor base");

bool UnicornFloor::read_reg(XReg r, void* buf, size_t n) {
    int id = xreg_to_uc(r);
    if (id < 0 || !buf || n < xreg_size(r)) return false;
    return uc_reg_read(reinterpret_cast<uc_engine*>(uc_), id, buf) == UC_ERR_OK;
}
bool UnicornFloor::write_reg(XReg r, const void* buf, size_t n) {
    int id = xreg_to_uc(r);
    if (id < 0 || !buf || n < xreg_size(r)) return false;
    return uc_reg_write(reinterpret_cast<uc_engine*>(uc_), id, const_cast<void*>(buf)) ==
           UC_ERR_OK;
}

bool UnicornFloor::setup_flat_segments(uint32_t gdt_addr) {
    uc_engine* uc = reinterpret_cast<uc_engine*>(uc_);
    // 3 descriptors: null, flat 32-bit code (0x08), flat 32-bit data (0x10).
    uint64_t gdt[3] = {
        0x0000000000000000ull,
        0x00CF9A000000FFFFull,  // base 0, limit 4GB, code exec/read, G=1 D=1
        0x00CF92000000FFFFull,  // base 0, limit 4GB, data read/write, G=1 B=1
    };
    if (uc_mem_write(uc, gdt_addr, gdt, sizeof(gdt)) != UC_ERR_OK) return false;
    uc_x86_mmr gdtr{};
    gdtr.base = gdt_addr;
    gdtr.limit = sizeof(gdt) - 1;
    if (uc_reg_write(uc, UC_X86_REG_GDTR, &gdtr) != UC_ERR_OK) return false;
    int cs = 0x08, ds = 0x10;
    uc_reg_write(uc, UC_X86_REG_CS, &cs);
    for (int r : {UC_X86_REG_DS, UC_X86_REG_ES, UC_X86_REG_SS, UC_X86_REG_FS, UC_X86_REG_GS})
        uc_reg_write(uc, r, &ds);
    return true;
}

void UnicornFloor::on_io(IoInCb in_cb, IoOutCb out_cb) {
    io_in_cb_ = std::move(in_cb);
    io_out_cb_ = std::move(out_cb);
    uc_engine* uc = reinterpret_cast<uc_engine*>(uc_);
    // UC_HOOK_INSN with a specific x86 instruction id (IN/OUT). Registered once, when the
    // servicing callbacks are installed.
    if (!hook_in_) {
        uc_hook h = 0;
        uc_hook_add(uc, &h, UC_HOOK_INSN, reinterpret_cast<void*>(&UnicornFloor::in_trampoline),
                    this, 1, 0, UC_X86_INS_IN);
        hook_in_ = from_hook(h);
    }
    if (!hook_out_) {
        uc_hook h = 0;
        uc_hook_add(uc, &h, UC_HOOK_INSN, reinterpret_cast<void*>(&UnicornFloor::out_trampoline),
                    this, 1, 0, UC_X86_INS_OUT);
        hook_out_ = from_hook(h);
    }
}

bool UnicornFloor::map_mmio(uint32_t addr, size_t size, MmioReadCb r, MmioWriteCb w) {
    uc_engine* uc = reinterpret_cast<uc_engine*>(uc_);
    auto region = std::make_unique<MmioRegion>();
    region->self = this;
    region->base = addr;
    region->r = std::move(r);
    region->w = std::move(w);
    MmioRegion* raw = region.get();
    uc_err e = uc_mmio_map(uc, addr, size, &UnicornFloor::mmio_read_trampoline, raw,
                           &UnicornFloor::mmio_write_trampoline, raw);
    if (e != UC_ERR_OK) return false;
    mmio_regions_.push_back(std::move(region));
    return true;
}

void UnicornFloor::request_stop(std::string reason) {
    stop_reason_ = std::move(reason);
    uc_emu_stop(reinterpret_cast<uc_engine*>(uc_));
}

bool UnicornFloor::interrupts_enabled() {
    uc_engine* uc = reinterpret_cast<uc_engine*>(uc_);
    uint32_t eflags = 0;
    uc_reg_read(uc, UC_X86_REG_EFLAGS, &eflags);
    return (eflags & 0x200u) != 0 && !xlle_unicorn_irq_inhibited(uc_);
}

bool UnicornFloor::translate(uint32_t linear, uint32_t& phys) {
    uc_engine* uc = reinterpret_cast<uc_engine*>(uc_);
    uint32_t cr0 = 0;
    uc_reg_read(uc, UC_X86_REG_CR0, &cr0);
    if (!(cr0 & 0x80000000u)) { phys = linear; return true; }  // paging off -> identity
    uint32_t cr3 = 0;
    uc_reg_read(uc, UC_X86_REG_CR3, &cr3);
    uint32_t pde = 0;
    if (uc_mem_read(uc, (cr3 & 0xFFFFF000u) + ((linear >> 22) << 2), &pde, 4) != UC_ERR_OK)
        return false;
    if (!(pde & 1)) return false;
    if (pde & 0x80) {  // 4 MB large page
        phys = (pde & 0xFFC00000u) | (linear & 0x003FFFFFu);
        return true;
    }
    uint32_t pte = 0;
    if (uc_mem_read(uc, (pde & 0xFFFFF000u) + (((linear >> 12) & 0x3FF) << 2), &pte, 4) !=
        UC_ERR_OK)
        return false;
    if (!(pte & 1)) return false;
    phys = (pte & 0xFFFFF000u) | (linear & 0xFFFu);
    return true;
}

bool UnicornFloor::inject_interrupt(uint8_t vector, std::string& err) {
    // Exclusive-time region at the delivery funnel (beads-xbox.1.14.3). Both consumers
    // reach this definition: the chunk-boundary injection in XboxMachine::run (charged out
    // of SLICE_OVERHEAD) and intr_trampoline for a guest INT n (charged out of TCG_EXEC).
    perf_excl::Scope region(perf_excl::kIntrDelivery);
    // Replay a 32-bit protected-mode interrupt-gate entry (same privilege, no error code), the
    // sequence do_interrupt_protected() would run natively (see the interrupt-injection notes).
    // Assumes a flat ring-0 SS (base 0) — true for the Xbox kernel's GDT.
    uc_engine* uc = reinterpret_cast<uc_engine*>(uc_);

    uc_x86_mmr idtr{};
    uc_reg_read(uc, UC_X86_REG_IDTR, &idtr);
    if (static_cast<uint64_t>(vector) * 8 + 7 > idtr.limit) return false;  // gate not in IDT
    uint32_t gate_phys = 0;
    if (!translate(static_cast<uint32_t>(idtr.base) + vector * 8, gate_phys)) return false;
    uint8_t gate[8] = {0};
    if (uc_mem_read(uc, gate_phys, gate, 8) != UC_ERR_OK) return false;
    uint8_t attr = gate[5];
    if (!(attr & 0x80)) return false;  // present bit clear -> nothing to deliver
    uint32_t handler = gate[0] | (gate[1] << 8) | (static_cast<uint32_t>(gate[6]) << 16) |
                       (static_cast<uint32_t>(gate[7]) << 24);
    uint16_t selector = gate[2] | (gate[3] << 8);
    int type = attr & 0x0F;  // 0xE = interrupt gate, 0xF = trap gate

    uint32_t eip = 0, esp = 0, eflags = 0, cs = 0;
    uc_reg_read(uc, UC_X86_REG_EIP, &eip);
    uc_reg_read(uc, UC_X86_REG_ESP, &esp);
    uc_reg_read(uc, UC_X86_REG_EFLAGS, &eflags);
    uc_reg_read(uc, UC_X86_REG_CS, &cs);

    // Push EFLAGS, CS, EIP (dwords), no error code for a hardware IRQ. ESP is a linear address;
    // translate each slot to physical for the write (assumes a flat SS, true for the kernel).
    uint32_t frame[3] = {eflags, cs, eip};
    const char* what[3] = {"irq push eflags", "irq push cs", "irq push eip"};
    for (int i = 0; i < 3; ++i) {
        esp -= 4;
        uint32_t p = 0;
        if (!translate(esp, p) || uc_mem_write(uc, p, &frame[i], 4) != UC_ERR_OK) {
            err = what[i];
            return false;
        }
    }
    uc_reg_write(uc, UC_X86_REG_ESP, &esp);

    uint32_t new_eflags = eflags & ~(0x100u | 0x20000u | 0x10000u | 0x4000u);  // TF,VM,RF,NT
    if ((type & 1) == 0) new_eflags &= ~0x200u;  // interrupt gate clears IF
    uc_reg_write(uc, UC_X86_REG_EFLAGS, &new_eflags);

    uint32_t new_cs = selector & ~3u;  // RPL forced to CPL0
    uc_reg_write(uc, UC_X86_REG_CS, &new_cs);
    uc_reg_write(uc, UC_X86_REG_EIP, &handler);
    return true;
}

// Idle fast-forward support (beads-xbox.1.4.7). No instruction executes here: the caller has
// proved that the guest instructions being skipped cannot change any state it or the guest can
// observe before the next device deadline. What this owes them is that everything DERIVED from
// the retired-instruction clock still moves as if they had run -- which for this floor is
// exactly `seq_` and the guest TSC it publishes. irq_window_stop_ is cleared because no
// uc_emu_start ran, and leaving the previous run's latch set would make the slice census count
// an interrupt-window stop that did not happen.
bool UnicornFloor::advance_instruction_clock(uint64_t insns) {
    if (!uc_) return false;
    seq_ += insns;
    xlle_guest_tsc_ticks = seq_;
    irq_window_stop_ = false;
    stats_.skipped_insns += insns;
    if (insns) stats_.skips++;
    return true;
}

bool UnicornFloor::idle_skip_context_ok(bool irq_shadow) {
    return !insn_mode_ && !mem_cb_ && stop_reason_.empty() && !irq_pending_ &&
           xlle_unicorn_idle_context_ok(uc_, irq_shadow);
}

bool UnicornFloor::read_cached_ram(uint32_t linear, void* data, size_t size, bool fetch) {
    if (!uc_ || !data || !size || uint64_t(linear) + size > (uint64_t(1) << 32)) return false;
    if (fetch && uint64_t(linear) + size - 1 > xlle_unicorn_aot_code_limit(uc_)) return false;
    for (size_t offset = 0; offset < size;) {
        const uint32_t at = linear + static_cast<uint32_t>(offset);
        const size_t part = std::min(size - offset, size_t(4096 - (at & 4095)));
        const auto* ptr = static_cast<const uint8_t*>(xlle_unicorn_aot_ram_ptr(
            uc_, at, part, fetch ? XLLE_UNICORN_AOT_ACCESS_FETCH : XLLE_UNICORN_AOT_ACCESS_READ));
        if (!ptr) return false;
        bool owned = false;
        for (const auto& mapping : host_ram_) {
            const uintptr_t delta = reinterpret_cast<uintptr_t>(ptr) -
                                    reinterpret_cast<uintptr_t>(mapping.host);
            if (delta < mapping.size && part <= mapping.size - delta) { owned = true; break; }
        }
        if (!owned) return false;
        std::memcpy(static_cast<uint8_t*>(data) + offset, ptr, part);
        offset += part;
    }
    return true;
}

uint64_t UnicornFloor::run_unicorn(uint32_t start_eip, uint64_t max_insns, std::string& err) {
    if (!max_insns) return 0;
    if (native_required_ && stop_reason_.empty()) {
        const char* reason = insn_mode_ || mem_cb_ ? "diagnostic-observer" :
            aot_ ? aot_->decline_reason() : "no-native-regions";
        char message[256];
        std::snprintf(message, sizeof(message),
            "native-required fallback refused: pc=0x%08x clock=%llu reason=%s",
            start_eip, static_cast<unsigned long long>(seq_), reason);
        request_stop(message);
        err = message;
        return 0;
    }
    uc_engine* uc = reinterpret_cast<uc_engine*>(uc_);
    const uint64_t before = seq_;
    // request_stop() is also used by servicing callbacks between CPU chunks. In that
    // case no uc_emu_start is active yet, so uc_emu_stop alone cannot do anything;
    // preserve the pending reason and refuse to enter the next chunk. Stop requests
    // are intentionally sticky because continuing after a device/host safety fault
    // would violate the floor's loud-stop contract.
    if (!stop_reason_.empty()) {
        const X86Regs r = get_regs();
        char buf[320];
        std::snprintf(buf, sizeof(buf), "%s (eip=0x%08x, after 0 insns)",
                      stop_reason_.c_str(), r.eip);
        err = buf;
        return 0;
    }
    stop_reason_.clear();
    if (aot_dispatching_) ++aot_->stats.fallback_runs;
    last_fault_addr_ = 0;
    last_fault_type_ = 0;
    irq_window_stop_ = false;
    if (insn_mode_) stats_.insn_mode_runs++; else stats_.block_mode_runs++;
    restore_charge_active_ = false;
    xlle_unicorn_set_restore_callback(uc_, restore_clock, this);
    xlle_guest_tsc_block_clock = !insn_mode_;
    xlle_guest_tsc_prefix = 0;
    xlle_guest_tsc_ticks = seq_;
    run_deadline_ = seq_ + max_insns;
    uc_err e;
    do {
        deadline_tail_ = 0;
        // TCG_EXEC: the SELF time of this region is translated code, Unicorn's internals
        // and the block (or, on the instruction path, UC_HOOK_CODE) trampoline. Everything
        // the guest reaches synchronously from inside uc_emu_start -- MMIO, I/O ports, IDT
        // delivery, and the whole NV2A path behind an MMIO write -- opens its own region and
        // is carved out (beads-xbox.1.14.3). The legacy inclusive `cpu` bucket is preserved
        // beside it so the existing [perf] line keeps its meaning for the comparison tools.
        perf_excl::Scope region(perf_excl::kTcgExec);
        // `count` is ALWAYS 0. A positive count makes Unicorn insert its own UC_HOOK_CODE
        // instruction counter (uc.c:1073-1088) -- the per-instruction global kill this whole
        // change exists to remove -- and, worse, the first count==0 call after it deletes
        // that hook and calls tb_flush (uc.c:1064-1071), which reaches tcg_region_assign's
        // `memset(code_gen_buffer, 0, code_gen_buffer_size)` (qemu/tcg/tcg.c:410) over the
        // whole 1 GiB RESERVED-but-uncommitted code buffer; the lazy-commit vectored handler
        // (translate-all.c:869) cannot satisfy that and the process takes an access
        // violation. Both paths bound the slice with their own hook instead.
        // `timeout` is ALWAYS 0 as well: it reads the host clock (uc.c:1097) and would make
        // the run depend on wall time (PRINCIPLES: guest time is retired instructions).
        if (perf::g_enabled) {
            perf::ScopedAdd t(perf::g_ns_cpu, perf::g_calls_cpu);
            e = uc_emu_start(uc, start_eip, 0, /*timeout=*/0, /*count=*/0);
        } else {
            e = uc_emu_start(uc, start_eip, 0, /*timeout=*/0, /*count=*/0);
        }
        if (e != UC_ERR_OK || !stop_reason_.empty() || !deadline_tail_) break;
        if (!xlle_unicorn_limit_next_tb(uc_, deadline_tail_)) {
            request_stop("CPU: cannot bound translated tail at instruction deadline");
            break;
        }
        ++stats_.deadline_splits;
        start_eip = get_regs().eip;
    } while (true);
    xlle_unicorn_set_restore_callback(uc_, nullptr, nullptr);
    if (!stop_reason_.empty()) {
        // A device/servicing callback requested a loud stop; its reason wins over Unicorn's
        // generic "stopped" code.
        X86Regs r = get_regs();
        char buf[320];
        std::snprintf(buf, sizeof(buf), "%s (eip=0x%08x, after %llu insns)", stop_reason_.c_str(),
                      r.eip, static_cast<unsigned long long>(seq_ - before));
        err = buf;
    } else if (e != UC_ERR_OK) {
        X86Regs r = get_regs();
        char buf[320];
        if (last_fault_addr_) {
            const char* what = last_fault_type_ == UC_MEM_WRITE_UNMAPPED ? "write"
                             : last_fault_type_ == UC_MEM_FETCH_UNMAPPED ? "fetch" : "read";
            std::snprintf(buf, sizeof(buf),
                          "unicorn fault: %s at unmapped %s addr=0x%08x (eip=0x%08x, after %llu "
                          "insns)", uc_strerror(e), what, last_fault_addr_, r.eip,
                          static_cast<unsigned long long>(seq_ - before));
        } else {
            std::snprintf(buf, sizeof(buf), "unicorn fault: %s (eip=0x%08x, after %llu insns)",
                          uc_strerror(e), r.eip,
                          static_cast<unsigned long long>(seq_ - before));
        }
        err = buf;
    }
    return seq_ - before;
}

// Cold precise restoration after a fault or self-modifying-code restart.
void UnicornFloor::restore_clock(void* opaque, const xlle_unicorn_restore_record* r) {
    auto* self = static_cast<UnicornFloor*>(opaque);
    if (!self->restore_charge_active_ || r->completed >= r->total || !self->stats_.tb_entries ||
        self->block_pc_ring_[(self->stats_.tb_entries - 1) & (kBlockPcRing - 1)] != r->pc) return;
    self->restore_charge_active_ = false;
    const uint64_t unretired = self->insn_mode_ ? 1 : r->total - r->completed;
    if (self->seq_ < unretired) {
        self->request_stop("Unicorn restore exceeds charged instruction clock");
        return;
    }
    self->seq_ -= unretired;
    if (!self->insn_mode_) self->stats_.tb_insns -= unretired;
    self->stats_.fault_unretired += unretired;
#ifdef XLLE_CPU_BLOCK_CENSUS
    if (!self->insn_mode_)
        if (auto* row = self->census_row(static_cast<uint32_t>(r->pc), r->size))
            row->unretired += unretired;
#endif
    xlle_guest_tsc_ticks = self->seq_;
}

// Per-instruction diagnostics; never installed on the canonical block path.
void UnicornFloor::code_trampoline(uc_struct* uc, uint64_t addr, uint32_t size, void* user) {
    auto* self = static_cast<UnicornFloor*>(user);
    self->restore_charge_active_ = false;
    // Slice budget, enforced here instead of by Unicorn's count hook so that count is never
    // passed as > 0 (see run()). The hook fires BEFORE `addr` executes, so stopping now
    // retires exactly run_deadline_ - run_start instructions, the same as the count hook.
    if (self->seq_ >= self->run_deadline_) {
        uc_emu_stop(reinterpret_cast<uc_engine*>(uc));
        return;
    }
    // If an IRQ is waiting, stop the moment the guest has interrupts enabled so the controller
    // can deliver it at this boundary (before `addr` executes). This catches the narrow window
    // the idle loop's `sti; nop; cli` opens each iteration — a coarse chunk boundary misses it.
    if (self->irq_pending_) {
        uint32_t eflags = 0;
        uc_reg_read(reinterpret_cast<uc_engine*>(uc), UC_X86_REG_EFLAGS, &eflags);
        if ((eflags & 0x200u) && !xlle_unicorn_irq_inhibited(uc)) {
            self->irq_window_stop_ = true;
            uc_emu_stop(reinterpret_cast<uc_engine*>(uc));
            return;
        }
    }
    if (addr >= self->entry_observer_min_ && addr <= self->entry_observer_max_) {
        self->notify_entry(static_cast<uint32_t>(addr));
        if (!self->stop_reason_.empty()) return;
    }
    xlle_guest_tsc_ticks = self->seq_;
    RetiredInsn ev{self->seq_++, static_cast<uint32_t>(addr), size};
    self->restore_charge_active_ = true;
    if (self->retired_cb_) self->retired_cb_(ev);
}

// Translation-time report of a newly generated block. Called from `tb_find` BEFORE the block
// can execute, so the table is always populated by the time the block hook needs it.
void UnicornFloor::edge_trampoline(uc_struct*, void* cur_tb, void* prev_tb, void* user) {
    auto* self = static_cast<UnicornFloor*>(user);
    const uc_tb* cur = static_cast<const uc_tb*>(cur_tb);
    self->icount_insert(static_cast<uint32_t>(cur->pc), cur->size, cur->icount);
    // The pinned build reports the first TB with a null predecessor. Refresh a
    // predecessor when present as well (including a regenerated identity).
    if (prev_tb) {
        const uc_tb* prev = static_cast<const uc_tb*>(prev_tb);
        self->icount_insert(static_cast<uint32_t>(prev->pc), prev->size, prev->icount);
    }
}

// Canonical scheduling point: fires once per translation block, before `gen_tb_start`, so a
// stop here retires none of the block. Everything the old per-instruction hook did -- the
// instruction clock, the slice deadline and the interrupt window -- happens here instead.
void UnicornFloor::block_trampoline(uc_struct* uc, uint64_t addr, uint32_t size, void* user) {
    auto* self = static_cast<UnicornFloor*>(user);
    self->restore_charge_active_ = false;
    uc_engine* engine = reinterpret_cast<uc_engine*>(uc);
    const uint32_t pc = static_cast<uint32_t>(addr);
    // Always-on block-PC ring (PRINCIPLES §7). One store, no branch; read backwards after a
    // fault. It records entry attempts, including the block a stop lands in front of.
    self->block_pc_ring_[self->stats_.tb_entries & (kBlockPcRing - 1)] = pc;
    self->stats_.tb_entries++;
    if (self->insn_mode_) return;  // the per-instruction hook owns the clock on that path

    // A device or host callback already asked for a loud stop. With exactly one block hook
    // installed Unicorn inlines the call (uc_add_inline_hook, tcg.c:666) and therefore skips
    // helper_uc_tracecode's own stop_request guard, so the check has to happen here. The
    // block will not execute: `gen_tb_start`'s exit-request branch follows immediately.
    if (!self->stop_reason_.empty()) return;

    // AOT dispatch happens outside uc_emu_start, before this TB is charged.
    // A declined entry gets one fallback block to make forward progress. A cold
    // MMU entry is thus populated by real execution, including permission/A-D
    // handling. All blocks, including the first, have a known count at entry.
    if (self->aot_dispatching_) {
        if (self->aot_bypass_entry_) self->aot_bypass_entry_ = false;
        else if (self->seq_ > self->aot_fallback_start_ &&
                 self->seq_ < self->run_deadline_ &&
                 self->aot_->contains(pc)) {
            self->aot_handoff_ = true;
            uc_emu_stop(engine);
            return;
        }
    }

    const uint16_t sz = static_cast<uint16_t>(size);
    uint32_t icount;
    if (pc == self->last_pc_ && sz == self->last_size_) {
        icount = self->last_icount_;
    } else {
        icount = self->icount_lookup(pc, sz);
        if (icount == 0) {
            // Cannot advance the clock exactly. Stop loudly rather than guess: an approximate
            // instruction count would silently move every device deadline (PRINCIPLES §3).
            self->stats_.icount_misses++;
            char b[192];
            std::snprintf(b, sizeof(b),
                          "block icount unknown at pc=0x%08x size=%u (table entries=%llu, "
                          "misses=%llu) — retired-instruction clock cannot advance exactly",
                          pc, size,
                          static_cast<unsigned long long>(self->stats_.icount_entries),
                          static_cast<unsigned long long>(self->stats_.icount_misses));
            self->stop_reason_ = b;
            uc_emu_stop(engine);
            return;
        }
        self->last_pc_ = pc;
        self->last_size_ = sz;
        self->last_icount_ = static_cast<uint16_t>(icount);
    }

    // Stop BEFORE a block that would cross the deadline, including the first.
    // run_unicorn resumes the remaining prefix using QEMU's bounded translation;
    // those instructions actually execute and get charged by this same hook.
    if (self->seq_ + icount > self->run_deadline_) {
        self->stats_.deadline_stops++;
        self->deadline_tail_ = self->seq_ < self->run_deadline_
            ? static_cast<uint32_t>(self->run_deadline_ - self->seq_) : 0;
        uc_emu_stop(engine);
        return;
    }

    // Interrupt window. IF can only transition 0->1 at a block boundary in the harvested
    // translator (`sti` ends the block via gen_eob_inhibit_irq, `popf`/`iret` via gen_eob,
    // each after gen_jmp_im has synced EIP), and set_irq_pending() is itself only called
    // between slices, i.e. at a block boundary. Checking here therefore lands the interrupt
    // at the same instruction the per-instruction poll did, including the idle loop's
    // `sti; nop; nop; cli` window. Not exempt at the first block of a slice: an already-open
    // window must be taken immediately, exactly as the per-instruction hook did.
    if (self->irq_pending_) {
        uint32_t eflags = 0;
        uc_reg_read(engine, UC_X86_REG_EFLAGS, &eflags);
        if ((eflags & 0x200u) && !xlle_unicorn_irq_inhibited(uc)) {
            self->irq_window_stop_ = true;
            self->stats_.irq_window_stops++;
            uc_emu_stop(engine);
            return;
        }
    }

    if (pc >= self->entry_observer_min_ && pc <= self->entry_observer_max_) {
        self->notify_entry(pc);
        if (!self->stop_reason_.empty()) return;
    }

    // Publish the guest time-stamp counter as of this block's first instruction. The
    // translated TSC read/write adds its exact instruction index (unicorn_metrics.c).
    // Publishing the entry count avoids exposing the precharged block suffix.
    xlle_guest_tsc_ticks = self->seq_;
    self->seq_ += icount;
    self->stats_.tb_insns += icount;
#ifdef XLLE_CPU_BLOCK_CENSUS
    self->census_charge(pc, sz, icount, 1);
#endif
    self->restore_charge_active_ = true;
}

void UnicornFloor::mem_trampoline(uc_struct*, int type, uint64_t addr, int size, int64_t value,
                                  void* user) {
    auto* self = static_cast<UnicornFloor*>(user);
    if (!self->mem_cb_) return;
    MemEvent ev{};
    ev.seq = self->seq_;
    ev.write = (type == UC_MEM_WRITE) ? 1 : 0;
    ev.addr = static_cast<uint32_t>(addr);
    ev.size = static_cast<uint8_t>(size);
    ev.value = static_cast<uint64_t>(value);
    self->mem_cb_(ev);
}

uint32_t UnicornFloor::in_trampoline(uc_struct*, uint32_t port, int size, void* user) {
    auto* self = static_cast<UnicornFloor*>(user);
    return self->io_in_cb_ ? self->io_in_cb_(static_cast<uint16_t>(port), size) : 0;
}

void UnicornFloor::out_trampoline(uc_struct*, uint32_t port, int size, uint32_t value,
                                  void* user) {
    auto* self = static_cast<UnicornFloor*>(user);
    if (self->io_out_cb_) self->io_out_cb_(static_cast<uint16_t>(port), size, value);
}

void UnicornFloor::intr_trampoline(uc_struct*, uint32_t intno, void* user) {
    auto* self = static_cast<UnicornFloor*>(user);
    if (self->intr_cb_) self->intr_cb_(intno);

    // Vectors whose architectural frame includes an error code (#DF, #TS, #NP, #SS, #GP,
    // #PF, #AC). inject_interrupt() builds the no-error-code frame, and for #PF CR2 would
    // also matter — deliver those only once a real boot path needs them (characterize first).
    // Note: for software INTs Unicorn has already advanced EIP to the next instruction (the
    // architecturally correct return address); for faults EIP stays at the faulting
    // instruction, which is likewise what the frame should hold.
    switch (intno) {
        case 8: case 10: case 11: case 12: case 13: case 14: case 17: {
            char b[96];
            std::snprintf(b, sizeof(b),
                          "guest CPU exception vector %u (error-code class) — delivery unmodeled",
                          intno);
            self->request_stop(b);
            return;
        }
        default:
            break;
    }
    std::string err;
    if (!self->inject_interrupt(static_cast<uint8_t>(intno & 0xFF), err)) {
        char b[128];
        std::snprintf(b, sizeof(b), "guest vector %u: IDT delivery failed (%s)", intno,
                      err.empty() ? "gate absent/invalid" : err.c_str());
        self->request_stop(b);
    }
}

bool UnicornFloor::unmapped_trampoline(uc_struct*, int type, uint64_t addr, int, int64_t,
                                       void* user) {
    auto* self = static_cast<UnicornFloor*>(user);
    self->last_fault_addr_ = static_cast<uint32_t>(addr);
    self->last_fault_type_ = type;
    return false;  // do not resolve: let Unicorn take the fault (we only record it)
}

uint64_t UnicornFloor::mmio_read_trampoline(uc_struct*, uint64_t offset, unsigned size,
                                            void* user) {
    auto* reg = static_cast<MmioRegion*>(user);
    return reg->r ? reg->r(reg->base + static_cast<uint32_t>(offset), static_cast<int>(size)) : 0;
}

void UnicornFloor::mmio_write_trampoline(uc_struct*, uint64_t offset, unsigned size,
                                         uint64_t value, void* user) {
    auto* reg = static_cast<MmioRegion*>(user);
    if (reg->w)
        reg->w(reg->base + static_cast<uint32_t>(offset), static_cast<int>(size),
               static_cast<uint32_t>(value));
}

// Silence unused-parameter warnings for the hook handles we retain for lifetime clarity.
static_assert(sizeof(void*) >= sizeof(uc_hook), "uc_hook must fit in void* storage");

}  // namespace xlle
