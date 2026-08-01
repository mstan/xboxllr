// xlle — NV2A MMIO register shell. See nv2a.hpp for the value policy.
#include "machine/nv2a.hpp"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "nv2a_vsh_emulator.h"

namespace xlle {

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

bool Nv2a::submit_vertex_batch() {
    if (vertex_indices_.empty()) return true;
    if (primitive_mode_ == 0 || primitive_mode_ >= primitive_mode_counts_.size()) {
        fault_("NV2A indexed vertices submitted outside a valid BEGIN/END");
        return false;
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
    transformed_batch_.reserve(vertex_indices_.size());

    for (uint32_t index : vertex_indices_) {
        largest_vertex_index_ = std::max(largest_vertex_index_, index);
        for (uint32_t slot = 0; slot < vertex_attributes_.size(); ++slot) {
            if (!fetch_vertex_attribute(slot, index)) {
                if (program_parsed) nv2a_vsh_program_destroy(&program);
                return false;
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

            std::memcpy(vertex.position.data(), &full_state.output_regs[0], 4 * sizeof(float));
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

    const uint64_t n = vertex_indices_.size();
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
    assembled_primitives_ += primitives;
    assembled_triangles_ += triangles;
    ++primitive_mode_counts_[primitive_mode_];
    return true;
}

void Nv2a::dispatch_method(uint8_t channel, uint8_t subchannel, uint32_t method,
                           uint32_t parameter) {
    methods_.push(PushMethod{methods_.total(), channel, subchannel,
                             static_cast<uint16_t>(method), parameter});
    if (method < 0x2000 && !(method & 3u)) ++method_counts_[method >> 2];

    // PFIFO resolves SET_OBJECT through RAMHT, then PGRAPH caches the object's class for
    // this subchannel. Only Kelvin (class 0x97) methods may mutate the 3D/scanout state.
    if (method == 0) {
        if (bind_object(channel, subchannel, parameter))
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
    } else if (method == 0x0204) {
        surface_clip_vertical_ = parameter;
    } else if (method == 0x0208) {
        surface_format_ = parameter;
    } else if (method == 0x020C) {
        surface_pitch_ = parameter;
    } else if (method == 0x0210) {
        surface_color_offset_ = parameter;
    } else if (method == 0x0214) {
        surface_zeta_offset_ = parameter;
    } else if (method == 0x1D8C) {
        zeta_clear_value_ = parameter;
    } else if (method == 0x1D90) {
        color_clear_value_ = parameter;
    } else if (method == 0x1D98) {
        clear_rect_horizontal_ = parameter;
    } else if (method == 0x1D9C) {
        clear_rect_vertical_ = parameter;
    } else if (method == 0x1D94) {
        clear_surface(parameter);
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
            if (primitive_mode_ != 0) submit_vertex_batch();
            primitive_mode_ = 0;
            vertex_indices_.clear();
        } else if (parameter <= 10 && primitive_mode_ == 0) {
            primitive_mode_ = static_cast<uint8_t>(parameter);
            vertex_indices_.clear();
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
        dispatch_method(m.channel, m.subchannel, m.method, m.parameter);
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
            pull_queue_.push_back(PushMethod{0, channel, ch.subchannel,
                                             static_cast<uint16_t>(ch.method), word});
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
