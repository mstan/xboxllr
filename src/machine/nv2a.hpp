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
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include "machine/io_device.hpp"   // FaultFn
#include "machine/mmio_device.hpp"
#include "machine/nv2a_renderer.hpp"
#include "machine/nv2a_generic_vertex_scratch.hpp"
#include "machine/nv2a_texture_conversion_cache.hpp"
#include "machine/nv2a_vsh_compiled.hpp"
#include "machine/nv2a_vsh_dot_prefix.hpp"
#if XLLE_VSH_CAPTURE
#include "machine/nv2a_vsh_capture.hpp"
#include "machine/nv2a_vsh_batch_capture.hpp"
#endif
#ifndef XLLE_VSH_DRAW_OVERLAP_CENSUS
#define XLLE_VSH_DRAW_OVERLAP_CENSUS 0
#endif
#if XLLE_VSH_DRAW_OVERLAP_CENSUS
#include "machine/nv2a_vsh_draw_overlap.hpp"
#endif
#if XLLE_VERTEX_REUSE_DIAGNOSTICS
#include "machine/nv2a_vertex_reuse_shadow.hpp"
#endif
#include "xlle/ring.hpp"

namespace xlle {

// Swizzled-texture / swizzled-surface address table. Immutable after
// construction, so a const instance is safe to read from any thread.
//
// It lives in the header (rather than in nv2a.cpp, where it used to sit in an
// anonymous namespace) because a DrawJob OWNS its swizzle tables: stage (e)
// hands the job to a worker thread, and everything the fragment loop reads has
// to be reachable through the job. DESIGN-NV2A-SEAM.md section 1.2.
class SwizzleLookup {
public:
    SwizzleLookup(uint32_t width, uint32_t height)
        : x_(width), y_(height) {
        uint32_t output_bit = 1;
        for (uint32_t input_bit = 1;
             input_bit < width || input_bit < height;
             input_bit <<= 1) {
            if (input_bit < width) {
                for (uint32_t x = 0; x < width; ++x)
                    if (x & input_bit) x_[x] |= output_bit;
                output_bit <<= 1;
            }
            if (input_bit < height) {
                for (uint32_t y = 0; y < height; ++y)
                    if (y & input_bit) y_[y] |= output_bit;
                output_bit <<= 1;
            }
        }
    }

    uint32_t index(uint32_t x, uint32_t y) const {
        return x_[x] | y_[y];
    }

private:
    std::vector<uint32_t> x_;
    std::vector<uint32_t> y_;
};

class Nv2a : public MmioDevice {
public:
    static constexpr uint32_t kBase = 0xFD000000u;
    static constexpr uint32_t kSize = 0x01000000u;  // 16 MB

    explicit Nv2a(FaultFn fault, Nv2aRenderer* renderer = nullptr)
        : fault_(std::move(fault)), renderer_(renderer), pramin_(0x100000, 0), methods_(4096) {}
    // Stage (e): the draw worker's lifetime is the device's. The destructor
    // drains whatever is still queued, stops the worker and joins it, so no
    // worker can outlive the RAM it writes into.
    ~Nv2a() override;

    void set_renderer(Nv2aRenderer* renderer) { renderer_ = renderer; }
#if XLLE_VSH_GPU_RESIDENT
    void set_vsh_gpu_resident_mode(uint32_t mode) { vsh_gpu_resident_mode_ = mode; }
    Nv2aRenderer::VshGpuResidentStats vsh_gpu_resident_stats() const {
        return renderer_ ? renderer_->vsh_gpu_resident_stats() : Nv2aRenderer::VshGpuResidentStats{};
    }
    struct VshGpuDeferredStats {
        uint64_t deferred_batches = 0;
        uint64_t deferred_executions = 0;
        uint64_t deferred_references = 0;
        uint64_t prepared_batches = 0;
        uint64_t prepared_vertices = 0;
        uint64_t declined_batches = 0;
        uint64_t fallback_batches = 0;
        uint64_t fallback_executions = 0;
    };
    const VshGpuDeferredStats& vsh_gpu_deferred_stats() const {
        return vsh_gpu_deferred_stats_;
    }
#endif
    void set_detailed_telemetry(bool enabled) { detailed_telemetry_ = enabled; }
    // Per-write raster telemetry (the 22 FNV-1a output hashes, the
    // last-written-address trackers, and the per-write census counters) is a
    // serial loop-carried dependency on the hottest loop in the emulator --
    // DESIGN-NV2A-SEAM.md section 4.3 records it as the reason the witnessed
    // fragment loop cannot vectorize, and option O3 is to gate it. The flag is
    // latched once per batch by rasterize_witnessed_batch, never read per
    // fragment. It defaults to TRUE so today's goldens are unchanged; flipping
    // the default to off is a separate re-baseline decision (PERF-METHODOLOGY.md
    // section 3) and wants a `--nv2a-profile-hashes` CLI switch calling this.
    void set_profile_hashes_enabled(bool enabled) {
        profile_hashes_enabled_ = enabled;
    }
    bool profile_hashes_enabled() const { return profile_hashes_enabled_; }

    const char* name() const override { return "nv2a"; }
    uint32_t mmio_base() const override { return kBase; }
    uint32_t mmio_size() const override { return kSize; }
    uint32_t mmio_read(uint32_t addr, int size) override;
    void mmio_write(uint32_t addr, int size, uint32_t value) override;
    // Bounded PCI config access shares the actual PBUS register state.
    uint32_t pci_read(uint8_t reg, int size);
    void pci_write(uint8_t reg, int size, uint32_t value);

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
                        std::function<bool(uint32_t, const void*, size_t)> write,
                        std::function<uint8_t*(uint32_t, size_t)> direct = {}) {
        mem_read_ = std::move(read);
        mem_write_ = std::move(write);
        mem_direct_ = std::move(direct);
    }

    struct GenericBatchDebugInfo {
        uint64_t batch = 0;
        uint64_t fragments = 0;
        uint64_t writes = 0;
        uint64_t fingerprint = 0;
        uint64_t full_state_hash = 0;
        uint32_t color_address = 0;
        uint32_t pitch = 0;
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t surface_format = 0;
        uint32_t shader_stage_program = 0;
        uint32_t combiner_control = 0;
        uint32_t alpha_func = 0;
        uint32_t alpha_ref = 0;
        uint32_t blend_src = 0;
        uint32_t blend_dst = 0;
        uint32_t blend_equation = 0;
        uint32_t depth_func = 0;
        uint32_t depth_mask = 0;
        uint32_t texture_enable_mask = 0;
        uint32_t primitive_mode = 0;
        uint32_t vertex_count = 0;
        bool texture_modulate_observed = false;
        bool texture_modulate_exact = false;
        uint32_t texture_modulate_reject = 0;
    };
    void on_generic_batch_complete(std::function<void(const GenericBatchDebugInfo&)> cb) {
        generic_batch_debug_cb_ = std::move(cb);
    }
    struct GenericPacketDebugInfo {
        uint64_t candidate = 0;
        uint64_t fingerprint = 0;
        uint64_t full_state_hash = 0;
        uint32_t color_address = 0;
        uint32_t depth_address = 0;
        uint32_t pitch = 0;
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t surface_format = 0;
        uint32_t shader_stage_program = 0;
        uint32_t combiner_control = 0;
        uint32_t texture_enable_mask = 0;
        uint32_t primitive_mode = 0;
        uint32_t vertex_count = 0;
    };
    struct GenericPacketSnapshot {
        Nv2aGenericCombinerPacket packet;
        std::vector<uint8_t> color_output;
        std::vector<uint8_t> depth_output;
    };
    void on_generic_packet_snapshot(
        std::function<bool(const GenericPacketDebugInfo&)> filter,
        std::function<bool(const GenericPacketDebugInfo&,
                           const GenericPacketSnapshot&)> cb) {
        generic_packet_snapshot_filter_cb_ = std::move(filter);
        generic_packet_snapshot_cb_ = std::move(cb);
    }
    struct SurfaceWriteDebugInfo {
        const char* kind = "";
        uint64_t sequence = 0;
        uint64_t fragments = 0;
        uint64_t writes = 0;
        uint64_t fingerprint = 0;
        uint64_t full_state_hash = 0;
        uint32_t color_address = 0;
        uint32_t pitch = 0;
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t surface_format = 0;
        uint32_t shader_stage_program = 0;
        uint32_t combiner_control = 0;
        uint32_t alpha_func = 0;
        uint32_t alpha_ref = 0;
        uint32_t blend_src = 0;
        uint32_t blend_dst = 0;
        uint32_t blend_equation = 0;
        uint32_t depth_func = 0;
        uint32_t depth_mask = 0;
        uint32_t texture_enable_mask = 0;
        uint32_t primitive_mode = 0;
        uint32_t vertex_count = 0;
        uint32_t profile = 0;
        uint32_t debug_vertex_count = 0;
        std::array<std::array<float, 4>, 4> debug_position{};
        std::array<std::array<float, 4>, 4> debug_diffuse{};
        std::array<std::array<float, 4>, 4> debug_tex0{};
    };
    void on_surface_write_complete(std::function<void(const SurfaceWriteDebugInfo&)> cb) {
        surface_write_debug_cb_ = std::move(cb);
    }

    struct PushMethod {
        uint64_t seq = 0;
        uint8_t channel = 0;
        uint8_t subchannel = 0;
        uint8_t object_class = 0;
        uint16_t method = 0;
        uint32_t parameter = 0;
        // Snapshot when PFIFO decoded this parameter, before deferred PGRAPH dispatch.
        uint32_t parser_get = 0;
        uint32_t parser_put = 0;
    };
    uint64_t push_methods() const { return methods_.total(); }
    uint64_t method_count(uint32_t method) const {
        return (method < 0x2000 && !(method & 3u)) ? method_counts_[method >> 2] : 0;
    }
    uint64_t class_method_count(uint8_t object_class) const {
        return class_method_counts_[object_class];
    }
    struct ClassMethodStat {
        uint64_t count = 0;
        uint32_t first_parameter = 0;
        uint32_t last_parameter = 0;
    };
    ClassMethodStat class_method_stat(uint8_t object_class, uint32_t method) const;
    uint64_t image_blit_copies() const { return image_blit_copies_; }
    uint64_t image_blit_bytes() const { return image_blit_bytes_; }
    uint32_t last_image_blit_source() const { return last_image_blit_source_; }
    uint32_t last_image_blit_dest() const { return last_image_blit_dest_; }
    uint32_t last_image_blit_size() const { return last_image_blit_size_; }
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
    uint64_t depth_raster_batches() const { return depth_raster_batches_; }
    uint64_t rasterized_triangles() const { return rasterized_triangles_; }
    uint64_t rasterized_fragments() const { return rasterized_fragments_; }
    uint64_t depth_test_passes() const { return depth_test_passes_; }
    uint64_t depth_writes() const { return depth_writes_; }
    uint32_t last_raster_address() const { return last_raster_address_; }
    uint64_t raster_output_hash() const { return raster_output_hash_; }
    uint64_t radial_color_batches() const { return radial_color_batches_; }
    uint64_t radial_color_triangles() const { return radial_color_triangles_; }
    uint64_t radial_color_fragments() const { return radial_color_fragments_; }
    uint64_t radial_color_depth_passes() const {
        return radial_color_depth_passes_;
    }
    uint64_t radial_color_depth_writes() const {
        return radial_color_depth_writes_;
    }
    uint64_t radial_color_writes() const { return radial_color_writes_; }
    uint32_t last_color_raster_address() const {
        return last_color_raster_address_;
    }
    uint64_t radial_color_output_hash() const {
        return radial_color_output_hash_;
    }
    uint64_t textured_color_batches() const { return textured_color_batches_; }
    uint64_t textured_color_triangles() const {
        return textured_color_triangles_;
    }
    uint64_t textured_color_fragments() const {
        return textured_color_fragments_;
    }
    uint64_t textured_color_depth_passes() const {
        return textured_color_depth_passes_;
    }
    uint64_t textured_color_depth_writes() const {
        return textured_color_depth_writes_;
    }
    uint64_t textured_color_writes() const { return textured_color_writes_; }
    uint64_t texture_samples() const { return texture_samples_; }
    uint32_t last_textured_raster_address() const {
        return last_textured_raster_address_;
    }
    uint64_t textured_color_output_hash() const {
        return textured_color_output_hash_;
    }
    uint64_t pitch_textured_color_batches() const {
        return pitch_textured_color_batches_;
    }
    uint64_t pitch_textured_color_triangles() const {
        return pitch_textured_color_triangles_;
    }
    uint64_t pitch_textured_color_fragments() const {
        return pitch_textured_color_fragments_;
    }
    uint64_t pitch_textured_color_depth_passes() const {
        return pitch_textured_color_depth_passes_;
    }
    uint64_t pitch_textured_color_depth_writes() const {
        return pitch_textured_color_depth_writes_;
    }
    uint64_t pitch_textured_color_writes() const {
        return pitch_textured_color_writes_;
    }
    uint64_t pitch_texture_samples() const {
        return pitch_texture_samples_;
    }
    uint32_t last_pitch_textured_raster_address() const {
        return last_pitch_textured_raster_address_;
    }
    uint64_t pitch_textured_color_output_hash() const {
        return pitch_textured_color_output_hash_;
    }
    uint64_t projected_color_batches() const { return projected_color_batches_; }
    uint64_t projected_color_triangles() const {
        return projected_color_triangles_;
    }
    uint64_t projected_color_fragments() const {
        return projected_color_fragments_;
    }
    uint64_t projected_color_depth_passes() const {
        return projected_color_depth_passes_;
    }
    uint64_t projected_color_depth_writes() const {
        return projected_color_depth_writes_;
    }
    uint64_t projected_color_writes() const { return projected_color_writes_; }
    uint64_t projected_texture_samples() const {
        return projected_texture_samples_;
    }
    uint32_t last_projected_raster_address() const {
        return last_projected_raster_address_;
    }
    uint64_t projected_color_output_hash() const {
        return projected_color_output_hash_;
    }
    uint64_t pitch_projected_color_batches() const {
        return pitch_projected_color_batches_;
    }
    uint64_t pitch_projected_color_triangles() const {
        return pitch_projected_color_triangles_;
    }
    uint64_t pitch_projected_color_fragments() const {
        return pitch_projected_color_fragments_;
    }
    uint64_t pitch_projected_color_depth_passes() const {
        return pitch_projected_color_depth_passes_;
    }
    uint64_t pitch_projected_color_depth_writes() const {
        return pitch_projected_color_depth_writes_;
    }
    uint64_t pitch_projected_color_writes() const {
        return pitch_projected_color_writes_;
    }
    uint64_t pitch_projected_texture_samples() const {
        return pitch_projected_texture_samples_;
    }
    uint32_t last_pitch_projected_raster_address() const {
        return last_pitch_projected_raster_address_;
    }
    uint64_t pitch_projected_color_output_hash() const {
        return pitch_projected_color_output_hash_;
    }
    uint64_t pitch_single_texture_batches() const {
        return pitch_single_texture_batches_;
    }
    uint64_t pitch_single_texture_triangles() const {
        return pitch_single_texture_triangles_;
    }
    uint64_t pitch_single_texture_fragments() const {
        return pitch_single_texture_fragments_;
    }
    uint64_t pitch_single_texture_depth_passes() const {
        return pitch_single_texture_depth_passes_;
    }
    uint64_t pitch_single_texture_writes() const {
        return pitch_single_texture_writes_;
    }
    uint64_t pitch_single_texture_samples() const {
        return pitch_single_texture_samples_;
    }
    uint32_t last_pitch_single_texture_raster_address() const {
        return last_pitch_single_texture_raster_address_;
    }
    uint64_t pitch_single_texture_output_hash() const {
        return pitch_single_texture_output_hash_;
    }
    uint64_t tex7_fan_batches() const { return tex7_fan_batches_; }
    uint64_t tex7_fan_triangles() const { return tex7_fan_triangles_; }
    uint64_t tex7_fan_fragments() const { return tex7_fan_fragments_; }
    uint64_t tex7_fan_depth_passes() const { return tex7_fan_depth_passes_; }
    uint64_t tex7_fan_writes() const { return tex7_fan_writes_; }
    uint64_t tex7_fan_samples() const { return tex7_fan_samples_; }
    uint32_t last_tex7_fan_raster_address() const {
        return last_tex7_fan_raster_address_;
    }
    uint64_t tex7_fan_output_hash() const { return tex7_fan_output_hash_; }
    uint64_t pitch_multi_texture_batches() const {
        return pitch_multi_texture_batches_;
    }
    uint64_t pitch_multi_texture_triangles() const {
        return pitch_multi_texture_triangles_;
    }
    uint64_t pitch_multi_texture_fragments() const {
        return pitch_multi_texture_fragments_;
    }
    uint64_t pitch_multi_texture_depth_passes() const {
        return pitch_multi_texture_depth_passes_;
    }
    uint64_t pitch_multi_texture_alpha_passes() const {
        return pitch_multi_texture_alpha_passes_;
    }
    uint64_t pitch_multi_texture_depth_writes() const {
        return pitch_multi_texture_depth_writes_;
    }
    uint64_t pitch_multi_texture_color_writes() const {
        return pitch_multi_texture_color_writes_;
    }
    uint64_t pitch_multi_texture_samples() const {
        return pitch_multi_texture_samples_;
    }
    uint32_t last_pitch_multi_texture_raster_address() const {
        return last_pitch_multi_texture_raster_address_;
    }
    uint64_t pitch_multi_texture_output_hash() const {
        return pitch_multi_texture_output_hash_;
    }
    uint64_t pitch_three_texture_batches() const {
        return pitch_three_texture_batches_;
    }
    uint64_t pitch_three_texture_triangles() const {
        return pitch_three_texture_triangles_;
    }
    uint64_t pitch_three_texture_fragments() const {
        return pitch_three_texture_fragments_;
    }
    uint64_t pitch_three_texture_depth_passes() const {
        return pitch_three_texture_depth_passes_;
    }
    uint64_t pitch_three_texture_depth_writes() const {
        return pitch_three_texture_depth_writes_;
    }
    uint64_t pitch_three_texture_color_writes() const {
        return pitch_three_texture_color_writes_;
    }
    uint64_t pitch_three_texture_samples() const {
        return pitch_three_texture_samples_;
    }
    uint64_t pitch_three_texture_zero_direction0() const {
        return pitch_three_texture_zero_direction0_;
    }
    uint64_t pitch_three_texture_zero_direction1() const {
        return pitch_three_texture_zero_direction1_;
    }
    uint64_t pitch_three_texture_rescale_direction0() const {
        return pitch_three_texture_rescale_direction0_;
    }
    uint64_t pitch_three_texture_rescale_direction1() const {
        return pitch_three_texture_rescale_direction1_;
    }
    uint64_t pitch_three_texture_xy_tie_direction0() const {
        return pitch_three_texture_xy_tie_direction0_;
    }
    uint64_t pitch_three_texture_xy_tie_direction1() const {
        return pitch_three_texture_xy_tie_direction1_;
    }
    uint32_t last_pitch_three_zero_factor3() const {
        return last_pitch_three_zero_factor3_;
    }
    uint32_t last_pitch_three_rescale_factor3() const {
        return last_pitch_three_rescale_factor3_;
    }
    uint32_t last_pitch_three_xy_tie_factor3() const {
        return last_pitch_three_xy_tie_factor3_;
    }
    uint32_t last_pitch_three_texture_raster_address() const {
        return last_pitch_three_texture_raster_address_;
    }
    uint64_t pitch_three_texture_output_hash() const {
        return pitch_three_texture_output_hash_;
    }
    uint64_t texf_projected_fan_batches() const { return texf_projected_fan_batches_; }
    uint64_t texf_projected_fan_triangles() const { return texf_projected_fan_triangles_; }
    uint64_t texf_projected_fan_fragments() const { return texf_projected_fan_fragments_; }
    uint64_t texf_projected_fan_alpha_passes() const { return texf_projected_fan_alpha_passes_; }
    uint64_t texf_projected_fan_writes() const { return texf_projected_fan_writes_; }
    uint64_t texf_projected_fan_samples() const { return texf_projected_fan_samples_; }
    uint32_t last_texf_projected_fan_raster_address() const {
        return last_texf_projected_fan_raster_address_;
    }
    uint64_t texf_projected_fan_output_hash() const {
        return texf_projected_fan_output_hash_;
    }
    uint64_t pitch_tex5_batches() const { return pitch_tex5_batches_; }
    uint64_t pitch_tex5_triangles() const { return pitch_tex5_triangles_; }
    uint64_t pitch_tex5_fragments() const { return pitch_tex5_fragments_; }
    uint64_t pitch_tex5_depth_passes() const {
        return pitch_tex5_depth_passes_;
    }
    uint64_t pitch_tex5_depth_writes() const {
        return pitch_tex5_depth_writes_;
    }
    uint64_t pitch_tex5_color_writes() const {
        return pitch_tex5_color_writes_;
    }
    uint64_t pitch_tex5_samples() const { return pitch_tex5_samples_; }
    uint32_t last_pitch_tex5_raster_address() const {
        return last_pitch_tex5_raster_address_;
    }
    uint64_t pitch_tex5_output_hash() const {
        return pitch_tex5_output_hash_;
    }
    uint64_t inline_linear_blit_batches() const {
        return inline_linear_blit_batches_;
    }
    uint64_t inline_linear_blit_triangles() const {
        return inline_linear_blit_triangles_;
    }
    uint64_t inline_linear_blit_fragments() const {
        return inline_linear_blit_fragments_;
    }
    uint64_t inline_linear_blit_writes() const {
        return inline_linear_blit_writes_;
    }
    uint64_t inline_linear_blit_samples() const {
        return inline_linear_blit_samples_;
    }
    uint32_t last_inline_linear_blit_raster_address() const {
        return last_inline_linear_blit_raster_address_;
    }
    uint64_t inline_linear_blit_output_hash() const {
        return inline_linear_blit_output_hash_;
    }
    uint64_t inline_color_batches() const { return inline_color_batches_; }
    uint64_t inline_color_triangles() const { return inline_color_triangles_; }
    uint64_t inline_color_fragments() const { return inline_color_fragments_; }
    uint64_t inline_color_alpha_passes() const {
        return inline_color_alpha_passes_;
    }
    uint64_t inline_color_writes() const { return inline_color_writes_; }
    uint64_t inline_texture_samples() const { return inline_texture_samples_; }
    uint32_t last_inline_raster_address() const {
        return last_inline_raster_address_;
    }
    uint64_t inline_color_output_hash() const {
        return inline_color_output_hash_;
    }
    // Generic register-combiner evaluator: rasterizes any witnessed raster
    // state that does not match one of the hand-derived Profile fingerprints
    // above, by decoding and evaluating the actual bound combiner state
    // (stage ICW/OCW, factors, final combiner) instead of returning
    // executed=false. See rasterize_generic_combiner_batch in nv2a.cpp for
    // the supported subset and skip_reason names for what still falls back
    // to recorder-only.
    uint64_t generic_combiner_batches() const {
        return generic_combiner_batches_;
    }
    uint64_t generic_combiner_attempted_batches() const {
        return generic_combiner_attempted_batches_;
    }
    uint64_t generic_combiner_executed_batches() const {
        return generic_combiner_executed_batches_;
    }
    uint64_t generic_combiner_accelerated_batches() const {
        return generic_combiner_accelerated_batches_;
    }
    uint64_t generic_combiner_resident_candidates() const {
        return generic_combiner_resident_candidates_;
    }
    uint64_t generic_combiner_resident_supported() const {
        return generic_combiner_resident_supported_;
    }
    uint64_t generic_combiner_resident_uploads() const {
        return generic_combiner_resident_uploads_;
    }
    uint64_t generic_combiner_resident_reuses() const {
        return generic_combiner_resident_reuses_;
    }
    uint64_t generic_combiner_resident_target_switch_resolves() const {
        return generic_combiner_resident_target_switch_resolves_;
    }
    uint64_t generic_combiner_resident_submits() const {
        return generic_combiner_resident_submits_;
    }
    enum class GenericResidentRejectReason {
        kNoRenderer,
        kUnsupportedPrimitive,
        kVertexCount,
        kTextureInput,
        kDepth,
        kColorMask,
        kColorFormat,
        kStageCount,
        kAlphaMode,
        kBlendMode,
        kShaderProgram,
        kOtherStageInput,
        kCombinerWords,
        kVertexExpansion,
        kBackendUnsupported,
        kCount,
    };
    static size_t generic_resident_reject_reason_count() {
        return static_cast<size_t>(GenericResidentRejectReason::kCount);
    }
    static const char* generic_resident_reject_reason_name(size_t index) {
        static constexpr const char* kNames[] = {
            "no-renderer",
            "unsupported-primitive",
            "vertex-count",
            "texture-input",
            "depth",
            "color-mask",
            "color-format",
            "stage-count",
            "alpha-mode",
            "blend-mode",
            "shader-program",
            "other-stage-input",
            "combiner-words",
            "vertex-expansion",
            "backend-unsupported",
        };
        return index < (sizeof(kNames) / sizeof(kNames[0])) ? kNames[index]
                                                             : "unknown";
    }
    uint64_t generic_combiner_resident_reject_batches(size_t index) const {
        return index < generic_combiner_resident_rejects_.size()
                   ? generic_combiner_resident_rejects_[index]
                   : 0;
    }
    uint64_t generic_combiner_texture_modulate_observed() const {
        return generic_combiner_texture_modulate_observed_;
    }
    uint64_t generic_combiner_texture_modulate_exact() const {
        return generic_combiner_texture_modulate_exact_;
    }
    enum class GenericTextureModulateRejectReason {
        kUnsupportedPrimitive,
        kVertexCount,
        kTextureInput,
        kDepth,
        kColorMask,
        kColorFormat,
        kStageCount,
        kAlphaMode,
        kBlendMode,
        kShaderProgram,
        kOtherStageInput,
        kCombinerWords,
        kTexture0Registers,
        kTexture0Decoded,
        kTargetSurface,
        kCount,
    };
    static size_t generic_texture_modulate_reject_reason_count() {
        return static_cast<size_t>(GenericTextureModulateRejectReason::kCount);
    }
    static const char* generic_texture_modulate_reject_reason_name(size_t index) {
        static constexpr const char* kNames[] = {
            "unsupported-primitive",
            "vertex-count",
            "texture-input",
            "depth",
            "color-mask",
            "color-format",
            "stage-count",
            "alpha-mode",
            "blend-mode",
            "shader-program",
            "other-stage-input",
            "combiner-words",
            "texture0-registers",
            "texture0-decoded",
            "target-surface",
        };
        return index < (sizeof(kNames) / sizeof(kNames[0])) ? kNames[index]
                                                             : "unknown";
    }
    uint64_t generic_combiner_texture_modulate_reject_batches(size_t index) const {
        return index < generic_combiner_texture_modulate_rejects_.size()
                   ? generic_combiner_texture_modulate_rejects_[index]
                   : 0;
    }
    uint64_t generic_combiner_fallback_batches() const {
        return generic_combiner_fallback_batches_;
    }
    uint64_t generic_combiner_triangles() const {
        return generic_combiner_triangles_;
    }
    uint64_t generic_combiner_fragments() const {
        return generic_combiner_fragments_;
    }
    uint64_t generic_combiner_depth_passes() const {
        return generic_combiner_depth_passes_;
    }
    uint64_t generic_combiner_writes() const {
        return generic_combiner_writes_;
    }
    uint64_t generic_combiner_color_reads() const {
        return generic_combiner_color_reads_;
    }
    uint64_t generic_combiner_depth_reads() const {
        return generic_combiner_depth_reads_;
    }
    uint64_t generic_combiner_texture_samples() const {
        return generic_combiner_texture_samples_;
    }
    uint32_t last_generic_combiner_raster_address() const {
        return last_generic_combiner_raster_address_;
    }
    uint64_t generic_combiner_output_hash() const {
        return generic_combiner_output_hash_;
    }
    // The legacy census is intentionally kept for existing diagnostics: each pair is
    // (fingerprint, successful generic batch count).
    size_t generic_combiner_fingerprint_census(
        std::vector<std::pair<uint64_t, uint64_t>>& out) const {
        out.clear();
        out.reserve(generic_combiner_fingerprints_.size());
        for (const auto& entry : generic_combiner_fingerprints_)
            out.push_back({entry.fingerprint, entry.batches});
        return out.size();
    }
    // Cumulative per-state attribution for --perf-accounting. Fragment/write/wall
    // fields remain zero when accounting was disabled, avoiding profiler work during
    // normal replays while preserving the legacy batch census above.
    struct GenericCombinerFingerprintStats {
        uint64_t fingerprint = 0;
        uint64_t batches = 0;
        uint64_t fragments = 0;
        uint64_t writes = 0;
        uint64_t color_reads = 0;
        uint64_t depth_reads = 0;
        uint64_t wall_ns = 0;
        uint64_t texture_setup_ns = 0;
        uint64_t software_raster_ns = 0;
        uint64_t flush_ns = 0;
        uint64_t bbox_pixels = 0;
        uint64_t max_batch_fragments = 0;
        uint64_t max_batch_bbox_pixels = 0;
        uint64_t texture_modulate_batches = 0;
        uint64_t texture_modulate_exact_batches = 0;
        std::array<uint64_t,
                  static_cast<size_t>(GenericTextureModulateRejectReason::kCount)>
            texture_modulate_reject_batches{};
        uint64_t resident_batches = 0;
        uint64_t resident_exact_batches = 0;
        std::array<uint64_t,
                  static_cast<size_t>(GenericResidentRejectReason::kCount)>
            resident_reject_batches{};
        // Populated only while --perf-accounting is enabled. This links to the
        // immutable first full state in raster_state_samples_, keeping normal
        // replay overhead to one compact index rather than copying state here.
        uint32_t raster_state_sample_index = UINT32_MAX;
        uint64_t full_state_hash = 0;
        uint8_t primitive_mode = 0;
        uint32_t min_vertices = 0;
        uint32_t max_vertices = 0;
        bool first_triangle_recorded = false;
        std::array<int64_t, 6> first_triangle_xy_16{};
        std::array<int32_t, 4> first_triangle_bounds{};
        uint64_t first_triangle_area_16 = 0;
        bool first_vertices_recorded = false;
        uint32_t first_vertex_count = 0;
        std::array<std::array<float, 4>, 4> first_vertex_position{};
        std::array<std::array<float, 4>, 4> first_vertex_diffuse{};
        std::array<std::array<float, 4>, 4> first_vertex_specular{};
        std::array<std::array<std::array<float, 4>, 4>, 4> first_vertex_texture{};
    };
    size_t generic_combiner_fingerprint_perf_census(
        std::vector<GenericCombinerFingerprintStats>& out) const {
        out = generic_combiner_fingerprints_;
        return out.size();
    }
    enum class GenericSkipReason {
        kUnsupportedSurface,
        kUnsupportedPrimitive,
        kStageCount,
        kStageInput,
        kOcwFeature,
        kFinalCombiner,
        kAlphaFunc,
        kDepthFunc,
        kBlendMode,
        kTextureStage,
        kUnsupportedColorFormat,
        kUnsupportedShadeMode,
        kUnsupportedPolygonMode,
        kUnsupportedCullFace,
        kUnsupportedFrontFace,
        kUnsupportedZMinMax,
        kUnsupportedControl0,
        kUnsupportedColorMask,
        kUnsupportedEnableBits,
        kUnsupportedBlendEquation,
        kUnsupportedTextureFormat,
        kUnsupportedTextureWrap,
        kUnsupportedTextureShaderProgram,
        kCount,
    };
    static size_t generic_skip_reason_count() {
        return static_cast<size_t>(GenericSkipReason::kCount);
    }
    static const char* generic_skip_reason_name(size_t index) {
        static constexpr const char* kNames[] = {
            "unsupported-surface",     "unsupported-primitive",
            "stage-count",             "stage-input",
            "ocw-feature",             "final-combiner",
            "alpha-func",              "depth-func",
            "blend-mode",              "texture-stage",
            "unsupported-color-format","unsupported-shademode",
            "unsupported-polygonmode", "unsupported-cullface",
            "unsupported-frontface",   "unsupported-zminmax",
            "unsupported-control0",    "unsupported-colormask",
            "unsupported-enable-bits", "unsupported-blend-equation",
            "unsupported-texture-format", "unsupported-texture-wrap",
            "unsupported-texture-shader-program",
        };
        return index < (sizeof(kNames) / sizeof(kNames[0])) ? kNames[index]
                                                             : "unknown";
    }
    uint64_t generic_skip_reason_batches(size_t index) const {
        return index < generic_skip_reasons_.size()
                   ? generic_skip_reasons_[index]
                   : 0;
    }
    // Detail census for a skip reason: (register value or bitmask, count). Lets a
    // "named reason" carry enough to act on next time (e.g. unsupported-color-format
    // with detail=5 means "format 5 was requested and is not yet handled"), instead of
    // a single opaque bucket per reason.
    size_t generic_skip_detail_census(
        GenericSkipReason reason,
        std::vector<std::pair<uint64_t, uint64_t>>& out) const {
        out.clear();
        for (const auto& entry : generic_skip_details_) {
            if (entry.reason == reason) out.push_back({entry.detail, entry.count});
        }
        return out.size();
    }
    uint64_t immediate_quad_raster_batches() const {
        return immediate_quad_raster_batches_;
    }
    uint64_t immediate_quad_raster_triangles() const {
        return immediate_quad_raster_triangles_;
    }
    uint64_t immediate_quad_raster_fragments() const {
        return immediate_quad_raster_fragments_;
    }
    uint64_t immediate_quad_raster_alpha_passes() const {
        return immediate_quad_raster_alpha_passes_;
    }
    uint64_t immediate_quad_raster_writes() const {
        return immediate_quad_raster_writes_;
    }
    uint64_t immediate_quad_texture_samples() const {
        return immediate_quad_texture_samples_;
    }
    uint32_t last_immediate_quad_raster_address() const {
        return last_immediate_quad_raster_address_;
    }
    uint64_t immediate_quad_raster_output_hash() const {
        return immediate_quad_raster_output_hash_;
    }
    uint64_t immediate_quad_rs12_candidates() const {
        return immediate_quad_rs12_candidates_;
    }
    uint32_t last_immediate_quad_rs12_reject_mask() const {
        return last_immediate_quad_rs12_reject_mask_;
    }
    uint32_t last_immediate_quad_rs12_raster_reject_mask() const {
        return last_immediate_quad_rs12_raster_reject_mask_;
    }
    uint64_t immediate_quad_i8_raster_batches() const {
        return immediate_quad_i8_raster_batches_;
    }
    uint64_t immediate_quad_i8_raster_triangles() const {
        return immediate_quad_i8_raster_triangles_;
    }
    uint64_t immediate_quad_i8_raster_fragments() const {
        return immediate_quad_i8_raster_fragments_;
    }
    uint64_t immediate_quad_i8_raster_alpha_passes() const {
        return immediate_quad_i8_raster_alpha_passes_;
    }
    uint64_t immediate_quad_i8_raster_writes() const {
        return immediate_quad_i8_raster_writes_;
    }
    uint64_t immediate_quad_i8_texture_samples() const {
        return immediate_quad_i8_texture_samples_;
    }
    uint32_t last_immediate_quad_i8_raster_address() const {
        return last_immediate_quad_i8_raster_address_;
    }
    uint64_t immediate_quad_i8_raster_output_hash() const {
        return immediate_quad_i8_raster_output_hash_;
    }
    uint64_t immediate_quad_i8_candidates() const {
        return immediate_quad_i8_candidates_;
    }
    uint32_t last_immediate_quad_i8_reject_mask() const {
        return last_immediate_quad_i8_reject_mask_;
    }
    uint32_t last_immediate_quad_i8_raster_reject_mask() const {
        return last_immediate_quad_i8_raster_reject_mask_;
    }
    uint64_t immediate_quad_a8_raster_batches() const {
        return immediate_quad_a8_raster_batches_;
    }
    uint64_t immediate_quad_a8_raster_triangles() const {
        return immediate_quad_a8_raster_triangles_;
    }
    uint64_t immediate_quad_a8_raster_fragments() const {
        return immediate_quad_a8_raster_fragments_;
    }
    uint64_t immediate_quad_a8_raster_alpha_passes() const {
        return immediate_quad_a8_raster_alpha_passes_;
    }
    uint64_t immediate_quad_a8_raster_writes() const {
        return immediate_quad_a8_raster_writes_;
    }
    uint64_t immediate_quad_a8_texture_samples() const {
        return immediate_quad_a8_texture_samples_;
    }
    uint32_t last_immediate_quad_a8_raster_address() const {
        return last_immediate_quad_a8_raster_address_;
    }
    uint64_t immediate_quad_a8_raster_output_hash() const {
        return immediate_quad_a8_raster_output_hash_;
    }
    uint64_t immediate_quad_a8_candidates() const {
        return immediate_quad_a8_candidates_;
    }
    uint32_t last_immediate_quad_a8_reject_mask() const {
        return last_immediate_quad_a8_reject_mask_;
    }
    uint32_t last_immediate_quad_a8_raster_reject_mask() const {
        return last_immediate_quad_a8_raster_reject_mask_;
    }
    uint64_t immediate_quad_menu_i8_raster_batches() const {
        return immediate_quad_menu_i8_raster_batches_;
    }
    uint64_t immediate_quad_menu_i8_raster_triangles() const {
        return immediate_quad_menu_i8_raster_triangles_;
    }
    uint64_t immediate_quad_menu_i8_raster_fragments() const {
        return immediate_quad_menu_i8_raster_fragments_;
    }
    uint64_t immediate_quad_menu_i8_raster_alpha_passes() const {
        return immediate_quad_menu_i8_raster_alpha_passes_;
    }
    uint64_t immediate_quad_menu_i8_raster_writes() const {
        return immediate_quad_menu_i8_raster_writes_;
    }
    uint64_t immediate_quad_menu_i8_texture_samples() const {
        return immediate_quad_menu_i8_texture_samples_;
    }
    uint32_t last_immediate_quad_menu_i8_raster_address() const {
        return last_immediate_quad_menu_i8_raster_address_;
    }
    uint64_t immediate_quad_menu_i8_raster_output_hash() const {
        return immediate_quad_menu_i8_raster_output_hash_;
    }
    uint64_t immediate_quad_menu_i8_candidates() const {
        return immediate_quad_menu_i8_candidates_;
    }
    uint32_t last_immediate_quad_menu_i8_reject_mask() const {
        return last_immediate_quad_menu_i8_reject_mask_;
    }
    uint32_t last_immediate_quad_menu_i8_raster_reject_mask() const {
        return last_immediate_quad_menu_i8_raster_reject_mask_;
    }
    uint64_t immediate_quad_artwork_i8_raster_batches() const {
        return immediate_quad_artwork_i8_raster_batches_;
    }
    uint64_t immediate_quad_artwork_i8_raster_triangles() const {
        return immediate_quad_artwork_i8_raster_triangles_;
    }
    uint64_t immediate_quad_artwork_i8_raster_fragments() const {
        return immediate_quad_artwork_i8_raster_fragments_;
    }
    uint64_t immediate_quad_artwork_i8_raster_alpha_passes() const {
        return immediate_quad_artwork_i8_raster_alpha_passes_;
    }
    uint64_t immediate_quad_artwork_i8_raster_writes() const {
        return immediate_quad_artwork_i8_raster_writes_;
    }
    uint64_t immediate_quad_artwork_i8_texture_samples() const {
        return immediate_quad_artwork_i8_texture_samples_;
    }
    uint32_t last_immediate_quad_artwork_i8_raster_address() const {
        return last_immediate_quad_artwork_i8_raster_address_;
    }
    uint64_t immediate_quad_artwork_i8_raster_output_hash() const {
        return immediate_quad_artwork_i8_raster_output_hash_;
    }
    uint64_t immediate_quad_artwork_i8_candidates() const {
        return immediate_quad_artwork_i8_candidates_;
    }
    uint32_t last_immediate_quad_artwork_i8_reject_mask() const {
        return last_immediate_quad_artwork_i8_reject_mask_;
    }
    uint32_t last_immediate_quad_artwork_i8_raster_reject_mask() const {
        return last_immediate_quad_artwork_i8_raster_reject_mask_;
    }
    uint64_t immediate_quad_main_panel_raster_batches() const { return immediate_quad_main_panel_raster_batches_; }
    uint64_t immediate_quad_main_panel_raster_triangles() const { return immediate_quad_main_panel_raster_triangles_; }
    uint64_t immediate_quad_main_panel_raster_fragments() const { return immediate_quad_main_panel_raster_fragments_; }
    uint64_t immediate_quad_main_panel_raster_alpha_passes() const { return immediate_quad_main_panel_raster_alpha_passes_; }
    uint64_t immediate_quad_main_panel_raster_writes() const { return immediate_quad_main_panel_raster_writes_; }
    uint64_t immediate_quad_main_panel_texture_samples() const { return immediate_quad_main_panel_texture_samples_; }
    uint32_t last_immediate_quad_main_panel_raster_address() const { return last_immediate_quad_main_panel_raster_address_; }
    uint64_t immediate_quad_main_panel_raster_output_hash() const { return immediate_quad_main_panel_raster_output_hash_; }
    uint64_t immediate_quad_main_panel_candidates() const { return immediate_quad_main_panel_candidates_; }
    uint32_t last_immediate_quad_main_panel_reject_mask() const { return last_immediate_quad_main_panel_reject_mask_; }
    uint32_t last_immediate_quad_main_panel_raster_reject_mask() const { return last_immediate_quad_main_panel_raster_reject_mask_; }
    uint64_t immediate_quad_fullscreen_raster_batches() const { return immediate_quad_fullscreen_raster_batches_; }
    uint64_t immediate_quad_fullscreen_raster_triangles() const { return immediate_quad_fullscreen_raster_triangles_; }
    uint64_t immediate_quad_fullscreen_raster_fragments() const { return immediate_quad_fullscreen_raster_fragments_; }
    uint64_t immediate_quad_fullscreen_raster_alpha_passes() const { return immediate_quad_fullscreen_raster_alpha_passes_; }
    uint64_t immediate_quad_fullscreen_raster_writes() const { return immediate_quad_fullscreen_raster_writes_; }
    uint64_t immediate_quad_fullscreen_texture_samples() const { return immediate_quad_fullscreen_texture_samples_; }
    uint32_t last_immediate_quad_fullscreen_raster_address() const { return last_immediate_quad_fullscreen_raster_address_; }
    uint64_t immediate_quad_fullscreen_raster_output_hash() const { return immediate_quad_fullscreen_raster_output_hash_; }
    uint64_t immediate_quad_fullscreen_candidates() const { return immediate_quad_fullscreen_candidates_; }
    uint32_t last_immediate_quad_fullscreen_reject_mask() const { return last_immediate_quad_fullscreen_reject_mask_; }
    uint32_t last_immediate_quad_fullscreen_raster_reject_mask() const { return last_immediate_quad_fullscreen_raster_reject_mask_; }
    uint64_t vertex_batches() const { return vertex_batches_; }
    uint64_t submitted_vertices() const { return submitted_vertices_; }
    uint64_t vertex_attribute_fetches() const { return vertex_attribute_fetches_; }
    uint64_t immediate_methods() const { return immediate_methods_; }
    uint64_t immediate_vertices() const { return immediate_vertices_total_; }
    uint64_t immediate_batches() const { return immediate_batches_; }
    uint64_t inline_array_batches() const { return inline_array_batches_; }
    uint64_t inline_array_words() const { return inline_array_words_total_; }
    uint64_t inline_array_vertices() const { return inline_array_vertices_total_; }
    uint64_t inline_attribute_decodes() const {
        return inline_attribute_decodes_;
    }
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
    uint64_t transform_mode_batches(uint32_t mode) const {
        return mode < transform_mode_batches_.size() ? transform_mode_batches_[mode] : 0;
    }
    uint64_t transformed_vertices() const { return transformed_vertices_; }
    uint64_t transform_program_words() const { return transform_program_words_; }
    uint64_t transform_constant_words() const { return transform_constant_words_; }
    uint64_t transform_output_hash() const { return transform_output_hash_; }
    std::array<float, 4> last_transformed_position() const {
        return transformed_batch_.empty()
                   ? std::array<float, 4>{} : transformed_batch_.back().position;
    }
    std::array<float, 4> last_transformed_diffuse() const {
        return transformed_batch_.empty()
                   ? std::array<float, 4>{} : transformed_batch_.back().diffuse;
    }
    std::array<float, 4> last_transformed_specular() const {
        return transformed_batch_.empty()
                   ? std::array<float, 4>{} : transformed_batch_.back().specular;
    }
    // Read-only diagnostic observation. The reference expires on submission;
    // retaining it must never replace command consumption or actual draws.
    const std::vector<Nv2aGenericCombinerVertex>& last_transformed_batch() const {
        return transformed_batch_;
    }
    uint64_t fixed_skin_batches(uint32_t mode) const {
        return mode < fixed_skin_batches_.size() ? fixed_skin_batches_[mode] : 0;
    }
    uint64_t fixed_lighting_batches() const { return fixed_lighting_batches_; }
    uint64_t fixed_normalization_batches() const { return fixed_normalization_batches_; }
    uint64_t fixed_specular_batches() const { return fixed_specular_batches_; }
    uint64_t fixed_fog_batches() const { return fixed_fog_batches_; }
    uint64_t fixed_texture_matrix_batches() const {
        return fixed_texture_matrix_batches_;
    }
    uint64_t fixed_texgen_batches() const { return fixed_texgen_batches_; }
    uint64_t fixed_light_type_batches(uint32_t type) const {
        return type < fixed_light_type_batches_.size()
                   ? fixed_light_type_batches_[type] : 0;
    }
    uint64_t fixed_color_material_batches(uint32_t sources) const {
        return sources < fixed_color_material_batches_.size()
                   ? fixed_color_material_batches_[sources] : 0;
    }
    uint64_t fixed_light_control_batches(uint32_t flags) const {
        return flags < fixed_light_control_batches_.size()
                   ? fixed_light_control_batches_[flags] : 0;
    }
    // =================== stage (d) seam census (beads-xbox.1.15.4) ==========
    // DESIGN-NV2A-SEAM.md section 2.2(c) enumerates ten guest-anchored points at
    // which a threaded consumer would have to drain the job queue. Stage (d) has
    // no queue -- every job runs inline at the latch point -- so these anchors do
    // nothing but COUNT how often a drain would have been required. That is
    // measurement M5 (section 6), the measurement that sizes, or kills, stage (e)
    // before any threading code is written.
    //
    // The order of this enum is the order of the table in section 2.2(c) and is
    // pinned by tests/test_seam_guards.py.
    enum SeamAnchor : size_t {
        kSeamAnchorSemaphoreRelease = 0,  // method 0x1D70 (F11)
        kSeamAnchorClearSurface,          // CLEAR_SURFACE (F4)
        kSeamAnchorScanout,               // capture_scanout (F5)
        kSeamAnchorSetFrame,              // set_frame / vblank (F12)
        kSeamAnchorFlipStallClear,        // flip-stall clear, resume_pushbuffers (F10)
        kSeamAnchorTextureOverlap,        // texture snapshot over a pending dest (F6)
        kSeamAnchorPgraphRegisterRead,    // any PGRAPH register read
        kSeamAnchorResolveOverlap,        // resolve_renderer_*_overlap (F3)
        kSeamAnchorSurfaceRetarget,       // SET_SURFACE_{COLOR,ZETA}_OFFSET/FORMAT/PITCH
        kSeamAnchorCheckpoint,            // frame-series checkpoint / debug read / shutdown
        kSeamAnchorCount,
    };
    static const char* seam_anchor_name(size_t anchor);
    // Why a latched job could not have been handed to a worker. Section 1.3
    // (direct pointer), section 4.3 option O3 (per-write hash fold), F3 (GL
    // path), F15 (debug callbacks).
    enum SeamNotThreadable : uint32_t {
        kSeamThreadable = 0,
        kSeamNoDirectPointer,   // F2: a surface region had no mem_direct_ pointer
        kSeamHashOn,            // F14/O3: the per-write FNV fold is enabled
        kSeamRendererPath,      // F3: the generic combiner reached the GL backend
        kSeamDebugCallback,     // F15: a diagnostic callback is installed
        kSeamNotThreadableReasons,
    };
    static const char* seam_reason_name(uint32_t reason);

    uint64_t seam_jobs_latched() const { return seam_jobs_latched_; }
    uint64_t seam_jobs_threadable() const { return seam_jobs_threadable_; }
    uint64_t seam_jobs_queueable() const { return seam_jobs_queueable_; }
    uint64_t seam_jobs_not_executed() const { return seam_jobs_not_executed_; }
    uint64_t seam_not_threadable(uint32_t reason) const {
        return reason < kSeamNotThreadableReasons
                   ? seam_not_threadable_[reason] : 0;
    }
    uint64_t seam_anchor_hits(size_t anchor) const {
        return anchor < kSeamAnchorCount ? seam_anchor_hits_[anchor] : 0;
    }
    uint64_t seam_anchor_drains(size_t anchor) const {
        return anchor < kSeamAnchorCount ? seam_anchor_drains_[anchor] : 0;
    }
    uint64_t seam_anchor_pending_sum(size_t anchor) const {
        return anchor < kSeamAnchorCount ? seam_anchor_pending_sum_[anchor] : 0;
    }
    uint64_t seam_anchor_pending_max(size_t anchor) const {
        return anchor < kSeamAnchorCount ? seam_anchor_pending_max_[anchor] : 0;
    }
    // Second, independent simulation: jobs latched between vblanks, reset only at
    // set_frame. This is the ceiling the anchor drains cut into (M2).
    uint64_t seam_vblank_jobs_max() const { return seam_vblank_jobs_max_; }
    uint64_t seam_vblank_jobs_sum() const { return seam_vblank_jobs_sum_; }
    uint64_t seam_vblanks() const { return seam_vblanks_; }
    // Subset of resolve-overlap anchor hits that actually moved bytes back into
    // guest RAM. A resolve that transfers nothing neither reads nor writes guest
    // memory, so reporting both lets the conservative anchor be judged.
    uint64_t seam_resolve_transfers() const { return seam_resolve_transfers_; }
    // Anchor 10 is raised by the host at a frame-series checkpoint / debug read /
    // shutdown; those live in main.cpp, not in the device.
    void seam_checkpoint_anchor();

    // ============ stage (f) GPU-resident consumer: residency model ==========
    // beads-xbox.1.15.7. ONE state machine, used in two modes:
    //   * simulate  -- no GL calls; every transition is counted only. This is
    //     the always-on census that sizes the design before it is built, and it
    //     answers the question stage (f) lives or dies on: how often would a
    //     GPU-authoritative surface have to be resolved back into guest RAM
    //     because a CPU consumer (another profile, a clear, a blit, scanout)
    //     touched it. It is NOT an arm-then-capture probe: it accumulates from
    //     process start and is queried after the fact.
    //   * execute   -- the same transitions, with the uploads/resolves actually
    //     performed against the GL backend.
    // The transition set is identical in both modes by construction, so the
    // census measured in simulate mode is the cost model of execute mode.
    enum GpuResidencyCause : uint32_t {
        kGpuResCauseClearSurface = 0,
        kGpuResCauseWitnessedCpuDraw,
        kGpuResCauseGeneric,
        kGpuResCauseImageBlit,
        kGpuResCauseScanout,
        kGpuResCauseImmediateQuad,
        kGpuResCauseRetarget,
        kGpuResCauseOther,
        kGpuResidencyCauses,
    };
    static const char* gpu_residency_cause_name(size_t cause);
    enum GpuRejectReason : uint32_t {
        kGpuRejectHashOn = 0,
        kGpuRejectDebugCb,
        kGpuRejectCombiner,
        kGpuRejectRasterSubset,
        kGpuRejectSurface,
        kGpuRejectTexture,
        kGpuRejectPrimitive,
        kGpuRejectVertexRange,
        kGpuRejectVertexNonFinite,
        kGpuRejectReasons,
    };
    static const char* gpu_reject_reason_name(size_t reason);
    uint64_t gpu_profile_reject(size_t reason) const {
        return reason < kGpuRejectReasons ? gpu_profile_reject_[reason] : 0;
    }
    // Phase 2 (beads-xbox.1.15.10) attribution. `Profile` lives in an anonymous
    // namespace inside nv2a.cpp and `job.profile` is its underlying integer;
    // this is the slot count for the per-profile census arrays below, with the
    // name table supplied by gpu_res_profile_name().
    static constexpr size_t kGpuResProfileSlots = 16;
    static const char* gpu_res_profile_name(size_t profile);
    struct GpuResidencyCensus {
        uint64_t gpu_draws = 0;        // draws the GPU executor would take
        uint64_t cpu_draws = 0;        // witnessed draws it would not
        uint64_t uploads = 0;          // colour surface uploads
        uint64_t upload_bytes = 0;
        uint64_t depth_uploads = 0;
        uint64_t depth_upload_bytes = 0;
        uint64_t resolves = 0;         // colour surface resolves
        uint64_t resolve_bytes = 0;
        uint64_t depth_resolves = 0;
        uint64_t depth_resolve_bytes = 0;
        uint64_t clears_on_gpu = 0;    // CLEAR_SURFACE served as glClear
        uint64_t runs = 0;             // consecutive GPU-draw runs
        uint64_t run_sum = 0;          // total GPU draws inside those runs
        uint64_t run_max = 0;
        std::array<uint64_t, kGpuResidencyCauses> cause{};
        // What the CONSERVATIVE anchor policy would add on top of the
        // demand-driven one: a resolve at every semaphore release / PGRAPH
        // register read that found the surface GPU-authoritative.
        uint64_t semaphore_resolves = 0;
        uint64_t semaphore_resolve_bytes = 0;
        uint64_t pgraph_read_resolves = 0;
        // Phase 2 (beads-xbox.1.15.10). Two very different distributions:
        //   cpu_draws_profile -- how many witnessed draws of each profile the
        //     GPU executor did NOT take. This is the CPU work still on the
        //     emulation thread and its helpers.
        //   breaks_profile    -- how many residency BREAKS (a full-surface
        //     resolve) each profile actually caused, i.e. how many of those
        //     draws landed on a surface that was GPU-authoritative at the time.
        // The order of work follows the SECOND, not the first: a profile with
        // 142 k draws that never touches a resident surface costs zero round
        // trips, and a profile with 100 draws that alternates on one costs 100.
        std::array<uint64_t, kGpuResProfileSlots> cpu_draws_profile{};
        std::array<uint64_t, kGpuResProfileSlots> breaks_profile{};
    };
    const GpuResidencyCensus& gpu_residency_census() const {
        return gpu_res_;
    }
    // beads-xbox.1.15.10 phase 5, item 1. The phase-4 rule -- "a residency
    // BREAK is caused by whichever CPU consumer touches the surface FIRST, so
    // removing the first one in line RE-ATTRIBUTES the break rather than
    // removing it; only removing the LAST CPU consumer of a surface removes its
    // resolves" -- makes `breaks_profile` above the wrong work order all by
    // itself. What decides the order is the SET of CPU consumers per
    // destination plane: a plane is only freed of round trips when EVERY family
    // that draws to it is on the GPU.
    //
    // This is that set, and it is a direct count at the GPU/CPU decision point
    // (not a model): one row per distinct destination plane address, with the
    // GPU-taken and CPU-executed draw counts and, for the CPU ones, the
    // per-profile split. Colour and zeta planes are counted in SEPARATE tables
    // because a depth-only profile (kPitchZ16Depth) has no colour plane at all
    // and would otherwise be invisible in a pair-keyed table.
    static constexpr size_t kGpuResSurfaceSlots = 32;
    struct GpuResSurfaceRow {
        uint32_t address = 0;
        uint32_t bytes = 0;
        uint64_t gpu_draws = 0;
        uint64_t cpu_draws = 0;
        std::array<uint64_t, kGpuResProfileSlots> cpu_draws_profile{};
    };
    struct GpuResSurfaceTable {
        std::array<GpuResSurfaceRow, kGpuResSurfaceSlots> rows{};
        size_t count = 0;
        uint64_t overflow = 0;  // draws that found no free row
    };
    const GpuResSurfaceTable& gpu_res_color_surfaces() const {
        return gpu_res_color_surfaces_;
    }
    const GpuResSurfaceTable& gpu_res_depth_surfaces() const {
        return gpu_res_depth_surfaces_;
    }

    // ---- phase 5 item 3: the vertex-pipeline split ------------------------
    // `nv2a_vertex` is 8.551 s of a 63.909 s instrumented run and the existing
    // [perf-nv2a-split] line resolves only its per-vertex transform loop
    // (7.817 s) against everything else (0.734 s). This census splits it
    // further so the lever is chosen on measurement:
    //   parse_*   -- nv2a_vsh_parse_program, per batch. ALWAYS ON (two clock
    //                reads per batch, 455 k per run, under 5 ms).
    //   fetch_* / vsh_* -- per VERTEX, so they are gated behind
    //                --nv2a-vertex-census: at tens of millions of vertices two
    //                clock reads each is a real cost and must not ride in a
    //                shipping run. Rule 9: an instrument carries its own
    //                denominator and its own cost.
    struct VertexCensus {
        uint64_t batches = 0;
        uint64_t vertices = 0;          // vertices entering the transform loop
        uint64_t reuse_hits = 0;        // batch transform cache hits
        uint64_t parses = 0;            // actual nv2a_vsh_parse_program calls
        uint64_t parse_reuses = 0;      // batches served from the cached parse
        uint64_t parse_ticks = 0;
        uint64_t fetch_ticks = 0;
        uint64_t fetch_n = 0;
        uint64_t vsh_ticks = 0;
        uint64_t vsh_n = 0;
        uint64_t vsh_context_vertices = 0;  // the 3.7 kB full-state path
        uint64_t fixed_function_vertices = 0;
        // beads-xbox.1.15.11. Which of the two step forms actually ran, so the
        // compiled path can never be claimed without its own denominator.
        uint64_t vsh_compiled_vertices = 0;
        uint64_t vsh_interpreted_vertices = 0;
        uint64_t vsh_compiles = 0;      // programs pre-decoded (per parse)
        uint64_t vsh_compile_ticks = 0;  // and what that cost, always on
        uint64_t vsh_compile_declines = 0;
        uint64_t discarded_deltas = 0; // Invalid direct census clock intervals.
    };
    const VertexCensus& vertex_census() const { return vertex_census_; }

    // ---- the distinct-program census (beads-xbox.1.15.11) -----------------
    // The compiled step form is amortised over every vertex a program runs for,
    // so "how many DISTINCT programs are there and what share of the vertices
    // does each carry" is the number that decides whether the lever exists at
    // all. ALWAYS ON: the fingerprint is folded once per PARSE (19 542 per
    // canonical run with the phase-5 cache on, not once per batch), and the
    // per-batch cost is a stored row pointer plus two adds.
    struct VshProgramCensusRow {
        uint64_t fingerprint = 0;
        uint64_t batches = 0;
        uint64_t vertices = 0;  // vertices that actually ran the program
        uint32_t start_slot = 0;
        uint32_t slots = 0;   // slots handed to the parser
        uint32_t steps = 0;   // steps up to and including the final one
        uint32_t ops = 0;     // non-NOP halves, i.e. compiled records
        uint32_t paired = 0;  // steps issuing a MAC and an ILU together
        // Register files the read-before-write analysis could NOT prove dead,
        // i.e. what the compiled path still has to zero per vertex out of the
        // interpreter's 13 + 12 + 1.
        uint32_t zero_outputs = 13;
        uint32_t zero_temps = 12;
        uint32_t zero_address = 1;
        bool compiled = false;
        bool writes_context = false;
    };
    struct VshProgramCensus {
        static constexpr size_t kSlots = 64;
        std::array<VshProgramCensusRow, kSlots> rows{};
        size_t count = 0;
        uint64_t overflow = 0;  // distinct programs that found no free row
    };
    const VshProgramCensus& vsh_program_census() const {
        return vsh_program_census_;
    }
#if XLLE_VSH_CAPTURE
    struct VshCaptureStats {
        uint64_t executions = 0, eligible = 0, excluded = 0, samples = 0, cache_hits = 0;
    };
    void set_vsh_capture(uint64_t interval, uint32_t budget,
                         std::function<void(const vsh_capture::Vertex&)> callback) {
        vsh_capture_interval_ = interval;
        vsh_capture_budget_ = budget;
        vsh_capture_callback_ = std::move(callback);
        vsh_capture_stats_ = {};
    }
    const VshCaptureStats& vsh_capture_stats() const { return vsh_capture_stats_; }
    struct VshBatchCaptureStats {
        uint64_t batches = 0, eligible = 0, selected = 0, written = 0;
    };
    void set_vsh_batch_capture(uint64_t interval, uint32_t budget,
        std::function<bool(const vsh_batch_capture::Batch&)> callback) {
        vsh_batch_capture_interval_ = interval;
        vsh_batch_capture_budget_ = budget;
        vsh_batch_capture_callback_ = std::move(callback);
        vsh_batch_capture_stats_ = {};
    }
    const VshBatchCaptureStats& vsh_batch_capture_stats() const { return vsh_batch_capture_stats_; }
#endif
    // The pre-decoded step form. DEFAULT ON; --no-nv2a-vsh-compile runs the
    // vendored nv2a_vsh_emu_execute on the same binary, which is the arm the
    // decision A/B is measured against.
    void set_vsh_compile_enabled(bool on) {
        if (vsh_compile_enabled_ == on) return;
        vsh_compile_enabled_ = on;
        invalidate_vsh_program_cache();
    }
    bool vsh_compile_enabled() const { return vsh_compile_enabled_; }
    void set_vsh_dot_prefix(bool on) {
        if (vsh_dot_prefix_enabled_ == on) return;
        vsh_dot_prefix_enabled_ = on;
        invalidate_vsh_program_cache();
    }
    bool vsh_dot_prefix_enabled() const { return vsh_dot_prefix_enabled_; }
    const VshDotPrefixStats& vsh_dot_prefix_stats() const {
        return vsh_dot_prefix_stats_;
    }
    void set_generic_vertex_scratch(bool on) {
        if (generic_vertex_scratch_enabled_ == on) return;
        generic_vertex_scratch_enabled_ = on;
        ++generic_vertex_scratch_epoch_;
        if (!on) {
            generic_vector_scratch_release(generic_packet_vertices_scratch_);
            generic_vector_scratch_release(generic_primary_expanded_vertices_scratch_);
        }
    }
    bool generic_vertex_scratch_enabled() const {
        return generic_vertex_scratch_enabled_;
    }
    GenericVertexScratchStats generic_vertex_scratch_stats() const {
        GenericVertexScratchStats out = generic_vertex_scratch_stats_;
        out.retained_bytes =
            generic_vector_scratch_retained_bytes(generic_packet_vertices_scratch_) +
            generic_vector_scratch_retained_bytes(
                generic_primary_expanded_vertices_scratch_);
        return out;
    }
#if XLLE_VSH_DRAW_OVERLAP_CENSUS
    void set_vsh_draw_overlap_census(bool on) {
        if (vsh_draw_overlap_enabled_ == on) return;
        if (vsh_draw_overlap_enabled_) draw_order_fence();
        vsh_draw_overlap_enabled_ = on;
        vsh_draw_overlap_census_.enabled = on;
    }
    bool vsh_draw_overlap_census_enabled() const {
        return vsh_draw_overlap_enabled_;
    }
    const VshDrawOverlapCensus& vsh_draw_overlap_census() const {
        return vsh_draw_overlap_census_;
    }
#endif
    // Ceiling probe, KNOWINGLY INCORRECT when armed. Default 0; no --no-
    // spelling. See nv2a_vsh_compiled.hpp.
    void set_vsh_unsafe_probe(uint32_t mode) { vsh_unsafe_probe_ = mode; }
    uint32_t vsh_unsafe_probe() const { return vsh_unsafe_probe_; }
    // ---- the differential verifier (beads-xbox.1.15.11) -------------------
    // DIAGNOSTIC, default 0, very slow when armed. For the first N vertices
    // that run a compiled program it runs the VENDORED interpreter on a scratch
    // state from the SAME inputs and compares, BIT FOR BIT, the seven output
    // registers the device reads back -- position, diffuse, specular and the
    // four texture coordinates. That is the whole observable contract of the
    // vertex program, so it covers the operand fetch, the operations, the
    // destination stores AND the read-before-write zero plan together. It is
    // how the exactness claim is evidence rather than argument.
    void set_vsh_verify(uint32_t vertices) { vsh_verify_budget_ = vertices; }
    uint32_t vsh_verify_budget() const { return vsh_verify_budget_; }
    uint64_t vsh_verify_checked() const { return vsh_verify_checked_; }
    uint64_t vsh_verify_mismatched() const { return vsh_verify_mismatched_; }
    void set_vertex_program_cache_enabled(bool on) {
        vertex_program_cache_enabled_ = on;
        if (!on) invalidate_vsh_program_cache();
    }
    bool vertex_program_cache_enabled() const {
        return vertex_program_cache_enabled_;
    }
    // The two exactness-neutral hoists in the per-vertex loop: iterate only
    // the attribute slots whose count is non-zero, and skip the dead 256-byte
    // zero-fill of the input register file. Both are pure removals of work
    // whose result is never read; the flag exists ONLY so the pair can be
    // A/B'd against the previous behaviour on the same binary.
    void set_vertex_fast_fetch_enabled(bool on) { vertex_fast_fetch_ = on; }
    bool vertex_fast_fetch_enabled() const { return vertex_fast_fetch_; }
    void set_vertex_census_enabled(bool on) { vertex_census_enabled_ = on; }
    bool vertex_census_enabled() const { return vertex_census_enabled_; }
#if XLLE_VERTEX_REUSE_DIAGNOSTICS
    struct VertexReuseShadowCensus {
        uint64_t ineligible_nonprogrammable = 0, ineligible_context = 0;
        uint64_t ineligible_probe = 0, ineligible_verify = 0, ineligible_capture = 0;
        uint64_t ineligible_census = 0, batch_cache_bypassed = 0;
        uint64_t state_ticks = 0, lookup_ticks = 0, timed_lookups = 0;
        uint64_t discarded_deltas = 0;
    };
    void set_vertex_reuse_shadow(bool enabled, bool timing = false) {
        if (enabled && !vertex_reuse_shadow_) {
            vertex_reuse_shadow_ = std::make_unique<Nv2aVertexReuseShadow>();
            vertex_reuse_shadow_census_ = {};
        }
        if (!enabled) vertex_reuse_shadow_.reset();
        vertex_reuse_shadow_timing_ = enabled && timing;
    }
    const Nv2aVertexReuseShadow* vertex_reuse_shadow() const {
        return vertex_reuse_shadow_.get();
    }
    bool vertex_reuse_shadow_timing() const { return vertex_reuse_shadow_timing_; }
    const VertexReuseShadowCensus& vertex_reuse_shadow_census() const {
        return vertex_reuse_shadow_census_;
    }
#endif

    // ---- phase 5 item 2: what a residency QUERY actually costs -------------
    // beads-xbox.1.15.10 phase 4 section 5 inferred ~0.9 s for the 148 317
    // no-op residency queries from the difference between a ceiling probe
    // (-1.3 s) and a byte-rate model of the 230 queries that transfer
    // (0.39 s). That is an inference across two instruments and it is the kind
    // of number this bead has already had to retract twice, so it is measured
    // directly instead: every entry to resolve_renderer_color_overlap /
    // resolve_renderer_depth_overlap is timed and credited to `noop` or
    // `moved` by whether it wrote any bytes back into guest RAM. Always on:
    // two clock reads per call, 314 336 calls per canonical run, ~3 ms.
    struct ResolveQueryCensus {
        uint64_t color_noop_n = 0;
        uint64_t color_noop_ticks = 0;
        uint64_t color_moved_n = 0;
        uint64_t color_moved_ticks = 0;
        uint64_t depth_noop_n = 0;
        uint64_t depth_noop_ticks = 0;
        uint64_t depth_moved_n = 0;
        uint64_t depth_moved_ticks = 0;
        // Inside a TRANSFERRING resolve, which is where the seconds actually
        // are (2.2-3.8 ms each, measured): the backend call -- glReadPixels
        // plus the flip/deswizzle copy, and the pipeline stall in front of
        // them -- against the guest-RAM writeback that follows it. Counted
        // only on calls that moved bytes, so it costs nothing on the 312 k
        // no-ops.
        uint64_t color_backend_ticks = 0;
        uint64_t color_memwrite_ticks = 0;
        uint64_t color_memwrite_bytes = 0;
        uint64_t depth_backend_ticks = 0;
        uint64_t depth_memwrite_ticks = 0;
        uint64_t depth_memwrite_bytes = 0;
        // The witnessed texture-snapshot call site specifically -- the 148 547
        // the ceiling probe skipped.
        uint64_t texsrc_noop_n = 0;
        uint64_t texsrc_noop_ticks = 0;
        uint64_t texsrc_moved_n = 0;
        uint64_t texsrc_moved_ticks = 0;
    };
    const ResolveQueryCensus& resolve_query_census() const {
        return resolve_query_census_;
    }
    // ==================== generic-combiner SHADER census =====================
    // beads-xbox.1.15.10 phase 6. Phase 5 measured that 1 307 of the 1 954
    // transferring colour resolves are generic-combiner paths -- 704 from the
    // software fallbacks and 602 from the generic texture reads -- and that a
    // resolve costs 2.2-2.9 ms, so those alone are 3.8 s of a 57.6 s run. What
    // it could NOT say is WHICH generic states cause them, because the only
    // per-fingerprint census in this file
    // (GenericCombinerFingerprintStats) is gated on --nv2a-detailed-telemetry,
    // folds the WHOLE raster block, and keys on state a generated shader does
    // not depend on (surface addresses, offsets, clip words), so it splits one
    // shader across many rows.
    //
    // This census keys on EXACTLY the words the generated fragment program is a
    // function of, and it is ALWAYS ON: one FNV fold over ~50 words plus a
    // linear scan of at most kGenericShaderCensusSlots rows, once per generic
    // BATCH (19 702 per canonical 12 G run), which is ~1 ms.
    //
    // A row's `color_resolves` is not modelled: it counts the batches whose own
    // software-fallback resolve actually moved bytes back into guest RAM, taken
    // from the same seam_resolve_transfers_ counter the `moved` column of
    // [host] gpu-resolve-cost is taken from, so the two cannot disagree.
    static constexpr size_t kGenericShaderCensusSlots = 128;
    struct GenericShaderCensusRow {
        uint64_t fingerprint = 0;
        uint64_t batches = 0;             // batches carrying this shader state
        uint64_t software_batches = 0;    // of those, the ones GL declined
        uint64_t accelerated_batches = 0; // of those, the ones GL took
        uint64_t gpu_batches = 0;         // of those, the ones the GENERATED
                                          // shader executor took (phase 6)
        uint64_t triangles = 0;           // software-rasterized triangles
        uint64_t bbox_pixels = 0;         // their scanline-box area
        uint64_t vertices = 0;
        uint64_t color_resolves = 0;      // fallback colour resolves that MOVED
        uint64_t depth_resolves = 0;      // fallback zeta resolves that MOVED
        uint64_t texture_resolves = 0;    // generic texture reads that MOVED
        // WHICH destination plane this state's resolves belong to. Without it
        // the census cannot answer the question phase 5's plane table answers
        // for the witnessed families -- "is this the same plane, so does
        // removing only one side remove nothing" -- because the plane table
        // counts witnessed draws only and a generic software draw is invisible
        // to it. Four slots, first-come; a fifth distinct plane is counted in
        // dest_overflow rather than evicting one that is already reported.
        std::array<uint32_t, 4> dest_address{};
        std::array<uint64_t, 4> dest_batches{};
        std::array<uint64_t, 4> dest_resolves{};
        uint64_t dest_overflow = 0;
        // The state the fingerprint is over, kept verbatim so the work order
        // can be read out of one run instead of re-derived from a second.
        uint32_t combiner_control = 0;
        uint32_t shader_stage_program = 0;
        uint32_t other_stage_input = 0;
        uint32_t enables = 0;
        uint32_t alpha_func = 0, alpha_ref = 0;
        uint32_t blend_src = 0, blend_dst = 0, blend_equation = 0;
        uint32_t depth_func = 0, depth_mask = 0, color_mask = 0;
        uint32_t surface_format = 0;
        uint32_t texture_enable_mask = 0;
        uint32_t cull_face = 0;
        std::array<uint32_t, 8> color_icw{};
        std::array<uint32_t, 8> alpha_icw{};
        std::array<uint32_t, 8> color_ocw{};
        std::array<uint32_t, 8> alpha_ocw{};
        std::array<uint32_t, 8> factor0{};
        std::array<uint32_t, 8> factor1{};
        uint32_t specular_fog0 = 0, specular_fog1 = 0;
        uint32_t final_factor0 = 0, final_factor1 = 0;
        std::array<uint32_t, 4> tex_format{};
        std::array<uint32_t, 4> tex_address{};
        std::array<uint32_t, 4> tex_filter{};
        std::array<uint32_t, 4> tex_width{};
        std::array<uint32_t, 4> tex_height{};
        uint32_t primitive_modes = 0;     // bitmask over primitive_mode
        uint32_t reject_reason = 0xFFFFFFFFu;  // last resident-reject reason
        uint8_t stage_count = 0;
        uint8_t uses_depth = 0;
        uint8_t texture_referenced = 0;   // bit per stage
        uint8_t texture_cubemap = 0;
        uint8_t texture_linear = 0;
        uint8_t texture_alpha8 = 0;
        uint8_t swizzled_destination = 0;
    };
    struct GenericShaderCensus {
        std::array<GenericShaderCensusRow, kGenericShaderCensusSlots> rows{};
        size_t count = 0;
        // Batches whose state did not fit the table. Counted, never evicted, so
        // a row the table already knows about cannot be under-reported.
        uint64_t overflow_batches = 0;
        uint64_t overflow_color_resolves = 0;
    };
    const GenericShaderCensus& generic_shader_census() const {
        return generic_shader_census_;
    }
    // ============ stage (f3): the generated-shader executor =================
    // --nv2a-gpu-generic-shader. Off restores the pre-phase-6 behaviour on the
    // SAME binary (every generic batch the hand-written fixtures decline goes
    // to the software rasterizer and resolves its destination), so the decision
    // A/B is a one-flag change and not a rebuild.
    void set_gpu_generic_shader(bool on) { gpu_generic_shader_ = on; }
    bool gpu_generic_shader() const { return gpu_generic_shader_; }
    // --nv2a-gpu-generic-shader-mask: bit k enables the k-th DISTINCT generated
    // program in first-attempt order. It is the bisection tool the hand-written
    // profiles have as --nv2a-gpu-profile-mask, and it is what makes "which
    // generated program moved a pixel" a measurement instead of a guess. The
    // order is deterministic because the replay is.
    // --no-nv2a-gpu-generic-shader-fp64 switches the generated programs'
    // perspective interpolation to single precision on the SAME binary, which
    // is how the precision question was measured rather than assumed.
    void set_gpu_generic_shader_fp64(bool on) { gpu_generic_shader_fp64_ = on; }
    bool gpu_generic_shader_fp64() const { return gpu_generic_shader_fp64_; }
    // --nv2a-gpu-generic-shader-wide N: a triangle with a vertex more than N
    // guest pixels from the origin is drawn as its own scanline bounding box
    // instead of as itself, so the fixed-function rasterizer never has to
    // place it and can never under-cover it. 0 forces every triangle onto that
    // path, which is the exactness control.
    void set_gpu_generic_shader_wide_pixels(uint32_t pixels) {
        gpu_generic_shader_wide_pixels_ = pixels;
    }
    uint32_t gpu_generic_shader_wide_pixels() const {
        return gpu_generic_shader_wide_pixels_;
    }
    // --nv2a-gpu-generic-shader-verify N: for the first N submitted batches,
    // run BOTH paths on the same inputs and report the difference per pixel.
    // DIAGNOSTIC, VERY SLOW, and it changes residency (it invalidates the
    // plane so the GPU re-uploads the pre-draw image). It is the instrument
    // that answers "which pixel and by how much", which no amount of reading
    // the generated source can.
    // --no-nv2a-gpu-generic-shader-image-blend restores GL fixed-function
    // blending on the SAME binary. The difference between the two is measured,
    // not assumed: see Nv2aCombinerShaderState::blend.
    void set_gpu_generic_shader_image_blend(bool on) {
        gpu_generic_shader_image_blend_ = on;
    }
    bool gpu_generic_shader_image_blend() const {
        return gpu_generic_shader_image_blend_;
    }
    // --nv2a-gpu-generic-shader-depth: the generated executor attaches the
    // resident ZETA plane -- the same slot and the same (address, size) key
    // the witnessed executor uses -- and the ROP performs the guest's depth
    // comparison against the value the fragment program writes to
    // gl_FragDepth. Without it, every batch that reads or writes zeta declines
    // `uses-depth` (512 of them on the canonical 12 B run) and is rasterized
    // in software after resolving BOTH planes.
    //
    // DEFAULT OFF, and the reason is MEASURED rather than cautious. On its own
    // it is a REGRESSION, because plane 0x03628000 still has 3 181 witnessed
    // CPU draws on it and residency breaks at the FIRST CPU consumer: turning
    // the 527 generic depth batches from CPU consumers into GPU producers
    // makes the plane dirty again after each one, so the next witnessed CPU
    // draw breaks it again. Canonical 3 G window, one binary, one session:
    // transferring colour resolves 566 -> 761 and zeta 180 -> 375. It is
    // EXACT (10 of 10 checkpoints byte-identical to 5.0 G with the reroute
    // below off) and it is a PREREQUISITE for that reroute; it is not a win by
    // itself and must not be turned on without it.
    void set_gpu_generic_shader_depth(bool on) {
        gpu_generic_shader_depth_ = on;
    }
    bool gpu_generic_shader_depth() const { return gpu_generic_shader_depth_; }
    // --nv2a-gpu-generic-shader-witnessed MASK. Bit k sends the k-th of the
    // EIGHT witnessed families that are still CPU-rasterized on plane
    // 0x03628000 down the strict generic fallback instead of its hand-derived
    // specialization, so the GENERATED executor can take it. The bit order is
    // the one gpu_res_profile_name prints and is fixed by
    // generic_shader_witnessed_bit below:
    //   0 pitch_multi_texture   1 pitch_three_texture  2 pitch_tex5_projected
    //   3 pitch_tex5_diffuse    4 pitch_single_texture 5 tex7_fan
    //   6 texf_projected_fan    7 pitch_additive
    //
    // WHY IT IS ALL-OR-NOTHING, measured rather than argued. A plane's
    // residency is broken by its FIRST CPU consumer, so moving SOME of the
    // consumers to the GPU does not remove a single resolve -- it ADDS them,
    // because each GPU batch re-establishes residency that the next CPU
    // consumer then breaks again. The mask exists for BISECTION, not for
    // partial deployment.
    //
    // DEFAULT 0, AND HERE IS THE MEASUREMENT THAT PUT IT THERE. With all eight
    // rerouted the plane census reaches the goal exactly -- 0x03628000 and
    // 0x033d0000 both fall to `cpu_draws=0`, transferring zeta resolves
    // 304 -> 0, colour 1 356 -> 1 244, guest-memory surface uploads 391 -> 4
    // (915.4 MB -> 8.3 MB), `uses-depth` declines 512 -> 0, and the 12 G wall
    // 54.542 -> 52.374 s on one leg -- but the IMAGE MOVES, and not by an LSB.
    // Single-bit legs to 5.1 G against the shipping defaults, all ten
    // checkpoints compared with tools/compare_ppm_series.py:
    //   bit 0 pitch_multi_texture  (120 draws)        0 px   EXACT
    //   bit 1 pitch_three_texture  (2 304)            0 px   EXACT
    //   bit 6 texf_projected_fan   (51)               0 px   EXACT
    //   bit 2 pitch_tex5_projected (126)        286 151 px, max delta 255
    //   bit 3 pitch_tex5_diffuse   (42)          20 875 px, max delta 122
    //   bit 4 pitch_single_texture (121)         18 883 px, max delta 202
    //   bit 5 tex7_fan             (304)         14 560 px, max delta  98
    //   bit 7 pitch_additive       (113)        204 691 px, max delta 116
    // This table records the phase-7 result. The 2026-09-06 independent PROJECT2D
    // admission fix now executes bits 2 and 5; full-target CPU comparisons pass.
    // Their GPU replay still has a one-LSB checkpoint difference, and bits 3/4/7
    // remain under investigation. Keep the reroute opt-in until validated/gated.
    // The phase-7 CAUSE was in the GENERIC EVALUATOR, not in the GPU executor: the same
    // bit-2 leg with the generated executor masked off
    // (--nv2a-gpu-generic-shader-mask 0, so the reroute lands in SOFTWARE)
    // reproduces the SAME 286 151 pixels. For bits 2 and 5 the evaluator does
    // did not implement the batch's texture-shader program and skipped it --
    // `[2bl] NV2A generic-combiner skip census:
    // unsupported-texture-shader-program=126` and `=304`, exactly those draw
    // counts -- so the draws vanish. Bits 3, 4 and 7 are skipped by nothing
    // and still differ, which is a numeric divergence between the generic
    // decode and the pinned specialization. Three of eight is worth nothing
    // under the first-consumer rule, so the whole lever waits on the five.
    void set_gpu_generic_shader_witnessed(uint32_t mask) {
        gpu_generic_shader_witnessed_ = mask;
    }
    uint32_t gpu_generic_shader_witnessed() const {
        return gpu_generic_shader_witnessed_;
    }
    uint64_t generic_shader_witnessed_reroutes() const {
        return generic_shader_witnessed_reroutes_;
    }
    void set_gpu_generic_shader_verify(uint32_t batches) {
        gpu_generic_shader_verify_ = batches;
    }
    void set_gpu_generic_shader_nodraw(bool on) {
        if (renderer_) renderer_->set_generated_shader_nodraw(on);
    }
    // --no-nv2a-gpu-generic-shader-bgra-upload restores the pre-phase-7
    // per-pixel byte shuffle on the SAME binary. See
    // Nv2aRenderer::set_generated_shader_bgra_upload.
    void set_gpu_generic_shader_bgra_upload(bool on) {
        gpu_generic_shader_bgra_upload_ = on;
        if (renderer_) renderer_->set_generated_shader_bgra_upload(on);
    }
    bool gpu_generic_shader_bgra_upload() const {
        return gpu_generic_shader_bgra_upload_;
    }
    void set_gpu_generic_texture_cache(bool on) { gpu_generic_texture_cache_ = on; }
    bool gpu_generic_texture_cache() const { return gpu_generic_texture_cache_; }
    void set_dxt_decode_cache(bool on) { dxt_decode_cache_enabled_ = on; }
    bool dxt_decode_cache_enabled() const { return dxt_decode_cache_enabled_; }
    const Nv2aTextureConversionCache& dxt_decode_cache() const {
        return dxt_decode_cache_;
    }
    void set_gpu_generic_texture_trace(bool on) { gpu_generic_texture_trace_ = on; }
    const Nv2aTextureConversionCache& generic_texture_conversion_cache() const {
        return generic_texture_conversion_cache_;
    }
    // ---- PFIFO run batching (beads-xbox.1.15.1 stages (c) and (d)) ----------
    //
    // `--no-nv2a-pfifo-run-batching` restores the strict per-word pusher on the
    // SAME binary: one DMA translate, one uc_mem_read, one CACHE1 deque round
    // trip and one drain_pull_queue call for every method DATA word. That is the
    // arm-A form of the decision A/B, and it is also the fallback the fast path
    // takes whenever any of its preconditions fails, so the per-word code is
    // never dead.
    void set_pfifo_run_batching(bool on) { pfifo_run_batching_ = on; }
    bool pfifo_run_batching() const { return pfifo_run_batching_; }
    // `--no-nv2a-pfifo-run-bulk` keeps the run path but sends every word through
    // dispatch_method instead of the bulk vertex-index / inline-array append, so
    // the two stages can be attributed separately.
    void set_pfifo_run_bulk(bool on) { pfifo_run_bulk_ = on; }
    bool pfifo_run_bulk() const { return pfifo_run_bulk_; }
    // `--no-nv2a-pfifo-direct-fetch` restores the per-word mem_read_ (uc_mem_read)
    // hop for every pushbuffer word. It is a SEPARATE stage from run batching and
    // it helps the per-word path too, so arm A of the decision A/B must pass BOTH
    // negatives to reproduce the pre-change pusher; measured 2026-09-05, turning
    // this off alone moved a 3-leg per-word arm 52.282 -> 53.479 s.
    void set_pfifo_direct_fetch(bool on) { pfifo_direct_fetch_ = on; }
    bool pfifo_direct_fetch() const { return pfifo_direct_fetch_; }
    uint64_t pfifo_runs_batched() const { return pfifo_runs_batched_; }
    uint64_t pfifo_words_batched() const { return pfifo_words_batched_; }
    uint64_t pfifo_runs_bulk() const { return pfifo_runs_bulk_; }
    uint64_t pfifo_words_bulk() const { return pfifo_words_bulk_; }
    uint64_t pfifo_words_per_word() const { return pfifo_words_per_word_; }
    uint64_t pfifo_run_declines() const { return pfifo_run_declines_; }
    // Why a data word fell back to the per-word pusher. One counter per
    // precondition, so the batched fraction is attributable instead of argued.
    uint64_t pfifo_decline_queue() const { return pfifo_decline_queue_; }
    uint64_t pfifo_decline_stall() const { return pfifo_decline_stall_; }
    uint64_t pfifo_decline_dma() const { return pfifo_decline_dma_; }
    uint64_t pfifo_decline_direct() const { return pfifo_decline_direct_; }
    void set_gpu_generic_shader_mask(uint64_t mask) {
        gpu_generic_shader_mask_ = mask;
    }
    uint64_t gpu_generic_shader_mask() const {
        return gpu_generic_shader_mask_;
    }
    // The fingerprint each mask bit selects, in the order the bits are
    // assigned, so a decline can be named in the log rather than counted.
    size_t generic_shader_program_count() const {
        return generic_shader_seen_count_;
    }
    uint64_t generic_shader_program_fingerprint(size_t index) const {
        return index < generic_shader_seen_count_
                   ? generic_shader_seen_fp_[index]
                   : 0;
    }
    uint64_t generic_shader_program_batches(size_t index) const {
        return index < generic_shader_seen_count_
                   ? generic_shader_seen_batches_[index]
                   : 0;
    }
    // Why a batch the executor was offered did NOT take the GPU. The first
    // block is device-side; kGeneratorFirst + Nv2aCombinerShaderReject is the
    // generator's own answer, so one census covers both halves and no decline
    // is unattributed.
    enum class GenericShaderDeclineReason : uint32_t {
        kNone = 0,
        kSwizzledDestination,
        kUsesDepth,
        kNoopBatch,
        kVertexCount,
        kTextureUnsupported,
        kBackendDeclined,
        kTriangleUnsupported,
        kRasterFailed,
        kMaskedOff,
        kGeneratorFirst,
        kCount = kGeneratorFirst +
                 static_cast<uint32_t>(Nv2aCombinerShaderReject::kCount),
    };
    static size_t generic_shader_decline_count() {
        return static_cast<size_t>(GenericShaderDeclineReason::kCount);
    }
    static const char* generic_shader_decline_name(size_t index) {
        switch (static_cast<GenericShaderDeclineReason>(index)) {
            case GenericShaderDeclineReason::kNone: return "none";
            case GenericShaderDeclineReason::kSwizzledDestination:
                return "swizzled-destination";
            case GenericShaderDeclineReason::kUsesDepth: return "uses-depth";
            case GenericShaderDeclineReason::kNoopBatch: return "noop-batch";
            case GenericShaderDeclineReason::kVertexCount: return "vertex-count";
            case GenericShaderDeclineReason::kTextureUnsupported:
                return "texture-unsupported";
            case GenericShaderDeclineReason::kBackendDeclined:
                return "backend-declined";
            case GenericShaderDeclineReason::kTriangleUnsupported:
                return "triangle-unsupported";
            case GenericShaderDeclineReason::kRasterFailed:
                return "raster-failed";
            case GenericShaderDeclineReason::kMaskedOff: return "masked-off";
            default: break;
        }
        const size_t first =
            static_cast<size_t>(GenericShaderDeclineReason::kGeneratorFirst);
        if (index >= first &&
            index < first +
                        static_cast<size_t>(Nv2aCombinerShaderReject::kCount)) {
            return nv2a_combiner_shader_reject_name(
                static_cast<Nv2aCombinerShaderReject>(index - first));
        }
        return "unknown";
    }
    struct GeneratedShaderCensus {
        uint64_t attempts = 0;
        uint64_t submits = 0;
        uint64_t empty_batches = 0;
        uint64_t triangles = 0;
        uint64_t color_upload_bytes = 0;
        uint64_t depth_upload_bytes = 0;
        uint64_t texture_upload_bytes = 0;
        std::array<uint64_t, 32> declines{};
    };
    // The BACKEND's own view of the same executor: how many distinct programs
    // it compiled, how many texture uploads the hash skipped. Taken from the
    // renderer so the device census and the backend census can be compared
    // rather than one standing in for the other.
    Nv2aRenderer::GeneratedShaderStats generated_shader_backend_stats() const {
        return renderer_ ? renderer_->generated_shader_stats()
                         : Nv2aRenderer::GeneratedShaderStats{};
    }
    Nv2aRenderer::GeneratedSubmitCpuStats generated_submit_cpu_stats() const {
        return renderer_ ? renderer_->generated_submit_cpu_stats()
                         : Nv2aRenderer::GeneratedSubmitCpuStats{};
    }
    GeneratedShaderCensus generated_shader_census() const {
        GeneratedShaderCensus out;
        out.attempts = generic_shader_attempts_;
        out.submits = generic_shader_submits_;
        out.empty_batches = generic_shader_empty_batches_;
        out.triangles = generic_shader_triangles_;
        out.color_upload_bytes = generic_shader_color_upload_bytes_;
        out.depth_upload_bytes = generic_shader_depth_upload_bytes_;
        out.texture_upload_bytes = generic_shader_texture_upload_bytes_;
        out.declines = generic_shader_declines_;
        return out;
    }
    // --nv2a-gpu-profiles: execute the two dominant witnessed profiles on the
    // GPU with the destination surfaces resident. Off means the residency model
    // above still runs as a pure simulation, so both arms of an A/B carry the
    // same partition and it can be compared directly.
    void set_gpu_profiles_enabled(bool enabled) {
        gpu_profiles_enabled_ = enabled;
    }
    bool gpu_profiles_enabled() const { return gpu_profiles_enabled_; }
    uint64_t gpu_profile_jobs() const { return gpu_profile_jobs_; }
    uint64_t gpu_profile_triangles() const { return gpu_profile_triangles_; }
    uint64_t gpu_profile_draw_calls() const { return gpu_profile_draw_calls_; }
    uint64_t gpu_profile_declined() const { return gpu_profile_declined_; }
    uint64_t gpu_profile_uploads() const { return gpu_profile_uploads_; }
    uint64_t gpu_profile_color_uploads() const {
        return gpu_profile_color_uploads_;
    }
    uint64_t gpu_profile_depth_uploads() const {
        return gpu_profile_depth_uploads_;
    }
    uint64_t gpu_profile_empty_packets() const {
        return gpu_profile_empty_packets_;
    }
    uint64_t gpu_clear_accepted() const { return gpu_clear_accepted_; }
    uint64_t gpu_clear_decline_mask() const { return gpu_clear_decline_mask_; }
    uint64_t gpu_clear_decline_geometry() const {
        return gpu_clear_decline_geometry_;
    }
    uint64_t gpu_clear_decline_planes() const {
        return gpu_clear_decline_planes_;
    }
    uint64_t gpu_clear_decline_resident() const {
        return gpu_clear_decline_resident_;
    }
    uint64_t gpu_clear_bytes() const { return gpu_clear_bytes_; }
    uint64_t gpu_clear_kept_clean() const { return gpu_clear_kept_clean_; }
    // --nv2a-gpu-clear (default ON with the executor). Off restores the
    // software clear + invalidate for every CLEAR_SURFACE, so the two can be
    // A/B'd on one binary.
    void set_gpu_clear_enabled(bool on) { gpu_clear_enabled_ = on; }
    bool gpu_clear_enabled() const { return gpu_clear_enabled_; }
    void set_gpu_skip_empty_packets(bool on) { gpu_skip_empty_packets_ = on; }
    bool gpu_skip_empty_packets() const { return gpu_skip_empty_packets_; }
    Nv2aWitnessedUploadCensus witnessed_upload_census() const {
        return renderer_ ? renderer_->witnessed_upload_census()
                         : Nv2aWitnessedUploadCensus{};
    }
    uint64_t gpu_profile_upload_bytes() const { return gpu_profile_upload_bytes_; }
    uint64_t gpu_profile_texture_upload_bytes() const {
        return gpu_profile_texture_upload_bytes_;
    }
    // Largest 1/16-pixel screen coordinate magnitude seen on a job that was
    // otherwise GPU-eligible but out of the exact-double range. Always on, so
    // the limit is chosen from the measured distribution rather than guessed.
    uint64_t gpu_profile_max_fixed() const { return gpu_profile_max_fixed_; }
    // Which surfaces each GPU profile targets. Two profiles sharing a zeta
    // range is the one interaction that a per-profile validation cannot see,
    // so it is recorded rather than reasoned about.
    static constexpr size_t kGpuProfileSurfaceSlots = 3;
    uint32_t gpu_profile_surface(size_t profile, bool depth) const {
        if (profile >= kGpuProfileSurfaceSlots) return 0;
        return depth ? gpu_profile_depth_addr_[profile]
                     : gpu_profile_color_addr_[profile];
    }
    uint32_t gpu_profile_surface_bytes(size_t profile) const {
        return profile >= kGpuProfileSurfaceSlots
                   ? 0
                   : gpu_profile_surface_bytes_[profile];
    }
    // Bound on the 1/16-pixel screen coordinate a GPU packet may carry.
    //
    // EXACT (the default): 2^24. The coordinates themselves travel as doubles
    // and are exact to 2^53, but the shader's edge PRODUCTS are (b-a)*(p-a),
    // so they stay exact only while the coordinates stay under 2^24; beyond
    // that the double rounds and the software path's int64 wraps, and the two
    // approximate the same degenerate geometry differently. Measured on this
    // workload: 0.58 percent of eligible jobs exceed it, magnitudes up to
    // 21 846 069 248 (2^34.3).
    //
    // RELAXED: 2^50, i.e. everything llround can still express. Faster --
    // those 0.58 percent of jobs are what shatters the residency runs, taking
    // the surface round trips from 235 to 925 per 2 G instructions -- but it
    // moved one of the 28 checkpoint frames (4.0 G, the boot animation) by up
    // to 9 LSB over 84 percent of its pixels. --nv2a-gpu-relaxed-coords opts
    // in; the default keeps those jobs on the CPU executor.
    //
    // PHASE 2 (beads-xbox.1.15.10) SUPERSEDES THE PARAGRAPH ABOVE. Both of the
    // mechanisms that made the relaxed bound move a checkpoint were in the
    // SHADER, not in the bound:
    //   1. the edge products were carried in double and rounded above 2^53
    //      while the software path's int64 wrapped -- now the shader runs the
    //      same int64 expression and wraps identically
    //      (GL_ARB_gpu_shader_int64);
    //   2. a triangle with a 2^34 coordinate cannot be placed by the
    //      fixed-function rasterizer in float clip space, so the driver
    //      UNDER-covered and the in-shader test, which can only remove
    //      fragments, could not repair it -- such a triangle is now drawn as
    //      its own scanline bounding box instead of as itself.
    // The default is therefore 2^50 and --nv2a-gpu-exact-coords restores 2^24
    // as a bisection tool. The census that forced this: the 1 126 jobs the
    // 2^24 bound rejected caused 806 of the 1 110 witnessed-CPU-draw residency
    // breaks per 12 G instructions.
    static constexpr int64_t kGpuFixedCoordinateLimitExact = 1ll << 24;
    static constexpr int64_t kGpuFixedCoordinateLimitRelaxed = 1ll << 50;
    // Kept so existing call sites and scripts keep working; the relaxed bound
    // is now the default, so this is a no-op that records the intent.
    void set_gpu_relaxed_coords(bool relaxed) {
        if (relaxed) gpu_exact_coords_ = false;
    }
    void set_gpu_exact_coords(bool exact) { gpu_exact_coords_ = exact; }
    bool gpu_exact_coords() const { return gpu_exact_coords_; }
    // Bit 0 = kSwizzledRadialColor, bit 1 = kPitchTexturedColor. Default 3.
    // This exists to make a behavioral difference ATTRIBUTABLE: with two
    // profiles sharing one destination surface, a checkpoint that moves cannot
    // be blamed on either without turning one off, and turning one off with a
    // rebuild would confound the change with the build.
    void set_gpu_profile_mask(uint32_t mask) { gpu_profile_mask_ = mask; }
    void set_gpu_unsafe_skip_texture_resolve(bool on) {
        gpu_unsafe_skip_texture_resolve_ = on;
    }
    bool gpu_unsafe_skip_texture_resolve() const {
        return gpu_unsafe_skip_texture_resolve_;
    }
    uint64_t witnessed_texture_source_resolves() const {
        return witnessed_texture_source_resolves_;
    }
    uint64_t witnessed_texture_source_skipped() const {
        return witnessed_texture_source_skipped_;
    }
    void set_gpu_vblank_resolve(bool on) { gpu_vblank_resolve_ = on; }
    uint32_t gpu_profile_mask() const { return gpu_profile_mask_; }
    // beads-xbox.1.15.10 step 1 (overlap): kick the pending GPU batch at the
    // frame boundary as well as at the size bound, so a partial batch left at
    // the end of a residency run starts executing immediately instead of
    // waiting for whatever consumer eventually needs it.
    void set_gpu_vblank_kick(bool on) { gpu_vblank_kick_ = on; }
    bool gpu_vblank_kick() const { return gpu_vblank_kick_; }
    Nv2aRenderer::WitnessedFlushStats witnessed_flush_stats() const {
        return renderer_ ? renderer_->witnessed_flush_stats()
                         : Nv2aRenderer::WitnessedFlushStats{};
    }
    bool gpu_relaxed_coords() const { return gpu_relaxed_coords_; }

    // =================== stage (e): the threaded draw consumer ==============
    // beads-xbox.1.15.5, DESIGN-NV2A-SEAM.md section 5(e).
    //
    // 0 = inline: every job runs at the latch point, on the emulation thread,
    // exactly as stage (d) did -- this is the default and it is the faithful
    // path.
    //
    // 1 = stage (e): one worker thread pops jobs off the ring and calls the SAME
    // execute_witnessed_fragments, strictly in submission order. Exact, and
    // REJECTED on timing (+0.39 percent against a 4.0 percent ceiling): the
    // emulation thread has nothing to overlap with, so latency hiding buys
    // nothing on this workload. Retained as the mechanism, not as a win.
    //
    // N >= 2 = stage (e2), beads-xbox.1.15.9: WORK DIVISION inside one draw.
    // The ring is not used. All N workers execute the SAME job at once, each
    // owning a disjoint set of destination scanlines (row % N == part), with a
    // barrier between jobs -- so draw order is preserved globally, no two
    // workers ever write the same pixel, and the job never outlives the latch
    // point. The emulation thread is worker 0. Order-dependence between draws to
    // one surface (read-modify-write blending in the shared colour body,
    // depth-test-against-stored in twelve of fifteen profiles) is satisfied by
    // construction because only one job is ever in flight.
    //
    // The bound is a REFUSAL, not a clamp: a run asked for a part count it did
    // not get would silently measure something else. Raised 8 -> 32 on
    // 2026-09-05 so the knee sweep can reach past the reference host's physical
    // core count (9800X3D, 8C/16T) into its SMT siblings; beads-xbox.1.15.9
    // measured 0 -> 4 -> 8 without reaching a knee, so 8 was a bound on the
    // experiment rather than a finding. The Amdahl ceiling on a 46-percent
    // raster share is 1.85x at N -> inf.
    static constexpr unsigned kDrawThreadsMax = 32;
    // Ring capacity. M5 (beads-xbox.1.15.6) measured max queue depth 423 over
    // the 12 B canonical run, so 1024 leaves better than 2x headroom; a full
    // ring is not an error, it executes the job inline and counts it.
    static constexpr size_t kDrawQueueSlots = 1024;
    // Slot count is not a memory bound: a job OWNS its texture snapshots, which
    // run to megabytes for the 1024x1024 families, so 1024 slots of the largest
    // job is gigabytes. The ring is therefore bounded by BYTES as well as by
    // slots, and whichever bound is hit first takes the same backpressure path
    // (drain, then execute inline). 64 MiB is ~14x the mean in-flight footprint
    // measured on the canonical run and still an order of magnitude under the
    // 768 MiB host cap the replay harness enforces.
    static constexpr uint64_t kDrawQueueMaxBytes = 64ull * 1024 * 1024;
    uint64_t draw_backpressure_bytes() const { return draw_backpressure_bytes_; }
    uint64_t draw_order_fences() const { return draw_order_fences_; }
    uint64_t draw_renderer_query_fences() const {
        return draw_renderer_query_fences_;
    }
    uint64_t draw_queue_bytes_max() const { return draw_queue_bytes_max_; }
    // Returns false and leaves the device inline if n is out of range.
    bool set_draw_threads(unsigned n);
    unsigned draw_threads() const { return draw_threads_; }
    // Drain everything the worker still owes and fold it. Idempotent, and a
    // no-op when draw_threads_ == 0.
    void draw_queue_drain_all();

    uint64_t draw_jobs_queued() const { return draw_jobs_queued_; }
    uint64_t draw_jobs_inline() const { return draw_jobs_inline_; }
    uint64_t draw_backpressure_inline() const { return draw_backpressure_inline_; }
    uint64_t draw_drains_empty() const { return draw_drains_empty_; }
    uint64_t draw_drains_nonempty() const { return draw_drains_nonempty_; }
    uint64_t draw_depth_sum() const { return draw_depth_sum_; }
    uint64_t draw_depth_samples() const { return draw_depth_samples_; }
    uint64_t draw_depth_max() const { return draw_depth_max_; }
    static constexpr size_t kDrawDepthBuckets = 8;
    uint64_t draw_depth_bucket(size_t i) const {
        return i < kDrawDepthBuckets ? draw_depth_hist_[i] : 0;
    }
    static const char* draw_depth_bucket_name(size_t i);
    uint64_t draw_worker_busy_ticks() const { return draw_worker_busy_ticks_; }
    uint64_t draw_drain_wait_ticks() const { return draw_drain_wait_ticks_; }
    uint64_t draw_elapsed_ticks() const;
    uint64_t draw_worker_failures() const { return draw_worker_failures_; }

    // ---- stage (e2) census (beads-xbox.1.15.9) ----------------------------
    // span_jobs   : jobs executed span-parallel (parts == draw_threads_).
    // span_inline : jobs that ran with parts == 1 while N >= 2 was configured,
    //               i.e. not threadable (no direct pointer / hashes on / debug
    //               callback) or the generic and immediate-quad families.
    // barrier_wait: emulation-thread ticks spent at the per-job barrier AFTER
    //               finishing its own part -- the direct cost of the mechanism.
    // helper_busy : summed ticks the N-1 helper threads spent inside
    //               execute_witnessed_fragments. Divided by span_elapsed_ticks
    //               and by (N-1) this is the helper duty cycle.
    uint64_t draw_span_jobs() const { return draw_span_jobs_; }
    uint64_t draw_span_inline_jobs() const { return draw_span_inline_jobs_; }
    uint64_t draw_span_barrier_wait_ticks() const {
        return draw_span_barrier_wait_ticks_;
    }
    uint64_t draw_span_helper_busy_ticks() const {
        return span_helper_busy_ticks_.load(std::memory_order_relaxed);
    }
    uint64_t draw_span_part0_busy_ticks() const {
        return draw_span_part0_busy_ticks_;
    }
    uint64_t draw_span_fold_ticks() const { return draw_span_fold_ticks_; }

    enum RasterStateWord : size_t {
        kRasterEnables = 0,
        kRasterSurfaceFormat,
        kRasterSurfacePitch,
        kRasterSurfaceColorOffset,
        kRasterSurfaceZetaOffset,
        kRasterSurfaceClipHorizontal,
        kRasterSurfaceClipVertical,
        kRasterWindowClipHorizontal,
        kRasterWindowClipVertical,
        kRasterControl0,
        kRasterAlphaFunc,
        kRasterAlphaRef,
        kRasterBlendSrc,
        kRasterBlendDst,
        kRasterBlendEquation,
        kRasterBlendColor,
        kRasterDepthFunc,
        kRasterColorMask,
        kRasterDepthMask,
        kRasterStencilWriteMask,
        kRasterStencilFunc,
        kRasterStencilRef,
        kRasterStencilFuncMask,
        kRasterStencilOpFail,
        kRasterStencilOpZFail,
        kRasterStencilOpZPass,
        kRasterShadeMode,
        kRasterPolygonOffsetScale,
        kRasterPolygonOffsetBias,
        kRasterFrontPolygonMode,
        kRasterBackPolygonMode,
        kRasterClipMin,
        kRasterClipMax,
        kRasterCullFace,
        kRasterFrontFace,
        kRasterLogicOp,
        kRasterZMinMaxControl,
        kRasterShaderStageProgram,
        kRasterShaderOtherStageInput,
        kRasterCombinerControl,
        kRasterDotRgbMapping,
        kRasterShadowDepthFunc,
        kRasterTextureEnableMask,
        kRasterTextureDmaA,
        kRasterTextureDmaB,
        kRasterTextureOffset0,
        kRasterTexturePalette0 = kRasterTextureOffset0 + 4,
        kRasterTextureBorderColor0 = kRasterTexturePalette0 + 4,
        kRasterTextureFormat0 = kRasterTextureBorderColor0 + 4,
        kRasterTextureAddress0 = kRasterTextureFormat0 + 4,
        kRasterTextureFilter0 = kRasterTextureAddress0 + 4,
        kRasterTextureControl0_0 = kRasterTextureFilter0 + 4,
        kRasterTextureControl1_0 = kRasterTextureControl0_0 + 4,
        kRasterTextureImageRect0 = kRasterTextureControl1_0 + 4,
        kRasterCombinerAlphaIcw0 = kRasterTextureImageRect0 + 4,
        kRasterCombinerColorIcw0 = kRasterCombinerAlphaIcw0 + 8,
        kRasterCombinerAlphaOcw0 = kRasterCombinerColorIcw0 + 8,
        kRasterCombinerColorOcw0 = kRasterCombinerAlphaOcw0 + 8,
        kRasterCombinerFactor0_0 = kRasterCombinerColorOcw0 + 8,
        kRasterCombinerFactor1_0 = kRasterCombinerFactor0_0 + 8,
        kRasterCombinerSpecularFog0 = kRasterCombinerFactor1_0 + 8,
        kRasterCombinerSpecularFog1,
        kRasterCombinerFinalFactor0,
        kRasterCombinerFinalFactor1,
        kRasterProvokingVertex,
        kRasterStateWords,
    };
    uint32_t debug_raster_state_word(RasterStateWord word) const {
        return word < kRasterStateWords ? raster_state_[word] : 0;
    }
    struct RasterStateSample {
        struct ImmediateQuadTransformVariant {
            uint8_t mode = 0;
            uint8_t start = 0;
            uint64_t program_hash = 0;
            uint64_t constants_hash = 0;
        };
        static constexpr size_t kImmediateQuadTransformVariants = 4;
        std::array<uint32_t, kRasterStateWords> words{};
        uint64_t batches = 0;
        uint64_t executed_batches = 0;
        uint64_t vertices = 0;
        uint64_t inline_batches = 0;
        std::array<uint64_t, 11> primitive_batches{};
        // Passive evidence for immediate mode-8 batches. The first batch's values are
        // retained verbatim so future raster work can be based on replay data.
        uint64_t immediate_quad_batches = 0;
        uint32_t immediate_quad_min_vertices = 0;
        uint32_t immediate_quad_max_vertices = 0;
        uint64_t immediate_quad_nonmultiple_of_four = 0;
        bool immediate_quad_first_captured = false;
        std::array<std::array<float, 4>, 4> immediate_quad_decoded_input0{};
        std::array<std::array<float, 4>, 4> immediate_quad_decoded_input3{};
        std::array<std::array<float, 4>, 4> immediate_quad_decoded_input9{};
        std::array<std::array<float, 4>, 4> immediate_quad_opos{};
        std::array<std::array<float, 4>, 4> immediate_quad_od0{};
        std::array<std::array<float, 4>, 4> immediate_quad_ot0{};
        std::array<std::array<float, 4>, 4> immediate_quad_raster_opos{};
        uint8_t immediate_quad_transform_mode = 0;
        uint8_t immediate_quad_transform_start = 0;
        uint64_t immediate_quad_program_hash = 0;
        uint64_t immediate_quad_constants_hash = 0;
        uint64_t immediate_quad_first_transform_batches = 0;
        uint64_t immediate_quad_later_transform_batches = 0;
        uint64_t immediate_quad_transform_variant_overflow_batches = 0;
        uint8_t immediate_quad_transform_variant_count = 0;
        std::array<ImmediateQuadTransformVariant,
                   kImmediateQuadTransformVariants>
            immediate_quad_transform_variants{};
        bool immediate_quad_t0_dma_resolved = false;
        bool immediate_quad_t0_probe_read = false;
        uint32_t immediate_quad_t0_address = 0;
        uint8_t immediate_quad_t0_probe_size = 0;
        std::array<uint8_t, 16> immediate_quad_t0_probe{};
        bool immediate_quad_palette_relevant = false;
        bool immediate_quad_palette_dma_resolved = false;
        bool immediate_quad_palette_probe_read = false;
        uint32_t immediate_quad_palette_address = 0;
        uint8_t immediate_quad_palette_probe_entries = 0;
        std::array<uint32_t, 4> immediate_quad_palette_probe{};
    };
    size_t raster_state_samples(std::vector<RasterStateSample>& out) const {
        out = raster_state_samples_;
        return out.size();
    }
    bool raster_state_sample_at(uint32_t index, RasterStateSample& out) const {
        if (index >= raster_state_samples_.size()) return false;
        out = raster_state_samples_[index];
        return true;
    }
    uint32_t pgraph_surface() const;
    bool waiting_for_flip() const { return waiting_for_flip_; }
    size_t recent_methods(size_t n, std::vector<PushMethod>& out) const {
        return methods_.window(n, out);
    }
    // Opt-in diagnostic: after the first successful color clear to `color_base`, retain the
    // 32 methods ending at the first subsequent Kelvin BEGIN_END(nonzero), then every decoded
    // method through that same channel/subchannel's BEGIN_END(0), capped at 4096 records.
    // This only observes the recorder; it never changes PFIFO/PGRAPH execution.
    void arm_color_clear_trace(uint32_t color_base) {
        clear_trace_target_ = color_base;
        clear_trace_armed_ = true;
        clear_trace_triggered_ = false;
        clear_trace_begin_triggered_ = false;
        clear_trace_complete_ = false;
        clear_trace_truncated_ = false;
        clear_trace_trigger_seq_ = 0;
        clear_trace_begin_seq_ = 0;
        clear_trace_pre_count_ = 0;
        clear_trace_begin_channel_ = 0;
        clear_trace_begin_subchannel_ = 0;
        clear_trace_pre_methods_.clear();
        clear_trace_methods_.clear();
        clear_trace_methods_.reserve(kClearTraceMethodLimit);
    }
    bool color_clear_trace_triggered() const { return clear_trace_triggered_; }
    bool color_clear_trace_begin_triggered() const {
        return clear_trace_begin_triggered_;
    }
    bool color_clear_trace_complete() const { return clear_trace_complete_; }
    bool color_clear_trace_truncated() const { return clear_trace_truncated_; }
    uint32_t color_clear_trace_target() const { return clear_trace_target_; }
    uint64_t color_clear_trace_trigger_seq() const { return clear_trace_trigger_seq_; }
    uint64_t color_clear_trace_begin_seq() const { return clear_trace_begin_seq_; }
    size_t color_clear_trace_pre_count() const { return clear_trace_pre_count_; }
    size_t color_clear_trace(std::vector<PushMethod>& out) const {
        out = clear_trace_methods_;
        return out.size();
    }
    // Frame clock: the machine advances this from retired instructions (~60 Hz). Each new
    // frame latches VBLANK into PCRTC_INTR_0; PCRTC_INTR_EN_0 and PMC_INTR_EN_0 gate the
    // line (xemu nv2a update_irq / pcrtc.c). The kernel's AV code paces on this interrupt.
    void set_frame(uint64_t frame_no);
    // Absolute core-clock edges from the selected machine clock. PTIMER is a
    // programmable counter; MMIO reads must not themselves advance it.
    void set_timer_clock(uint64_t core_cycles);
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
        // 16-bit mode only: PRAMDAC general control bit 12 selects RGB565 over
        // XRGB1555. Derived state, carried here so a presenter can name the guest
        // pixel format without reaching back into PRAMDAC.
        bool rgb565 = false;
    };
    ScanoutInfo scanout_info() const;
    bool capture_scanout(std::vector<uint8_t>& rgba, ScanoutInfo& info,
                         std::string& error);
    // INTERACTIVE PRESENTER ONLY (beads-xbox.1.15.8 stage (g)). A zero-copy view of
    // the current scanout: the same validation and the same seam anchor and resident
    // resolve as capture_scanout, but it returns the guest bytes where they already
    // live instead of a converted RGBA copy. Returns nullptr when guest RAM has no
    // contiguous host pointer for the range, in which case the caller falls back to
    // capture_scanout. `size` is the byte extent from `info.start`.
    //
    // The pointer is valid only until the guest runs again, so it must be consumed
    // (uploaded) before returning to the emulation loop. The replay path never calls
    // this: --frame-series and --frame-dump keep capture_scanout, which is the
    // byte-lockable capture every golden is taken with.
    const uint8_t* scanout_direct_bytes(ScanoutInfo& info, size_t& size, std::string& error);
    bool debug_resolve_color_surface(uint32_t address, size_t size,
                                     const char* context) {
        return resolve_renderer_color_overlap(address, size, context);
    }
    uint64_t scanout_writes() const { return scanout_writes_; }
    uint64_t scanout_nonzero_writes() const { return scanout_nonzero_writes_; }
    uint32_t last_nonzero_scanout_start() const { return last_nonzero_scanout_start_; }
    size_t recent_scanout_starts(size_t n, std::vector<uint32_t>& out) const {
        return scanout_starts_.window(n, out);
    }

private:
    // Resolve an absolute NV2A address to its block; returns nullptr (a hole) if unmapped.
    static const Block* find_block(uint32_t rel_addr, uint32_t& block_rel);
    bool resolve_renderer_color_overlap(uint32_t address, size_t size,
                                        const char* context);
    // The validation and the resident-surface resolve shared by capture_scanout and
    // scanout_direct_bytes. ONE copy, so the byte-locked capture path and the
    // interactive presenter cannot disagree about what a valid scanout is.
    bool prepare_scanout(ScanoutInfo& info, uint64_t& source_size, std::string& error);
    // `cause` is the always-on attribution token (nv2a_invalidate_token);
    // beads-xbox.1.15.10 phase 3. Attribution only -- no behaviour reads it.
    void invalidate_renderer_color_overlap(uint32_t address, size_t size,
                                           uint32_t cause);
    bool resolve_renderer_depth_overlap(uint32_t address, size_t size,
                                        const char* context);
    void invalidate_renderer_depth_overlap(uint32_t address, size_t size,
                                           uint32_t cause);
    // After a software generic-combiner batch wrote guest color memory
    // directly, keep an exactly-matching valid resident renderer surface
    // coherent by uploading only the written rows. Falls back to the blanket
    // invalidate when the backend cannot do region uploads, the target is not
    // resident, or the surface is not linear. Returns true when the resident
    // surface (if any) is coherent with guest memory.
    bool refresh_resident_color_after_software_write(
        uint32_t address, size_t size, uint32_t key_pitch, uint32_t row_pitch,
        uint32_t height, uint32_t format, size_t write_begin,
        size_t write_end);
    bool execute_image_blit_srccopy();

    FaultFn fault_;
    // PBUS command mirror. The bounded original XDK path changes only bus
    // mastering; external PCI arbitration/decode is outside this RAM bus.
    uint32_t pbus_command_ = 0;
    uint32_t pbus_latency_ = 0, pbus_rom_ = 0;
    uint32_t pbus_agp_command_ = 0;
    // This component starts with its engines available, as its existing
    // packet/scanout consumers require. This is not a silicon cold-reset mask.
    uint32_t pmc_enable_ = 0xffffffffu;
    // Boot PLL coefficients from the pinned xemu NV2A configuration. Original
    // XDK/pbkit decode these using the Xbox 16,666,666-Hz crystal.
    uint32_t pramdac_nvpll_ = 0x00011c01u;
    uint32_t pramdac_mpll_ = 0;
    uint32_t pramdac_vpll_ = 0x0003c20du;
    // Inactive overlay configuration. Enabling an overlay requires the real
    // overlay pixel/scanout path, which this bounded route explicitly rejects.
    std::array<uint32_t, 0x1000 / 4> pvideo_regs_{};
    uint64_t ptimer_source_ = 0, ptimer_ticks_ = 0, ptimer_fraction_ = 0;
    uint32_t ptimer_div_ = 0, ptimer_mul_ = 0, ptimer_alarm_ = 0;
    uint32_t ptimer_pending_ = 0, ptimer_enabled_ = 0;
    bool ptimer_alarm_programmed_ = false;
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
        uint32_t reference = 0;
        uint32_t engines = 0, last_engine = 0;
        uint8_t subchannel = 0;
        bool non_incrementing = false;
        bool subroutine_active = false;
        bool get_initialized = false;
    };
    std::array<ChannelState, 32> channels_{};
    bool pfifo_context_read(uint32_t offset, uint32_t& value);
    bool pfifo_context_write(uint32_t offset, uint32_t value);
    std::vector<uint8_t> pramin_;
    // Scratch for software-batch resident region uploads; reused to avoid
    // per-batch allocation churn.
    std::vector<uint8_t> region_upload_scratch_;
    // Persistent backing storage swapped into the per-batch generic-combiner
    // packet's surface snapshots so multi-megabyte buffers keep their capacity
    // across batches instead of reallocating and zero-filling every submit.
    std::vector<uint8_t> generic_color_input_scratch_;
    std::vector<uint8_t> generic_depth_input_scratch_;
    bool generic_vertex_scratch_enabled_ = false;
    uint64_t generic_vertex_scratch_epoch_ = 0;
    GenericVertexScratchStats generic_vertex_scratch_stats_{};
    std::vector<Nv2aGenericCombinerVertex> generic_packet_vertices_scratch_;
    std::vector<Nv2aGenericCombinerVertex> generic_primary_expanded_vertices_scratch_;
    // Recycled GL->guest readback buffer for resident color resolves.
    std::vector<uint8_t> color_resolve_scratch_;
    Ring<PushMethod> methods_;
    static constexpr size_t kClearTracePreMethods = 32;
    static constexpr size_t kClearTraceMethodLimit = 4096;
    bool clear_trace_armed_ = false;
    bool clear_trace_triggered_ = false;
    bool clear_trace_begin_triggered_ = false;
    bool clear_trace_complete_ = false;
    bool clear_trace_truncated_ = false;
    uint32_t clear_trace_target_ = 0;
    uint64_t clear_trace_trigger_seq_ = 0;
    uint64_t clear_trace_begin_seq_ = 0;
    size_t clear_trace_pre_count_ = 0;
    uint8_t clear_trace_begin_channel_ = 0;
    uint8_t clear_trace_begin_subchannel_ = 0;
    std::deque<PushMethod> clear_trace_pre_methods_;
    std::vector<PushMethod> clear_trace_methods_;
    std::array<uint64_t, 0x800> method_counts_{};
    std::array<uint64_t, 256> class_method_counts_{};
    struct ClassMethodStats {
        std::array<uint64_t, 0x800> counts{};
        std::array<uint32_t, 0x800> first_parameters{};
        std::array<uint32_t, 0x800> last_parameters{};
    };
    std::array<ClassMethodStats, 3> class2d_method_stats_{};
    // CACHE1 exposes 0x800 bytes of interleaved METHOD/DATA storage at
    // 0x1800..0x1fff: 0x800 / 8 = 256 method/data entries.
    static constexpr size_t kPullQueueCapacity = 256;
    std::deque<PushMethod> pull_queue_;
    std::function<bool(uint32_t, void*, size_t)> mem_read_;
    std::function<bool(uint32_t, const void*, size_t)> mem_write_;
    std::function<uint8_t*(uint32_t, size_t)> mem_direct_;
    // NV097 Z-pass reporting: CPU reference for the bounded single-sample
    // generic rasterizer. Report writes follow actual ordered raster work.
    std::array<uint32_t, 8> report_dma_{};
    std::array<bool, 8> report_dma_bound_{};
    bool zpass_enabled_ = false;
    bool zpass_valid_ = true;
    uint32_t zpass_count_ = 0;
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
    uint32_t pgraph_context_table_ = 0;
    // Original-driver control plane. Packet fixtures already own their
    // context; writing CTX_CONTROL opts into the actual driver-managed gate.
    bool pgraph_context_managed_ = false;
    bool pgraph_executing_ = false;
    uint32_t pgraph_context_control_ = 0, pgraph_context_user_ = 0;
    uint32_t pgraph_ffintfc_ = 0, pgraph_rdi_index_ = 0;
    // Original/nxdk DXT1-noise controls in RDI E0:50 and DF:08. Transport
    // is modeled separately; enabled pixel behavior remains guarded.
    std::array<uint32_t, 2> dxt1_noise_control_{};
    uint32_t* dxt1_noise_control_word(uint32_t select, uint32_t address);
    std::array<uint32_t, 4> pgraph_context_switch_{};
    std::array<uint32_t, 0xc00 / 4> pgraph_setup_{};
    struct RamTileConfig {
        uint32_t config0 = 0x03070003, config1 = 0, zcomp_offset = 0;
        std::array<std::array<uint32_t, 3>, 8> tiles{};
        std::array<uint32_t, 8> zcomp{};
    } pfb_tiles_, pgraph_tiles_, texture_tiles_;
    // Sparse, explicitly initialized compression tags, indexed by the memory
    // controller's partition/tag address, not by tile or host surface identity.
    // Unknown power-on contents must not be silently reported as uncompressed.
    std::unordered_map<uint32_t,bool> zeta_compression_tags_;
    bool compress_zbuffer_ = false;
    struct ZetaBlock {
        uint32_t key=0;
        std::array<uint32_t,4> rows{};
    };
    bool ram_zeta_block(unsigned tile,uint32_t linear,ZetaBlock& block);
    bool ram_zeta_read(const ZetaBlock& block,std::array<uint32_t,16>& pixels);
    bool ram_zeta_write(const ZetaBlock& block,const std::array<uint32_t,16>& pixels,bool compress);
    bool ram_zeta_clear(unsigned tile,uint32_t first,uint32_t pitch,uint32_t width,
                        uint32_t height,uint32_t value,uint32_t mask);
    static uint32_t* ram_config_word(RamTileConfig& config, uint32_t offset);
    bool ram_config_write(RamTileConfig& config, uint32_t offset, uint32_t value);
    bool ram_tiles_active() const;
    bool ram_tile_overlap(uint32_t address, size_t bytes) const;
    bool ram_tile_configuration();
    int ram_tile_at(uint32_t address) const;
    bool ram_tile_transfer(uint32_t address, void* data, size_t bytes, bool write);
    bool pgraph_setup_read(uint32_t offset, uint32_t& value);
    bool pgraph_setup_write(uint32_t offset, uint32_t value);
    void pgraph_reset_setup();
    uint32_t pgraph_status() const;
    uint32_t patt_color0_ = 0;
    struct ContextSurfaces2DState {
        uint32_t object_instance = 0;
        uint32_t dma_source = 0;
        uint32_t dma_dest = 0;
        bool dma_source_bound = false;
        bool dma_dest_bound = false;
        uint32_t color_format = 0;
        uint32_t source_pitch = 0;
        uint32_t dest_pitch = 0;
        uint32_t source_offset = 0;
        uint32_t dest_offset = 0;
    };
    ContextSurfaces2DState context_surfaces_2d_{};
    struct ImageBlitState {
        uint32_t object_instance = 0;
        uint32_t context_surfaces = 0;
        bool context_surfaces_bound = false;
        uint32_t operation = 0;
        uint32_t in_x = 0;
        uint32_t in_y = 0;
        uint32_t out_x = 0;
        uint32_t out_y = 0;
        uint32_t width = 0;
        uint32_t height = 0;
    };
    ImageBlitState image_blit_{};
    uint64_t image_blit_copies_ = 0;
    uint64_t image_blit_bytes_ = 0;
    uint32_t last_image_blit_source_ = 0;
    uint32_t last_image_blit_dest_ = 0;
    uint32_t last_image_blit_size_ = 0;

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
    uint64_t depth_raster_batches_ = 0;
    uint64_t rasterized_triangles_ = 0;
    uint64_t rasterized_fragments_ = 0;
    uint64_t depth_test_passes_ = 0;
    uint64_t depth_writes_ = 0;
    uint32_t last_raster_address_ = 0;
    uint64_t raster_output_hash_ = 1469598103934665603ull;
    uint64_t radial_color_batches_ = 0;
    uint64_t radial_color_triangles_ = 0;
    uint64_t radial_color_fragments_ = 0;
    uint64_t radial_color_depth_passes_ = 0;
    uint64_t radial_color_depth_writes_ = 0;
    uint64_t radial_color_writes_ = 0;
    uint32_t last_color_raster_address_ = 0;
    uint64_t radial_color_output_hash_ = 1469598103934665603ull;
    uint64_t textured_color_batches_ = 0;
    uint64_t textured_color_triangles_ = 0;
    uint64_t textured_color_fragments_ = 0;
    uint64_t textured_color_depth_passes_ = 0;
    uint64_t textured_color_depth_writes_ = 0;
    uint64_t textured_color_writes_ = 0;
    uint64_t texture_samples_ = 0;
    uint32_t last_textured_raster_address_ = 0;
    uint64_t textured_color_output_hash_ = 1469598103934665603ull;
    uint64_t pitch_textured_color_batches_ = 0;
    uint64_t pitch_textured_color_triangles_ = 0;
    uint64_t pitch_textured_color_fragments_ = 0;
    uint64_t pitch_textured_color_depth_passes_ = 0;
    uint64_t pitch_textured_color_depth_writes_ = 0;
    uint64_t pitch_textured_color_writes_ = 0;
    uint64_t pitch_texture_samples_ = 0;
    uint32_t last_pitch_textured_raster_address_ = 0;
    uint64_t pitch_textured_color_output_hash_ = 1469598103934665603ull;
    uint64_t projected_color_batches_ = 0;
    uint64_t projected_color_triangles_ = 0;
    uint64_t projected_color_fragments_ = 0;
    uint64_t projected_color_depth_passes_ = 0;
    uint64_t projected_color_depth_writes_ = 0;
    uint64_t projected_color_writes_ = 0;
    uint64_t projected_texture_samples_ = 0;
    uint32_t last_projected_raster_address_ = 0;
    uint64_t projected_color_output_hash_ = 1469598103934665603ull;
    uint64_t pitch_projected_color_batches_ = 0;
    uint64_t pitch_projected_color_triangles_ = 0;
    uint64_t pitch_projected_color_fragments_ = 0;
    uint64_t pitch_projected_color_depth_passes_ = 0;
    uint64_t pitch_projected_color_depth_writes_ = 0;
    uint64_t pitch_projected_color_writes_ = 0;
    uint64_t pitch_projected_texture_samples_ = 0;
    uint32_t last_pitch_projected_raster_address_ = 0;
    uint64_t pitch_projected_color_output_hash_ = 1469598103934665603ull;
    uint64_t pitch_single_texture_batches_ = 0;
    uint64_t pitch_single_texture_triangles_ = 0;
    uint64_t pitch_single_texture_fragments_ = 0;
    uint64_t pitch_single_texture_depth_passes_ = 0;
    uint64_t pitch_single_texture_writes_ = 0;
    uint64_t pitch_single_texture_samples_ = 0;
    uint32_t last_pitch_single_texture_raster_address_ = 0;
    uint64_t pitch_single_texture_output_hash_ = 1469598103934665603ull;
    uint64_t tex7_fan_batches_ = 0;
    uint64_t tex7_fan_triangles_ = 0;
    uint64_t tex7_fan_fragments_ = 0;
    uint64_t tex7_fan_depth_passes_ = 0;
    uint64_t tex7_fan_writes_ = 0;
    uint64_t tex7_fan_samples_ = 0;
    uint32_t last_tex7_fan_raster_address_ = 0;
    uint64_t tex7_fan_output_hash_ = 1469598103934665603ull;
    uint64_t pitch_multi_texture_batches_ = 0;
    uint64_t pitch_multi_texture_triangles_ = 0;
    uint64_t pitch_multi_texture_fragments_ = 0;
    uint64_t pitch_multi_texture_depth_passes_ = 0;
    uint64_t pitch_multi_texture_alpha_passes_ = 0;
    uint64_t pitch_multi_texture_depth_writes_ = 0;
    uint64_t pitch_multi_texture_color_writes_ = 0;
    uint64_t pitch_multi_texture_samples_ = 0;
    uint32_t last_pitch_multi_texture_raster_address_ = 0;
    uint64_t pitch_multi_texture_output_hash_ = 1469598103934665603ull;
    uint64_t pitch_three_texture_batches_ = 0;
    uint64_t pitch_three_texture_triangles_ = 0;
    uint64_t pitch_three_texture_fragments_ = 0;
    uint64_t pitch_three_texture_depth_passes_ = 0;
    uint64_t pitch_three_texture_depth_writes_ = 0;
    uint64_t pitch_three_texture_color_writes_ = 0;
    uint64_t pitch_three_texture_samples_ = 0;
    uint64_t pitch_three_texture_zero_direction0_ = 0;
    uint64_t pitch_three_texture_zero_direction1_ = 0;
    uint64_t pitch_three_texture_rescale_direction0_ = 0;
    uint64_t pitch_three_texture_rescale_direction1_ = 0;
    uint64_t pitch_three_texture_xy_tie_direction0_ = 0;
    uint64_t pitch_three_texture_xy_tie_direction1_ = 0;
    uint32_t last_pitch_three_zero_factor3_ = 0;
    uint32_t last_pitch_three_rescale_factor3_ = 0;
    uint32_t last_pitch_three_xy_tie_factor3_ = 0;
    uint32_t last_pitch_three_texture_raster_address_ = 0;
    uint64_t pitch_three_texture_output_hash_ = 1469598103934665603ull;
    uint64_t texf_projected_fan_batches_ = 0;
    uint64_t texf_projected_fan_triangles_ = 0;
    uint64_t texf_projected_fan_fragments_ = 0;
    uint64_t texf_projected_fan_alpha_passes_ = 0;
    uint64_t texf_projected_fan_writes_ = 0;
    uint64_t texf_projected_fan_samples_ = 0;
    uint32_t last_texf_projected_fan_raster_address_ = 0;
    uint64_t texf_projected_fan_output_hash_ = 1469598103934665603ull;
    uint64_t pitch_tex5_batches_ = 0;
    uint64_t pitch_tex5_triangles_ = 0;
    uint64_t pitch_tex5_fragments_ = 0;
    uint64_t pitch_tex5_depth_passes_ = 0;
    uint64_t pitch_tex5_depth_writes_ = 0;
    uint64_t pitch_tex5_color_writes_ = 0;
    uint64_t pitch_tex5_samples_ = 0;
    uint32_t last_pitch_tex5_raster_address_ = 0;
    uint64_t pitch_tex5_output_hash_ = 1469598103934665603ull;
    uint64_t inline_linear_blit_batches_ = 0;
    uint64_t inline_linear_blit_triangles_ = 0;
    uint64_t inline_linear_blit_fragments_ = 0;
    uint64_t inline_linear_blit_writes_ = 0;
    uint64_t inline_linear_blit_samples_ = 0;
    uint32_t last_inline_linear_blit_raster_address_ = 0;
    uint64_t inline_linear_blit_output_hash_ = 1469598103934665603ull;
    uint64_t inline_color_batches_ = 0;
    uint64_t inline_color_triangles_ = 0;
    uint64_t inline_color_fragments_ = 0;
    uint64_t inline_color_alpha_passes_ = 0;
    uint64_t inline_color_writes_ = 0;
    uint64_t inline_texture_samples_ = 0;
    uint32_t last_inline_raster_address_ = 0;
    uint64_t inline_color_output_hash_ = 1469598103934665603ull;
    uint64_t generic_combiner_batches_ = 0;
    uint64_t generic_combiner_attempted_batches_ = 0;
    uint64_t generic_combiner_executed_batches_ = 0;
    uint64_t generic_combiner_accelerated_batches_ = 0;
    uint64_t generic_combiner_resident_candidates_ = 0;
    uint64_t generic_combiner_resident_supported_ = 0;
    uint64_t generic_combiner_resident_uploads_ = 0;
    uint64_t generic_combiner_resident_reuses_ = 0;
    uint64_t generic_combiner_resident_target_switch_resolves_ = 0;
    uint64_t generic_combiner_resident_submits_ = 0;
    std::array<uint64_t,
              static_cast<size_t>(GenericResidentRejectReason::kCount)>
        generic_combiner_resident_rejects_{};
    uint64_t generic_combiner_texture_modulate_observed_ = 0;
    uint64_t generic_combiner_texture_modulate_exact_ = 0;
    std::array<uint64_t,
              static_cast<size_t>(GenericTextureModulateRejectReason::kCount)>
        generic_combiner_texture_modulate_rejects_{};
    uint64_t generic_combiner_fallback_batches_ = 0;
    uint64_t generic_combiner_triangles_ = 0;
    uint64_t generic_combiner_fragments_ = 0;
    uint64_t generic_combiner_depth_passes_ = 0;
    uint64_t generic_combiner_writes_ = 0;
    uint64_t generic_combiner_color_reads_ = 0;
    uint64_t generic_combiner_depth_reads_ = 0;
    uint64_t generic_combiner_texture_samples_ = 0;
    uint32_t last_generic_combiner_raster_address_ = 0;
    uint64_t generic_combiner_output_hash_ = 1469598103934665603ull;
    std::vector<GenericCombinerFingerprintStats> generic_combiner_fingerprints_;
    std::array<uint64_t,
              static_cast<size_t>(GenericSkipReason::kCount)>
        generic_skip_reasons_{};
    struct GenericSkipDetail {
        GenericSkipReason reason;
        uint64_t detail;
        uint64_t count;
    };
    std::vector<GenericSkipDetail> generic_skip_details_;
    uint64_t immediate_quad_raster_batches_ = 0;
    uint64_t immediate_quad_raster_triangles_ = 0;
    uint64_t immediate_quad_raster_fragments_ = 0;
    uint64_t immediate_quad_raster_alpha_passes_ = 0;
    uint64_t immediate_quad_raster_writes_ = 0;
    uint64_t immediate_quad_texture_samples_ = 0;
    uint32_t last_immediate_quad_raster_address_ = 0;
    uint64_t immediate_quad_raster_output_hash_ = 1469598103934665603ull;
    uint64_t immediate_quad_rs12_candidates_ = 0;
    uint32_t last_immediate_quad_rs12_reject_mask_ = 0;
    uint32_t last_immediate_quad_rs12_raster_reject_mask_ = 0;
    uint64_t immediate_quad_i8_raster_batches_ = 0;
    uint64_t immediate_quad_i8_raster_triangles_ = 0;
    uint64_t immediate_quad_i8_raster_fragments_ = 0;
    uint64_t immediate_quad_i8_raster_alpha_passes_ = 0;
    uint64_t immediate_quad_i8_raster_writes_ = 0;
    uint64_t immediate_quad_i8_texture_samples_ = 0;
    uint32_t last_immediate_quad_i8_raster_address_ = 0;
    uint64_t immediate_quad_i8_raster_output_hash_ = 1469598103934665603ull;
    uint64_t immediate_quad_i8_candidates_ = 0;
    uint32_t last_immediate_quad_i8_reject_mask_ = 0;
    uint32_t last_immediate_quad_i8_raster_reject_mask_ = 0;
    uint64_t immediate_quad_a8_raster_batches_ = 0;
    uint64_t immediate_quad_a8_raster_triangles_ = 0;
    uint64_t immediate_quad_a8_raster_fragments_ = 0;
    uint64_t immediate_quad_a8_raster_alpha_passes_ = 0;
    uint64_t immediate_quad_a8_raster_writes_ = 0;
    uint64_t immediate_quad_a8_texture_samples_ = 0;
    uint32_t last_immediate_quad_a8_raster_address_ = 0;
    uint64_t immediate_quad_a8_raster_output_hash_ = 1469598103934665603ull;
    uint64_t immediate_quad_a8_candidates_ = 0;
    uint32_t last_immediate_quad_a8_reject_mask_ = 0;
    uint32_t last_immediate_quad_a8_raster_reject_mask_ = 0;
    uint64_t immediate_quad_menu_i8_raster_batches_ = 0;
    uint64_t immediate_quad_menu_i8_raster_triangles_ = 0;
    uint64_t immediate_quad_menu_i8_raster_fragments_ = 0;
    uint64_t immediate_quad_menu_i8_raster_alpha_passes_ = 0;
    uint64_t immediate_quad_menu_i8_raster_writes_ = 0;
    uint64_t immediate_quad_menu_i8_texture_samples_ = 0;
    uint32_t last_immediate_quad_menu_i8_raster_address_ = 0;
    uint64_t immediate_quad_menu_i8_raster_output_hash_ = 1469598103934665603ull;
    uint64_t immediate_quad_menu_i8_candidates_ = 0;
    uint32_t last_immediate_quad_menu_i8_reject_mask_ = 0;
    uint32_t last_immediate_quad_menu_i8_raster_reject_mask_ = 0;
    uint64_t immediate_quad_artwork_i8_raster_batches_ = 0;
    uint64_t immediate_quad_artwork_i8_raster_triangles_ = 0;
    uint64_t immediate_quad_artwork_i8_raster_fragments_ = 0;
    uint64_t immediate_quad_artwork_i8_raster_alpha_passes_ = 0;
    uint64_t immediate_quad_artwork_i8_raster_writes_ = 0;
    uint64_t immediate_quad_artwork_i8_texture_samples_ = 0;
    uint32_t last_immediate_quad_artwork_i8_raster_address_ = 0;
    uint64_t immediate_quad_artwork_i8_raster_output_hash_ = 1469598103934665603ull;
    uint64_t immediate_quad_artwork_i8_candidates_ = 0;
    uint32_t last_immediate_quad_artwork_i8_reject_mask_ = 0;
    uint32_t last_immediate_quad_artwork_i8_raster_reject_mask_ = 0;
    uint64_t immediate_quad_main_panel_raster_batches_ = 0;
    uint64_t immediate_quad_main_panel_raster_triangles_ = 0;
    uint64_t immediate_quad_main_panel_raster_fragments_ = 0;
    uint64_t immediate_quad_main_panel_raster_alpha_passes_ = 0;
    uint64_t immediate_quad_main_panel_raster_writes_ = 0;
    uint64_t immediate_quad_main_panel_texture_samples_ = 0;
    uint32_t last_immediate_quad_main_panel_raster_address_ = 0;
    uint64_t immediate_quad_main_panel_raster_output_hash_ = 1469598103934665603ull;
    uint64_t immediate_quad_main_panel_candidates_ = 0;
    uint32_t last_immediate_quad_main_panel_reject_mask_ = 0;
    uint32_t last_immediate_quad_main_panel_raster_reject_mask_ = 0;
    uint64_t immediate_quad_fullscreen_raster_batches_ = 0;
    uint64_t immediate_quad_fullscreen_raster_triangles_ = 0;
    uint64_t immediate_quad_fullscreen_raster_fragments_ = 0;
    uint64_t immediate_quad_fullscreen_raster_alpha_passes_ = 0;
    uint64_t immediate_quad_fullscreen_raster_writes_ = 0;
    uint64_t immediate_quad_fullscreen_texture_samples_ = 0;
    uint32_t last_immediate_quad_fullscreen_raster_address_ = 0;
    uint64_t immediate_quad_fullscreen_raster_output_hash_ = 1469598103934665603ull;
    uint64_t immediate_quad_fullscreen_candidates_ = 0;
    uint32_t last_immediate_quad_fullscreen_reject_mask_ = 0;
    uint32_t last_immediate_quad_fullscreen_raster_reject_mask_ = 0;

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
    uint32_t texture_dma_a_ = 0;
    uint32_t texture_dma_b_ = 0;
    bool texture_dma_a_bound_ = false;
    bool texture_dma_b_bound_ = false;
    std::array<VertexAttribute, 16> vertex_attributes_{};
    uint8_t primitive_mode_ = 0;
    std::vector<uint32_t> vertex_indices_;
    using VertexInputs =
        std::array<std::array<float, 4>, 16>;
    std::vector<uint32_t> inline_array_words_;
    std::vector<VertexInputs> inline_vertices_;
    // Kelvin immediate-mode methods update the persistent attribute values above.  The
    // witnessed legacy SET_VERTEX4F position W method snapshots all 16 values here; it is a
    // third mutually exclusive submission source beside indexed arrays and NV097_INLINE_ARRAY.
    std::vector<VertexInputs> immediate_vertices_;
    uint64_t vertex_batches_ = 0;
    uint64_t submitted_vertices_ = 0;
    uint64_t vertex_attribute_fetches_ = 0;
    uint64_t immediate_methods_ = 0;
    uint64_t immediate_vertices_total_ = 0;
    uint64_t immediate_batches_ = 0;
    uint64_t inline_array_batches_ = 0;
    uint64_t inline_array_words_total_ = 0;
    uint64_t inline_array_vertices_total_ = 0;
    uint64_t inline_attribute_decodes_ = 0;
    uint64_t assembled_primitives_ = 0;
    uint64_t assembled_triangles_ = 0;
    uint32_t largest_vertex_index_ = 0;
    std::array<uint64_t, 11> primitive_mode_counts_{};
    std::array<uint64_t, 7> vertex_format_fetches_{};
    std::array<uint64_t, 16> vertex_slot_fetches_{};
    bool detailed_telemetry_ = true;
    // See set_profile_hashes_enabled(). Default TRUE keeps the 22 witnessed and
    // scanout hashes, the last-address trackers and the per-write census exactly
    // as they are today; the fragment loop reads a batch-local copy, not this.
    bool profile_hashes_enabled_ = true;

    // NV2A Cheops transform state. Programmable vertex shaders consist of up to 136
    // four-word instructions and address 192 float4 constants. The pinned xemu CPU
    // interpreter executes these exact tokens; shader output oPos is already in Xbox
    // screen space, which is the input contract for the software rasterizer.
    std::array<std::array<uint32_t, 4>, 136> transform_program_{};
    std::array<std::array<float, 4>, 192> transform_constants_{};
    std::array<float, 4> viewport_offset_{};
    std::array<float, 4> viewport_scale_{};
    uint8_t transform_execution_mode_ = 0;
    uint8_t transform_program_load_ = 0;
    uint8_t transform_program_start_ = 0;
    uint8_t transform_constant_load_ = 0;
    uint64_t transform_program_words_ = 0;
    uint64_t transform_constant_words_ = 0;
    // ---- parsed-program cache (beads-xbox.1.15.10 phase 5, item 3) ---------
    // nv2a_vsh_parse_program() mallocs sizeof(Nv2aVshStep) * (136 - start) --
    // about 21 kB -- and disassembles EVERY slot, including the ones past the
    // final instruction that the interpreter never reaches, and it did so once
    // per vertex batch (455 812 per canonical 12 G run). The parse is a pure
    // function of the program words and the start slot, both of which change
    // only through two method handlers, so the result is cached and the two
    // handlers invalidate it. Exactness-neutral by construction: the cached
    // Nv2aVshProgram is the byte-identical output of the same call, and
    // nv2a_vsh_emu_execute takes it by const pointer.
    //
    // Held as void* so nv2a.hpp does not have to pull in the vendored
    // nv2a_vsh_disassembler.h; nv2a.cpp casts it back to Nv2aVshStep*.
    void* vsh_cache_steps_ = nullptr;
    bool vsh_cache_valid_ = false;
    bool vsh_cache_writes_context_ = false;
    bool vertex_program_cache_enabled_ = true;
    // ---- the pre-decoded step form (beads-xbox.1.15.11) --------------------
    // Compiled from the parsed program, once per parse, into 32-byte records
    // whose operand bases, offsets, swizzles and writemasks are already
    // resolved. `vsh_cache_ops_valid_` is cleared with the parse cache and
    // whenever the flag moves, and a program the compiler declines leaves it
    // false so the vendored interpreter runs unchanged.
    std::vector<VshCompiledOp> vsh_cache_ops_;
#if XLLE_VSH_CAPTURE
    VshCaptureStats vsh_capture_stats_{};
    uint64_t vsh_capture_interval_ = 0;
    uint32_t vsh_capture_budget_ = 0;
    std::function<void(const vsh_capture::Vertex&)> vsh_capture_callback_;
    VshBatchCaptureStats vsh_batch_capture_stats_{};
    uint64_t vsh_batch_capture_interval_ = 0;
    uint32_t vsh_batch_capture_budget_ = 0;
    std::function<bool(const vsh_batch_capture::Batch&)> vsh_batch_capture_callback_;
    void capture_vsh_vertex(bool eligible, uint64_t fingerprint,
                            const float* inputs, const float* outputs, const float* post_position);
#endif
#if XLLE_VSH_CAPTURE || XLLE_VSH_GPU_RESIDENT
    bool vsh_capture_plan_supported_ = false;
    std::vector<vsh_capture::Op> capture_vsh_ops() const;
#endif
#if XLLE_VSH_GPU_RESIDENT
    uint32_t vsh_gpu_resident_mode_ = 0;
    VshGpuDeferredStats vsh_gpu_deferred_stats_{};
#endif
    // Which register files this program can actually observe unwritten. See
    // VshZeroPlan in nv2a_vsh_compiled.hpp.
    VshZeroPlan vsh_cache_zero_plan_{};
    VshDotPrefixPlan vsh_dot_prefix_plan_{};
    VshDotPrefixStats vsh_dot_prefix_stats_{};
    bool vsh_cache_ops_valid_ = false;
    bool vsh_compile_enabled_ = true;
    bool vsh_dot_prefix_enabled_ = false;
    uint32_t vsh_unsafe_probe_ = 0;
    uint32_t vsh_verify_budget_ = 0;
    uint64_t vsh_verify_checked_ = 0;
    uint64_t vsh_verify_mismatched_ = 0;
    uint64_t vsh_verify_reported_ = 0;
    // Runs one vertex through BOTH forms and compares. Out of line and called
    // only when the budget is non-zero, so the shipping loop carries one
    // predictable branch and nothing else.
    void vsh_verify_vertex(const void* program, const float* input_regs,
                           const float* compiled_output_regs);
    uint64_t vsh_cache_fingerprint_ = 0;
    VshProgramCensusRow* vsh_cache_census_row_ = nullptr;
#if XLLE_VSH_DRAW_OVERLAP_CENSUS
    VshDrawOverlapProgramIdentity vsh_cache_overlap_identity_{};
    bool vsh_draw_overlap_enabled_ = false;
    VshDrawOverlapCensus vsh_draw_overlap_census_{};
#endif
    // Returns false when the program contains anything the pre-decoded form
    // cannot represent EXACTLY -- an index outside the range the vendored code
    // asserts, an opcode outside its dispatch table, or no final step. There is
    // no partial compile: the caller runs the interpreter for the whole program.
    // `steps` is the Nv2aVshStep array the vendored parser produced; it is taken
    // as void* for the same reason vsh_cache_steps_ is, so this header does not
    // have to pull in nv2a_vsh_disassembler.h.
    bool compile_vsh_program(const void* steps, uint32_t slots,
                             VshProgramCensusRow* row);
    VshProgramCensusRow* vsh_program_census_row(uint64_t fingerprint);
    void invalidate_vsh_program_cache();
    void destroy_vsh_program_cache();
    uint64_t transformed_vertices_ = 0;
    uint64_t transform_output_hash_ = 1469598103934665603ull;
    std::array<uint64_t, 4> transform_mode_batches_{};
    uint8_t fixed_skin_mode_ = 0;
    bool fixed_lighting_ = false;
    bool fixed_normalization_ = false;
    bool fixed_specular_ = false;
    bool fixed_fog_ = false;
    uint8_t fixed_color_material_ = 0;
    uint8_t fixed_light_control_ = 0;
    uint16_t fixed_light_enable_mask_ = 0;
    std::array<float, 3> fixed_scene_ambient_{};
    std::array<float, 3> fixed_material_emission_{};
    float fixed_material_alpha_ = 0.0f;
    std::array<bool, 4> fixed_texture_matrix_enable_{};
    std::array<std::array<uint32_t, 4>, 4> fixed_texgen_{};
    std::array<uint64_t, 7> fixed_skin_batches_{};
    uint64_t fixed_lighting_batches_ = 0;
    uint64_t fixed_normalization_batches_ = 0;
    uint64_t fixed_specular_batches_ = 0;
    uint64_t fixed_fog_batches_ = 0;
    uint64_t fixed_texture_matrix_batches_ = 0;
    uint64_t fixed_texgen_batches_ = 0;
    std::array<uint64_t, 4> fixed_light_type_batches_{};
    std::array<uint64_t, 256> fixed_color_material_batches_{};
    std::array<uint64_t, 8> fixed_light_control_batches_{};
    std::array<uint32_t, kRasterStateWords> raster_state_{};
    Nv2aRenderer* renderer_ = nullptr;  // owned by the host/application layer
    std::function<void(const GenericBatchDebugInfo&)> generic_batch_debug_cb_;
    std::function<bool(const GenericPacketDebugInfo&)>
        generic_packet_snapshot_filter_cb_;
    std::function<bool(const GenericPacketDebugInfo&,
                       const GenericPacketSnapshot&)>
        generic_packet_snapshot_cb_;
    std::function<void(const SurfaceWriteDebugInfo&)> surface_write_debug_cb_;
    std::vector<RasterStateSample> raster_state_samples_;
    uint32_t current_raster_state_sample_index_ = UINT32_MAX;
    using TransformedVertex = Nv2aGenericCombinerVertex;
    std::vector<TransformedVertex> transformed_batch_;

    // ============ stage (e): the job's private telemetry sink ==============
    // beads-xbox.1.15.5, DESIGN-NV2A-SEAM.md F14 and section 4.3. A worker must
    // write NO Nv2a member, so every counter, hash and last-value scalar the
    // fragment loop used to increment on `this` is written here instead, and the
    // emulation thread folds them into the members in latch order at
    // fold_draw_telemetry(). With one strictly-ordered consumer that fold
    // sequence is the single-threaded sequence, so the numbers are identical.
    struct DrawJobLastValue {
        uint32_t value = 0;
        bool set = false;
        DrawJobLastValue& operator=(uint32_t v) {
            value = v;
            set = true;
            return *this;
        }
        operator uint32_t() const { return value; }
    };

    struct DrawJobTelemetry {
        // 89 pure accumulators: the job counts its own fragments, samples,
        // passes, writes, triangles and batches from zero and the emulation
        // thread adds them into the members at fold time, in latch order.
        uint64_t depth_raster_batches = 0;
        uint64_t depth_test_passes = 0;
        uint64_t depth_writes = 0;
        uint64_t inline_color_alpha_passes = 0;
        uint64_t inline_color_batches = 0;
        uint64_t inline_color_fragments = 0;
        uint64_t inline_color_triangles = 0;
        uint64_t inline_color_writes = 0;
        uint64_t inline_linear_blit_batches = 0;
        uint64_t inline_linear_blit_fragments = 0;
        uint64_t inline_linear_blit_samples = 0;
        uint64_t inline_linear_blit_triangles = 0;
        uint64_t inline_linear_blit_writes = 0;
        uint64_t inline_texture_samples = 0;
        uint64_t pitch_multi_texture_alpha_passes = 0;
        uint64_t pitch_three_texture_rescale_direction0 = 0;
        uint64_t pitch_three_texture_rescale_direction1 = 0;
        uint64_t pitch_three_texture_zero_direction0 = 0;
        uint64_t pitch_three_texture_zero_direction1 = 0;
        uint64_t pitch_three_texture_xy_tie_direction0 = 0;
        uint64_t pitch_three_texture_xy_tie_direction1 = 0;
        uint64_t pitch_multi_texture_batches = 0;
        uint64_t pitch_multi_texture_color_writes = 0;
        uint64_t pitch_multi_texture_depth_passes = 0;
        uint64_t pitch_multi_texture_depth_writes = 0;
        uint64_t pitch_multi_texture_fragments = 0;
        uint64_t pitch_multi_texture_samples = 0;
        uint64_t pitch_multi_texture_triangles = 0;
        uint64_t pitch_projected_color_batches = 0;
        uint64_t pitch_projected_color_depth_passes = 0;
        uint64_t pitch_projected_color_depth_writes = 0;
        uint64_t pitch_projected_color_fragments = 0;
        uint64_t pitch_projected_color_triangles = 0;
        uint64_t pitch_projected_color_writes = 0;
        uint64_t pitch_projected_texture_samples = 0;
        uint64_t pitch_single_texture_batches = 0;
        uint64_t pitch_single_texture_depth_passes = 0;
        uint64_t pitch_single_texture_fragments = 0;
        uint64_t pitch_single_texture_samples = 0;
        uint64_t pitch_single_texture_triangles = 0;
        uint64_t pitch_single_texture_writes = 0;
        uint64_t pitch_tex5_batches = 0;
        uint64_t pitch_tex5_color_writes = 0;
        uint64_t pitch_tex5_depth_passes = 0;
        uint64_t pitch_tex5_depth_writes = 0;
        uint64_t pitch_tex5_fragments = 0;
        uint64_t pitch_tex5_samples = 0;
        uint64_t pitch_tex5_triangles = 0;
        uint64_t pitch_texture_samples = 0;
        uint64_t pitch_textured_color_batches = 0;
        uint64_t pitch_textured_color_depth_passes = 0;
        uint64_t pitch_textured_color_depth_writes = 0;
        uint64_t pitch_textured_color_fragments = 0;
        uint64_t pitch_textured_color_triangles = 0;
        uint64_t pitch_textured_color_writes = 0;
        uint64_t pitch_three_texture_batches = 0;
        uint64_t pitch_three_texture_color_writes = 0;
        uint64_t pitch_three_texture_depth_passes = 0;
        uint64_t pitch_three_texture_depth_writes = 0;
        uint64_t pitch_three_texture_fragments = 0;
        uint64_t pitch_three_texture_samples = 0;
        uint64_t pitch_three_texture_triangles = 0;
        uint64_t projected_color_batches = 0;
        uint64_t projected_color_depth_passes = 0;
        uint64_t projected_color_depth_writes = 0;
        uint64_t projected_color_fragments = 0;
        uint64_t projected_color_triangles = 0;
        uint64_t projected_color_writes = 0;
        uint64_t projected_texture_samples = 0;
        uint64_t radial_color_batches = 0;
        uint64_t radial_color_depth_passes = 0;
        uint64_t radial_color_depth_writes = 0;
        uint64_t radial_color_fragments = 0;
        uint64_t radial_color_triangles = 0;
        uint64_t radial_color_writes = 0;
        uint64_t rasterized_fragments = 0;
        uint64_t rasterized_triangles = 0;
        uint64_t tex7_fan_batches = 0;
        uint64_t tex7_fan_depth_passes = 0;
        uint64_t tex7_fan_fragments = 0;
        uint64_t tex7_fan_samples = 0;
        uint64_t tex7_fan_triangles = 0;
        uint64_t tex7_fan_writes = 0;
        uint64_t texf_projected_fan_alpha_passes = 0;
        uint64_t texf_projected_fan_batches = 0;
        uint64_t texf_projected_fan_fragments = 0;
        uint64_t texf_projected_fan_triangles = 0;
        uint64_t texf_projected_fan_writes = 0;
        uint64_t texture_samples = 0;
        uint64_t textured_color_batches = 0;
        uint64_t textured_color_depth_passes = 0;
        uint64_t textured_color_depth_writes = 0;
        uint64_t textured_color_fragments = 0;
        uint64_t textured_color_triangles = 0;
        uint64_t textured_color_writes = 0;

        // 12 order-dependent FNV-1a accumulators. Seeded from the member at
        // latch and written back at fold, and only ever touched when hash_on --
        // which forces the job inline (design section 4.3 option O3), so the
        // fold sequence is exactly the single-threaded one.
        uint64_t inline_color_output_hash = 0;
        uint64_t inline_linear_blit_output_hash = 0;
        uint64_t pitch_projected_color_output_hash = 0;
        uint64_t pitch_multi_texture_output_hash = 0;
        uint64_t pitch_three_texture_output_hash = 0;
        uint64_t pitch_single_texture_output_hash = 0;
        uint64_t pitch_tex5_output_hash = 0;
        uint64_t pitch_textured_color_output_hash = 0;
        uint64_t projected_color_output_hash = 0;
        uint64_t radial_color_output_hash = 0;
        uint64_t raster_output_hash = 0;
        uint64_t tex7_fan_output_hash = 0;
        uint64_t texf_projected_fan_output_hash = 0;
        uint64_t textured_color_output_hash = 0;

        // 17 last-write-wins scalars. DrawJobLastValue records whether this job
        // assigned at all, so folding jobs in latch order reproduces the
        // single-threaded result: a job that never wrote leaves the member alone.
        DrawJobLastValue last_color_raster_address;
        DrawJobLastValue last_inline_linear_blit_raster_address;
        DrawJobLastValue last_inline_raster_address;
        DrawJobLastValue last_pitch_multi_texture_raster_address;
        DrawJobLastValue last_pitch_projected_raster_address;
        DrawJobLastValue last_pitch_single_texture_raster_address;
        DrawJobLastValue last_pitch_tex5_raster_address;
        DrawJobLastValue last_pitch_textured_raster_address;
        DrawJobLastValue last_pitch_three_rescale_factor3;
        DrawJobLastValue last_pitch_three_texture_raster_address;
        DrawJobLastValue last_pitch_three_xy_tie_factor3;
        DrawJobLastValue last_pitch_three_zero_factor3;
        DrawJobLastValue last_projected_raster_address;
        DrawJobLastValue last_raster_address;
        DrawJobLastValue last_tex7_fan_raster_address;
        DrawJobLastValue last_texf_projected_fan_raster_address;
        DrawJobLastValue last_textured_raster_address;
        // ---- F13: the executor may not fault_. It records the first message and
        // the emulation thread raises the real fault at the next fold, with the
        // original text. `ok` is the executor's return value, carried across the
        // queue for a job whose caller has already returned.
        bool ok = true;
        bool failed = false;
        std::string fault_message;

        // ---- F3: renderer invalidation is a guest-visible side effect that
        // touches renderer_ and mem_write_; it is recorded here and applied on
        // the emulation thread, in job order, at the fold.
        bool invalidate_color = false;
        uint32_t invalidate_color_address = 0;
        size_t invalidate_color_bytes = 0;
        uint32_t invalidate_color_cause = 0;
        bool invalidate_depth = false;
        uint32_t invalidate_depth_address = 0;
        size_t invalidate_depth_bytes = 0;
        uint32_t invalidate_depth_cause = 0;
        // beads-xbox.1.15.10 phase 3, step 4: the destination ROW range this
        // draw actually wrote, in guest surface rows, unioned across every span
        // part. [row_lo, row_hi] inclusive; row_hi < row_lo means "nothing
        // written". This is what lets a CPU witnessed draw refresh only the
        // rows it touched into a resident GL surface instead of dropping the
        // whole plane.
        uint32_t color_row_lo = 0xFFFFFFFFu;
        uint32_t color_row_hi = 0;
        uint32_t depth_row_lo = 0xFFFFFFFFu;
        uint32_t depth_row_hi = 0;

        // Worker-side busy time for this job, in rdtsc ticks. Accumulated by
        // whichever thread executed it and folded on the emulation thread.
        uint64_t busy_ticks = 0;

        void reset() { *this = DrawJobTelemetry(); }
    };

    // ================== stage (d): the latched DrawJob =====================
    // DESIGN-NV2A-SEAM.md section 1.2, item for item. Every field is state the
    // fragment loop reads today and that a later method in the same pushbuffer
    // batch could change before a worker ran. Stage (d) executes the job inline
    // at the latch point, so nothing here changes behavior -- the point is that
    // the executor reads the JOB and the ownership is already correct when
    // stage (e) moves execution off the emulation thread.
    struct DrawJob {
#if XLLE_VSH_GPU_RESIDENT
        std::shared_ptr<Nv2aGenericCombinerPacket::GpuVshReference> gpu_vsh;
        std::vector<VshCompiledOp> gpu_vsh_compiled_ops;
#endif
#if XLLE_VSH_CAPTURE
        std::unique_ptr<vsh_batch_capture::Batch> vsh_batch_capture;
#endif
        // --- identity / ordering ---------------------------------------------
        uint64_t seq = 0;               // <- vertex_batches_
        uint32_t profile = 0;           // <- the witnessed Profile ladder result
        uint32_t primitive_mode = 0;    // <- primitive_mode_

        // --- register mirrors -------------------------------------------------
        std::array<uint32_t, kRasterStateWords> raster_state{};  // <- raster_state_
        uint32_t surface_format = 0;            // <- surface_format_
        uint32_t surface_pitch = 0;             // <- surface_pitch_
        uint32_t surface_clip_horizontal = 0;   // <- surface_clip_horizontal_
        uint32_t surface_clip_vertical = 0;     // <- surface_clip_vertical_
        uint32_t surface_color_offset = 0;      // <- surface_color_offset_
        uint32_t surface_zeta_offset = 0;       // <- surface_zeta_offset_
        // F16: raster_state_samples_ holds a RAW POINTER that a push_back can
        // invalidate. The job carries the INDEX by value, never the pointer.
        uint32_t raster_state_sample_index = 0; // <- current_raster_state_sample_index_
        bool hash_on = false;                   // <- profile_hashes_enabled_
        bool debug_cb_installed = false;        // <- F15 callbacks, by presence

        // --- derived surface descriptors (filled by the profile latch) --------
        uint32_t color_address = 0;
        uint32_t depth_address = 0;
        uint32_t pitch = 0;
        uint32_t surface_width = 0;
        uint32_t surface_height = 0;
        uint32_t clip_x = 0, clip_y = 0, clip_width = 0, clip_height = 0;
        uint64_t color_bytes = 0, depth_bytes = 0;
        bool uses_depth = false;
        bool writes_depth = false;
        bool color_profile = false;
        bool inline_depth_test = false;

        // --- destination ownership (section 1.3) ------------------------------
        // NON-NEGOTIABLE for a threaded job: both non-null, because the paged
        // fallback writes through mem_write_ == uc_mem_write, which is not safe
        // off the emulation thread.
        uint8_t* color_direct = nullptr;
        uint8_t* depth_direct = nullptr;

        // --- vertex data ownership --------------------------------------------
        std::vector<TransformedVertex> vertices;  // <- moved out of transformed_batch_

        // --- texture source bytes (owned snapshots, MOVED not copied) ---------
        std::vector<uint8_t> texture_data;
        std::vector<uint8_t> projected_texture_data;
        std::vector<uint8_t> linear_blit_texture_data;
        std::vector<uint8_t> multi_texture_data;
        std::vector<uint8_t> single_texture_data;
        std::vector<uint8_t> pitch_tex5_texture_data;
        std::vector<uint8_t> inline_texture_data;
        // Latch slot for the immediate-quad palette. DELIBERATELY UNPOPULATED in
        // stage (d): that family reads its palette per fragment through a
        // PagedRasterSurface built WITHOUT mem_direct_, so it is inline-only
        // under section 1.3 anyway, and materializing 1 KiB per batch here would
        // change the guest-read pattern in a step whose entire gate is "zero
        // behavior change". Filled when that family is converted to the direct
        // path (design section 3.2).
        std::vector<uint8_t> palette_data;

        // --- texture descriptors and swizzle tables (stage (e)) ---------------
        // The setup half derives these from raster_state and they are read by the
        // fragment loop, so they cross the latch/execute boundary and must live
        // in the job. SwizzleLookup is immutable after construction.
        std::unique_ptr<SwizzleLookup> texture_swizzle;
        uint32_t texture_width = 0;
        uint32_t texture_height = 0;
        std::unique_ptr<SwizzleLookup> projected_texture_swizzle;
        uint32_t projected_texture_width = 0;
        uint32_t projected_texture_height = 0;
        uint32_t linear_blit_texture_width = 0;
        uint32_t linear_blit_texture_height = 0;
        uint32_t linear_blit_texture_pitch = 0;
        std::unique_ptr<SwizzleLookup> multi_texture_swizzle;
        uint32_t multi_texture_width = 0;
        uint32_t multi_texture_height = 0;
        std::unique_ptr<SwizzleLookup> single_texture_swizzle;
        uint32_t single_texture_width = 0;
        uint32_t single_texture_height = 0;
        std::unique_ptr<SwizzleLookup> pitch_tex5_texture_swizzle;
        uint32_t pitch_tex5_texture_width = 0;
        uint32_t pitch_tex5_texture_height = 0;
        std::unique_ptr<SwizzleLookup> inline_texture_swizzle;
        uint32_t inline_texture_width = 0;
        uint32_t inline_texture_height = 0;
        bool pitch_tex5_profile = false;
        // Destination-surface swizzle table for the three swizzled colour
        // profiles. Built at the LATCH (stage (e2), beads-xbox.1.15.9) rather
        // than inside the executor: with N workers running the same job the
        // executor is entered N times, and SwizzleLookup(640,480) is ~10.7 k
        // loop iterations against ~26 k fragments per batch, so rebuilding it
        // per worker would have added 30-40 percent to the swizzled profiles.
        // Built once per job either way, so the inline path is unchanged.
        // Immutable after construction, so all N workers share it read-only.
        std::unique_ptr<SwizzleLookup> surface_swizzle;

        // --- telemetry sink (F14) ---------------------------------------------
        // Part 0's accumulator. Parts 1..span_parts-1 accumulate into the
        // device's span_out_ cells and are folded straight into the members by
        // the SAME fold_draw_telemetry, once per part, in part order -- there is
        // no second summation list to drift out of sync.
        DrawJobTelemetry out;
        // How many destination-span parts actually executed this job: 1 for
        // every inline and every queued job, draw_threads_ for a span-parallel
        // one. Read by fold_draw_job to know how many sub-accumulators to fold.
        unsigned span_parts = 1;

        // --- eligibility, decided at latch, acted on at stage (e) -------------
        bool threadable = false;
        uint32_t not_threadable_reason = kSeamNoDirectPointer;
        // Whether the job would be IN THE RING in the configuration stage (e)
        // would actually run in. Identical to `threadable` except that a job
        // held back only by the per-write FNV fold counts as queueable: the fold
        // is a runtime switch (section 4.3 option O3) that stage (e) turns off,
        // whereas the direct-pointer rule, the GL path and the debug callbacks
        // are structural. This, not `threadable`, drives the M5 simulation, so
        // M5 is answerable from the canonical hashes-on run.
        bool queueable = false;
        // Stage (e): the latch half decided to hand this job to the worker
        // instead of executing it. Set only inside the one enqueue branch in
        // rasterize_witnessed_batch, and only when draw_threads_ > 0.
        bool deferred = false;
#if XLLE_VSH_DRAW_OVERLAP_CENSUS
        // Host-only census metadata copied from submit_vertex_batch locals at
        // latch. It is never consulted by emulation semantics.
        VshDrawOverlapJob vsh_overlap{};
#endif
    };

    void latch_draw_job(DrawJob& job);
    bool execute_draw_job(DrawJob& job, bool& executed);
    void retire_draw_job(DrawJob& job, bool executed);
    // Stage (e): fold a finished job's private telemetry into the members and
    // apply its deferred guest-visible side effects. Emulation thread only, in
    // latch order -- inline jobs at their own retire, queued jobs at the drain.
    void fold_draw_job(DrawJob& job);
#if XLLE_VSH_DRAW_OVERLAP_CENSUS
    void record_vsh_draw_overlap(DrawJob& job, VshDrawOverlapOutcome fallback);
#endif
    // The ONE place a job's counters are added into the members. Stage (e2)
    // calls it once per destination-span part, in part order; every field it
    // touches is either a pure sum (order-independent) or a last-write-wins cell
    // whose value is job-invariant, so the result does not depend on the part
    // order and equals the single-threaded value exactly.
    void fold_draw_telemetry(const DrawJobTelemetry& out, bool hash_on);
    void seed_draw_telemetry(DrawJob& job);
#if XLLE_VSH_GPU_RESIDENT
    bool materialize_deferred_gpu_vsh(DrawJob& job);
#endif
    void seam_note_threadable(DrawJob& job, bool threadable, uint32_t reason);
    void seam_publish_job(const DrawJob& job);
    void seam_anchor(SeamAnchor anchor, bool required);
    void seam_anchor_drain(SeamAnchor anchor);
    void seam_texture_source_anchor(uint32_t address, uint64_t bytes);

    uint64_t seam_jobs_latched_ = 0;
    uint64_t seam_jobs_threadable_ = 0;
    uint64_t seam_jobs_queueable_ = 0;
    uint64_t seam_jobs_not_executed_ = 0;
    std::array<uint64_t, kSeamNotThreadableReasons> seam_not_threadable_{};
    std::array<uint64_t, kSeamAnchorCount> seam_anchor_hits_{};
    std::array<uint64_t, kSeamAnchorCount> seam_anchor_drains_{};
    std::array<uint64_t, kSeamAnchorCount> seam_anchor_pending_sum_{};
    std::array<uint64_t, kSeamAnchorCount> seam_anchor_pending_max_{};
    // Simulated queue depth: threadable jobs published since the last drain.
    uint64_t seam_pending_ = 0;
    // Destination spans of the simulated-pending jobs, for the F6 texture
    // overlap test. Two entries per job, cleared at every drain; bounded by the
    // drain frequency, which is exactly what M5 measures.
    std::vector<std::pair<uint64_t, uint64_t>> seam_pending_spans_;
    uint64_t seam_vblank_jobs_ = 0;
    uint64_t seam_vblank_jobs_max_ = 0;
    uint64_t seam_vblank_jobs_sum_ = 0;
    uint64_t seam_vblanks_ = 0;
    uint64_t seam_resolve_transfers_ = 0;

    // ---- stage (f) residency state (beads-xbox.1.15.7) ---------------------
    // A single authoritative destination pair, mirroring the GL backend's
    // one-active-resident-surface model. address 0 == the guest copy is
    // authoritative (no GPU-side copy exists).
    GpuResidencyCensus gpu_res_{};
    uint32_t gpu_res_color_addr_ = 0;
    uint64_t gpu_res_color_bytes_ = 0;
    uint32_t gpu_res_depth_addr_ = 0;
    uint64_t gpu_res_depth_bytes_ = 0;
    uint64_t gpu_res_run_ = 0;
    // Set across the GPU-eligible job's OWN residency queries so its own
    // resolve_renderer_* calls are not counted as a CPU consumer touching the
    // surface. Emulation thread only.
    bool gpu_res_in_gpu_draw_ = false;
    // The profile of the witnessed draw currently being latched, valid only
    // while a CPU-executed witnessed draw is asking its residency questions.
    // Phase 2 attribution (beads-xbox.1.15.10).
    uint32_t gpu_res_cpu_profile_ = 0;
    // Phase 5 per-plane consumer tables (see GpuResSurfaceTable above).
    GpuResSurfaceTable gpu_res_color_surfaces_{};
    GpuResSurfaceTable gpu_res_depth_surfaces_{};
    VertexCensus vertex_census_{};
    VshProgramCensus vsh_program_census_{};
    bool vertex_census_enabled_ = false;
#if XLLE_VERTEX_REUSE_DIAGNOSTICS
    std::unique_ptr<Nv2aVertexReuseShadow> vertex_reuse_shadow_;
    VertexReuseShadowCensus vertex_reuse_shadow_census_{};
    bool vertex_reuse_shadow_timing_ = false;
#endif
    bool vertex_fast_fetch_ = true;
    ResolveQueryCensus resolve_query_census_{};
    // Set for the duration of one witnessed texture-snapshot residency query so
    // the census can attribute that call site apart from every other consumer.
    bool resolve_query_from_texture_source_ = false;
    // beads-xbox.1.15.10 phase 6: the generic-combiner shader census (above).
    GenericShaderCensus generic_shader_census_{};
    // Fingerprint of exactly the words a generated fragment program is a
    // function of, plus the row it selected, for the batch currently being
    // decoded. Reset per batch.
    GenericShaderCensusRow* generic_shader_row_ = nullptr;
    uint64_t generic_batch_texture_resolves_ = 0;
    size_t generic_shader_dest_slot_ = 0;
    bool gpu_generic_shader_ = false;
    uint64_t gpu_generic_shader_mask_ = ~uint64_t{0};
    bool gpu_generic_shader_fp64_ = true;
    uint32_t gpu_generic_shader_wide_pixels_ = 4096;
    bool gpu_generic_shader_image_blend_ = true;
    bool gpu_generic_shader_depth_ = false;
    bool gpu_generic_shader_bgra_upload_ = true;
    bool gpu_generic_texture_cache_ = true; // Exact post-resolve snapshots; measured default.
    bool gpu_generic_texture_trace_ = false; // Explicit FNV trace; perturbs cache timing.
    Nv2aTextureConversionCache generic_texture_conversion_cache_;
    bool dxt_decode_cache_enabled_ = false; // Donor reuse candidate; real decode opt-out.
    Nv2aTextureConversionCache dxt_decode_cache_;
    // PFIFO run batching (beads-xbox.1.15.1 stages (c)/(d)). Both default ON;
    // the negative spellings restore the per-word pusher and the per-word
    // handler call respectively, on the same binary.
    bool pfifo_run_batching_ = true;
    bool pfifo_run_bulk_ = true;
    bool pfifo_direct_fetch_ = true;
    uint64_t pfifo_runs_batched_ = 0;
    uint64_t pfifo_words_batched_ = 0;
    uint64_t pfifo_runs_bulk_ = 0;
    uint64_t pfifo_words_bulk_ = 0;
    uint64_t pfifo_words_per_word_ = 0;
    uint64_t pfifo_run_declines_ = 0;
    uint64_t pfifo_decline_queue_ = 0;
    uint64_t pfifo_decline_stall_ = 0;
    uint64_t pfifo_decline_dma_ = 0;
    uint64_t pfifo_decline_direct_ = 0;
    uint32_t gpu_generic_shader_witnessed_ = 0u;
    uint64_t generic_shader_witnessed_reroutes_ = 0;
    uint32_t gpu_generic_shader_verify_ = 0;
    uint32_t gpu_generic_shader_verified_ = 0;
    std::vector<uint8_t> generic_shader_verify_before_;
    std::vector<uint8_t> generic_shader_verify_soft_;
    std::vector<uint8_t> generic_shader_verify_gpu_;
    static constexpr size_t kGenericShaderProgramSlots = 64;
    std::array<uint64_t, kGenericShaderProgramSlots> generic_shader_seen_fp_{};
    std::array<uint64_t, kGenericShaderProgramSlots>
        generic_shader_seen_batches_{};
    size_t generic_shader_seen_count_ = 0;
    // Index of a generated program in first-attempt order, assigning one on
    // first sight. Returns kGenericShaderProgramSlots when the table is full,
    // which the caller treats as "not selectable" rather than evicting an
    // index a mask already refers to.
    size_t generic_shader_program_index(uint64_t fingerprint) {
        for (size_t i = 0; i < generic_shader_seen_count_; ++i) {
            if (generic_shader_seen_fp_[i] == fingerprint) {
                ++generic_shader_seen_batches_[i];
                return i;
            }
        }
        if (generic_shader_seen_count_ >= kGenericShaderProgramSlots)
            return kGenericShaderProgramSlots;
        const size_t index = generic_shader_seen_count_++;
        generic_shader_seen_fp_[index] = fingerprint;
        generic_shader_seen_batches_[index] = 1;
        return index;
    }
    uint64_t generic_shader_attempts_ = 0;
    uint64_t generic_shader_submits_ = 0;
    uint64_t generic_shader_empty_batches_ = 0;
    uint64_t generic_shader_triangles_ = 0;
    uint64_t generic_shader_color_upload_bytes_ = 0;
    uint64_t generic_shader_depth_upload_bytes_ = 0;
    uint64_t generic_shader_texture_upload_bytes_ = 0;
    std::array<uint64_t, 32> generic_shader_declines_{};
    void record_generic_shader_decline(GenericShaderDeclineReason reason) {
        const size_t index = static_cast<size_t>(reason);
        if (index < generic_shader_declines_.size())
            ++generic_shader_declines_[index];
    }
    GenericShaderCensusRow* generic_shader_census_row(uint64_t fingerprint);
    void gpu_res_note_surface_consumer(GpuResSurfaceTable& table,
                                       uint32_t address, uint64_t bytes,
                                       uint32_t profile, bool on_gpu);
    void gpu_res_note_gpu_draw(uint32_t color_addr, uint64_t color_bytes,
                               uint32_t depth_addr, uint64_t depth_bytes);
    void gpu_res_end_run();
    void gpu_res_cpu_color_access(uint32_t address, uint64_t bytes,
                                  uint32_t cause);
    void gpu_res_cpu_depth_access(uint32_t address, uint64_t bytes,
                                  uint32_t cause);
    static uint32_t gpu_res_cause_from_context(const char* context);

    // ---- stage (f) GPU executor (beads-xbox.1.15.7) ------------------------
    bool gpu_profiles_enabled_ = false;
    bool gpu_clear_enabled_ = true;
    // Phase 3. Off restores the pre-change behaviour -- snapshot the whole
    // destination pair even for a packet whose triangle list is empty, which
    // the backend discards. Exists so the change is bisectable on ONE binary;
    // it cannot alter output either way, only how much guest memory is read.
    bool gpu_skip_empty_packets_ = true;
    // Reused across submits so the triangle array and the texture staging keep
    // their capacity; a fresh packet per draw would allocate ~50 triangles'
    // worth of vector every batch.
    Nv2aWitnessedProfilePacket gpu_packet_;
    uint64_t gpu_profile_jobs_ = 0;
    uint64_t gpu_profile_triangles_ = 0;
    uint64_t gpu_profile_draw_calls_ = 0;
    uint64_t gpu_profile_declined_ = 0;
    uint64_t gpu_profile_uploads_ = 0;
    // Phase 3: the pair count above is a residency BREAK; these two split it by
    // plane, which is what says whether the break was colour, zeta or both.
    uint64_t gpu_profile_color_uploads_ = 0;
    uint64_t gpu_profile_depth_uploads_ = 0;
    // Eligible jobs whose triangle list came out empty. The backend discards
    // them without touching GL, so their destination is never snapshotted.
    uint64_t gpu_profile_empty_packets_ = 0;
    // Step 3 census. A CLEAR_SURFACE that both planes' geometry made eligible
    // and the backend accepted becomes a glClear on the resident texture pair:
    // no resolve, no guest-memory fill, no invalidation, no re-upload. The
    // decline counters say which half of the predicate refused, so a workload
    // where this never fires is diagnosable rather than merely silent.
    uint64_t gpu_clear_accepted_ = 0;
    uint64_t gpu_clear_decline_mask_ = 0;      // partial write mask
    uint64_t gpu_clear_decline_geometry_ = 0;  // rect is not the whole plane
    uint64_t gpu_clear_decline_planes_ = 0;    // not both planes in one call
    uint64_t gpu_clear_decline_resident_ = 0;  // backend has no such pair
    uint64_t gpu_clear_bytes_ = 0;
    uint64_t gpu_clear_kept_clean_ = 0;
    uint64_t gpu_profile_upload_bytes_ = 0;
    uint64_t gpu_profile_texture_upload_bytes_ = 0;
    mutable uint64_t gpu_profile_max_fixed_ = 0;
    // Slot 0 = kSwizzledRadialColor, 1 = kPitchTexturedColor,
    // 2 = kPitchProjectedColor (beads-xbox.1.15.10 phase 4).
    std::array<uint32_t, kGpuProfileSurfaceSlots> gpu_profile_color_addr_{};
    std::array<uint32_t, kGpuProfileSurfaceSlots> gpu_profile_depth_addr_{};
    std::array<uint32_t, kGpuProfileSurfaceSlots> gpu_profile_surface_bytes_{};
    bool gpu_relaxed_coords_ = false;
    // Phase 2: the 2^24 bound is now opt-in, not the default. See the block
    // comment on kGpuFixedCoordinateLimitExact.
    bool gpu_exact_coords_ = false;
    // Bit 0 radial, bit 1 pitch-textured, bit 2 pitch-projected. The host
    // flag (--nv2a-gpu-profile-mask) is what actually sets it; this is the
    // value a device constructed without one carries.
    uint32_t gpu_profile_mask_ = 7;
    // beads-xbox.1.15.10 phase 4 (lever B) ceiling probe. DIAGNOSTIC ONLY and
    // known incorrect when on; see the call site in rasterize_witnessed_batch.
    bool gpu_unsafe_skip_texture_resolve_ = false;
    uint64_t witnessed_texture_source_resolves_ = 0;
    uint64_t witnessed_texture_source_skipped_ = 0;
    bool gpu_vblank_resolve_ = false;
    bool gpu_vblank_kick_ = true;
    // Always-on reject census: WHY a job that carries one of the two GPU
    // profiles was not taken. Without this the residency runs get shattered by
    // a handful of jobs and there is no way to tell which clause did it.
    mutable std::array<uint64_t, kGpuRejectReasons> gpu_profile_reject_{};
    bool gpu_profile_state_ok(const DrawJob& job) const;
    bool gpu_profile_non_finite_reaches_raster(const DrawJob& job,
                                               bool textured,
                                               bool projected) const;
    bool gpu_profile_submit(DrawJob& job, bool& submitted);

    // ---------------- stage (e): the bounded in-order job queue --------------
    // Single producer (the emulation thread), single consumer (one worker), and
    // a third cursor -- draw_retired_ -- owned by the emulation thread, because a
    // finished job still has to be FOLDED in order: its telemetry added to the
    // members and its deferred renderer invalidation applied (F3/F14). A slot is
    // free only once it has been folded, so free = kDrawQueueSlots -
    // (draw_published_ - draw_retired_).
    //
    //   emulation thread            worker
    //   ----------------            ------
    //   publish -> draw_published_  reads draw_published_ (acquire)
    //                               executes slot, then draw_executed_ (release)
    //   drain: wait for draw_executed_ == draw_published_, then fold
    //          draw_retired_ .. draw_published_ in order
    //
    // The hot-path fence is one relaxed load of draw_published_ against a plain
    // emulation-thread integer; the mutex is only entered to sleep or to wake.
    unsigned draw_threads_ = 0;
    std::vector<DrawJob> draw_slots_;
    uint64_t draw_published_ = 0;    // emulation thread only (producer cursor)
    uint64_t draw_retired_ = 0;      // emulation thread only (fold cursor)
    std::atomic<uint64_t> draw_published_pub_{0};  // published_ visible to the worker
    std::atomic<uint64_t> draw_executed_{0};       // worker's completion counter
    std::atomic<bool> draw_stop_{false};
    std::atomic<bool> draw_worker_sleeping_{false};
    std::atomic<bool> draw_emu_waiting_{false};
    std::mutex draw_mutex_;
    std::condition_variable draw_work_cv_;
    std::condition_variable draw_done_cv_;
    std::thread draw_worker_;
    std::atomic<uint64_t> draw_worker_busy_ticks_{0};
    // Recycled transform buffers. At the latch the job MOVES transformed_batch_
    // out; an inline job hands it straight back at retire, but a queued job keeps
    // it until the drain, so the producer takes a spare from here instead of
    // allocating a fresh vector for the next batch.
    std::vector<std::vector<TransformedVertex>> draw_free_vertex_buffers_;
    static constexpr size_t kDrawFreeVertexBuffers = 64;

    uint64_t draw_jobs_queued_ = 0;
    uint64_t draw_jobs_inline_ = 0;
    uint64_t draw_backpressure_inline_ = 0;
    uint64_t draw_backpressure_bytes_ = 0;
    uint64_t draw_order_fences_ = 0;
    // Times a renderer residency query found the ring non-empty and had to
    // drain first. This is the ALWAYS-ON evidence for the F3 argument: while it
    // stays 0 the query result provably could not have depended on a pending
    // job, and a non-zero value is the staleness window actually occurring.
    uint64_t draw_renderer_query_fences_ = 0;
    uint64_t draw_queue_bytes_ = 0;
    uint64_t draw_queue_bytes_max_ = 0;
    uint64_t draw_drains_empty_ = 0;
    uint64_t draw_drains_nonempty_ = 0;
    uint64_t draw_depth_sum_ = 0;
    uint64_t draw_depth_samples_ = 0;
    uint64_t draw_depth_max_ = 0;
    std::array<uint64_t, kDrawDepthBuckets> draw_depth_hist_{};
    uint64_t draw_drain_wait_ticks_ = 0;
    uint64_t draw_worker_failures_ = 0;
    uint64_t draw_epoch_ticks_ = 0;

    // ---------- stage (e2): the destination-span worker pool -----------------
    // beads-xbox.1.15.9. N >= 2 only. There is no ring here and no queue depth:
    // exactly one job is in flight at a time and every worker is inside it, so
    // draw order is preserved by construction and the ten coherence anchors are
    // satisfied trivially (nothing is ever outstanding when they are reached).
    //
    //   emulation thread (part 0)          helper i (parts 1..N-1)
    //   -------------------------          -----------------------
    //   span_job_ = &job; span_parts_ = N
    //   span_gen_++ under the mutex  --->  wakes, sees a new generation
    //   execute part 0 itself              executes part i into span_out_[i]
    //   wait for span_done_ == N-1   <---  span_done_.fetch_add(1)
    //   merge the four scalars, fold
    //
    // The generation counter is published UNDER the mutex on purpose: ndsrecomp
    // commit 0206ba3 recorded a lost-wakeup window where the producer stored the
    // head and notified without the lock, and a worker between its predicate
    // check and its wait slept with work queued. Correctness survives that (the
    // barrier still completes, the helper just runs late) but the feature
    // silently degrades to single-threaded plus overhead.
    std::vector<std::thread> span_workers_;
    // Sub-accumulators for parts 1..N-1. Index 0 is unused: part 0 accumulates
    // straight into job.out. Owned by the device, not by the job, because a
    // span-parallel job never enters the ring -- only one is ever live.
    std::vector<DrawJobTelemetry> span_out_;
    DrawJob* span_job_ = nullptr;
    unsigned span_parts_ = 1;
    std::atomic<uint64_t> span_gen_{0};
    std::atomic<unsigned> span_done_{0};
    std::atomic<bool> span_stop_{false};
    std::atomic<unsigned> span_sleepers_{0};
    std::mutex span_mutex_;
    std::condition_variable span_work_cv_;
    std::condition_variable span_done_cv_;
    std::atomic<bool> span_emu_waiting_{false};
    std::atomic<uint64_t> span_helper_busy_ticks_{0};
    uint64_t draw_span_jobs_ = 0;
    uint64_t draw_span_inline_jobs_ = 0;
    uint64_t draw_span_barrier_wait_ticks_ = 0;
    uint64_t draw_span_part0_busy_ticks_ = 0;
    uint64_t draw_span_fold_ticks_ = 0;

    // Execute one job across draw_threads_ destination-span parts and barrier.
    // Emulation thread only. Returns the AND of every part's ok.
    bool execute_draw_spans(DrawJob& job);
    void span_worker_main(unsigned part);
    void span_pool_start(unsigned parts);
    void span_pool_stop();

    static uint64_t draw_job_bytes(const DrawJob& job);
    void draw_queue_publish(DrawJob& job);
    void draw_queue_drain();
    // Ordering fence. Anything that executes pixels on the EMULATION thread while
    // a worker owns queued jobs must drain first, or it overtakes jobs that
    // precede it in guest order. Three such paths exist: the immediate-quad
    // family and the generic-combiner GL path (both structurally inline, F3 and
    // section 1.3) and a witnessed job whose destination has no mem_direct_
    // pointer. Same-surface draws are order-dependent, so this is correctness,
    // not tidiness -- see beads-xbox.1.15.5 for the byte-lock that caught it.
    void draw_order_fence();
    // F3 CLOSURE (beads-xbox.1.15.5 "RESIDUAL RISK", closed 2026-09-05). A
    // renderer RESIDENCY QUERY must not observe a resident set that a queued
    // job's invalidate_renderer_*_overlap has not been applied to yet: the
    // executor only RECORDS its invalidations (DrawJobTelemetry::invalidate_*)
    // and fold_draw_job applies them at the drain, so between publish and drain
    // the renderer can still claim to own bytes a pending job is overwriting.
    // Resolving those bytes back into guest RAM afterwards would clobber the
    // software output. Same shape as draw_order_fence: two integer compares
    // when nothing is outstanding (always, at N == 0 and at N >= 2, where a
    // span job never enters the ring), a full drain when something is.
    void draw_renderer_query_fence();
    void draw_worker_main();

    // Display/scanout state. PRMCIO is the memory-mapped VGA register alias; AvSetDisplayMode
    // programs CRTC pitch/depth there, PRAMDAC display extents, then PCRTC_START.
    std::array<uint8_t, 256> crtc_regs_{};
    uint8_t crtc_index_ = 0;
    uint8_t sequencer_index_ = 0;
    uint8_t sequencer_clock_ = 1; // existing eight-dot digital scanout profile
    bool sequencer_clock_programmed_ = false;
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
    // beads-xbox.1.15.1 stage (c): dispatch a RUN of method data words in one
    // pass instead of one CACHE1 round trip per word. Returns the number of
    // words consumed; 0 means the run was declined and the caller must use the
    // per-word path. See the definition in nv2a.cpp for the preconditions and
    // for why the result is identical word for word.
    uint32_t drain_method_run(ChannelState& ch, uint8_t channel, uint32_t max_words);
    // beads-xbox.1.15.1 stage (d): one resolved host window over the pushbuffer
    // per kick, instead of a std::function hop into uc_mem_read for every four
    // bytes. Returns nullptr when the span is not directly addressable, in which
    // case the caller falls back to mem_read_.
    const uint8_t* pushbuffer_host_bytes(uint32_t addr, uint32_t bytes);
    // The window, and the guest range it covers. Reset at the top of every
    // process_pushbuffer call so a host pointer never outlives the invocation
    // that produced it.
    const uint8_t* push_win_ = nullptr;
    uint64_t push_win_lo_ = 0;
    uint64_t push_win_hi_ = 0;
    void resume_pushbuffers();
    void dispatch_method(PushMethod push);
    bool bind_object(uint8_t channel, uint8_t subchannel, uint32_t handle);
    bool resolve_object(uint8_t channel, uint32_t handle, uint32_t& instance);
    bool clear_surface(uint32_t parameter);
    bool clear_surface_plane(uint32_t dma_instance, bool dma_bound,
                             uint32_t surface_offset, uint32_t pitch,
                             uint32_t bytes_per_pixel, uint32_t write_mask,
                             uint32_t clear_value, const char* plane,
                             uint32_t& first_address, bool skip_memory,
                             bool skip_invalidate);
    // beads-xbox.1.15.10 phase 3, step 3. The SAME address arithmetic
    // clear_surface_plane performs, with none of its side effects and none of
    // its fault_ calls: it answers "where would this plane's clear land, and
    // does it cover every byte of a whole pitch-linear plane", so the resident
    // clear can be attempted before anything has been written. Returning false
    // means "not eligible", never "malformed" -- the software path runs next
    // and raises whatever fault the input deserves, in the original order.
    bool clear_plane_extent(uint32_t dma_instance, bool dma_bound,
                            uint32_t surface_offset, uint32_t pitch,
                            uint32_t bytes_per_pixel, uint32_t write_mask,
                            uint32_t& first_address, size_t& span) const;
    bool set_vertex_array_format(uint32_t slot, uint32_t parameter);
    bool commit_immediate_vertex();
    bool decode_inline_array();
    bool submit_vertex_batch();
#if XLLE_RENDERER_HLE
    bool submit_hle_vertex_batch();
#endif
    bool rasterize_witnessed_immediate_quad(DrawJob& job, bool& executed);
    bool rasterize_witnessed_batch(DrawJob& job, bool& executed);
    // The pure execute half of the witnessed path (stage (e)). Runs on the
    // emulation thread when draw_threads_ == 0 and on the worker otherwise; it
    // is the ONE implementation of the fragment loop in every mode.
    //
    // Stage (e2), beads-xbox.1.15.9: `part`/`parts` select a disjoint subset of
    // the DESTINATION SCANLINES -- part p renders exactly the rows y with
    // y % parts == p. parts == 1 is the whole surface and is what every inline
    // and every queued job passes. `out` is the caller's accumulator: job.out
    // for part 0, span_out_[p] for the helper parts.
    //
    // WHAT IS NOT PER-PIXEL, and how each item is handled (the enumeration the
    // exactness argument rests on; the guard test pins it):
    //   * per-triangle setup (fixed-point vertices, edge functions, area, the
    //     1/w and 1/area reciprocals, vertex_diffuse_clamped) -- recomputed
    //     identically and independently by every part from the same latched
    //     vertices. Duplicated work, not shared state. Measured cost: about
    //     563 fragments per triangle on radial and 585 on pitch-textured, so
    //     the setup is a low-single-digit percentage of a triangle.
    //   * texture sampling and the swizzle tables -- READ-ONLY. SwizzleLookup
    //     is immutable after construction (nv2a.hpp:48) and the texture
    //     snapshots are owned const byte vectors in the job.
    //   * the ~110 batch counters -- pure sums in a job-local (now part-local)
    //     accumulator, summed into the members by fold_draw_telemetry once per
    //     part. Addition is associative and commutative, so the total does not
    //     depend on the partition.
    //   * the 14 per-write FNV-1a chains -- order-dependent, and therefore
    //     hash_on forces the job INLINE (parts == 1) exactly as stage (e) did.
    //     A span-parallel job never folds a hash.
    //   * the 17 last-write-wins uint32 scalars -- fourteen of them are written
    //     ONLY inside `if (hash_on)` blocks and so are unreachable from a
    //     span-parallel job. The other three
    //     (last_pitch_three_zero_factor3_, last_pitch_three_xy_tie_factor3_,
    //     last_pitch_three_rescale_factor3_) are ungated, and every one of them
    //     assigns raster_state[kRasterCombinerFactor0_0 + 3] -- a JOB-INVARIANT
    //     constant. Every part that writes one writes the same value, so the
    //     fold order is irrelevant and the result is the single-threaded value.
    //     Nothing is dropped and nothing is declared n/a.
    //   * the destination pixels themselves -- color and depth are
    //     read-modify-write of the fragment's OWN pixel only. Disjoint rows map
    //     to disjoint destination offsets under both address maps: the pitch map
    //     is y*pitch + x*bpp, and the swizzled map is a bit-interleave of x and
    //     y, which is injective, so distinct (x,y) give distinct words.
    //   * the two PagedRasterSurface objects -- constructed per part, so their
    //     written-span and page-cache state is never shared. A span-parallel job
    //     has both mem_direct_ pointers by the section 1.3 eligibility rule, so
    //     the paged fallback (and therefore uc_mem_read/uc_mem_write) is
    //     unreachable from a helper thread.
    //   * the once-per-batch effects -- the two batch-counter ladders, the F15
    //     surface_write_debug_cb_ record and the F3 renderer invalidations run
    //     under `part == 0` so they happen exactly once per draw.
    //   * `raster_failed` -- part-local. A data-dependent fault (non-finite
    //     varying, zero texture W, invalid projection) aborts only the part that
    //     hit it, so the set of pixels written before a fault is
    //     partition-dependent. That run is a hard stop under the methodology
    //     gate anyway; the rule, as for F13, is that any faulting threaded run
    //     is re-run with --nv2a-draw-threads 0 for true attribution.
    bool execute_witnessed_fragments(DrawJob& job, DrawJobTelemetry& out,
                                     unsigned part, unsigned parts);
    bool rasterize_generic_combiner_batch(DrawJob& job, bool& executed);
    void record_generic_skip(GenericSkipReason reason);
    void record_generic_skip_detail(GenericSkipReason reason, uint64_t detail);
    void record_generic_resident_reject(GenericResidentRejectReason reason);
    void record_generic_texture_modulate_reject(
        GenericTextureModulateRejectReason reason);
    bool fetch_vertex_attribute(uint32_t slot, uint32_t index);
    bool ramht_instance(uint8_t channel, uint32_t handle, uint32_t& instance,
                        uint32_t* engine = nullptr) const;
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
