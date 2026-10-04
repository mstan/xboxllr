// xlle — XboxMachine: the guest memory map (and, later, devices) around the CPU floor.
// A2 foundation. Layout: 64 MB unified RAM at 0x0; the flash BIOS mirrored across
// 0xFF000000-0xFFFFFFFF (256KB image, 64 mirrors in the 16MB window).
#pragma once
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "machine/ide.hpp"
#include "machine/io_device.hpp"
#include "machine/mmio_device.hpp"
#include "machine/nv2a.hpp"
#include "machine/nv2a_renderer.hpp"
#include "machine/nvnet.hpp"
#include "machine/ohci.hpp"
#include "machine/pci.hpp"
#include "machine/pic.hpp"
#include "machine/pit.hpp"
#include "machine/pm.hpp"
#include "machine/smbus.hpp"
#include "xlle/cpu_floor.hpp"
#include "xlle/aot_execution.hpp"

namespace xlle {

class McpxApu;
class McpxAci;

class XboxMachine {
public:
    static constexpr uint32_t kRamSize     = 64u * 1024 * 1024;  // 0x0400_0000
    static constexpr uint32_t kFlashBase    = 0xFF000000u;
    static constexpr uint32_t kFlashWindow  = 0x01000000u;       // 16MB, ends at 0xFFFFFFFF

    struct Config {
        // Production stays on the captured root-attached XID model.  The
        // synthetic hub is an explicit, test-only opt-in selected before
        // device construction.
        Ohci::UsbTopology ohci0_topology = Ohci::UsbTopology::kDirectXid;
        // Non-owning: the host owns its backend and must outlive this machine.
        Nv2aRenderer* nv2a_renderer = nullptr;
        // CPU execution slice size. 4096 is the conservative LLE floor used by
        // existing gates; larger values are an explicit speed/iteration knob.
        uint64_t cpu_chunk_insns = 4096;
    };

    XboxMachine();
    explicit XboxMachine(Config config);
    bool ok() const;

    bool init_ram();                              // map 64 MB at 0x0
    bool load_flash(const std::string& path);     // map + mirror the flash image into the window
    bool load_blob(uint32_t addr, const std::string& path);  // load a raw blob into mapped RAM
    void init_devices();  // build the I/O fabric (PCI host, SMBus+SMC) and wire dispatch
    // Attach the FATX HDD image to IDE drive 0 (call after init_devices). Without it the
    // drive reports absent and the kernel cannot mount the boot device.
    // writeback=false (the default) attaches the image copy-on-write: guest writes are held
    // in memory for the run and the file is never modified, so consecutive replays read the
    // same disk. See the write-policy note in ide.hpp (beads-xbox.1.14.11).
    bool attach_hdd(const std::string& path, bool writeback = false);
    // Attach the config EEPROM dump (256 bytes) as the SMBus slave at 0x54 (call after
    // init_devices). The kernel HMAC-verifies it and derives the HDD key from it.
    bool attach_eeprom(const std::string& path);

    bool read(uint32_t addr, void* buf, size_t n);
    uint64_t run(uint32_t start_eip, uint64_t max_insns, std::string& err);

    CpuFloor& cpu() { return *cpu_; }
    // Register before execution. Immutable descriptors/code must outlive this machine.
    bool register_aot_region(const xlle_aot_region& region);
    bool register_aot_replacement(const AotReplacement& replacement);
    void set_aot_profile(uint32_t profile);
    AotExecutionStats aot_stats() const;
    std::vector<AotRegionStats> aot_region_stats() const;
    size_t flash_size() const { return flash_size_; }
    uint64_t total_insns() const { return total_insns_; }
    // Relaxed mirror of total_insns_ for host probes (e.g. the sampled
    // profiler) that read progress from another thread without locking.
    std::atomic<uint64_t>& progress_insns() { return progress_insns_atomic_; }

    // Observability: fired for every serviced I/O-port access (see IoEvent). Installed once.
    void on_io_event(std::function<void(const IoEvent&)> cb) { io_event_cb_ = std::move(cb); }
    // Observability: fired for every serviced MMIO access (MemEvent-shaped; write flag reused).
    void on_mmio_event(std::function<void(const MemEvent&)> cb) { mmio_event_cb_ = std::move(cb); }
    // Optional host-service callback, paced at 1 kHz of guest time between CPU chunks.
    // Used by nonblocking transports that must only touch guest memory on the emulation thread.
    void on_host_poll(std::function<void()> cb) { host_poll_cb_ = std::move(cb); }
    PciHost* pci() { return pci_; }
    IdeController* ide() { return ide_; }
    Nv2a* nv2a() { return nv2a_; }
    NvNet* nvnet() { return nvnet_; }
    McpxApu* apu() { return apu_; }
    McpxAci* aci() { return aci_; }
    Ohci* ohci0() { return ohci0_; }
    uint64_t injected_interrupts() const { return injected_; }
    uint64_t irq0_pulses() const { return irq0_pulses_; }
    uint64_t pending_boundaries() const { return pending_boundaries_; }
    DualPic* pic_dbg() { return pic_; }

    // Idle census: always-on, slice-granularity accounting of where the guest is when a CPU
    // slice ends. One EIP compare, one small table walk and one 4-byte guest read per slice
    // (i.e. per cpu_chunk_insns_ retired instructions), so the per-instruction cost is nil and
    // no guest-visible behavior changes. Its denominators (slices/insns) are exported with it.
    static constexpr size_t kIdleCensusPages = 64;    // distinct 4KB boundary-EIP pages tracked
    static constexpr size_t kIdleCensusThreads = 32;  // distinct KTHREAD pointers tracked
    struct IdleCensus {
        uint64_t slices = 0;       // slice boundaries observed
        uint64_t insns = 0;        // instructions retired across those slices
        uint64_t idle_slices = 0;  // boundary EIP inside the idle range
        uint64_t idle_insns = 0;   // instructions retired by slices ending in the idle range
        uint64_t early_stops = 0;  // slices the floor ended on an open guest interrupt window
        uint64_t early_stops_irq_armed = 0;  // ... of those, entered with an IRQ armed
        uint64_t budget_slack = 0;  // instructions left unused by block-granularity slicing
        uint64_t thread_reads = 0;           // slices where CurrentThread read as a kernel ptr
        struct PageBucket {
            uint32_t page = 0;
            uint64_t slices = 0;
            uint64_t insns = 0;
        };
        struct ThreadBucket {
            uint32_t thread = 0;
            uint64_t slices = 0;
            uint64_t insns = 0;
        };
        PageBucket pages[kIdleCensusPages]{};
        ThreadBucket threads[kIdleCensusThreads]{};
        uint32_t page_count = 0;
        uint32_t thread_count = 0;
        uint64_t pages_dropped = 0;    // slices whose page did not fit the table
        uint64_t threads_dropped = 0;  // slices whose thread did not fit the table
        // --- idle fast-forward (beads-xbox.1.4.7) ------------------------------------
        uint64_t ff_skips = 0;       // slices satisfied by the skip instead of by execution
        uint64_t ff_insns = 0;       // instructions the skip retired without executing
        uint64_t ff_iters = 0;       // idle-loop iterations those instructions represent
        uint64_t ff_max_insns = 0;   // largest single skip
        uint64_t ff_at_head = 0;     // slice boundaries at a loop phase with the flag on
        uint64_t ff_phase[8] = {};  // skips at each instruction boundary in loop order
        uint64_t ff_rej_context = 0; // hidden state, observers, debug or pending stop
        uint64_t ff_rej_flags = 0;   // ... rejected: EFLAGS not the loop's fixed point
        uint64_t ff_rej_state = 0;   // ... rejected: DPC list non-empty / next thread queued
        uint64_t ff_rej_short = 0;   // ... rejected: fewer than one whole iteration to the deadline
        uint64_t ff_rej_read = 0;    // ... rejected: a guest read/translate failed
        uint64_t ff_rej_bytes = 0;   // ... rejected: the loop bytes are not the pinned shape
    };
    const IdleCensus& idle_census() const { return idle_census_; }
    // Idle range is kernel-build specific: the default is the 4983 kernel's NT idle thread loop
    // (0x8001e3ce-0x8001e3ff: sti; nop; nop; cli; cmp ebp,[ebp]; je, plus the DPC dispatch call).
    void set_idle_eip_range(uint32_t lo, uint32_t hi) { idle_eip_lo_ = lo; idle_eip_hi_ = hi; }
    uint32_t idle_eip_lo() const { return idle_eip_lo_; }
    uint32_t idle_eip_hi() const { return idle_eip_hi_; }

    // Smallest instruction count > `insns` at which any device clock derived from the
    // retired-instruction count changes value. Public so a guard test can pin the set.
    uint64_t next_device_deadline(uint64_t insns) const;

    // ---- idle fast-forward (beads-xbox.1.4.7) -----------------------------------------
    // The kernel-4983 NT idle loop, verified byte for byte before every skip:
    //   8001e3ce fb              sti
    //   8001e3cf 90              nop            <- the sti inhibit-IRQ block
    //   8001e3d0 90              nop            <- IF open, shadow cleared
    //   8001e3d1 fa              cli            <- IF still open before CLI executes
    //   8001e3d2 3b 6d 00        cmp  ebp,[ebp]  ; DpcListHead empty <=> [ebp] == ebp
    //   8001e3d5 74 0c           je   8001e3e3
    //   8001e3d7 b1 02           mov  cl,2       ; DPC path -- NOT taken while idle
    //   8001e3d9 e8 de 8d ff ff  call 80017 1bc
    //   8001e3de e8 4c 00 00 00  call 8001e42f
    //   8001e3e3 83 7b 2c 00     cmp  dword [ebx+0x2c],0   ; a ready thread would exit
    //   8001e3e7 74 e5           je   8001e3ce
    // One idle iteration is therefore exactly 8 retired instructions in 4 translation blocks
    // (sti ends its block; the block after it is the 1-instruction inhibit block).
    static constexpr uint32_t kIdleLoopHead = 0x8001e3ceu;
    static constexpr unsigned kIdleLoopInsns = 8;
    static constexpr unsigned kIdleLoopBytes = 27;  // 0x8001e3ce .. 0x8001e3e8 inclusive
    // Exact deadlines can stop at any of the eight instruction boundaries.
    // Whole iterations preserve the EIP, flags, and the phase-1 STI shadow.
    // ZF/PF are 1 and CF/AF/SF/OF are 0 at every one of them: both `cmp`s in the loop compare
    // equal values while it is idle, so the flag word is the same at all eight phases apart
    // from IF, which is what kIdlePhaseIf records.
    static constexpr unsigned kIdlePhases = 8;
    static constexpr uint32_t kIdlePhaseEip[kIdlePhases] = {0x8001e3ceu, 0x8001e3cfu,
        0x8001e3d0u, 0x8001e3d1u, 0x8001e3d2u, 0x8001e3d5u, 0x8001e3e3u, 0x8001e3e7u};
    static constexpr bool kIdlePhaseIf[kIdlePhases] = {false, true, true, true, false, false, false, false};
    // EFLAGS is a FIXED POINT of the iteration: the last flag-setting instruction is
    // `cmp dword [ebx+0x2c],0` with a zero operand, and `cli` leaves IF clear. So an
    // iteration entered with these bits leaves exactly these bits, and skipping any whole
    // number of iterations needs no EFLAGS fixup at all. TF must be clear or the guest is
    // single-stepping and every instruction is observable.
    //          OF     TF     IF     SF    ZF    AF    PF   CF
    static constexpr uint32_t kIdleFlagsMask = 0x800u | 0x100u | 0x200u | 0x80u | 0x40u |
                                               0x10u | 0x04u | 0x01u;
    static constexpr uint32_t kIdleFlagsValue = 0x40u | 0x04u;  // ZF | PF
    static constexpr uint32_t kIdleFlagIf = 0x200u;
    void set_idle_fast_forward(bool on) { idle_fast_forward_ = on; }
    bool idle_fast_forward() const { return idle_fast_forward_; }

    // ---- determinism trace (beads-xbox.1.14.11) ---------------------------------------
    // ALWAYS-ON capture, opt-in REPORTING (PERF-METHODOLOGY rule 10: never arm-then-capture).
    // One 5-word FNV-1a64 fold and one 24-byte ring store per CPU slice. At ~3 M slices per
    // 12 B run that is ~15 M folds against 12 G retired instructions, i.e. below the noise of
    // the slice body it sits in.
    //
    // The digest is the guest trajectory at slice granularity: (entry insn count, entry EIP,
    // retired count, exit EIP, irq/inject flags) for every slice, chained. Two runs whose
    // digests agree at a checkpoint executed the same instruction stream, sliced the same way,
    // with the same interrupts delivered at the same boundaries, up to that point. The first
    // checkpoint at which two runs disagree brackets the divergence to one interval; the ring
    // then hands back the individual slices inside that bracket without re-running anything.
    struct DetSlice {
        uint64_t insns = 0;     // total_insns_ at slice entry
        uint32_t eip_in = 0;    // EIP at slice entry
        uint32_t retired = 0;   // instructions the slice retired (executed or fast-forwarded)
        uint32_t eip_out = 0;   // EIP at slice exit, after any interrupt injection
        uint32_t flags = 0;     // bit0 irq_armed, bit1 idle-fast-forwarded,
                                // bit2 interrupt injected, bits 8..15 injected vector
        uint32_t dev_events = 0;  // port-I/O + MMIO accesses the slice made
        uint32_t dev_hash32 = 0;  // low 32 bits of dev_digest_ at slice exit
    };
    static constexpr uint32_t kDetIrqArmed = 1u;
    static constexpr uint32_t kDetFastForward = 2u;
    static constexpr uint32_t kDetInjected = 4u;
    // 1 Mi slices x 24 B = 24 MiB. At the canonical 4096-instruction chunk one slice retires
    // 2.5 k-4 k instructions, so the ring covers the last 2.5-4 G retired instructions --
    // enough that a dump taken at the first divergent checkpoint always reaches back past it.
    static constexpr size_t kDetRingSlices = 1u << 20;
    uint64_t det_digest() const { return det_digest_; }
    uint64_t det_slices() const { return det_slices_; }
    // Second always-on digest: every VALUE a device hands the guest through the port-I/O and
    // MMIO funnels, plus every value the guest hands a device, chained in order. It separates
    // "a device returned something different" from "the CPU executed something different":
    // if two runs' dev digests are equal at the last agreeing slice checkpoint, no device
    // register read caused the divergence.
    uint64_t dev_digest() const { return dev_digest_; }
    uint64_t dev_events() const { return io_seq_ + mmio_seq_; }
    // Always-on ring of the last kDetDevRing device accesses, so the slice the dev digest
    // first differs in can be opened up to the individual register access without re-running
    // with a trace armed.
    struct DetDev {
        uint64_t insns = 0;   // total_insns_ at the slice the access belongs to
        uint32_t addr = 0;    // MMIO address or I/O port
        uint32_t value = 0;   // value read or written
        uint16_t size = 0;    // access width in bytes
        uint16_t flags = 0;   // bit0 write, bit1 MMIO (else port I/O)
    };
    static constexpr size_t kDetDevRing = 1u << 20;
    bool dump_det_dev_ring(const std::string& path) const;
    // Third always-on ring: every DMA write a device makes into guest RAM. These bypass the
    // MMIO/port funnels entirely (they go straight through CpuFloor::write_mem), so without
    // this ring a device that writes host-dependent BYTES into guest memory is invisible to
    // the other two digests until the guest branches on them, billions of instructions later.
    struct DetDma {
        uint64_t insns = 0;
        uint64_t hash = 0;     // FNV-1a over the written bytes, 8 bytes at a time
        uint32_t addr = 0;
        uint32_t size = 0;
        uint32_t source = 0;   // DmaSource
    };
    enum DmaSource : uint32_t {
        kDmaIde = 0, kDmaNv2a, kDmaApu, kDmaNvNet, kDmaOhci0, kDmaOhci1, kDmaSourceCount
    };
    static const char* dma_source_name(uint32_t source);
    static constexpr size_t kDetDmaRing = 1u << 18;
    bool dump_det_dma_ring(const std::string& path) const;
    void set_det_dma_dump(const std::string& path, uint64_t at_insns) {
        det_dma_dump_path_ = path;
        det_dma_dump_at_ = at_insns;
    }
    uint64_t dma_digest() const { return dma_digest_; }
    uint64_t dma_writes() const { return dma_writes_; }
    void set_det_dev_dump(const std::string& path, uint64_t at_insns) {
        det_dev_dump_path_ = path;
        det_dev_dump_at_ = at_insns;
    }
    // Whole-RAM image dump at a requested instruction count. Two runs' images diff to the
    // exact guest addresses that differ, which names the writer.
    void add_det_ram_dump(const std::string& path, uint64_t at_insns) {
        det_ram_dumps_.push_back({path, at_insns, false});
    }
    // Checkpoint stream: one line per `interval` retired instructions into `path`. Reporting
    // only -- the fold and the ring run whether or not this is set.
    bool open_det_log(const std::string& path, uint64_t interval);
    // Dump the always-on ring (oldest first) to `path`. Called on demand, at a requested
    // instruction count, and on any fault stop.
    bool dump_det_ring(const std::string& path) const;
    void set_det_ring_dump(const std::string& path, uint64_t at_insns) {
        det_ring_dump_path_ = path;
        det_ring_dump_at_ = at_insns;
    }

private:
    void install_hle_allocation_observers();
    bool hle_allocation_observers_installed_ = false;
#if XLLE_XDK_DRAW_SPIKE
    void install_hle_xdk_draw();
    bool hle_xdk_draw_installed_ = false;
#endif
    void det_record_slice(const DetSlice& s);
    // Returns the number of instructions that may be retired without executing them, or 0.
    bool try_idle_fast_forward(uint32_t eip, bool irq_armed, uint64_t deadline,
                               uint64_t remaining, uint64_t& skipped);
    void record_idle_census(uint32_t eip, uint64_t retired, uint64_t chunk, bool irq_armed);
    uint32_t io_in(uint16_t port, int size);
    void io_out(uint16_t port, int size, uint32_t value);
    IoPortDevice* find_io(uint16_t port);

    uint32_t mmio_read(uint32_t addr, int size);
    void mmio_write(uint32_t addr, int size, uint32_t value);
    MmioDevice* find_mmio(uint32_t addr);

    std::unique_ptr<CpuFloor> cpu_;
    const Config config_;
    std::vector<uint8_t> ram_;  // 64 MB backing, mapped at 0x0 and aliased at 0x04000000
    size_t flash_size_ = 0;
    bool ram_ok_ = false, flash_ok_ = false;

    std::vector<std::unique_ptr<IoPortDevice>> io_devices_;  // owns PCI host, SMBus, PIC, PIT...
    std::vector<std::unique_ptr<MmioDevice>> mmio_devices_;  // owns NV2A, ...
    PciHost* pci_ = nullptr;                                 // non-owning convenience pointers
    DualPic* pic_ = nullptr;
    Pit8254* pit_ = nullptr;
    IdeController* ide_ = nullptr;
    Amd756Smbus* smbus_ = nullptr;
    Nv2a* nv2a_ = nullptr;
    NvNet* nvnet_ = nullptr;
    McpxApu* apu_ = nullptr;
    McpxAci* aci_ = nullptr;
    Ohci* ohci0_ = nullptr;
    Ohci* ohci1_ = nullptr;
    std::function<void(const IoEvent&)> io_event_cb_;
    std::function<void(const MemEvent&)> mmio_event_cb_;
    std::function<void()> host_poll_cb_;
    uint64_t io_seq_ = 0;
    uint64_t mmio_seq_ = 0;
    uint64_t total_insns_ = 0;  // retired-instruction clock feeding the PIT time base
    std::atomic<uint64_t> progress_insns_atomic_{0};
    uint64_t injected_ = 0;     // external interrupts actually delivered to the guest
    uint64_t irq0_pulses_ = 0;  // PIT channel-0 period boundaries reached
    uint64_t pending_boundaries_ = 0;  // chunk boundaries with an unmasked IRQ pending
    uint64_t host_poll_millisecond_ = UINT64_MAX;
    uint64_t cpu_chunk_insns_ = 4096;
    IdleCensus idle_census_{};
    uint32_t idle_eip_lo_ = 0x8001e3ceu;  // kernel 4983 NT idle loop, inclusive
    uint32_t idle_eip_hi_ = 0x8001e3ffu;
    bool idle_fast_forward_ = false;      // --idle-fast-forward; see try_idle_fast_forward
    bool idle_ff_bytes_bad_ = false;      // the pinned loop shape failed to verify: stay off

    // ---- determinism trace state (always-on capture) ----------------------------------
    uint64_t det_digest_ = 0xcbf29ce484222325ull;  // FNV-1a 64 offset basis
    uint64_t det_slices_ = 0;
    std::vector<DetSlice> det_ring_;   // lazily sized to kDetRingSlices on first slice
    size_t det_ring_head_ = 0;         // next write index
    bool det_ring_wrapped_ = false;
    std::FILE* det_log_ = nullptr;     // opt-in checkpoint stream
    uint64_t det_log_interval_ = 0;
    uint64_t det_log_next_ = 0;
    std::string det_ring_dump_path_;
    uint64_t det_ring_dump_at_ = 0;
    bool det_ring_dumped_ = false;
    uint64_t dev_digest_ = 0xcbf29ce484222325ull;
    uint64_t det_dev_events_at_slice_ = 0;
    std::vector<DetDev> det_dev_ring_;
    size_t det_dev_head_ = 0;
    bool det_dev_wrapped_ = false;
    std::string det_dev_dump_path_;
    uint64_t det_dev_dump_at_ = 0;
    bool det_dev_dumped_ = false;
    std::vector<DetDma> det_dma_ring_;
    size_t det_dma_head_ = 0;
    bool det_dma_wrapped_ = false;
    std::string det_dma_dump_path_;
    uint64_t det_dma_dump_at_ = 0;
    bool det_dma_dumped_ = false;
    uint64_t dma_digest_ = 0xcbf29ce484222325ull;
    uint64_t dma_writes_ = 0;
    // Single funnel for every device DMA write into guest RAM, so the ring above cannot be
    // bypassed by adding a device.
    bool dev_dma_write(uint32_t source, uint32_t addr, const void* buf, size_t n);
    struct DetRamDump {
        std::string path;
        uint64_t at_insns = 0;
        bool done = false;
    };
    std::vector<DetRamDump> det_ram_dumps_;
    void det_fold_dev(uint64_t a, uint64_t b);

    // KPCR.PrcbData.CurrentThread for the 4983 kernel, as a PHYSICAL address (KPCR 0x8003da3c
    // + 0x28, identity-mapped low RAM). Read once per slice for thread attribution.
    static constexpr uint32_t kKpcrCurrentThreadPhys = 0x0003da64u;

    // Guest CPU clock estimate (Hz) mapping retired instructions -> PIT ticks. The Xbox CPU is
    // 733.33 MHz; combined with ~1 IPC this sets the tick cadence (calibratable).
    static constexpr uint64_t kCpuHz = 733333333ull;
};

}  // namespace xlle
