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
    uint64_t method_count(uint32_t method) const {
        return (method < 0x2000 && !(method & 3u)) ? method_counts_[method >> 2] : 0;
    }
    uint64_t class_method_count(uint8_t object_class) const {
        return class_method_counts_[object_class];
    }
    uint64_t semaphore_writes() const { return semaphore_writes_; }
    uint64_t flip_increments() const { return flip_increments_; }
    uint64_t flip_stalls() const { return flip_stalls_; }
    uint64_t pgraph_read_increments() const { return pgraph_read_increments_; }
    uint64_t pgraph_notifications() const { return pgraph_notifications_; }
    uint64_t color_clears() const { return color_clears_; }
    uint64_t color_clear_pixels() const { return color_clear_pixels_; }
    uint32_t last_color_clear_address() const { return last_color_clear_address_; }
    uint64_t zeta_clears() const { return zeta_clears_; }
    uint64_t zeta_clear_pixels() const { return zeta_clear_pixels_; }
    uint32_t last_zeta_clear_address() const { return last_zeta_clear_address_; }
    uint64_t vertex_batches() const { return vertex_batches_; }
    uint64_t submitted_vertices() const { return submitted_vertices_; }
    uint64_t vertex_attribute_fetches() const { return vertex_attribute_fetches_; }
    uint64_t assembled_primitives() const { return assembled_primitives_; }
    uint64_t assembled_triangles() const { return assembled_triangles_; }
    uint32_t largest_vertex_index() const { return largest_vertex_index_; }
    uint64_t primitive_mode_count(uint32_t mode) const {
        return mode < primitive_mode_counts_.size() ? primitive_mode_counts_[mode] : 0;
    }
    uint64_t vertex_format_fetches(uint32_t format) const {
        return format < vertex_format_fetches_.size() ? vertex_format_fetches_[format] : 0;
    }
    uint64_t vertex_slot_fetches(uint32_t slot) const {
        return slot < vertex_slot_fetches_.size() ? vertex_slot_fetches_[slot] : 0;
    }
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

    struct ScanoutInfo {
        uint32_t start = 0;
        uint32_t pitch = 0;
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t bits_per_pixel = 0;
    };
    ScanoutInfo scanout_info() const;
    bool capture_scanout(std::vector<uint8_t>& rgba, ScanoutInfo& info,
                         std::string& error) const;
    uint64_t scanout_writes() const { return scanout_writes_; }
    uint64_t scanout_nonzero_writes() const { return scanout_nonzero_writes_; }
    uint32_t last_nonzero_scanout_start() const { return last_nonzero_scanout_start_; }
    size_t recent_scanout_starts(size_t n, std::vector<uint32_t>& out) const {
        return scanout_starts_.window(n, out);
    }

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
    std::array<uint64_t, 0x800> method_counts_{};
    std::array<uint64_t, 256> class_method_counts_{};
    std::deque<PushMethod> pull_queue_;
    std::function<bool(uint32_t, void*, size_t)> mem_read_;
    std::function<bool(uint32_t, const void*, size_t)> mem_write_;
    std::array<uint32_t, 8> semaphore_dma_{};
    std::array<uint32_t, 8> semaphore_offset_{};
    std::array<uint32_t, 8> object_instance_{};
    std::array<uint8_t, 8> object_class_{};
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

    // Minimal Kelvin color-surface state. Object-taking methods hold resolved PRAMIN
    // instances, matching the PFIFO puller's RAMHT translation before PGRAPH dispatch.
    uint32_t color_dma_ = 0;
    uint32_t zeta_dma_ = 0;
    bool color_dma_bound_ = false;
    bool zeta_dma_bound_ = false;
    uint32_t surface_clip_horizontal_ = 0;
    uint32_t surface_clip_vertical_ = 0;
    uint32_t surface_format_ = 0;
    uint32_t surface_pitch_ = 0;
    uint32_t surface_color_offset_ = 0;
    uint32_t surface_zeta_offset_ = 0;
    uint32_t zeta_clear_value_ = 0;
    uint32_t color_clear_value_ = 0;
    uint32_t clear_rect_horizontal_ = 0;
    uint32_t clear_rect_vertical_ = 0;
    uint64_t color_clears_ = 0;
    uint64_t color_clear_pixels_ = 0;
    uint32_t last_color_clear_address_ = 0;
    uint64_t zeta_clears_ = 0;
    uint64_t zeta_clear_pixels_ = 0;
    uint32_t last_zeta_clear_address_ = 0;

    // Kelvin's array-fed vertex path. The dashboard overwhelmingly submits ARRAY_ELEMENT16:
    // each method contains two indices. Attribute offsets select vertex DMA A/B with bit 31;
    // formats carry type/count/stride. At END we fetch every enabled attribute through its
    // PRAMIN DMA object and assemble the selected primitive topology. This is deliberately
    // kept separate from raster side effects until the exercised transform state is decoded.
    struct VertexAttribute {
        uint32_t offset = 0;
        uint32_t stride = 0;
        uint8_t format = 0;
        uint8_t count = 0;
        uint8_t element_size = 0;
        bool dma_b = false;
        std::array<float, 4> value{{0.0f, 0.0f, 0.0f, 1.0f}};
    };
    uint32_t vertex_dma_a_ = 0;
    uint32_t vertex_dma_b_ = 0;
    bool vertex_dma_a_bound_ = false;
    bool vertex_dma_b_bound_ = false;
    std::array<VertexAttribute, 16> vertex_attributes_{};
    uint8_t primitive_mode_ = 0;
    std::vector<uint32_t> vertex_indices_;
    uint64_t vertex_batches_ = 0;
    uint64_t submitted_vertices_ = 0;
    uint64_t vertex_attribute_fetches_ = 0;
    uint64_t assembled_primitives_ = 0;
    uint64_t assembled_triangles_ = 0;
    uint32_t largest_vertex_index_ = 0;
    std::array<uint64_t, 11> primitive_mode_counts_{};
    std::array<uint64_t, 7> vertex_format_fetches_{};
    std::array<uint64_t, 16> vertex_slot_fetches_{};

    // Display/scanout state. PRMCIO is the memory-mapped VGA register alias; AvSetDisplayMode
    // programs CRTC pitch/depth there, PRAMDAC display extents, then PCRTC_START.
    std::array<uint8_t, 256> crtc_regs_{};
    uint8_t crtc_index_ = 0;
    uint32_t pcrtc_start_ = 0;
    uint32_t pramdac_general_control_ = 0;
    uint32_t pramdac_fp_vdisplay_end_ = 0;
    uint32_t pramdac_fp_hdisplay_end_ = 0;
    uint64_t scanout_writes_ = 0;
    uint64_t scanout_nonzero_writes_ = 0;
    uint32_t last_nonzero_scanout_start_ = 0;
    Ring<uint32_t> scanout_starts_{64};

    void process_pushbuffer(uint8_t channel);
    bool pusher_enabled(uint8_t channel) const;
    void drain_pull_queue();
    void dispatch_method(uint8_t channel, uint8_t subchannel, uint32_t method,
                         uint32_t parameter);
    bool bind_object(uint8_t channel, uint8_t subchannel, uint32_t handle);
    bool resolve_object(uint8_t channel, uint32_t handle, uint32_t& instance);
    bool clear_surface(uint32_t parameter);
    bool clear_surface_plane(uint32_t dma_instance, bool dma_bound,
                             uint32_t surface_offset, uint32_t pitch,
                             uint32_t bytes_per_pixel, uint32_t write_mask,
                             uint32_t clear_value, const char* plane,
                             uint32_t& first_address);
    bool set_vertex_array_format(uint32_t slot, uint32_t parameter);
    bool submit_vertex_batch();
    bool fetch_vertex_attribute(uint32_t slot, uint32_t index);
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
