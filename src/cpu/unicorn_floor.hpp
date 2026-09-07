// xlle — UnicornFloor: CPU floor backed by the harvested QEMU x86 core (via Unicorn).
#pragma once
#include "cpu/unicorn_aot_bridge.h"
#include <cstddef>
#include <memory>
#include <string>
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

    uint64_t run(uint32_t start_eip, uint64_t max_insns, std::string& err) override;

    // Compiled artifacts and hooks must outlive the floor. Configure between runs.
    bool register_aot_region(const xlle_aot_region& region);
    bool register_aot_replacement(const AotReplacement& replacement);
    void set_aot_profile(uint32_t profile);
    AotExecutionStats aot_stats() const;

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
        uint64_t bootstrap_blocks = 0;  // uncharged-then-repaid first-block executions (<= a few)
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

private:
    friend class AotExecutor;
    uint64_t run_unicorn(uint32_t start_eip, uint64_t max_insns, std::string& err);
    struct HostRamMapping { uint32_t base; size_t size; uint8_t* host; };
    std::vector<HostRamMapping> host_ram_;
    std::unique_ptr<AotExecutor> aot_;
    bool aot_dispatching_ = false;
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
    // Charge the first-ever block once its icount becomes known (see the .cpp).
    void settle_bootstrap_block();
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
    uint64_t tbs_this_run_ = 0;     // blocks entered since uc_emu_start (forward-progress guard)
    // The one block the edge hook cannot report before it executes: the first ever, since
    // UC_HOOK_EDGE_GENERATED only fires once uc->last_tb is set. It runs uncharged and is
    // charged as soon as its icount arrives (as the edge hook's prev_tb on the next block's
    // generation). Bounded: more than kBootstrapPendingMax re-entries is a loud stop.
    static constexpr uint64_t kBootstrapPendingMax = 4096;
    uint32_t boot_pc_ = 0;
    uint16_t boot_size_ = 0;
    uint64_t boot_pending_ = 0;  // executions of that block still uncharged
    bool boot_claimed_ = false;  // the bootstrap allowance has been handed out
    bool boot_settled_ = false;  // ... and repaid
    std::unique_ptr<IcountSlot[]> icount_;
    std::unique_ptr<uint32_t[]> block_pc_ring_;
    BlockStats stats_{};
    // One-entry inline cache in front of the icount table: consecutive block entries repeat
    // heavily (the idle loop cycles through three blocks).
    uint32_t last_pc_ = 0;
    uint16_t last_size_ = 0;
    uint16_t last_icount_ = 0;
};

}  // namespace xlle
