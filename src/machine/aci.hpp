// xlle — MCPX ACI (AC'97 audio codec interface) MMIO shell at 0xFEC00000 (4 KB, PCI 00:06.0
// BAR2). Layout from xemu/hw/xbox/mcpx/aci.c: the NAM (native audio mixer) register file
// occupies +0x000..0x0FF and the NABM (native audio bus master) block +0x100..0x17F, both
// memory-mapped aliases of qemu's ICH AC97 model (xemu hw/audio/ac97.c, whose semantics and
// reset values are harvested here — SigmaTel STAC9700 codec identity, GLOB_STA always
// reporting the primary codec ready, per-stream CIV/LVI/SR/PICB/PIV/CR/BDBAR).
//
// No audio engine runs behind it (same deliberate A4 scope as the APU shell): starting a
// stream (CR_RPBM) is accepted and logged; the buffer-descriptor machinery is modelled only
// at the register level. xemu boots this same retail kernel through this exact register
// surface, so it is a boot-correct witness.
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "machine/mmio_device.hpp"

namespace xlle {

class McpxAci : public MmioDevice {
public:
    static constexpr uint32_t kBase = 0xFEC00000u;
    static constexpr uint32_t kSize = 0x1000u;

    explicit McpxAci(FaultFn fault) : fault_(std::move(fault)) { reset(); }

    const char* name() const override { return "mcpx-aci"; }
    uint32_t mmio_base() const override { return kBase; }
    uint32_t mmio_size() const override { return kSize; }

    uint32_t mmio_read(uint32_t addr, int size) override {
        uint32_t off = addr - kBase;
        if (off < 0x100) return nam_read(off, size);
        if (off < 0x180) return nabm_read(off - 0x100, size);
        log_unknown("rd", off);
        return 0;
    }
    void mmio_write(uint32_t addr, int size, uint32_t value) override {
        uint32_t off = addr - kBase;
        if (off < 0x100) { nam_write(off, size, value); return; }
        if (off < 0x180) { nabm_write(off - 0x100, size, value); return; }
        log_unknown("wr", off);
    }

private:
    // --- AC97 constants (qemu ac97.c) ---------------------------------------------------------
    enum { SR_FIFOE = 16, SR_BCIS = 8, SR_LVBCI = 4, SR_CELV = 2, SR_DCH = 1 };
    static constexpr uint32_t kSrValidMask = 0x1F;
    static constexpr uint32_t kSrWclearMask = SR_FIFOE | SR_BCIS | SR_LVBCI;
    static constexpr uint32_t kSrRoMask = SR_DCH | SR_CELV;
    enum { CR_IOCE = 16, CR_FEIE = 8, CR_LVBIE = 4, CR_RR = 2, CR_RPBM = 1 };
    static constexpr uint32_t kCrValidMask = 0x1F;
    static constexpr uint32_t kGcValidMask = 0x3F;
    static constexpr uint32_t kGcWr = 4, kGcCr = 2;
    static constexpr uint32_t kGsS0cr = 1u << 8;  // primary codec ready (always, qemu)
    static constexpr uint32_t kGsWclearMask = (1u << 15) | (1u << 11) | (1u << 10) | 1u;
    static constexpr uint32_t kGsRoMask = 0x7F7C;  // GS_RO_MASK (qemu)
    static constexpr uint32_t kGsValidMask = (1u << 18) - 1;

    struct BmRegs {
        uint32_t bdbar = 0;
        uint8_t civ = 0, lvi = 0, piv = 0, cr = 0;
        uint16_t sr = SR_DCH, picb = 0;
    };

    void reset() {
        std::memset(mixer_, 0, sizeof(mixer_));
        mixer_store(0x26, 0x000f);  // Powerdown_Ctrl_Stat
        mixer_store(0x7c, 0x8384);  // Vendor ID1 (SigmaTel)
        mixer_store(0x7e, 0x7600);  // Vendor ID2 (STAC9700)
        mixer_store(0x28, 0x0809);  // Extended Audio ID
        mixer_store(0x2a, 0x0009);  // Extended Audio Ctrl/Stat
        mixer_store(0x2c, 0xbb80);  // PCM Front DAC rate (48 kHz)
        mixer_store(0x2e, 0xbb80);
        mixer_store(0x30, 0xbb80);
        mixer_store(0x32, 0xbb80);
        mixer_store(0x34, 0xbb80);  // MIC ADC rate
        mixer_store(0x02, 0x8000);  // Master volume: muted (qemu set_volume)
        mixer_store(0x18, 0x8808);  // PCM out volume
        mixer_store(0x1c, 0x8808);  // Record gain
        for (auto& r : bm_) r = BmRegs{};
        glob_cnt_ = 0;
        glob_sta_ = 0;
        cas_ = 0;
    }

    void mixer_store(uint32_t idx, uint16_t v) {
        mixer_[idx & 0xFE] = static_cast<uint8_t>(v);
        mixer_[(idx & 0xFE) + 1] = static_cast<uint8_t>(v >> 8);
    }
    uint16_t mixer_load(uint32_t idx) const {
        return static_cast<uint16_t>(mixer_[idx & 0xFE] | (mixer_[(idx & 0xFE) + 1] << 8));
    }

    uint32_t nam_read(uint32_t off, int size) {
        cas_ = 0;
        if (size == 2) return mixer_load(off);
        return 0xFFFFFFFFu >> (32 - 8 * size);  // qemu: non-word NAM access reads all-ones
    }

    void nam_write(uint32_t off, int size, uint32_t val) {
        cas_ = 0;
        if (size != 2) return;  // qemu ignores non-word NAM writes
        switch (off) {
            case 0x00:  // AC97_Reset: any write resets the mixer
                reset();
                break;
            case 0x26:  // Powerdown: low nibble is read-only status
                mixer_store(off, static_cast<uint16_t>((val & ~0x800fu) |
                                                       (mixer_load(off) & 0xf)));
                break;
            default:
                mixer_store(off, static_cast<uint16_t>(val));
                break;
        }
    }

    BmRegs& bm(uint32_t off) { return bm_[(off >> 4) & 7]; }

    uint32_t nabm_read(uint32_t off, int size) {
        switch (off) {
            case 0x2C: return glob_cnt_;
            case 0x30: return glob_sta_ | kGsS0cr;
            case 0x34: { uint32_t v = cas_; cas_ = 1; return v; }
            default: break;
        }
        BmRegs& r = bm(off);
        switch (off & 0xF) {
            case 0x0: return r.bdbar;
            case 0x4:
                if (size == 4)
                    return r.civ | (static_cast<uint32_t>(r.lvi) << 8) |
                           (static_cast<uint32_t>(r.sr) << 16);
                return r.civ;
            case 0x5: return r.lvi;
            case 0x6: return size == 1 ? (r.sr & 0xff) : r.sr;
            case 0x8:
                if (size == 4)
                    return r.picb | (static_cast<uint32_t>(r.piv) << 16) |
                           (static_cast<uint32_t>(r.cr) << 24);
                return r.picb;
            case 0xA: return r.piv;
            case 0xB: return r.cr;
            default:
                log_unknown("nabm rd", off + 0x100);
                return 0;
        }
    }

    void nabm_write(uint32_t off, int size, uint32_t val) {
        switch (off) {
            case 0x2C:  // GLOB_CNT: warm/cold reset requests read back as complete (qemu)
                if (!(val & (kGcWr | kGcCr))) glob_cnt_ = val & kGcValidMask;
                return;
            case 0x30:  // GLOB_STA: write-1-clear + writable non-RO bits
                glob_sta_ &= ~(val & kGsWclearMask);
                glob_sta_ |= (val & ~(kGsWclearMask | kGsRoMask)) & kGsValidMask;
                return;
            default: break;
        }
        BmRegs& r = bm(off);
        switch (off & 0xF) {
            case 0x0:  // BDBAR
                r.bdbar = val & ~3u;
                return;
            case 0x5:  // LVI
                if ((r.cr & CR_RPBM) && (r.sr & SR_DCH)) {
                    r.sr &= ~(SR_DCH | SR_CELV);
                    r.civ = r.piv;
                    r.piv = (r.piv + 1) % 32;
                }
                r.lvi = val % 32;
                return;
            case 0x6:  // SR: write-1-clear
                r.sr = static_cast<uint16_t>((r.sr | (val & ~(kSrRoMask | kSrWclearMask))) &
                                             ~(val & kSrWclearMask) & kSrValidMask) |
                       (r.sr & kSrRoMask);
                return;
            case 0xB:  // CR
                if (val & CR_RR) {
                    r = BmRegs{};  // reset_bm_regs
                } else {
                    r.cr = static_cast<uint8_t>(val & kCrValidMask);
                    if ((r.cr & CR_RPBM) && run_logged_ < 8) {
                        std::printf("[aci] stream %u RUN (bdbar=0x%08x lvi=%u) — no audio "
                                    "engine behind the shell\n",
                                    (off >> 4) & 7, r.bdbar, r.lvi);
                        ++run_logged_;
                    }
                }
                return;
            default:
                log_unknown("nabm wr", off + 0x100);
                return;
        }
    }

    void log_unknown(const char* what, uint32_t off) {
        if (unk_logged_ < 16) {
            std::printf("[aci] %s off=0x%03x — outside modeled NAM/NABM, open (0)\n", what, off);
            if (++unk_logged_ == 16) std::printf("[aci] (log capped)\n");
        }
    }

    FaultFn fault_;
    uint8_t mixer_[0x100] = {0};
    BmRegs bm_[8];
    uint32_t glob_cnt_ = 0, glob_sta_ = 0, cas_ = 0;
    int unk_logged_ = 0, run_logged_ = 0;
};

}  // namespace xlle
