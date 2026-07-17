// xlle — NV2A MMIO register shell. See nv2a.hpp for the value policy.
#include "machine/nv2a.hpp"

#include <cstdio>

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
            // Consistent with the USER-block pushbuffer sink (commands consumed instantly):
            // both the runout FIFO and CACHE1 always read EMPTY (LOW_MARK, xemu pfifo.c sets
            // it whenever the cache drains). The kernel's wait-for-FIFO-idle loop needs this.
            case 0x400: return 1u << 4;   // NV_PFIFO_RUNOUT_STATUS: empty
            case 0x1214: return 1u << 4;  // NV_PFIFO_CACHE1_STATUS: empty
            default: break;
        }
    } else if (blk->off == 0x100000) {  // PFB
        // Captured from silicon 2026-07-16 (agent read 0xFD100200).
        if (bo == 0x200) return 0x03070003;  // NV_PFB_CFG0
    } else if (blk->off == 0x101000) {  // PSTRAPS
        // Real strap value captured from silicon 2026-07-16 (agent read 0xFD101000). xemu
        // stubs this to 0; the 2BL discards it, but the kernel may consume it — use the truth.
        if (bo == 0x000) return 0x861D4401;
    } else if (blk->off == 0x800000) {  // USER: per-channel PFIFO control (64 KB stride)
        // Pushbuffer SINK (deliberate, documented A4 stand-in — NOT the final model): the
        // kernel submits GPU commands by bumping DMA_PUT (+0x40) and polls DMA_GET (+0x44)
        // until the GPU consumes them. There is no pushbuffer engine yet, so GET reads back
        // as PUT: commands are treated as instantly consumed and are otherwise discarded.
        // The A4 finish line replaces this with a pushbuffer recorder + software rasterizer.
        if ((bo & 0xFFFF) == 0x44) {
            uint32_t put_key = (rel & ~0xFFFFu) | 0x40;
            auto it = user_regs_.find(put_key);
            uint32_t put = it != user_regs_.end() ? it->second : 0;
            if (pb_logged_ < 8) {
                std::printf("[nv2a] USER ch%u DMA_GET read -> tracking DMA_PUT=0x%08x "
                            "(pushbuffer sink)\n", (bo >> 16) & 0x1F, put);
                ++pb_logged_;
            }
            return put;
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
    // USER block: store channel-control writes so DMA_GET can track DMA_PUT (see mmio_read).
    if (blk->off == 0x800000) {
        user_regs_[rel] = value;
        return;
    }
    // No modelled write side effects yet; accept + log (xemu ignores unknown writes).
    if (unmodeled_cb_) unmodeled_cb_(true, addr, bo, blk->name, value);
    (void)size;
}

// NV_PMC_INTR_0 is computed from the engines' pending&enabled states (xemu nv2a.c
// update_irq): PCRTC contributes bit 24. Other engines (PFIFO bit 8, PGRAPH bit 12) join
// when they gain interrupt sources.
uint32_t Nv2a::pmc_intr_status() const {
    uint32_t v = 0;
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
