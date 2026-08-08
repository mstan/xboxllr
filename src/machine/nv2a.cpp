// xlle — NV2A MMIO register shell. See nv2a.hpp for the value policy.
#include "machine/nv2a.hpp"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>

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
          pages_((size + kPageSize - 1) / kPageSize) {}

    bool read16(size_t offset, uint16_t& value) {
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
            // The recorder consumes each kick synchronously, so both the runout FIFO and
            // CACHE1 are empty whenever the CPU can observe them (LOW_MARK in xemu).
            case 0x400: return 1u << 4;   // NV_PFIFO_RUNOUT_STATUS: empty
            case 0x1214: return 1u << 4;  // NV_PFIFO_CACHE1_STATUS: empty
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
                drain_pull_queue();
            }
        }
        return;
    }
    if (blk->off == 0x400000 && bo == 0x100) {
        pgraph_intr_ &= ~value;  // NV_PGRAPH_INTR is write-1-clear
        if (!(pgraph_intr_ & (1u << 20))) waiting_for_nop_ = false;
        update_irq();
        drain_pull_queue();
        return;
    }
    if (blk->off == 0x400000 && bo == 0x140) {
        pgraph_intr_en_ = value;
        update_irq();
        return;
    }
    if (blk->off == 0x400000 && bo == 0x720) {
        pgraph_fifo_access_ = (value & 1u) != 0;
        if (pgraph_fifo_access_) drain_pull_queue();
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
    if (!rasterize_witnessed_batch(raster_executed)) return false;
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

bool Nv2a::rasterize_witnessed_batch(bool& executed) {
    executed = false;
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
        return true;
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
                        auto vertex_diffuse =
                            [component](const TransformedVertex* vertex) {
                                float value = vertex->diffuse[component];
                                if (std::isnan(value)) value = 1.0f;
                                return std::clamp(value, 0.0f, 1.0f);
                            };
                        const double d =
                            (q0 * vertex_diffuse(a) +
                             q1 * vertex_diffuse(b) +
                             q2 * vertex_diffuse(c)) / qsum;
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
    // this subchannel. Only Kelvin (class 0x97) methods may mutate the 3D/scanout state.
    if (method == 0) {
        if (object_bound)
            ++class_method_counts_[object_class_[subchannel]];
        return;
    }
    ++class_method_counts_[object_class_[subchannel]];
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
    // changed, while a completed position write snapshots one full 16-slot vertex.
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
    } else if (method >= 0x1940 && method < 0x1980) {
        const uint32_t slot = (method - 0x1940) / 4;
        auto& value = vertex_attributes_[slot].value;
        value[0] = static_cast<float>(parameter & 0xFFu) / 255.0f;
        value[1] = static_cast<float>((parameter >> 8) & 0xFFu) / 255.0f;
        value[2] = static_cast<float>((parameter >> 16) & 0xFFu) / 255.0f;
        value[3] = static_cast<float>((parameter >> 24) & 0xFFu) / 255.0f;
        ++immediate_methods_;
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

void Nv2a::drain_pull_queue() {
    while (!waiting_for_flip_ && !waiting_for_nop_ && pgraph_fifo_access_ &&
           !pull_queue_.empty()) {
        PushMethod m = pull_queue_.front();
        pull_queue_.pop_front();
        dispatch_method(m);
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
    auto& ch = channels_[channel];
    if (!mem_read_) {
        fault_("NV2A PFIFO has no unified-memory read callback");
        return;
    }

    constexpr uint32_t kMaxWordsPerKick = 4u * 1024u * 1024u;
    uint32_t words = 0;
    while (ch.get != ch.put && words++ < kMaxWordsPerKick) {
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
            pull_queue_.push_back(PushMethod{0, channel, ch.subchannel, 0,
                                             static_cast<uint16_t>(ch.method), word,
                                             ch.get, ch.put});
            if (!ch.non_incrementing) ch.method += 4;
            --ch.method_count;
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
    drain_pull_queue();
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
                           std::string& error) const {
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
