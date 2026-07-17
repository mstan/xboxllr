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
            case 0x100: return 0x00000000;  // PMC_INTR_0: no pending IRQs at cold boot
            case 0x140: return 0x00000000;  // PMC_INTR_EN_0
            default: break;
        }
    } else if (blk->off == 0x001000) {  // PBUS
        switch (bo) {
            case 0x800: return kPciNv0;  // NV_PBUS_PCI_NV_0
            case 0x804: return kPciNv1;  // NV_PBUS_PCI_NV_1
            case 0x808: return kPciNv2;  // NV_PBUS_PCI_NV_2
            default: break;
        }
    } else if (blk->off == 0x100000) {  // PFB
        // Captured from silicon 2026-07-16 (agent read 0xFD100200).
        if (bo == 0x200) return 0x03070003;  // NV_PFB_CFG0
    } else if (blk->off == 0x101000) {  // PSTRAPS
        // Real strap value captured from silicon 2026-07-16 (agent read 0xFD101000). xemu
        // stubs this to 0; the 2BL discards it, but the kernel may consume it — use the truth.
        if (bo == 0x000) return 0x861D4401;
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
    // No modelled write side effects yet; accept + log (xemu ignores unknown writes).
    if (unmodeled_cb_) unmodeled_cb_(true, addr, bo, blk->name, value);
    (void)size;
}

}  // namespace xlle
