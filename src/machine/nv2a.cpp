// xlle — NV2A MMIO register shell. See nv2a.hpp for the value policy.
#include "machine/nv2a.hpp"

#include <cstdio>
#include <cstring>

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

void Nv2a::dispatch_method(uint8_t channel, uint8_t subchannel, uint32_t method,
                           uint32_t parameter) {
    methods_.push(PushMethod{methods_.total(), channel, subchannel,
                             static_cast<uint16_t>(method), parameter});

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
    // NV097 semaphore methods. AvSetDisplayMode uses RELEASE as a GPU completion fence.
    } else if (method == 0x01A4) {
        uint32_t instance = 0;
        if (!ramht_instance(channel, parameter, instance)) {
            char why[128];
            std::snprintf(why, sizeof(why),
                          "NV2A RAMHT lookup failed: channel=%u handle=0x%08x", channel,
                          parameter);
            fault_(why);
            return;
        }
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
