// xlle — UnicornFloor: CPU floor backed by the harvested QEMU x86 core (via Unicorn).
#pragma once
#include "cpu/unicorn_aot_bridge.h"
#include <cstddef>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "xlle/cpu_floor.hpp"
#include "xlle/aot_execution.hpp"

struct uc_struct;  // unicorn's uc_engine (opaque)

namespace xlle {
class AotExecutor;

class UnicornFloor : public CpuFloor {
public:
    UnicornFloor();
    ~UnicornFloor() override;

    const char* name() const override { return "unicorn"; }

    bool map_mem(uint32_t addr, size_t size) override;
    bool map_mem_ptr(uint32_t addr, size_t size, void* host) override;
    bool write_mem(uint32_t addr, const void* data, size_t size) override;
    bool read_mem(uint32_t addr, void* data, size_t size) override;

    void set_regs(const X86Regs& r) override;
    X86Regs get_regs() override;

    bool read_reg(XReg r, void* buf, size_t n) override;
    bool write_reg(XReg r, const void* buf, size_t n) override;
    bool setup_flat_segments(uint32_t gdt_addr) override;

    // Installing a retired-instruction observer switches the floor to per-instruction
    // scheduling and installs UC_HOOK_CODE. Without one the floor stays on the block path
    // and no per-instruction hook is ever added (see the class comment in the .cpp).
    void on_retired(RetiredCb cb) override;
    void on_mem(MemCb cb) override;
    void on_interrupt(IntrCb cb) override { intr_cb_ = std::move(cb); }
    void on_io(IoInCb in_cb, IoOutCb out_cb) override;
    bool map_mmio(uint32_t addr, size_t size, MmioReadCb r, MmioWriteCb w) override;
    void request_stop(std::string reason) override;
    bool interrupts_enabled() override;
    void set_irq_pending(bool pending) override { irq_pending_ = pending; }
    bool stopped_for_irq_window() const override { return irq_window_stop_; }
    bool inject_interrupt(uint8_t vector, std::string& err) override;

    // Translate a guest linear address to physical via the guest page tables (32-bit paging;
    // identity if paging is off). uc_mem_read/write operate on physical memory, so interrupt
    // delivery must translate the IDT and stack addresses itself.
    bool translate(uint32_t linear, uint32_t& phys) override;

    bool advance_instruction_clock(uint64_t insns) override;
    bool idle_skip_context_ok(bool irq_shadow) override;
    bool read_cached_ram(uint32_t linear, void* data, size_t size, bool fetch) override;

    uint64_t run(uint32_t start_eip, uint64_t max_insns, std::string& err) override;

    // Compiled artifacts and hooks must outlive the floor. Configure between runs.
    bool register_aot_region(const xlle_aot_region& region);
    bool register_aot_replacement(const AotReplacement& replacement);
    void set_aot_profile(uint32_t profile);
    // Configure after registration, between runs; false retains the public API.
    void set_aot_bulk_registers(bool enabled);
    // Validation scope starts at the next run boundary. The centralized fallback
    // entry refuses guest execution even with no AOT regions or with observers.
    void require_native_execution(bool required) { native_required_ = required; }
    bool native_execution_required() const { return native_required_; }
    AotExecutionStats aot_stats() const;
    std::vector<AotRegionStats> aot_region_stats() const;

    bool ok() const { return uc_ != nullptr; }
    void hook_region_counts(uint64_t& code_regions, uint64_t& count_regions,
                            uint64_t& block_regions) const;

    // Force the per-instruction scheduling path (UC_HOOK_CODE, one stop check per
    // instruction) without installing a retired-instruction observer. This is the toggle-off
    // arm of the block-tier change: the same binary, the same device models, the same guest
    // workload -- only the CPU floor's scheduling granularity differs. Must be called before
    // the first run(). See --cpu-insn-hook in src/app/main.cpp.
    void force_insn_scheduling() { set_insn_mode(true); }
    bool insn_scheduling() const { return insn_mode_; }

    // Block-tier accounting, exported with its own denominators (PERF-METHODOLOGY rule 9).
    struct BlockStats {
        uint64_t tb_entries = 0;      // translation blocks entered (block-hook calls)
        uint64_t tb_insns = 0;        // instructions charged by those entries
        uint64_t icount_entries = 0;  // live (pc,size)->icount table entries
        uint64_t icount_misses = 0;   // block-hook lookups with no table entry (must stay 0)
        uint64_t icount_evictions = 0;  // table inserts that displaced a different identity
        uint64_t deadline_stops = 0;  // slices ended by the block hook at the budget
        uint64_t irq_window_stops = 0;  // slices ended by the block hook on an open IF window
        uint64_t insn_mode_runs = 0;  // run() calls that used the per-instruction path
        uint64_t block_mode_runs = 0;
        uint64_t bootstrap_blocks = 0;  // legacy report field; all TBs now charged before execution
        uint64_t deadline_splits = 0;   // bounded translated tails, no instruction hooks
        // Instructions added to the clock by advance_instruction_clock() (idle fast-forward).
        // In block mode, tb_insns + skipped_insns + aot_stats().retired equals
        // the machine's retired total; these three sources are disjoint.
        uint64_t skipped_insns = 0;
        uint64_t skips = 0;
        uint64_t fault_unretired = 0;  // charged suffix removed on fault/SMC restart
    };
    BlockStats block_stats() const;

    // Always-on ring of the last kBlockPcRing translation-block entry PCs (PRINCIPLES §7:
    // the ring records continuously; observers read backwards, they never arm it).
    static constexpr size_t kBlockPcRing = 8192;
    const uint32_t* block_pc_ring() const { return block_pc_ring_.get(); }
    uint64_t block_pc_ring_writes() const { return stats_.tb_entries; }
    // Only available in attribution builds; counts executed TBs, not sampled slices.
    bool write_block_census(const char* path) const;

private:
    friend class AotExecutor;
    uint64_t run_unicorn(uint32_t start_eip, uint64_t max_insns, std::string& err);
    struct HostRamMapping { uint32_t base; size_t size; uint8_t* host; };
    std::vector<HostRamMapping> host_ram_;
    std::unique_ptr<AotExecutor> aot_;
    bool aot_dispatching_ = false;
    bool native_required_ = false;
    bool aot_bypass_entry_ = false;
    bool aot_handoff_ = false;
    uint64_t aot_fallback_start_ = 0;
    bool restore_charge_active_ = false;
    static void restore_clock(void*, const xlle_unicorn_restore_record*);
    // C-ABI trampolines Unicorn calls; they recover `this` from user_data.
    static void code_trampoline(uc_struct*, uint64_t addr, uint32_t size, void* user);
    static void block_trampoline(uc_struct*, uint64_t addr, uint32_t size, void* user);
    static void edge_trampoline(uc_struct*, void* cur_tb, void* prev_tb, void* user);
    static void mem_trampoline(uc_struct*, int type, uint64_t addr, int size, int64_t value,
                               void* user);
    static uint32_t in_trampoline(uc_struct*, uint32_t port, int size, void* user);
    static void out_trampoline(uc_struct*, uint32_t port, int size, uint32_t value, void* user);
    static bool unmapped_trampoline(uc_struct*, int type, uint64_t addr, int size, int64_t value,
                                    void* user);
    static void intr_trampoline(uc_struct*, uint32_t intno, void* user);

    // One MMIO region + its callbacks. Held type-erased behind a small heap record whose
    // address is the user_data Unicorn hands back to the MMIO trampolines.
    struct MmioRegion {
        UnicornFloor* self;
        uint32_t base;  // absolute region base; Unicorn hands the callback a region offset
        MmioReadCb r;
        MmioWriteCb w;
    };
    static uint64_t mmio_read_trampoline(uc_struct*, uint64_t offset, unsigned size, void* user);
    static void mmio_write_trampoline(uc_struct*, uint64_t offset, unsigned size, uint64_t value,
                                      void* user);

    // (pc,size) -> icount, filled by UC_HOOK_EDGE_GENERATED at translation time and read by
    // the block hook at execution time. (pc,size) is a sound key: the same byte range decoded
    // from the same start address always holds the same number of instructions, and a block
    // whose bytes changed is regenerated (firing the edge hook again) before it can be
    // re-entered. icount == 0 marks an empty slot -- no real block has zero instructions.
    struct IcountSlot {
        uint32_t pc = 0;
        uint16_t size = 0;
        uint16_t icount = 0;
    };
    static constexpr size_t kIcountSlots = 1u << 18;  // 2 MiB; ~37k live block identities
    static constexpr size_t kIcountProbes = 8;
    static inline size_t icount_hash(uint32_t pc) {
        return (static_cast<size_t>(pc) * 2654435761u >> 4) & (kIcountSlots - 1);
    }
    void icount_insert(uint32_t pc, uint16_t size, uint16_t icount);
    uint32_t icount_lookup(uint32_t pc, uint16_t size) const;

    // Per-instruction scheduling is opt-in for observers/diagnostics.
    void set_insn_mode(bool on);

    uc_struct* uc_ = nullptr;
    void* hook_code_ = nullptr;  // uc_hook is uintptr-sized; stored type-erased
    void* hook_block_ = nullptr;
    void* hook_edge_ = nullptr;
    void* hook_mem_ = nullptr;
    void* hook_in_ = nullptr;
    void* hook_out_ = nullptr;
    void* hook_unmapped_ = nullptr;
    void* hook_intr_ = nullptr;
    uint32_t last_fault_addr_ = 0;  // most recent unmapped-access address (for fault reporting)
    int last_fault_type_ = 0;
    RetiredCb retired_cb_;
    MemCb mem_cb_;
    IntrCb intr_cb_;
    IoInCb io_in_cb_;
    IoOutCb io_out_cb_;
    std::vector<std::unique_ptr<MmioRegion>> mmio_regions_;
    std::string stop_reason_;  // set by request_stop(); surfaced by run()
    bool irq_pending_ = false;  // an external IRQ is waiting; stop when IF opens (see set_irq_pending)
    bool irq_window_stop_ = false;  // the last run() ended on an open IF window
    bool insn_mode_ = false;        // per-instruction hook installed and counting
    uint64_t seq_ = 0;              // retired-instruction clock; also the ring sequence
    uint64_t run_deadline_ = 0;     // absolute seq_ value this run() may not exceed
    uint32_t deadline_tail_ = 0;   // restart the stopped TB with this instruction limit
    std::unique_ptr<IcountSlot[]> icount_;
    // A displaced entry may still have live translated code. Retain its exact
    // count until a generation callback replaces the same identity; eviction
    // from our fast table does not force QEMU to regenerate that block.
    std::unordered_map<uint64_t, uint16_t> icount_spill_;
    std::unique_ptr<uint32_t[]> block_pc_ring_;
    BlockStats stats_{};
#ifdef XLLE_CPU_BLOCK_CENSUS
    struct CensusRow {
        uint32_t pc = 0, size = 0;
        uint64_t entries = 0, charged = 0, unretired = 0;
        std::vector<uint8_t> first_code;
    };
    std::unique_ptr<CensusRow[]> census_;
    std::unordered_map<uint64_t, CensusRow> census_spill_;
    CensusRow* census_last_ = nullptr;
    uint64_t census_overflow_ = 0;
    std::string census_run_id_; // host-only provenance for cumulative snapshot pairing
    CensusRow* census_row(uint32_t pc, uint32_t size);
    void census_charge(uint32_t pc, uint32_t size, uint64_t insns, uint64_t entries);
#endif
    // One-entry inline cache in front of the icount table: consecutive block entries repeat
    // heavily (the idle loop cycles through three blocks).
    uint32_t last_pc_ = 0;
    uint16_t last_size_ = 0;
    uint16_t last_icount_ = 0;
};

}  // namespace xlle
