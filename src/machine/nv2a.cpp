// xlle — NV2A MMIO register shell. See nv2a.hpp for the value policy.
#include "machine/nv2a.hpp"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>

#include "machine/perf_accounting.hpp"
#include "nv2a_vsh_emulator.h"

namespace xlle {

namespace {

// A draw must see memory written by earlier draws, clears, and the CPU, but reading an
// entire multi-megabyte swizzled target for every small dashboard particle is needlessly
// expensive. Keep coherence at the draw boundary while faulting pages in only when covered.
class PagedRasterSurface {
public:
    using ReadFn = std::function<bool(uint32_t, void*, size_t)>;
    using WriteFn = std::function<bool(uint32_t, const void*, size_t)>;

    PagedRasterSurface(uint32_t address, size_t size,
                       const ReadFn& read, const WriteFn& write)
        : address_(address), size_(size), read_(read), write_(write),
          pages_((size + kPageSize - 1) / kPageSize) {
        perf::record_paged_surface(size_);
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

    bool flush() {
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
    std::vector<Page> pages_;
    size_t resident_bytes_ = 0;
};

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

bool Nv2a::resolve_renderer_color_overlap(uint32_t address, size_t size,
                                          const char* context) {
    if (!renderer_ || size == 0) return true;
    Nv2aResidentColorResolve resolve;
    std::string error;
    if (!renderer_->resolve_resident_color_surface(address, size, resolve, error)) {
        fault_(error.empty() ? "NV2A resident color resolve failed" : error.c_str());
        return false;
    }
    if (resolve.data.empty()) return true;
    if (!mem_write_ ||
        !mem_write_(resolve.address, resolve.data.data(), resolve.data.size())) {
        char why[160];
        std::snprintf(why, sizeof(why),
                      "NV2A resident color resolve DMA write failed before %s",
                      context ? context : "memory access");
        fault_(why);
        return false;
    }
    return true;
}

void Nv2a::invalidate_renderer_color_overlap(uint32_t address, size_t size) {
    if (renderer_ && size != 0)
        renderer_->invalidate_resident_color_surface(address, size);
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
    } else if (blk->off == 0x400000) {  // PGRAPH
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

bool Nv2a::clear_surface_plane(uint32_t dma_instance, bool dma_bound,
                               uint32_t surface_offset, uint32_t pitch,
                               uint32_t bytes_per_pixel, uint32_t write_mask,
                               uint32_t clear_value, const char* plane,
                               uint32_t& first_address) {
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
    if (!resolve_renderer_color_overlap(first_address, resolved_span, "CLEAR_SURFACE"))
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
    invalidate_renderer_color_overlap(first_address, resolved_span);
    return true;
}

bool Nv2a::clear_surface(uint32_t parameter) {
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
                                 color_clear_value_, "color", first_address))
            return false;
        ++color_clears_;
        color_clear_pixels_ += width * height;
        last_color_clear_address_ = first_address;
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
                                 zeta_clear_value_, "zeta", first_address))
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
    if (!mem_read_ ||
        !mem_read_(first_address, data.data(), static_cast<size_t>(element_bytes))) {
        char why[160];
        std::snprintf(why, sizeof(why),
                      "NV2A vertex attribute %u DMA read failed at 0x%08x",
                      slot, first_address);
        fault_(why);
        return false;
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

    ++vertex_attribute_fetches_;
    ++vertex_format_fetches_[attr.format];
    ++vertex_slot_fetches_[slot];
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

bool Nv2a::submit_vertex_batch() {
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

    auto raster_sample = std::find_if(
        raster_state_samples_.begin(), raster_state_samples_.end(),
        [&](const RasterStateSample& sample) {
            return sample.words == raster_state_;
        });
    size_t raster_sample_index = 0;
    if (raster_sample == raster_state_samples_.end()) {
        raster_state_samples_.push_back(RasterStateSample{raster_state_, 1, 0});
        raster_sample_index = raster_state_samples_.size() - 1;
    } else {
        ++raster_sample->batches;
        raster_sample_index =
            static_cast<size_t>(raster_sample - raster_state_samples_.begin());
    }
    auto& tracked_raster_sample =
        raster_state_samples_[raster_sample_index];
    current_raster_state_sample_index_ = static_cast<uint32_t>(raster_sample_index);
    tracked_raster_sample.vertices += vertex_count;
    tracked_raster_sample.inline_batches += inline_array ? 1 : 0;
    ++tracked_raster_sample.primitive_batches[primitive_mode_];
    const bool immediate_quad = immediate && primitive_mode_ == 8;
    bool capture_immediate_quad = false;
    if (immediate_quad) {
        const uint64_t program_hash = fnv1a_bytes(
            transform_program_.data(), sizeof(transform_program_));
        const uint64_t constants_hash = fnv1a_bytes(
            transform_constants_.data(), sizeof(transform_constants_));
        const RasterStateSample::ImmediateQuadTransformVariant identity{
            transform_execution_mode_, transform_program_start_, program_hash, constants_hash};
        ++tracked_raster_sample.immediate_quad_batches;
        const uint32_t count = static_cast<uint32_t>(std::min<size_t>(
            vertex_count, std::numeric_limits<uint32_t>::max()));
        if (tracked_raster_sample.immediate_quad_batches == 1) {
            tracked_raster_sample.immediate_quad_min_vertices = count;
            tracked_raster_sample.immediate_quad_max_vertices = count;
        } else {
            tracked_raster_sample.immediate_quad_min_vertices = std::min(
                tracked_raster_sample.immediate_quad_min_vertices, count);
            tracked_raster_sample.immediate_quad_max_vertices = std::max(
                tracked_raster_sample.immediate_quad_max_vertices, count);
        }
        if (vertex_count % 4 != 0)
            ++tracked_raster_sample.immediate_quad_nonmultiple_of_four;
        capture_immediate_quad =
            !tracked_raster_sample.immediate_quad_first_captured;
        if (capture_immediate_quad) {
            tracked_raster_sample.immediate_quad_first_captured = true;
            tracked_raster_sample.immediate_quad_transform_mode =
                transform_execution_mode_;
            tracked_raster_sample.immediate_quad_transform_start =
                transform_program_start_;
            tracked_raster_sample.immediate_quad_program_hash = program_hash;
            tracked_raster_sample.immediate_quad_constants_hash = constants_hash;
            tracked_raster_sample.immediate_quad_transform_variants[0] = identity;
            tracked_raster_sample.immediate_quad_transform_variant_count = 1;
            ++tracked_raster_sample.immediate_quad_first_transform_batches;
            const size_t captured_vertices = std::min<size_t>(4, vertex_count);
            for (size_t vertex = 0; vertex < captured_vertices; ++vertex) {
                tracked_raster_sample.immediate_quad_decoded_input0[vertex] =
                    immediate_vertices_[vertex][0];
                tracked_raster_sample.immediate_quad_decoded_input3[vertex] =
                    immediate_vertices_[vertex][3];
                tracked_raster_sample.immediate_quad_decoded_input9[vertex] =
                    immediate_vertices_[vertex][9];
            }
        } else {
            const auto& first =
                tracked_raster_sample.immediate_quad_transform_variants[0];
            if (identity.mode == first.mode && identity.start == first.start &&
                identity.program_hash == first.program_hash &&
                identity.constants_hash == first.constants_hash) {
                ++tracked_raster_sample.immediate_quad_first_transform_batches;
            } else {
                ++tracked_raster_sample.immediate_quad_later_transform_batches;
                bool known = false;
                for (uint8_t variant = 0;
                     variant < tracked_raster_sample.immediate_quad_transform_variant_count;
                     ++variant) {
                    const auto& existing =
                        tracked_raster_sample.immediate_quad_transform_variants[variant];
                    known = existing.mode == identity.mode &&
                            existing.start == identity.start &&
                            existing.program_hash == identity.program_hash &&
                            existing.constants_hash == identity.constants_hash;
                    if (known) break;
                }
                if (!known) {
                    if (tracked_raster_sample.immediate_quad_transform_variant_count <
                        tracked_raster_sample.immediate_quad_transform_variants.size()) {
                        tracked_raster_sample.immediate_quad_transform_variants[
                            tracked_raster_sample.immediate_quad_transform_variant_count++] =
                            identity;
                    } else {
                        ++tracked_raster_sample
                              .immediate_quad_transform_variant_overflow_batches;
                    }
                }
            }
        }
    }

    ++transform_mode_batches_[transform_execution_mode_ & 3u];
    if (transform_execution_mode_ == 0) {
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
    Nv2aVshProgram program{};
    bool program_parsed = false;
    bool fixed_function = false;
    if (transform_execution_mode_ == 2) {
        const uint32_t slots =
            static_cast<uint32_t>(transform_program_.size()) - transform_program_start_;
        Nv2aVshParseResult result = nv2a_vsh_parse_program(
            &program, transform_program_[transform_program_start_].data(), slots);
        if (result != NV2AVPR_SUCCESS) {
            char why[128];
            std::snprintf(why, sizeof(why),
                          "NV2A vertex program parse failed at slot %u: result=%u",
                          transform_program_start_, static_cast<unsigned>(result));
            fault_(why);
            return false;
        }
        program_parsed = true;
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
    transformed_batch_.clear();
    transformed_batch_.reserve(vertex_count);

    for (size_t vertex_index = 0; vertex_index < vertex_count; ++vertex_index) {
        if (inline_array || immediate) {
            const auto& inputs = inline_array ? inline_vertices_[vertex_index]
                                              : immediate_vertices_[vertex_index];
            for (uint32_t slot = 0; slot < vertex_attributes_.size(); ++slot)
                vertex_attributes_[slot].value = inputs[slot];
        } else {
            const uint32_t index = vertex_indices_[vertex_index];
            largest_vertex_index_ = std::max(largest_vertex_index_, index);
            for (uint32_t slot = 0; slot < vertex_attributes_.size(); ++slot) {
                if (!fetch_vertex_attribute(slot, index)) {
                    if (program_parsed) nv2a_vsh_program_destroy(&program);
                    return false;
                }
            }
        }
        TransformedVertex vertex;
        if (program_parsed) {
            Nv2aVshCPUFullExecutionState full_state;
            Nv2aVshExecutionState execution =
                nv2a_vsh_emu_initialize_full_execution_state(&full_state);
            std::memcpy(full_state.context_regs, transform_constants_.data(),
                        sizeof(full_state.context_regs));
            for (uint32_t slot = 0; slot < vertex_attributes_.size(); ++slot) {
                std::memcpy(&full_state.input_regs[slot * 4],
                            vertex_attributes_[slot].value.data(), 4 * sizeof(float));
            }
            nv2a_vsh_emu_execute(&execution, &program);

            if (capture_immediate_quad && vertex_index < 4) {
                auto& evidence = raster_state_samples_[raster_sample_index];
                std::memcpy(evidence.immediate_quad_opos[vertex_index].data(),
                            &full_state.output_regs[0], 4 * sizeof(float));
                std::memcpy(evidence.immediate_quad_od0[vertex_index].data(),
                            &full_state.output_regs[3 * 4], 4 * sizeof(float));
                std::memcpy(evidence.immediate_quad_ot0[vertex_index].data(),
                            &full_state.output_regs[9 * 4], 4 * sizeof(float));
            }

            std::memcpy(vertex.position.data(), &full_state.output_regs[0], 4 * sizeof(float));
            vertex.position[0] = round_screen_coordinate(vertex.position[0]);
            vertex.position[1] = round_screen_coordinate(vertex.position[1]);
            vertex.position[3] = clamp_away_zero_inf(vertex.position[3]);
            if (capture_immediate_quad && vertex_index < 4)
                raster_state_samples_[raster_sample_index]
                    .immediate_quad_raster_opos[vertex_index] = vertex.position;
            std::memcpy(vertex.diffuse.data(), &full_state.output_regs[3 * 4],
                        4 * sizeof(float));
            std::memcpy(vertex.specular.data(), &full_state.output_regs[4 * 4],
                        4 * sizeof(float));
            for (uint32_t texture = 0; texture < 4; ++texture) {
                std::memcpy(vertex.texture[texture].data(),
                            &full_state.output_regs[(9 + texture) * 4],
                            4 * sizeof(float));
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
        }
        if (program_parsed || fixed_function) {
            transformed_batch_.push_back(vertex);
            for (float component : vertex.position) {
                uint32_t bits = 0;
                std::memcpy(&bits, &component, sizeof(bits));
                transform_output_hash_ ^= bits;
                transform_output_hash_ *= 1099511628211ull;
            }
            ++transformed_vertices_;
        }
    }
    if (program_parsed) nv2a_vsh_program_destroy(&program);
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
    bool raster_executed = false;
    bool raster_ok;
    {
        perf::ScopedAdd t(perf::g_ns_raster, perf::g_calls_raster);
        raster_ok = rasterize_witnessed_batch(raster_executed);
    }
    if (!raster_ok) return false;
    if (raster_executed)
        ++raster_state_samples_[raster_sample_index].executed_batches;

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
    return true;
}

bool Nv2a::rasterize_witnessed_immediate_quad(bool& executed) {
    executed = false;

    // The authoritative 11.5B capture exposes alternating A4, I8/palette,
    // RS8/RS13 A8R8G8B8, RS16/RS17 I8 menu-column, and IQRS3/IQRS7 upper-left
    // I8 artwork immediate quads. Keep every other mode-8 state
    // recorder-only until its own texture/combiner behavior is witnessed.
    const bool immediate_source =
        !immediate_vertices_.empty() &&
        immediate_vertices_.size() == transformed_batch_.size();
    const size_t vertex_count = transformed_batch_.size();
    const uint32_t color_offset = surface_color_offset_;
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
        surface_format_ == 0x00000128 &&
        surface_pitch_ == 0x00400C00 && witnessed_target &&
        surface_zeta_offset_ == 0 &&
        surface_clip_horizontal_ == 0x00B4010E &&
        surface_clip_vertical_ == 0x00D7009E &&
        raster_state_[kRasterWindowClipHorizontal] == 0x02D00000 &&
        raster_state_[kRasterWindowClipVertical] == 0x01E00000;
    const bool menu_i8_surface_state =
        surface_format_ == 0x00000128 &&
        surface_pitch_ == 0x00400C00 && witnessed_target &&
        surface_zeta_offset_ == 0 &&
        surface_clip_horizontal_ == 0x02D00000 &&
        surface_clip_vertical_ == 0x01E00000 &&
        raster_state_[kRasterWindowClipHorizontal] == 0x02D00000 &&
        raster_state_[kRasterWindowClipVertical] == 0x01E00000;
    uint32_t a4_raster_reject_mask = 0;
    auto reject_raster = [&](size_t bit, bool matches) {
        if (!matches) a4_raster_reject_mask |= 1u << bit;
    };
    reject_raster(0, raster_state_[kRasterEnables] == 0x00000007);
    reject_raster(1, raster_state_[kRasterControl0] == 0x00100001);
    reject_raster(2, raster_state_[kRasterAlphaFunc] == 0x0206);
    // NV097_SET_ALPHA_REF is an 8-bit comparison value. Preserve the raw
    // retained word in telemetry, but only its guest-visible low byte gates.
    reject_raster(3, (raster_state_[kRasterAlphaRef] & 0xFFu) == 8);
    reject_raster(4, raster_state_[kRasterBlendSrc] == 0x0302);
    reject_raster(5, raster_state_[kRasterBlendDst] == 0x0303);
    reject_raster(6, raster_state_[kRasterBlendEquation] == 0x8006);
    reject_raster(7, raster_state_[kRasterDepthFunc] == 0x0203);
    reject_raster(8, raster_state_[kRasterColorMask] == 0x01010101);
    reject_raster(9, raster_state_[kRasterDepthMask] == 1);
    reject_raster(10, raster_state_[kRasterShadeMode] == 0x1D01);
    reject_raster(11,
                  raster_state_[kRasterPolygonOffsetScale] == 0x80000000);
    reject_raster(12,
                  raster_state_[kRasterPolygonOffsetBias] == 0x80000000);
    reject_raster(13, raster_state_[kRasterFrontPolygonMode] == 0x1B02);
    reject_raster(14, raster_state_[kRasterBackPolygonMode] == 0x1B02);
    reject_raster(15, raster_state_[kRasterClipMin] == 0);
    reject_raster(16, raster_state_[kRasterClipMax] == 0);
    reject_raster(17, raster_state_[kRasterCullFace] == 0x0405);
    reject_raster(18, raster_state_[kRasterFrontFace] == 0x0900);
    reject_raster(19, raster_state_[kRasterZMinMaxControl] == 1);
    reject_raster(20, raster_state_[kRasterShadowDepthFunc] == 0);
    const bool a4_raster_state = a4_raster_reject_mask == 0;
    uint32_t i8_raster_reject_mask = 0;
    auto reject_i8_raster = [&](size_t bit, bool matches) {
        if (!matches) i8_raster_reject_mask |= 1u << bit;
    };
    reject_i8_raster(0, raster_state_[kRasterEnables] == 0x00000006);
    reject_i8_raster(1, raster_state_[kRasterControl0] == 0x00100001);
    reject_i8_raster(2, raster_state_[kRasterAlphaFunc] == 0x0207);
    reject_i8_raster(3, (raster_state_[kRasterAlphaRef] & 0xFFu) == 0);
    reject_i8_raster(4, raster_state_[kRasterBlendSrc] == 0x0302);
    reject_i8_raster(5, raster_state_[kRasterBlendDst] == 0x0303);
    reject_i8_raster(6, raster_state_[kRasterBlendEquation] == 0x8006);
    reject_i8_raster(7, raster_state_[kRasterDepthFunc] == 0x0203);
    reject_i8_raster(8, raster_state_[kRasterColorMask] == 0x01010101);
    reject_i8_raster(9, raster_state_[kRasterDepthMask] == 1);
    reject_i8_raster(10, raster_state_[kRasterShadeMode] == 0x1D01);
    reject_i8_raster(11, raster_state_[kRasterPolygonOffsetScale] == 0x80000000);
    reject_i8_raster(12, raster_state_[kRasterPolygonOffsetBias] == 0x80000000);
    reject_i8_raster(13, raster_state_[kRasterFrontPolygonMode] == 0x1B02);
    reject_i8_raster(14, raster_state_[kRasterBackPolygonMode] == 0x1B02);
    reject_i8_raster(15, raster_state_[kRasterClipMin] == 0);
    reject_i8_raster(16, raster_state_[kRasterClipMax] == 0);
    reject_i8_raster(17, raster_state_[kRasterCullFace] == 0x0405);
    reject_i8_raster(18, raster_state_[kRasterFrontFace] == 0x0900);
    reject_i8_raster(19, raster_state_[kRasterZMinMaxControl] == 1);
    reject_i8_raster(20, raster_state_[kRasterShadowDepthFunc] == 0);
    const bool i8_raster_state = i8_raster_reject_mask == 0;
    const bool stencil_state =
        raster_state_[kRasterStencilFunc] == 0x0207 &&
        raster_state_[kRasterStencilRef] == 0 &&
        raster_state_[kRasterStencilFuncMask] == 0xFFFFFFFF &&
        raster_state_[kRasterStencilOpFail] == 0x1E00 &&
        raster_state_[kRasterStencilOpZFail] == 0x1E00 &&
        raster_state_[kRasterStencilOpZPass] == 0x1E00;
    const bool a4_combiner_state =
        raster_state_[kRasterTextureEnableMask] == 1 &&
        raster_state_[kRasterShaderStageProgram] == 1 &&
        raster_state_[kRasterShaderOtherStageInput] == 0 &&
        raster_state_[kRasterCombinerControl] == 1 &&
        raster_state_[kRasterCombinerAlphaIcw0] == 0x18140000 &&
        raster_state_[kRasterCombinerColorIcw0] == 0x08040000 &&
        raster_state_[kRasterCombinerAlphaOcw0] == 0x000000C0 &&
        raster_state_[kRasterCombinerColorOcw0] == 0x000000C0 &&
        raster_state_[kRasterCombinerFactor0_0] == 0 &&
        raster_state_[kRasterCombinerFactor1_0] == 0 &&
        raster_state_[kRasterCombinerSpecularFog0] == 0x010C0002 &&
        raster_state_[kRasterCombinerSpecularFog1] == 0x00001C00 &&
        raster_state_[kRasterCombinerFinalFactor0] == 0x00DADADA &&
        raster_state_[kRasterCombinerFinalFactor1] == 0x00101010;
    const bool a4_texture_state =
        raster_state_[kRasterTextureOffset0] == 0x03C0C000 &&
        raster_state_[kRasterTextureFormat0] == 0x08810429 &&
        raster_state_[kRasterTextureAddress0] == 0x00010303 &&
        raster_state_[kRasterTextureFilter0] == 0x02022000 &&
        raster_state_[kRasterTextureControl0_0] == 0x4003FFC0 &&
        raster_state_[kRasterTextureControl1_0] == 0x01C00000 &&
        raster_state_[kRasterTextureImageRect0] == 0x00640011 &&
        raster_state_[kRasterTexturePalette0] == 0x02DED080 &&
        raster_state_[kRasterTextureBorderColor0] == 0;
    const bool rs12_candidate =
        witnessed_target && vertex_count >= 28 && vertex_count <= 44 &&
        raster_state_[kRasterAlphaFunc] == 0x0206 &&
        (raster_state_[kRasterAlphaRef] & 0xFFu) == 8 &&
        raster_state_[kRasterTextureFormat0] == 0x08810429;
    const bool i8_combiner_state =
        raster_state_[kRasterTextureEnableMask] == 1 &&
        raster_state_[kRasterShaderStageProgram] == 1 &&
        raster_state_[kRasterShaderOtherStageInput] == 0x00210000 &&
        raster_state_[kRasterCombinerControl] == 1 &&
        raster_state_[kRasterCombinerAlphaIcw0] == 0x18140000 &&
        raster_state_[kRasterCombinerColorIcw0] == 0x08040000 &&
        raster_state_[kRasterCombinerAlphaOcw0] == 0x00000C00 &&
        raster_state_[kRasterCombinerColorOcw0] == 0x00000C00 &&
        raster_state_[kRasterCombinerFactor0_0] == 0xFFFFFFFF &&
        raster_state_[kRasterCombinerFactor1_0] == 0xFFFFFFFF &&
        raster_state_[kRasterCombinerSpecularFog0] == 0x0000000C &&
        raster_state_[kRasterCombinerSpecularFog1] == 0x00001C80 &&
        raster_state_[kRasterCombinerFinalFactor0] == 0x00DADADA &&
        raster_state_[kRasterCombinerFinalFactor1] == 0x00101010;
    const bool i8_texture_state =
        raster_state_[kRasterTextureOffset0] == 0x02DED480 &&
        raster_state_[kRasterTextureFormat0] == 0x05710B29 &&
        raster_state_[kRasterTextureAddress0] == 0x00010303 &&
        raster_state_[kRasterTextureFilter0] == 0x02022000 &&
        raster_state_[kRasterTextureControl0_0] == 0x4003FFC4 &&
        raster_state_[kRasterTextureControl1_0] == 0x01C00000 &&
        raster_state_[kRasterTextureImageRect0] == 0x00640011 &&
        raster_state_[kRasterTexturePalette0] == 0x02DED080 &&
        raster_state_[kRasterTextureBorderColor0] == 0;
    // RS8/RS13 are the format-6 sibling of the I8 pair: the same 128x32
    // swizzle and captured pixel state, but direct little-endian A8R8G8B8
    // texels instead of palette indices.  The trace pins the two target
    // offsets, T0 offset, and every admitted state word below.
    const bool a8_texture_state =
        raster_state_[kRasterTextureOffset0] == 0x02DEF080 &&
        raster_state_[kRasterTextureFormat0] == 0x05710629 &&
        raster_state_[kRasterTextureAddress0] == 0x00010303 &&
        raster_state_[kRasterTextureFilter0] == 0x02022000 &&
        raster_state_[kRasterTextureControl0_0] == 0x4003FFC4 &&
        raster_state_[kRasterTextureControl1_0] == 0x01C00000 &&
        raster_state_[kRasterTextureImageRect0] == 0x00640011 &&
        raster_state_[kRasterTextureBorderColor0] == 0;
    // RS16/RS17 use the distinct format-0x0b indexed texture: format sizes
    // encode a 64x128 swizzle, with a palette immediately before T0.  The
    // exact nonzero final factors exclude the captured one-off lookalike.
    const bool menu_i8_texture_state =
        raster_state_[kRasterTextureOffset0] == 0x039FE800 &&
        raster_state_[kRasterTextureFormat0] == 0x07610B29 &&
        raster_state_[kRasterTextureAddress0] == 0x00010303 &&
        raster_state_[kRasterTextureFilter0] == 0x02022000 &&
        raster_state_[kRasterTextureControl0_0] == 0x4003FFC4 &&
        raster_state_[kRasterTextureControl1_0] == 0x01C00000 &&
        raster_state_[kRasterTextureImageRect0] == 0x00640011 &&
        raster_state_[kRasterTexturePalette0] == 0x039FE400 &&
        raster_state_[kRasterTextureBorderColor0] == 0;
    // IQRS3/IQRS7 are an independently witnessed upper-left artwork layer:
    // a 512x128 indexed texture immediately following its A8R8G8B8 palette.
    const bool artwork_i8_texture_state =
        raster_state_[kRasterTextureOffset0] == 0x02DF4480 &&
        raster_state_[kRasterTextureFormat0] == 0x07910B29 &&
        raster_state_[kRasterTextureAddress0] == 0x00010303 &&
        raster_state_[kRasterTextureFilter0] == 0x02022000 &&
        raster_state_[kRasterTextureControl0_0] == 0x4003FFC4 &&
        raster_state_[kRasterTextureControl1_0] == 0x01C00000 &&
        raster_state_[kRasterTextureImageRect0] == 0x00640011 &&
        raster_state_[kRasterTexturePalette0] == 0x02DF4080 &&
        raster_state_[kRasterTextureBorderColor0] == 0;
    // IQRS5/IQRS10 are the direct-color 512x512 main-panel pair. Palette0 is
    // intentionally passive for format 6, just as for RS8/RS13.
    const bool main_panel_texture_state =
        raster_state_[kRasterTextureOffset0] == 0x034FC080 &&
        raster_state_[kRasterTextureFormat0] == 0x09910629 &&
        raster_state_[kRasterTextureAddress0] == 0x00010303 &&
        raster_state_[kRasterTextureFilter0] == 0x02022000 &&
        raster_state_[kRasterTextureControl0_0] == 0x4003FFC4 &&
        raster_state_[kRasterTextureControl1_0] == 0x01C00000 &&
        raster_state_[kRasterTextureImageRect0] == 0x00640011 &&
        raster_state_[kRasterTextureBorderColor0] == 0;
    // IQRS4/IQRS7 are the 1024x1024 direct-color full-screen pair. Palette0
    // is stale/passive for format 6; nonzero final factors exclude IQRS12.
    const bool fullscreen_texture_state =
        raster_state_[kRasterTextureOffset0] == 0x035FD080 &&
        raster_state_[kRasterTextureFormat0] == 0x0AA10629 &&
        raster_state_[kRasterTextureAddress0] == 0x00010303 &&
        raster_state_[kRasterTextureFilter0] == 0x02022000 &&
        raster_state_[kRasterTextureControl0_0] == 0x4003FFC4 &&
        raster_state_[kRasterTextureControl1_0] == 0x01C00000 &&
        raster_state_[kRasterTextureImageRect0] == 0x00640011 &&
        raster_state_[kRasterTextureBorderColor0] == 0;
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
        raster_state_[kRasterAlphaFunc] == 0x0207 &&
        (raster_state_[kRasterAlphaRef] & 0xFFu) == 0 &&
        raster_state_[kRasterTextureFormat0] == 0x05710B29;
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
        raster_state_[kRasterAlphaFunc] == 0x0207 &&
        (raster_state_[kRasterAlphaRef] & 0xFFu) == 0 &&
        raster_state_[kRasterTextureFormat0] == 0x05710629;
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
    if (raster_state_[kRasterCombinerFinalFactor0] != 0x00DADADA ||
        raster_state_[kRasterCombinerFinalFactor1] != 0x00101010)
        menu_i8_reject_mask |= 1u << 7;
    const bool menu_i8_candidate =
        immediate_source && witnessed_target && vertex_count == 4 &&
        raster_state_[kRasterAlphaFunc] == 0x0207 &&
        (raster_state_[kRasterAlphaRef] & 0xFFu) == 0 &&
        raster_state_[kRasterTextureFormat0] == 0x07610B29 &&
        raster_state_[kRasterCombinerFinalFactor0] == 0x00DADADA &&
        raster_state_[kRasterCombinerFinalFactor1] == 0x00101010;
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
    if (raster_state_[kRasterCombinerFinalFactor0] != 0x00DADADA ||
        raster_state_[kRasterCombinerFinalFactor1] != 0x00101010)
        artwork_i8_reject_mask |= 1u << 7;
    const bool artwork_i8_candidate =
        immediate_source && witnessed_target && vertex_count == 4 &&
        raster_state_[kRasterAlphaFunc] == 0x0207 &&
        (raster_state_[kRasterAlphaRef] & 0xFFu) == 0 &&
        raster_state_[kRasterTextureOffset0] == 0x02DF4480 &&
        raster_state_[kRasterTextureFormat0] == 0x07910B29 &&
        raster_state_[kRasterCombinerFinalFactor0] == 0x00DADADA &&
        raster_state_[kRasterCombinerFinalFactor1] == 0x00101010;
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
    if (raster_state_[kRasterCombinerFinalFactor0] != 0x00DADADA ||
        raster_state_[kRasterCombinerFinalFactor1] != 0x00101010)
        main_panel_reject_mask |= 1u << 7;
    const bool main_panel_candidate =
        immediate_source && witnessed_target && vertex_count == 4 &&
        raster_state_[kRasterAlphaFunc] == 0x0207 &&
        (raster_state_[kRasterAlphaRef] & 0xFFu) == 0 &&
        raster_state_[kRasterTextureOffset0] == 0x034FC080 &&
        raster_state_[kRasterTextureFormat0] == 0x09910629 &&
        raster_state_[kRasterCombinerFinalFactor0] == 0x00DADADA &&
        raster_state_[kRasterCombinerFinalFactor1] == 0x00101010;
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
    if (raster_state_[kRasterCombinerFinalFactor0] != 0x00DADADA ||
        raster_state_[kRasterCombinerFinalFactor1] != 0x00101010)
        fullscreen_reject_mask |= 1u << 7;
    const bool fullscreen_candidate =
        immediate_source && witnessed_target && vertex_count == 4 &&
        raster_state_[kRasterAlphaFunc] == 0x0207 &&
        (raster_state_[kRasterAlphaRef] & 0xFFu) == 0 &&
        raster_state_[kRasterTextureOffset0] == 0x035FD080 &&
        raster_state_[kRasterTextureFormat0] == 0x0AA10629 &&
        raster_state_[kRasterCombinerFinalFactor0] == 0x00DADADA &&
        raster_state_[kRasterCombinerFinalFactor1] == 0x00101010;
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
                       raster_state_[kRasterTextureOffset0], texture_bytes,
                       texture_address, texture_name)) {
        return false;
    }

    uint32_t palette_address = 0;
    if ((i8_profile || menu_i8_profile || artwork_i8_profile) &&
        !resolve_range(texture_dma_a_, raster_state_[kRasterTexturePalette0] & ~0x3Fu,
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

    const uint32_t clip_x = surface_clip_horizontal_ & 0xFFFFu;
    const uint32_t clip_width = surface_clip_horizontal_ >> 16;
    const uint32_t clip_y = surface_clip_vertical_ & 0xFFFFu;
    const uint32_t clip_height = surface_clip_vertical_ >> 16;
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
        rasterize_triangle(transformed_batch_[i + 1],
                           transformed_batch_[i + 2],
                           transformed_batch_[i]);
        rasterize_triangle(transformed_batch_[i + 2],
                           transformed_batch_[i + 3],
                           transformed_batch_[i]);
    }
    if (failed) return false;
    if (!color_surface.flush()) {
        fault_("NV2A immediate-quad color DMA write failed");
        return false;
    }
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

bool Nv2a::rasterize_witnessed_batch(bool& executed) {
    executed = false;
    if (primitive_mode_ == 8)
        return rasterize_witnessed_immediate_quad(executed);
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

    const uint32_t enables = raster_state_[kRasterEnables];
    const uint32_t color_format = surface_format_ & 0xFu;
    const uint32_t surface_type = (surface_format_ >> 8) & 0xFu;
    const uint32_t anti_aliasing = (surface_format_ >> 12) & 0xFu;
    const uint32_t zeta_format = (surface_format_ >> 4) & 0xFu;
    const bool supported_primitive =
        primitive_mode_ == 5 || primitive_mode_ == 6 || primitive_mode_ == 7;
    // Recorder-only primitive families still travel through transform/assembly, but must
    // not enter a witnessed triangle-raster profile (or its validation path).
    if (!supported_primitive) return true;
    const bool depth_only =
        raster_state_[kRasterColorMask] == 0 &&
        (enables & (1u << 3)) != 0 &&
        raster_state_[kRasterDepthMask] != 0;
    const bool radial_combiner =
        raster_state_[kRasterTextureEnableMask] == 0 &&
        raster_state_[kRasterShaderStageProgram] == 0x00000004 &&
        raster_state_[kRasterShaderOtherStageInput] == 0 &&
        raster_state_[kRasterDotRgbMapping] == 0 &&
        raster_state_[kRasterCombinerControl] == 0x00011103 &&
        raster_state_[kRasterCombinerColorIcw0 + 0] == 0x48480000 &&
        raster_state_[kRasterCombinerColorIcw0 + 1] == 0x2D2D0000 &&
        raster_state_[kRasterCombinerColorIcw0 + 2] == 0xC4CD0000 &&
        raster_state_[kRasterCombinerAlphaIcw0 + 0] == 0x00000000 &&
        raster_state_[kRasterCombinerAlphaIcw0 + 1] == 0x3D3D1010 &&
        raster_state_[kRasterCombinerAlphaIcw0 + 2] == 0xD4DD1010 &&
        raster_state_[kRasterCombinerColorOcw0 + 0] == 0x000820D0 &&
        raster_state_[kRasterCombinerColorOcw0 + 1] == 0x000000D0 &&
        raster_state_[kRasterCombinerColorOcw0 + 2] == 0x000000C0 &&
        raster_state_[kRasterCombinerAlphaOcw0 + 0] == 0x00000000 &&
        raster_state_[kRasterCombinerAlphaOcw0 + 1] == 0x000000D0 &&
        raster_state_[kRasterCombinerAlphaOcw0 + 2] == 0x000000C0 &&
        raster_state_[kRasterCombinerFactor0_0 + 0] == 0 &&
        raster_state_[kRasterCombinerFactor0_0 + 1] == 0 &&
        raster_state_[kRasterCombinerFactor0_0 + 2] == 0 &&
        raster_state_[kRasterCombinerFactor1_0 + 0] == 0 &&
        raster_state_[kRasterCombinerFactor1_0 + 1] == 0 &&
        raster_state_[kRasterCombinerFactor1_0 + 2] == 0 &&
        raster_state_[kRasterCombinerSpecularFog0] == 0x0000000C &&
        raster_state_[kRasterCombinerSpecularFog1] == 0x00002080;
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
        raster_state_[kRasterTextureEnableMask] == 0xFu &&
        raster_state_[kRasterShaderStageProgram] == 0x00010C60 &&
        raster_state_[kRasterShaderOtherStageInput] == 0 &&
        raster_state_[kRasterDotRgbMapping] == 0 &&
        (raster_state_[kRasterShadowDepthFunc] == 0 ||
         raster_state_[kRasterShadowDepthFunc] == 4) &&
        raster_state_[kRasterCombinerControl] == 0x00011108 &&
        raster_state_[kRasterCombinerSpecularFog0] == 0x0C0B000F &&
        raster_state_[kRasterCombinerSpecularFog1] == 0x0C011180 &&
        raster_state_[kRasterCombinerFinalFactor0] == 0xFF3F3F3F &&
        raster_state_[kRasterCombinerFinalFactor1] == 0;
    for (size_t stage = 0; stage < 8 && textured_combiner; ++stage) {
        textured_combiner =
            raster_state_[kRasterCombinerColorIcw0 + stage] ==
                textured_color_icw[stage] &&
            raster_state_[kRasterCombinerAlphaIcw0 + stage] ==
                textured_alpha_icw[stage] &&
            raster_state_[kRasterCombinerColorOcw0 + stage] ==
                textured_color_ocw[stage] &&
            raster_state_[kRasterCombinerAlphaOcw0 + stage] == 0 &&
            (stage == 6 ||
             (raster_state_[kRasterCombinerFactor0_0 + stage] ==
                  textured_factor0[stage] &&
              raster_state_[kRasterCombinerFactor1_0 + stage] ==
                  textured_factor1[stage]));
    }
    const bool textured_fixed_stage6 =
        raster_state_[kRasterCombinerFactor0_0 + 6] ==
            textured_factor0[6] &&
        raster_state_[kRasterCombinerFactor1_0 + 6] ==
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
        raster_state_[kRasterTextureEnableMask] == 0xFu &&
        raster_state_[kRasterShaderStageProgram] == 0x00010C61 &&
        raster_state_[kRasterShaderOtherStageInput] == 0 &&
        raster_state_[kRasterDotRgbMapping] == 0 &&
        (raster_state_[kRasterShadowDepthFunc] == 0 ||
         raster_state_[kRasterShadowDepthFunc] == 4) &&
        raster_state_[kRasterCombinerControl] == 0x00011107 &&
        raster_state_[kRasterCombinerSpecularFog0] == 0x0C0B000F &&
        raster_state_[kRasterCombinerSpecularFog1] == 0x0C011180 &&
        raster_state_[kRasterCombinerFinalFactor0] == 0xFF3F3F3F &&
        raster_state_[kRasterCombinerFinalFactor1] == 0;
    for (size_t stage = 0; stage < 7 && projected_combiner; ++stage) {
        projected_combiner =
            raster_state_[kRasterCombinerColorIcw0 + stage] ==
                projected_color_icw[stage] &&
            raster_state_[kRasterCombinerAlphaIcw0 + stage] ==
                projected_alpha_icw[stage] &&
            raster_state_[kRasterCombinerColorOcw0 + stage] ==
                projected_color_ocw[stage] &&
            raster_state_[kRasterCombinerAlphaOcw0 + stage] == 0 &&
            (stage == 5 ||
             (raster_state_[kRasterCombinerFactor0_0 + stage] ==
                  projected_factor0[stage] &&
              raster_state_[kRasterCombinerFactor1_0 + stage] ==
                  projected_factor1[stage]));
    }
    const bool projected_fixed_stage5 =
        raster_state_[kRasterCombinerFactor0_0 + 5] ==
            projected_factor0[5] &&
        raster_state_[kRasterCombinerFactor1_0 + 5] ==
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
        raster_state_[kRasterTextureEnableMask] == 3 &&
        raster_state_[kRasterShaderStageProgram] == 0x00000063 &&
        raster_state_[kRasterShaderOtherStageInput] == 0 &&
        raster_state_[kRasterDotRgbMapping] == 0 &&
        raster_state_[kRasterShadowDepthFunc] == 7 &&
        raster_state_[kRasterCombinerControl] == 0x00011106 &&
        raster_state_[kRasterCombinerSpecularFog0] == 0x0000000C &&
        raster_state_[kRasterCombinerSpecularFog1] == 0x00001C80 &&
        raster_state_[kRasterCombinerFinalFactor0] == 0 &&
        raster_state_[kRasterCombinerFinalFactor1] == 0;
    for (size_t stage = 0;
         stage < pitch_multi_color_icw.size() &&
         pitch_multi_texture_combiner;
         ++stage) {
        pitch_multi_texture_combiner =
            raster_state_[kRasterCombinerColorIcw0 + stage] ==
                pitch_multi_color_icw[stage] &&
            raster_state_[kRasterCombinerAlphaIcw0 + stage] ==
                pitch_multi_alpha_icw[stage] &&
            raster_state_[kRasterCombinerColorOcw0 + stage] ==
                pitch_multi_color_ocw[stage] &&
            raster_state_[kRasterCombinerAlphaOcw0 + stage] ==
                pitch_multi_alpha_ocw[stage] &&
            raster_state_[kRasterCombinerFactor0_0 + stage] ==
                pitch_multi_factor0[stage] &&
            raster_state_[kRasterCombinerFactor1_0 + stage] == 0;
    }
    bool pitch_three_texture_combiner =
        raster_state_[kRasterTextureEnableMask] == 7 &&
        raster_state_[kRasterShaderStageProgram] == 0x00000063 &&
        raster_state_[kRasterShaderOtherStageInput] == 0 &&
        raster_state_[kRasterDotRgbMapping] == 0 &&
        raster_state_[kRasterShadowDepthFunc] == 7 &&
        raster_state_[kRasterCombinerControl] == 0x00011106 &&
        raster_state_[kRasterCombinerSpecularFog0] == 0x0000000C &&
        raster_state_[kRasterCombinerSpecularFog1] == 0x00001C80 &&
        raster_state_[kRasterCombinerFinalFactor0] == 0 &&
        raster_state_[kRasterCombinerFinalFactor1] == 0;
    for (size_t stage = 0;
         stage < pitch_multi_color_icw.size() &&
         pitch_three_texture_combiner;
         ++stage) {
        const uint32_t factor0 =
            raster_state_[kRasterCombinerFactor0_0 + stage];
        const bool witnessed_factor0 =
            stage == 3 ? (factor0 & 0xFF000000u) == 0xFF000000u
                       : factor0 == pitch_multi_factor0[stage];
        pitch_three_texture_combiner =
            raster_state_[kRasterCombinerColorIcw0 + stage] ==
                pitch_multi_color_icw[stage] &&
            raster_state_[kRasterCombinerAlphaIcw0 + stage] ==
                pitch_multi_alpha_icw[stage] &&
            raster_state_[kRasterCombinerColorOcw0 + stage] ==
                pitch_multi_color_ocw[stage] &&
            raster_state_[kRasterCombinerAlphaOcw0 + stage] ==
                pitch_multi_alpha_ocw[stage] &&
            witnessed_factor0 &&
            raster_state_[kRasterCombinerFactor1_0 + stage] == 0;
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
        raster_state_[kRasterCombinerFactor0_0 + 3];
    const bool pitch_three_black_border_factor =
        std::find(kPitchThreeBlackBorderFactors.begin(),
                  kPitchThreeBlackBorderFactors.end(),
                  pitch_three_factor3) != kPitchThreeBlackBorderFactors.end();
    const bool pitch_three_texture_border0 =
        raster_state_[kRasterTextureBorderColor0] == 0 ||
        (raster_state_[kRasterTextureBorderColor0] == 0xFF000000 &&
         pitch_three_black_border_factor);
    const bool pitch_three_texture_state =
        raster_state_[kRasterTextureFormat0] == 0x0661072E &&
        raster_state_[kRasterTextureFormat0 + 1] == 0x0661072E &&
        raster_state_[kRasterTextureFormat0 + 2] == 0x0771072E &&
        raster_state_[kRasterTextureOffset0 + 1] ==
            raster_state_[kRasterTextureOffset0] &&
        raster_state_[kRasterTextureAddress0] == 0x00030303 &&
        raster_state_[kRasterTextureAddress0 + 1] == 0x00030303 &&
        raster_state_[kRasterTextureAddress0 + 2] == 0x00030303 &&
        raster_state_[kRasterTextureFilter0] == 0x02022000 &&
        raster_state_[kRasterTextureFilter0 + 1] == 0x02022000 &&
        raster_state_[kRasterTextureFilter0 + 2] == 0x02022000 &&
        raster_state_[kRasterTextureControl0_0] == 0x4003FFC0 &&
        raster_state_[kRasterTextureControl0_0 + 1] == 0x4003FFC0 &&
        raster_state_[kRasterTextureControl0_0 + 2] == 0x4003FFC0 &&
        raster_state_[kRasterTextureControl1_0] == 0x14000000 &&
        raster_state_[kRasterTextureControl1_0 + 1] == 0 &&
        raster_state_[kRasterTextureControl1_0 + 2] == 0 &&
        raster_state_[kRasterTextureImageRect0] == 0x050001E0 &&
        raster_state_[kRasterTextureImageRect0 + 1] == 0 &&
        raster_state_[kRasterTextureImageRect0 + 2] == 0 &&
        raster_state_[kRasterTexturePalette0] == 0 &&
        raster_state_[kRasterTexturePalette0 + 1] == 0 &&
        raster_state_[kRasterTexturePalette0 + 2] == 0 &&
        pitch_three_texture_border0 &&
        raster_state_[kRasterTextureBorderColor0 + 1] == 0 &&
        raster_state_[kRasterTextureBorderColor0 + 2] == 0;
    const bool pitch_tex5_raster =
        primitive_mode_ == 5 && surface_type == 1 &&
        anti_aliasing == 0 && color_format == 8 && zeta_format == 2 &&
        surface_pitch_ == 0x14001400 &&
        surface_clip_horizontal_ == 0x05000000 &&
        surface_clip_vertical_ == 0x01E00000 &&
        enables == 0x000Cu &&
        raster_state_[kRasterControl0] == 0x00100001 &&
        raster_state_[kRasterDepthFunc] == 0x0203 &&
        raster_state_[kRasterColorMask] == 0x01010101 &&
        raster_state_[kRasterDepthMask] == 1 &&
        raster_state_[kRasterShadeMode] == 0x1D01 &&
        raster_state_[kRasterFrontPolygonMode] == 0x1B02 &&
        raster_state_[kRasterBackPolygonMode] == 0x1B02 &&
        raster_state_[kRasterCullFace] == 0x0404 &&
        raster_state_[kRasterFrontFace] == 0x0900 &&
        raster_state_[kRasterZMinMaxControl] == 1;
    const bool pitch_tex5_projected_combiner =
        raster_state_[kRasterTextureEnableMask] == 5 &&
        raster_state_[kRasterShaderStageProgram] == 0x00000C01 &&
        raster_state_[kRasterShaderOtherStageInput] == 0x00210000 &&
        raster_state_[kRasterDotRgbMapping] == 0 &&
        raster_state_[kRasterShadowDepthFunc] == 7 &&
        raster_state_[kRasterCombinerControl] == 0x00000001 &&
        raster_state_[kRasterCombinerColorIcw0] == 0x08200000 &&
        raster_state_[kRasterCombinerAlphaIcw0] == 0x11200000 &&
        raster_state_[kRasterCombinerColorOcw0] == 0x00000C00 &&
        raster_state_[kRasterCombinerAlphaOcw0] == 0x00000C00 &&
        (raster_state_[kRasterCombinerFactor0_0] &
         0xFF000000u) == 0xFF000000u &&
        raster_state_[kRasterCombinerSpecularFog0] == 0x0000000C &&
        raster_state_[kRasterCombinerSpecularFog1] == 0x00001C80 &&
        raster_state_[kRasterCombinerFinalFactor0] == 0 &&
        raster_state_[kRasterCombinerFinalFactor1] == 0;
    const uint32_t pitch_tex5_format0 =
        raster_state_[kRasterTextureFormat0];
    const bool pitch_tex5_projected_texture =
        (pitch_tex5_format0 == 0x0741062A ||
         pitch_tex5_format0 == 0x0881062A) &&
        raster_state_[kRasterTextureAddress0] == 0x00030404 &&
        raster_state_[kRasterTextureFilter0] == 0x02022000 &&
        raster_state_[kRasterTextureControl0_0] == 0x4003FFC0 &&
        raster_state_[kRasterTextureControl1_0] == 0x14000000 &&
        raster_state_[kRasterTextureImageRect0] == 0x050001E0 &&
        raster_state_[kRasterTexturePalette0] == 0 &&
        raster_state_[kRasterTextureBorderColor0] == 0xFF000000 &&
        raster_state_[kRasterTextureFormat0 + 2] == 0x0771072E &&
        raster_state_[kRasterTextureAddress0 + 2] == 0x00030303 &&
        raster_state_[kRasterTextureFilter0 + 2] == 0x02022000 &&
        raster_state_[kRasterTextureControl0_0 + 2] == 0x4003FFC0 &&
        raster_state_[kRasterTextureControl1_0 + 2] == 0 &&
        raster_state_[kRasterTextureImageRect0 + 2] == 0 &&
        raster_state_[kRasterTexturePalette0 + 2] == 0 &&
        raster_state_[kRasterTextureBorderColor0 + 2] == 0;
    const bool pitch_tex5_diffuse_combiner =
        raster_state_[kRasterTextureEnableMask] == 5 &&
        raster_state_[kRasterShaderStageProgram] == 0 &&
        raster_state_[kRasterShaderOtherStageInput] == 0 &&
        raster_state_[kRasterDotRgbMapping] == 0 &&
        raster_state_[kRasterShadowDepthFunc] == 7 &&
        raster_state_[kRasterCombinerControl] == 0x00011101 &&
        raster_state_[kRasterCombinerColorIcw0] == 0xC4200000 &&
        raster_state_[kRasterCombinerAlphaIcw0] == 0xD4301010 &&
        raster_state_[kRasterCombinerColorOcw0] == 0x000000C0 &&
        raster_state_[kRasterCombinerAlphaOcw0] == 0x000000C0 &&
        raster_state_[kRasterCombinerSpecularFog0] == 0x0000000C &&
        raster_state_[kRasterCombinerSpecularFog1] == 0x00001C80 &&
        raster_state_[kRasterCombinerFinalFactor0] == 0 &&
        raster_state_[kRasterCombinerFinalFactor1] == 0;
    const bool inline_linear_blit_combiner =
        raster_state_[kRasterTextureEnableMask] == 1 &&
        raster_state_[kRasterShaderStageProgram] == 0x00000001 &&
        raster_state_[kRasterShaderOtherStageInput] == 0x00210000 &&
        raster_state_[kRasterDotRgbMapping] == 0 &&
        // PROJECT2D does not perform shadow comparison; both retained values
        // observed around the boot resolve are therefore equivalent.
        (raster_state_[kRasterShadowDepthFunc] == 0 ||
         raster_state_[kRasterShadowDepthFunc] == 7) &&
        raster_state_[kRasterCombinerControl] == 0x00000001 &&
        raster_state_[kRasterCombinerColorIcw0] == 0x08200000 &&
        raster_state_[kRasterCombinerAlphaIcw0] == 0x14200000 &&
        raster_state_[kRasterCombinerColorOcw0] == 0x00000C00 &&
        raster_state_[kRasterCombinerAlphaOcw0] == 0x00000C00 &&
        raster_state_[kRasterCombinerSpecularFog0] == 0x0000000C &&
        raster_state_[kRasterCombinerSpecularFog1] == 0x00001C80 &&
        raster_state_[kRasterCombinerFinalFactor0] == 0 &&
        raster_state_[kRasterCombinerFinalFactor1] == 0;
    const bool inline_linear_blit_texture =
        raster_state_[kRasterTextureFormat0] == 0x0001122A &&
        // PROJECT2D consumes U/V only; the witnessed P-axis WRAP/CLAMP
        // difference cannot affect this texture.
        (raster_state_[kRasterTextureAddress0] == 0x00010303 ||
         raster_state_[kRasterTextureAddress0] == 0x00030303) &&
        raster_state_[kRasterTextureFilter0] == 0x02022000 &&
        raster_state_[kRasterTextureControl0_0] == 0x4003FFC0 &&
        raster_state_[kRasterTextureControl1_0] == 0x14000000 &&
        raster_state_[kRasterTextureImageRect0] == 0x050001E0 &&
        raster_state_[kRasterTexturePalette0] == 0 &&
        // The sampled U/V axes are CLAMP_TO_EDGE, so the two witnessed border
        // colors are output-inert.
        (raster_state_[kRasterTextureBorderColor0] == 0 ||
         raster_state_[kRasterTextureBorderColor0] == 0xFF000000);
    const bool inline_linear_blit_surface =
        primitive_mode_ == 5 && surface_type == 1 &&
        anti_aliasing == 0 && color_format == 8 && zeta_format == 2 &&
        surface_pitch_ == 0x0A000A00 &&
        surface_clip_horizontal_ == 0x02800000 &&
        surface_clip_vertical_ == 0x01E00000 &&
        enables == 0x0010u &&
        raster_state_[kRasterControl0] == 0x00100001 &&
        raster_state_[kRasterColorMask] == 0x01010101 &&
        raster_state_[kRasterShadeMode] == 0x1D01 &&
        raster_state_[kRasterFrontPolygonMode] == 0x1B02 &&
        raster_state_[kRasterBackPolygonMode] == 0x1B02 &&
        raster_state_[kRasterZMinMaxControl] == 1;
    const bool pitch_single_texture_combiner =
        raster_state_[kRasterTextureEnableMask] == 1 &&
        raster_state_[kRasterShaderStageProgram] == 0x00000001 &&
        raster_state_[kRasterShaderOtherStageInput] == 0x00210000 &&
        raster_state_[kRasterDotRgbMapping] == 0 &&
        raster_state_[kRasterShadowDepthFunc] == 7 &&
        raster_state_[kRasterCombinerControl] == 0x00000001 &&
        raster_state_[kRasterCombinerColorIcw0] == 0x08200000 &&
        raster_state_[kRasterCombinerAlphaIcw0] == 0x11200000 &&
        raster_state_[kRasterCombinerColorOcw0] == 0x00000C00 &&
        raster_state_[kRasterCombinerAlphaOcw0] == 0x00000C00 &&
        raster_state_[kRasterCombinerFactor0_0] == 0xFFFFFFFF &&
        raster_state_[kRasterCombinerFactor1_0] == 0xFFFFFFFF &&
        raster_state_[kRasterCombinerSpecularFog0] == 0x0000000C &&
        raster_state_[kRasterCombinerSpecularFog1] == 0x00001C80 &&
        raster_state_[kRasterCombinerFinalFactor0] == 0 &&
        raster_state_[kRasterCombinerFinalFactor1] == 0;
    // RS0 in probe-reduced-diag-6.4b: only t0 reaches R0; t1/t2 are
    // enabled cubemaps but their shader stages are unused by the one combiner.
    const bool tex7_fan_combiner =
        raster_state_[kRasterTextureEnableMask] == 7 &&
        raster_state_[kRasterShaderStageProgram] == 0x00000C61 &&
        raster_state_[kRasterShaderOtherStageInput] == 0x00210000 &&
        raster_state_[kRasterDotRgbMapping] == 0 &&
        raster_state_[kRasterShadowDepthFunc] == 7 &&
        raster_state_[kRasterCombinerControl] == 1 &&
        raster_state_[kRasterCombinerColorIcw0] == 0x08010000 &&
        raster_state_[kRasterCombinerAlphaIcw0] == 0x11200000 &&
        raster_state_[kRasterCombinerColorOcw0] == 0x00000C00 &&
        raster_state_[kRasterCombinerAlphaOcw0] == 0x00000C00 &&
        raster_state_[kRasterCombinerFactor0_0] == 0xFFA0FF40 &&
        raster_state_[kRasterCombinerFactor1_0] == 0xFFA0FF40 &&
        raster_state_[kRasterCombinerSpecularFog0] == 0x0000000C &&
        raster_state_[kRasterCombinerSpecularFog1] == 0x00001C80 &&
        raster_state_[kRasterCombinerFinalFactor0] == 0xFF3F3F3F &&
        raster_state_[kRasterCombinerFinalFactor1] == 0;
    const bool tex7_fan_texture_state =
        raster_state_[kRasterTextureFormat0] == 0x0888062A &&
        raster_state_[kRasterTextureAddress0] == 0x00030303 &&
        raster_state_[kRasterTextureFilter0] == 0x02022000 &&
        raster_state_[kRasterTextureControl0_0] == 0x4003FFC0 &&
        raster_state_[kRasterTextureControl1_0] == 0x14000000 &&
        raster_state_[kRasterTextureImageRect0] == 0x050001E0 &&
        raster_state_[kRasterTexturePalette0] == 0 &&
        (raster_state_[kRasterTextureBorderColor0] == 0 ||
         raster_state_[kRasterTextureBorderColor0] == 0xFF000000) &&
        raster_state_[kRasterTextureFormat0 + 1] == 0x0771072E &&
        raster_state_[kRasterTextureFormat0 + 2] == 0x0771072E &&
        raster_state_[kRasterTextureAddress0 + 1] == 0x00030303 &&
        raster_state_[kRasterTextureAddress0 + 2] == 0x00030303 &&
        raster_state_[kRasterTextureFilter0 + 1] == 0x02022000 &&
        raster_state_[kRasterTextureFilter0 + 2] == 0x02022000 &&
        raster_state_[kRasterTextureControl0_0 + 1] == 0x4003FFC0 &&
        raster_state_[kRasterTextureControl0_0 + 2] == 0x4003FFC0 &&
        raster_state_[kRasterTextureControl1_0 + 1] == 0 &&
        raster_state_[kRasterTextureControl1_0 + 2] == 0 &&
        raster_state_[kRasterTextureImageRect0 + 1] == 0 &&
        raster_state_[kRasterTextureImageRect0 + 2] == 0 &&
        raster_state_[kRasterTexturePalette0 + 1] == 0 &&
        raster_state_[kRasterTexturePalette0 + 2] == 0 &&
        raster_state_[kRasterTextureBorderColor0 + 1] == 0 &&
        raster_state_[kRasterTextureBorderColor0 + 2] == 0;
    const bool pitch_additive_combiner =
        raster_state_[kRasterTextureEnableMask] == 1 &&
        raster_state_[kRasterShaderStageProgram] == 0x00000001 &&
        raster_state_[kRasterShaderOtherStageInput] == 0x00210000 &&
        raster_state_[kRasterDotRgbMapping] == 0 &&
        raster_state_[kRasterCombinerControl] == 0x00000001 &&
        raster_state_[kRasterCombinerColorIcw0] == 0x01080000 &&
        raster_state_[kRasterCombinerAlphaIcw0] == 0x11180000 &&
        raster_state_[kRasterCombinerColorOcw0] == 0x00000C00 &&
        raster_state_[kRasterCombinerAlphaOcw0] == 0x00000C00 &&
        raster_state_[kRasterCombinerFactor0_0] ==
            raster_state_[kRasterCombinerFactor1_0] &&
        raster_state_[kRasterCombinerSpecularFog0] == 0x0000000C &&
        raster_state_[kRasterCombinerSpecularFog1] == 0x00001C80 &&
        raster_state_[kRasterCombinerFinalFactor0] == 0 &&
        raster_state_[kRasterCombinerFinalFactor1] == 0;
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
        raster_state_[kRasterTextureEnableMask] == 0xF &&
        raster_state_[kRasterShaderStageProgram] == 0x00008421 &&
        raster_state_[kRasterShaderOtherStageInput] == 0 &&
        raster_state_[kRasterDotRgbMapping] == 0 &&
        raster_state_[kRasterShadowDepthFunc] == 7 &&
        raster_state_[kRasterCombinerControl] == 0x00011106 &&
        raster_state_[kRasterCombinerSpecularFog0] == 0x0000000C &&
        raster_state_[kRasterCombinerSpecularFog1] == 0x00001C80 &&
        raster_state_[kRasterCombinerFinalFactor0] == 0 &&
        raster_state_[kRasterCombinerFinalFactor1] == 0;
    for (size_t stage = 0; stage < texf_ci.size() &&
                           texf_projected_fan_combiner; ++stage) {
        texf_projected_fan_combiner =
            raster_state_[kRasterCombinerColorIcw0 + stage] == texf_ci[stage] &&
            raster_state_[kRasterCombinerAlphaIcw0 + stage] == texf_ai[stage] &&
            raster_state_[kRasterCombinerColorOcw0 + stage] == texf_co[stage] &&
            raster_state_[kRasterCombinerAlphaOcw0 + stage] == texf_ao[stage] &&
            raster_state_[kRasterCombinerFactor0_0 + stage] == texf_f0[stage] &&
            raster_state_[kRasterCombinerFactor1_0 + stage] == 0;
    }
    const uint32_t texf_t0_offset = raster_state_[kRasterTextureOffset0];
    const bool texf_projected_fan_textures =
        (texf_t0_offset == 0x02E05000 || texf_t0_offset == 0x02C05000) &&
        raster_state_[kRasterTextureFormat0] == 0x09A1062A &&
        raster_state_[kRasterTextureAddress0] == 0x00030303 &&
        raster_state_[kRasterTextureFilter0] == 0x02022000 &&
        raster_state_[kRasterTextureControl0_0] == 0x4003FFC0 &&
        raster_state_[kRasterTextureControl1_0] == 0x14000000 &&
        raster_state_[kRasterTextureImageRect0] == 0x050001E0 &&
        raster_state_[kRasterTexturePalette0] == 0 &&
        raster_state_[kRasterTextureBorderColor0] == 0 &&
        raster_state_[kRasterTextureOffset0 + 1] == 0x029F3000 &&
        raster_state_[kRasterTextureOffset0 + 2] == 0x029E3000 &&
        raster_state_[kRasterTextureOffset0 + 3] == 0x029D3000 &&
        raster_state_[kRasterTextureFormat0 + 1] == 0x0881192A &&
        raster_state_[kRasterTextureFormat0 + 2] == 0x0881192A &&
        raster_state_[kRasterTextureFormat0 + 3] == 0x0881192A &&
        raster_state_[kRasterTextureAddress0 + 1] == 0x00030101 &&
        raster_state_[kRasterTextureAddress0 + 2] == 0x00030101 &&
        raster_state_[kRasterTextureAddress0 + 3] == 0x00030101 &&
        raster_state_[kRasterTextureFilter0 + 1] == 0x02022000 &&
        raster_state_[kRasterTextureFilter0 + 2] == 0x02022000 &&
        raster_state_[kRasterTextureFilter0 + 3] == 0x02022000 &&
        raster_state_[kRasterTextureControl0_0 + 1] == 0x4003FFC0 &&
        raster_state_[kRasterTextureControl0_0 + 2] == 0x4003FFC0 &&
        raster_state_[kRasterTextureControl0_0 + 3] == 0x4003FFC0 &&
        raster_state_[kRasterTextureControl1_0 + 1] == 0 &&
        raster_state_[kRasterTextureControl1_0 + 2] == 0 &&
        raster_state_[kRasterTextureControl1_0 + 3] == 0x04000000 &&
        raster_state_[kRasterTextureImageRect0 + 1] == 0 &&
        raster_state_[kRasterTextureImageRect0 + 2] == 0 &&
        raster_state_[kRasterTextureImageRect0 + 3] == 0x02000200 &&
        raster_state_[kRasterTexturePalette0 + 1] == 0 &&
        raster_state_[kRasterTexturePalette0 + 2] == 0 &&
        raster_state_[kRasterTexturePalette0 + 3] == 0 &&
        raster_state_[kRasterTextureBorderColor0 + 1] == 0 &&
        raster_state_[kRasterTextureBorderColor0 + 2] == 0 &&
        raster_state_[kRasterTextureBorderColor0 + 3] == 0xFFFFFFFF;
    const bool texf_projected_fan_bounds =
        raster_state_[kRasterWindowClipHorizontal] == 0x04FF0000 &&
        raster_state_[kRasterWindowClipVertical] == 0x01DF0000 &&
        raster_state_[kRasterClipMin] == 0 &&
        raster_state_[kRasterClipMax] == 0x4B7FFFFF;

    Profile profile = Profile::kNone;
    if (depth_only && surface_type == 1 && anti_aliasing == 0 &&
        zeta_format == 1 && supported_primitive &&
        raster_state_[kRasterTextureEnableMask] == 0) {
        profile = Profile::kPitchZ16Depth;
    } else if (radial_combiner) {
        profile = Profile::kSwizzledRadialColor;
    } else if (textured_combiner && surface_type == 1 &&
               raster_state_[kRasterShadowDepthFunc] == 4) {
        profile = Profile::kPitchTexturedColor;
    } else if (textured_combiner && textured_fixed_stage6 &&
               surface_type == 2 &&
               raster_state_[kRasterShadowDepthFunc] == 0) {
        profile = Profile::kSwizzledTexturedColor;
    } else if (projected_combiner && surface_type == 1 &&
               raster_state_[kRasterShadowDepthFunc] == 4) {
        profile = Profile::kPitchProjectedColor;
    } else if (projected_combiner && projected_fixed_stage5 &&
               surface_type == 2 &&
               raster_state_[kRasterShadowDepthFunc] == 0) {
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
        perf::ScopedAdd t(perf::g_ns_raster_generic, perf::g_calls_raster_generic);
        return rasterize_generic_combiner_batch(executed);
    }
    executed = true;
    const bool pitch_tex5_profile =
        profile == Profile::kPitchTex5ProjectedColor ||
        profile == Profile::kPitchTex5DiffuseColor;

    if (profile == Profile::kPitchZ16Depth) {
        constexpr uint32_t kSupportedEnables =
            (1u << 2) | (1u << 3) | (1u << 13);
        if ((enables & ~kSupportedEnables) != 0 ||
            (enables & (1u << 2)) == 0 ||
            raster_state_[kRasterDepthFunc] != 0x0203 ||
            raster_state_[kRasterCullFace] != 0x0405 ||
            raster_state_[kRasterFrontFace] != 0x0900 ||
            raster_state_[kRasterFrontPolygonMode] != 0x1B02 ||
            raster_state_[kRasterBackPolygonMode] != 0x1B02 ||
            (raster_state_[kRasterControl0] &
             ((1u << 12) | (1u << 16))) != 0 ||
            (raster_state_[kRasterZMinMaxControl] & 0xF0u) != 0) {
            fault_("NV2A depth-only draw exceeds the witnessed raster subset");
            return false;
        }
    } else if (profile == Profile::kPitchMultiTextureColor) {
        if (primitive_mode_ != 6 || surface_type != 1 ||
            anti_aliasing != 0 || color_format != 8 || zeta_format != 2 ||
            enables != 0x000Fu ||
            raster_state_[kRasterControl0] != 0x00100001 ||
            raster_state_[kRasterAlphaFunc] != 0x0206 ||
            (raster_state_[kRasterAlphaRef] & 0xFFu) != 1 ||
            raster_state_[kRasterBlendSrc] != 0x0302 ||
            raster_state_[kRasterBlendDst] != 0x0303 ||
            raster_state_[kRasterBlendEquation] != 0x8006 ||
            raster_state_[kRasterDepthFunc] != 0x0203 ||
            raster_state_[kRasterColorMask] != 0x01010101 ||
            raster_state_[kRasterDepthMask] != 1 ||
            raster_state_[kRasterShadeMode] != 0x1D01 ||
            raster_state_[kRasterFrontPolygonMode] != 0x1B02 ||
            raster_state_[kRasterBackPolygonMode] != 0x1B02 ||
            raster_state_[kRasterCullFace] != 0x0405 ||
            raster_state_[kRasterFrontFace] != 0x0900 ||
            raster_state_[kRasterZMinMaxControl] != 1 ||
            raster_state_[kRasterPolygonOffsetScale] != 0x40800000 ||
            raster_state_[kRasterPolygonOffsetBias] != 0x41200000) {
            fault_("NV2A pitch multi-texture draw exceeds the witnessed raster subset");
            return false;
        }
    } else if (profile == Profile::kPitchThreeTextureColor) {
        if (primitive_mode_ != 6 || surface_type != 1 ||
            anti_aliasing != 0 || color_format != 8 || zeta_format != 2 ||
            surface_pitch_ != 0x14001400 ||
            surface_clip_horizontal_ != 0x05000000 ||
            surface_clip_vertical_ != 0x01E00000 ||
            enables != 0x000Eu ||
            raster_state_[kRasterControl0] != 0x00100001 ||
            raster_state_[kRasterAlphaFunc] != 0x0206 ||
            (raster_state_[kRasterAlphaRef] & 0xFFu) != 1 ||
            raster_state_[kRasterBlendSrc] != 0x0302 ||
            raster_state_[kRasterBlendDst] != 0x0303 ||
            raster_state_[kRasterBlendEquation] != 0x8006 ||
            raster_state_[kRasterDepthFunc] != 0x0203 ||
            raster_state_[kRasterColorMask] != 0x01010101 ||
            raster_state_[kRasterDepthMask] != 1 ||
            raster_state_[kRasterShadeMode] != 0x1D01 ||
            raster_state_[kRasterFrontPolygonMode] != 0x1B02 ||
            raster_state_[kRasterBackPolygonMode] != 0x1B02 ||
            raster_state_[kRasterCullFace] != 0x0405 ||
            raster_state_[kRasterFrontFace] != 0x0900 ||
            raster_state_[kRasterZMinMaxControl] != 1 ||
            raster_state_[kRasterPolygonOffsetScale] != 0x40800000 ||
            raster_state_[kRasterPolygonOffsetBias] != 0x41200000) {
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
        if (primitive_mode_ != 5 || surface_type != 1 ||
            anti_aliasing != 0 || color_format != 8 || zeta_format != 2 ||
            surface_pitch_ != 0x0A000A00 ||
            surface_clip_horizontal_ != 0x02800000 ||
            surface_clip_vertical_ != 0x01E00000 ||
            enables != 0x0010u ||
            raster_state_[kRasterControl0] != 0x00100001 ||
            raster_state_[kRasterColorMask] != 0x01010101 ||
            raster_state_[kRasterShadeMode] != 0x1D01 ||
            raster_state_[kRasterFrontPolygonMode] != 0x1B02 ||
            raster_state_[kRasterBackPolygonMode] != 0x1B02 ||
            raster_state_[kRasterZMinMaxControl] != 1) {
            fault_("NV2A inline linear-blit draw exceeds the witnessed raster subset");
            return false;
        }
    } else if (profile == Profile::kPitchSingleTextureColor) {
        if (primitive_mode_ != 7 || surface_type != 1 ||
            anti_aliasing != 0 || color_format != 8 || zeta_format != 2 ||
            enables != 0x000Eu ||
            raster_state_[kRasterControl0] != 0x00100001 ||
            raster_state_[kRasterBlendSrc] != 1 ||
            raster_state_[kRasterBlendDst] != 0 ||
            raster_state_[kRasterBlendEquation] != 0x8006 ||
            raster_state_[kRasterDepthFunc] != 0x0203 ||
            raster_state_[kRasterColorMask] != 0x01010101 ||
            raster_state_[kRasterDepthMask] != 0 ||
            raster_state_[kRasterShadeMode] != 0x1D01 ||
            raster_state_[kRasterFrontPolygonMode] != 0x1B02 ||
            raster_state_[kRasterBackPolygonMode] != 0x1B02 ||
            raster_state_[kRasterCullFace] != 0x0405 ||
            raster_state_[kRasterFrontFace] != 0x0900 ||
            raster_state_[kRasterZMinMaxControl] != 1) {
            fault_("NV2A pitch single-texture draw exceeds the witnessed raster subset");
            return false;
        }
    } else if (profile == Profile::kTex7FanColor) {
        if (primitive_mode_ != 7 || surface_type != 1 ||
            anti_aliasing != 0 || color_format != 8 || zeta_format != 2 ||
            surface_pitch_ != 0x14001400 ||
            surface_clip_horizontal_ != 0x05000000 ||
            surface_clip_vertical_ != 0x01E00000 || enables != 0x000Eu ||
            raster_state_[kRasterControl0] != 0x00100001 ||
            raster_state_[kRasterAlphaFunc] != 0x0206 ||
            (raster_state_[kRasterAlphaRef] & 0xFFu) != 1 ||
            raster_state_[kRasterBlendSrc] != 0x0302 ||
            raster_state_[kRasterBlendDst] != 1 ||
            raster_state_[kRasterBlendEquation] != 0x8006 ||
            raster_state_[kRasterDepthFunc] != 0x0203 ||
            raster_state_[kRasterColorMask] != 0x01010101 ||
            raster_state_[kRasterDepthMask] != 0 ||
            raster_state_[kRasterShadeMode] != 0x1D01 ||
            raster_state_[kRasterFrontPolygonMode] != 0x1B02 ||
            raster_state_[kRasterBackPolygonMode] != 0x1B02 ||
            raster_state_[kRasterCullFace] != 0x0405 ||
            raster_state_[kRasterFrontFace] != 0x0900 ||
            raster_state_[kRasterZMinMaxControl] != 1) {
            fault_("NV2A tex7 fan draw exceeds the witnessed RS0 subset");
            return false;
        }
    } else if (profile == Profile::kTexfProjectedFanColor) {
        if (primitive_mode_ != 7 || surface_type != 1 || anti_aliasing != 0 ||
            color_format != 8 || zeta_format != 2 ||
            surface_pitch_ != 0x14001400 ||
            surface_clip_horizontal_ != 0x05000000 ||
            surface_clip_vertical_ != 0x01E00000 || enables != 0x000Fu ||
            raster_state_[kRasterWindowClipHorizontal] != 0x04FF0000 ||
            raster_state_[kRasterWindowClipVertical] != 0x01DF0000 ||
            raster_state_[kRasterClipMin] != 0 ||
            raster_state_[kRasterClipMax] != 0x4B7FFFFF ||
            raster_state_[kRasterControl0] != 0x00100001 ||
            raster_state_[kRasterAlphaFunc] != 0x0206 ||
            (raster_state_[kRasterAlphaRef] & 0xFFu) != 1 ||
            raster_state_[kRasterBlendSrc] != 0x0302 ||
            raster_state_[kRasterBlendDst] != 1 ||
            raster_state_[kRasterBlendEquation] != 0x8006 ||
            raster_state_[kRasterDepthFunc] != 0x0207 ||
            raster_state_[kRasterColorMask] != 0x01010101 ||
            raster_state_[kRasterDepthMask] != 0 ||
            raster_state_[kRasterShadeMode] != 0x1D01 ||
            raster_state_[kRasterFrontPolygonMode] != 0x1B02 ||
            raster_state_[kRasterBackPolygonMode] != 0x1B02 ||
            raster_state_[kRasterCullFace] != 0x0405 ||
            raster_state_[kRasterFrontFace] != 0x0900 ||
            raster_state_[kRasterZMinMaxControl] != 1) {
            fault_("NV2A tex=f projected fan draw exceeds RS0/RS1 subset");
            return false;
        }
    } else if (profile == Profile::kPitchAdditiveColor) {
        if (!supported_primitive || surface_type != 1 ||
            anti_aliasing != 0 || color_format != 8 || zeta_format != 2 ||
            enables != 0x000Fu ||
            raster_state_[kRasterControl0] != 0x00100001 ||
            raster_state_[kRasterAlphaFunc] != 0x0206 ||
            (raster_state_[kRasterAlphaRef] & 0xFFu) != 1 ||
            raster_state_[kRasterBlendSrc] != 0x0302 ||
            raster_state_[kRasterBlendDst] != 1 ||
            raster_state_[kRasterBlendEquation] != 0x8006 ||
            (raster_state_[kRasterDepthFunc] != 0x0202 &&
             raster_state_[kRasterDepthFunc] != 0x0207) ||
            raster_state_[kRasterColorMask] != 0x01010101 ||
            raster_state_[kRasterDepthMask] != 0 ||
            raster_state_[kRasterShadeMode] != 0x1D01 ||
            raster_state_[kRasterFrontPolygonMode] != 0x1B02 ||
            raster_state_[kRasterBackPolygonMode] != 0x1B02 ||
            raster_state_[kRasterCullFace] != 0x0405 ||
            raster_state_[kRasterFrontFace] != 0x0900 ||
            raster_state_[kRasterZMinMaxControl] != 1) {
            char why[640];
            std::snprintf(
                why, sizeof(why),
                "NV2A inline-color draw exceeds the witnessed raster subset "
                "(prim=%x sfmt=%08x en=%08x ctrl=%08x alpha=%x/%02x "
                "blend=%x/%x/%x depth=%x/%x mask=%08x "
                "shade=%x poly=%x/%x cull=%x/%x zctl=%08x)",
                primitive_mode_, surface_format_, enables,
                raster_state_[kRasterControl0],
                raster_state_[kRasterAlphaFunc],
                raster_state_[kRasterAlphaRef] & 0xFFu,
                raster_state_[kRasterBlendSrc],
                raster_state_[kRasterBlendDst],
                raster_state_[kRasterBlendEquation],
                raster_state_[kRasterDepthFunc],
                raster_state_[kRasterDepthMask],
                raster_state_[kRasterColorMask],
                raster_state_[kRasterShadeMode],
                raster_state_[kRasterFrontPolygonMode],
                raster_state_[kRasterBackPolygonMode],
                raster_state_[kRasterCullFace],
                raster_state_[kRasterFrontFace],
                raster_state_[kRasterZMinMaxControl]);
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
                ? (raster_state_[kRasterDepthMask] == 1 ||
                   raster_state_[kRasterDepthMask] == 0xFFFFFFFFu)
                : raster_state_[kRasterDepthMask] == 1;
        const bool common_color_state =
            supported_primitive && witnessed_surface_type &&
            anti_aliasing == 0 && color_format == 8 && zeta_format == 2 &&
            raster_state_[kRasterControl0] == 0x00100001 &&
            raster_state_[kRasterBlendSrc] == 1 &&
            raster_state_[kRasterBlendDst] == 0 &&
            raster_state_[kRasterBlendEquation] == 0x8006 &&
            raster_state_[kRasterDepthFunc] == 0x0203 &&
            raster_state_[kRasterColorMask] == 0x01010101 &&
            witnessed_depth_mask &&
            raster_state_[kRasterShadeMode] == 0x1D01 &&
            raster_state_[kRasterFrontPolygonMode] == 0x1B02 &&
            raster_state_[kRasterBackPolygonMode] == 0x1B02 &&
            raster_state_[kRasterCullFace] == 0x0405 &&
            raster_state_[kRasterFrontFace] == 0x0900 &&
            raster_state_[kRasterZMinMaxControl] == 1;
        const bool radial_state =
            profile == Profile::kSwizzledRadialColor &&
            enables == 0x000Fu &&
            raster_state_[kRasterAlphaFunc] == 0x0206 &&
            (raster_state_[kRasterAlphaRef] & 0xFFu) == 1;
        const bool textured_state =
            (profile == Profile::kSwizzledTexturedColor ||
             profile == Profile::kSwizzledProjectedColor) &&
            enables == 0x000Cu &&
            raster_state_[kRasterAlphaFunc] == 0x0207 &&
            (raster_state_[kRasterAlphaRef] & 0xFFu) == 0;
        const bool pitch_textured_state =
            (profile == Profile::kPitchTexturedColor ||
             profile == Profile::kPitchProjectedColor) &&
            enables == 0x000Cu &&
            raster_state_[kRasterAlphaFunc] == 0x0206 &&
            (raster_state_[kRasterAlphaRef] & 0xFFu) == 1;
        if (!common_color_state ||
            (!radial_state && !textured_state && !pitch_textured_state)) {
            fault_("NV2A color draw exceeds the witnessed raster subset");
            return false;
        }
    }

    const uint32_t clip_x = surface_clip_horizontal_ & 0xFFFFu;
    const uint32_t clip_width = surface_clip_horizontal_ >> 16;
    const uint32_t clip_y = surface_clip_vertical_ & 0xFFFFu;
    const uint32_t clip_height = surface_clip_vertical_ >> 16;
    if (!clip_width || !clip_height) {
        fault_("NV2A raster surface has an empty clip");
        return false;
    }

    const bool inline_depth_test =
        profile == Profile::kPitchAdditiveColor &&
        raster_state_[kRasterDepthFunc] == 0x0202;
    uint32_t surface_width = 0;
    uint32_t surface_height = 0;
    uint32_t pitch = 0;
    uint64_t depth_bytes64 = 0;
    uint64_t color_bytes64 = 0;
    if (profile == Profile::kPitchZ16Depth) {
        pitch = surface_pitch_ >> 16;
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
        pitch = surface_pitch_ & 0xFFFFu;
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
        if (!pitch || (surface_pitch_ >> 16) != pitch ||
            static_cast<uint64_t>(clip_x + clip_width) * 4 > pitch) {
            fault_("NV2A color pitch surface is invalid");
            return false;
        }
    } else {
        const uint32_t width_log2 = (surface_format_ >> 16) & 0xFFu;
        const uint32_t height_log2 = surface_format_ >> 24;
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
            (surface_pitch_ & 0xFFFFu) != pitch ||
            (surface_pitch_ >> 16) != pitch) {
            fault_("NV2A color swizzled surface is invalid");
            return false;
        }
    }
    const bool uses_depth_surface =
        profile == Profile::kPitchAdditiveColor
            ? inline_depth_test
            : profile != Profile::kInlineLinearBlitColor;
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
            return true;
        };

    uint32_t depth_address = 0;
    uint32_t color_address = 0;
    const bool color_profile = profile != Profile::kPitchZ16Depth;
    if (uses_depth_surface &&
        !resolve_surface(zeta_dma_, zeta_dma_bound_, surface_zeta_offset_,
                         depth_bytes64, depth_address, "zeta")) {
        return false;
    }
    if (color_profile &&
        !resolve_surface(color_dma_, color_dma_bound_, surface_color_offset_,
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
            const uint32_t format = raster_state_[kRasterTextureFormat0 + stage];
            const uint32_t width = 1u << ((format >> 20) & 0xFu);
            const uint32_t height = 1u << ((format >> 24) & 0xFu);
            const uint64_t bytes = static_cast<uint64_t>(width) * height * 4;
            uint32_t address = 0;
            if (bytes > SIZE_MAX ||
                !resolve_surface(texture_dma_b_, texture_dma_b_bound_,
                                 raster_state_[kRasterTextureOffset0 + stage],
                                 bytes, address, "tex=f texture")) {
                return false;
            }
        }
    }

    std::vector<uint8_t> texture_data;
    std::unique_ptr<SwizzleLookup> texture_swizzle;
    uint32_t texture_width = 0;
    uint32_t texture_height = 0;
    std::vector<uint8_t> projected_texture_data;
    std::unique_ptr<SwizzleLookup> projected_texture_swizzle;
    uint32_t projected_texture_width = 0;
    uint32_t projected_texture_height = 0;
    const bool sampled_color_profile =
        profile == Profile::kSwizzledTexturedColor ||
        profile == Profile::kPitchTexturedColor ||
        profile == Profile::kSwizzledProjectedColor ||
        profile == Profile::kPitchProjectedColor;
    if (sampled_color_profile) {
        const uint32_t format1 = raster_state_[kRasterTextureFormat0 + 1];
        const uint32_t format2 = raster_state_[kRasterTextureFormat0 + 2];
        const uint32_t offset1 = raster_state_[kRasterTextureOffset0 + 1];
        const uint32_t offset2 = raster_state_[kRasterTextureOffset0 + 2];
        const bool witnessed_texture =
            format1 == format2 &&
            (format1 == 0x0661072E || format1 == 0x0771072E) &&
            offset1 == offset2 &&
            raster_state_[kRasterTextureAddress0 + 1] == 0x00030303 &&
            raster_state_[kRasterTextureAddress0 + 2] == 0x00030303 &&
            raster_state_[kRasterTextureFilter0 + 1] == 0x02022000 &&
            raster_state_[kRasterTextureFilter0 + 2] == 0x02022000 &&
            raster_state_[kRasterTextureControl0_0 + 1] == 0x4003FFC0 &&
            raster_state_[kRasterTextureControl0_0 + 2] == 0x4003FFC0 &&
            raster_state_[kRasterTexturePalette0 + 1] == 0 &&
            raster_state_[kRasterTexturePalette0 + 2] == 0;
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
                raster_state_[kRasterTextureFormat0 + 0];
            const uint32_t offset0 =
                raster_state_[kRasterTextureOffset0 + 0];
            const uint32_t border0 =
                raster_state_[kRasterTextureBorderColor0 + 0];
            const bool witnessed_projected_border =
                profile == Profile::kPitchProjectedColor
                    ? (border0 == 0 || border0 == 0xFF000000)
                    : border0 == 0;
            const bool witnessed_projected_texture =
                format0 == 0x0771062A &&
                raster_state_[kRasterTextureAddress0 + 0] == 0x00010101 &&
                raster_state_[kRasterTextureFilter0 + 0] == 0x02022000 &&
                raster_state_[kRasterTextureControl0_0 + 0] == 0x4003FFF0 &&
                raster_state_[kRasterTextureControl1_0 + 0] == 0x14000000 &&
                raster_state_[kRasterTexturePalette0 + 0] == 0 &&
                witnessed_projected_border;
            if (!witnessed_projected_texture || (format0 & 3u) != 2u) {
                char why[320];
                std::snprintf(
                    why, sizeof(why),
                    "NV2A projected-color stage-0 texture exceeds the "
                    "witnessed subset (off=%08x fmt=%08x addr=%08x "
                    "filter=%08x ctl0=%08x ctl1=%08x pal=%08x border=%08x)",
                    offset0, format0,
                    raster_state_[kRasterTextureAddress0 + 0],
                    raster_state_[kRasterTextureFilter0 + 0],
                    raster_state_[kRasterTextureControl0_0 + 0],
                    raster_state_[kRasterTextureControl1_0 + 0],
                    raster_state_[kRasterTexturePalette0 + 0],
                    raster_state_[kRasterTextureBorderColor0 + 0]);
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
    std::vector<uint8_t> linear_blit_texture_data;
    uint32_t linear_blit_texture_width = 0;
    uint32_t linear_blit_texture_height = 0;
    uint32_t linear_blit_texture_pitch = 0;
    if (profile == Profile::kInlineLinearBlitColor) {
        const uint32_t format0 = raster_state_[kRasterTextureFormat0];
        const uint32_t offset0 = raster_state_[kRasterTextureOffset0];
        if (!inline_linear_blit_texture || (format0 & 3u) != 2u) {
            fault_("NV2A inline linear-blit texture exceeds the witnessed subset");
            return false;
        }
        linear_blit_texture_width =
            raster_state_[kRasterTextureImageRect0] >> 16;
        linear_blit_texture_height =
            raster_state_[kRasterTextureImageRect0] & 0xFFFFu;
        linear_blit_texture_pitch =
            raster_state_[kRasterTextureControl1_0] >> 16;
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
    std::vector<uint8_t> multi_texture_data;
    std::unique_ptr<SwizzleLookup> multi_texture_swizzle;
    uint32_t multi_texture_width = 0;
    uint32_t multi_texture_height = 0;
    if (profile == Profile::kPitchMultiTextureColor ||
        profile == Profile::kPitchThreeTextureColor) {
        const uint32_t format0 = raster_state_[kRasterTextureFormat0];
        const uint32_t format1 = raster_state_[kRasterTextureFormat0 + 1];
        const uint32_t offset0 = raster_state_[kRasterTextureOffset0];
        const uint32_t offset1 = raster_state_[kRasterTextureOffset0 + 1];
        const bool witnessed_t0_border =
            raster_state_[kRasterTextureBorderColor0] == 0 ||
            (profile == Profile::kPitchThreeTextureColor &&
             raster_state_[kRasterTextureBorderColor0] == 0xFF000000 &&
             pitch_three_black_border_factor);
        const bool witnessed_multi_texture =
            format0 == 0x0661072E && format1 == format0 &&
            offset1 == offset0 &&
            raster_state_[kRasterTextureAddress0] == 0x00030303 &&
            raster_state_[kRasterTextureAddress0 + 1] == 0x00030303 &&
            raster_state_[kRasterTextureFilter0] == 0x02022000 &&
            raster_state_[kRasterTextureFilter0 + 1] == 0x02022000 &&
            raster_state_[kRasterTextureControl0_0] == 0x4003FFC0 &&
            raster_state_[kRasterTextureControl0_0 + 1] == 0x4003FFC0 &&
            raster_state_[kRasterTextureControl1_0] == 0x14000000 &&
            raster_state_[kRasterTextureControl1_0 + 1] == 0 &&
            raster_state_[kRasterTextureImageRect0] == 0x050001E0 &&
            raster_state_[kRasterTextureImageRect0 + 1] == 0 &&
            raster_state_[kRasterTexturePalette0] == 0 &&
            raster_state_[kRasterTexturePalette0 + 1] == 0 &&
            witnessed_t0_border &&
            raster_state_[kRasterTextureBorderColor0 + 1] == 0;
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
    std::vector<uint8_t> single_texture_data;
    std::unique_ptr<SwizzleLookup> single_texture_swizzle;
    uint32_t single_texture_width = 0;
    uint32_t single_texture_height = 0;
    if (profile == Profile::kPitchSingleTextureColor ||
        profile == Profile::kTex7FanColor) {
        const uint32_t format0 = raster_state_[kRasterTextureFormat0];
        const uint32_t offset0 = raster_state_[kRasterTextureOffset0];
        const bool witnessed_single_texture =
            (profile == Profile::kPitchSingleTextureColor &&
            format0 == 0x0AA1062A &&
            raster_state_[kRasterTextureAddress0] == 0x00030404 &&
            raster_state_[kRasterTextureFilter0] == 0x02022000 &&
            raster_state_[kRasterTextureControl0_0] == 0x4003FFC0 &&
            raster_state_[kRasterTextureControl1_0] == 0x14000000 &&
            raster_state_[kRasterTextureImageRect0] == 0x050001E0 &&
            raster_state_[kRasterTexturePalette0] == 0 &&
            raster_state_[kRasterTextureBorderColor0] == 0xFF000000) ||
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
    std::vector<uint8_t> pitch_tex5_texture_data;
    std::unique_ptr<SwizzleLookup> pitch_tex5_texture_swizzle;
    uint32_t pitch_tex5_texture_width = 0;
    uint32_t pitch_tex5_texture_height = 0;
    if (profile == Profile::kPitchTex5ProjectedColor) {
        const uint32_t format0 = raster_state_[kRasterTextureFormat0];
        const uint32_t offset0 = raster_state_[kRasterTextureOffset0];
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
    std::vector<uint8_t> inline_texture_data;
    std::unique_ptr<SwizzleLookup> inline_texture_swizzle;
    uint32_t inline_texture_width = 0;
    uint32_t inline_texture_height = 0;
    if (profile == Profile::kPitchAdditiveColor) {
        const uint32_t format0 = raster_state_[kRasterTextureFormat0];
        const uint32_t address0 = raster_state_[kRasterTextureAddress0];
        // The dashboard switches the unused P coordinate between WRAP and
        // CLAMP_TO_EDGE. PROJECT2D only consumes the witnessed clamped U/V.
        const bool witnessed_inline_address =
            address0 == 0x00010303 || address0 == 0x00030303;
        const uint32_t border0 =
            raster_state_[kRasterTextureBorderColor0];
        const bool witnessed_inline_border =
            border0 == 0 || border0 == 0xFF000000;
        const bool witnessed_inline_texture =
            format0 == 0x0888062A &&
            witnessed_inline_address &&
            raster_state_[kRasterTextureFilter0] == 0x02022000 &&
            raster_state_[kRasterTextureControl0_0] == 0x4003FFC0 &&
            raster_state_[kRasterTextureControl1_0] == 0x14000000 &&
            raster_state_[kRasterTexturePalette0] == 0 &&
            witnessed_inline_border;
        if (!witnessed_inline_texture || (format0 & 3u) != 2u) {
            char why[320];
            std::snprintf(
                why, sizeof(why),
                "NV2A inline-color texture exceeds the witnessed subset "
                "(fmt=%08x addr=%08x filter=%08x ctl0=%08x ctl1=%08x "
                "pal=%08x border=%08x)",
                format0, address0,
                raster_state_[kRasterTextureFilter0],
                raster_state_[kRasterTextureControl0_0],
                raster_state_[kRasterTextureControl1_0],
                raster_state_[kRasterTexturePalette0],
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
                             raster_state_[kRasterTextureOffset0],
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

    if (!resolve_renderer_color_overlap(color_address, static_cast<size_t>(color_bytes64),
                                        "witnessed color surface")) {
        return false;
    }
    PagedRasterSurface depth_surface(
        depth_address, static_cast<size_t>(depth_bytes64),
        mem_read_, mem_write_);
    PagedRasterSurface color_surface(
        color_address, static_cast<size_t>(color_bytes64),
        mem_read_, mem_write_);
    std::unique_ptr<SwizzleLookup> surface_swizzle;
    if (profile == Profile::kSwizzledRadialColor ||
        profile == Profile::kSwizzledTexturedColor ||
        profile == Profile::kSwizzledProjectedColor) {
        surface_swizzle =
            std::make_unique<SwizzleLookup>(surface_width, surface_height);
    }

    uint32_t xmin = clip_x;
    uint32_t xmax = clip_x + clip_width - 1;
    uint32_t ymin = clip_y;
    uint32_t ymax = clip_y + clip_height - 1;
    const uint32_t window_x = raster_state_[kRasterWindowClipHorizontal];
    const uint32_t window_y = raster_state_[kRasterWindowClipVertical];
    xmin = std::max(xmin, window_x & 0xFFFu);
    xmax = std::min(xmax, (window_x >> 16) & 0xFFFu);
    ymin = std::max(ymin, window_y & 0xFFFu);
    ymax = std::min(ymax, (window_y >> 16) & 0xFFFu);
    if (xmax < xmin || ymax < ymin) {
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
    std::memcpy(&clip_min, &raster_state_[kRasterClipMin], sizeof(clip_min));
    std::memcpy(&clip_max, &raster_state_[kRasterClipMax], sizeof(clip_max));
    float polygon_scale = 0.0f, polygon_bias = 0.0f;
    if (enables & (1u << 13)) {
        std::memcpy(&polygon_scale,
                    &raster_state_[kRasterPolygonOffsetScale],
                    sizeof(polygon_scale));
        std::memcpy(&polygon_bias,
                    &raster_state_[kRasterPolygonOffsetBias],
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
                    raster_state_[kRasterCombinerFactor0_0 + 3];
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
                    raster_state_[kRasterCombinerFactor0_0 + 3];
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

    bool raster_failed = false;
    auto rasterize_triangle =
        [&](const TransformedVertex& input_a,
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
            if (pitch_tex5_profile) {
                if (area >= 0) return;  // FRONT culling.
                std::swap(b, c);
                std::swap(bx, cx);
                std::swap(by, cy);
                area = -area;
            } else if (area < 0 &&
                       profile == Profile::kInlineLinearBlitColor) {
                std::swap(b, c);
                std::swap(bx, cx);
                std::swap(by, cy);
                area = -area;  // Culling is disabled for this blit.
            } else if (area <= 0) {
                return;  // CW is front-facing in the witnessed culled states.
            }
            if (area == 0) return;

            if (profile == Profile::kPitchZ16Depth)
                ++rasterized_triangles_;
            else if (profile == Profile::kSwizzledRadialColor)
                ++radial_color_triangles_;
            else if (profile == Profile::kSwizzledTexturedColor)
                ++textured_color_triangles_;
            else if (profile == Profile::kPitchTexturedColor)
                ++pitch_textured_color_triangles_;
            else if (profile == Profile::kSwizzledProjectedColor)
                ++projected_color_triangles_;
            else if (profile == Profile::kPitchProjectedColor)
                ++pitch_projected_color_triangles_;
            else if (profile == Profile::kPitchMultiTextureColor)
                ++pitch_multi_texture_triangles_;
            else if (profile == Profile::kPitchThreeTextureColor)
                ++pitch_three_texture_triangles_;
            else if (pitch_tex5_profile)
                ++pitch_tex5_triangles_;
            else if (profile == Profile::kInlineLinearBlitColor)
                ++inline_linear_blit_triangles_;
            else if (profile == Profile::kPitchSingleTextureColor)
                ++pitch_single_texture_triangles_;
            else if (profile == Profile::kTex7FanColor)
                ++tex7_fan_triangles_;
            else if (profile == Profile::kTexfProjectedFanColor)
                ++texf_projected_fan_triangles_;
            else
                ++inline_color_triangles_;
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
                    if (profile == Profile::kPitchZ16Depth)
                        ++rasterized_fragments_;
                    else if (profile == Profile::kSwizzledRadialColor)
                        ++radial_color_fragments_;
                    else if (profile == Profile::kSwizzledTexturedColor)
                        ++textured_color_fragments_;
                    else if (profile == Profile::kPitchTexturedColor)
                        ++pitch_textured_color_fragments_;
                    else if (profile == Profile::kSwizzledProjectedColor)
                        ++projected_color_fragments_;
                    else if (profile == Profile::kPitchProjectedColor)
                        ++pitch_projected_color_fragments_;
                    else if (profile == Profile::kPitchMultiTextureColor)
                        ++pitch_multi_texture_fragments_;
                    else if (profile == Profile::kPitchThreeTextureColor)
                        ++pitch_three_texture_fragments_;
                    else if (pitch_tex5_profile)
                        ++pitch_tex5_fragments_;
                    else if (profile == Profile::kInlineLinearBlitColor)
                        ++inline_linear_blit_fragments_;
                    else if (profile == Profile::kPitchSingleTextureColor)
                        ++pitch_single_texture_fragments_;
                    else if (profile == Profile::kTex7FanColor)
                        ++tex7_fan_fragments_;
                    else if (profile == Profile::kTexfProjectedFanColor)
                        ++texf_projected_fan_fragments_;
                    else
                        ++inline_color_fragments_;
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
                    if (profile == Profile::kPitchZ16Depth) {
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
                        ++depth_writes_;
                        const uint32_t address =
                            depth_address + static_cast<uint32_t>(offset);
                        last_raster_address_ = address;
                        for (uint32_t word :
                             {address, static_cast<uint32_t>(incoming)}) {
                            raster_output_hash_ ^= word;
                            raster_output_hash_ *= 1099511628211ull;
                        }
                        continue;
                    }
                    if (profile == Profile::kInlineLinearBlitColor) {
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
                        continue;
                    }
                    if (profile == Profile::kPitchMultiTextureColor ||
                        profile == Profile::kPitchThreeTextureColor) {
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
                        if (profile == Profile::kPitchMultiTextureColor)
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
                                        raster_state_[kRasterCombinerFactor0_0 + 3];
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
                            profile == Profile::kPitchThreeTextureColor
                                ? &pitch_three_texture_rescale_direction0_
                                : nullptr);
                        narrow_cube_direction(
                            direction1_double, direction1,
                            profile == Profile::kPitchThreeTextureColor
                                ? &pitch_three_texture_rescale_direction1_
                                : nullptr);
                        std::array<float, 3> texture0{};
                        std::array<float, 3> texture1{};
                        uint64_t* zero_direction0 =
                            profile == Profile::kPitchThreeTextureColor
                                ? &pitch_three_texture_zero_direction0_
                                : nullptr;
                        uint64_t* zero_direction1 =
                            profile == Profile::kPitchThreeTextureColor
                                ? &pitch_three_texture_zero_direction1_
                                : nullptr;
                        uint64_t* xy_tie_direction0 =
                            profile == Profile::kPitchThreeTextureColor
                                ? &pitch_three_texture_xy_tie_direction0_
                                : nullptr;
                        uint64_t* xy_tie_direction1 =
                            profile == Profile::kPitchThreeTextureColor
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
                        if (profile == Profile::kPitchMultiTextureColor)
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
                        if (profile == Profile::kPitchMultiTextureColor &&
                            std::lround(source_alpha * 255.0f) < 1)
                            continue;  // GEQUAL alpha reference 1.
                        if (profile == Profile::kPitchMultiTextureColor)
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
                            raster_state_[kRasterCombinerFactor0_0 + 3];
                        const uint32_t stage4_factor =
                            raster_state_[kRasterCombinerFactor0_0 + 4];
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
                        if (profile == Profile::kPitchMultiTextureColor) {
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
                        if (profile == Profile::kPitchMultiTextureColor)
                            last_pitch_multi_texture_raster_address_ =
                                color_pixel_address;
                        else
                            last_pitch_three_texture_raster_address_ =
                                color_pixel_address;
                        for (uint32_t word :
                             {color_pixel_address, color,
                              depth_pixel_address, depth_stencil}) {
                            uint64_t& output_hash =
                                profile == Profile::kPitchMultiTextureColor
                                    ? pitch_multi_texture_output_hash_
                                    : pitch_three_texture_output_hash_;
                            output_hash ^= word;
                            output_hash *= 1099511628211ull;
                        }
                        continue;
                    }
                    if (pitch_tex5_profile) {
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
                        if (profile ==
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
                        continue;
                    }
                    if (profile == Profile::kTexfProjectedFanColor) {
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
                        ++texf_projected_fan_writes_;
                        const uint32_t address = color_address + static_cast<uint32_t>(offset);
                        last_texf_projected_fan_raster_address_ = address;
                        for (uint32_t word : {address, color}) {
                            texf_projected_fan_output_hash_ ^= word;
                            texf_projected_fan_output_hash_ *= 1099511628211ull;
                        }
                        continue;
                    }
                    if (profile == Profile::kPitchSingleTextureColor ||
                        profile == Profile::kTex7FanColor) {
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
                        if (profile == Profile::kPitchSingleTextureColor)
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
                                profile == Profile::kPitchSingleTextureColor
                                    ? TextureAddressMode::kBorder
                                    : TextureAddressMode::kClampToEdge,
                                0xFF000000, texture, nullptr)) {
                            fault_(
                                "NV2A pitch single-texture projection is invalid");
                            raster_failed = true;
                            return;
                        }
                        if (profile == Profile::kPitchSingleTextureColor)
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
                            profile == Profile::kTex7FanColor
                                ? std::array<float, 3>{{texture[0] * (160.0f / 255.0f),
                                                        texture[1],
                                                        texture[2] * (64.0f / 255.0f)}}
                                : texture;
                        uint32_t destination = 0;
                        if (profile == Profile::kTex7FanColor &&
                            !color_surface.read32(offset, destination)) {
                            fault_("NV2A tex7 fan color DMA read failed");
                            raster_failed = true;
                            return;
                        }
                        auto add_saturate = [&](float value, uint32_t dst) {
                            return pack_unorm8(value +
                                static_cast<float>(dst) / 255.0f);
                        };
                        const uint32_t color = profile == Profile::kTex7FanColor
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
                        if (profile == Profile::kPitchSingleTextureColor)
                            ++pitch_single_texture_writes_;
                        else
                            ++tex7_fan_writes_;
                        const uint32_t address =
                            color_address + static_cast<uint32_t>(offset);
                        if (profile == Profile::kPitchSingleTextureColor) {
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
                        continue;
                    }
                    if (profile == Profile::kPitchAdditiveColor) {
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
                            raster_state_[kRasterCombinerFactor0_0];
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
                        ++inline_color_writes_;
                        const uint32_t address =
                            color_address + static_cast<uint32_t>(offset);
                        last_inline_raster_address_ = address;
                        for (uint32_t word : {address, color}) {
                            inline_color_output_hash_ ^= word;
                            inline_color_output_hash_ *= 1099511628211ull;
                        }
                        continue;
                    }

                    const size_t offset =
                        (profile == Profile::kPitchTexturedColor ||
                         profile == Profile::kPitchProjectedColor)
                            ? static_cast<size_t>(y) * pitch +
                                  static_cast<size_t>(x) * sizeof(uint32_t)
                            : static_cast<size_t>(surface_swizzle->index(
                                  static_cast<uint32_t>(x),
                                  static_cast<uint32_t>(y))) *
                                  sizeof(uint32_t);
                    const uint32_t incoming = static_cast<uint32_t>(
                        std::floor(std::clamp(depth, 0.0f, 16777215.0f)));
                    uint32_t stored_depth_stencil = 0;
                    if (!depth_surface.read32(offset, stored_depth_stencil)) {
                        fault_("NV2A color zeta DMA read failed");
                        raster_failed = true;
                        return;
                    }
                    if (incoming > (stored_depth_stencil >> 8)) continue;
                    if (profile == Profile::kSwizzledRadialColor)
                        ++radial_color_depth_passes_;
                    else if (profile == Profile::kSwizzledTexturedColor)
                        ++textured_color_depth_passes_;
                    else if (profile == Profile::kPitchTexturedColor)
                        ++pitch_textured_color_depth_passes_;
                    else if (profile == Profile::kPitchProjectedColor)
                        ++pitch_projected_color_depth_passes_;
                    else
                        ++projected_color_depth_passes_;

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
                    if (profile == Profile::kSwizzledRadialColor) {
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
                        const bool textured_family =
                            profile == Profile::kSwizzledTexturedColor ||
                            profile == Profile::kPitchTexturedColor;
                        if (textured_family) {
                            if (profile == Profile::kPitchTexturedColor)
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
                            if (profile == Profile::kPitchProjectedColor)
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
                            raster_state_[kRasterCombinerFactor0_0 +
                                          factor_stage];
                        const uint32_t factor1 =
                            raster_state_[kRasterCombinerFactor1_0 +
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

                    if (!color_surface.write32(offset, color) ||
                        !depth_surface.write32(offset, depth_stencil)) {
                        fault_("NV2A color raster cache write failed");
                        raster_failed = true;
                        return;
                    }
                    const uint32_t color_pixel_address =
                        color_address + static_cast<uint32_t>(offset);
                    const uint32_t depth_pixel_address =
                        depth_address + static_cast<uint32_t>(offset);
                    if (profile == Profile::kSwizzledRadialColor) {
                        ++radial_color_writes_;
                        ++radial_color_depth_writes_;
                        last_color_raster_address_ = color_pixel_address;
                        for (uint32_t word :
                             {color_pixel_address, color,
                              depth_pixel_address, depth_stencil}) {
                            radial_color_output_hash_ ^= word;
                            radial_color_output_hash_ *= 1099511628211ull;
                        }
                    } else if (profile == Profile::kSwizzledTexturedColor) {
                        ++textured_color_writes_;
                        ++textured_color_depth_writes_;
                        last_textured_raster_address_ = color_pixel_address;
                        for (uint32_t word :
                             {color_pixel_address, color,
                              depth_pixel_address, depth_stencil}) {
                            textured_color_output_hash_ ^= word;
                            textured_color_output_hash_ *= 1099511628211ull;
                        }
                    } else if (profile == Profile::kPitchTexturedColor) {
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
                    } else if (profile == Profile::kPitchProjectedColor) {
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
        };

    const size_t vertex_count = transformed_batch_.size();
    if (primitive_mode_ == 5) {
        for (size_t i = 0; i + 2 < vertex_count; i += 3)
            rasterize_triangle(transformed_batch_[i],
                               transformed_batch_[i + 1],
                               transformed_batch_[i + 2]);
    } else if (primitive_mode_ == 6) {
        for (size_t i = 2; i < vertex_count; ++i) {
            if ((i & 1u) == 0)
                rasterize_triangle(transformed_batch_[i - 2],
                                   transformed_batch_[i - 1],
                                   transformed_batch_[i]);
            else
                rasterize_triangle(transformed_batch_[i - 1],
                                   transformed_batch_[i - 2],
                                   transformed_batch_[i]);
        }
    } else {
        for (size_t i = 2; i < vertex_count; ++i)
            rasterize_triangle(transformed_batch_[0],
                               transformed_batch_[i - 1],
                               transformed_batch_[i]);
    }

    if (raster_failed) return false;
    if (uses_depth_surface && !depth_surface.flush()) {
        fault_("NV2A raster zeta DMA write failed");
        return false;
    }
    if (color_profile && !color_surface.flush()) {
        fault_("NV2A color DMA write failed");
        return false;
    }
    if (color_profile)
        invalidate_renderer_color_overlap(color_address, static_cast<size_t>(color_bytes64));
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
//   * surface: pitch-linear color (surface_type==1), no antialiasing; color_format 8
//     (A8R8G8B8, alpha stored as computed) or 4 (X8R8G8B8, alpha byte forced to 0xFF
//     on write -- there is no alpha channel on real hardware); optional Z24S8 zeta
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
//     bit (stencil test=7, logic op=8, ...) is unimplemented and gates a skip
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
//   * stencil test, logic op, any blend equation other than FUNC_ADD, any blend factor
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
bool Nv2a::rasterize_generic_combiner_batch(bool& executed) {
    executed = false;
    ++generic_combiner_attempted_batches_;
    // This routine is reached only by the strict generic fallback. Keep all extra
    // counter snapshots and clock reads behind --perf-accounting: normal replays
    // retain the pre-profiling path's fingerprint-batch census cost only.
    const bool profile_generic_fingerprint = perf::g_enabled;
    const uint64_t generic_fragments_before =
        profile_generic_fingerprint ? generic_combiner_fragments_ : 0;
    const uint64_t generic_writes_before =
        profile_generic_fingerprint ? generic_combiner_writes_ : 0;
    std::chrono::steady_clock::time_point generic_started_at;
    if (profile_generic_fingerprint)
        generic_started_at = std::chrono::steady_clock::now();
    uint64_t generic_batch_bbox_pixels = 0;
    const bool supported_primitive =
        primitive_mode_ == 5 || primitive_mode_ == 6 || primitive_mode_ == 7;
    if (!supported_primitive) {
        record_generic_skip(GenericSkipReason::kUnsupportedPrimitive);
        return true;
    }
    const uint32_t color_format = surface_format_ & 0xFu;
    const uint32_t surface_type = (surface_format_ >> 8) & 0xFu;
    const uint32_t anti_aliasing = (surface_format_ >> 12) & 0xFu;
    const uint32_t zeta_format = (surface_format_ >> 4) & 0xFu;
    const uint32_t enables = raster_state_[kRasterEnables];

    // --- Per-feature semantic gate -----------------------------------------------
    // Every check below tests the specific behavior the shared rasterizer actually
    // implements, not a single magic register tuple. Anything requesting a behavior
    // this evaluator does not (yet) implement records a NAMED reason carrying the
    // offending register value, so the next witnessed state can be added without
    // re-deriving the whole gate.
    if (surface_type != 1 || anti_aliasing != 0) {
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
    if ((raster_state_[kRasterControl0] & kControl0Required) != kControl0Required ||
        (raster_state_[kRasterControl0] & kControl0Unsupported) != 0) {
        record_generic_skip_detail(GenericSkipReason::kUnsupportedControl0,
                                   raster_state_[kRasterControl0]);
        return true;
    }
    // Shading: the shared per-pixel loop always interpolates diffuse/specular smoothly
    // (Gouraud); flat shading is not implemented.
    if (raster_state_[kRasterShadeMode] != 0x1D01) {
        record_generic_skip_detail(GenericSkipReason::kUnsupportedShadeMode,
                                   raster_state_[kRasterShadeMode]);
        return true;
    }
    // Polygon mode: the shared rasterizer only ever fills triangles (no line/point
    // polygon-mode rendering), for both front and back faces independently.
    if (raster_state_[kRasterFrontPolygonMode] != 0x1B02 ||
        raster_state_[kRasterBackPolygonMode] != 0x1B02) {
        record_generic_skip_detail(
            GenericSkipReason::kUnsupportedPolygonMode,
            (static_cast<uint64_t>(raster_state_[kRasterFrontPolygonMode]) << 32) |
                raster_state_[kRasterBackPolygonMode]);
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
        if (raster_state_[kRasterCullFace] == 0x0405) {
            cull_front = false;
        } else if (raster_state_[kRasterCullFace] == 0x0404) {
            cull_front = true;
        } else {
            record_generic_skip_detail(GenericSkipReason::kUnsupportedCullFace,
                                       raster_state_[kRasterCullFace]);
            return true;
        }
        if (raster_state_[kRasterFrontFace] != 0x0900) {
            record_generic_skip_detail(GenericSkipReason::kUnsupportedFrontFace,
                                       raster_state_[kRasterFrontFace]);
            return true;
        }
    }
    // Z min/max control is not read anywhere in this evaluator's math; every witnessed
    // profile carries the same value 1, so require it named rather than assume it is
    // inert.
    if (raster_state_[kRasterZMinMaxControl] != 1) {
        record_generic_skip_detail(GenericSkipReason::kUnsupportedZMinMax,
                                   raster_state_[kRasterZMinMaxControl]);
        return true;
    }
    // Color mask: honor each of the four per-channel write-enable bytes exactly
    // (0=disabled, 1=enabled per channel), applied for real at write time below,
    // instead of requiring the single all-channels-enabled tuple.
    const uint32_t color_mask_word = raster_state_[kRasterColorMask];
    bool color_mask_write[4];
    bool color_mask_supported = true;
    for (size_t channel = 0; channel < 4; ++channel) {
        const uint32_t byte = (color_mask_word >> (channel * 8)) & 0xFFu;
        color_mask_write[channel] = byte != 0;
        if (byte != 0 && byte != 1) color_mask_supported = false;
    }
    if (!color_mask_supported) {
        record_generic_skip_detail(GenericSkipReason::kUnsupportedColorMask,
                                   color_mask_word);
        return true;
    }
    // Enables: bits this evaluator does not implement (stencil test, logic op) must
    // gate a skip if the dashboard/game actually turns them on; bits it always treats
    // as inert (dither, point/line smoothing, point/line polygon offset -- this
    // rasterizer never draws points or lines) are masked out first.
    constexpr uint32_t kEnableInertBits =
        (1u << 4) | (1u << 5) | (1u << 9) | (1u << 10) | (1u << 11) | (1u << 12);
    constexpr uint32_t kEnableHandledBits =
        (1u << 0) | (1u << 1) | (1u << 2) | (1u << 3) | (1u << 13);
    const uint32_t enable_unsupported_bits =
        enables & ~(kEnableInertBits | kEnableHandledBits);
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

    const uint32_t stage_count_raw = raster_state_[kRasterCombinerControl] & 0xFFu;
    if (stage_count_raw == 0) {
        record_generic_skip(GenericSkipReason::kStageCount);
        return true;
    }
    const uint32_t stage_count = std::min<uint32_t>(stage_count_raw, 8u);
    // NV097_SET_COMBINER_CONTROL bit8 selects the MUX_SUM comparison: MSB of R0.alpha
    // (bit set) vs LSB (bit clear) -- a single global flag shared by every stage's
    // MUX_SUM output, matching xemu's PS_COMBINERCOUNT_MUX_MSB.
    const bool mux_on_msb = ((raster_state_[kRasterCombinerControl] >> 8) & 1u) != 0;

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
        const uint32_t ci = raster_state_[kRasterCombinerColorIcw0 + stage];
        const uint32_t ai = raster_state_[kRasterCombinerAlphaIcw0 + stage];
        const uint32_t co = raster_state_[kRasterCombinerColorOcw0 + stage];
        const uint32_t ao = raster_state_[kRasterCombinerAlphaOcw0 + stage];
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
    const uint32_t final_cw0 = raster_state_[kRasterCombinerSpecularFog0];
    const uint32_t final_cw1 = raster_state_[kRasterCombinerSpecularFog1];
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
    const uint32_t alpha_func = raster_state_[kRasterAlphaFunc];
    const uint32_t depth_func = raster_state_[kRasterDepthFunc];
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
    const bool blend_enabled = (enables & (1u << 1)) != 0;
    const uint32_t blend_src = raster_state_[kRasterBlendSrc];
    const uint32_t blend_dst = raster_state_[kRasterBlendDst];
    const uint32_t blend_eq = raster_state_[kRasterBlendEquation];
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

    const uint32_t clip_x = surface_clip_horizontal_ & 0xFFFFu;
    const uint32_t clip_width = surface_clip_horizontal_ >> 16;
    const uint32_t clip_y = surface_clip_vertical_ & 0xFFFFu;
    const uint32_t clip_height = surface_clip_vertical_ >> 16;
    if (!clip_width || !clip_height) {
        record_generic_skip(GenericSkipReason::kUnsupportedSurface);
        return true;
    }
    // SET_SURFACE_PITCH packs an independent color pitch (bits 15:0) and zeta pitch
    // (bits 31:16) -- real hardware allows these to differ (e.g. the dashboard's own
    // witnessed 0x00401400: color pitch 0x1400, zeta pitch 0x0040). Only the color
    // pitch is required to size the color surface; the zeta pitch is validated
    // separately, and only when the zeta surface is actually touched below.
    const uint32_t pitch = surface_pitch_ & 0xFFFFu;
    const uint32_t zeta_pitch = surface_pitch_ >> 16;
    const uint32_t surface_height = clip_y + clip_height;
    const uint64_t color_bytes64 =
        static_cast<uint64_t>(pitch) * surface_height;
    if (!pitch || static_cast<uint64_t>(clip_x + clip_width) * 4 > pitch ||
        color_bytes64 > SIZE_MAX) {
        record_generic_skip_detail(GenericSkipReason::kUnsupportedSurface, pitch);
        return true;
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
    const bool depth_write_enabled = raster_state_[kRasterDepthMask] != 0;
    const bool wants_zeta = depth_test_enabled_bit || depth_write_enabled;
    bool uses_depth = zeta_format == 2 && wants_zeta && zeta_pitch == pitch;
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
    if (!resolve_surface(color_dma_, color_dma_bound_, surface_color_offset_,
                         color_bytes64, color_address)) {
        fault_("NV2A generic-combiner color surface is unbound or overflows");
        return false;
    }
    if (uses_depth &&
        !resolve_surface(zeta_dma_, zeta_dma_bound_, surface_zeta_offset_,
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
    const uint32_t shader_stage_program = raster_state_[kRasterShaderStageProgram];
    auto decode_address_mode = [](uint32_t byte, bool& supported) {
        supported = true;
        switch (byte) {
            case 1: return TextureAddressMode::kRepeat;
            case 3: return TextureAddressMode::kClampToEdge;
            case 4: return TextureAddressMode::kBorder;
            default: supported = false; return TextureAddressMode::kRepeat;
        }
    };
    for (uint32_t stage = 0; stage < 4; ++stage) {
        if (!texture_referenced[stage]) continue;
        if ((raster_state_[kRasterTextureEnableMask] & (1u << stage)) == 0) {
            record_generic_skip_detail(GenericSkipReason::kTextureStage, stage);
            return true;
        }
        const uint32_t stage_mode = (shader_stage_program >> (stage * 5)) & 0x1Fu;
        TextureStage& ts = texture_stages[stage];
        if (shader_stage_program == 1 && stage_mode == 1) {
            if (stage != 0) {
                record_generic_skip_detail(
                    GenericSkipReason::kUnsupportedTextureShaderProgram,
                    (1ull << 32) | stage);
                return true;
            }
            const uint32_t rect = raster_state_[kRasterTextureImageRect0 + stage];
            ts.width = rect >> 16;
            ts.height = rect & 0xFFFFu;
            ts.pitch = raster_state_[kRasterTextureControl1_0 + stage] >> 16;
            if (!ts.width || !ts.height ||
                ts.pitch < ts.width * static_cast<uint32_t>(sizeof(uint32_t))) {
                record_generic_skip_detail(GenericSkipReason::kUnsupportedTextureFormat,
                                           raster_state_[kRasterTextureFormat0 + stage]);
                return true;
            }
            const uint64_t bytes64 = static_cast<uint64_t>(ts.pitch) * ts.height;
            uint32_t address = 0;
            if (bytes64 > SIZE_MAX ||
                !resolve_surface(texture_dma_b_, texture_dma_b_bound_,
                                 raster_state_[kRasterTextureOffset0 + stage], bytes64,
                                 address)) {
                fault_("NV2A generic-combiner texture surface is unbound or overflows");
                return false;
            }
            ts.data.resize(static_cast<size_t>(bytes64));
            if (!resolve_renderer_color_overlap(address, ts.data.size(),
                                                "generic texture read")) {
                return false;
            }
            if (!mem_read_ || !mem_read_(address, ts.data.data(), ts.data.size())) {
                fault_("NV2A generic-combiner texture DMA read failed");
                return false;
            }
            generic_texture_bytes_this_batch += ts.data.size();
            perf::record_generic_texture_allocation(
                ts.data.size(), generic_texture_bytes_this_batch);
            ts.linear = true;
            ts.valid = true;
        } else if (shader_stage_program == 0 ||
                  (stage_count >= 2 && (stage_mode == 1 || stage_mode == 3))) {
            // shader_stage_program==0 (every stage's mode is NONE/0x00) is the
            // original narrowly-proven "ordinary swizzled 2D" special case (matches
            // kSwizzledTexturedColor/kPitchSingleTextureColor/kTex7FanColor's own
            // textures). The newer, fully general per-stage PROJECT2D/CUBEMAP decode
            // is additionally restricted to stage_count>=2 (a real multi-stage
            // combiner, matching the witnessed production evidence for both
            // ShaderStageProgram==0x63 and ==0x8421, comb=00011106/6 stages) --
            // NOT a single-general-combiner-stage program like tex7_fan's own
            // ShaderStageProgram==0x00000C61 (comb=1). Single-stage combiners with a
            // non-{0,1} whole-register value are exactly what the 15 hand-derived
            // Profiles above already cover; letting this evaluator's simpler
            // per-ICW-literal formula loose on that same narrow state produced a
            // wrong (not hardware-matching) answer for tex7_fan's own unwitnessed-
            // border-color test case, because a single referenced TEXTURE0 there
            // does not account for whatever the texture-shader stage chain across
            // stages 1-2 (both CUBEMAP) may or may not feed forward into it on real
            // hardware -- unmodeled here, and only safe to ignore once a real
            // multi-stage combiner is actually consuming those other textures too.
            const uint32_t format = raster_state_[kRasterTextureFormat0 + stage];
            const uint32_t color_code = (format >> 8) & 0xFFu;
            // Color codes 6/7 are the raw-ARGB8 (swizzled) codes cross-checked against
            // the pinned profiles' own textures; 0x19 is NV097_SET_TEXTURE_FORMAT_
            // COLOR_SZ_A8 (production-witnessed: T1-T3 of the menu scene's PROJECT2D
            // texture chain, format word 0x0881192a) -- a swizzled, 1-byte-per-texel,
            // alpha-only format (see sample_swizzled's alpha8 branch above). Anything
            // else (DXT-compressed, other SZ_*/LU_* codes) stays a named census rather
            // than a guess at a new pixel decode.
            const bool alpha8 = color_code == 0x19;
            if ((format & 3u) != 2u ||
                (color_code != 6 && color_code != 7 && !alpha8)) {
                record_generic_skip_detail(GenericSkipReason::kUnsupportedTextureFormat,
                                           format);
                return true;
            }
            if (raster_state_[kRasterTexturePalette0 + stage] != 0) {
                record_generic_skip_detail(GenericSkipReason::kUnsupportedTextureFormat,
                                           0x80000000ull | format);
                return true;
            }
            const uint32_t address_word = raster_state_[kRasterTextureAddress0 + stage];
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
                !resolve_surface(texture_dma_b_, texture_dma_b_bound_,
                                 raster_state_[kRasterTextureOffset0 + stage], bytes64,
                                 address)) {
                fault_("NV2A generic-combiner texture surface is unbound or overflows");
                return false;
            }
            ts.data.resize(static_cast<size_t>(bytes64));
            if (!resolve_renderer_color_overlap(address, ts.data.size(),
                                                "generic texture read")) {
                return false;
            }
            if (!mem_read_ || !mem_read_(address, ts.data.data(), ts.data.size())) {
                fault_("NV2A generic-combiner texture DMA read failed");
                return false;
            }
            generic_texture_bytes_this_batch += ts.data.size();
            perf::record_generic_texture_allocation(
                ts.data.size(), generic_texture_bytes_this_batch);
            ts.swizzle = std::make_unique<SwizzleLookup>(ts.width, ts.height);
            ts.address_u = address_u;
            ts.address_v = address_v;
            ts.border = raster_state_[kRasterTextureBorderColor0 + stage];
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

    if (renderer_) {
        Nv2aGenericCombinerPacket packet;
        packet.primitive_mode = primitive_mode_;
        packet.surface_format = surface_format_;
        packet.surface_pitch = surface_pitch_;
        packet.surface_height = surface_height;
        packet.color_address = color_address;
        packet.depth_address = depth_address;
        packet.raster_state.assign(raster_state_.begin(), raster_state_.end());
        packet.vertices.reserve(transformed_batch_.size());
        for (const auto& vertex : transformed_batch_) {
            packet.vertices.push_back(
                {vertex.position, vertex.diffuse, vertex.specular, vertex.texture});
        }
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
                raster_state_[kRasterTextureAddress0 + stage];
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
        const bool no_texture_inputs = !texture_referenced[0] &&
                                       !texture_referenced[1] &&
                                       !texture_referenced[2] &&
                                       !texture_referenced[3];
        const bool compute_fixture_state =
            primitive_mode_ == 5 && transformed_batch_.size() == 3 &&
            no_texture_inputs && !uses_depth && color_format_a8 &&
            stage_count == 1 && (!cull_enabled || !cull_front) &&
            (!alpha_test_enabled ||
             (alpha_func == 0x0207 &&
              (raster_state_[kRasterAlphaRef] & 0xFFu) == 0)) &&
            !blend_enabled && color_mask_word == 0x01010101 &&
            raster_state_[kRasterShaderStageProgram] == 0 &&
            raster_state_[kRasterShaderOtherStageInput] == 0x00210000 &&
            raster_state_[kRasterCombinerControl] == 1 &&
            raster_state_[kRasterCombinerColorIcw0] == 0x04200000 &&
            raster_state_[kRasterCombinerAlphaIcw0] == 0x14200000 &&
            raster_state_[kRasterCombinerColorOcw0] == 0x00000C00 &&
            raster_state_[kRasterCombinerAlphaOcw0] == 0x00000C00 &&
            raster_state_[kRasterCombinerSpecularFog0] == 0x0000000C &&
            raster_state_[kRasterCombinerSpecularFog1] == 0x00001C80 &&
            raster_state_[kRasterCombinerFinalFactor0] == 0 &&
            raster_state_[kRasterCombinerFinalFactor1] == 0 &&
            same_diffuse(transformed_batch_[0], transformed_batch_[1]) &&
            same_diffuse(transformed_batch_[0], transformed_batch_[2]);
        if (compute_fixture_state) {
            const auto& diffuse = transformed_batch_[0].diffuse;
            auto pack_component = [](float value) {
                return static_cast<uint32_t>(std::floor(
                    std::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f));
            };
            bool positions_representable = true;
            for (size_t vertex = 0; vertex < 3; ++vertex) {
                for (size_t component = 0; component < 2; ++component) {
                    const float position = transformed_batch_[vertex].position[component];
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
                packet.fixture_clip_x = clip_x;
                packet.fixture_clip_y = clip_y;
                packet.fixture_clip_width = clip_width;
                packet.fixture_clip_height = clip_height;
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
            primitive_mode_ == 7 && transformed_batch_.size() == 4;
        for (size_t vertex = 0; dashboard_fullscreen_vertices && vertex < 4; ++vertex) {
            const auto& position = transformed_batch_[vertex].position;
            dashboard_fullscreen_vertices =
                exact_fixed_xy(position[0], dashboard_xy[vertex][0]) &&
                exact_fixed_xy(position[1], dashboard_xy[vertex][1]) &&
                position[2] == 16777215.0f && position[3] == 1.0f;
            for (uint32_t stage = 0; dashboard_fullscreen_vertices && stage < 4;
                 ++stage) {
                for (float component : transformed_batch_[vertex].texture[stage]) {
                    if (!std::isfinite(component)) {
                        dashboard_fullscreen_vertices = false;
                        break;
                    }
                }
            }
        }
        const bool dashboard_project2d_state =
            dashboard_fullscreen_vertices && color_format_a8 && uses_depth &&
            depth_test_enabled_bit && depth_func == 0x0207 &&
            raster_state_[kRasterDepthMask] == 0 && alpha_test_enabled &&
            alpha_func == 0x0206 &&
            (raster_state_[kRasterAlphaRef] & 0xFFu) == 1 &&
            blend_enabled && blend_src == 0x0302 && blend_dst == 1 &&
            blend_eq == 0x8006 && cull_enabled && !cull_front &&
            color_mask_word == 0x01010101 && clip_x == 0 && clip_y == 0 &&
            clip_width == 1280 && clip_height == 480 && pitch == 0x1400 &&
            surface_height == 480 && raster_state_[kRasterWindowClipHorizontal] ==
                                        0x04ff0000 &&
            raster_state_[kRasterWindowClipVertical] == 0x01df0000 &&
            raster_state_[kRasterShaderStageProgram] == 0x00008421 &&
            raster_state_[kRasterShaderOtherStageInput] == 0 &&
            raster_state_[kRasterCombinerControl] == 0x00011106 &&
            raster_state_[kRasterTextureEnableMask] == 0x0000000f &&
            texture_stages[0].valid && texture_stages[1].valid &&
            texture_stages[2].valid && texture_stages[3].valid &&
            !texture_stages[0].linear && !texture_stages[0].cubemap &&
            !texture_stages[0].alpha8 && texture_stages[0].width == 1024 &&
            texture_stages[0].height == 512 && texture_stages[1].alpha8 &&
            texture_stages[2].alpha8 && texture_stages[3].alpha8 &&
            texture_stages[1].width == 256 && texture_stages[1].height == 256 &&
            texture_stages[2].width == 256 && texture_stages[2].height == 256 &&
            texture_stages[3].width == 256 && texture_stages[3].height == 256 &&
            (raster_state_[kRasterTextureAddress0] == 0x00010303 ||
             raster_state_[kRasterTextureAddress0] == 0x00030303) &&
            raster_state_[kRasterTextureAddress0 + 1] == 0x00030101 &&
            raster_state_[kRasterTextureAddress0 + 2] == 0x00030101 &&
            raster_state_[kRasterTextureAddress0 + 3] == 0x00030101;
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
                    raster_state_[kRasterCombinerAlphaIcw0 + stage] ==
                        alpha_icw[stage] &&
                    raster_state_[kRasterCombinerColorIcw0 + stage] ==
                        color_icw[stage] &&
                    raster_state_[kRasterCombinerAlphaOcw0 + stage] ==
                        alpha_ocw[stage] &&
                    raster_state_[kRasterCombinerColorOcw0 + stage] ==
                        color_ocw[stage] &&
                    raster_state_[kRasterCombinerFactor1_0 + stage] == 0;
            }
            if (combiner_words_match &&
                raster_state_[kRasterCombinerFactor0_0] == 0 &&
                raster_state_[kRasterCombinerFactor0_0 + 1] == 0 &&
                raster_state_[kRasterCombinerFactor0_0 + 3] == 0 &&
                raster_state_[kRasterCombinerFactor0_0 + 5] == 0 &&
                raster_state_[kRasterCombinerSpecularFog0] == 0x0000000c &&
                raster_state_[kRasterCombinerSpecularFog1] == 0x00001c80 &&
                raster_state_[kRasterCombinerFinalFactor0] == 0 &&
                raster_state_[kRasterCombinerFinalFactor1] == 0) {
                packet.compute_fixture =
                    Nv2aGenericCombinerPacket::ComputeFixture::kDashboardProject2dFan;
                packet.fixture_factor0_stage2_argb =
                    raster_state_[kRasterCombinerFactor0_0 + 2];
                packet.fixture_factor0_stage4_argb =
                    raster_state_[kRasterCombinerFactor0_0 + 4];
            }
        }
        if (packet.compute_fixture == Nv2aGenericCombinerPacket::ComputeFixture::kNone) {
            using ResidentReject = GenericResidentRejectReason;
            const bool resident_primary_combiner_words =
                raster_state_[kRasterCombinerControl] == 1 &&
                raster_state_[kRasterCombinerColorIcw0] == 0x04200000 &&
                raster_state_[kRasterCombinerAlphaIcw0] == 0x14200000 &&
                raster_state_[kRasterCombinerColorOcw0] == 0x00000C00 &&
                raster_state_[kRasterCombinerAlphaOcw0] == 0x00000C00 &&
                raster_state_[kRasterCombinerSpecularFog0] == 0x0000000C &&
                raster_state_[kRasterCombinerSpecularFog1] == 0x00001C80;
            const bool resident_primary_observable =
                raster_state_[kRasterCombinerControl] == 1 ||
                resident_primary_combiner_words;
            const bool resident_primitive_supported =
                primitive_mode_ == 5 || primitive_mode_ == 6 || primitive_mode_ == 7;
            const bool resident_vertex_count_supported =
                transformed_batch_.size() >= 3 && transformed_batch_.size() <= 20000;
            const bool resident_alpha_supported =
                !alpha_test_enabled ||
                (alpha_func == 0x0207 &&
                 (raster_state_[kRasterAlphaRef] & 0xFFu) == 0);
            const bool resident_blend_supported =
                (!blend_enabled || blend_eq == 0x8006) &&
                (!blend_enabled ||
                 ((blend_src == 0x0302 || blend_src == 1) &&
                  (blend_dst == 0x0303 || blend_dst == 1 || blend_dst == 0)));
            const bool resident_shader_supported =
                raster_state_[kRasterShaderStageProgram] == 0 ||
                raster_state_[kRasterShaderStageProgram] == 1;
            const bool resident_other_stage_supported =
                raster_state_[kRasterShaderOtherStageInput] == 0 ||
                raster_state_[kRasterShaderOtherStageInput] == 0x00210000;
            ResidentReject resident_reject_reason = ResidentReject::kCount;
            if (!resident_primitive_supported) {
                resident_reject_reason = ResidentReject::kUnsupportedPrimitive;
            } else if (!resident_vertex_count_supported) {
                resident_reject_reason = ResidentReject::kVertexCount;
            } else if (!no_texture_inputs) {
                resident_reject_reason = ResidentReject::kTextureInput;
            } else if (uses_depth) {
                resident_reject_reason = ResidentReject::kDepth;
            } else if (color_mask_word != 0x01010101) {
                resident_reject_reason = ResidentReject::kColorMask;
            } else if (!color_format_x8 && !color_format_a8) {
                resident_reject_reason = ResidentReject::kColorFormat;
            } else if (stage_count != 1) {
                resident_reject_reason = ResidentReject::kStageCount;
            } else if (!resident_alpha_supported) {
                resident_reject_reason = ResidentReject::kAlphaMode;
            } else if (!resident_blend_supported) {
                resident_reject_reason = ResidentReject::kBlendMode;
            } else if (!resident_shader_supported) {
                resident_reject_reason = ResidentReject::kShaderProgram;
            } else if (!resident_other_stage_supported) {
                resident_reject_reason = ResidentReject::kOtherStageInput;
            } else if (!resident_primary_combiner_words) {
                resident_reject_reason = ResidentReject::kCombinerWords;
            }
            const bool resident_primary_state =
                resident_reject_reason == ResidentReject::kCount;
            if (resident_primary_state) {
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
                expanded.reserve((packet.vertices.size() - 2) * 3);
                auto append_triangle = [&](size_t ia, size_t ib, size_t ic) {
                    const auto* a = &packet.vertices[ia];
                    const auto* b = &packet.vertices[ib];
                    const auto* c = &packet.vertices[ic];
                    if (!finite_vertex(*a) || !finite_vertex(*b) || !finite_vertex(*c))
                        return false;
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
                    return true;
                };
                bool expanded_ok = true;
                if (primitive_mode_ == 5) {
                    for (size_t i = 0; expanded_ok && i + 2 < packet.vertices.size();
                         i += 3)
                        expanded_ok = append_triangle(i, i + 1, i + 2);
                } else if (primitive_mode_ == 6) {
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
                if (expanded_ok && !expanded.empty()) {
                    packet.vertices = std::move(expanded);
                    packet.primitive_mode = 5;
                    packet.compute_fixture = Nv2aGenericCombinerPacket::
                        ComputeFixture::kResidentPrimaryColorTriangles;
                    packet.fixture_clip_x = clip_x;
                    packet.fixture_clip_y = clip_y;
                    packet.fixture_clip_width = clip_width;
                    packet.fixture_clip_height = clip_height;
                    packet.fixture_blend_enabled = blend_enabled;
                    packet.fixture_blend_src = blend_src;
                    packet.fixture_blend_dst = blend_dst;
                    packet.fixture_color_format_x8 = color_format_x8;
                    ++generic_combiner_resident_candidates_;
                } else if (resident_primary_observable) {
                    record_generic_resident_reject(ResidentReject::kVertexExpansion);
                }
            } else if (resident_primary_observable) {
                record_generic_resident_reject(resident_reject_reason);
            }
        }
        {
            using TextureReject = GenericTextureModulateRejectReason;
            const bool texture_modulate_observable =
                raster_state_[kRasterShaderStageProgram] == 1 &&
                raster_state_[kRasterCombinerControl] == 1;
            if (texture_modulate_observable) {
                ++generic_combiner_texture_modulate_observed_;
                const bool only_texture0_input =
                    texture_referenced[0] && !texture_referenced[1] &&
                    !texture_referenced[2] && !texture_referenced[3];
                const bool primitive_supported =
                    primitive_mode_ == 5 || primitive_mode_ == 6 ||
                    primitive_mode_ == 7;
                const bool vertex_count_supported =
                    transformed_batch_.size() >= 3 &&
                    transformed_batch_.size() <= 20000;
                const bool alpha_supported =
                    !alpha_test_enabled ||
                    (alpha_func == 0x0207 &&
                     (raster_state_[kRasterAlphaRef] & 0xFFu) == 0);
                const bool blend_supported =
                    (!blend_enabled || blend_eq == 0x8006) &&
                    (!blend_enabled ||
                     ((blend_src == 0x0302 || blend_src == 1) &&
                      (blend_dst == 0x0303 || blend_dst == 1 ||
                       blend_dst == 0)));
                const bool combiner_words_match =
                    raster_state_[kRasterCombinerColorIcw0] == 0x04080000 &&
                    raster_state_[kRasterCombinerAlphaIcw0] == 0x14180000 &&
                    raster_state_[kRasterCombinerColorOcw0] == 0x00000C00 &&
                    raster_state_[kRasterCombinerAlphaOcw0] == 0x00000C00 &&
                    raster_state_[kRasterCombinerSpecularFog0] == 0x0000000C &&
                    raster_state_[kRasterCombinerSpecularFog1] == 0x00001C80;
                const bool texture_registers_match =
                    raster_state_[kRasterTextureFormat0] == 0x08890E2A &&
                    raster_state_[kRasterTextureImageRect0] == 0x050003C0 &&
                    raster_state_[kRasterTextureControl1_0] == 0x14000000;
                const bool texture_decoded_match =
                    texture_stages[0].valid && texture_stages[0].linear &&
                    !texture_stages[0].alpha8 && texture_stages[0].width == 1280 &&
                    texture_stages[0].height == 960 &&
                    texture_stages[0].pitch == 0x1400;
                const bool target_surface_match =
                    color_address == 0x03AA4000 && color_format_x8 &&
                    surface_type == 1 && anti_aliasing == 0 &&
                    surface_pitch_ == 0x00401400 && surface_height == 960 &&
                    clip_x == 0 && clip_y == 0 && clip_width == 1280 &&
                    clip_height == 960;
                TextureReject reject_reason = TextureReject::kCount;
                if (!primitive_supported) {
                    reject_reason = TextureReject::kUnsupportedPrimitive;
                } else if (!vertex_count_supported) {
                    reject_reason = TextureReject::kVertexCount;
                } else if (!only_texture0_input) {
                    reject_reason = TextureReject::kTextureInput;
                } else if (uses_depth) {
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
                } else if (raster_state_[kRasterShaderStageProgram] != 1) {
                    reject_reason = TextureReject::kShaderProgram;
                } else if (raster_state_[kRasterShaderOtherStageInput] !=
                           0x00210000) {
                    reject_reason = TextureReject::kOtherStageInput;
                } else if (!combiner_words_match) {
                    reject_reason = TextureReject::kCombinerWords;
                } else if (!texture_registers_match) {
                    reject_reason = TextureReject::kTexture0Registers;
                } else if (!texture_decoded_match) {
                    reject_reason = TextureReject::kTexture0Decoded;
                } else if (!target_surface_match) {
                    reject_reason = TextureReject::kTargetSurface;
                }
                if (reject_reason == TextureReject::kCount) {
                    ++generic_combiner_texture_modulate_exact_;
                } else {
                    record_generic_texture_modulate_reject(reject_reason);
                }
            }
        }
        const bool resident_packet =
            packet.compute_fixture == Nv2aGenericCombinerPacket::ComputeFixture::
                                          kResidentPrimaryColorTriangles;
        if (renderer_->supports_resident_generic_combiner(packet)) {
            ++generic_combiner_resident_supported_;
            const size_t color_size = static_cast<size_t>(color_bytes64);
            const bool already_resident = renderer_->has_resident_color_surface(
                color_address, color_size, surface_pitch_, surface_height,
                surface_format_);
            if (!already_resident) {
                ++generic_combiner_resident_target_switch_resolves_;
                if (!resolve_renderer_color_overlap(
                        0, 64u * 1024u * 1024u,
                        "resident generic target switch")) {
                    return false;
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
            Nv2aGenericCombinerResult result;
            std::string renderer_error;
            if (!renderer_->submit_resident_generic_combiner(packet, result,
                                                             renderer_error)) {
                fault_(renderer_error.empty()
                           ? "NV2A resident generic-combiner submission failed"
                           : renderer_error);
                return false;
            }
            ++generic_combiner_resident_submits_;
            executed = true;
            ++generic_combiner_accelerated_batches_;
            generic_combiner_triangles_ += result.triangles;
            generic_combiner_fragments_ += result.fragments;
            generic_combiner_writes_ += result.writes;
            ++generic_combiner_executed_batches_;
            return true;
        } else if (resident_packet) {
            record_generic_resident_reject(
                GenericResidentRejectReason::kBackendUnsupported);
        }
        if (renderer_->supports_generic_combiner(packet)) {
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

            Nv2aGenericCombinerResult result;
            std::string renderer_error;
            if (!renderer_->submit_generic_combiner(packet, result,
                                                     renderer_error)) {
                fault_(renderer_error.empty()
                           ? "NV2A accelerated generic-combiner submission failed"
                           : renderer_error);
                return false;
            }
            if (result.color_output.size() != packet.color_input.size() ||
                result.depth_output.size() != packet.depth_input.size()) {
                fault_("NV2A accelerated generic-combiner returned invalid surface sizes");
                return false;
            }
            if (!mem_write_ ||
                !mem_write_(color_address, result.color_output.data(),
                            result.color_output.size()) ||
                (uses_depth &&
                 !mem_write_(depth_address, result.depth_output.data(),
                             result.depth_output.size()))) {
                fault_("NV2A accelerated generic-combiner DMA write failed");
                return false;
            }
            invalidate_renderer_color_overlap(color_address, result.color_output.size());
            executed = true;
            ++generic_combiner_accelerated_batches_;
            generic_combiner_triangles_ += result.triangles;
            generic_combiner_fragments_ += result.fragments;
            generic_combiner_writes_ += result.writes;
            ++generic_combiner_executed_batches_;
            return true;
        }
    }
    ++generic_combiner_fallback_batches_;
    executed = true;
    ++generic_combiner_batches_;
    GenericCombinerFingerprintStats* generic_fingerprint_stats = nullptr;
    {
        uint64_t fingerprint = 1469598103934665603ull;
        for (uint32_t index = kRasterShaderStageProgram;
             index <= kRasterCombinerFinalFactor1; ++index) {
            fingerprint ^= raster_state_[index];
            fingerprint *= 1099511628211ull;
        }
        auto it = std::find_if(
            generic_combiner_fingerprints_.begin(),
            generic_combiner_fingerprints_.end(),
            [&](const auto& entry) { return entry.fingerprint == fingerprint; });
        if (it == generic_combiner_fingerprints_.end()) {
            GenericCombinerFingerprintStats entry;
            entry.fingerprint = fingerprint;
            entry.batches = 1;
            generic_combiner_fingerprints_.push_back(std::move(entry));
            generic_fingerprint_stats = &generic_combiner_fingerprints_.back();
        } else {
            ++it->batches;
            generic_fingerprint_stats = &*it;
        }
        if (profile_generic_fingerprint) {
            const uint32_t vertex_count = static_cast<uint32_t>(std::min<size_t>(
                transformed_batch_.size(), std::numeric_limits<uint32_t>::max()));
            if (generic_fingerprint_stats->raster_state_sample_index == UINT32_MAX) {
                generic_fingerprint_stats->raster_state_sample_index =
                    current_raster_state_sample_index_;
                generic_fingerprint_stats->full_state_hash =
                    fnv1a_bytes(raster_state_.data(), sizeof(raster_state_));
                generic_fingerprint_stats->primitive_mode = primitive_mode_;
                generic_fingerprint_stats->min_vertices = vertex_count;
                generic_fingerprint_stats->max_vertices = vertex_count;
                generic_fingerprint_stats->first_vertices_recorded = true;
                generic_fingerprint_stats->first_vertex_count =
                    std::min<uint32_t>(vertex_count, 4);
                for (uint32_t vertex = 0;
                     vertex < generic_fingerprint_stats->first_vertex_count;
                     ++vertex) {
                    generic_fingerprint_stats->first_vertex_position[vertex] =
                        transformed_batch_[vertex].position;
                    generic_fingerprint_stats->first_vertex_texture[vertex] =
                        transformed_batch_[vertex].texture;
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
        if (!profile_generic_fingerprint) return;
        const auto elapsed = std::chrono::steady_clock::now() - generic_started_at;
        generic_fingerprint_stats->fragments +=
            generic_combiner_fragments_ - generic_fragments_before;
        generic_fingerprint_stats->writes +=
            generic_combiner_writes_ - generic_writes_before;
        generic_fingerprint_stats->wall_ns += static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count());
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

    uint32_t xmin = clip_x;
    uint32_t xmax = clip_x + clip_width - 1;
    uint32_t ymin = clip_y;
    uint32_t ymax = clip_y + clip_height - 1;
    const uint32_t window_x = raster_state_[kRasterWindowClipHorizontal];
    const uint32_t window_y = raster_state_[kRasterWindowClipVertical];
    xmin = std::max(xmin, window_x & 0xFFFu);
    xmax = std::min(xmax, (window_x >> 16) & 0xFFFu);
    ymin = std::max(ymin, window_y & 0xFFFu);
    ymax = std::min(ymax, (window_y >> 16) & 0xFFFu);
    if (xmax < xmin || ymax < ymin) {
        record_generic_fingerprint_perf();
        return true;
    }

    float clip_min = 0.0f, clip_max = 0.0f;
    std::memcpy(&clip_min, &raster_state_[kRasterClipMin], sizeof(clip_min));
    std::memcpy(&clip_max, &raster_state_[kRasterClipMax], sizeof(clip_max));
    float polygon_scale = 0.0f, polygon_bias = 0.0f;
    if (enables & (1u << 13)) {
        std::memcpy(&polygon_scale, &raster_state_[kRasterPolygonOffsetScale],
                   sizeof(polygon_scale));
        std::memcpy(&polygon_bias, &raster_state_[kRasterPolygonOffsetBias],
                   sizeof(polygon_bias));
    }

    if (!resolve_renderer_color_overlap(color_address, static_cast<size_t>(color_bytes64),
                                        "software generic color surface")) {
        record_generic_fingerprint_perf();
        return false;
    }
    PagedRasterSurface color_surface(
        color_address, static_cast<size_t>(color_bytes64), mem_read_, mem_write_);
    PagedRasterSurface depth_surface(
        depth_address, static_cast<size_t>(depth_bytes64), mem_read_, mem_write_);

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
                             std::array<float, 4>& color) {
        if (!std::isfinite(s) || !std::isfinite(t) || !width || !height) return false;
        s = std::clamp(s, 0.0f, static_cast<float>(width));
        t = std::clamp(t, 0.0f, static_cast<float>(height));
        const float texel_x = s - 0.5f;
        const float texel_y = t - 0.5f;
        const int32_t x0 = static_cast<int32_t>(std::floor(texel_x));
        const int32_t y0 = static_cast<int32_t>(std::floor(texel_y));
        const float fx = texel_x - static_cast<float>(x0);
        const float fy = texel_y - static_cast<float>(y0);
        auto texel = [&](int32_t x, int32_t y) {
            const uint32_t cx = static_cast<uint32_t>(
                std::clamp(x, 0, static_cast<int32_t>(width) - 1));
            const uint32_t cy = static_cast<uint32_t>(
                std::clamp(y, 0, static_cast<int32_t>(height) - 1));
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
    auto rasterize_triangle = [&](const TransformedVertex& input_a,
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
                ++generic_combiner_fragments_;
                float depth = (static_cast<float>(w0) * a->position[2] +
                              static_cast<float>(w1) * b->position[2] +
                              static_cast<float>(w2) * c->position[2]) *
                             inverse_area;
                depth += polygon_bias + polygon_scale * max_z_slope;
                if (!std::isfinite(depth) || depth < clip_min || depth > clip_max)
                    continue;

                const size_t offset = static_cast<size_t>(y) * pitch +
                                      static_cast<size_t>(x) * sizeof(uint32_t);
                uint32_t stored_depth_stencil = 0;
                uint32_t incoming_depth = 0;
                if (uses_depth) {
                    if (!depth_surface.read32(offset, stored_depth_stencil)) {
                        fault_("NV2A generic-combiner zeta DMA read failed");
                        raster_failed = true;
                        return;
                    }
                    incoming_depth = static_cast<uint32_t>(
                        std::floor(std::clamp(depth, 0.0f, 16777215.0f)));
                    // Depth-test-enable (bit3) is semantic: when disabled every
                    // fragment passes, even though the surface is still touched here
                    // because depth writes are enabled.
                    if (depth_test_enabled_bit &&
                        !compare(depth_func, incoming_depth,
                                stored_depth_stencil >> 8)) {
                        continue;
                    }
                }
                ++generic_combiner_depth_passes_;

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
                auto interpolate4 = [&](auto member) {
                    std::array<float, 4> result{};
                    for (size_t c4 = 0; c4 < 4; ++c4) {
                        auto component = [&](const TransformedVertex* v) {
                            float value = (v->*member)[c4];
                            if (std::isnan(value)) value = c4 == 3 ? 1.0f : 0.0f;
                            return std::clamp(value, 0.0f, 1.0f);
                        };
                        const double d = (q0 * component(a) + q1 * component(b) +
                                         q2 * component(c)) / qsum;
                        if (!std::isfinite(d)) {
                            varying_failed = true;
                            return result;
                        }
                        result[c4] = static_cast<float>(d);
                    }
                    return result;
                };
                diffuse = interpolate4(&TransformedVertex::diffuse);
                specular = interpolate4(&TransformedVertex::specular);
                if (varying_failed) {
                    fault_("NV2A generic-combiner varying is non-finite");
                    raster_failed = true;
                    return;
                }

                std::array<std::array<float, 4>, 16> regs{};
                regs[4] = diffuse;
                regs[5] = specular;

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
                        auto component = [&](const TransformedVertex* v) {
                            float value = v->texture[stage][c4];
                            return std::isnan(value) ? 0.0f : value;
                        };
                        const double t = (q0 * component(a) + q1 * component(b) +
                                         q2 * component(c)) / qsum;
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
                    std::array<float, 4> sampled{};
                    bool sample_ok = false;
                    if (ts.linear) {
                        if (texcoord[3] == 0.0f) {
                            fault_("NV2A generic-combiner texture Q is zero");
                            raster_failed = true;
                            return;
                        }
                        sample_ok = sample_linear(ts.data, ts.width, ts.height,
                                                 ts.pitch, texcoord[0] / texcoord[3],
                                                 texcoord[1] / texcoord[3], sampled);
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
                        sample_ok = sample_swizzled(
                            ts.data, *ts.swizzle, ts.width, ts.height, texcoord[0],
                            texcoord[1], ts.address_u, ts.address_v, ts.border,
                            ts.alpha8, sampled);
                    }
                    if (!sample_ok) {
                        fault_("NV2A generic-combiner texture sample is invalid");
                        raster_failed = true;
                        return;
                    }
                    regs[8 + stage] = sampled;
                    ++generic_combiner_texture_samples_;
                }

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
                        raster_state_[kRasterCombinerFactor0_0 + stage]);
                    regs[2] = unpack_argb8(
                        raster_state_[kRasterCombinerFactor1_0 + stage]);

                    const std::array<float, 3> ca = fetch_color(s.color[0], regs);
                    const std::array<float, 3> cb = fetch_color(s.color[1], regs);
                    const std::array<float, 3> cc = fetch_color(s.color[2], regs);
                    const std::array<float, 3> cd = fetch_color(s.color[3], regs);
                    std::array<float, 3> cab{}, ccd{};
                    if (s.color_ab_dot) {
                        const float dot = ca[0] * cb[0] + ca[1] * cb[1] + ca[2] * cb[2];
                        cab = {{dot, dot, dot}};
                    } else {
                        cab = {{ca[0] * cb[0], ca[1] * cb[1], ca[2] * cb[2]}};
                    }
                    if (s.color_cd_dot) {
                        const float dot = cc[0] * cd[0] + cc[1] * cd[1] + cc[2] * cd[2];
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
                        csum = {{cab[0] + ccd[0], cab[1] + ccd[1], cab[2] + ccd[2]}};
                    }
                    auto store_color = [&](uint32_t dst, const std::array<float, 3>& value,
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
                        s.alpha_mux ? (alpha_mux_condition ? acd : aab) : (aab + acd);
                    auto store_alpha = [&](uint32_t dst, float value) {
                        if (!dst) return;
                        regs[dst][3] = std::clamp(
                            apply_scale_bias(s.alpha_scale_bias, value), -1.0f, 1.0f);
                    };
                    store_alpha(s.alpha_ab_dst, aab);
                    store_alpha(s.alpha_cd_dst, acd);
                    store_alpha(s.alpha_sum_dst, asum);
                }

                // Final combiner: RGB = D + A*B + (1-A)*C; Alpha = G. When absent
                // (both SpecularFogCW words zero), R0/R0.a pass through untouched --
                // which the generic A=B=C=ZERO, D=R0, G=R0.alpha decode already
                // produces on its own, so no separate branch is needed.
                std::array<float, 4> final_color;
                if (final_enabled) {
                    // The final combiner's own CONSTANT_COLOR0/1 (registers 1/2) come
                    // from SPECFOGFACTOR0/1 (FinalFactor0/1), not the per-stage
                    // Factor0_0[stage]/Factor1_0[stage] array -- reload them now that
                    // every ordinary stage above is done reading regs[1]/regs[2].
                    regs[1] = unpack_argb8(raster_state_[kRasterCombinerFinalFactor0]);
                    regs[2] = unpack_argb8(raster_state_[kRasterCombinerFinalFactor1]);
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
                const uint32_t alpha_ref = raster_state_[kRasterAlphaRef] & 0xFFu;
                // Alpha-test-enable (bit0) is semantic: when disabled every fragment
                // passes regardless of AlphaFunc/AlphaRef.
                if (alpha_test_enabled &&
                    !compare(alpha_func, alpha_scaled, alpha_ref)) {
                    continue;
                }

                std::array<float, 3> out_rgb{{
                    std::clamp(final_color[0], 0.0f, 1.0f),
                    std::clamp(final_color[1], 0.0f, 1.0f),
                    std::clamp(final_color[2], 0.0f, 1.0f),
                }};
                // Need the existing pixel if blending is enabled (as a blend operand)
                // or if any color-mask channel is disabled (to preserve it unwritten).
                const bool needs_existing_pixel =
                    blend_enabled || !color_mask_write[0] || !color_mask_write[1] ||
                    !color_mask_write[2] || !color_mask_write[3];
                std::array<float, 4> dst{};
                if (needs_existing_pixel) {
                    uint32_t existing = 0;
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
                }
                auto pack_unorm8 = [](float value) {
                    return static_cast<uint32_t>(
                        std::floor(std::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f));
                };
                // X8R8G8B8 has no alpha channel on real hardware; always store 0xFF in
                // the padding byte. A8R8G8B8 stores the combiner's alpha untouched.
                const uint32_t stored_alpha_byte =
                    color_format_x8 ? 0xFFu : alpha_scaled;
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
                if (uses_depth && raster_state_[kRasterDepthMask] != 0) {
                    const uint32_t depth_stencil =
                        (incoming_depth << 8) | (stored_depth_stencil & 0xFFu);
                    if (!depth_surface.write32(offset, depth_stencil)) {
                        fault_("NV2A generic-combiner zeta DMA write failed");
                        raster_failed = true;
                        return;
                    }
                }
                ++generic_combiner_writes_;
                const uint32_t address = color_address + static_cast<uint32_t>(offset);
                last_generic_combiner_raster_address_ = address;
                for (uint32_t word : {address, color}) {
                    generic_combiner_output_hash_ ^= word;
                    generic_combiner_output_hash_ *= 1099511628211ull;
                }
            }
        }
    };

    const size_t vertex_count = transformed_batch_.size();
    if (primitive_mode_ == 5) {
        for (size_t i = 0; i + 2 < vertex_count; i += 3)
            rasterize_triangle(transformed_batch_[i], transformed_batch_[i + 1],
                              transformed_batch_[i + 2]);
    } else if (primitive_mode_ == 6) {
        for (size_t i = 2; i < vertex_count; ++i) {
            if ((i & 1u) == 0)
                rasterize_triangle(transformed_batch_[i - 2], transformed_batch_[i - 1],
                                  transformed_batch_[i]);
            else
                rasterize_triangle(transformed_batch_[i - 1], transformed_batch_[i - 2],
                                  transformed_batch_[i]);
        }
    } else {
        for (size_t i = 2; i < vertex_count; ++i)
            rasterize_triangle(transformed_batch_[0], transformed_batch_[i - 1],
                              transformed_batch_[i]);
    }

    if (raster_failed) {
        record_generic_fingerprint_perf();
        return false;
    }
    if (!color_surface.flush()) {
        fault_("NV2A generic-combiner color DMA write failed");
        record_generic_fingerprint_perf();
        return false;
    }
    invalidate_renderer_color_overlap(color_address, static_cast<size_t>(color_bytes64));
    if (uses_depth && !depth_surface.flush()) {
        fault_("NV2A generic-combiner zeta DMA write failed");
        record_generic_fingerprint_perf();
        return false;
    }
    ++generic_combiner_executed_batches_;
    record_generic_fingerprint_perf();
    return true;
}

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
    push.seq = methods_.total();
    methods_.push(push);

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
    if (method < 0x2000 && !(method & 3u)) ++method_counts_[method >> 2];

    // PFIFO resolves SET_OBJECT through RAMHT, then PGRAPH caches the object's class for
    // this subchannel.
    if (method == 0) {
        if (object_bound)
            ++class_method_counts_[object_class_[subchannel]];
        return;
    }
    ++class_method_counts_[object_class_[subchannel]];
    // NV_CONTEXT_PATTERN (class 0x44) method NV044_SET_MONOCHROME_COLOR0 feeds the
    // guest-readable PGRAPH pattern-color shadow. The stock dashboard polls this state
    // while setting up its 2D context; modeling the register does not render 2D methods.
    if (object_class_[subchannel] == 0x44) {
        if (method == 0x0310) patt_color0_ = parameter;
        return;
    }
    if (object_class_[subchannel] != 0x97) return;

    // NV097 flip queue. FLIP_INCREMENT_WRITE advances the producer modulo; FLIP_STALL
    // blocks PFIFO only when the producer has caught the display/vblank consumer. The
    // kernel advances READ_3D through NV_PGRAPH_INCREMENT in its vblank ISR.
    if (method == 0x0100 && parameter != 0) {
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
    } else if (method == 0x0120) {
        flip_read_ = static_cast<uint8_t>(parameter & 7u);
    } else if (method == 0x0124) {
        flip_write_ = static_cast<uint8_t>(parameter & 7u);
    } else if (method == 0x0128) {
        flip_modulo_ = static_cast<uint8_t>(parameter & 7u);
    } else if (method == 0x012C) {
        if (!flip_modulo_) {
            fault_("NV2A FLIP_INCREMENT_WRITE with zero modulo");
            return;
        }
        flip_write_ = static_cast<uint8_t>((flip_write_ + 1u) % flip_modulo_);
        ++flip_increments_;
    } else if (method == 0x0130) {
        ++flip_stalls_;
        if (flip_modulo_ && flip_read_ == flip_write_) {
            waiting_for_flip_ = true;
        }
    // NV097 object-taking methods arrive at PGRAPH as resolved PRAMIN instances.
    } else if (method == 0x0184) {
        uint32_t instance = 0;
        if (resolve_object(channel, parameter, instance)) {
            texture_dma_a_ = instance;
            texture_dma_a_bound_ = true;
            raster_state_[kRasterTextureDmaA] = instance;
        }
    } else if (method == 0x0188) {
        uint32_t instance = 0;
        if (resolve_object(channel, parameter, instance)) {
            texture_dma_b_ = instance;
            texture_dma_b_bound_ = true;
            raster_state_[kRasterTextureDmaB] = instance;
        }
    } else if (method == 0x0194) {
        uint32_t instance = 0;
        if (resolve_object(channel, parameter, instance)) {
            color_dma_ = instance;
            color_dma_bound_ = true;
        }
    } else if (method == 0x0198) {
        uint32_t instance = 0;
        if (resolve_object(channel, parameter, instance)) {
            zeta_dma_ = instance;
            zeta_dma_bound_ = true;
        }
    } else if (method == 0x019C) {
        uint32_t instance = 0;
        if (resolve_object(channel, parameter, instance)) {
            vertex_dma_a_ = instance;
            vertex_dma_a_bound_ = true;
        }
    } else if (method == 0x01A0) {
        uint32_t instance = 0;
        if (resolve_object(channel, parameter, instance)) {
            vertex_dma_b_ = instance;
            vertex_dma_b_bound_ = true;
        }
    // NV097 color-surface state and the first software raster operation.
    } else if (method == 0x0200) {
        surface_clip_horizontal_ = parameter;
        raster_state_[kRasterSurfaceClipHorizontal] = parameter;
    } else if (method == 0x0204) {
        surface_clip_vertical_ = parameter;
        raster_state_[kRasterSurfaceClipVertical] = parameter;
    } else if (method == 0x0208) {
        surface_format_ = parameter;
        raster_state_[kRasterSurfaceFormat] = parameter;
    } else if (method == 0x020C) {
        surface_pitch_ = parameter;
        raster_state_[kRasterSurfacePitch] = parameter;
    } else if (method == 0x0210) {
        surface_color_offset_ = parameter;
        raster_state_[kRasterSurfaceColorOffset] = parameter;
    } else if (method == 0x0214) {
        surface_zeta_offset_ = parameter;
        raster_state_[kRasterSurfaceZetaOffset] = parameter;
    } else if (method == 0x1D8C) {
        zeta_clear_value_ = parameter;
    } else if (method == 0x1D90) {
        color_clear_value_ = parameter;
    } else if (method == 0x1D98) {
        clear_rect_horizontal_ = parameter;
    } else if (method == 0x1D9C) {
        clear_rect_vertical_ = parameter;
    } else if (method == 0x1D94) {
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
    // Raster state is sampled at each indexed batch. Enable bits are alpha,
    // blend, cull, depth, dither, stencil, logic, point-params/smooth,
    // line/poly smooth, and point/line/fill polygon offset respectively.
    } else if (method >= 0x0260 && method < 0x0280) {
        raster_state_[kRasterCombinerAlphaIcw0 +
                      (method - 0x0260) / 4] = parameter;
    } else if (method == 0x0288) {
        raster_state_[kRasterCombinerSpecularFog0] = parameter;
    } else if (method == 0x028C) {
        raster_state_[kRasterCombinerSpecularFog1] = parameter;
    } else if (method == 0x0290) {
        raster_state_[kRasterControl0] = parameter;
    } else if (method == 0x02C0) {
        raster_state_[kRasterWindowClipHorizontal] = parameter;
    } else if (method == 0x02E0) {
        raster_state_[kRasterWindowClipVertical] = parameter;
    } else if (method == 0x0300) {
        raster_state_[kRasterEnables] =
            (raster_state_[kRasterEnables] & ~(1u << 0)) |
            ((parameter != 0) << 0);
    } else if (method == 0x0304) {
        raster_state_[kRasterEnables] =
            (raster_state_[kRasterEnables] & ~(1u << 1)) |
            ((parameter != 0) << 1);
    } else if (method == 0x0308) {
        raster_state_[kRasterEnables] =
            (raster_state_[kRasterEnables] & ~(1u << 2)) |
            ((parameter != 0) << 2);
    } else if (method == 0x030C) {
        raster_state_[kRasterEnables] =
            (raster_state_[kRasterEnables] & ~(1u << 3)) |
            ((parameter != 0) << 3);
    } else if (method == 0x0310) {
        raster_state_[kRasterEnables] =
            (raster_state_[kRasterEnables] & ~(1u << 4)) |
            ((parameter != 0) << 4);
    } else if (method == 0x0318) {
        raster_state_[kRasterEnables] =
            (raster_state_[kRasterEnables] & ~(1u << 7)) |
            ((parameter != 0) << 7);
    } else if (method == 0x031C) {
        raster_state_[kRasterEnables] =
            (raster_state_[kRasterEnables] & ~(1u << 8)) |
            ((parameter != 0) << 8);
    } else if (method == 0x0320) {
        raster_state_[kRasterEnables] =
            (raster_state_[kRasterEnables] & ~(1u << 9)) |
            ((parameter != 0) << 9);
    } else if (method == 0x0324) {
        raster_state_[kRasterEnables] =
            (raster_state_[kRasterEnables] & ~(1u << 10)) |
            ((parameter != 0) << 10);
    } else if (method == 0x032C) {
        raster_state_[kRasterEnables] =
            (raster_state_[kRasterEnables] & ~(1u << 5)) |
            ((parameter != 0) << 5);
    } else if (method == 0x0330) {
        raster_state_[kRasterEnables] =
            (raster_state_[kRasterEnables] & ~(1u << 11)) |
            ((parameter != 0) << 11);
    } else if (method == 0x0334) {
        raster_state_[kRasterEnables] =
            (raster_state_[kRasterEnables] & ~(1u << 12)) |
            ((parameter != 0) << 12);
    } else if (method == 0x0338) {
        raster_state_[kRasterEnables] =
            (raster_state_[kRasterEnables] & ~(1u << 13)) |
            ((parameter != 0) << 13);
    } else if (method == 0x033C) {
        raster_state_[kRasterAlphaFunc] = parameter;
    } else if (method == 0x0340) {
        raster_state_[kRasterAlphaRef] = parameter;
    } else if (method == 0x0344) {
        raster_state_[kRasterBlendSrc] = parameter;
    } else if (method == 0x0348) {
        raster_state_[kRasterBlendDst] = parameter;
    } else if (method == 0x034C) {
        raster_state_[kRasterBlendColor] = parameter;
    } else if (method == 0x0350) {
        raster_state_[kRasterBlendEquation] = parameter;
    } else if (method == 0x0354) {
        raster_state_[kRasterDepthFunc] = parameter;
    } else if (method == 0x0358) {
        raster_state_[kRasterColorMask] = parameter;
    } else if (method == 0x035C) {
        raster_state_[kRasterDepthMask] = parameter;
    } else if (method == 0x0360) {
        raster_state_[kRasterStencilWriteMask] = parameter;
    } else if (method == 0x0364) {
        raster_state_[kRasterStencilFunc] = parameter;
    } else if (method == 0x0368) {
        raster_state_[kRasterStencilRef] = parameter;
    } else if (method == 0x036C) {
        raster_state_[kRasterStencilFuncMask] = parameter;
    } else if (method == 0x0370) {
        raster_state_[kRasterStencilOpFail] = parameter;
    } else if (method == 0x0374) {
        raster_state_[kRasterStencilOpZFail] = parameter;
    } else if (method == 0x0378) {
        raster_state_[kRasterStencilOpZPass] = parameter;
    } else if (method == 0x037C) {
        raster_state_[kRasterShadeMode] = parameter;
    } else if (method == 0x0384) {
        raster_state_[kRasterPolygonOffsetScale] = parameter;
    } else if (method == 0x0388) {
        raster_state_[kRasterPolygonOffsetBias] = parameter;
    } else if (method == 0x038C) {
        raster_state_[kRasterFrontPolygonMode] = parameter;
    } else if (method == 0x0390) {
        raster_state_[kRasterBackPolygonMode] = parameter;
    } else if (method == 0x0394) {
        raster_state_[kRasterClipMin] = parameter;
    } else if (method == 0x0398) {
        raster_state_[kRasterClipMax] = parameter;
    } else if (method == 0x039C) {
        raster_state_[kRasterCullFace] = parameter;
    } else if (method == 0x03A0) {
        raster_state_[kRasterFrontFace] = parameter;
    } else if (method == 0x17BC) {
        raster_state_[kRasterEnables] =
            (raster_state_[kRasterEnables] & ~(1u << 6)) |
            ((parameter != 0) << 6);
    } else if (method == 0x17C0) {
        raster_state_[kRasterLogicOp] = parameter;
    } else if (method == 0x1D78) {
        raster_state_[kRasterZMinMaxControl] = parameter;
    } else if (method == 0x1E20) {
        raster_state_[kRasterCombinerFinalFactor0] = parameter;
    } else if (method == 0x1E24) {
        raster_state_[kRasterCombinerFinalFactor1] = parameter;
    } else if (method >= 0x1E40 && method < 0x1E60) {
        raster_state_[kRasterCombinerColorOcw0 +
                      (method - 0x1E40) / 4] = parameter;
    } else if (method == 0x1E60) {
        raster_state_[kRasterCombinerControl] = parameter;
    } else if (method == 0x1E6C) {
        raster_state_[kRasterShadowDepthFunc] = parameter;
    } else if (method == 0x1E70) {
        raster_state_[kRasterShaderStageProgram] = parameter;
    } else if (method == 0x1E74) {
        raster_state_[kRasterDotRgbMapping] = parameter;
    } else if (method == 0x1E78) {
        raster_state_[kRasterShaderOtherStageInput] = parameter;
    } else if (method >= 0x1B00 && method < 0x1C00) {
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
    // Fixed-function vertex-state selectors. Their batch census gates which xemu equations
    // must execute before fixed-function output can be admitted to rasterization.
    } else if (method == 0x0294) {
        fixed_light_control_ =
            static_cast<uint8_t>((parameter & 1u) |
                                 ((parameter >> 15) & 2u) |
                                 ((parameter >> 15) & 4u));
    } else if (method == 0x0298) {
        fixed_color_material_ = static_cast<uint8_t>(parameter);
    } else if (method == 0x02A4) {
        fixed_fog_ = parameter != 0;
    } else if (method == 0x0314) {
        fixed_lighting_ = parameter != 0;
    } else if (method == 0x0328) {
        if (parameter > 6) {
            fault_("NV2A fixed-function skin mode is invalid");
            return;
        }
        fixed_skin_mode_ = static_cast<uint8_t>(parameter);
    } else if (method == 0x03A4) {
        fixed_normalization_ = parameter != 0;
    } else if (method >= 0x03A8 && method < 0x03B4) {
        const uint32_t component = (method - 0x03A8) / 4;
        std::memcpy(&fixed_material_emission_[component],
                    &parameter, sizeof(parameter));
    } else if (method == 0x03B4) {
        std::memcpy(&fixed_material_alpha_, &parameter, sizeof(parameter));
    } else if (method == 0x03B8) {
        fixed_specular_ = parameter != 0;
    } else if (method == 0x03BC) {
        fixed_light_enable_mask_ = static_cast<uint16_t>(parameter);
    } else if (method >= 0x03C0 && method < 0x0400) {
        const uint32_t stage = (method - 0x03C0) / 0x10;
        const uint32_t component = ((method - 0x03C0) & 0xFu) / 4;
        fixed_texgen_[stage][component] = parameter;
    } else if (method >= 0x0420 && method < 0x0430) {
        fixed_texture_matrix_enable_[(method - 0x0420) / 4] = parameter != 0;
    // Fixed-function matrix uploads share Cheops constant memory. Matrices are
    // interleaved exactly as the NV2A XF context lays them out.
    } else if (method >= 0x0440 && method < 0x0480) {
        const uint32_t slot = (method - 0x0440) / 4;
        std::memcpy(&transform_constants_[0x04 + slot / 4][slot & 3u],
                    &parameter, sizeof(parameter));
    } else if (method >= 0x0480 && method < 0x0580) {
        const uint32_t slot = (method - 0x0480) / 4;
        const uint32_t matrix = slot / 16;
        const uint32_t entry = slot & 15u;
        std::memcpy(&transform_constants_[0x08 + matrix * 8 + entry / 4]
                                         [entry & 3u],
                    &parameter, sizeof(parameter));
    } else if (method >= 0x0580 && method < 0x0680) {
        const uint32_t slot = (method - 0x0580) / 4;
        const uint32_t matrix = slot / 16;
        const uint32_t entry = slot & 15u;
        std::memcpy(&transform_constants_[0x0C + matrix * 8 + entry / 4]
                                         [entry & 3u],
                    &parameter, sizeof(parameter));
    } else if (method >= 0x0680 && method < 0x06C0) {
        const uint32_t slot = (method - 0x0680) / 4;
        std::memcpy(&transform_constants_[slot / 4][slot & 3u],
                    &parameter, sizeof(parameter));
    } else if (method >= 0x06C0 && method < 0x07C0) {
        const uint32_t slot = (method - 0x06C0) / 4;
        const uint32_t texture = slot / 16;
        const uint32_t entry = slot & 15u;
        std::memcpy(&transform_constants_[0x44 + texture * 8 + entry / 4]
                                         [entry & 3u],
                    &parameter, sizeof(parameter));
    } else if (method >= 0x0840 && method < 0x0940) {
        const uint32_t slot = (method - 0x0840) / 4;
        const uint32_t texture = slot / 16;
        const uint32_t entry = slot & 15u;
        std::memcpy(&transform_constants_[0x40 + texture * 8 + entry / 4]
                                         [entry & 3u],
                    &parameter, sizeof(parameter));
    } else if (method >= 0x0A10 && method < 0x0A1C) {
        const uint32_t component = (method - 0x0A10) / 4;
        std::memcpy(&fixed_scene_ambient_[component],
                    &parameter, sizeof(parameter));
    // Kelvin viewport and Cheops programmable-transform state. Program/constant upload
    // pointers advance after each complete four-word vector, matching xemu's PGRAPH model.
    } else if (method >= 0x0A20 && method < 0x0A30) {
        const uint32_t slot = (method - 0x0A20) / 4;
        std::memcpy(&viewport_offset_[slot], &parameter, sizeof(parameter));
        transform_constants_[0x3B][slot] = viewport_offset_[slot];
    } else if (method >= 0x0A60 && method < 0x0A80) {
        raster_state_[kRasterCombinerFactor0_0 +
                      (method - 0x0A60) / 4] = parameter;
    } else if (method >= 0x0A80 && method < 0x0AA0) {
        raster_state_[kRasterCombinerFactor1_0 +
                      (method - 0x0A80) / 4] = parameter;
    } else if (method >= 0x0AA0 && method < 0x0AC0) {
        raster_state_[kRasterCombinerAlphaOcw0 +
                      (method - 0x0AA0) / 4] = parameter;
    } else if (method >= 0x0AC0 && method < 0x0AE0) {
        raster_state_[kRasterCombinerColorIcw0 +
                      (method - 0x0AC0) / 4] = parameter;
    } else if (method >= 0x0AF0 && method < 0x0B00) {
        const uint32_t slot = (method - 0x0AF0) / 4;
        std::memcpy(&viewport_scale_[slot], &parameter, sizeof(parameter));
        transform_constants_[0x3A][slot] = viewport_scale_[slot];
    } else if (method >= 0x0B00 && method < 0x0B80) {
        const uint32_t slot = (method - 0x0B00) / 4;
        if (transform_program_load_ >= transform_program_.size()) {
            fault_("NV2A transform program upload exceeds instruction memory");
            return;
        }
        transform_program_[transform_program_load_][slot & 3u] = parameter;
        ++transform_program_words_;
        if ((slot & 3u) == 3) ++transform_program_load_;
    } else if (method >= 0x0B80 && method < 0x0C00) {
        const uint32_t slot = (method - 0x0B80) / 4;
        if (transform_constant_load_ >= transform_constants_.size()) {
            fault_("NV2A transform constant upload exceeds constant memory");
            return;
        }
        std::memcpy(&transform_constants_[transform_constant_load_][slot & 3u],
                    &parameter, sizeof(parameter));
        ++transform_constant_words_;
        if ((slot & 3u) == 3) ++transform_constant_load_;
    } else if (method == 0x1E94) {
        transform_execution_mode_ = static_cast<uint8_t>(parameter & 3u);
    } else if (method == 0x1E9C) {
        if (parameter >= transform_program_.size()) {
            fault_("NV2A transform program load pointer is out of range");
            return;
        }
        transform_program_load_ = static_cast<uint8_t>(parameter);
    } else if (method == 0x1EA0) {
        if (parameter >= transform_program_.size()) {
            fault_("NV2A transform program start is out of range");
            return;
        }
        transform_program_start_ = static_cast<uint8_t>(parameter);
    } else if (method == 0x1EA4) {
        if (parameter >= transform_constants_.size()) {
            fault_("NV2A transform constant load pointer is out of range");
            return;
        }
        transform_constant_load_ = static_cast<uint8_t>(parameter);
    // The dashboard's immediate quad path uses generic texture/diffuse attributes and the
    // legacy position stream.  These follow xemu's NV097 handlers: attributes persist until
    // changed, while a completed write to attribute slot 0 (position) snapshots one full
    // 16-slot vertex.  This covers the legacy SET_VERTEX4F position stream plus every
    // SET_VERTEX_DATA* family (2F_M, 2S, 4UB, 4S_M, 4F_M); each commits on the write that
    // finishes slot 0, matching hardware semantics where position completion latches a vertex.
    } else if (method >= 0x1518 && method < 0x1528) {
        const uint32_t component = (method - 0x1518) / 4;
        std::memcpy(&vertex_attributes_[0].value[component], &parameter, sizeof(parameter));
        ++immediate_methods_;
        if (component == 3 && !commit_immediate_vertex()) return;
    } else if (method >= 0x1880 && method < 0x1900) {
        const uint32_t word = (method - 0x1880) / 4;
        const uint32_t slot = word / 2;
        const uint32_t component = word & 1u;
        auto& value = vertex_attributes_[slot].value;
        std::memcpy(&value[component], &parameter, sizeof(parameter));
        value[2] = 0.0f;
        value[3] = 1.0f;
        ++immediate_methods_;
        if (slot == 0 && component == 1 && !commit_immediate_vertex()) return;
    } else if (method >= 0x1900 && method < 0x1940) {
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
    } else if (method >= 0x1940 && method < 0x1980) {
        const uint32_t slot = (method - 0x1940) / 4;
        auto& value = vertex_attributes_[slot].value;
        value[0] = static_cast<float>(parameter & 0xFFu) / 255.0f;
        value[1] = static_cast<float>((parameter >> 8) & 0xFFu) / 255.0f;
        value[2] = static_cast<float>((parameter >> 16) & 0xFFu) / 255.0f;
        value[3] = static_cast<float>((parameter >> 24) & 0xFFu) / 255.0f;
        ++immediate_methods_;
        if (slot == 0 && !commit_immediate_vertex()) return;
    } else if (method >= 0x1980 && method < 0x1A00) {
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
    } else if (method >= 0x1A00 && method < 0x1B00) {
        const uint32_t word = (method - 0x1A00) / 4;
        const uint32_t slot = word / 4;
        const uint32_t component = word & 3u;
        std::memcpy(&vertex_attributes_[slot].value[component], &parameter, sizeof(parameter));
        ++immediate_methods_;
        if (slot == 0 && component == 3 && !commit_immediate_vertex()) return;
    // Kelvin array-fed vertex submission. ARRAY_ELEMENT16 is non-incrementing and packs two
    // indices per parameter; the pusher has already expanded non-incrementing method runs.
    } else if (method >= 0x1720 && method < 0x1760) {
        const uint32_t slot = (method - 0x1720) / 4;
        vertex_attributes_[slot].dma_b = (parameter & 0x80000000u) != 0;
        vertex_attributes_[slot].offset = parameter & 0x7FFFFFFFu;
    } else if (method >= 0x1760 && method < 0x17A0) {
        set_vertex_array_format((method - 0x1760) / 4, parameter);
    } else if (method == 0x17FC) {
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
    } else if (method == 0x1800) {
        if (vertex_indices_.size() > (1u << 24) - 2) {
            fault_("NV2A indexed vertex batch exceeds safety bound");
            return;
        }
        vertex_indices_.push_back(parameter & 0xFFFFu);
        vertex_indices_.push_back(parameter >> 16);
    } else if (method == 0x1808) {
        if (vertex_indices_.size() >= (1u << 24)) {
            fault_("NV2A indexed vertex batch exceeds safety bound");
            return;
        }
        vertex_indices_.push_back(parameter);
    } else if (method == 0x1810) {
        const uint32_t start = parameter & 0x00FFFFFFu;
        const uint32_t count = (parameter >> 24) + 1u;
        if (vertex_indices_.size() > (1u << 24) - count) {
            fault_("NV2A draw-array batch exceeds safety bound");
            return;
        }
        for (uint32_t i = 0; i < count; ++i)
            vertex_indices_.push_back(start + i);
    } else if (method == 0x1818) {
        if (primitive_mode_ == 0) {
            fault_("NV2A inline-array data submitted outside BEGIN/END");
            return;
        }
        if (inline_array_words_.size() >= (1u << 24)) {
            fault_("NV2A inline-array payload exceeds safety bound");
            return;
        }
        inline_array_words_.push_back(parameter);
    // NV097 semaphore methods. AvSetDisplayMode uses RELEASE as a GPU completion fence.
    } else if (method == 0x01A4) {
        uint32_t instance = 0;
        if (resolve_object(channel, parameter, instance))
            semaphore_dma_[subchannel] = instance;
    } else if (method == 0x1D6C) {
        semaphore_offset_[subchannel] = parameter;
    } else if (method == 0x1D70) {
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
    }
}

void Nv2a::resume_pushbuffers() {
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
    while (!waiting_for_flip_ && !waiting_for_nop_ && pgraph_fifo_access_ &&
           !pull_queue_.empty()) {
        PushMethod push = pull_queue_.front();
        pull_queue_.pop_front();
        perf::record_pfifo_queue_pop();
        dispatch_method(push);
    }
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
    perf::ScopedAdd perf_t(perf::g_ns_nv2a_pushbuffer, perf::g_calls_nv2a_pushbuffer);
    auto& ch = channels_[channel];
    // CACHE1 is finite hardware storage, not an unbounded host command queue. The
    // pusher may prefetch METHOD/DATA entries while PGRAPH is blocked, but stops at
    // the 256-entry high-water mark and leaves later commands behind DMA_GET.
    if (!mem_read_) {
        fault_("NV2A PFIFO has no unified-memory read callback");
        return;
    }

    constexpr uint32_t kMaxWordsPerKick = 4u * 1024u * 1024u;
    uint32_t words = 0;
    while (ch.get != ch.put && pull_queue_.size() < kPullQueueCapacity &&
           words++ < kMaxWordsPerKick) {
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
        if (!mem_read_(fetch, &word, sizeof(word))) {
            char why[128];
            std::snprintf(why, sizeof(why), "NV2A PFIFO read failed at 0x%08x", fetch);
            fault_(why);
            return;
        }
        ch.get += 4;

        if (ch.method_count) {
            pull_queue_.push_back(PushMethod{
                0, channel, ch.subchannel, 0,
                static_cast<uint16_t>(ch.method), word, ch.get, ch.put});
            perf::record_pfifo_queue_push();
            if (!ch.non_incrementing) ch.method += 4;
            --ch.method_count;
            drain_pull_queue();
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
    return info;
}

bool Nv2a::capture_scanout(std::vector<uint8_t>& rgba, ScanoutInfo& info,
                           std::string& error) {
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
    const uint64_t source_size =
        static_cast<uint64_t>(info.pitch) * (info.height - 1) + row_bytes;
    if (static_cast<uint64_t>(info.start) + source_size > 64ull * 1024 * 1024) {
        error = "NV2A scanout exceeds unified RAM";
        return false;
    }
    if (!resolve_renderer_color_overlap(info.start, static_cast<size_t>(source_size),
                                        "scanout capture")) {
        error = "NV2A scanout resident color resolve failed";
        return false;
    }

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
    if (frame_no > frame_no_) {
        frame_no_ = frame_no;
        vblanks_++;
        pcrtc_intr_ |= 1u;  // NV_PCRTC_INTR_0_VBLANK latches every frame boundary
        update_irq();
    }
}

}  // namespace xlle
