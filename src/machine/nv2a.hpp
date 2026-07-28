// xlle — NV2A (Xbox GPU) MMIO register shell (A2c minimal shell; full engine is A4).
//
// The NV2A MMIO aperture is 0xFD000000..0xFDFFFFFF (16 MB), divided into engine blocks (PMC,
// PBUS, PFIFO, PTIMER, PFB, PSTRAPS, PGRAPH, PCRTC, PRAMDAC, ...). Block map + register
// semantics harvested from xemu/hw/xbox/nv2a/*.
//
// Value policy (a deliberate, documented deviation from blanket fault-stop, justified by the
// oracle being offline and xemu being a valid boot witness):
//   * Registers the boot reads are modelled with faithful values — from real-hardware capture
//     (machine/probes) where available, else xemu.
//   * Reads of an unmodelled register *inside a real block* return xemu's documented default
//     (0) and are LOGGED as unmodelled, not silently absorbed. xemu boots the same retail 2BL
//     with these defaults, so they are boot-correct witnesses; each is a debt to replace with
//     hardware truth via the oracle.
//   * A read/write to an aperture HOLE (no block) fault-stops loudly (PRINCIPLES §3).
//   * Writes to unmodelled registers are accepted+logged (a write carries no return value to
//     get wrong; xemu likewise ignores unknown writes).
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "machine/io_device.hpp"   // FaultFn
#include "machine/mmio_device.hpp"
#include "xlle/ring.hpp"

namespace xlle {

class Nv2a : public MmioDevice {
public:
    static constexpr uint32_t kBase = 0xFD000000u;
    static constexpr uint32_t kSize = 0x01000000u;  // 16 MB

    explicit Nv2a(FaultFn fault)
        : fault_(std::move(fault)), pramin_(0x100000, 0), methods_(4096) {}

    const char* name() const override { return "nv2a"; }
    uint32_t mmio_base() const override { return kBase; }
    uint32_t mmio_size() const override { return kSize; }
    uint32_t mmio_read(uint32_t addr, int size) override;
    void mmio_write(uint32_t addr, int size, uint32_t value) override;

    // One engine block within the aperture (offset/size relative to kBase).
    struct Block { uint32_t off, size; const char* name; };

    // Observability: called for every access that hit an unmodelled register (returned the
    // xemu default). `write` mirrors MemEvent semantics. Lets a run summarize its NV2A debt.
    void on_unmodeled(std::function<void(bool write, uint32_t addr, uint32_t rel,
                                         const char* block, uint32_t value)> cb) {
        unmodeled_cb_ = std::move(cb);
    }

    // GPU interrupt line (Xbox: ISA IRQ 3), level-triggered. Same wiring shape as the IDE and
    // SMBus completion interrupts.
    void set_irq(std::function<void(int irq, bool level)> cb, int irq_num) {
        irq_cb_ = std::move(cb);
        irq_num_ = irq_num;
    }
    void set_mem_access(std::function<bool(uint32_t, void*, size_t)> read,
                        std::function<bool(uint32_t, const void*, size_t)> write) {
        mem_read_ = std::move(read);
        mem_write_ = std::move(write);
    }

    struct PushMethod {
        uint64_t seq = 0;
        uint8_t channel = 0;
        uint8_t subchannel = 0;
        uint16_t method = 0;
        uint32_t parameter = 0;
    };
    uint64_t push_methods() const { return methods_.total(); }
    uint64_t semaphore_writes() const { return semaphore_writes_; }
    uint64_t flip_increments() const { return flip_increments_; }
    uint64_t flip_stalls() const { return flip_stalls_; }
    uint64_t pgraph_read_increments() const { return pgraph_read_increments_; }
    uint64_t pgraph_notifications() const { return pgraph_notifications_; }
    uint32_t pgraph_surface() const;
    bool waiting_for_flip() const { return waiting_for_flip_; }
    size_t recent_methods(size_t n, std::vector<PushMethod>& out) const {
        return methods_.window(n, out);
    }
    // Frame clock: the machine advances this from retired instructions (~60 Hz). Each new
    // frame latches VBLANK into PCRTC_INTR_0; PCRTC_INTR_EN_0 and PMC_INTR_EN_0 gate the
    // line (xemu nv2a update_irq / pcrtc.c). The kernel's AV code paces on this interrupt.
    void set_frame(uint64_t frame_no);
    // Sub-frame phase in permille (0..999), same instruction-derived time base: drives the
    // VGA input-status-1 retrace bit (PRMCIO 0x3DA) the kernel polls for scan position.
    void set_frame_phase(uint32_t permille) { frame_phase_ = permille; }
    uint64_t vblanks() const { return vblanks_; }
    uint32_t pmc_intr_en() const { return pmc_intr_en_; }
    uint32_t pcrtc_intr_en() const { return pcrtc_intr_en_; }

private:
    // Resolve an absolute NV2A address to its block; returns nullptr (a hole) if unmapped.
    static const Block* find_block(uint32_t rel_addr, uint32_t& block_rel);

    FaultFn fault_;
    std::function<void(bool, uint32_t, uint32_t, const char*, uint32_t)> unmodeled_cb_;
    // PFIFO USER channel registers (rel 0x800000+, 64 KB per channel; +0x40 DMA_PUT,
    // +0x44 DMA_GET). The channel state below is the synchronous pusher/recorder state.
    std::unordered_map<uint32_t, uint32_t> user_regs_;
    std::unordered_map<uint32_t, uint32_t> pfifo_regs_;
    int pb_logged_ = 0;

    struct ChannelState {
        uint32_t get = 0, put = 0;
        uint32_t dma_instance = 0;
        uint32_t method = 0, method_count = 0;
        uint32_t subroutine = 0;
        uint8_t subchannel = 0;
        bool non_incrementing = false;
        bool subroutine_active = false;
        bool get_initialized = false;
    };
    std::array<ChannelState, 32> channels_{};
    std::vector<uint8_t> pramin_;
    Ring<PushMethod> methods_;
    std::deque<PushMethod> pull_queue_;
    std::function<bool(uint32_t, void*, size_t)> mem_read_;
    std::function<bool(uint32_t, const void*, size_t)> mem_write_;
    std::array<uint32_t, 8> semaphore_dma_{};
    std::array<uint32_t, 8> semaphore_offset_{};
    uint64_t semaphore_writes_ = 0;
    uint8_t flip_read_ = 0;
    uint8_t flip_write_ = 0;
    uint8_t flip_modulo_ = 0;
    bool waiting_for_flip_ = false;
    bool waiting_for_nop_ = false;
    bool pgraph_fifo_access_ = true;
    uint64_t flip_increments_ = 0;
    uint64_t flip_stalls_ = 0;
    uint64_t pgraph_read_increments_ = 0;
    uint64_t pgraph_notifications_ = 0;
    uint32_t pgraph_intr_ = 0;
    uint32_t pgraph_intr_en_ = 0;
    uint32_t pgraph_nsource_ = 0;
    uint32_t pgraph_trapped_addr_ = 0;
    uint32_t pgraph_trapped_data_ = 0;

    void process_pushbuffer(uint8_t channel);
    bool pusher_enabled(uint8_t channel) const;
    void drain_pull_queue();
    void dispatch_method(uint8_t channel, uint8_t subchannel, uint32_t method,
                         uint32_t parameter);
    bool ramht_instance(uint8_t channel, uint32_t handle, uint32_t& instance) const;
    bool dma_object_address(uint32_t instance, uint32_t offset, uint32_t& address) const;

    // Interrupt state (PMC aggregation of PCRTC, xemu nv2a.c update_irq).
    void update_irq();
    uint32_t pmc_intr_status() const;  // computed NV_PMC_INTR_0
    std::function<void(int, bool)> irq_cb_;
    int irq_num_ = 3;
    bool irq_asserted_ = false;
    uint32_t pmc_intr_en_ = 0;    // NV_PMC_INTR_EN_0 (0x000140)
    uint32_t pcrtc_intr_ = 0;     // NV_PCRTC_INTR_0 (0x600100), bit0 = VBLANK, W1C
    uint32_t pcrtc_intr_en_ = 0;  // NV_PCRTC_INTR_EN_0 (0x600140)
    uint64_t frame_no_ = 0;
    uint64_t vblanks_ = 0;
    uint32_t frame_phase_ = 0;
};

}  // namespace xlle
