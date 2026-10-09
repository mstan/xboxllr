// xlle — NV2A MMIO register shell. See nv2a.hpp for the value policy.
#include "machine/nv2a.hpp"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <optional>
#include <type_traits>

#include "machine/nv2a_dispatch_micro.hpp"
#include "machine/perf_accounting.hpp"
#include "machine/perf_excl.hpp"
#include "nv2a_vsh_emulator.h"

namespace xlle {

namespace {

// ---- the pre-decoded vertex-shader step form (beads-xbox.1.15.11) ----------
//
// This table is a TRANSCRIPTION of `kDispatchTable` in the vendored
// nv2a_vsh_emulator.c, in the same order, naming the same functions. It exists
// so a compiled record can hold the implementation POINTER directly instead of
// an opcode the executor would have to switch on; it deliberately does NOT
// reimplement any operation, so every IEEE detail and every NV2A special case
// is the same object code the interpreter calls.
const VshCompiledOp::Fn kVshDispatchTable[] = {
    nullptr,          nv2a_vsh_cpu_mov, nv2a_vsh_cpu_mul, nv2a_vsh_cpu_add,
    nv2a_vsh_cpu_mad, nv2a_vsh_cpu_dp3, nv2a_vsh_cpu_dph, nv2a_vsh_cpu_dp4,
    nv2a_vsh_cpu_dst, nv2a_vsh_cpu_min, nv2a_vsh_cpu_max, nv2a_vsh_cpu_slt,
    nv2a_vsh_cpu_sge, nv2a_vsh_cpu_arl, nv2a_vsh_cpu_rcp, nv2a_vsh_cpu_rcc,
    nv2a_vsh_cpu_rsq, nv2a_vsh_cpu_exp, nv2a_vsh_cpu_log, nv2a_vsh_cpu_lit,
};
static_assert(sizeof(kVshDispatchTable) / sizeof(kVshDispatchTable[0]) ==
                  static_cast<size_t>(NV2AOP_LIT) + 1,
              "the compiled dispatch table must cover exactly the vendored "
              "Nv2aVshOpcode enum, in its order");

#if XLLE_VSH_DRAW_OVERLAP_CENSUS
uint64_t vsh_draw_overlap_triangle_candidates(uint32_t primitive_mode,
                                              size_t vertex_count) {
    if (primitive_mode == 5) return static_cast<uint64_t>(vertex_count / 3u);
    if (primitive_mode == 6 || primitive_mode == 7)
        return vertex_count >= 3u ? static_cast<uint64_t>(vertex_count - 2u) : 0;
    return 0;
}
#endif

// Resolve ONE operand. Returns false -- declining the whole program -- for any
// index the vendored code only guards with an assert, because with NDEBUG the
// interpreter would read out of bounds there and the compiled form must not
// invent a different wrong answer. Declining is safe: the interpreter runs.
bool compile_vsh_input(const Nv2aVshInput& input, VshCompiledOp& op,
                       unsigned slot) {
    switch (input.type) {
        case NV2ART_TEMPORARY:
            if (input.index == 12) {
                // r12 aliases the output register file; `fetch_value` reads
                // output_regs from element 0, not from element 12 * 4.
                op.in_base[slot] = kVshBaseOutput;
                op.in_offset[slot] = 0;
            } else if (input.index < 12) {
                op.in_base[slot] = kVshBaseTemp;
                op.in_offset[slot] =
                    static_cast<uint16_t>(input.index * 4u);
            } else {
                return false;  // r13-r15: an out-of-bounds read either way
            }
            break;
        case NV2ART_INPUT:
            if (input.index >= 16) return false;
            op.in_base[slot] = kVshBaseInput;
            op.in_offset[slot] = static_cast<uint16_t>(input.index * 4u);
            break;
        case NV2ART_CONTEXT:
            op.in_base[slot] = kVshBaseContext;
            if (input.is_relative) {
                // The final offset depends on a0 and is computed per fetch with
                // the interpreter's own uint32_t arithmetic, so the RAW index
                // is what the record carries.
                op.in_relative_mask |= static_cast<uint8_t>(1u << slot);
                op.in_offset[slot] = static_cast<uint16_t>(input.index);
            } else {
                if (input.index >= 192) return false;
                op.in_offset[slot] = static_cast<uint16_t>(input.index * 4u);
            }
            break;
        default:
            return false;
    }
    if (input.is_negated) op.in_negate_mask |= static_cast<uint8_t>(1u << slot);
    op.in_swizzle[slot] = static_cast<uint8_t>(
        (input.swizzle[0] & 3u) | ((input.swizzle[1] & 3u) << 2) |
        ((input.swizzle[2] & 3u) << 4) | ((input.swizzle[3] & 3u) << 6));
    return true;
}

// Resolve ONE destination half. A slot that provably stores nothing -- unused,
// a write to an input register (which the vendored code falls through to
// `continue` for once asserts are off), or a zero vector writemask -- becomes
// kVshBaseNone. ARL is a scalar address store despite its decoded zero mask.
bool compile_vsh_output(const Nv2aVshOutput& output, VshCompiledOp& op,
                        unsigned slot) {
    op.out_base[slot] = kVshBaseNone;
    op.out_mask[slot] = 0;
    op.out_offset[slot] = 0;
    if (output.type == NV2ART_NONE || output.type == NV2ART_INPUT) return true;
    const unsigned mask = output.type == NV2ART_ADDRESS ? NV2AWM_X
        : static_cast<unsigned>(output.writemask) & 0xFu;
    if (!mask) return true;
    switch (output.type) {
        case NV2ART_OUTPUT:
            if (output.index >= 13) return false;
            op.out_base[slot] = kVshBaseOutput;
            op.out_offset[slot] = static_cast<uint16_t>(output.index * 4u);
            break;
        case NV2ART_TEMPORARY:
            if (output.index >= 12) return false;
            op.out_base[slot] = kVshBaseTemp;
            op.out_offset[slot] = static_cast<uint16_t>(output.index * 4u);
            break;
        case NV2ART_CONTEXT:
            if (output.index >= 192) return false;
            op.out_base[slot] = kVshBaseContext;
            op.out_offset[slot] = static_cast<uint16_t>(output.index * 4u);
            break;
        case NV2ART_ADDRESS:
            op.out_base[slot] = kVshBaseAddress;
            op.out_offset[slot] = 0;
            break;
        default:
            return false;
    }
    op.out_mask[slot] = static_cast<uint8_t>(mask);
    return true;
}

bool compile_vsh_operation(const Nv2aVshOperation& operation,
                           VshCompiledOp& op) {
    const size_t opcode = static_cast<size_t>(operation.opcode);
    if (opcode == 0 ||
        opcode >= sizeof(kVshDispatchTable) / sizeof(kVshDispatchTable[0]))
        return false;
    op.fn = kVshDispatchTable[opcode];
    if (!op.fn) return false;
    // `prepare_inputs` stops at the first NV2ART_NONE and leaves the remaining
    // operand block uninitialised; the arity recorded here is exactly that
    // prefix, and no operation reads past its own arity.
    unsigned count = 0;
    for (unsigned k = 0; k < 3; ++k) {
        if (operation.inputs[k].type == NV2ART_NONE) break;
        if (!compile_vsh_input(operation.inputs[k], op, k)) return false;
        ++count;
    }
    op.input_count = static_cast<uint8_t>(count);
    for (unsigned o = 0; o < 2; ++o)
        if (!compile_vsh_output(operation.outputs[o], op, o)) return false;
    return true;
}

bool vsh_program_writes_context(const Nv2aVshProgram& program) {
    for (const Nv2aVshStep* step = program.steps; step; ++step) {
        for (const Nv2aVshOutput& output : step->mac.outputs) {
            if (output.type == NV2ART_CONTEXT) return true;
        }
        for (const Nv2aVshOutput& output : step->ilu.outputs) {
            if (output.type == NV2ART_CONTEXT) return true;
        }
        if (step->is_final) return false;
    }
    return true;
}

// A draw must see memory written by earlier draws, clears, and the CPU, but reading an
// entire multi-megabyte swizzled target for every small dashboard particle is needlessly
// expensive. Keep coherence at the draw boundary while faulting pages in only when covered.
class PagedRasterSurface {
public:
    using ReadFn = std::function<bool(uint32_t, void*, size_t)>;
    using WriteFn = std::function<bool(uint32_t, const void*, size_t)>;
    using DirectFn = std::function<uint8_t*(uint32_t, size_t)>;

    PagedRasterSurface(uint32_t address, size_t size,
                       const ReadFn& read, const WriteFn& write,
                       const DirectFn& direct = DirectFn{})
        : address_(address), size_(size), read_(read), write_(write),
          direct_(direct), direct_base_(direct_ ? direct_(address, size) : nullptr),
          pages_(direct_base_ ? 0 : (size + kPageSize - 1) / kPageSize) {
        perf::record_paged_surface(size_);
        if (direct_base_) perf::record_paged_surface_direct(size_);
    }

    ~PagedRasterSurface() {
        perf::release_paged_surface_bytes(resident_bytes_);
    }

    PagedRasterSurface(const PagedRasterSurface&) = delete;
    PagedRasterSurface& operator=(const PagedRasterSurface&) = delete;
    PagedRasterSurface(PagedRasterSurface&&) = delete;
    PagedRasterSurface& operator=(PagedRasterSurface&&) = delete;

    bool read16(size_t offset, uint16_t& value) {
        return access(offset, value, false);
    }
    bool read8(size_t offset, uint8_t& value) {
        return access(offset, value, false);
    }
    bool read32(size_t offset, uint32_t& value) {
        return access(offset, value, false);
    }
    bool write16(size_t offset, uint16_t value) {
        return access(offset, value, true);
    }
    bool write32(size_t offset, uint32_t value) {
        return access(offset, value, true);
    }
    uint8_t* direct_data() const { return direct_base_; }

    // Written-byte span since the last reset_written_span(), [begin, end).
    // Callers use this to push only the covered rows back into a resident
    // renderer surface after a software batch writes guest memory directly.
    void reset_written_span() {
        written_begin_ = 0;
        written_end_ = 0;
    }
    size_t written_begin() const { return written_begin_; }
    size_t written_end() const { return written_end_; }

    bool flush() {
        if (direct_base_) return true;
        if (!write_) return false;
        for (size_t page_index = 0; page_index < pages_.size(); ++page_index) {
            Page& page = pages_[page_index];
            if (!page.dirty) continue;
            const size_t page_offset = page_index * kPageSize;
            if (!write_(address_ + static_cast<uint32_t>(page_offset),
                        page.bytes.data(), page.bytes.size())) {
                return false;
            }
            page.dirty = false;
        }
        return true;
    }

private:
    static constexpr size_t kPageSize = 4096;
    struct Page {
        std::vector<uint8_t> bytes;
        bool dirty = false;
    };

    template <typename T>
    bool access(size_t offset, T& value, bool write) {
        if (offset > size_ || sizeof(T) > size_ - offset) return false;
        if (write) {
            if (written_end_ == 0 || written_begin_ == written_end_) {
                // First recorded write of the span.
                written_begin_ = offset;
                written_end_ = offset + sizeof(T);
            } else {
                if (offset < written_begin_) written_begin_ = offset;
                const size_t end = offset + sizeof(T);
                if (end > written_end_) written_end_ = end;
            }
        }
        if (direct_base_) {
            if (write) {
                std::memcpy(direct_base_ + offset, &value, sizeof(T));
            } else {
                std::memcpy(&value, direct_base_ + offset, sizeof(T));
            }
            return true;
        }
        const size_t page_index = offset / kPageSize;
        const size_t in_page = offset % kPageSize;
        Page& page = pages_[page_index];
        if (page.bytes.empty()) {
            const size_t page_offset = page_index * kPageSize;
            const size_t page_size = std::min(kPageSize, size_ - page_offset);
            page.bytes.resize(page_size);
            if (!read_ ||
                !read_(address_ + static_cast<uint32_t>(page_offset),
                       page.bytes.data(), page.bytes.size())) {
                page.bytes.clear();
                return false;
            }
            resident_bytes_ += page.bytes.size();
            perf::record_paged_surface_page(page.bytes.size());
        }
        if (sizeof(T) > page.bytes.size() - in_page) return false;
        if (write) {
            std::memcpy(page.bytes.data() + in_page, &value, sizeof(T));
            page.dirty = true;
        } else {
            std::memcpy(&value, page.bytes.data() + in_page, sizeof(T));
        }
        return true;
    }

    uint32_t address_;
    size_t size_;
    const ReadFn& read_;
    const WriteFn& write_;
    const DirectFn& direct_;
    uint8_t* direct_base_ = nullptr;
    std::vector<Page> pages_;
    size_t resident_bytes_ = 0;
    size_t written_begin_ = 0;
    size_t written_end_ = 0;
};

// SwizzleLookup moved to nv2a.hpp (stage (e), beads-xbox.1.15.5): a DrawJob owns
// its swizzle tables because a worker thread reads them through the job.

// The witnessed profile tag. It used to be declared INSIDE
// Nv2a::rasterize_witnessed_batch; stage (e) splits that function into a latch
// half (which matches the profile, validates it, resolves surfaces and snapshots
// textures -- all emulation-thread work that can fault_) and a pure execute half
// (Nv2a::execute_witnessed_fragments, which a worker may run), and both halves
// need the tag. Same enumerators, same order, same values: `job.profile` is the
// underlying integer and every `Profile::kX` use site below is unchanged.
enum class Profile {
    kNone,
    kPitchZ16Depth,
    kSwizzledRadialColor,
    kSwizzledTexturedColor,
    kPitchTexturedColor,
    kSwizzledProjectedColor,
    kPitchProjectedColor,
    kPitchMultiTextureColor,
    kPitchThreeTextureColor,
    kPitchTex5ProjectedColor,
    kPitchTex5DiffuseColor,
    kInlineLinearBlitColor,
    kPitchSingleTextureColor,
    kTex7FanColor,
    kTexfProjectedFanColor,
    kPitchAdditiveColor,
};

// Phase 7 (beads-xbox.1.15.10). The mask bit that reroutes a witnessed family
// into the strict generic fallback, or -1 for a profile that is never
// rerouted. The eight are exactly the CPU consumers the phase-5 plane census
// found on plane 0x03628000 / 0x033d0000; every other profile either already
// runs on the GPU (pitch_textured, pitch_projected, swizzled_radial), draws to
// a pure-CPU plane no GPU draw ever touches (the six swizzled planes,
// 0x032a4000, the two z16 zeta planes), or is the inline blit.
inline int generic_shader_witnessed_bit(Profile profile) {
    switch (profile) {
    case Profile::kPitchMultiTextureColor:  return 0;
    case Profile::kPitchThreeTextureColor:  return 1;
    case Profile::kPitchTex5ProjectedColor: return 2;
    case Profile::kPitchTex5DiffuseColor:   return 3;
    case Profile::kPitchSingleTextureColor: return 4;
    case Profile::kTex7FanColor:            return 5;
    case Profile::kTexfProjectedFanColor:   return 6;
    case Profile::kPitchAdditiveColor:      return 7;
    default:                                return -1;
    }
}

}  // namespace

// Engine-block map (offsets/sizes from xemu nv2a.c blocktable). USER/PRAMIN sizes trimmed to
// what the aperture actually carves; a real access into them still resolves to the block name.
static const Nv2a::Block kBlocks[] = {
    {0x000000, 0x001000, "PMC"},     {0x001000, 0x001000, "PBUS"},
    {0x002000, 0x002000, "PFIFO"},   {0x007000, 0x001000, "PRMA"},
    {0x008000, 0x001000, "PVIDEO"},  {0x009000, 0x001000, "PTIMER"},
    {0x00a000, 0x001000, "PCOUNTER"},{0x00b000, 0x001000, "PVPE"},
    {0x00d000, 0x001000, "PTV"},     {0x0a0000, 0x020000, "PRMFB"},
    {0x0c0000, 0x001000, "PRMVIO"},  {0x100000, 0x001000, "PFB"},
    {0x101000, 0x001000, "PSTRAPS"}, {0x400000, 0x002000, "PGRAPH"},
    {0x600000, 0x001000, "PCRTC"},   {0x601000, 0x001000, "PRMCIO"},
    {0x680000, 0x001000, "PRAMDAC"}, {0x681000, 0x001000, "PRMDIO"},
    {0x700000, 0x100000, "PRAMIN"},  {0x800000, 0x800000, "USER"},
};

const Nv2a::Block* Nv2a::find_block(uint32_t rel, uint32_t& block_rel) {
    for (const auto& b : kBlocks) {
        if (rel >= b.off && rel < b.off + b.size) {
            block_rel = rel - b.off;
            return &b;
        }
    }
    return nullptr;
}

// ============ stage (f) GPU-resident consumer: residency model ==============
// beads-xbox.1.15.7. See the contract on GpuResidencyCensus in nv2a.hpp. In
// simulate mode (the only mode until the GL executor lands) these five
// functions do nothing but count, so they are always on: the whole point is
// that the cost model of the design is measured from the canonical run rather
// than estimated, and measured continuously from process start rather than
// captured in a window (PRINCIPLES section 7).
const char* Nv2a::gpu_reject_reason_name(size_t reason) {
    switch (reason) {
    case kGpuRejectHashOn:          return "hash_on";
    case kGpuRejectDebugCb:         return "debug_cb";
    case kGpuRejectCombiner:        return "combiner_words";
    case kGpuRejectRasterSubset:    return "raster_subset";
    case kGpuRejectSurface:         return "surface";
    case kGpuRejectTexture:         return "texture";
    case kGpuRejectPrimitive:       return "primitive";
    case kGpuRejectVertexRange:     return "vertex_range";
    case kGpuRejectVertexNonFinite: return "vertex_non_finite";
    default: break;
    }
    return "unknown";
}

const char* Nv2a::gpu_residency_cause_name(size_t cause) {
    switch (cause) {
    case kGpuResCauseClearSurface:     return "clear_surface";
    case kGpuResCauseWitnessedCpuDraw: return "witnessed_cpu_draw";
    case kGpuResCauseGeneric:          return "generic";
    case kGpuResCauseImageBlit:        return "image_blit";
    case kGpuResCauseScanout:          return "scanout";
    case kGpuResCauseImmediateQuad:    return "immediate_quad";
    case kGpuResCauseRetarget:         return "retarget";
    case kGpuResCauseOther:            return "other";
    default: break;
    }
    return "unknown";
}

uint32_t Nv2a::gpu_res_cause_from_context(const char* context) {
    if (!context) return kGpuResCauseOther;
    if (std::strcmp(context, "CLEAR_SURFACE") == 0)
        return kGpuResCauseClearSurface;
    if (std::strcmp(context, "witnessed color surface") == 0 ||
        std::strcmp(context, "witnessed zeta surface") == 0)
        return kGpuResCauseWitnessedCpuDraw;
    if (std::strcmp(context, "scanout capture") == 0)
        return kGpuResCauseScanout;
    if (std::strncmp(context, "IMAGE_BLIT", 10) == 0)
        return kGpuResCauseImageBlit;
    if (std::strstr(context, "generic") != nullptr)
        return kGpuResCauseGeneric;
    return kGpuResCauseOther;
}

// Phase 2 (beads-xbox.1.15.10). Mirrors the anonymous-namespace `Profile` enum
// declared at the top of this file, by value, so the census can be printed from
// src/app/main.cpp without exporting the enum. A slot that drifts out of sync
// would show up immediately as a mislabelled row against the [2bl] per-profile
// batch counters, which are produced by a completely separate path.
const char* Nv2a::gpu_res_profile_name(size_t profile) {
    switch (static_cast<Profile>(profile)) {
    case Profile::kNone:                    return "none";
    case Profile::kPitchZ16Depth:           return "pitch_z16_depth";
    case Profile::kSwizzledRadialColor:     return "swizzled_radial";
    case Profile::kSwizzledTexturedColor:   return "swizzled_textured";
    case Profile::kPitchTexturedColor:      return "pitch_textured";
    case Profile::kSwizzledProjectedColor:  return "swizzled_projected";
    case Profile::kPitchProjectedColor:     return "pitch_projected";
    case Profile::kPitchMultiTextureColor:  return "pitch_multi_texture";
    case Profile::kPitchThreeTextureColor:  return "pitch_three_texture";
    case Profile::kPitchTex5ProjectedColor: return "pitch_tex5_projected";
    case Profile::kPitchTex5DiffuseColor:   return "pitch_tex5_diffuse";
    case Profile::kInlineLinearBlitColor:   return "inline_linear_blit";
    case Profile::kPitchSingleTextureColor: return "pitch_single_texture";
    case Profile::kTex7FanColor:            return "tex7_fan";
    case Profile::kTexfProjectedFanColor:   return "texf_projected_fan";
    case Profile::kPitchAdditiveColor:      return "pitch_additive";
    }
    return "unknown";
}

// Phase 5 item 1 (beads-xbox.1.15.10). One row per distinct destination plane
// address; the per-profile column is only filled for CPU-executed draws, which
// is what "the CPU consumers of this surface" means. A linear scan over at most
// kGpuResSurfaceSlots rows, once per witnessed draw (436 k per canonical run):
// the whole table fits in two cache lines' worth of addresses and the workload
// has fewer than a dozen distinct planes, so the scan terminates in the first
// few entries in practice. Overflow is counted rather than evicting, so the
// table can never silently under-report a plane it already knows about.
void Nv2a::gpu_res_note_surface_consumer(GpuResSurfaceTable& table,
                                         uint32_t address, uint64_t bytes,
                                         uint32_t profile, bool on_gpu) {
    if (bytes == 0) return;
    GpuResSurfaceRow* row = nullptr;
    for (size_t i = 0; i < table.count; ++i) {
        if (table.rows[i].address == address) {
            row = &table.rows[i];
            break;
        }
    }
    if (!row) {
        if (table.count >= kGpuResSurfaceSlots) {
            ++table.overflow;
            return;
        }
        row = &table.rows[table.count++];
        row->address = address;
    }
    // The largest span seen at this base: a plane re-bound at a different
    // height is the same plane for the purpose of "who consumes it".
    if (bytes > row->bytes) {
        row->bytes = static_cast<uint32_t>(
            bytes > UINT32_MAX ? UINT32_MAX : bytes);
    }
    if (on_gpu) {
        ++row->gpu_draws;
        return;
    }
    ++row->cpu_draws;
    if (profile < kGpuResProfileSlots) ++row->cpu_draws_profile[profile];
}

void Nv2a::gpu_res_end_run() {
    if (gpu_res_run_ == 0) return;
    ++gpu_res_.runs;
    gpu_res_.run_sum += gpu_res_run_;
    if (gpu_res_run_ > gpu_res_.run_max) gpu_res_.run_max = gpu_res_run_;
    gpu_res_run_ = 0;
}

// A draw the GPU executor would take. If the destination pair is not already
// GPU-authoritative it costs an upload of both planes; if a DIFFERENT pair is,
// that one is resolved first (the backend keeps one active target).
void Nv2a::gpu_res_note_gpu_draw(uint32_t color_addr, uint64_t color_bytes,
                                 uint32_t depth_addr, uint64_t depth_bytes) {
    ++gpu_res_.gpu_draws;
    ++gpu_res_run_;
    if (gpu_res_color_addr_ == color_addr &&
        gpu_res_color_bytes_ == color_bytes &&
        gpu_res_depth_addr_ == depth_addr &&
        gpu_res_depth_bytes_ == depth_bytes) {
        return;  // already resident and authoritative: no transfer at all
    }
    if (gpu_res_color_addr_ != 0) {
        ++gpu_res_.resolves;
        gpu_res_.resolve_bytes += gpu_res_color_bytes_;
        ++gpu_res_.cause[kGpuResCauseRetarget];
        if (gpu_res_depth_addr_ != 0) {
            ++gpu_res_.depth_resolves;
            gpu_res_.depth_resolve_bytes += gpu_res_depth_bytes_;
        }
    }
    ++gpu_res_.uploads;
    gpu_res_.upload_bytes += color_bytes;
    gpu_res_color_addr_ = color_addr;
    gpu_res_color_bytes_ = color_bytes;
    gpu_res_depth_addr_ = depth_addr;
    gpu_res_depth_bytes_ = depth_bytes;
    if (depth_addr != 0) {
        ++gpu_res_.depth_uploads;
        gpu_res_.depth_upload_bytes += depth_bytes;
    }
}

void Nv2a::gpu_res_cpu_color_access(uint32_t address, uint64_t bytes,
                                    uint32_t cause) {
    if (gpu_res_in_gpu_draw_ || gpu_res_color_addr_ == 0 || bytes == 0) return;
    const uint64_t lo = address;
    const uint64_t hi = lo + bytes;
    const uint64_t rlo = gpu_res_color_addr_;
    const uint64_t rhi = rlo + gpu_res_color_bytes_;
    if (hi <= rlo || rhi <= lo) return;
    if (cause == kGpuResCauseClearSurface && lo <= rlo && hi >= rhi) {
        // A full-surface clear of a GPU-authoritative surface is a glClear,
        // not a readback: residency is retained and nothing crosses the bus.
        ++gpu_res_.clears_on_gpu;
        return;
    }
    gpu_res_end_run();
    ++gpu_res_.resolves;
    gpu_res_.resolve_bytes += gpu_res_color_bytes_;
    ++gpu_res_.cause[cause < kGpuResidencyCauses ? cause : kGpuResCauseOther];
    // Phase 2 attribution: a witnessed CPU draw is the only cause that carries
    // a profile, and gpu_res_cpu_profile_ was set for THIS draw at the
    // classification point in rasterize_witnessed_batch.
    if (cause == kGpuResCauseWitnessedCpuDraw &&
        gpu_res_cpu_profile_ < kGpuResProfileSlots) {
        ++gpu_res_.breaks_profile[gpu_res_cpu_profile_];
    }
    if (gpu_res_depth_addr_ != 0) {
        ++gpu_res_.depth_resolves;
        gpu_res_.depth_resolve_bytes += gpu_res_depth_bytes_;
    }
    gpu_res_color_addr_ = 0;
    gpu_res_color_bytes_ = 0;
    gpu_res_depth_addr_ = 0;
    gpu_res_depth_bytes_ = 0;
}

void Nv2a::gpu_res_cpu_depth_access(uint32_t address, uint64_t bytes,
                                    uint32_t cause) {
    if (gpu_res_in_gpu_draw_ || gpu_res_depth_addr_ == 0 || bytes == 0) return;
    const uint64_t lo = address;
    const uint64_t hi = lo + bytes;
    const uint64_t rlo = gpu_res_depth_addr_;
    const uint64_t rhi = rlo + gpu_res_depth_bytes_;
    if (hi <= rlo || rhi <= lo) return;
    if (cause == kGpuResCauseClearSurface && lo <= rlo && hi >= rhi) return;
    // The pair is resolved together: the colour plane is what the executor
    // renders and the two are always bound as one target.
    const uint64_t resolves_before = gpu_res_.resolves;
    gpu_res_cpu_color_access(gpu_res_color_addr_,
                             gpu_res_color_bytes_ ? gpu_res_color_bytes_ : 1,
                             cause);
    if (gpu_res_depth_addr_ != 0) {
        gpu_res_end_run();
        ++gpu_res_.depth_resolves;
        gpu_res_.depth_resolve_bytes += gpu_res_depth_bytes_;
        ++gpu_res_.cause[cause < kGpuResidencyCauses ? cause
                                                    : kGpuResCauseOther];
        // Attribute the break exactly once. If the colour half already took a
        // resolve above it has already counted this draw; only a depth-only
        // break (no resident colour plane) is counted here.
        if (gpu_res_.resolves == resolves_before &&
            cause == kGpuResCauseWitnessedCpuDraw &&
            gpu_res_cpu_profile_ < kGpuResProfileSlots) {
            ++gpu_res_.breaks_profile[gpu_res_cpu_profile_];
        }
        gpu_res_depth_addr_ = 0;
        gpu_res_depth_bytes_ = 0;
    }
}

// Phase 5 item 2 (beads-xbox.1.15.10). Times ONE residency query and credits it
// to the no-op or the transferring bucket at scope exit. The bucket is decided
// by `moved`, which the body sets the moment mem_write_ puts bytes back into
// guest RAM -- the same predicate `seam_resolve_transfers_` uses -- so the two
// columns cannot disagree about which calls transferred.
namespace {
struct ResolveQueryTimer {
    uint64_t* noop_n;
    uint64_t* noop_ticks;
    uint64_t* moved_n;
    uint64_t* moved_ticks;
    uint64_t* alt_noop_n;
    uint64_t* alt_noop_ticks;
    uint64_t* alt_moved_n;
    uint64_t* alt_moved_ticks;
    uint64_t start;
    bool moved = false;
    ResolveQueryTimer(uint64_t* nn, uint64_t* nt, uint64_t* mn, uint64_t* mt,
                      uint64_t* ann, uint64_t* ant, uint64_t* amn,
                      uint64_t* amt)
        : noop_n(nn), noop_ticks(nt), moved_n(mn), moved_ticks(mt),
          alt_noop_n(ann), alt_noop_ticks(ant), alt_moved_n(amn),
          alt_moved_ticks(amt), start(perf_excl::tick_now()) {}
    ~ResolveQueryTimer() {
        const uint64_t delta = perf_excl::tick_now() - start;
        // The same plausibility clamp perf_excl::accrue uses: one backwards TSC
        // read would otherwise wrap and poison the bucket for the whole run.
        if (delta > perf_excl::g_max_plausible_ticks) return;
        if (moved) {
            ++*moved_n;
            *moved_ticks += delta;
            if (alt_moved_n) {
                ++*alt_moved_n;
                *alt_moved_ticks += delta;
            }
        } else {
            ++*noop_n;
            *noop_ticks += delta;
            if (alt_noop_n) {
                ++*alt_noop_n;
                *alt_noop_ticks += delta;
            }
        }
    }
};
}  // namespace

bool Nv2a::resolve_renderer_color_overlap(uint32_t address, size_t size,
                                          const char* context) {
    if (!renderer_ || size == 0) return true;
    const bool from_texsrc = resolve_query_from_texture_source_;
    ResolveQueryTimer query_timer(
        &resolve_query_census_.color_noop_n,
        &resolve_query_census_.color_noop_ticks,
        &resolve_query_census_.color_moved_n,
        &resolve_query_census_.color_moved_ticks,
        from_texsrc ? &resolve_query_census_.texsrc_noop_n : nullptr,
        from_texsrc ? &resolve_query_census_.texsrc_noop_ticks : nullptr,
        from_texsrc ? &resolve_query_census_.texsrc_moved_n : nullptr,
        from_texsrc ? &resolve_query_census_.texsrc_moved_ticks : nullptr);
    // F3 CLOSURE (beads-xbox.1.15.5 residual risk). BEFORE the renderer is asked
    // what it owns, drain anything queued: a pending job's recorded
    // invalidate_renderer_color_overlap has not been applied yet, so without
    // this the query can return bytes for a surface a pending job is about to
    // overwrite, and the mem_write_ below would then clobber the software
    // output. Two compares when the ring is empty, which at N == 0 and at every
    // N >= 2 (a span job never enters the ring) is always.
    draw_renderer_query_fence();
    // Stage (f) residency census: every CPU consumer that is about to read or
    // overwrite a colour surface already funnels through here with its context
    // string, so this one call covers CLEAR_SURFACE, the witnessed CPU draws,
    // the generic-combiner paths, IMAGE_BLIT and scanout without a hook at each.
    gpu_res_cpu_color_access(address, size, gpu_res_cause_from_context(context));
    // Seam anchor 8 (F3): GL/software mixing. HIT counted here, after the
    // no-renderer early-out -- with no renderer this function neither reads nor
    // writes guest memory and cannot be a coherence point at all. The DRAIN is
    // raised below, only when the resolve actually moves bytes back into guest
    // RAM through mem_write_, because that is the whole of the hazard: a resolve
    // that returns nothing queries the GL backend's resident set and touches no
    // guest memory, so it cannot collide with a pending job's destination. The
    // resident set is decided by the renderer independently of what is pending,
    // so evaluating it first and draining afterwards is safe.
    seam_anchor(kSeamAnchorResolveOverlap, false);
    // NV2A_GL_RESOLVE (beads-xbox.1.14.3): the resident-surface readback plus the
    // guest-memory writeback that makes it coherent. Opened AFTER the early-out on
    // purpose: with no renderer this function is a compare and a return, and timing that
    // would put two clock reads on a hot path for a bucket that stays zero.
    perf_excl::Scope region(perf_excl::kNv2aGlResolve);
    for (;;) {
        Nv2aResidentColorResolve resolve;
        // Recycle the readback buffer across resolves so repeated full-surface
        // readbacks stop reallocating and zero-filling multi-megabyte vectors.
        resolve.data = std::move(color_resolve_scratch_);
        std::string error;
        // Phase 5 item 2: split the TRANSFERRING resolve into its backend half
        // and its guest-writeback half. Two clock reads per iteration of a loop
        // that runs 2 258 times per canonical run.
        const uint64_t backend_start = perf_excl::tick_now();
        if (!renderer_->resolve_resident_color_surface(address, size, resolve,
                                                       error)) {
            color_resolve_scratch_ = std::move(resolve.data);
            fault_(error.empty() ? "NV2A resident color resolve failed"
                                 : error.c_str());
            return false;
        }
        const uint64_t backend_delta = perf_excl::tick_now() - backend_start;
        if (resolve.data.empty()) {
            color_resolve_scratch_ = std::move(resolve.data);
            return true;
        }
        if (backend_delta <= perf_excl::g_max_plausible_ticks)
            resolve_query_census_.color_backend_ticks += backend_delta;
        // The subset of anchor-8 hits that actually moves bytes back into guest
        // RAM -- and therefore the actual drain condition. Reporting hits and
        // transfers separately lets the conservative policy be judged too.
        ++seam_resolve_transfers_;
        query_timer.moved = true;
        seam_anchor_drain(kSeamAnchorResolveOverlap);
        perf::record_renderer_color_resolve_context(context, resolve.data.size());
        const uint64_t write_start = perf_excl::tick_now();
        const bool wrote =
            mem_write_ &&
            mem_write_(resolve.address, resolve.data.data(),
                       resolve.data.size());
        const uint64_t write_delta = perf_excl::tick_now() - write_start;
        if (write_delta <= perf_excl::g_max_plausible_ticks)
            resolve_query_census_.color_memwrite_ticks += write_delta;
        resolve_query_census_.color_memwrite_bytes += resolve.data.size();
        color_resolve_scratch_ = std::move(resolve.data);
        if (!wrote) {
            char why[160];
            std::snprintf(
                why, sizeof(why),
                "NV2A resident color resolve DMA write failed before %s",
                context ? context : "memory access");
            fault_(why);
            return false;
        }
    }
}

void Nv2a::invalidate_renderer_color_overlap(uint32_t address, size_t size,
                                             uint32_t cause) {
    if (renderer_ && size != 0)
        renderer_->invalidate_resident_color_surface(address, size, cause);
}

bool Nv2a::refresh_resident_color_after_software_write(
    uint32_t address, size_t size, uint32_t key_pitch, uint32_t row_pitch,
    uint32_t height, uint32_t format, size_t write_begin, size_t write_end) {
    if (!renderer_ || size == 0 || write_end <= write_begin || row_pitch == 0) {
        return true;  // nothing written or nothing to keep coherent
    }
    // F3 CLOSURE, third and last renderer residency query in the device. Its
    // caller (the generic-combiner software flush) is already behind
    // draw_order_fence, so this is expected to stay at zero; it is here so the
    // property is local to the query rather than a fact about one call site.
    draw_renderer_query_fence();
    if (renderer_->supports_resident_color_region_upload() &&
        renderer_->has_resident_color_surface(address, size, key_pitch, height,
                                              format)) {
        const uint32_t first_row =
            static_cast<uint32_t>(write_begin / row_pitch);
        const uint32_t last_row =
            static_cast<uint32_t>((write_end - 1) / row_pitch);
        const size_t row_bytes = static_cast<size_t>(row_pitch);
        region_upload_scratch_.resize((last_row - first_row + 1) * row_bytes);
        const uint32_t rows_address =
            address + static_cast<uint32_t>(first_row * row_bytes);
        if (!mem_read_ ||
            !mem_read_(rows_address, region_upload_scratch_.data(),
                       region_upload_scratch_.size())) {
            fault_("NV2A resident color region upload DMA read failed");
            return false;
        }
        std::string error;
        if (!renderer_->upload_resident_color_rows(
                address, size, key_pitch, height, format, first_row, last_row,
                region_upload_scratch_, error)) {
            fault_(error.empty()
                       ? "NV2A resident color region upload failed"
                       : error.c_str());
            return false;
        }
        perf::record_renderer_color_resolve_context(
            "software generic color region upload",
            region_upload_scratch_.size());
        return true;
    }
    // Not resident (or backend cannot region-upload): preserve the old
    // contract — drop residency so the next accelerated draw re-uploads.
    invalidate_renderer_color_overlap(
        address, size, nv2a_invalidate_token(kNv2aInvalidateRegionFallback));
    return true;
}

bool Nv2a::resolve_renderer_depth_overlap(uint32_t address, size_t size,
                                          const char* context) {
    if (!renderer_ || size == 0) return true;
    ResolveQueryTimer query_timer(&resolve_query_census_.depth_noop_n,
                                  &resolve_query_census_.depth_noop_ticks,
                                  &resolve_query_census_.depth_moved_n,
                                  &resolve_query_census_.depth_moved_ticks,
                                  nullptr, nullptr, nullptr, nullptr);
    // F3 CLOSURE, depth half. Same argument as the colour half above.
    draw_renderer_query_fence();
    gpu_res_cpu_depth_access(address, size, gpu_res_cause_from_context(context));
    // Seam anchor 8 (F3), depth half. Same hit/drain split as the colour half.
    seam_anchor(kSeamAnchorResolveOverlap, false);
    perf_excl::Scope region(perf_excl::kNv2aGlResolve);
    for (;;) {
        Nv2aResidentDepthResolve resolve;
        std::string error;
        const uint64_t backend_start = perf_excl::tick_now();
        if (!renderer_->resolve_resident_depth_surface(address, size, resolve,
                                                       error)) {
            fault_(error.empty() ? "NV2A resident depth resolve failed"
                                 : error.c_str());
            return false;
        }
        const uint64_t backend_delta = perf_excl::tick_now() - backend_start;
        if (resolve.data.empty()) return true;
        if (backend_delta <= perf_excl::g_max_plausible_ticks)
            resolve_query_census_.depth_backend_ticks += backend_delta;
        ++seam_resolve_transfers_;
        query_timer.moved = true;
        seam_anchor_drain(kSeamAnchorResolveOverlap);
        perf::record_renderer_depth_resolve_context(context,
                                                    resolve.data.size());
        const uint64_t write_start = perf_excl::tick_now();
        const bool depth_wrote =
            mem_write_ && mem_write_(resolve.address, resolve.data.data(),
                                     resolve.data.size());
        const uint64_t write_delta = perf_excl::tick_now() - write_start;
        if (write_delta <= perf_excl::g_max_plausible_ticks)
            resolve_query_census_.depth_memwrite_ticks += write_delta;
        resolve_query_census_.depth_memwrite_bytes += resolve.data.size();
        if (!depth_wrote) {
            char why[160];
            std::snprintf(
                why, sizeof(why),
                "NV2A resident depth resolve DMA write failed before %s",
                context ? context : "memory access");
            fault_(why);
            return false;
        }
    }
}

void Nv2a::invalidate_renderer_depth_overlap(uint32_t address, size_t size,
                                             uint32_t cause) {
    if (renderer_ && size != 0)
        renderer_->invalidate_resident_depth_surface(address, size, cause);
}

// PCI identity mirror exposed through PBUS (xemu pbus.c reads d->config). NV2A = 10DE:02A0,
// class VGA (0x030000), revision 0xA1.
static constexpr uint32_t kPciNv0 = 0x02A0'10DEu;  // device<<16 | vendor
static constexpr uint32_t kPciNv1 = 0x0000'0000u;  // command/status (decode off at reset)
static constexpr uint32_t kPciNv2 = 0x0300'00A1u;  // class(0x030000)<<8 | revision(0xA1)

// xemu's fixed-function shader clamps reciprocal divisors to NV2A's signed
// [2^-64, 2^64] range, preserving the sign of zero, then truncates screen
// coordinates to four fractional bits.
static float clamp_away_zero_inf(float value) {
    const float minimum = std::ldexp(1.0f, -64);
    const float maximum = std::ldexp(1.0f, 64);
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    if (value > 0.0f || bits == 0)
        return std::clamp(value, minimum, maximum);
    return std::clamp(value, -maximum, -minimum);
}

static float round_screen_coordinate(float value) {
    return std::trunc(value * 16.0f) / 16.0f;
}

static uint64_t fnv1a_bytes(const void* data, size_t size) {
    const auto* bytes = static_cast<const uint8_t*>(data);
    uint64_t hash = 1469598103934665603ull;
    for (size_t index = 0; index < size; ++index) {
        hash ^= bytes[index];
        hash *= 1099511628211ull;
    }
    return hash;
}

uint32_t Nv2a::mmio_read(uint32_t addr, int size) {
    uint32_t rel = addr - kBase;
    uint32_t bo = 0;
    const Block* blk = find_block(rel, bo);
    if (!blk) {
        // Undecoded space within the 16 MB aperture. The aperture is legitimately sparse; xemu
        // returns 0 here (parent-region default) and boots the same kernel, so 0 is a
        // boot-correct witness. Logged as a hardware-TODO, not faulted.
        if (unmodeled_cb_) unmodeled_cb_(false, addr, rel, "hole", 0);
        return 0;
    }

    // Modelled registers (faithful values). Block-relative offsets from xemu nv2a_regs.h.
    if (blk->off == 0x000000) {  // PMC
        switch (bo) {
            case 0x000: return 0x02A000A3;  // PMC_BOOT_0: NV2A, A03 (matches captured silicon)
            case 0x100: return pmc_intr_status();  // PMC_INTR_0 (aggregates PCRTC, bit 24)
            case 0x140: return pmc_intr_en_;       // PMC_INTR_EN_0
            default: break;
        }
    } else if (blk->off == 0x600000) {  // PCRTC
        switch (bo) {
            case 0x100: return pcrtc_intr_;     // NV_PCRTC_INTR_0 (bit0 VBLANK)
            case 0x140: return pcrtc_intr_en_;  // NV_PCRTC_INTR_EN_0
            case 0x800: return pcrtc_start_;    // NV_PCRTC_START
            default: break;
        }
    } else if (blk->off == 0x601000) {  // PRMCIO (VGA CRTC/attribute mirror)
        // Input status 1 (0x3DA): bit3 = vertical retrace in progress, bit0 = blanking.
        // Driven from the instruction-derived frame phase (last ~5% of each frame is the
        // vertical blank), so the kernel's poll-for-retrace loops see a real cadence.
        if (bo == 0x3DA) {
            bool vblank = frame_phase_ >= 950;
            return vblank ? 0x09u : 0x00u;
        }
        if (bo == 0x3D4) return crtc_index_;
        if (bo == 0x3D5) return crtc_regs_[crtc_index_];
    } else if (blk->off == 0x680000) {  // PRAMDAC
        switch (bo & ~3u) {
            case 0x600: return pramdac_general_control_;
            case 0x800: return pramdac_fp_vdisplay_end_;
            case 0x820: return pramdac_fp_hdisplay_end_;
            default: break;
        }
    } else if (blk->off == 0x001000) {  // PBUS
        switch (bo) {
            case 0x800: return kPciNv0;  // NV_PBUS_PCI_NV_0
            case 0x804: return kPciNv1;  // NV_PBUS_PCI_NV_1
            case 0x808: return kPciNv2;  // NV_PBUS_PCI_NV_2
            default: break;
        }
    } else if (blk->off == 0x002000) {  // PFIFO
        switch (bo) {
            case 0x400:
                return pull_queue_.empty() ? 1u << 4 : 0u;
            case 0x1214:
                return (pull_queue_.empty() ? 1u << 4 : 0u) |
                       (pull_queue_.size() >= kPullQueueCapacity ? 1u << 8 : 0u);
            default: break;
        }
        auto it = pfifo_regs_.find(bo);
        if (it != pfifo_regs_.end()) return it->second;
    } else if (blk->off == 0x100000) {  // PFB
        // Captured from silicon 2026-07-16 (agent read 0xFD100200).
        if (bo == 0x200) return 0x03070003;  // NV_PFB_CFG0
        if (bo == 0x410 && size == 4) {
            // WBC FLUSH completes synchronously on the coherent shared-RAM
            // bus below. There is no deferred CPU write-combining queue.
            return 0;
        }
    } else if (blk->off == 0x400000) {  // PGRAPH
        // Seam anchor 7: conservative catch-all for guest status polling of
        // PGRAPH. Cannot detect a bare framebuffer read (F7, measurement M1).
        seam_anchor(kSeamAnchorPgraphRegisterRead, true);
        // Stage (f): what the CONSERVATIVE anchor policy would cost on top of
        // the demand-driven residency rule. Counted, never acted on.
        if (gpu_res_color_addr_ != 0) ++gpu_res_.pgraph_read_resolves;
        switch (bo) {
            case 0x100: return pgraph_intr_;
            case 0x108: return pgraph_nsource_;
            case 0x140: return pgraph_intr_en_;
            case 0x704: return pgraph_trapped_addr_;
            case 0x708: return pgraph_trapped_data_;
            case 0x710: return pgraph_surface();
            case 0x71C: return 0;  // NV_PGRAPH_INCREMENT (write-only)
            case 0x720: return pgraph_fifo_access_ ? 1u : 0u;
            case 0xB10: return patt_color0_;  // NV_PGRAPH_PATT_COLOR0
            // Kelvin's clear-value methods also feed the two PGRAPH parameter shadows.
            // The retail kernel reads PARAMETER_A after a trapped NOP 0x304 and queues it
            // as the next PCRTC start address (the same hardware path documented by pbkit).
            case 0x186C: return color_clear_value_;  // NV_PGRAPH_PARAMETER_B
            case 0x1A88: return zeta_clear_value_;   // NV_PGRAPH_PARAMETER_A
            default: break;
        }
    } else if (blk->off == 0x101000) {  // PSTRAPS
        // Real strap value captured from silicon 2026-07-16 (agent read 0xFD101000). xemu
        // stubs this to 0; the 2BL discards it, but the kernel may consume it — use the truth.
        if (bo == 0x000) return 0x861D4401;
    } else if (blk->off == 0x700000) {  // PRAMIN: object/RAMHT/RAMFC backing store
        if (bo + static_cast<uint32_t>(size) <= pramin_.size()) {
            uint32_t value = 0;
            std::memcpy(&value, pramin_.data() + bo, size);
            return value;
        }
    } else if (blk->off == 0x800000) {  // USER: per-channel PFIFO control (64 KB stride)
        // The kernel submits commands by bumping DMA_PUT (+0x40) and polls DMA_GET (+0x44)
        // until PFIFO consumes them. The recorder parses the stream synchronously, executes
        // the guest-visible method subset, and leaves the rest in the always-on method ring.
        if ((bo & 0xFFFF) == 0x44) {
            uint8_t channel = static_cast<uint8_t>((bo >> 16) & 0x1Fu);
            const auto& ch = channels_[channel];
            uint32_t get = ch.get_initialized ? ch.get : ch.put;
            if (pb_logged_ < 8) {
                std::printf("[nv2a] USER ch%u DMA_GET=0x%08x DMA_PUT=0x%08x "
                            "(PFIFO recorder)\n", channel, get, ch.put);
                ++pb_logged_;
            }
            return get;
        }
        auto it = user_regs_.find(rel);
        if (it != user_regs_.end()) return it->second;
    }

    // Unmodelled register inside a real block: xemu-default 0, logged as a hardware-TODO.
    if (unmodeled_cb_) unmodeled_cb_(false, addr, bo, blk->name, 0);
    (void)size;
    return 0;
}

void Nv2a::mmio_write(uint32_t addr, int size, uint32_t value) {
    uint32_t rel = addr - kBase;
    uint32_t bo = 0;
    const Block* blk = find_block(rel, bo);
    if (!blk) {  // undecoded aperture space: accept + log (xemu ignores; see mmio_read)
        if (unmodeled_cb_) unmodeled_cb_(true, addr, rel, "hole", value);
        return;
    }
    // Interrupt plumbing (xemu nv2a.c/pcrtc.c): PMC_INTR_EN_0 gates the PCI line; PCRTC's
    // pending register is write-1-clear, its enable register plain. Every change re-evaluates
    // the level-triggered GPU IRQ.
    if (blk->off == 0x000000 && bo == 0x140) {
        pmc_intr_en_ = value;
        update_irq();
        return;
    }
    if (blk->off == 0x100000 && bo == 0x410) {
        // Original XDK KickPushBuffer flushes CPU writes before publishing
        // PUT. Our CPU/DMA memory callbacks commit synchronously to shared
        // RAM; publish those writes before returning flush-complete. This is
        // not a GPU draw-completion fence. Other WBC controls are unqualified.
        // Wire/status reference: pinned xemu pfb.c and NV_PFB_WBC_FLUSH.
        if (size != 4 || (value & ~0x10000u)) {
            fault_("NV2A unqualified PFB write-buffer control"); return;
        }
        std::atomic_thread_fence(std::memory_order_seq_cst);
        return;
    }
    if (blk->off == 0x600000 && bo == 0x100) {
        pcrtc_intr_ &= ~value;
        update_irq();
        return;
    }
    if (blk->off == 0x600000 && bo == 0x140) {
        pcrtc_intr_en_ = value;
        update_irq();
        return;
    }
    if (blk->off == 0x600000 && bo == 0x800) {
        pcrtc_start_ = value & 0x07ffffffu;
        ++scanout_writes_;
        if (pcrtc_start_) {
            ++scanout_nonzero_writes_;
            last_nonzero_scanout_start_ = pcrtc_start_;
        }
        scanout_starts_.push(pcrtc_start_);
        return;
    }
    if (blk->off == 0x601000) {
        if (bo == 0x3D4) {
            crtc_index_ = static_cast<uint8_t>(value);
            return;
        }
        if (bo == 0x3D5) {
            crtc_regs_[crtc_index_] = static_cast<uint8_t>(value);
            return;
        }
    }
    if (blk->off == 0x680000) {
        switch (bo) {
            case 0x600:
                pramdac_general_control_ = value;
                return;
            case 0x800:
                pramdac_fp_vdisplay_end_ = value;
                return;
            case 0x820:
                pramdac_fp_hdisplay_end_ = value;
                return;
            default:
                break;
        }
    }
    if (blk->off == 0x400000 && bo == 0x71C) {
        // NV_PGRAPH_INCREMENT_READ_3D is written by the kernel's vblank ISR. It retires
        // one displayed surface and wakes a PFIFO puller blocked in NV097_FLIP_STALL.
        if ((value & 2u) && flip_modulo_) {
            flip_read_ = static_cast<uint8_t>((flip_read_ + 1u) % flip_modulo_);
            ++pgraph_read_increments_;
            if (waiting_for_flip_ && flip_read_ != flip_write_) {
                waiting_for_flip_ = false;
                resume_pushbuffers();
            }
        }
        return;
    }
    if (blk->off == 0x400000 && bo == 0x100) {
        pgraph_intr_ &= ~value;  // NV_PGRAPH_INTR is write-1-clear
        if (!(pgraph_intr_ & (1u << 20))) waiting_for_nop_ = false;
        update_irq();
        resume_pushbuffers();
        return;
    }
    if (blk->off == 0x400000 && bo == 0x140) {
        pgraph_intr_en_ = value;
        update_irq();
        return;
    }
    if (blk->off == 0x400000 && bo == 0x720) {
        pgraph_fifo_access_ = (value & 1u) != 0;
        if (pgraph_fifo_access_) resume_pushbuffers();
        return;
    }
    if (blk->off == 0x002000) {
        pfifo_regs_[bo] = value;
        uint8_t channel = static_cast<uint8_t>(pfifo_regs_[0x1204] & 0x1Fu);
        if (bo == 0x122C) {
            channels_[channel].dma_instance = (value & 0xFFFFu) << 4;
        } else if (bo == 0x1244) {
            channels_[channel].get = value;
            channels_[channel].get_initialized = true;
        } else if (bo == 0x1240) {
            channels_[channel].put = value;
        }
        // PFIFO register writes also wake the hardware pusher, but it must remain quiescent
        // while CACHE1_PUSH0 or CACHE1_DMA_PUSH access is disabled. The D3D runtime relies on
        // that lifecycle when it clears PUT before GET while rebuilding a channel context.
        if (channels_[channel].get_initialized && pusher_enabled(channel))
            process_pushbuffer(channel);
        return;
    }
    if (blk->off == 0x700000) {
        if (bo + static_cast<uint32_t>(size) > pramin_.size()) {
            fault_("NV2A PRAMIN write exceeds aperture");
            return;
        }
        std::memcpy(pramin_.data() + bo, &value, size);
        return;
    }
    // USER block: channel GET/PUT writes drive the synchronous PFIFO recorder.
    if (blk->off == 0x800000) {
        user_regs_[rel] = value;
        uint8_t channel = static_cast<uint8_t>((bo >> 16) & 0x1Fu);
        uint32_t reg = bo & 0xFFFF;
        if (reg == 0x44) {
            channels_[channel].get = value;
            channels_[channel].get_initialized = true;
            pfifo_regs_[0x1244] = value;
        } else if (reg == 0x40) {
            channels_[channel].put = value;
            pfifo_regs_[0x1240] = value;
            if (channels_[channel].get_initialized && pusher_enabled(channel))
                process_pushbuffer(channel);
        }
        return;
    }
    // No modelled write side effects yet; accept + log (xemu ignores unknown writes).
    if (unmodeled_cb_) unmodeled_cb_(true, addr, bo, blk->name, value);
    (void)size;
}

bool Nv2a::dma_object_address(uint32_t instance, uint32_t offset, uint32_t& address) const {
    if (instance > pramin_.size() - 12) return false;
    uint32_t flags = 0, limit = 0, frame = 0;
    std::memcpy(&flags, pramin_.data() + instance, 4);
    std::memcpy(&limit, pramin_.data() + instance + 4, 4);
    std::memcpy(&frame, pramin_.data() + instance + 8, 4);
    if (offset > limit) return false;
    uint32_t adjust = (flags & 0xFFF00000u) >> 20;
    address = (((frame & 0xFFFFF000u) | adjust) + offset) & 0x07FFFFFFu;
    return true;
}

bool Nv2a::ramht_instance(uint8_t channel, uint32_t handle, uint32_t& instance) const {
    auto it = pfifo_regs_.find(0x210);  // NV_PFIFO_RAMHT
    uint32_t ramht = it != pfifo_regs_.end() ? it->second : 0;
    uint32_t size = 1u << (((ramht & 0x00030000u) >> 16) + 12);
    uint32_t bits = 0;
    for (uint32_t n = size; n > 1; n >>= 1) ++bits;
    --bits;
    uint32_t hash = 0, h = handle;
    while (h) {
        hash ^= h & ((1u << bits) - 1u);
        h >>= bits;
    }
    hash ^= static_cast<uint32_t>(channel) << (bits - 4);
    uint32_t base = (ramht & 0x000001F0u) << 8;
    uint32_t entry = base + hash * 8;
    if (entry > pramin_.size() - 8) return false;
    uint32_t entry_handle = 0, context = 0;
    std::memcpy(&entry_handle, pramin_.data() + entry, 4);
    std::memcpy(&context, pramin_.data() + entry + 4, 4);
    uint8_t entry_channel = static_cast<uint8_t>((context >> 24) & 0x1Fu);
    if (entry_handle != handle || !(context & 0x80000000u) || entry_channel != channel)
        return false;
    instance = (context & 0x0000FFFFu) << 4;
    return true;
}

bool Nv2a::resolve_object(uint8_t channel, uint32_t handle, uint32_t& instance) {
    if (ramht_instance(channel, handle, instance)) return true;
    char why[128];
    std::snprintf(why, sizeof(why),
                  "NV2A RAMHT lookup failed: channel=%u handle=0x%08x", channel, handle);
    fault_(why);
    return false;
}

bool Nv2a::bind_object(uint8_t channel, uint8_t subchannel, uint32_t handle) {
    uint32_t instance = 0;
    if (!resolve_object(channel, handle, instance)) return false;
    if (instance > pramin_.size() - 4) {
        char why[128];
        std::snprintf(why, sizeof(why),
                      "NV2A graphics object exceeds PRAMIN: instance=0x%08x", instance);
        fault_(why);
        return false;
    }
    uint32_t context = 0;
    std::memcpy(&context, pramin_.data() + instance, 4);
    object_instance_[subchannel] = instance;
    object_class_[subchannel] = static_cast<uint8_t>(context & 0xFFu);
    return true;
}

bool Nv2a::clear_plane_extent(uint32_t dma_instance, bool dma_bound,
                              uint32_t surface_offset, uint32_t pitch,
                              uint32_t bytes_per_pixel, uint32_t write_mask,
                              uint32_t& first_address, size_t& span) const {
    const uint32_t xmin = clear_rect_horizontal_ & 0xFFFFu;
    const uint32_t xmax = clear_rect_horizontal_ >> 16;
    const uint32_t ymin = clear_rect_vertical_ & 0xFFFFu;
    const uint32_t ymax = clear_rect_vertical_ >> 16;
    if (!dma_bound || !pitch || xmax < xmin || ymax < ymin) return false;
    if (bytes_per_pixel != 2 && bytes_per_pixel != 4) return false;
    const uint32_t pixel_mask = bytes_per_pixel == 2 ? 0xFFFFu : 0xFFFFFFFFu;
    // A partial write mask is a read-modify-write of the guest bytes, not a
    // fill, so it cannot become a glClear.
    if (write_mask != pixel_mask) return false;
    const uint64_t width = static_cast<uint64_t>(xmax) - xmin + 1;
    const uint64_t height = static_cast<uint64_t>(ymax) - ymin + 1;
    const uint64_t row_bytes = width * bytes_per_pixel;
    const uint64_t x_bytes = static_cast<uint64_t>(xmin) * bytes_per_pixel;
    // Every byte of every row in the range, and the range starts at the plane
    // base: only then is the cleared region a contiguous whole-plane fill whose
    // guest-address extent equals the resident surface's extent. Anything else
    // leaves guest bytes the GL copy would not have.
    if (x_bytes != 0 || row_bytes != pitch || ymin != 0) return false;
    const uint64_t first_offset64 = static_cast<uint64_t>(surface_offset);
    const uint64_t last_offset64 =
        first_offset64 + (height - 1) * pitch + row_bytes - 1;
    if (last_offset64 > UINT32_MAX) return false;
    uint32_t last_address = 0;
    if (!dma_object_address(dma_instance, static_cast<uint32_t>(first_offset64),
                            first_address) ||
        !dma_object_address(dma_instance, static_cast<uint32_t>(last_offset64),
                            last_address) ||
        last_address < first_address ||
        static_cast<uint64_t>(last_address) - first_address !=
            last_offset64 - first_offset64) {
        return false;
    }
    span = static_cast<size_t>(static_cast<uint64_t>(last_address) -
                               first_address + 1);
    return true;
}

bool Nv2a::clear_surface_plane(uint32_t dma_instance, bool dma_bound,
                               uint32_t surface_offset, uint32_t pitch,
                               uint32_t bytes_per_pixel, uint32_t write_mask,
                               uint32_t clear_value, const char* plane,
                               uint32_t& first_address, bool skip_memory,
                               bool skip_invalidate) {
    const uint32_t xmin = clear_rect_horizontal_ & 0xFFFFu;
    const uint32_t xmax = clear_rect_horizontal_ >> 16;
    const uint32_t ymin = clear_rect_vertical_ & 0xFFFFu;
    const uint32_t ymax = clear_rect_vertical_ >> 16;
    if (!dma_bound || !pitch || xmax < xmin || ymax < ymin) {
        char why[128];
        std::snprintf(why, sizeof(why),
                      "NV2A CLEAR_SURFACE %s has unbound DMA or invalid pitch/rectangle",
                      plane);
        fault_(why);
        return false;
    }
    if ((bytes_per_pixel != 2 && bytes_per_pixel != 4) || !write_mask) {
        char why[128];
        std::snprintf(why, sizeof(why),
                      "NV2A CLEAR_SURFACE %s has invalid pixel format/mask", plane);
        fault_(why);
        return false;
    }
    const uint64_t width = static_cast<uint64_t>(xmax) - xmin + 1;
    const uint64_t height = static_cast<uint64_t>(ymax) - ymin + 1;
    const uint64_t row_bytes = width * bytes_per_pixel;
    const uint64_t x_bytes = static_cast<uint64_t>(xmin) * bytes_per_pixel;
    if (x_bytes + row_bytes > pitch) {
        char why[128];
        std::snprintf(why, sizeof(why),
                      "NV2A CLEAR_SURFACE %s rectangle exceeds pitch", plane);
        fault_(why);
        return false;
    }

    const uint64_t first_offset64 = static_cast<uint64_t>(surface_offset) +
                                    static_cast<uint64_t>(ymin) * pitch + x_bytes;
    const uint64_t last_offset64 = static_cast<uint64_t>(surface_offset) +
                                   static_cast<uint64_t>(ymax) * pitch + x_bytes +
                                   row_bytes - 1;
    if (first_offset64 > UINT32_MAX || last_offset64 > UINT32_MAX) {
        fault_("NV2A CLEAR_SURFACE offset overflows");
        return false;
    }
    uint32_t last_address = 0;
    if (!dma_object_address(dma_instance, static_cast<uint32_t>(first_offset64),
                            first_address) ||
        !dma_object_address(dma_instance, static_cast<uint32_t>(last_offset64),
                            last_address) ||
        last_address < first_address ||
        static_cast<uint64_t>(last_address) - first_address !=
            last_offset64 - first_offset64) {
        char why[128];
        std::snprintf(why, sizeof(why),
                      "NV2A CLEAR_SURFACE %s DMA range is invalid", plane);
        fault_(why);
        return false;
    }
    const size_t resolved_span =
        static_cast<size_t>(static_cast<uint64_t>(last_address) - first_address + 1);
    // beads-xbox.1.15.10 phase 3, step 3. The GL copy has already been cleared
    // by glClear and is the authoritative image; guest memory is stale by the
    // same residency contract every witnessed GPU draw already relies on, and
    // the next consumer resolves it. Resolving here would read back the
    // PRE-clear image and write it into memory this call is about to overwrite,
    // and invalidating would throw away the clear. Every fault check above this
    // point still runs, in order, so a malformed clear fails identically.
    if (skip_memory) return true;
    if (!resolve_renderer_color_overlap(first_address, resolved_span, "CLEAR_SURFACE") ||
        !resolve_renderer_depth_overlap(first_address, resolved_span, "CLEAR_SURFACE"))
        return false;

    std::vector<uint8_t> row(static_cast<size_t>(row_bytes));
    const uint32_t pixel_mask = bytes_per_pixel == 2 ? 0xFFFFu : 0xFFFFFFFFu;
    for (uint32_t y = ymin; y <= ymax; ++y) {
        uint32_t address = first_address + (y - ymin) * pitch;
        if (write_mask != pixel_mask) {
            if (!mem_read_ || !mem_read_(address, row.data(), row.size())) {
                char why[128];
                std::snprintf(why, sizeof(why),
                              "NV2A CLEAR_SURFACE %s DMA read failed at 0x%08x",
                              plane, address);
                fault_(why);
                return false;
            }
        }
        for (size_t x = 0; x < row.size(); x += bytes_per_pixel) {
            uint32_t old_pixel = 0;
            if (write_mask != pixel_mask)
                std::memcpy(&old_pixel, row.data() + x, bytes_per_pixel);
            const uint32_t pixel = (old_pixel & ~write_mask) |
                                   (clear_value & write_mask);
            std::memcpy(row.data() + x, &pixel, bytes_per_pixel);
        }
        if (!mem_write_ || !mem_write_(address, row.data(), row.size())) {
            char why[128];
            std::snprintf(why, sizeof(why),
                          "NV2A CLEAR_SURFACE %s DMA write failed at 0x%08x",
                          plane, address);
            fault_(why);
            return false;
        }
    }
    // skip_invalidate: the backend cleared the resident copy of this plane to
    // the same values the loop above just wrote into guest memory, so the two
    // agree and residency is KEPT. Dropping it here would force a full
    // re-upload for nothing -- that re-upload is what phase 3 set out to
    // remove. The overlap contract for OTHER resident surfaces is honoured by
    // clear_resident_surface, which invalidates them itself.
    if (skip_invalidate) return true;
    invalidate_renderer_color_overlap(
        first_address, resolved_span,
        nv2a_invalidate_token(kNv2aInvalidateClearSurface));
    invalidate_renderer_depth_overlap(
        first_address, resolved_span,
        nv2a_invalidate_token(kNv2aInvalidateClearSurface));
    return true;
}

bool Nv2a::clear_surface(uint32_t parameter) {
    // Seam anchor 2 (F4): a clear is a read-modify-write over the same
    // destination range as pending jobs and is ordered against them.
    seam_anchor(kSeamAnchorClearSurface, true);
    // NV2A_CLEAR: the plane fills reached from the CLEAR_SURFACE method, exclusive of the
    // resident-surface resolves they drive (those open NV2A_GL_RESOLVE).
    perf_excl::Scope region(perf_excl::kNv2aClear);
    constexpr uint32_t kKnownMask = 0xF3u;
    constexpr uint32_t kColorMask = 0xF0u;
    if (parameter & ~kKnownMask) {
        char why[128];
        std::snprintf(why, sizeof(why),
                      "NV2A CLEAR_SURFACE has unknown mask 0x%08x", parameter);
        fault_(why);
        return false;
    }
    if (!parameter) return true;

    const uint32_t surface_type = (surface_format_ >> 8) & 0xFu;
    const uint32_t anti_aliasing = (surface_format_ >> 12) & 0xFu;
    if (surface_type != 1 || anti_aliasing != 0) {
        fault_("NV2A CLEAR_SURFACE requires a non-antialiased pitch surface");
        return false;
    }

    const uint32_t xmin = clear_rect_horizontal_ & 0xFFFFu;
    const uint32_t xmax = clear_rect_horizontal_ >> 16;
    const uint32_t ymin = clear_rect_vertical_ & 0xFFFFu;
    const uint32_t ymax = clear_rect_vertical_ >> 16;
    if (xmax < xmin || ymax < ymin) {
        fault_("NV2A CLEAR_SURFACE has an inverted rectangle");
        return false;
    }
    const uint64_t width = static_cast<uint64_t>(xmax) - xmin + 1;
    const uint64_t height = static_cast<uint64_t>(ymax) - ymin + 1;

    // ---- step 3: serve the clear as a glClear on the resident pair ---------
    //
    // MEASURED reason this exists (phase 3 census, 12 B canonical, shipping
    // defaults): CLEAR_SURFACE is the LARGEST single cause of witnessed
    // residency breaks -- 535 colour plane re-uploads (1131.5 MB) and 398 zeta
    // re-uploads (810.1 MB), against 51+262 for kPitchProjectedColor and 74+0
    // for the additive profile -- plus 358 zeta and 15 colour resolves
    // (746.0 MB) taken on the way in. The simulation census reported
    // cause=clear_surface resolves=0 because it MODELS a glClear that the
    // device never actually asked the backend for: clear_resident_surface has
    // existed since phase 1 and had no caller. This is that caller.
    //
    // Eligibility is deliberately narrow and is all-or-nothing across the pair:
    // a clear that fills one plane on the GPU and the other in guest memory
    // would leave the slot half authoritative on each side.
    bool resident_cleared_color = false;
    bool resident_cleared_zeta = false;
    // The plane was clean: the backend cleared the GL copy, the software fill
    // below writes the same values into guest memory, and residency is kept
    // WITHOUT making the plane dirty. No resolve, no re-upload.
    bool resident_kept_clean_color = false;
    bool resident_kept_clean_zeta = false;
    if (gpu_clear_enabled_ && renderer_ != nullptr) {
        const uint32_t color_format_id = surface_format_ & 0xFu;
        const uint32_t zeta_format_id = (surface_format_ >> 4) & 0xFu;
        const bool color_is32 = color_format_id >= 4 && color_format_id <= 8;
        const bool zeta_is32 = zeta_format_id == 2;
        // Each plane is decided on its own. The pair-only form declined every
        // clear on this workload -- 457 of 768 CLEAR_SURFACE calls at 2 G
        // instructions request exactly one plane -- and a plane is its own
        // resident image with its own dirty flag, so there is no coupling to
        // preserve. A partially masked plane stays on the software path: it is
        // a read-modify-write of guest bytes, not a fill.
        const bool want_color = (parameter & kColorMask) == kColorMask &&
                                color_is32;
        const bool want_zeta = (parameter & 3u) == 3u && zeta_is32;
        const bool partial_color =
            (parameter & kColorMask) != 0 && !want_color;
        const bool partial_zeta = (parameter & 3u) != 0 && !want_zeta;
        if (partial_color || partial_zeta) ++gpu_clear_decline_mask_;
        uint32_t color_first = 0, zeta_first = 0;
        size_t color_span = 0, zeta_span = 0;
        const bool color_ok =
            want_color &&
            clear_plane_extent(color_dma_, color_dma_bound_,
                               surface_color_offset_, surface_pitch_ & 0xFFFFu,
                               4u, 0xFFFFFFFFu, color_first, color_span);
        const bool zeta_ok =
            want_zeta &&
            clear_plane_extent(zeta_dma_, zeta_dma_bound_,
                               surface_zeta_offset_, surface_pitch_ >> 16, 4u,
                               0xFFFFFFFFu, zeta_first, zeta_span);
        if ((want_color && !color_ok) || (want_zeta && !zeta_ok))
            ++gpu_clear_decline_geometry_;
        // Two separate attempts, so a clear that can serve one plane on the GPU
        // is not forced back onto the software path for both.
        for (int plane = 0; plane < 2; ++plane) {
            const bool is_color = plane == 0;
            if (is_color ? !color_ok : !zeta_ok) continue;
            std::string error;
            bool needs_guest_fill = true;
            perf_excl::Scope gl_region(perf_excl::kNv2aGl);
            const bool ok = renderer_->clear_resident_surface(
                is_color ? color_first : 0u, is_color ? color_span : 0u,
                is_color, color_clear_value_, is_color ? 0u : zeta_first,
                is_color ? 0u : zeta_span, !is_color, zeta_clear_value_,
                needs_guest_fill, error);
            if (ok) {
                if (is_color) {
                    resident_cleared_color = !needs_guest_fill;
                    resident_kept_clean_color = needs_guest_fill;
                    if (!needs_guest_fill) gpu_clear_bytes_ += color_span;
                } else {
                    resident_cleared_zeta = !needs_guest_fill;
                    resident_kept_clean_zeta = needs_guest_fill;
                    if (!needs_guest_fill) gpu_clear_bytes_ += zeta_span;
                }
                ++gpu_clear_accepted_;
                if (needs_guest_fill) ++gpu_clear_kept_clean_;
            } else if (!error.empty()) {
                fault_(error.c_str());
                return false;
            } else {
                ++gpu_clear_decline_resident_;
            }
        }
    }

    if (parameter & kColorMask) {
        const uint32_t color_format = surface_format_ & 0xFu;
        uint32_t bytes_per_pixel = 0, write_mask = 0;
        switch (color_format) {
        case 1:
        case 2:
            bytes_per_pixel = 2;
            if (parameter & 0x10) write_mask |= 0x7C00u;
            if (parameter & 0x20) write_mask |= 0x03E0u;
            if (parameter & 0x40) write_mask |= 0x001Fu;
            if (parameter & 0x80) write_mask |= 0x8000u;
            break;
        case 3:
            bytes_per_pixel = 2;
            if (parameter & 0x10) write_mask |= 0xF800u;
            if (parameter & 0x20) write_mask |= 0x07E0u;
            if (parameter & 0x40) write_mask |= 0x001Fu;
            break;
        case 4:
        case 5:
        case 6:
        case 7:
        case 8:
            bytes_per_pixel = 4;
            if (parameter & 0x10) write_mask |= 0x00FF0000u;
            if (parameter & 0x20) write_mask |= 0x0000FF00u;
            if (parameter & 0x40) write_mask |= 0x000000FFu;
            if (parameter & 0x80) write_mask |= 0xFF000000u;
            break;
        default:
            fault_("NV2A CLEAR_SURFACE has unsupported color format");
            return false;
        }
        uint32_t first_address = 0;
        if (!clear_surface_plane(color_dma_, color_dma_bound_, surface_color_offset_,
                                 surface_pitch_ & 0xFFFFu, bytes_per_pixel, write_mask,
                                 color_clear_value_, "color", first_address,
                                 resident_cleared_color,
                                 resident_cleared_color ||
                                     resident_kept_clean_color))
            return false;
        ++color_clears_;
        color_clear_pixels_ += width * height;
        last_color_clear_address_ = first_address;
        if (surface_write_debug_cb_ && bytes_per_pixel == 4) {
            SurfaceWriteDebugInfo info;
            info.kind = "clear-color";
            info.sequence = color_clears_;
            info.writes = width * height;
            info.color_address = first_address;
            info.pitch = surface_pitch_ & 0xFFFFu;
            info.width = static_cast<uint32_t>(width);
            info.height = static_cast<uint32_t>(height);
            info.surface_format = surface_format_;
            surface_write_debug_cb_(info);
        }
    }

    if (parameter & 3u) {
        const uint32_t zeta_format = (surface_format_ >> 4) & 0xFu;
        uint32_t bytes_per_pixel = 0, write_mask = 0;
        if (zeta_format == 1) {
            bytes_per_pixel = 2;
            if (parameter & 1u) write_mask = 0xFFFFu;
            if (parameter & 2u) {
                fault_("NV2A CLEAR_SURFACE stencil requested for Z16");
                return false;
            }
        } else if (zeta_format == 2) {
            bytes_per_pixel = 4;
            if (parameter & 1u) write_mask |= 0xFFFFFF00u;
            if (parameter & 2u) write_mask |= 0x000000FFu;
        } else {
            fault_("NV2A CLEAR_SURFACE has unsupported zeta format");
            return false;
        }
        uint32_t first_address = 0;
        if (!clear_surface_plane(zeta_dma_, zeta_dma_bound_, surface_zeta_offset_,
                                 surface_pitch_ >> 16, bytes_per_pixel, write_mask,
                                 zeta_clear_value_, "zeta", first_address,
                                 resident_cleared_zeta,
                                 resident_cleared_zeta ||
                                     resident_kept_clean_zeta))
            return false;
        ++zeta_clears_;
        zeta_clear_pixels_ += width * height;
        last_zeta_clear_address_ = first_address;
    }
    return true;
}

bool Nv2a::set_vertex_array_format(uint32_t slot, uint32_t parameter) {
    auto& attr = vertex_attributes_[slot];
    attr.format = static_cast<uint8_t>(parameter & 0xFu);
    attr.count = static_cast<uint8_t>((parameter >> 4) & 0xFu);
    attr.stride = parameter >> 8;
    if (!attr.count) {
        attr.element_size = 0;
        return true;
    }
    if (attr.count > 4) {
        fault_("NV2A vertex array has more than four components");
        return false;
    }
    switch (attr.format) {
    case 0:  // UB_D3D
        if (attr.count != 4) {
            fault_("NV2A UB_D3D vertex array must have four components");
            return false;
        }
        attr.element_size = 1;
        break;
    case 1:  // signed normalized 16-bit
        attr.element_size = 2;
        break;
    case 2:  // float
        attr.element_size = 4;
        break;
    case 4:  // UB_OGL
        attr.element_size = 1;
        break;
    case 5:  // signed 16-bit, unnormalized
        attr.element_size = 2;
        break;
    case 6:  // signed normalized packed 11/11/10
        if (attr.count != 1) {
            fault_("NV2A CMP vertex array must have one packed component");
            return false;
        }
        attr.element_size = 4;
        break;
    default:
        fault_("NV2A vertex array has an unsupported format");
        return false;
    }
    return true;
}

bool Nv2a::fetch_vertex_attribute(uint32_t slot, uint32_t index) {
    auto& attr = vertex_attributes_[slot];
    if (!attr.count) return true;

    const uint32_t dma_instance = attr.dma_b ? vertex_dma_b_ : vertex_dma_a_;
    const bool dma_bound = attr.dma_b ? vertex_dma_b_bound_ : vertex_dma_a_bound_;
    if (!dma_bound) {
        char why[128];
        std::snprintf(why, sizeof(why),
                      "NV2A vertex attribute %u uses unbound DMA %c",
                      slot, attr.dma_b ? 'B' : 'A');
        fault_(why);
        return false;
    }

    const uint64_t element_bytes =
        static_cast<uint64_t>(attr.element_size) * attr.count;
    const uint64_t offset64 = static_cast<uint64_t>(attr.offset) +
                              static_cast<uint64_t>(attr.stride) * index;
    if (!element_bytes || offset64 > UINT32_MAX ||
        element_bytes - 1 > UINT32_MAX - offset64) {
        fault_("NV2A vertex attribute offset overflows");
        return false;
    }
    uint32_t first_address = 0, last_address = 0;
    if (!dma_object_address(dma_instance, static_cast<uint32_t>(offset64),
                            first_address) ||
        !dma_object_address(dma_instance,
                            static_cast<uint32_t>(offset64 + element_bytes - 1),
                            last_address) ||
        last_address < first_address ||
        static_cast<uint64_t>(last_address) - first_address != element_bytes - 1) {
        char why[160];
        std::snprintf(why, sizeof(why),
                      "NV2A vertex attribute %u index %u exceeds DMA range",
                      slot, index);
        fault_(why);
        return false;
    }

    std::array<uint8_t, 16> data{};
    const uint8_t* source = nullptr;
    if (mem_direct_) {
        source = mem_direct_(first_address, static_cast<size_t>(element_bytes));
    }
    if (source) {
        std::memcpy(data.data(), source, static_cast<size_t>(element_bytes));
    } else {
        if (!mem_read_ ||
            !mem_read_(first_address, data.data(), static_cast<size_t>(element_bytes))) {
            char why[160];
            std::snprintf(why, sizeof(why),
                          "NV2A vertex attribute %u DMA read failed at 0x%08x",
                          slot, first_address);
            fault_(why);
            return false;
        }
    }

    attr.value = {{0.0f, 0.0f, 0.0f, 1.0f}};
    switch (attr.format) {
    case 0:
    case 4:
        for (uint32_t i = 0; i < attr.count; ++i)
            attr.value[i] = static_cast<float>(data[i]) / 255.0f;
        break;
    case 1:
        for (uint32_t i = 0; i < attr.count; ++i) {
            int16_t value = 0;
            std::memcpy(&value, data.data() + i * 2, 2);
            attr.value[i] = value == INT16_MIN
                                ? -1.0f
                                : static_cast<float>(value) / 32767.0f;
        }
        break;
    case 2:
        std::memcpy(attr.value.data(), data.data(),
                    static_cast<size_t>(element_bytes));
        break;
    case 5:
        for (uint32_t i = 0; i < attr.count; ++i) {
            int16_t value = 0;
            std::memcpy(&value, data.data() + i * 2, 2);
            attr.value[i] = static_cast<float>(value);
        }
        break;
    case 6: {
        int32_t packed = 0;
        std::memcpy(&packed, data.data(), 4);
        int32_t x = packed & 0x7FF;
        int32_t y = (packed >> 11) & 0x7FF;
        int32_t z = (packed >> 22) & 0x3FF;
        if (x & 0x400) x |= ~0x7FF;
        if (y & 0x400) y |= ~0x7FF;
        if (z & 0x200) z |= ~0x3FF;
        attr.value[0] = x == -1024 ? -1.0f : static_cast<float>(x) / 1023.0f;
        attr.value[1] = y == -1024 ? -1.0f : static_cast<float>(y) / 1023.0f;
        attr.value[2] = z == -512 ? -1.0f : static_cast<float>(z) / 511.0f;
        break;
    }
    default:
        fault_("NV2A vertex attribute format changed after validation");
        return false;
    }

    // beads-xbox.1.14.3 item 4 completed: --perf-accounting must NOT change the code
    // being timed. perf::g_enabled was still OR'd in here, so every attribute fetch on
    // an instrumented run did three extra counter increments the noperf run did not.
    // Detailed telemetry is now the only switch; use --nv2a-detailed-telemetry to get
    // these counters back alongside --perf-accounting.
    if (detailed_telemetry_) {
        ++vertex_attribute_fetches_;
        ++vertex_format_fetches_[attr.format];
        ++vertex_slot_fetches_[slot];
    }
    return true;
}

bool Nv2a::commit_immediate_vertex() {
    if (primitive_mode_ == 0) {
        fault_("NV2A immediate vertex submitted outside BEGIN/END");
        return false;
    }
    if (immediate_vertices_.size() >= (1u << 24)) {
        fault_("NV2A immediate vertex batch exceeds safety bound");
        return false;
    }
    VertexInputs inputs{};
    for (uint32_t slot = 0; slot < vertex_attributes_.size(); ++slot)
        inputs[slot] = vertex_attributes_[slot].value;
    immediate_vertices_.push_back(inputs);
    return true;
}

bool Nv2a::decode_inline_array() {
    inline_vertices_.clear();
    if (inline_array_words_.empty()) return true;

    std::array<size_t, 16> offsets{};
    size_t vertex_size = 0;
    bool has_position = false;
    for (size_t slot = 0; slot < vertex_attributes_.size(); ++slot) {
        const auto& attr = vertex_attributes_[slot];
        if (!attr.count) continue;
        if (!attr.element_size) {
            fault_("NV2A inline array has an invalid attribute size");
            return false;
        }
        const size_t alignment = attr.element_size;
        vertex_size = (vertex_size + alignment - 1) & ~(alignment - 1);
        offsets[slot] = vertex_size;
        const size_t bytes =
            static_cast<size_t>(attr.element_size) * attr.count;
        if (vertex_size > SIZE_MAX - bytes) {
            fault_("NV2A inline array vertex layout overflows");
            return false;
        }
        vertex_size += bytes;
        vertex_size = (vertex_size + alignment - 1) & ~(alignment - 1);
        if (slot == 0) has_position = true;
    }
    if (!has_position || !vertex_size) {
        fault_("NV2A inline array has no position attribute");
        return false;
    }

    const size_t total_bytes = inline_array_words_.size() * sizeof(uint32_t);
    const size_t vertex_count = total_bytes / vertex_size;
    if (!vertex_count || vertex_count >= (1u << 24)) {
        fault_("NV2A inline array vertex count exceeds safety bound");
        return false;
    }
    const auto* bytes =
        reinterpret_cast<const uint8_t*>(inline_array_words_.data());
    inline_vertices_.resize(vertex_count);
    for (size_t vertex_index = 0; vertex_index < vertex_count;
         ++vertex_index) {
        auto& inputs = inline_vertices_[vertex_index];
        for (size_t slot = 0; slot < vertex_attributes_.size(); ++slot) {
            auto& attr = vertex_attributes_[slot];
            inputs[slot] = attr.value;
            if (!attr.count) continue;
            const uint8_t* data =
                bytes + vertex_index * vertex_size + offsets[slot];
            auto& value = inputs[slot];
            value = {{0.0f, 0.0f, 0.0f, 1.0f}};
            switch (attr.format) {
            case 0:
            case 4:
                for (uint32_t component = 0; component < attr.count;
                     ++component) {
                    value[component] =
                        static_cast<float>(data[component]) / 255.0f;
                }
                break;
            case 1:
                for (uint32_t component = 0; component < attr.count;
                     ++component) {
                    int16_t element = 0;
                    std::memcpy(&element, data + component * 2, 2);
                    value[component] =
                        element == INT16_MIN
                            ? -1.0f
                            : static_cast<float>(element) / 32767.0f;
                }
                break;
            case 2:
                std::memcpy(value.data(), data,
                            static_cast<size_t>(attr.element_size) *
                                attr.count);
                break;
            case 5:
                for (uint32_t component = 0; component < attr.count;
                     ++component) {
                    int16_t element = 0;
                    std::memcpy(&element, data + component * 2, 2);
                    value[component] = static_cast<float>(element);
                }
                break;
            case 6: {
                int32_t packed = 0;
                std::memcpy(&packed, data, sizeof(packed));
                int32_t x = packed & 0x7FF;
                int32_t y = (packed >> 11) & 0x7FF;
                int32_t z = (packed >> 22) & 0x3FF;
                if (x & 0x400) x |= ~0x7FF;
                if (y & 0x400) y |= ~0x7FF;
                if (z & 0x200) z |= ~0x3FF;
                value[0] =
                    x == -1024 ? -1.0f : static_cast<float>(x) / 1023.0f;
                value[1] =
                    y == -1024 ? -1.0f : static_cast<float>(y) / 1023.0f;
                value[2] =
                    z == -512 ? -1.0f : static_cast<float>(z) / 511.0f;
                break;
            }
            default:
                fault_("NV2A inline array format changed after validation");
                inline_vertices_.clear();
                return false;
            }
            ++inline_attribute_decodes_;
        }
    }
    ++inline_array_batches_;
    inline_array_words_total_ += inline_array_words_.size();
    inline_array_vertices_total_ += vertex_count;
    return true;
}

// ==================== stage (d): the latch/execute seam =====================
// DESIGN-NV2A-SEAM.md sections 1.1-1.5 and 3.1, bead beads-xbox.1.15.4.
//
// The producer/consumer boundary is the draw-batch boundary inside
// submit_vertex_batch: after vertex transform has filled transformed_batch_ and
// before any pixel is touched. latch_draw_job() takes owned copies of everything
// the fragment loop reads that a later method in the same pushbuffer batch could
// still change; execute_draw_job() is the ONLY implementation of the pixel math.
// In stage (d) the job is executed inline, immediately, at the latch point --
// which is also, by construction, what stage (e)'s toggle-off path does.
const char* Nv2a::seam_anchor_name(size_t anchor) {
    switch (anchor) {
    case kSeamAnchorSemaphoreRelease: return "semaphore_release";
    case kSeamAnchorClearSurface:     return "clear_surface";
    case kSeamAnchorScanout:          return "scanout";
    case kSeamAnchorSetFrame:         return "set_frame";
    case kSeamAnchorFlipStallClear:   return "flip_stall_clear";
    case kSeamAnchorTextureOverlap:   return "texture_overlap";
    case kSeamAnchorPgraphRegisterRead: return "pgraph_register_read";
    case kSeamAnchorResolveOverlap:   return "resolve_overlap";
    case kSeamAnchorSurfaceRetarget:  return "surface_retarget";
    case kSeamAnchorCheckpoint:       return "checkpoint";
    default: break;
    }
    return "unknown";
}

const char* Nv2a::seam_reason_name(uint32_t reason) {
    switch (reason) {
    case kSeamThreadable:       return "threadable";
    case kSeamNoDirectPointer:  return "no_direct_ptr";
    case kSeamHashOn:           return "hash_on";
    case kSeamRendererPath:     return "f3_renderer_path";
    case kSeamDebugCallback:    return "f15_debug_cb";
    default: break;
    }
    return "unknown";
}

void Nv2a::latch_draw_job(DrawJob& job) {
    ++seam_jobs_latched_;
    // Identity / ordering. job.seq is read back by the executor for the F15
    // surface-write debug record, exactly where it used to read vertex_batches_.
    job.seq = vertex_batches_;
    job.primitive_mode = primitive_mode_;
    // Register mirrors: the full raster_state_ block plus the surface scalars.
    // SET_SURFACE_* and every combiner method can rewrite any of these before a
    // worker would run, so they are copied, not referenced.
    job.raster_state = raster_state_;
    job.surface_format = surface_format_;
    job.surface_pitch = surface_pitch_;
    job.surface_clip_horizontal = surface_clip_horizontal_;
    job.surface_clip_vertical = surface_clip_vertical_;
    job.surface_color_offset = surface_color_offset_;
    job.surface_zeta_offset = surface_zeta_offset_;
    // F16: by value. raster_state_samples_ can reallocate under a raw pointer.
    job.raster_state_sample_index = current_raster_state_sample_index_;
    job.hash_on = profile_hashes_enabled_;
    // F15: any diagnostic callback pins the job to the emulation thread.
    job.debug_cb_installed = static_cast<bool>(surface_write_debug_cb_) ||
                             static_cast<bool>(generic_batch_debug_cb_) ||
                             static_cast<bool>(generic_packet_snapshot_cb_);
    // The single most dangerous shared buffer (section 1.2): transformed_batch_
    // is one member vector that the very next batch clear()s. MOVED, not copied.
    job.vertices = std::move(transformed_batch_);
    seed_draw_telemetry(job);
}

// The twelve per-write FNV-1a accumulators are order-dependent chains, so a job
// cannot start one from zero: it continues the run's chain. Seeding at the latch
// and writing back at the fold is exact because hash_on forces the job INLINE
// (design section 4.3 option O3), i.e. the seed is always the value left by the
// immediately preceding job. Skipped entirely when the fold is off, which is the
// configuration a threaded run uses.
void Nv2a::seed_draw_telemetry(DrawJob& job) {
    if (job.hash_on) {
        job.out.inline_color_output_hash = inline_color_output_hash_;
        job.out.inline_linear_blit_output_hash = inline_linear_blit_output_hash_;
        job.out.pitch_projected_color_output_hash = pitch_projected_color_output_hash_;
        job.out.pitch_multi_texture_output_hash = pitch_multi_texture_output_hash_;
        job.out.pitch_three_texture_output_hash = pitch_three_texture_output_hash_;
        job.out.pitch_single_texture_output_hash = pitch_single_texture_output_hash_;
        job.out.pitch_tex5_output_hash = pitch_tex5_output_hash_;
        job.out.pitch_textured_color_output_hash = pitch_textured_color_output_hash_;
        job.out.projected_color_output_hash = projected_color_output_hash_;
        job.out.radial_color_output_hash = radial_color_output_hash_;
        job.out.raster_output_hash = raster_output_hash_;
        job.out.tex7_fan_output_hash = tex7_fan_output_hash_;
        job.out.texf_projected_fan_output_hash = texf_projected_fan_output_hash_;
        job.out.textured_color_output_hash = textured_color_output_hash_;
    }
}

bool Nv2a::execute_draw_job(DrawJob& job, bool& executed) {
    // The one and only entry to the pixel math. Stage (e) hands the same job to
    // a worker which calls this same function; there is no second copy of the
    // fragment loop and no #ifdef.
    return rasterize_witnessed_batch(job, executed);
}

void Nv2a::seam_note_threadable(DrawJob& job, bool threadable, uint32_t reason) {
    job.threadable = threadable;
    job.not_threadable_reason = threadable ? kSeamThreadable : reason;
    // The M5 queue simulation asks a COHERENCE question, not a telemetry one:
    // how deep would the ring be at each anchor, in the configuration stage (e)
    // would actually run in? The per-write FNV fold (section 4.3 option O3) is a
    // runtime switch that stage (e) turns off, so a job held back only by
    // hash_on is still queueable for the simulation. A job held back by the
    // direct-pointer rule, the GL path or a debug callback is not: those are
    // structural, and no flag makes them go away.
    job.queueable = threadable || reason == kSeamHashOn;
}

#if XLLE_VSH_DRAW_OVERLAP_CENSUS
void Nv2a::record_vsh_draw_overlap(DrawJob& job,
                                   VshDrawOverlapOutcome fallback) {
    if (!vsh_draw_overlap_enabled_) return;
    if (!job.vsh_overlap.valid || job.vsh_overlap.counted) return;
    if (job.vsh_overlap.outcome == VshDrawOverlapOutcome::kUnknown)
        job.vsh_overlap.outcome = fallback;
    vsh_draw_overlap_record(vsh_draw_overlap_census_, job.vsh_overlap);
}
#endif

void Nv2a::retire_draw_job(DrawJob& job, bool executed) {
    if (job.deferred) {
        // Stage (e): the fragment loop has NOT run; the job is being handed to
        // the worker. Its census is taken here, at the latch point, exactly
        // where the inline path takes it, so the seam-census numbers mean the
        // same thing in both modes. Its telemetry and its deferred guest-visible
        // side effects are folded later, in latch order, by the drain that
        // retires it. `executed` is true for every deferred job by construction:
        // only a matched witnessed profile can set job.threadable.
        ++seam_jobs_threadable_;
        seam_publish_job(job);
        draw_queue_publish(job);
        return;
    }
    // Hand the transform buffer -- and its capacity -- back, so the next batch's
    // clear()/reserve() and the last_transformed_* accessors behave exactly as
    // they did before the split. This is what makes the move free.
    transformed_batch_ = std::move(job.vertices);
    if (!executed) {
#if XLLE_VSH_DRAW_OVERLAP_CENSUS
        record_vsh_draw_overlap(job, VshDrawOverlapOutcome::kNoRaster);
#endif
        ++seam_jobs_not_executed_;
        return;
    }
    ++draw_jobs_inline_;
    // Stage (e2): a job that ran as ONE part while N >= 2 was configured could
    // not be divided -- the immediate-quad family and the generic-combiner GL
    // path are structurally inline, and a witnessed job can be held back by the
    // direct-pointer rule, the hash fold or a debug callback. Counted here, at
    // the single funnel every executed non-deferred job passes through, so the
    // census covers all three families rather than only the witnessed one.
    if (draw_threads_ > 1 && job.span_parts == 1) ++draw_span_inline_jobs_;
    fold_draw_job(job);
    if (job.threadable) {
        ++seam_jobs_threadable_;
    } else {
        const uint32_t reason =
            job.not_threadable_reason < kSeamNotThreadableReasons
                ? job.not_threadable_reason : kSeamNoDirectPointer;
        ++seam_not_threadable_[reason];
    }
    seam_publish_job(job);
}

// Apply everything a finished job owes the device. Emulation thread only, in
// latch order: for an inline job this runs immediately after the fragment loop,
// for a queued job at the drain that retires it. Because there is exactly one
// consumer executing strictly in order, the fold sequence IS the single-threaded
// sequence, which is what makes the counters and the FNV chains identical.
void Nv2a::fold_draw_job(DrawJob& job) {
    // Part 0 first, then the stage (e2) helper parts in index order. Every field
    // fold_draw_telemetry touches is a pure sum or a last-write-wins cell whose
    // value is job-invariant, so this is order-independent and equal to the
    // single-threaded fold; the enumeration that proves it is in nv2a.hpp above
    // execute_witnessed_fragments. There is deliberately no second summation
    // list: the same 110-line fold runs once per part.
    //
    // beads-xbox.1.15.9 loose end 2, closed: draw_span_fold_ticks_ is now
    // MEASURED rather than estimated. The fold runs span_parts times instead of
    // once, so this is the direct cost of the partition's telemetry merge. The
    // two clock reads are gated on perf_excl::g_enabled so an uninstrumented
    // timing leg -- the runs the gate is decided on -- executes exactly the code
    // it executed before this line existed. That is the same rule the rest of
    // this module follows: an instrument may not perturb the number it is not
    // being asked for.
    const bool time_fold = perf_excl::g_enabled;
    const uint64_t fold_t0 = time_fold ? perf_excl::tick_now() : 0;
    fold_draw_telemetry(job.out, job.hash_on);
    for (unsigned p = 1; p < job.span_parts; ++p)
        fold_draw_telemetry(span_out_[p], job.hash_on);
    if (time_fold) draw_span_fold_ticks_ += perf_excl::tick_now() - fold_t0;
    DrawJobTelemetry& out = job.out;
    // F3: the renderer invalidations the executor recorded instead of making.
    if (out.invalidate_color) {
        invalidate_renderer_color_overlap(out.invalidate_color_address,
                                          out.invalidate_color_bytes,
                                          out.invalidate_color_cause);
    }
    if (out.invalidate_depth) {
        invalidate_renderer_depth_overlap(out.invalidate_depth_address,
                                          out.invalidate_depth_bytes,
                                          out.invalidate_depth_cause);
    }
    // F13: the executor may not fault_. It recorded the first message; the fault
    // is raised here, on the emulation thread, with the original text. For a
    // queued job this is a DELAYED attribution -- the guest instruction blamed is
    // the one that reached the drain, not the one that issued the draw -- so any
    // faulting threaded run must be re-run with --nv2a-draw-threads 0 to get the
    // true attribution.
#if XLLE_VSH_DRAW_OVERLAP_CENSUS
    if (out.failed || !out.ok)
        job.vsh_overlap.outcome = VshDrawOverlapOutcome::kFault;
    record_vsh_draw_overlap(job, VshDrawOverlapOutcome::kCpuWitnessed);
#endif
    if (out.failed) fault_(out.fault_message);
    if (!out.ok) ++draw_worker_failures_;
}

void Nv2a::fold_draw_telemetry(const DrawJobTelemetry& out, bool hash_on) {
    depth_raster_batches_ += out.depth_raster_batches;
    depth_test_passes_ += out.depth_test_passes;
    depth_writes_ += out.depth_writes;
    inline_color_alpha_passes_ += out.inline_color_alpha_passes;
    inline_color_batches_ += out.inline_color_batches;
    inline_color_fragments_ += out.inline_color_fragments;
    inline_color_triangles_ += out.inline_color_triangles;
    inline_color_writes_ += out.inline_color_writes;
    inline_linear_blit_batches_ += out.inline_linear_blit_batches;
    inline_linear_blit_fragments_ += out.inline_linear_blit_fragments;
    inline_linear_blit_samples_ += out.inline_linear_blit_samples;
    inline_linear_blit_triangles_ += out.inline_linear_blit_triangles;
    inline_linear_blit_writes_ += out.inline_linear_blit_writes;
    inline_texture_samples_ += out.inline_texture_samples;
    pitch_multi_texture_alpha_passes_ += out.pitch_multi_texture_alpha_passes;
    pitch_three_texture_rescale_direction0_ += out.pitch_three_texture_rescale_direction0;
    pitch_three_texture_rescale_direction1_ += out.pitch_three_texture_rescale_direction1;
    pitch_three_texture_zero_direction0_ += out.pitch_three_texture_zero_direction0;
    pitch_three_texture_zero_direction1_ += out.pitch_three_texture_zero_direction1;
    pitch_three_texture_xy_tie_direction0_ += out.pitch_three_texture_xy_tie_direction0;
    pitch_three_texture_xy_tie_direction1_ += out.pitch_three_texture_xy_tie_direction1;
    pitch_multi_texture_batches_ += out.pitch_multi_texture_batches;
    pitch_multi_texture_color_writes_ += out.pitch_multi_texture_color_writes;
    pitch_multi_texture_depth_passes_ += out.pitch_multi_texture_depth_passes;
    pitch_multi_texture_depth_writes_ += out.pitch_multi_texture_depth_writes;
    pitch_multi_texture_fragments_ += out.pitch_multi_texture_fragments;
    pitch_multi_texture_samples_ += out.pitch_multi_texture_samples;
    pitch_multi_texture_triangles_ += out.pitch_multi_texture_triangles;
    pitch_projected_color_batches_ += out.pitch_projected_color_batches;
    pitch_projected_color_depth_passes_ += out.pitch_projected_color_depth_passes;
    pitch_projected_color_depth_writes_ += out.pitch_projected_color_depth_writes;
    pitch_projected_color_fragments_ += out.pitch_projected_color_fragments;
    pitch_projected_color_triangles_ += out.pitch_projected_color_triangles;
    pitch_projected_color_writes_ += out.pitch_projected_color_writes;
    pitch_projected_texture_samples_ += out.pitch_projected_texture_samples;
    pitch_single_texture_batches_ += out.pitch_single_texture_batches;
    pitch_single_texture_depth_passes_ += out.pitch_single_texture_depth_passes;
    pitch_single_texture_fragments_ += out.pitch_single_texture_fragments;
    pitch_single_texture_samples_ += out.pitch_single_texture_samples;
    pitch_single_texture_triangles_ += out.pitch_single_texture_triangles;
    pitch_single_texture_writes_ += out.pitch_single_texture_writes;
    pitch_tex5_batches_ += out.pitch_tex5_batches;
    pitch_tex5_color_writes_ += out.pitch_tex5_color_writes;
    pitch_tex5_depth_passes_ += out.pitch_tex5_depth_passes;
    pitch_tex5_depth_writes_ += out.pitch_tex5_depth_writes;
    pitch_tex5_fragments_ += out.pitch_tex5_fragments;
    pitch_tex5_samples_ += out.pitch_tex5_samples;
    pitch_tex5_triangles_ += out.pitch_tex5_triangles;
    pitch_texture_samples_ += out.pitch_texture_samples;
    pitch_textured_color_batches_ += out.pitch_textured_color_batches;
    pitch_textured_color_depth_passes_ += out.pitch_textured_color_depth_passes;
    pitch_textured_color_depth_writes_ += out.pitch_textured_color_depth_writes;
    pitch_textured_color_fragments_ += out.pitch_textured_color_fragments;
    pitch_textured_color_triangles_ += out.pitch_textured_color_triangles;
    pitch_textured_color_writes_ += out.pitch_textured_color_writes;
    pitch_three_texture_batches_ += out.pitch_three_texture_batches;
    pitch_three_texture_color_writes_ += out.pitch_three_texture_color_writes;
    pitch_three_texture_depth_passes_ += out.pitch_three_texture_depth_passes;
    pitch_three_texture_depth_writes_ += out.pitch_three_texture_depth_writes;
    pitch_three_texture_fragments_ += out.pitch_three_texture_fragments;
    pitch_three_texture_samples_ += out.pitch_three_texture_samples;
    pitch_three_texture_triangles_ += out.pitch_three_texture_triangles;
    projected_color_batches_ += out.projected_color_batches;
    projected_color_depth_passes_ += out.projected_color_depth_passes;
    projected_color_depth_writes_ += out.projected_color_depth_writes;
    projected_color_fragments_ += out.projected_color_fragments;
    projected_color_triangles_ += out.projected_color_triangles;
    projected_color_writes_ += out.projected_color_writes;
    projected_texture_samples_ += out.projected_texture_samples;
    radial_color_batches_ += out.radial_color_batches;
    radial_color_depth_passes_ += out.radial_color_depth_passes;
    radial_color_depth_writes_ += out.radial_color_depth_writes;
    radial_color_fragments_ += out.radial_color_fragments;
    radial_color_triangles_ += out.radial_color_triangles;
    radial_color_writes_ += out.radial_color_writes;
    rasterized_fragments_ += out.rasterized_fragments;
    rasterized_triangles_ += out.rasterized_triangles;
    tex7_fan_batches_ += out.tex7_fan_batches;
    tex7_fan_depth_passes_ += out.tex7_fan_depth_passes;
    tex7_fan_fragments_ += out.tex7_fan_fragments;
    tex7_fan_samples_ += out.tex7_fan_samples;
    tex7_fan_triangles_ += out.tex7_fan_triangles;
    tex7_fan_writes_ += out.tex7_fan_writes;
    texf_projected_fan_alpha_passes_ += out.texf_projected_fan_alpha_passes;
    texf_projected_fan_batches_ += out.texf_projected_fan_batches;
    texf_projected_fan_fragments_ += out.texf_projected_fan_fragments;
    texf_projected_fan_triangles_ += out.texf_projected_fan_triangles;
    texf_projected_fan_writes_ += out.texf_projected_fan_writes;
    texture_samples_ += out.texture_samples;
    textured_color_batches_ += out.textured_color_batches;
    textured_color_depth_passes_ += out.textured_color_depth_passes;
    textured_color_depth_writes_ += out.textured_color_depth_writes;
    textured_color_fragments_ += out.textured_color_fragments;
    textured_color_triangles_ += out.textured_color_triangles;
    textured_color_writes_ += out.textured_color_writes;
    if (hash_on) {
        inline_color_output_hash_ = out.inline_color_output_hash;
        inline_linear_blit_output_hash_ = out.inline_linear_blit_output_hash;
        pitch_projected_color_output_hash_ = out.pitch_projected_color_output_hash;
        pitch_multi_texture_output_hash_ = out.pitch_multi_texture_output_hash;
        pitch_three_texture_output_hash_ = out.pitch_three_texture_output_hash;
        pitch_single_texture_output_hash_ = out.pitch_single_texture_output_hash;
        pitch_tex5_output_hash_ = out.pitch_tex5_output_hash;
        pitch_textured_color_output_hash_ = out.pitch_textured_color_output_hash;
        projected_color_output_hash_ = out.projected_color_output_hash;
        radial_color_output_hash_ = out.radial_color_output_hash;
        raster_output_hash_ = out.raster_output_hash;
        tex7_fan_output_hash_ = out.tex7_fan_output_hash;
        texf_projected_fan_output_hash_ = out.texf_projected_fan_output_hash;
        textured_color_output_hash_ = out.textured_color_output_hash;
    }
    if (out.last_color_raster_address.set) last_color_raster_address_ = out.last_color_raster_address.value;
    if (out.last_inline_linear_blit_raster_address.set) last_inline_linear_blit_raster_address_ = out.last_inline_linear_blit_raster_address.value;
    if (out.last_inline_raster_address.set) last_inline_raster_address_ = out.last_inline_raster_address.value;
    if (out.last_pitch_multi_texture_raster_address.set) last_pitch_multi_texture_raster_address_ = out.last_pitch_multi_texture_raster_address.value;
    if (out.last_pitch_projected_raster_address.set) last_pitch_projected_raster_address_ = out.last_pitch_projected_raster_address.value;
    if (out.last_pitch_single_texture_raster_address.set) last_pitch_single_texture_raster_address_ = out.last_pitch_single_texture_raster_address.value;
    if (out.last_pitch_tex5_raster_address.set) last_pitch_tex5_raster_address_ = out.last_pitch_tex5_raster_address.value;
    if (out.last_pitch_textured_raster_address.set) last_pitch_textured_raster_address_ = out.last_pitch_textured_raster_address.value;
    if (out.last_pitch_three_rescale_factor3.set) last_pitch_three_rescale_factor3_ = out.last_pitch_three_rescale_factor3.value;
    if (out.last_pitch_three_texture_raster_address.set) last_pitch_three_texture_raster_address_ = out.last_pitch_three_texture_raster_address.value;
    if (out.last_pitch_three_xy_tie_factor3.set) last_pitch_three_xy_tie_factor3_ = out.last_pitch_three_xy_tie_factor3.value;
    if (out.last_pitch_three_zero_factor3.set) last_pitch_three_zero_factor3_ = out.last_pitch_three_zero_factor3.value;
    if (out.last_projected_raster_address.set) last_projected_raster_address_ = out.last_projected_raster_address.value;
    if (out.last_raster_address.set) last_raster_address_ = out.last_raster_address.value;
    if (out.last_tex7_fan_raster_address.set) last_tex7_fan_raster_address_ = out.last_tex7_fan_raster_address.value;
    if (out.last_texf_projected_fan_raster_address.set) last_texf_projected_fan_raster_address_ = out.last_texf_projected_fan_raster_address.value;
    if (out.last_textured_raster_address.set) last_textured_raster_address_ = out.last_textured_raster_address.value;
}

void Nv2a::seam_publish_job(const DrawJob& job) {
    if (!job.queueable) return;
    ++seam_jobs_queueable_;
    // Publish into the M5 simulation: in stage (e) this job would be sitting in
    // the ring, unretired, until the next anchor drains it.
    ++seam_pending_;
    ++seam_vblank_jobs_;
    if (seam_vblank_jobs_ > seam_vblank_jobs_max_)
        seam_vblank_jobs_max_ = seam_vblank_jobs_;
    if (job.color_profile && job.color_bytes) {
        seam_pending_spans_.emplace_back(
            static_cast<uint64_t>(job.color_address),
            static_cast<uint64_t>(job.color_address) + job.color_bytes);
    }
    if (job.writes_depth && job.depth_bytes) {
        seam_pending_spans_.emplace_back(
            static_cast<uint64_t>(job.depth_address),
            static_cast<uint64_t>(job.depth_address) + job.depth_bytes);
    }
}

void Nv2a::seam_anchor(SeamAnchor anchor, bool required) {
    // `hits` counts every time the anchor SITE was reached; `drains` counts the
    // subset at which a threaded consumer would actually have had to stop and
    // wait, i.e. the site's own condition held AND jobs were outstanding. In
    // stage (d) nothing is outstanding for real -- the count is the simulation.
    ++seam_anchor_hits_[anchor];
    if (!required) return;
    seam_anchor_drain(anchor);
}

void Nv2a::seam_anchor_drain(SeamAnchor anchor) {
    // Stage (e): THIS is the fence. All ten anchors of design section 2.2(c)
    // funnel through here, so every one of them becomes a real drain the moment
    // a worker exists. With --nv2a-draw-threads 0 the whole thing is one compare
    // against a plain emulation-thread integer and the simulation below is
    // byte-for-byte what stage (d) counted.
    if (draw_threads_ != 0) draw_queue_drain();
    if (seam_pending_ == 0) return;
    ++seam_anchor_drains_[anchor];
    seam_anchor_pending_sum_[anchor] += seam_pending_;
    if (seam_pending_ > seam_anchor_pending_max_[anchor])
        seam_anchor_pending_max_[anchor] = seam_pending_;
    seam_pending_ = 0;
    seam_pending_spans_.clear();
}

void Nv2a::seam_texture_source_anchor(uint32_t address, uint64_t bytes) {
    // F6 / anchor 6: a texture snapshot is only a drain when its source span
    // intersects a pending job's destination span (render-to-texture inside one
    // frame). One linear scan of a list bounded by the drain frequency.
    const uint64_t begin = address;
    const uint64_t end = begin + bytes;
    bool overlap = false;
    for (const auto& span : seam_pending_spans_) {
        if (begin < span.second && span.first < end) {
            overlap = true;
            break;
        }
    }
    seam_anchor(kSeamAnchorTextureOverlap, overlap);
}

void Nv2a::seam_checkpoint_anchor() {
    seam_anchor(kSeamAnchorCheckpoint, true);
}

// ================== stage (e): the threaded draw consumer ====================
// beads-xbox.1.15.5, DESIGN-NV2A-SEAM.md section 5(e), PERF-METHODOLOGY.md
// section 4. One bounded ring, one worker, three cursors, ten fences.
//
// ORDERING. Draws to the same destination surface are order-dependent: the
// shared colour body does read-modify-write blending and twelve of the fifteen
// witnessed profiles test against stored depth. There is therefore exactly ONE
// consumer and it executes jobs strictly in submission order. That is also why
// kDrawThreadsMax is 1: a second worker needs destination-span partitioning,
// which is design section 1.5's deferred item.
//
// WHY THE DRAIN WAITS RATHER THAN HELPING. PERF-METHODOLOGY.md section 4 allows
// the emulation thread to execute pending jobs itself while draining. Under
// strict in-order execution with one consumer that is not available: the
// emulation thread may only run a job once every earlier job has completed, and
// at that point the worker is either idle (queue already empty -- nothing to
// help with) or still running the job in front (helping would reorder). So the
// drain waits. The degrade-to-single-threaded guarantee is provided instead by
// --nv2a-draw-threads 0 and by the backpressure path, which drains and then runs
// the job inline. draw_drain_wait_ticks_ measures exactly what the waiting cost.
namespace {
inline void draw_cpu_pause() {
#if defined(__i386__) || defined(__x86_64__) || defined(_M_IX86) || defined(_M_X64)
    __builtin_ia32_pause();
#endif
}
// Spin before sleeping. A burst is ~96 jobs deep (M5), so the interesting waits
// are short; the condvar is the backstop, not the common case.
constexpr int kDrawSpin = 1024;
}  // namespace

const char* Nv2a::draw_depth_bucket_name(size_t i) {
    static const char* kNames[kDrawDepthBuckets] = {
        // Prefixed because "16_63" and friends parse as INTEGERS in Python
        // (PEP 515 digit separators), which silently turned the histogram into
        // nonsense keys in measure_xbox_replay.py.
        "d0", "d1", "d2_3", "d4_7", "d8_15", "d16_63", "d64_255", "d256_plus",
    };
    return i < kDrawDepthBuckets ? kNames[i] : "unknown";
}

uint64_t Nv2a::draw_elapsed_ticks() const {
    if (!draw_epoch_ticks_) return 0;
    const uint64_t now = perf_excl::tick_now();
    return now > draw_epoch_ticks_ ? now - draw_epoch_ticks_ : 0;
}

bool Nv2a::set_draw_threads(unsigned n) {
    if (n > kDrawThreadsMax) return false;
    if (n == draw_threads_) return true;
    if (draw_threads_ != 0) {
        draw_queue_drain_all();
        draw_stop_.store(true);
        {
            std::lock_guard<std::mutex> lock(draw_mutex_);
            draw_work_cv_.notify_all();
        }
        if (draw_worker_.joinable()) draw_worker_.join();
        draw_stop_.store(false);
        span_pool_stop();
    }
    draw_threads_ = n;
    if (n == 1) {
        // Stage (e): the asynchronous single consumer and its ring.
        draw_slots_.resize(kDrawQueueSlots);
        draw_published_ = 0;
        draw_retired_ = 0;
        draw_published_pub_.store(0);
        draw_executed_.store(0);
        draw_epoch_ticks_ = perf_excl::tick_now();
        draw_worker_ = std::thread([this] { draw_worker_main(); });
    } else if (n >= 2) {
        // Stage (e2): N destination-span parts, of which the emulation thread is
        // part 0. No ring worker is started and draw_slots_ stays empty -- a
        // span-parallel job never enters the queue, so draw_published_ ==
        // draw_retired_ == 0 forever and every anchor drain is the empty case.
        draw_epoch_ticks_ = perf_excl::tick_now();
        span_pool_start(n);
    }
    return true;
}

// Phase 5 item 3 (beads-xbox.1.15.10). Cache lifetime. destroy_ frees the
// malloc the vendored parser owns; invalidate_ is what the two method handlers
// call and is the only thing on a hot path, so it is a free plus two stores in
// the rare case and a single predictable branch otherwise.
void Nv2a::destroy_vsh_program_cache() {
    if (!vsh_cache_steps_) return;
    Nv2aVshProgram cached{static_cast<Nv2aVshStep*>(vsh_cache_steps_)};
    nv2a_vsh_program_destroy(&cached);
    vsh_cache_steps_ = nullptr;
}

void Nv2a::invalidate_vsh_program_cache() {
    if (!vsh_cache_valid_ && !vsh_cache_steps_) {
        // The compiled form is derived from the parse; when the parse cache is
        // OFF it is rebuilt per batch, and this is the one place that says the
        // previous batch's records no longer describe the program.
        vsh_cache_ops_valid_ = false;
        vsh_dot_prefix_plan_.clear();
#if XLLE_VSH_DRAW_OVERLAP_CENSUS
        vsh_cache_overlap_identity_ = {};
#endif
        return;
    }
    vsh_cache_valid_ = false;
    vsh_cache_writes_context_ = false;
    vsh_cache_ops_valid_ = false;
    vsh_dot_prefix_plan_.clear();
    vsh_cache_census_row_ = nullptr;
#if XLLE_VSH_DRAW_OVERLAP_CENSUS
    vsh_cache_overlap_identity_ = {};
#endif
    destroy_vsh_program_cache();
}

// beads-xbox.1.15.11. One row per distinct program, keyed on a fold of the
// program words and the start slot -- exactly the two inputs the parse is a
// function of. Overflow is COUNTED, never evicted, so a program the table
// already knows about can never be under-reported; the same rule the phase-5
// plane table and the phase-6 shader table follow.
Nv2a::VshProgramCensusRow* Nv2a::vsh_program_census_row(uint64_t fingerprint) {
    for (size_t i = 0; i < vsh_program_census_.count; ++i)
        if (vsh_program_census_.rows[i].fingerprint == fingerprint)
            return &vsh_program_census_.rows[i];
    if (vsh_program_census_.count >= VshProgramCensus::kSlots) {
        ++vsh_program_census_.overflow;
        return nullptr;
    }
    VshProgramCensusRow& row =
        vsh_program_census_.rows[vsh_program_census_.count++];
    row.fingerprint = fingerprint;
    return &row;
}

// beads-xbox.1.15.11. The differential verifier. `compiled_output_regs` is the
// output register file the compiled form just produced for this vertex, from
// the same `input_regs` and the same transform constants; this reruns the
// vendored interpreter on a scratch state and compares the SEVEN registers the
// device reads back afterwards, which is the whole observable contract.
void Nv2a::vsh_verify_vertex(const void* program, const float* input_regs,
                             const float* compiled_output_regs) {
    const Nv2aVshProgram reference{
        const_cast<Nv2aVshStep*>(static_cast<const Nv2aVshStep*>(program))};
    std::vector<float> context(192 * 4);
    std::memcpy(context.data(), transform_constants_.data(),
                context.size() * sizeof(float));
    std::array<float, 13 * 4> output{};
    std::array<float, 12 * 4> temp{};
    std::array<float, 4> address{};
    Nv2aVshExecutionState state{const_cast<float*>(input_regs),
                                output.data(),
                                temp.data(),
                                context.data(),
                                address.data(),
                                nullptr};
    nv2a_vsh_emu_execute(&state, &reference);
    static constexpr unsigned kConsumed[] = {0, 3, 4, 9, 10, 11, 12};
    bool mismatch = false;
    for (unsigned index : kConsumed) {
        for (unsigned lane = 0; lane < 4; ++lane) {
            const size_t element = index * 4 + lane;
            uint32_t reference_bits = 0;
            uint32_t compiled_bits = 0;
            std::memcpy(&reference_bits, &output[element], sizeof(uint32_t));
            std::memcpy(&compiled_bits, &compiled_output_regs[element],
                        sizeof(uint32_t));
            if (reference_bits == compiled_bits) continue;
            mismatch = true;
            if (vsh_verify_reported_ < 32) {
                ++vsh_verify_reported_;
                std::printf(
                    "[host] nv2a-vsh-verify MISMATCH vertex=%llu o%u.%u "
                    "interpreter=%08x compiled=%08x\n",
                    static_cast<unsigned long long>(vsh_verify_checked_), index,
                    lane, reference_bits, compiled_bits);
            }
        }
    }
    ++vsh_verify_checked_;
    if (mismatch) ++vsh_verify_mismatched_;
}

bool Nv2a::compile_vsh_program(const void* steps, uint32_t slots,
                               VshProgramCensusRow* row) {
    vsh_cache_ops_valid_ = false;
    vsh_cache_ops_.clear();
    vsh_dot_prefix_plan_.clear();
    const Nv2aVshStep* step = static_cast<const Nv2aVshStep*>(steps);
    if (!step || !slots) return false;
    uint32_t step_count = 0;
    uint32_t paired_steps = 0;
    bool found_final = false;
    for (uint32_t index = 0; index < slots; ++index, ++step) {
        ++step_count;
        const bool has_mac = step->mac.opcode != NV2AOP_NOP;
        const bool has_ilu = step->ilu.opcode != NV2AOP_NOP;
        if (has_mac) {
            VshCompiledOp op{};
            if (!compile_vsh_operation(step->mac, op)) return false;
            // The ILU half of the SAME step must have its operand fetched
            // before this one executes; `prepare_inputs` does both first.
            op.paired = has_ilu ? 1u : 0u;
            vsh_cache_ops_.push_back(op);
        }
        if (has_ilu) {
            VshCompiledOp op{};
            if (!compile_vsh_operation(step->ilu, op)) return false;
            vsh_cache_ops_.push_back(op);
        }
        if (has_mac && has_ilu) ++paired_steps;
        if (step->is_final) {
            found_final = true;
            break;
        }
    }
    // A program with no final step runs off the end of the parsed array in the
    // vendored walker. Decline rather than guess where it ends.
    if (!found_final) return false;
    // ---- the read-before-write analysis (beads-xbox.1.15.11) --------------
    // Straight-line code with no branches makes "can this register be read
    // before it is written" decidable exactly, per LANE. The walk is over the
    // COMPILED ops in issue order, and it respects the MAC/ILU pairing the same
    // way the executor does: both halves of a step read before either writes,
    // so an ILU reading what its own step's MAC writes counts as reading the
    // OLD value and therefore as a read-before-write.
    std::array<uint8_t, 12> temp_written{};
    std::array<uint8_t, 13> output_written{};
    VshZeroPlan plan{};
    plan.temp_mask = 0;
    plan.output_mask = 0;
    plan.address = false;
    const auto note_read = [&](const VshCompiledOp& op, unsigned k) {
        if (op.in_relative_mask & (1u << k)) {
            plan.address = true;  // the fetch reads a0
            return;
        }
        const unsigned lanes = vsh_swizzle_lane_mask(op.in_swizzle[k]);
        const unsigned index = op.in_offset[k] / 4u;
        if (op.in_base[k] == kVshBaseTemp && index < temp_written.size()) {
            if (lanes & ~static_cast<unsigned>(temp_written[index])) {
                plan.temp_mask |= static_cast<uint16_t>(1u << index);
                temp_written[index] = 0xF;
            }
        } else if (op.in_base[k] == kVshBaseOutput &&
                   index < output_written.size()) {
            if (lanes & ~static_cast<unsigned>(output_written[index])) {
                plan.output_mask |= static_cast<uint16_t>(1u << index);
                output_written[index] = 0xF;
            }
        }
    };
    const auto note_write = [&](const VshCompiledOp& op) {
        for (unsigned o = 0; o < 2; ++o) {
            if (op.out_base[o] == kVshBaseNone) continue;
            const unsigned lanes = vsh_writemask_lane_mask(op.out_mask[o]);
            const unsigned index = op.out_offset[o] / 4u;
            if (op.out_base[o] == kVshBaseTemp && index < temp_written.size())
                temp_written[index] |= static_cast<uint8_t>(lanes);
            else if (op.out_base[o] == kVshBaseOutput &&
                     index < output_written.size())
                output_written[index] |= static_cast<uint8_t>(lanes);
        }
    };
    for (size_t i = 0; i < vsh_cache_ops_.size();) {
        const VshCompiledOp& op = vsh_cache_ops_[i];
        const bool paired = op.paired != 0;
        for (unsigned k = 0; k < op.input_count; ++k) note_read(op, k);
        if (paired) {
            const VshCompiledOp& ilu = vsh_cache_ops_[i + 1];
            for (unsigned k = 0; k < ilu.input_count; ++k) note_read(ilu, k);
            note_write(op);
            note_write(ilu);
            i += 2;
        } else {
            note_write(op);
            i += 1;
        }
    }
    // The DEVICE reads these seven output registers back after the program:
    // position (0), diffuse (3), specular (4) and the four texture coordinates
    // (9-12). That is a read like any other, of all four lanes.
    static constexpr unsigned kConsumedOutputs[] = {0, 3, 4, 9, 10, 11, 12};
    for (unsigned index : kConsumedOutputs)
        if (0xFu & ~static_cast<unsigned>(output_written[index]))
            plan.output_mask |= static_cast<uint16_t>(1u << index);
    vsh_cache_zero_plan_ = plan;
    if (row) {
        row->zero_outputs =
            static_cast<uint32_t>(__builtin_popcount(plan.output_mask));
        row->zero_temps =
            static_cast<uint32_t>(__builtin_popcount(plan.temp_mask));
        row->zero_address = plan.address ? 1u : 0u;
    }
    if (row) {
        row->steps = step_count;
        row->ops = static_cast<uint32_t>(vsh_cache_ops_.size());
        row->paired = paired_steps;
    }
    vsh_cache_ops_valid_ = true;
#if XLLE_VSH_CAPTURE || XLLE_VSH_GPU_RESIDENT
    vsh_capture_plan_supported_ = std::all_of(vsh_cache_ops_.begin(), vsh_cache_ops_.end(),
        [](const VshCompiledOp& op) {
            if (op.in_relative_mask) return false;
            for (unsigned i = 0; i < 2; ++i)
                if (op.out_base[i] != kVshBaseNone && op.out_base[i] != kVshBaseTemp &&
                    op.out_base[i] != kVshBaseOutput) return false;
            return true;
        });
#endif
    return true;
}

#if XLLE_VSH_CAPTURE
void Nv2a::capture_vsh_vertex(bool eligible, uint64_t fingerprint,
                             const float* inputs, const float* outputs, const float* post_position) {
    if (!vsh_capture_callback_) return;
    auto& stats = vsh_capture_stats_;
    ++stats.executions;
    if (!eligible || !vsh_capture_plan_supported_ || vsh_cache_ops_.empty() ||
        vsh_cache_ops_.size() > vsh_capture::kMaxOps) {
        ++stats.excluded;
        return;
    }
    ++stats.eligible;
    if (!vsh_capture_interval_ || (stats.eligible - 1) % vsh_capture_interval_ ||
        stats.samples >= vsh_capture_budget_) return;
    vsh_capture::Vertex vertex;
    vertex.ordinal = stats.executions;
    vertex.fingerprint = fingerprint;
    std::memcpy(vertex.inputs.data(), inputs, sizeof(vertex.inputs));
    std::memcpy(vertex.constants.data(), transform_constants_.data(), sizeof(vertex.constants));
    std::memcpy(vertex.post_position.data(), post_position, sizeof(vertex.post_position));
    for (size_t i = 0; i < vsh_capture::kConsumedOutputs.size(); ++i)
        std::memcpy(vertex.expected.data() + i * 4,
                    outputs + vsh_capture::kConsumedOutputs[i] * 4, 4 * sizeof(float));
    vertex.ops = capture_vsh_ops();
    vsh_capture_callback_(vertex);
    ++stats.samples;
}
#endif

#if XLLE_VSH_GPU_RESIDENT
bool Nv2a::materialize_deferred_gpu_vsh(DrawJob& job) {
    if (!job.gpu_vsh || job.gpu_vsh->mode != 3 || !job.vertices.empty())
        return true;
    auto& reference = *job.gpu_vsh;
    const auto& batch = reference.batch;
    if (batch.shape.execution_count != batch.executions.size() ||
        batch.shape.reference_count != batch.references.size() ||
        job.gpu_vsh_compiled_ops.empty()) {
        job.out.failed = true;
        job.out.fault_message = "NV2A deferred GPU VSH fallback payload is incomplete";
        return false;
    }
    ++vsh_gpu_deferred_stats_.fallback_batches;
    vsh_gpu_deferred_stats_.fallback_executions += batch.executions.size();

    std::array<float, 192 * 4> constants;
    std::memcpy(constants.data(), batch.constants.words.data(), sizeof(constants));
    std::vector<TransformedVertex> executed;
    executed.reserve(batch.executions.size());
    for (const auto& saved : batch.executions) {
        std::array<float, 16 * 4> input_regs;
        std::array<float, 13 * 4> output_regs;
        std::array<float, 12 * 4> temp_regs;
        std::array<float, 4> address_reg;
        std::memcpy(input_regs.data(), saved.words.data(), sizeof(input_regs));
        output_regs.fill(0.0f);
        temp_regs.fill(0.0f);
        address_reg.fill(0.0f);
        float* bases[kVshBaseCount] = {
            temp_regs.data(),
            output_regs.data(),
            input_regs.data(),
            constants.data(),
            address_reg.data()};
        vsh_execute_compiled(job.gpu_vsh_compiled_ops.data(),
                             static_cast<uint32_t>(job.gpu_vsh_compiled_ops.size()),
                             bases);

        TransformedVertex vertex;
        std::memcpy(vertex.position.data(), output_regs.data(), 4 * sizeof(float));
        vertex.position[0] = round_screen_coordinate(vertex.position[0]);
        vertex.position[1] = round_screen_coordinate(vertex.position[1]);
        vertex.position[3] = clamp_away_zero_inf(vertex.position[3]);
        std::memcpy(vertex.diffuse.data(), output_regs.data() + 3 * 4,
                    4 * sizeof(float));
        std::memcpy(vertex.specular.data(), output_regs.data() + 4 * 4,
                    4 * sizeof(float));
        for (uint32_t texture = 0; texture < 4; ++texture) {
            std::memcpy(vertex.texture[texture].data(),
                        output_regs.data() + (9 + texture) * 4,
                        4 * sizeof(float));
        }
        executed.push_back(vertex);
        ++vertex_census_.vsh_compiled_vertices;
    }

    job.vertices.clear();
    job.vertices.reserve(batch.references.size());
    for (uint32_t id : batch.references) {
        if (id >= executed.size()) {
            job.out.failed = true;
            job.out.fault_message = "NV2A deferred GPU VSH fallback reference is out of range";
            job.vertices.clear();
            return false;
        }
        job.vertices.push_back(executed[id]);
    }
    reference.prepared_token = 0;
    reference.prepared_vertices = 0;
    return true;
}
#endif

#if XLLE_VSH_CAPTURE || XLLE_VSH_GPU_RESIDENT
std::vector<vsh_capture::Op> Nv2a::capture_vsh_ops() const {
    std::vector<vsh_capture::Op> result;
    for (const auto& source : vsh_cache_ops_) {
        vsh_capture::Op op;
        auto& w = op.words;
        for (uint32_t id = 1; id < std::size(kVshDispatchTable); ++id)
            if (source.fn == kVshDispatchTable[id]) w[0] = id;
        w[1] = source.paired; w[2] = source.input_count; w[3] = source.in_negate_mask;
        w[7] = source.in_relative_mask;
        for (size_t i = 0; i < 3; ++i) {
            w[4 + i] = source.in_base[i]; w[8 + i] = source.in_offset[i];
            w[12 + i] = source.in_swizzle[i];
        }
        w[16] = source.out_base[0]; w[17] = source.out_base[1];
        w[18] = source.out_offset[0]; w[19] = source.out_offset[1];
        w[20] = source.out_mask[0]; w[21] = source.out_mask[1];
        result.push_back(op);
    }
    return result;
}
#endif

Nv2a::~Nv2a() {
    // Worker lifetime is tied to the device. Stop it and JOIN before any member
    // it reads -- the slot ring, the job vectors, the direct pointers into guest
    // RAM -- is destroyed. The worker finishes whatever is already published
    // first (its loop only tests the stop flag when the ring is empty), so no
    // half-executed draw is left behind. Folding is deliberately NOT done here:
    // the fold applies renderer invalidations, and the renderer may already be
    // gone by the time the device is destroyed. main.cpp drains at shutdown
    // through the checkpoint anchor while everything is still alive.
    draw_stop_.store(true);
    {
        std::lock_guard<std::mutex> lock(draw_mutex_);
        draw_work_cv_.notify_all();
    }
    if (draw_worker_.joinable()) draw_worker_.join();
    // Phase 5 item 3: the parsed-program cache owns one malloc from the
    // vendored disassembler. Released here so the device does not leak it.
    destroy_vsh_program_cache();
    // Stage (e2): the destination-span helpers hold raw pointers into the job
    // and into guest RAM for the duration of a barrier; the barrier has always
    // completed by the time control returns to the emulation thread, so there is
    // never work in flight here -- but they must still be joined before the
    // members they read are destroyed.
    span_pool_stop();
}

void Nv2a::draw_worker_main() {
    uint64_t next = 0;
    for (;;) {
        uint64_t published = draw_published_pub_.load(std::memory_order_acquire);
        if (next == published) {
            if (draw_stop_.load()) break;
            for (int spin = 0; spin < kDrawSpin; ++spin) {
                draw_cpu_pause();
                published = draw_published_pub_.load(std::memory_order_acquire);
                if (next != published) break;
            }
            if (next == published) {
                std::unique_lock<std::mutex> lock(draw_mutex_);
                // The consumer half of the Dekker pair documented in
                // draw_queue_publish: seq_cst store of the flag, then seq_cst
                // load of the cursor.
                draw_worker_sleeping_.store(true, std::memory_order_seq_cst);
                draw_work_cv_.wait(lock, [this, next] {
                    return draw_published_pub_.load(std::memory_order_seq_cst) != next ||
                           draw_stop_.load();
                });
                draw_worker_sleeping_.store(false, std::memory_order_seq_cst);
            }
            continue;
        }
        DrawJob& job = draw_slots_[static_cast<size_t>(next % kDrawQueueSlots)];
        const uint64_t t0 = perf_excl::tick_now();
        // The SAME function the inline path calls. There is no worker-only copy
        // of the pixel math, and nothing in it reaches Unicorn, the renderer or
        // an Nv2a member.
        job.out.ok = execute_witnessed_fragments(job, job.out, 0, 1);
        job.out.busy_ticks = perf_excl::tick_now() - t0;
        draw_worker_busy_ticks_.fetch_add(job.out.busy_ticks,
                                          std::memory_order_relaxed);
        ++next;
        // Same Dekker pair as the publish side, roles swapped: this cursor and
        // draw_emu_waiting_ are read by the draining emulation thread.
        draw_executed_.store(next, std::memory_order_seq_cst);
        if (draw_emu_waiting_.load(std::memory_order_seq_cst)) {
            std::lock_guard<std::mutex> lock(draw_mutex_);
            draw_done_cv_.notify_one();
        }
    }
}

// Bytes a job holds hostage while it sits in the ring: its transformed vertices
// and its texture snapshots. The swizzle tables and the register mirror are
// bounded and small; these two are not.
uint64_t Nv2a::draw_job_bytes(const DrawJob& job) {
    uint64_t bytes = job.vertices.capacity() * sizeof(TransformedVertex);
#if XLLE_VSH_GPU_RESIDENT
    if (job.gpu_vsh) {
        const auto& batch = job.gpu_vsh->batch;
        bytes += sizeof(*job.gpu_vsh) + batch.ops.capacity() * sizeof(vsh_gpu_batch::Op) +
            batch.executions.capacity() * sizeof(vsh_gpu_batch::ExecutionInput) +
            batch.references.capacity() * sizeof(uint32_t) +
            job.gpu_vsh->expected.capacity() * sizeof(vsh_gpu_batch::ProgramOutput128B);
        bytes += job.gpu_vsh_compiled_ops.capacity() * sizeof(VshCompiledOp);
    }
#endif
#if XLLE_VSH_CAPTURE
    if (job.vsh_batch_capture) {
        const auto& batch = *job.vsh_batch_capture;
        bytes += sizeof(batch) + batch.ops.capacity() * sizeof(vsh_capture::Op) +
            batch.executions.capacity() * sizeof(vsh_batch_capture::Execution) +
            (batch.references.capacity() + batch.source_indices.capacity() +
             batch.expected_indices.capacity()) * sizeof(uint32_t);
    }
#endif
    bytes += job.texture_data.capacity();
    bytes += job.projected_texture_data.capacity();
    bytes += job.linear_blit_texture_data.capacity();
    bytes += job.multi_texture_data.capacity();
    bytes += job.single_texture_data.capacity();
    bytes += job.pitch_tex5_texture_data.capacity();
    bytes += job.inline_texture_data.capacity();
    bytes += job.palette_data.capacity();
    return bytes;
}

void Nv2a::draw_queue_publish(DrawJob& job) {
    const uint64_t bytes = draw_job_bytes(job);
    const bool slots_full = draw_published_ - draw_retired_ >= kDrawQueueSlots;
    const bool bytes_full = draw_queue_bytes_ + bytes > kDrawQueueMaxBytes;
    if (slots_full || bytes_full) {
        // Backpressure. A full ring is not an error and not a drop: drain it
        // (which empties every slot AND guarantees nothing is outstanding), then
        // run this job inline, here, which is in order precisely because the
        // drain just happened. M5 measured a maximum depth of 423 against 1024
        // slots, so this is expected to stay at zero; it is counted so that a
        // non-zero value is visible rather than silently costing time.
        draw_queue_drain();
        ++draw_backpressure_inline_;
        if (bytes_full && !slots_full) ++draw_backpressure_bytes_;
        const uint64_t t0 = perf_excl::tick_now();
        job.out.ok = execute_witnessed_fragments(job, job.out, 0, 1);
        draw_worker_busy_ticks_.fetch_add(perf_excl::tick_now() - t0,
                                          std::memory_order_relaxed);
        transformed_batch_ = std::move(job.vertices);
        fold_draw_job(job);
        return;
    }
    const size_t slot = static_cast<size_t>(draw_published_ % kDrawQueueSlots);
    draw_slots_[slot] = std::move(job);
    draw_queue_bytes_ += bytes;
    if (draw_queue_bytes_ > draw_queue_bytes_max_)
        draw_queue_bytes_max_ = draw_queue_bytes_;
    ++draw_published_;
    // SEQ_CST, NOT RELEASE, and the pairing below is why. This store and the
    // draw_worker_sleeping_ load at the end of this function form a Dekker
    // store-load pair with the worker's own sleeping-flag store and its
    // published-cursor load. A release store followed by a load is NOT ordered
    // against that load on x86 (the store sits in the store buffer), so both
    // sides could read the other's stale value: the producer decides the worker
    // is awake and skips the notify while the worker decides there is no work
    // and sleeps. The predicate form of condition_variable::wait does not save
    // you -- a predicate is only re-evaluated on a WAKE, and the wake is the
    // thing that was lost. Measured 2026-09-05: the async ring hung at draw 174
    // with published_pub=174, executed=173, worker_sleeping=1, and a bare
    // notify_all released it. Making all four operations seq_cst restores the
    // total order in which at least one side must observe the other.
    draw_published_pub_.store(draw_published_, std::memory_order_seq_cst);
    ++draw_jobs_queued_;

    const uint64_t depth =
        draw_published_ - draw_executed_.load(std::memory_order_relaxed);
    draw_depth_sum_ += depth;
    ++draw_depth_samples_;
    if (depth > draw_depth_max_) draw_depth_max_ = depth;
    size_t bucket = 0;
    if (depth >= 256) bucket = 7;
    else if (depth >= 64) bucket = 6;
    else if (depth >= 16) bucket = 5;
    else if (depth >= 8) bucket = 4;
    else if (depth >= 4) bucket = 3;
    else if (depth >= 2) bucket = 2;
    else if (depth >= 1) bucket = 1;
    ++draw_depth_hist_[bucket];

    // The latch MOVED transformed_batch_ into the job and the job is now in the
    // ring, so the producer needs a buffer for the next batch. Recycling one a
    // finished job gave back keeps the allocation count identical to the inline
    // path, where retire_draw_job hands the same vector straight back.
    if (!draw_free_vertex_buffers_.empty()) {
        transformed_batch_ = std::move(draw_free_vertex_buffers_.back());
        draw_free_vertex_buffers_.pop_back();
        transformed_batch_.clear();
    }
    if (draw_worker_sleeping_.load(std::memory_order_seq_cst)) {
        std::lock_guard<std::mutex> lock(draw_mutex_);
        draw_work_cv_.notify_one();
    }
}

void Nv2a::draw_queue_drain() {
    if (draw_threads_ == 0) return;
    const uint64_t target = draw_published_;
    if (draw_retired_ == target) {
        ++draw_drains_empty_;
        return;
    }
    ++draw_drains_nonempty_;
    if (draw_executed_.load(std::memory_order_acquire) != target) {
        const uint64_t t0 = perf_excl::tick_now();
        for (int spin = 0; spin < kDrawSpin; ++spin) {
            draw_cpu_pause();
            if (draw_executed_.load(std::memory_order_acquire) == target) break;
        }
        if (draw_executed_.load(std::memory_order_acquire) != target) {
            std::unique_lock<std::mutex> lock(draw_mutex_);
            draw_emu_waiting_.store(true, std::memory_order_seq_cst);
            // Bounded wait with an always-on stall report. An unbounded wait
            // here turns any ordering defect into a silent hang with no
            // evidence at all -- which is exactly what happened on 2026-09-05
            // when the F3 query fence was first wired in. The predicate is
            // re-checked on every wakeup, so this is behaviourally identical to
            // an unbounded wait; the only addition is that a drain that has not
            // completed in five seconds prints the three cursors and the
            // worker's own state, once per stall, before continuing to wait.
            while (!draw_done_cv_.wait_for(
                lock, std::chrono::seconds(5), [this, target] {
                    return draw_executed_.load(std::memory_order_seq_cst) ==
                           target;
                })) {
                std::fprintf(
                    stderr,
                    "[nv2a] draw drain stalled >5s: threads=%u target=%llu "
                    "published=%llu published_pub=%llu executed=%llu "
                    "retired=%llu worker_sleeping=%d emu_waiting=%d stop=%d\n",
                    draw_threads_, (unsigned long long)target,
                    (unsigned long long)draw_published_,
                    (unsigned long long)draw_published_pub_.load(
                        std::memory_order_relaxed),
                    (unsigned long long)draw_executed_.load(
                        std::memory_order_relaxed),
                    (unsigned long long)draw_retired_,
                    draw_worker_sleeping_.load() ? 1 : 0,
                    draw_emu_waiting_.load() ? 1 : 0,
                    draw_stop_.load() ? 1 : 0);
                std::fflush(stderr);
            }
            draw_emu_waiting_.store(false, std::memory_order_seq_cst);
        }
        draw_drain_wait_ticks_ += perf_excl::tick_now() - t0;
    }
    // Fold in latch order. Nothing else may touch a slot between the worker's
    // release store and this loop, which is why draw_retired_ exists: a slot is
    // reusable only once the emulation thread has taken its telemetry and its
    // deferred side effects.
    while (draw_retired_ != target) {
        DrawJob& done = draw_slots_[static_cast<size_t>(draw_retired_ % kDrawQueueSlots)];
        fold_draw_job(done);
        const uint64_t bytes = draw_job_bytes(done);
        done.vertices.clear();
        if (draw_free_vertex_buffers_.size() < kDrawFreeVertexBuffers)
            draw_free_vertex_buffers_.push_back(std::move(done.vertices));
        // Release everything else the job owns NOW rather than at the slot's next
        // reuse. A slot that keeps a multi-megabyte texture snapshot alive until
        // it is overwritten makes the ring's resident set kDrawQueueSlots times
        // the largest job -- measured: it blew the harness's 768 MiB host-memory
        // cap on the canonical run before this line existed.
        done = DrawJob();
        draw_queue_bytes_ -= bytes < draw_queue_bytes_ ? bytes : draw_queue_bytes_;
        ++draw_retired_;
    }
}

void Nv2a::draw_order_fence() {
    if (draw_threads_ == 0) return;
    if (draw_published_ == draw_retired_) return;
    ++draw_order_fences_;
    draw_queue_drain();
}

// F3 CLOSURE. See the comment on the declaration in nv2a.hpp. Called at the top
// of every renderer RESIDENCY QUERY, before the query is made, so the resident
// set the query evaluates already reflects every invalidation a pending job
// owes. Deliberately a separate counter from draw_order_fences_: that one
// measures inline pixel paths not overtaking the queue, this one measures the
// staleness window, and folding them would make either unreadable.
void Nv2a::draw_renderer_query_fence() {
    if (draw_threads_ == 0) return;
    if (draw_published_ == draw_retired_) return;
    ++draw_renderer_query_fences_;
    draw_queue_drain();
}

void Nv2a::draw_queue_drain_all() {
    draw_queue_drain();
}

// ============ stage (e2): within-job destination-span parallel raster ========
// beads-xbox.1.15.9. Stage (e) moved a whole draw onto a second thread and was
// rejected: it is latency hiding, and the emulation thread has nothing to
// overlap with (worker busy 0.489 of elapsed, emulation thread blocked in
// drains 0.449, computed ceiling 4.0 percent, measured +0.39 percent). This is
// WORK DIVISION instead. One job at a time, globally; all N workers inside it;
// each owning a disjoint set of destination scanlines; a barrier before the next
// job. Draw order is therefore preserved by construction and no two workers ever
// write the same pixel, which is what makes it exact rather than merely
// probably-exact.
//
// The ring, the ten anchors and the order fence are untouched and simply never
// have anything outstanding at N >= 2, so every coherence question stage (e)
// had to answer is satisfied trivially here: a job is finished before the guest
// instruction that issued it retires, exactly as at --nv2a-draw-threads 0.
namespace {
// Helpers spin before sleeping. A burst is ~96 draws deep (M5) and a draw is
// tens of microseconds, so within a burst a helper must never reach the condvar;
// the sleep is for the gap between bursts, where the guest is in its idle loop.
constexpr int kSpanSpin = 20000;
}  // namespace

void Nv2a::span_pool_start(unsigned parts) {
    span_out_.assign(parts, DrawJobTelemetry());
    span_job_ = nullptr;
    span_parts_ = 1;
    span_gen_.store(0, std::memory_order_release);
    span_done_.store(0, std::memory_order_relaxed);
    span_stop_.store(false);
    span_workers_.reserve(parts - 1);
    for (unsigned i = 1; i < parts; ++i)
        span_workers_.emplace_back([this, i] { span_worker_main(i); });
}

void Nv2a::span_pool_stop() {
    if (span_workers_.empty()) return;
    span_stop_.store(true);
    {
        std::lock_guard<std::mutex> lock(span_mutex_);
        span_work_cv_.notify_all();
    }
    for (auto& worker : span_workers_)
        if (worker.joinable()) worker.join();
    span_workers_.clear();
    span_out_.clear();
}

void Nv2a::span_worker_main(unsigned part) {
    uint64_t seen = span_gen_.load(std::memory_order_acquire);
    for (;;) {
        uint64_t gen = span_gen_.load(std::memory_order_acquire);
        if (gen == seen) {
            if (span_stop_.load()) break;
            for (int spin = 0; spin < kSpanSpin; ++spin) {
                draw_cpu_pause();
                gen = span_gen_.load(std::memory_order_acquire);
                if (gen != seen) break;
            }
            if (gen == seen) {
                std::unique_lock<std::mutex> lock(span_mutex_);
                span_sleepers_.fetch_add(1, std::memory_order_relaxed);
                span_work_cv_.wait(lock, [this, seen] {
                    return span_gen_.load(std::memory_order_acquire) != seen ||
                           span_stop_.load();
                });
                span_sleepers_.fetch_sub(1, std::memory_order_relaxed);
            }
            continue;
        }
        seen = gen;
        // The generation store is a release and this load is an acquire, so
        // span_job_ and span_parts_ -- plain members written by the emulation
        // thread before the store -- are visible here. The job itself is not
        // mutated again until the barrier completes.
        DrawJob& job = *span_job_;
        const unsigned parts = span_parts_;
        if (part < parts) {
            DrawJobTelemetry& out = span_out_[part];
            const uint64_t t0 = perf_excl::tick_now();
            // The SAME function part 0 and every inline job call. There is no
            // worker-only copy of the pixel math; the only difference is which
            // destination rows this call owns.
            out.ok = execute_witnessed_fragments(job, out, part, parts);
            span_helper_busy_ticks_.fetch_add(perf_excl::tick_now() - t0,
                                              std::memory_order_relaxed);
        }
        // Release: everything this part wrote -- destination pixels and its own
        // telemetry cell -- happens-before the emulation thread's acquire on the
        // completion count, so the fold and the guest both see finished work.
        // seq_cst for the same Dekker reason as the async ring's publish: a
        // relaxed load of span_emu_waiting_ after a release RMW lets this helper
        // conclude the emulation thread is not waiting while the emulation
        // thread concludes this part has not finished, and the barrier sleeps
        // forever. This is the DEFAULT path as of 2026-09-05, so the unsound
        // version was a latent hang in the shipping configuration.
        span_done_.fetch_add(1, std::memory_order_seq_cst);
        if (span_emu_waiting_.load(std::memory_order_seq_cst)) {
            std::lock_guard<std::mutex> lock(span_mutex_);
            span_done_cv_.notify_one();
        }
    }
}

bool Nv2a::execute_draw_spans(DrawJob& job) {
    const unsigned parts = draw_threads_;
    const unsigned helpers = parts - 1;
    job.span_parts = parts;
    span_job_ = &job;
    span_parts_ = parts;
    for (unsigned p = 1; p < parts; ++p) span_out_[p].reset();
    span_done_.store(0, std::memory_order_relaxed);
    {
        // Published UNDER the mutex on purpose. ndsrecomp commit 0206ba3 records
        // the failure mode of publishing outside it: a worker between its
        // predicate check and its wait misses the notify and sleeps with work
        // queued. Correctness survives (the barrier still completes once it
        // wakes) but the feature silently degrades to single-threaded plus
        // overhead, which is exactly the outcome a timing gate cannot
        // distinguish from "the idea does not work".
        std::lock_guard<std::mutex> lock(span_mutex_);
        span_gen_.fetch_add(1, std::memory_order_release);
        span_work_cv_.notify_all();
    }

    const uint64_t t0 = perf_excl::tick_now();
    bool ok = execute_witnessed_fragments(job, job.out, 0, parts);
    const uint64_t t1 = perf_excl::tick_now();
    draw_span_part0_busy_ticks_ += t1 - t0;

    // ---- the barrier ------------------------------------------------------
    if (span_done_.load(std::memory_order_acquire) != helpers) {
        for (int spin = 0; spin < kSpanSpin; ++spin) {
            draw_cpu_pause();
            if (span_done_.load(std::memory_order_acquire) == helpers) break;
        }
        if (span_done_.load(std::memory_order_acquire) != helpers) {
            std::unique_lock<std::mutex> lock(span_mutex_);
            span_emu_waiting_.store(true, std::memory_order_seq_cst);
            span_done_cv_.wait(lock, [this, helpers] {
                return span_done_.load(std::memory_order_seq_cst) == helpers;
            });
            span_emu_waiting_.store(false, std::memory_order_seq_cst);
        }
    }
    draw_span_barrier_wait_ticks_ += perf_excl::tick_now() - t1;
    span_job_ = nullptr;
    ++draw_span_jobs_;

    // ---- merge the four non-additive scalars ------------------------------
    // Everything else -- the ~110 counters and the three job-invariant
    // last-value cells -- is folded straight into the members by
    // fold_draw_telemetry, once per part, so there is no second summation list.
    for (unsigned p = 1; p < parts; ++p) {
        const DrawJobTelemetry& sub = span_out_[p];
        ok = ok && sub.ok;
        // First failure by PART INDEX, not by wall-clock order, so the recorded
        // message is deterministic across runs. F13 already documents that a
        // fault from a threaded run has shifted attribution and must be re-run
        // at --nv2a-draw-threads 0; with a partition the SET of pixels written
        // before the stop is partition-dependent too, so that rule is stronger
        // here, not weaker.
        if (sub.failed && !job.out.failed) {
            job.out.failed = true;
            job.out.fault_message = sub.fault_message;
        }
    }
    job.out.ok = ok;
    return ok;
}

bool Nv2a::submit_vertex_batch() {
#if XLLE_RENDERER_HLE
    return submit_hle_vertex_batch();
#else
    // NV2A_VERTEX: attribute fetch, vertex-shader/fixed-function transform and batch
    // assembly. The raster call below opens its own region, so this bucket's self time is
    // the vertex pipeline alone.
    perf_excl::Scope region(perf_excl::kNv2aVertex);
    std::optional<perf::ScopedAdd> submit_timer;
    if (perf::g_enabled) {
        submit_timer.emplace(perf::g_ns_nv2a_submit_vertex,
                             perf::g_calls_nv2a_submit_vertex);
    }
    const bool indexed = !vertex_indices_.empty();
    const bool inline_array = !inline_vertices_.empty();
    const bool immediate = !immediate_vertices_.empty();
    const uint32_t sources = static_cast<uint32_t>(indexed) +
                             static_cast<uint32_t>(inline_array) +
                             static_cast<uint32_t>(immediate);
    if (!sources) return true;
    if (sources != 1) {
        fault_("NV2A batch mixes indexed, inline-array, or immediate vertices");
        return false;
    }
    if (primitive_mode_ == 0 || primitive_mode_ >= primitive_mode_counts_.size()) {
        fault_("NV2A vertices submitted outside a valid BEGIN/END");
        return false;
    }
    const size_t vertex_count = indexed ? vertex_indices_.size()
                              : inline_array ? inline_vertices_.size()
                                             : immediate_vertices_.size();

    // beads-xbox.1.14.3 item 4 completed: perf::g_enabled removed from the disjunction.
    // The raster-state sample search below is a linear scan per batch; running it only
    // because timing is on is the exact "instrument changes the instrumented code"
    // hazard. --nv2a-detailed-telemetry restores it.
    const bool collect_diagnostics = detailed_telemetry_;
    size_t raster_sample_index = 0;
    RasterStateSample* tracked_raster_sample = nullptr;
    if (collect_diagnostics) {
        auto raster_sample = std::find_if(
            raster_state_samples_.begin(), raster_state_samples_.end(),
            [&](const RasterStateSample& sample) {
                return sample.words == raster_state_;
            });
        if (raster_sample == raster_state_samples_.end()) {
            raster_state_samples_.push_back(RasterStateSample{raster_state_, 1, 0});
            raster_sample_index = raster_state_samples_.size() - 1;
        } else {
            ++raster_sample->batches;
            raster_sample_index =
                static_cast<size_t>(raster_sample - raster_state_samples_.begin());
        }
        tracked_raster_sample = &raster_state_samples_[raster_sample_index];
        current_raster_state_sample_index_ = static_cast<uint32_t>(raster_sample_index);
        tracked_raster_sample->vertices += vertex_count;
        tracked_raster_sample->inline_batches += inline_array ? 1 : 0;
        ++tracked_raster_sample->primitive_batches[primitive_mode_];
    }
    const bool immediate_quad = immediate && primitive_mode_ == 8;
    bool capture_immediate_quad = false;
    if (immediate_quad && tracked_raster_sample) {
        const uint64_t program_hash = fnv1a_bytes(
            transform_program_.data(), sizeof(transform_program_));
        const uint64_t constants_hash = fnv1a_bytes(
            transform_constants_.data(), sizeof(transform_constants_));
        const RasterStateSample::ImmediateQuadTransformVariant identity{
            transform_execution_mode_, transform_program_start_, program_hash, constants_hash};
        ++tracked_raster_sample->immediate_quad_batches;
        const uint32_t count = static_cast<uint32_t>(std::min<size_t>(
            vertex_count, std::numeric_limits<uint32_t>::max()));
        if (tracked_raster_sample->immediate_quad_batches == 1) {
            tracked_raster_sample->immediate_quad_min_vertices = count;
            tracked_raster_sample->immediate_quad_max_vertices = count;
        } else {
            tracked_raster_sample->immediate_quad_min_vertices = std::min(
                tracked_raster_sample->immediate_quad_min_vertices, count);
            tracked_raster_sample->immediate_quad_max_vertices = std::max(
                tracked_raster_sample->immediate_quad_max_vertices, count);
        }
        if (vertex_count % 4 != 0)
            ++tracked_raster_sample->immediate_quad_nonmultiple_of_four;
        capture_immediate_quad =
            !tracked_raster_sample->immediate_quad_first_captured;
        if (capture_immediate_quad) {
            tracked_raster_sample->immediate_quad_first_captured = true;
            tracked_raster_sample->immediate_quad_transform_mode =
                transform_execution_mode_;
            tracked_raster_sample->immediate_quad_transform_start =
                transform_program_start_;
            tracked_raster_sample->immediate_quad_program_hash = program_hash;
            tracked_raster_sample->immediate_quad_constants_hash = constants_hash;
            tracked_raster_sample->immediate_quad_transform_variants[0] = identity;
            tracked_raster_sample->immediate_quad_transform_variant_count = 1;
            ++tracked_raster_sample->immediate_quad_first_transform_batches;
            const size_t captured_vertices = std::min<size_t>(4, vertex_count);
            for (size_t vertex = 0; vertex < captured_vertices; ++vertex) {
                tracked_raster_sample->immediate_quad_decoded_input0[vertex] =
                    immediate_vertices_[vertex][0];
                tracked_raster_sample->immediate_quad_decoded_input3[vertex] =
                    immediate_vertices_[vertex][3];
                tracked_raster_sample->immediate_quad_decoded_input9[vertex] =
                    immediate_vertices_[vertex][9];
            }
        } else {
            const auto& first =
                tracked_raster_sample->immediate_quad_transform_variants[0];
            if (identity.mode == first.mode && identity.start == first.start &&
                identity.program_hash == first.program_hash &&
                identity.constants_hash == first.constants_hash) {
                ++tracked_raster_sample->immediate_quad_first_transform_batches;
            } else {
                ++tracked_raster_sample->immediate_quad_later_transform_batches;
                bool known = false;
                for (uint8_t variant = 0;
                     variant < tracked_raster_sample->immediate_quad_transform_variant_count;
                     ++variant) {
                    const auto& existing =
                        tracked_raster_sample->immediate_quad_transform_variants[variant];
                    known = existing.mode == identity.mode &&
                            existing.start == identity.start &&
                            existing.program_hash == identity.program_hash &&
                            existing.constants_hash == identity.constants_hash;
                    if (known) break;
                }
                if (!known) {
                    if (tracked_raster_sample->immediate_quad_transform_variant_count <
                        tracked_raster_sample->immediate_quad_transform_variants.size()) {
                        tracked_raster_sample->immediate_quad_transform_variants[
                            tracked_raster_sample->immediate_quad_transform_variant_count++] =
                            identity;
                    } else {
                        ++tracked_raster_sample
                              ->immediate_quad_transform_variant_overflow_batches;
                    }
                }
            }
        }
    }

    if (collect_diagnostics) {
        ++transform_mode_batches_[transform_execution_mode_ & 3u];
    }
    if (collect_diagnostics && transform_execution_mode_ == 0) {
        ++fixed_skin_batches_[fixed_skin_mode_];
        if (fixed_lighting_) ++fixed_lighting_batches_;
        if (fixed_normalization_) ++fixed_normalization_batches_;
        if (fixed_specular_) ++fixed_specular_batches_;
        if (fixed_fog_) ++fixed_fog_batches_;
        if (std::any_of(fixed_texture_matrix_enable_.begin(),
                        fixed_texture_matrix_enable_.end(),
                        [](bool enabled) { return enabled; }))
            ++fixed_texture_matrix_batches_;
        bool any_texgen = false;
        for (const auto& stage : fixed_texgen_)
            for (uint32_t mode : stage) any_texgen |= mode != 0;
        if (any_texgen) ++fixed_texgen_batches_;
        for (uint32_t light = 0; light < 8; ++light) {
            const uint32_t type = (fixed_light_enable_mask_ >> (light * 2)) & 3u;
            ++fixed_light_type_batches_[type];
        }
        ++fixed_color_material_batches_[fixed_color_material_];
        ++fixed_light_control_batches_[fixed_light_control_];
    }
    ++vertex_census_.batches;
    Nv2aVshProgram program{};
    bool program_parsed = false;
    bool fixed_function = false;
    // True when `program` owns a malloc this function must release. With the
    // parse cache on and warm, the cache owns it and this stays false.
    bool program_owned = false;
    if (transform_execution_mode_ == 2) {
        if (vertex_program_cache_enabled_ && vsh_cache_valid_) {
            // Phase 5 item 3: the cached parse. Byte-identical to what the call
            // below would produce -- the cache is invalidated by the only two
            // method handlers that can change either input (the program words
            // and the start slot), so a valid cache means the inputs have not
            // moved since it was filled.
            program.steps = static_cast<Nv2aVshStep*>(vsh_cache_steps_);
            program_parsed = true;
            ++vertex_census_.parse_reuses;
        } else {
            const uint64_t parse_start = perf_excl::tick_now();
            const uint32_t slots =
                static_cast<uint32_t>(transform_program_.size()) - transform_program_start_;
            Nv2aVshParseResult result = nv2a_vsh_parse_program(
                &program, transform_program_[transform_program_start_].data(), slots);
            const uint64_t parse_delta = perf_excl::tick_now() - parse_start;
            vertex_census_.parse_ticks += perf_excl::compensated_delta(
                parse_delta, vertex_census_.discarded_deltas);
            ++vertex_census_.parses;
            if (result != NV2AVPR_SUCCESS) {
                char why[128];
                std::snprintf(why, sizeof(why),
                              "NV2A vertex program parse failed at slot %u: result=%u",
                              transform_program_start_, static_cast<unsigned>(result));
                fault_(why);
                return false;
            }
            program_parsed = true;
            program_owned = true;
        }
    } else if (transform_execution_mode_ == 0) {
        bool any_texture_matrix =
            std::any_of(fixed_texture_matrix_enable_.begin(),
                        fixed_texture_matrix_enable_.end(),
                        [](bool enabled) { return enabled; });
        bool any_texgen = false;
        for (const auto& stage : fixed_texgen_)
            for (uint32_t mode : stage) any_texgen |= mode != 0;
        const bool ambient_lit_subset =
            fixed_lighting_ && fixed_specular_ && fixed_light_control_ == 1;
        const bool unlit_subset =
            !fixed_lighting_ && !fixed_specular_ && fixed_light_control_ == 5;
        if (fixed_skin_mode_ != 0 ||
            (!ambient_lit_subset && !unlit_subset) ||
            fixed_normalization_ || fixed_fog_ ||
            any_texture_matrix || any_texgen ||
            fixed_light_enable_mask_ != 0 ||
            fixed_color_material_ != 0) {
            fault_("NV2A fixed-function batch exceeds the witnessed transform subset");
            return false;
        }
        fixed_function = true;
    } else if (transform_execution_mode_ != 0) {
        fault_("NV2A vertex batch uses unsupported transform execution mode");
        return false;
    }
    bool program_writes_context = false;
    VshProgramCensusRow* census_row = nullptr;
#if XLLE_VSH_DRAW_OVERLAP_CENSUS
    VshDrawOverlapProgramIdentity overlap_program_identity{};
#endif
    if (program_parsed && !program_owned) {
        // Served from the cache: vsh_program_writes_context is a pure function
        // of the same steps, so it is cached beside them rather than re-walked.
        program_writes_context = vsh_cache_writes_context_;
        census_row = vsh_cache_census_row_;
#if XLLE_VSH_DRAW_OVERLAP_CENSUS
        overlap_program_identity = vsh_cache_overlap_identity_;
#endif
    } else if (program_parsed) {
        program_writes_context = vsh_program_writes_context(program);
        // beads-xbox.1.15.11. The distinct-program census and the pre-decode
        // are both pure functions of the parse, so both are paid HERE -- once
        // per parse (19 542 per canonical run with the phase-5 cache on), never
        // per batch and never per vertex.
        const uint32_t program_slots =
            static_cast<uint32_t>(transform_program_.size()) -
            transform_program_start_;
        uint64_t fingerprint = 1469598103934665603ull;
        const auto fold_word = [&fingerprint](uint32_t word) {
            for (unsigned byte = 0; byte < 4; ++byte) {
                fingerprint ^= (word >> (byte * 8)) & 0xFFu;
                fingerprint *= 1099511628211ull;
            }
        };
        fold_word(transform_program_start_);
        for (uint32_t slot = transform_program_start_;
             slot < transform_program_.size(); ++slot)
            for (uint32_t word = 0; word < 4; ++word)
                fold_word(transform_program_[slot][word]);
        census_row = vsh_program_census_row(fingerprint);
        if (census_row) {
            census_row->start_slot = transform_program_start_;
            census_row->slots = program_slots;
            census_row->writes_context = program_writes_context;
        }
#if XLLE_VSH_DRAW_OVERLAP_CENSUS
        overlap_program_identity.known = true;
        overlap_program_identity.fingerprint = fingerprint;
        overlap_program_identity.start_slot = transform_program_start_;
        overlap_program_identity.slots = program_slots;
#endif
        bool program_compiled = false;
        if (vsh_compile_enabled_) {
            const uint64_t compile_start = perf_excl::tick_now();
            program_compiled =
                compile_vsh_program(program.steps, program_slots, census_row);
            if (program_compiled && vsh_dot_prefix_enabled_ &&
                !program_writes_context) {
                const VshDotPrefixDispatch dispatch{
                    kVshDispatchTable[NV2AOP_DP3],
                    kVshDispatchTable[NV2AOP_DP4]};
                if (vsh_build_dot_prefix_plan(vsh_cache_ops_.data(),
                                              static_cast<uint32_t>(
                                                  vsh_cache_ops_.size()),
                                              dispatch, vsh_dot_prefix_plan_)) {
                    ++vsh_dot_prefix_stats_.matched_programs;
                    vsh_dot_prefix_stats_.planned_ops +=
                        vsh_dot_prefix_plan_.op_count;
                }
            }
            const uint64_t compile_delta =
                perf_excl::tick_now() - compile_start;
            vertex_census_.vsh_compile_ticks += perf_excl::compensated_delta(
                compile_delta, vertex_census_.discarded_deltas);
            ++vertex_census_.vsh_compiles;
            if (!program_compiled) ++vertex_census_.vsh_compile_declines;
        } else {
            // Arm A of the same-binary A/B. The records from whatever program
            // ran last must not be reachable.
            vsh_cache_ops_valid_ = false;
            vsh_dot_prefix_plan_.clear();
        }
        if (census_row) census_row->compiled = program_compiled;
        vsh_cache_census_row_ = census_row;
#if XLLE_VSH_DRAW_OVERLAP_CENSUS
        vsh_cache_overlap_identity_ = overlap_program_identity;
#endif
        if (vertex_program_cache_enabled_) {
            destroy_vsh_program_cache();
            vsh_cache_steps_ = program.steps;
            vsh_cache_writes_context_ = program_writes_context;
            vsh_cache_valid_ = true;
            // Ownership transferred to the cache: every destroy site below is
            // keyed on program_owned, so the steps outlive this batch.
            program_owned = false;
        }
    }
    transformed_batch_.clear();
#if XLLE_VSH_GPU_RESIDENT
    std::shared_ptr<Nv2aGenericCombinerPacket::GpuVshReference> gpu_vsh;
    const bool vsh_gpu_deferred_requested = vsh_gpu_resident_mode_ == 3;
    bool vsh_gpu_deferred = false;
    bool vsh_gpu_deferred_diagnostics_safe =
        !collect_diagnostics && !profile_hashes_enabled_ && !vertex_census_enabled_ &&
        vsh_verify_budget_ == 0 && !generic_batch_debug_cb_ &&
        !generic_packet_snapshot_filter_cb_ && !generic_packet_snapshot_cb_ &&
        !surface_write_debug_cb_;
#if XLLE_VSH_CAPTURE
    vsh_gpu_deferred_diagnostics_safe =
        vsh_gpu_deferred_diagnostics_safe && !vsh_capture_callback_ &&
        !vsh_batch_capture_callback_;
#endif
#if XLLE_VSH_DRAW_OVERLAP_CENSUS
    vsh_gpu_deferred_diagnostics_safe =
        vsh_gpu_deferred_diagnostics_safe && !vsh_draw_overlap_enabled_;
#endif
#if XLLE_VERTEX_REUSE_DIAGNOSTICS
    vsh_gpu_deferred_diagnostics_safe =
        vsh_gpu_deferred_diagnostics_safe && !vertex_reuse_shadow_;
#endif
    if (vsh_gpu_resident_mode_ && renderer_ && program_parsed && vsh_cache_ops_valid_ &&
        !program_writes_context && !vsh_unsafe_probe_ && vsh_capture_plan_supported_ &&
        !vsh_cache_ops_.empty() && vsh_cache_ops_.size() <= vsh_gpu_batch::kMaxOps &&
        vertex_count >= 3 && vertex_count <= vsh_gpu_batch::kMaxReferences &&
        primitive_mode_ >= 5 && primitive_mode_ <= 7 &&
        raster_state_[kRasterCombinerControl] == 1 &&
        (!vsh_gpu_deferred_requested || vsh_gpu_deferred_diagnostics_safe)) {
        gpu_vsh = std::make_shared<Nv2aGenericCombinerPacket::GpuVshReference>();
        gpu_vsh->mode = vsh_gpu_resident_mode_;
        auto& batch = gpu_vsh->batch;
        batch.ops = capture_vsh_ops();
        batch.shape.primitive_mode = primitive_mode_;
        batch.shape.op_count = static_cast<uint32_t>(batch.ops.size());
        std::memcpy(batch.constants.words.data(), transform_constants_.data(), sizeof(batch.constants));
        batch.executions.reserve(vertex_count);
        batch.references.reserve(vertex_count);
        if (vsh_gpu_deferred_requested) {
            vsh_gpu_deferred = true;
        } else {
            gpu_vsh->expected.reserve(vertex_count);
        }
    }
    if (!vsh_gpu_deferred) transformed_batch_.reserve(vertex_count);
#else
    transformed_batch_.reserve(vertex_count);
#endif
#if XLLE_VSH_CAPTURE
    std::unique_ptr<vsh_batch_capture::Batch> captured_batch;
    if (vsh_batch_capture_callback_) {
        auto& stats = vsh_batch_capture_stats_;
        ++stats.batches;
        if (program_parsed && vsh_cache_ops_valid_ && !program_writes_context &&
            !vsh_unsafe_probe_ && vsh_capture_plan_supported_ && !vsh_cache_ops_.empty() &&
            vsh_cache_ops_.size() <= vsh_capture::kMaxOps &&
            vertex_count >= 3 && vertex_count <= vsh_batch_capture::kMaxReferences &&
            primitive_mode_ >= 5 && primitive_mode_ <= 7) {
            ++stats.eligible;
            if (vsh_batch_capture_interval_ &&
                (stats.eligible - 1) % vsh_batch_capture_interval_ == 0 &&
                stats.selected < vsh_batch_capture_budget_) {
                ++stats.selected;
                captured_batch = std::make_unique<vsh_batch_capture::Batch>();
                // The existing vertex_batches_ counter is telemetry-gated.
                // Capture ordering must also work with telemetry disabled.
                captured_batch->sequence = stats.batches;
                captured_batch->fingerprint = census_row ? census_row->fingerprint : 0;
                captured_batch->primitive_mode = primitive_mode_;
                captured_batch->ops = capture_vsh_ops();
                std::memcpy(captured_batch->constants.data(), transform_constants_.data(),
                            sizeof(captured_batch->constants));
                captured_batch->executions.reserve(vertex_count);
                captured_batch->references.reserve(vertex_count);
                captured_batch->source_indices.reserve(vertex_count);
            }
        }
    }
    std::array<uint32_t, 1024> capture_cache_ids{};
#endif

    constexpr size_t kBatchTransformCacheEntries = 1024;

    struct BatchTransformCache {
        BatchTransformCache() { valid.fill(0); }

        std::array<uint8_t, kBatchTransformCacheEntries> valid;
        std::array<uint32_t, kBatchTransformCacheEntries> indices;
        std::array<TransformedVertex, kBatchTransformCacheEntries> vertices;
#if XLLE_VSH_GPU_RESIDENT
        std::array<uint32_t, kBatchTransformCacheEntries> gpu_execution_ids{};
#endif
    };
    std::optional<BatchTransformCache> batch_transform_cache;
    if (indexed && program_parsed && !program_writes_context && vertex_count >= 64) {
        batch_transform_cache.emplace();
    }
#if XLLE_VSH_CAPTURE
    if (captured_batch) captured_batch->cache_enabled = batch_transform_cache.has_value();
#endif
    auto batch_transform_cache_base = [](uint32_t index) {
        const uint32_t mixed = index ^ (index >> 4) ^ (index >> 9);
        return static_cast<size_t>(mixed & (kBatchTransformCacheEntries - 1));
    };
    auto lookup_batch_transform_cache =
        [&](uint32_t index, TransformedVertex& vertex) {
            if (!batch_transform_cache) return false;
            const size_t base = batch_transform_cache_base(index);
            if (!batch_transform_cache->valid[base] ||
                batch_transform_cache->indices[base] != index) {
                return false;
            }
            vertex = batch_transform_cache->vertices[base];
            return true;
        };
    auto store_batch_transform_cache =
        [&](uint32_t index, const TransformedVertex& vertex) {
            if (!batch_transform_cache) return;
            const size_t base = batch_transform_cache_base(index);
            batch_transform_cache->valid[base] = 1;
            batch_transform_cache->indices[base] = index;
            batch_transform_cache->vertices[base] = vertex;
        };
#if XLLE_VSH_GPU_RESIDENT
    auto lookup_batch_transform_cache_gpu_id =
        [&](uint32_t index, uint32_t& id) {
            if (!batch_transform_cache) return false;
            const size_t base = batch_transform_cache_base(index);
            if (!batch_transform_cache->valid[base] ||
                batch_transform_cache->indices[base] != index) {
                return false;
            }
            id = batch_transform_cache->gpu_execution_ids[base];
            return true;
        };
    auto store_batch_transform_cache_gpu_id =
        [&](uint32_t index, uint32_t id) {
            if (!batch_transform_cache) return;
            const size_t base = batch_transform_cache_base(index);
            batch_transform_cache->valid[base] = 1;
            batch_transform_cache->indices[base] = index;
            batch_transform_cache->gpu_execution_ids[base] = id;
        };
#endif

    auto record_transformed_vertex_stats = [&](const TransformedVertex& vertex) {
        for (float component : vertex.position) {
            uint32_t bits = 0;
            std::memcpy(&bits, &component, sizeof(bits));
            transform_output_hash_ ^= bits;
            transform_output_hash_ *= 1099511628211ull;
        }
        ++transformed_vertices_;
    };
    auto emit_transformed_vertex = [&](const TransformedVertex& vertex) {
        transformed_batch_.push_back(vertex);
        if (collect_diagnostics) record_transformed_vertex_stats(vertex);
    };
#if XLLE_VSH_DRAW_OVERLAP_CENSUS
    VshDrawOverlapJob overlap_job;
#endif

    {
        std::optional<perf::ScopedAdd> transform_timer;
        if (perf::g_enabled) {
            transform_timer.emplace(perf::g_ns_nv2a_vertex_transform,
                                    perf::g_calls_nv2a_vertex_transform);
        }
        vertex_census_.vertices += vertex_count;
        const bool vertex_census_on = vertex_census_enabled_;
        // Phase 5 item 3, exactness-neutral hoist. fetch_vertex_attribute's
        // FIRST statement is `if (!attr.count) return true;` -- an inactive
        // slot reads nothing, writes nothing and cannot fault -- and
        // attr.count only moves through SET_VERTEX_ARRAY_FORMAT, which is a
        // method and therefore cannot run inside this batch. So the per-vertex
        // loop over all 16 slots is a loop over ~4 real ones plus 12 calls
        // that return immediately, 52.5 M times per canonical run. The active
        // list is built ONCE per batch and the loop runs over it instead. The
        // sequence of real fetches, and their order, are unchanged.
        const bool fast_fetch = vertex_fast_fetch_;
        // beads-xbox.1.15.11. Hoisted per batch: whether this program has a
        // pre-decoded form, and where it is. An empty compiled program is a
        // real possibility (every step a NOP) and must not be mistaken for a
        // missing one, which is why the emptiness is NOT part of the test.
        const bool use_compiled_vsh = vsh_cache_ops_valid_;
        const uint32_t vsh_probe = vsh_unsafe_probe_;
#if XLLE_VERTEX_REUSE_DIAGNOSTICS
        // Shadow diagnostics never skip fetch or shader work. Keep body timing
        // separate: otherwise these probes would be charged to the VSH census.
        Nv2aVertexReuseShadow* shadow = nullptr;
        if (vertex_reuse_shadow_) {
            auto& c = vertex_reuse_shadow_census_;
            if (!program_parsed) ++c.ineligible_nonprogrammable;
            else if (program_writes_context) ++c.ineligible_context;
            else if (vsh_probe) ++c.ineligible_probe;
            else if (vsh_verify_budget_) ++c.ineligible_verify;
            else if (capture_immediate_quad) ++c.ineligible_capture;
            else if (vertex_census_on) ++c.ineligible_census;
            else {
                shadow = vertex_reuse_shadow_.get();
                const uint64_t t = vertex_reuse_shadow_timing_ ? perf_excl::tick_now() : 0;
                shadow->begin_batch(transform_program_, transform_program_start_,
                                    transform_constants_, use_compiled_vsh);
                if (vertex_reuse_shadow_timing_)
                    c.state_ticks += perf_excl::compensated_delta(
                        perf_excl::tick_now() - t, c.discarded_deltas);
            }
        }
#endif
        const VshZeroPlan vsh_zero_plan = vsh_cache_zero_plan_;
        const VshCompiledOp* const compiled_ops = vsh_cache_ops_.data();
        const uint32_t compiled_op_count =
            static_cast<uint32_t>(vsh_cache_ops_.size());
        const uint32_t dot_prefix_op_count = vsh_dot_prefix_plan_.op_count;
        const bool use_dot_prefix =
            use_compiled_vsh && vsh_dot_prefix_enabled_ &&
            dot_prefix_op_count >= 2u && !program_writes_context && !vsh_probe;
        uint64_t program_vertices = 0;
#if XLLE_VSH_DRAW_OVERLAP_CENSUS
        uint64_t overlap_index_cache_hits = 0;
#endif
        std::array<uint8_t, 16> active_attribute_slots{};
        uint32_t active_attribute_count = 0;
        for (uint32_t slot = 0; slot < vertex_attributes_.size(); ++slot) {
            if (!fast_fetch || vertex_attributes_[slot].count)
                active_attribute_slots[active_attribute_count++] =
                    static_cast<uint8_t>(slot);
        }
        for (size_t vertex_index = 0; vertex_index < vertex_count; ++vertex_index) {
            if (inline_array || immediate) {
                const auto& inputs = inline_array ? inline_vertices_[vertex_index]
                                                  : immediate_vertices_[vertex_index];
                for (uint32_t slot = 0; slot < vertex_attributes_.size(); ++slot)
                    vertex_attributes_[slot].value = inputs[slot];
            } else {
                const uint32_t index = vertex_indices_[vertex_index];
                largest_vertex_index_ = std::max(largest_vertex_index_, index);
#if XLLE_VSH_GPU_RESIDENT
                if (vsh_gpu_deferred) {
                    uint32_t cached_id = 0;
                    if (lookup_batch_transform_cache_gpu_id(index, cached_id)) {
                        gpu_vsh->batch.references.push_back(cached_id);
                        ++vertex_census_.reuse_hits;
                        continue;
                    }
                }
#endif
                TransformedVertex cached_vertex;
                if (lookup_batch_transform_cache(index, cached_vertex)) {
#if XLLE_VSH_GPU_RESIDENT
                    if (gpu_vsh)
                        gpu_vsh->batch.references.push_back(
                            batch_transform_cache->gpu_execution_ids[
                                batch_transform_cache_base(index)]);
#endif
#if XLLE_VSH_CAPTURE
                    if (vsh_capture_callback_) ++vsh_capture_stats_.cache_hits;
                    if (captured_batch) {
                        captured_batch->source_indices.push_back(index);
                        captured_batch->references.push_back(capture_cache_ids[batch_transform_cache_base(index)]);
                    }
#endif
                    ++vertex_census_.reuse_hits;
#if XLLE_VSH_DRAW_OVERLAP_CENSUS
                    if (vsh_draw_overlap_enabled_) ++overlap_index_cache_hits;
#endif
#if XLLE_VERTEX_REUSE_DIAGNOSTICS
                    if (shadow) ++vertex_reuse_shadow_census_.batch_cache_bypassed;
#endif
                    emit_transformed_vertex(cached_vertex);
                    continue;
                }
                // Phase 5 item 3: attribute fetch, timed per VERTEX and
                // therefore only under --nv2a-vertex-census (rule 9).
                const uint64_t fetch_start =
                    vertex_census_on ? perf_excl::tick_now() : 0u;
                for (uint32_t i = 0; i < active_attribute_count; ++i) {
                    if (!fetch_vertex_attribute(active_attribute_slots[i],
                                                index)) {
                        if (program_owned) nv2a_vsh_program_destroy(&program);
                        return false;
                    }
                }
                if (vertex_census_on) {
                    const uint64_t d = perf_excl::tick_now() - fetch_start;
                    vertex_census_.fetch_ticks += perf_excl::compensated_delta(
                        d, vertex_census_.discarded_deltas);
                    ++vertex_census_.fetch_n;
                }
            }
            TransformedVertex vertex;
            if (program_parsed) {
                const uint64_t vsh_start =
                    vertex_census_on ? perf_excl::tick_now() : 0u;
                const float* output_regs = nullptr;
                Nv2aVshCPUFullExecutionState full_state;
                // NOT value-initialized: every one of its 16 * 4 floats is
                // overwritten by the vertex_attributes_ copy below before the
                // interpreter runs, so the 256-byte zero-fill was dead. The
                // other three register files ARE zeroed, because the
                // interpreter can read a temp or an output the program never
                // wrote and their initial value is observable.
                std::array<float, 16 * 4> input_regs;
                // Arm A of the same-binary A/B restores the zero-fill verbatim.
                if (!fast_fetch) input_regs.fill(0.0f);
                bool input_regs_loaded = false;
                auto load_input_regs = [&]() {
                    if (input_regs_loaded) return;
                    for (uint32_t slot = 0; slot < vertex_attributes_.size(); ++slot) {
                        std::memcpy(&input_regs[slot * 4],
                                    vertex_attributes_[slot].value.data(),
                                    4 * sizeof(float));
                    }
                    input_regs_loaded = true;
                };
#if XLLE_VSH_GPU_RESIDENT
                if (vsh_gpu_deferred && !program_writes_context) {
                    load_input_regs();
                    auto& batch = gpu_vsh->batch;
                    vsh_gpu_batch::ExecutionInput input;
                    std::memcpy(input.words.data(), input_regs.data(), sizeof(input));
                    const uint32_t id =
                        static_cast<uint32_t>(batch.executions.size());
                    batch.executions.push_back(input);
                    batch.references.push_back(id);
                    if (indexed)
                        store_batch_transform_cache_gpu_id(
                            vertex_indices_[vertex_index], id);
                    continue;
                }
#endif
                // beads-xbox.1.15.11: 416 bytes of zero-fill per vertex, of
                // which the compiled form's read-before-write analysis usually
                // proves most dead. The interpreter arm still fills all three,
                // because without the analysis any of them is observable.
                std::array<float, 13 * 4> output_regs_storage;
                std::array<float, 12 * 4> temp_regs;
                std::array<float, 4> address_reg;
                if (use_compiled_vsh) {
                    vsh_apply_zero_plan(vsh_zero_plan, output_regs_storage.data(),
                                        temp_regs.data(), address_reg.data());
                } else {
                    output_regs_storage.fill(0.0f);
                    temp_regs.fill(0.0f);
                    address_reg.fill(0.0f);
                }
                ++program_vertices;
                if (program_writes_context) {
                    Nv2aVshExecutionState execution =
                        nv2a_vsh_emu_initialize_full_execution_state(&full_state);
                    std::memcpy(full_state.context_regs, transform_constants_.data(),
                                sizeof(full_state.context_regs));
                    for (uint32_t slot = 0; slot < vertex_attributes_.size(); ++slot) {
                        std::memcpy(&full_state.input_regs[slot * 4],
                                    vertex_attributes_[slot].value.data(),
                                    4 * sizeof(float));
                    }
                    if (use_compiled_vsh) {
                        // The compiled form takes the five register files as a
                        // base table in the fixed order nv2a_vsh_compiled.hpp
                        // declares; the vendored walker takes the same five
                        // pointers inside Nv2aVshExecutionState.
                        float* bases[kVshBaseCount] = {
                            full_state.temp_regs, full_state.output_regs,
                            full_state.input_regs, full_state.context_regs,
                            full_state.address_reg};
                        if (vsh_probe)
                            vsh_execute_compiled_probe(
                                compiled_ops, compiled_op_count, bases,
                                vsh_probe);
                        else
                            vsh_execute_compiled(compiled_ops,
                                                 compiled_op_count, bases);
                        ++vertex_census_.vsh_compiled_vertices;
                    } else {
                        nv2a_vsh_emu_execute(&execution, &program);
                        ++vertex_census_.vsh_interpreted_vertices;
                    }
                    output_regs = full_state.output_regs;
                } else {
                    Nv2aVshExecutionState execution{
                        input_regs.data(),
                        output_regs_storage.data(),
                        temp_regs.data(),
                        const_cast<float*>(transform_constants_[0].data()),
                        address_reg.data(),
                        nullptr};
                    load_input_regs();
#if XLLE_VERTEX_REUSE_DIAGNOSTICS
                    if (shadow) {
                        const uint64_t t = vertex_reuse_shadow_timing_ ? perf_excl::tick_now() : 0;
                        shadow->observe(input_regs.data());
                        if (vertex_reuse_shadow_timing_) {
                            auto& c = vertex_reuse_shadow_census_;
                            c.lookup_ticks += perf_excl::compensated_delta(
                                perf_excl::tick_now() - t, c.discarded_deltas);
                            ++c.timed_lookups;
                        }
                    }
#endif
                    if (use_compiled_vsh) {
                        float* bases[kVshBaseCount] = {
                            temp_regs.data(), output_regs_storage.data(),
                            input_regs.data(),
                            const_cast<float*>(transform_constants_[0].data()),
                            address_reg.data()};
                        if (vsh_probe)
                            vsh_execute_compiled_probe(
                                compiled_ops, compiled_op_count, bases,
                                vsh_probe);
                        else if (use_dot_prefix) {
                            const uint32_t suffix_start =
                                vsh_execute_dot_prefix(vsh_dot_prefix_plan_,
                                                       bases);
                            vsh_execute_compiled(compiled_ops + suffix_start,
                                                 compiled_op_count -
                                                     suffix_start,
                                                 bases);
                        } else {
                            vsh_execute_compiled(compiled_ops,
                                                 compiled_op_count, bases);
                        }
                        if (vsh_verify_budget_ &&
                            vsh_verify_checked_ < vsh_verify_budget_)
                            vsh_verify_vertex(program.steps, input_regs.data(),
                                              output_regs_storage.data());
                        ++vertex_census_.vsh_compiled_vertices;
                    } else {
                        nv2a_vsh_emu_execute(&execution, &program);
                        ++vertex_census_.vsh_interpreted_vertices;
                    }
                    output_regs = output_regs_storage.data();
                }

                if (capture_immediate_quad && vertex_index < 4) {
                    auto& evidence = raster_state_samples_[raster_sample_index];
                    std::memcpy(evidence.immediate_quad_opos[vertex_index].data(),
                                &output_regs[0], 4 * sizeof(float));
                    std::memcpy(evidence.immediate_quad_od0[vertex_index].data(),
                                &output_regs[3 * 4], 4 * sizeof(float));
                    std::memcpy(evidence.immediate_quad_ot0[vertex_index].data(),
                                &output_regs[9 * 4], 4 * sizeof(float));
                }

                std::memcpy(vertex.position.data(), &output_regs[0],
                            4 * sizeof(float));
                vertex.position[0] = round_screen_coordinate(vertex.position[0]);
                vertex.position[1] = round_screen_coordinate(vertex.position[1]);
                vertex.position[3] = clamp_away_zero_inf(vertex.position[3]);
                if (capture_immediate_quad && vertex_index < 4)
                    raster_state_samples_[raster_sample_index]
                        .immediate_quad_raster_opos[vertex_index] = vertex.position;
                std::memcpy(vertex.diffuse.data(), &output_regs[3 * 4],
                            4 * sizeof(float));
                std::memcpy(vertex.specular.data(), &output_regs[4 * 4],
                            4 * sizeof(float));
                for (uint32_t texture = 0; texture < 4; ++texture) {
                    std::memcpy(vertex.texture[texture].data(),
                                &output_regs[(9 + texture) * 4],
                                4 * sizeof(float));
                }
#if XLLE_VSH_GPU_RESIDENT
                if (gpu_vsh) {
                    auto& batch = gpu_vsh->batch;
                    vsh_gpu_batch::ExecutionInput input;
                    std::memcpy(input.words.data(), input_regs.data(), sizeof(input));
                    vsh_gpu_batch::ProgramOutput128B expected;
                    for (size_t i = 0; i < vsh_capture::kConsumedOutputs.size(); ++i)
                        std::memcpy(expected.values[i].data(),
                            output_regs + vsh_capture::kConsumedOutputs[i] * 4, 16);
                    std::memcpy(expected.values[7].data(), vertex.position.data(), 16);
                    const uint32_t id = static_cast<uint32_t>(batch.executions.size());
                    batch.executions.push_back(input);
                    gpu_vsh->expected.push_back(expected);
                    batch.references.push_back(id);
                    if (indexed)
                        store_batch_transform_cache_gpu_id(
                            vertex_indices_[vertex_index], id);
                }
#endif
#if XLLE_VSH_CAPTURE
                capture_vsh_vertex(use_compiled_vsh && !program_writes_context && !vsh_probe,
                    census_row ? census_row->fingerprint : 0,
                    input_regs.data(), output_regs, vertex.position.data());
                if (captured_batch) {
                    vsh_batch_capture::Execution execution;
                    std::memcpy(execution.inputs.data(), input_regs.data(), sizeof(execution.inputs));
                    for (size_t i = 0; i < vsh_capture::kConsumedOutputs.size(); ++i)
                        std::memcpy(execution.expected.data() + i * 4,
                            output_regs + vsh_capture::kConsumedOutputs[i] * 4, 4 * sizeof(float));
                    std::memcpy(execution.expected.data() + 28, vertex.position.data(), 4 * sizeof(float));
                    const uint32_t id = static_cast<uint32_t>(captured_batch->executions.size());
                    const uint32_t source = indexed ? vertex_indices_[vertex_index] :
                        static_cast<uint32_t>(vertex_index);
                    captured_batch->executions.push_back(execution);
                    captured_batch->source_indices.push_back(source);
                    captured_batch->references.push_back(id);
                    if (captured_batch->cache_enabled)
                        capture_cache_ids[batch_transform_cache_base(source)] = id;
                }
#endif
                if (vertex_census_on) {
                    const uint64_t d = perf_excl::tick_now() - vsh_start;
                    vertex_census_.vsh_ticks += perf_excl::compensated_delta(
                        d, vertex_census_.discarded_deltas);
                    ++vertex_census_.vsh_n;
                    if (program_writes_context)
                        ++vertex_census_.vsh_context_vertices;
                }
            } else if (fixed_function) {
                const auto& position = vertex_attributes_[0].value;
                std::array<float, 4> clip{};
                for (uint32_t component = 0; component < 4; ++component) {
                    for (uint32_t term = 0; term < 4; ++term)
                        clip[component] +=
                            position[term] * transform_constants_[component][term];
                }
                clip[3] = clamp_away_zero_inf(clip[3]);
                vertex.position[0] = round_screen_coordinate(
                    clip[0] / clip[3] + transform_constants_[0x3B][0]);
                vertex.position[1] = round_screen_coordinate(
                    clip[1] / clip[3] + transform_constants_[0x3B][1]);
                vertex.position[2] = clip[2] / clip[3];
                vertex.position[3] = clip[3];

                if (fixed_lighting_) {
                    for (uint32_t component = 0; component < 3; ++component) {
                        vertex.diffuse[component] =
                            fixed_scene_ambient_[component] *
                                fixed_material_emission_[component] +
                            fixed_scene_ambient_[component];
                    }
                    vertex.diffuse[3] = fixed_material_alpha_;
                    vertex.specular = {0.0f, 0.0f, 0.0f,
                                       (fixed_light_control_ & 4u)
                                           ? vertex_attributes_[4].value[3] : 1.0f};
                } else {
                    vertex.diffuse = vertex_attributes_[3].value;
                    vertex.specular = {0.0f, 0.0f, 0.0f, 1.0f};
                }
                for (uint32_t texture = 0; texture < 4; ++texture)
                    vertex.texture[texture] = vertex_attributes_[9 + texture].value;
                if (vertex_census_on) ++vertex_census_.fixed_function_vertices;
            }
            if (program_parsed || fixed_function) {
                if (indexed && program_parsed && !program_writes_context)
                    store_batch_transform_cache(vertex_indices_[vertex_index], vertex);
                emit_transformed_vertex(vertex);
            }
        }
        // beads-xbox.1.15.11: the per-program denominator, folded once per
        // batch rather than per vertex. `program_vertices` counts the vertices
        // that actually RAN the program, so it excludes the 59 % the batch
        // transform cache serves and the fixed-function batches entirely.
        if (census_row) {
            ++census_row->batches;
            census_row->vertices += program_vertices;
        }
        if (use_dot_prefix && program_vertices) {
            vsh_dot_prefix_stats_.executed_vertices += program_vertices;
            vsh_dot_prefix_stats_.executed_ops +=
                program_vertices * dot_prefix_op_count;
        }
#if XLLE_VSH_DRAW_OVERLAP_CENSUS
        if (vsh_draw_overlap_enabled_) {
            overlap_job.valid = true;
            overlap_job.program = overlap_program_identity;
            overlap_job.submitted_vertices = vertex_count;
            overlap_job.transformed_vertices = transformed_batch_.size();
            overlap_job.executed_vsh_vertices = program_vertices;
            overlap_job.index_cache_hits = overlap_index_cache_hits;
            overlap_job.transform_mode = transform_execution_mode_;
            overlap_job.fixed_function = fixed_function;
            overlap_job.programmable = program_parsed;
            overlap_job.compiled = program_parsed && use_compiled_vsh;
            overlap_job.writes_context = program_writes_context;
        }
#endif
    }
    if (program_owned) nv2a_vsh_program_destroy(&program);
    if (capture_immediate_quad) {
        auto& evidence = raster_state_samples_[raster_sample_index];
        if (!program_parsed) {
            const size_t captured_vertices =
                std::min<size_t>(4, transformed_batch_.size());
            for (size_t vertex = 0; vertex < captured_vertices; ++vertex) {
                evidence.immediate_quad_opos[vertex] = transformed_batch_[vertex].position;
                evidence.immediate_quad_od0[vertex] = transformed_batch_[vertex].diffuse;
                evidence.immediate_quad_ot0[vertex] = transformed_batch_[vertex].texture[0];
                evidence.immediate_quad_raster_opos[vertex] =
                    transformed_batch_[vertex].position;
            }
        }

        const uint32_t texture_format =
            raster_state_[kRasterTextureFormat0];
        const bool texture_uses_dma_b = (texture_format & 2u) != 0;
        const bool texture_dma_bound = texture_uses_dma_b
            ? texture_dma_b_bound_ : texture_dma_a_bound_;
        const uint32_t texture_dma = texture_uses_dma_b
            ? texture_dma_b_ : texture_dma_a_;
        const uint32_t texture_offset =
            raster_state_[kRasterTextureOffset0];
        constexpr size_t kProbeBytes = 16;
        uint32_t texture_last = 0;
        if (texture_dma_bound &&
            texture_offset <= UINT32_MAX - (kProbeBytes - 1) &&
            dma_object_address(texture_dma, texture_offset,
                               evidence.immediate_quad_t0_address) &&
            dma_object_address(texture_dma,
                               texture_offset + static_cast<uint32_t>(kProbeBytes - 1),
                               texture_last) &&
            texture_last >= evidence.immediate_quad_t0_address &&
            texture_last - evidence.immediate_quad_t0_address == kProbeBytes - 1) {
            evidence.immediate_quad_t0_dma_resolved = true;
            if (mem_read_ && mem_read_(evidence.immediate_quad_t0_address,
                                      evidence.immediate_quad_t0_probe.data(),
                                      kProbeBytes)) {
                evidence.immediate_quad_t0_probe_read = true;
                evidence.immediate_quad_t0_probe_size = kProbeBytes;
            }
        }

        const uint32_t texture_color = (texture_format >> 8) & 0x7Fu;
        evidence.immediate_quad_palette_relevant = texture_color == 0x0Bu;
        if (evidence.immediate_quad_palette_relevant) {
            const uint32_t palette = raster_state_[kRasterTexturePalette0];
            const bool palette_uses_dma_b = (palette & 1u) != 0;
            const bool palette_dma_bound = palette_uses_dma_b
                ? texture_dma_b_bound_ : texture_dma_a_bound_;
            const uint32_t palette_dma = palette_uses_dma_b
                ? texture_dma_b_ : texture_dma_a_;
            const uint32_t palette_offset = palette & ~0x3Fu;
            constexpr size_t kPaletteProbeBytes = 4 * sizeof(uint32_t);
            uint32_t palette_last = 0;
            if (palette_dma_bound &&
                palette_offset <= UINT32_MAX - (kPaletteProbeBytes - 1) &&
                dma_object_address(palette_dma, palette_offset,
                                   evidence.immediate_quad_palette_address) &&
                dma_object_address(palette_dma,
                                   palette_offset + static_cast<uint32_t>(kPaletteProbeBytes - 1),
                                   palette_last) &&
                palette_last >= evidence.immediate_quad_palette_address &&
                palette_last - evidence.immediate_quad_palette_address ==
                    kPaletteProbeBytes - 1) {
                evidence.immediate_quad_palette_dma_resolved = true;
                if (mem_read_ && mem_read_(evidence.immediate_quad_palette_address,
                                          evidence.immediate_quad_palette_probe.data(),
                                          kPaletteProbeBytes)) {
                    evidence.immediate_quad_palette_probe_read = true;
                    evidence.immediate_quad_palette_probe_entries = 4;
                }
            }
        }
    }
    // ==================== THE SEAM (stage (d)) ==========================
    // DESIGN-NV2A-SEAM.md section 1.1 candidate (C). Everything above this line
    // -- attribute fetch, VSH/fixed-function transform, the immediate-quad
    // evidence capture -- ran on the emulation thread and stays there. From here
    // the batch is an owned DrawJob. Profile match, the subset validators,
    // surface resolution and the texture snapshots still run on this thread
    // (they fault, and faults must be raised in guest order at the instruction
    // that caused them, section 1.1) -- they run inside the executor's latch
    // half and write their results into the job.
    DrawJob job;
    latch_draw_job(job);
#if XLLE_VSH_GPU_RESIDENT
    if (gpu_vsh) {
        gpu_vsh->batch.shape.execution_count = static_cast<uint32_t>(gpu_vsh->batch.executions.size());
        gpu_vsh->batch.shape.reference_count = static_cast<uint32_t>(gpu_vsh->batch.references.size());
        if (gpu_vsh->mode == 3) {
            job.gpu_vsh_compiled_ops = vsh_cache_ops_;
            ++vsh_gpu_deferred_stats_.deferred_batches;
            vsh_gpu_deferred_stats_.deferred_executions +=
                gpu_vsh->batch.executions.size();
            vsh_gpu_deferred_stats_.deferred_references +=
                gpu_vsh->batch.references.size();
        }
        job.gpu_vsh = std::move(gpu_vsh);
    }
#endif
#if XLLE_VSH_CAPTURE
    job.vsh_batch_capture = std::move(captured_batch);
#endif
#if XLLE_VSH_DRAW_OVERLAP_CENSUS
    job.vsh_overlap = overlap_job;
#endif
    bool raster_executed = false;
    bool raster_ok;
    {
        std::optional<perf::ScopedAdd> t;
        if (perf::g_enabled) t.emplace(perf::g_ns_raster, perf::g_calls_raster);
        // Stage (d): inline, immediately, at the latch point. Stage (e) publishes
        // the job to a ring and a worker calls this same function.
        raster_ok = execute_draw_job(job, raster_executed);
    }
#if XLLE_VSH_DRAW_OVERLAP_CENSUS
    if (!raster_ok)
        job.vsh_overlap.outcome = VshDrawOverlapOutcome::kFault;
#endif
    retire_draw_job(job, raster_executed);
    if (!raster_ok) return false;
    if (raster_executed && tracked_raster_sample)
        ++raster_state_samples_[raster_sample_index].executed_batches;

    if (collect_diagnostics) {
        const uint64_t n = vertex_count;
        uint64_t primitives = 0, triangles = 0;
        switch (primitive_mode_) {
        case 1:  // points
            primitives = n;
            break;
        case 2:  // lines
            primitives = n / 2;
            break;
        case 3:  // line loop
            primitives = n >= 2 ? n : 0;
            break;
        case 4:  // line strip
            primitives = n >= 2 ? n - 1 : 0;
            break;
        case 5:  // triangles
            primitives = triangles = n / 3;
            break;
        case 6:  // triangle strip
        case 7:  // triangle fan
            primitives = triangles = n >= 3 ? n - 2 : 0;
            break;
        case 8:  // quads
            primitives = n / 4;
            triangles = primitives * 2;
            break;
        case 9:  // quad strip
            primitives = n >= 4 ? (n - 2) / 2 : 0;
            triangles = primitives * 2;
            break;
        case 10:  // polygon
            primitives = n >= 3 ? 1 : 0;
            triangles = n >= 3 ? n - 2 : 0;
            break;
        }
        ++vertex_batches_;
        submitted_vertices_ += n;
        if (immediate) {
            ++immediate_batches_;
            immediate_vertices_total_ += n;
        }
        assembled_primitives_ += primitives;
        assembled_triangles_ += triangles;
        ++primitive_mode_counts_[primitive_mode_];
    }
    return true;
#endif
}

bool Nv2a::rasterize_witnessed_immediate_quad(DrawJob& job, bool& executed) {
    executed = false;

    // The authoritative 11.5B capture exposes alternating A4, I8/palette,
    // RS8/RS13 A8R8G8B8, RS16/RS17 I8 menu-column, and IQRS3/IQRS7 upper-left
    // I8 artwork immediate quads. Keep every other mode-8 state
    // recorder-only until its own texture/combiner behavior is witnessed.
    // Stage (d) seam (beads-xbox.1.15.4, DESIGN-NV2A-SEAM.md section 3.2):
    // the executor reads the LATCHED JOB, never the live members. These
    // aliases are the whole of the mechanical redirection -- every use site
    // below is textually unchanged, so no expression, and in particular no
    // floating-point expression, moved with the split.
    const auto& raster_state = job.raster_state;
    const auto& transformed_batch = job.vertices;
    const uint32_t surface_format = job.surface_format;
    const uint32_t surface_pitch = job.surface_pitch;
    const uint32_t surface_clip_horizontal = job.surface_clip_horizontal;
    const uint32_t surface_clip_vertical = job.surface_clip_vertical;
    const uint32_t surface_color_offset = job.surface_color_offset;
    const uint32_t surface_zeta_offset = job.surface_zeta_offset;
    const bool immediate_source =
        !immediate_vertices_.empty() &&
        immediate_vertices_.size() == transformed_batch.size();
    const size_t vertex_count = transformed_batch.size();
    const uint32_t color_offset = surface_color_offset;
    const bool witnessed_target =
        color_offset == 0x03DF4000 || color_offset == 0x03C8C000;
    const bool a4_source_shape =
        immediate_source && vertex_count >= 28 && vertex_count <= 44 &&
        (vertex_count & 3u) == 0;
    // Like the A4 profile, this matches the captured pixel pipeline rather
    // than freezing a particular selected-row's post-VSH varyings.
    const bool i8_source_shape = immediate_source && vertex_count == 4;
    const bool a8_source_shape = immediate_source && vertex_count == 4;
    const bool menu_i8_source_shape = immediate_source && vertex_count == 4;
    const bool artwork_i8_source_shape = immediate_source && vertex_count == 4;
    const bool main_panel_source_shape = immediate_source && vertex_count == 4;
    const bool fullscreen_source_shape = immediate_source && vertex_count == 4;
    // Program/constants hashes are retained as provenance evidence, not as
    // raster semantics: once the VSH has produced oPos/oD0/oT0, the same
    // guest-visible pixel state must process those varyings identically.
    const bool transform_state =
        transform_execution_mode_ == 2 && transform_program_start_ == 0;
    const bool surface_state =
        surface_format == 0x00000128 &&
        surface_pitch == 0x00400C00 && witnessed_target &&
        surface_zeta_offset == 0 &&
        surface_clip_horizontal == 0x00B4010E &&
        surface_clip_vertical == 0x00D7009E &&
        raster_state[kRasterWindowClipHorizontal] == 0x02D00000 &&
        raster_state[kRasterWindowClipVertical] == 0x01E00000;
    const bool menu_i8_surface_state =
        surface_format == 0x00000128 &&
        surface_pitch == 0x00400C00 && witnessed_target &&
        surface_zeta_offset == 0 &&
        surface_clip_horizontal == 0x02D00000 &&
        surface_clip_vertical == 0x01E00000 &&
        raster_state[kRasterWindowClipHorizontal] == 0x02D00000 &&
        raster_state[kRasterWindowClipVertical] == 0x01E00000;
    uint32_t a4_raster_reject_mask = 0;
    auto reject_raster = [&](size_t bit, bool matches) {
        if (!matches) a4_raster_reject_mask |= 1u << bit;
    };
    reject_raster(0, raster_state[kRasterEnables] == 0x00000007);
    reject_raster(1, raster_state[kRasterControl0] == 0x00100001);
    reject_raster(2, raster_state[kRasterAlphaFunc] == 0x0206);
    // NV097_SET_ALPHA_REF is an 8-bit comparison value. Preserve the raw
    // retained word in telemetry, but only its guest-visible low byte gates.
    reject_raster(3, (raster_state[kRasterAlphaRef] & 0xFFu) == 8);
    reject_raster(4, raster_state[kRasterBlendSrc] == 0x0302);
    reject_raster(5, raster_state[kRasterBlendDst] == 0x0303);
    reject_raster(6, raster_state[kRasterBlendEquation] == 0x8006);
    reject_raster(7, raster_state[kRasterDepthFunc] == 0x0203);
    reject_raster(8, raster_state[kRasterColorMask] == 0x01010101);
    reject_raster(9, raster_state[kRasterDepthMask] == 1);
    reject_raster(10, raster_state[kRasterShadeMode] == 0x1D01);
    reject_raster(11,
                  raster_state[kRasterPolygonOffsetScale] == 0x80000000);
    reject_raster(12,
                  raster_state[kRasterPolygonOffsetBias] == 0x80000000);
    reject_raster(13, raster_state[kRasterFrontPolygonMode] == 0x1B02);
    reject_raster(14, raster_state[kRasterBackPolygonMode] == 0x1B02);
    reject_raster(15, raster_state[kRasterClipMin] == 0);
    reject_raster(16, raster_state[kRasterClipMax] == 0);
    reject_raster(17, raster_state[kRasterCullFace] == 0x0405);
    reject_raster(18, raster_state[kRasterFrontFace] == 0x0900);
    reject_raster(19, raster_state[kRasterZMinMaxControl] == 1);
    reject_raster(20, raster_state[kRasterShadowDepthFunc] == 0);
    const bool a4_raster_state = a4_raster_reject_mask == 0;
    uint32_t i8_raster_reject_mask = 0;
    auto reject_i8_raster = [&](size_t bit, bool matches) {
        if (!matches) i8_raster_reject_mask |= 1u << bit;
    };
    reject_i8_raster(0, raster_state[kRasterEnables] == 0x00000006);
    reject_i8_raster(1, raster_state[kRasterControl0] == 0x00100001);
    reject_i8_raster(2, raster_state[kRasterAlphaFunc] == 0x0207);
    reject_i8_raster(3, (raster_state[kRasterAlphaRef] & 0xFFu) == 0);
    reject_i8_raster(4, raster_state[kRasterBlendSrc] == 0x0302);
    reject_i8_raster(5, raster_state[kRasterBlendDst] == 0x0303);
    reject_i8_raster(6, raster_state[kRasterBlendEquation] == 0x8006);
    reject_i8_raster(7, raster_state[kRasterDepthFunc] == 0x0203);
    reject_i8_raster(8, raster_state[kRasterColorMask] == 0x01010101);
    reject_i8_raster(9, raster_state[kRasterDepthMask] == 1);
    reject_i8_raster(10, raster_state[kRasterShadeMode] == 0x1D01);
    reject_i8_raster(11, raster_state[kRasterPolygonOffsetScale] == 0x80000000);
    reject_i8_raster(12, raster_state[kRasterPolygonOffsetBias] == 0x80000000);
    reject_i8_raster(13, raster_state[kRasterFrontPolygonMode] == 0x1B02);
    reject_i8_raster(14, raster_state[kRasterBackPolygonMode] == 0x1B02);
    reject_i8_raster(15, raster_state[kRasterClipMin] == 0);
    reject_i8_raster(16, raster_state[kRasterClipMax] == 0);
    reject_i8_raster(17, raster_state[kRasterCullFace] == 0x0405);
    reject_i8_raster(18, raster_state[kRasterFrontFace] == 0x0900);
    reject_i8_raster(19, raster_state[kRasterZMinMaxControl] == 1);
    reject_i8_raster(20, raster_state[kRasterShadowDepthFunc] == 0);
    const bool i8_raster_state = i8_raster_reject_mask == 0;
    const bool stencil_state =
        raster_state[kRasterStencilFunc] == 0x0207 &&
        raster_state[kRasterStencilRef] == 0 &&
        raster_state[kRasterStencilFuncMask] == 0xFFFFFFFF &&
        raster_state[kRasterStencilOpFail] == 0x1E00 &&
        raster_state[kRasterStencilOpZFail] == 0x1E00 &&
        raster_state[kRasterStencilOpZPass] == 0x1E00;
    const bool a4_combiner_state =
        raster_state[kRasterTextureEnableMask] == 1 &&
        raster_state[kRasterShaderStageProgram] == 1 &&
        raster_state[kRasterShaderOtherStageInput] == 0 &&
        raster_state[kRasterCombinerControl] == 1 &&
        raster_state[kRasterCombinerAlphaIcw0] == 0x18140000 &&
        raster_state[kRasterCombinerColorIcw0] == 0x08040000 &&
        raster_state[kRasterCombinerAlphaOcw0] == 0x000000C0 &&
        raster_state[kRasterCombinerColorOcw0] == 0x000000C0 &&
        raster_state[kRasterCombinerFactor0_0] == 0 &&
        raster_state[kRasterCombinerFactor1_0] == 0 &&
        raster_state[kRasterCombinerSpecularFog0] == 0x010C0002 &&
        raster_state[kRasterCombinerSpecularFog1] == 0x00001C00 &&
        raster_state[kRasterCombinerFinalFactor0] == 0x00DADADA &&
        raster_state[kRasterCombinerFinalFactor1] == 0x00101010;
    const bool a4_texture_state =
        raster_state[kRasterTextureOffset0] == 0x03C0C000 &&
        raster_state[kRasterTextureFormat0] == 0x08810429 &&
        raster_state[kRasterTextureAddress0] == 0x00010303 &&
        raster_state[kRasterTextureFilter0] == 0x02022000 &&
        raster_state[kRasterTextureControl0_0] == 0x4003FFC0 &&
        raster_state[kRasterTextureControl1_0] == 0x01C00000 &&
        raster_state[kRasterTextureImageRect0] == 0x00640011 &&
        raster_state[kRasterTexturePalette0] == 0x02DED080 &&
        raster_state[kRasterTextureBorderColor0] == 0;
    const bool rs12_candidate =
        witnessed_target && vertex_count >= 28 && vertex_count <= 44 &&
        raster_state[kRasterAlphaFunc] == 0x0206 &&
        (raster_state[kRasterAlphaRef] & 0xFFu) == 8 &&
        raster_state[kRasterTextureFormat0] == 0x08810429;
    const bool i8_combiner_state =
        raster_state[kRasterTextureEnableMask] == 1 &&
        raster_state[kRasterShaderStageProgram] == 1 &&
        raster_state[kRasterShaderOtherStageInput] == 0x00210000 &&
        raster_state[kRasterCombinerControl] == 1 &&
        raster_state[kRasterCombinerAlphaIcw0] == 0x18140000 &&
        raster_state[kRasterCombinerColorIcw0] == 0x08040000 &&
        raster_state[kRasterCombinerAlphaOcw0] == 0x00000C00 &&
        raster_state[kRasterCombinerColorOcw0] == 0x00000C00 &&
        raster_state[kRasterCombinerFactor0_0] == 0xFFFFFFFF &&
        raster_state[kRasterCombinerFactor1_0] == 0xFFFFFFFF &&
        raster_state[kRasterCombinerSpecularFog0] == 0x0000000C &&
        raster_state[kRasterCombinerSpecularFog1] == 0x00001C80 &&
        raster_state[kRasterCombinerFinalFactor0] == 0x00DADADA &&
        raster_state[kRasterCombinerFinalFactor1] == 0x00101010;
    const bool i8_texture_state =
        raster_state[kRasterTextureOffset0] == 0x02DED480 &&
        raster_state[kRasterTextureFormat0] == 0x05710B29 &&
        raster_state[kRasterTextureAddress0] == 0x00010303 &&
        raster_state[kRasterTextureFilter0] == 0x02022000 &&
        raster_state[kRasterTextureControl0_0] == 0x4003FFC4 &&
        raster_state[kRasterTextureControl1_0] == 0x01C00000 &&
        raster_state[kRasterTextureImageRect0] == 0x00640011 &&
        raster_state[kRasterTexturePalette0] == 0x02DED080 &&
        raster_state[kRasterTextureBorderColor0] == 0;
    // RS8/RS13 are the format-6 sibling of the I8 pair: the same 128x32
    // swizzle and captured pixel state, but direct little-endian A8R8G8B8
    // texels instead of palette indices.  The trace pins the two target
    // offsets, T0 offset, and every admitted state word below.
    const bool a8_texture_state =
        raster_state[kRasterTextureOffset0] == 0x02DEF080 &&
        raster_state[kRasterTextureFormat0] == 0x05710629 &&
        raster_state[kRasterTextureAddress0] == 0x00010303 &&
        raster_state[kRasterTextureFilter0] == 0x02022000 &&
        raster_state[kRasterTextureControl0_0] == 0x4003FFC4 &&
        raster_state[kRasterTextureControl1_0] == 0x01C00000 &&
        raster_state[kRasterTextureImageRect0] == 0x00640011 &&
        raster_state[kRasterTextureBorderColor0] == 0;
    // RS16/RS17 use the distinct format-0x0b indexed texture: format sizes
    // encode a 64x128 swizzle, with a palette immediately before T0.  The
    // exact nonzero final factors exclude the captured one-off lookalike.
    const bool menu_i8_texture_state =
        raster_state[kRasterTextureOffset0] == 0x039FE800 &&
        raster_state[kRasterTextureFormat0] == 0x07610B29 &&
        raster_state[kRasterTextureAddress0] == 0x00010303 &&
        raster_state[kRasterTextureFilter0] == 0x02022000 &&
        raster_state[kRasterTextureControl0_0] == 0x4003FFC4 &&
        raster_state[kRasterTextureControl1_0] == 0x01C00000 &&
        raster_state[kRasterTextureImageRect0] == 0x00640011 &&
        raster_state[kRasterTexturePalette0] == 0x039FE400 &&
        raster_state[kRasterTextureBorderColor0] == 0;
    // IQRS3/IQRS7 are an independently witnessed upper-left artwork layer:
    // a 512x128 indexed texture immediately following its A8R8G8B8 palette.
    const bool artwork_i8_texture_state =
        raster_state[kRasterTextureOffset0] == 0x02DF4480 &&
        raster_state[kRasterTextureFormat0] == 0x07910B29 &&
        raster_state[kRasterTextureAddress0] == 0x00010303 &&
        raster_state[kRasterTextureFilter0] == 0x02022000 &&
        raster_state[kRasterTextureControl0_0] == 0x4003FFC4 &&
        raster_state[kRasterTextureControl1_0] == 0x01C00000 &&
        raster_state[kRasterTextureImageRect0] == 0x00640011 &&
        raster_state[kRasterTexturePalette0] == 0x02DF4080 &&
        raster_state[kRasterTextureBorderColor0] == 0;
    // IQRS5/IQRS10 are the direct-color 512x512 main-panel pair. Palette0 is
    // intentionally passive for format 6, just as for RS8/RS13.
    const bool main_panel_texture_state =
        raster_state[kRasterTextureOffset0] == 0x034FC080 &&
        raster_state[kRasterTextureFormat0] == 0x09910629 &&
        raster_state[kRasterTextureAddress0] == 0x00010303 &&
        raster_state[kRasterTextureFilter0] == 0x02022000 &&
        raster_state[kRasterTextureControl0_0] == 0x4003FFC4 &&
        raster_state[kRasterTextureControl1_0] == 0x01C00000 &&
        raster_state[kRasterTextureImageRect0] == 0x00640011 &&
        raster_state[kRasterTextureBorderColor0] == 0;
    // IQRS4/IQRS7 are the 1024x1024 direct-color full-screen pair. Palette0
    // is stale/passive for format 6; nonzero final factors exclude IQRS12.
    const bool fullscreen_texture_state =
        raster_state[kRasterTextureOffset0] == 0x035FD080 &&
        raster_state[kRasterTextureFormat0] == 0x0AA10629 &&
        raster_state[kRasterTextureAddress0] == 0x00010303 &&
        raster_state[kRasterTextureFilter0] == 0x02022000 &&
        raster_state[kRasterTextureControl0_0] == 0x4003FFC4 &&
        raster_state[kRasterTextureControl1_0] == 0x01C00000 &&
        raster_state[kRasterTextureImageRect0] == 0x00640011 &&
        raster_state[kRasterTextureBorderColor0] == 0;
    uint32_t reject_mask = 0;
    if (!a4_source_shape) reject_mask |= 1u << 0;
    if (!transform_state) reject_mask |= 1u << 1;
    if (!surface_state) reject_mask |= 1u << 2;
    if (!a4_raster_state) reject_mask |= 1u << 3;
    if (!stencil_state) reject_mask |= 1u << 4;
    if (!a4_combiner_state) reject_mask |= 1u << 5;
    if (!a4_texture_state) reject_mask |= 1u << 6;
    if (rs12_candidate) {
        ++immediate_quad_rs12_candidates_;
        last_immediate_quad_rs12_reject_mask_ = reject_mask;
        last_immediate_quad_rs12_raster_reject_mask_ = a4_raster_reject_mask;
    }
    uint32_t i8_reject_mask = 0;
    if (!i8_source_shape) i8_reject_mask |= 1u << 0;
    if (!transform_state) i8_reject_mask |= 1u << 1;
    if (!surface_state) i8_reject_mask |= 1u << 2;
    if (!i8_raster_state) i8_reject_mask |= 1u << 3;
    if (!stencil_state) i8_reject_mask |= 1u << 4;
    if (!i8_combiner_state) i8_reject_mask |= 1u << 5;
    if (!i8_texture_state) i8_reject_mask |= 1u << 6;
    const bool i8_candidate =
        immediate_source && witnessed_target && vertex_count == 4 &&
        raster_state[kRasterAlphaFunc] == 0x0207 &&
        (raster_state[kRasterAlphaRef] & 0xFFu) == 0 &&
        raster_state[kRasterTextureFormat0] == 0x05710B29;
    if (i8_candidate) {
        ++immediate_quad_i8_candidates_;
        last_immediate_quad_i8_reject_mask_ = i8_reject_mask;
        last_immediate_quad_i8_raster_reject_mask_ = i8_raster_reject_mask;
    }
    uint32_t a8_reject_mask = 0;
    if (!a8_source_shape) a8_reject_mask |= 1u << 0;
    if (!transform_state) a8_reject_mask |= 1u << 1;
    if (!surface_state) a8_reject_mask |= 1u << 2;
    if (!i8_raster_state) a8_reject_mask |= 1u << 3;
    if (!stencil_state) a8_reject_mask |= 1u << 4;
    if (!i8_combiner_state) a8_reject_mask |= 1u << 5;
    if (!a8_texture_state) a8_reject_mask |= 1u << 6;
    const bool a8_candidate =
        immediate_source && witnessed_target && vertex_count == 4 &&
        raster_state[kRasterAlphaFunc] == 0x0207 &&
        (raster_state[kRasterAlphaRef] & 0xFFu) == 0 &&
        raster_state[kRasterTextureFormat0] == 0x05710629;
    if (a8_candidate) {
        ++immediate_quad_a8_candidates_;
        last_immediate_quad_a8_reject_mask_ = a8_reject_mask;
        last_immediate_quad_a8_raster_reject_mask_ = i8_raster_reject_mask;
    }
    uint32_t menu_i8_reject_mask = 0;
    if (!menu_i8_source_shape) menu_i8_reject_mask |= 1u << 0;
    if (!transform_state) menu_i8_reject_mask |= 1u << 1;
    if (!menu_i8_surface_state) menu_i8_reject_mask |= 1u << 2;
    if (!i8_raster_state) menu_i8_reject_mask |= 1u << 3;
    if (!stencil_state) menu_i8_reject_mask |= 1u << 4;
    if (!i8_combiner_state) menu_i8_reject_mask |= 1u << 5;
    if (!menu_i8_texture_state) menu_i8_reject_mask |= 1u << 6;
    if (raster_state[kRasterCombinerFinalFactor0] != 0x00DADADA ||
        raster_state[kRasterCombinerFinalFactor1] != 0x00101010)
        menu_i8_reject_mask |= 1u << 7;
    const bool menu_i8_candidate =
        immediate_source && witnessed_target && vertex_count == 4 &&
        raster_state[kRasterAlphaFunc] == 0x0207 &&
        (raster_state[kRasterAlphaRef] & 0xFFu) == 0 &&
        raster_state[kRasterTextureFormat0] == 0x07610B29 &&
        raster_state[kRasterCombinerFinalFactor0] == 0x00DADADA &&
        raster_state[kRasterCombinerFinalFactor1] == 0x00101010;
    if (menu_i8_candidate) {
        ++immediate_quad_menu_i8_candidates_;
        last_immediate_quad_menu_i8_reject_mask_ = menu_i8_reject_mask;
        last_immediate_quad_menu_i8_raster_reject_mask_ = i8_raster_reject_mask;
    }
    uint32_t artwork_i8_reject_mask = 0;
    if (!artwork_i8_source_shape) artwork_i8_reject_mask |= 1u << 0;
    if (!transform_state) artwork_i8_reject_mask |= 1u << 1;
    if (!menu_i8_surface_state) artwork_i8_reject_mask |= 1u << 2;
    if (!i8_raster_state) artwork_i8_reject_mask |= 1u << 3;
    if (!stencil_state) artwork_i8_reject_mask |= 1u << 4;
    if (!i8_combiner_state) artwork_i8_reject_mask |= 1u << 5;
    if (!artwork_i8_texture_state) artwork_i8_reject_mask |= 1u << 6;
    if (raster_state[kRasterCombinerFinalFactor0] != 0x00DADADA ||
        raster_state[kRasterCombinerFinalFactor1] != 0x00101010)
        artwork_i8_reject_mask |= 1u << 7;
    const bool artwork_i8_candidate =
        immediate_source && witnessed_target && vertex_count == 4 &&
        raster_state[kRasterAlphaFunc] == 0x0207 &&
        (raster_state[kRasterAlphaRef] & 0xFFu) == 0 &&
        raster_state[kRasterTextureOffset0] == 0x02DF4480 &&
        raster_state[kRasterTextureFormat0] == 0x07910B29 &&
        raster_state[kRasterCombinerFinalFactor0] == 0x00DADADA &&
        raster_state[kRasterCombinerFinalFactor1] == 0x00101010;
    if (artwork_i8_candidate) {
        ++immediate_quad_artwork_i8_candidates_;
        last_immediate_quad_artwork_i8_reject_mask_ = artwork_i8_reject_mask;
        last_immediate_quad_artwork_i8_raster_reject_mask_ = i8_raster_reject_mask;
    }
    uint32_t main_panel_reject_mask = 0;
    if (!main_panel_source_shape) main_panel_reject_mask |= 1u << 0;
    if (!transform_state) main_panel_reject_mask |= 1u << 1;
    if (!menu_i8_surface_state) main_panel_reject_mask |= 1u << 2;
    if (!i8_raster_state) main_panel_reject_mask |= 1u << 3;
    if (!stencil_state) main_panel_reject_mask |= 1u << 4;
    if (!i8_combiner_state) main_panel_reject_mask |= 1u << 5;
    if (!main_panel_texture_state) main_panel_reject_mask |= 1u << 6;
    if (raster_state[kRasterCombinerFinalFactor0] != 0x00DADADA ||
        raster_state[kRasterCombinerFinalFactor1] != 0x00101010)
        main_panel_reject_mask |= 1u << 7;
    const bool main_panel_candidate =
        immediate_source && witnessed_target && vertex_count == 4 &&
        raster_state[kRasterAlphaFunc] == 0x0207 &&
        (raster_state[kRasterAlphaRef] & 0xFFu) == 0 &&
        raster_state[kRasterTextureOffset0] == 0x034FC080 &&
        raster_state[kRasterTextureFormat0] == 0x09910629 &&
        raster_state[kRasterCombinerFinalFactor0] == 0x00DADADA &&
        raster_state[kRasterCombinerFinalFactor1] == 0x00101010;
    if (main_panel_candidate) {
        ++immediate_quad_main_panel_candidates_;
        last_immediate_quad_main_panel_reject_mask_ = main_panel_reject_mask;
        last_immediate_quad_main_panel_raster_reject_mask_ = i8_raster_reject_mask;
    }
    uint32_t fullscreen_reject_mask = 0;
    if (!fullscreen_source_shape) fullscreen_reject_mask |= 1u << 0;
    if (!transform_state) fullscreen_reject_mask |= 1u << 1;
    if (!menu_i8_surface_state) fullscreen_reject_mask |= 1u << 2;
    if (!i8_raster_state) fullscreen_reject_mask |= 1u << 3;
    if (!stencil_state) fullscreen_reject_mask |= 1u << 4;
    if (!i8_combiner_state) fullscreen_reject_mask |= 1u << 5;
    if (!fullscreen_texture_state) fullscreen_reject_mask |= 1u << 6;
    if (raster_state[kRasterCombinerFinalFactor0] != 0x00DADADA ||
        raster_state[kRasterCombinerFinalFactor1] != 0x00101010)
        fullscreen_reject_mask |= 1u << 7;
    const bool fullscreen_candidate =
        immediate_source && witnessed_target && vertex_count == 4 &&
        raster_state[kRasterAlphaFunc] == 0x0207 &&
        (raster_state[kRasterAlphaRef] & 0xFFu) == 0 &&
        raster_state[kRasterTextureOffset0] == 0x035FD080 &&
        raster_state[kRasterTextureFormat0] == 0x0AA10629 &&
        raster_state[kRasterCombinerFinalFactor0] == 0x00DADADA &&
        raster_state[kRasterCombinerFinalFactor1] == 0x00101010;
    if (fullscreen_candidate) {
        ++immediate_quad_fullscreen_candidates_;
        last_immediate_quad_fullscreen_reject_mask_ = fullscreen_reject_mask;
        last_immediate_quad_fullscreen_raster_reject_mask_ = i8_raster_reject_mask;
    }
    const bool i8_profile = i8_reject_mask == 0;
    const bool a8_profile = a8_reject_mask == 0;
    const bool menu_i8_profile = menu_i8_reject_mask == 0;
    const bool artwork_i8_profile = artwork_i8_reject_mask == 0;
    const bool main_panel_profile = main_panel_reject_mask == 0;
    const bool fullscreen_profile = fullscreen_reject_mask == 0;
    if (reject_mask && !i8_profile && !a8_profile && !menu_i8_profile &&
        !artwork_i8_profile && !main_panel_profile && !fullscreen_profile) return true;

    if (!color_dma_bound_ || !texture_dma_a_bound_) {
        fault_("NV2A immediate-quad DMA objects are not witnessed");
        return false;
    }

    constexpr uint32_t kSurfacePitch = 0x0C00;
    constexpr uint32_t kSurfaceHeight = 480;
    constexpr uint32_t kA4TextureWidth = 256;
    constexpr uint32_t kA4TextureHeight = 256;
    constexpr uint32_t kI8TextureWidth = 128;
    constexpr uint32_t kI8TextureHeight = 32;
    constexpr uint32_t kMenuI8TextureWidth = 64;
    constexpr uint32_t kMenuI8TextureHeight = 128;
    constexpr uint32_t kArtworkI8TextureWidth = 512;
    constexpr uint32_t kArtworkI8TextureHeight = 128;
    constexpr uint32_t kMainPanelTextureWidth = 512;
    constexpr uint32_t kMainPanelTextureHeight = 512;
    constexpr uint32_t kFullscreenTextureWidth = 1024;
    constexpr uint32_t kFullscreenTextureHeight = 1024;
    const uint32_t texture_width =
        fullscreen_profile ? kFullscreenTextureWidth :
        (main_panel_profile ? kMainPanelTextureWidth :
        (artwork_i8_profile ? kArtworkI8TextureWidth :
        (menu_i8_profile ? kMenuI8TextureWidth :
         ((i8_profile || a8_profile) ? kI8TextureWidth : kA4TextureWidth))));
    const uint32_t texture_height =
        fullscreen_profile ? kFullscreenTextureHeight :
        (main_panel_profile ? kMainPanelTextureHeight :
        (artwork_i8_profile ? kArtworkI8TextureHeight :
        (menu_i8_profile ? kMenuI8TextureHeight :
         ((i8_profile || a8_profile) ? kI8TextureHeight : kA4TextureHeight))));
    constexpr uint64_t kColorBytes =
        static_cast<uint64_t>(kSurfacePitch) * kSurfaceHeight;
    const uint64_t texture_bytes = (i8_profile || menu_i8_profile || artwork_i8_profile)
        ? static_cast<uint64_t>(texture_width) * texture_height
        : static_cast<uint64_t>(texture_width) * texture_height *
              ((a8_profile || main_panel_profile || fullscreen_profile) ? sizeof(uint32_t) : sizeof(uint16_t));

    auto resolve_range = [&](uint32_t dma, uint32_t offset, uint64_t bytes,
                             uint32_t& address, const char* name) {
        if (!bytes || static_cast<uint64_t>(offset) + bytes - 1 > UINT32_MAX) {
            char why[128];
            std::snprintf(why, sizeof(why),
                          "NV2A immediate-quad %s range overflows", name);
            fault_(why);
            return false;
        }
        const uint32_t last_offset =
            offset + static_cast<uint32_t>(bytes - 1);
        uint32_t last_address = 0;
        if (!dma_object_address(dma, offset, address) ||
            !dma_object_address(dma, last_offset, last_address) ||
            last_address < address ||
            static_cast<uint64_t>(last_address) - address + 1 != bytes) {
            char why[128];
            std::snprintf(why, sizeof(why),
                          "NV2A immediate-quad %s DMA range is invalid", name);
            fault_(why);
            return false;
        }
        return true;
    };

    uint32_t color_address = 0;
    uint32_t texture_address = 0;
    const char* texture_name = "A4R4G4B4 texture";
    if (i8_profile) texture_name = "I8 texture";
    else if (menu_i8_profile) texture_name = "menu I8 texture";
    else if (artwork_i8_profile) texture_name = "artwork I8 texture";
    else if (main_panel_profile) texture_name = "main-panel A8R8G8B8 texture";
    else if (fullscreen_profile) texture_name = "full-screen A8R8G8B8 texture";
    else if (a8_profile) texture_name = "A8R8G8B8 texture";
    if (!resolve_range(color_dma_, color_offset, kColorBytes,
                       color_address, "color") ||
        !resolve_range(texture_dma_a_,
                       raster_state[kRasterTextureOffset0], texture_bytes,
                       texture_address, texture_name)) {
        return false;
    }

    uint32_t palette_address = 0;
    if ((i8_profile || menu_i8_profile || artwork_i8_profile) &&
        !resolve_range(texture_dma_a_, raster_state[kRasterTexturePalette0] & ~0x3Fu,
                       256u * sizeof(uint32_t), palette_address, "I8 palette")) {
        return false;
    }
    if ((i8_profile || menu_i8_profile || artwork_i8_profile) &&
        (palette_address > UINT32_MAX - 256u * sizeof(uint32_t) ||
         texture_address != palette_address + 256u * sizeof(uint32_t))) {
        fault_("NV2A immediate-quad I8 palette and texture are not contiguous");
        return false;
    }
    // The direct A8 path is intentionally not a feedback implementation.
    // Resolve the complete canonical DMA spans before constructing cached
    // surfaces: an aliased texture would otherwise observe undefined ordering
    // between their independent page caches. Keep this submission recorder-only
    // without a partial draw.
    if (fullscreen_profile &&
        static_cast<uint64_t>(texture_address) <
            static_cast<uint64_t>(color_address) + kColorBytes &&
        static_cast<uint64_t>(color_address) <
            static_cast<uint64_t>(texture_address) + texture_bytes) {
        last_immediate_quad_fullscreen_raster_reject_mask_ |= 1u << 21;
        return true;
    }

    // Stage (d): the immediate-quad family builds its surfaces WITHOUT
    // mem_direct_, so under DESIGN-NV2A-SEAM.md section 1.3 it is inline-only --
    // its destination writes and its per-fragment palette reads both go through
    // uc_mem_read/uc_mem_write. Converting it to the direct path is its own bead
    // (design section 3.2); until then this is a permanent no_direct_ptr.
    seam_note_threadable(job, false, kSeamNoDirectPointer);
    // Seam anchor 6 (F6): the quad's texture source against pending destinations.
    seam_texture_source_anchor(texture_address, texture_bytes);
    // Stage (f) residency census: this family never queries renderer residency
    // (it is the one witnessed path with no resolve_renderer_* call), so it is
    // hooked directly. It is a full-screen CPU blit family and therefore one of
    // the consumers that can force a GPU-authoritative surface back to guest RAM.
    ++gpu_res_.cpu_draws;
    gpu_res_cpu_color_access(color_address, kColorBytes,
                             kGpuResCauseImmediateQuad);
    // This family is the one witnessed path with no renderer residency query at
    // all: it reads its texture and palette and writes its destination straight
    // through uc_mem_read/uc_mem_write. That was invisible while nothing it
    // touched was ever GPU-resident; with the stage (f) executor it is a
    // coherence hole in both directions, so both are closed here.
    if (!resolve_renderer_color_overlap(color_address,
                                        static_cast<size_t>(kColorBytes),
                                        "immediate-quad destination") ||
        !resolve_renderer_color_overlap(texture_address,
                                        static_cast<size_t>(texture_bytes),
                                        "immediate-quad texture")) {
        return false;
    }
    PagedRasterSurface color_surface(
        color_address, static_cast<size_t>(kColorBytes), mem_read_, mem_write_);
    PagedRasterSurface texture_surface(
        texture_address, static_cast<size_t>(texture_bytes), mem_read_, mem_write_);
    std::unique_ptr<PagedRasterSurface> palette_surface;
    if (i8_profile || menu_i8_profile || artwork_i8_profile) {
        palette_surface = std::make_unique<PagedRasterSurface>(
            palette_address, 256u * sizeof(uint32_t), mem_read_, mem_write_);
    }
    SwizzleLookup texture_swizzle(texture_width, texture_height);

    const uint32_t clip_x = surface_clip_horizontal & 0xFFFFu;
    const uint32_t clip_width = surface_clip_horizontal >> 16;
    const uint32_t clip_y = surface_clip_vertical & 0xFFFFu;
    const uint32_t clip_height = surface_clip_vertical >> 16;
    const uint32_t xmin = clip_x;
    const uint32_t xmax = clip_x + clip_width - 1;
    const uint32_t ymin = clip_y;
    const uint32_t ymax = clip_y + clip_height - 1;

    auto sample_texture =
        [&](float u, float v, std::array<float, 4>& color) {
            if (!std::isfinite(u) || !std::isfinite(v)) return false;
            u = std::clamp(u, 0.0f, 1.0f);
            v = std::clamp(v, 0.0f, 1.0f);
            const float texel_x = u * texture_width - 0.5f;
            const float texel_y = v * texture_height - 0.5f;
            const int32_t x0 = static_cast<int32_t>(std::floor(texel_x));
            const int32_t y0 = static_cast<int32_t>(std::floor(texel_y));
            const float fx = texel_x - static_cast<float>(x0);
            const float fy = texel_y - static_cast<float>(y0);
            auto texel = [&](int32_t x, int32_t y,
                             std::array<float, 4>& unpacked) {
                const uint32_t sx = static_cast<uint32_t>(std::clamp(
                    x, 0, static_cast<int32_t>(texture_width) - 1));
                const uint32_t sy = static_cast<uint32_t>(std::clamp(
                    y, 0, static_cast<int32_t>(texture_height) - 1));
                const size_t swizzled = texture_swizzle.index(sx, sy);
                if (i8_profile || menu_i8_profile || artwork_i8_profile) {
                    uint8_t index = 0;
                    uint32_t pixel = 0;
                    if (!texture_surface.read8(swizzled, index) ||
                        !palette_surface->read32(
                            static_cast<size_t>(index) * sizeof(pixel), pixel)) {
                        return false;
                    }
                    unpacked = {{
                        static_cast<float>((pixel >> 16) & 0xFFu) / 255.0f,
                        static_cast<float>((pixel >> 8) & 0xFFu) / 255.0f,
                        static_cast<float>(pixel & 0xFFu) / 255.0f,
                        static_cast<float>((pixel >> 24) & 0xFFu) / 255.0f,
                    }};
                } else if (a8_profile || main_panel_profile || fullscreen_profile) {
                    uint32_t pixel = 0;
                    if (!texture_surface.read32(
                            swizzled * sizeof(pixel), pixel)) {
                        return false;
                    }
                    // xemu maps NV097 format 6 to B8G8R8A8 host storage;
                    // a little-endian guest word therefore remains AARRGGBB.
                    unpacked = {{
                        static_cast<float>((pixel >> 16) & 0xFFu) / 255.0f,
                        static_cast<float>((pixel >> 8) & 0xFFu) / 255.0f,
                        static_cast<float>(pixel & 0xFFu) / 255.0f,
                        static_cast<float>((pixel >> 24) & 0xFFu) / 255.0f,
                    }};
                } else {
                    uint16_t pixel = 0;
                    if (!texture_surface.read16(swizzled * sizeof(pixel), pixel))
                        return false;
                    unpacked = {{
                        static_cast<float>((pixel >> 8) & 0xFu) / 15.0f,
                        static_cast<float>((pixel >> 4) & 0xFu) / 15.0f,
                        static_cast<float>(pixel & 0xFu) / 15.0f,
                        static_cast<float>((pixel >> 12) & 0xFu) / 15.0f,
                    }};
                }
                return true;
            };
            std::array<float, 4> c00{}, c10{}, c01{}, c11{};
            if (!texel(x0, y0, c00) || !texel(x0 + 1, y0, c10) ||
                !texel(x0, y0 + 1, c01) ||
                !texel(x0 + 1, y0 + 1, c11)) {
                return false;
            }
            for (size_t component = 0; component < color.size(); ++component) {
                const float top =
                    c00[component] + (c10[component] - c00[component]) * fx;
                const float bottom =
                    c01[component] + (c11[component] - c01[component]) * fx;
                color[component] = top + (bottom - top) * fy;
            }
            return true;
        };

    bool failed = false;
    auto rasterize_triangle =
        [&](const TransformedVertex& a, const TransformedVertex& b,
            const TransformedVertex& c) {
            if (failed) return;
            for (const TransformedVertex* vertex : {&a, &b, &c}) {
                if (!std::isfinite(vertex->position[0]) ||
                    !std::isfinite(vertex->position[1]) ||
                    !std::isfinite(vertex->position[3])) {
                    return;
                }
            }
            auto fixed_xy = [](float value) {
                return static_cast<int64_t>(std::llround(value * 16.0f));
            };
            const int64_t ax = fixed_xy(a.position[0]);
            const int64_t ay = fixed_xy(a.position[1]);
            const int64_t bx = fixed_xy(b.position[0]);
            const int64_t by = fixed_xy(b.position[1]);
            const int64_t cx = fixed_xy(c.position[0]);
            const int64_t cy = fixed_xy(c.position[1]);
            auto edge = [](int64_t x0, int64_t y0, int64_t x1, int64_t y1,
                           int64_t px, int64_t py) {
                return (x1 - x0) * (py - y0) -
                       (y1 - y0) * (px - x0);
            };
            const int64_t area = edge(ax, ay, bx, by, cx, cy);
            if (area <= 0) return;  // BACK culling with the witnessed CW front face.
            if (i8_profile)
                ++immediate_quad_i8_raster_triangles_;
            else if (menu_i8_profile)
                ++immediate_quad_menu_i8_raster_triangles_;
            else if (artwork_i8_profile)
                ++immediate_quad_artwork_i8_raster_triangles_;
            else if (main_panel_profile)
                ++immediate_quad_main_panel_raster_triangles_;
            else if (fullscreen_profile)
                ++immediate_quad_fullscreen_raster_triangles_;
            else if (a8_profile)
                ++immediate_quad_a8_raster_triangles_;
            else
                ++immediate_quad_raster_triangles_;

            const int32_t tri_xmin = std::max<int32_t>(
                static_cast<int32_t>(xmin),
                static_cast<int32_t>(std::ceil(
                    (static_cast<double>(std::min({ax, bx, cx})) - 8.0) /
                    16.0)));
            const int32_t tri_xmax = std::min<int32_t>(
                static_cast<int32_t>(xmax),
                static_cast<int32_t>(std::floor(
                    (static_cast<double>(std::max({ax, bx, cx})) - 8.0) /
                    16.0)));
            const int32_t tri_ymin = std::max<int32_t>(
                static_cast<int32_t>(ymin),
                static_cast<int32_t>(std::ceil(
                    (static_cast<double>(std::min({ay, by, cy})) - 8.0) /
                    16.0)));
            const int32_t tri_ymax = std::min<int32_t>(
                static_cast<int32_t>(ymax),
                static_cast<int32_t>(std::floor(
                    (static_cast<double>(std::max({ay, by, cy})) - 8.0) /
                    16.0)));
            if (tri_xmax < tri_xmin || tri_ymax < tri_ymin) return;

            const auto top_left = [](int64_t x0, int64_t y0,
                                     int64_t x1, int64_t y1) {
                const int64_t dx = x1 - x0;
                const int64_t dy = y1 - y0;
                return dy < 0 || (dy == 0 && dx > 0);
            };
            const bool edge0_top_left = top_left(bx, by, cx, cy);
            const bool edge1_top_left = top_left(cx, cy, ax, ay);
            const bool edge2_top_left = top_left(ax, ay, bx, by);

            for (int32_t y = tri_ymin; y <= tri_ymax; ++y) {
                const int64_t py = static_cast<int64_t>(y) * 16 + 8;
                for (int32_t x = tri_xmin; x <= tri_xmax; ++x) {
                    const int64_t px = static_cast<int64_t>(x) * 16 + 8;
                    const int64_t w0 = edge(bx, by, cx, cy, px, py);
                    const int64_t w1 = edge(cx, cy, ax, ay, px, py);
                    const int64_t w2 = edge(ax, ay, bx, by, px, py);
                    if (w0 < 0 || (w0 == 0 && !edge0_top_left) ||
                        w1 < 0 || (w1 == 0 && !edge1_top_left) ||
                        w2 < 0 || (w2 == 0 && !edge2_top_left)) {
                        continue;
                    }
                    if (i8_profile)
                        ++immediate_quad_i8_raster_fragments_;
                    else if (menu_i8_profile)
                        ++immediate_quad_menu_i8_raster_fragments_;
                    else if (artwork_i8_profile)
                        ++immediate_quad_artwork_i8_raster_fragments_;
                    else if (main_panel_profile)
                        ++immediate_quad_main_panel_raster_fragments_;
                    else if (fullscreen_profile)
                        ++immediate_quad_fullscreen_raster_fragments_;
                    else if (a8_profile)
                        ++immediate_quad_a8_raster_fragments_;
                    else
                        ++immediate_quad_raster_fragments_;

                    const double lambda0 =
                        static_cast<double>(w0) / static_cast<double>(area);
                    const double lambda1 =
                        static_cast<double>(w1) / static_cast<double>(area);
                    const double lambda2 =
                        static_cast<double>(w2) / static_cast<double>(area);
                    const double q0 = lambda0 / a.position[3];
                    const double q1 = lambda1 / b.position[3];
                    const double q2 = lambda2 / c.position[3];
                    const double qsum = q0 + q1 + q2;
                    if (!std::isfinite(qsum) || qsum == 0.0) continue;
                    const double u =
                        (q0 * a.texture[0][0] + q1 * b.texture[0][0] +
                         q2 * c.texture[0][0]) /
                        qsum;
                    const double v =
                        (q0 * a.texture[0][1] + q1 * b.texture[0][1] +
                         q2 * c.texture[0][1]) /
                        qsum;
                    if (!std::isfinite(u) || !std::isfinite(v)) {
                        fault_("NV2A immediate-quad texture varying is non-finite");
                        failed = true;
                        return;
                    }
                    std::array<float, 4> source{};
                    if (!sample_texture(static_cast<float>(u),
                                        static_cast<float>(v), source)) {
                        fault_(i8_profile ? "NV2A immediate-quad I8 palette sample failed"
                               : (menu_i8_profile ? "NV2A immediate-quad menu I8 palette sample failed"
                               : (artwork_i8_profile ? "NV2A immediate-quad artwork I8 palette sample failed"
                               : (main_panel_profile ? "NV2A immediate-quad main-panel A8R8G8B8 sample failed"
                               : (fullscreen_profile ? "NV2A immediate-quad full-screen A8R8G8B8 sample failed"
                               : (a8_profile ? "NV2A immediate-quad A8R8G8B8 sample failed"
                                             : "NV2A immediate-quad A4R4G4B4 sample failed"))))));
                        failed = true;
                        return;
                    }
                    if (i8_profile)
                        ++immediate_quad_i8_texture_samples_;
                    else if (menu_i8_profile)
                        ++immediate_quad_menu_i8_texture_samples_;
                    else if (artwork_i8_profile)
                        ++immediate_quad_artwork_i8_texture_samples_;
                    else if (main_panel_profile)
                        ++immediate_quad_main_panel_texture_samples_;
                    else if (fullscreen_profile)
                        ++immediate_quad_fullscreen_texture_samples_;
                    else if (a8_profile)
                        ++immediate_quad_a8_texture_samples_;
                    else
                        ++immediate_quad_texture_samples_;
                    // RS0/RS3 have TEXCTL0.ALPHAKILLEN set. It operates on
                    // the filtered palette sample, before the C0/final combiner
                    // and before blending.
                    if ((i8_profile || menu_i8_profile || artwork_i8_profile || main_panel_profile || fullscreen_profile || a8_profile) &&
                        source[3] == 0.0f)
                        continue;
                    // C0 is T0 * V0 into R0 for both RGB and alpha; the final
                    // combiner selects R0. The captured dashboard shader emits
                    // white V0, but retain the actual combiner equation here.
                    for (size_t component = 0; component < source.size();
                         ++component) {
                        const double diffuse =
                            (q0 * a.diffuse[component] +
                             q1 * b.diffuse[component] +
                             q2 * c.diffuse[component]) /
                            qsum;
                        if (!std::isfinite(diffuse)) {
                            fault_("NV2A immediate-quad diffuse varying is non-finite");
                            failed = true;
                            return;
                        }
                        source[component] = std::clamp(
                            source[component] *
                                std::max(static_cast<float>(diffuse), 0.0f),
                            0.0f, 1.0f);
                    }
                    if (!i8_profile && !menu_i8_profile && !artwork_i8_profile && !main_panel_profile && !fullscreen_profile && !a8_profile) {
                        // A4 final RGB is C1 + mix(0, R0, C0): the captured
                        // final constants are 16/255 and 218/255. Alpha selects R0.a.
                        for (size_t component = 0; component < 3; ++component) {
                            source[component] = std::clamp(
                                (16.0f + 218.0f * source[component]) / 255.0f,
                                0.0f, 1.0f);
                        }
                    }
                    auto pack_unorm8 = [](float value) {
                        return static_cast<uint32_t>(std::floor(
                            std::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f));
                    };
                    const uint32_t source_alpha8 = pack_unorm8(source[3]);
                    if (!i8_profile && !menu_i8_profile && !artwork_i8_profile && !main_panel_profile && !fullscreen_profile && !a8_profile &&
                        source_alpha8 < 8)
                        continue;  // A4 GEQUAL, reference 8.
                    if (i8_profile)
                        ++immediate_quad_i8_raster_alpha_passes_;
                    else if (menu_i8_profile)
                        ++immediate_quad_menu_i8_raster_alpha_passes_;
                    else if (artwork_i8_profile)
                        ++immediate_quad_artwork_i8_raster_alpha_passes_;
                    else if (main_panel_profile)
                        ++immediate_quad_main_panel_raster_alpha_passes_;
                    else if (fullscreen_profile)
                        ++immediate_quad_fullscreen_raster_alpha_passes_;
                    else if (a8_profile)
                        ++immediate_quad_a8_raster_alpha_passes_;
                    else
                        ++immediate_quad_raster_alpha_passes_;

                    const size_t offset =
                        static_cast<size_t>(y) * kSurfacePitch +
                        static_cast<size_t>(x) * sizeof(uint32_t);
                    uint32_t destination_pixel = 0;
                    if (!color_surface.read32(offset, destination_pixel)) {
                        fault_("NV2A immediate-quad color DMA read failed");
                        failed = true;
                        return;
                    }
                    const std::array<float, 4> destination{{
                        static_cast<float>((destination_pixel >> 16) & 0xFFu) /
                            255.0f,
                        static_cast<float>((destination_pixel >> 8) & 0xFFu) /
                            255.0f,
                        static_cast<float>(destination_pixel & 0xFFu) / 255.0f,
                        static_cast<float>((destination_pixel >> 24) & 0xFFu) /
                            255.0f,
                    }};
                    const float inverse_alpha = 1.0f - source[3];
                    std::array<uint32_t, 4> blended{};
                    for (size_t component = 0; component < blended.size();
                         ++component) {
                        blended[component] = pack_unorm8(
                            source[component] * source[3] +
                            destination[component] * inverse_alpha);
                    }
                    const uint32_t color =
                        (blended[3] << 24) | (blended[0] << 16) |
                        (blended[1] << 8) | blended[2];
                    if (!color_surface.write32(offset, color)) {
                        fault_("NV2A immediate-quad color cache write failed");
                        failed = true;
                        return;
                    }
                    if (i8_profile)
                        ++immediate_quad_i8_raster_writes_;
                    else if (menu_i8_profile)
                        ++immediate_quad_menu_i8_raster_writes_;
                    else if (artwork_i8_profile)
                        ++immediate_quad_artwork_i8_raster_writes_;
                    else if (main_panel_profile)
                        ++immediate_quad_main_panel_raster_writes_;
                    else if (fullscreen_profile)
                        ++immediate_quad_fullscreen_raster_writes_;
                    else if (a8_profile)
                        ++immediate_quad_a8_raster_writes_;
                    else
                        ++immediate_quad_raster_writes_;
                    const uint32_t address =
                        color_address + static_cast<uint32_t>(offset);
                    if (i8_profile)
                        last_immediate_quad_i8_raster_address_ = address;
                    else if (menu_i8_profile)
                        last_immediate_quad_menu_i8_raster_address_ = address;
                    else if (artwork_i8_profile)
                        last_immediate_quad_artwork_i8_raster_address_ = address;
                    else if (main_panel_profile)
                        last_immediate_quad_main_panel_raster_address_ = address;
                    else if (fullscreen_profile)
                        last_immediate_quad_fullscreen_raster_address_ = address;
                    else if (a8_profile)
                        last_immediate_quad_a8_raster_address_ = address;
                    else
                        last_immediate_quad_raster_address_ = address;
                    for (uint32_t word : {address, color}) {
                        if (i8_profile) {
                            immediate_quad_i8_raster_output_hash_ ^= word;
                            immediate_quad_i8_raster_output_hash_ *= 1099511628211ull;
                        } else if (menu_i8_profile) {
                            immediate_quad_menu_i8_raster_output_hash_ ^= word;
                            immediate_quad_menu_i8_raster_output_hash_ *= 1099511628211ull;
                        } else if (artwork_i8_profile) {
                            immediate_quad_artwork_i8_raster_output_hash_ ^= word;
                            immediate_quad_artwork_i8_raster_output_hash_ *= 1099511628211ull;
                        } else if (main_panel_profile) {
                            immediate_quad_main_panel_raster_output_hash_ ^= word;
                            immediate_quad_main_panel_raster_output_hash_ *= 1099511628211ull;
                        } else if (fullscreen_profile) {
                            immediate_quad_fullscreen_raster_output_hash_ ^= word;
                            immediate_quad_fullscreen_raster_output_hash_ *= 1099511628211ull;
                        } else if (a8_profile) {
                            immediate_quad_a8_raster_output_hash_ ^= word;
                            immediate_quad_a8_raster_output_hash_ *= 1099511628211ull;
                        } else {
                            immediate_quad_raster_output_hash_ ^= word;
                            immediate_quad_raster_output_hash_ *= 1099511628211ull;
                        }
                    }
                }
            }
        };

    // Kelvin QUADS uses two triangles per independent four-vertex group. This ordering
    // matches the retained winding and preserves the diagonal interpolation edge.
    for (size_t i = 0; i + 3 < vertex_count; i += 4) {
        rasterize_triangle(transformed_batch[i + 1],
                           transformed_batch[i + 2],
                           transformed_batch[i]);
        rasterize_triangle(transformed_batch[i + 2],
                           transformed_batch[i + 3],
                           transformed_batch[i]);
    }
    if (failed) return false;
    if (!color_surface.flush()) {
        fault_("NV2A immediate-quad color DMA write failed");
        return false;
    }
    // The other half of the coherence hole closed at the head of this function:
    // guest memory is now newer than any GPU copy of the same range.
    invalidate_renderer_color_overlap(
        color_address, static_cast<size_t>(kColorBytes),
        nv2a_invalidate_token(kNv2aInvalidateImmediateQuad));
    if (i8_profile)
        ++immediate_quad_i8_raster_batches_;
    else if (menu_i8_profile)
        ++immediate_quad_menu_i8_raster_batches_;
    else if (artwork_i8_profile)
        ++immediate_quad_artwork_i8_raster_batches_;
    else if (main_panel_profile)
        ++immediate_quad_main_panel_raster_batches_;
    else if (fullscreen_profile)
        ++immediate_quad_fullscreen_raster_batches_;
    else if (a8_profile)
        ++immediate_quad_a8_raster_batches_;
    else
        ++immediate_quad_raster_batches_;
    executed = true;
    return true;
}

// ============ stage (f): the witnessed-profile GPU executor =================
// beads-xbox.1.15.7, DESIGN-NV2A-SEAM.md section 5(f).
//
// Selection is an EXACT STATE PREDICATE, not a profile test. The witnessed
// matchers already pin every combiner word of these two profiles, and the
// per-profile subset validators already pin the raster state (they fault_ on
// anything outside it), so the shader for a profile has to implement exactly
// one combiner configuration. gpu_profile_state_ok re-asserts that pinning
// independently of the matcher, so a future matcher relaxation cannot silently
// widen what the GPU claims to implement -- it would fall back to the CPU
// executor instead, unchanged.
bool Nv2a::gpu_profile_state_ok(const DrawJob& job) const {
    // Deliberately independent of --nv2a-gpu-profiles and of whether a renderer
    // exists: this is the STATE question, and the residency census below uses
    // it in both arms so the flag-off run measures the same partition the
    // flag-on run executes.
    // F14 / design section 4.3 option O3: the per-write FNV folds are a
    // sequential accumulator over every written word and cannot be reproduced
    // by a fragment shader. F15: a diagnostic callback wants the software
    // path's per-write record. Both fall back, exactly as threading does.
    if (job.hash_on) { ++gpu_profile_reject_[kGpuRejectHashOn]; return false; }
    if (job.debug_cb_installed) {
        ++gpu_profile_reject_[kGpuRejectDebugCb];
        return false;
    }
    const auto& rs = job.raster_state;
    const Profile profile = static_cast<Profile>(job.profile);
    const bool radial = profile == Profile::kSwizzledRadialColor;
    const bool textured = profile == Profile::kPitchTexturedColor;
    // beads-xbox.1.15.10 phase 4 (lever A). Third executor profile, mask bit 2.
    const bool projected = profile == Profile::kPitchProjectedColor;
    if (!radial && !textured && !projected) return false;
    if ((gpu_profile_mask_ & (radial ? 1u : (textured ? 2u : 4u))) == 0)
        return false;
    // Polygon offset: the shader implements depth WITHOUT the
    // `depth += bias + scale * max_z_slope` term, which is exact only while the
    // offset is disabled. Both profiles' validators pin `enables` to a value
    // with bit 13 clear, so this is a re-assertion, not a new restriction.
    if ((rs[kRasterEnables] & (1u << 13)) != 0 ||
        rs[kRasterShaderOtherStageInput] != 0 || rs[kRasterDotRgbMapping] != 0) {
        ++gpu_profile_reject_[kGpuRejectRasterSubset];
        return false;
    }
    if (radial) {
        if (rs[kRasterTextureEnableMask] != 0 ||
            rs[kRasterShaderStageProgram] != 0x00000004 ||
            rs[kRasterCombinerControl] != 0x00011103 ||
            rs[kRasterCombinerColorIcw0 + 0] != 0x48480000 ||
            rs[kRasterCombinerColorIcw0 + 1] != 0x2D2D0000 ||
            rs[kRasterCombinerColorIcw0 + 2] != 0xC4CD0000 ||
            rs[kRasterCombinerAlphaIcw0 + 0] != 0x00000000 ||
            rs[kRasterCombinerAlphaIcw0 + 1] != 0x3D3D1010 ||
            rs[kRasterCombinerAlphaIcw0 + 2] != 0xD4DD1010 ||
            rs[kRasterCombinerColorOcw0 + 0] != 0x000820D0 ||
            rs[kRasterCombinerColorOcw0 + 1] != 0x000000D0 ||
            rs[kRasterCombinerColorOcw0 + 2] != 0x000000C0 ||
            rs[kRasterCombinerAlphaOcw0 + 0] != 0x00000000 ||
            rs[kRasterCombinerAlphaOcw0 + 1] != 0x000000D0 ||
            rs[kRasterCombinerAlphaOcw0 + 2] != 0x000000C0 ||
            rs[kRasterCombinerSpecularFog0] != 0x0000000C ||
            rs[kRasterCombinerSpecularFog1] != 0x00002080 ||
            rs[kRasterEnables] != 0x000Fu ||
            rs[kRasterAlphaFunc] != 0x0206 ||
            (rs[kRasterAlphaRef] & 0xFFu) != 1) {
            ++gpu_profile_reject_[kGpuRejectCombiner];
            return false;
        }
        for (size_t stage = 0; stage < 3; ++stage) {
            if (rs[kRasterCombinerFactor0_0 + stage] != 0 ||
                rs[kRasterCombinerFactor1_0 + stage] != 0) {
                ++gpu_profile_reject_[kGpuRejectCombiner];
                return false;
            }
        }
    } else if (projected) {
        // The EXACT combiner fingerprint of kPitchProjectedColor, re-asserted
        // independently of the matcher exactly as the two profiles above are:
        // nv2a.cpp `projected_color_icw` / `projected_alpha_icw` /
        // `projected_color_ocw` / `projected_factor0` / `projected_factor1`,
        // seven stages, with STAGE 5 the only pair of free words (the shader's
        // weight0 / weight1). RS0 0x00010C61 and CombinerControl 0x00011107
        // separate it from the textured program, which is 0x00010C60 /
        // 0x00011108 and has eight stages.
        static const std::array<uint32_t, 7> kIcw{{
            0x4849484A, 0x0D0D0000, 0xCDCD0000, 0xCDCD0000,
            0xCDCD0000, 0xC10CC20D, 0xC40520C1,
        }};
        static const std::array<uint32_t, 7> kAlphaIcw{{
            0x5859585A, 0, 0, 0, 0, 0, 0,
        }};
        static const std::array<uint32_t, 7> kOcw{{
            0x000C30CD, 0x000000D0, 0x000000D0, 0x000000D0,
            0x000000D0, 0x000005CD, 0x00000C00,
        }};
        static const std::array<uint32_t, 7> kFactor0{{
            0, 0, 0, 0, 0, 0, 0xFF000000,
        }};
        static const std::array<uint32_t, 7> kFactor1{{
            0, 0, 0, 0, 0, 0, 0,
        }};
        if (rs[kRasterTextureEnableMask] != 0xFu ||
            rs[kRasterShaderStageProgram] != 0x00010C61 ||
            rs[kRasterShadowDepthFunc] != 4 ||
            rs[kRasterCombinerControl] != 0x00011107 ||
            rs[kRasterCombinerSpecularFog0] != 0x0C0B000F ||
            rs[kRasterCombinerSpecularFog1] != 0x0C011180 ||
            rs[kRasterCombinerFinalFactor0] != 0xFF3F3F3F ||
            rs[kRasterCombinerFinalFactor1] != 0 ||
            rs[kRasterEnables] != 0x000Cu ||
            rs[kRasterAlphaFunc] != 0x0206 ||
            (rs[kRasterAlphaRef] & 0xFFu) != 1) {
            ++gpu_profile_reject_[kGpuRejectCombiner];
            return false;
        }
        for (size_t stage = 0; stage < 7; ++stage) {
            if (rs[kRasterCombinerColorIcw0 + stage] != kIcw[stage] ||
                rs[kRasterCombinerAlphaIcw0 + stage] != kAlphaIcw[stage] ||
                rs[kRasterCombinerColorOcw0 + stage] != kOcw[stage] ||
                rs[kRasterCombinerAlphaOcw0 + stage] != 0) {
                ++gpu_profile_reject_[kGpuRejectCombiner];
                return false;
            }
            if (stage == 5) continue;
            if (rs[kRasterCombinerFactor0_0 + stage] != kFactor0[stage] ||
                rs[kRasterCombinerFactor1_0 + stage] != kFactor1[stage]) {
                ++gpu_profile_reject_[kGpuRejectCombiner];
                return false;
            }
        }
        // The two sampled images, pinned exactly as the profile's own subset
        // validator pins them (nv2a.cpp, `witnessed_texture` and
        // `witnessed_projected_texture`). The shader implements ONE addressing
        // and filtering configuration per unit: CLAMP-to-edge bilinear on the
        // cube pair (address 0x00030303, filter 0x02022000, control0
        // 0x4003FFC0) and REPEAT bilinear on the stage-0 projective image
        // (format 0x0771062A, address 0x00010101, filter 0x02022000, control0
        // 0x4003FFF0, control1 0x14000000, palette 0). The border colour is not
        // asserted because kRepeat and kClampToEdge never read it.
        const uint32_t format1 = rs[kRasterTextureFormat0 + 1];
        if (format1 != rs[kRasterTextureFormat0 + 2] ||
            (format1 != 0x0661072E && format1 != 0x0771072E) ||
            rs[kRasterTextureOffset0 + 1] != rs[kRasterTextureOffset0 + 2] ||
            rs[kRasterTextureAddress0 + 1] != 0x00030303 ||
            rs[kRasterTextureAddress0 + 2] != 0x00030303 ||
            rs[kRasterTextureFilter0 + 1] != 0x02022000 ||
            rs[kRasterTextureFilter0 + 2] != 0x02022000 ||
            rs[kRasterTextureControl0_0 + 1] != 0x4003FFC0 ||
            rs[kRasterTextureControl0_0 + 2] != 0x4003FFC0 ||
            rs[kRasterTexturePalette0 + 1] != 0 ||
            rs[kRasterTexturePalette0 + 2] != 0 ||
            rs[kRasterTextureFormat0 + 0] != 0x0771062A ||
            rs[kRasterTextureAddress0 + 0] != 0x00010101 ||
            rs[kRasterTextureFilter0 + 0] != 0x02022000 ||
            rs[kRasterTextureControl0_0 + 0] != 0x4003FFF0 ||
            rs[kRasterTextureControl1_0 + 0] != 0x14000000 ||
            rs[kRasterTexturePalette0 + 0] != 0) {
            ++gpu_profile_reject_[kGpuRejectCombiner];
            return false;
        }
        if (job.texture_width == 0 || job.texture_height == 0 ||
            job.texture_data.size() != static_cast<size_t>(job.texture_width) *
                                           job.texture_height * 4u ||
            job.projected_texture_width == 0 ||
            job.projected_texture_height == 0 ||
            job.projected_texture_data.size() !=
                static_cast<size_t>(job.projected_texture_width) *
                    job.projected_texture_height * 4u) {
            ++gpu_profile_reject_[kGpuRejectTexture];
            return false;
        }
    } else {
        static const std::array<uint32_t, 8> kIcw{{
            0xC149C14A, 0x0D0D0000, 0xCDCD0000, 0xCDCD0000,
            0xCDCD0000, 0xCDCD0000, 0xC10CC20D, 0xC40520C1,
        }};
        static const std::array<uint32_t, 8> kAlphaIcw{{
            0xD159D15A, 0, 0, 0, 0, 0, 0, 0,
        }};
        static const std::array<uint32_t, 8> kOcw{{
            0x000C30CD, 0x000000D0, 0x000000D0, 0x000000D0,
            0x000000D0, 0x000000D0, 0x000005CD, 0x00000C00,
        }};
        static const std::array<uint32_t, 8> kFactor0{{
            0xFF0000FF, 0, 0, 0, 0, 0, 0, 0xFF000000,
        }};
        static const std::array<uint32_t, 8> kFactor1{{
            0, 0, 0, 0, 0, 0, 0, 0,
        }};
        if (rs[kRasterTextureEnableMask] != 0xFu ||
            rs[kRasterShaderStageProgram] != 0x00010C60 ||
            rs[kRasterShadowDepthFunc] != 4 ||
            rs[kRasterCombinerControl] != 0x00011108 ||
            rs[kRasterCombinerSpecularFog0] != 0x0C0B000F ||
            rs[kRasterCombinerSpecularFog1] != 0x0C011180 ||
            rs[kRasterCombinerFinalFactor0] != 0xFF3F3F3F ||
            rs[kRasterCombinerFinalFactor1] != 0 ||
            rs[kRasterEnables] != 0x000Cu ||
            rs[kRasterAlphaFunc] != 0x0206 ||
            (rs[kRasterAlphaRef] & 0xFFu) != 1) {
            ++gpu_profile_reject_[kGpuRejectCombiner];
            return false;
        }
        for (size_t stage = 0; stage < 8; ++stage) {
            if (rs[kRasterCombinerColorIcw0 + stage] != kIcw[stage] ||
                rs[kRasterCombinerAlphaIcw0 + stage] != kAlphaIcw[stage] ||
                rs[kRasterCombinerColorOcw0 + stage] != kOcw[stage] ||
                rs[kRasterCombinerAlphaOcw0 + stage] != 0) {
                ++gpu_profile_reject_[kGpuRejectCombiner];
                return false;
            }
            // Stage 6 carries the only two combiner words this fingerprint
            // leaves free; they are the shader's weight0 / weight1 uniforms.
            if (stage == 6) continue;
            if (rs[kRasterCombinerFactor0_0 + stage] != kFactor0[stage] ||
                rs[kRasterCombinerFactor1_0 + stage] != kFactor1[stage]) {
                ++gpu_profile_reject_[kGpuRejectCombiner];
                return false;
            }
        }
        if (job.texture_width == 0 || job.texture_height == 0 ||
            job.texture_data.size() != static_cast<size_t>(job.texture_width) *
                                           job.texture_height * 4u) {
            ++gpu_profile_reject_[kGpuRejectTexture];
            return false;
        }
    }
    // Common raster subset, re-asserted (the validators already faulted on
    // anything else): no blending, LEQUAL depth with the write on, smooth
    // shading, full colour mask.
    if (rs[kRasterControl0] != 0x00100001 || rs[kRasterBlendSrc] != 1 ||
        rs[kRasterBlendDst] != 0 || rs[kRasterBlendEquation] != 0x8006 ||
        rs[kRasterDepthFunc] != 0x0203 || rs[kRasterColorMask] != 0x01010101 ||
        rs[kRasterShadeMode] != 0x1D01 ||
        rs[kRasterFrontPolygonMode] != 0x1B02 ||
        rs[kRasterBackPolygonMode] != 0x1B02 ||
        rs[kRasterCullFace] != 0x0405 || rs[kRasterFrontFace] != 0x0900 ||
        rs[kRasterZMinMaxControl] != 1) {
        ++gpu_profile_reject_[kGpuRejectRasterSubset];
        return false;
    }
    const uint32_t surface_type = (job.surface_format >> 8) & 0xFu;
    // beads-xbox.1.15.10 phase 2 session 2. A SWIZZLED destination (type 2) is
    // Morton-ordered, and the Morton index only maps [0,w) x [0,h) ONTO
    // [0, w*h) when BOTH extents are powers of two. It is not merely a gap
    // otherwise: at 1280x480 the largest index the interleave produces is
    // 718 847 against w*h - 1 = 614 399, so the upload and the resolve would
    // index a pixel past the end of a buffer sized to the surface. Verified
    // numerically, not assumed: for 1024x512 (the only swizzled witnessed
    // surface this workload uses) the map is a bijection, max index 524 287 of
    // 524 288; for 1280x480 and 300x200 it is not. The hardware only produces
    // power-of-two swizzled surfaces, so this predicate has never rejected a
    // job here -- it exists so that a workload that does produce one takes the
    // CPU path instead of writing out of bounds.
    const auto power_of_two = [](uint32_t v) {
        return v != 0 && (v & (v - 1)) == 0;
    };
    if (!job.uses_depth || !job.writes_depth ||
        job.color_bytes != job.depth_bytes || job.surface_width == 0 ||
        job.surface_height == 0 || job.pitch != job.surface_width * 4u ||
        job.color_bytes !=
            static_cast<uint64_t>(job.pitch) * job.surface_height ||
        radial != (surface_type == 2) ||
        (surface_type == 2 && !(power_of_two(job.surface_width) &&
                                power_of_two(job.surface_height)))) {
        ++gpu_profile_reject_[kGpuRejectSurface];
        return false;
    }
    if (job.primitive_mode != 5 && job.primitive_mode != 6 &&
        job.primitive_mode != 7) {
        ++gpu_profile_reject_[kGpuRejectPrimitive];
        return false;
    }
    // Phase 2 (beads-xbox.1.15.10): the shader now runs the SOFTWARE PATH'S OWN
    // int64 edge arithmetic (GL_ARB_gpu_shader_int64), so there is no longer a
    // magnitude at which the two disagree by construction -- including the
    // degenerate regime where both wrap. The bound that remains is the one that
    // keeps `llround(pos * 16.0f)` itself meaningful, so the DEFAULT moved from
    // 2^24 to 2^50 (see set_gpu_exact_coords for the opt-out that restores the
    // old bound for bisection).
    //
    // This is not a tuning change, it is the phase-2 census result. Per 12 G
    // instructions the 2^24 bound rejected 1 126 jobs, and those 1 126 jobs
    // caused 806 of the 1 110 witnessed-CPU-draw residency breaks -- 0.72 full
    // surface round trips EACH, against 0.029 for kPitchProjectedColor and
    // 0.000 for every one of the other eleven CPU profiles, including
    // kPitchZ16Depth's 142 842 draws. A rejected radial or pitch-textured draw
    // is, by definition, a draw on the surface that is GPU-authoritative at
    // that instant, so it is the single most expensive kind of draw to leave on
    // the CPU.
    const double kFixedLimit = static_cast<double>(
        gpu_exact_coords_ ? kGpuFixedCoordinateLimitExact
                          : kGpuFixedCoordinateLimitRelaxed);
    // Both sampled profiles read texcoord1 and texcoord2; the projected one
    // also reads the PROJECTIVE texcoord0, components 0, 1 and 3.
    const bool textured_profile = textured || projected;
    bool out_of_range = false;
    bool non_finite = false;
    for (const TransformedVertex& v : job.vertices) {
        // position[0..2] non-finite makes the software path DROP the triangle
        // (the per-triangle isfinite guard), and position[3] non-finite is
        // legal there -- 1.0/inf is 0.0 and 1.0/nan is nan, both of which the
        // shader carries in the same doubles. So neither is a job-level
        // disqualifier; the triangle-level drop is reproduced in the assembly
        // loop of gpu_profile_submit.
        const float fx = v.position[0] * 16.0f;
        const float fy = v.position[1] * 16.0f;
        const double magnitude = std::max(std::abs(static_cast<double>(fx)),
                                          std::abs(static_cast<double>(fy)));
        if (std::isfinite(fx) && std::isfinite(fy)) {
            if (magnitude > static_cast<double>(gpu_profile_max_fixed_))
                gpu_profile_max_fixed_ = static_cast<uint64_t>(magnitude);
            // The packet carries the fixed-point coordinates as doubles, which
            // hold every integer below 2^53 exactly, so the only bound left is
            // the one that keeps llround itself meaningful.
            if (magnitude >= kFixedLimit) out_of_range = true;
        }
        for (size_t c = 0; c < 3; ++c) {
            // DIFFUSE IS NOT A DISQUALIFIER, at any value. The software path
            // runs `if (isnan) value = 1.0f;` then clamps to [0,1] BEFORE
            // interpolating, so +inf becomes 1.0f and -inf becomes 0.0f and the
            // packet carries the already-fixed-up, already-clamped float. An
            // earlier revision of this predicate rejected infinite diffuse and
            // that alone put 1 857 of 105 070 eligible jobs back on the CPU per
            // 2 G instructions, which shattered the residency runs (137 -> 1 279
            // runs, 235 -> 1 377 surface round trips). Measured, not reasoned.
            //
            // TEXCOORDS ARE. A non-finite INTERPOLATED varying is a software
            // fault_, and the executor may not fault (design F13), so a vertex
            // carrying one keeps the whole job on the CPU where the fault can
            // be raised at the instruction that caused it.
            if (!std::isfinite(v.texture[textured_profile ? 1 : 0][c]) ||
                (textured_profile && !std::isfinite(v.texture[2][c]))) {
                non_finite = true;
            }
            if (projected && c < 2 && !std::isfinite(v.texture[0][c]))
                non_finite = true;
        }
        if (projected && !std::isfinite(v.texture[0][3])) {
            non_finite = true;
        }
    }
    if (out_of_range) {
        ++gpu_profile_reject_[kGpuRejectVertexRange];
        return false;
    }
    // A non-finite texcoord on some vertex of the batch is NOT by itself a
    // disqualifier, and treating it as one cost 1 857 of 105 070 eligible jobs
    // per 2 G instructions -- 1.8 percent of the jobs, but enough interleaving
    // to take the residency runs from 137 to 1 279 and the surface round trips
    // from 235 to 1 377. The software path only faults when the INTERPOLATED
    // varying is non-finite, which requires the bad vertex to belong to a
    // triangle that survives culling and covers a fragment. The cheap
    // conservative half of that -- does a surviving triangle reference the bad
    // vertex at all -- is evaluated here, and only for the 1.8 percent of jobs
    // that need it, so the common path pays one bool.
    if (non_finite &&
        gpu_profile_non_finite_reaches_raster(job, textured_profile,
                                              projected)) {
        ++gpu_profile_reject_[kGpuRejectVertexNonFinite];
        return false;
    }
    return true;
}

// True when a triangle that survives the software path's finiteness guard and
// its back-face cull references a vertex with a non-finite texcoord -- i.e.
// when the software path could reach its `fault_` on a non-finite varying.
bool Nv2a::gpu_profile_non_finite_reaches_raster(const DrawJob& job,
                                                 bool textured,
                                                 bool projected) const {
    const std::vector<TransformedVertex>& vertices = job.vertices;
    auto bad = [textured, projected](const TransformedVertex& v) {
        for (size_t c = 0; c < 3; ++c) {
            if (!std::isfinite(v.texture[textured ? 1 : 0][c])) return true;
            if (textured && !std::isfinite(v.texture[2][c])) return true;
            if (projected && c < 2 && !std::isfinite(v.texture[0][c]))
                return true;
        }
        if (projected && !std::isfinite(v.texture[0][3])) return true;
        return false;
    };
    auto fixed_xy = [](float value) -> int64_t {
        return static_cast<int64_t>(std::llround(value * 16.0f));
    };
    auto survives = [&](const TransformedVertex& a, const TransformedVertex& b,
                        const TransformedVertex& c) {
        for (const TransformedVertex* v : {&a, &b, &c}) {
            if (!std::isfinite(v->position[0]) ||
                !std::isfinite(v->position[1]) ||
                !std::isfinite(v->position[2])) {
                return false;
            }
        }
        const int64_t ax = fixed_xy(a.position[0]);
        const int64_t ay = fixed_xy(a.position[1]);
        const int64_t bx = fixed_xy(b.position[0]);
        const int64_t by = fixed_xy(b.position[1]);
        const int64_t cx = fixed_xy(c.position[0]);
        const int64_t cy = fixed_xy(c.position[1]);
        const int64_t area = (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
        if (area <= 0) return false;
        return bad(a) || bad(b) || bad(c);
    };
    const size_t n = vertices.size();
    if (job.primitive_mode == 5) {
        for (size_t i = 0; i + 2 < n; i += 3)
            if (survives(vertices[i], vertices[i + 1], vertices[i + 2]))
                return true;
    } else if (job.primitive_mode == 6) {
        for (size_t i = 2; i < n; ++i) {
            const bool even = (i & 1u) == 0;
            if (survives(vertices[even ? i - 2 : i - 1],
                         vertices[even ? i - 1 : i - 2], vertices[i])) {
                return true;
            }
        }
    } else {
        for (size_t i = 2; i < n; ++i)
            if (survives(vertices[0], vertices[i - 1], vertices[i]))
                return true;
    }
    return false;
}

// Build the packet and hand it to the backend. Everything that can fault_ has
// already run; a failure here is reported as a fault on the emulation thread at
// the call site, exactly as the resident generic-combiner path reports one.
bool Nv2a::gpu_profile_submit(DrawJob& job, bool& submitted) {
    submitted = false;
    const Profile profile = static_cast<Profile>(job.profile);
    const bool radial = profile == Profile::kSwizzledRadialColor;
    const bool projected = profile == Profile::kPitchProjectedColor;
    // Slot in the per-profile surface census; also the index the executor's
    // batch counters use below.
    const size_t profile_slot = radial ? 0u : (projected ? 2u : 1u);
    const auto& rs = job.raster_state;
    Nv2aWitnessedProfilePacket& packet = gpu_packet_;
    packet.profile = radial
        ? Nv2aWitnessedProfilePacket::Profile::kSwizzledRadialColor
        : (projected
               ? Nv2aWitnessedProfilePacket::Profile::kPitchProjectedColor
               : Nv2aWitnessedProfilePacket::Profile::kPitchTexturedColor);
    packet.color_address = job.color_address;
    packet.depth_address = job.depth_address;
    packet.surface_size = static_cast<size_t>(job.color_bytes);
    packet.surface_pitch = job.surface_pitch;
    packet.surface_format = job.surface_format;
    packet.surface_width = job.surface_width;
    packet.surface_height = job.surface_height;
    packet.swizzled_destination = radial;
    gpu_profile_color_addr_[profile_slot] = job.color_address;
    gpu_profile_depth_addr_[profile_slot] = job.depth_address;
    gpu_profile_surface_bytes_[profile_slot] =
        static_cast<uint32_t>(job.color_bytes);
    // Clip: exactly the software executor's xmin/xmax/ymin/ymax, which is the
    // surface clip rect intersected with the window clip.
    uint32_t xmin = job.clip_x;
    uint32_t xmax = job.clip_x + job.clip_width - 1;
    uint32_t ymin = job.clip_y;
    uint32_t ymax = job.clip_y + job.clip_height - 1;
    const uint32_t window_x = rs[kRasterWindowClipHorizontal];
    const uint32_t window_y = rs[kRasterWindowClipVertical];
    xmin = std::max(xmin, window_x & 0xFFFu);
    xmax = std::min(xmax, (window_x >> 16) & 0xFFFu);
    ymin = std::max(ymin, window_y & 0xFFFu);
    ymax = std::min(ymax, (window_y >> 16) & 0xFFFu);
    if (xmax < xmin || ymax < ymin) {
        // Empty clip: the software path counts the batch and writes nothing.
        // Same here, with no GL work at all.
        if (radial) ++radial_color_batches_;
        else if (projected) ++pitch_projected_color_batches_;
        else ++pitch_textured_color_batches_;
        ++gpu_profile_jobs_;
#if XLLE_VSH_DRAW_OVERLAP_CENSUS
        if (job.vsh_overlap.outcome == VshDrawOverlapOutcome::kUnknown)
            job.vsh_overlap.outcome = VshDrawOverlapOutcome::kWitnessedEmpty;
#endif
        submitted = true;
        return true;
    }
    if (xmax >= job.surface_width || ymax >= job.surface_height) return true;
    packet.xmin = xmin; packet.xmax = xmax;
    packet.ymin = ymin; packet.ymax = ymax;
    std::memcpy(&packet.clip_min, &rs[kRasterClipMin], sizeof(float));
    std::memcpy(&packet.clip_max, &rs[kRasterClipMax], sizeof(float));

    // ---- primitive assembly, identical to emit_batch_primitives -------------
    packet.triangles.clear();
    const std::vector<TransformedVertex>& vertices = job.vertices;
    const size_t vertex_count = vertices.size();
    uint64_t triangle_tally = 0;
    auto emit = [&](const TransformedVertex& ia, const TransformedVertex& ib,
                    const TransformedVertex& ic) {
        const TransformedVertex* a = &ia;
        const TransformedVertex* b = &ib;
        const TransformedVertex* c = &ic;
        // The software path's per-triangle finiteness guard, verbatim: a
        // triangle with a non-finite screen position contributes nothing.
        for (const TransformedVertex* vertex : {a, b, c}) {
            if (!std::isfinite(vertex->position[0]) ||
                !std::isfinite(vertex->position[1]) ||
                !std::isfinite(vertex->position[2])) {
                return;
            }
        }
        auto fixed_xy = [](float value) -> int64_t {
            return static_cast<int64_t>(std::llround(value * 16.0f));
        };
        int64_t ax = fixed_xy(a->position[0]);
        int64_t ay = fixed_xy(a->position[1]);
        int64_t bx = fixed_xy(b->position[0]);
        int64_t by = fixed_xy(b->position[1]);
        int64_t cx = fixed_xy(c->position[0]);
        int64_t cy = fixed_xy(c->position[1]);
        auto edge = [](int64_t x0, int64_t y0, int64_t x1, int64_t y1,
                       int64_t px, int64_t py) -> int64_t {
            return (x1 - x0) * (py - y0) - (y1 - y0) * (px - x0);
        };
        int64_t area = edge(ax, ay, bx, by, cx, cy);
        if (area <= 0) return;   // CW is front-facing in the witnessed states
        // The per-triangle counter sits here in the software path, after the
        // cull and before the bounding-box test, so it is tallied here too.
        ++triangle_tally;
        // The software path adds `polygon_bias + polygon_scale * max_z_slope`
        // to every fragment's depth. Both terms are zero for these profiles
        // (bit 13 of `enables` is clear, re-asserted by the predicate), so the
        // sum is +0.0f -- EXCEPT when max_z_slope is not finite, where 0.0f*inf
        // is NaN and the software path discards every fragment of the triangle.
        // That case is reproduced by dropping the triangle here rather than by
        // carrying a NaN through the shader.
        const float x10 = static_cast<float>(bx - ax) / 16.0f;
        const float y10 = static_cast<float>(by - ay) / 16.0f;
        const float x20 = static_cast<float>(cx - ax) / 16.0f;
        const float y20 = static_cast<float>(cy - ay) / 16.0f;
        const float z10 = b->position[2] - a->position[2];
        const float z20 = c->position[2] - a->position[2];
        const float determinant = x10 * y20 - x20 * y10;
        const float dzdx = (z10 * y20 - z20 * y10) / determinant;
        const float dzdy = (z20 * x10 - z10 * x20) / determinant;
        const float max_z_slope = std::max(std::abs(dzdx), std::abs(dzdy));
        if (!std::isfinite(max_z_slope)) return;
        Nv2aWitnessedTriangle t;
        t.ax = ax; t.ay = ay;
        t.bx = bx; t.by = by;
        t.cx = cx; t.cy = cy;
        // Phase 2 (beads-xbox.1.15.10): the software path's own scanline
        // bounds, computed by the SAME expression on the same compiler so the
        // two agree bit for bit -- including in the degenerate regime where the
        // double-to-int32 conversion saturates, which is exactly the regime
        // this path exists to serve. They are carried for every triangle
        // (cheap: six compares) and consumed only by the WIDE path.
        const auto scan_min = [](int64_t lo_bound, int64_t p, int64_t q,
                                 int64_t r) {
            return std::max<int32_t>(
                static_cast<int32_t>(lo_bound),
                static_cast<int32_t>(std::ceil(
                    (static_cast<double>(std::min({p, q, r})) - 8.0) / 16.0)));
        };
        const auto scan_max = [](int64_t hi_bound, int64_t p, int64_t q,
                                 int64_t r) {
            return std::min<int32_t>(
                static_cast<int32_t>(hi_bound),
                static_cast<int32_t>(std::floor(
                    (static_cast<double>(std::max({p, q, r})) - 8.0) / 16.0)));
        };
        t.bbox_xmin = scan_min(xmin, ax, bx, cx);
        t.bbox_xmax = scan_max(xmax, ax, bx, cx);
        t.bbox_ymin = scan_min(ymin, ay, by, cy);
        t.bbox_ymax = scan_max(ymax, ay, by, cy);
        if (t.bbox_xmax < t.bbox_xmin || t.bbox_ymax < t.bbox_ymin) return;
        // A coordinate the fixed-function rasterizer cannot place reliably in
        // float clip space. 2^24 is where a float stops holding every integer,
        // and the vertex shader's clip-space position is a float; beyond it the
        // driver may under-cover, which the in-shader test cannot repair
        // because it can only remove fragments. Such a triangle is drawn as its
        // own bounding box instead.
        const int64_t kWide = 1ll << 24;
        const auto is_wide = [kWide](int64_t v) {
            return v >= kWide || v <= -kWide;
        };
        t.wide = is_wide(ax) || is_wide(ay) || is_wide(bx) || is_wide(by) ||
                 is_wide(cx) || is_wide(cy);
        t.za = a->position[2]; t.zb = b->position[2]; t.zc = c->position[2];
        t.inverse_area = 1.0f / static_cast<float>(area);
        t.wa = a->position[3]; t.wb = b->position[3]; t.wc = c->position[3];
        t.inverse_area_double = 1.0 / static_cast<double>(area);
        t.inverse_w[0] = 1.0 / static_cast<double>(a->position[3]);
        t.inverse_w[1] = 1.0 / static_cast<double>(b->position[3]);
        t.inverse_w[2] = 1.0 / static_cast<double>(c->position[3]);
        const TransformedVertex* corner[3] = {a, b, c};
        const size_t primary_tex = radial ? 0 : 1;
        for (size_t v = 0; v < 3; ++v) {
            for (size_t comp = 0; comp < 3; ++comp) {
                float value = corner[v]->diffuse[comp];
                if (std::isnan(value)) value = 1.0f;
                t.diffuse[v][comp] = std::clamp(value, 0.0f, 1.0f);
                t.tex_a[v][comp] = corner[v]->texture[primary_tex][comp];
                if (!radial) t.tex_b[v][comp] = corner[v]->texture[2][comp];
            }
            if (projected) {
                // The software path reads components 0, 1 and 3 of texture[0]
                // and stores them at indices 0, 1, 2 -- `projected_coordinate[
                // component == 3 ? 2 : component]`. Same order here.
                t.tex_p[v][0] = corner[v]->texture[0][0];
                t.tex_p[v][1] = corner[v]->texture[0][1];
                t.tex_p[v][2] = corner[v]->texture[0][3];
            }
        }
        packet.triangles.push_back(t);
    };
    if (job.primitive_mode == 5) {
        for (size_t i = 0; i + 2 < vertex_count; i += 3)
            emit(vertices[i], vertices[i + 1], vertices[i + 2]);
    } else if (job.primitive_mode == 6) {
        for (size_t i = 2; i < vertex_count; ++i) {
            if ((i & 1u) == 0)
                emit(vertices[i - 2], vertices[i - 1], vertices[i]);
            else
                emit(vertices[i - 1], vertices[i - 2], vertices[i]);
        }
    } else {
        for (size_t i = 2; i < vertex_count; ++i)
            emit(vertices[0], vertices[i - 1], vertices[i]);
    }

    // ---- texture snapshot, unswizzled here because the swizzle table is the
    // ---- device's (nv2a.hpp SwizzleLookup), not the backend's ---------------
    packet.texture_linear.clear();
    packet.texture_hash = 0;
    packet.texture_width = 0;
    packet.texture_height = 0;
    packet.projected_texture_linear.clear();
    packet.projected_texture_hash = 0;
    packet.projected_texture_width = 0;
    packet.projected_texture_height = 0;
    packet.factor0_stage6 = 0;
    packet.factor1_stage6 = 0;
    // The unswizzle, once, so both images go through the same transform.
    const auto unswizzle_into = [](const std::vector<uint8_t>& src,
                                   const SwizzleLookup& swizzle, uint32_t w,
                                   uint32_t h, std::vector<uint8_t>& dst) {
        dst.resize(static_cast<size_t>(w) * h * 4u);
        for (uint32_t y = 0; y < h; ++y) {
            for (uint32_t x = 0; x < w; ++x) {
                const size_t from =
                    static_cast<size_t>(swizzle.index(x, y)) * 4u;
                const size_t to = (static_cast<size_t>(y) * w + x) * 4u;
                std::memcpy(dst.data() + to, src.data() + from, 4u);
            }
        }
    };
    if (!radial) {
        if (!job.texture_swizzle) return true;
        packet.texture_width = job.texture_width;
        packet.texture_height = job.texture_height;
        // The only free combiner words: stage 6 for the textured fingerprint,
        // stage 5 for the projected one.
        const size_t factor_stage = projected ? 5u : 6u;
        packet.factor0_stage6 = rs[kRasterCombinerFactor0_0 + factor_stage];
        packet.factor1_stage6 = rs[kRasterCombinerFactor1_0 + factor_stage];
        unswizzle_into(job.texture_data, *job.texture_swizzle,
                       job.texture_width, job.texture_height,
                       packet.texture_linear);
        packet.texture_hash = fnv1a_bytes(packet.texture_linear.data(),
                                          packet.texture_linear.size());
        if (projected) {
            if (!job.projected_texture_swizzle) return true;
            packet.projected_texture_width = job.projected_texture_width;
            packet.projected_texture_height = job.projected_texture_height;
            unswizzle_into(job.projected_texture_data,
                           *job.projected_texture_swizzle,
                           job.projected_texture_width,
                           job.projected_texture_height,
                           packet.projected_texture_linear);
            packet.projected_texture_hash =
                fnv1a_bytes(packet.projected_texture_linear.data(),
                            packet.projected_texture_linear.size());
        }
    }

    if (!renderer_->supports_witnessed_profile(packet)) return true;

    // ---- residency: supply the initial surface bytes only when needed -------
    //
    // beads-xbox.1.15.10 phase 3. PER PLANE, not "both or neither". The two
    // planes are invalidated independently and ensure_witnessed_surface already
    // uploads only the one it is missing, so asking a single question here made
    // a zeta-only break read a full colour surface out of guest RAM through
    // mem_read_ and hand the backend bytes it then ignored. The predicate is
    // the SAME function the backend acts on (select_witnessed_slot), so the two
    // cannot drift.
    //
    // And a packet whose triangle list came out EMPTY (every primitive culled,
    // degenerate or clipped away) is discarded by the backend on its first line
    // -- submit_witnessed_profile returns before ensure_witnessed_surface -- so
    // snapshotting its destination reaches nothing at all. MEASURED on the
    // shipping binary db326068: the device took 1336 surface snapshots
    // (6016.4 MB of mem_read_) while the backend performed 662 colour and 662
    // zeta plane uploads, so 674 of those snapshots -- about 3.0 GB per 12 G
    // instructions -- were read out of guest memory and thrown away. Residency
    // state is untouched either way; this declines to read bytes that provably
    // reach nothing, and everything after it runs exactly as before.
    const bool empty_packet =
        gpu_skip_empty_packets_ && packet.triangles.empty();
    bool color_resident = false;
    bool depth_resident = false;
    if (!empty_packet) {
        renderer_->witnessed_target_residency(
            job.color_address, static_cast<size_t>(job.color_bytes),
            job.surface_pitch, job.surface_height, job.surface_format,
            job.depth_address, color_resident, depth_resident);
    } else {
        color_resident = true;
        depth_resident = true;
        ++gpu_profile_empty_packets_;
    }
    packet.needs_color_upload = !color_resident;
    packet.needs_depth_upload = !depth_resident;
    if (!color_resident) {
        packet.color_input.resize(static_cast<size_t>(job.color_bytes));
        if (!mem_read_ ||
            !mem_read_(job.color_address, packet.color_input.data(),
                       packet.color_input.size())) {
            fault_("NV2A witnessed GPU surface snapshot read failed");
            return false;
        }
    } else {
        packet.color_input.clear();
    }
    if (!depth_resident) {
        packet.depth_input.resize(static_cast<size_t>(job.depth_bytes));
        if (!mem_read_ ||
            !mem_read_(job.depth_address, packet.depth_input.data(),
                       packet.depth_input.size())) {
            fault_("NV2A witnessed GPU surface snapshot read failed");
            return false;
        }
    } else {
        packet.depth_input.clear();
    }
    if (!color_resident || !depth_resident) {
        ++gpu_profile_uploads_;
        gpu_profile_upload_bytes_ +=
            packet.color_input.size() + packet.depth_input.size();
        if (!color_resident) ++gpu_profile_color_uploads_;
        if (!depth_resident) ++gpu_profile_depth_uploads_;
    }

    Nv2aWitnessedProfileResult result;
    std::string error;
    if (!renderer_->submit_witnessed_profile(packet, result, error)) {
        fault_(error.empty() ? "NV2A witnessed GPU submission failed"
                             : error.c_str());
        return false;
    }
    if (radial) {
        ++radial_color_batches_;
        radial_color_triangles_ += triangle_tally;
    } else if (projected) {
        ++pitch_projected_color_batches_;
        pitch_projected_color_triangles_ += triangle_tally;
    } else {
        ++pitch_textured_color_batches_;
        pitch_textured_color_triangles_ += triangle_tally;
    }
    ++gpu_profile_jobs_;
#if XLLE_VSH_DRAW_OVERLAP_CENSUS
    job.vsh_overlap.outcome = VshDrawOverlapOutcome::kGpuWitnessed;
#endif
    gpu_profile_triangles_ += result.triangles;
    // The backend reports its CUMULATIVE flush count; this is an assignment,
    // not an accumulation.
    gpu_profile_draw_calls_ = result.draw_calls;
    gpu_profile_texture_upload_bytes_ += result.texture_upload_bytes;
    submitted = true;
    return true;
}

bool Nv2a::rasterize_witnessed_batch(DrawJob& job, bool& executed) {
    // NV2A_RASTER_WITNESSED: the fingerprint ladder plus the witnessed per-pixel profile
    // that matches (including the immediate-quad path delegated to below). The generic
    // fallback opens NV2A_RASTER_GENERIC_SW and is carved out of this bucket.
    perf_excl::Scope region(perf_excl::kNv2aRasterWitnessed);
    executed = false;
    if (zpass_enabled_) {
        // Keep the reference small: use the existing software fragment tests,
        // after draining older jobs. Count actual samples, never draw bounds.
        draw_order_fence();
        bool ok = rasterize_generic_combiner_batch(job, executed);
        if (!ok || !executed) {
            zpass_valid_ = false;
            fault_("NV2A visibility reference encountered an unsupported draw");
            return false;
        }
        return true;
    }
    // Stage (d) seam (beads-xbox.1.15.4, DESIGN-NV2A-SEAM.md section 3.2):
    // the executor reads the LATCHED JOB, never the live members. These
    // aliases are the whole of the mechanical redirection -- every use site
    // below is textually unchanged, so no expression, and in particular no
    // floating-point expression, moved with the split.
    const auto& raster_state = job.raster_state;
    const auto& transformed_batch = job.vertices;
    const uint32_t primitive_mode = job.primitive_mode;
    const uint32_t surface_format = job.surface_format;
    const uint32_t surface_pitch = job.surface_pitch;
    const uint32_t surface_clip_horizontal = job.surface_clip_horizontal;
    const uint32_t surface_clip_vertical = job.surface_clip_vertical;
    const uint32_t surface_color_offset = job.surface_color_offset;
    const uint32_t surface_zeta_offset = job.surface_zeta_offset;
    if (primitive_mode == 8) {
        // Stage (e) ordering fence: this family builds its surfaces WITHOUT
        // mem_direct_ (section 1.3), so it always executes here, on the
        // emulation thread. It writes the same colour surfaces queued jobs
        // write, and it must not overtake them.
        draw_order_fence();
        return rasterize_witnessed_immediate_quad(job, executed);
    }
    const uint32_t enables = raster_state[kRasterEnables];
    const uint32_t color_format = surface_format & 0xFu;
    const uint32_t surface_type = (surface_format >> 8) & 0xFu;
    const uint32_t anti_aliasing = (surface_format >> 12) & 0xFu;
    const uint32_t zeta_format = (surface_format >> 4) & 0xFu;
    const bool supported_primitive =
        primitive_mode == 5 || primitive_mode == 6 || primitive_mode == 7;
    // Recorder-only primitive families still travel through transform/assembly, but must
    // not enter a witnessed triangle-raster profile (or its validation path).
    if (!supported_primitive) return true;
    const bool depth_only =
        raster_state[kRasterColorMask] == 0 &&
        (enables & (1u << 3)) != 0 &&
        raster_state[kRasterDepthMask] != 0;
    const bool radial_combiner =
        raster_state[kRasterTextureEnableMask] == 0 &&
        raster_state[kRasterShaderStageProgram] == 0x00000004 &&
        raster_state[kRasterShaderOtherStageInput] == 0 &&
        raster_state[kRasterDotRgbMapping] == 0 &&
        raster_state[kRasterCombinerControl] == 0x00011103 &&
        raster_state[kRasterCombinerColorIcw0 + 0] == 0x48480000 &&
        raster_state[kRasterCombinerColorIcw0 + 1] == 0x2D2D0000 &&
        raster_state[kRasterCombinerColorIcw0 + 2] == 0xC4CD0000 &&
        raster_state[kRasterCombinerAlphaIcw0 + 0] == 0x00000000 &&
        raster_state[kRasterCombinerAlphaIcw0 + 1] == 0x3D3D1010 &&
        raster_state[kRasterCombinerAlphaIcw0 + 2] == 0xD4DD1010 &&
        raster_state[kRasterCombinerColorOcw0 + 0] == 0x000820D0 &&
        raster_state[kRasterCombinerColorOcw0 + 1] == 0x000000D0 &&
        raster_state[kRasterCombinerColorOcw0 + 2] == 0x000000C0 &&
        raster_state[kRasterCombinerAlphaOcw0 + 0] == 0x00000000 &&
        raster_state[kRasterCombinerAlphaOcw0 + 1] == 0x000000D0 &&
        raster_state[kRasterCombinerAlphaOcw0 + 2] == 0x000000C0 &&
        raster_state[kRasterCombinerFactor0_0 + 0] == 0 &&
        raster_state[kRasterCombinerFactor0_0 + 1] == 0 &&
        raster_state[kRasterCombinerFactor0_0 + 2] == 0 &&
        raster_state[kRasterCombinerFactor1_0 + 0] == 0 &&
        raster_state[kRasterCombinerFactor1_0 + 1] == 0 &&
        raster_state[kRasterCombinerFactor1_0 + 2] == 0 &&
        raster_state[kRasterCombinerSpecularFog0] == 0x0000000C &&
        raster_state[kRasterCombinerSpecularFog1] == 0x00002080;
    const std::array<uint32_t, 8> textured_color_icw{{
        0xC149C14A, 0x0D0D0000, 0xCDCD0000, 0xCDCD0000,
        0xCDCD0000, 0xCDCD0000, 0xC10CC20D, 0xC40520C1,
    }};
    const std::array<uint32_t, 8> textured_alpha_icw{{
        0xD159D15A, 0, 0, 0, 0, 0, 0, 0,
    }};
    const std::array<uint32_t, 8> textured_color_ocw{{
        0x000C30CD, 0x000000D0, 0x000000D0, 0x000000D0,
        0x000000D0, 0x000000D0, 0x000005CD, 0x00000C00,
    }};
    const std::array<uint32_t, 8> textured_factor0{{
        0xFF0000FF, 0, 0, 0, 0, 0, 0xFF072103, 0xFF000000,
    }};
    const std::array<uint32_t, 8> textured_factor1{{
        0, 0, 0, 0, 0, 0, 0xFF35FF1A, 0,
    }};
    bool textured_combiner =
        raster_state[kRasterTextureEnableMask] == 0xFu &&
        raster_state[kRasterShaderStageProgram] == 0x00010C60 &&
        raster_state[kRasterShaderOtherStageInput] == 0 &&
        raster_state[kRasterDotRgbMapping] == 0 &&
        (raster_state[kRasterShadowDepthFunc] == 0 ||
         raster_state[kRasterShadowDepthFunc] == 4) &&
        raster_state[kRasterCombinerControl] == 0x00011108 &&
        raster_state[kRasterCombinerSpecularFog0] == 0x0C0B000F &&
        raster_state[kRasterCombinerSpecularFog1] == 0x0C011180 &&
        raster_state[kRasterCombinerFinalFactor0] == 0xFF3F3F3F &&
        raster_state[kRasterCombinerFinalFactor1] == 0;
    for (size_t stage = 0; stage < 8 && textured_combiner; ++stage) {
        textured_combiner =
            raster_state[kRasterCombinerColorIcw0 + stage] ==
                textured_color_icw[stage] &&
            raster_state[kRasterCombinerAlphaIcw0 + stage] ==
                textured_alpha_icw[stage] &&
            raster_state[kRasterCombinerColorOcw0 + stage] ==
                textured_color_ocw[stage] &&
            raster_state[kRasterCombinerAlphaOcw0 + stage] == 0 &&
            (stage == 6 ||
             (raster_state[kRasterCombinerFactor0_0 + stage] ==
                  textured_factor0[stage] &&
              raster_state[kRasterCombinerFactor1_0 + stage] ==
                  textured_factor1[stage]));
    }
    const bool textured_fixed_stage6 =
        raster_state[kRasterCombinerFactor0_0 + 6] ==
            textured_factor0[6] &&
        raster_state[kRasterCombinerFactor1_0 + 6] ==
            textured_factor1[6];
    const std::array<uint32_t, 7> projected_color_icw{{
        0x4849484A, 0x0D0D0000, 0xCDCD0000, 0xCDCD0000,
        0xCDCD0000, 0xC10CC20D, 0xC40520C1,
    }};
    const std::array<uint32_t, 7> projected_alpha_icw{{
        0x5859585A, 0, 0, 0, 0, 0, 0,
    }};
    const std::array<uint32_t, 7> projected_color_ocw{{
        0x000C30CD, 0x000000D0, 0x000000D0, 0x000000D0,
        0x000000D0, 0x000005CD, 0x00000C00,
    }};
    const std::array<uint32_t, 7> projected_factor0{{
        0, 0, 0, 0, 0, 0xFF072103, 0xFF000000,
    }};
    const std::array<uint32_t, 7> projected_factor1{{
        0, 0, 0, 0, 0, 0xFF35FF1A, 0,
    }};
    bool projected_combiner =
        raster_state[kRasterTextureEnableMask] == 0xFu &&
        raster_state[kRasterShaderStageProgram] == 0x00010C61 &&
        raster_state[kRasterShaderOtherStageInput] == 0 &&
        raster_state[kRasterDotRgbMapping] == 0 &&
        (raster_state[kRasterShadowDepthFunc] == 0 ||
         raster_state[kRasterShadowDepthFunc] == 4) &&
        raster_state[kRasterCombinerControl] == 0x00011107 &&
        raster_state[kRasterCombinerSpecularFog0] == 0x0C0B000F &&
        raster_state[kRasterCombinerSpecularFog1] == 0x0C011180 &&
        raster_state[kRasterCombinerFinalFactor0] == 0xFF3F3F3F &&
        raster_state[kRasterCombinerFinalFactor1] == 0;
    for (size_t stage = 0; stage < 7 && projected_combiner; ++stage) {
        projected_combiner =
            raster_state[kRasterCombinerColorIcw0 + stage] ==
                projected_color_icw[stage] &&
            raster_state[kRasterCombinerAlphaIcw0 + stage] ==
                projected_alpha_icw[stage] &&
            raster_state[kRasterCombinerColorOcw0 + stage] ==
                projected_color_ocw[stage] &&
            raster_state[kRasterCombinerAlphaOcw0 + stage] == 0 &&
            (stage == 5 ||
             (raster_state[kRasterCombinerFactor0_0 + stage] ==
                  projected_factor0[stage] &&
              raster_state[kRasterCombinerFactor1_0 + stage] ==
                  projected_factor1[stage]));
    }
    const bool projected_fixed_stage5 =
        raster_state[kRasterCombinerFactor0_0 + 5] ==
            projected_factor0[5] &&
        raster_state[kRasterCombinerFactor1_0 + 5] ==
            projected_factor1[5];
    const std::array<uint32_t, 6> pitch_multi_color_icw{{
        0x48490000, 0x0C200000, 0x2D2D0000,
        0x2CC10000, 0xCC20C120, 0x00000000,
    }};
    const std::array<uint32_t, 6> pitch_multi_alpha_icw{{
        0x00000000, 0x1C301010, 0x3D3D1010,
        0x3CD11010, 0x00000000, 0xDCD11010,
    }};
    const std::array<uint32_t, 6> pitch_multi_color_ocw{{
        0x000820C0, 0x000000D0, 0x000000C0,
        0x000000C0, 0x00000C00, 0x00000000,
    }};
    const std::array<uint32_t, 6> pitch_multi_alpha_ocw{{
        0x00000000, 0x000000D0, 0x000000C0,
        0x000000C0, 0x00000000, 0x000000C0,
    }};
    const std::array<uint32_t, 6> pitch_multi_factor0{{
        0, 0, 0, 0xFF000000, 0xFF0D3308, 0xFFFFFFFF,
    }};
    bool pitch_multi_texture_combiner =
        raster_state[kRasterTextureEnableMask] == 3 &&
        raster_state[kRasterShaderStageProgram] == 0x00000063 &&
        raster_state[kRasterShaderOtherStageInput] == 0 &&
        raster_state[kRasterDotRgbMapping] == 0 &&
        raster_state[kRasterShadowDepthFunc] == 7 &&
        raster_state[kRasterCombinerControl] == 0x00011106 &&
        raster_state[kRasterCombinerSpecularFog0] == 0x0000000C &&
        raster_state[kRasterCombinerSpecularFog1] == 0x00001C80 &&
        raster_state[kRasterCombinerFinalFactor0] == 0 &&
        raster_state[kRasterCombinerFinalFactor1] == 0;
    for (size_t stage = 0;
         stage < pitch_multi_color_icw.size() &&
         pitch_multi_texture_combiner;
         ++stage) {
        pitch_multi_texture_combiner =
            raster_state[kRasterCombinerColorIcw0 + stage] ==
                pitch_multi_color_icw[stage] &&
            raster_state[kRasterCombinerAlphaIcw0 + stage] ==
                pitch_multi_alpha_icw[stage] &&
            raster_state[kRasterCombinerColorOcw0 + stage] ==
                pitch_multi_color_ocw[stage] &&
            raster_state[kRasterCombinerAlphaOcw0 + stage] ==
                pitch_multi_alpha_ocw[stage] &&
            raster_state[kRasterCombinerFactor0_0 + stage] ==
                pitch_multi_factor0[stage] &&
            raster_state[kRasterCombinerFactor1_0 + stage] == 0;
    }
    bool pitch_three_texture_combiner =
        raster_state[kRasterTextureEnableMask] == 7 &&
        raster_state[kRasterShaderStageProgram] == 0x00000063 &&
        raster_state[kRasterShaderOtherStageInput] == 0 &&
        raster_state[kRasterDotRgbMapping] == 0 &&
        raster_state[kRasterShadowDepthFunc] == 7 &&
        raster_state[kRasterCombinerControl] == 0x00011106 &&
        raster_state[kRasterCombinerSpecularFog0] == 0x0000000C &&
        raster_state[kRasterCombinerSpecularFog1] == 0x00001C80 &&
        raster_state[kRasterCombinerFinalFactor0] == 0 &&
        raster_state[kRasterCombinerFinalFactor1] == 0;
    for (size_t stage = 0;
         stage < pitch_multi_color_icw.size() &&
         pitch_three_texture_combiner;
         ++stage) {
        const uint32_t factor0 =
            raster_state[kRasterCombinerFactor0_0 + stage];
        const bool witnessed_factor0 =
            stage == 3 ? (factor0 & 0xFF000000u) == 0xFF000000u
                       : factor0 == pitch_multi_factor0[stage];
        pitch_three_texture_combiner =
            raster_state[kRasterCombinerColorIcw0 + stage] ==
                pitch_multi_color_icw[stage] &&
            raster_state[kRasterCombinerAlphaIcw0 + stage] ==
                pitch_multi_alpha_icw[stage] &&
            raster_state[kRasterCombinerColorOcw0 + stage] ==
                pitch_multi_color_ocw[stage] &&
            raster_state[kRasterCombinerAlphaOcw0 + stage] ==
                pitch_multi_alpha_ocw[stage] &&
            witnessed_factor0 &&
            raster_state[kRasterCombinerFactor1_0 + stage] == 0;
    }
    // Shader 0x63 cube sampling never enters the 2D border path.  Opaque
    // black is output-inert here, but it is admitted only for the finite
    // captured set: prior FF16590D plus ten RS2/RS5-RS13 additions--never as
    // a high-byte wildcard.
    constexpr std::array<uint32_t, 11> kPitchThreeBlackBorderFactors{{
        0xFF16590D, 0xFF175B0E, 0xFF1F7D13, 0xFF16580D, 0xFF18610F,
        0xFF185F0E, 0xFF175C0E, 0xFF144F0C, 0xFF175D0E, 0xFF16570D,
        0xFF18600E}};
    const uint32_t pitch_three_factor3 =
        raster_state[kRasterCombinerFactor0_0 + 3];
    const bool pitch_three_black_border_factor =
        std::find(kPitchThreeBlackBorderFactors.begin(),
                  kPitchThreeBlackBorderFactors.end(),
                  pitch_three_factor3) != kPitchThreeBlackBorderFactors.end();
    const bool pitch_three_texture_border0 =
        raster_state[kRasterTextureBorderColor0] == 0 ||
        (raster_state[kRasterTextureBorderColor0] == 0xFF000000 &&
         pitch_three_black_border_factor);
    const bool pitch_three_texture_state =
        raster_state[kRasterTextureFormat0] == 0x0661072E &&
        raster_state[kRasterTextureFormat0 + 1] == 0x0661072E &&
        raster_state[kRasterTextureFormat0 + 2] == 0x0771072E &&
        raster_state[kRasterTextureOffset0 + 1] ==
            raster_state[kRasterTextureOffset0] &&
        raster_state[kRasterTextureAddress0] == 0x00030303 &&
        raster_state[kRasterTextureAddress0 + 1] == 0x00030303 &&
        raster_state[kRasterTextureAddress0 + 2] == 0x00030303 &&
        raster_state[kRasterTextureFilter0] == 0x02022000 &&
        raster_state[kRasterTextureFilter0 + 1] == 0x02022000 &&
        raster_state[kRasterTextureFilter0 + 2] == 0x02022000 &&
        raster_state[kRasterTextureControl0_0] == 0x4003FFC0 &&
        raster_state[kRasterTextureControl0_0 + 1] == 0x4003FFC0 &&
        raster_state[kRasterTextureControl0_0 + 2] == 0x4003FFC0 &&
        raster_state[kRasterTextureControl1_0] == 0x14000000 &&
        raster_state[kRasterTextureControl1_0 + 1] == 0 &&
        raster_state[kRasterTextureControl1_0 + 2] == 0 &&
        raster_state[kRasterTextureImageRect0] == 0x050001E0 &&
        raster_state[kRasterTextureImageRect0 + 1] == 0 &&
        raster_state[kRasterTextureImageRect0 + 2] == 0 &&
        raster_state[kRasterTexturePalette0] == 0 &&
        raster_state[kRasterTexturePalette0 + 1] == 0 &&
        raster_state[kRasterTexturePalette0 + 2] == 0 &&
        pitch_three_texture_border0 &&
        raster_state[kRasterTextureBorderColor0 + 1] == 0 &&
        raster_state[kRasterTextureBorderColor0 + 2] == 0;
    const bool pitch_tex5_raster =
        primitive_mode == 5 && surface_type == 1 &&
        anti_aliasing == 0 && color_format == 8 && zeta_format == 2 &&
        surface_pitch == 0x14001400 &&
        surface_clip_horizontal == 0x05000000 &&
        surface_clip_vertical == 0x01E00000 &&
        enables == 0x000Cu &&
        raster_state[kRasterControl0] == 0x00100001 &&
        raster_state[kRasterDepthFunc] == 0x0203 &&
        raster_state[kRasterColorMask] == 0x01010101 &&
        raster_state[kRasterDepthMask] == 1 &&
        raster_state[kRasterShadeMode] == 0x1D01 &&
        raster_state[kRasterFrontPolygonMode] == 0x1B02 &&
        raster_state[kRasterBackPolygonMode] == 0x1B02 &&
        raster_state[kRasterCullFace] == 0x0404 &&
        raster_state[kRasterFrontFace] == 0x0900 &&
        raster_state[kRasterZMinMaxControl] == 1;
    const bool pitch_tex5_projected_combiner =
        raster_state[kRasterTextureEnableMask] == 5 &&
        raster_state[kRasterShaderStageProgram] == 0x00000C01 &&
        raster_state[kRasterShaderOtherStageInput] == 0x00210000 &&
        raster_state[kRasterDotRgbMapping] == 0 &&
        raster_state[kRasterShadowDepthFunc] == 7 &&
        raster_state[kRasterCombinerControl] == 0x00000001 &&
        raster_state[kRasterCombinerColorIcw0] == 0x08200000 &&
        raster_state[kRasterCombinerAlphaIcw0] == 0x11200000 &&
        raster_state[kRasterCombinerColorOcw0] == 0x00000C00 &&
        raster_state[kRasterCombinerAlphaOcw0] == 0x00000C00 &&
        (raster_state[kRasterCombinerFactor0_0] &
         0xFF000000u) == 0xFF000000u &&
        raster_state[kRasterCombinerSpecularFog0] == 0x0000000C &&
        raster_state[kRasterCombinerSpecularFog1] == 0x00001C80 &&
        raster_state[kRasterCombinerFinalFactor0] == 0 &&
        raster_state[kRasterCombinerFinalFactor1] == 0;
    const uint32_t pitch_tex5_format0 =
        raster_state[kRasterTextureFormat0];
    const bool pitch_tex5_projected_texture =
        (pitch_tex5_format0 == 0x0741062A ||
         pitch_tex5_format0 == 0x0881062A) &&
        raster_state[kRasterTextureAddress0] == 0x00030404 &&
        raster_state[kRasterTextureFilter0] == 0x02022000 &&
        raster_state[kRasterTextureControl0_0] == 0x4003FFC0 &&
        raster_state[kRasterTextureControl1_0] == 0x14000000 &&
        raster_state[kRasterTextureImageRect0] == 0x050001E0 &&
        raster_state[kRasterTexturePalette0] == 0 &&
        raster_state[kRasterTextureBorderColor0] == 0xFF000000 &&
        raster_state[kRasterTextureFormat0 + 2] == 0x0771072E &&
        raster_state[kRasterTextureAddress0 + 2] == 0x00030303 &&
        raster_state[kRasterTextureFilter0 + 2] == 0x02022000 &&
        raster_state[kRasterTextureControl0_0 + 2] == 0x4003FFC0 &&
        raster_state[kRasterTextureControl1_0 + 2] == 0 &&
        raster_state[kRasterTextureImageRect0 + 2] == 0 &&
        raster_state[kRasterTexturePalette0 + 2] == 0 &&
        raster_state[kRasterTextureBorderColor0 + 2] == 0;
    const bool pitch_tex5_diffuse_combiner =
        raster_state[kRasterTextureEnableMask] == 5 &&
        raster_state[kRasterShaderStageProgram] == 0 &&
        raster_state[kRasterShaderOtherStageInput] == 0 &&
        raster_state[kRasterDotRgbMapping] == 0 &&
        raster_state[kRasterShadowDepthFunc] == 7 &&
        raster_state[kRasterCombinerControl] == 0x00011101 &&
        raster_state[kRasterCombinerColorIcw0] == 0xC4200000 &&
        raster_state[kRasterCombinerAlphaIcw0] == 0xD4301010 &&
        raster_state[kRasterCombinerColorOcw0] == 0x000000C0 &&
        raster_state[kRasterCombinerAlphaOcw0] == 0x000000C0 &&
        raster_state[kRasterCombinerSpecularFog0] == 0x0000000C &&
        raster_state[kRasterCombinerSpecularFog1] == 0x00001C80 &&
        raster_state[kRasterCombinerFinalFactor0] == 0 &&
        raster_state[kRasterCombinerFinalFactor1] == 0;
    const bool inline_linear_blit_combiner =
        raster_state[kRasterTextureEnableMask] == 1 &&
        raster_state[kRasterShaderStageProgram] == 0x00000001 &&
        raster_state[kRasterShaderOtherStageInput] == 0x00210000 &&
        raster_state[kRasterDotRgbMapping] == 0 &&
        // PROJECT2D does not perform shadow comparison; both retained values
        // observed around the boot resolve are therefore equivalent.
        (raster_state[kRasterShadowDepthFunc] == 0 ||
         raster_state[kRasterShadowDepthFunc] == 7) &&
        raster_state[kRasterCombinerControl] == 0x00000001 &&
        raster_state[kRasterCombinerColorIcw0] == 0x08200000 &&
        raster_state[kRasterCombinerAlphaIcw0] == 0x14200000 &&
        raster_state[kRasterCombinerColorOcw0] == 0x00000C00 &&
        raster_state[kRasterCombinerAlphaOcw0] == 0x00000C00 &&
        raster_state[kRasterCombinerSpecularFog0] == 0x0000000C &&
        raster_state[kRasterCombinerSpecularFog1] == 0x00001C80 &&
        raster_state[kRasterCombinerFinalFactor0] == 0 &&
        raster_state[kRasterCombinerFinalFactor1] == 0;
    const bool inline_linear_blit_texture =
        raster_state[kRasterTextureFormat0] == 0x0001122A &&
        // PROJECT2D consumes U/V only; the witnessed P-axis WRAP/CLAMP
        // difference cannot affect this texture.
        (raster_state[kRasterTextureAddress0] == 0x00010303 ||
         raster_state[kRasterTextureAddress0] == 0x00030303) &&
        raster_state[kRasterTextureFilter0] == 0x02022000 &&
        raster_state[kRasterTextureControl0_0] == 0x4003FFC0 &&
        raster_state[kRasterTextureControl1_0] == 0x14000000 &&
        raster_state[kRasterTextureImageRect0] == 0x050001E0 &&
        raster_state[kRasterTexturePalette0] == 0 &&
        // The sampled U/V axes are CLAMP_TO_EDGE, so the two witnessed border
        // colors are output-inert.
        (raster_state[kRasterTextureBorderColor0] == 0 ||
         raster_state[kRasterTextureBorderColor0] == 0xFF000000);
    const bool inline_linear_blit_surface =
        primitive_mode == 5 && surface_type == 1 &&
        anti_aliasing == 0 && color_format == 8 && zeta_format == 2 &&
        surface_pitch == 0x0A000A00 &&
        surface_clip_horizontal == 0x02800000 &&
        surface_clip_vertical == 0x01E00000 &&
        enables == 0x0010u &&
        raster_state[kRasterControl0] == 0x00100001 &&
        raster_state[kRasterColorMask] == 0x01010101 &&
        raster_state[kRasterShadeMode] == 0x1D01 &&
        raster_state[kRasterFrontPolygonMode] == 0x1B02 &&
        raster_state[kRasterBackPolygonMode] == 0x1B02 &&
        raster_state[kRasterZMinMaxControl] == 1;
    const bool pitch_single_texture_combiner =
        raster_state[kRasterTextureEnableMask] == 1 &&
        raster_state[kRasterShaderStageProgram] == 0x00000001 &&
        raster_state[kRasterShaderOtherStageInput] == 0x00210000 &&
        raster_state[kRasterDotRgbMapping] == 0 &&
        raster_state[kRasterShadowDepthFunc] == 7 &&
        raster_state[kRasterCombinerControl] == 0x00000001 &&
        raster_state[kRasterCombinerColorIcw0] == 0x08200000 &&
        raster_state[kRasterCombinerAlphaIcw0] == 0x11200000 &&
        raster_state[kRasterCombinerColorOcw0] == 0x00000C00 &&
        raster_state[kRasterCombinerAlphaOcw0] == 0x00000C00 &&
        raster_state[kRasterCombinerFactor0_0] == 0xFFFFFFFF &&
        raster_state[kRasterCombinerFactor1_0] == 0xFFFFFFFF &&
        raster_state[kRasterCombinerSpecularFog0] == 0x0000000C &&
        raster_state[kRasterCombinerSpecularFog1] == 0x00001C80 &&
        raster_state[kRasterCombinerFinalFactor0] == 0 &&
        raster_state[kRasterCombinerFinalFactor1] == 0;
    // RS0 in probe-reduced-diag-6.4b: only t0 reaches R0; t1/t2 are
    // enabled cubemaps but their shader stages are unused by the one combiner.
    const uint32_t tex7_factor0 = raster_state[kRasterCombinerFactor0_0];
    const uint32_t tex7_factor1 = raster_state[kRasterCombinerFactor1_0];
    const bool tex7_fan_factor_rgb =
        tex7_factor0 == tex7_factor1 &&
        (tex7_factor0 & 0x00FFFFFFu) == 0x00A0FF40u &&
        (tex7_factor1 & 0x00FFFFFFu) == 0x00A0FF40u;
    const bool tex7_fan_combiner =
        raster_state[kRasterTextureEnableMask] == 7 &&
        raster_state[kRasterShaderStageProgram] == 0x00000C61 &&
        raster_state[kRasterShaderOtherStageInput] == 0x00210000 &&
        raster_state[kRasterDotRgbMapping] == 0 &&
        raster_state[kRasterShadowDepthFunc] == 7 &&
        raster_state[kRasterCombinerControl] == 1 &&
        raster_state[kRasterCombinerColorIcw0] == 0x08010000 &&
        raster_state[kRasterCombinerAlphaIcw0] == 0x11200000 &&
        raster_state[kRasterCombinerColorOcw0] == 0x00000C00 &&
        raster_state[kRasterCombinerAlphaOcw0] == 0x00000C00 &&
        tex7_fan_factor_rgb &&
        raster_state[kRasterCombinerSpecularFog0] == 0x0000000C &&
        raster_state[kRasterCombinerSpecularFog1] == 0x00001C80 &&
        raster_state[kRasterCombinerFinalFactor0] == 0xFF3F3F3F &&
        raster_state[kRasterCombinerFinalFactor1] == 0;
    const bool tex7_fan_texture_state =
        raster_state[kRasterTextureFormat0] == 0x0888062A &&
        raster_state[kRasterTextureAddress0] == 0x00030303 &&
        raster_state[kRasterTextureFilter0] == 0x02022000 &&
        raster_state[kRasterTextureControl0_0] == 0x4003FFC0 &&
        raster_state[kRasterTextureControl1_0] == 0x14000000 &&
        raster_state[kRasterTextureImageRect0] == 0x050001E0 &&
        raster_state[kRasterTexturePalette0] == 0 &&
        (raster_state[kRasterTextureBorderColor0] == 0 ||
         raster_state[kRasterTextureBorderColor0] == 0xFF000000) &&
        raster_state[kRasterTextureFormat0 + 1] == 0x0771072E &&
        raster_state[kRasterTextureFormat0 + 2] == 0x0771072E &&
        raster_state[kRasterTextureAddress0 + 1] == 0x00030303 &&
        raster_state[kRasterTextureAddress0 + 2] == 0x00030303 &&
        raster_state[kRasterTextureFilter0 + 1] == 0x02022000 &&
        raster_state[kRasterTextureFilter0 + 2] == 0x02022000 &&
        raster_state[kRasterTextureControl0_0 + 1] == 0x4003FFC0 &&
        raster_state[kRasterTextureControl0_0 + 2] == 0x4003FFC0 &&
        raster_state[kRasterTextureControl1_0 + 1] == 0 &&
        raster_state[kRasterTextureControl1_0 + 2] == 0 &&
        raster_state[kRasterTextureImageRect0 + 1] == 0 &&
        raster_state[kRasterTextureImageRect0 + 2] == 0 &&
        raster_state[kRasterTexturePalette0 + 1] == 0 &&
        raster_state[kRasterTexturePalette0 + 2] == 0 &&
        raster_state[kRasterTextureBorderColor0 + 1] == 0 &&
        raster_state[kRasterTextureBorderColor0 + 2] == 0;
    const bool pitch_additive_combiner =
        raster_state[kRasterTextureEnableMask] == 1 &&
        raster_state[kRasterShaderStageProgram] == 0x00000001 &&
        raster_state[kRasterShaderOtherStageInput] == 0x00210000 &&
        raster_state[kRasterDotRgbMapping] == 0 &&
        raster_state[kRasterCombinerControl] == 0x00000001 &&
        raster_state[kRasterCombinerColorIcw0] == 0x01080000 &&
        raster_state[kRasterCombinerAlphaIcw0] == 0x11180000 &&
        raster_state[kRasterCombinerColorOcw0] == 0x00000C00 &&
        raster_state[kRasterCombinerAlphaOcw0] == 0x00000C00 &&
        raster_state[kRasterCombinerFactor0_0] ==
            raster_state[kRasterCombinerFactor1_0] &&
        raster_state[kRasterCombinerSpecularFog0] == 0x0000000C &&
        raster_state[kRasterCombinerSpecularFog1] == 0x00001C80 &&
        raster_state[kRasterCombinerFinalFactor0] == 0 &&
        raster_state[kRasterCombinerFinalFactor1] == 0;
    // RS0/RS1 from probe-rs2-ohci-6.4b.  This intentionally names every
    // register which affects the generated Xemu register-combiner program;
    // adjacent shadow=0 states are a distinct, still-recorder-only family.
    const std::array<uint32_t, 6> texf_ci{{0xD920DA20, 0xCD20DB20,
                                            0xCDC10000, 0xC8CD0000,
                                            0xCC20C120, 0}};
    const std::array<uint32_t, 6> texf_ai{{0xD930DA30, 0xDD30DB30,
                                            0xDDD11010, 0, 0, 0x30301010}};
    const std::array<uint32_t, 6> texf_co{{0x00000D00, 0x00000D00,
                                            0x000000D0, 0x000000C0,
                                            0x00000C00, 0}};
    const std::array<uint32_t, 6> texf_ao{{0x00000D00, 0x00000D00,
                                            0x000000D0, 0, 0,
                                            0x000000C0}};
    const std::array<uint32_t, 6> texf_f0{{0, 0, 0xFF000000, 0, 0, 0}};
    bool texf_projected_fan_combiner =
        raster_state[kRasterTextureEnableMask] == 0xF &&
        raster_state[kRasterShaderStageProgram] == 0x00008421 &&
        raster_state[kRasterShaderOtherStageInput] == 0 &&
        raster_state[kRasterDotRgbMapping] == 0 &&
        raster_state[kRasterShadowDepthFunc] == 7 &&
        raster_state[kRasterCombinerControl] == 0x00011106 &&
        raster_state[kRasterCombinerSpecularFog0] == 0x0000000C &&
        raster_state[kRasterCombinerSpecularFog1] == 0x00001C80 &&
        raster_state[kRasterCombinerFinalFactor0] == 0 &&
        raster_state[kRasterCombinerFinalFactor1] == 0;
    for (size_t stage = 0; stage < texf_ci.size() &&
                           texf_projected_fan_combiner; ++stage) {
        texf_projected_fan_combiner =
            raster_state[kRasterCombinerColorIcw0 + stage] == texf_ci[stage] &&
            raster_state[kRasterCombinerAlphaIcw0 + stage] == texf_ai[stage] &&
            raster_state[kRasterCombinerColorOcw0 + stage] == texf_co[stage] &&
            raster_state[kRasterCombinerAlphaOcw0 + stage] == texf_ao[stage] &&
            raster_state[kRasterCombinerFactor0_0 + stage] == texf_f0[stage] &&
            raster_state[kRasterCombinerFactor1_0 + stage] == 0;
    }
    const uint32_t texf_t0_offset = raster_state[kRasterTextureOffset0];
    const bool texf_projected_fan_textures =
        (texf_t0_offset == 0x02E05000 || texf_t0_offset == 0x02C05000) &&
        raster_state[kRasterTextureFormat0] == 0x09A1062A &&
        raster_state[kRasterTextureAddress0] == 0x00030303 &&
        raster_state[kRasterTextureFilter0] == 0x02022000 &&
        raster_state[kRasterTextureControl0_0] == 0x4003FFC0 &&
        raster_state[kRasterTextureControl1_0] == 0x14000000 &&
        raster_state[kRasterTextureImageRect0] == 0x050001E0 &&
        raster_state[kRasterTexturePalette0] == 0 &&
        raster_state[kRasterTextureBorderColor0] == 0 &&
        raster_state[kRasterTextureOffset0 + 1] == 0x029F3000 &&
        raster_state[kRasterTextureOffset0 + 2] == 0x029E3000 &&
        raster_state[kRasterTextureOffset0 + 3] == 0x029D3000 &&
        raster_state[kRasterTextureFormat0 + 1] == 0x0881192A &&
        raster_state[kRasterTextureFormat0 + 2] == 0x0881192A &&
        raster_state[kRasterTextureFormat0 + 3] == 0x0881192A &&
        raster_state[kRasterTextureAddress0 + 1] == 0x00030101 &&
        raster_state[kRasterTextureAddress0 + 2] == 0x00030101 &&
        raster_state[kRasterTextureAddress0 + 3] == 0x00030101 &&
        raster_state[kRasterTextureFilter0 + 1] == 0x02022000 &&
        raster_state[kRasterTextureFilter0 + 2] == 0x02022000 &&
        raster_state[kRasterTextureFilter0 + 3] == 0x02022000 &&
        raster_state[kRasterTextureControl0_0 + 1] == 0x4003FFC0 &&
        raster_state[kRasterTextureControl0_0 + 2] == 0x4003FFC0 &&
        raster_state[kRasterTextureControl0_0 + 3] == 0x4003FFC0 &&
        raster_state[kRasterTextureControl1_0 + 1] == 0 &&
        raster_state[kRasterTextureControl1_0 + 2] == 0 &&
        raster_state[kRasterTextureControl1_0 + 3] == 0x04000000 &&
        raster_state[kRasterTextureImageRect0 + 1] == 0 &&
        raster_state[kRasterTextureImageRect0 + 2] == 0 &&
        raster_state[kRasterTextureImageRect0 + 3] == 0x02000200 &&
        raster_state[kRasterTexturePalette0 + 1] == 0 &&
        raster_state[kRasterTexturePalette0 + 2] == 0 &&
        raster_state[kRasterTexturePalette0 + 3] == 0 &&
        raster_state[kRasterTextureBorderColor0 + 1] == 0 &&
        raster_state[kRasterTextureBorderColor0 + 2] == 0 &&
        raster_state[kRasterTextureBorderColor0 + 3] == 0xFFFFFFFF;
    const bool texf_projected_fan_bounds =
        raster_state[kRasterWindowClipHorizontal] == 0x04FF0000 &&
        raster_state[kRasterWindowClipVertical] == 0x01DF0000 &&
        raster_state[kRasterClipMin] == 0 &&
        raster_state[kRasterClipMax] == 0x4B7FFFFF;

    Profile profile = Profile::kNone;
    if (depth_only && surface_type == 1 && anti_aliasing == 0 &&
        zeta_format == 1 && supported_primitive &&
        raster_state[kRasterTextureEnableMask] == 0) {
        profile = Profile::kPitchZ16Depth;
    } else if (radial_combiner) {
        profile = Profile::kSwizzledRadialColor;
    } else if (textured_combiner && surface_type == 1 &&
               raster_state[kRasterShadowDepthFunc] == 4) {
        profile = Profile::kPitchTexturedColor;
    } else if (textured_combiner && textured_fixed_stage6 &&
               surface_type == 2 &&
               raster_state[kRasterShadowDepthFunc] == 0) {
        profile = Profile::kSwizzledTexturedColor;
    } else if (projected_combiner && surface_type == 1 &&
               raster_state[kRasterShadowDepthFunc] == 4) {
        profile = Profile::kPitchProjectedColor;
    } else if (projected_combiner && projected_fixed_stage5 &&
               surface_type == 2 &&
               raster_state[kRasterShadowDepthFunc] == 0) {
        profile = Profile::kSwizzledProjectedColor;
    } else if (pitch_multi_texture_combiner) {
        profile = Profile::kPitchMultiTextureColor;
    } else if (pitch_three_texture_combiner &&
               pitch_three_texture_state) {
        profile = Profile::kPitchThreeTextureColor;
    } else if (pitch_tex5_projected_combiner &&
               pitch_tex5_projected_texture &&
               pitch_tex5_raster) {
        profile = Profile::kPitchTex5ProjectedColor;
    } else if (pitch_tex5_diffuse_combiner &&
               pitch_tex5_raster) {
        profile = Profile::kPitchTex5DiffuseColor;
    } else if (inline_linear_blit_combiner &&
               inline_linear_blit_texture &&
               inline_linear_blit_surface) {
        profile = Profile::kInlineLinearBlitColor;
    } else if (pitch_single_texture_combiner) {
        profile = Profile::kPitchSingleTextureColor;
    } else if (tex7_fan_combiner && tex7_fan_texture_state) {
        profile = Profile::kTex7FanColor;
    } else if (texf_projected_fan_combiner &&
               texf_projected_fan_textures &&
               texf_projected_fan_bounds) {
        profile = Profile::kTexfProjectedFanColor;
    } else if (pitch_additive_combiner) {
        profile = Profile::kPitchAdditiveColor;
    } else {
        // No hand-derived Profile fingerprint matches. Rather than recording this
        // batch (executed=false, zero fragment writes), decode and evaluate the
        // actual bound combiner state generically. All witnessed profiles above
        // are pinned evidence and are matched first; this path is a strict
        // fallback that never overrides them.
        std::optional<perf::ScopedAdd> t;
        if (perf::g_enabled) {
            t.emplace(perf::g_ns_raster_generic, perf::g_calls_raster_generic);
        }
        // Stage (e) ordering fence: the generic path reaches the GL backend and
        // the resident-surface protocol (F3) and is never threadable, so it runs
        // here. It writes colour and zeta -- and it is what the threads=1
        // byte-lock caught overtaking queued jobs, moving the read-only-depth
        // profiles' depth-pass counts.
        draw_order_fence();
        return rasterize_generic_combiner_batch(job, executed);
    }
    // ---- phase 7: the witnessed-family reroute ---------------------------
    // A profile matched, and it is one of the EIGHT families that still
    // rasterize on the CPU on plane 0x03628000 (the phase-5 plane census).
    // Sending it down the strict generic fallback is exactly what the ladder's
    // own `else` comment describes -- the generic evaluator decodes the ACTUAL
    // bound combiner state, and every witnessed profile is a specialization of
    // it, so the two produce the same fragments -- and the generated executor
    // then takes it on the GPU with the resident colour AND zeta planes.
    //
    // It is deliberately NOT eight hand-written GPU fixtures. The plane's
    // residency breaks at its FIRST CPU consumer, so all eight have to move at
    // once or none of them is worth a resolve, and eight more pinned shaders
    // would have to be right eight times over. One generator, one census.
    //
    // Gated on the generated executor AND on its depth support: without either
    // of them the reroute would land these batches in the SOFTWARE generic
    // evaluator, which is the same pixels through a slower loop. Both negative
    // spellings therefore restore the ladder exactly.
    const int witnessed_reroute_bit = generic_shader_witnessed_bit(profile);
    if (witnessed_reroute_bit >= 0 && gpu_generic_shader_ &&
        gpu_generic_shader_depth_ && renderer_ &&
        (gpu_generic_shader_witnessed_ &
         (uint32_t{1} << witnessed_reroute_bit)) != 0) {
        std::optional<perf::ScopedAdd> t;
        if (perf::g_enabled) {
            t.emplace(perf::g_ns_raster_generic, perf::g_calls_raster_generic);
        }
        ++generic_shader_witnessed_reroutes_;
        // Same fence the `else` arm takes, and for the same reason: the
        // generic path reaches the GL backend and the resident-surface
        // protocol and is never threadable.
        draw_order_fence();
        return rasterize_generic_combiner_batch(job, executed);
    }
#if XLLE_VSH_GPU_RESIDENT
    if (!materialize_deferred_gpu_vsh(job)) return false;
#endif
    executed = true;
    // Stage (d): the ladder result is part of the job identity.
    job.profile = static_cast<uint32_t>(profile);
    std::optional<perf::ScopedAdd> witnessed_profile_timer;
    if (perf::g_enabled) {
        witnessed_profile_timer.emplace(
            perf::g_ns_witnessed_profile[static_cast<size_t>(profile)],
            perf::g_calls_witnessed_profile[static_cast<size_t>(profile)]);
    }
    const bool& pitch_tex5_profile = job.pitch_tex5_profile =
        profile == Profile::kPitchTex5ProjectedColor ||
        profile == Profile::kPitchTex5DiffuseColor;

    if (profile == Profile::kPitchZ16Depth) {
        constexpr uint32_t kSupportedEnables =
            (1u << 2) | (1u << 3) | (1u << 13);
        if ((enables & ~kSupportedEnables) != 0 ||
            (enables & (1u << 2)) == 0 ||
            raster_state[kRasterDepthFunc] != 0x0203 ||
            raster_state[kRasterCullFace] != 0x0405 ||
            raster_state[kRasterFrontFace] != 0x0900 ||
            raster_state[kRasterFrontPolygonMode] != 0x1B02 ||
            raster_state[kRasterBackPolygonMode] != 0x1B02 ||
            (raster_state[kRasterControl0] &
             ((1u << 12) | (1u << 16))) != 0 ||
            (raster_state[kRasterZMinMaxControl] & 0xF0u) != 0) {
            fault_("NV2A depth-only draw exceeds the witnessed raster subset");
            return false;
        }
    } else if (profile == Profile::kPitchMultiTextureColor) {
        if (primitive_mode != 6 || surface_type != 1 ||
            anti_aliasing != 0 || color_format != 8 || zeta_format != 2 ||
            enables != 0x000Fu ||
            raster_state[kRasterControl0] != 0x00100001 ||
            raster_state[kRasterAlphaFunc] != 0x0206 ||
            (raster_state[kRasterAlphaRef] & 0xFFu) != 1 ||
            raster_state[kRasterBlendSrc] != 0x0302 ||
            raster_state[kRasterBlendDst] != 0x0303 ||
            raster_state[kRasterBlendEquation] != 0x8006 ||
            raster_state[kRasterDepthFunc] != 0x0203 ||
            raster_state[kRasterColorMask] != 0x01010101 ||
            raster_state[kRasterDepthMask] != 1 ||
            raster_state[kRasterShadeMode] != 0x1D01 ||
            raster_state[kRasterFrontPolygonMode] != 0x1B02 ||
            raster_state[kRasterBackPolygonMode] != 0x1B02 ||
            raster_state[kRasterCullFace] != 0x0405 ||
            raster_state[kRasterFrontFace] != 0x0900 ||
            raster_state[kRasterZMinMaxControl] != 1 ||
            raster_state[kRasterPolygonOffsetScale] != 0x40800000 ||
            raster_state[kRasterPolygonOffsetBias] != 0x41200000) {
            fault_("NV2A pitch multi-texture draw exceeds the witnessed raster subset");
            return false;
        }
    } else if (profile == Profile::kPitchThreeTextureColor) {
        if (primitive_mode != 6 || surface_type != 1 ||
            anti_aliasing != 0 || color_format != 8 || zeta_format != 2 ||
            surface_pitch != 0x14001400 ||
            surface_clip_horizontal != 0x05000000 ||
            surface_clip_vertical != 0x01E00000 ||
            enables != 0x000Eu ||
            raster_state[kRasterControl0] != 0x00100001 ||
            raster_state[kRasterAlphaFunc] != 0x0206 ||
            (raster_state[kRasterAlphaRef] & 0xFFu) != 1 ||
            raster_state[kRasterBlendSrc] != 0x0302 ||
            raster_state[kRasterBlendDst] != 0x0303 ||
            raster_state[kRasterBlendEquation] != 0x8006 ||
            raster_state[kRasterDepthFunc] != 0x0203 ||
            raster_state[kRasterColorMask] != 0x01010101 ||
            raster_state[kRasterDepthMask] != 1 ||
            raster_state[kRasterShadeMode] != 0x1D01 ||
            raster_state[kRasterFrontPolygonMode] != 0x1B02 ||
            raster_state[kRasterBackPolygonMode] != 0x1B02 ||
            raster_state[kRasterCullFace] != 0x0405 ||
            raster_state[kRasterFrontFace] != 0x0900 ||
            raster_state[kRasterZMinMaxControl] != 1 ||
            raster_state[kRasterPolygonOffsetScale] != 0x40800000 ||
            raster_state[kRasterPolygonOffsetBias] != 0x41200000) {
            fault_("NV2A pitch three-texture draw exceeds the witnessed raster subset");
            return false;
        }
    } else if (profile == Profile::kPitchTex5ProjectedColor ||
               profile == Profile::kPitchTex5DiffuseColor) {
        if (!pitch_tex5_raster ||
            (profile == Profile::kPitchTex5ProjectedColor &&
             !pitch_tex5_projected_texture)) {
            fault_("NV2A pitch tex5 draw exceeds the witnessed raster subset");
            return false;
        }
    } else if (profile == Profile::kInlineLinearBlitColor) {
        if (primitive_mode != 5 || surface_type != 1 ||
            anti_aliasing != 0 || color_format != 8 || zeta_format != 2 ||
            surface_pitch != 0x0A000A00 ||
            surface_clip_horizontal != 0x02800000 ||
            surface_clip_vertical != 0x01E00000 ||
            enables != 0x0010u ||
            raster_state[kRasterControl0] != 0x00100001 ||
            raster_state[kRasterColorMask] != 0x01010101 ||
            raster_state[kRasterShadeMode] != 0x1D01 ||
            raster_state[kRasterFrontPolygonMode] != 0x1B02 ||
            raster_state[kRasterBackPolygonMode] != 0x1B02 ||
            raster_state[kRasterZMinMaxControl] != 1) {
            fault_("NV2A inline linear-blit draw exceeds the witnessed raster subset");
            return false;
        }
    } else if (profile == Profile::kPitchSingleTextureColor) {
        if (primitive_mode != 7 || surface_type != 1 ||
            anti_aliasing != 0 || color_format != 8 || zeta_format != 2 ||
            enables != 0x000Eu ||
            raster_state[kRasterControl0] != 0x00100001 ||
            raster_state[kRasterBlendSrc] != 1 ||
            raster_state[kRasterBlendDst] != 0 ||
            raster_state[kRasterBlendEquation] != 0x8006 ||
            raster_state[kRasterDepthFunc] != 0x0203 ||
            raster_state[kRasterColorMask] != 0x01010101 ||
            raster_state[kRasterDepthMask] != 0 ||
            raster_state[kRasterShadeMode] != 0x1D01 ||
            raster_state[kRasterFrontPolygonMode] != 0x1B02 ||
            raster_state[kRasterBackPolygonMode] != 0x1B02 ||
            raster_state[kRasterCullFace] != 0x0405 ||
            raster_state[kRasterFrontFace] != 0x0900 ||
            raster_state[kRasterZMinMaxControl] != 1) {
            fault_("NV2A pitch single-texture draw exceeds the witnessed raster subset");
            return false;
        }
    } else if (profile == Profile::kTex7FanColor) {
        if (primitive_mode != 7 || surface_type != 1 ||
            anti_aliasing != 0 || color_format != 8 || zeta_format != 2 ||
            surface_pitch != 0x14001400 ||
            surface_clip_horizontal != 0x05000000 ||
            surface_clip_vertical != 0x01E00000 || enables != 0x000Eu ||
            raster_state[kRasterControl0] != 0x00100001 ||
            raster_state[kRasterAlphaFunc] != 0x0206 ||
            (raster_state[kRasterAlphaRef] & 0xFFu) != 1 ||
            raster_state[kRasterBlendSrc] != 0x0302 ||
            raster_state[kRasterBlendDst] != 1 ||
            raster_state[kRasterBlendEquation] != 0x8006 ||
            raster_state[kRasterDepthFunc] != 0x0203 ||
            raster_state[kRasterColorMask] != 0x01010101 ||
            raster_state[kRasterDepthMask] != 0 ||
            raster_state[kRasterShadeMode] != 0x1D01 ||
            raster_state[kRasterFrontPolygonMode] != 0x1B02 ||
            raster_state[kRasterBackPolygonMode] != 0x1B02 ||
            raster_state[kRasterCullFace] != 0x0405 ||
            raster_state[kRasterFrontFace] != 0x0900 ||
            raster_state[kRasterZMinMaxControl] != 1) {
            fault_("NV2A tex7 fan draw exceeds the witnessed RS0 subset");
            return false;
        }
    } else if (profile == Profile::kTexfProjectedFanColor) {
        if (primitive_mode != 7 || surface_type != 1 || anti_aliasing != 0 ||
            color_format != 8 || zeta_format != 2 ||
            surface_pitch != 0x14001400 ||
            surface_clip_horizontal != 0x05000000 ||
            surface_clip_vertical != 0x01E00000 || enables != 0x000Fu ||
            raster_state[kRasterWindowClipHorizontal] != 0x04FF0000 ||
            raster_state[kRasterWindowClipVertical] != 0x01DF0000 ||
            raster_state[kRasterClipMin] != 0 ||
            raster_state[kRasterClipMax] != 0x4B7FFFFF ||
            raster_state[kRasterControl0] != 0x00100001 ||
            raster_state[kRasterAlphaFunc] != 0x0206 ||
            (raster_state[kRasterAlphaRef] & 0xFFu) != 1 ||
            raster_state[kRasterBlendSrc] != 0x0302 ||
            raster_state[kRasterBlendDst] != 1 ||
            raster_state[kRasterBlendEquation] != 0x8006 ||
            raster_state[kRasterDepthFunc] != 0x0207 ||
            raster_state[kRasterColorMask] != 0x01010101 ||
            raster_state[kRasterDepthMask] != 0 ||
            raster_state[kRasterShadeMode] != 0x1D01 ||
            raster_state[kRasterFrontPolygonMode] != 0x1B02 ||
            raster_state[kRasterBackPolygonMode] != 0x1B02 ||
            raster_state[kRasterCullFace] != 0x0405 ||
            raster_state[kRasterFrontFace] != 0x0900 ||
            raster_state[kRasterZMinMaxControl] != 1) {
            fault_("NV2A tex=f projected fan draw exceeds RS0/RS1 subset");
            return false;
        }
    } else if (profile == Profile::kPitchAdditiveColor) {
        if (!supported_primitive || surface_type != 1 ||
            anti_aliasing != 0 || color_format != 8 || zeta_format != 2 ||
            enables != 0x000Fu ||
            raster_state[kRasterControl0] != 0x00100001 ||
            raster_state[kRasterAlphaFunc] != 0x0206 ||
            (raster_state[kRasterAlphaRef] & 0xFFu) != 1 ||
            raster_state[kRasterBlendSrc] != 0x0302 ||
            raster_state[kRasterBlendDst] != 1 ||
            raster_state[kRasterBlendEquation] != 0x8006 ||
            (raster_state[kRasterDepthFunc] != 0x0202 &&
             raster_state[kRasterDepthFunc] != 0x0207) ||
            raster_state[kRasterColorMask] != 0x01010101 ||
            raster_state[kRasterDepthMask] != 0 ||
            raster_state[kRasterShadeMode] != 0x1D01 ||
            raster_state[kRasterFrontPolygonMode] != 0x1B02 ||
            raster_state[kRasterBackPolygonMode] != 0x1B02 ||
            raster_state[kRasterCullFace] != 0x0405 ||
            raster_state[kRasterFrontFace] != 0x0900 ||
            raster_state[kRasterZMinMaxControl] != 1) {
            char why[640];
            std::snprintf(
                why, sizeof(why),
                "NV2A inline-color draw exceeds the witnessed raster subset "
                "(prim=%x sfmt=%08x en=%08x ctrl=%08x alpha=%x/%02x "
                "blend=%x/%x/%x depth=%x/%x mask=%08x "
                "shade=%x poly=%x/%x cull=%x/%x zctl=%08x)",
                primitive_mode, surface_format, enables,
                raster_state[kRasterControl0],
                raster_state[kRasterAlphaFunc],
                raster_state[kRasterAlphaRef] & 0xFFu,
                raster_state[kRasterBlendSrc],
                raster_state[kRasterBlendDst],
                raster_state[kRasterBlendEquation],
                raster_state[kRasterDepthFunc],
                raster_state[kRasterDepthMask],
                raster_state[kRasterColorMask],
                raster_state[kRasterShadeMode],
                raster_state[kRasterFrontPolygonMode],
                raster_state[kRasterBackPolygonMode],
                raster_state[kRasterCullFace],
                raster_state[kRasterFrontFace],
                raster_state[kRasterZMinMaxControl]);
            fault_(why);
            return false;
        }
    } else {
        const bool witnessed_surface_type =
            (profile == Profile::kPitchTexturedColor ||
             profile == Profile::kPitchProjectedColor)
                ? surface_type == 1
                : surface_type == 2;
        const bool witnessed_depth_mask =
            (profile == Profile::kPitchTexturedColor ||
             profile == Profile::kPitchProjectedColor)
                ? (raster_state[kRasterDepthMask] == 1 ||
                   raster_state[kRasterDepthMask] == 0xFFFFFFFFu)
                : raster_state[kRasterDepthMask] == 1;
        const bool common_color_state =
            supported_primitive && witnessed_surface_type &&
            anti_aliasing == 0 && color_format == 8 && zeta_format == 2 &&
            raster_state[kRasterControl0] == 0x00100001 &&
            raster_state[kRasterBlendSrc] == 1 &&
            raster_state[kRasterBlendDst] == 0 &&
            raster_state[kRasterBlendEquation] == 0x8006 &&
            raster_state[kRasterDepthFunc] == 0x0203 &&
            raster_state[kRasterColorMask] == 0x01010101 &&
            witnessed_depth_mask &&
            raster_state[kRasterShadeMode] == 0x1D01 &&
            raster_state[kRasterFrontPolygonMode] == 0x1B02 &&
            raster_state[kRasterBackPolygonMode] == 0x1B02 &&
            raster_state[kRasterCullFace] == 0x0405 &&
            raster_state[kRasterFrontFace] == 0x0900 &&
            raster_state[kRasterZMinMaxControl] == 1;
        const bool radial_state =
            profile == Profile::kSwizzledRadialColor &&
            enables == 0x000Fu &&
            raster_state[kRasterAlphaFunc] == 0x0206 &&
            (raster_state[kRasterAlphaRef] & 0xFFu) == 1;
        const bool textured_state =
            (profile == Profile::kSwizzledTexturedColor ||
             profile == Profile::kSwizzledProjectedColor) &&
            enables == 0x000Cu &&
            raster_state[kRasterAlphaFunc] == 0x0207 &&
            (raster_state[kRasterAlphaRef] & 0xFFu) == 0;
        const bool pitch_textured_state =
            (profile == Profile::kPitchTexturedColor ||
             profile == Profile::kPitchProjectedColor) &&
            enables == 0x000Cu &&
            raster_state[kRasterAlphaFunc] == 0x0206 &&
            (raster_state[kRasterAlphaRef] & 0xFFu) == 1;
        if (!common_color_state ||
            (!radial_state && !textured_state && !pitch_textured_state)) {
            fault_("NV2A color draw exceeds the witnessed raster subset");
            return false;
        }
    }

    // Stage (d): the derived surface descriptors of DESIGN-NV2A-SEAM.md
    // section 1.2 become DrawJob fields. Declared as references bound to the
    // job so that every use site below is textually unchanged and the storage
    // the executor reads is the job's, not the frame's stack.
    uint32_t& clip_x = job.clip_x;
    uint32_t& clip_width = job.clip_width;
    uint32_t& clip_y = job.clip_y;
    uint32_t& clip_height = job.clip_height;
    clip_x = surface_clip_horizontal & 0xFFFFu;
    clip_width = surface_clip_horizontal >> 16;
    clip_y = surface_clip_vertical & 0xFFFFu;
    clip_height = surface_clip_vertical >> 16;
    if (!clip_width || !clip_height) {
        fault_("NV2A raster surface has an empty clip");
        return false;
    }

    bool& inline_depth_test = job.inline_depth_test;
    inline_depth_test =
        profile == Profile::kPitchAdditiveColor &&
        raster_state[kRasterDepthFunc] == 0x0202;
    uint32_t& surface_width = job.surface_width;
    uint32_t& surface_height = job.surface_height;
    uint32_t& pitch = job.pitch;
    uint64_t& depth_bytes64 = job.depth_bytes;
    uint64_t& color_bytes64 = job.color_bytes;
    if (profile == Profile::kPitchZ16Depth) {
        pitch = surface_pitch >> 16;
        surface_width = pitch / 2;
        surface_height = clip_y + clip_height;
        depth_bytes64 = static_cast<uint64_t>(pitch) * surface_height;
        if (!pitch ||
            static_cast<uint64_t>(clip_x + clip_width) * 2 > pitch) {
            fault_("NV2A depth-only raster surface is invalid");
            return false;
        }
    } else if (profile == Profile::kPitchAdditiveColor ||
               profile == Profile::kPitchTexturedColor ||
               profile == Profile::kPitchProjectedColor ||
               profile == Profile::kPitchMultiTextureColor ||
               profile == Profile::kPitchThreeTextureColor ||
               profile == Profile::kPitchTex5ProjectedColor ||
               profile == Profile::kPitchTex5DiffuseColor ||
               profile == Profile::kInlineLinearBlitColor ||
               profile == Profile::kPitchSingleTextureColor ||
               profile == Profile::kTex7FanColor ||
               profile == Profile::kTexfProjectedFanColor) {
        pitch = surface_pitch & 0xFFFFu;
        surface_width = pitch / 4;
        surface_height = clip_y + clip_height;
        color_bytes64 = static_cast<uint64_t>(pitch) * surface_height;
        if (inline_depth_test ||
            profile == Profile::kPitchTexturedColor ||
            profile == Profile::kPitchProjectedColor ||
            profile == Profile::kPitchMultiTextureColor ||
            profile == Profile::kPitchThreeTextureColor ||
            profile == Profile::kPitchTex5ProjectedColor ||
            profile == Profile::kPitchTex5DiffuseColor ||
            profile == Profile::kPitchSingleTextureColor ||
            profile == Profile::kTex7FanColor ||
            profile == Profile::kTexfProjectedFanColor)
            depth_bytes64 = color_bytes64;
        if (!pitch || (surface_pitch >> 16) != pitch ||
            static_cast<uint64_t>(clip_x + clip_width) * 4 > pitch) {
            fault_("NV2A color pitch surface is invalid");
            return false;
        }
    } else {
        const uint32_t width_log2 = (surface_format >> 16) & 0xFFu;
        const uint32_t height_log2 = surface_format >> 24;
        if (width_log2 >= 16 || height_log2 >= 16) {
            fault_("NV2A color swizzled dimensions are invalid");
            return false;
        }
        surface_width = 1u << width_log2;
        surface_height = 1u << height_log2;
        pitch = surface_width * 4;
        depth_bytes64 = color_bytes64 =
            static_cast<uint64_t>(surface_width) * surface_height * 4;
        if (clip_x + clip_width > surface_width ||
            clip_y + clip_height > surface_height ||
            (surface_pitch & 0xFFFFu) != pitch ||
            (surface_pitch >> 16) != pitch) {
            fault_("NV2A color swizzled surface is invalid");
            return false;
        }
    }
    bool& uses_depth_surface = job.uses_depth;
    uses_depth_surface =
        profile == Profile::kPitchAdditiveColor
            ? inline_depth_test
            : profile != Profile::kInlineLinearBlitColor;
    bool& writes_depth_surface = job.writes_depth;
    writes_depth_surface =
        uses_depth_surface && raster_state[kRasterDepthMask] != 0;
    if ((uses_depth_surface && !depth_bytes64) ||
        depth_bytes64 > SIZE_MAX || color_bytes64 > SIZE_MAX) {
        fault_("NV2A raster surface size overflows");
        return false;
    }

    auto resolve_surface =
        [&](uint32_t dma, bool bound, uint32_t offset, uint64_t bytes,
            uint32_t& address, const char* plane) {
            if (!bound || !bytes ||
                static_cast<uint64_t>(offset) + bytes - 1 > UINT32_MAX) {
                char why[128];
                std::snprintf(why, sizeof(why),
                              "NV2A raster %s surface is unbound or overflows",
                              plane);
                fault_(why);
                return false;
            }
            uint32_t last_address = 0;
            const uint32_t last_offset =
                offset + static_cast<uint32_t>(bytes - 1);
            if (!dma_object_address(dma, offset, address) ||
                !dma_object_address(dma, last_offset, last_address) ||
                last_address < address ||
                static_cast<uint64_t>(last_address) - address + 1 != bytes) {
                char why[128];
                std::snprintf(why, sizeof(why),
                              "NV2A raster %s DMA range is invalid", plane);
                fault_(why);
                return false;
            }
            // Seam anchor 6 (F6): every texture snapshot in this function is
            // resolved through here. A snapshot whose source span intersects a
            // pending job's DESTINATION span is render-to-texture inside one
            // frame and would have to drain first. The job's own two destination
            // planes are not sources, so they are excluded by name.
            if (std::strcmp(plane, "zeta") != 0 &&
                std::strcmp(plane, "color") != 0) {
                seam_texture_source_anchor(address, bytes);
                // F6, and the hole stage (f) opened: a witnessed texture
                // snapshot reads guest RAM directly, so if the source span is a
                // surface the GPU executor currently owns, the snapshot reads a
                // stale image. Measured: the radial destination 0x02E05000 IS a
                // texture source for other profiles in the boot animation, and
                // without this the 3.65 G - 4.05 G checkpoints differ by up to
                // 24 LSB over 80-90 percent of their pixels. The generic
                // combiner path has always resolved before its texture reads
                // (nv2a.cpp "generic texture read"); the witnessed path did not
                // need to while nothing it drew was ever resident.
                // beads-xbox.1.15.10 phase 4 (lever B) CEILING PROBE.
                // --nv2a-gpu-unsafe-skip-texture-resolve is a DIAGNOSTIC and is
                // KNOWN INCORRECT: it makes the snapshot read a stale image
                // exactly as the pre-phase-1 code did (the bead records the
                // symptom: 3.65-4.05 G differ by up to 24 LSB over 80-90 pct of
                // their pixels). It exists to put an upper bound on what any
                // correct implementation of "sample the resident surface
                // instead of round-tripping it" could ever be worth, WITHOUT
                // first building the Morton-inverting shader-side sampler that
                // implementation needs. Default off; never a shipping path.
                ++witnessed_texture_source_resolves_;
                if (gpu_unsafe_skip_texture_resolve_) {
                    ++witnessed_texture_source_skipped_;
                    return true;
                }
                // Phase 5 item 2: attribute THIS call site apart. The flag is a
                // plain bool on the emulation thread, set only across the call.
                resolve_query_from_texture_source_ = true;
                const bool texture_resolve_ok = resolve_renderer_color_overlap(
                    address, static_cast<size_t>(bytes),
                    "witnessed texture snapshot");
                resolve_query_from_texture_source_ = false;
                if (!texture_resolve_ok) return false;
            }
            return true;
        };

    uint32_t& depth_address = job.depth_address;
    uint32_t& color_address = job.color_address;
    bool& color_profile = job.color_profile;
    color_profile = profile != Profile::kPitchZ16Depth;
    if (uses_depth_surface &&
        !resolve_surface(zeta_dma_, zeta_dma_bound_, surface_zeta_offset,
                         depth_bytes64, depth_address, "zeta")) {
        return false;
    }
    if (color_profile &&
        !resolve_surface(color_dma_, color_dma_bound_, surface_color_offset,
                         color_bytes64, color_address, "color")) {
        return false;
    }
    if (color_profile && uses_depth_surface) {
        const uint64_t color_end =
            static_cast<uint64_t>(color_address) + color_bytes64;
        const uint64_t depth_end =
            static_cast<uint64_t>(depth_address) + depth_bytes64;
        if (color_address < depth_end && depth_address < color_end) {
            fault_("NV2A color and zeta surfaces overlap");
            return false;
        }
    }

    if (profile == Profile::kTexfProjectedFanColor) {
        // Texture values cannot affect this exact combiner result, but its
        // four enabled PROJECT2D images still require valid DMA-B ranges.
        // Resolve their full format-derived extents without copying data.
        for (size_t stage = 0; stage < 4; ++stage) {
            const uint32_t format = raster_state[kRasterTextureFormat0 + stage];
            const uint32_t width = 1u << ((format >> 20) & 0xFu);
            const uint32_t height = 1u << ((format >> 24) & 0xFu);
            const uint64_t bytes = static_cast<uint64_t>(width) * height * 4;
            uint32_t address = 0;
            if (bytes > SIZE_MAX ||
                !resolve_surface(texture_dma_b_, texture_dma_b_bound_,
                                 raster_state[kRasterTextureOffset0 + stage],
                                 bytes, address, "tex=f texture")) {
                return false;
            }
        }
    }

    // Stage (d): the texture snapshots are the job's bytes. Declaring them
    // here as references to the job fields is the "move, do not copy" of
    // DESIGN-NV2A-SEAM.md section 1.2 -- the bytes are read straight into the
    // job, so the latch costs no extra copy at all.
    std::vector<uint8_t>& texture_data = job.texture_data;
    std::unique_ptr<SwizzleLookup>& texture_swizzle = job.texture_swizzle;
    uint32_t& texture_width = job.texture_width;
    uint32_t& texture_height = job.texture_height;
    std::vector<uint8_t>& projected_texture_data = job.projected_texture_data;
    std::unique_ptr<SwizzleLookup>& projected_texture_swizzle =
        job.projected_texture_swizzle;
    uint32_t& projected_texture_width = job.projected_texture_width;
    uint32_t& projected_texture_height = job.projected_texture_height;
    const bool sampled_color_profile =
        profile == Profile::kSwizzledTexturedColor ||
        profile == Profile::kPitchTexturedColor ||
        profile == Profile::kSwizzledProjectedColor ||
        profile == Profile::kPitchProjectedColor;
    if (sampled_color_profile) {
        const uint32_t format1 = raster_state[kRasterTextureFormat0 + 1];
        const uint32_t format2 = raster_state[kRasterTextureFormat0 + 2];
        const uint32_t offset1 = raster_state[kRasterTextureOffset0 + 1];
        const uint32_t offset2 = raster_state[kRasterTextureOffset0 + 2];
        const bool witnessed_texture =
            format1 == format2 &&
            (format1 == 0x0661072E || format1 == 0x0771072E) &&
            offset1 == offset2 &&
            raster_state[kRasterTextureAddress0 + 1] == 0x00030303 &&
            raster_state[kRasterTextureAddress0 + 2] == 0x00030303 &&
            raster_state[kRasterTextureFilter0 + 1] == 0x02022000 &&
            raster_state[kRasterTextureFilter0 + 2] == 0x02022000 &&
            raster_state[kRasterTextureControl0_0 + 1] == 0x4003FFC0 &&
            raster_state[kRasterTextureControl0_0 + 2] == 0x4003FFC0 &&
            raster_state[kRasterTexturePalette0 + 1] == 0 &&
            raster_state[kRasterTexturePalette0 + 2] == 0;
        if (!witnessed_texture || (format1 & 3u) != 2u) {
            fault_("NV2A textured-color texture state exceeds the witnessed subset");
            return false;
        }

        texture_width = 1u << ((format1 >> 20) & 0xFu);
        texture_height = 1u << ((format1 >> 24) & 0xFu);
        const uint64_t texture_bytes64 =
            static_cast<uint64_t>(texture_width) * texture_height * 4;
        uint32_t texture_address = 0;
        if (texture_bytes64 > SIZE_MAX ||
            !resolve_surface(texture_dma_b_, texture_dma_b_bound_, offset1,
                             texture_bytes64, texture_address, "texture")) {
            return false;
        }
        texture_data.resize(static_cast<size_t>(texture_bytes64));
        if (!mem_read_ ||
            !mem_read_(texture_address, texture_data.data(), texture_data.size())) {
            fault_("NV2A textured-color texture DMA read failed");
            return false;
        }
        texture_swizzle =
            std::make_unique<SwizzleLookup>(texture_width, texture_height);

        if (profile == Profile::kSwizzledProjectedColor ||
            profile == Profile::kPitchProjectedColor) {
            const uint32_t format0 =
                raster_state[kRasterTextureFormat0 + 0];
            const uint32_t offset0 =
                raster_state[kRasterTextureOffset0 + 0];
            const uint32_t border0 =
                raster_state[kRasterTextureBorderColor0 + 0];
            const bool witnessed_projected_border =
                profile == Profile::kPitchProjectedColor
                    ? (border0 == 0 || border0 == 0xFF000000)
                    : border0 == 0;
            const bool witnessed_projected_texture =
                format0 == 0x0771062A &&
                raster_state[kRasterTextureAddress0 + 0] == 0x00010101 &&
                raster_state[kRasterTextureFilter0 + 0] == 0x02022000 &&
                raster_state[kRasterTextureControl0_0 + 0] == 0x4003FFF0 &&
                raster_state[kRasterTextureControl1_0 + 0] == 0x14000000 &&
                raster_state[kRasterTexturePalette0 + 0] == 0 &&
                witnessed_projected_border;
            if (!witnessed_projected_texture || (format0 & 3u) != 2u) {
                char why[320];
                std::snprintf(
                    why, sizeof(why),
                    "NV2A projected-color stage-0 texture exceeds the "
                    "witnessed subset (off=%08x fmt=%08x addr=%08x "
                    "filter=%08x ctl0=%08x ctl1=%08x pal=%08x border=%08x)",
                    offset0, format0,
                    raster_state[kRasterTextureAddress0 + 0],
                    raster_state[kRasterTextureFilter0 + 0],
                    raster_state[kRasterTextureControl0_0 + 0],
                    raster_state[kRasterTextureControl1_0 + 0],
                    raster_state[kRasterTexturePalette0 + 0],
                    raster_state[kRasterTextureBorderColor0 + 0]);
                fault_(why);
                return false;
            }
            projected_texture_width = 1u << ((format0 >> 20) & 0xFu);
            projected_texture_height = 1u << ((format0 >> 24) & 0xFu);
            const uint64_t projected_texture_bytes64 =
                static_cast<uint64_t>(projected_texture_width) *
                projected_texture_height * 4;
            uint32_t projected_texture_address = 0;
            if (projected_texture_bytes64 > SIZE_MAX ||
                !resolve_surface(texture_dma_b_, texture_dma_b_bound_, offset0,
                                 projected_texture_bytes64,
                                 projected_texture_address, "projected texture")) {
                return false;
            }
            projected_texture_data.resize(
                static_cast<size_t>(projected_texture_bytes64));
            if (!mem_read_ ||
                !mem_read_(projected_texture_address,
                           projected_texture_data.data(),
                           projected_texture_data.size())) {
                fault_("NV2A projected-color texture DMA read failed");
                return false;
            }
            projected_texture_swizzle = std::make_unique<SwizzleLookup>(
                projected_texture_width, projected_texture_height);
        }
    }
    std::vector<uint8_t>& linear_blit_texture_data = job.linear_blit_texture_data;
    uint32_t& linear_blit_texture_width = job.linear_blit_texture_width;
    uint32_t& linear_blit_texture_height = job.linear_blit_texture_height;
    uint32_t& linear_blit_texture_pitch = job.linear_blit_texture_pitch;
    if (profile == Profile::kInlineLinearBlitColor) {
        const uint32_t format0 = raster_state[kRasterTextureFormat0];
        const uint32_t offset0 = raster_state[kRasterTextureOffset0];
        if (!inline_linear_blit_texture || (format0 & 3u) != 2u) {
            fault_("NV2A inline linear-blit texture exceeds the witnessed subset");
            return false;
        }
        linear_blit_texture_width =
            raster_state[kRasterTextureImageRect0] >> 16;
        linear_blit_texture_height =
            raster_state[kRasterTextureImageRect0] & 0xFFFFu;
        linear_blit_texture_pitch =
            raster_state[kRasterTextureControl1_0] >> 16;
        const uint64_t linear_blit_texture_bytes64 =
            static_cast<uint64_t>(linear_blit_texture_pitch) *
            linear_blit_texture_height;
        uint32_t linear_blit_texture_address = 0;
        if (!linear_blit_texture_width ||
            !linear_blit_texture_height ||
            linear_blit_texture_pitch <
                linear_blit_texture_width * sizeof(uint32_t) ||
            linear_blit_texture_bytes64 > SIZE_MAX ||
            !resolve_surface(texture_dma_b_, texture_dma_b_bound_, offset0,
                             linear_blit_texture_bytes64,
                             linear_blit_texture_address,
                             "linear blit texture")) {
            return false;
        }
        const uint64_t texture_end =
            static_cast<uint64_t>(linear_blit_texture_address) +
            linear_blit_texture_bytes64;
        const uint64_t color_end =
            static_cast<uint64_t>(color_address) + color_bytes64;
        if (linear_blit_texture_address < color_end &&
            color_address < texture_end) {
            fault_("NV2A inline linear-blit source and target overlap");
            return false;
        }
        linear_blit_texture_data.resize(
            static_cast<size_t>(linear_blit_texture_bytes64));
        if (!mem_read_ ||
            !mem_read_(linear_blit_texture_address,
                       linear_blit_texture_data.data(),
                       linear_blit_texture_data.size())) {
            fault_("NV2A inline linear-blit texture DMA read failed");
            return false;
        }
    }
    std::vector<uint8_t>& multi_texture_data = job.multi_texture_data;
    std::unique_ptr<SwizzleLookup>& multi_texture_swizzle =
        job.multi_texture_swizzle;
    uint32_t& multi_texture_width = job.multi_texture_width;
    uint32_t& multi_texture_height = job.multi_texture_height;
    if (profile == Profile::kPitchMultiTextureColor ||
        profile == Profile::kPitchThreeTextureColor) {
        const uint32_t format0 = raster_state[kRasterTextureFormat0];
        const uint32_t format1 = raster_state[kRasterTextureFormat0 + 1];
        const uint32_t offset0 = raster_state[kRasterTextureOffset0];
        const uint32_t offset1 = raster_state[kRasterTextureOffset0 + 1];
        const bool witnessed_t0_border =
            raster_state[kRasterTextureBorderColor0] == 0 ||
            (profile == Profile::kPitchThreeTextureColor &&
             raster_state[kRasterTextureBorderColor0] == 0xFF000000 &&
             pitch_three_black_border_factor);
        const bool witnessed_multi_texture =
            format0 == 0x0661072E && format1 == format0 &&
            offset1 == offset0 &&
            raster_state[kRasterTextureAddress0] == 0x00030303 &&
            raster_state[kRasterTextureAddress0 + 1] == 0x00030303 &&
            raster_state[kRasterTextureFilter0] == 0x02022000 &&
            raster_state[kRasterTextureFilter0 + 1] == 0x02022000 &&
            raster_state[kRasterTextureControl0_0] == 0x4003FFC0 &&
            raster_state[kRasterTextureControl0_0 + 1] == 0x4003FFC0 &&
            raster_state[kRasterTextureControl1_0] == 0x14000000 &&
            raster_state[kRasterTextureControl1_0 + 1] == 0 &&
            raster_state[kRasterTextureImageRect0] == 0x050001E0 &&
            raster_state[kRasterTextureImageRect0 + 1] == 0 &&
            raster_state[kRasterTexturePalette0] == 0 &&
            raster_state[kRasterTexturePalette0 + 1] == 0 &&
            witnessed_t0_border &&
            raster_state[kRasterTextureBorderColor0 + 1] == 0;
        if (!witnessed_multi_texture ||
            (profile == Profile::kPitchThreeTextureColor &&
             !pitch_three_texture_state) ||
            (format0 & 3u) != 2u) {
            fault_("NV2A pitch cube-pair state exceeds the witnessed subset");
            return false;
        }
        multi_texture_width = 1u << ((format0 >> 20) & 0xFu);
        multi_texture_height = 1u << ((format0 >> 24) & 0xFu);
        const uint64_t multi_texture_bytes64 =
            static_cast<uint64_t>(multi_texture_width) *
            multi_texture_height * 4;
        uint32_t multi_texture_address = 0;
        if (multi_texture_bytes64 > SIZE_MAX ||
            !resolve_surface(texture_dma_b_, texture_dma_b_bound_, offset0,
                             multi_texture_bytes64,
                             multi_texture_address, "multi texture")) {
            return false;
        }
        multi_texture_data.resize(
            static_cast<size_t>(multi_texture_bytes64));
        if (!mem_read_ ||
            !mem_read_(multi_texture_address, multi_texture_data.data(),
                       multi_texture_data.size())) {
            fault_("NV2A pitch cube-pair DMA read failed");
            return false;
        }
        multi_texture_swizzle = std::make_unique<SwizzleLookup>(
            multi_texture_width, multi_texture_height);
    }
    std::vector<uint8_t>& single_texture_data = job.single_texture_data;
    std::unique_ptr<SwizzleLookup>& single_texture_swizzle =
        job.single_texture_swizzle;
    uint32_t& single_texture_width = job.single_texture_width;
    uint32_t& single_texture_height = job.single_texture_height;
    if (profile == Profile::kPitchSingleTextureColor ||
        profile == Profile::kTex7FanColor) {
        const uint32_t format0 = raster_state[kRasterTextureFormat0];
        const uint32_t offset0 = raster_state[kRasterTextureOffset0];
        const bool witnessed_single_texture =
            (profile == Profile::kPitchSingleTextureColor &&
            format0 == 0x0AA1062A &&
            raster_state[kRasterTextureAddress0] == 0x00030404 &&
            raster_state[kRasterTextureFilter0] == 0x02022000 &&
            raster_state[kRasterTextureControl0_0] == 0x4003FFC0 &&
            raster_state[kRasterTextureControl1_0] == 0x14000000 &&
            raster_state[kRasterTextureImageRect0] == 0x050001E0 &&
            raster_state[kRasterTexturePalette0] == 0 &&
            raster_state[kRasterTextureBorderColor0] == 0xFF000000) ||
            (profile == Profile::kTex7FanColor && tex7_fan_texture_state);
        if (!witnessed_single_texture || (format0 & 3u) != 2u) {
            fault_("NV2A pitch single-texture state exceeds the witnessed subset");
            return false;
        }
        single_texture_width = 1u << ((format0 >> 20) & 0xFu);
        single_texture_height = 1u << ((format0 >> 24) & 0xFu);
        const uint64_t single_texture_bytes64 =
            static_cast<uint64_t>(single_texture_width) *
            single_texture_height * 4;
        uint32_t single_texture_address = 0;
        if (single_texture_bytes64 > SIZE_MAX ||
            !resolve_surface(texture_dma_b_, texture_dma_b_bound_, offset0,
                             single_texture_bytes64,
                             single_texture_address, "single texture")) {
            return false;
        }
        single_texture_data.resize(
            static_cast<size_t>(single_texture_bytes64));
        if (!mem_read_ ||
            !mem_read_(single_texture_address, single_texture_data.data(),
                       single_texture_data.size())) {
            fault_("NV2A pitch single-texture DMA read failed");
            return false;
        }
        single_texture_swizzle = std::make_unique<SwizzleLookup>(
            single_texture_width, single_texture_height);
    }
    std::vector<uint8_t>& pitch_tex5_texture_data = job.pitch_tex5_texture_data;
    std::unique_ptr<SwizzleLookup>& pitch_tex5_texture_swizzle =
        job.pitch_tex5_texture_swizzle;
    uint32_t& pitch_tex5_texture_width = job.pitch_tex5_texture_width;
    uint32_t& pitch_tex5_texture_height = job.pitch_tex5_texture_height;
    if (profile == Profile::kPitchTex5ProjectedColor) {
        const uint32_t format0 = raster_state[kRasterTextureFormat0];
        const uint32_t offset0 = raster_state[kRasterTextureOffset0];
        if (!pitch_tex5_projected_texture || (format0 & 3u) != 2u) {
            fault_("NV2A pitch tex5 texture exceeds the witnessed subset");
            return false;
        }
        pitch_tex5_texture_width =
            1u << ((format0 >> 20) & 0xFu);
        pitch_tex5_texture_height =
            1u << ((format0 >> 24) & 0xFu);
        const uint64_t pitch_tex5_texture_bytes64 =
            static_cast<uint64_t>(pitch_tex5_texture_width) *
            pitch_tex5_texture_height * 4;
        uint32_t pitch_tex5_texture_address = 0;
        if (pitch_tex5_texture_bytes64 > SIZE_MAX ||
            !resolve_surface(texture_dma_b_, texture_dma_b_bound_, offset0,
                             pitch_tex5_texture_bytes64,
                             pitch_tex5_texture_address,
                             "pitch tex5 texture")) {
            return false;
        }
        pitch_tex5_texture_data.resize(
            static_cast<size_t>(pitch_tex5_texture_bytes64));
        if (!mem_read_ ||
            !mem_read_(pitch_tex5_texture_address,
                       pitch_tex5_texture_data.data(),
                       pitch_tex5_texture_data.size())) {
            fault_("NV2A pitch tex5 texture DMA read failed");
            return false;
        }
        pitch_tex5_texture_swizzle =
            std::make_unique<SwizzleLookup>(
                pitch_tex5_texture_width,
                pitch_tex5_texture_height);
    }
    std::vector<uint8_t>& inline_texture_data = job.inline_texture_data;
    std::unique_ptr<SwizzleLookup>& inline_texture_swizzle =
        job.inline_texture_swizzle;
    uint32_t& inline_texture_width = job.inline_texture_width;
    uint32_t& inline_texture_height = job.inline_texture_height;
    if (profile == Profile::kPitchAdditiveColor) {
        const uint32_t format0 = raster_state[kRasterTextureFormat0];
        const uint32_t address0 = raster_state[kRasterTextureAddress0];
        // The dashboard switches the unused P coordinate between WRAP and
        // CLAMP_TO_EDGE. PROJECT2D only consumes the witnessed clamped U/V.
        const bool witnessed_inline_address =
            address0 == 0x00010303 || address0 == 0x00030303;
        const uint32_t border0 =
            raster_state[kRasterTextureBorderColor0];
        const bool witnessed_inline_border =
            border0 == 0 || border0 == 0xFF000000;
        const bool witnessed_inline_texture =
            format0 == 0x0888062A &&
            witnessed_inline_address &&
            raster_state[kRasterTextureFilter0] == 0x02022000 &&
            raster_state[kRasterTextureControl0_0] == 0x4003FFC0 &&
            raster_state[kRasterTextureControl1_0] == 0x14000000 &&
            raster_state[kRasterTexturePalette0] == 0 &&
            witnessed_inline_border;
        if (!witnessed_inline_texture || (format0 & 3u) != 2u) {
            char why[320];
            std::snprintf(
                why, sizeof(why),
                "NV2A inline-color texture exceeds the witnessed subset "
                "(fmt=%08x addr=%08x filter=%08x ctl0=%08x ctl1=%08x "
                "pal=%08x border=%08x)",
                format0, address0,
                raster_state[kRasterTextureFilter0],
                raster_state[kRasterTextureControl0_0],
                raster_state[kRasterTextureControl1_0],
                raster_state[kRasterTexturePalette0],
                border0);
            fault_(why);
            return false;
        }
        inline_texture_width = 1u << ((format0 >> 20) & 0xFu);
        inline_texture_height = 1u << ((format0 >> 24) & 0xFu);
        const uint64_t inline_texture_bytes64 =
            static_cast<uint64_t>(inline_texture_width) *
            inline_texture_height * 4;
        uint32_t inline_texture_address = 0;
        if (inline_texture_bytes64 > SIZE_MAX ||
            !resolve_surface(texture_dma_b_, texture_dma_b_bound_,
                             raster_state[kRasterTextureOffset0],
                             inline_texture_bytes64,
                             inline_texture_address, "inline texture")) {
            return false;
        }
        inline_texture_data.resize(
            static_cast<size_t>(inline_texture_bytes64));
        if (!mem_read_ ||
            !mem_read_(inline_texture_address, inline_texture_data.data(),
                       inline_texture_data.size())) {
            fault_("NV2A inline-color texture DMA read failed");
            return false;
        }
        inline_texture_swizzle = std::make_unique<SwizzleLookup>(
            inline_texture_width, inline_texture_height);
    }

    // Stage (f) residency census (beads-xbox.1.15.7). The GPU executor takes
    // exactly the two dominant witnessed profiles; every other witnessed draw is
    // a CPU consumer of the same surfaces and is what forces a resolve. The
    // classification is taken here, where the destination pair is resolved and
    // before the residency queries below, and the queries a GPU draw makes about
    // its OWN destination are excluded -- they are not a foreign consumer.
    const bool gpu_profile_candidate = gpu_profile_state_ok(job);
    // Phase 5 item 1: the per-plane consumer tables. Taken HERE, at the same
    // point and on the same condition as the pair-scoped model above, so the
    // two are directly comparable; unlike the model this one is a plain count
    // of what each plane's draws actually were.
    if (color_profile) {
        gpu_res_note_surface_consumer(gpu_res_color_surfaces_, color_address,
                                      color_bytes64, job.profile,
                                      gpu_profile_candidate);
    }
    if (uses_depth_surface) {
        gpu_res_note_surface_consumer(gpu_res_depth_surfaces_, depth_address,
                                      depth_bytes64, job.profile,
                                      gpu_profile_candidate);
    }
    if (gpu_profile_candidate) {
        gpu_res_note_gpu_draw(color_address, color_bytes64,
                              uses_depth_surface ? depth_address : 0,
                              uses_depth_surface ? depth_bytes64 : 0);
    } else {
        ++gpu_res_.cpu_draws;
        // Phase 2 attribution (beads-xbox.1.15.10). Set BEFORE the residency
        // queries below, which is where a break, if any, is taken and
        // attributed. The member is a plain scalar on the emulation thread and
        // is read only inside gpu_res_cpu_*_access on the same thread.
        gpu_res_cpu_profile_ = job.profile;
        if (job.profile < kGpuResProfileSlots)
            ++gpu_res_.cpu_draws_profile[job.profile];
    }
    // ---- stage (f): the GPU executor branch ------------------------------
    // This `if`, and nothing else, is the difference between the software
    // executor and the GPU one. Everything above it -- profile match, subset
    // validation, surface resolution, texture snapshot, every fault_ site --
    // ran on the emulation thread in both modes. A job the GPU takes does NOT
    // resolve its own destination back into guest RAM: that resolve is exactly
    // what the residency model exists to avoid, and it happens instead at the
    // first CPU consumer of the surface, through the same
    // resolve_renderer_*_overlap seam every other path already calls.
    if (gpu_profile_candidate && gpu_profiles_enabled_ && renderer_) {
        // Stage (e) ordering fence: a queued software job writing the same
        // surface must retire before the GPU copy becomes authoritative. With
        // two or more span workers the ring is always empty and this is two
        // compares. (The thread count is deliberately not named here: the
        // draw-threads guard asserts that the raster path READS it only inside
        // the enqueue branch, and it matches on the identifier.)
        draw_order_fence();
        bool gpu_submitted = false;
        if (!gpu_profile_submit(job, gpu_submitted)) return false;
        if (gpu_submitted) return true;
        ++gpu_profile_declined_;
    }
    gpu_res_in_gpu_draw_ = gpu_profile_candidate;
    const bool witnessed_resolve_ok =
        resolve_renderer_color_overlap(color_address,
                                       static_cast<size_t>(color_bytes64),
                                       "witnessed color surface") &&
        (!uses_depth_surface ||
         resolve_renderer_depth_overlap(depth_address,
                                        static_cast<size_t>(depth_bytes64),
                                        "witnessed zeta surface"));
    gpu_res_in_gpu_draw_ = false;
    if (!witnessed_resolve_ok) return false;
    // Stage (e): the destination direct pointers are resolved HERE, at the latch,
    // and the PagedRasterSurface objects are constructed by the executor instead.
    // mem_direct_ is a pure pointer computation over the two RAM aliases
    // (machine.cpp:153-159) -- it never enters Unicorn -- so it can answer the
    // threadability question before any surface object exists, and a queued job's
    // surfaces then live on whichever thread runs the fragment loop. The value is
    // identical: PagedRasterSurface's constructor computes direct_base_ with the
    // same call on the same arguments.
    uint8_t*& depth_direct = job.depth_direct;
    uint8_t*& color_direct = job.color_direct;
    depth_direct = mem_direct_ ? mem_direct_(depth_address,
                                             static_cast<size_t>(depth_bytes64))
                               : nullptr;
    color_direct = mem_direct_ ? mem_direct_(color_address,
                                             static_cast<size_t>(color_bytes64))
                               : nullptr;
    // Stage (d): the threadability decision, taken at the latch, on the
    // emulation thread, with everything the job needs already resolved.
    // Rules, in the order DESIGN-NV2A-SEAM.md fixes them:
    //   section 1.3 (F2) -- every destination region a worker would touch
    //     must have a mem_direct_ pointer, because the paged fallback writes
    //     through mem_write_ == uc_mem_write, which Unicorn does not permit
    //     off the emulation thread. A depth-less profile needs no zeta
    //     pointer, so it is only required when uses_depth_surface is set.
    //   section 4.3 option O3 (F14) -- the per-write FNV fold is a serial,
    //     order-dependent accumulator; with it on, jobs run inline.
    //   F15 -- any installed diagnostic callback forces inline.
    // NOTHING ACTS ON THE FLAG IN STAGE (d): every job is executed inline at
    // the latch point regardless. It is measured so that stage (e) can be
    // sized before it is written (measurement M5).
    // Stage (e2), beads-xbox.1.15.9: the DESTINATION swizzle table moves here,
    // to the latch, from the top of execute_witnessed_fragments. It is a pure
    // function of (surface_width, surface_height) and immutable once built, so
    // the N destination-span parts share one const instance instead of building
    // an identical ~10.7 k-entry table each. Built once per job in every mode,
    // so the inline path pays exactly what it paid before.
    if (profile == Profile::kSwizzledRadialColor ||
        profile == Profile::kSwizzledTexturedColor ||
        profile == Profile::kSwizzledProjectedColor) {
        job.surface_swizzle =
            std::make_unique<SwizzleLookup>(surface_width, surface_height);
    }
    const bool direct_ok = color_direct != nullptr &&
                           (!uses_depth_surface || depth_direct != nullptr);
    if (!direct_ok)
        seam_note_threadable(job, false, kSeamNoDirectPointer);
    else if (job.hash_on)
        seam_note_threadable(job, false, kSeamHashOn);
    else if (job.debug_cb_installed)
        seam_note_threadable(job, false, kSeamDebugCallback);
    else
        seam_note_threadable(job, true, kSeamThreadable);
    // ================= stage (e): THE ENQUEUE BRANCH =======================
    // beads-xbox.1.15.5, DESIGN-NV2A-SEAM.md section 5(e). This `if`, and
    // nothing else, is the difference between --nv2a-draw-threads 0 (today's
    // behaviour, byte for byte) and a threaded run. Everything above it ran on
    // the emulation thread in both modes -- profile match, the subset validators,
    // surface resolution, the texture snapshots, every fault_ site. Everything
    // below it is the pure pixel math, and it runs on the emulation thread when
    // threads == 0 and on the single worker when the job was queued.
    if (draw_threads_ > 0) {
        if (job.threadable) {
            if (draw_threads_ == 1) {
                // Stage (e): hand the whole job to the single asynchronous
                // consumer, which executes it in submission order off the ring.
                job.deferred = true;
                return true;
            }
            // Stage (e2), beads-xbox.1.15.9: N >= 2 divides THIS job across N
            // destination-span parts and barriers before returning, so the job
            // does not outlive the latch point and the ring stays empty.
            return execute_draw_spans(job);
        }
        // Not threadable (no direct destination pointer, or the per-write hash
        // fold is on, or a diagnostic callback is installed): it executes below,
        // on this thread, so it must not overtake the queue. At N >= 2 the queue
        // is always empty and the fence is free, but it stays because the fence
        // is what makes the N == 1 path correct and there is one code path.
        draw_order_fence();
    }
    return execute_witnessed_fragments(job, job.out, 0, 1);
}

// The pure execute half of the witnessed raster path: triangle setup and the
// shared fragment loop, and nothing else. It reads ONLY the job (plus the
// immutable SwizzleLookup tables the job owns) and writes ONLY into the job's
// destination surfaces through mem_direct_ pointers and into job.out. It calls
// no Unicorn API, touches no renderer_, and raises no fault_ -- a failure is
// recorded in job.out and reported by the emulation thread at the next fold.
// DESIGN-NV2A-SEAM.md sections 1.3, 3.1, 3.2 and F13/F14/F3.
bool Nv2a::execute_witnessed_fragments(DrawJob& job, DrawJobTelemetry& out,
                                       unsigned part, unsigned parts) {
    // Stage (e2), beads-xbox.1.15.9. `parts` is the number of disjoint
    // destination-scanline sets this draw is split across and `part` is which
    // one this call owns; parts == 1 is the whole surface and is byte-for-byte
    // the stage (e) executor. The row filter is applied as a STRIDE on the
    // per-triangle scanline loop below, so a part never visits a row it does
    // not own and the parts == 1 loop is the same loop with step 1.
    // The full enumeration of everything in here that is not per-pixel, and
    // how each item is handled, is in the declaration in nv2a.hpp.
    // ---- the latched state this half reads, bound by the same alias technique
    // ---- stage (d) used, so the ~1900 lines below are textually unchanged.
    const auto& raster_state = job.raster_state;
    const auto& transformed_batch = job.vertices;
    const uint32_t primitive_mode = job.primitive_mode;
    const uint32_t surface_format = job.surface_format;
    const Profile profile = static_cast<Profile>(job.profile);
    const uint32_t enables = raster_state[kRasterEnables];
    const bool pitch_tex5_profile = job.pitch_tex5_profile;
    const uint32_t color_address = job.color_address;
    const uint32_t depth_address = job.depth_address;
    const uint32_t pitch = job.pitch;
    const uint32_t surface_width = job.surface_width;
    const uint32_t surface_height = job.surface_height;
    const uint32_t clip_x = job.clip_x;
    const uint32_t clip_y = job.clip_y;
    const uint32_t clip_width = job.clip_width;
    const uint32_t clip_height = job.clip_height;
    const uint64_t color_bytes64 = job.color_bytes;
    const uint64_t depth_bytes64 = job.depth_bytes;
    const bool uses_depth_surface = job.uses_depth;
    const bool writes_depth_surface = job.writes_depth;
    const bool color_profile = job.color_profile;
    const bool inline_depth_test = job.inline_depth_test;
    uint8_t* const depth_direct = job.depth_direct;
    uint8_t* const color_direct = job.color_direct;
    const std::vector<uint8_t>& texture_data = job.texture_data;
    const std::unique_ptr<SwizzleLookup>& texture_swizzle = job.texture_swizzle;
    const uint32_t texture_width = job.texture_width;
    const uint32_t texture_height = job.texture_height;
    const std::vector<uint8_t>& projected_texture_data = job.projected_texture_data;
    const std::unique_ptr<SwizzleLookup>& projected_texture_swizzle =
        job.projected_texture_swizzle;
    const uint32_t projected_texture_width = job.projected_texture_width;
    const uint32_t projected_texture_height = job.projected_texture_height;
    const std::vector<uint8_t>& linear_blit_texture_data =
        job.linear_blit_texture_data;
    const uint32_t linear_blit_texture_width = job.linear_blit_texture_width;
    const uint32_t linear_blit_texture_height = job.linear_blit_texture_height;
    const uint32_t linear_blit_texture_pitch = job.linear_blit_texture_pitch;
    const std::vector<uint8_t>& multi_texture_data = job.multi_texture_data;
    const std::unique_ptr<SwizzleLookup>& multi_texture_swizzle =
        job.multi_texture_swizzle;
    const uint32_t multi_texture_width = job.multi_texture_width;
    const uint32_t multi_texture_height = job.multi_texture_height;
    const std::vector<uint8_t>& single_texture_data = job.single_texture_data;
    const std::unique_ptr<SwizzleLookup>& single_texture_swizzle =
        job.single_texture_swizzle;
    const uint32_t single_texture_width = job.single_texture_width;
    const uint32_t single_texture_height = job.single_texture_height;
    const std::vector<uint8_t>& pitch_tex5_texture_data =
        job.pitch_tex5_texture_data;
    const std::unique_ptr<SwizzleLookup>& pitch_tex5_texture_swizzle =
        job.pitch_tex5_texture_swizzle;
    const uint32_t pitch_tex5_texture_width = job.pitch_tex5_texture_width;
    const uint32_t pitch_tex5_texture_height = job.pitch_tex5_texture_height;
    const std::vector<uint8_t>& inline_texture_data = job.inline_texture_data;
    const std::unique_ptr<SwizzleLookup>& inline_texture_swizzle =
        job.inline_texture_swizzle;
    const uint32_t inline_texture_width = job.inline_texture_width;
    const uint32_t inline_texture_height = job.inline_texture_height;

    // ---- telemetry redirection (stage (e), beads-xbox.1.15.5) ----------------
    // Every counter the fragment loop used to increment on `this` is bound here
    // to a JOB-LOCAL cell that shadows the member name, so all ~1900 lines below
    // are textually unchanged and a worker writes no Nv2a member at all. The
    // emulation thread folds these into the members at retire/drain, in latch
    // order, in Nv2a::fold_draw_telemetry.
    uint64_t& depth_raster_batches_ = out.depth_raster_batches;
    uint64_t& depth_test_passes_ = out.depth_test_passes;
    uint64_t& depth_writes_ = out.depth_writes;
    uint64_t& inline_color_alpha_passes_ = out.inline_color_alpha_passes;
    uint64_t& inline_color_batches_ = out.inline_color_batches;
    uint64_t& inline_color_fragments_ = out.inline_color_fragments;
    uint64_t& inline_color_triangles_ = out.inline_color_triangles;
    uint64_t& inline_color_writes_ = out.inline_color_writes;
    uint64_t& inline_linear_blit_batches_ = out.inline_linear_blit_batches;
    uint64_t& inline_linear_blit_fragments_ = out.inline_linear_blit_fragments;
    uint64_t& inline_linear_blit_samples_ = out.inline_linear_blit_samples;
    uint64_t& inline_linear_blit_triangles_ = out.inline_linear_blit_triangles;
    uint64_t& inline_linear_blit_writes_ = out.inline_linear_blit_writes;
    uint64_t& inline_texture_samples_ = out.inline_texture_samples;
    uint64_t& pitch_multi_texture_alpha_passes_ = out.pitch_multi_texture_alpha_passes;
    uint64_t& pitch_three_texture_rescale_direction0_ = out.pitch_three_texture_rescale_direction0;
    uint64_t& pitch_three_texture_rescale_direction1_ = out.pitch_three_texture_rescale_direction1;
    uint64_t& pitch_three_texture_zero_direction0_ = out.pitch_three_texture_zero_direction0;
    uint64_t& pitch_three_texture_zero_direction1_ = out.pitch_three_texture_zero_direction1;
    uint64_t& pitch_three_texture_xy_tie_direction0_ = out.pitch_three_texture_xy_tie_direction0;
    uint64_t& pitch_three_texture_xy_tie_direction1_ = out.pitch_three_texture_xy_tie_direction1;
    uint64_t& pitch_multi_texture_batches_ = out.pitch_multi_texture_batches;
    uint64_t& pitch_multi_texture_color_writes_ = out.pitch_multi_texture_color_writes;
    uint64_t& pitch_multi_texture_depth_passes_ = out.pitch_multi_texture_depth_passes;
    uint64_t& pitch_multi_texture_depth_writes_ = out.pitch_multi_texture_depth_writes;
    uint64_t& pitch_multi_texture_fragments_ = out.pitch_multi_texture_fragments;
    uint64_t& pitch_multi_texture_samples_ = out.pitch_multi_texture_samples;
    uint64_t& pitch_multi_texture_triangles_ = out.pitch_multi_texture_triangles;
    uint64_t& pitch_projected_color_batches_ = out.pitch_projected_color_batches;
    uint64_t& pitch_projected_color_depth_passes_ = out.pitch_projected_color_depth_passes;
    uint64_t& pitch_projected_color_depth_writes_ = out.pitch_projected_color_depth_writes;
    uint64_t& pitch_projected_color_fragments_ = out.pitch_projected_color_fragments;
    uint64_t& pitch_projected_color_triangles_ = out.pitch_projected_color_triangles;
    uint64_t& pitch_projected_color_writes_ = out.pitch_projected_color_writes;
    uint64_t& pitch_projected_texture_samples_ = out.pitch_projected_texture_samples;
    uint64_t& pitch_single_texture_batches_ = out.pitch_single_texture_batches;
    uint64_t& pitch_single_texture_depth_passes_ = out.pitch_single_texture_depth_passes;
    uint64_t& pitch_single_texture_fragments_ = out.pitch_single_texture_fragments;
    uint64_t& pitch_single_texture_samples_ = out.pitch_single_texture_samples;
    uint64_t& pitch_single_texture_triangles_ = out.pitch_single_texture_triangles;
    uint64_t& pitch_single_texture_writes_ = out.pitch_single_texture_writes;
    uint64_t& pitch_tex5_batches_ = out.pitch_tex5_batches;
    uint64_t& pitch_tex5_color_writes_ = out.pitch_tex5_color_writes;
    uint64_t& pitch_tex5_depth_passes_ = out.pitch_tex5_depth_passes;
    uint64_t& pitch_tex5_depth_writes_ = out.pitch_tex5_depth_writes;
    uint64_t& pitch_tex5_fragments_ = out.pitch_tex5_fragments;
    uint64_t& pitch_tex5_samples_ = out.pitch_tex5_samples;
    uint64_t& pitch_tex5_triangles_ = out.pitch_tex5_triangles;
    uint64_t& pitch_texture_samples_ = out.pitch_texture_samples;
    uint64_t& pitch_textured_color_batches_ = out.pitch_textured_color_batches;
    uint64_t& pitch_textured_color_depth_passes_ = out.pitch_textured_color_depth_passes;
    uint64_t& pitch_textured_color_depth_writes_ = out.pitch_textured_color_depth_writes;
    uint64_t& pitch_textured_color_fragments_ = out.pitch_textured_color_fragments;
    uint64_t& pitch_textured_color_triangles_ = out.pitch_textured_color_triangles;
    uint64_t& pitch_textured_color_writes_ = out.pitch_textured_color_writes;
    uint64_t& pitch_three_texture_batches_ = out.pitch_three_texture_batches;
    uint64_t& pitch_three_texture_color_writes_ = out.pitch_three_texture_color_writes;
    uint64_t& pitch_three_texture_depth_passes_ = out.pitch_three_texture_depth_passes;
    uint64_t& pitch_three_texture_depth_writes_ = out.pitch_three_texture_depth_writes;
    uint64_t& pitch_three_texture_fragments_ = out.pitch_three_texture_fragments;
    uint64_t& pitch_three_texture_samples_ = out.pitch_three_texture_samples;
    uint64_t& pitch_three_texture_triangles_ = out.pitch_three_texture_triangles;
    uint64_t& projected_color_batches_ = out.projected_color_batches;
    uint64_t& projected_color_depth_passes_ = out.projected_color_depth_passes;
    uint64_t& projected_color_depth_writes_ = out.projected_color_depth_writes;
    uint64_t& projected_color_fragments_ = out.projected_color_fragments;
    uint64_t& projected_color_triangles_ = out.projected_color_triangles;
    uint64_t& projected_color_writes_ = out.projected_color_writes;
    uint64_t& projected_texture_samples_ = out.projected_texture_samples;
    uint64_t& radial_color_batches_ = out.radial_color_batches;
    uint64_t& radial_color_depth_passes_ = out.radial_color_depth_passes;
    uint64_t& radial_color_depth_writes_ = out.radial_color_depth_writes;
    uint64_t& radial_color_fragments_ = out.radial_color_fragments;
    uint64_t& radial_color_triangles_ = out.radial_color_triangles;
    uint64_t& radial_color_writes_ = out.radial_color_writes;
    uint64_t& rasterized_fragments_ = out.rasterized_fragments;
    uint64_t& rasterized_triangles_ = out.rasterized_triangles;
    uint64_t& tex7_fan_batches_ = out.tex7_fan_batches;
    uint64_t& tex7_fan_depth_passes_ = out.tex7_fan_depth_passes;
    uint64_t& tex7_fan_fragments_ = out.tex7_fan_fragments;
    uint64_t& tex7_fan_samples_ = out.tex7_fan_samples;
    uint64_t& tex7_fan_triangles_ = out.tex7_fan_triangles;
    uint64_t& tex7_fan_writes_ = out.tex7_fan_writes;
    uint64_t& texf_projected_fan_alpha_passes_ = out.texf_projected_fan_alpha_passes;
    uint64_t& texf_projected_fan_batches_ = out.texf_projected_fan_batches;
    uint64_t& texf_projected_fan_fragments_ = out.texf_projected_fan_fragments;
    uint64_t& texf_projected_fan_triangles_ = out.texf_projected_fan_triangles;
    uint64_t& texf_projected_fan_writes_ = out.texf_projected_fan_writes;
    uint64_t& texture_samples_ = out.texture_samples;
    uint64_t& textured_color_batches_ = out.textured_color_batches;
    uint64_t& textured_color_depth_passes_ = out.textured_color_depth_passes;
    uint64_t& textured_color_depth_writes_ = out.textured_color_depth_writes;
    uint64_t& textured_color_fragments_ = out.textured_color_fragments;
    uint64_t& textured_color_triangles_ = out.textured_color_triangles;
    uint64_t& textured_color_writes_ = out.textured_color_writes;
    uint64_t& inline_color_output_hash_ = out.inline_color_output_hash;
    uint64_t& inline_linear_blit_output_hash_ = out.inline_linear_blit_output_hash;
    uint64_t& pitch_projected_color_output_hash_ = out.pitch_projected_color_output_hash;
    uint64_t& pitch_multi_texture_output_hash_ = out.pitch_multi_texture_output_hash;
    uint64_t& pitch_three_texture_output_hash_ = out.pitch_three_texture_output_hash;
    uint64_t& pitch_single_texture_output_hash_ = out.pitch_single_texture_output_hash;
    uint64_t& pitch_tex5_output_hash_ = out.pitch_tex5_output_hash;
    uint64_t& pitch_textured_color_output_hash_ = out.pitch_textured_color_output_hash;
    uint64_t& projected_color_output_hash_ = out.projected_color_output_hash;
    uint64_t& radial_color_output_hash_ = out.radial_color_output_hash;
    uint64_t& raster_output_hash_ = out.raster_output_hash;
    uint64_t& tex7_fan_output_hash_ = out.tex7_fan_output_hash;
    uint64_t& texf_projected_fan_output_hash_ = out.texf_projected_fan_output_hash;
    uint64_t& textured_color_output_hash_ = out.textured_color_output_hash;
    DrawJobLastValue& last_color_raster_address_ = out.last_color_raster_address;
    DrawJobLastValue& last_inline_linear_blit_raster_address_ = out.last_inline_linear_blit_raster_address;
    DrawJobLastValue& last_inline_raster_address_ = out.last_inline_raster_address;
    DrawJobLastValue& last_pitch_multi_texture_raster_address_ = out.last_pitch_multi_texture_raster_address;
    DrawJobLastValue& last_pitch_projected_raster_address_ = out.last_pitch_projected_raster_address;
    DrawJobLastValue& last_pitch_single_texture_raster_address_ = out.last_pitch_single_texture_raster_address;
    DrawJobLastValue& last_pitch_tex5_raster_address_ = out.last_pitch_tex5_raster_address;
    DrawJobLastValue& last_pitch_textured_raster_address_ = out.last_pitch_textured_raster_address;
    DrawJobLastValue& last_pitch_three_rescale_factor3_ = out.last_pitch_three_rescale_factor3;
    DrawJobLastValue& last_pitch_three_texture_raster_address_ = out.last_pitch_three_texture_raster_address;
    DrawJobLastValue& last_pitch_three_xy_tie_factor3_ = out.last_pitch_three_xy_tie_factor3;
    DrawJobLastValue& last_pitch_three_zero_factor3_ = out.last_pitch_three_zero_factor3;
    DrawJobLastValue& last_projected_raster_address_ = out.last_projected_raster_address;
    DrawJobLastValue& last_raster_address_ = out.last_raster_address;
    DrawJobLastValue& last_tex7_fan_raster_address_ = out.last_tex7_fan_raster_address;
    DrawJobLastValue& last_texf_projected_fan_raster_address_ = out.last_texf_projected_fan_raster_address;
    DrawJobLastValue& last_textured_raster_address_ = out.last_textured_raster_address;

    // ---- F13: the executor may not fault_. This local lambda SHADOWS the
    // member of the same name, so every fault_("...") site below is unchanged;
    // the message is recorded and the emulation thread raises the real fault at
    // the next fold, with the original text. First message wins, exactly as the
    // machine's first fault wins today.
    auto fault_ = [&out](const std::string& message) {
        if (!out.failed) {
            out.failed = true;
            out.fault_message = message;
        }
    };
    // ---- F3: the renderer invalidations are guest-visible side effects that
    // touch renderer_ and mem_write_. Same shadowing trick: the call sites are
    // unchanged, the effect is recorded and applied on the emulation thread, in
    // job order, at the fold.
    auto invalidate_renderer_color_overlap = [&out](uint32_t address, size_t bytes,
                                                    uint32_t cause) {
        out.invalidate_color = true;
        out.invalidate_color_address = address;
        out.invalidate_color_bytes = bytes;
        out.invalidate_color_cause = cause;
    };
    auto invalidate_renderer_depth_overlap = [&out](uint32_t address, size_t bytes,
                                                    uint32_t cause) {
        out.invalidate_depth = true;
        out.invalidate_depth_address = address;
        out.invalidate_depth_bytes = bytes;
        out.invalidate_depth_cause = cause;
    };
    // The destination surfaces. For a queued job both direct pointers are
    // non-null by the section 1.3 eligibility rule, so read_/write_ are never
    // invoked and no Unicorn call is reachable from here; the constructor's only
    // use of them is to bind the references.
    PagedRasterSurface depth_surface(
        depth_address, static_cast<size_t>(depth_bytes64),
        mem_read_, mem_write_, mem_direct_);
    PagedRasterSurface color_surface(
        color_address, static_cast<size_t>(color_bytes64),
        mem_read_, mem_write_, mem_direct_);
    auto read_depth32 = [&](size_t offset, uint32_t& value) {
        if (depth_direct) {
            std::memcpy(&value, depth_direct + offset, sizeof(value));
            return true;
        }
        return depth_surface.read32(offset, value);
    };
    auto write_depth32 = [&](size_t offset, uint32_t value) {
        if (depth_direct) {
            std::memcpy(depth_direct + offset, &value, sizeof(value));
            return true;
        }
        return depth_surface.write32(offset, value);
    };
    auto write_color32 = [&](size_t offset, uint32_t value) {
        if (color_direct) {
            std::memcpy(color_direct + offset, &value, sizeof(value));
            return true;
        }
        return color_surface.write32(offset, value);
    };
    // Stage (e2): built ONCE per job at the latch (rasterize_witnessed_batch)
    // and owned by the job, not rebuilt here. With N parts this function is
    // entered N times, and SwizzleLookup(640,480) walks ~10.7 k entries against
    // ~26 k fragments per batch, so per-part construction would have been a
    // 30-40 percent tax on the three swizzled profiles. Immutable after
    // construction, so every part reads the same table concurrently.
    const SwizzleLookup* const surface_swizzle = job.surface_swizzle.get();

    uint32_t xmin = clip_x;
    uint32_t xmax = clip_x + clip_width - 1;
    uint32_t ymin = clip_y;
    uint32_t ymax = clip_y + clip_height - 1;
    const uint32_t window_x = raster_state[kRasterWindowClipHorizontal];
    const uint32_t window_y = raster_state[kRasterWindowClipVertical];
    xmin = std::max(xmin, window_x & 0xFFFu);
    xmax = std::min(xmax, (window_x >> 16) & 0xFFFu);
    ymin = std::max(ymin, window_y & 0xFFFu);
    ymax = std::min(ymax, (window_y >> 16) & 0xFFFu);
    if (xmax < xmin || ymax < ymin) {
        // Stage (e2): a batch counter is a property of the DRAW, not of a
        // destination span. Every part evaluates this clip test identically
        // (it reads only latched state), so counting it on part 0 alone
        // reproduces the single-threaded tally exactly.
        if (part != 0) return true;
        if (profile == Profile::kPitchZ16Depth)
            ++depth_raster_batches_;
        else if (profile == Profile::kSwizzledRadialColor)
            ++radial_color_batches_;
        else if (profile == Profile::kSwizzledTexturedColor)
            ++textured_color_batches_;
        else if (profile == Profile::kPitchTexturedColor)
            ++pitch_textured_color_batches_;
        else if (profile == Profile::kSwizzledProjectedColor)
            ++projected_color_batches_;
        else if (profile == Profile::kPitchProjectedColor)
            ++pitch_projected_color_batches_;
        else if (profile == Profile::kPitchMultiTextureColor)
            ++pitch_multi_texture_batches_;
        else if (profile == Profile::kPitchThreeTextureColor)
            ++pitch_three_texture_batches_;
        else if (pitch_tex5_profile)
            ++pitch_tex5_batches_;
        else if (profile == Profile::kInlineLinearBlitColor)
            ++inline_linear_blit_batches_;
        else if (profile == Profile::kPitchSingleTextureColor)
            ++pitch_single_texture_batches_;
        else if (profile == Profile::kTex7FanColor)
            ++tex7_fan_batches_;
        else if (profile == Profile::kTexfProjectedFanColor)
            ++texf_projected_fan_batches_;
        else
            ++inline_color_batches_;
        return true;
    }

    float clip_min = 0.0f, clip_max = 0.0f;
    std::memcpy(&clip_min, &raster_state[kRasterClipMin], sizeof(clip_min));
    std::memcpy(&clip_max, &raster_state[kRasterClipMax], sizeof(clip_max));
    float polygon_scale = 0.0f, polygon_bias = 0.0f;
    if (enables & (1u << 13)) {
        std::memcpy(&polygon_scale,
                    &raster_state[kRasterPolygonOffsetScale],
                    sizeof(polygon_scale));
        std::memcpy(&polygon_bias,
                    &raster_state[kRasterPolygonOffsetBias],
                    sizeof(polygon_bias));
    }

    enum class TextureAddressMode {
        kRepeat,
        kClampToEdge,
        kBorder,
    };
    auto sample_texture =
        [](const std::vector<uint8_t>& data,
           const SwizzleLookup& swizzle,
           uint32_t width, uint32_t height,
           float u, float v, TextureAddressMode address_mode,
           uint32_t border_pixel,
           std::array<float, 3>& color,
           float* alpha) {
            if (!std::isfinite(u) || !std::isfinite(v)) return false;
            if (address_mode == TextureAddressMode::kRepeat) {
                u -= std::floor(u);
                v -= std::floor(v);
            } else if (address_mode == TextureAddressMode::kClampToEdge) {
                u = std::clamp(u, 0.0f, 1.0f);
                v = std::clamp(v, 0.0f, 1.0f);
            }
            const float texel_x = u * static_cast<float>(width) - 0.5f;
            const float texel_y = v * static_cast<float>(height) - 0.5f;
            const int32_t x0 = static_cast<int32_t>(std::floor(texel_x));
            const int32_t y0 = static_cast<int32_t>(std::floor(texel_y));
            const float fx = texel_x - static_cast<float>(x0);
            const float fy = texel_y - static_cast<float>(y0);
            auto coordinate = [address_mode](int32_t value, uint32_t size) {
                if (address_mode != TextureAddressMode::kRepeat) {
                    return static_cast<uint32_t>(std::clamp(
                        value, 0, static_cast<int32_t>(size) - 1));
                }
                const int32_t signed_size = static_cast<int32_t>(size);
                int32_t wrapped = value % signed_size;
                if (wrapped < 0) wrapped += signed_size;
                return static_cast<uint32_t>(wrapped);
            };
            auto unpack = [](uint32_t pixel) {
                return std::array<float, 4>{{
                    static_cast<float>((pixel >> 16) & 0xFFu) / 255.0f,
                    static_cast<float>((pixel >> 8) & 0xFFu) / 255.0f,
                    static_cast<float>(pixel & 0xFFu) / 255.0f,
                    static_cast<float>((pixel >> 24) & 0xFFu) / 255.0f,
                }};
            };
            const auto border = unpack(border_pixel);
            auto texel = [&](int32_t x, int32_t y) {
                if (address_mode == TextureAddressMode::kBorder &&
                    (x < 0 || y < 0 ||
                     x >= static_cast<int32_t>(width) ||
                     y >= static_cast<int32_t>(height))) {
                    return border;
                }
                const size_t offset =
                    static_cast<size_t>(swizzle.index(
                        coordinate(x, width), coordinate(y, height))) *
                    sizeof(uint32_t);
                uint32_t pixel = 0;
                std::memcpy(&pixel, data.data() + offset, sizeof(pixel));
                return unpack(pixel);
            };
            const auto c00 = texel(x0, y0);
            const auto c10 = texel(x0 + 1, y0);
            const auto c01 = texel(x0, y0 + 1);
            const auto c11 = texel(x0 + 1, y0 + 1);
            for (size_t component = 0; component < 3; ++component) {
                const float top =
                    c00[component] + (c10[component] - c00[component]) * fx;
                const float bottom =
                    c01[component] + (c11[component] - c01[component]) * fx;
                color[component] = top + (bottom - top) * fy;
            }
            if (alpha) {
                const float top = c00[3] + (c10[3] - c00[3]) * fx;
                const float bottom = c01[3] + (c11[3] - c01[3]) * fx;
                *alpha = top + (bottom - top) * fy;
            }
            return true;
        };
    auto sample_linear_texture =
        [](const std::vector<uint8_t>& data,
           uint32_t width, uint32_t height, uint32_t row_pitch,
           float s, float t, std::array<float, 3>& color) {
            if (!std::isfinite(s) || !std::isfinite(t) ||
                !width || !height) {
                return false;
            }
            s = std::clamp(s, 0.0f, static_cast<float>(width));
            t = std::clamp(t, 0.0f, static_cast<float>(height));
            const float texel_x = s - 0.5f;
            const float texel_y = t - 0.5f;
            const int32_t x0 =
                static_cast<int32_t>(std::floor(texel_x));
            const int32_t y0 =
                static_cast<int32_t>(std::floor(texel_y));
            const float fx = texel_x - static_cast<float>(x0);
            const float fy = texel_y - static_cast<float>(y0);
            auto texel = [&](int32_t x, int32_t y) {
                const uint32_t clamped_x = static_cast<uint32_t>(
                    std::clamp(x, 0, static_cast<int32_t>(width) - 1));
                const uint32_t clamped_y = static_cast<uint32_t>(
                    std::clamp(y, 0, static_cast<int32_t>(height) - 1));
                const size_t offset =
                    static_cast<size_t>(clamped_y) * row_pitch +
                    static_cast<size_t>(clamped_x) * sizeof(uint32_t);
                uint32_t pixel = 0;
                std::memcpy(&pixel, data.data() + offset, sizeof(pixel));
                return std::array<float, 3>{{
                    static_cast<float>((pixel >> 16) & 0xFFu) / 255.0f,
                    static_cast<float>((pixel >> 8) & 0xFFu) / 255.0f,
                    static_cast<float>(pixel & 0xFFu) / 255.0f,
                }};
            };
            const auto c00 = texel(x0, y0);
            const auto c10 = texel(x0 + 1, y0);
            const auto c01 = texel(x0, y0 + 1);
            const auto c11 = texel(x0 + 1, y0 + 1);
            for (size_t component = 0; component < 3; ++component) {
                const float top =
                    c00[component] +
                    (c10[component] - c00[component]) * fx;
                const float bottom =
                    c01[component] +
                    (c11[component] - c01[component]) * fx;
                color[component] =
                    top + (bottom - top) * fy;
            }
            return true;
        };
    auto sample_cube_texture =
        [&](const std::vector<uint8_t>& data,
            const SwizzleLookup& swizzle,
            uint32_t width, uint32_t height,
            const std::array<float, 3>& direction,
            std::array<float, 3>& color,
            uint64_t* zero_direction_counter = nullptr,
            uint64_t* xy_tie_counter = nullptr) {
            if (!std::isfinite(direction[0]) || !std::isfinite(direction[1]) ||
                !std::isfinite(direction[2])) {
                return false;
            }
            // The retained pitch-three profile reaches the exact zero vector.
            // Apply its deterministic +Z-axis tie-break policy only when that
            // caller explicitly opts in.
            if (direction[0] == 0.0f && direction[1] == 0.0f &&
                direction[2] == 0.0f) {
                if (!zero_direction_counter) return false;
                ++*zero_direction_counter;
                last_pitch_three_zero_factor3_ =
                    raster_state[kRasterCombinerFactor0_0 + 3];
                return sample_texture(data, swizzle, width, height,
                                      0.0f, 0.0f,
                                      TextureAddressMode::kClampToEdge, 0,
                                      color, nullptr);
            }
            const float abs_x = std::abs(direction[0]);
            const float abs_y = std::abs(direction[1]);
            const float abs_z = std::abs(direction[2]);
            float u = 0.0f;
            float v = 0.0f;
            float divisor = 0.0f;
            // The retained pitch-three profile uses a first-major tie policy
            // for exactly |X|==|Y|>|Z|. Other profiles retain the historical
            // fallback.
            if (abs_x == abs_y && abs_x > abs_z && xy_tie_counter) {
                ++*xy_tie_counter;
                last_pitch_three_xy_tie_factor3_ =
                    raster_state[kRasterCombinerFactor0_0 + 3];
                divisor = abs_x;
                u = direction[0] >= 0.0f ? -direction[2] : direction[2];
                v = direction[1];
            } else if (abs_x > abs_y && abs_x > abs_z) {
                divisor = abs_x;
                u = direction[0] >= 0.0f ? -direction[2] : direction[2];
                v = direction[1];
            } else if (abs_y > abs_x && abs_y > abs_z) {
                divisor = abs_y;
                u = direction[0];
                v = direction[1] >= 0.0f ? -direction[2] : direction[2];
            } else {
                divisor = abs_z;
                u = direction[2] >= 0.0f ? direction[0] : -direction[0];
                v = direction[1];
            }
            if (divisor == 0.0f) return false;
            u /= divisor;
            v /= divisor;
            return sample_texture(data, swizzle, width, height,
                                  u, v, TextureAddressMode::kClampToEdge, 0,
                                  color, nullptr);
        };

    // Stage (b2) / DESIGN-NV2A-SEAM.md section 4.3 option O3: the per-write
    // FNV-1a folds (`h ^= word; h *= 1099511628211ull;`) are a two-operation
    // loop-carried dependency executed once per written word on the hottest loop
    // in the emulator. Latch the switch once per batch so the whole telemetry
    // block -- fold, last-written-address store, and write census -- sits behind
    // one perfectly-predicted branch instead of forming a serial chain through
    // the fragment path. Default is ON, so the 22 witnessed/scanout hashes and
    // the write counters are bit-identical to today until the default flips,
    // which is a separate re-baseline decision (methodology section 3).
    const bool hash_on = job.hash_on;
    // Stage (b1): per-batch fragment tally, folded into the profile's counter
    // once below the primitive-assembly switch.
    uint64_t witnessed_fragment_count = 0;
    bool raster_failed = false;
    auto rasterize_triangle =
        [&](auto profile_tag,
            const TransformedVertex& input_a,
            const TransformedVertex& input_b,
            const TransformedVertex& input_c) {
            // Stage (b3): the batch profile is loop-invariant, so it arrives as a
            // compile-time tag and every test against it in this body is a
            // constant expression. `if constexpr` then removes the 14 dead profile
            // arms from each of the 15 instantiations, which is what makes the ~57
            // per-fragment `profile ==` compares disappear.
            constexpr Profile kProfile =
                static_cast<Profile>(decltype(profile_tag)::value);
            constexpr bool kPitchTex5Profile =
                kProfile == Profile::kPitchTex5ProjectedColor ||
                kProfile == Profile::kPitchTex5DiffuseColor;
            if (raster_failed) return;
            const TransformedVertex* a = &input_a;
            const TransformedVertex* b = &input_b;
            const TransformedVertex* c = &input_c;
            for (const TransformedVertex* vertex : {a, b, c}) {
                if (!std::isfinite(vertex->position[0]) ||
                    !std::isfinite(vertex->position[1]) ||
                    !std::isfinite(vertex->position[2])) {
                    return;
                }
            }

            auto fixed_xy = [](float value) -> int64_t {
                return static_cast<int64_t>(std::llround(value * 16.0f));
            };
            int64_t ax = fixed_xy(a->position[0]);
            int64_t ay = fixed_xy(a->position[1]);
            int64_t bx = fixed_xy(b->position[0]);
            int64_t by = fixed_xy(b->position[1]);
            int64_t cx = fixed_xy(c->position[0]);
            int64_t cy = fixed_xy(c->position[1]);
            auto edge = [](int64_t x0, int64_t y0,
                           int64_t x1, int64_t y1,
                           int64_t px, int64_t py) -> int64_t {
                return (x1 - x0) * (py - y0) -
                       (y1 - y0) * (px - x0);
            };
            int64_t area = edge(ax, ay, bx, by, cx, cy);
            if constexpr (kPitchTex5Profile) {
                if (area >= 0) return;  // FRONT culling.
                std::swap(b, c);
                std::swap(bx, cx);
                std::swap(by, cy);
                area = -area;
            } else if constexpr (kProfile == Profile::kInlineLinearBlitColor) {
                if (area < 0) {
                    std::swap(b, c);
                    std::swap(bx, cx);
                    std::swap(by, cy);
                    area = -area;  // Culling is disabled for this blit.
                } else if (area <= 0) {
                    return;  // CW is front-facing in the witnessed culled states.
                }
            } else if (area <= 0) {
                return;  // CW is front-facing in the witnessed culled states.
            }
            if (area == 0) return;

            // Stage (e2), beads-xbox.1.15.9: a TRIANGLE counter is per primitive,
            // not per destination span. Every part runs the whole primitive loop
            // and repeats this setup -- the culling and degeneracy tests above
            // read only latched vertex data, so every part reaches this point for
            // exactly the same triangles -- and counting on each would multiply
            // the tally by `parts`. It did: the threads 0 vs 4 byte-lock caught
            // all fourteen per-profile triangle counters at exactly 4x (radial
            // 7 293 635 -> 29 174 540) with every fragment, pass, write, sample
            // and batch counter, and all 24 checkpoint framebuffers, identical.
            // The per-FRAGMENT ladder below stays per-part, because each part
            // rasterizes its own rows and those sums add.
            if (part == 0) {
            if constexpr (kProfile == Profile::kPitchZ16Depth)
                ++rasterized_triangles_;
            else if constexpr (kProfile == Profile::kSwizzledRadialColor)
                ++radial_color_triangles_;
            else if constexpr (kProfile == Profile::kSwizzledTexturedColor)
                ++textured_color_triangles_;
            else if constexpr (kProfile == Profile::kPitchTexturedColor)
                ++pitch_textured_color_triangles_;
            else if constexpr (kProfile == Profile::kSwizzledProjectedColor)
                ++projected_color_triangles_;
            else if constexpr (kProfile == Profile::kPitchProjectedColor)
                ++pitch_projected_color_triangles_;
            else if constexpr (kProfile == Profile::kPitchMultiTextureColor)
                ++pitch_multi_texture_triangles_;
            else if constexpr (kProfile == Profile::kPitchThreeTextureColor)
                ++pitch_three_texture_triangles_;
            else if constexpr (kPitchTex5Profile)
                ++pitch_tex5_triangles_;
            else if constexpr (kProfile == Profile::kInlineLinearBlitColor)
                ++inline_linear_blit_triangles_;
            else if constexpr (kProfile == Profile::kPitchSingleTextureColor)
                ++pitch_single_texture_triangles_;
            else if constexpr (kProfile == Profile::kTex7FanColor)
                ++tex7_fan_triangles_;
            else if constexpr (kProfile == Profile::kTexfProjectedFanColor)
                ++texf_projected_fan_triangles_;
            else
                ++inline_color_triangles_;
            }
            const int32_t tri_xmin = std::max<int32_t>(
                static_cast<int32_t>(xmin),
                static_cast<int32_t>(std::ceil(
                    (static_cast<double>(std::min({ax, bx, cx})) - 8.0) /
                    16.0)));
            const int32_t tri_xmax = std::min<int32_t>(
                static_cast<int32_t>(xmax),
                static_cast<int32_t>(std::floor(
                    (static_cast<double>(std::max({ax, bx, cx})) - 8.0) /
                    16.0)));
            const int32_t tri_ymin = std::max<int32_t>(
                static_cast<int32_t>(ymin),
                static_cast<int32_t>(std::ceil(
                    (static_cast<double>(std::min({ay, by, cy})) - 8.0) /
                    16.0)));
            const int32_t tri_ymax = std::min<int32_t>(
                static_cast<int32_t>(ymax),
                static_cast<int32_t>(std::floor(
                    (static_cast<double>(std::max({ay, by, cy})) - 8.0) /
                    16.0)));
            if (tri_xmax < tri_xmin || tri_ymax < tri_ymin) return;

            const auto top_left = [](int64_t x0, int64_t y0,
                                     int64_t x1, int64_t y1) {
                const int64_t dx = x1 - x0;
                const int64_t dy = y1 - y0;
                return dy < 0 || (dy == 0 && dx > 0);
            };
            const bool edge0_top_left = top_left(bx, by, cx, cy);
            const bool edge1_top_left = top_left(cx, cy, ax, ay);
            const bool edge2_top_left = top_left(ax, ay, bx, by);

            const float x10 = static_cast<float>(bx - ax) / 16.0f;
            const float y10 = static_cast<float>(by - ay) / 16.0f;
            const float x20 = static_cast<float>(cx - ax) / 16.0f;
            const float y20 = static_cast<float>(cy - ay) / 16.0f;
            const float z10 = b->position[2] - a->position[2];
            const float z20 = c->position[2] - a->position[2];
            const float determinant = x10 * y20 - x20 * y10;
            const float dzdx = (z10 * y20 - z20 * y10) / determinant;
            const float dzdy = (z20 * x10 - z10 * x20) / determinant;
            const float max_z_slope =
                std::max(std::abs(dzdx), std::abs(dzdy));
            const float inverse_area = 1.0f / static_cast<float>(area);
            const double inverse_area_double =
                1.0 / static_cast<double>(area);
            const double inverse_position_w0 =
                1.0 / static_cast<double>(a->position[3]);
            const double inverse_position_w1 =
                1.0 / static_cast<double>(b->position[3]);
            const double inverse_position_w2 =
                1.0 / static_cast<double>(c->position[3]);

            // Per-triangle hoist (beads-xbox.1.13 Phase 1): a/b/c->diffuse is a
            // per-VERTEX value, invariant across every pixel this triangle
            // rasterizes, but the per-pixel color path below used to re-run the
            // isnan-fixup + clamp for all 3 vertices x 3 components on every single
            // fragment (radial-color alone rasterizes ~4.7K fragments/triangle on
            // the stock-dashboard probe). Precompute the fixed-up value once here;
            // the per-pixel loop below reads the cached float instead of
            // recomputing it. Bit-for-bit identical to the old inline computation
            // (same isnan+clamp on the same vertex field, just hoisted out of the
            // fragment loop), so this does not change rasterizer output.
            std::array<std::array<float, 3>, 3> vertex_diffuse_clamped{};
            for (size_t vtx = 0; vtx < 3; ++vtx) {
                const TransformedVertex* vertex = vtx == 0 ? a : (vtx == 1 ? b : c);
                for (size_t component = 0; component < 3; ++component) {
                    float value = vertex->diffuse[component];
                    if (std::isnan(value)) value = 1.0f;
                    vertex_diffuse_clamped[vtx][component] =
                        std::clamp(value, 0.0f, 1.0f);
                }
            }

            // ---- stage (e2) destination-span partition (beads-xbox.1.15.9) --
            // This part owns exactly the destination rows y with
            // y % parts == part. Expressed as a STRIDE rather than as an
            // ownership test inside the loop: it renders the identical row set
            // and never enters an iteration it would immediately skip, and at
            // parts == 1 it degenerates to `y = tri_ymin; y <= tri_ymax; y += 1`,
            // i.e. the loop that was here before. Disjoint rows are the whole
            // exactness argument -- see the enumeration in nv2a.hpp.
            int32_t y_first = tri_ymin;
            const int32_t y_step = static_cast<int32_t>(parts);
            if (parts > 1) {
                int32_t skew = (tri_ymin - static_cast<int32_t>(part)) % y_step;
                if (skew < 0) skew += y_step;
                if (skew != 0) y_first = tri_ymin + (y_step - skew);
            }
            for (int32_t y = y_first; y <= tri_ymax; y += y_step) {
                const int64_t py = static_cast<int64_t>(y) * 16 + 8;
                for (int32_t x = tri_xmin; x <= tri_xmax; ++x) {
                    const int64_t px = static_cast<int64_t>(x) * 16 + 8;
                    const int64_t w0 = edge(bx, by, cx, cy, px, py);
                    const int64_t w1 = edge(cx, cy, ax, ay, px, py);
                    const int64_t w2 = edge(ax, ay, bx, by, px, py);
                    if (w0 < 0 || (w0 == 0 && !edge0_top_left) ||
                        w1 < 0 || (w1 == 0 && !edge1_top_left) ||
                        w2 < 0 || (w2 == 0 && !edge2_top_left)) {
                        continue;
                    }
                    // Stage (b1): the 15-way per-fragment profile ladder that used
                    // to run here -- 8.59 G times per canonical run -- is now one
                    // register increment. The tally is folded into the same profile
                    // counter, through the same ladder, once per batch below the
                    // primitive-assembly switch. This is the only fragment-counter
                    // site in the loop and the fold runs on every path out of the
                    // assembly loop, so the totals are unchanged.
                    ++witnessed_fragment_count;
                    float depth =
                        (static_cast<float>(w0) * a->position[2] +
                         static_cast<float>(w1) * b->position[2] +
                         static_cast<float>(w2) * c->position[2]) *
                        inverse_area;
                    depth += polygon_bias + polygon_scale * max_z_slope;
                    if (!std::isfinite(depth) ||
                        depth < clip_min || depth > clip_max) {
                        continue;
                    }
                    if constexpr (kProfile == Profile::kPitchZ16Depth) {
                        const uint16_t incoming = static_cast<uint16_t>(
                            std::floor(std::clamp(depth, 0.0f, 65535.0f)));
                        const size_t offset =
                            static_cast<size_t>(y) * pitch +
                            static_cast<size_t>(x) * sizeof(uint16_t);
                        uint16_t stored = 0;
                        if (!depth_surface.read16(offset, stored)) {
                            fault_("NV2A depth-only raster DMA read failed");
                            raster_failed = true;
                            return;
                        }
                        if (incoming > stored) continue;  // LEQUAL
                        ++depth_test_passes_;
                        if (!depth_surface.write16(offset, incoming)) {
                            fault_("NV2A depth-only raster cache write failed");
                            raster_failed = true;
                            return;
                        }
                        if (hash_on) {
                            ++depth_writes_;
                            const uint32_t address =
                                depth_address + static_cast<uint32_t>(offset);
                            last_raster_address_ = address;
                            for (uint32_t word :
                                 {address, static_cast<uint32_t>(incoming)}) {
                                raster_output_hash_ ^= word;
                                raster_output_hash_ *= 1099511628211ull;
                            }
                        }
                        continue;
                    }
                    if constexpr (kProfile == Profile::kInlineLinearBlitColor) {
                        const size_t offset =
                            static_cast<size_t>(y) * pitch +
                            static_cast<size_t>(x) * sizeof(uint32_t);
                        const double lambda0 =
                            static_cast<double>(w0) /
                            static_cast<double>(area);
                        const double lambda1 =
                            static_cast<double>(w1) /
                            static_cast<double>(area);
                        const double lambda2 =
                            static_cast<double>(w2) /
                            static_cast<double>(area);
                        const double q0 = lambda0 / a->position[3];
                        const double q1 = lambda1 / b->position[3];
                        const double q2 = lambda2 / c->position[3];
                        const double qsum = q0 + q1 + q2;
                        if (!std::isfinite(qsum) || qsum == 0.0)
                            continue;

                        std::array<float, 3> coordinate{};
                        for (size_t component :
                             {size_t{0}, size_t{1}, size_t{3}}) {
                            const double value =
                                (q0 * a->texture[0][component] +
                                 q1 * b->texture[0][component] +
                                 q2 * c->texture[0][component]) / qsum;
                            if (!std::isfinite(value)) {
                                fault_(
                                    "NV2A inline linear-blit varying is non-finite");
                                raster_failed = true;
                                return;
                            }
                            coordinate[component == 3 ? 2 : component] =
                                static_cast<float>(value);
                        }
                        if (coordinate[2] == 0.0f) {
                            fault_(
                                "NV2A inline linear-blit texture W is zero");
                            raster_failed = true;
                            return;
                        }
                        std::array<float, 3> texture{};
                        if (!sample_linear_texture(
                                linear_blit_texture_data,
                                linear_blit_texture_width,
                                linear_blit_texture_height,
                                linear_blit_texture_pitch,
                                coordinate[0] / coordinate[2],
                                coordinate[1] / coordinate[2],
                                texture)) {
                            fault_(
                                "NV2A inline linear-blit projection is invalid");
                            raster_failed = true;
                            return;
                        }
                        ++inline_linear_blit_samples_;

                        auto vertex_alpha =
                            [](const TransformedVertex* vertex) {
                                float value = vertex->diffuse[3];
                                if (std::isnan(value)) value = 1.0f;
                                return std::clamp(value, 0.0f, 1.0f);
                            };
                        const double interpolated_alpha =
                            (q0 * vertex_alpha(a) +
                             q1 * vertex_alpha(b) +
                             q2 * vertex_alpha(c)) / qsum;
                        if (!std::isfinite(interpolated_alpha)) {
                            fault_(
                                "NV2A inline linear-blit alpha is non-finite");
                            raster_failed = true;
                            return;
                        }
                        auto pack_unorm8 = [](float value) {
                            return static_cast<uint32_t>(std::floor(
                                std::clamp(value, 0.0f, 1.0f) *
                                    255.0f +
                                0.5f));
                        };
                        const uint32_t color =
                            (pack_unorm8(
                                 static_cast<float>(
                                     interpolated_alpha)) << 24) |
                            (pack_unorm8(texture[0]) << 16) |
                            (pack_unorm8(texture[1]) << 8) |
                            pack_unorm8(texture[2]);
                        if (!color_surface.write32(offset, color)) {
                            fault_(
                                "NV2A inline linear-blit cache write failed");
                            raster_failed = true;
                            return;
                        }
                        if (hash_on) {
                            ++inline_linear_blit_writes_;
                            const uint32_t address =
                                color_address +
                                static_cast<uint32_t>(offset);
                            last_inline_linear_blit_raster_address_ =
                                address;
                            for (uint32_t word : {address, color}) {
                                inline_linear_blit_output_hash_ ^= word;
                                inline_linear_blit_output_hash_ *=
                                    1099511628211ull;
                            }
                        }
                        continue;
                    }
                    if constexpr (kProfile == Profile::kPitchMultiTextureColor ||
                                  kProfile == Profile::kPitchThreeTextureColor) {
                        const size_t offset =
                            static_cast<size_t>(y) * pitch +
                            static_cast<size_t>(x) * sizeof(uint32_t);
                        const uint32_t incoming = static_cast<uint32_t>(
                            std::floor(std::clamp(
                                depth, 0.0f, 16777215.0f)));
                        uint32_t stored_depth_stencil = 0;
                        if (!depth_surface.read32(
                                offset, stored_depth_stencil)) {
                            fault_(
                                "NV2A pitch cube-pair zeta DMA read failed");
                            raster_failed = true;
                            return;
                        }
                        if (incoming > (stored_depth_stencil >> 8))
                            continue;  // LEQUAL
                        if constexpr (kProfile == Profile::kPitchMultiTextureColor)
                            ++pitch_multi_texture_depth_passes_;
                        else
                            ++pitch_three_texture_depth_passes_;

                        const double lambda0 =
                            static_cast<double>(w0) /
                            static_cast<double>(area);
                        const double lambda1 =
                            static_cast<double>(w1) /
                            static_cast<double>(area);
                        const double lambda2 =
                            static_cast<double>(w2) /
                            static_cast<double>(area);
                        const double q0 = lambda0 / a->position[3];
                        const double q1 = lambda1 / b->position[3];
                        const double q2 = lambda2 / c->position[3];
                        const double qsum = q0 + q1 + q2;
                        if (!std::isfinite(qsum) || qsum == 0.0)
                            continue;

                        std::array<double, 3> direction0_double{};
                        std::array<double, 3> direction1_double{};
                        for (size_t component = 0; component < 3;
                             ++component) {
                            const double t0 =
                                (q0 * a->texture[0][component] +
                                 q1 * b->texture[0][component] +
                                 q2 * c->texture[0][component]) / qsum;
                            const double t1 =
                                (q0 * a->texture[1][component] +
                                 q1 * b->texture[1][component] +
                                 q2 * c->texture[1][component]) / qsum;
                            if (!std::isfinite(t0) ||
                                !std::isfinite(t1)) {
                                fault_(
                                    "NV2A pitch cube-pair varying is non-finite");
                                raster_failed = true;
                                return;
                            }
                            direction0_double[component] = t0;
                            direction1_double[component] = t1;
                        }
                        auto narrow_cube_direction =
                            [&](const std::array<double, 3>& source,
                                std::array<float, 3>& destination,
                                uint64_t* rescale_counter) {
                                const double max_abs = std::max(
                                    {std::abs(source[0]), std::abs(source[1]),
                                     std::abs(source[2])});
                                const bool exceeds_float =
                                    max_abs > std::numeric_limits<float>::max();
                                bool all_float_zero = source[0] != 0.0 ||
                                    source[1] != 0.0 || source[2] != 0.0;
                                if (all_float_zero) {
                                    all_float_zero =
                                        static_cast<float>(source[0]) == 0.0f &&
                                        static_cast<float>(source[1]) == 0.0f &&
                                        static_cast<float>(source[2]) == 0.0f;
                                }
                                const bool rescale = rescale_counter &&
                                    (exceeds_float || all_float_zero);
                                const double scale = rescale ? max_abs : 1.0;
                                if (rescale) {
                                    ++*rescale_counter;
                                    last_pitch_three_rescale_factor3_ =
                                        raster_state[kRasterCombinerFactor0_0 + 3];
                                }
                                for (size_t component = 0; component < 3;
                                     ++component) {
                                    destination[component] = static_cast<float>(
                                        source[component] / scale);
                                }
                            };
                        std::array<float, 3> direction0{};
                        std::array<float, 3> direction1{};
                        narrow_cube_direction(
                            direction0_double, direction0,
                            kProfile == Profile::kPitchThreeTextureColor
                                ? &pitch_three_texture_rescale_direction0_
                                : nullptr);
                        narrow_cube_direction(
                            direction1_double, direction1,
                            kProfile == Profile::kPitchThreeTextureColor
                                ? &pitch_three_texture_rescale_direction1_
                                : nullptr);
                        std::array<float, 3> texture0{};
                        std::array<float, 3> texture1{};
                        uint64_t* zero_direction0 =
                            kProfile == Profile::kPitchThreeTextureColor
                                ? &pitch_three_texture_zero_direction0_
                                : nullptr;
                        uint64_t* zero_direction1 =
                            kProfile == Profile::kPitchThreeTextureColor
                                ? &pitch_three_texture_zero_direction1_
                                : nullptr;
                        uint64_t* xy_tie_direction0 =
                            kProfile == Profile::kPitchThreeTextureColor
                                ? &pitch_three_texture_xy_tie_direction0_
                                : nullptr;
                        uint64_t* xy_tie_direction1 =
                            kProfile == Profile::kPitchThreeTextureColor
                                ? &pitch_three_texture_xy_tie_direction1_
                                : nullptr;
                        if (!sample_cube_texture(
                                multi_texture_data,
                                *multi_texture_swizzle,
                                multi_texture_width,
                                multi_texture_height,
                                direction0, texture0, zero_direction0,
                                xy_tie_direction0) ||
                            !sample_cube_texture(
                                multi_texture_data,
                                *multi_texture_swizzle,
                                multi_texture_width,
                                multi_texture_height,
                                direction1, texture1, zero_direction1,
                                xy_tie_direction1)) {
                            fault_(
                                "NV2A pitch cube-pair remap is invalid");
                            raster_failed = true;
                            return;
                        }
                        if constexpr (kProfile == Profile::kPitchMultiTextureColor)
                            pitch_multi_texture_samples_ += 2;
                        else
                            pitch_three_texture_samples_ += 2;

                        float dot = 0.0f;
                        for (size_t component = 0; component < 3;
                             ++component) {
                            dot +=
                                (2.0f * std::max(
                                    texture0[component], 0.0f) - 1.0f) *
                                (2.0f * std::max(
                                    texture1[component], 0.0f) - 1.0f);
                        }
                        const float r1 = std::clamp(
                            std::clamp(dot, -1.0f, 1.0f),
                            0.0f, 1.0f);
                        const float inverted = 1.0f - r1;
                        const float source_alpha =
                            1.0f - std::clamp(
                                inverted * inverted, 0.0f, 1.0f);
                        if constexpr (kProfile == Profile::kPitchMultiTextureColor) {
                            if (std::lround(source_alpha * 255.0f) < 1)
                                continue;  // GEQUAL alpha reference 1.
                        }
                        if constexpr (kProfile == Profile::kPitchMultiTextureColor)
                            ++pitch_multi_texture_alpha_passes_;

                        uint32_t destination_pixel = 0;
                        if (!color_surface.read32(
                                offset, destination_pixel)) {
                            fault_(
                                "NV2A pitch cube-pair color DMA read failed");
                            raster_failed = true;
                            return;
                        }
                        const uint32_t stage3_factor =
                            raster_state[kRasterCombinerFactor0_0 + 3];
                        const uint32_t stage4_factor =
                            raster_state[kRasterCombinerFactor0_0 + 4];
                        auto factor_component =
                            [](uint32_t factor, uint32_t shift) {
                                return static_cast<float>(
                                           (factor >> shift) & 0xFFu) /
                                       255.0f;
                            };
                        const std::array<float, 4> source{{
                            std::clamp(
                                factor_component(stage4_factor, 16) +
                                    source_alpha *
                                        factor_component(stage3_factor, 16),
                                0.0f, 1.0f),
                            std::clamp(
                                factor_component(stage4_factor, 8) +
                                    source_alpha *
                                        factor_component(stage3_factor, 8),
                                0.0f, 1.0f),
                            std::clamp(
                                factor_component(stage4_factor, 0) +
                                    source_alpha *
                                        factor_component(stage3_factor, 0),
                                0.0f, 1.0f),
                            source_alpha,
                        }};
                        const std::array<float, 4> destination{{
                            static_cast<float>(
                                (destination_pixel >> 16) & 0xFFu) /
                                255.0f,
                            static_cast<float>(
                                (destination_pixel >> 8) & 0xFFu) /
                                255.0f,
                            static_cast<float>(
                                destination_pixel & 0xFFu) / 255.0f,
                            static_cast<float>(
                                (destination_pixel >> 24) & 0xFFu) /
                                255.0f,
                        }};
                        auto pack_unorm8 = [](float value) {
                            return static_cast<uint32_t>(std::floor(
                                std::clamp(value, 0.0f, 1.0f) *
                                    255.0f +
                                0.5f));
                        };
                        std::array<uint32_t, 4> blended{};
                        for (size_t component = 0; component < 4;
                             ++component) {
                            blended[component] = pack_unorm8(
                                source[component] * source_alpha +
                                destination[component] *
                                    (1.0f - source_alpha));
                        }
                        const uint32_t color =
                            (blended[3] << 24) |
                            (blended[0] << 16) |
                            (blended[1] << 8) |
                            blended[2];
                        const uint32_t depth_stencil =
                            (incoming << 8) |
                            (stored_depth_stencil & 0xFFu);
                        if (!color_surface.write32(offset, color) ||
                            !depth_surface.write32(
                                offset, depth_stencil)) {
                            fault_(
                                "NV2A pitch cube-pair cache write failed");
                            raster_failed = true;
                            return;
                        }
                        if (hash_on) {
                            if constexpr (kProfile == Profile::kPitchMultiTextureColor) {
                                ++pitch_multi_texture_color_writes_;
                                ++pitch_multi_texture_depth_writes_;
                            } else {
                                ++pitch_three_texture_color_writes_;
                                ++pitch_three_texture_depth_writes_;
                            }
                            const uint32_t color_pixel_address =
                                color_address +
                                static_cast<uint32_t>(offset);
                            const uint32_t depth_pixel_address =
                                depth_address +
                                static_cast<uint32_t>(offset);
                            if constexpr (kProfile == Profile::kPitchMultiTextureColor)
                                last_pitch_multi_texture_raster_address_ =
                                    color_pixel_address;
                            else
                                last_pitch_three_texture_raster_address_ =
                                    color_pixel_address;
                            for (uint32_t word :
                                 {color_pixel_address, color,
                                  depth_pixel_address, depth_stencil}) {
                                uint64_t& output_hash =
                                    kProfile == Profile::kPitchMultiTextureColor
                                        ? pitch_multi_texture_output_hash_
                                        : pitch_three_texture_output_hash_;
                                output_hash ^= word;
                                output_hash *= 1099511628211ull;
                            }
                        }
                        continue;
                    }
                    if constexpr (kPitchTex5Profile) {
                        const size_t offset =
                            static_cast<size_t>(y) * pitch +
                            static_cast<size_t>(x) * sizeof(uint32_t);
                        const uint32_t incoming = static_cast<uint32_t>(
                            std::floor(std::clamp(
                                depth, 0.0f, 16777215.0f)));
                        uint32_t stored_depth_stencil = 0;
                        if (!depth_surface.read32(
                                offset, stored_depth_stencil)) {
                            fault_("NV2A pitch tex5 zeta DMA read failed");
                            raster_failed = true;
                            return;
                        }
                        if (incoming > (stored_depth_stencil >> 8))
                            continue;  // LEQUAL
                        ++pitch_tex5_depth_passes_;

                        const double lambda0 =
                            static_cast<double>(w0) /
                            static_cast<double>(area);
                        const double lambda1 =
                            static_cast<double>(w1) /
                            static_cast<double>(area);
                        const double lambda2 =
                            static_cast<double>(w2) /
                            static_cast<double>(area);
                        const double q0 = lambda0 / a->position[3];
                        const double q1 = lambda1 / b->position[3];
                        const double q2 = lambda2 / c->position[3];
                        const double qsum = q0 + q1 + q2;
                        if (!std::isfinite(qsum) || qsum == 0.0)
                            continue;

                        std::array<float, 4> source{};
                        if constexpr (kProfile ==
                                      Profile::kPitchTex5ProjectedColor) {
                            std::array<float, 3> coordinate{};
                            for (size_t component :
                                 {size_t{0}, size_t{1}, size_t{3}}) {
                                const double value =
                                    (q0 * a->texture[0][component] +
                                     q1 * b->texture[0][component] +
                                     q2 * c->texture[0][component]) /
                                    qsum;
                                if (!std::isfinite(value)) {
                                    fault_(
                                        "NV2A pitch tex5 varying is non-finite");
                                    raster_failed = true;
                                    return;
                                }
                                coordinate[
                                    component == 3 ? 2 : component] =
                                    static_cast<float>(value);
                            }
                            if (coordinate[2] == 0.0f) {
                                fault_("NV2A pitch tex5 W is zero");
                                raster_failed = true;
                                return;
                            }
                            std::array<float, 3> texture{};
                            if (!sample_texture(
                                    pitch_tex5_texture_data,
                                    *pitch_tex5_texture_swizzle,
                                    pitch_tex5_texture_width,
                                    pitch_tex5_texture_height,
                                    coordinate[0] / coordinate[2],
                                    coordinate[1] / coordinate[2],
                                    TextureAddressMode::kBorder,
                                    0xFF000000, texture, nullptr)) {
                                fault_(
                                    "NV2A pitch tex5 projection is invalid");
                                raster_failed = true;
                                return;
                            }
                            ++pitch_tex5_samples_;
                            source = {{
                                texture[0], texture[1], texture[2], 1.0f,
                            }};
                        } else {
                            for (size_t component = 0; component < 4;
                                 ++component) {
                                const double value =
                                    (q0 * a->diffuse[component] +
                                     q1 * b->diffuse[component] +
                                     q2 * c->diffuse[component]) / qsum;
                                if (!std::isfinite(value)) {
                                    fault_(
                                        "NV2A pitch tex5 diffuse is non-finite");
                                    raster_failed = true;
                                    return;
                                }
                                source[component] =
                                    std::clamp(
                                        static_cast<float>(value),
                                        0.0f, 1.0f);
                            }
                        }
                        auto pack_unorm8 = [](float value) {
                            return static_cast<uint32_t>(std::floor(
                                std::clamp(value, 0.0f, 1.0f) *
                                    255.0f +
                                0.5f));
                        };
                        const uint32_t color =
                            (pack_unorm8(source[3]) << 24) |
                            (pack_unorm8(source[0]) << 16) |
                            (pack_unorm8(source[1]) << 8) |
                            pack_unorm8(source[2]);
                        const uint32_t depth_stencil =
                            (incoming << 8) |
                            (stored_depth_stencil & 0xFFu);
                        if (!color_surface.write32(offset, color) ||
                            !depth_surface.write32(
                                offset, depth_stencil)) {
                            fault_("NV2A pitch tex5 cache write failed");
                            raster_failed = true;
                            return;
                        }
                        if (hash_on) {
                            ++pitch_tex5_color_writes_;
                            ++pitch_tex5_depth_writes_;
                            const uint32_t color_pixel_address =
                                color_address +
                                static_cast<uint32_t>(offset);
                            const uint32_t depth_pixel_address =
                                depth_address +
                                static_cast<uint32_t>(offset);
                            last_pitch_tex5_raster_address_ =
                                color_pixel_address;
                            for (uint32_t word :
                                 {color_pixel_address, color,
                                  depth_pixel_address, depth_stencil}) {
                                pitch_tex5_output_hash_ ^= word;
                                pitch_tex5_output_hash_ *=
                                    1099511628211ull;
                            }
                        }
                        continue;
                    }
                    if constexpr (kProfile == Profile::kTexfProjectedFanColor) {
                        const size_t offset = static_cast<size_t>(y) * pitch +
                            static_cast<size_t>(x) * sizeof(uint32_t);
                        // The exact captured six-stage combiner reduces to
                        // R0.rgb=0 and R0.a=1 (Xemu psh.c register semantics).
                        // All four PROJECT2D samples are therefore output-inert;
                        // do not DMA-read or interpolate them just to reproduce
                        // this black SRC_ALPHA/ONE coverage pass.
                        ++texf_projected_fan_alpha_passes_;
                        uint32_t destination = 0;
                        if (!color_surface.read32(offset, destination)) {
                            fault_("NV2A tex=f color DMA read failed");
                            raster_failed = true;
                            return;
                        }
                        const uint32_t color = destination | 0xFF000000u;
                        if (!color_surface.write32(offset, color)) {
                            fault_("NV2A tex=f color DMA write failed");
                            raster_failed = true;
                            return;
                        }
                        if (hash_on) {
                            ++texf_projected_fan_writes_;
                            const uint32_t address =
                                color_address + static_cast<uint32_t>(offset);
                            last_texf_projected_fan_raster_address_ = address;
                            for (uint32_t word : {address, color}) {
                                texf_projected_fan_output_hash_ ^= word;
                                texf_projected_fan_output_hash_ *= 1099511628211ull;
                            }
                        }
                        continue;
                    }
                    if constexpr (kProfile == Profile::kPitchSingleTextureColor ||
                                  kProfile == Profile::kTex7FanColor) {
                        const size_t offset =
                            static_cast<size_t>(y) * pitch +
                            static_cast<size_t>(x) * sizeof(uint32_t);
                        const uint32_t incoming = static_cast<uint32_t>(
                            std::floor(std::clamp(
                                depth, 0.0f, 16777215.0f)));
                        uint32_t stored_depth_stencil = 0;
                        if (!depth_surface.read32(
                                offset, stored_depth_stencil)) {
                            fault_(
                                "NV2A pitch single-texture zeta DMA read failed");
                            raster_failed = true;
                            return;
                        }
                        if (incoming > (stored_depth_stencil >> 8))
                            continue;  // LEQUAL, read-only depth.
                        if constexpr (kProfile == Profile::kPitchSingleTextureColor)
                            ++pitch_single_texture_depth_passes_;
                        else
                            ++tex7_fan_depth_passes_;

                        const double lambda0 =
                            static_cast<double>(w0) /
                            static_cast<double>(area);
                        const double lambda1 =
                            static_cast<double>(w1) /
                            static_cast<double>(area);
                        const double lambda2 =
                            static_cast<double>(w2) /
                            static_cast<double>(area);
                        const double q0 = lambda0 / a->position[3];
                        const double q1 = lambda1 / b->position[3];
                        const double q2 = lambda2 / c->position[3];
                        const double qsum = q0 + q1 + q2;
                        if (!std::isfinite(qsum) || qsum == 0.0)
                            continue;
                        std::array<float, 3> coordinate{};
                        for (size_t component : {size_t{0}, size_t{1},
                                                 size_t{3}}) {
                            const double value =
                                (q0 * a->texture[0][component] +
                                 q1 * b->texture[0][component] +
                                 q2 * c->texture[0][component]) / qsum;
                            if (!std::isfinite(value)) {
                                fault_(
                                    "NV2A pitch single-texture varying is non-finite");
                                raster_failed = true;
                                return;
                            }
                            coordinate[component == 3 ? 2 : component] =
                                static_cast<float>(value);
                        }
                        if (coordinate[2] == 0.0f) {
                            fault_(
                                "NV2A pitch single-texture W is zero");
                            raster_failed = true;
                            return;
                        }
                        std::array<float, 3> texture{};
                        if (!sample_texture(
                                single_texture_data,
                                *single_texture_swizzle,
                                single_texture_width,
                                single_texture_height,
                                coordinate[0] / coordinate[2],
                                coordinate[1] / coordinate[2],
                                kProfile == Profile::kPitchSingleTextureColor
                                    ? TextureAddressMode::kBorder
                                    : TextureAddressMode::kClampToEdge,
                                0xFF000000, texture, nullptr)) {
                            fault_(
                                "NV2A pitch single-texture projection is invalid");
                            raster_failed = true;
                            return;
                        }
                        if constexpr (kProfile == Profile::kPitchSingleTextureColor)
                            ++pitch_single_texture_samples_;
                        else
                            ++tex7_fan_samples_;
                        auto pack_unorm8 = [](float value) {
                            return static_cast<uint32_t>(std::floor(
                                std::clamp(value, 0.0f, 1.0f) *
                                    255.0f +
                                0.5f));
                        };
                        const std::array<float, 3> source =
                            kProfile == Profile::kTex7FanColor
                                ? std::array<float, 3>{{texture[0] * (160.0f / 255.0f),
                                                        texture[1],
                                                        texture[2] * (64.0f / 255.0f)}}
                                : texture;
                        uint32_t destination = 0;
                        if constexpr (kProfile == Profile::kTex7FanColor) {
                            if (!color_surface.read32(offset, destination)) {
                                fault_("NV2A tex7 fan color DMA read failed");
                                raster_failed = true;
                                return;
                            }
                        }
                        auto add_saturate = [&](float value, uint32_t dst) {
                            return pack_unorm8(value +
                                static_cast<float>(dst) / 255.0f);
                        };
                        const uint32_t color = kProfile == Profile::kTex7FanColor
                            ? (add_saturate(1.0f,
                                            (destination >> 24) & 0xFFu) << 24) |
                                  (add_saturate(source[0],
                                                (destination >> 16) & 0xFFu) << 16) |
                                  (add_saturate(source[1],
                                                (destination >> 8) & 0xFFu) << 8) |
                                  add_saturate(source[2], destination & 0xFFu)
                            : 0xFF000000u |
                                  (pack_unorm8(source[0]) << 16) |
                                  (pack_unorm8(source[1]) << 8) |
                                  pack_unorm8(source[2]);
                        if (!color_surface.write32(offset, color)) {
                            fault_(
                                "NV2A pitch single-texture cache write failed");
                            raster_failed = true;
                            return;
                        }
                        if (hash_on) {
                            if constexpr (kProfile == Profile::kPitchSingleTextureColor)
                                ++pitch_single_texture_writes_;
                            else
                                ++tex7_fan_writes_;
                            const uint32_t address =
                                color_address + static_cast<uint32_t>(offset);
                            if constexpr (kProfile == Profile::kPitchSingleTextureColor) {
                                last_pitch_single_texture_raster_address_ = address;
                                for (uint32_t word : {address, color}) {
                                    pitch_single_texture_output_hash_ ^= word;
                                    pitch_single_texture_output_hash_ *= 1099511628211ull;
                                }
                            } else {
                                last_tex7_fan_raster_address_ = address;
                                for (uint32_t word : {address, color}) {
                                    tex7_fan_output_hash_ ^= word;
                                    tex7_fan_output_hash_ *= 1099511628211ull;
                                }
                            }
                        }
                        continue;
                    }
                    if constexpr (kProfile == Profile::kPitchAdditiveColor) {
                        const double lambda0 =
                            static_cast<double>(w0) /
                            static_cast<double>(area);
                        const double lambda1 =
                            static_cast<double>(w1) /
                            static_cast<double>(area);
                        const double lambda2 =
                            static_cast<double>(w2) /
                            static_cast<double>(area);
                        const double q0 = lambda0 / a->position[3];
                        const double q1 = lambda1 / b->position[3];
                        const double q2 = lambda2 / c->position[3];
                        const double qsum = q0 + q1 + q2;
                        if (!std::isfinite(qsum) || qsum == 0.0) continue;
                        std::array<float, 3> coordinate{};
                        for (size_t component : {size_t{0}, size_t{1},
                                                 size_t{3}}) {
                            const double value =
                                (q0 * a->texture[0][component] +
                                 q1 * b->texture[0][component] +
                                 q2 * c->texture[0][component]) / qsum;
                            if (!std::isfinite(value)) {
                                fault_("NV2A inline-color varying is non-finite");
                                raster_failed = true;
                                return;
                            }
                            coordinate[component == 3 ? 2 : component] =
                                static_cast<float>(value);
                        }
                        if (coordinate[2] == 0.0f) {
                            fault_("NV2A inline-color texture W is zero");
                            raster_failed = true;
                            return;
                        }
                        std::array<float, 3> texture{};
                        float texture_alpha = 0.0f;
                        if (!sample_texture(
                                inline_texture_data,
                                *inline_texture_swizzle,
                                inline_texture_width, inline_texture_height,
                                coordinate[0] / coordinate[2],
                                coordinate[1] / coordinate[2],
                                TextureAddressMode::kClampToEdge, 0,
                                texture, &texture_alpha)) {
                            fault_("NV2A inline-color projection is invalid");
                            raster_failed = true;
                            return;
                        }
                        ++inline_texture_samples_;
                        const uint32_t factor =
                            raster_state[kRasterCombinerFactor0_0];
                        const std::array<float, 4> constant{{
                            static_cast<float>((factor >> 16) & 0xFFu) /
                                255.0f,
                            static_cast<float>((factor >> 8) & 0xFFu) /
                                255.0f,
                            static_cast<float>(factor & 0xFFu) / 255.0f,
                            static_cast<float>((factor >> 24) & 0xFFu) /
                                255.0f,
                        }};
                        std::array<float, 4> source{{
                            std::clamp(texture[0] * constant[0],
                                       -1.0f, 1.0f),
                            std::clamp(texture[1] * constant[1],
                                       -1.0f, 1.0f),
                            std::clamp(texture[2] * constant[2],
                                       -1.0f, 1.0f),
                            std::clamp(texture_alpha * constant[3],
                                       -1.0f, 1.0f),
                        }};
                        const uint32_t source_alpha8 =
                            static_cast<uint32_t>(std::floor(
                                std::clamp(source[3], 0.0f, 1.0f) *
                                    255.0f +
                                0.5f));
                        if (source_alpha8 < 1) continue;
                        ++inline_color_alpha_passes_;

                        const size_t offset =
                            static_cast<size_t>(y) * pitch +
                            static_cast<size_t>(x) * sizeof(uint32_t);
                        if (inline_depth_test) {
                            uint32_t stored_depth_stencil = 0;
                            if (!depth_surface.read32(
                                    offset, stored_depth_stencil)) {
                                fault_(
                                    "NV2A inline-color zeta DMA read failed");
                                raster_failed = true;
                                return;
                            }
                            const uint32_t incoming =
                                static_cast<uint32_t>(std::floor(std::clamp(
                                    depth, 0.0f, 16777215.0f)));
                            if (incoming >= (stored_depth_stencil >> 8))
                                continue;  // LESS
                        }
                        uint32_t destination_pixel = 0;
                        if (!color_surface.read32(offset,
                                                  destination_pixel)) {
                            fault_("NV2A inline-color DMA read failed");
                            raster_failed = true;
                            return;
                        }
                        const std::array<float, 4> destination{{
                            static_cast<float>(
                                (destination_pixel >> 16) & 0xFFu) /
                                255.0f,
                            static_cast<float>(
                                (destination_pixel >> 8) & 0xFFu) /
                                255.0f,
                            static_cast<float>(destination_pixel & 0xFFu) /
                                255.0f,
                            static_cast<float>(
                                (destination_pixel >> 24) & 0xFFu) /
                                255.0f,
                        }};
                        auto pack_unorm8 = [](float value) {
                            return static_cast<uint32_t>(std::floor(
                                std::clamp(value, 0.0f, 1.0f) * 255.0f +
                                0.5f));
                        };
                        std::array<uint32_t, 4> blended{};
                        for (size_t component = 0; component < 4;
                             ++component) {
                            blended[component] = pack_unorm8(
                                source[component] * source[3] +
                                destination[component]);
                        }
                        const uint32_t color =
                            (blended[3] << 24) | (blended[0] << 16) |
                            (blended[1] << 8) | blended[2];
                        if (!color_surface.write32(offset, color)) {
                            fault_("NV2A inline-color cache write failed");
                            raster_failed = true;
                            return;
                        }
                        if (hash_on) {
                            ++inline_color_writes_;
                            const uint32_t address =
                                color_address + static_cast<uint32_t>(offset);
                            last_inline_raster_address_ = address;
                            for (uint32_t word : {address, color}) {
                                inline_color_output_hash_ ^= word;
                                inline_color_output_hash_ *= 1099511628211ull;
                            }
                        }
                        continue;
                    }

                    const size_t offset =
                        (kProfile == Profile::kPitchTexturedColor ||
                         kProfile == Profile::kPitchProjectedColor)
                            ? static_cast<size_t>(y) * pitch +
                                  static_cast<size_t>(x) * sizeof(uint32_t)
                            : static_cast<size_t>(surface_swizzle->index(
                                  static_cast<uint32_t>(x),
                                  static_cast<uint32_t>(y))) *
                                  sizeof(uint32_t);
                    const uint32_t incoming = static_cast<uint32_t>(
                        std::floor(std::clamp(depth, 0.0f, 16777215.0f)));
                    uint32_t stored_depth_stencil = 0;
                    if (!read_depth32(offset, stored_depth_stencil)) {
                        fault_("NV2A color zeta DMA read failed");
                        raster_failed = true;
                        return;
                    }
                    if (incoming > (stored_depth_stencil >> 8)) continue;
                    if constexpr (kProfile == Profile::kSwizzledRadialColor)
                        ++radial_color_depth_passes_;
                    else if constexpr (kProfile == Profile::kSwizzledTexturedColor)
                        ++textured_color_depth_passes_;
                    else if constexpr (kProfile == Profile::kPitchTexturedColor)
                        ++pitch_textured_color_depth_passes_;
                    else if constexpr (kProfile == Profile::kPitchProjectedColor)
                        ++pitch_projected_color_depth_passes_;
                    else
                        ++projected_color_depth_passes_;

                    const double lambda0 =
                        static_cast<double>(w0) * inverse_area_double;
                    const double lambda1 =
                        static_cast<double>(w1) * inverse_area_double;
                    const double lambda2 =
                        static_cast<double>(w2) * inverse_area_double;
                    const double q0 = lambda0 * inverse_position_w0;
                    const double q1 = lambda1 * inverse_position_w1;
                    const double q2 = lambda2 * inverse_position_w2;
                    const double qsum = q0 + q1 + q2;
                    if (!std::isfinite(qsum) || qsum == 0.0) continue;

                    std::array<float, 3> diffuse{};
                    for (size_t component = 0; component < 3; ++component) {
                        const double d =
                            (q0 * vertex_diffuse_clamped[0][component] +
                             q1 * vertex_diffuse_clamped[1][component] +
                             q2 * vertex_diffuse_clamped[2][component]) / qsum;
                        if (!std::isfinite(d)) {
                            fault_("NV2A color varying is non-finite");
                            raster_failed = true;
                            return;
                        }
                        diffuse[component] = static_cast<float>(d);
                    }

                    std::array<float, 3> output{};
                    if constexpr (kProfile == Profile::kSwizzledRadialColor) {
                        std::array<float, 3> texture{};
                        for (size_t component = 0; component < 3; ++component) {
                            const double t =
                                (q0 * a->texture[0][component] +
                                 q1 * b->texture[0][component] +
                                 q2 * c->texture[0][component]) / qsum;
                            if (!std::isfinite(t)) {
                                fault_("NV2A radial-color varying is non-finite");
                                raster_failed = true;
                                return;
                            }
                            texture[component] = static_cast<float>(t);
                        }
                        float dot = 0.0f;
                        for (float component : texture) {
                            const float expanded =
                                2.0f * std::max(component, 0.0f) - 1.0f;
                            dot += expanded * expanded;
                        }
                        const float r1 =
                            1.0f - std::clamp(dot, -1.0f, 1.0f);
                        const float radial = r1 * r1;
                        for (size_t component = 0; component < 3; ++component)
                            output[component] = diffuse[component] * radial;
                    } else {
                        std::array<float, 3> direction1{};
                        std::array<float, 3> direction2{};
                        for (size_t component = 0; component < 3; ++component) {
                            const double t1 =
                                (q0 * a->texture[1][component] +
                                 q1 * b->texture[1][component] +
                                 q2 * c->texture[1][component]) / qsum;
                            const double t2 =
                                (q0 * a->texture[2][component] +
                                 q1 * b->texture[2][component] +
                                 q2 * c->texture[2][component]) / qsum;
                            if (!std::isfinite(t1) || !std::isfinite(t2)) {
                                fault_("NV2A textured-color varying is non-finite");
                                raster_failed = true;
                                return;
                            }
                            direction1[component] = static_cast<float>(t1);
                            direction2[component] = static_cast<float>(t2);
                        }
                        std::array<float, 3> texture1{};
                        std::array<float, 3> texture2{};
                        if (!sample_cube_texture(
                                texture_data, *texture_swizzle,
                                texture_width, texture_height,
                                direction1, texture1) ||
                            !sample_cube_texture(
                                texture_data, *texture_swizzle,
                                texture_width, texture_height,
                                direction2, texture2)) {
                            fault_("NV2A textured-color cube remap is invalid");
                            raster_failed = true;
                            return;
                        }
                        float r0 = 0.0f;
                        float r1 = 0.0f;
                        constexpr bool textured_family =
                            kProfile == Profile::kSwizzledTexturedColor ||
                            kProfile == Profile::kPitchTexturedColor;
                        if constexpr (textured_family) {
                            if constexpr (kProfile == Profile::kPitchTexturedColor)
                                pitch_texture_samples_ += 2;
                            else
                                texture_samples_ += 2;
                            r0 = std::clamp(
                                2.0f * texture1[2] - 1.0f, -1.0f, 1.0f);
                            r1 = std::max(std::clamp(
                                2.0f * texture2[2] - 1.0f, -1.0f, 1.0f),
                                0.0f);
                            for (size_t stage = 0; stage < 5; ++stage)
                                r1 *= r1;
                        } else {
                            std::array<float, 3> projected_coordinate{};
                            for (size_t component : {size_t{0}, size_t{1},
                                                     size_t{3}}) {
                                const double t0 =
                                    (q0 * a->texture[0][component] +
                                     q1 * b->texture[0][component] +
                                     q2 * c->texture[0][component]) / qsum;
                                if (!std::isfinite(t0)) {
                                    fault_("NV2A projected-color varying is non-finite");
                                    raster_failed = true;
                                    return;
                                }
                                projected_coordinate[
                                    component == 3 ? 2 : component] =
                                    static_cast<float>(t0);
                            }
                            if (projected_coordinate[2] == 0.0f) {
                                fault_("NV2A projected-color texture W is zero");
                                raster_failed = true;
                                return;
                            }
                            std::array<float, 3> texture0{};
                            if (!sample_texture(
                                    projected_texture_data,
                                    *projected_texture_swizzle,
                                    projected_texture_width,
                                    projected_texture_height,
                                    projected_coordinate[0] /
                                        projected_coordinate[2],
                                    projected_coordinate[1] /
                                        projected_coordinate[2],
                                    TextureAddressMode::kRepeat, 0,
                                    texture0, nullptr)) {
                                fault_("NV2A projected-color stage-0 projection is invalid");
                                raster_failed = true;
                                return;
                            }
                            if constexpr (kProfile == Profile::kPitchProjectedColor)
                                pitch_projected_texture_samples_ += 3;
                            else
                                projected_texture_samples_ += 3;
                            for (size_t component = 0; component < 3; ++component) {
                                const float expanded0 =
                                    2.0f * texture0[component] - 1.0f;
                                const float expanded1 =
                                    2.0f * texture1[component] - 1.0f;
                                const float expanded2 =
                                    2.0f * texture2[component] - 1.0f;
                                r0 += expanded0 * expanded1;
                                r1 += expanded0 * expanded2;
                            }
                            r0 = std::clamp(r0, -1.0f, 1.0f);
                            r1 = std::max(std::clamp(r1, -1.0f, 1.0f),
                                          0.0f);
                            for (size_t stage = 0; stage < 4; ++stage)
                                r1 *= r1;
                        }
                        const size_t factor_stage =
                            textured_family ? 6 : 5;
                        const uint32_t factor0 =
                            raster_state[kRasterCombinerFactor0_0 +
                                          factor_stage];
                        const uint32_t factor1 =
                            raster_state[kRasterCombinerFactor1_0 +
                                          factor_stage];
                        const std::array<float, 3> weight0{{
                            static_cast<float>((factor0 >> 16) & 0xFFu) /
                                255.0f,
                            static_cast<float>((factor0 >> 8) & 0xFFu) /
                                255.0f,
                            static_cast<float>(factor0 & 0xFFu) / 255.0f,
                        }};
                        const std::array<float, 3> weight1{{
                            static_cast<float>((factor1 >> 16) & 0xFFu) /
                                255.0f,
                            static_cast<float>((factor1 >> 8) & 0xFFu) /
                                255.0f,
                            static_cast<float>(factor1 & 0xFFu) / 255.0f,
                        }};
                        const float positive_r0 = std::max(r0, 0.0f);
                        for (size_t component = 0; component < 3; ++component) {
                            const float weighted = std::clamp(
                                weight0[component] * positive_r0 +
                                weight1[component] * r1,
                                -1.0f, 1.0f);
                            output[component] =
                                diffuse[component] * std::max(weighted, 0.0f) *
                                (63.0f / 255.0f);
                        }
                    }
                    auto pack_unorm8 = [](float value) {
                        return static_cast<uint32_t>(std::floor(
                            std::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f));
                    };
                    const uint32_t red = pack_unorm8(output[0]);
                    const uint32_t green = pack_unorm8(output[1]);
                    const uint32_t blue = pack_unorm8(output[2]);
                    const uint32_t color =
                        0xFF000000u | (red << 16) | (green << 8) | blue;
                    const uint32_t depth_stencil =
                        (incoming << 8) | (stored_depth_stencil & 0xFFu);

                    if (!write_color32(offset, color) ||
                        !write_depth32(offset, depth_stencil)) {
                        fault_("NV2A color raster cache write failed");
                        raster_failed = true;
                        return;
                    }
                    if (hash_on) {
                        const uint32_t color_pixel_address =
                            color_address + static_cast<uint32_t>(offset);
                        const uint32_t depth_pixel_address =
                            depth_address + static_cast<uint32_t>(offset);
                        if constexpr (kProfile == Profile::kSwizzledRadialColor) {
                            ++radial_color_writes_;
                            ++radial_color_depth_writes_;
                            last_color_raster_address_ = color_pixel_address;
                            for (uint32_t word :
                                 {color_pixel_address, color,
                                  depth_pixel_address, depth_stencil}) {
                                radial_color_output_hash_ ^= word;
                                radial_color_output_hash_ *= 1099511628211ull;
                            }
                        } else if constexpr (kProfile ==
                                             Profile::kSwizzledTexturedColor) {
                            ++textured_color_writes_;
                            ++textured_color_depth_writes_;
                            last_textured_raster_address_ = color_pixel_address;
                            for (uint32_t word :
                                 {color_pixel_address, color,
                                  depth_pixel_address, depth_stencil}) {
                                textured_color_output_hash_ ^= word;
                                textured_color_output_hash_ *= 1099511628211ull;
                            }
                        } else if constexpr (kProfile == Profile::kPitchTexturedColor) {
                            ++pitch_textured_color_writes_;
                            ++pitch_textured_color_depth_writes_;
                            last_pitch_textured_raster_address_ =
                                color_pixel_address;
                            for (uint32_t word :
                                 {color_pixel_address, color,
                                  depth_pixel_address, depth_stencil}) {
                                pitch_textured_color_output_hash_ ^= word;
                                pitch_textured_color_output_hash_ *=
                                    1099511628211ull;
                            }
                        } else if constexpr (kProfile == Profile::kPitchProjectedColor) {
                            ++pitch_projected_color_writes_;
                            ++pitch_projected_color_depth_writes_;
                            last_pitch_projected_raster_address_ =
                                color_pixel_address;
                            for (uint32_t word :
                                 {color_pixel_address, color,
                                  depth_pixel_address, depth_stencil}) {
                                pitch_projected_color_output_hash_ ^= word;
                                pitch_projected_color_output_hash_ *=
                                    1099511628211ull;
                            }
                        } else {
                            ++projected_color_writes_;
                            ++projected_color_depth_writes_;
                            last_projected_raster_address_ = color_pixel_address;
                            for (uint32_t word :
                                 {color_pixel_address, color,
                                  depth_pixel_address, depth_stencil}) {
                                projected_color_output_hash_ ^= word;
                                projected_color_output_hash_ *= 1099511628211ull;
                            }
                        }
                    }
                }
            }
        };

    const size_t vertex_count = transformed_batch.size();
    // Stage (b3): assemble the batch's primitives through a generic lambda so the
    // batch profile travels as a compile-time tag. The switch below runs ONCE PER
    // BATCH -- not per triangle and not per fragment -- and selects one of the 15
    // instantiations of rasterize_triangle, inside which every test against
    // kProfile is a constant expression and the 14 dead profile arms of the
    // fragment loop are discarded by `if constexpr`.
    auto emit_batch_primitives = [&](auto profile_tag) {
        if (primitive_mode == 5) {
            for (size_t i = 0; i + 2 < vertex_count; i += 3)
                rasterize_triangle(profile_tag,
                                   transformed_batch[i],
                                   transformed_batch[i + 1],
                                   transformed_batch[i + 2]);
        } else if (primitive_mode == 6) {
            for (size_t i = 2; i < vertex_count; ++i) {
                if ((i & 1u) == 0)
                    rasterize_triangle(profile_tag,
                                       transformed_batch[i - 2],
                                       transformed_batch[i - 1],
                                       transformed_batch[i]);
                else
                    rasterize_triangle(profile_tag,
                                       transformed_batch[i - 1],
                                       transformed_batch[i - 2],
                                       transformed_batch[i]);
            }
        } else {
            for (size_t i = 2; i < vertex_count; ++i)
                rasterize_triangle(profile_tag,
                                   transformed_batch[0],
                                   transformed_batch[i - 1],
                                   transformed_batch[i]);
        }
    };
    switch (profile) {
        case Profile::kPitchZ16Depth:
            emit_batch_primitives(std::integral_constant<
                int, static_cast<int>(Profile::kPitchZ16Depth)>{});
            break;
        case Profile::kSwizzledRadialColor:
            emit_batch_primitives(std::integral_constant<
                int, static_cast<int>(Profile::kSwizzledRadialColor)>{});
            break;
        case Profile::kSwizzledTexturedColor:
            emit_batch_primitives(std::integral_constant<
                int, static_cast<int>(Profile::kSwizzledTexturedColor)>{});
            break;
        case Profile::kPitchTexturedColor:
            emit_batch_primitives(std::integral_constant<
                int, static_cast<int>(Profile::kPitchTexturedColor)>{});
            break;
        case Profile::kSwizzledProjectedColor:
            emit_batch_primitives(std::integral_constant<
                int, static_cast<int>(Profile::kSwizzledProjectedColor)>{});
            break;
        case Profile::kPitchProjectedColor:
            emit_batch_primitives(std::integral_constant<
                int, static_cast<int>(Profile::kPitchProjectedColor)>{});
            break;
        case Profile::kPitchMultiTextureColor:
            emit_batch_primitives(std::integral_constant<
                int, static_cast<int>(Profile::kPitchMultiTextureColor)>{});
            break;
        case Profile::kPitchThreeTextureColor:
            emit_batch_primitives(std::integral_constant<
                int, static_cast<int>(Profile::kPitchThreeTextureColor)>{});
            break;
        case Profile::kPitchTex5ProjectedColor:
            emit_batch_primitives(std::integral_constant<
                int, static_cast<int>(Profile::kPitchTex5ProjectedColor)>{});
            break;
        case Profile::kPitchTex5DiffuseColor:
            emit_batch_primitives(std::integral_constant<
                int, static_cast<int>(Profile::kPitchTex5DiffuseColor)>{});
            break;
        case Profile::kInlineLinearBlitColor:
            emit_batch_primitives(std::integral_constant<
                int, static_cast<int>(Profile::kInlineLinearBlitColor)>{});
            break;
        case Profile::kPitchSingleTextureColor:
            emit_batch_primitives(std::integral_constant<
                int, static_cast<int>(Profile::kPitchSingleTextureColor)>{});
            break;
        case Profile::kTex7FanColor:
            emit_batch_primitives(std::integral_constant<
                int, static_cast<int>(Profile::kTex7FanColor)>{});
            break;
        case Profile::kTexfProjectedFanColor:
            emit_batch_primitives(std::integral_constant<
                int, static_cast<int>(Profile::kTexfProjectedFanColor)>{});
            break;
        case Profile::kPitchAdditiveColor:
            emit_batch_primitives(std::integral_constant<
                int, static_cast<int>(Profile::kPitchAdditiveColor)>{});
            break;
        case Profile::kNone:
            // Unreachable: the no-fingerprint-matched arm of the ladder above
            // returns through rasterize_generic_combiner_batch, so `profile` is
            // never kNone here. No instantiation is emitted for it.
            break;
    }

    // Stage (b1): fold the batch's fragment tally into the profile's counter
    // once. This ladder is the ladder that used to run per fragment (same arm
    // order, same `else` arm), it runs on every path the per-fragment increments
    // could reach, and it precedes every `return` that follows the assembly loop
    // -- so the per-profile totals are unchanged.
    if (profile == Profile::kPitchZ16Depth)
        rasterized_fragments_ += witnessed_fragment_count;
    else if (profile == Profile::kSwizzledRadialColor)
        radial_color_fragments_ += witnessed_fragment_count;
    else if (profile == Profile::kSwizzledTexturedColor)
        textured_color_fragments_ += witnessed_fragment_count;
    else if (profile == Profile::kPitchTexturedColor)
        pitch_textured_color_fragments_ += witnessed_fragment_count;
    else if (profile == Profile::kSwizzledProjectedColor)
        projected_color_fragments_ += witnessed_fragment_count;
    else if (profile == Profile::kPitchProjectedColor)
        pitch_projected_color_fragments_ += witnessed_fragment_count;
    else if (profile == Profile::kPitchMultiTextureColor)
        pitch_multi_texture_fragments_ += witnessed_fragment_count;
    else if (profile == Profile::kPitchThreeTextureColor)
        pitch_three_texture_fragments_ += witnessed_fragment_count;
    else if (pitch_tex5_profile)
        pitch_tex5_fragments_ += witnessed_fragment_count;
    else if (profile == Profile::kInlineLinearBlitColor)
        inline_linear_blit_fragments_ += witnessed_fragment_count;
    else if (profile == Profile::kPitchSingleTextureColor)
        pitch_single_texture_fragments_ += witnessed_fragment_count;
    else if (profile == Profile::kTex7FanColor)
        tex7_fan_fragments_ += witnessed_fragment_count;
    else if (profile == Profile::kTexfProjectedFanColor)
        texf_projected_fan_fragments_ += witnessed_fragment_count;
    else
        inline_color_fragments_ += witnessed_fragment_count;

    if (raster_failed) return false;
    if (uses_depth_surface && !depth_surface.flush()) {
        fault_("NV2A raster zeta DMA write failed");
        return false;
    }
    if (color_profile && !color_surface.flush()) {
        fault_("NV2A color DMA write failed");
        return false;
    }
    // Stage (e2): F3 and F15 are per-DRAW side effects. fold_draw_job reads them
    // out of job.out, which is part 0's accumulator, so recording them on any
    // other part would silently drop them. Gated so the intent is explicit.
    if (part == 0 && color_profile)
        invalidate_renderer_color_overlap(
            color_address, static_cast<size_t>(color_bytes64),
            nv2a_invalidate_token(kNv2aInvalidateWitnessedCpuDraw,
                                  static_cast<uint32_t>(profile)));
    if (part == 0 && writes_depth_surface)
        invalidate_renderer_depth_overlap(
            depth_address, static_cast<size_t>(depth_bytes64),
            nv2a_invalidate_token(kNv2aInvalidateWitnessedCpuDraw,
                                  static_cast<uint32_t>(profile)));
    // F15 forces the job inline (parts == 1) at the latch, so this can only be
    // reached with part == 0; the guard is defensive and pinned by the test.
    if (part == 0 && color_profile && surface_write_debug_cb_) {
        SurfaceWriteDebugInfo info;
        info.kind = "witnessed";
        info.sequence = job.seq;
        info.color_address = color_address;
        info.pitch = pitch;
        info.width = pitch / 4u;
        info.height = surface_height;
        info.surface_format = surface_format;
        info.shader_stage_program = raster_state[kRasterShaderStageProgram];
        info.combiner_control = raster_state[kRasterCombinerControl];
        info.alpha_func = raster_state[kRasterAlphaFunc];
        info.alpha_ref = raster_state[kRasterAlphaRef] & 0xFFu;
        info.blend_src = raster_state[kRasterBlendSrc];
        info.blend_dst = raster_state[kRasterBlendDst];
        info.blend_equation = raster_state[kRasterBlendEquation];
        info.depth_func = raster_state[kRasterDepthFunc];
        info.depth_mask = raster_state[kRasterDepthMask];
        info.texture_enable_mask = raster_state[kRasterTextureEnableMask];
        info.primitive_mode = primitive_mode;
        info.vertex_count = static_cast<uint32_t>(std::min<size_t>(
            transformed_batch.size(), std::numeric_limits<uint32_t>::max()));
        info.profile = static_cast<uint32_t>(profile);
        surface_write_debug_cb_(info);
    }
    // Stage (e2): one batch, one increment, regardless of how many destination
    // spans executed it. The fragment ladder above is per-part on purpose (each
    // part counts its own fragments and the sums add); this one is not.
    if (part != 0) return true;
    if (profile == Profile::kPitchZ16Depth)
        ++depth_raster_batches_;
    else if (profile == Profile::kSwizzledRadialColor)
        ++radial_color_batches_;
    else if (profile == Profile::kSwizzledTexturedColor)
        ++textured_color_batches_;
    else if (profile == Profile::kPitchTexturedColor)
        ++pitch_textured_color_batches_;
    else if (profile == Profile::kSwizzledProjectedColor)
        ++projected_color_batches_;
    else if (profile == Profile::kPitchProjectedColor)
        ++pitch_projected_color_batches_;
    else if (profile == Profile::kPitchMultiTextureColor)
        ++pitch_multi_texture_batches_;
    else if (profile == Profile::kPitchThreeTextureColor)
        ++pitch_three_texture_batches_;
    else if (pitch_tex5_profile)
        ++pitch_tex5_batches_;
    else if (profile == Profile::kInlineLinearBlitColor)
        ++inline_linear_blit_batches_;
    else if (profile == Profile::kPitchSingleTextureColor)
        ++pitch_single_texture_batches_;
    else if (profile == Profile::kTex7FanColor)
        ++tex7_fan_batches_;
    else if (profile == Profile::kTexfProjectedFanColor)
        ++texf_projected_fan_batches_;
    else
        ++inline_color_batches_;
    return true;
}

void Nv2a::record_generic_skip(GenericSkipReason reason) {
    const size_t index = static_cast<size_t>(reason);
    if (index < generic_skip_reasons_.size()) ++generic_skip_reasons_[index];
}

void Nv2a::record_generic_skip_detail(GenericSkipReason reason, uint64_t detail) {
    record_generic_skip(reason);
    for (auto& entry : generic_skip_details_) {
        if (entry.reason == reason && entry.detail == detail) {
            ++entry.count;
            return;
        }
    }
    generic_skip_details_.push_back({reason, detail, 1});
}

void Nv2a::record_generic_resident_reject(GenericResidentRejectReason reason) {
    const size_t index = static_cast<size_t>(reason);
    if (index < generic_combiner_resident_rejects_.size())
        ++generic_combiner_resident_rejects_[index];
}

void Nv2a::record_generic_texture_modulate_reject(
    GenericTextureModulateRejectReason reason) {
    const size_t index = static_cast<size_t>(reason);
    if (index < generic_combiner_texture_modulate_rejects_.size())
        ++generic_combiner_texture_modulate_rejects_[index];
}

// beads-xbox.1.15.10 phase 6. Linear scan of at most 128 rows, once per generic
// BATCH. Overflow is COUNTED, never evicted, so a state the table already knows
// about can never be under-reported -- the same rule the phase-5 plane table
// follows and for the same reason.
Nv2a::GenericShaderCensusRow* Nv2a::generic_shader_census_row(
    uint64_t fingerprint) {
    for (size_t i = 0; i < generic_shader_census_.count; ++i) {
        if (generic_shader_census_.rows[i].fingerprint == fingerprint)
            return &generic_shader_census_.rows[i];
    }
    if (generic_shader_census_.count >= kGenericShaderCensusSlots) {
        ++generic_shader_census_.overflow_batches;
        return nullptr;
    }
    GenericShaderCensusRow& row =
        generic_shader_census_.rows[generic_shader_census_.count++];
    row = GenericShaderCensusRow{};
    row.fingerprint = fingerprint;
    return &row;
}

// Generic NV2A register-combiner evaluator.
//
// Every Profile above is a hand-derived, exact-match fingerprint: a whole-register
// whitelist paired with a closed-form pixel formula worked out by inspecting one
// witnessed draw. That approach cannot generalize — any raster state not on the list
// falls through with executed=false and zero fragment writes (see the caller's final
// `else` branch). This function replaces that silent drop for the *rest* of the state
// space by decoding the combiner registers per NV2A/GL_NV_register_combiners semantics
// and evaluating them, instead of matching a precomputed answer.
//
// Supported subset (anything else increments a named GenericSkipReason counter --
// carrying the offending register value via generic_skip_detail_census -- and returns
// executed=false, so it is still visible in telemetry, never silently dropped). Every
// check below is a per-feature semantic test of what the shared rasterizer actually
// implements, not a single required register tuple:
//   * primitive: triangle list/strip/fan (5/6/7)
//   * surface: pitch-linear (surface_type==1) or swizzled (surface_type==2) color,
//     no antialiasing; color_format 8 (A8R8G8B8, alpha stored as computed) or 4
//     (X8R8G8B8, alpha byte forced to 0xFF on write -- there is no alpha channel
//     on real hardware); optional Z24S8 zeta
//   * CONTROL0: engine-enable bit (0) required, the two bits kPitchZ16Depth already
//     treats as unimplemented (12, 16) rejected -- any other bit combination passes
//   * shading: smooth (Gouraud) only; polygon mode: FILL only, independently for
//     front/back faces
//   * cull: honors the enable bit (enables&4) for real -- disabled renders both
//     windings, enabled requires the BACK/CW convention every witnessed profile uses
//   * color mask: honors each of the four per-channel write-enable bytes for real
//     (a disabled channel preserves the existing pixel byte)
//   * enables bitmask: alpha test(0)/blend(1)/cull(2)/depth test(3)/polygon-offset(13)
//     honored per-bit; dither(4)/point-line-smooth(5,9,10)/point-line-offset(11,12) are
//     inert for this rasterizer (it never draws points or lines) and ignored; any other
//     bit gates a skip except logic COPY (bit6), which overrides blending
//   * combiner: 1..8 general-combiner stages, per-stage ColorIcw/AlphaIcw (inputs
//     ZERO/CONSTANT_COLOR0/CONSTANT_COLOR1/PRIMARY_COLOR/SECONDARY_COLOR/R0/R1, all 8
//     input mappings) and ColorOcw/AlphaOcw (AB/CD/SUM destination register, AB/CD dot
//     product); final combiner only for the identity-passthrough signature
//     (SpecularFog==0x0000000C/0x00001C80, FinalFactor==0/0) proven against the working
//     kInlineLinearBlitColor profile, which shares that exact signature
//   * alpha test / depth test: honors the enable bit; when enabled, any of
//     NEVER/LESS/EQUAL/LEQUAL/GREATER/NOTEQUAL/GEQUAL/ALWAYS; when disabled the
//     *_func register is unread by hardware and never gates a skip
//   * blend: honors the enable bit; when enabled, FUNC_ADD with any combination of
//     ZERO/ONE/SRC_ALPHA/ONE_MINUS_SRC_ALPHA (covers the witnessed
//     SRC_ALPHA/ONE_MINUS_SRC_ALPHA blend plus the trivially adjacent replace/additive
//     cases); when disabled the factor/equation registers never gate a skip
//
// Not yet implemented (recorded, counted, never silently dropped):
//   * any stage input that reads FOG or a texture register (TEXTURE0..3) — texture
//     sampling itself is fully implemented elsewhere in this file, but wiring a stage's
//     texture-register input through the generic evaluator's per-pixel loop is future
//     work; see GenericSkipReason::kTextureStage
//   * OCW scale/bias and MUX_SUM (bit 14) — see GenericSkipReason::kOcwFeature
//   * any final-combiner signature other than the proven identity passthrough — see
//     GenericSkipReason::kFinalCombiner
//   * stencil test, logic ops other than COPY, blend equations other than FUNC_ADD, blend factors
//     other than ZERO/ONE/SRC_ALPHA/ONE_MINUS_SRC_ALPHA
//
// Bit-layout note (flagged, not silently guessed): this decoder's ICW/OCW field
// layout is this author's own expression of NV2A/GL_NV_register_combiners semantics,
// cross-checked against the *witnessed* ICW/OCW/OCW words already pinned by the
// hand-derived profiles above (e.g. kInlineLinearBlitColor's ColorIcw0/AlphaIcw0/
// ColorOcw0/AlphaOcw0/SpecularFog/FinalFactor and kPitchMultiTextureColor's per-stage
// OCW dot-product words) rather than against a register reference doc, since none was
// available in this checkout. It reproduces every cross-checked value correctly, but
// the AlphaIcw component-select polarity (bit4: this decoder treats 1=ALPHA, 0=BLUE)
// and the OCW scale/bias field layout are inferred, not verified against hardware, and
// are exactly the two things gated behind GenericSkipReason so a wrong guess there
// cannot silently mis-render — it skips instead.
bool Nv2a::rasterize_generic_combiner_batch(DrawJob& job, bool& executed) {
    // NV2A_RASTER_GENERIC_SW: generic-combiner decode and software raster. Texture setup,
    // GL submission and resident-surface resolves each open their own region below.
    perf_excl::Scope region(perf_excl::kNv2aRasterGenericSw);
    // F3 CLOSURE. This body asks the renderer three more residency questions --
    // supports_resident_generic_combiner, has_resident_color_surface,
    // has_resident_depth_surface -- and picks its whole execution path from the
    // answers. Its single call site already carries draw_order_fence(), so this
    // is expected to stay at zero; it is stated here so the invariant belongs to
    // the query rather than to one caller.
    draw_renderer_query_fence();
    executed = false;
    // Stage (d) seam (beads-xbox.1.15.4, DESIGN-NV2A-SEAM.md section 3.2):
    // the executor reads the LATCHED JOB, never the live members. These
    // aliases are the whole of the mechanical redirection -- every use site
    // below is textually unchanged, so no expression, and in particular no
    // floating-point expression, moved with the split.
    const auto& raster_state = job.raster_state;
    const auto& transformed_batch = job.vertices;
    const uint32_t primitive_mode = job.primitive_mode;
    const uint32_t surface_format = job.surface_format;
    const uint32_t surface_pitch = job.surface_pitch;
    const uint32_t surface_clip_horizontal = job.surface_clip_horizontal;
    const uint32_t surface_clip_vertical = job.surface_clip_vertical;
    const uint32_t surface_color_offset = job.surface_color_offset;
    const uint32_t surface_zeta_offset = job.surface_zeta_offset;
    ++generic_combiner_attempted_batches_;
    // beads-xbox.1.15.10 phase 6: the generic-combiner shader census. Per-batch
    // state, reset here so an early return cannot leak a row pointer or a
    // texture-resolve count into the next batch.
    generic_shader_row_ = nullptr;
    generic_batch_texture_resolves_ = 0;
    generic_shader_dest_slot_ = 0;
    // This routine is reached only by the strict generic fallback. Keep diagnostic
    // census and split timing work out of reduced, non-perf interactive replays.
    // profile_generic_fingerprint still tracks --perf-accounting because it gates the
    // per-phase steady_clock TIMING below (texture setup / software raster / flush ns),
    // which is what an instrumented run is for.
    const bool profile_generic_fingerprint = perf::g_enabled;
    // beads-xbox.1.14.3 item 4 completed: profile_generic_fingerprint REMOVED from this
    // disjunction. The census it used to switch on is per-batch hot-loop work -- an FNV
    // fold over the whole raster-state block plus a linear search of
    // generic_combiner_fingerprints_ on every generic batch -- so gating it on
    // --perf-accounting made the timed code differ from the untimed code.
    // DECISION: the "[2bl] NV2A generic-combiner fingerprints:" census now requires
    // --nv2a-detailed-telemetry; it is no longer produced by --perf-accounting alone.
    // docs/PERF-METHODOLOGY.md section 5a already declares these families
    // instrumentation artifacts and out of scope for goldens. The two per-profile
    // hashes (generic_combiner, transform_position_fnv64) therefore stay at the FNV-1a
    // offset basis 14650fb0739d0383 in a --perf-accounting run, which is what section
    // 5a says they should do and what the tools already expect.
    // Every surviving profile_generic_fingerprint site is pure timing except two that
    // read generic_fingerprint_stats, and both are null-guarded, so the pointer simply
    // stays nullptr when the census is off.
    const bool collect_diagnostics =
        detailed_telemetry_ || generic_batch_debug_cb_ ||
        generic_packet_snapshot_filter_cb_ || surface_write_debug_cb_;
    const bool capture_generic_deltas =
        collect_diagnostics;
    const uint64_t generic_fragments_before =
        capture_generic_deltas ? generic_combiner_fragments_ : 0;
    const uint64_t generic_writes_before =
        capture_generic_deltas ? generic_combiner_writes_ : 0;
    const uint64_t generic_color_reads_before =
        capture_generic_deltas ? generic_combiner_color_reads_ : 0;
    const uint64_t generic_depth_reads_before =
        capture_generic_deltas ? generic_combiner_depth_reads_ : 0;
    std::chrono::steady_clock::time_point generic_started_at;
    if (profile_generic_fingerprint)
        generic_started_at = std::chrono::steady_clock::now();
    std::optional<perf::ScopedAdd> generic_state_timer;
    if (perf::g_enabled) {
        generic_state_timer.emplace(perf::g_ns_generic_state_setup,
                                    perf::g_calls_generic_state_setup);
    }
    uint64_t generic_batch_bbox_pixels = 0;
    uint64_t generic_batch_texture_setup_ns = 0;
    uint64_t generic_batch_software_raster_ns = 0;
    uint64_t generic_batch_flush_ns = 0;
    const bool supported_primitive =
        primitive_mode == 5 || primitive_mode == 6 || primitive_mode == 7;
    if (!supported_primitive) {
        record_generic_skip(GenericSkipReason::kUnsupportedPrimitive);
        return true;
    }
    const uint32_t color_format = surface_format & 0xFu;
    const uint32_t surface_type = (surface_format >> 8) & 0xFu;
    const uint32_t anti_aliasing = (surface_format >> 12) & 0xFu;
    const uint32_t zeta_format = (surface_format >> 4) & 0xFu;
    const uint32_t enables = raster_state[kRasterEnables];

    // --- Per-feature semantic gate -----------------------------------------------
    // Every check below tests the specific behavior the shared rasterizer actually
    // implements, not a single magic register tuple. Anything requesting a behavior
    // this evaluator does not (yet) implement records a NAMED reason carrying the
    // offending register value, so the next witnessed state can be added without
    // re-deriving the whole gate.
    const bool surface_pitch_linear = surface_type == 1;
    const bool surface_swizzled = surface_type == 2;
    if ((!surface_pitch_linear && !surface_swizzled) || anti_aliasing != 0) {
        record_generic_skip(GenericSkipReason::kUnsupportedSurface);
        return true;
    }
    // Color format: A8R8G8B8 (8) stores the combiner's alpha byte as-is. X8R8G8B8 (4)
    // has no alpha channel on real hardware; the top byte is padding, so this
    // evaluator always stores 0xFF there on write (matching every other 32bpp path in
    // this file, which only ever produces A8R8G8B8 surfaces and therefore never had to
    // decide this -- X8 is new territory for the generic path).
    const bool color_format_x8 = color_format == 4;
    const bool color_format_a8 = color_format == 8;
    if (!color_format_x8 && !color_format_a8) {
        record_generic_skip_detail(GenericSkipReason::kUnsupportedColorFormat,
                                   color_format);
        return true;
    }
    // CONTROL0: every witnessed profile carries bit0 (engine enable) and bit20 (an
    // always-1 flag none of them vary). Bits 12/16 are the ones kPitchZ16Depth already
    // guards against as exceeding its witnessed subset (unimplemented alpha-to-
    // coverage/stencil-adjacent behavior) -- reject those specifically rather than the
    // whole register, so any other harmless bit combination the dashboard uses still
    // passes.
    constexpr uint32_t kControl0Required = 1u;
    constexpr uint32_t kControl0Unsupported = (1u << 12) | (1u << 16);
    if ((raster_state[kRasterControl0] & kControl0Required) != kControl0Required ||
        (raster_state[kRasterControl0] & kControl0Unsupported) != 0) {
        record_generic_skip_detail(GenericSkipReason::kUnsupportedControl0,
                                   raster_state[kRasterControl0]);
        return true;
    }
    const bool flat_shading = raster_state[kRasterShadeMode] == 0x1D00;
    if ((!flat_shading && raster_state[kRasterShadeMode] != 0x1D01) ||
        (flat_shading && raster_state[kRasterProvokingVertex] > 1)) {
        record_generic_skip_detail(GenericSkipReason::kUnsupportedShadeMode,
                                   raster_state[kRasterShadeMode]);
        return true;
    }
    // Polygon mode: the shared rasterizer only ever fills triangles (no line/point
    // polygon-mode rendering), for both front and back faces independently.
    if (raster_state[kRasterFrontPolygonMode] != 0x1B02 ||
        raster_state[kRasterBackPolygonMode] != 0x1B02) {
        record_generic_skip_detail(
            GenericSkipReason::kUnsupportedPolygonMode,
            (static_cast<uint64_t>(raster_state[kRasterFrontPolygonMode]) << 32) |
                raster_state[kRasterBackPolygonMode]);
        return true;
    }
    // Cull: honor the enable bit (bit2) semantically instead of assuming culling is
    // always on, and honor which face is being culled -- GL_BACK (0x0405, every other
    // witnessed profile) or GL_FRONT (0x0404, trivially the mirror image: cull the
    // opposite winding sign). When disabled, both windings render (handled in the
    // triangle setup below).
    const bool cull_enabled = (enables & (1u << 2)) != 0;
    bool cull_front = false;
    if (cull_enabled) {
        if (raster_state[kRasterCullFace] == 0x0405) {
            cull_front = false;
        } else if (raster_state[kRasterCullFace] == 0x0404) {
            cull_front = true;
        } else {
            record_generic_skip_detail(GenericSkipReason::kUnsupportedCullFace,
                                       raster_state[kRasterCullFace]);
            return true;
        }
        if (raster_state[kRasterFrontFace] != 0x0900) {
            record_generic_skip_detail(GenericSkipReason::kUnsupportedFrontFace,
                                       raster_state[kRasterFrontFace]);
            return true;
        }
    }
    // Z min/max control is not read anywhere in this evaluator's math; every witnessed
    // profile carries the same value 1, so require it named rather than assume it is
    // inert.
    if (raster_state[kRasterZMinMaxControl] != 1) {
        record_generic_skip_detail(GenericSkipReason::kUnsupportedZMinMax,
                                   raster_state[kRasterZMinMaxControl]);
        return true;
    }
    // Color mask: honor each of the four per-channel write-enable bytes exactly
    // (0=disabled, 1=enabled per channel), applied for real at write time below,
    // instead of requiring the single all-channels-enabled tuple.
    const uint32_t color_mask_word = raster_state[kRasterColorMask];
    bool color_mask_write[4];
    bool color_mask_supported = true;
    constexpr unsigned color_mask_shifts[] = {16, 8, 0, 24}; // R,G,B,A in NV097
    for (size_t channel = 0; channel < 4; ++channel) {
        const uint32_t byte = (color_mask_word >> color_mask_shifts[channel]) & 0xFFu;
        color_mask_write[channel] = byte != 0;
        if (byte != 0 && byte != 1) color_mask_supported = false;
    }
    if (!color_mask_supported) {
        record_generic_skip_detail(GenericSkipReason::kUnsupportedColorMask,
                                   color_mask_word);
        return true;
    }
    const bool logic_enabled = (enables & (1u << 6)) != 0;
    const bool logic_copy = logic_enabled && raster_state[kRasterLogicOp] == 0x1503;
    // COPY replaces blending while preserving write masks. Other bitwise
    // operations remain outside this bounded floor, never treated as COPY.
    // Enables: bits this evaluator does not implement (stencil test, other logic) must
    // gate a skip if the dashboard/game actually turns them on; bits it always treats
    // as inert (dither, point/line smoothing, point/line polygon offset -- this
    // rasterizer never draws points or lines) are masked out first.
    constexpr uint32_t kEnableInertBits =
        (1u << 4) | (1u << 5) | (1u << 9) | (1u << 10) | (1u << 11) | (1u << 12);
    constexpr uint32_t kEnableHandledBits =
        (1u << 0) | (1u << 1) | (1u << 2) | (1u << 3) | (1u << 13);
    const uint32_t enable_unsupported_bits =
        enables & ~(kEnableInertBits | kEnableHandledBits | (logic_copy ? (1u << 6) : 0));
    if (enable_unsupported_bits != 0) {
        record_generic_skip_detail(GenericSkipReason::kUnsupportedEnableBits,
                                   enable_unsupported_bits);
        return true;
    }

    // --- Combiner decode -------------------------------------------------------
    struct DecodedInput {
        uint32_t reg = 0;
        bool alt = false;
        uint32_t mapping = 0;
    };
    auto decode = [](uint32_t byte) {
        DecodedInput in;
        in.reg = byte & 0xFu;
        in.alt = ((byte >> 4) & 1u) != 0;
        in.mapping = (byte >> 5) & 0x7u;
        return in;
    };
    // Register ids match real NV2A/xemu PS_REGISTER exactly: 0 ZERO/DISCARD,
    // 1 CONSTANT_COLOR0, 2 CONSTANT_COLOR1, 3 FOG (unsupported), 4 PRIMARY_COLOR,
    // 5 SECONDARY_COLOR, 6/7 reserved (unsupported), 8-11 TEXTURE0-3 (sampled below,
    // subject to the per-stage texture-shader-program/format/address checks),
    // 12 R0/spare0, 13 R1/spare1, 14 V1R0_SUM (specular+R0, final-combiner flags),
    // 15 EF_PROD (the final combiner's own E*F product).
    auto register_supported = [](uint32_t reg, bool& is_texture) {
        is_texture = reg >= 8 && reg <= 11;
        if (is_texture) return true;
        return reg == 0 || reg == 1 || reg == 2 || reg == 4 || reg == 5 ||
               reg == 12 || reg == 13 || reg == 14 || reg == 15;
    };
    // Which of TEXTURE0-3 a stage's color/alpha inputs actually read.
    bool texture_referenced[4] = {false, false, false, false};

    const uint32_t stage_count_raw = raster_state[kRasterCombinerControl] & 0xFFu;
    if (stage_count_raw == 0) {
        record_generic_skip(GenericSkipReason::kStageCount);
        return true;
    }
    const uint32_t stage_count = std::min<uint32_t>(stage_count_raw, 8u);
    // NV097_SET_COMBINER_CONTROL bit8 selects the MUX_SUM comparison: MSB of R0.alpha
    // (bit set) vs LSB (bit clear) -- a single global flag shared by every stage's
    // MUX_SUM output, matching xemu's PS_COMBINERCOUNT_MUX_MSB.
    const bool mux_on_msb = ((raster_state[kRasterCombinerControl] >> 8) & 1u) != 0;

    struct Stage {
        DecodedInput color[4];
        DecodedInput alpha[4];
        uint32_t color_cd_dst = 0, color_ab_dst = 0, color_sum_dst = 0;
        bool color_cd_dot = false, color_ab_dot = false, color_mux = false;
        bool color_ab_blue_to_alpha = false, color_cd_blue_to_alpha = false;
        uint32_t color_scale_bias = 0;
        uint32_t alpha_cd_dst = 0, alpha_ab_dst = 0, alpha_sum_dst = 0;
        bool alpha_mux = false;
        uint32_t alpha_scale_bias = 0;
    };
    std::array<Stage, 8> stages{};
    for (uint32_t stage = 0; stage < stage_count; ++stage) {
        const uint32_t ci = raster_state[kRasterCombinerColorIcw0 + stage];
        const uint32_t ai = raster_state[kRasterCombinerAlphaIcw0 + stage];
        const uint32_t co = raster_state[kRasterCombinerColorOcw0 + stage];
        const uint32_t ao = raster_state[kRasterCombinerAlphaOcw0 + stage];
        Stage& s = stages[stage];
        // Byte order within the ICW word is A (bits31:24), B (23:16), C (15:8),
        // D (bits7:0) -- matching xemu's parse_combiner_inputs() exactly (cross-checked
        // against nv2a_regs.h/psh.c, not guessed: this is this evaluator's own
        // independent expression of that same, now-verified bit layout).
        s.color[0] = decode((ci >> 24) & 0xFFu);
        s.color[1] = decode((ci >> 16) & 0xFFu);
        s.color[2] = decode((ci >> 8) & 0xFFu);
        s.color[3] = decode(ci & 0xFFu);
        s.alpha[0] = decode((ai >> 24) & 0xFFu);
        s.alpha[1] = decode((ai >> 16) & 0xFFu);
        s.alpha[2] = decode((ai >> 8) & 0xFFu);
        s.alpha[3] = decode(ai & 0xFFu);
        for (const DecodedInput& in : s.color) {
            bool is_texture = false;
            if (!register_supported(in.reg, is_texture)) {
                record_generic_skip_detail(GenericSkipReason::kStageInput, in.reg);
                return true;
            }
            if (is_texture) texture_referenced[in.reg - 8] = true;
        }
        for (const DecodedInput& in : s.alpha) {
            bool is_texture = false;
            if (!register_supported(in.reg, is_texture)) {
                record_generic_skip_detail(GenericSkipReason::kStageInput, in.reg);
                return true;
            }
            if (is_texture) texture_referenced[in.reg - 8] = true;
        }
        // OCW layout (matching xemu's parse_combiner_output()): bits[3:0]=cd dest,
        // [7:4]=ab dest, [11:8]=muxsum dest, [12]=cd dot-product, [13]=ab dot-product,
        // [14]=muxsum uses MUX (else AB+CD sum), [17:15]=output scale/bias
        // (SHIFTLEFTBY1/2, SHIFTRIGHTBY1, BIAS_BY_NEGATIVE_ONE_HALF and the two
        // combinations of shift+bias), [18]=CD blue-to-alpha, [19]=AB blue-to-alpha
        // (the latter two are not implemented -- named census, not a silent drop).
        s.color_cd_dst = co & 0xFu;
        s.color_ab_dst = (co >> 4) & 0xFu;
        s.color_sum_dst = (co >> 8) & 0xFu;
        const uint32_t color_flags = co >> 12;
        s.color_cd_dot = (color_flags & 1u) != 0;
        s.color_ab_dot = (color_flags & 2u) != 0;
        s.color_mux = (color_flags & 4u) != 0;
        s.color_scale_bias = color_flags & 0x38u;
        // AB/CD_BLUE_TO_ALPHA ("RGB only" per the enum comment: xemu's add_stage_code
        // only ever applies it for the is_alpha==false / color call, i.e. these bits
        // are read from ColorOcw only -- the same two bit positions read from
        // AlphaOcw are simply inert on real hardware, never gated below).
        s.color_ab_blue_to_alpha = (color_flags & 0x80u) != 0;
        s.color_cd_blue_to_alpha = (color_flags & 0x40u) != 0;
        s.alpha_cd_dst = ao & 0xFu;
        s.alpha_ab_dst = (ao >> 4) & 0xFu;
        s.alpha_sum_dst = (ao >> 8) & 0xFu;
        const uint32_t alpha_flags = ao >> 12;
        s.alpha_mux = (alpha_flags & 4u) != 0;
        s.alpha_scale_bias = alpha_flags & 0x38u;
        auto dst_supported = [](uint32_t dst) {
            return dst == 0 || dst == 12 || dst == 13;
        };
        auto scale_bias_supported = [](uint32_t sb) {
            return sb == 0x00 || sb == 0x08 || sb == 0x10 || sb == 0x18 ||
                   sb == 0x20 || sb == 0x30;
        };
        if (!dst_supported(s.color_cd_dst) || !dst_supported(s.color_ab_dst) ||
            !dst_supported(s.color_sum_dst) || !dst_supported(s.alpha_cd_dst) ||
            !dst_supported(s.alpha_ab_dst) || !dst_supported(s.alpha_sum_dst) ||
            !scale_bias_supported(s.color_scale_bias) ||
            !scale_bias_supported(s.alpha_scale_bias)) {
            record_generic_skip_detail(
                GenericSkipReason::kOcwFeature,
                (static_cast<uint64_t>(co) << 32) | ao);
            return true;
        }
    }
    // Final combiner: RGB = D + A*B + (1-A)*C; Alpha = G (matching xemu's
    // add_final_stage_code() exactly: fragColor.rgb = d + mix(c, b, a),
    // fragColor.a = g, where mix(c,b,a) = c*(1-a) + b*a). A/B/C/D come from
    // SpecularFogCW0 with the same 4-input byte layout as any stage ICW; E/F/G and
    // the CLAMP_SUM/COMPLEMENT_V1/COMPLEMENT_R0 flags (used only if a register
    // references V1R0_SUM) come from SpecularFogCW1: E=bits31:24, F=bits23:16,
    // G=bits15:8, flags=bits7:0. When both words are zero the final combiner is
    // absent and R0/R0.a simply pass through untouched (this is exactly the
    // identity signature this evaluator originally only recognized by its exact
    // SpecularFogCW/FinalFactor values -- that was this constant pair, decoded
    // generically it turns out to mean A=B=C=0(ZERO), D=R0(identity), G=R0.alpha).
    const uint32_t final_cw0 = raster_state[kRasterCombinerSpecularFog0];
    const uint32_t final_cw1 = raster_state[kRasterCombinerSpecularFog1];
    DecodedInput final_a = decode((final_cw0 >> 24) & 0xFFu);
    DecodedInput final_b = decode((final_cw0 >> 16) & 0xFFu);
    DecodedInput final_c = decode((final_cw0 >> 8) & 0xFFu);
    DecodedInput final_d = decode(final_cw0 & 0xFFu);
    DecodedInput final_e = decode((final_cw1 >> 24) & 0xFFu);
    DecodedInput final_f = decode((final_cw1 >> 16) & 0xFFu);
    DecodedInput final_g = decode((final_cw1 >> 8) & 0xFFu);
    const uint32_t final_flags = final_cw1 & 0xFFu;
    const bool final_clamp_sum = (final_flags & 0x80u) != 0;
    const bool final_inv_v1 = (final_flags & 0x40u) != 0;
    const bool final_inv_r0 = (final_flags & 0x20u) != 0;
    const bool final_enabled = final_cw0 != 0 || final_cw1 != 0;
    if (final_enabled) {
        for (const DecodedInput* in :
             {&final_a, &final_b, &final_c, &final_d, &final_e, &final_f, &final_g}) {
            bool is_texture = false;
            if (!register_supported(in->reg, is_texture)) {
                record_generic_skip_detail(GenericSkipReason::kFinalCombiner,
                                           (static_cast<uint64_t>(final_cw0) << 32) |
                                               final_cw1);
                return true;
            }
            if (is_texture) texture_referenced[in->reg - 8] = true;
        }
    }
    // NV097_SET_SPECULAR_FOG_FACTOR (FinalFactor0/1) is NOT a fog enable flag -- per
    // xemu's pgraph_glsl_set_psh_uniform_values() (the final combiner is constant
    // slot index 8: "if (i==8) constant[0]=SPECFOGFACTOR0; constant[1]=SPECFOGFACTOR1;
    // else constant[]=COMBINEFACTOR{0,1}+i"), it is simply the CONSTANT_COLOR0/
    // CONSTANT_COLOR1 value the FINAL combiner sees when any of A/B/C/D/E/F/G
    // reference register 1 or 2 -- a completely different source from the per-stage
    // Factor0_0[stage]/Factor1_0[stage] array used by ordinary stages. This is wired
    // below (regs[1]/regs[2] are reloaded from FinalFactor0/1 right before evaluating
    // the final combiner, once every stage that needed the per-stage constants is
    // done with them) rather than treated as an unconditional "fog, unsupported" skip.
    // True fog (register 3, FOG_COLOR + the per-vertex fog coordinate) is a separate,
    // still-unimplemented register -- already gated by register_supported() above,
    // which never allows reg==3 through, so a final combiner that genuinely
    // references FOG still lands in the kFinalCombiner census, just for that reason
    // specifically instead of a blanket nonzero-factor guess.
    const bool no_texture_inputs =
        !texture_referenced[0] && !texture_referenced[1] &&
        !texture_referenced[2] && !texture_referenced[3];
    const bool primary_color_passthrough =
        no_texture_inputs && stage_count == 1 &&
        raster_state[kRasterCombinerColorIcw0] == 0x04200000 &&
        raster_state[kRasterCombinerAlphaIcw0] == 0x14200000 &&
        raster_state[kRasterCombinerColorOcw0] == 0x00000C00 &&
        raster_state[kRasterCombinerAlphaOcw0] == 0x00000C00 &&
        final_cw0 == 0x0000000C && final_cw1 == 0x00001C80;

    // --- Alpha test / depth test / blend mode ----------------------------------
    auto compare_supported = [](uint32_t func) {
        return func >= 0x0200 && func <= 0x0207;
    };
    auto compare = [](uint32_t func, double lhs, double rhs) {
        switch (func) {
            case 0x0200: return false;               // NEVER
            case 0x0201: return lhs < rhs;            // LESS
            case 0x0202: return lhs == rhs;           // EQUAL
            case 0x0203: return lhs <= rhs;           // LEQUAL
            case 0x0204: return lhs > rhs;            // GREATER
            case 0x0205: return lhs != rhs;           // NOTEQUAL
            case 0x0206: return lhs >= rhs;           // GEQUAL
            default: return true;                     // ALWAYS (0x0207)
        }
    };
    // Alpha/depth test enable bits (0 and 3) are semantic: when a test is disabled,
    // the corresponding *_func register is unread by hardware and must not gate a
    // skip no matter what stale value it holds.
    const bool alpha_test_enabled = (enables & (1u << 0)) != 0;
    const bool depth_test_enabled_bit = (enables & (1u << 3)) != 0;
    const uint32_t alpha_func = raster_state[kRasterAlphaFunc];
    const uint32_t depth_func = raster_state[kRasterDepthFunc];
    const uint32_t alpha_ref = raster_state[kRasterAlphaRef] & 0xFFu;
    const bool zero_alpha_gequal_noop =
        alpha_test_enabled && alpha_func == 0x0206 && alpha_ref != 0 &&
        stage_count == 1 &&
        raster_state[kRasterCombinerAlphaIcw0] == 0x11200000 &&
        raster_state[kRasterCombinerAlphaOcw0] == 0x00000C00 &&
        ((raster_state[kRasterCombinerFactor0_0] >> 24) & 0xFFu) == 0 &&
        final_cw0 == 0x0000000C && final_cw1 == 0x00001C80;
    if (alpha_test_enabled && !compare_supported(alpha_func)) {
        record_generic_skip_detail(GenericSkipReason::kAlphaFunc, alpha_func);
        return true;
    }
    if (depth_test_enabled_bit && !compare_supported(depth_func)) {
        record_generic_skip_detail(GenericSkipReason::kDepthFunc, depth_func);
        return true;
    }
    // Blend enable (bit1) is semantic too: when disabled, BlendSrc/Dst/Equation may
    // hold anything and must not gate a skip. When enabled, support the witnessed
    // SRC_ALPHA/ONE_MINUS_SRC_ALPHA/ADD combo plus the trivially adjacent factors
    // (ZERO/ONE/SRC_ALPHA/ONE_MINUS_SRC_ALPHA) needed to express "disabled via factors"
    // and simple additive/replace blends.
    const bool blend_enabled = !logic_copy && (enables & (1u << 1)) != 0;
    const uint32_t blend_src = raster_state[kRasterBlendSrc];
    const uint32_t blend_dst = raster_state[kRasterBlendDst];
    const uint32_t blend_eq = raster_state[kRasterBlendEquation];
    auto blend_factor_supported = [](uint32_t factor) {
        return factor == 0 || factor == 1 || factor == 0x0302 || factor == 0x0303;
    };
    if (blend_enabled) {
        if (blend_eq != 0x8006) {
            record_generic_skip_detail(GenericSkipReason::kUnsupportedBlendEquation,
                                       blend_eq);
            return true;
        }
        if (!blend_factor_supported(blend_src) || !blend_factor_supported(blend_dst)) {
            record_generic_skip_detail(
                GenericSkipReason::kBlendMode,
                (static_cast<uint64_t>(blend_src) << 32) | blend_dst);
            return true;
        }
    }

    const uint32_t clip_x = surface_clip_horizontal & 0xFFFFu;
    const uint32_t clip_width = surface_clip_horizontal >> 16;
    const uint32_t clip_y = surface_clip_vertical & 0xFFFFu;
    const uint32_t clip_height = surface_clip_vertical >> 16;
    if (!clip_width || !clip_height) {
        record_generic_skip(GenericSkipReason::kUnsupportedSurface);
        return true;
    }
    // SET_SURFACE_PITCH packs an independent color pitch (bits 15:0) and zeta pitch
    // (bits 31:16) -- real hardware allows these to differ (e.g. the dashboard's own
    // witnessed 0x00401400: color pitch 0x1400, zeta pitch 0x0040). Only the color
    // pitch is required to size the color surface; the zeta pitch is validated
    // separately, and only when the zeta surface is actually touched below.
    const bool depth_write_enabled = raster_state[kRasterDepthMask] != 0;
    const bool wants_zeta = depth_test_enabled_bit || depth_write_enabled;
    const uint32_t pitch = surface_pitch & 0xFFFFu;
    const uint32_t zeta_pitch = surface_pitch >> 16;
    uint32_t surface_width = clip_x + clip_width;
    uint32_t surface_height = clip_y + clip_height;
    uint64_t color_bytes64 = 0;
    std::unique_ptr<SwizzleLookup> surface_swizzle;
    if (surface_pitch_linear) {
        color_bytes64 = static_cast<uint64_t>(pitch) * surface_height;
        if (!pitch || static_cast<uint64_t>(surface_width) * 4 > pitch ||
            color_bytes64 > SIZE_MAX) {
            record_generic_skip_detail(GenericSkipReason::kUnsupportedSurface, pitch);
            return true;
        }
    } else {
        const uint32_t width_log2 = (surface_format >> 16) & 0xFFu;
        const uint32_t height_log2 = (surface_format >> 24) & 0xFFu;
        if (width_log2 >= 31 || height_log2 >= 31) {
            record_generic_skip_detail(GenericSkipReason::kUnsupportedSurface,
                                       surface_format);
            return true;
        }
        surface_width = 1u << width_log2;
        surface_height = 1u << height_log2;
        if (clip_x + clip_width > surface_width ||
            clip_y + clip_height > surface_height) {
            record_generic_skip_detail(GenericSkipReason::kUnsupportedSurface,
                                       surface_format);
            return true;
        }
        const uint32_t expected_pitch = surface_width * sizeof(uint32_t);
        if (pitch != expected_pitch ||
            (wants_zeta && zeta_pitch != expected_pitch)) {
            record_generic_skip_detail(GenericSkipReason::kUnsupportedSurface,
                                       surface_pitch);
            return true;
        }
        color_bytes64 =
            static_cast<uint64_t>(surface_width) * surface_height * sizeof(uint32_t);
        if (color_bytes64 > SIZE_MAX) {
            record_generic_skip_detail(GenericSkipReason::kUnsupportedSurface,
                                       surface_format);
            return true;
        }
        surface_swizzle = std::make_unique<SwizzleLookup>(surface_width,
                                                          surface_height);
    }
    // The zeta surface only needs to be touched at all if the depth test is actually
    // enabled (a real comparison against stored depth) or a write is requested; when
    // depth testing is disabled, hardware treats every fragment as passing without
    // ever reading zeta -- and the (possibly unrelated/smaller) zeta pitch never needs
    // to be validated at all in that case.
    // A mismatched zeta pitch (e.g. the witnessed 0x00400a00: color pitch 0xa00/2560
    // for a 640-wide surface, zeta pitch 0x0040/64 -- clearly not describing the same
    // 640-wide zeta buffer) or a color_format that doesn't even carry a zeta component
    // are both treated the same way as an unresolvable zeta DMA context below: degrade
    // to color-only rendering rather than dropping the whole batch. Every witnessed
    // profile above requires an exact pitch/format match because their fixed pixel
    // formulas always read zeta; this evaluator's depth path is already conditioned on
    // wants_zeta, so "can't use this zeta" and "don't want zeta" collapse to the same
    // uses_depth=false outcome.
    bool uses_depth = zeta_format == 2 && wants_zeta &&
                      (surface_swizzled || zeta_pitch == pitch);
    uint64_t depth_bytes64 = uses_depth ? color_bytes64 : 0;

    // BUG FIX (guest-memory-corruption regression): a raster batch can bind a DMA
    // context whose PRAMIN descriptor was never actually programmed with a real
    // frame/base (e.g. an "identity, whole-of-RAM" object reused from an unrelated
    // purpose, or a genuinely all-zero/default descriptor) together with a zero
    // SURFACE_*_OFFSET. dma_object_address() then resolves that -- entirely
    // correctly, per the descriptor's own bits -- to physical address 0. Every
    // hand-derived Profile above only ever witnessed real, deep-into-RAM color/zeta
    // addresses, so none of them needed to guard against this; this generic path is
    // the first to reach raster states where it actually happens (observed: a
    // depth-mask-enabled boot-animation draw whose zeta context resolves to address
    // 0), and writing depth data through PagedRasterSurface(address=0, size=...)
    // corrupts the first few hundred KB of guest RAM -- exactly where 2BL is loaded,
    // producing an "invalid instruction" fault a few thousand guest instructions
    // later. PagedRasterSurface only bounds the OFFSET within a surface; it has no
    // way to know the BASE address itself is bogus. Physical address 0 is never a
    // legitimate color/zeta/texture target anywhere else in this file (every pinned
    // profile's witnessed addresses are always deep into RAM), so treat a resolved
    // address of exactly 0 as an invalid/unbound surface rather than writing through
    // it.
    auto resolve_surface = [&](uint32_t dma, bool bound, uint32_t offset,
                               uint64_t bytes, uint32_t& address) {
        if (!bound || !bytes ||
            static_cast<uint64_t>(offset) + bytes - 1 > UINT32_MAX) {
            return false;
        }
        uint32_t last_address = 0;
        const uint32_t last_offset = offset + static_cast<uint32_t>(bytes - 1);
        return dma_object_address(dma, offset, address) &&
               dma_object_address(dma, last_offset, last_address) &&
               last_address >= address &&
               static_cast<uint64_t>(last_address) - address + 1 == bytes &&
               address != 0;
    };
    uint32_t color_address = 0;
    uint32_t depth_address = 0;
    if (!resolve_surface(color_dma_, color_dma_bound_, surface_color_offset,
                         color_bytes64, color_address)) {
        fault_("NV2A generic-combiner color surface is unbound or overflows");
        return false;
    }
    if (uses_depth &&
        !resolve_surface(zeta_dma_, zeta_dma_bound_, surface_zeta_offset,
                         depth_bytes64, depth_address)) {
        // The zeta context resolves to something this evaluator cannot safely treat
        // as a real depth buffer (unbound, out of range, or degenerate -- e.g. an
        // "identity" DMA context combined with a zero SURFACE_ZETA_OFFSET, which
        // legitimately resolves to physical address 0 and must never be written
        // through). Render color-only rather than hard-faulting the whole draw: the
        // color target is still valid, and treating "no usable zeta" as "no depth
        // test/write this batch" is the safe, recoverable interpretation. The batch
        // still executes (color-only), so this is not counted in the skip census --
        // it is not a dropped batch, just a degraded one.
        uses_depth = false;
        depth_bytes64 = 0;
        depth_address = 0;
    }
    if (uses_depth) {
        const uint64_t color_end = static_cast<uint64_t>(color_address) + color_bytes64;
        const uint64_t depth_end = static_cast<uint64_t>(depth_address) + depth_bytes64;
        if (color_address < depth_end && depth_address < color_end) {
            fault_("NV2A generic-combiner color and zeta surfaces overlap");
            return false;
        }
    }
    uint32_t effective_xmin = clip_x;
    uint32_t effective_xmax = clip_x + clip_width - 1;
    uint32_t effective_ymin = clip_y;
    uint32_t effective_ymax = clip_y + clip_height - 1;
    const uint32_t window_x = raster_state[kRasterWindowClipHorizontal];
    const uint32_t window_y = raster_state[kRasterWindowClipVertical];
    effective_xmin = std::max(effective_xmin, window_x & 0xFFFu);
    effective_xmax = std::min(effective_xmax, (window_x >> 16) & 0xFFFu);
    effective_ymin = std::max(effective_ymin, window_y & 0xFFFu);
    effective_ymax = std::min(effective_ymax, (window_y >> 16) & 0xFFFu);
    const bool effective_clip_valid =
        effective_xmax >= effective_xmin && effective_ymax >= effective_ymin;
    const uint32_t effective_clip_x = effective_clip_valid ? effective_xmin : 0;
    const uint32_t effective_clip_y = effective_clip_valid ? effective_ymin : 0;
    const uint32_t effective_clip_width =
        effective_clip_valid ? (effective_xmax - effective_xmin + 1u) : 0;
    const uint32_t effective_clip_height =
        effective_clip_valid ? (effective_ymax - effective_ymin + 1u) : 0;
    // --- Texture stages 0-3 ------------------------------------------------------
    // Only stages a combiner input actually reads are set up. Per-stage texture mode
    // is a real 5-bit field (matching xemu's pgraph_glsl_gen_psh() exactly:
    // "ps.tex_modes[i] = (state->shader_stage_program >> (i*5)) & 0x1F", NOT a per-
    // stage nibble as this evaluator originally guessed): mode 0 = NONE, 1 =
    // PROJECT2D, 3 = CUBEMAP are the ones implemented below (cross-checked against
    // psh_regs.h's PS_TEXTUREMODES enum); BUMPENVMAP/DOT_ST/DOT_STR/BRDF/PASSTHRU/
    // CLIPPLANE/PROJECT3D and any other mode is a named census, not a guess.
    //   * PROJECT2D (any stage, mode==1): the exact witnessed dashboard fingerprint
    //     (ShaderStageProgram==0x00000001, a single active stage0) is
    //     kInlineLinearBlitColor's own linear/pitch layout -- kept as that exact,
    //     narrowly-proven special case. Every other PROJECT2D stage (including
    //     multi-stage chains like 0x00008421, four independent PROJECT2D stages) is
    //     sampled exactly like kSwizzledTexturedColor/kPitchSingleTextureColor/
    //     kTex7FanColor's own textures -- format log2 width/height, SwizzleLookup
    //     addressing, normalized 0..1 UV. COLOR codes 6/7 (already cross-checked
    //     against those pinned profiles, both raw 32-bit ARGB8) plus 0x19/SZ_A8 (a
    //     swizzled 1-byte-per-texel alpha-only format, production-witnessed on the
    //     menu scene's T1-T3: format word 0x0881192a) are accepted; texel format
    //     itself is decoupled from texture *mode* -- both are independent register
    //     fields on real hardware. Any other COLOR code (DXT-compressed or other
    //     SZ_*/LU_* variants) stays a named kUnsupportedTextureFormat census.
    //   * CUBEMAP (mode==3, e.g. ShaderStageProgram==0x00000063's two active stages):
    //     sampled with the exact same major-axis direction-to-UV algorithm as
    //     kSwizzledTexturedColor/kSwizzledProjectedColor's own sample_cube_texture
    //     (a fresh independent copy, same reasoning as every other sampler here),
    //     against the same swizzled 2D texture data/format whitelist as PROJECT2D.
    enum class TextureAddressMode { kRepeat, kClampToEdge, kBorder };
    struct TextureStage {
        bool valid = false;
        bool linear = false;
        bool project2d = false;
        bool cubemap = false;
        bool alpha8 = false;
        std::vector<uint8_t> data;
        uint32_t width = 0, height = 0, pitch = 0;
        std::unique_ptr<SwizzleLookup> swizzle;
        TextureAddressMode address_u = TextureAddressMode::kRepeat;
        TextureAddressMode address_v = TextureAddressMode::kRepeat;
        uint32_t border = 0;
    };
    std::array<TextureStage, 4> texture_stages;
    size_t generic_texture_bytes_this_batch = 0;
    const uint32_t shader_stage_program = raster_state[kRasterShaderStageProgram];
    auto decode_address_mode = [](uint32_t byte, bool& supported) {
        supported = true;
        switch (byte) {
            case 1: return TextureAddressMode::kRepeat;
            case 3: return TextureAddressMode::kClampToEdge;
            case 4: return TextureAddressMode::kBorder;
            default: supported = false; return TextureAddressMode::kRepeat;
        }
    };
    auto resolve_texture_surface = [&](uint32_t format, uint32_t offset,
                                        uint64_t bytes64, uint32_t& address) {
        const bool uses_dma_b = (format & 2u) != 0;
        if (!resolve_surface(uses_dma_b ? texture_dma_b_ : texture_dma_a_,
                             uses_dma_b ? texture_dma_b_bound_
                                         : texture_dma_a_bound_,
                             offset, bytes64, address)) {
            return false;
        }
        // Seam anchor 6 (F6), generic path.
        seam_texture_source_anchor(address, bytes64);
        return true;
    };
    auto unpack_rgb565 = [](uint16_t value) {
        const uint32_t r5 = (value >> 11) & 0x1Fu;
        const uint32_t g6 = (value >> 5) & 0x3Fu;
        const uint32_t b5 = value & 0x1Fu;
        return std::array<uint8_t, 3>{{
            static_cast<uint8_t>((r5 << 3) | (r5 >> 2)),
            static_cast<uint8_t>((g6 << 2) | (g6 >> 4)),
            static_cast<uint8_t>((b5 << 3) | (b5 >> 2)),
        }};
    };
    auto decode_dxt23_argb = [&](const std::vector<uint8_t>& compressed,
                                 uint32_t width, uint32_t height,
                                 uint32_t compressed_pitch,
                                 std::vector<uint8_t>& argb) {
        if (!width || !height) return false;
        const uint32_t block_cols = (width + 3u) / 4u;
        const uint32_t block_rows = (height + 3u) / 4u;
        const uint64_t min_pitch64 = static_cast<uint64_t>(block_cols) * 16u;
        if (compressed_pitch < min_pitch64) return false;
        const uint64_t required64 =
            static_cast<uint64_t>(compressed_pitch) * block_rows;
        const uint64_t output64 = static_cast<uint64_t>(width) * height * 4u;
        if (required64 > compressed.size() || output64 > SIZE_MAX) return false;
        argb.assign(static_cast<size_t>(output64), 0);
        for (uint32_t by = 0; by < block_rows; ++by) {
            for (uint32_t bx = 0; bx < block_cols; ++bx) {
                const size_t block_offset =
                    static_cast<size_t>(by) * compressed_pitch +
                    static_cast<size_t>(bx) * 16u;
                const uint8_t* block = compressed.data() + block_offset;
                uint64_t alpha_bits = 0;
                for (uint32_t byte = 0; byte < 8; ++byte)
                    alpha_bits |= static_cast<uint64_t>(block[byte]) << (byte * 8);
                const uint16_t c0 =
                    static_cast<uint16_t>(block[8] | (block[9] << 8));
                const uint16_t c1 =
                    static_cast<uint16_t>(block[10] | (block[11] << 8));
                std::array<std::array<uint8_t, 3>, 4> colors{};
                colors[0] = unpack_rgb565(c0);
                colors[1] = unpack_rgb565(c1);
                for (uint32_t component = 0; component < 3; ++component) {
                    colors[2][component] = static_cast<uint8_t>(
                        (2u * colors[0][component] + colors[1][component] + 1u) / 3u);
                    colors[3][component] = static_cast<uint8_t>(
                        (colors[0][component] + 2u * colors[1][component] + 1u) / 3u);
                }
                const uint32_t color_bits =
                    static_cast<uint32_t>(block[12]) |
                    (static_cast<uint32_t>(block[13]) << 8) |
                    (static_cast<uint32_t>(block[14]) << 16) |
                    (static_cast<uint32_t>(block[15]) << 24);
                for (uint32_t py = 0; py < 4; ++py) {
                    const uint32_t y = by * 4u + py;
                    if (y >= height) continue;
                    for (uint32_t px = 0; px < 4; ++px) {
                        const uint32_t x = bx * 4u + px;
                        if (x >= width) continue;
                        const uint32_t texel = py * 4u + px;
                        const uint8_t alpha4 =
                            static_cast<uint8_t>((alpha_bits >> (texel * 4u)) & 0xFu);
                        const uint8_t alpha8 =
                            static_cast<uint8_t>((alpha4 << 4) | alpha4);
                        const uint32_t color_index =
                            (color_bits >> (texel * 2u)) & 0x3u;
                        const auto& rgb = colors[color_index];
                        const size_t out =
                            (static_cast<size_t>(y) * width + x) * 4u;
                        argb[out + 0] = rgb[2];
                        argb[out + 1] = rgb[1];
                        argb[out + 2] = rgb[0];
                        argb[out + 3] = alpha8;
                    }
                }
            }
        }
        return true;
    };
    auto decode_dxt1_argb = [&](const std::vector<uint8_t>& compressed,
                                uint32_t width, uint32_t height,
                                uint32_t compressed_pitch,
                                std::vector<uint8_t>& argb) {
        if (!width || !height) return false;
        const uint32_t block_cols = (width + 3u) / 4u;
        const uint32_t block_rows = (height + 3u) / 4u;
        const uint64_t min_pitch64 = static_cast<uint64_t>(block_cols) * 8u;
        if (compressed_pitch < min_pitch64) return false;
        const uint64_t required64 =
            static_cast<uint64_t>(compressed_pitch) * block_rows;
        const uint64_t output64 = static_cast<uint64_t>(width) * height * 4u;
        if (required64 > compressed.size() || output64 > SIZE_MAX) return false;
        argb.assign(static_cast<size_t>(output64), 0);
        for (uint32_t by = 0; by < block_rows; ++by) {
            for (uint32_t bx = 0; bx < block_cols; ++bx) {
                const size_t block_offset =
                    static_cast<size_t>(by) * compressed_pitch +
                    static_cast<size_t>(bx) * 8u;
                const uint8_t* block = compressed.data() + block_offset;
                const uint16_t c0 =
                    static_cast<uint16_t>(block[0] | (block[1] << 8));
                const uint16_t c1 =
                    static_cast<uint16_t>(block[2] | (block[3] << 8));
                std::array<std::array<uint8_t, 4>, 4> colors{};
                const auto rgb0 = unpack_rgb565(c0);
                const auto rgb1 = unpack_rgb565(c1);
                colors[0] = {{rgb0[0], rgb0[1], rgb0[2], 0xFF}};
                colors[1] = {{rgb1[0], rgb1[1], rgb1[2], 0xFF}};
                if (c0 > c1) {
                    for (uint32_t component = 0; component < 3; ++component) {
                        colors[2][component] = static_cast<uint8_t>(
                            (2u * colors[0][component] + colors[1][component] + 1u) /
                            3u);
                        colors[3][component] = static_cast<uint8_t>(
                            (colors[0][component] + 2u * colors[1][component] + 1u) /
                            3u);
                    }
                    colors[2][3] = colors[3][3] = 0xFF;
                } else {
                    for (uint32_t component = 0; component < 3; ++component) {
                        colors[2][component] = static_cast<uint8_t>(
                            (static_cast<uint32_t>(colors[0][component]) +
                             colors[1][component]) /
                            2u);
                    }
                    colors[2][3] = 0xFF;
                    colors[3] = {{0, 0, 0, 0}};
                }
                const uint32_t color_bits =
                    static_cast<uint32_t>(block[4]) |
                    (static_cast<uint32_t>(block[5]) << 8) |
                    (static_cast<uint32_t>(block[6]) << 16) |
                    (static_cast<uint32_t>(block[7]) << 24);
                for (uint32_t py = 0; py < 4; ++py) {
                    const uint32_t y = by * 4u + py;
                    if (y >= height) continue;
                    for (uint32_t px = 0; px < 4; ++px) {
                        const uint32_t x = bx * 4u + px;
                        if (x >= width) continue;
                        const uint32_t texel = py * 4u + px;
                        const uint32_t color_index =
                            (color_bits >> (texel * 2u)) & 0x3u;
                        const auto& rgba = colors[color_index];
                        const size_t out =
                            (static_cast<size_t>(y) * width + x) * 4u;
                        argb[out + 0] = rgba[2];
                        argb[out + 1] = rgba[1];
                        argb[out + 2] = rgba[0];
                        argb[out + 3] = rgba[3];
                    }
                }
            }
        }
        return true;
    };
    std::chrono::steady_clock::time_point generic_texture_setup_started_at;
    if (profile_generic_fingerprint)
        generic_texture_setup_started_at = std::chrono::steady_clock::now();
    if (generic_state_timer) generic_state_timer.reset();
    {
        // NV2A_TEXTURE: surface resolve, DMA read, DXT decode and swizzle-table build for
        // this batch. This block is the single funnel every texture stage passes through in
        // the generic path; the enclosing function is monolithic, so the region sits at the
        // funnel rather than at a definition.
        perf_excl::Scope texture_region(perf_excl::kNv2aTexture);
        std::optional<perf::ScopedAdd> texture_setup_timer;
        if (perf::g_enabled) {
            texture_setup_timer.emplace(perf::g_ns_generic_texture_setup,
                                        perf::g_calls_generic_texture_setup);
        }
        // Texture shaders run before the register combiners. Independent NONE,
        // PROJECT2D and CUBEMAP stages cannot feed a later stage back into T0.
        // Other modes may discard fragments or replace depth even when their
        // color is not referenced by an ICW, so do not silently omit them.
        for (uint32_t stage = 0; stage < 4; ++stage) {
            const uint32_t mode = (shader_stage_program >> (stage * 5)) & 0x1Fu;
            if (mode != 0 && mode != 1 && mode != 3) {
                record_generic_skip_detail(
                    GenericSkipReason::kUnsupportedTextureShaderProgram,
                    (static_cast<uint64_t>(mode) << 8) | stage);
                return true;
            }
        }
        for (uint32_t stage = 0; stage < 4; ++stage) {
        if (!texture_referenced[stage]) continue;
        if ((raster_state[kRasterTextureEnableMask] & (1u << stage)) == 0) {
            record_generic_skip_detail(GenericSkipReason::kTextureStage, stage);
            return true;
        }
        const uint32_t stage_mode = (shader_stage_program >> (stage * 5)) & 0x1Fu;
        TextureStage& ts = texture_stages[stage];
        const bool single_stage_project2d =
            shader_stage_program == 1 && stage_mode == 1 && stage == 0;
        const bool independent_project2d =
            stage_mode == 1 &&
            (raster_state[kRasterTextureFormat0 + stage] & (1u << 2)) == 0;
        ts.project2d = stage_mode == 1;
        bool handled_linear_project2d = false;
        if (single_stage_project2d) {
            const uint32_t format = raster_state[kRasterTextureFormat0 + stage];
            const uint32_t color_code = (format >> 8) & 0xFFu;
            const bool legacy_raw_linear = format == 0;
            const bool dxt1_argb8 = color_code == 0x0Cu;
            const bool dxt23_argb8 = color_code == 0x0Eu;
            const bool compressed_argb8 = dxt1_argb8 || dxt23_argb8;
            const bool packed_yuv = color_code == 0x24 || color_code == 0x25;
            if(packed_yuv && (!(raster_state[kRasterControl0] & (1u<<28)))) {
                fault_("NV2A packed YUV sampling without conversion is outside the bounded reference");
                return false;
            }
            // The linear PROJECT2D path is also used for pitch render targets
            // rebound as textures (e.g. the stock dashboard present path's
            // format 0x00011e29). Those were already proven as raw pitch-linear
            // words before compressed dashboard textures were encountered, so
            // preserve that behavior for every non-DXT format and specialize
            // only the compressed color codes witnessed in the dashboard.
            const bool raw_argb8 = !compressed_argb8 && !packed_yuv;
            const uint32_t rect = raster_state[kRasterTextureImageRect0 + stage];
            ts.width = rect >> 16;
            ts.height = rect & 0xFFFFu;
            ts.pitch = raster_state[kRasterTextureControl1_0 + stage] >> 16;
            if (ts.width && ts.height && (raw_argb8 || compressed_argb8 || packed_yuv)) {
                if(packed_yuv && (ts.width&1)) {
                    fault_("NV2A packed YUV requires complete chroma pairs");return false;
                }
                const uint32_t address_word =
                    raster_state[kRasterTextureAddress0 + stage];
                bool u_ok = true, v_ok = true;
                TextureAddressMode address_u = TextureAddressMode::kClampToEdge;
                TextureAddressMode address_v = TextureAddressMode::kClampToEdge;
                if (address_word != 0) {
                    address_u = decode_address_mode(address_word & 0xFFu, u_ok);
                    address_v = decode_address_mode((address_word >> 8) & 0xFFu, v_ok);
                }
                if (!u_ok || !v_ok) {
                    record_generic_skip_detail(GenericSkipReason::kUnsupportedTextureWrap,
                                               address_word);
                    return true;
                }
                const uint32_t min_pitch =
                    packed_yuv ? ts.width * 2u : raw_argb8 ? ts.width * static_cast<uint32_t>(sizeof(uint32_t))
                              : ((ts.width + 3u) / 4u) *
                                    (dxt1_argb8 ? 8u : 16u);
                const uint32_t source_rows =
                    compressed_argb8 ? ((ts.height + 3u) / 4u) : ts.height;
                if (ts.pitch < min_pitch) {
                    record_generic_skip_detail(
                        GenericSkipReason::kUnsupportedTextureFormat, format);
                    return true;
                }
                const uint64_t bytes64 =
                    static_cast<uint64_t>(ts.pitch) * source_rows;
                uint32_t address = 0;
                if (bytes64 > SIZE_MAX ||
                    !(legacy_raw_linear
                          ? resolve_surface(texture_dma_b_, texture_dma_b_bound_,
                                            raster_state[kRasterTextureOffset0 + stage],
                                            bytes64, address)
                          : resolve_texture_surface(
                                format,
                                raster_state[kRasterTextureOffset0 + stage],
                                bytes64, address))) {
                    fault_("NV2A generic-combiner texture surface is unbound or overflows");
                    return false;
                }
                // Seam anchor 6 (F6): the legacy raw-linear branch above bypasses
                // resolve_texture_surface, so it raises the anchor here instead.
                if (legacy_raw_linear) seam_texture_source_anchor(address, bytes64);
                std::vector<uint8_t> source;
                std::vector<uint8_t>* read_target = &ts.data;
                if (compressed_argb8 || packed_yuv) read_target = &source;
                read_target->resize(static_cast<size_t>(bytes64));
                {
                    perf::ScopedAdd timer(perf::g_ns_generic_texture_resolve,
                                          perf::g_calls_generic_texture_resolve);
                    // Phase 6 census, linear/compressed branch. Same accounting
                    // as the swizzled branch below; both call sites carry the
                    // same "generic texture read" context, so the census row
                    // and [perf-renderer-resolve-context] count the same calls.
                    const uint64_t transfers_before = seam_resolve_transfers_;
                    if (!resolve_renderer_color_overlap(address, read_target->size(),
                                                        "generic texture read")) {
                        return false;
                    }
                    generic_batch_texture_resolves_ +=
                        seam_resolve_transfers_ - transfers_before;
                }
                {
                    perf::ScopedAdd timer(perf::g_ns_generic_texture_dma_read,
                                          perf::g_calls_generic_texture_dma_read);
                    if (!mem_read_ ||
                        !mem_read_(address, read_target->data(), read_target->size())) {
                        fault_("NV2A generic-combiner texture DMA read failed");
                        return false;
                    }
                }
                if(packed_yuv) {
                    // Bounded BT.601 studio-range reference, independently
                    // evaluated in floating point. Native uses Phobos's integer
                    // conversion; allow one output unit of rounding difference.
                    // DMA source stays packed and unmodified. Exact NV2A filter
                    // order/chroma interpolation remain outside this fixture.
                    ts.data.resize(size_t(ts.width)*ts.height*4);
                    auto channel=[](double value){return uint32_t(std::clamp(std::lround(value),0l,255l));};
                    const unsigned yoff=color_code==0x24?0:1;
                    for(uint32_t row=0;row<ts.height;++row)for(uint32_t x=0;x<ts.width;++x) {
                        const auto* pair=source.data()+size_t(row)*ts.pitch+(x&~1u)*2;
                        const double y=1.16438356*(int(pair[yoff+2*(x&1)])-16);
                        const int u=int(pair[1-yoff])-128,v=int(pair[3-yoff])-128;
                        const uint32_t color=0xff000000u|(channel(y+1.59602678*v)<<16)|
                            (channel(y-0.39176229*u-0.81296764*v)<<8)|channel(y+2.01723214*u);
                        std::memcpy(ts.data.data()+(size_t(row)*ts.width+x)*4,&color,4);
                    }
                } else if (compressed_argb8) {
                    perf::ScopedAdd timer(perf::g_ns_generic_texture_decode,
                                          perf::g_calls_generic_texture_decode);
                    // Adapt sp00nznet/xboxrecomp nv2a_pb_exec.c's DXT reuse:
                    // compare complete compressed bytes, never sampled hashes.
                    // Our boundary owns a whole image, so cache whole decoded
                    // images using the existing bounded storage. Resolve/DMA
                    // above still run, including guest-visible failure paths.
                    const auto* cached = dxt_decode_cache_enabled_
                        ? dxt_decode_cache_.find(source, ts.width, ts.height,
                                                 true, false, ts.pitch, color_code)
                        : nullptr;
                    if (cached) {
                        ts.data = cached->output;
                    } else {
                        const bool decoded = dxt1_argb8
                            ? decode_dxt1_argb(source, ts.width, ts.height,
                                               ts.pitch, ts.data)
                            : decode_dxt23_argb(source, ts.width, ts.height,
                                                ts.pitch, ts.data);
                        if (!decoded) {
                            record_generic_skip_detail(
                                GenericSkipReason::kUnsupportedTextureFormat, format);
                            return true;
                        }
                        if (dxt_decode_cache_enabled_)
                            dxt_decode_cache_.store(source, ts.width, ts.height,
                                true, false, ts.data, 0, 0, ts.pitch, color_code);
                    }
                }
                ts.pitch = ts.width * static_cast<uint32_t>(sizeof(uint32_t));
                ts.address_u = address_u;
                ts.address_v = address_v;
                ts.border = raster_state[kRasterTextureBorderColor0 + stage];
                generic_texture_bytes_this_batch += ts.data.size();
                perf::record_generic_texture_allocation(
                    ts.data.size(), generic_texture_bytes_this_batch);
                ts.linear = true;
                ts.valid = true;
                handled_linear_project2d = true;
            }
        }
        if (handled_linear_project2d) {
            continue;
        } else if (shader_stage_program == 0 || single_stage_project2d ||
                  independent_project2d ||
                  (stage_count >= 2 && (stage_mode == 1 || stage_mode == 3))) {
            // shader_stage_program==0 (every stage's mode is NONE/0x00) is the
            // original compatibility interpretation as ordinary swizzled 2D.
            // PROJECT2D admission depends on the texture program, not the number
            // of register-combiner stages. The independent modes checked above
            // have no backward dependency or fragment side effects. Do not
            // extend this admission to cube-format PROJECT2D: that needs layout
            // semantics beyond a single swizzled image. The pre-existing cubemap
            // compatibility path remains bounded to its prior admission here;
            // a full face-layout implementation is separate work.
            const uint32_t format = raster_state[kRasterTextureFormat0 + stage];
            const uint32_t color_code = (format >> 8) & 0xFFu;
            // Color codes 6/7 are the raw-ARGB8 (swizzled) codes cross-checked against
            // the pinned profiles' own textures; 0x19 is NV097_SET_TEXTURE_FORMAT_
            // COLOR_SZ_A8 (production-witnessed: T1-T3 of the menu scene's PROJECT2D
            // texture chain, format word 0x0881192a) -- a swizzled, 1-byte-per-texel,
            // alpha-only format (see sample_swizzled's alpha8 branch above). Anything
            // else (DXT-compressed, other SZ_*/LU_* codes) stays a named census rather
            // than a guess at a new pixel decode.
            const bool alpha8 = color_code == 0x19;
            const uint32_t dma_selector = format & 3u;
            if ((dma_selector != 1u && dma_selector != 2u) ||
                (color_code != 6 && color_code != 7 && !alpha8)) {
                record_generic_skip_detail(GenericSkipReason::kUnsupportedTextureFormat,
                                           format);
                return true;
            }
            if (raster_state[kRasterTexturePalette0 + stage] != 0) {
                record_generic_skip_detail(GenericSkipReason::kUnsupportedTextureFormat,
                                           0x80000000ull | format);
                return true;
            }
            const uint32_t address_word = raster_state[kRasterTextureAddress0 + stage];
            bool u_ok = false, v_ok = false;
            const TextureAddressMode address_u =
                decode_address_mode(address_word & 0xFFu, u_ok);
            const TextureAddressMode address_v =
                decode_address_mode((address_word >> 8) & 0xFFu, v_ok);
            if (!u_ok || !v_ok) {
                record_generic_skip_detail(GenericSkipReason::kUnsupportedTextureWrap,
                                           address_word);
                return true;
            }
            ts.width = 1u << ((format >> 20) & 0xFu);
            ts.height = 1u << ((format >> 24) & 0xFu);
            const uint32_t bytes_per_texel = alpha8 ? 1u : 4u;
            const uint64_t bytes64 =
                static_cast<uint64_t>(ts.width) * ts.height * bytes_per_texel;
            uint32_t address = 0;
            if (bytes64 > SIZE_MAX ||
                !resolve_texture_surface(
                    format, raster_state[kRasterTextureOffset0 + stage], bytes64,
                    address)) {
                fault_("NV2A generic-combiner texture surface is unbound or overflows");
                return false;
            }
            ts.data.resize(static_cast<size_t>(bytes64));
            {
                perf::ScopedAdd timer(perf::g_ns_generic_texture_resolve,
                                      perf::g_calls_generic_texture_resolve);
                // Phase 6 census: whether THIS texture read moved bytes back out
                // of a GPU-authoritative surface, taken from the same counter
                // [host] gpu-resolve-cost's `moved` column is taken from. The
                // fingerprint is not known yet (it needs the texture modes this
                // loop is still deriving), so it is banked per batch and folded
                // into the row below.
                const uint64_t transfers_before = seam_resolve_transfers_;
                if (!resolve_renderer_color_overlap(address, ts.data.size(),
                                                    "generic texture read")) {
                    return false;
                }
                generic_batch_texture_resolves_ +=
                    seam_resolve_transfers_ - transfers_before;
            }
            {
                perf::ScopedAdd timer(perf::g_ns_generic_texture_dma_read,
                                      perf::g_calls_generic_texture_dma_read);
                if (!mem_read_ || !mem_read_(address, ts.data.data(), ts.data.size())) {
                    fault_("NV2A generic-combiner texture DMA read failed");
                    return false;
                }
            }
            generic_texture_bytes_this_batch += ts.data.size();
            perf::record_generic_texture_allocation(
                ts.data.size(), generic_texture_bytes_this_batch);
            {
                perf::ScopedAdd timer(perf::g_ns_generic_texture_swizzle_lookup,
                                      perf::g_calls_generic_texture_swizzle_lookup);
                ts.swizzle = std::make_unique<SwizzleLookup>(ts.width, ts.height);
            }
            ts.address_u = address_u;
            ts.address_v = address_v;
            ts.border = raster_state[kRasterTextureBorderColor0 + stage];
            ts.linear = false;
            ts.cubemap = (stage_mode == 3);
            ts.alpha8 = alpha8;
            ts.valid = true;
        } else {
            record_generic_skip_detail(
                GenericSkipReason::kUnsupportedTextureShaderProgram,
                (static_cast<uint64_t>(stage_mode) << 8) | stage);
            return true;
        }
        }
    }
    if (profile_generic_fingerprint) {
        generic_batch_texture_setup_ns = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() -
                generic_texture_setup_started_at)
                .count());
    }
    std::optional<perf::ScopedAdd> generic_accel_timer;
    if (perf::g_enabled) {
        generic_accel_timer.emplace(perf::g_ns_generic_accel_path,
                                    perf::g_calls_generic_accel_path);
    }
    const bool only_texture0_input =
        texture_referenced[0] && !texture_referenced[1] &&
        !texture_referenced[2] && !texture_referenced[3];
    const bool texture0_input =
        only_texture0_input && texture_stages[0].valid;
    auto writes_r0_from_ab_or_sum = [](uint32_t ocw) {
        return ocw == 0x000000C0 || ocw == 0x00000C00;
    };
    const bool texture0_modulate_primary =
        texture0_input && stage_count == 1 &&
        (raster_state[kRasterCombinerColorIcw0] == 0x08040000 ||
         raster_state[kRasterCombinerColorIcw0] == 0x04080000) &&
        writes_r0_from_ab_or_sum(raster_state[kRasterCombinerColorOcw0]) &&
        (raster_state[kRasterCombinerAlphaIcw0] == 0x14200000 ||
         raster_state[kRasterCombinerAlphaIcw0] == 0x14180000) &&
        writes_r0_from_ab_or_sum(raster_state[kRasterCombinerAlphaOcw0]) &&
        final_cw0 == 0x0000000C && final_cw1 == 0x00001C80;
    const bool texture0_passthrough =
        texture0_input && stage_count == 1 &&
        raster_state[kRasterCombinerColorIcw0] == 0x08200000 &&
        raster_state[kRasterCombinerAlphaIcw0] == 0x14200000 &&
        writes_r0_from_ab_or_sum(raster_state[kRasterCombinerColorOcw0]) &&
        writes_r0_from_ab_or_sum(raster_state[kRasterCombinerAlphaOcw0]) &&
        final_cw0 == 0x0000000C && final_cw1 == 0x00001C80;
    const std::array<uint32_t, 6> texf_black_color_icw{{
        0xD920DA20, 0xCD20DB20, 0xCDC10000, 0xC8CD0000, 0xCC20C120, 0x00000000}};
    const std::array<uint32_t, 6> texf_black_alpha_icw{{
        0xD930DA30, 0xDD30DB30, 0xDDD11010, 0x00000000, 0x00000000, 0x30301010}};
    const std::array<uint32_t, 6> texf_black_color_ocw{{
        0x00000D00, 0x00000D00, 0x000000D0, 0x000000C0, 0x00000C00, 0x00000000}};
    const std::array<uint32_t, 6> texf_black_alpha_ocw{{
        0x00000D00, 0x00000D00, 0x000000D0, 0x00000000, 0x00000000, 0x000000C0}};
    bool texf_projected_black_alpha_one =
        shader_stage_program == 0x00008421 && stage_count == 6 &&
        texture_referenced[0] && texture_referenced[1] &&
        texture_referenced[2] && texture_referenced[3] &&
        texture_stages[0].valid && texture_stages[1].valid &&
        texture_stages[2].valid && texture_stages[3].valid &&
        final_cw0 == 0x0000000C && final_cw1 == 0x00001C80;
    for (uint32_t stage = 0; texf_projected_black_alpha_one && stage < 6;
         ++stage) {
        texf_projected_black_alpha_one =
            raster_state[kRasterCombinerColorIcw0 + stage] ==
                texf_black_color_icw[stage] &&
            raster_state[kRasterCombinerAlphaIcw0 + stage] ==
                texf_black_alpha_icw[stage] &&
            raster_state[kRasterCombinerColorOcw0 + stage] ==
                texf_black_color_ocw[stage] &&
            raster_state[kRasterCombinerAlphaOcw0 + stage] ==
                texf_black_alpha_ocw[stage] &&
            raster_state[kRasterCombinerFactor1_0 + stage] == 0 &&
            raster_state[kRasterCombinerFactor0_0 + stage] ==
                (stage == 2 ? 0xFF000000u : 0u);
    }
    const bool needs_general_combiner =
        !primary_color_passthrough && !texture0_modulate_primary &&
        !texture0_passthrough && !texf_projected_black_alpha_one;
    const bool needs_diffuse = !texf_projected_black_alpha_one;
    const bool needs_specular = needs_general_combiner;
    std::unique_ptr<GenericPacketSnapshot> debug_packet_snapshot;
    GenericPacketDebugInfo debug_packet_snapshot_info;

    // ---- generic-combiner SHADER census (beads-xbox.1.15.10 phase 6) --------
    // ALWAYS ON. The fingerprint is over EXACTLY the words a generated fragment
    // program is a function of and nothing else -- no surface address, no
    // offset, no clip rectangle, no vertex data -- so one generated program is
    // one row. It is taken HERE, after the combiner decode and the texture
    // setup have derived the per-stage modes and formats, and before the
    // accelerate/software decision, so every batch that reaches the decision is
    // counted exactly once whichever way it goes.
    {
        uint64_t fp = 1469598103934665603ull;
        const auto fold = [&fp](uint32_t word) {
            fp ^= word;
            fp *= 1099511628211ull;
        };
        fold(raster_state[kRasterCombinerControl]);
        fold(raster_state[kRasterShaderStageProgram]);
        fold(raster_state[kRasterShaderOtherStageInput]);
        for (uint32_t stage = 0; stage < stage_count; ++stage) {
            fold(raster_state[kRasterCombinerColorIcw0 + stage]);
            fold(raster_state[kRasterCombinerAlphaIcw0 + stage]);
            fold(raster_state[kRasterCombinerColorOcw0 + stage]);
            fold(raster_state[kRasterCombinerAlphaOcw0 + stage]);
        }
        fold(final_cw0);
        fold(final_cw1);
        // The raster words the generated program's own code depends on. Values
        // that are only UNIFORMS of an otherwise identical program (the eight
        // per-stage constant pairs, the two final factors, the texture
        // dimensions) are deliberately NOT folded: they must not split a row.
        fold(raster_state[kRasterAlphaFunc]);
        fold(alpha_test_enabled ? 1u : 0u);
        fold(blend_enabled ? ((blend_src << 16) ^ blend_dst ^ (blend_eq << 8))
                           : 0u);
        fold(raster_state[kRasterColorMask]);
        fold(uses_depth ? ((depth_test_enabled_bit ? depth_func : 0u) ^
                           (raster_state[kRasterDepthMask] ? 0x80000000u : 0u))
                        : 0u);
        fold(color_format);
        fold(surface_swizzled ? 1u : 0u);
        fold(cull_enabled ? (cull_front ? 2u : 1u) : 0u);
        fold(enables & (1u << 13));  // polygon offset
        for (uint32_t stage = 0; stage < 4; ++stage) {
            if (!texture_referenced[stage]) {
                fold(0u);
                continue;
            }
            const TextureStage& ts = texture_stages[stage];
            fold((ts.valid ? 1u : 0u) | (ts.linear ? 2u : 0u) |
                 (ts.cubemap ? 4u : 0u) | (ts.alpha8 ? 8u : 0u) |
                 (ts.project2d ? 16u : 0u) |
                 (static_cast<uint32_t>(ts.address_u) << 8) |
                 (static_cast<uint32_t>(ts.address_v) << 12));
        }
        generic_shader_row_ = generic_shader_census_row(fp);
        if (generic_shader_row_) {
            GenericShaderCensusRow& row = *generic_shader_row_;
            ++row.batches;
            row.vertices += transformed_batch.size();
            // Destination plane, first-come over four slots. Recorded for every
            // batch (not only the ones that resolve) so the denominator is
            // visible: a plane with many batches and no resolves is one the GPU
            // never owns, and moving its draws buys nothing.
            {
                size_t slot = 0;
                for (; slot < row.dest_address.size(); ++slot) {
                    if (row.dest_batches[slot] == 0) {
                        row.dest_address[slot] = color_address;
                        break;
                    }
                    if (row.dest_address[slot] == color_address) break;
                }
                if (slot < row.dest_address.size()) {
                    ++row.dest_batches[slot];
                    generic_shader_dest_slot_ = slot;
                } else {
                    ++row.dest_overflow;
                    generic_shader_dest_slot_ = row.dest_address.size();
                }
            }
            row.texture_resolves += generic_batch_texture_resolves_;
            row.primitive_modes |= 1u << (primitive_mode & 31u);
            row.combiner_control = raster_state[kRasterCombinerControl];
            row.shader_stage_program = raster_state[kRasterShaderStageProgram];
            row.other_stage_input = raster_state[kRasterShaderOtherStageInput];
            row.enables = enables;
            row.alpha_func = alpha_test_enabled ? alpha_func : 0u;
            row.alpha_ref = alpha_test_enabled ? alpha_ref : 0u;
            row.blend_src = blend_enabled ? blend_src : 0u;
            row.blend_dst = blend_enabled ? blend_dst : 0u;
            row.blend_equation = blend_enabled ? blend_eq : 0u;
            row.depth_func = depth_test_enabled_bit ? depth_func : 0u;
            row.depth_mask = raster_state[kRasterDepthMask];
            row.color_mask = color_mask_word;
            row.surface_format = surface_format;
            row.texture_enable_mask = raster_state[kRasterTextureEnableMask];
            row.cull_face = cull_enabled ? raster_state[kRasterCullFace] : 0u;
            for (uint32_t stage = 0; stage < 8; ++stage) {
                row.color_icw[stage] =
                    raster_state[kRasterCombinerColorIcw0 + stage];
                row.alpha_icw[stage] =
                    raster_state[kRasterCombinerAlphaIcw0 + stage];
                row.color_ocw[stage] =
                    raster_state[kRasterCombinerColorOcw0 + stage];
                row.alpha_ocw[stage] =
                    raster_state[kRasterCombinerAlphaOcw0 + stage];
                row.factor0[stage] =
                    raster_state[kRasterCombinerFactor0_0 + stage];
                row.factor1[stage] =
                    raster_state[kRasterCombinerFactor1_0 + stage];
            }
            row.specular_fog0 = final_cw0;
            row.specular_fog1 = final_cw1;
            row.final_factor0 = raster_state[kRasterCombinerFinalFactor0];
            row.final_factor1 = raster_state[kRasterCombinerFinalFactor1];
            row.stage_count = static_cast<uint8_t>(stage_count);
            row.uses_depth = uses_depth ? 1u : 0u;
            row.swizzled_destination = surface_swizzled ? 1u : 0u;
            row.texture_referenced = 0;
            row.texture_cubemap = 0;
            row.texture_linear = 0;
            row.texture_alpha8 = 0;
            for (uint32_t stage = 0; stage < 4; ++stage) {
                row.tex_format[stage] =
                    raster_state[kRasterTextureFormat0 + stage];
                row.tex_address[stage] =
                    raster_state[kRasterTextureAddress0 + stage];
                row.tex_filter[stage] =
                    raster_state[kRasterTextureFilter0 + stage];
                row.tex_width[stage] = texture_stages[stage].width;
                row.tex_height[stage] = texture_stages[stage].height;
                if (texture_referenced[stage])
                    row.texture_referenced |= static_cast<uint8_t>(1u << stage);
                if (texture_stages[stage].cubemap)
                    row.texture_cubemap |= static_cast<uint8_t>(1u << stage);
                if (texture_stages[stage].linear)
                    row.texture_linear |= static_cast<uint8_t>(1u << stage);
                if (texture_stages[stage].alpha8)
                    row.texture_alpha8 |= static_cast<uint8_t>(1u << stage);
            }
        } else {
            generic_shader_census_.overflow_color_resolves +=
                generic_batch_texture_resolves_;
        }
    }

    bool texture_modulate_observed_this_batch = false;
    bool texture_modulate_exact_this_batch = false;
    GenericTextureModulateRejectReason texture_modulate_reject_this_batch =
        GenericTextureModulateRejectReason::kCount;
    bool resident_observed_this_batch = false;
    bool resident_exact_this_batch = false;
    GenericResidentRejectReason resident_reject_this_batch =
        GenericResidentRejectReason::kCount;
    if (!logic_enabled && !flat_shading && !zpass_enabled_ && renderer_ && effective_clip_valid) {
#if XLLE_VSH_DRAW_OVERLAP_CENSUS
        const bool fixture_overlap = vsh_draw_overlap_enabled_ && job.vsh_overlap.valid;
        const uint64_t fixture_overlap_t0 = fixture_overlap && perf_excl::g_enabled
            ? perf_excl::tick_now() : 0;
#endif
        Nv2aGenericCombinerPacket packet;
        std::optional<GenericVectorScratchLease<Nv2aGenericCombinerVertex>>
            packet_vertices_lease;
        if (generic_vertex_scratch_enabled_) {
            packet_vertices_lease.emplace(
                generic_packet_vertices_scratch_, packet.vertices,
                generic_vertex_scratch_enabled_, generic_vertex_scratch_epoch_,
                generic_vertex_scratch_stats_, false);
        }
        // Reuse persistent surface-snapshot storage across batches: the
        // renderer contract forbids retaining packet pointers after submit
        // returns, so swapping the big vectors out and back is safe and keeps
        // their capacity (avoids per-batch multi-MB realloc + zero-fill).
        struct SurfaceScratchSwap {
            std::vector<uint8_t>& scratch;
            std::vector<uint8_t>& packet_buffer;
            SurfaceScratchSwap(std::vector<uint8_t>& s,
                               std::vector<uint8_t>& p)
                : scratch(s), packet_buffer(p) {
                packet_buffer = std::move(scratch);
                // Keep the recycled capacity but present the same visible
                // state a fresh packet would: empty with size() == 0.
                packet_buffer.clear();
            }
            ~SurfaceScratchSwap() { scratch = std::move(packet_buffer); }
        } color_input_swap(generic_color_input_scratch_, packet.color_input),
            depth_input_swap(generic_depth_input_scratch_,
                             packet.depth_input);
        packet.primitive_mode = primitive_mode;
        packet.surface_format = surface_format;
        packet.surface_pitch = surface_pitch;
        packet.surface_height = surface_height;
        packet.color_address = color_address;
        packet.depth_address = depth_address;
        packet.raster_state.assign(raster_state.begin(), raster_state.end());
#if XLLE_VSH_GPU_RESIDENT
        const bool deferred_gpu_vsh =
            job.gpu_vsh && job.gpu_vsh->mode == 3;
#else
        const bool deferred_gpu_vsh = false;
#endif
        bool packet_vertices_copied = false;
        auto copy_packet_vertices = [&]() -> bool {
            if (packet_vertices_copied) return true;
#if XLLE_VSH_GPU_RESIDENT
            if (deferred_gpu_vsh && !materialize_deferred_gpu_vsh(job))
                return false;
            if (deferred_gpu_vsh) packet.gpu_vsh.reset();
#endif
            packet.vertices.reserve(transformed_batch.size());
            for (const auto& vertex : transformed_batch) {
                packet.vertices.push_back(
                    {vertex.position, vertex.diffuse, vertex.specular, vertex.texture});
            }
            packet_vertices_copied = true;
            return true;
        };
        struct ResidentPrimaryStateInfo {
            GenericResidentRejectReason reject =
                GenericResidentRejectReason::kCount;
            bool observed = false;
            bool state = false;
            bool primary = false;
            bool constant = false;
            bool large_target = false;
        };
        auto evaluate_resident_primary_state =
            [&](size_t vertex_total) -> ResidentPrimaryStateInfo {
                using ResidentReject = GenericResidentRejectReason;
                ResidentPrimaryStateInfo info;
                const bool resident_primary_combiner_words =
                    raster_state[kRasterCombinerControl] == 1 &&
                    raster_state[kRasterCombinerColorIcw0] == 0x04200000 &&
                    raster_state[kRasterCombinerAlphaIcw0] == 0x14200000 &&
                    raster_state[kRasterCombinerColorOcw0] == 0x00000C00 &&
                    raster_state[kRasterCombinerAlphaOcw0] == 0x00000C00 &&
                    raster_state[kRasterCombinerSpecularFog0] == 0x0000000C &&
                    raster_state[kRasterCombinerSpecularFog1] == 0x00001C80;
                const bool resident_constant_color_target =
                    color_address == 0x03AA4000 && color_format_x8 &&
                    surface_type == 1 && anti_aliasing == 0 &&
                    surface_pitch == 0x00401400 && surface_height == 960 &&
                    clip_x == 0 && clip_y == 0 && clip_width == 1280 &&
                    clip_height == 960;
                const bool resident_constant_color_combiner_words =
                    resident_constant_color_target && no_texture_inputs &&
                    raster_state[kRasterCombinerControl] == 1 &&
                    raster_state[kRasterShaderStageProgram] == 0 &&
                    raster_state[kRasterTextureEnableMask] == 0 &&
                    raster_state[kRasterCombinerColorIcw0] == 0x01200000 &&
                    raster_state[kRasterCombinerAlphaIcw0] == 0x14110000 &&
                    raster_state[kRasterCombinerColorOcw0] == 0x00000C00 &&
                    raster_state[kRasterCombinerAlphaOcw0] == 0x00000C00 &&
                    raster_state[kRasterCombinerSpecularFog0] == 0x0000000C &&
                    raster_state[kRasterCombinerSpecularFog1] == 0x00001C80;
                info.primary = resident_primary_combiner_words;
                info.constant = resident_constant_color_combiner_words;
                info.observed =
                    raster_state[kRasterCombinerControl] == 1 ||
                    resident_primary_combiner_words ||
                    resident_constant_color_combiner_words;
                const bool resident_primitive_supported =
                    primitive_mode == 5 || primitive_mode == 6 || primitive_mode == 7;
                const bool resident_vertex_count_supported =
                    vertex_total >= 3 && vertex_total <= 20000;
                const bool resident_alpha_supported =
                    !alpha_test_enabled ||
                    (alpha_func == 0x0207 &&
                     (raster_state[kRasterAlphaRef] & 0xFFu) == 0);
                const bool resident_blend_supported =
                    (!blend_enabled || blend_eq == 0x8006) &&
                    (!blend_enabled ||
                     ((blend_src == 0x0302 || blend_src == 1) &&
                      (blend_dst == 0x0303 || blend_dst == 1 || blend_dst == 0)));
                const bool resident_shader_supported =
                    raster_state[kRasterShaderStageProgram] == 0 ||
                    raster_state[kRasterShaderStageProgram] == 1 ||
                    (raster_state[kRasterShaderStageProgram] == 0x20 &&
                     no_texture_inputs);
                const bool resident_other_stage_supported =
                    raster_state[kRasterShaderOtherStageInput] == 0 ||
                    raster_state[kRasterShaderOtherStageInput] == 0x00210000;
                if (!resident_primitive_supported) {
                    info.reject = ResidentReject::kUnsupportedPrimitive;
                } else if (!resident_vertex_count_supported) {
                    info.reject = ResidentReject::kVertexCount;
                } else if (uses_depth) {
                    info.reject = ResidentReject::kDepth;
                } else if (color_mask_word != 0x01010101) {
                    info.reject = ResidentReject::kColorMask;
                } else if (!color_format_x8 && !color_format_a8) {
                    info.reject = ResidentReject::kColorFormat;
                } else if (stage_count != 1) {
                    info.reject = ResidentReject::kStageCount;
                } else if (!resident_alpha_supported) {
                    info.reject = ResidentReject::kAlphaMode;
                } else if (!resident_blend_supported) {
                    info.reject = ResidentReject::kBlendMode;
                } else if (!resident_shader_supported) {
                    info.reject = ResidentReject::kShaderProgram;
                } else if (!resident_other_stage_supported) {
                    info.reject = ResidentReject::kOtherStageInput;
                } else if (!resident_primary_combiner_words &&
                           !resident_constant_color_combiner_words) {
                    info.reject = ResidentReject::kCombinerWords;
                }
                info.state = info.reject == ResidentReject::kCount;
                info.large_target =
                    color_address == 0x03AA4000 && color_format_x8 &&
                    surface_type == 1 && anti_aliasing == 0 &&
                    surface_pitch == 0x00401400 && surface_height == 960 &&
                    clip_x == 0 && clip_y == 0 && clip_width == 1280 &&
                    clip_height == 960;
                return info;
            };
#if XLLE_VSH_GPU_RESIDENT
        const size_t resident_vertex_total = deferred_gpu_vsh && job.gpu_vsh
            ? job.gpu_vsh->batch.references.size()
            : transformed_batch.size();
#else
        const size_t resident_vertex_total = transformed_batch.size();
#endif
        const ResidentPrimaryStateInfo resident_state =
            evaluate_resident_primary_state(resident_vertex_total);
        const bool deferred_resident_primary_candidate =
            deferred_gpu_vsh && resident_state.state && resident_state.primary &&
            !resident_state.constant && resident_vertex_total > 3;
        if (!deferred_resident_primary_candidate && !copy_packet_vertices())
            return false;
        bool force_software_after_deferred_backend_decline = false;
        for (uint32_t stage = 0; stage < packet.textures.size(); ++stage) {
            if (!texture_stages[stage].valid) continue;
            auto& destination = packet.textures[stage];
            const auto& source = texture_stages[stage];
            destination.referenced = true;
            destination.layout = source.linear
                ? Nv2aGenericCombinerPacket::TextureLayout::kLinear
                : Nv2aGenericCombinerPacket::TextureLayout::kSwizzled;
            destination.format = source.alpha8
                ? Nv2aGenericCombinerPacket::TextureFormat::kA8
                : Nv2aGenericCombinerPacket::TextureFormat::kArgb8;
            destination.mode = source.cubemap
                ? Nv2aGenericCombinerPacket::TextureMode::kCubemap
                : Nv2aGenericCombinerPacket::TextureMode::kProject2d;
            destination.width = source.width;
            destination.height = source.height;
            destination.pitch = source.pitch;
            destination.address_mode =
                raster_state[kRasterTextureAddress0 + stage];
            destination.border_color = source.border;
        }
        // First correctness fixture for the GL compute path.  Keep this predicate
        // intentionally exact: the host shader implements just a uniformly coloured
        // triangle and this one fully validated NV097 state.  Everything else stays
        // on the software LLE rasterizer.  The decoded fields below keep GL code
        // independent of this class's private raster-state numbering.
        const auto same_diffuse = [](const TransformedVertex& lhs,
                                     const TransformedVertex& rhs) {
            return lhs.diffuse == rhs.diffuse;
        };
        const bool compute_fixture_state =
            primitive_mode == 5 && transformed_batch.size() == 3 &&
            no_texture_inputs && !uses_depth && color_format_a8 &&
            stage_count == 1 && (!cull_enabled || !cull_front) &&
            (!alpha_test_enabled ||
             (alpha_func == 0x0207 &&
              (raster_state[kRasterAlphaRef] & 0xFFu) == 0)) &&
            !blend_enabled && color_mask_word == 0x01010101 &&
            raster_state[kRasterShaderStageProgram] == 0 &&
            raster_state[kRasterShaderOtherStageInput] == 0x00210000 &&
            raster_state[kRasterCombinerControl] == 1 &&
            raster_state[kRasterCombinerColorIcw0] == 0x04200000 &&
            raster_state[kRasterCombinerAlphaIcw0] == 0x14200000 &&
            raster_state[kRasterCombinerColorOcw0] == 0x00000C00 &&
            raster_state[kRasterCombinerAlphaOcw0] == 0x00000C00 &&
            raster_state[kRasterCombinerSpecularFog0] == 0x0000000C &&
            raster_state[kRasterCombinerSpecularFog1] == 0x00001C80 &&
            raster_state[kRasterCombinerFinalFactor0] == 0 &&
            raster_state[kRasterCombinerFinalFactor1] == 0 &&
            same_diffuse(transformed_batch[0], transformed_batch[1]) &&
            same_diffuse(transformed_batch[0], transformed_batch[2]);
        if (compute_fixture_state) {
            const auto& diffuse = transformed_batch[0].diffuse;
            auto pack_component = [](float value) {
                return static_cast<uint32_t>(std::floor(
                    std::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f));
            };
            bool positions_representable = true;
            for (size_t vertex = 0; vertex < 3; ++vertex) {
                for (size_t component = 0; component < 2; ++component) {
                    const float position = transformed_batch[vertex].position[component];
                    const double fixed = std::round(static_cast<double>(position) * 16.0);
                    if (!std::isfinite(position) ||
                        fixed < static_cast<double>(INT32_MIN) ||
                        fixed > static_cast<double>(INT32_MAX) ||
                        std::abs(fixed) > 16384.0) {
                        positions_representable = false;
                        break;
                    }
                    packet.fixture_xy_16[vertex * 2 + component] =
                        static_cast<int32_t>(fixed);
                }
                if (!positions_representable) break;
            }
            if (positions_representable) {
                const int64_t abx = static_cast<int64_t>(packet.fixture_xy_16[2]) -
                                    packet.fixture_xy_16[0];
                const int64_t aby = static_cast<int64_t>(packet.fixture_xy_16[3]) -
                                    packet.fixture_xy_16[1];
                const int64_t acx = static_cast<int64_t>(packet.fixture_xy_16[4]) -
                                    packet.fixture_xy_16[0];
                const int64_t acy = static_cast<int64_t>(packet.fixture_xy_16[5]) -
                                    packet.fixture_xy_16[1];
                // The shader's signed 32-bit edge values are safe under the
                // coordinate bound above. Only front-facing triangles belong to
                // this first fixture; other winding is still software-rendered.
                positions_representable = abx * acy - aby * acx > 0;
            }
            if (positions_representable) {
                packet.compute_fixture =
                    Nv2aGenericCombinerPacket::ComputeFixture::kUntexturedPrimaryTriangle;
                packet.fixture_clip_x = effective_clip_x;
                packet.fixture_clip_y = effective_clip_y;
                packet.fixture_clip_width = effective_clip_width;
                packet.fixture_clip_height = effective_clip_height;
                packet.fixture_source_argb =
                    (pack_component(diffuse[3]) << 24) |
                    (pack_component(diffuse[0]) << 16) |
                    (pack_component(diffuse[1]) << 8) |
                    pack_component(diffuse[2]);
            }
        }
        auto exact_fixed_xy = [](float value, int32_t expected) {
            if (!std::isfinite(value)) return false;
            const double fixed = std::round(static_cast<double>(value) * 16.0);
            return fixed == static_cast<double>(expected);
        };
        const std::array<std::array<int32_t, 2>, 4> dashboard_xy{{
            {{8, 7688}},
            {{8, 8}},
            {{20488, 8}},
            {{20488, 7688}},
        }};
        bool dashboard_fullscreen_vertices =
            primitive_mode == 7 && transformed_batch.size() == 4;
        for (size_t vertex = 0; dashboard_fullscreen_vertices && vertex < 4; ++vertex) {
            const auto& position = transformed_batch[vertex].position;
            dashboard_fullscreen_vertices =
                exact_fixed_xy(position[0], dashboard_xy[vertex][0]) &&
                exact_fixed_xy(position[1], dashboard_xy[vertex][1]) &&
                position[2] == 16777215.0f && position[3] == 1.0f;
            for (uint32_t stage = 0; dashboard_fullscreen_vertices && stage < 4;
                 ++stage) {
                for (float component : transformed_batch[vertex].texture[stage]) {
                    if (!std::isfinite(component)) {
                        dashboard_fullscreen_vertices = false;
                        break;
                    }
                }
            }
        }
        const bool dashboard_project2d_state =
            dashboard_fullscreen_vertices && color_format_a8 && uses_depth &&
            depth_test_enabled_bit &&
            (depth_func == 0x0202 || depth_func == 0x0207) &&
            raster_state[kRasterDepthMask] == 0 && alpha_test_enabled &&
            alpha_func == 0x0206 &&
            (raster_state[kRasterAlphaRef] & 0xFFu) == 1 &&
            blend_enabled && blend_src == 0x0302 && blend_dst == 1 &&
            blend_eq == 0x8006 && cull_enabled && !cull_front &&
            color_mask_word == 0x01010101 && clip_x == 0 && clip_y == 0 &&
            clip_width == 1280 && clip_height == 480 && pitch == 0x1400 &&
            surface_height == 480 && raster_state[kRasterWindowClipHorizontal] ==
                                        0x04ff0000 &&
            raster_state[kRasterWindowClipVertical] == 0x01df0000 &&
            raster_state[kRasterShaderStageProgram] == 0x00008421 &&
            raster_state[kRasterShaderOtherStageInput] == 0 &&
            raster_state[kRasterCombinerControl] == 0x00011106 &&
            raster_state[kRasterTextureEnableMask] == 0x0000000f &&
            texture_stages[0].valid && texture_stages[1].valid &&
            texture_stages[2].valid && texture_stages[3].valid &&
            !texture_stages[0].linear && !texture_stages[0].cubemap &&
            !texture_stages[0].alpha8 && texture_stages[0].width == 1024 &&
            texture_stages[0].height == 512 && texture_stages[1].alpha8 &&
            texture_stages[2].alpha8 && texture_stages[3].alpha8 &&
            texture_stages[1].width == 256 && texture_stages[1].height == 256 &&
            texture_stages[2].width == 256 && texture_stages[2].height == 256 &&
            texture_stages[3].width == 256 && texture_stages[3].height == 256 &&
            (raster_state[kRasterTextureAddress0] == 0x00010303 ||
             raster_state[kRasterTextureAddress0] == 0x00030303) &&
            raster_state[kRasterTextureAddress0 + 1] == 0x00030101 &&
            raster_state[kRasterTextureAddress0 + 2] == 0x00030101 &&
            raster_state[kRasterTextureAddress0 + 3] == 0x00030101;
        if (dashboard_project2d_state) {
            const std::array<uint32_t, 6> alpha_icw{{
                0xd930da30, 0xdd30db30, 0xddd11010, 0, 0, 0x30301010,
            }};
            const std::array<uint32_t, 6> color_icw{{
                0xd920da20, 0xcd20db20, 0xcdc10000,
                0xc8cd0000, 0xcc20c120, 0,
            }};
            const std::array<uint32_t, 6> alpha_ocw{{
                0x00000d00, 0x00000d00, 0x000000d0, 0, 0, 0x000000c0,
            }};
            const std::array<uint32_t, 6> color_ocw{{
                0x00000d00, 0x00000d00, 0x000000d0,
                0x000000c0, 0x00000c00, 0,
            }};
            bool combiner_words_match = true;
            for (uint32_t stage = 0; stage < 6; ++stage) {
                combiner_words_match =
                    combiner_words_match &&
                    raster_state[kRasterCombinerAlphaIcw0 + stage] ==
                        alpha_icw[stage] &&
                    raster_state[kRasterCombinerColorIcw0 + stage] ==
                        color_icw[stage] &&
                    raster_state[kRasterCombinerAlphaOcw0 + stage] ==
                        alpha_ocw[stage] &&
                    raster_state[kRasterCombinerColorOcw0 + stage] ==
                        color_ocw[stage] &&
                    raster_state[kRasterCombinerFactor1_0 + stage] == 0;
            }
            if (combiner_words_match &&
                raster_state[kRasterCombinerFactor0_0] == 0 &&
                raster_state[kRasterCombinerFactor0_0 + 1] == 0 &&
                raster_state[kRasterCombinerFactor0_0 + 3] == 0 &&
                raster_state[kRasterCombinerFactor0_0 + 5] == 0 &&
                raster_state[kRasterCombinerSpecularFog0] == 0x0000000c &&
                raster_state[kRasterCombinerSpecularFog1] == 0x00001c80 &&
                raster_state[kRasterCombinerFinalFactor0] == 0 &&
                raster_state[kRasterCombinerFinalFactor1] == 0) {
                packet.compute_fixture =
                    Nv2aGenericCombinerPacket::ComputeFixture::kDashboardProject2dFan;
                packet.fixture_factor0_stage2_argb =
                    raster_state[kRasterCombinerFactor0_0 + 2];
                packet.fixture_factor0_stage4_argb =
                    raster_state[kRasterCombinerFactor0_0 + 4];
                packet.fixture_clip_x = effective_clip_x;
                packet.fixture_clip_y = effective_clip_y;
                packet.fixture_clip_width = effective_clip_width;
                packet.fixture_clip_height = effective_clip_height;
                packet.fixture_blend_enabled = blend_enabled;
                packet.fixture_blend_src = blend_src;
                packet.fixture_blend_dst = blend_dst;
                packet.fixture_depth_test_enabled = depth_func == 0x0202;
                packet.fixture_depth_write_enabled = false;
                packet.fixture_depth_func = depth_func;
                packet.fixture_color_format_x8 = color_format_x8;
            }
        }
        bool resident_noop_batch = false;
        if (packet.compute_fixture == Nv2aGenericCombinerPacket::ComputeFixture::kNone) {
            using ResidentReject = GenericResidentRejectReason;
            const bool resident_primary_combiner_words = resident_state.primary;
            const bool resident_constant_color_combiner_words = resident_state.constant;
            const bool resident_primary_observable = resident_state.observed;
            resident_observed_this_batch = resident_primary_observable;
            ResidentReject resident_reject_reason = resident_state.reject;
            const bool resident_primary_state = resident_state.state;
            if (resident_primary_state) {
                const bool resident_primary_large_dashboard_target =
                    resident_state.large_target;
#if XLLE_VSH_GPU_RESIDENT
                if (job.gpu_vsh) {
                    job.gpu_vsh->batch.shape.large_target = resident_primary_large_dashboard_target;
                    job.gpu_vsh->batch.shape.cull_enabled = cull_enabled;
                    job.gpu_vsh->batch.shape.cull_front = cull_front;
                    if (job.gpu_vsh->mode != 3)
                        packet.gpu_vsh = job.gpu_vsh;
                }
#endif
                bool resident_deferred_prepared = false;
#if XLLE_VSH_GPU_RESIDENT
                if (deferred_resident_primary_candidate && job.gpu_vsh) {
                    std::string renderer_error;
                    if (!renderer_->prepare_vsh_gpu_batch(*job.gpu_vsh,
                                                          renderer_error)) {
                        fault_(renderer_error.empty()
                                   ? "NV2A deferred GPU VSH preparation failed"
                                   : renderer_error);
                        return false;
                    }
                    if (job.gpu_vsh->prepared_token != 0) {
                        if (job.gpu_vsh->prepared_vertices != 0) {
                            packet.gpu_vsh = job.gpu_vsh;
                            packet.compute_fixture = Nv2aGenericCombinerPacket::
                                ComputeFixture::kResidentPrimaryColorTriangles;
                            packet.primitive_mode = 5;
                        } else {
                            resident_noop_batch = true;
                        }
                        packet.fixture_clip_x = effective_clip_x;
                        packet.fixture_clip_y = effective_clip_y;
                        packet.fixture_clip_width = effective_clip_width;
                        packet.fixture_clip_height = effective_clip_height;
                        packet.fixture_blend_enabled = blend_enabled;
                        packet.fixture_blend_src = blend_src;
                        packet.fixture_blend_dst = blend_dst;
                        packet.fixture_color_format_x8 = color_format_x8;
                        ++generic_combiner_resident_candidates_;
                        resident_exact_this_batch = true;
                        resident_deferred_prepared = true;
                        ++vsh_gpu_deferred_stats_.prepared_batches;
                        vsh_gpu_deferred_stats_.prepared_vertices +=
                            job.gpu_vsh->prepared_vertices;
                    } else {
                        ++vsh_gpu_deferred_stats_.declined_batches;
                        if (!copy_packet_vertices()) return false;
                        packet.gpu_vsh.reset();
                    }
                }
#endif
                if (!resident_deferred_prepared) {
#if XLLE_VSH_CAPTURE
                auto* captured = job.vsh_batch_capture.get();
                if (captured) {
                    captured->large_target = resident_primary_large_dashboard_target;
                    captured->cull_enabled = cull_enabled;
                    captured->cull_front = cull_front;
                    // Check the captured cache map against the actual packet,
                    // independently of the codec's reconstructed cache model.
                    if (captured->references.size() != packet.vertices.size())
                        throw std::runtime_error("VSH batch capture reference count mismatch");
                    for (size_t i = 0; i < packet.vertices.size(); ++i) {
                        const auto& expected = captured->executions.at(captured->references[i]).expected;
                        const auto& vertex = packet.vertices[i];
                        if (std::memcmp(expected.data() + 28, vertex.position.data(), 16) ||
                            std::memcmp(expected.data() + 4, vertex.diffuse.data(), 16) ||
                            std::memcmp(expected.data() + 8, vertex.specular.data(), 16) ||
                            std::memcmp(expected.data() + 12, vertex.texture.data(), 64))
                            throw std::runtime_error("VSH batch capture cache/packet mismatch");
                    }
                }
                const auto capture_triangle = [&](const Nv2aGenericCombinerVertex* a,
                    const Nv2aGenericCombinerVertex* b, const Nv2aGenericCombinerVertex* c) {
                    if (!captured) return;
                    for (const auto* vertex : {a, b, c})
                        captured->expected_indices.push_back(
                            captured->references.at(static_cast<size_t>(vertex - packet.vertices.data())));
                };
#endif
                auto fixed_xy = [](float value, int64_t& fixed) {
                    if (!std::isfinite(value)) return false;
                    const double rounded =
                        std::llround(static_cast<double>(value) * 16.0);
                    if (std::abs(rounded) > 1000000.0) return false;
                    fixed = static_cast<int64_t>(rounded);
                    return true;
                };
                auto finite_vertex = [](const Nv2aGenericCombinerVertex& vertex) {
                    for (float value : vertex.position)
                        if (!std::isfinite(value)) return false;
                    if (vertex.position[3] == 0.0f) return false;
                    for (float value : vertex.diffuse)
                        if (!std::isfinite(value)) return false;
                    return true;
                };
                std::vector<Nv2aGenericCombinerVertex> expanded;
                std::optional<GenericVectorScratchLease<Nv2aGenericCombinerVertex>>
                    expanded_vertices_lease;
                if (generic_vertex_scratch_enabled_) {
                    expanded_vertices_lease.emplace(
                        generic_primary_expanded_vertices_scratch_, expanded,
                        generic_vertex_scratch_enabled_, generic_vertex_scratch_epoch_,
                        generic_vertex_scratch_stats_, true);
                }
                expanded.reserve((packet.vertices.size() - 2) * 3);
                auto append_triangle = [&](size_t ia, size_t ib, size_t ic) {
                    const auto* a = &packet.vertices[ia];
                    const auto* b = &packet.vertices[ib];
                    const auto* c = &packet.vertices[ic];
                    if (!finite_vertex(*a) || !finite_vertex(*b) || !finite_vertex(*c))
                        return false;
                    const bool all_behind_w =
                        a->position[3] < 0.0f && b->position[3] < 0.0f &&
                        c->position[3] < 0.0f;
                    if (all_behind_w) return true;
                    const bool crosses_w_plane =
                        a->position[3] < 0.0f || b->position[3] < 0.0f ||
                        c->position[3] < 0.0f;
                    if (crosses_w_plane) return false;
                    if (resident_primary_large_dashboard_target) {
                        const double ax = static_cast<double>(a->position[0]);
                        const double ay = static_cast<double>(a->position[1]);
                        const double bx = static_cast<double>(b->position[0]);
                        const double by = static_cast<double>(b->position[1]);
                        const double cx = static_cast<double>(c->position[0]);
                        const double cy = static_cast<double>(c->position[1]);
                        const double area =
                            (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
                        if (area == 0.0) return true;
                        if (!cull_enabled) {
                            if (area < 0.0) std::swap(b, c);
                        } else if (cull_front) {
                            if (area > 0.0) return true;
                            std::swap(b, c);
                        } else if (area < 0.0) {
                            return true;
                        }
                        expanded.push_back(*a);
                        expanded.push_back(*b);
                        expanded.push_back(*c);
#if XLLE_VSH_CAPTURE
                        capture_triangle(a, b, c);
#endif
                        return true;
                    }
                    int64_t ax = 0, ay = 0, bx = 0, by = 0, cx = 0, cy = 0;
                    if (!fixed_xy(a->position[0], ax) ||
                        !fixed_xy(a->position[1], ay) ||
                        !fixed_xy(b->position[0], bx) ||
                        !fixed_xy(b->position[1], by) ||
                        !fixed_xy(c->position[0], cx) ||
                        !fixed_xy(c->position[1], cy)) {
                        return false;
                    }
                    const int64_t area =
                        (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
                    if (area == 0) return true;
                    if (!cull_enabled) {
                        if (area < 0) std::swap(b, c);
                    } else if (cull_front) {
                        if (area > 0) return true;
                        std::swap(b, c);
                    } else if (area < 0) {
                        return true;
                    }
                    expanded.push_back(*a);
                    expanded.push_back(*b);
                    expanded.push_back(*c);
#if XLLE_VSH_CAPTURE
                    capture_triangle(a, b, c);
#endif
                    return true;
                };
                bool expanded_ok = true;
                if (primitive_mode == 5) {
                    for (size_t i = 0; expanded_ok && i + 2 < packet.vertices.size();
                         i += 3)
                        expanded_ok = append_triangle(i, i + 1, i + 2);
                } else if (primitive_mode == 6) {
                    for (size_t i = 2; expanded_ok && i < packet.vertices.size(); ++i) {
                        if ((i & 1u) == 0)
                            expanded_ok = append_triangle(i - 2, i - 1, i);
                        else
                            expanded_ok = append_triangle(i - 1, i - 2, i);
                    }
                } else {
                    for (size_t i = 2; expanded_ok && i < packet.vertices.size(); ++i)
                        expanded_ok = append_triangle(0, i - 1, i);
                }
#if XLLE_VSH_CAPTURE
                if (captured) {
                    captured->expected_decline = !expanded_ok;
                    if (!expanded_ok) captured->expected_indices.clear();
                    if (vsh_batch_capture_callback_(*captured)) ++vsh_batch_capture_stats_.written;
                }
#endif
                if (expanded_ok && !expanded.empty()) {
                    if (generic_vertex_scratch_enabled_) {
                        packet.vertices.swap(expanded);
                    } else {
                        packet.vertices = std::move(expanded);
                    }
                    packet.primitive_mode = 5;
                    packet.compute_fixture = Nv2aGenericCombinerPacket::
                        ComputeFixture::kResidentPrimaryColorTriangles;
                    if (resident_constant_color_combiner_words) {
                        packet.compute_fixture = Nv2aGenericCombinerPacket::
                            ComputeFixture::kResidentConstantColorTriangles;
                        packet.fixture_constant_argb =
                            raster_state[kRasterCombinerFactor0_0];
                    }
                    packet.fixture_clip_x = effective_clip_x;
                    packet.fixture_clip_y = effective_clip_y;
                    packet.fixture_clip_width = effective_clip_width;
                    packet.fixture_clip_height = effective_clip_height;
                    packet.fixture_blend_enabled = blend_enabled;
                    packet.fixture_blend_src = blend_src;
                    packet.fixture_blend_dst = blend_dst;
                    packet.fixture_color_format_x8 = color_format_x8;
                    ++generic_combiner_resident_candidates_;
                    resident_exact_this_batch = true;
                } else if (expanded_ok && expanded.empty()) {
                    resident_noop_batch = true;
                    ++generic_combiner_resident_candidates_;
                    resident_exact_this_batch = true;
                } else if (resident_primary_observable) {
                    record_generic_resident_reject(ResidentReject::kVertexExpansion);
                    resident_reject_this_batch = ResidentReject::kVertexExpansion;
                }
                }
            } else if (resident_primary_observable) {
                record_generic_resident_reject(resident_reject_reason);
                resident_reject_this_batch = resident_reject_reason;
            }
        }
        {
            using TextureReject = GenericTextureModulateRejectReason;
            const bool texture_modulate_observable =
                raster_state[kRasterShaderStageProgram] == 1 &&
                raster_state[kRasterCombinerControl] == 1;
            if (texture_modulate_observable) {
                ++generic_combiner_texture_modulate_observed_;
                texture_modulate_observed_this_batch = true;
                const bool only_texture0_input =
                    texture_referenced[0] && !texture_referenced[1] &&
                    !texture_referenced[2] && !texture_referenced[3];
                const bool primitive_supported =
                    primitive_mode == 5 || primitive_mode == 6 ||
                    primitive_mode == 7;
                const bool vertex_count_supported =
                    transformed_batch.size() >= 3 &&
                    transformed_batch.size() <= 20000;
                const bool alpha_supported =
                    !alpha_test_enabled ||
                    (alpha_func == 0x0207 &&
                     (raster_state[kRasterAlphaRef] & 0xFFu) == 0);
                const bool blend_supported =
                    (!blend_enabled || blend_eq == 0x8006) &&
                    (!blend_enabled ||
                     ((blend_src == 0x0302 || blend_src == 1) &&
                      (blend_dst == 0x0303 || blend_dst == 1 ||
                       blend_dst == 0)));
                const bool texture_depth_supported =
                    !uses_depth ||
                    (depth_test_enabled_bit && depth_func == 0x0203);
                const bool combiner_words_match =
                    raster_state[kRasterCombinerColorIcw0] == 0x04080000 &&
                    raster_state[kRasterCombinerAlphaIcw0] == 0x14180000 &&
                    raster_state[kRasterCombinerColorOcw0] == 0x00000C00 &&
                    raster_state[kRasterCombinerAlphaOcw0] == 0x00000C00 &&
                    raster_state[kRasterCombinerSpecularFog0] == 0x0000000C &&
                    raster_state[kRasterCombinerSpecularFog1] == 0x00001C80;
                const bool constant_color_alpha_combiner_words_match =
                    raster_state[kRasterCombinerColorIcw0] == 0x01200000 &&
                    raster_state[kRasterCombinerAlphaIcw0] == 0x18110000 &&
                    raster_state[kRasterCombinerColorOcw0] == 0x00000C00 &&
                    raster_state[kRasterCombinerAlphaOcw0] == 0x00000C00 &&
                    raster_state[kRasterCombinerSpecularFog0] == 0x0000000C &&
                    raster_state[kRasterCombinerSpecularFog1] == 0x00001C80;
                const bool passthrough_combiner_words_match =
                    raster_state[kRasterCombinerColorIcw0] == 0x08200000 &&
                    raster_state[kRasterCombinerAlphaIcw0] == 0x14200000 &&
                    raster_state[kRasterCombinerColorOcw0] == 0x00000C00 &&
                    raster_state[kRasterCombinerAlphaOcw0] == 0x00000C00 &&
                    raster_state[kRasterCombinerSpecularFog0] == 0x0000000C &&
                    raster_state[kRasterCombinerSpecularFog1] == 0x00001C80;
                const bool texture_modulate_registers_match =
                    raster_state[kRasterTextureFormat0] == 0x08890E2A &&
                    raster_state[kRasterTextureImageRect0] == 0x050003C0 &&
                    raster_state[kRasterTextureControl1_0] == 0x14000000;
                const bool passthrough_texture_registers_match =
                    raster_state[kRasterTextureFormat0] == 0x00011E29 &&
                    raster_state[kRasterTextureImageRect0] == 0x050003C0 &&
                    raster_state[kRasterTextureControl1_0] == 0x14000000;
                const bool texture_registers_match =
                    texture_modulate_registers_match ||
                    (passthrough_combiner_words_match &&
                     passthrough_texture_registers_match);
                const bool texture_decoded_match =
                    texture_stages[0].valid && texture_stages[0].linear &&
                    !texture_stages[0].alpha8 && texture_stages[0].width == 1280 &&
                    texture_stages[0].height == 960 &&
                    texture_stages[0].pitch == 0x1400;
                const bool target_surface_match =
                    color_address == 0x03AA4000 && color_format_x8 &&
                    surface_type == 1 && anti_aliasing == 0 &&
                    surface_pitch == 0x00401400 && surface_height == 960 &&
                    clip_x == 0 && clip_y == 0 && clip_width == 1280 &&
                    clip_height == 960;
                const bool passthrough_front_surface_match =
                    color_address == 0x03978000 && color_format_x8 &&
                    surface_type == 1 && anti_aliasing == 0 &&
                    surface_pitch == 0x00400A00 && surface_height == 480 &&
                    clip_x == 0 && clip_y == 0 && clip_width == 640 &&
                    clip_height == 480;
                const bool resident_texture_target_match =
                    (combiner_words_match && target_surface_match) ||
                    (passthrough_combiner_words_match &&
                     passthrough_front_surface_match);
                TextureReject reject_reason = TextureReject::kCount;
                if (!primitive_supported) {
                    reject_reason = TextureReject::kUnsupportedPrimitive;
                } else if (!vertex_count_supported) {
                    reject_reason = TextureReject::kVertexCount;
                } else if (!only_texture0_input) {
                    reject_reason = TextureReject::kTextureInput;
                } else if (!texture_depth_supported) {
                    reject_reason = TextureReject::kDepth;
                } else if (color_mask_word != 0x01010101) {
                    reject_reason = TextureReject::kColorMask;
                } else if (!color_format_x8 && !color_format_a8) {
                    reject_reason = TextureReject::kColorFormat;
                } else if (stage_count != 1) {
                    reject_reason = TextureReject::kStageCount;
                } else if (!alpha_supported) {
                    reject_reason = TextureReject::kAlphaMode;
                } else if (!blend_supported) {
                    reject_reason = TextureReject::kBlendMode;
                } else if (raster_state[kRasterShaderStageProgram] != 1) {
                    reject_reason = TextureReject::kShaderProgram;
                } else if (raster_state[kRasterShaderOtherStageInput] !=
                           0x00210000) {
                    reject_reason = TextureReject::kOtherStageInput;
                } else if (!combiner_words_match &&
                           !constant_color_alpha_combiner_words_match &&
                           !passthrough_combiner_words_match) {
                    reject_reason = TextureReject::kCombinerWords;
                } else if (!texture_registers_match) {
                    reject_reason = TextureReject::kTexture0Registers;
                } else if (!texture_decoded_match) {
                    reject_reason = TextureReject::kTexture0Decoded;
                } else if (!resident_texture_target_match) {
                    reject_reason = TextureReject::kTargetSurface;
                }
                if (reject_reason == TextureReject::kCount) {
                    ++generic_combiner_texture_modulate_exact_;
                    texture_modulate_exact_this_batch = true;
                } else {
                    record_generic_texture_modulate_reject(reject_reason);
                    texture_modulate_reject_this_batch = reject_reason;
                }
                const uint32_t texture_surface_width =
                    (surface_pitch & 0xffffu) / 4u;
                const bool texture_compute_surface_small =
                    texture_surface_width > 0 && texture_surface_width <= 64 &&
                    surface_height > 0 && surface_height <= 64;
                const bool texture_compute_state =
                    packet.compute_fixture ==
                        Nv2aGenericCombinerPacket::ComputeFixture::kNone &&
                    primitive_mode == 5 && transformed_batch.size() >= 3 &&
                    (transformed_batch.size() % 3u) == 0 &&
                    texture_compute_surface_small &&
                    primitive_supported && vertex_count_supported &&
                    only_texture0_input && !uses_depth &&
                    !cull_enabled &&
                    color_mask_word == 0x01010101 &&
                    (color_format_x8 || color_format_a8) && stage_count == 1 &&
                    alpha_supported && blend_supported &&
                    raster_state[kRasterShaderStageProgram] == 1 &&
                    raster_state[kRasterShaderOtherStageInput] == 0x00210000 &&
                    (combiner_words_match ||
                     constant_color_alpha_combiner_words_match) &&
                    texture_stages[0].valid && !texture_stages[0].alpha8 &&
                    (texture_stages[0].linear ||
                     (texture_stages[0].address_u ==
                          TextureAddressMode::kClampToEdge &&
                      texture_stages[0].address_v ==
                          TextureAddressMode::kClampToEdge)) &&
                    texture_stages[0].width > 0 && texture_stages[0].height > 0 &&
                    (texture_stages[0].linear
                         ? texture_stages[0].pitch >= texture_stages[0].width * 4u
                         : texture_stages[0].data.size() ==
                               static_cast<size_t>(texture_stages[0].width) *
                                   texture_stages[0].height * 4u);
                if (texture_compute_state) {
                    bool finite_vertices = true;
                    std::vector<std::array<int64_t, 2>> fixed_xy(packet.vertices.size());
                    for (const auto& vertex : packet.vertices) {
                        for (float value : vertex.position)
                            if (!std::isfinite(value)) finite_vertices = false;
                        if (vertex.position[3] != 1.0f) finite_vertices = false;
                        for (float value : vertex.diffuse)
                            if (!std::isfinite(value)) finite_vertices = false;
                        for (float value : vertex.texture[0])
                            if (!std::isfinite(value)) finite_vertices = false;
                    }
                    for (size_t vertex = 0; finite_vertices && vertex < packet.vertices.size(); ++vertex) {
                        for (size_t component = 0; component < 2; ++component) {
                            const double fixed = std::llround(
                                static_cast<double>(
                                    packet.vertices[vertex].position[component]) *
                                16.0);
                            if (std::abs(fixed) > 1000000.0) {
                                finite_vertices = false;
                                break;
                            }
                            fixed_xy[vertex][component] =
                                static_cast<int64_t>(fixed);
                        }
                    }
                    auto edge_terms_fit_int32 =
                        [](int64_t ax, int64_t ay, int64_t bx, int64_t by,
                           int64_t px, int64_t py, int64_t& value) {
                            const int64_t dx = bx - ax;
                            const int64_t dy = by - ay;
                            const int64_t pdy = py - ay;
                            const int64_t pdx = px - ax;
                            const int64_t term0 = dx * pdy;
                            const int64_t term1 = dy * pdx;
                            value = term0 - term1;
                            return term0 >= INT32_MIN && term0 <= INT32_MAX &&
                                   term1 >= INT32_MIN && term1 <= INT32_MAX &&
                                   value >= INT32_MIN && value <= INT32_MAX;
                        };
                    const int64_t clip_min_x =
                        static_cast<int64_t>(clip_x) * 16 + 8;
                    const int64_t clip_min_y =
                        static_cast<int64_t>(clip_y) * 16 + 8;
                    const int64_t clip_max_x =
                        static_cast<int64_t>(clip_x + clip_width - 1) * 16 + 8;
                    const int64_t clip_max_y =
                        static_cast<int64_t>(clip_y + clip_height - 1) * 16 + 8;
                    const std::array<std::array<int64_t, 2>, 4> clip_corners{{
                        {{clip_min_x, clip_min_y}},
                        {{clip_max_x, clip_min_y}},
                        {{clip_min_x, clip_max_y}},
                        {{clip_max_x, clip_max_y}},
                    }};
                    bool any_positive_area = false;
                    for (size_t vertex = 0; finite_vertices && vertex + 2 < fixed_xy.size();
                         vertex += 3) {
                        int64_t area = 0;
                        if (!edge_terms_fit_int32(
                                fixed_xy[vertex][0], fixed_xy[vertex][1],
                                fixed_xy[vertex + 1][0],
                                fixed_xy[vertex + 1][1],
                                fixed_xy[vertex + 2][0],
                                fixed_xy[vertex + 2][1], area)) {
                            finite_vertices = false;
                            break;
                        }
                        if (area < 0) finite_vertices = false;
                        if (area > 0) any_positive_area = true;
                        for (const auto& corner : clip_corners) {
                            int64_t edge = 0;
                            finite_vertices =
                                edge_terms_fit_int32(
                                    fixed_xy[vertex + 1][0],
                                    fixed_xy[vertex + 1][1],
                                    fixed_xy[vertex + 2][0],
                                    fixed_xy[vertex + 2][1], corner[0],
                                    corner[1], edge) &&
                                edge_terms_fit_int32(
                                    fixed_xy[vertex + 2][0],
                                    fixed_xy[vertex + 2][1],
                                    fixed_xy[vertex][0], fixed_xy[vertex][1],
                                    corner[0], corner[1], edge) &&
                                edge_terms_fit_int32(
                                    fixed_xy[vertex][0], fixed_xy[vertex][1],
                                    fixed_xy[vertex + 1][0],
                                    fixed_xy[vertex + 1][1], corner[0],
                                    corner[1], edge);
                            if (!finite_vertices) break;
                        }
                    }
                    if (!any_positive_area) finite_vertices = false;
                    if (finite_vertices) {
                        packet.compute_fixture = Nv2aGenericCombinerPacket::
                            ComputeFixture::kTextureModulateTriangle;
                        packet.fixture_clip_x = effective_clip_x;
                        packet.fixture_clip_y = effective_clip_y;
                        packet.fixture_clip_width = effective_clip_width;
                        packet.fixture_clip_height = effective_clip_height;
                        packet.fixture_blend_enabled = blend_enabled;
                        packet.fixture_blend_src = blend_src;
                        packet.fixture_blend_dst = blend_dst;
                        packet.fixture_color_format_x8 = color_format_x8;
                        packet.fixture_texture_constant_color_alpha =
                            constant_color_alpha_combiner_words_match;
                        if (constant_color_alpha_combiner_words_match) {
                            packet.fixture_constant_argb =
                                raster_state[kRasterCombinerFactor0_0];
                        }
                    }
                }
                const bool texture_resident_state =
                    packet.compute_fixture ==
                        Nv2aGenericCombinerPacket::ComputeFixture::kNone &&
                    reject_reason == TextureReject::kCount &&
                    texture_modulate_exact_this_batch;
                if (texture_resident_state) {
                    auto triangle_crosses_w_plane = [&](size_t ia, size_t ib,
                                                        size_t ic) {
                        const auto& a = packet.vertices[ia];
                        const auto& b = packet.vertices[ib];
                        const auto& c = packet.vertices[ic];
                        const bool any_behind =
                            a.position[3] < 0.0f || b.position[3] < 0.0f ||
                            c.position[3] < 0.0f;
                        const bool all_behind =
                            a.position[3] < 0.0f && b.position[3] < 0.0f &&
                            c.position[3] < 0.0f;
                        return any_behind && !all_behind;
                    };
                    bool mixed_w_triangle = false;
                    if (primitive_mode == 5) {
                        for (size_t i = 0; !mixed_w_triangle &&
                             i + 2 < packet.vertices.size(); i += 3) {
                            mixed_w_triangle =
                                triangle_crosses_w_plane(i, i + 1, i + 2);
                        }
                    } else if (primitive_mode == 6) {
                        for (size_t i = 2; !mixed_w_triangle &&
                             i < packet.vertices.size(); ++i) {
                            mixed_w_triangle = ((i & 1u) == 0)
                                ? triangle_crosses_w_plane(i - 2, i - 1, i)
                                : triangle_crosses_w_plane(i - 1, i - 2, i);
                        }
                    } else {
                        for (size_t i = 2; !mixed_w_triangle &&
                             i < packet.vertices.size(); ++i) {
                            mixed_w_triangle =
                                triangle_crosses_w_plane(0, i - 1, i);
                        }
                    }
                    auto finite_vertex = [](const Nv2aGenericCombinerVertex& vertex) {
                        for (float value : vertex.position)
                            if (!std::isfinite(value)) return false;
                        if (vertex.position[3] == 0.0f) return false;
                        for (float value : vertex.diffuse)
                            if (!std::isfinite(value)) return false;
                        for (float value : vertex.texture[0])
                            if (!std::isfinite(value)) return false;
                        return true;
                    };
                    std::vector<Nv2aGenericCombinerVertex> expanded;
                    auto append_oriented_triangle =
                        [&](const Nv2aGenericCombinerVertex& a,
                            const Nv2aGenericCombinerVertex& b,
                            const Nv2aGenericCombinerVertex& c) {
                        if (!finite_vertex(a) || !finite_vertex(b) ||
                            !finite_vertex(c)) {
                            return false;
                        }
                        const bool all_behind_w =
                            a.position[3] < 0.0f && b.position[3] < 0.0f &&
                            c.position[3] < 0.0f;
                        if (all_behind_w) return true;
                        const bool crosses_w_plane =
                            a.position[3] < 0.0f || b.position[3] < 0.0f ||
                            c.position[3] < 0.0f;
                        if (crosses_w_plane) return false;
                        const double ax = static_cast<double>(a.position[0]);
                        const double ay = static_cast<double>(a.position[1]);
                        const double bx = static_cast<double>(b.position[0]);
                        const double by = static_cast<double>(b.position[1]);
                        const double cx = static_cast<double>(c.position[0]);
                        const double cy = static_cast<double>(c.position[1]);
                        const double area =
                            (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
                        if (area == 0) return true;
                        if (cull_enabled && cull_front && area > 0) return true;
                        if (cull_enabled && !cull_front && area < 0) return true;
                        if (!cull_enabled && area < 0) {
                            expanded.push_back(a);
                            expanded.push_back(c);
                            expanded.push_back(b);
                        } else if (cull_enabled && cull_front) {
                            expanded.push_back(a);
                            expanded.push_back(c);
                            expanded.push_back(b);
                        } else {
                            expanded.push_back(a);
                            expanded.push_back(b);
                            expanded.push_back(c);
                        }
                        return true;
                    };
                    auto append_triangle = [&](size_t ia, size_t ib, size_t ic) {
                        return append_oriented_triangle(
                            packet.vertices[ia], packet.vertices[ib],
                            packet.vertices[ic]);
                    };
                    auto append_clipped_triangle =
                        [&](size_t ia, size_t ib, size_t ic) {
                            const Nv2aGenericCombinerVertex& a = packet.vertices[ia];
                            const Nv2aGenericCombinerVertex& b = packet.vertices[ib];
                            const Nv2aGenericCombinerVertex& c = packet.vertices[ic];
                            if (!finite_vertex(a) || !finite_vertex(b) ||
                                !finite_vertex(c)) {
                                return false;
                            }
                            constexpr float kMinPositiveW = 1.0e-6f;
                            const float surface_w =
                                static_cast<float>((surface_pitch & 0xffffu) / 4u);
                            const float surface_h = static_cast<float>(surface_height);
                            struct ClipVertex {
                                Nv2aGenericCombinerVertex vertex;
                                float clip_x = 0.0f;
                                float clip_y = 0.0f;
                                float clip_w = 0.0f;
                            };
                            auto make_clip_vertex =
                                [&](const Nv2aGenericCombinerVertex& vertex) {
                                    ClipVertex out{};
                                    out.vertex = vertex;
                                    out.clip_w = vertex.position[3];
                                    out.clip_x =
                                        (2.0f * vertex.position[0] / surface_w -
                                         1.0f) *
                                        out.clip_w;
                                    out.clip_y =
                                        (2.0f * vertex.position[1] / surface_h -
                                         1.0f) *
                                        out.clip_w;
                                    return out;
                                };
                            auto lerp_clip_vertex = [](const ClipVertex& v0,
                                                       const ClipVertex& v1,
                                                       float t) {
                                ClipVertex out{};
                                out.clip_x = v0.clip_x + (v1.clip_x - v0.clip_x) * t;
                                out.clip_y = v0.clip_y + (v1.clip_y - v0.clip_y) * t;
                                out.clip_w = v0.clip_w + (v1.clip_w - v0.clip_w) * t;
                                out.vertex.position[2] =
                                    v0.vertex.position[2] +
                                    (v1.vertex.position[2] -
                                     v0.vertex.position[2]) *
                                        t;
                                out.vertex.position[3] = out.clip_w;
                                for (size_t component = 0; component < 4; ++component) {
                                    out.vertex.diffuse[component] =
                                        v0.vertex.diffuse[component] +
                                        (v1.vertex.diffuse[component] -
                                         v0.vertex.diffuse[component]) *
                                            t;
                                    out.vertex.specular[component] =
                                        v0.vertex.specular[component] +
                                        (v1.vertex.specular[component] -
                                         v0.vertex.specular[component]) *
                                            t;
                                    for (size_t stage = 0; stage < 4; ++stage) {
                                        out.vertex.texture[stage][component] =
                                            v0.vertex.texture[stage][component] +
                                            (v1.vertex.texture[stage][component] -
                                             v0.vertex.texture[stage][component]) *
                                                t;
                                    }
                                }
                                return out;
                            };
                            std::vector<ClipVertex> clip_polygon{
                                make_clip_vertex(a), make_clip_vertex(b),
                                make_clip_vertex(c)};
                            auto clip_plane = [&](auto plane_value) {
                                if (clip_polygon.empty()) return;
                                std::vector<ClipVertex> output;
                                output.reserve(clip_polygon.size() + 1);
                                ClipVertex prev = clip_polygon.back();
                                float prev_value = plane_value(prev);
                                bool prev_inside = prev_value >= 0.0f;
                                for (const ClipVertex& cur : clip_polygon) {
                                    const float cur_value = plane_value(cur);
                                    const bool cur_inside = cur_value >= 0.0f;
                                    if (cur_inside != prev_inside) {
                                        const float denom = prev_value - cur_value;
                                        const float t = denom == 0.0f ? 0.0f
                                                                      : prev_value / denom;
                                        output.push_back(lerp_clip_vertex(prev, cur, t));
                                    }
                                    if (cur_inside) output.push_back(cur);
                                    prev = cur;
                                    prev_value = cur_value;
                                    prev_inside = cur_inside;
                                }
                                clip_polygon = std::move(output);
                            };
                            constexpr float kClipTolerance = 1.0e-4f;
                            const bool needs_clip = std::any_of(
                                clip_polygon.begin(), clip_polygon.end(),
                                [](const ClipVertex& vertex) {
                                    return vertex.clip_w < kMinPositiveW ||
                                           vertex.clip_x <
                                               -vertex.clip_w - kClipTolerance ||
                                           vertex.clip_x >
                                               vertex.clip_w + kClipTolerance ||
                                           vertex.clip_y <
                                               -vertex.clip_w - kClipTolerance ||
                                           vertex.clip_y >
                                               vertex.clip_w + kClipTolerance;
                                });
                            if (!needs_clip) return append_oriented_triangle(a, b, c);
                            clip_plane([](const ClipVertex& vertex) {
                                return vertex.clip_w - kMinPositiveW;
                            });
                            clip_plane([](const ClipVertex& vertex) {
                                return vertex.clip_x + vertex.clip_w;
                            });
                            clip_plane([](const ClipVertex& vertex) {
                                return vertex.clip_w - vertex.clip_x;
                            });
                            clip_plane([](const ClipVertex& vertex) {
                                return vertex.clip_y + vertex.clip_w;
                            });
                            clip_plane([](const ClipVertex& vertex) {
                                return vertex.clip_w - vertex.clip_y;
                            });
                            if (clip_polygon.size() < 3) return true;
                            std::vector<Nv2aGenericCombinerVertex> clipped;
                            clipped.reserve(clip_polygon.size());
                            for (ClipVertex& vertex : clip_polygon) {
                                if (vertex.clip_w <= 0.0f) return false;
                                vertex.vertex.position[0] =
                                    (vertex.clip_x / vertex.clip_w + 1.0f) *
                                    surface_w * 0.5f;
                                vertex.vertex.position[1] =
                                    (vertex.clip_y / vertex.clip_w + 1.0f) *
                                    surface_h * 0.5f;
                                vertex.vertex.position[3] = vertex.clip_w;
                                clipped.push_back(vertex.vertex);
                            }
                            for (size_t i = 1; i + 1 < clipped.size(); ++i) {
                                if (!append_oriented_triangle(
                                        clipped[0], clipped[i], clipped[i + 1])) {
                                    return false;
                                }
                            }
                            return true;
                        };
                    const bool binned_texture_compute =
                        mixed_w_triangle && !uses_depth &&
                        !passthrough_combiner_words_match;
                    bool expanded_ok = binned_texture_compute || !mixed_w_triangle;
                    if (expanded_ok) {
                        expanded.reserve((packet.vertices.size() - 2) * 3);
                    }
                    if (expanded_ok && primitive_mode == 5) {
                        for (size_t i = 0; expanded_ok && i + 2 < packet.vertices.size();
                             i += 3)
                            expanded_ok = binned_texture_compute
                                ? append_clipped_triangle(i, i + 1, i + 2)
                                : append_triangle(i, i + 1, i + 2);
                    } else if (primitive_mode == 6) {
                        for (size_t i = 2; expanded_ok && i < packet.vertices.size(); ++i) {
                            if ((i & 1u) == 0)
                                expanded_ok = binned_texture_compute
                                    ? append_clipped_triangle(i - 2, i - 1, i)
                                    : append_triangle(i - 2, i - 1, i);
                            else
                                expanded_ok = binned_texture_compute
                                    ? append_clipped_triangle(i - 1, i - 2, i)
                                    : append_triangle(i - 1, i - 2, i);
                        }
                    } else {
                        for (size_t i = 2; expanded_ok && i < packet.vertices.size(); ++i)
                            expanded_ok = binned_texture_compute
                                ? append_clipped_triangle(0, i - 1, i)
                                : append_triangle(0, i - 1, i);
                    }
                    if (expanded_ok && !expanded.empty()) {
                        packet.vertices = std::move(expanded);
                        packet.primitive_mode = 5;
                        packet.compute_fixture = Nv2aGenericCombinerPacket::
                            ComputeFixture::kResidentTextureModulateTriangles;
                        if (binned_texture_compute) {
                            packet.compute_fixture = Nv2aGenericCombinerPacket::
                                ComputeFixture::kTextureModulateTriangle;
                        }
                        packet.fixture_clip_x = effective_clip_x;
                        packet.fixture_clip_y = effective_clip_y;
                        packet.fixture_clip_width = effective_clip_width;
                        packet.fixture_clip_height = effective_clip_height;
                        packet.fixture_blend_enabled = blend_enabled;
                        packet.fixture_blend_src = blend_src;
                        packet.fixture_blend_dst = blend_dst;
                        packet.fixture_depth_test_enabled = depth_test_enabled_bit;
                        packet.fixture_depth_write_enabled =
                            depth_write_enabled && uses_depth;
                        packet.fixture_depth_func =
                            depth_test_enabled_bit ? depth_func : 0x0207u;
                        packet.fixture_color_format_x8 = color_format_x8;
                        packet.fixture_texture_passthrough =
                            passthrough_combiner_words_match;
                        packet.fixture_texture_constant_color_alpha =
                            constant_color_alpha_combiner_words_match;
                        packet.fixture_use_tile_bins = binned_texture_compute;
                        if (constant_color_alpha_combiner_words_match) {
                            packet.fixture_constant_argb =
                                raster_state[kRasterCombinerFactor0_0];
                        }
                        if (!binned_texture_compute) {
                            ++generic_combiner_resident_candidates_;
                            resident_observed_this_batch = true;
                            resident_exact_this_batch = true;
                            resident_reject_this_batch =
                                GenericResidentRejectReason::kCount;
                        }
                    } else if (expanded_ok && expanded.empty()) {
                        resident_noop_batch = true;
                        ++generic_combiner_resident_candidates_;
                        resident_observed_this_batch = true;
                        resident_exact_this_batch = true;
                        resident_reject_this_batch =
                            GenericResidentRejectReason::kCount;
                    } else {
                        record_generic_resident_reject(
                            GenericResidentRejectReason::kVertexExpansion);
                        resident_observed_this_batch = true;
                        resident_exact_this_batch = false;
                        resident_reject_this_batch =
                            GenericResidentRejectReason::kVertexExpansion;
                    }
                }
            }
        }
#if XLLE_VSH_DRAW_OVERLAP_CENSUS
        // Packet copying, state recognition, triangle expansion/clipping and
        // CPU bin construction only. Backend admission/submission, debug
        // snapshots, surface resolves and texture preparation are outside.
        if (fixture_overlap) {
            const uint64_t raw = fixture_overlap_t0
                ? perf_excl::tick_now() - fixture_overlap_t0 : 0;
            auto& overlap = job.vsh_overlap;
            overlap.fixture = static_cast<uint32_t>(packet.compute_fixture);
            ++overlap.fixture_setup_calls;
            overlap.fixture_input_vertices += transformed_batch.size();
            overlap.fixture_output_vertices += packet.vertices.size();
            if (fixture_overlap_t0) {
                overlap.fixture_setup_raw_ticks += raw;
                overlap.fixture_setup_compensated_ticks +=
                    perf_excl::compensated_delta(raw, overlap.discarded_deltas);
            }
        }
#endif
        const bool resident_packet =
            packet.compute_fixture == Nv2aGenericCombinerPacket::ComputeFixture::
                                          kResidentPrimaryColorTriangles ||
            packet.compute_fixture == Nv2aGenericCombinerPacket::ComputeFixture::
                                          kResidentConstantColorTriangles ||
            packet.compute_fixture == Nv2aGenericCombinerPacket::ComputeFixture::
                                          kResidentTextureModulateTriangles ||
            packet.compute_fixture == Nv2aGenericCombinerPacket::ComputeFixture::
                                          kDashboardProject2dFan;
        uint64_t debug_batch_fingerprint = 1469598103934665603ull;
        for (uint32_t index = kRasterShaderStageProgram;
             index <= kRasterCombinerFinalFactor1; ++index) {
            debug_batch_fingerprint ^= raster_state[index];
            debug_batch_fingerprint *= 1099511628211ull;
        }
        const uint64_t debug_batch_full_hash =
            fnv1a_bytes(raster_state.data(), sizeof(raster_state));
        auto fill_surface_write_vertex_debug =
            [](SurfaceWriteDebugInfo& info,
               const std::vector<Nv2aGenericCombinerVertex>& vertices) {
                info.debug_vertex_count = static_cast<uint32_t>(
                    std::min<size_t>(vertices.size(), info.debug_position.size()));
                for (uint32_t index = 0; index < info.debug_vertex_count; ++index) {
                    info.debug_position[index] = vertices[index].position;
                    info.debug_diffuse[index] = vertices[index].diffuse;
                    info.debug_tex0[index] = vertices[index].texture[0];
                }
            };
        if (generic_packet_snapshot_filter_cb_ &&
            generic_packet_snapshot_cb_) {
            GenericPacketDebugInfo info;
            info.candidate = generic_combiner_attempted_batches_;
            info.fingerprint = debug_batch_fingerprint;
            info.full_state_hash = debug_batch_full_hash;
            info.color_address = color_address;
            info.depth_address = depth_address;
            info.pitch = pitch;
            info.width = pitch / 4u;
            info.height = surface_height;
            info.surface_format = surface_format;
            info.shader_stage_program = raster_state[kRasterShaderStageProgram];
            info.combiner_control = raster_state[kRasterCombinerControl];
            info.texture_enable_mask = raster_state[kRasterTextureEnableMask];
            info.primitive_mode = primitive_mode;
            info.vertex_count = static_cast<uint32_t>(std::min<size_t>(
                transformed_batch.size(), std::numeric_limits<uint32_t>::max()));
            if (generic_packet_snapshot_filter_cb_(info)) {
                auto snapshot = std::make_unique<GenericPacketSnapshot>();
                snapshot->packet = packet;
                if (!resolve_renderer_color_overlap(
                        color_address, static_cast<size_t>(color_bytes64),
                        "debug generic packet color snapshot")) {
                    return false;
                }
                snapshot->packet.color_input.resize(static_cast<size_t>(color_bytes64));
                if (!mem_read_ ||
                    !mem_read_(color_address, snapshot->packet.color_input.data(),
                               snapshot->packet.color_input.size())) {
                    fault_("NV2A debug generic packet color DMA read failed");
                    return false;
                }
                if (uses_depth) {
                    if (!resolve_renderer_depth_overlap(
                            depth_address, static_cast<size_t>(depth_bytes64),
                            "debug generic packet zeta snapshot")) {
                        return false;
                    }
                    snapshot->packet.depth_input.resize(static_cast<size_t>(depth_bytes64));
                    if (!mem_read_ ||
                        !mem_read_(depth_address, snapshot->packet.depth_input.data(),
                                   snapshot->packet.depth_input.size())) {
                        fault_("NV2A debug generic packet zeta DMA read failed");
                        return false;
                    }
                }
                for (uint32_t stage = 0; stage < snapshot->packet.textures.size(); ++stage) {
                    if (snapshot->packet.textures[stage].referenced)
                        snapshot->packet.textures[stage].input =
                            texture_stages[stage].data;
                }
                debug_packet_snapshot = std::move(snapshot);
                debug_packet_snapshot_info = info;
            }
        }
        if (resident_noop_batch) {
            executed = true;
            // Stage (d) / F3: this batch reached the GL backend. GL calls must
            // stay on the thread owning the context and the resident protocol
            // writes guest memory through mem_write_, so the job is inline-only.
            seam_note_threadable(job, false, kSeamRendererPath);
#if XLLE_VSH_DRAW_OVERLAP_CENSUS
            job.vsh_overlap.outcome = VshDrawOverlapOutcome::kGenericEmpty;
#endif
            ++generic_combiner_accelerated_batches_;
            if (generic_shader_row_) ++generic_shader_row_->accelerated_batches;
            if (surface_write_debug_cb_) {
                SurfaceWriteDebugInfo info;
                info.kind = "resident-generic-noop";
                info.sequence = generic_combiner_batches_;
                info.fingerprint = debug_batch_fingerprint;
                info.full_state_hash = debug_batch_full_hash;
                info.color_address = color_address;
                info.pitch = pitch;
                info.width = pitch / 4u;
                info.height = surface_height;
                info.surface_format = surface_format;
                info.shader_stage_program =
                    raster_state[kRasterShaderStageProgram];
                info.combiner_control = raster_state[kRasterCombinerControl];
                info.alpha_func = raster_state[kRasterAlphaFunc];
                info.alpha_ref = raster_state[kRasterAlphaRef] & 0xFFu;
                info.blend_src = raster_state[kRasterBlendSrc];
                info.blend_dst = raster_state[kRasterBlendDst];
                info.blend_equation = raster_state[kRasterBlendEquation];
                info.depth_func = raster_state[kRasterDepthFunc];
                info.depth_mask = raster_state[kRasterDepthMask];
                info.texture_enable_mask = raster_state[kRasterTextureEnableMask];
                info.primitive_mode = primitive_mode;
                info.vertex_count = static_cast<uint32_t>(std::min<size_t>(
                    transformed_batch.size(),
                    std::numeric_limits<uint32_t>::max()));
                fill_surface_write_vertex_debug(info, packet.vertices);
                surface_write_debug_cb_(info);
            }
            ++generic_combiner_executed_batches_;
            return true;
        }
        bool resident_supported = false;
        {
            perf::ScopedAdd timer(perf::g_ns_generic_resident_support,
                                  perf::g_calls_generic_resident_support);
            resident_supported = renderer_->supports_resident_generic_combiner(packet);
        }
        if (resident_supported) {
            ++generic_combiner_resident_supported_;
            {
                perf::ScopedAdd timer(perf::g_ns_generic_resident_prepare,
                                      perf::g_calls_generic_resident_prepare);
                const size_t color_size = static_cast<size_t>(color_bytes64);
                const bool already_resident = renderer_->has_resident_color_surface(
                    color_address, color_size, surface_pitch, surface_height,
                    surface_format);
                if (!already_resident) {
                    ++generic_combiner_resident_target_switch_resolves_;
                    if (renderer_->requires_resident_target_switch_resolve()) {
                        if (!resolve_renderer_color_overlap(
                                0, 64u * 1024u * 1024u,
                                "resident generic target switch")) {
                            return false;
                        }
                    }
                    if (!resolve_renderer_color_overlap(
                            color_address, color_size,
                            "resident generic color upload")) {
                        return false;
                    }
                    packet.color_input.resize(color_size);
                    if (!mem_read_ ||
                        !mem_read_(color_address, packet.color_input.data(),
                                   packet.color_input.size())) {
                        fault_("NV2A resident generic color DMA read failed");
                        return false;
                    }
                    ++generic_combiner_resident_uploads_;
                } else {
                    ++generic_combiner_resident_reuses_;
                }
                if ((packet.fixture_depth_test_enabled ||
                     packet.fixture_depth_write_enabled) &&
                    uses_depth) {
                    const size_t depth_size = static_cast<size_t>(depth_bytes64);
                    const bool depth_already_resident =
                        renderer_->has_resident_depth_surface(
                            depth_address, depth_size, surface_pitch,
                            surface_height, surface_format);
                    if (!depth_already_resident) {
                        if (!resolve_renderer_depth_overlap(
                                depth_address, depth_size,
                                "resident generic zeta upload")) {
                            return false;
                        }
                        packet.depth_input.resize(depth_size);
                        if (!mem_read_ ||
                            !mem_read_(depth_address, packet.depth_input.data(),
                                       packet.depth_input.size())) {
                            fault_("NV2A resident generic zeta DMA read failed");
                            return false;
                        }
                    }
                }
                for (uint32_t stage = 0; stage < packet.textures.size(); ++stage) {
                    if (packet.textures[stage].referenced)
                        packet.textures[stage].input =
                            std::move(texture_stages[stage].data);
                }
            }
            Nv2aGenericCombinerResult result;
            std::string renderer_error;
            {
                perf::ScopedAdd timer(perf::g_ns_generic_resident_submit,
                                      perf::g_calls_generic_resident_submit);
                if (!renderer_->submit_resident_generic_combiner(packet, result,
                                                                 renderer_error)) {
                    fault_(renderer_error.empty()
                               ? "NV2A resident generic-combiner submission failed"
                               : renderer_error);
                    return false;
                }
            }
            ++generic_combiner_resident_submits_;
            executed = true;
            // Stage (d) / F3: this batch reached the GL backend. GL calls must
            // stay on the thread owning the context and the resident protocol
            // writes guest memory through mem_write_, so the job is inline-only.
            seam_note_threadable(job, false, kSeamRendererPath);
#if XLLE_VSH_DRAW_OVERLAP_CENSUS
            job.vsh_overlap.outcome = VshDrawOverlapOutcome::kGenericResidentGpu;
#endif
            ++generic_combiner_accelerated_batches_;
            if (generic_shader_row_) ++generic_shader_row_->accelerated_batches;
            generic_combiner_triangles_ += result.triangles;
            generic_combiner_fragments_ += result.fragments;
            generic_combiner_writes_ += result.writes;
            if (surface_write_debug_cb_) {
                SurfaceWriteDebugInfo info;
                info.kind = "resident-generic";
                info.sequence = generic_combiner_batches_;
                info.fragments = result.fragments;
                info.writes = result.writes;
                info.fingerprint = debug_batch_fingerprint;
                info.full_state_hash = debug_batch_full_hash;
                info.color_address = color_address;
                info.pitch = pitch;
                info.width = pitch / 4u;
                info.height = surface_height;
                info.surface_format = surface_format;
                info.shader_stage_program =
                    raster_state[kRasterShaderStageProgram];
                info.combiner_control = raster_state[kRasterCombinerControl];
                info.alpha_func = raster_state[kRasterAlphaFunc];
                info.alpha_ref = raster_state[kRasterAlphaRef] & 0xFFu;
                info.blend_src = raster_state[kRasterBlendSrc];
                info.blend_dst = raster_state[kRasterBlendDst];
                info.blend_equation = raster_state[kRasterBlendEquation];
                info.depth_func = raster_state[kRasterDepthFunc];
                info.depth_mask = raster_state[kRasterDepthMask];
                info.texture_enable_mask = raster_state[kRasterTextureEnableMask];
                info.primitive_mode = primitive_mode;
                info.vertex_count = static_cast<uint32_t>(std::min<size_t>(
                    transformed_batch.size(),
                    std::numeric_limits<uint32_t>::max()));
                fill_surface_write_vertex_debug(info, packet.vertices);
                surface_write_debug_cb_(info);
            }
            ++generic_combiner_executed_batches_;
            return true;
        } else if (resident_packet) {
            record_generic_resident_reject(
                GenericResidentRejectReason::kBackendUnsupported);
            resident_observed_this_batch = true;
            resident_exact_this_batch = false;
            resident_reject_this_batch =
                GenericResidentRejectReason::kBackendUnsupported;
#if XLLE_VSH_GPU_RESIDENT
            if (deferred_gpu_vsh) {
                if (!copy_packet_vertices()) return false;
                packet.gpu_vsh.reset();
                packet.compute_fixture =
                    Nv2aGenericCombinerPacket::ComputeFixture::kNone;
                packet.primitive_mode = primitive_mode;
                force_software_after_deferred_backend_decline = true;
            }
#endif
        }
        bool nonresident_supported = false;
        if (!force_software_after_deferred_backend_decline) {
            perf::ScopedAdd timer(perf::g_ns_generic_nonresident_support,
                                  perf::g_calls_generic_nonresident_support);
            nonresident_supported = renderer_->supports_generic_combiner(packet);
        }
        if (nonresident_supported) {
            {
                perf::ScopedAdd timer(perf::g_ns_generic_nonresident_snapshot,
                                      perf::g_calls_generic_nonresident_snapshot);
                if (!resolve_renderer_color_overlap(
                        color_address, static_cast<size_t>(color_bytes64),
                        "accelerated generic color snapshot")) {
                    return false;
                }
                packet.color_input.resize(static_cast<size_t>(color_bytes64));
                if (!mem_read_ ||
                    !mem_read_(color_address, packet.color_input.data(),
                               packet.color_input.size())) {
                    fault_("NV2A accelerated color DMA read failed");
                    return false;
                }
                if (uses_depth) {
                    if (!resolve_renderer_depth_overlap(
                            depth_address, static_cast<size_t>(depth_bytes64),
                            "accelerated generic zeta snapshot")) {
                        return false;
                    }
                    packet.depth_input.resize(static_cast<size_t>(depth_bytes64));
                    if (!mem_read_(depth_address, packet.depth_input.data(),
                                   packet.depth_input.size())) {
                        fault_("NV2A accelerated zeta DMA read failed");
                        return false;
                    }
                }
                for (uint32_t stage = 0; stage < packet.textures.size(); ++stage) {
                    if (packet.textures[stage].referenced)
                        packet.textures[stage].input =
                            std::move(texture_stages[stage].data);
                }
            }

            Nv2aGenericCombinerResult result;
            std::string renderer_error;
            {
                perf::ScopedAdd timer(perf::g_ns_generic_nonresident_submit,
                                      perf::g_calls_generic_nonresident_submit);
                if (!renderer_->submit_generic_combiner(packet, result,
                                                        renderer_error)) {
                    fault_(renderer_error.empty()
                               ? "NV2A accelerated generic-combiner submission failed"
                               : renderer_error);
                    return false;
                }
            }
            if (result.color_output.size() != packet.color_input.size() ||
                result.depth_output.size() != packet.depth_input.size()) {
                fault_("NV2A accelerated generic-combiner returned invalid surface sizes");
                return false;
            }
            {
                perf::ScopedAdd timer(perf::g_ns_generic_nonresident_writeback,
                                      perf::g_calls_generic_nonresident_writeback);
                if (!mem_write_ ||
                    !mem_write_(color_address, result.color_output.data(),
                                result.color_output.size()) ||
                    (uses_depth &&
                     !mem_write_(depth_address, result.depth_output.data(),
                                 result.depth_output.size()))) {
                    fault_("NV2A accelerated generic-combiner DMA write failed");
                    return false;
                }
                invalidate_renderer_color_overlap(
                    color_address, result.color_output.size(),
                    nv2a_invalidate_token(kNv2aInvalidateGenericAccelerated));
                if (uses_depth)
                    invalidate_renderer_depth_overlap(
                        depth_address, result.depth_output.size(),
                        nv2a_invalidate_token(
                            kNv2aInvalidateGenericAccelerated));
            }
            executed = true;
            // Stage (d) / F3: this batch reached the GL backend. GL calls must
            // stay on the thread owning the context and the resident protocol
            // writes guest memory through mem_write_, so the job is inline-only.
            seam_note_threadable(job, false, kSeamRendererPath);
#if XLLE_VSH_DRAW_OVERLAP_CENSUS
            job.vsh_overlap.outcome = VshDrawOverlapOutcome::kGenericSnapshotGpu;
#endif
            ++generic_combiner_accelerated_batches_;
            if (generic_shader_row_) ++generic_shader_row_->accelerated_batches;
            generic_combiner_triangles_ += result.triangles;
            generic_combiner_fragments_ += result.fragments;
            generic_combiner_writes_ += result.writes;
            if (surface_write_debug_cb_) {
                SurfaceWriteDebugInfo info;
                info.kind = "accelerated-generic";
                info.sequence = generic_combiner_batches_;
                info.fragments = result.fragments;
                info.writes = result.writes;
                info.fingerprint = debug_batch_fingerprint;
                info.full_state_hash = debug_batch_full_hash;
                info.color_address = color_address;
                info.pitch = pitch;
                info.width = pitch / 4u;
                info.height = surface_height;
                info.surface_format = surface_format;
                info.shader_stage_program =
                    raster_state[kRasterShaderStageProgram];
                info.combiner_control = raster_state[kRasterCombinerControl];
                info.alpha_func = raster_state[kRasterAlphaFunc];
                info.alpha_ref = raster_state[kRasterAlphaRef] & 0xFFu;
                info.blend_src = raster_state[kRasterBlendSrc];
                info.blend_dst = raster_state[kRasterBlendDst];
                info.blend_equation = raster_state[kRasterBlendEquation];
                info.depth_func = raster_state[kRasterDepthFunc];
                info.depth_mask = raster_state[kRasterDepthMask];
                info.texture_enable_mask = raster_state[kRasterTextureEnableMask];
                info.primitive_mode = primitive_mode;
                info.vertex_count = static_cast<uint32_t>(std::min<size_t>(
                    transformed_batch.size(),
                    std::numeric_limits<uint32_t>::max()));
                fill_surface_write_vertex_debug(info, packet.vertices);
                surface_write_debug_cb_(info);
            }
            ++generic_combiner_executed_batches_;
            return true;
        }
    }
#if XLLE_VSH_GPU_RESIDENT
    if (job.gpu_vsh && job.gpu_vsh->mode == 3 &&
        !materialize_deferred_gpu_vsh(job)) {
        return false;
    }
#endif
    if (generic_accel_timer) generic_accel_timer.reset();
    ++generic_combiner_fallback_batches_;
    // Phase 6 census: this batch is the software fallback. The resident-path
    // reject reason is recorded on the row so the census says WHY each shader
    // state is still on the CPU, not merely that it is.
    if (generic_shader_row_) {
        ++generic_shader_row_->software_batches;
        generic_shader_row_->reject_reason =
            static_cast<uint32_t>(resident_reject_this_batch);
    }
    executed = true;
#if XLLE_VSH_DRAW_OVERLAP_CENSUS
    if (job.vsh_overlap.outcome == VshDrawOverlapOutcome::kUnknown)
        job.vsh_overlap.outcome = VshDrawOverlapOutcome::kGenericCpu;
#endif
    // Stage (d) / section 1.3: the generic software fallback builds its zeta
    // surface WITHOUT mem_direct_ (see the PagedRasterSurface pair below), so
    // the depth destination has no direct pointer and the job is inline-only.
    seam_note_threadable(job, false, kSeamNoDirectPointer);
    ++generic_combiner_batches_;
    const uint64_t generic_batch_index = generic_combiner_batches_;
    std::optional<perf::ScopedAdd> generic_fallback_timer;
    if (perf::g_enabled) {
        generic_fallback_timer.emplace(perf::g_ns_generic_fallback_setup,
                                       perf::g_calls_generic_fallback_setup);
    }
    GenericCombinerFingerprintStats* generic_fingerprint_stats = nullptr;
    uint64_t generic_batch_fingerprint = 1469598103934665603ull;
    uint64_t generic_batch_full_hash = 0;
    if (collect_diagnostics) {
        generic_batch_full_hash =
            fnv1a_bytes(raster_state.data(), sizeof(raster_state));
        for (uint32_t index = kRasterShaderStageProgram;
             index <= kRasterCombinerFinalFactor1; ++index) {
            generic_batch_fingerprint ^= raster_state[index];
            generic_batch_fingerprint *= 1099511628211ull;
        }
        auto it = std::find_if(
            generic_combiner_fingerprints_.begin(),
            generic_combiner_fingerprints_.end(),
            [&](const auto& entry) {
                return entry.fingerprint == generic_batch_fingerprint;
            });
        if (it == generic_combiner_fingerprints_.end()) {
            GenericCombinerFingerprintStats entry;
            entry.fingerprint = generic_batch_fingerprint;
            entry.batches = 1;
            generic_combiner_fingerprints_.push_back(std::move(entry));
            generic_fingerprint_stats = &generic_combiner_fingerprints_.back();
        } else {
            ++it->batches;
            generic_fingerprint_stats = &*it;
        }
        if (profile_generic_fingerprint) {
            if (resident_observed_this_batch) {
                ++generic_fingerprint_stats->resident_batches;
                if (resident_exact_this_batch) {
                    ++generic_fingerprint_stats->resident_exact_batches;
                } else {
                    const size_t reject_index =
                        static_cast<size_t>(resident_reject_this_batch);
                    if (reject_index <
                        generic_fingerprint_stats->resident_reject_batches.size()) {
                        ++generic_fingerprint_stats
                              ->resident_reject_batches[reject_index];
                    }
                }
            }
            if (texture_modulate_observed_this_batch) {
                ++generic_fingerprint_stats->texture_modulate_batches;
                if (texture_modulate_exact_this_batch) {
                    ++generic_fingerprint_stats->texture_modulate_exact_batches;
                } else {
                    const size_t reject_index =
                        static_cast<size_t>(texture_modulate_reject_this_batch);
                    if (reject_index <
                        generic_fingerprint_stats
                            ->texture_modulate_reject_batches.size()) {
                        ++generic_fingerprint_stats
                              ->texture_modulate_reject_batches[reject_index];
                    }
                }
            }
            const uint32_t vertex_count = static_cast<uint32_t>(std::min<size_t>(
                transformed_batch.size(), std::numeric_limits<uint32_t>::max()));
            if (generic_fingerprint_stats->raster_state_sample_index == UINT32_MAX) {
                generic_fingerprint_stats->raster_state_sample_index =
                    job.raster_state_sample_index;
                generic_fingerprint_stats->full_state_hash = generic_batch_full_hash;
                generic_fingerprint_stats->primitive_mode = primitive_mode;
                generic_fingerprint_stats->min_vertices = vertex_count;
                generic_fingerprint_stats->max_vertices = vertex_count;
                generic_fingerprint_stats->first_vertices_recorded = true;
                generic_fingerprint_stats->first_vertex_count =
                    std::min<uint32_t>(vertex_count, 4);
                for (uint32_t vertex = 0;
                     vertex < generic_fingerprint_stats->first_vertex_count;
                     ++vertex) {
                    generic_fingerprint_stats->first_vertex_position[vertex] =
                        transformed_batch[vertex].position;
                    generic_fingerprint_stats->first_vertex_diffuse[vertex] =
                        transformed_batch[vertex].diffuse;
                    generic_fingerprint_stats->first_vertex_specular[vertex] =
                        transformed_batch[vertex].specular;
                    generic_fingerprint_stats->first_vertex_texture[vertex] =
                        transformed_batch[vertex].texture;
                }
            } else {
                generic_fingerprint_stats->min_vertices = std::min(
                    generic_fingerprint_stats->min_vertices, vertex_count);
                generic_fingerprint_stats->max_vertices = std::max(
                    generic_fingerprint_stats->max_vertices, vertex_count);
            }
        }
    }
    auto record_generic_fingerprint_perf = [&]() {
        if (!profile_generic_fingerprint || !generic_fingerprint_stats) return;
        const auto elapsed = std::chrono::steady_clock::now() - generic_started_at;
        generic_fingerprint_stats->fragments +=
            generic_combiner_fragments_ - generic_fragments_before;
        generic_fingerprint_stats->writes +=
            generic_combiner_writes_ - generic_writes_before;
        generic_fingerprint_stats->color_reads +=
            generic_combiner_color_reads_ - generic_color_reads_before;
        generic_fingerprint_stats->depth_reads +=
            generic_combiner_depth_reads_ - generic_depth_reads_before;
        generic_fingerprint_stats->wall_ns += static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count());
        generic_fingerprint_stats->texture_setup_ns +=
            generic_batch_texture_setup_ns;
        generic_fingerprint_stats->software_raster_ns +=
            generic_batch_software_raster_ns;
        generic_fingerprint_stats->flush_ns += generic_batch_flush_ns;
        const uint64_t batch_fragments =
            generic_combiner_fragments_ - generic_fragments_before;
        generic_fingerprint_stats->bbox_pixels += generic_batch_bbox_pixels;
        generic_fingerprint_stats->max_batch_fragments =
            std::max(generic_fingerprint_stats->max_batch_fragments,
                     batch_fragments);
        generic_fingerprint_stats->max_batch_bbox_pixels =
            std::max(generic_fingerprint_stats->max_batch_bbox_pixels,
                     generic_batch_bbox_pixels);
    };

    uint32_t xmin = effective_clip_x;
    uint32_t xmax = effective_xmax;
    uint32_t ymin = effective_clip_y;
    uint32_t ymax = effective_ymax;
    if (!effective_clip_valid) {
#if XLLE_VSH_DRAW_OVERLAP_CENSUS
        job.vsh_overlap.outcome = VshDrawOverlapOutcome::kGenericEmpty;
#endif
        record_generic_fingerprint_perf();
        return true;
    }
    if (zero_alpha_gequal_noop) {
#if XLLE_VSH_DRAW_OVERLAP_CENSUS
        job.vsh_overlap.outcome = VshDrawOverlapOutcome::kGenericEmpty;
#endif
        if (generic_batch_debug_cb_) {
            GenericBatchDebugInfo info;
            info.batch = generic_batch_index;
            info.fingerprint = generic_batch_fingerprint;
            info.full_state_hash = generic_batch_full_hash;
            info.color_address = color_address;
            info.pitch = pitch;
            info.width = pitch / 4u;
            info.height = surface_height;
            info.surface_format = surface_format;
            info.shader_stage_program = raster_state[kRasterShaderStageProgram];
            info.combiner_control = raster_state[kRasterCombinerControl];
            info.alpha_func = raster_state[kRasterAlphaFunc];
            info.alpha_ref = alpha_ref;
            info.blend_src = raster_state[kRasterBlendSrc];
            info.blend_dst = raster_state[kRasterBlendDst];
            info.blend_equation = raster_state[kRasterBlendEquation];
            info.depth_func = raster_state[kRasterDepthFunc];
            info.depth_mask = raster_state[kRasterDepthMask];
            info.texture_enable_mask = raster_state[kRasterTextureEnableMask];
            info.primitive_mode = primitive_mode;
            info.vertex_count = static_cast<uint32_t>(std::min<size_t>(
                transformed_batch.size(), std::numeric_limits<uint32_t>::max()));
            info.texture_modulate_observed = texture_modulate_observed_this_batch;
            info.texture_modulate_exact = texture_modulate_exact_this_batch;
            info.texture_modulate_reject =
                texture_modulate_reject_this_batch == GenericTextureModulateRejectReason::kCount
                    ? UINT32_MAX
                    : static_cast<uint32_t>(texture_modulate_reject_this_batch);
            generic_batch_debug_cb_(info);
        }
        if (surface_write_debug_cb_) {
            SurfaceWriteDebugInfo info;
            info.kind = "generic-alpha-noop";
            info.sequence = generic_batch_index;
            info.fingerprint = generic_batch_fingerprint;
            info.full_state_hash = generic_batch_full_hash;
            info.color_address = color_address;
            info.pitch = pitch;
            info.width = pitch / 4u;
            info.height = surface_height;
            info.surface_format = surface_format;
            info.shader_stage_program = raster_state[kRasterShaderStageProgram];
            info.combiner_control = raster_state[kRasterCombinerControl];
            info.alpha_func = raster_state[kRasterAlphaFunc];
            info.alpha_ref = alpha_ref;
            info.blend_src = raster_state[kRasterBlendSrc];
            info.blend_dst = raster_state[kRasterBlendDst];
            info.blend_equation = raster_state[kRasterBlendEquation];
            info.depth_func = raster_state[kRasterDepthFunc];
            info.depth_mask = raster_state[kRasterDepthMask];
            info.texture_enable_mask = raster_state[kRasterTextureEnableMask];
            info.primitive_mode = primitive_mode;
            info.vertex_count = static_cast<uint32_t>(std::min<size_t>(
                transformed_batch.size(), std::numeric_limits<uint32_t>::max()));
            surface_write_debug_cb_(info);
        }
        ++generic_combiner_executed_batches_;
        record_generic_fingerprint_perf();
        return true;
    }

    float clip_min = 0.0f, clip_max = 0.0f;
    std::memcpy(&clip_min, &raster_state[kRasterClipMin], sizeof(clip_min));
    std::memcpy(&clip_max, &raster_state[kRasterClipMax], sizeof(clip_max));
    float polygon_scale = 0.0f, polygon_bias = 0.0f;
    if (enables & (1u << 13)) {
        std::memcpy(&polygon_scale, &raster_state[kRasterPolygonOffsetScale],
                   sizeof(polygon_scale));
        std::memcpy(&polygon_bias, &raster_state[kRasterPolygonOffsetBias],
                   sizeof(polygon_bias));
    }

    // ======================================================================
    // GENERATED-SHADER EXECUTOR (beads-xbox.1.15.10 phase 6, stage f3)
    // ======================================================================
    // The decision is taken HERE, before the destination resolve below,
    // because that resolve IS the cost being removed: the phase-6 census
    // measured all 704 transferring `software-generic-color-surface` resolves
    // of the canonical run at this exact call site, on ONE plane, from EIGHT
    // combiner states. Collecting the triangles cannot half-execute: the
    // collection pass runs the SAME rasterize_triangle setup the software path
    // runs and returns before the pixel loop, so a decline at any triangle
    // throws the whole list away, performs the resolve, and re-runs the
    // identical loop in software.
    Nv2aGeneratedShaderPacket gpu_shader_packet;
    bool gpu_shader_collect = false;
    bool gpu_shader_collect_failed = false;
#if XLLE_VSH_DRAW_OVERLAP_CENSUS
    bool gpu_shader_collect_nonfinite = false;
#endif
    if (!logic_enabled && !flat_shading && !zpass_enabled_ && gpu_generic_shader_ && renderer_ && effective_clip_valid) {
        ++generic_shader_attempts_;
        GenericShaderDeclineReason decline = GenericShaderDeclineReason::kNone;
        auto& st = gpu_shader_packet.state;
        st.stage_count = static_cast<uint8_t>(stage_count);
        st.mux_on_msb = mux_on_msb;
        for (uint32_t i = 0; i < 8; ++i) {
            st.color_icw[i] = raster_state[kRasterCombinerColorIcw0 + i];
            st.alpha_icw[i] = raster_state[kRasterCombinerAlphaIcw0 + i];
            st.color_ocw[i] = raster_state[kRasterCombinerColorOcw0 + i];
            st.alpha_ocw[i] = raster_state[kRasterCombinerAlphaOcw0 + i];
        }
        st.final_cw0 = final_cw0;
        st.final_cw1 = final_cw1;
        st.final_enabled = final_enabled;
        st.needs_general_combiner = needs_general_combiner;
        st.needs_diffuse = needs_diffuse;
        st.needs_specular = needs_specular;
        using FinalMode = Nv2aCombinerShaderState::FinalMode;
        if (primary_color_passthrough) {
            st.final_mode = FinalMode::kPrimaryColor;
        } else if (texture0_modulate_primary) {
            st.final_mode =
                raster_state[kRasterCombinerAlphaIcw0] == 0x14180000
                    ? FinalMode::kTexture0ModulatePrimaryAlpha
                    : FinalMode::kTexture0ModulatePrimary;
        } else if (texture0_passthrough) {
            st.final_mode = FinalMode::kTexture0Passthrough;
        } else if (texf_projected_black_alpha_one) {
            st.final_mode = FinalMode::kBlackAlphaOne;
        } else if (final_enabled) {
            st.final_mode = FinalMode::kGeneric;
        } else {
            st.final_mode = FinalMode::kPassthroughR0;
        }
        using TexMode = Nv2aCombinerShaderState::TexMode;
        using GlslAddress = Nv2aCombinerShaderState::AddressMode;
        const auto map_address = [](TextureAddressMode mode) {
            switch (mode) {
                case TextureAddressMode::kRepeat: return GlslAddress::kRepeat;
                case TextureAddressMode::kClampToEdge:
                    return GlslAddress::kClampToEdge;
                default: return GlslAddress::kBorder;
            }
        };
        for (uint32_t i = 0; i < 4; ++i) {
            auto& ts = st.textures[i];
            ts.referenced = texture_referenced[i];
            if (!ts.referenced) continue;
            const TextureStage& src = texture_stages[i];
            if (!src.valid) {
                decline = GenericShaderDeclineReason::kTextureUnsupported;
                break;
            }
            ts.alpha8 = src.alpha8;
            ts.address_u = map_address(src.address_u);
            ts.address_v = map_address(src.address_v);
            if (src.cubemap) {
                ts.mode = TexMode::kCubemap;
            } else if (src.linear) {
                ts.mode = TexMode::kLinear2d;
            } else if (src.project2d) {
                ts.mode = TexMode::kProject2d;
            } else {
                ts.mode = TexMode::kSwizzled2d;
            }
        }
        st.alpha_test = alpha_test_enabled;
        st.alpha_func = alpha_func;
        st.blend = blend_enabled;
        // PHASE 7. A depth-using batch cannot take the image-blend path. The
        // fragment program's imageStore happens INSIDE the shader, and writing
        // gl_FragDepth forces the depth test to run AFTER it, so a fragment
        // that loses the depth test would already have written its colour --
        // the software path's `continue` skips both. With the ROP owning the
        // colour write, the hardware orders it after the depth test for us.
        // The cost is that GL's fixed-function blend quantises its inputs to 8
        // bits where the software path does not (section 3j(4)); that is a
        // measured trade on 527 batches of one plane, not a silent one, and
        // --no-nv2a-gpu-generic-shader-depth is the arm it is measured
        // against. `image_blend` is part of the shader-state identity, so the
        // depth and non-depth forms of one combiner are simply two programs.
        st.image_blend = gpu_generic_shader_image_blend_ && !uses_depth;
        st.blend_src = blend_enabled ? blend_src : 0;
        st.blend_dst = blend_enabled ? blend_dst : 0;
        for (size_t c = 0; c < 4; ++c) st.color_mask[c] = color_mask_write[c];
        st.uses_depth = uses_depth;
        st.fp64_interpolate = gpu_generic_shader_fp64_;
        st.color_format_x8 = color_format_x8;
        auto& guest = gpu_shader_packet.guest;
        guest.valid = true;
        guest.combiner_control = raster_state[kRasterCombinerControl];
        guest.shader_program = raster_state[kRasterShaderStageProgram];
        guest.shader_input = (raster_state[kRasterShaderOtherStageInput] & 0x0FFFF000u) |
                             (raster_state[kRasterDotRgbMapping] & 0xFFFu);
        guest.control0 = raster_state[kRasterControl0];
        guest.zminmax = raster_state[kRasterZMinMaxControl];
        guest.shade_mode = raster_state[kRasterShadeMode];
        for (size_t i = 0; i < 4; ++i) {
            guest.texture_format[i] = raster_state[kRasterTextureFormat0 + i];
            guest.texture_control[i] = raster_state[kRasterTextureControl0_0 + i];
            guest.texture_filter[i] = raster_state[kRasterTextureFilter0 + i];
        }
        if (decline == GenericShaderDeclineReason::kNone) {
            const Nv2aCombinerShaderReject reject =
                nv2a_combiner_shader_supported(st);
            if (reject != Nv2aCombinerShaderReject::kNone) {
                decline = static_cast<GenericShaderDeclineReason>(
                    static_cast<uint32_t>(
                        GenericShaderDeclineReason::kGeneratorFirst) +
                    static_cast<uint32_t>(reject));
            }
        }
        // Device-side limits, each named so the census says which one bit.
        if (decline == GenericShaderDeclineReason::kNone) {
            if (surface_swizzled)
                decline = GenericShaderDeclineReason::kSwizzledDestination;
            else if (uses_depth &&
                     (!gpu_generic_shader_depth_ || depth_address == 0 ||
                      depth_bytes64 != color_bytes64))
                decline = GenericShaderDeclineReason::kUsesDepth;
            else if (zero_alpha_gequal_noop)
                decline = GenericShaderDeclineReason::kNoopBatch;
            else if (transformed_batch.size() < 3)
                decline = GenericShaderDeclineReason::kVertexCount;
        }
        if (decline == GenericShaderDeclineReason::kNone) {
            gpu_shader_packet.fingerprint =
                nv2a_combiner_shader_fingerprint(st);
            const size_t program_index =
                generic_shader_program_index(gpu_shader_packet.fingerprint);
            if (program_index >= 64 ||
                (gpu_generic_shader_mask_ & (uint64_t{1} << program_index)) ==
                    0) {
                decline = GenericShaderDeclineReason::kMaskedOff;
            }
        }
        if (decline == GenericShaderDeclineReason::kNone) {
            gpu_shader_packet.color_address = color_address;
            gpu_shader_packet.depth_address = depth_address;
            gpu_shader_packet.surface_size =
                static_cast<size_t>(pitch) * surface_height;
            gpu_shader_packet.surface_pitch = surface_pitch;
            gpu_shader_packet.surface_format = surface_format;
            gpu_shader_packet.surface_width = pitch / 4u;
            gpu_shader_packet.surface_height = surface_height;
            gpu_shader_packet.xmin = effective_xmin;
            gpu_shader_packet.xmax = effective_xmax;
            gpu_shader_packet.ymin = effective_ymin;
            gpu_shader_packet.ymax = effective_ymax;
            gpu_shader_packet.clip_min = clip_min;
            gpu_shader_packet.clip_max = clip_max;
            for (uint32_t i = 0; i < 8; ++i) {
                gpu_shader_packet.stage_const0[i] =
                    raster_state[kRasterCombinerFactor0_0 + i];
                gpu_shader_packet.stage_const1[i] =
                    raster_state[kRasterCombinerFactor1_0 + i];
            }
            gpu_shader_packet.final_factor0 =
                raster_state[kRasterCombinerFinalFactor0];
            gpu_shader_packet.final_factor1 =
                raster_state[kRasterCombinerFinalFactor1];
            gpu_shader_packet.alpha_ref = alpha_ref;
            gpu_shader_packet.blend_src = blend_src;
            gpu_shader_packet.blend_dst = blend_dst;
            // Phase 7: the guest's own depth semantics, carried verbatim.
            // depth_test_enabled_bit is SEMANTIC and independent of the write
            // mask -- the software path's "test disabled, write enabled" case
            // passes every fragment and still stores -- so both travel.
            gpu_shader_packet.depth_func = depth_func;
            gpu_shader_packet.depth_test = uses_depth && depth_test_enabled_bit;
            gpu_shader_packet.depth_write =
                uses_depth && raster_state[kRasterDepthMask] != 0;
            for (size_t c = 0; c < 4; ++c)
                gpu_shader_packet.color_mask[c] = color_mask_write[c];
            // The colour plane is snapshotted ONLY when the backend does not
            // already hold it, which is the whole point: on the canonical run
            // this plane is resident for 17 324 of 18 663 generic batches.
            const bool resident = renderer_->generated_shader_target_resident(
                color_address, gpu_shader_packet.surface_size, surface_pitch,
                surface_height, surface_format);
            if (!resident) {
                gpu_shader_packet.needs_color_upload = true;
                gpu_shader_packet.color_input.resize(
                    gpu_shader_packet.surface_size);
                if (!mem_read_ ||
                    !mem_read_(color_address,
                               gpu_shader_packet.color_input.data(),
                               gpu_shader_packet.color_input.size())) {
                    fault_("NV2A generated-shader colour snapshot read failed");
                    return false;
                }
            }
            // Phase 7, and the same rule for the zeta plane: it is snapshotted
            // ONLY when the backend does not already hold it. On the canonical
            // run plane 0x033d0000 is made resident by the witnessed executor's
            // 131 632 GPU draws and is therefore resident for nearly every one
            // of these batches, so this read almost never happens.
            if (uses_depth) {
                const bool depth_resident =
                    renderer_->generated_shader_depth_resident(
                        depth_address, gpu_shader_packet.surface_size);
                if (!depth_resident) {
                    gpu_shader_packet.needs_depth_upload = true;
                    gpu_shader_packet.depth_input.resize(
                        gpu_shader_packet.surface_size);
                    if (!mem_read_ ||
                        !mem_read_(depth_address,
                                   gpu_shader_packet.depth_input.data(),
                                   gpu_shader_packet.depth_input.size())) {
                        fault_("NV2A generated-shader zeta snapshot read failed");
                        return false;
                    }
                }
            }
            bool textures_ok = true;
            for (uint32_t i = 0; i < 4 && textures_ok; ++i) {
                if (!texture_referenced[i]) continue;
                const TextureStage& src = texture_stages[i];
                Nv2aGeneratedTexture& dst = gpu_shader_packet.textures[i];
                if (!src.width || !src.height) {
                    textures_ok = false;
                    break;
                }
                dst.present = true;
                dst.width = src.width;
                dst.height = src.height;
                dst.border_argb = src.border;
                const auto trace_conversion = [&](bool hit) {
                    if (gpu_generic_texture_trace_) {
                        std::printf(
                            "[nv2a-texture-conversion] batch=%llu stage=%u "
                            "width=%u height=%u linear=%u alpha8=%u "
                            "source_bytes=%llu output_bytes=%llu hash=%016llx hit=%u\n",
                            (unsigned long long)generic_shader_attempts_, i,
                            src.width, src.height, src.linear ? 1u : 0u,
                            src.alpha8 ? 1u : 0u,
                            (unsigned long long)src.data.size(),
                            (unsigned long long)dst.linear.size(),
                            (unsigned long long)(dst.hash ? dst.hash :
                                fnv1a_bytes(dst.linear.data(), dst.linear.size())),
                            hit ? 1u : 0u);
                    }
                };
                // src.data was just resolved/read by texture setup. Reuse only
                // the pure layout conversion when ALL source bytes still agree.
                // Border/wrap/projective sampling state remains in this packet.
                if (gpu_generic_texture_cache_) {
                    const auto* cached = generic_texture_conversion_cache_.find(
                        src.data, src.width, src.height, src.linear, src.alpha8);
                    if (cached) {
                        dst.linear = cached->output;
                        dst.hash = cached->hash;
                        dst.content_id = cached->content_id;
                        trace_conversion(true);
                        continue;
                    }
                }
                const size_t texels =
                    static_cast<size_t>(src.width) * src.height;
                dst.linear.resize(texels * 4u);
                uint32_t* out = reinterpret_cast<uint32_t*>(dst.linear.data());
                if (src.linear) {
                    // sample_linear reads data[y * row_pitch + x * 4] with
                    // row_pitch = width * 4 (the texture setup rewrites
                    // ts.pitch to that after the DMA read), so the tight
                    // prefix of ts.data IS the image the software samples.
                    if (src.data.size() < texels * 4u) {
                        textures_ok = false;
                        break;
                    }
                    std::memcpy(out, src.data.data(), texels * 4u);
                } else {
                    const size_t bytes_per_texel = src.alpha8 ? 1u : 4u;
                    for (uint32_t y = 0; y < src.height; ++y) {
                        for (uint32_t x = 0; x < src.width; ++x) {
                            const size_t offset =
                                static_cast<size_t>(src.swizzle->index(x, y)) *
                                bytes_per_texel;
                            if (offset + bytes_per_texel > src.data.size()) {
                                textures_ok = false;
                                break;
                            }
                            uint32_t word = 0;
                            if (src.alpha8) {
                                // sample_swizzled's A8 texel is (1,1,1,a).
                                word = 0x00FFFFFFu |
                                       (static_cast<uint32_t>(
                                            src.data[offset]) << 24);
                            } else {
                                std::memcpy(&word, src.data.data() + offset,
                                            sizeof(word));
                            }
                            out[static_cast<size_t>(y) * src.width + x] = word;
                        }
                        if (!textures_ok) break;
                    }
                }
                dst.content_id = gpu_generic_texture_cache_
                    ? Nv2aTextureConversionCache::new_content_id() : 0;
                // The backend needs a stable upload identity, not a digest of
                // every fresh multi-megabyte snapshot. Keep the FNV for legacy
                // routing, exhausted IDs, and instrumented conversion traces.
                if (!dst.content_id || gpu_generic_texture_trace_)
                    dst.hash = fnv1a_bytes(dst.linear.data(), dst.linear.size());
                trace_conversion(false);
                if (gpu_generic_texture_cache_ && textures_ok) {
                    generic_texture_conversion_cache_.store(
                        src.data, src.width, src.height, src.linear, src.alpha8,
                        dst.linear, dst.hash, dst.content_id);
                }
            }
            if (!textures_ok) {
                decline = GenericShaderDeclineReason::kTextureUnsupported;
            } else if (!renderer_->supports_generated_shader(
                           gpu_shader_packet)) {
                decline = GenericShaderDeclineReason::kBackendDeclined;
            } else {
                gpu_shader_collect = true;
            }
        }
        if (!gpu_shader_collect) {
            record_generic_shader_decline(decline);
            gpu_shader_packet = Nv2aGeneratedShaderPacket{};
        }
    }

#if XLLE_NATIVE_DRAW
    // Donor-style whole-draw submission. No CPU clipping, edge equations,
    // triangle expansion or software interpolation records are needed here.
    // Offset polygons retain the existing implementation until the adapter
    // carries their depth-bias contract. Unsupported batches still render.
    if (gpu_shader_collect && polygon_bias == 0.0f && polygon_scale == 0.0f &&
        transformed_batch.size() >= 3 && !collect_diagnostics &&
        gpu_generic_shader_verify_ == 0) {
        gpu_shader_packet.native_vertices = transformed_batch;
        gpu_shader_packet.native_primitive = primitive_mode;
        gpu_shader_packet.native_cull = cull_enabled;
        gpu_shader_packet.native_cull_front = cull_front;
        if (renderer_->prepare_native_draw(gpu_shader_packet)) {
            Nv2aGeneratedShaderResult result;
            std::string error;
            if (!renderer_->submit_generated_shader(gpu_shader_packet, result, error)) {
                fault_(("NV2A native draw failed: " + error).c_str());
                return false;
            }
            ++generic_shader_submits_;
            generic_shader_triangles_ += result.triangles;
            generic_shader_color_upload_bytes_ += result.color_upload_bytes;
            generic_shader_depth_upload_bytes_ += result.depth_upload_bytes;
            generic_shader_texture_upload_bytes_ += result.texture_upload_bytes;
            if (generic_shader_row_) {
                ++generic_shader_row_->gpu_batches;
                if (generic_shader_row_->software_batches) --generic_shader_row_->software_batches;
            }
            ++generic_combiner_executed_batches_;
            record_generic_fingerprint_perf();
            return true;
        }
        gpu_shader_packet.native_vertices.clear();
        gpu_shader_packet.native_primitive = 0;
    }
#endif

    if (!gpu_shader_collect) {
        perf::ScopedAdd timer(perf::g_ns_generic_fallback_color_resolve,
                              perf::g_calls_generic_fallback_color_resolve);
        // Phase 6 census: the 704 `software-generic-color-surface` resolves of
        // the phase-5 table are exactly the ones this call makes that MOVE
        // bytes. Crediting them to the shader row here is what turns "1 307 of
        // the 1 954 colour resolves are generic" into a work order.
        const uint64_t transfers_before = seam_resolve_transfers_;
        if (!resolve_renderer_color_overlap(color_address,
                                            static_cast<size_t>(color_bytes64),
                                            "software generic color surface")) {
            record_generic_fingerprint_perf();
            return false;
        }
        if (generic_shader_row_) {
            const uint64_t moved = seam_resolve_transfers_ - transfers_before;
            generic_shader_row_->color_resolves += moved;
            if (generic_shader_dest_slot_ <
                generic_shader_row_->dest_resolves.size()) {
                generic_shader_row_->dest_resolves[generic_shader_dest_slot_] +=
                    moved;
            }
        } else {
            generic_shader_census_.overflow_color_resolves +=
                seam_resolve_transfers_ - transfers_before;
        }
    }
    if (uses_depth && !gpu_shader_collect) {
        perf::ScopedAdd timer(perf::g_ns_generic_fallback_depth_resolve,
                              perf::g_calls_generic_fallback_depth_resolve);
        const uint64_t transfers_before = seam_resolve_transfers_;
        if (!resolve_renderer_depth_overlap(depth_address,
                                            static_cast<size_t>(depth_bytes64),
                                            "software generic zeta surface")) {
            record_generic_fingerprint_perf();
            return false;
        }
        if (generic_shader_row_) {
            generic_shader_row_->depth_resolves +=
                seam_resolve_transfers_ - transfers_before;
        }
    }
    PagedRasterSurface color_surface(
        color_address, static_cast<size_t>(color_bytes64), mem_read_, mem_write_,
        mem_direct_);
    PagedRasterSurface depth_surface(
        depth_address, static_cast<size_t>(depth_bytes64), mem_read_, mem_write_);
    // Track the written byte span so the post-flush resident refresh can
    // region-upload exactly the covered rows instead of dropping residency.
    color_surface.reset_written_span();

    auto unpack_argb8 = [](uint32_t pixel) {
        return std::array<float, 4>{{
            static_cast<float>((pixel >> 16) & 0xFFu) / 255.0f,
            static_cast<float>((pixel >> 8) & 0xFFu) / 255.0f,
            static_cast<float>(pixel & 0xFFu) / 255.0f,
            static_cast<float>((pixel >> 24) & 0xFFu) / 255.0f,
        }};
    };
    // Bilinear samplers matching the ones every witnessed textured profile above
    // already uses (this function cannot literally call those local lambdas -- they
    // are private to rasterize_witnessed_batch -- so this is a faithful independent
    // copy of the same algorithm, the same duplication pattern this file already uses
    // between rasterize_witnessed_immediate_quad and rasterize_witnessed_batch).
    // bytes_per_texel/alpha8 generalize this sampler beyond the original raw-ARGB8
    // (4 bytes/texel) case to NV097_SET_TEXTURE_FORMAT color code 0x19 (SZ_A8): a
    // swizzled, 1-byte-per-texel, alpha-only format -- per xemu's own
    // kelvin_color_format_info_map/GL sampler-swizzle table, the single stored byte
    // becomes the alpha channel while RGB reads back as white (1,1,1), matching
    // NV097_SET_TEXTURE_FORMAT_COLOR_SZ_A8's GL_R8 + {ONE,ONE,ONE,RED} swizzle
    // exactly (an independent expression of that same mapping, not copied code).
    // The swizzle-index/Morton-tiling math itself (SwizzleLookup) is already
    // element-size-agnostic -- only the per-texel byte offset stride and the raw
    // bytes -> RGBA unpacking need to branch on format.
    auto sample_swizzled = [&unpack_argb8](
                               const std::vector<uint8_t>& data,
                               const SwizzleLookup& swizzle, uint32_t width,
                               uint32_t height, float u, float v,
                               TextureAddressMode mode_u, TextureAddressMode mode_v,
                               uint32_t border_pixel, bool alpha8,
                               std::array<float, 4>& color) {
        if (!std::isfinite(u) || !std::isfinite(v)) return false;
        const size_t bytes_per_texel = alpha8 ? 1 : sizeof(uint32_t);
        auto wrap = [](float value, TextureAddressMode mode) {
            if (mode == TextureAddressMode::kRepeat) return value - std::floor(value);
            return std::clamp(value, 0.0f, 1.0f);
        };
        u = wrap(u, mode_u);
        v = wrap(v, mode_v);
        const float texel_x = u * static_cast<float>(width) - 0.5f;
        const float texel_y = v * static_cast<float>(height) - 0.5f;
        const int32_t x0 = static_cast<int32_t>(std::floor(texel_x));
        const int32_t y0 = static_cast<int32_t>(std::floor(texel_y));
        const float fx = texel_x - static_cast<float>(x0);
        const float fy = texel_y - static_cast<float>(y0);
        auto coordinate = [](int32_t value, uint32_t size, TextureAddressMode mode) {
            if (mode != TextureAddressMode::kRepeat) {
                return static_cast<uint32_t>(
                    std::clamp(value, 0, static_cast<int32_t>(size) - 1));
            }
            const int32_t signed_size = static_cast<int32_t>(size);
            int32_t wrapped = value % signed_size;
            if (wrapped < 0) wrapped += signed_size;
            return static_cast<uint32_t>(wrapped);
        };
        const auto border = unpack_argb8(border_pixel);
        auto texel = [&](int32_t x, int32_t y) {
            const bool out_of_range =
                x < 0 || y < 0 || x >= static_cast<int32_t>(width) ||
                y >= static_cast<int32_t>(height);
            if (out_of_range &&
                (mode_u == TextureAddressMode::kBorder ||
                 mode_v == TextureAddressMode::kBorder)) {
                return border;
            }
            const size_t offset =
                static_cast<size_t>(swizzle.index(coordinate(x, width, mode_u),
                                                  coordinate(y, height, mode_v))) *
                bytes_per_texel;
            if (alpha8) {
                const uint8_t a = data[offset];
                return std::array<float, 4>{
                    {1.0f, 1.0f, 1.0f, static_cast<float>(a) / 255.0f}};
            }
            uint32_t pixel = 0;
            std::memcpy(&pixel, data.data() + offset, sizeof(pixel));
            return unpack_argb8(pixel);
        };
        const auto c00 = texel(x0, y0);
        const auto c10 = texel(x0 + 1, y0);
        const auto c01 = texel(x0, y0 + 1);
        const auto c11 = texel(x0 + 1, y0 + 1);
        for (size_t c4 = 0; c4 < 4; ++c4) {
            const float top = c00[c4] + (c10[c4] - c00[c4]) * fx;
            const float bottom = c01[c4] + (c11[c4] - c01[c4]) * fx;
            color[c4] = top + (bottom - top) * fy;
        }
        return true;
    };
    auto sample_linear = [&unpack_argb8](
                             const std::vector<uint8_t>& data, uint32_t width,
                             uint32_t height, uint32_t row_pitch, float s, float t,
                             TextureAddressMode mode_u, TextureAddressMode mode_v,
                             uint32_t border_pixel, std::array<float, 4>& color) {
        if (!std::isfinite(s) || !std::isfinite(t) || !width || !height) return false;
        const auto wrap_texel_space = [](float value, uint32_t size,
                                         TextureAddressMode mode) {
            if (mode == TextureAddressMode::kClampToEdge) {
                return std::clamp(value, 0.0f, static_cast<float>(size));
            }
            if (mode == TextureAddressMode::kRepeat) {
                const float extent = static_cast<float>(size);
                return value - std::floor(value / extent) * extent;
            }
            return value;
        };
        s = wrap_texel_space(s, width, mode_u);
        t = wrap_texel_space(t, height, mode_v);
        const float texel_x = s - 0.5f;
        const float texel_y = t - 0.5f;
        const int32_t x0 = static_cast<int32_t>(std::floor(texel_x));
        const int32_t y0 = static_cast<int32_t>(std::floor(texel_y));
        const float fx = texel_x - static_cast<float>(x0);
        const float fy = texel_y - static_cast<float>(y0);
        if (mode_u != TextureAddressMode::kBorder &&
            mode_v != TextureAddressMode::kBorder) {
            auto coordinate_fast = [](int32_t value, uint32_t size,
                                      TextureAddressMode mode) {
                if (mode == TextureAddressMode::kRepeat) {
                    if (value < 0) return size - 1;
                    if (value >= static_cast<int32_t>(size)) return 0u;
                    return static_cast<uint32_t>(value);
                }
                return static_cast<uint32_t>(
                    std::clamp(value, 0, static_cast<int32_t>(size) - 1));
            };
            auto texel = [&](int32_t x, int32_t y) {
                const uint32_t cx = coordinate_fast(x, width, mode_u);
                const uint32_t cy = coordinate_fast(y, height, mode_v);
                const size_t offset = static_cast<size_t>(cy) * row_pitch +
                                      static_cast<size_t>(cx) * sizeof(uint32_t);
                uint32_t pixel = 0;
                std::memcpy(&pixel, data.data() + offset, sizeof(pixel));
                return unpack_argb8(pixel);
            };
            const auto c00 = texel(x0, y0);
            const auto c10 = texel(x0 + 1, y0);
            const auto c01 = texel(x0, y0 + 1);
            const auto c11 = texel(x0 + 1, y0 + 1);
            for (size_t c4 = 0; c4 < 4; ++c4) {
                const float top = c00[c4] + (c10[c4] - c00[c4]) * fx;
                const float bottom = c01[c4] + (c11[c4] - c01[c4]) * fx;
                color[c4] = top + (bottom - top) * fy;
            }
            return true;
        }
        const auto border = unpack_argb8(border_pixel);
        auto coordinate = [](int32_t value, uint32_t size, TextureAddressMode mode) {
            if (mode == TextureAddressMode::kRepeat) {
                const int32_t signed_size = static_cast<int32_t>(size);
                int32_t wrapped = value % signed_size;
                if (wrapped < 0) wrapped += signed_size;
                return static_cast<uint32_t>(wrapped);
            }
            return static_cast<uint32_t>(
                std::clamp(value, 0, static_cast<int32_t>(size) - 1));
        };
        auto texel = [&](int32_t x, int32_t y) {
            if ((mode_u == TextureAddressMode::kBorder &&
                 (x < 0 || x >= static_cast<int32_t>(width))) ||
                (mode_v == TextureAddressMode::kBorder &&
                 (y < 0 || y >= static_cast<int32_t>(height)))) {
                return border;
            }
            const uint32_t cx = coordinate(x, width, mode_u);
            const uint32_t cy = coordinate(y, height, mode_v);
            const size_t offset = static_cast<size_t>(cy) * row_pitch +
                                  static_cast<size_t>(cx) * sizeof(uint32_t);
            uint32_t pixel = 0;
            std::memcpy(&pixel, data.data() + offset, sizeof(pixel));
            return unpack_argb8(pixel);
        };
        const auto c00 = texel(x0, y0);
        const auto c10 = texel(x0 + 1, y0);
        const auto c01 = texel(x0, y0 + 1);
        const auto c11 = texel(x0 + 1, y0 + 1);
        for (size_t c4 = 0; c4 < 4; ++c4) {
            const float top = c00[c4] + (c10[c4] - c00[c4]) * fx;
            const float bottom = c01[c4] + (c11[c4] - c01[c4]) * fx;
            color[c4] = top + (bottom - top) * fy;
        }
        return true;
    };
    // Matches xemu's get_input_var() exactly (cross-checked against psh.c): only
    // UNSIGNED_INVERT clamps its input to [0,1] before inverting; every other
    // unsigned/half-bias mapping only floors at 0 (max(x,0)), so an intermediate
    // register that legitimately holds a scaled value >1 (via an output
    // SHIFTLEFTBY1/2 below) is not silently clipped back down by the next stage
    // that reads it.
    auto apply_mapping = [](uint32_t mapping, float value) {
        switch (mapping) {
            case 0: return std::max(value, 0.0f);                         // UNSIGNED_IDENTITY
            case 1: return 1.0f - std::clamp(value, 0.0f, 1.0f);          // UNSIGNED_INVERT
            case 2: return 2.0f * std::max(value, 0.0f) - 1.0f;           // EXPAND_NORMAL
            case 3: return -2.0f * std::max(value, 0.0f) + 1.0f;         // EXPAND_NEGATE
            case 4: return std::max(value, 0.0f) - 0.5f;                  // HALF_BIAS_NORMAL
            case 5: return -std::max(value, 0.0f) + 0.5f;                 // HALF_BIAS_NEGATE
            case 6: return value;                                        // SIGNED_IDENTITY
            default: return -value;                                      // SIGNED_NEGATE
        }
    };
    // Registers 14 (V1R0_SUM) and 15 (EF_PROD) are not plain stored values -- they are
    // computed on the fly from other registers, matching xemu's get_var(): V1R0_SUM is
    // SECONDARY_COLOR (v1, regs[5]) + R0 (regs[12]) under the final combiner's own
    // CLAMP_SUM/COMPLEMENT_V1/COMPLEMENT_R0 flags (a single global setting shared by
    // every reader, per stage or final), and EF_PROD is the final combiner's own E*F
    // product (E/F decoded from SpecularFogCW1's top two bytes).
    auto get_register_value = [&](uint32_t reg,
                                  const std::array<std::array<float, 4>, 16>& regs) {
        if (reg == 14) {
            std::array<float, 4> result{{0.0f, 0.0f, 0.0f, 0.0f}};
            for (size_t c3 = 0; c3 < 3; ++c3) {
                const float v1c = final_inv_v1 ? (1.0f - regs[5][c3]) : regs[5][c3];
                const float r0c = final_inv_r0 ? (1.0f - regs[12][c3]) : regs[12][c3];
                const float sum = v1c + r0c;
                result[c3] = final_clamp_sum ? std::clamp(sum, 0.0f, 1.0f) : sum;
            }
            return result;
        }
        if (reg == 15) {
            const std::array<float, 4> raw_e =
                final_e.reg <= 13 ? regs[final_e.reg]
                                  : std::array<float, 4>{{0, 0, 0, 0}};
            const std::array<float, 4> raw_f =
                final_f.reg <= 13 ? regs[final_f.reg]
                                  : std::array<float, 4>{{0, 0, 0, 0}};
            std::array<float, 4> result{{0.0f, 0.0f, 0.0f, 0.0f}};
            for (size_t c3 = 0; c3 < 3; ++c3) {
                const float ec = final_e.alt ? raw_e[3] : raw_e[c3];
                const float fc = final_f.alt ? raw_f[3] : raw_f[c3];
                result[c3] = apply_mapping(final_e.mapping, ec) *
                            apply_mapping(final_f.mapping, fc);
            }
            return result;
        }
        return regs[reg];
    };
    auto fetch_color = [&](const DecodedInput& in,
                          const std::array<std::array<float, 4>, 16>& regs) {
        const std::array<float, 4> r = get_register_value(in.reg, regs);
        const float base[3] = {in.alt ? r[3] : r[0], in.alt ? r[3] : r[1],
                               in.alt ? r[3] : r[2]};
        return std::array<float, 3>{{apply_mapping(in.mapping, base[0]),
                                     apply_mapping(in.mapping, base[1]),
                                     apply_mapping(in.mapping, base[2])}};
    };
    // bit4 polarity here (1=ALPHA, 0=BLUE) matches xemu's get_input_var() PS_CHANNEL
    // handling for the alpha portion exactly (cross-checked, not guessed).
    auto fetch_alpha = [&](const DecodedInput& in,
                          const std::array<std::array<float, 4>, 16>& regs) {
        const std::array<float, 4> r = get_register_value(in.reg, regs);
        const float base = in.alt ? r[3] : r[2];
        return apply_mapping(in.mapping, base);
    };

    bool raster_failed = false;
    std::function<void(const TransformedVertex&, const TransformedVertex&,
                       const TransformedVertex&)>
        rasterize_triangle;
    rasterize_triangle = [&](const TransformedVertex& input_a,
                             const TransformedVertex& input_b,
                             const TransformedVertex& input_c) {
        if (raster_failed) return;
        const TransformedVertex* a = &input_a;
        const TransformedVertex* b = &input_b;
        const TransformedVertex* c = &input_c;
        for (const TransformedVertex* vertex : {a, b, c}) {
            if (!std::isfinite(vertex->position[0]) ||
                !std::isfinite(vertex->position[1]) ||
                !std::isfinite(vertex->position[2])) {
                return;
            }
        }
        constexpr float kMinPositiveW = 1.0e-6f;
        const float surface_w = static_cast<float>((surface_pitch & 0xffffu) / 4u);
        const float surface_h = static_cast<float>(surface_height);
        struct ClipVertex {
            TransformedVertex vertex;
            float clip_x = 0.0f;
            float clip_y = 0.0f;
            float clip_w = 0.0f;
        };
        auto make_clip_vertex = [&](const TransformedVertex& vertex) {
            ClipVertex out{};
            out.vertex = vertex;
            out.clip_w = vertex.position[3];
            out.clip_x = (2.0f * vertex.position[0] / surface_w - 1.0f) *
                         out.clip_w;
            out.clip_y = (2.0f * vertex.position[1] / surface_h - 1.0f) *
                         out.clip_w;
            return out;
        };
        auto lerp_clip_vertex = [](const ClipVertex& v0, const ClipVertex& v1,
                                   float t) {
            ClipVertex out{};
            out.clip_x = v0.clip_x + (v1.clip_x - v0.clip_x) * t;
            out.clip_y = v0.clip_y + (v1.clip_y - v0.clip_y) * t;
            out.clip_w = v0.clip_w + (v1.clip_w - v0.clip_w) * t;
            out.vertex.position[2] =
                v0.vertex.position[2] +
                (v1.vertex.position[2] - v0.vertex.position[2]) * t;
            out.vertex.position[3] = out.clip_w;
            for (size_t component = 0; component < 4; ++component) {
                out.vertex.diffuse[component] =
                    v0.vertex.diffuse[component] +
                    (v1.vertex.diffuse[component] -
                     v0.vertex.diffuse[component]) *
                        t;
                out.vertex.specular[component] =
                    v0.vertex.specular[component] +
                    (v1.vertex.specular[component] -
                     v0.vertex.specular[component]) *
                        t;
                for (size_t stage = 0; stage < 4; ++stage) {
                    out.vertex.texture[stage][component] =
                        v0.vertex.texture[stage][component] +
                        (v1.vertex.texture[stage][component] -
                         v0.vertex.texture[stage][component]) *
                            t;
                }
            }
            return out;
        };
        std::vector<ClipVertex> clip_polygon{
            make_clip_vertex(*a), make_clip_vertex(*b), make_clip_vertex(*c)};
        auto clip_plane = [&](auto plane_value) {
            if (clip_polygon.empty()) return;
            std::vector<ClipVertex> output;
            output.reserve(clip_polygon.size() + 1);
            ClipVertex prev = clip_polygon.back();
            float prev_value = plane_value(prev);
            bool prev_inside = prev_value >= 0.0f;
            for (const ClipVertex& cur : clip_polygon) {
                const float cur_value = plane_value(cur);
                const bool cur_inside = cur_value >= 0.0f;
                if (cur_inside != prev_inside) {
                    const float denom = prev_value - cur_value;
                    const float t = denom == 0.0f ? 0.0f : prev_value / denom;
                    output.push_back(lerp_clip_vertex(prev, cur, t));
                }
                if (cur_inside) output.push_back(cur);
                prev = cur;
                prev_value = cur_value;
                prev_inside = cur_inside;
            }
            clip_polygon = std::move(output);
        };
        constexpr float kClipTolerance = 1.0e-4f;
        const bool needs_clip =
            std::any_of(clip_polygon.begin(), clip_polygon.end(),
                        [](const ClipVertex& vertex) {
                            return vertex.clip_w < kMinPositiveW ||
                                   vertex.clip_x < -vertex.clip_w - kClipTolerance ||
                                   vertex.clip_x > vertex.clip_w + kClipTolerance ||
                                   vertex.clip_y < -vertex.clip_w - kClipTolerance ||
                                   vertex.clip_y > vertex.clip_w + kClipTolerance;
                        });
        if (needs_clip) {
            clip_plane([](const ClipVertex& vertex) {
                return vertex.clip_w - kMinPositiveW;
            });
            clip_plane([](const ClipVertex& vertex) {
                return vertex.clip_x + vertex.clip_w;
            });
            clip_plane([](const ClipVertex& vertex) {
                return vertex.clip_w - vertex.clip_x;
            });
            clip_plane([](const ClipVertex& vertex) {
                return vertex.clip_y + vertex.clip_w;
            });
            clip_plane([](const ClipVertex& vertex) {
                return vertex.clip_w - vertex.clip_y;
            });
            if (clip_polygon.size() < 3) return;
            std::vector<TransformedVertex> clipped;
            clipped.reserve(clip_polygon.size());
            for (ClipVertex& vertex : clip_polygon) {
                if (vertex.clip_w <= 0.0f) return;
                vertex.vertex.position[0] =
                    (vertex.clip_x / vertex.clip_w + 1.0f) * surface_w * 0.5f;
                vertex.vertex.position[1] =
                    (vertex.clip_y / vertex.clip_w + 1.0f) * surface_h * 0.5f;
                vertex.vertex.position[3] = vertex.clip_w;
                clipped.push_back(vertex.vertex);
            }
            for (size_t i = 1; i + 1 < clipped.size(); ++i)
                rasterize_triangle(clipped[0], clipped[i], clipped[i + 1]);
            return;
        }
        auto fixed_xy = [](float value) -> int64_t {
            return static_cast<int64_t>(std::llround(value * 16.0f));
        };
        int64_t ax = fixed_xy(a->position[0]);
        int64_t ay = fixed_xy(a->position[1]);
        int64_t bx = fixed_xy(b->position[0]);
        int64_t by = fixed_xy(b->position[1]);
        int64_t cx = fixed_xy(c->position[0]);
        int64_t cy = fixed_xy(c->position[1]);
        auto edge = [](int64_t x0, int64_t y0, int64_t x1, int64_t y1,
                       int64_t px, int64_t py) -> int64_t {
            return (x1 - x0) * (py - y0) - (y1 - y0) * (px - x0);
        };
        int64_t area = edge(ax, ay, bx, by, cx, cy);
        if (area == 0) return;
        // area>0 is the front-facing (CW) winding, matching every witnessed profile.
        // Cull disabled: both windings render (flip negative-area triangles to the
        // front-facing orientation so the rest of the pipeline always sees area>0).
        // Cull GL_BACK (0x0405): keep front-facing (area>0), reject area<0.
        // Cull GL_FRONT (0x0404): the mirror image -- keep back-facing (area<0),
        // reject area>0; flip so the rest of the pipeline still sees a positive area.
        if (!cull_enabled) {
            if (area < 0) {
                std::swap(b, c);
                std::swap(bx, cx);
                std::swap(by, cy);
                area = -area;
            }
        } else if (cull_front) {
            if (area > 0) return;
            std::swap(b, c);
            std::swap(bx, cx);
            std::swap(by, cy);
            area = -area;
        } else if (area < 0) {
            return;
        }

        ++generic_combiner_triangles_;
        if (generic_shader_row_) ++generic_shader_row_->triangles;
        const int32_t tri_xmin = std::max<int32_t>(
            static_cast<int32_t>(xmin),
            static_cast<int32_t>(std::ceil(
                (static_cast<double>(std::min({ax, bx, cx})) - 8.0) / 16.0)));
        const int32_t tri_xmax = std::min<int32_t>(
            static_cast<int32_t>(xmax),
            static_cast<int32_t>(std::floor(
                (static_cast<double>(std::max({ax, bx, cx})) - 8.0) / 16.0)));
        const int32_t tri_ymin = std::max<int32_t>(
            static_cast<int32_t>(ymin),
            static_cast<int32_t>(std::ceil(
                (static_cast<double>(std::min({ay, by, cy})) - 8.0) / 16.0)));
        const int32_t tri_ymax = std::min<int32_t>(
            static_cast<int32_t>(ymax),
            static_cast<int32_t>(std::floor(
                (static_cast<double>(std::max({ay, by, cy})) - 8.0) / 16.0)));
        if (tri_xmax < tri_xmin || tri_ymax < tri_ymin) return;
        // Phase 6 census: the scanline box this triangle's software loop will
        // walk, ALWAYS ON (the block below is --perf-accounting only). One
        // 64-bit multiply-add per software triangle; it is the only per-
        // triangle size number available without collect_diagnostics and it is
        // what sizes the GPU work a generated shader would take over.
        if (generic_shader_row_) {
            generic_shader_row_->bbox_pixels +=
                static_cast<uint64_t>(tri_xmax - tri_xmin + 1) *
                static_cast<uint64_t>(tri_ymax - tri_ymin + 1);
        }
        if (profile_generic_fingerprint && generic_fingerprint_stats) {
            const uint64_t bbox_pixels =
                static_cast<uint64_t>(tri_xmax - tri_xmin + 1) *
                static_cast<uint64_t>(tri_ymax - tri_ymin + 1);
            generic_batch_bbox_pixels += bbox_pixels;
            if (!generic_fingerprint_stats->first_triangle_recorded) {
                generic_fingerprint_stats->first_triangle_recorded = true;
                generic_fingerprint_stats->first_triangle_xy_16 =
                    {ax, ay, bx, by, cx, cy};
                generic_fingerprint_stats->first_triangle_bounds =
                    {tri_xmin, tri_ymin, tri_xmax, tri_ymax};
                generic_fingerprint_stats->first_triangle_area_16 =
                    static_cast<uint64_t>(area);
            }
        }

        const auto top_left = [](int64_t x0, int64_t y0, int64_t x1, int64_t y1) {
            const int64_t dx = x1 - x0;
            const int64_t dy = y1 - y0;
            return dy < 0 || (dy == 0 && dx > 0);
        };
        const bool edge0_top_left = top_left(bx, by, cx, cy);
        const bool edge1_top_left = top_left(cx, cy, ax, ay);
        const bool edge2_top_left = top_left(ax, ay, bx, by);

        const float x10 = static_cast<float>(bx - ax) / 16.0f;
        const float y10 = static_cast<float>(by - ay) / 16.0f;
        const float x20 = static_cast<float>(cx - ax) / 16.0f;
        const float y20 = static_cast<float>(cy - ay) / 16.0f;
        const float z10 = b->position[2] - a->position[2];
        const float z20 = c->position[2] - a->position[2];
        const float determinant = x10 * y20 - x20 * y10;
        const float dzdx = (z10 * y20 - z20 * y10) / determinant;
        const float dzdy = (z20 * x10 - z10 * x20) / determinant;
        const float max_z_slope = std::max(std::abs(dzdx), std::abs(dzdy));
        const float inverse_area = 1.0f / static_cast<float>(area);
        const bool constant_depth =
            a->position[2] == b->position[2] &&
            a->position[2] == c->position[2];
        const float constant_depth_value =
            a->position[2] + polygon_bias + polygon_scale * max_z_slope;
        const bool constant_depth_passes =
            constant_depth && std::isfinite(constant_depth_value) &&
            constant_depth_value >= clip_min && constant_depth_value <= clip_max;
        const uint32_t constant_incoming_depth = constant_depth_passes
            ? static_cast<uint32_t>(std::floor(
                  std::clamp(constant_depth_value, 0.0f, 16777215.0f)))
            : 0u;
        const bool depth_read_required =
            uses_depth &&
            ((raster_state[kRasterDepthMask] != 0) ||
             (depth_test_enabled_bit && depth_func != 0x0207));
        const std::array<const TransformedVertex*, 3> triangle_vertices{{a, b, c}};
        std::array<std::array<float, 4>, 3> diffuse_values{};
        std::array<std::array<float, 4>, 3> specular_values{};
        std::array<std::array<std::array<float, 4>, 3>, 4> texture_values{};
        std::array<uint8_t, 4> texture_component_masks{};
        auto sanitize_color_component = [](float value, size_t component) {
            if (std::isnan(value)) value = component == 3 ? 1.0f : 0.0f;
            return std::clamp(value, 0.0f, 1.0f);
        };
        if (needs_diffuse) {
            for (size_t vertex = 0; vertex < triangle_vertices.size(); ++vertex) {
                for (size_t component = 0; component < 4; ++component) {
                    diffuse_values[vertex][component] =
                        sanitize_color_component(
                            triangle_vertices[vertex]->diffuse[component],
                            component);
                }
            }
        }
        if (needs_specular) {
            for (size_t vertex = 0; vertex < triangle_vertices.size(); ++vertex) {
                for (size_t component = 0; component < 4; ++component) {
                    specular_values[vertex][component] =
                        sanitize_color_component(
                            triangle_vertices[vertex]->specular[component],
                            component);
                }
            }
        }
        for (uint32_t stage = 0; stage < 4; ++stage) {
            if (!texture_referenced[stage]) continue;
            const TextureStage& ts = texture_stages[stage];
            uint8_t component_mask = ts.cubemap
                ? 0x07u
                : static_cast<uint8_t>((ts.project2d || ts.linear) ? 0x0Bu : 0x03u);
            for (size_t vertex = 0; vertex < triangle_vertices.size(); ++vertex) {
                for (size_t component = 0; component < 4; ++component) {
                    const float value =
                        triangle_vertices[vertex]->texture[stage][component];
                    texture_values[stage][vertex][component] =
                        std::isnan(value) ? 0.0f : value;
                    // Preserve the old generic fault behavior for infinities in
                    // otherwise-unused texture coordinates: NaN sanitized to zero
                    // before, but +/-Inf made the interpolation non-finite.
                    if ((component_mask & (1u << component)) == 0 &&
                        !std::isfinite(value) && !std::isnan(value)) {
                        component_mask |= static_cast<uint8_t>(1u << component);
                    }
                }
            }
            texture_component_masks[stage] = component_mask;
        }

        // ---- generated-shader COLLECTION (phase 6) -------------------------
        // Everything above this point is the software path's own per-triangle
        // setup, unmodified: the clip, the fixed-point snap, the cull, the
        // scanline box, the depth slope, the diffuse/specular sanitisation and
        // the texcoord component mask. Collecting HERE is what makes the GPU
        // execute the same triangle set with the same numbers -- and it is
        // also why the four `vertex-expansion` declines the phase-6 census
        // recorded (519 batches, 97 resolves) simply do not exist on this
        // path: a w-plane crossing has already been clipped by the recursion
        // above, so there is no geometry left for the executor to refuse.
        if (gpu_shader_collect) {
            if (constant_depth && !constant_depth_passes) return;
            Nv2aGeneratedTriangle t;
            t.ax = ax; t.ay = ay; t.bx = bx; t.by = by; t.cx = cx; t.cy = cy;
            t.bbox_xmin = tri_xmin; t.bbox_xmax = tri_xmax;
            t.bbox_ymin = tri_ymin; t.bbox_ymax = tri_ymax;
            // The witnessed executor's rule is 2^24, where a float stops
            // holding every integer. That is necessary but NOT sufficient
            // here: the geometry this executor takes is precisely the geometry
            // the hand-written resident fixture declined, which means vertices
            // far outside the viewport, and a triangle beyond the driver's
            // GUARD BAND is clipped by the driver in float NDC -- moving its
            // edges by a fraction of a pixel and UNDER-covering, the one error
            // an in-shader test cannot repair. The bound is therefore a
            // runtime value in guest pixels, defaulted well inside any desktop
            // guard band, and 0 forces every triangle onto the exact
            // scanline-box path (the diagnostic that measured this).
            const int64_t kWide =
                static_cast<int64_t>(gpu_generic_shader_wide_pixels_) * 16;
            const auto is_wide = [kWide](int64_t v) {
                return v >= kWide || v <= -kWide;
            };
            t.wide = kWide == 0 || is_wide(ax) || is_wide(ay) || is_wide(bx) ||
                     is_wide(by) || is_wide(cx) || is_wide(cy);
            t.za = a->position[2];
            t.zb = b->position[2];
            t.zc = c->position[2];
            t.inverse_area = inverse_area;
            t.inv_area_double = 1.0 / static_cast<double>(area);
            t.inv_area_recip = static_cast<float>(t.inv_area_double);
            // The bias is carried and ALWAYS added by the shader, never
            // optimised away when polygon offset is off: max_z_slope is
            // infinite for a triangle whose float determinant underflows to
            // zero, and 0.0f * inf is NaN, which is exactly how the software
            // path discards those fragments.
            t.depth_bias = polygon_bias + polygon_scale * max_z_slope;
            t.constant_depth = constant_depth;
            t.constant_depth_value = constant_depth_value;
            bool collect_ok = true;
            for (size_t v = 0; v < 3 && collect_ok; ++v) {
                const float pw = triangle_vertices[v]->position[3];
                if (!std::isfinite(pw) || pw == 0.0f) {
#if XLLE_VSH_DRAW_OVERLAP_CENSUS
                    if (!std::isfinite(pw)) gpu_shader_collect_nonfinite = true;
#endif
                    collect_ok = false;
                    break;
                }
                t.inv_w_double[v] = 1.0 / static_cast<double>(pw);
                t.inv_w[v] = static_cast<float>(t.inv_w_double[v]);
                for (size_t c4 = 0; c4 < 4; ++c4) {
                    t.varying[v][0][c4] =
                        needs_diffuse ? diffuse_values[v][c4] : 0.0f;
                    t.varying[v][1][c4] =
                        needs_specular ? specular_values[v][c4] : 0.0f;
                }
                for (uint32_t stage = 0; stage < 4 && collect_ok; ++stage) {
                    if (!texture_referenced[stage]) continue;
                    for (size_t c4 = 0; c4 < 4; ++c4) {
                        // A lane the software path's component mask leaves
                        // uninterpolated stays exactly zero, which is what the
                        // software's own texcoord[] holds for it.
                        const bool masked =
                            (texture_component_masks[stage] & (1u << c4)) != 0;
                        const float value =
                            masked ? texture_values[stage][v][c4] : 0.0f;
                        if (masked && !std::isfinite(value)) {
#if XLLE_VSH_DRAW_OVERLAP_CENSUS
                            gpu_shader_collect_nonfinite = true;
#endif
                            collect_ok = false;
                            break;
                        }
                        t.varying[v][2 + stage][c4] = value;
                    }
                }
            }
            if (!collect_ok) {
                gpu_shader_collect_failed = true;
                return;
            }
            gpu_shader_packet.triangles.push_back(t);
            return;
        }

        for (int32_t y = tri_ymin; y <= tri_ymax; ++y) {
            const int64_t py = static_cast<int64_t>(y) * 16 + 8;
            for (int32_t x = tri_xmin; x <= tri_xmax; ++x) {
                const int64_t px = static_cast<int64_t>(x) * 16 + 8;
                const int64_t w0 = edge(bx, by, cx, cy, px, py);
                const int64_t w1 = edge(cx, cy, ax, ay, px, py);
                const int64_t w2 = edge(ax, ay, bx, by, px, py);
                if (w0 < 0 || (w0 == 0 && !edge0_top_left) ||
                    w1 < 0 || (w1 == 0 && !edge1_top_left) ||
                    w2 < 0 || (w2 == 0 && !edge2_top_left)) {
                    continue;
                }
                if (collect_diagnostics) ++generic_combiner_fragments_;
                uint32_t incoming_depth = constant_incoming_depth;
                if (constant_depth) {
                    if (!constant_depth_passes) continue;
                } else {
                    float depth = (static_cast<float>(w0) * a->position[2] +
                                  static_cast<float>(w1) * b->position[2] +
                                  static_cast<float>(w2) * c->position[2]) *
                                 inverse_area;
                    depth += polygon_bias + polygon_scale * max_z_slope;
                    if (!std::isfinite(depth) || depth < clip_min || depth > clip_max)
                        continue;
                    incoming_depth = static_cast<uint32_t>(
                        std::floor(std::clamp(depth, 0.0f, 16777215.0f)));
                }

                const size_t offset = surface_swizzled
                    ? static_cast<size_t>(surface_swizzle->index(
                          static_cast<uint32_t>(x),
                          static_cast<uint32_t>(y))) * sizeof(uint32_t)
                    : static_cast<size_t>(y) * pitch +
                          static_cast<size_t>(x) * sizeof(uint32_t);
                uint32_t stored_depth_stencil = 0;
                if (depth_read_required) {
                    if (capture_generic_deltas) ++generic_combiner_depth_reads_;
                    if (!depth_surface.read32(offset, stored_depth_stencil)) {
                        fault_("NV2A generic-combiner zeta DMA read failed");
                        raster_failed = true;
                        return;
                    }
                    // Depth-test-enable (bit3) is semantic: when disabled every
                    // fragment passes, even though the surface is still touched here
                    // because depth writes are enabled.
                    if (depth_test_enabled_bit &&
                        !compare(depth_func, incoming_depth,
                                stored_depth_stencil >> 8)) {
                        continue;
                    }
                }
                if (collect_diagnostics) ++generic_combiner_depth_passes_;

                const double lambda0 =
                    static_cast<double>(w0) / static_cast<double>(area);
                const double lambda1 =
                    static_cast<double>(w1) / static_cast<double>(area);
                const double lambda2 =
                    static_cast<double>(w2) / static_cast<double>(area);
                const double q0 = lambda0 / a->position[3];
                const double q1 = lambda1 / b->position[3];
                const double q2 = lambda2 / c->position[3];
                const double qsum = q0 + q1 + q2;
                if (!std::isfinite(qsum) || qsum == 0.0) continue;

                std::array<float, 4> diffuse{};
                std::array<float, 4> specular{};
                bool varying_failed = false;
                auto interpolate4 =
                    [&](const std::array<std::array<float, 4>, 3>& values) {
                    std::array<float, 4> result{};
                    for (size_t c4 = 0; c4 < 4; ++c4) {
                        const double d = (q0 * values[0][c4] +
                                         q1 * values[1][c4] +
                                         q2 * values[2][c4]) / qsum;
                        if (!std::isfinite(d)) {
                            varying_failed = true;
                            return result;
                        }
                        result[c4] = static_cast<float>(d);
                    }
                    return result;
                };
                if (needs_diffuse)
                    diffuse = interpolate4(diffuse_values);
                if (!varying_failed && needs_specular)
                    specular = interpolate4(specular_values);
                if (varying_failed) {
                    fault_("NV2A generic-combiner varying is non-finite");
                    raster_failed = true;
                    return;
                }

                std::array<std::array<float, 4>, 16> regs{};
                if (needs_diffuse) regs[4] = diffuse;
                if (needs_specular) regs[5] = specular;

                // Texture stages: interpolate texcoords the exact same way every
                // witnessed textured profile does (perspective-correct via the
                // position-based q0/q1/q2, unclamped -- unlike diffuse/specular, UV
                // and texel-space coordinates routinely go outside [0,1]). This reads
                // TransformedVertex::texture[stage], which submit_vertex_batch already
                // populates identically for both the fixed-function path
                // (vertex_attributes_[9+stage]) and the vertex-program path
                // (oT0.. output registers) -- so the generic evaluator picks up
                // interpolated texcoords the same way regardless of transform mode.
                for (uint32_t stage = 0; stage < 4; ++stage) {
                    if (!texture_referenced[stage]) continue;
                    const TextureStage& ts = texture_stages[stage];
                    std::array<float, 4> texcoord{};
                    bool texcoord_failed = false;
                    for (size_t c4 = 0; c4 < 4; ++c4) {
                        if ((texture_component_masks[stage] & (1u << c4)) == 0)
                            continue;
                        const double t = (q0 * texture_values[stage][0][c4] +
                                         q1 * texture_values[stage][1][c4] +
                                         q2 * texture_values[stage][2][c4]) /
                                        qsum;
                        if (!std::isfinite(t)) {
                            texcoord_failed = true;
                            break;
                        }
                        texcoord[c4] = static_cast<float>(t);
                    }
                    if (texcoord_failed) {
                        fault_("NV2A generic-combiner texcoord is non-finite");
                        raster_failed = true;
                        return;
                    }
                    if (texf_projected_black_alpha_one) continue;
                    std::array<float, 4> sampled{};
                    bool sample_ok = false;
                    if (ts.linear) {
                        if (texcoord[3] == 0.0f) {
                            fault_("NV2A generic-combiner texture Q is zero");
                            raster_failed = true;
                            return;
                        }
                        sample_ok = sample_linear(
                            ts.data, ts.width, ts.height, ts.pitch,
                            texcoord[0] / texcoord[3],
                            texcoord[1] / texcoord[3], ts.address_u, ts.address_v,
                            ts.border, sampled);
                    } else if (ts.cubemap) {
                        // Same major-axis direction-to-UV algorithm as the pinned
                        // profiles' own sample_cube_texture (fresh independent copy):
                        // the interpolated texcoord.xyz is a direction vector, not a
                        // normalized UV pair. Always CLAMP_TO_EDGE, matching every
                        // pinned cubemap sample regardless of the texture's own
                        // address register.
                        // Strict '>' with a Z-axis fallback on any tie, matching
                        // sample_cube_texture's own default (no-tie-break-counter)
                        // path exactly -- using '>=' here previously picked a
                        // different face than the pinned profiles do on an exact
                        // tie, which is what the tex7_fan pinned test's own
                        // unwitnessed-border-value case exposed.
                        const float abs_x = std::abs(texcoord[0]);
                        const float abs_y = std::abs(texcoord[1]);
                        const float abs_z = std::abs(texcoord[2]);
                        float u = 0.0f, v = 0.0f, divisor = 0.0f;
                        if (abs_x > abs_y && abs_x > abs_z) {
                            divisor = abs_x;
                            u = texcoord[0] >= 0.0f ? -texcoord[2] : texcoord[2];
                            v = texcoord[1];
                        } else if (abs_y > abs_x && abs_y > abs_z) {
                            divisor = abs_y;
                            u = texcoord[0];
                            v = texcoord[1] >= 0.0f ? -texcoord[2] : texcoord[2];
                        } else {
                            divisor = abs_z;
                            u = texcoord[2] >= 0.0f ? texcoord[0] : -texcoord[0];
                            v = texcoord[1];
                        }
                        if (divisor == 0.0f) {
                            fault_("NV2A generic-combiner cubemap direction is zero");
                            raster_failed = true;
                            return;
                        }
                        sample_ok = sample_swizzled(
                            ts.data, *ts.swizzle, ts.width, ts.height, u / divisor,
                            v / divisor, TextureAddressMode::kClampToEdge,
                            TextureAddressMode::kClampToEdge, ts.border, ts.alpha8,
                            sampled);
                    } else {
                        float u = texcoord[0];
                        float v = texcoord[1];
                        if (ts.project2d) {
                            if (texcoord[3] == 0.0f) {
                                fault_("NV2A generic-combiner PROJECT2D Q is zero");
                                raster_failed = true;
                                return;
                            }
                            u /= texcoord[3];
                            v /= texcoord[3];
                        }
                        sample_ok = sample_swizzled(
                            ts.data, *ts.swizzle, ts.width, ts.height, u, v,
                            ts.address_u, ts.address_v, ts.border, ts.alpha8,
                            sampled);
                    }
                    if (!sample_ok) {
                        fault_("NV2A generic-combiner texture sample is invalid");
                        raster_failed = true;
                        return;
                    }
                    regs[8 + stage] = sampled;
                    if (collect_diagnostics) ++generic_combiner_texture_samples_;
                }

                if (needs_general_combiner) {
                    // Matches xemu's get_output(): the scale/bias op is applied to the
                    // AB/CD/SUM result BEFORE the final clamp to [-1,1] (real hardware
                    // range -- unlike the input side, general-combiner outputs are
                    // signed), and the SAME scale/bias value applies to AB, CD, and
                    // SUM/MUX alike (there is only one output-mapping field per stage).
                    auto apply_scale_bias = [](uint32_t sb, float value) {
                        switch (sb) {
                            case 0x00: return value;                       // IDENTITY
                            case 0x08: return value - 0.5f;                // BIAS
                            case 0x10: return value * 2.0f;                // SHIFTLEFTBY1
                            case 0x18: return (value - 0.5f) * 2.0f;       // SHIFTLEFTBY1+BIAS
                            case 0x20: return value * 4.0f;                // SHIFTLEFTBY2
                            default: return value * 0.5f;                  // SHIFTRIGHTBY1 (0x30)
                        }
                    };
                    for (uint32_t stage = 0; stage < stage_count; ++stage) {
                        const Stage& s = stages[stage];
                        regs[1] = unpack_argb8(
                            raster_state[kRasterCombinerFactor0_0 + stage]);
                        regs[2] = unpack_argb8(
                            raster_state[kRasterCombinerFactor1_0 + stage]);

                        const std::array<float, 3> ca = fetch_color(s.color[0], regs);
                        const std::array<float, 3> cb = fetch_color(s.color[1], regs);
                        const std::array<float, 3> cc = fetch_color(s.color[2], regs);
                        const std::array<float, 3> cd = fetch_color(s.color[3], regs);
                        std::array<float, 3> cab{}, ccd{};
                        if (s.color_ab_dot) {
                            const float dot =
                                ca[0] * cb[0] + ca[1] * cb[1] + ca[2] * cb[2];
                            cab = {{dot, dot, dot}};
                        } else {
                            cab = {{ca[0] * cb[0], ca[1] * cb[1], ca[2] * cb[2]}};
                        }
                        if (s.color_cd_dot) {
                            const float dot =
                                cc[0] * cd[0] + cc[1] * cd[1] + cc[2] * cd[2];
                            ccd = {{dot, dot, dot}};
                        } else {
                            ccd = {{cc[0] * cd[0], cc[1] * cd[1], cc[2] * cd[2]}};
                        }
                        // MUX_SUM: the third output is either AB+CD (plain sum) or a
                        // select between CD (condition true) and AB (condition false)
                        // based on R0.alpha's LSB/MSB, matching xemu's add_stage_code().
                        const bool mux_condition =
                            mux_on_msb ? (regs[12][3] >= 0.5f)
                                      : ((static_cast<uint32_t>(std::floor(
                                            regs[12][3] * 255.0f + 0.5f)) &
                                         1u) != 0);
                        std::array<float, 3> csum{};
                        if (s.color_mux) {
                            csum = mux_condition ? ccd : cab;
                        } else {
                            csum = {{cab[0] + ccd[0], cab[1] + ccd[1],
                                     cab[2] + ccd[2]}};
                        }
                        auto store_color =
                            [&](uint32_t dst, const std::array<float, 3>& value,
                                bool blue_to_alpha) {
                            if (!dst) return;
                            for (size_t c3 = 0; c3 < 3; ++c3) {
                                regs[dst][c3] = std::clamp(
                                    apply_scale_bias(s.color_scale_bias, value[c3]),
                                    -1.0f, 1.0f);
                            }
                            // AB/CD_BLUE_TO_ALPHA: the same (already scaled/biased/
                            // clamped) blue channel also lands in this register's alpha
                            // channel, matching xemu's add_stage_code() exactly. Never
                            // applies to the SUM/MUX output (real hardware only wires
                            // this from the dedicated AB/CD destinations).
                            if (blue_to_alpha) regs[dst][3] = regs[dst][2];
                        };
                        store_color(s.color_ab_dst, cab, s.color_ab_blue_to_alpha);
                        store_color(s.color_cd_dst, ccd, s.color_cd_blue_to_alpha);
                        store_color(s.color_sum_dst, csum, false);

                        const float aa = fetch_alpha(s.alpha[0], regs);
                        const float ab = fetch_alpha(s.alpha[1], regs);
                        const float ac = fetch_alpha(s.alpha[2], regs);
                        const float ad = fetch_alpha(s.alpha[3], regs);
                        const float aab = aa * ab;
                        const float acd = ac * ad;
                        const bool alpha_mux_condition =
                            mux_on_msb ? (regs[12][3] >= 0.5f)
                                      : ((static_cast<uint32_t>(std::floor(
                                            regs[12][3] * 255.0f + 0.5f)) &
                                         1u) != 0);
                        const float asum =
                            s.alpha_mux ? (alpha_mux_condition ? acd : aab)
                                        : (aab + acd);
                        auto store_alpha = [&](uint32_t dst, float value) {
                            if (!dst) return;
                            regs[dst][3] = std::clamp(
                                apply_scale_bias(s.alpha_scale_bias, value), -1.0f,
                                1.0f);
                        };
                        store_alpha(s.alpha_ab_dst, aab);
                        store_alpha(s.alpha_cd_dst, acd);
                        store_alpha(s.alpha_sum_dst, asum);
                    }
                }

                // Final combiner: RGB = D + A*B + (1-A)*C; Alpha = G. When absent
                // (both SpecularFogCW words zero), R0/R0.a pass through untouched --
                // which the generic A=B=C=ZERO, D=R0, G=R0.alpha decode already
                // produces on its own, so no separate branch is needed.
                std::array<float, 4> final_color;
                if (primary_color_passthrough) {
                    final_color = diffuse;
                } else if (texture0_modulate_primary) {
                    const std::array<float, 4>& tex0 = regs[8];
                    for (size_t c3 = 0; c3 < 3; ++c3)
                        final_color[c3] = tex0[c3] * diffuse[c3];
                    if (raster_state[kRasterCombinerAlphaIcw0] == 0x14180000)
                        final_color[3] = tex0[3] * diffuse[3];
                    else
                        final_color[3] = diffuse[3];
                } else if (texture0_passthrough) {
                    const std::array<float, 4>& tex0 = regs[8];
                    for (size_t c3 = 0; c3 < 3; ++c3) final_color[c3] = tex0[c3];
                    final_color[3] = diffuse[3];
                } else if (texf_projected_black_alpha_one) {
                    final_color = {{0.0f, 0.0f, 0.0f, 1.0f}};
                } else if (final_enabled) {
                    // The final combiner's own CONSTANT_COLOR0/1 (registers 1/2) come
                    // from SPECFOGFACTOR0/1 (FinalFactor0/1), not the per-stage
                    // Factor0_0[stage]/Factor1_0[stage] array -- reload them now that
                    // every ordinary stage above is done reading regs[1]/regs[2].
                    regs[1] = unpack_argb8(raster_state[kRasterCombinerFinalFactor0]);
                    regs[2] = unpack_argb8(raster_state[kRasterCombinerFinalFactor1]);
                    const std::array<float, 3> fa = fetch_color(final_a, regs);
                    const std::array<float, 3> fb = fetch_color(final_b, regs);
                    const std::array<float, 3> fc = fetch_color(final_c, regs);
                    const std::array<float, 3> fd = fetch_color(final_d, regs);
                    for (size_t c3 = 0; c3 < 3; ++c3) {
                        final_color[c3] =
                            fd[c3] + fa[c3] * fb[c3] + (1.0f - fa[c3]) * fc[c3];
                    }
                    final_color[3] = fetch_alpha(final_g, regs);
                } else {
                    final_color = regs[12];
                }
                const float src_alpha = std::clamp(final_color[3], 0.0f, 1.0f);
                const uint32_t alpha_scaled =
                    static_cast<uint32_t>(std::floor(src_alpha * 255.0f + 0.5f));
                const uint32_t alpha_ref = raster_state[kRasterAlphaRef] & 0xFFu;
                // Alpha-test-enable (bit0) is semantic: when disabled every fragment
                // passes regardless of AlphaFunc/AlphaRef.
                if (alpha_test_enabled &&
                    !compare(alpha_func, alpha_scaled, alpha_ref)) {
                    continue;
                }

                // Coverage, depth, shader kills and alpha tests have passed.
                // Write masks do not suppress visibility samples.
                if (zpass_enabled_) ++zpass_count_;

                std::array<float, 3> out_rgb{{
                    std::clamp(final_color[0], 0.0f, 1.0f),
                    std::clamp(final_color[1], 0.0f, 1.0f),
                    std::clamp(final_color[2], 0.0f, 1.0f),
                }};
                float out_alpha = src_alpha;
                // Need the existing pixel if blending is enabled (as a blend operand)
                // or if any color-mask channel is disabled (to preserve it unwritten).
                const bool needs_existing_pixel =
                    blend_enabled || !color_mask_write[0] || !color_mask_write[1] ||
                    !color_mask_write[2] || !color_mask_write[3];
                std::array<float, 4> dst{};
                if (needs_existing_pixel) {
                    uint32_t existing = 0;
                    if (capture_generic_deltas) ++generic_combiner_color_reads_;
                    if (!color_surface.read32(offset, existing)) {
                        fault_("NV2A generic-combiner color DMA read failed");
                        raster_failed = true;
                        return;
                    }
                    dst = unpack_argb8(existing);
                }
                if (blend_enabled) {
                    // Only ZERO/ONE/SRC_ALPHA/ONE_MINUS_SRC_ALPHA are accepted above,
                    // so the per-component blend factor is fully determined by
                    // src_alpha (dst_alpha is not among the accepted factors).
                    auto blend_weight = [&](uint32_t factor) {
                        switch (factor) {
                            case 0: return 0.0f;                       // ZERO
                            case 1: return 1.0f;                       // ONE
                            case 0x0302: return src_alpha;             // SRC_ALPHA
                            default:                                   // ONE_MINUS_SRC_ALPHA
                                return 1.0f - src_alpha;
                        }
                    };
                    const float src_weight = blend_weight(blend_src);
                    const float dst_weight = blend_weight(blend_dst);
                    for (size_t c3 = 0; c3 < 3; ++c3) {
                        out_rgb[c3] = std::clamp(
                            out_rgb[c3] * src_weight + dst[c3] * dst_weight, 0.0f, 1.0f);
                    }
                    out_alpha = std::clamp(
                        src_alpha * src_weight + dst[3] * dst_weight, 0.0f, 1.0f);
                }
                auto pack_unorm8 = [](float value) {
                    return static_cast<uint32_t>(
                        std::floor(std::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f));
                };
                // X8R8G8B8 has no alpha channel on real hardware; always store 0xFF in
                // the padding byte. A8R8G8B8 stores the post-blend alpha.
                const uint32_t stored_alpha_byte =
                    color_format_x8 ? 0xFFu : pack_unorm8(out_alpha);
                const uint32_t written_r =
                    color_mask_write[0] ? pack_unorm8(out_rgb[0])
                                        : static_cast<uint32_t>(dst[0] * 255.0f + 0.5f);
                const uint32_t written_g =
                    color_mask_write[1] ? pack_unorm8(out_rgb[1])
                                        : static_cast<uint32_t>(dst[1] * 255.0f + 0.5f);
                const uint32_t written_b =
                    color_mask_write[2] ? pack_unorm8(out_rgb[2])
                                        : static_cast<uint32_t>(dst[2] * 255.0f + 0.5f);
                const uint32_t written_a =
                    color_mask_write[3] ? stored_alpha_byte
                                        : static_cast<uint32_t>(dst[3] * 255.0f + 0.5f);
                const uint32_t color = (written_a << 24) | (written_r << 16) |
                                       (written_g << 8) | written_b;
                if (!color_surface.write32(offset, color)) {
                    fault_("NV2A generic-combiner color DMA write failed");
                    raster_failed = true;
                    return;
                }
                if (uses_depth && raster_state[kRasterDepthMask] != 0) {
                    const uint32_t depth_stencil =
                        (incoming_depth << 8) | (stored_depth_stencil & 0xFFu);
                    if (!depth_surface.write32(offset, depth_stencil)) {
                        fault_("NV2A generic-combiner zeta DMA write failed");
                        raster_failed = true;
                        return;
                    }
                }
                // Stage (b2) applied to the generic path: the write census and the FNV
                // fold are gated on collect_diagnostics. The last-written-address store
                // is NOT gated and must not be: it is reported unconditionally, and in
                // the canonical noperf replay gating it changed the report from
                // last=03d20840 to last=0. Restored to the baseline structure
                // (nv2a.cpp:9727-9736 at HEAD 93d006d): counter gated, address store
                // bare, fold gated. The generic combiner is NOT specialized: it has one
                // decoded path, no profile ladder, and its per-fragment counters were
                // already gated.
                if (collect_diagnostics) ++generic_combiner_writes_;
                const uint32_t address =
                    color_address + static_cast<uint32_t>(offset);
                last_generic_combiner_raster_address_ = address;
                if (collect_diagnostics) {
                    for (uint32_t word : {address, color}) {
                        generic_combiner_output_hash_ ^= word;
                        generic_combiner_output_hash_ *= 1099511628211ull;
                    }
                }
            }
        }
    };

    std::chrono::steady_clock::time_point generic_software_raster_started_at;
    if (profile_generic_fingerprint)
        generic_software_raster_started_at = std::chrono::steady_clock::now();
    if (generic_fallback_timer) generic_fallback_timer.reset();
    // The primitive dispatch is a lambda because the generated-shader executor
    // may have to run it TWICE: once collecting, and -- only if the collection
    // declined -- again rasterizing in software after the resolve it skipped.
    // It is the SAME loop in the same order both times, which is what makes
    // the fallback structurally identical to never having tried.
    // Select before winding correction and clipping; a clipped-away provoking
    // vertex still supplies the primitive's colors. This slow floor intentionally
    // leaves texture coordinates/positions untouched. Native production uses GS.
    const auto submit_shaded_triangle = [&](const TransformedVertex& a,
                                           const TransformedVertex& b,
                                           const TransformedVertex& c,
                                           const TransformedVertex& provoking) {
        if (!flat_shading) { rasterize_triangle(a, b, c); return; }
        TransformedVertex copies[3] = {a, b, c};
        for (auto& vertex : copies) {
            vertex.diffuse = provoking.diffuse;
            vertex.specular = provoking.specular;
        }
        rasterize_triangle(copies[0], copies[1], copies[2]);
    };
    const auto run_primitives = [&]() {
        std::optional<perf::ScopedAdd> software_raster_timer;
        if (perf::g_enabled && !gpu_shader_collect) {
            software_raster_timer.emplace(perf::g_ns_generic_software_raster,
                                          perf::g_calls_generic_software_raster);
        }
        const size_t vertex_count = transformed_batch.size();
        const bool first = raster_state[kRasterProvokingVertex] == 1;
        if (primitive_mode == 5) {
            for (size_t i = 0; i + 2 < vertex_count; i += 3)
                submit_shaded_triangle(transformed_batch[i],
                                  transformed_batch[i + 1],
                                  transformed_batch[i + 2], transformed_batch[i + (first ? 0 : 2)]);
        } else if (primitive_mode == 6) {
            for (size_t i = 2; i < vertex_count; ++i) {
                if ((i & 1u) == 0)
                    submit_shaded_triangle(transformed_batch[i - 2],
                                      transformed_batch[i - 1],
                                      transformed_batch[i], transformed_batch[first ? i - 2 : i]);
                else
                    submit_shaded_triangle(transformed_batch[i - 1],
                                      transformed_batch[i - 2],
                                      transformed_batch[i], transformed_batch[first ? i - 2 : i]);
            }
        } else {
            for (size_t i = 2; i < vertex_count; ++i)
                submit_shaded_triangle(transformed_batch[0],
                                  transformed_batch[i - 1],
                                  transformed_batch[i], transformed_batch[first ? i - 1 : i]);
        }
    };
#if XLLE_VSH_DRAW_OVERLAP_CENSUS
    const bool collect_overlap = gpu_shader_collect && vsh_draw_overlap_enabled_ &&
                                 job.vsh_overlap.valid;
    const uint64_t overlap_input_triangles = collect_overlap
        ? vsh_draw_overlap_triangle_candidates(primitive_mode, transformed_batch.size()) : 0;
    const uint64_t overlap_t0 = collect_overlap && perf_excl::g_enabled
        ? perf_excl::tick_now() : 0;
#endif
    run_primitives();
#if XLLE_VSH_DRAW_OVERLAP_CENSUS
    if (collect_overlap) {
        const uint64_t raw = overlap_t0 ? perf_excl::tick_now() - overlap_t0 : 0;
        VshDrawOverlapJob& overlap = job.vsh_overlap;
        ++overlap.generated_setup_calls;
        overlap.primitive_input_triangles += overlap_input_triangles;
        const uint64_t collected =
            static_cast<uint64_t>(gpu_shader_packet.triangles.size());
        overlap.primitive_collected_triangles += collected;
        if (gpu_shader_collect_failed || raster_failed) {
            ++overlap.primitive_refused_batches;
            if (gpu_shader_collect_nonfinite)
                ++overlap.primitive_nonfinite_batches;
        }
        if (overlap_t0) {
            overlap.generated_setup_raw_ticks += raw;
            overlap.generated_setup_compensated_ticks +=
                perf_excl::compensated_delta(raw, overlap.discarded_deltas);
        }
    }
#endif
    if (gpu_shader_collect) {
        bool submitted = false;
        if (!gpu_shader_collect_failed && !raster_failed &&
            !gpu_shader_packet.triangles.empty() &&
            !renderer_->prepare_generated_shader(gpu_shader_packet)) {
            gpu_shader_collect_failed = true;
        }
        if (!gpu_shader_collect_failed && !raster_failed) {
            if (gpu_shader_packet.triangles.empty()) {
                // Every triangle was culled or degenerate. The software path
                // would have written nothing either, so this is a completed
                // batch, not a decline -- and crucially it must NOT resolve.
                submitted = true;
#if XLLE_VSH_DRAW_OVERLAP_CENSUS
                job.vsh_overlap.outcome = VshDrawOverlapOutcome::kGeneratedEmpty;
#endif
                ++generic_shader_empty_batches_;
            } else {
                // ---- differential verification (diagnostic) --------------
                // Run the software path on the SAME collected geometry and the
                // SAME starting image, then run the GPU path from that image,
                // and report the per-pixel difference. Both halves start from
                // `before`, so nothing about residency history can confuse the
                // comparison.
                const bool verify =
                    gpu_generic_shader_verify_ != 0 &&
                    gpu_generic_shader_verified_ < gpu_generic_shader_verify_;
                if (verify) {
                    ++gpu_generic_shader_verified_;
                    const size_t bytes = static_cast<size_t>(color_bytes64);
                    if (!resolve_renderer_color_overlap(
                            color_address, bytes, "software generic color surface")) {
                        record_generic_fingerprint_perf();
                        return false;
                    }
                    generic_shader_verify_before_.resize(bytes);
                    if (!mem_read_ ||
                        !mem_read_(color_address,
                                   generic_shader_verify_before_.data(), bytes)) {
                        fault_("NV2A generated-shader verify read failed");
                        return false;
                    }
                    gpu_shader_collect = false;
                    color_surface.reset_written_span();
                    run_primitives();
                    if (!color_surface.flush()) {
                        fault_("NV2A generated-shader verify software flush failed");
                        return false;
                    }
                    generic_shader_verify_soft_.resize(bytes);
                    if (!mem_read_(color_address,
                                   generic_shader_verify_soft_.data(), bytes)) {
                        fault_("NV2A generated-shader verify read failed");
                        return false;
                    }
                    // Put the pre-draw image back and drop residency so the
                    // GPU path starts from exactly the same bytes.
                    if (!mem_write_ ||
                        !mem_write_(color_address,
                                    generic_shader_verify_before_.data(), bytes)) {
                        fault_("NV2A generated-shader verify restore failed");
                        return false;
                    }
                    invalidate_renderer_color_overlap(
                        color_address, bytes,
                        nv2a_invalidate_token(kNv2aInvalidateGenericSoftware));
                    gpu_shader_packet.needs_color_upload = true;
                    gpu_shader_packet.color_input =
                        generic_shader_verify_before_;
                    gpu_shader_collect = true;
                }
                Nv2aGeneratedShaderResult gpu_result;
                std::string gpu_error;
                if (!renderer_->submit_generated_shader(gpu_shader_packet,
                                                        gpu_result,
                                                        gpu_error)) {
                    fault_(gpu_error.empty()
                               ? "NV2A generated-shader submission failed"
                               : gpu_error.c_str());
                    record_generic_fingerprint_perf();
                    return false;
                }
                submitted = true;
#if XLLE_VSH_DRAW_OVERLAP_CENSUS
                job.vsh_overlap.outcome = VshDrawOverlapOutcome::kGeneratedGpu;
#endif
                ++generic_shader_submits_;
                generic_shader_triangles_ += gpu_result.triangles;
                generic_shader_color_upload_bytes_ +=
                    gpu_result.color_upload_bytes;
                generic_shader_depth_upload_bytes_ +=
                    gpu_result.depth_upload_bytes;
                generic_shader_texture_upload_bytes_ +=
                    gpu_result.texture_upload_bytes;
                if (generic_shader_row_) ++generic_shader_row_->gpu_batches;
                if (verify) {
                    const size_t bytes = static_cast<size_t>(color_bytes64);
                    if (!resolve_renderer_color_overlap(
                            color_address, bytes,
                            "software generic color surface")) {
                        record_generic_fingerprint_perf();
                        return false;
                    }
                    generic_shader_verify_gpu_.resize(bytes);
                    if (!mem_read_(color_address,
                                   generic_shader_verify_gpu_.data(), bytes)) {
                        fault_("NV2A generated-shader verify read failed");
                        return false;
                    }
                    uint64_t differing = 0;
                    uint32_t maxd = 0;
                    char samples[512];
                    size_t used = 0;
                    samples[0] = '\0';
                    const uint32_t row_words = pitch / 4u;
                    for (size_t i = 0; i + 3 < bytes; i += 4) {
                        uint32_t sw = 0, gp = 0;
                        std::memcpy(&sw, generic_shader_verify_soft_.data() + i,
                                    4);
                        std::memcpy(&gp, generic_shader_verify_gpu_.data() + i,
                                    4);
                        if (sw == gp) continue;
                        ++differing;
                        for (int c = 0; c < 4; ++c) {
                            const int32_t d =
                                static_cast<int32_t>((sw >> (c * 8)) & 0xFF) -
                                static_cast<int32_t>((gp >> (c * 8)) & 0xFF);
                            const uint32_t ad = d < 0 ? -d : d;
                            if (ad > maxd) maxd = ad;
                        }
                        if (differing <= 4 && used + 96 < sizeof(samples)) {
                            const size_t px = i / 4u;
                            const int n = std::snprintf(
                                samples + used, sizeof(samples) - used,
                                " (%zu,%zu sw=%08x gpu=%08x)",
                                row_words ? px % row_words : px,
                                row_words ? px / row_words : 0, sw, gp);
                            if (n > 0) used += static_cast<size_t>(n);
                        }
                    }
                    std::fprintf(
                        stderr,
                        "[host] generic-shader-verify batch=%u fp=%016llx "
                        "tris=%zu differing=%llu maxdelta=%u blend=%u/%04x/%04x "
                        "x8=%u samples:%s\n",
                        gpu_generic_shader_verified_,
                        (unsigned long long)gpu_shader_packet.fingerprint,
                        gpu_shader_packet.triangles.size(),
                        (unsigned long long)differing, maxd,
                        gpu_shader_packet.state.blend ? 1u : 0u,
                        gpu_shader_packet.blend_src, gpu_shader_packet.blend_dst,
                        gpu_shader_packet.state.color_format_x8 ? 1u : 0u,
                        samples);
                }
            }
        }
        if (submitted) {
            // The batch was already counted as a software fallback at the top
            // of this section (that counter means "the hand-written fixtures
            // declined it"), so move it to the GPU column now rather than
            // leaving the census claiming CPU work that did not happen. The
            // seam note is NOT repeated: the fallback path already marked this
            // job inline-only, which is what a GL submission needs too.
            if (generic_shader_row_ && generic_shader_row_->software_batches)
                --generic_shader_row_->software_batches;
            ++generic_combiner_executed_batches_;
            record_generic_fingerprint_perf();
            return true;
        }
        // DECLINED after collection. Nothing has been submitted and nothing
        // has been written, so the software path runs from the top of the
        // primitive loop with the resolve it skipped performed first.
        record_generic_shader_decline(
            raster_failed ? GenericShaderDeclineReason::kRasterFailed
                          : GenericShaderDeclineReason::kTriangleUnsupported);
        gpu_shader_collect = false;
        gpu_shader_packet.triangles.clear();
        raster_failed = false;
#if XLLE_VSH_DRAW_OVERLAP_CENSUS
        job.vsh_overlap.outcome = VshDrawOverlapOutcome::kGenericCpu;
#endif
        {
            perf::ScopedAdd timer(perf::g_ns_generic_fallback_color_resolve,
                                  perf::g_calls_generic_fallback_color_resolve);
            const uint64_t transfers_before = seam_resolve_transfers_;
            if (!resolve_renderer_color_overlap(
                    color_address, static_cast<size_t>(color_bytes64),
                    "software generic color surface")) {
                record_generic_fingerprint_perf();
                return false;
            }
            if (generic_shader_row_) {
                generic_shader_row_->color_resolves +=
                    seam_resolve_transfers_ - transfers_before;
            }
        }
        if (uses_depth) {
            perf::ScopedAdd timer(perf::g_ns_generic_fallback_depth_resolve,
                                  perf::g_calls_generic_fallback_depth_resolve);
            if (!resolve_renderer_depth_overlap(
                    depth_address, static_cast<size_t>(depth_bytes64),
                    "software generic zeta surface")) {
                record_generic_fingerprint_perf();
                return false;
            }
        }
        run_primitives();
    }
    if (profile_generic_fingerprint) {
        generic_batch_software_raster_ns = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() -
                generic_software_raster_started_at)
                .count());
    }

    if (raster_failed) {
        record_generic_fingerprint_perf();
        return false;
    }
    std::chrono::steady_clock::time_point generic_flush_started_at;
    if (profile_generic_fingerprint)
        generic_flush_started_at = std::chrono::steady_clock::now();
    {
        std::optional<perf::ScopedAdd> flush_timer;
        if (perf::g_enabled) {
            flush_timer.emplace(perf::g_ns_generic_flush,
                                perf::g_calls_generic_flush);
        }
        if (!color_surface.flush()) {
            fault_("NV2A generic-combiner color DMA write failed");
            record_generic_fingerprint_perf();
            return false;
        }
        if (surface_pitch_linear) {
            // Linear targets: refresh the exact resident surface's covered
            // rows from guest memory, keeping residency instead of forcing
            // the next accelerated draw to re-upload the whole surface.
            if (!refresh_resident_color_after_software_write(
                    color_address, static_cast<size_t>(color_bytes64),
                    surface_pitch, pitch, surface_height, surface_format,
                    color_surface.written_begin(),
                    color_surface.written_end())) {
                record_generic_fingerprint_perf();
                return false;
            }
        } else {
            invalidate_renderer_color_overlap(
                color_address, static_cast<size_t>(color_bytes64),
                nv2a_invalidate_token(kNv2aInvalidateGenericSoftware));
        }
        if (uses_depth && !depth_surface.flush()) {
            fault_("NV2A generic-combiner zeta DMA write failed");
            record_generic_fingerprint_perf();
            return false;
        }
    }
    if (profile_generic_fingerprint) {
        generic_batch_flush_ns = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - generic_flush_started_at)
                .count());
    }
    if (uses_depth && raster_state[kRasterDepthMask] != 0)
        invalidate_renderer_depth_overlap(
            depth_address, static_cast<size_t>(depth_bytes64),
            nv2a_invalidate_token(kNv2aInvalidateGenericSoftware));
    if (debug_packet_snapshot) {
        debug_packet_snapshot->color_output.resize(static_cast<size_t>(color_bytes64));
        if (!mem_read_ ||
            !mem_read_(color_address, debug_packet_snapshot->color_output.data(),
                       debug_packet_snapshot->color_output.size())) {
            fault_("NV2A debug generic packet color output read failed");
            record_generic_fingerprint_perf();
            return false;
        }
        if (uses_depth) {
            debug_packet_snapshot->depth_output.resize(
                static_cast<size_t>(depth_bytes64));
            if (!mem_read_ ||
                !mem_read_(depth_address,
                           debug_packet_snapshot->depth_output.data(),
                           debug_packet_snapshot->depth_output.size())) {
                fault_("NV2A debug generic packet zeta output read failed");
                record_generic_fingerprint_perf();
                return false;
            }
        }
        if (!generic_packet_snapshot_cb_(debug_packet_snapshot_info,
                                         *debug_packet_snapshot)) {
            fault_("NV2A debug generic packet snapshot callback failed");
            record_generic_fingerprint_perf();
            return false;
        }
    }
    if (generic_batch_debug_cb_) {
        GenericBatchDebugInfo info;
        info.batch = generic_batch_index;
        info.fragments = generic_combiner_fragments_ - generic_fragments_before;
        info.writes = generic_combiner_writes_ - generic_writes_before;
        info.fingerprint = generic_batch_fingerprint;
        info.full_state_hash = generic_batch_full_hash;
        info.color_address = color_address;
        info.pitch = pitch;
        info.width = pitch / 4u;
        info.height = surface_height;
        info.surface_format = surface_format;
        info.shader_stage_program = raster_state[kRasterShaderStageProgram];
        info.combiner_control = raster_state[kRasterCombinerControl];
        info.alpha_func = raster_state[kRasterAlphaFunc];
        info.alpha_ref = raster_state[kRasterAlphaRef] & 0xFFu;
        info.blend_src = raster_state[kRasterBlendSrc];
        info.blend_dst = raster_state[kRasterBlendDst];
        info.blend_equation = raster_state[kRasterBlendEquation];
        info.depth_func = raster_state[kRasterDepthFunc];
        info.depth_mask = raster_state[kRasterDepthMask];
        info.texture_enable_mask = raster_state[kRasterTextureEnableMask];
        info.primitive_mode = primitive_mode;
        info.vertex_count = static_cast<uint32_t>(std::min<size_t>(
            transformed_batch.size(), std::numeric_limits<uint32_t>::max()));
        info.texture_modulate_observed = texture_modulate_observed_this_batch;
        info.texture_modulate_exact = texture_modulate_exact_this_batch;
        info.texture_modulate_reject =
            texture_modulate_reject_this_batch == GenericTextureModulateRejectReason::kCount
                ? UINT32_MAX
                : static_cast<uint32_t>(texture_modulate_reject_this_batch);
        generic_batch_debug_cb_(info);
    }
    if (surface_write_debug_cb_) {
        SurfaceWriteDebugInfo info;
        info.kind = "generic";
        info.sequence = generic_batch_index;
        info.fragments = generic_combiner_fragments_ - generic_fragments_before;
        info.writes = generic_combiner_writes_ - generic_writes_before;
        info.fingerprint = generic_batch_fingerprint;
        info.full_state_hash = generic_batch_full_hash;
        info.color_address = color_address;
        info.pitch = pitch;
        info.width = pitch / 4u;
        info.height = surface_height;
        info.surface_format = surface_format;
        info.shader_stage_program = raster_state[kRasterShaderStageProgram];
        info.combiner_control = raster_state[kRasterCombinerControl];
        info.alpha_func = raster_state[kRasterAlphaFunc];
        info.alpha_ref = raster_state[kRasterAlphaRef] & 0xFFu;
        info.blend_src = raster_state[kRasterBlendSrc];
        info.blend_dst = raster_state[kRasterBlendDst];
        info.blend_equation = raster_state[kRasterBlendEquation];
        info.depth_func = raster_state[kRasterDepthFunc];
        info.depth_mask = raster_state[kRasterDepthMask];
        info.texture_enable_mask = raster_state[kRasterTextureEnableMask];
        info.primitive_mode = primitive_mode;
        info.vertex_count = static_cast<uint32_t>(std::min<size_t>(
            transformed_batch.size(), std::numeric_limits<uint32_t>::max()));
        surface_write_debug_cb_(info);
    }
#if XLLE_VSH_DRAW_OVERLAP_CENSUS
    if (job.vsh_overlap.outcome == VshDrawOverlapOutcome::kUnknown)
        job.vsh_overlap.outcome = VshDrawOverlapOutcome::kGenericCpu;
#endif
    ++generic_combiner_executed_batches_;
    record_generic_fingerprint_perf();
    return true;
}

Nv2a::ClassMethodStat Nv2a::class_method_stat(uint8_t object_class,
                                               uint32_t method) const {
    if (method >= 0x2000 || (method & 3u)) return {};
    size_t slot = 0;
    if (object_class == 0x62) {
        slot = 0;
    } else if (object_class == 0x9F) {
        slot = 1;
    } else if (object_class == 0x44) {
        slot = 2;
    } else {
        return {};
    }
    const size_t method_index = method >> 2;
    ClassMethodStat stat;
    stat.count = class2d_method_stats_[slot].counts[method_index];
    stat.first_parameter = class2d_method_stats_[slot].first_parameters[method_index];
    stat.last_parameter = class2d_method_stats_[slot].last_parameters[method_index];
    return stat;
}

bool Nv2a::execute_image_blit_srccopy() {
    if (image_blit_.operation != 3) return true;
    if (!image_blit_.context_surfaces_bound ||
        image_blit_.context_surfaces != context_surfaces_2d_.object_instance) {
        fault_("NV2A IMAGE_BLIT SRCCOPY has unbound/mismatched CONTEXT_SURFACES_2D");
        return false;
    }
    if (!context_surfaces_2d_.dma_source_bound ||
        !context_surfaces_2d_.dma_dest_bound) {
        fault_("NV2A IMAGE_BLIT SRCCOPY has unbound source/dest DMA");
        return false;
    }
    uint32_t bytes_per_pixel = 0;
    switch (context_surfaces_2d_.color_format) {
    case 0x01: bytes_per_pixel = 1; break;  // LE_Y8
    case 0x04: bytes_per_pixel = 2; break;  // LE_R5G6B5
    case 0x0A: bytes_per_pixel = 4; break;  // LE_A8R8G8B8
    default: {
        char why[128];
        std::snprintf(why, sizeof(why),
                      "NV2A IMAGE_BLIT SRCCOPY unsupported color format 0x%08x",
                      context_surfaces_2d_.color_format);
        fault_(why);
        return false;
    }
    }
    if (image_blit_.width == 0 || image_blit_.height == 0) return true;
    const uint64_t row_bytes64 =
        static_cast<uint64_t>(image_blit_.width) * bytes_per_pixel;
    if (row_bytes64 > std::numeric_limits<uint32_t>::max()) {
        fault_("NV2A IMAGE_BLIT SRCCOPY row is too wide");
        return false;
    }
    const uint32_t row_bytes = static_cast<uint32_t>(row_bytes64);
    if (row_bytes > context_surfaces_2d_.source_pitch ||
        row_bytes > context_surfaces_2d_.dest_pitch) {
        fault_("NV2A IMAGE_BLIT SRCCOPY row exceeds source/dest pitch");
        return false;
    }
    const uint64_t source_offset64 = static_cast<uint64_t>(
        context_surfaces_2d_.source_offset) +
        static_cast<uint64_t>(image_blit_.in_y) * context_surfaces_2d_.source_pitch +
        static_cast<uint64_t>(image_blit_.in_x) * bytes_per_pixel;
    const uint64_t dest_offset64 = static_cast<uint64_t>(
        context_surfaces_2d_.dest_offset) +
        static_cast<uint64_t>(image_blit_.out_y) * context_surfaces_2d_.dest_pitch +
        static_cast<uint64_t>(image_blit_.out_x) * bytes_per_pixel;
    const uint64_t source_span64 =
        static_cast<uint64_t>(image_blit_.height - 1) *
            context_surfaces_2d_.source_pitch + row_bytes;
    const uint64_t dest_span64 =
        static_cast<uint64_t>(image_blit_.height - 1) *
            context_surfaces_2d_.dest_pitch + row_bytes;
    if (source_offset64 > std::numeric_limits<uint32_t>::max() ||
        dest_offset64 > std::numeric_limits<uint32_t>::max() ||
        source_offset64 + source_span64 - 1 > std::numeric_limits<uint32_t>::max() ||
        dest_offset64 + dest_span64 - 1 > std::numeric_limits<uint32_t>::max()) {
        fault_("NV2A IMAGE_BLIT SRCCOPY offset range overflows");
        return false;
    }
    uint32_t source_address = 0, source_last = 0;
    uint32_t dest_address = 0, dest_last = 0;
    if (!dma_object_address(context_surfaces_2d_.dma_source,
                            static_cast<uint32_t>(source_offset64),
                            source_address) ||
        !dma_object_address(context_surfaces_2d_.dma_source,
                            static_cast<uint32_t>(source_offset64 +
                                                  source_span64 - 1),
                            source_last) ||
        !dma_object_address(context_surfaces_2d_.dma_dest,
                            static_cast<uint32_t>(dest_offset64),
                            dest_address) ||
        !dma_object_address(context_surfaces_2d_.dma_dest,
                            static_cast<uint32_t>(dest_offset64 +
                                                  dest_span64 - 1),
                            dest_last)) {
        fault_("NV2A IMAGE_BLIT SRCCOPY DMA range is invalid");
        return false;
    }
    if (source_last < source_address || dest_last < dest_address ||
        static_cast<uint64_t>(source_last) - source_address != source_span64 - 1 ||
        static_cast<uint64_t>(dest_last) - dest_address != dest_span64 - 1) {
        fault_("NV2A IMAGE_BLIT SRCCOPY DMA range is non-contiguous");
        return false;
    }
    if (!resolve_renderer_color_overlap(source_address, static_cast<size_t>(source_span64),
                                        "IMAGE_BLIT source") ||
        !resolve_renderer_color_overlap(dest_address, static_cast<size_t>(dest_span64),
                                        "IMAGE_BLIT destination") ||
        !resolve_renderer_depth_overlap(source_address, static_cast<size_t>(source_span64),
                                        "IMAGE_BLIT source") ||
        !resolve_renderer_depth_overlap(dest_address, static_cast<size_t>(dest_span64),
                                        "IMAGE_BLIT destination")) {
        return false;
    }
    std::vector<uint8_t> row(row_bytes);
    for (uint32_t y = 0; y < image_blit_.height; ++y) {
        const uint32_t src = source_address + y * context_surfaces_2d_.source_pitch;
        const uint32_t dst = dest_address + y * context_surfaces_2d_.dest_pitch;
        if (!mem_read_ || !mem_read_(src, row.data(), row.size())) {
            char why[128];
            std::snprintf(why, sizeof(why),
                          "NV2A IMAGE_BLIT SRCCOPY read failed at 0x%08x", src);
            fault_(why);
            return false;
        }
        if (!mem_write_ || !mem_write_(dst, row.data(), row.size())) {
            char why[128];
            std::snprintf(why, sizeof(why),
                          "NV2A IMAGE_BLIT SRCCOPY write failed at 0x%08x", dst);
            fault_(why);
            return false;
        }
    }
    invalidate_renderer_color_overlap(
        dest_address, static_cast<size_t>(dest_span64),
        nv2a_invalidate_token(kNv2aInvalidateImageBlit));
    invalidate_renderer_depth_overlap(
        dest_address, static_cast<size_t>(dest_span64),
        nv2a_invalidate_token(kNv2aInvalidateImageBlit));
    ++image_blit_copies_;
    image_blit_bytes_ += static_cast<uint64_t>(row_bytes) * image_blit_.height;
    last_image_blit_source_ = source_address;
    last_image_blit_dest_ = dest_address;
    last_image_blit_size_ =
        (static_cast<uint32_t>(image_blit_.height) << 16) |
        (static_cast<uint32_t>(image_blit_.width) & 0xFFFFu);
    return true;
}

namespace {

// Stage (a) of beads-xbox.1.15.1: the NV097 (class 0x97) method dispatch table.
//
// dispatch_method used to test 126 `if / else if (method ...)` arms in source order,
// so the hottest method in the canonical run -- 0x1800 ARRAY_ELEMENT16, 76.9 % of the
// 83.3 M dispatched method words -- reached its body only after ~120 predicted-
// not-taken compares.  The arms below are that chain, transcribed in the *same order*,
// as (begin, end, handler) triples over the half-open method range each arm accepted.
// build_nv097_table() replays them first-match-wins into a 2048-entry byte table indexed
// by method >> 2, which is exactly the index method_counts_ already uses, so a method
// now reaches its handler with one table load plus one indirect branch.  Because the
// replay is first-match-wins in chain order, an overlapping pair of arms keeps the
// earlier arm's body, matching the if/else-if chain by construction.
//
// Arm catalogue (chain order; `M` = single method, `R` = half-open method range):
//
//   #    methods                 handler                     kind
//   1    0x0100                  kNv097M0100                 exact + guard (parameter != 0)
//   2    0x0120                  kNv097M0120                 exact
//   3    0x0124                  kNv097M0124                 exact
//   4    0x0128                  kNv097M0128                 exact
//   5    0x012C                  kNv097M012C                 exact
//   6    0x0130                  kNv097M0130                 exact
//   7    0x0184                  kNv097M0184                 exact
//   8    0x0188                  kNv097M0188                 exact
//   9    0x0194                  kNv097M0194                 exact
//   10   0x0198                  kNv097M0198                 exact
//   11   0x019C                  kNv097M019C                 exact
//   12   0x01A0                  kNv097M01A0                 exact
//   13   0x0200                  kNv097M0200                 exact
//   14   0x0204                  kNv097M0204                 exact
//   15   0x0208                  kNv097M0208                 exact
//   16   0x020C                  kNv097M020C                 exact
//   17   0x0210                  kNv097M0210                 exact
//   18   0x0214                  kNv097M0214                 exact
//   19   0x1D8C                  kNv097M1D8C                 exact
//   20   0x1D90                  kNv097M1D90                 exact
//   21   0x1D98                  kNv097M1D98                 exact
//   22   0x1D9C                  kNv097M1D9C                 exact
//   23   0x1D94                  kNv097M1D94                 exact
//   24   0x0260..0x027C          kNv097R0260_0280            range, 8 methods
//   25   0x0288                  kNv097M0288                 exact
//   26   0x028C                  kNv097M028C                 exact
//   27   0x0290                  kNv097M0290                 exact
//   28   0x02C0                  kNv097M02C0                 exact
//   29   0x02E0                  kNv097M02E0                 exact
//   30   0x0300                  kNv097M0300                 exact
//   31   0x0304                  kNv097M0304                 exact
//   32   0x0308                  kNv097M0308                 exact
//   33   0x030C                  kNv097M030C                 exact
//   34   0x0310                  kNv097M0310                 exact
//   35   0x0318                  kNv097M0318                 exact
//   36   0x031C                  kNv097M031C                 exact
//   37   0x0320                  kNv097M0320                 exact
//   38   0x0324                  kNv097M0324                 exact
//   39   0x032C                  kNv097M032C                 exact
//   40   0x0330                  kNv097M0330                 exact
//   41   0x0334                  kNv097M0334                 exact
//   42   0x0338                  kNv097M0338                 exact
//   43   0x033C                  kNv097M033C                 exact
//   44   0x0340                  kNv097M0340                 exact
//   45   0x0344                  kNv097M0344                 exact
//   46   0x0348                  kNv097M0348                 exact
//   47   0x034C                  kNv097M034C                 exact
//   48   0x0350                  kNv097M0350                 exact
//   49   0x0354                  kNv097M0354                 exact
//   50   0x0358                  kNv097M0358                 exact
//   51   0x035C                  kNv097M035C                 exact
//   52   0x0360                  kNv097M0360                 exact
//   53   0x0364                  kNv097M0364                 exact
//   54   0x0368                  kNv097M0368                 exact
//   55   0x036C                  kNv097M036C                 exact
//   56   0x0370                  kNv097M0370                 exact
//   57   0x0374                  kNv097M0374                 exact
//   58   0x0378                  kNv097M0378                 exact
//   59   0x037C                  kNv097M037C                 exact
//   60   0x0384                  kNv097M0384                 exact
//   61   0x0388                  kNv097M0388                 exact
//   62   0x038C                  kNv097M038C                 exact
//   63   0x0390                  kNv097M0390                 exact
//   64   0x0394                  kNv097M0394                 exact
//   65   0x0398                  kNv097M0398                 exact
//   66   0x039C                  kNv097M039C                 exact
//   67   0x03A0                  kNv097M03A0                 exact
//   68   0x17BC                  kNv097M17BC                 exact
//   69   0x17C0                  kNv097M17C0                 exact
//   70   0x1D78                  kNv097M1D78                 exact
//   71   0x1E20                  kNv097M1E20                 exact
//   72   0x1E24                  kNv097M1E24                 exact
//   73   0x1E40..0x1E5C          kNv097R1E40_1E60            range, 8 methods
//   74   0x1E60                  kNv097M1E60                 exact
//   75   0x1E6C                  kNv097M1E6C                 exact
//   76   0x1E70                  kNv097M1E70                 exact
//   77   0x1E74                  kNv097M1E74                 exact
//   78   0x1E78                  kNv097M1E78                 exact
//   79   0x1B00..0x1BFC          kNv097R1B00_1C00            range, 64 methods
//   80   0x0294                  kNv097M0294                 exact
//   81   0x0298                  kNv097M0298                 exact
//   82   0x02A4                  kNv097M02A4                 exact
//   83   0x0314                  kNv097M0314                 exact
//   84   0x0328                  kNv097M0328                 exact
//   85   0x03A4                  kNv097M03A4                 exact
//   86   0x03A8..0x03B0          kNv097R03A8_03B4            range, 3 methods
//   87   0x03B4                  kNv097M03B4                 exact
//   88   0x03B8                  kNv097M03B8                 exact
//   89   0x03BC                  kNv097M03BC                 exact
//   90   0x03C0..0x03FC          kNv097R03C0_0400            range, 16 methods
//   91   0x0420..0x042C          kNv097R0420_0430            range, 4 methods
//   92   0x0440..0x047C          kNv097R0440_0480            range, 16 methods
//   93   0x0480..0x057C          kNv097R0480_0580            range, 64 methods
//   94   0x0580..0x067C          kNv097R0580_0680            range, 64 methods
//   95   0x0680..0x06BC          kNv097R0680_06C0            range, 16 methods
//   96   0x06C0..0x07BC          kNv097R06C0_07C0            range, 64 methods
//   97   0x0840..0x093C          kNv097R0840_0940            range, 64 methods
//   98   0x0A10..0x0A18          kNv097R0A10_0A1C            range, 3 methods
//   99   0x0A20..0x0A2C          kNv097R0A20_0A30            range, 4 methods
//   100  0x0A60..0x0A7C          kNv097R0A60_0A80            range, 8 methods
//   101  0x0A80..0x0A9C          kNv097R0A80_0AA0            range, 8 methods
//   102  0x0AA0..0x0ABC          kNv097R0AA0_0AC0            range, 8 methods
//   103  0x0AC0..0x0ADC          kNv097R0AC0_0AE0            range, 8 methods
//   104  0x0AF0..0x0AFC          kNv097R0AF0_0B00            range, 4 methods
//   105  0x0B00..0x0B7C          kNv097R0B00_0B80            range, 32 methods
//   106  0x0B80..0x0BFC          kNv097R0B80_0C00            range, 32 methods
//   107  0x1E94                  kNv097M1E94                 exact
//   108  0x1E9C                  kNv097M1E9C                 exact
//   109  0x1EA0                  kNv097M1EA0                 exact
//   110  0x1EA4                  kNv097M1EA4                 exact
//   111  0x1518..0x1524          kNv097R1518_1528            range, 4 methods
//   112  0x1880..0x18FC          kNv097R1880_1900            range, 32 methods
//   113  0x1900..0x193C          kNv097R1900_1940            range, 16 methods
//   114  0x1940..0x197C          kNv097R1940_1980            range, 16 methods
//   115  0x1980..0x19FC          kNv097R1980_1A00            range, 32 methods
//   116  0x1A00..0x1AFC          kNv097R1A00_1B00            range, 64 methods
//   117  0x1720..0x175C          kNv097R1720_1760            range, 16 methods
//   118  0x1760..0x179C          kNv097R1760_17A0            range, 16 methods
//   119  0x17FC                  kNv097M17FC                 exact
//   120  0x1800                  kNv097M1800                 exact
//   121  0x1808                  kNv097M1808                 exact
//   122  0x1810                  kNv097M1810                 exact
//   123  0x1818                  kNv097M1818                 exact
//   124  0x01A4                  kNv097M01A4                 exact
//   125  0x1D6C                  kNv097M1D6C                 exact
//   126  0x1D70                  kNv097M1D70                 exact
//
// Predicates that are NOT a pure function of `method`: arm 1 only
// (`method == 0x0100 && parameter != 0`).  Its guard is kept inside its case, and
// falling through the guard is a no-op exactly as falling through the whole chain was,
// because no later arm accepts 0x0100.
//
// Methods outside 0x0000-0x1FFC, and methods that are not a multiple of four, matched no
// arm and were silently dropped; the guard in front of the switch keeps that behavior.
// PFIFO cannot produce a misaligned method: the command header forces bits 0-1 of
// ch.method to zero (nv2a.cpp: `ch.method = word & 0x1FFFu` under a 0xE0030003 mask
// test) and an incrementing run only ever adds 4.
enum Nv097Handler : uint8_t {
    kNv097None = 0,  // no arm matched: same silent drop as falling off the old chain
    kNv097M0100,                // method == 0x0100 && parameter != 0
    kNv097M0120,                // method == 0x0120
    kNv097M0124,                // method == 0x0124
    kNv097M0128,                // method == 0x0128
    kNv097M012C,                // method == 0x012C
    kNv097M0130,                // method == 0x0130
    kNv097M0184,                // method == 0x0184
    kNv097M0188,                // method == 0x0188
    kNv097M0194,                // method == 0x0194
    kNv097M0198,                // method == 0x0198
    kNv097M019C,                // method == 0x019C
    kNv097M01A0,                // method == 0x01A0
    kNv097M0200,                // method == 0x0200
    kNv097M0204,                // method == 0x0204
    kNv097M0208,                // method == 0x0208
    kNv097M020C,                // method == 0x020C
    kNv097M0210,                // method == 0x0210
    kNv097M0214,                // method == 0x0214
    kNv097M1D8C,                // method == 0x1D8C
    kNv097M1D90,                // method == 0x1D90
    kNv097M1D98,                // method == 0x1D98
    kNv097M1D9C,                // method == 0x1D9C
    kNv097M1D94,                // method == 0x1D94
    kNv097R0260_0280,           // 0x0260 <= method < 0x0280
    kNv097M0288,                // method == 0x0288
    kNv097M028C,                // method == 0x028C
    kNv097M0290,                // method == 0x0290
    kNv097M02C0,                // method == 0x02C0
    kNv097M02E0,                // method == 0x02E0
    kNv097M0300,                // method == 0x0300
    kNv097M0304,                // method == 0x0304
    kNv097M0308,                // method == 0x0308
    kNv097M030C,                // method == 0x030C
    kNv097M0310,                // method == 0x0310
    kNv097M0318,                // method == 0x0318
    kNv097M031C,                // method == 0x031C
    kNv097M0320,                // method == 0x0320
    kNv097M0324,                // method == 0x0324
    kNv097M032C,                // method == 0x032C
    kNv097M0330,                // method == 0x0330
    kNv097M0334,                // method == 0x0334
    kNv097M0338,                // method == 0x0338
    kNv097M033C,                // method == 0x033C
    kNv097M0340,                // method == 0x0340
    kNv097M0344,                // method == 0x0344
    kNv097M0348,                // method == 0x0348
    kNv097M034C,                // method == 0x034C
    kNv097M0350,                // method == 0x0350
    kNv097M0354,                // method == 0x0354
    kNv097M0358,                // method == 0x0358
    kNv097M035C,                // method == 0x035C
    kNv097M0360,                // method == 0x0360
    kNv097M0364,                // method == 0x0364
    kNv097M0368,                // method == 0x0368
    kNv097M036C,                // method == 0x036C
    kNv097M0370,                // method == 0x0370
    kNv097M0374,                // method == 0x0374
    kNv097M0378,                // method == 0x0378
    kNv097M037C,                // method == 0x037C
    kNv097M09FC,
    kNv097M0384,                // method == 0x0384
    kNv097M0388,                // method == 0x0388
    kNv097M038C,                // method == 0x038C
    kNv097M0390,                // method == 0x0390
    kNv097M0394,                // method == 0x0394
    kNv097M0398,                // method == 0x0398
    kNv097M039C,                // method == 0x039C
    kNv097M03A0,                // method == 0x03A0
    kNv097M17BC,                // method == 0x17BC
    kNv097M17C0,                // method == 0x17C0
    kNv097M1D78,                // method == 0x1D78
    kNv097M1E20,                // method == 0x1E20
    kNv097M1E24,                // method == 0x1E24
    kNv097R1E40_1E60,           // 0x1E40 <= method < 0x1E60
    kNv097M1E60,                // method == 0x1E60
    kNv097M1E6C,                // method == 0x1E6C
    kNv097M1E70,                // method == 0x1E70
    kNv097M1E74,                // method == 0x1E74
    kNv097M1E78,                // method == 0x1E78
    kNv097R1B00_1C00,           // 0x1B00 <= method < 0x1C00
    kNv097M0294,                // method == 0x0294
    kNv097M0298,                // method == 0x0298
    kNv097M02A4,                // method == 0x02A4
    kNv097M0314,                // method == 0x0314
    kNv097M0328,                // method == 0x0328
    kNv097M03A4,                // method == 0x03A4
    kNv097R03A8_03B4,           // 0x03A8 <= method < 0x03B4
    kNv097M03B4,                // method == 0x03B4
    kNv097M03B8,                // method == 0x03B8
    kNv097M03BC,                // method == 0x03BC
    kNv097R03C0_0400,           // 0x03C0 <= method < 0x0400
    kNv097R0420_0430,           // 0x0420 <= method < 0x0430
    kNv097R0440_0480,           // 0x0440 <= method < 0x0480
    kNv097R0480_0580,           // 0x0480 <= method < 0x0580
    kNv097R0580_0680,           // 0x0580 <= method < 0x0680
    kNv097R0680_06C0,           // 0x0680 <= method < 0x06C0
    kNv097R06C0_07C0,           // 0x06C0 <= method < 0x07C0
    kNv097R0840_0940,           // 0x0840 <= method < 0x0940
    kNv097R0A10_0A1C,           // 0x0A10 <= method < 0x0A1C
    kNv097R0A20_0A30,           // 0x0A20 <= method < 0x0A30
    kNv097R0A60_0A80,           // 0x0A60 <= method < 0x0A80
    kNv097R0A80_0AA0,           // 0x0A80 <= method < 0x0AA0
    kNv097R0AA0_0AC0,           // 0x0AA0 <= method < 0x0AC0
    kNv097R0AC0_0AE0,           // 0x0AC0 <= method < 0x0AE0
    kNv097R0AF0_0B00,           // 0x0AF0 <= method < 0x0B00
    kNv097R0B00_0B80,           // 0x0B00 <= method < 0x0B80
    kNv097R0B80_0C00,           // 0x0B80 <= method < 0x0C00
    kNv097M1E94,                // method == 0x1E94
    kNv097M1E9C,                // method == 0x1E9C
    kNv097M1EA0,                // method == 0x1EA0
    kNv097M1EA4,                // method == 0x1EA4
    kNv097R1518_1528,           // 0x1518 <= method < 0x1528
    kNv097R1880_1900,           // 0x1880 <= method < 0x1900
    kNv097R1900_1940,           // 0x1900 <= method < 0x1940
    kNv097R1940_1980,           // 0x1940 <= method < 0x1980
    kNv097R1980_1A00,           // 0x1980 <= method < 0x1A00
    kNv097R1A00_1B00,           // 0x1A00 <= method < 0x1B00
    kNv097R1720_1760,           // 0x1720 <= method < 0x1760
    kNv097R1760_17A0,           // 0x1760 <= method < 0x17A0
    kNv097M17FC,                // method == 0x17FC
    kNv097M1800,                // method == 0x1800
    kNv097M1808,                // method == 0x1808
    kNv097M1810,                // method == 0x1810
    kNv097M1818,                // method == 0x1818
    kNv097M01A4,                // method == 0x01A4
    kNv097M1D6C,                // method == 0x1D6C
    kNv097M1D70,                // method == 0x1D70
    kNv097M01A8, kNv097M17C8, kNv097M17CC, kNv097M17D0, kNv097M0110,
};

constexpr uint32_t kNv097MethodLimit = 0x2000;
constexpr size_t kNv097TableSize = kNv097MethodLimit / 4;

struct Nv097Arm {
    uint32_t begin;   // first method accepted
    uint32_t end;     // one past the last method accepted (half-open, as in the chain)
    uint8_t handler;
};

// The old chain, in chain order.  Do not sort: order is the semantics.
constexpr Nv097Arm kNv097Arms[] = {
    {0x0100, 0x0104, kNv097M0100},
    {0x0120, 0x0124, kNv097M0120},
    {0x0124, 0x0128, kNv097M0124},
    {0x0128, 0x012C, kNv097M0128},
    {0x012C, 0x0130, kNv097M012C},
    {0x0130, 0x0134, kNv097M0130},
    {0x0184, 0x0188, kNv097M0184},
    {0x0188, 0x018C, kNv097M0188},
    {0x0194, 0x0198, kNv097M0194},
    {0x0198, 0x019C, kNv097M0198},
    {0x019C, 0x01A0, kNv097M019C},
    {0x01A0, 0x01A4, kNv097M01A0},
    {0x0200, 0x0204, kNv097M0200},
    {0x0204, 0x0208, kNv097M0204},
    {0x0208, 0x020C, kNv097M0208},
    {0x020C, 0x0210, kNv097M020C},
    {0x0210, 0x0214, kNv097M0210},
    {0x0214, 0x0218, kNv097M0214},
    {0x1D8C, 0x1D90, kNv097M1D8C},
    {0x1D90, 0x1D94, kNv097M1D90},
    {0x1D98, 0x1D9C, kNv097M1D98},
    {0x1D9C, 0x1DA0, kNv097M1D9C},
    {0x1D94, 0x1D98, kNv097M1D94},
    {0x0260, 0x0280, kNv097R0260_0280},
    {0x0288, 0x028C, kNv097M0288},
    {0x028C, 0x0290, kNv097M028C},
    {0x0290, 0x0294, kNv097M0290},
    {0x02C0, 0x02C4, kNv097M02C0},
    {0x02E0, 0x02E4, kNv097M02E0},
    {0x0300, 0x0304, kNv097M0300},
    {0x0304, 0x0308, kNv097M0304},
    {0x0308, 0x030C, kNv097M0308},
    {0x030C, 0x0310, kNv097M030C},
    {0x0310, 0x0314, kNv097M0310},
    {0x0318, 0x031C, kNv097M0318},
    {0x031C, 0x0320, kNv097M031C},
    {0x0320, 0x0324, kNv097M0320},
    {0x0324, 0x0328, kNv097M0324},
    {0x032C, 0x0330, kNv097M032C},
    {0x0330, 0x0334, kNv097M0330},
    {0x0334, 0x0338, kNv097M0334},
    {0x0338, 0x033C, kNv097M0338},
    {0x033C, 0x0340, kNv097M033C},
    {0x0340, 0x0344, kNv097M0340},
    {0x0344, 0x0348, kNv097M0344},
    {0x0348, 0x034C, kNv097M0348},
    {0x034C, 0x0350, kNv097M034C},
    {0x0350, 0x0354, kNv097M0350},
    {0x0354, 0x0358, kNv097M0354},
    {0x0358, 0x035C, kNv097M0358},
    {0x035C, 0x0360, kNv097M035C},
    {0x0360, 0x0364, kNv097M0360},
    {0x0364, 0x0368, kNv097M0364},
    {0x0368, 0x036C, kNv097M0368},
    {0x036C, 0x0370, kNv097M036C},
    {0x0370, 0x0374, kNv097M0370},
    {0x0374, 0x0378, kNv097M0374},
    {0x0378, 0x037C, kNv097M0378},
    {0x037C, 0x0380, kNv097M037C},
    {0x09FC, 0x0A00, kNv097M09FC},
    {0x0384, 0x0388, kNv097M0384},
    {0x0388, 0x038C, kNv097M0388},
    {0x038C, 0x0390, kNv097M038C},
    {0x0390, 0x0394, kNv097M0390},
    {0x0394, 0x0398, kNv097M0394},
    {0x0398, 0x039C, kNv097M0398},
    {0x039C, 0x03A0, kNv097M039C},
    {0x03A0, 0x03A4, kNv097M03A0},
    {0x17BC, 0x17C0, kNv097M17BC},
    {0x17C0, 0x17C4, kNv097M17C0},
    {0x1D78, 0x1D7C, kNv097M1D78},
    {0x1E20, 0x1E24, kNv097M1E20},
    {0x1E24, 0x1E28, kNv097M1E24},
    {0x1E40, 0x1E60, kNv097R1E40_1E60},
    {0x1E60, 0x1E64, kNv097M1E60},
    {0x1E6C, 0x1E70, kNv097M1E6C},
    {0x1E70, 0x1E74, kNv097M1E70},
    {0x1E74, 0x1E78, kNv097M1E74},
    {0x1E78, 0x1E7C, kNv097M1E78},
    {0x1B00, 0x1C00, kNv097R1B00_1C00},
    {0x0294, 0x0298, kNv097M0294},
    {0x0298, 0x029C, kNv097M0298},
    {0x02A4, 0x02A8, kNv097M02A4},
    {0x0314, 0x0318, kNv097M0314},
    {0x0328, 0x032C, kNv097M0328},
    {0x03A4, 0x03A8, kNv097M03A4},
    {0x03A8, 0x03B4, kNv097R03A8_03B4},
    {0x03B4, 0x03B8, kNv097M03B4},
    {0x03B8, 0x03BC, kNv097M03B8},
    {0x03BC, 0x03C0, kNv097M03BC},
    {0x03C0, 0x0400, kNv097R03C0_0400},
    {0x0420, 0x0430, kNv097R0420_0430},
    {0x0440, 0x0480, kNv097R0440_0480},
    {0x0480, 0x0580, kNv097R0480_0580},
    {0x0580, 0x0680, kNv097R0580_0680},
    {0x0680, 0x06C0, kNv097R0680_06C0},
    {0x06C0, 0x07C0, kNv097R06C0_07C0},
    {0x0840, 0x0940, kNv097R0840_0940},
    {0x0A10, 0x0A1C, kNv097R0A10_0A1C},
    {0x0A20, 0x0A30, kNv097R0A20_0A30},
    {0x0A60, 0x0A80, kNv097R0A60_0A80},
    {0x0A80, 0x0AA0, kNv097R0A80_0AA0},
    {0x0AA0, 0x0AC0, kNv097R0AA0_0AC0},
    {0x0AC0, 0x0AE0, kNv097R0AC0_0AE0},
    {0x0AF0, 0x0B00, kNv097R0AF0_0B00},
    {0x0B00, 0x0B80, kNv097R0B00_0B80},
    {0x0B80, 0x0C00, kNv097R0B80_0C00},
    {0x1E94, 0x1E98, kNv097M1E94},
    {0x1E9C, 0x1EA0, kNv097M1E9C},
    {0x1EA0, 0x1EA4, kNv097M1EA0},
    {0x1EA4, 0x1EA8, kNv097M1EA4},
    {0x1518, 0x1528, kNv097R1518_1528},
    {0x1880, 0x1900, kNv097R1880_1900},
    {0x1900, 0x1940, kNv097R1900_1940},
    {0x1940, 0x1980, kNv097R1940_1980},
    {0x1980, 0x1A00, kNv097R1980_1A00},
    {0x1A00, 0x1B00, kNv097R1A00_1B00},
    {0x1720, 0x1760, kNv097R1720_1760},
    {0x1760, 0x17A0, kNv097R1760_17A0},
    {0x17FC, 0x1800, kNv097M17FC},
    {0x1800, 0x1804, kNv097M1800},
    {0x1808, 0x180C, kNv097M1808},
    {0x1810, 0x1814, kNv097M1810},
    {0x1818, 0x181C, kNv097M1818},
    {0x01A4, 0x01A8, kNv097M01A4},
    {0x1D6C, 0x1D70, kNv097M1D6C},
    {0x1D70, 0x1D74, kNv097M1D70},
    {0x01A8, 0x01AC, kNv097M01A8},
    {0x17C8, 0x17CC, kNv097M17C8},
    {0x17CC, 0x17D0, kNv097M17CC},
    {0x17D0, 0x17D4, kNv097M17D0},
    {0x0110, 0x0114, kNv097M0110},
};

struct Nv097Table {
    uint8_t handler[kNv097TableSize];
};

constexpr Nv097Table build_nv097_table() {
    Nv097Table table{};  // value-initialized: every slot starts at kNv097None
    for (const Nv097Arm& arm : kNv097Arms) {
        for (uint32_t method = arm.begin; method < arm.end; method += 4) {
            const size_t index = method >> 2;
            if (table.handler[index] == kNv097None) table.handler[index] = arm.handler;
        }
    }
    return table;
}

constexpr Nv097Table kNv097Table = build_nv097_table();

}  // namespace

void Nv2a::dispatch_method(PushMethod push) {
    const uint8_t channel = push.channel;
    const uint8_t subchannel = push.subchannel;
    const uint32_t method = push.method;
    const uint32_t parameter = push.parameter;

    // SET_OBJECT changes the class which owns this very method; every other method observes
    // the class already bound on its subchannel.  GET/PUT were captured by the PFIFO parser
    // when it decoded the parameter, before deferred PGRAPH dispatch.
    bool object_bound = true;
    if (method == 0) {
        object_bound = bind_object(channel, subchannel, parameter);
        push.object_class = object_class_[subchannel];
    } else {
        push.object_class = object_class_[subchannel];
    }
    // NV2A_RECORDER (beads-xbox.1.15.1 / .1.14.3): the per-method bookkeeping that
    // runs before any handler. Measured separately from NV2A_DISPATCH so the jump
    // table's A/B is attributable -- see the bucket comment in perf_excl.hpp. The
    // scope closes at the end of this block, so method_counts_ is inside it and
    // the switch below is not.
    dispatch_micro::mark(dispatch_micro::kDmPrologue);
    {
        perf_excl::Scope recorder_region(perf_excl::kNv2aRecorder);
        push.seq = methods_.total();
        methods_.push(push);
        dispatch_micro::mark(dispatch_micro::kRecRing);

        if (clear_trace_triggered_ && !clear_trace_begin_triggered_ &&
            !clear_trace_truncated_) {
            clear_trace_pre_methods_.push_back(push);
            if (clear_trace_pre_methods_.size() > kClearTracePreMethods)
                clear_trace_pre_methods_.pop_front();

            if (push.object_class == 0x97 && method == 0x17FC && parameter != 0) {
                clear_trace_begin_triggered_ = true;
                clear_trace_begin_seq_ = push.seq;
                clear_trace_begin_channel_ = channel;
                clear_trace_begin_subchannel_ = subchannel;
                clear_trace_methods_.assign(clear_trace_pre_methods_.begin(),
                                            clear_trace_pre_methods_.end());
                clear_trace_pre_count_ = clear_trace_methods_.size();
                clear_trace_pre_methods_.clear();
            }
        } else if (clear_trace_begin_triggered_ && !clear_trace_complete_ &&
                   !clear_trace_truncated_) {
            if (clear_trace_methods_.size() < kClearTraceMethodLimit)
                clear_trace_methods_.push_back(push);

            const bool matching_end =
                push.object_class == 0x97 && channel == clear_trace_begin_channel_ &&
                subchannel == clear_trace_begin_subchannel_ && method == 0x17FC &&
                parameter == 0;
            if (matching_end) {
                clear_trace_complete_ = true;
            } else if (clear_trace_methods_.size() == kClearTraceMethodLimit) {
                clear_trace_truncated_ = true;
            }
        }
        dispatch_micro::mark(dispatch_micro::kRecTrace);
        if (method < 0x2000 && !(method & 3u)) ++method_counts_[method >> 2];
        dispatch_micro::mark(dispatch_micro::kRecCounts);
    }  // end NV2A_RECORDER region

    // PFIFO resolves SET_OBJECT through RAMHT, then PGRAPH caches the object's class for
    // this subchannel.
    if (method == 0) {
        if (object_bound) {
            ++class_method_counts_[object_class_[subchannel]];
            if (object_class_[subchannel] == 0x62) {
                context_surfaces_2d_.object_instance = object_instance_[subchannel];
            } else if (object_class_[subchannel] == 0x9F) {
                image_blit_.object_instance = object_instance_[subchannel];
            }
        }
        return;
    }
    ++class_method_counts_[object_class_[subchannel]];
    if (method < 0x2000 && !(method & 3u)) {
        size_t class2d_slot = class2d_method_stats_.size();
        if (object_class_[subchannel] == 0x62) {
            class2d_slot = 0;
        } else if (object_class_[subchannel] == 0x9F) {
            class2d_slot = 1;
        } else if (object_class_[subchannel] == 0x44) {
            class2d_slot = 2;
        }
        if (class2d_slot < class2d_method_stats_.size()) {
            const size_t method_index = method >> 2;
            auto& stats = class2d_method_stats_[class2d_slot];
            if (stats.counts[method_index] == 0)
                stats.first_parameters[method_index] = parameter;
            ++stats.counts[method_index];
            stats.last_parameters[method_index] = parameter;
        }
    }
    if (object_class_[subchannel] == 0x62) {
        switch (method) {
        case 0x0184: {
            uint32_t instance = 0;
            if (resolve_object(channel, parameter, instance)) {
                context_surfaces_2d_.dma_source = instance;
                context_surfaces_2d_.dma_source_bound = true;
            }
            break;
        }
        case 0x0188: {
            uint32_t instance = 0;
            if (resolve_object(channel, parameter, instance)) {
                context_surfaces_2d_.dma_dest = instance;
                context_surfaces_2d_.dma_dest_bound = true;
            }
            break;
        }
        case 0x0300: {
            context_surfaces_2d_.color_format = parameter;
            break;
        }
        case 0x0304: {
            context_surfaces_2d_.source_pitch = parameter & 0xFFFFu;
            context_surfaces_2d_.dest_pitch = parameter >> 16;
            break;
        }
        case 0x0308: {
            context_surfaces_2d_.source_offset = parameter & 0x07FFFFFFu;
            break;
        }
        case 0x030C: {
            context_surfaces_2d_.dest_offset = parameter & 0x07FFFFFFu;
            break;
        }
        default:
            break;
        }
        return;
    }
    if (object_class_[subchannel] == 0x9F) {
        switch (method) {
        case 0x019C: {
            uint32_t instance = 0;
            if (resolve_object(channel, parameter, instance)) {
                image_blit_.context_surfaces = instance;
                image_blit_.context_surfaces_bound = true;
            }
            break;
        }
        case 0x02FC: {
            image_blit_.operation = parameter;
            break;
        }
        case 0x0300: {
            image_blit_.in_x = parameter & 0xFFFFu;
            image_blit_.in_y = parameter >> 16;
            break;
        }
        case 0x0304: {
            image_blit_.out_x = parameter & 0xFFFFu;
            image_blit_.out_y = parameter >> 16;
            break;
        }
        case 0x0308: {
            image_blit_.width = parameter & 0xFFFFu;
            image_blit_.height = parameter >> 16;
            execute_image_blit_srccopy();
            break;
        }
        default:
            break;
        }
        return;
    }
    // NV_CONTEXT_PATTERN (class 0x44) method NV044_SET_MONOCHROME_COLOR0 feeds the
    // guest-readable PGRAPH pattern-color shadow. The stock dashboard polls this state
    // while setting up its 2D context; modeling the register does not render 2D methods.
    if (object_class_[subchannel] == 0x44) {
        if (method == 0x0310) patt_color0_ = parameter;
        return;
    }
    if (object_class_[subchannel] != 0x97) return;
    dispatch_micro::mark(dispatch_micro::kDmClass);

    // beads-xbox.1.15.1 stage (a): the 126-arm if/else-if chain that used to
    // start here is now the kNv097Arms catalogue above.  Case labels sit at the
    // `switch` indent on purpose: every handler body below is byte-for-byte the
    // body of the arm it came from, so the diff is the arm headers only.
    if (method >= kNv097MethodLimit || (method & 3u) != 0) return;
    switch (kNv097Table.handler[method >> 2]) {
    // WAIT_FOR_IDLE precedes WRITE software callbacks in the original XDK.
    // The software floor completes all older raster jobs before the IRQ trap.
    case kNv097M0110:
        draw_order_fence();
        break;
    // NV097 flip queue. FLIP_INCREMENT_WRITE advances the producer modulo; FLIP_STALL
    // blocks PFIFO only when the producer has caught the display/vblank consumer. The
    // kernel advances READ_3D through NV_PGRAPH_INCREMENT in its vblank ISR.
    case kNv097M0100: {
        if (parameter == 0) break;
        // The Xbox kernel uses a nonzero NV097_NO_OPERATION as a software method call.
        // NV2A traps it as a PGRAPH notification and holds the puller until the ISR clears
        // ERROR. In particular parameter 0x304 schedules the vblank-side flip increment.
        pgraph_trapped_addr_ = (static_cast<uint32_t>(channel) << 20) |
                               (static_cast<uint32_t>(subchannel) << 16) | method;
        pgraph_trapped_data_ = parameter;
        pgraph_nsource_ = 1;
        pgraph_intr_ |= 1u << 20;
        waiting_for_nop_ = true;
        ++pgraph_notifications_;
        update_irq();
        break;
    }
    case kNv097M0120: {
        flip_read_ = static_cast<uint8_t>(parameter & 7u);
        break;
    }
    case kNv097M0124: {
        flip_write_ = static_cast<uint8_t>(parameter & 7u);
        break;
    }
    case kNv097M0128: {
        flip_modulo_ = static_cast<uint8_t>(parameter & 7u);
        break;
    }
    case kNv097M012C: {
        if (!flip_modulo_) {
            fault_("NV2A FLIP_INCREMENT_WRITE with zero modulo");
            return;
        }
        flip_write_ = static_cast<uint8_t>((flip_write_ + 1u) % flip_modulo_);
        ++flip_increments_;
        break;
    }
    case kNv097M0130: {
        ++flip_stalls_;
        if (flip_modulo_ && flip_read_ == flip_write_) {
            waiting_for_flip_ = true;
        }
        break;
    }
    // NV097 object-taking methods arrive at PGRAPH as resolved PRAMIN instances.
    case kNv097M0184: {
        uint32_t instance = 0;
        if (resolve_object(channel, parameter, instance)) {
            texture_dma_a_ = instance;
            texture_dma_a_bound_ = true;
            raster_state_[kRasterTextureDmaA] = instance;
        }
        break;
    }
    case kNv097M0188: {
        uint32_t instance = 0;
        if (resolve_object(channel, parameter, instance)) {
            texture_dma_b_ = instance;
            texture_dma_b_bound_ = true;
            raster_state_[kRasterTextureDmaB] = instance;
        }
        break;
    }
    case kNv097M0194: {
        uint32_t instance = 0;
        if (resolve_object(channel, parameter, instance)) {
            color_dma_ = instance;
            color_dma_bound_ = true;
        }
        break;
    }
    case kNv097M0198: {
        uint32_t instance = 0;
        if (resolve_object(channel, parameter, instance)) {
            zeta_dma_ = instance;
            zeta_dma_bound_ = true;
        }
        break;
    }
    case kNv097M019C: {
        uint32_t instance = 0;
        if (resolve_object(channel, parameter, instance)) {
            vertex_dma_a_ = instance;
            vertex_dma_a_bound_ = true;
        }
        break;
    }
    case kNv097M01A0: {
        uint32_t instance = 0;
        if (resolve_object(channel, parameter, instance)) {
            vertex_dma_b_ = instance;
            vertex_dma_b_bound_ = true;
        }
        break;
    }
    // NV097 color-surface state and the first software raster operation.
    case kNv097M0200: {
        surface_clip_horizontal_ = parameter;
        raster_state_[kRasterSurfaceClipHorizontal] = parameter;
        break;
    }
    case kNv097M0204: {
        surface_clip_vertical_ = parameter;
        raster_state_[kRasterSurfaceClipVertical] = parameter;
        break;
    }
    case kNv097M0208: {
        // Seam anchor 9: a surface descriptor change retargets the
        // destination and can alias a pending job's.
        seam_anchor(kSeamAnchorSurfaceRetarget, true);
        surface_format_ = parameter;
        raster_state_[kRasterSurfaceFormat] = parameter;
        break;
    }
    case kNv097M020C: {
        // Seam anchor 9: a surface descriptor change retargets the
        // destination and can alias a pending job's.
        seam_anchor(kSeamAnchorSurfaceRetarget, true);
        surface_pitch_ = parameter;
        raster_state_[kRasterSurfacePitch] = parameter;
        break;
    }
    case kNv097M0210: {
        // Seam anchor 9: a surface descriptor change retargets the
        // destination and can alias a pending job's.
        seam_anchor(kSeamAnchorSurfaceRetarget, true);
        surface_color_offset_ = parameter;
        raster_state_[kRasterSurfaceColorOffset] = parameter;
        break;
    }
    case kNv097M0214: {
        // Seam anchor 9: a surface descriptor change retargets the
        // destination and can alias a pending job's.
        seam_anchor(kSeamAnchorSurfaceRetarget, true);
        surface_zeta_offset_ = parameter;
        raster_state_[kRasterSurfaceZetaOffset] = parameter;
        break;
    }
    case kNv097M1D8C: {
        zeta_clear_value_ = parameter;
        break;
    }
    case kNv097M1D90: {
        color_clear_value_ = parameter;
        break;
    }
    case kNv097M1D98: {
        clear_rect_horizontal_ = parameter;
        break;
    }
    case kNv097M1D9C: {
        clear_rect_vertical_ = parameter;
        break;
    }
    case kNv097M1D94: {
        const uint64_t color_clears_before = color_clears_;
        clear_surface(parameter);
        if (clear_trace_armed_ && color_clears_ != color_clears_before &&
            last_color_clear_address_ == clear_trace_target_) {
            // First matching successful clear wins.  Subsequent decoded methods feed the
            // rolling pre-BEGIN window; the clear itself is intentionally not part of the
            // selected draw window.
            clear_trace_armed_ = false;
            clear_trace_triggered_ = true;
            clear_trace_trigger_seq_ = push.seq;
            clear_trace_pre_methods_.clear();
        }
        break;
    }
    // Raster state is sampled at each indexed batch. Enable bits are alpha,
    // blend, cull, depth, dither, stencil, logic, point-params/smooth,
    // line/poly smooth, and point/line/fill polygon offset respectively.
    case kNv097R0260_0280: {
        raster_state_[kRasterCombinerAlphaIcw0 +
                      (method - 0x0260) / 4] = parameter;
        break;
    }
    case kNv097M0288: {
        raster_state_[kRasterCombinerSpecularFog0] = parameter;
        break;
    }
    case kNv097M028C: {
        raster_state_[kRasterCombinerSpecularFog1] = parameter;
        break;
    }
    case kNv097M0290: {
        raster_state_[kRasterControl0] = parameter;
        break;
    }
    case kNv097M02C0: {
        raster_state_[kRasterWindowClipHorizontal] = parameter;
        break;
    }
    case kNv097M02E0: {
        raster_state_[kRasterWindowClipVertical] = parameter;
        break;
    }
    case kNv097M0300: {
        raster_state_[kRasterEnables] =
            (raster_state_[kRasterEnables] & ~(1u << 0)) |
            ((parameter != 0) << 0);
        break;
    }
    case kNv097M0304: {
        raster_state_[kRasterEnables] =
            (raster_state_[kRasterEnables] & ~(1u << 1)) |
            ((parameter != 0) << 1);
        break;
    }
    case kNv097M0308: {
        raster_state_[kRasterEnables] =
            (raster_state_[kRasterEnables] & ~(1u << 2)) |
            ((parameter != 0) << 2);
        break;
    }
    case kNv097M030C: {
        raster_state_[kRasterEnables] =
            (raster_state_[kRasterEnables] & ~(1u << 3)) |
            ((parameter != 0) << 3);
        break;
    }
    case kNv097M0310: {
        raster_state_[kRasterEnables] =
            (raster_state_[kRasterEnables] & ~(1u << 4)) |
            ((parameter != 0) << 4);
        break;
    }
    case kNv097M0318: {
        raster_state_[kRasterEnables] =
            (raster_state_[kRasterEnables] & ~(1u << 7)) |
            ((parameter != 0) << 7);
        break;
    }
    case kNv097M031C: {
        raster_state_[kRasterEnables] =
            (raster_state_[kRasterEnables] & ~(1u << 8)) |
            ((parameter != 0) << 8);
        break;
    }
    case kNv097M0320: {
        raster_state_[kRasterEnables] =
            (raster_state_[kRasterEnables] & ~(1u << 9)) |
            ((parameter != 0) << 9);
        break;
    }
    case kNv097M0324: {
        raster_state_[kRasterEnables] =
            (raster_state_[kRasterEnables] & ~(1u << 10)) |
            ((parameter != 0) << 10);
        break;
    }
    case kNv097M032C: {
        raster_state_[kRasterEnables] =
            (raster_state_[kRasterEnables] & ~(1u << 5)) |
            ((parameter != 0) << 5);
        break;
    }
    case kNv097M0330: {
        raster_state_[kRasterEnables] =
            (raster_state_[kRasterEnables] & ~(1u << 11)) |
            ((parameter != 0) << 11);
        break;
    }
    case kNv097M0334: {
        raster_state_[kRasterEnables] =
            (raster_state_[kRasterEnables] & ~(1u << 12)) |
            ((parameter != 0) << 12);
        break;
    }
    case kNv097M0338: {
        raster_state_[kRasterEnables] =
            (raster_state_[kRasterEnables] & ~(1u << 13)) |
            ((parameter != 0) << 13);
        break;
    }
    case kNv097M033C: {
        raster_state_[kRasterAlphaFunc] = parameter;
        break;
    }
    case kNv097M0340: {
        raster_state_[kRasterAlphaRef] = parameter;
        break;
    }
    case kNv097M0344: {
        raster_state_[kRasterBlendSrc] = parameter;
        break;
    }
    case kNv097M0348: {
        raster_state_[kRasterBlendDst] = parameter;
        break;
    }
    case kNv097M034C: {
        raster_state_[kRasterBlendColor] = parameter;
        break;
    }
    case kNv097M0350: {
        raster_state_[kRasterBlendEquation] = parameter;
        break;
    }
    case kNv097M0354: {
        raster_state_[kRasterDepthFunc] = parameter;
        break;
    }
    case kNv097M0358: {
        raster_state_[kRasterColorMask] = parameter;
        break;
    }
    case kNv097M035C: {
        raster_state_[kRasterDepthMask] = parameter;
        break;
    }
    case kNv097M0360: {
        raster_state_[kRasterStencilWriteMask] = parameter;
        break;
    }
    case kNv097M0364: {
        raster_state_[kRasterStencilFunc] = parameter;
        break;
    }
    case kNv097M0368: {
        raster_state_[kRasterStencilRef] = parameter;
        break;
    }
    case kNv097M036C: {
        raster_state_[kRasterStencilFuncMask] = parameter;
        break;
    }
    case kNv097M0370: {
        raster_state_[kRasterStencilOpFail] = parameter;
        break;
    }
    case kNv097M0374: {
        raster_state_[kRasterStencilOpZFail] = parameter;
        break;
    }
    case kNv097M0378: {
        raster_state_[kRasterStencilOpZPass] = parameter;
        break;
    }
    case kNv097M09FC: {
        raster_state_[kRasterProvokingVertex] = parameter;
        break;
    }
    case kNv097M037C: {
        raster_state_[kRasterShadeMode] = parameter;
        break;
    }
    case kNv097M0384: {
        raster_state_[kRasterPolygonOffsetScale] = parameter;
        break;
    }
    case kNv097M0388: {
        raster_state_[kRasterPolygonOffsetBias] = parameter;
        break;
    }
    case kNv097M038C: {
        raster_state_[kRasterFrontPolygonMode] = parameter;
        break;
    }
    case kNv097M0390: {
        raster_state_[kRasterBackPolygonMode] = parameter;
        break;
    }
    case kNv097M0394: {
        raster_state_[kRasterClipMin] = parameter;
        break;
    }
    case kNv097M0398: {
        raster_state_[kRasterClipMax] = parameter;
        break;
    }
    case kNv097M039C: {
        raster_state_[kRasterCullFace] = parameter;
        break;
    }
    case kNv097M03A0: {
        raster_state_[kRasterFrontFace] = parameter;
        break;
    }
    case kNv097M17BC: {
        raster_state_[kRasterEnables] =
            (raster_state_[kRasterEnables] & ~(1u << 6)) |
            ((parameter != 0) << 6);
        break;
    }
    case kNv097M17C0: {
        raster_state_[kRasterLogicOp] = parameter;
        break;
    }
    case kNv097M1D78: {
        raster_state_[kRasterZMinMaxControl] = parameter;
        break;
    }
    case kNv097M1E20: {
        raster_state_[kRasterCombinerFinalFactor0] = parameter;
        break;
    }
    case kNv097M1E24: {
        raster_state_[kRasterCombinerFinalFactor1] = parameter;
        break;
    }
    case kNv097R1E40_1E60: {
        raster_state_[kRasterCombinerColorOcw0 +
                      (method - 0x1E40) / 4] = parameter;
        break;
    }
    case kNv097M1E60: {
        raster_state_[kRasterCombinerControl] = parameter;
        break;
    }
    case kNv097M1E6C: {
        raster_state_[kRasterShadowDepthFunc] = parameter;
        break;
    }
    case kNv097M1E70: {
        raster_state_[kRasterShaderStageProgram] = parameter;
        break;
    }
    case kNv097M1E74: {
        raster_state_[kRasterDotRgbMapping] = parameter;
        break;
    }
    case kNv097M1E78: {
        raster_state_[kRasterShaderOtherStageInput] = parameter;
        break;
    }
    case kNv097R1B00_1C00: {
        const uint32_t stage = (method - 0x1B00) / 0x40;
        const uint32_t local = (method - 0x1B00) & 0x3Fu;
        if (local == 0x00) {
            raster_state_[kRasterTextureOffset0 + stage] = parameter;
        } else if (local == 0x04) {
            raster_state_[kRasterTextureFormat0 + stage] = parameter;
        } else if (local == 0x08) {
            raster_state_[kRasterTextureAddress0 + stage] = parameter;
        } else if (local == 0x0C) {
            raster_state_[kRasterTextureControl0_0 + stage] = parameter;
            const uint32_t enable = (parameter >> 30) & 1u;
            raster_state_[kRasterTextureEnableMask] =
                (raster_state_[kRasterTextureEnableMask] & ~(1u << stage)) |
                (enable << stage);
        } else if (local == 0x10) {
            raster_state_[kRasterTextureControl1_0 + stage] = parameter;
        } else if (local == 0x14) {
            raster_state_[kRasterTextureFilter0 + stage] = parameter;
        } else if (local == 0x1C) {
            raster_state_[kRasterTextureImageRect0 + stage] = parameter;
        } else if (local == 0x20) {
            raster_state_[kRasterTexturePalette0 + stage] = parameter;
        } else if (local == 0x24) {
            raster_state_[kRasterTextureBorderColor0 + stage] = parameter;
        }
        break;
    }
    // Fixed-function vertex-state selectors. Their batch census gates which xemu equations
    // must execute before fixed-function output can be admitted to rasterization.
    case kNv097M0294: {
        fixed_light_control_ =
            static_cast<uint8_t>((parameter & 1u) |
                                 ((parameter >> 15) & 2u) |
                                 ((parameter >> 15) & 4u));
        break;
    }
    case kNv097M0298: {
        fixed_color_material_ = static_cast<uint8_t>(parameter);
        break;
    }
    case kNv097M02A4: {
        fixed_fog_ = parameter != 0;
        break;
    }
    case kNv097M0314: {
        fixed_lighting_ = parameter != 0;
        break;
    }
    case kNv097M0328: {
        if (parameter > 6) {
            fault_("NV2A fixed-function skin mode is invalid");
            return;
        }
        fixed_skin_mode_ = static_cast<uint8_t>(parameter);
        break;
    }
    case kNv097M03A4: {
        fixed_normalization_ = parameter != 0;
        break;
    }
    case kNv097R03A8_03B4: {
        const uint32_t component = (method - 0x03A8) / 4;
        std::memcpy(&fixed_material_emission_[component],
                    &parameter, sizeof(parameter));
        break;
    }
    case kNv097M03B4: {
        std::memcpy(&fixed_material_alpha_, &parameter, sizeof(parameter));
        break;
    }
    case kNv097M03B8: {
        fixed_specular_ = parameter != 0;
        break;
    }
    case kNv097M03BC: {
        fixed_light_enable_mask_ = static_cast<uint16_t>(parameter);
        break;
    }
    case kNv097R03C0_0400: {
        const uint32_t stage = (method - 0x03C0) / 0x10;
        const uint32_t component = ((method - 0x03C0) & 0xFu) / 4;
        fixed_texgen_[stage][component] = parameter;
        break;
    }
    case kNv097R0420_0430: {
        fixed_texture_matrix_enable_[(method - 0x0420) / 4] = parameter != 0;
        break;
    }
    // Fixed-function matrix uploads share Cheops constant memory. Matrices are
    // interleaved exactly as the NV2A XF context lays them out.
    case kNv097R0440_0480: {
        const uint32_t slot = (method - 0x0440) / 4;
        std::memcpy(&transform_constants_[0x04 + slot / 4][slot & 3u],
                    &parameter, sizeof(parameter));
        break;
    }
    case kNv097R0480_0580: {
        const uint32_t slot = (method - 0x0480) / 4;
        const uint32_t matrix = slot / 16;
        const uint32_t entry = slot & 15u;
        std::memcpy(&transform_constants_[0x08 + matrix * 8 + entry / 4]
                                         [entry & 3u],
                    &parameter, sizeof(parameter));
        break;
    }
    case kNv097R0580_0680: {
        const uint32_t slot = (method - 0x0580) / 4;
        const uint32_t matrix = slot / 16;
        const uint32_t entry = slot & 15u;
        std::memcpy(&transform_constants_[0x0C + matrix * 8 + entry / 4]
                                         [entry & 3u],
                    &parameter, sizeof(parameter));
        break;
    }
    case kNv097R0680_06C0: {
        const uint32_t slot = (method - 0x0680) / 4;
        std::memcpy(&transform_constants_[slot / 4][slot & 3u],
                    &parameter, sizeof(parameter));
        break;
    }
    case kNv097R06C0_07C0: {
        const uint32_t slot = (method - 0x06C0) / 4;
        const uint32_t texture = slot / 16;
        const uint32_t entry = slot & 15u;
        std::memcpy(&transform_constants_[0x44 + texture * 8 + entry / 4]
                                         [entry & 3u],
                    &parameter, sizeof(parameter));
        break;
    }
    case kNv097R0840_0940: {
        const uint32_t slot = (method - 0x0840) / 4;
        const uint32_t texture = slot / 16;
        const uint32_t entry = slot & 15u;
        std::memcpy(&transform_constants_[0x40 + texture * 8 + entry / 4]
                                         [entry & 3u],
                    &parameter, sizeof(parameter));
        break;
    }
    case kNv097R0A10_0A1C: {
        const uint32_t component = (method - 0x0A10) / 4;
        std::memcpy(&fixed_scene_ambient_[component],
                    &parameter, sizeof(parameter));
        break;
    }
    // Kelvin viewport and Cheops programmable-transform state. Program/constant upload
    // pointers advance after each complete four-word vector, matching xemu's PGRAPH model.
    case kNv097R0A20_0A30: {
        const uint32_t slot = (method - 0x0A20) / 4;
        std::memcpy(&viewport_offset_[slot], &parameter, sizeof(parameter));
        transform_constants_[0x3B][slot] = viewport_offset_[slot];
        break;
    }
    case kNv097R0A60_0A80: {
        raster_state_[kRasterCombinerFactor0_0 +
                      (method - 0x0A60) / 4] = parameter;
        break;
    }
    case kNv097R0A80_0AA0: {
        raster_state_[kRasterCombinerFactor1_0 +
                      (method - 0x0A80) / 4] = parameter;
        break;
    }
    case kNv097R0AA0_0AC0: {
        raster_state_[kRasterCombinerAlphaOcw0 +
                      (method - 0x0AA0) / 4] = parameter;
        break;
    }
    case kNv097R0AC0_0AE0: {
        raster_state_[kRasterCombinerColorIcw0 +
                      (method - 0x0AC0) / 4] = parameter;
        break;
    }
    case kNv097R0AF0_0B00: {
        const uint32_t slot = (method - 0x0AF0) / 4;
        std::memcpy(&viewport_scale_[slot], &parameter, sizeof(parameter));
        transform_constants_[0x3A][slot] = viewport_scale_[slot];
        break;
    }
    case kNv097R0B00_0B80: {
        const uint32_t slot = (method - 0x0B00) / 4;
        if (transform_program_load_ >= transform_program_.size()) {
            fault_("NV2A transform program upload exceeds instruction memory");
            return;
        }
        transform_program_[transform_program_load_][slot & 3u] = parameter;
        // Phase 5 item 3: one of exactly TWO inputs the parsed-program cache
        // depends on. Writing any program word invalidates it.
        invalidate_vsh_program_cache();
        ++transform_program_words_;
        if ((slot & 3u) == 3) ++transform_program_load_;
        break;
    }
    case kNv097R0B80_0C00: {
        const uint32_t slot = (method - 0x0B80) / 4;
        if (transform_constant_load_ >= transform_constants_.size()) {
            fault_("NV2A transform constant upload exceeds constant memory");
            return;
        }
        std::memcpy(&transform_constants_[transform_constant_load_][slot & 3u],
                    &parameter, sizeof(parameter));
        ++transform_constant_words_;
        if ((slot & 3u) == 3) ++transform_constant_load_;
        break;
    }
    case kNv097M1E94: {
        transform_execution_mode_ = static_cast<uint8_t>(parameter & 3u);
        break;
    }
    case kNv097M1E9C: {
        if (parameter >= transform_program_.size()) {
            fault_("NV2A transform program load pointer is out of range");
            return;
        }
        transform_program_load_ = static_cast<uint8_t>(parameter);
        break;
    }
    case kNv097M1EA0: {
        if (parameter >= transform_program_.size()) {
            fault_("NV2A transform program start is out of range");
            return;
        }
        // The SECOND of the parsed-program cache's two inputs: the parse begins
        // at this slot and covers 136 - start slots, so both the contents and
        // the length of the cached program change with it.
        if (transform_program_start_ != static_cast<uint8_t>(parameter))
            invalidate_vsh_program_cache();
        transform_program_start_ = static_cast<uint8_t>(parameter);
        break;
    }
    case kNv097M1EA4: {
        if (parameter >= transform_constants_.size()) {
            fault_("NV2A transform constant load pointer is out of range");
            return;
        }
        transform_constant_load_ = static_cast<uint8_t>(parameter);
        break;
    }
    // The dashboard's immediate quad path uses generic texture/diffuse attributes and the
    // legacy position stream.  These follow xemu's NV097 handlers: attributes persist until
    // changed, while a completed write to attribute slot 0 (position) snapshots one full
    // 16-slot vertex.  This covers the legacy SET_VERTEX4F position stream plus every
    // SET_VERTEX_DATA* family (2F_M, 2S, 4UB, 4S_M, 4F_M); each commits on the write that
    // finishes slot 0, matching hardware semantics where position completion latches a vertex.
    case kNv097R1518_1528: {
        const uint32_t component = (method - 0x1518) / 4;
        std::memcpy(&vertex_attributes_[0].value[component], &parameter, sizeof(parameter));
        ++immediate_methods_;
        if (component == 3 && !commit_immediate_vertex()) return;
        break;
    }
    case kNv097R1880_1900: {
        const uint32_t word = (method - 0x1880) / 4;
        const uint32_t slot = word / 2;
        const uint32_t component = word & 1u;
        auto& value = vertex_attributes_[slot].value;
        std::memcpy(&value[component], &parameter, sizeof(parameter));
        value[2] = 0.0f;
        value[3] = 1.0f;
        ++immediate_methods_;
        if (slot == 0 && component == 1 && !commit_immediate_vertex()) return;
        break;
    }
    case kNv097R1900_1940: {
        const uint32_t slot = (method - 0x1900) / 4;
        auto& value = vertex_attributes_[slot].value;
        const int16_t x = static_cast<int16_t>(parameter & 0xFFFFu);
        const int16_t y = static_cast<int16_t>((parameter >> 16) & 0xFFFFu);
        value[0] = static_cast<float>(x);
        value[1] = static_cast<float>(y);
        value[2] = 0.0f;
        value[3] = 1.0f;
        ++immediate_methods_;
        if (slot == 0 && !commit_immediate_vertex()) return;
        break;
    }
    case kNv097R1940_1980: {
        const uint32_t slot = (method - 0x1940) / 4;
        auto& value = vertex_attributes_[slot].value;
        value[0] = static_cast<float>(parameter & 0xFFu) / 255.0f;
        value[1] = static_cast<float>((parameter >> 8) & 0xFFu) / 255.0f;
        value[2] = static_cast<float>((parameter >> 16) & 0xFFu) / 255.0f;
        value[3] = static_cast<float>((parameter >> 24) & 0xFFu) / 255.0f;
        ++immediate_methods_;
        if (slot == 0 && !commit_immediate_vertex()) return;
        break;
    }
    case kNv097R1980_1A00: {
        const uint32_t word = (method - 0x1980) / 4;
        const uint32_t slot = word / 2;
        const uint32_t part = word & 1u;
        auto& value = vertex_attributes_[slot].value;
        const int16_t lo = static_cast<int16_t>(parameter & 0xFFFFu);
        const int16_t hi = static_cast<int16_t>((parameter >> 16) & 0xFFFFu);
        value[part * 2] = static_cast<float>(lo);
        value[part * 2 + 1] = static_cast<float>(hi);
        ++immediate_methods_;
        if (slot == 0 && part == 1 && !commit_immediate_vertex()) return;
        break;
    }
    case kNv097R1A00_1B00: {
        const uint32_t word = (method - 0x1A00) / 4;
        const uint32_t slot = word / 4;
        const uint32_t component = word & 3u;
        std::memcpy(&vertex_attributes_[slot].value[component], &parameter, sizeof(parameter));
        ++immediate_methods_;
        if (slot == 0 && component == 3 && !commit_immediate_vertex()) return;
        break;
    }
    // Kelvin array-fed vertex submission. ARRAY_ELEMENT16 is non-incrementing and packs two
    // indices per parameter; the pusher has already expanded non-incrementing method runs.
    case kNv097R1720_1760: {
        const uint32_t slot = (method - 0x1720) / 4;
        vertex_attributes_[slot].dma_b = (parameter & 0x80000000u) != 0;
        vertex_attributes_[slot].offset = parameter & 0x7FFFFFFFu;
        break;
    }
    case kNv097R1760_17A0: {
        set_vertex_array_format((method - 0x1760) / 4, parameter);
        break;
    }
    case kNv097M17FC: {
        if (parameter == 0) {
            if (primitive_mode_ != 0 &&
                decode_inline_array()) {
                submit_vertex_batch();
            }
            primitive_mode_ = 0;
            vertex_indices_.clear();
            inline_array_words_.clear();
            inline_vertices_.clear();
            immediate_vertices_.clear();
        } else if (parameter <= 10 && primitive_mode_ == 0) {
            primitive_mode_ = static_cast<uint8_t>(parameter);
            vertex_indices_.clear();
            inline_array_words_.clear();
            inline_vertices_.clear();
            immediate_vertices_.clear();
        }
        break;
    }
    case kNv097M1800: {
        if (vertex_indices_.size() > (1u << 24) - 2) {
            fault_("NV2A indexed vertex batch exceeds safety bound");
            return;
        }
        vertex_indices_.push_back(parameter & 0xFFFFu);
        vertex_indices_.push_back(parameter >> 16);
        break;
    }
    case kNv097M1808: {
        if (vertex_indices_.size() >= (1u << 24)) {
            fault_("NV2A indexed vertex batch exceeds safety bound");
            return;
        }
        vertex_indices_.push_back(parameter);
        break;
    }
    case kNv097M1810: {
        const uint32_t start = parameter & 0x00FFFFFFu;
        const uint32_t count = (parameter >> 24) + 1u;
        if (vertex_indices_.size() > (1u << 24) - count) {
            fault_("NV2A draw-array batch exceeds safety bound");
            return;
        }
        for (uint32_t i = 0; i < count; ++i)
            vertex_indices_.push_back(start + i);
        break;
    }
    case kNv097M1818: {
        if (primitive_mode_ == 0) {
            fault_("NV2A inline-array data submitted outside BEGIN/END");
            return;
        }
        if (inline_array_words_.size() >= (1u << 24)) {
            fault_("NV2A inline-array payload exceeds safety bound");
            return;
        }
        inline_array_words_.push_back(parameter);
        break;
    }
    // Register/report layout is corroborated by nxdk_pgraph_tests ZPass and
    // xemu's public NV097 definitions; no donor implementation is incorporated.
    case kNv097M01A8: {
        draw_order_fence();
        uint32_t instance = 0;
        if (resolve_object(channel, parameter, instance)) {
            report_dma_[subchannel] = instance;
            report_dma_bound_[subchannel] = true;
        }
        break;
    }
    case kNv097M17C8:
        draw_order_fence();
        if (parameter != 1) { fault_("NV2A unsupported report clear type"); return; }
        zpass_count_ = 0; zpass_valid_ = true;
        break;
    case kNv097M17CC:
        draw_order_fence();
#if XLLE_RENDERER_HLE
        if (parameter) { fault_("NV2A machine HLE visibility queries are not implemented"); return; }
#endif
        zpass_enabled_ = parameter != 0;
        break;
    case kNv097M17D0: {
        draw_order_fence();
        uint32_t offset = parameter & 0x00ffffffu, address = 0, last = 0;
        if ((parameter >> 24) != 1 || !zpass_valid_ || !report_dma_bound_[subchannel] ||
            (offset & 15) || !dma_object_address(report_dma_[subchannel], offset, address) ||
            !dma_object_address(report_dma_[subchannel], offset + 15, last) || last != address + 15) {
            fault_("NV2A invalid or unqualified visibility report"); return;
        }
        // Nanoseconds from the device's existing emulated display clock.
        // Resolution is 1/1000 display interval; exact PTIMER phase is not
        // part of this bounded reference. GET_REPORT does not clear count.
        uint64_t stamp = (frame_no_ / 60) * 1000000000ull +
            ((frame_no_ % 60) * 1000 + frame_phase_) * 1000000000ull / 60000;
        const uint32_t payload[] = {static_cast<uint32_t>(stamp),
            static_cast<uint32_t>(stamp >> 32), zpass_count_};
        const uint32_t done = 0;
        // Publish the completion word last, after payload and prior draws.
        if (!mem_write_ || !mem_write_(address, payload, sizeof(payload)) ||
            !mem_write_(address + 12, &done, sizeof(done))) {
            fault_("NV2A visibility report DMA write failed"); return;
        }
        break;
    }
    // NV097 semaphore methods. AvSetDisplayMode uses RELEASE as a GPU completion fence.
    case kNv097M01A4: {
        uint32_t instance = 0;
        if (resolve_object(channel, parameter, instance))
            semaphore_dma_[subchannel] = instance;
        break;
    }
    case kNv097M1D6C: {
        semaphore_offset_[subchannel] = parameter;
        break;
    }
    case kNv097M1D70: {
        // Seam anchor 1 (F11): a semaphore release is literally the guest
        // asking whether the GPU has finished. Answering it with jobs
        // pending is the one unambiguous correctness bug the seam could
        // introduce, so it is an unconditional drain.
        seam_anchor(kSeamAnchorSemaphoreRelease, true);
        if (gpu_res_color_addr_ != 0) {
            ++gpu_res_.semaphore_resolves;
            gpu_res_.semaphore_resolve_bytes += gpu_res_color_bytes_;
        }
        uint32_t address = 0;
        if (!dma_object_address(semaphore_dma_[subchannel], semaphore_offset_[subchannel],
                                address)) {
            char why[160];
            std::snprintf(why, sizeof(why),
                          "NV2A semaphore DMA invalid: instance=0x%08x offset=0x%08x",
                          semaphore_dma_[subchannel], semaphore_offset_[subchannel]);
            fault_(why);
            return;
        }
        if (!mem_write_ || !mem_write_(address, &parameter, sizeof(parameter))) {
            char why[128];
            std::snprintf(why, sizeof(why), "NV2A semaphore DMA write failed at 0x%08x",
                          address);
            fault_(why);
            return;
        }
        ++semaphore_writes_;
        if (semaphore_writes_ <= 16) {
            std::printf("[nv2a] semaphore release ch%u/sub%u [0x%08x] <- 0x%08x\n",
                        channel, subchannel, address, parameter);
        }
        break;
    }
    default:
        break;
    }
}

void Nv2a::resume_pushbuffers() {
    // Seam anchor 5 (F10): the guest is being told the GPU finished. Every
    // flip-stall clear reaches this function (nv2a.cpp, the PGRAPH
    // INCREMENT_READ_3D write clears waiting_for_flip_ and calls straight
    // in), so one anchor here covers the whole path without double counting.
    seam_anchor(kSeamAnchorFlipStallClear, true);
    drain_pull_queue();
    if (waiting_for_flip_ || waiting_for_nop_ || !pgraph_fifo_access_) return;
    for (uint8_t channel = 0; channel < channels_.size(); ++channel) {
        const ChannelState& ch = channels_[channel];
        if (ch.get_initialized && ch.get != ch.put && pusher_enabled(channel)) {
            process_pushbuffer(channel);
            if (waiting_for_flip_ || waiting_for_nop_ || !pgraph_fifo_access_) return;
        }
    }
}

void Nv2a::drain_pull_queue() {
    if (waiting_for_flip_ || waiting_for_nop_ || !pgraph_fifo_access_ ||
        pull_queue_.empty()) {
        return;
    }
    // NV2A_DISPATCH (beads-xbox.1.14.3). This definition is the single funnel every queued
    // PGRAPH method passes through, so a new dispatch call site cannot bypass the region.
    // Exact, not sampled: this is a hot child of the hot NV2A_PFIFO parent, and on an
    // unsampled entry its time would stay in the parent while a scaled child total also
    // covered it -- an overlap only a subtraction between buckets could remove, which is
    // the defect this bead deletes. See the header for the cost accounting.
    perf_excl::Scope region(perf_excl::kNv2aDispatch);
    std::optional<perf::ScopedAdd> perf_t;
    if (perf::g_pull_drain_enabled) {
        perf_t.emplace(perf::g_ns_nv2a_pull_drain,
                       perf::g_calls_nv2a_pull_drain);
    }
    while (!waiting_for_flip_ && !waiting_for_nop_ && pgraph_fifo_access_ &&
           !pull_queue_.empty()) {
        PushMethod push = pull_queue_.front();
        pull_queue_.pop_front();
        perf::record_pfifo_queue_pop();
        dispatch_micro::mark(dispatch_micro::kDrainEnter);
        if (perf::g_enabled) {
            perf::ScopedAdd dispatch_timer(perf::g_ns_nv2a_dispatch_method,
                                           perf::g_calls_nv2a_dispatch_method);
            dispatch_method(push);
        } else {
            dispatch_method(push);
        }
        // The handler slot closes here rather than at the tail of
        // dispatch_method: the call and return are part of what a per-word
        // dispatch costs, and the tail of dispatch_method is pinned by
        // tests/test_dispatch_table_guards.py as `default: break;` and nothing
        // after it.
        dispatch_micro::mark(dispatch_micro::kDmHandler);
    }
}

// beads-xbox.1.15.1 stage (d): one resolved host window per pushbuffer kick.
//
// Every word of the pushbuffer used to travel through mem_read_ -- a
// std::function into UnicornFloor::read_mem, i.e. uc_mem_read, for four bytes.
// The per-word micro-partition prices that at 18.73 ns of the word's 420.74 ns,
// 1.560 s of the canonical run, and it is the single largest item in the PFIFO
// half of the bucket pair. It is also entirely avoidable: XboxMachine maps guest
// RAM into Unicorn with uc_mem_map_ptr(ram_.data()) and hands the SAME pointer
// back through mem_direct_, so a read through that pointer returns the bytes
// uc_mem_read would have returned, by construction rather than by agreement.
//
// The window is cached across the words of one kick because the pushbuffer is
// contiguous between jumps, so 4 KiB of it covers ~2048 words. It is RESET at
// the top of every process_pushbuffer call, which bounds a cached host pointer
// to a single invocation: within one invocation nothing can move guest RAM
// (dispatch_method reaches no path that resizes it, and every re-entry into the
// parser comes from Nv2a::mmio_write, i.e. from the guest CPU, never from a
// handler). A span that is not directly addressable -- the top of a mapping, a
// non-RAM DMA target -- returns nullptr and the caller uses mem_read_, so the
// fallback is never dead.
const uint8_t* Nv2a::pushbuffer_host_bytes(uint32_t addr, uint32_t bytes) {
    constexpr uint32_t kPushWindowBytes = 8192;  // >= the 8188 B of a 2047-word run
    const uint64_t lo = addr;
    const uint64_t hi = lo + bytes;
    if (push_win_ && lo >= push_win_lo_ && hi <= push_win_hi_)
        return push_win_ + (lo - push_win_lo_);
    if (!pfifo_direct_fetch_ || !mem_direct_ || !bytes) return nullptr;
    const uint32_t span = bytes > kPushWindowBytes ? bytes : kPushWindowBytes;
    if (const uint8_t* p = mem_direct_(addr, span)) {
        push_win_ = p;
        push_win_lo_ = lo;
        push_win_hi_ = lo + span;
        return p;
    }
    // Too near the top of a mapping for a whole window. Serve this span alone
    // and leave the cached window as it was.
    return mem_direct_(addr, bytes);
}

// beads-xbox.1.15.1 stage (c): dispatch a RUN of method data words in one pass.
//
// WHAT THE PER-WORD PATH COSTS, MEASURED. The strict pusher below does, for
// every one of the run's 83.3 M method data words, a PRAMIN DMA-object decode,
// a std::function hop into uc_mem_read for four bytes, a PushMethod aggregate
// pushed onto a std::deque, a call into drain_pull_queue that re-tests the stall
// predicate, opens the NV2A_DISPATCH region, front()s and pop_front()s that same
// entry back off, and then dispatches it. The per-word micro-partition
// (src/machine/nv2a_dispatch_micro.hpp, one 12 B noperf leg, 650 697 sampled
// words, period 64) prices those pieces at, per word, over ALL data words:
//
//     pf_fetch (dma translate + uc_mem_read) 18.73 ns   1.560 s of the run
//     pf_queue (PushMethod + deque push)     22.37 ns   1.863 s
//     drain_enter (funnel + front/pop_front) 14.89 ns   1.240 s
//     pf_dma (dma_object_address)             1.42 ns   0.118 s
//     drain_exit                              0.43 ns   0.036 s
//                                            -------   -------
//                                            57.84 ns   4.817 s
//
// -- against a whole-word envelope of 420.74 ns, the rest of which is the draw
// work the handler for SET_BEGIN_END reaches. This function deletes that 4.8 s
// of per-word overhead and nothing else.
//
// WHY IT IS THE SAME ANSWER, WORD FOR WORD. The loop below is the per-word loop
// with five things removed and nothing added:
//
//   * The word comes from a host pointer into the same guest RAM page Unicorn
//     maps (XboxMachine passes ram_.data() to uc_mem_map_ptr and hands the same
//     pointer back through mem_direct_), so the bytes are the bytes uc_mem_read
//     would have returned. The DMA object is decoded for the FIRST and LAST word
//     of the run and the run is declined unless the two host addresses differ by
//     exactly 4 * (n - 1); the object maps offsets linearly, so that check
//     proves every word between them is inside the limit and contiguous, and it
//     catches a run that wraps the 0x07FFFFFF address mask.
//   * pull_queue_ is bypassed -- but ONLY when it is already empty, so nothing
//     can be reordered ahead of an entry left behind by a stalled drain.
//   * The queue telemetry is still exact: record_pfifo_queue_push() immediately
//     followed by record_pfifo_queue_pop() reproduces the same live count and
//     the same peak the push/drain pair produced.
//   * The stall predicate is re-tested EVERY word, exactly as drain_pull_queue's
//     loop does, so a handler that sets waiting_for_flip_ / waiting_for_nop_ or
//     clears pgraph_fifo_access_ stops the run at the same word. The caller then
//     resumes on the per-word path, which is what queues the remainder.
//   * ch.get, ch.method and ch.method_count are advanced per word rather than
//     per run, so an observer sees the same channel state at the same instant;
//     and `expected_get` re-checks ch.get at the top of every iteration, so if a
//     handler ever did re-enter the parser (no current path does: every
//     process_pushbuffer / resume_pushbuffers call site is in Nv2a::mmio_write)
//     the run stops instead of reading from a stale pointer.
//
// NV2A_DISPATCH is opened once per run rather than once per word. That is a
// granularity change to the perf_excl partition, not an estimate: the region
// still covers exactly the method-semantics time and nothing else, because the
// only work inside it that is not a handler is the loop itself.
uint32_t Nv2a::drain_method_run(ChannelState& ch, uint8_t channel, uint32_t max_words) {
    if (!max_words) return 0;
    uint32_t first = 0;
    if (!dma_object_address(ch.dma_instance, ch.get, first)) {
        // The per-word path faults here; let it, through its own code.
        ++pfifo_decline_dma_;
        return 0;
    }
    if (max_words > 1) {
        uint32_t last = 0;
        const uint32_t span = 4u * (max_words - 1u);
        if (!dma_object_address(ch.dma_instance, ch.get + span, last) ||
            last != first + span) {
            ++pfifo_decline_dma_;
            return 0;
        }
    }
    const uint8_t* host = pushbuffer_host_bytes(first, 4u * max_words);
    if (!host) {
        ++pfifo_decline_direct_;
        ++pfifo_run_declines_;
        return 0;
    }

    // The same funnel the per-word path opens, at the same bucket, once per run.
    perf_excl::Scope region(perf_excl::kNv2aDispatch);
    std::optional<perf::ScopedAdd> perf_t;
    if (perf::g_pull_drain_enabled) {
        perf_t.emplace(perf::g_ns_nv2a_pull_drain, perf::g_calls_nv2a_pull_drain);
    }
    ++pfifo_runs_batched_;

    PushMethod push{};
    push.channel = channel;
    push.subchannel = ch.subchannel;
    push.parser_put = ch.put;

    // ---- stage (d): the bulk append for the three run-friendly hot methods ---
    //
    // 0x1800 ARRAY_ELEMENT16 alone is 64 011 925 of the run's 83 288 956 method
    // words (76.9 %), it is always a NON-incrementing run, and its handler is two
    // vector appends behind one safety-bound test. Sending it through
    // dispatch_method costs the SET_OBJECT test, the recorder's three pieces, the
    // class histogram, the class2d slot search, three class compares, the table
    // load and the call -- measured at 11.3 ns/word on top of the two appends.
    // The bulk form below reproduces every one of those effects exactly:
    //
    //   * methods_.push runs PER WORD, in order, with the same seq, so the
    //     always-on ring and push_methods() are unchanged;
    //   * method_counts_ and class_method_counts_ take += n instead of ++ per
    //     word. Both are write-only until report time and no handler reads them,
    //     so the totals and every intermediate observation agree;
    //   * class2d_method_stats_ is not touched, which is what dispatch_method
    //     does for class 0x97 (its slot search finds no slot);
    //   * the safety bound is hoisted: the run is declined to the generic form
    //     unless EVERY word of it would pass the handler's own test, so a run
    //     that would fault still faults at the same word through the same code;
    //   * a clear trace in progress declines to the generic form, so the capture
    //     still sees every word through dispatch_method.
    //
    // None of the three handlers can set waiting_for_flip_ / waiting_for_nop_ or
    // clear pgraph_fifo_access_, so the stall re-test the generic loop performs
    // cannot fire inside a bulk run. 0x1808 (9 672 words) and 0x1818 (5 820) are
    // 0.017 % of the run between them and are here for completeness, not for
    // their time.
    if (pfifo_run_bulk_ && ch.non_incrementing && object_class_[ch.subchannel] == 0x97 &&
        !clear_trace_triggered_ && !clear_trace_begin_triggered_ &&
        !waiting_for_flip_ && !waiting_for_nop_ && pgraph_fifo_access_) {
        const uint32_t n = max_words;
        const uint32_t method = ch.method;
        bool bulk = false;
        // Envelope probe for the AFTER measurement; compiled away unless
        // XLLE_DISPATCH_MICRO.
        const uint64_t micro_run = dispatch_micro::begin_run(method);
        if (method == 0x1800) {
            // Handler: `if (size > (1<<24) - 2) fault;` then two appends.
            bulk = vertex_indices_.size() + 2u * static_cast<size_t>(n - 1) <=
                   (1u << 24) - 2u;
            if (bulk) {
                vertex_indices_.reserve(vertex_indices_.size() + 2u * n);
                push.method = 0x1800;
                push.object_class = 0x97;
                for (uint32_t i = 0; i < n; ++i) {
                    uint32_t word = 0;
                    std::memcpy(&word, host + 4u * static_cast<size_t>(i), 4);
                    ch.get += 4;
                    push.parameter = word;
                    push.parser_get = ch.get;
                    push.parser_put = ch.put;
                    push.seq = methods_.total();
                    methods_.push(push);
                    vertex_indices_.push_back(word & 0xFFFFu);
                    vertex_indices_.push_back(word >> 16);
                }
            }
        } else if (method == 0x1808) {
            bulk = vertex_indices_.size() + static_cast<size_t>(n - 1) < (1u << 24);
            if (bulk) {
                vertex_indices_.reserve(vertex_indices_.size() + n);
                push.method = 0x1808;
                push.object_class = 0x97;
                for (uint32_t i = 0; i < n; ++i) {
                    uint32_t word = 0;
                    std::memcpy(&word, host + 4u * static_cast<size_t>(i), 4);
                    ch.get += 4;
                    push.parameter = word;
                    push.parser_get = ch.get;
                    push.parser_put = ch.put;
                    push.seq = methods_.total();
                    methods_.push(push);
                    vertex_indices_.push_back(word);
                }
            }
        } else if (method == 0x1818) {
            bulk = primitive_mode_ != 0 &&
                   inline_array_words_.size() + static_cast<size_t>(n - 1) < (1u << 24);
            if (bulk) {
                inline_array_words_.reserve(inline_array_words_.size() + n);
                push.method = 0x1818;
                push.object_class = 0x97;
                for (uint32_t i = 0; i < n; ++i) {
                    uint32_t word = 0;
                    std::memcpy(&word, host + 4u * static_cast<size_t>(i), 4);
                    ch.get += 4;
                    push.parameter = word;
                    push.parser_get = ch.get;
                    push.parser_put = ch.put;
                    push.seq = methods_.total();
                    methods_.push(push);
                    inline_array_words_.push_back(word);
                }
            }
        }
        dispatch_micro::end_run(micro_run, bulk ? n : 0u);
        if (bulk) {
            method_counts_[method >> 2] += n;
            class_method_counts_[0x97] += n;
            ch.method_count -= n;
            if (perf::g_enabled) {
                for (uint32_t i = 0; i < n; ++i) {
                    perf::record_pfifo_queue_push();
                    perf::record_pfifo_queue_pop();
                }
            }
            ++pfifo_runs_bulk_;
            pfifo_words_bulk_ += n;
            pfifo_words_batched_ += n;
            return n;
        }
    }

    // ---- stage (c): the generic run, one dispatch_method per word ------------
    const uint64_t micro_generic = dispatch_micro::begin_run(ch.method);
    uint32_t consumed = 0;
    uint32_t expected_get = ch.get;
    while (consumed < max_words && !waiting_for_flip_ && !waiting_for_nop_ &&
           pgraph_fifo_access_ && ch.get == expected_get) {
        uint32_t word = 0;
        std::memcpy(&word, host + 4u * static_cast<size_t>(consumed), 4);
        ch.get += 4;
        expected_get = ch.get;
        ++consumed;
        push.method = static_cast<uint16_t>(ch.method);
        push.parameter = word;
        push.parser_get = ch.get;
        push.parser_put = ch.put;
        if (!ch.non_incrementing) ch.method += 4;
        --ch.method_count;
        perf::record_pfifo_queue_push();
        perf::record_pfifo_queue_pop();
        if (perf::g_enabled) {
            perf::ScopedAdd dispatch_timer(perf::g_ns_nv2a_dispatch_method,
                                           perf::g_calls_nv2a_dispatch_method);
            dispatch_method(push);
        } else {
            dispatch_method(push);
        }
    }
    dispatch_micro::end_run(micro_generic, consumed);
    pfifo_words_batched_ += consumed;
    return consumed;
}

bool Nv2a::pusher_enabled(uint8_t channel) const {
    auto reg = [this](uint32_t off) {
        auto it = pfifo_regs_.find(off);
        return it == pfifo_regs_.end() ? 0u : it->second;
    };
    const uint32_t push0 = reg(0x1200);
    const uint32_t push1 = reg(0x1204);
    const uint32_t dma_push = reg(0x1220);
    return (push0 & 1u) && (push1 & 0x100u) &&
           ((push1 & 0x1Fu) == channel) && (dma_push & 1u) &&
           !(dma_push & (1u << 12));
}

void Nv2a::process_pushbuffer(uint8_t channel) {
    // NV2A_PFIFO: word fetch, DMA-object translate, command parse and CACHE1 push. The
    // drain_pull_queue() call inside the parse loop opens NV2A_DISPATCH and is carved out,
    // so this bucket's self time is the pusher alone.
    perf_excl::Scope region(perf_excl::kNv2aPfifo);
    std::optional<perf::ScopedAdd> perf_t;
    if (perf::g_enabled) {
        perf_t.emplace(perf::g_ns_nv2a_pushbuffer,
                       perf::g_calls_nv2a_pushbuffer);
    }
    auto& ch = channels_[channel];
    // CACHE1 is finite hardware storage, not an unbounded host command queue. The
    // pusher may prefetch METHOD/DATA entries while PGRAPH is blocked, but stops at
    // the 256-entry high-water mark and leaves later commands behind DMA_GET.
    if (!mem_read_) {
        fault_("NV2A PFIFO has no unified-memory read callback");
        return;
    }

    // stage (d): a host window never outlives the kick that resolved it.
    push_win_ = nullptr;
    push_win_lo_ = 0;
    push_win_hi_ = 0;

    constexpr uint32_t kMaxWordsPerKick = 4u * 1024u * 1024u;
    uint32_t words = 0;
    while (ch.get != ch.put && pull_queue_.size() < kPullQueueCapacity &&
           words++ < kMaxWordsPerKick) {
        // beads-xbox.1.15.1 stage (c): a RUN of method data words in one pass.
        // Every precondition here is a condition under which the per-word body
        // below would have behaved differently, so failing any of them simply
        // falls through to it:
        //   * pull_queue_ must be EMPTY, or an entry a stalled drain left behind
        //     would be overtaken;
        //   * the puller must not be stalled, or the words belong in the queue
        //     rather than in PGRAPH;
        //   * ch.put must be ahead of ch.get, so (put - get) / 4 is the number of
        //     words this kick may consume without passing PUT. When PUT is behind
        //     GET the parse is walking toward a jump and the word budget is not
        //     computable, so the run is declined.
        // `words` was already incremented for this iteration, so the run's first
        // word is the one this iteration would have processed and the budget is
        // measured from words - 1.
        if (pfifo_run_batching_ && ch.method_count) {
            if (!pull_queue_.empty()) {
                ++pfifo_decline_queue_;
            } else if (waiting_for_flip_ || waiting_for_nop_ || !pgraph_fifo_access_) {
                ++pfifo_decline_stall_;
            } else {
                uint32_t run = ch.method_count;
                // PUT ahead of GET bounds the run: the outer loop stops the
                // moment GET reaches PUT, so a run may not step past it. PUT
                // BEHIND GET cannot be reached by incrementing GET at all (that
                // would need a 2^32 wrap, and a run is at most 2047 words), so
                // in that case ch.method_count is the only bound and capping is
                // neither possible nor needed.
                if (ch.put > ch.get) {
                    const uint32_t avail = (ch.put - ch.get) >> 2;
                    if (run > avail) run = avail;
                }
                const uint32_t budget = kMaxWordsPerKick - (words - 1u);
                if (run > budget) run = budget;
                const uint32_t consumed = drain_method_run(ch, channel, run);
                if (consumed) {
                    words += consumed - 1u;
                    continue;
                }
            }
        }
        // beads-xbox.1.15.1 step 1: whether this word is a method DATA word is
        // known from CACHE1 state before the fetch, so the micro-partition can
        // open the right denominator here rather than reclassifying later.
        // Compiled away entirely unless XLLE_DISPATCH_MICRO.
        uint64_t micro_header_probe = 0;
        if (ch.method_count)
            dispatch_micro::begin_word(ch.method);
        else
            micro_header_probe = dispatch_micro::begin_header_word();
        uint32_t word = 0;
        uint32_t fetch = 0;
        if (!dma_object_address(ch.dma_instance, ch.get, fetch)) {
            char why[192];
            std::snprintf(why, sizeof(why),
                          "NV2A PFIFO DMA invalid: channel=%u instance=0x%08x "
                          "GET=0x%08x PUT=0x%08x",
                          channel, ch.dma_instance, ch.get, ch.put);
            fault_(why);
            return;
        }
        dispatch_micro::mark(dispatch_micro::kPfDma);
        // stage (d): the kick's host window if the span is directly addressable,
        // and mem_read_ (uc_mem_read) otherwise. Both return the same four bytes;
        // see pushbuffer_host_bytes for why that is by construction.
        if (const uint8_t* src = pushbuffer_host_bytes(fetch, 4u)) {
            std::memcpy(&word, src, 4);
        } else if (!mem_read_(fetch, &word, sizeof(word))) {
            char why[128];
            std::snprintf(why, sizeof(why), "NV2A PFIFO read failed at 0x%08x", fetch);
            fault_(why);
            return;
        }
        ch.get += 4;
        dispatch_micro::mark(dispatch_micro::kPfFetch);

        if (ch.method_count) {
            pull_queue_.push_back(PushMethod{
                0, channel, ch.subchannel, 0,
                static_cast<uint16_t>(ch.method), word, ch.get, ch.put});
            perf::record_pfifo_queue_push();
            if (!ch.non_incrementing) ch.method += 4;
            --ch.method_count;
            ++pfifo_words_per_word_;
            dispatch_micro::mark(dispatch_micro::kPfQueue);
            drain_pull_queue();
            dispatch_micro::mark(dispatch_micro::kDrainExit);
            dispatch_micro::end_word();
            continue;
        }

        if ((word & 0xE0000003u) == 0x20000000u) {
            ch.get = word & 0x1FFFFFFFu;                   // old jump
        } else if ((word & 3u) == 1u) {
            ch.get = word & 0xFFFFFFFCu;                   // jump
        } else if ((word & 3u) == 2u) {
            if (ch.subroutine_active) {
                fault_("NV2A PFIFO nested CALL");
                return;
            }
            ch.subroutine = ch.get;
            ch.subroutine_active = true;
            ch.get = word & 0xFFFFFFFCu;                   // call
        } else if (word == 0x00020000u) {
            if (!ch.subroutine_active) {
                fault_("NV2A PFIFO RETURN without CALL");
                return;
            }
            ch.get = ch.subroutine;
            ch.subroutine_active = false;
        } else if ((word & 0xE0030003u) == 0) {
            ch.method = word & 0x1FFFu;
            ch.subchannel = static_cast<uint8_t>((word >> 13) & 7u);
            ch.method_count = (word >> 18) & 0x7FFu;
            ch.non_incrementing = false;
        } else if ((word & 0xE0030003u) == 0x40000000u) {
            ch.method = word & 0x1FFFu;
            ch.subchannel = static_cast<uint8_t>((word >> 13) & 7u);
            ch.method_count = (word >> 18) & 0x7FFu;
            ch.non_incrementing = true;
        } else {
            char why[128];
            std::snprintf(why, sizeof(why),
                          "NV2A PFIFO reserved command 0x%08x at 0x%08x", word, fetch);
            fault_(why);
            return;
        }
        dispatch_micro::end_header_word(micro_header_probe);
    }
    // CACHE1_DMA_GET is the same hardware state exposed through the PFIFO register window
    // and the current channel's USER window. The synchronous parser has completed by the
    // time the CPU can read either aperture, so publish the consumed offset to both views.
    pfifo_regs_[0x1244] = ch.get;
    if (words >= kMaxWordsPerKick) fault_("NV2A PFIFO command guard exhausted");
}

uint32_t Nv2a::pgraph_surface() const {
    return (static_cast<uint32_t>(flip_write_) << 20) |
           (static_cast<uint32_t>(flip_read_) << 24) |
           (static_cast<uint32_t>(flip_modulo_) << 28);
}

Nv2a::ScanoutInfo Nv2a::scanout_info() const {
    ScanoutInfo info;
    info.start = pcrtc_start_;
    const uint32_t pitch_units =
        crtc_regs_[0x13] |
        (static_cast<uint32_t>(crtc_regs_[0x19] & 0xe0u) << 3) |
        (static_cast<uint32_t>(crtc_regs_[0x25] & 0x20u) << 6);
    info.pitch = pitch_units << 3;
    info.width = pramdac_fp_hdisplay_end_ + 1;
    info.height = pramdac_fp_vdisplay_end_ + 1;
    const uint32_t depth = crtc_regs_[0x28] & 3u;
    if (depth == 2)
        info.bits_per_pixel = 16;
    else if (depth == 3)
        info.bits_per_pixel = 32;
    info.rgb565 = (pramdac_general_control_ & (1u << 12)) != 0;
    return info;
}

// Everything capture_scanout and scanout_direct_bytes must agree on: the seam
// drain, the geometry validation and the resident-surface
// resolve. Extracted verbatim from capture_scanout, which keeps calling it first.
bool Nv2a::prepare_scanout(ScanoutInfo& info, uint64_t& source_size, std::string& error) {
    // Seam anchor 3 (F5): scanout reads the whole visible framebuffer out
    // of guest RAM. Guest-anchored gate point.
    seam_anchor(kSeamAnchorScanout, true);
    info = scanout_info();
    if (!mem_read_) {
        error = "NV2A scanout has no guest-memory reader";
        return false;
    }
    if (!info.width || !info.height || info.width > 1920 || info.height > 1080) {
        error = "NV2A scanout dimensions are unset or outside Xbox limits";
        return false;
    }
    if (info.bits_per_pixel != 16 && info.bits_per_pixel != 32) {
        error = "NV2A scanout pixel depth is not modeled";
        return false;
    }
    const uint32_t bytes_per_pixel = info.bits_per_pixel / 8;
    const uint64_t row_bytes = static_cast<uint64_t>(info.width) * bytes_per_pixel;
    if (info.pitch < row_bytes) {
        error = "NV2A scanout pitch is smaller than the visible row";
        return false;
    }
    source_size = static_cast<uint64_t>(info.pitch) * (info.height - 1) + row_bytes;
    if (static_cast<uint64_t>(info.start) + source_size > 64ull * 1024 * 1024) {
        error = "NV2A scanout exceeds unified RAM";
        return false;
    }
    if (!resolve_renderer_color_overlap(info.start, static_cast<size_t>(source_size),
                                        "scanout capture")) {
        error = "NV2A scanout resident color resolve failed";
        return false;
    }
    return true;
}

const uint8_t* Nv2a::scanout_direct_bytes(ScanoutInfo& info, size_t& size,
                                          std::string& error) {
    perf_excl::Scope region(perf_excl::kNv2aScanout);
    size = 0;
    uint64_t source_size = 0;
    if (!prepare_scanout(info, source_size, error)) return nullptr;
    if (!mem_direct_) {
        error = "NV2A scanout has no direct guest-memory pointer";
        return nullptr;
    }
    const uint8_t* bytes = mem_direct_(info.start, static_cast<size_t>(source_size));
    if (!bytes) {
        error = "NV2A scanout range is not contiguously mapped";
        return nullptr;
    }
    size = static_cast<size_t>(source_size);
    error.clear();
    return bytes;
}

bool Nv2a::capture_scanout(std::vector<uint8_t>& rgba, ScanoutInfo& info,
                           std::string& error) {
    // One entry per capture attempt, including its coherence fence and resolve.
    perf_excl::Scope region(perf_excl::kNv2aScanout);
    uint64_t source_size = 0;
    if (!prepare_scanout(info, source_size, error)) return false;
    const uint32_t bytes_per_pixel = info.bits_per_pixel / 8;

    std::vector<uint8_t> source(static_cast<size_t>(source_size));
    if (!mem_read_(info.start, source.data(), source.size())) {
        error = "NV2A scanout guest-memory read failed";
        return false;
    }
    rgba.resize(static_cast<size_t>(info.width) * info.height * 4);
    for (uint32_t y = 0; y < info.height; ++y) {
        for (uint32_t x = 0; x < info.width; ++x) {
            const size_t source_offset =
                static_cast<size_t>(y) * info.pitch + x * bytes_per_pixel;
            const size_t dest_offset =
                (static_cast<size_t>(y) * info.width + x) * 4;
            if (bytes_per_pixel == 4) {
                rgba[dest_offset + 0] = source[source_offset + 2];
                rgba[dest_offset + 1] = source[source_offset + 1];
                rgba[dest_offset + 2] = source[source_offset + 0];
            } else {
                uint16_t pixel = 0;
                std::memcpy(&pixel, source.data() + source_offset, sizeof(pixel));
                const bool rgb565 = (pramdac_general_control_ & (1u << 12)) != 0;
                if (rgb565) {
                    rgba[dest_offset + 0] =
                        static_cast<uint8_t>(((pixel >> 11) & 0x1f) * 255 / 31);
                    rgba[dest_offset + 1] =
                        static_cast<uint8_t>(((pixel >> 5) & 0x3f) * 255 / 63);
                    rgba[dest_offset + 2] =
                        static_cast<uint8_t>((pixel & 0x1f) * 255 / 31);
                } else {
                    rgba[dest_offset + 0] =
                        static_cast<uint8_t>(((pixel >> 10) & 0x1f) * 255 / 31);
                    rgba[dest_offset + 1] =
                        static_cast<uint8_t>(((pixel >> 5) & 0x1f) * 255 / 31);
                    rgba[dest_offset + 2] =
                        static_cast<uint8_t>((pixel & 0x1f) * 255 / 31);
                }
            }
            rgba[dest_offset + 3] = 0xff;
        }
    }
    error.clear();
    return true;
}

// NV_PMC_INTR_0 is computed from the engines' pending&enabled states (xemu nv2a.c
// update_irq): PCRTC contributes bit 24. Other engines (PFIFO bit 8, PGRAPH bit 12) join
// when they gain interrupt sources.
uint32_t Nv2a::pmc_intr_status() const {
    uint32_t v = 0;
    if (pgraph_intr_ & pgraph_intr_en_) v |= 1u << 12;
    if (pcrtc_intr_ & pcrtc_intr_en_) v |= 1u << 24;
    return v;
}

void Nv2a::update_irq() {
    bool level = (pmc_intr_en_ & 0x3) != 0 && pmc_intr_status() != 0;
    if (irq_cb_ && level != irq_asserted_) {
        irq_asserted_ = level;
        irq_cb_(irq_num_, level);
    }
}

void Nv2a::set_frame(uint64_t frame_no) {
    // set_frame is called on every machine slice to advance the frame phase; the
    // FRAME BOUNDARY is the inner condition, and that is the anchor. Raising it
    // at the top would count ~2.9 M slice ticks as vblanks.
    if (frame_no > frame_no_) {
        // Seam anchor 4 (F12): the frame boundary, where the PPMs and the
        // scanout hash are taken. Also closes the per-vblank job window -- the
        // second, independent simulation that gives the ceiling the anchor
        // drains cut into (M2).
        seam_anchor(kSeamAnchorSetFrame, true);
        // DIAGNOSTIC (beads-xbox.1.15.7): the most conservative possible
        // residency policy -- every GPU-authoritative surface is pushed back to
        // guest RAM at every vblank. If a behavioral difference survives this,
        // it is not a missing resolve, because there is no window longer than
        // one frame in which guest memory can be stale.
        if (gpu_vblank_resolve_ && renderer_) {
            resolve_renderer_color_overlap(0, 0x08000000u, "vblank sweep");
            resolve_renderer_depth_overlap(0, 0x08000000u, "vblank sweep");
        }
        // beads-xbox.1.15.10 step 1 (overlap). NOT a resolve: nothing is read
        // back and no guest memory is touched. It only ISSUES the draws the
        // backend is still holding and submits them to the driver, so the GPU
        // starts on them while the emulation thread continues. Order is
        // unchanged -- the pending batch precedes everything that follows it in
        // guest order by construction.
        if (gpu_vblank_kick_ && renderer_) renderer_->kick_pending_gpu_work();
        ++seam_vblanks_;
        seam_vblank_jobs_sum_ += seam_vblank_jobs_;
        seam_vblank_jobs_ = 0;
        frame_no_ = frame_no;
        vblanks_++;
        pcrtc_intr_ |= 1u;  // NV_PCRTC_INTR_0_VBLANK latches every frame boundary
        update_irq();
    }
}

}  // namespace xlle
