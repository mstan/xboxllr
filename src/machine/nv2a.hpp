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
        kRasterStateWords,
    };
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
    std::vector<RasterStateSample> raster_state_samples_;
    struct TransformedVertex {
        std::array<float, 4> position{};
        std::array<float, 4> diffuse{};
        std::array<float, 4> specular{};
        std::array<std::array<float, 4>, 4> texture{};
    };
    std::vector<TransformedVertex> transformed_batch_;

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
    void dispatch_method(PushMethod push);
    bool bind_object(uint8_t channel, uint8_t subchannel, uint32_t handle);
    bool resolve_object(uint8_t channel, uint32_t handle, uint32_t& instance);
    bool clear_surface(uint32_t parameter);
    bool clear_surface_plane(uint32_t dma_instance, bool dma_bound,
                             uint32_t surface_offset, uint32_t pitch,
                             uint32_t bytes_per_pixel, uint32_t write_mask,
                             uint32_t clear_value, const char* plane,
                             uint32_t& first_address);
    bool set_vertex_array_format(uint32_t slot, uint32_t parameter);
    bool commit_immediate_vertex();
    bool decode_inline_array();
    bool submit_vertex_batch();
    bool rasterize_witnessed_immediate_quad(bool& executed);
    bool rasterize_witnessed_batch(bool& executed);
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
