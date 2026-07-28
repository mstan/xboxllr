// xlle — MCPX ACI (AC'97 audio codec interface) at 0xFEC00000
// (4 KB, PCI 00:06.0 BAR2).
//
// Layout and register semantics are harvested from xemu's MCPX ACI wrapper and QEMU AC97
// model. The NAM mixer occupies +0x000..0x0ff and the NABM bus master +0x100..0x17f.
//
// PCM output is headless but functional: it fetches the guest's 32-entry descriptor ring,
// advances CIV/PIV/PICB at the 48 kHz stereo cadence, publishes BCIS/LVBCI/GLOB_STA, and
// drives the routed level IRQ. Samples are consumed without host output. Other streams retain
// register semantics until a guest-visible path needs them.
#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>

#include "machine/mmio_device.hpp"

namespace xlle {

class McpxAci : public MmioDevice {
public:
    static constexpr uint32_t kBase = 0xFEC00000u;
    static constexpr uint32_t kSize = 0x1000u;

    using MemReadFn = std::function<bool(uint32_t, void*, size_t)>;
    using IrqFn = std::function<void(int, bool)>;

    explicit McpxAci(FaultFn fault) : fault_(std::move(fault)) { reset(); }

    const char* name() const override { return "mcpx-aci"; }
    uint32_t mmio_base() const override { return kBase; }
    uint32_t mmio_size() const override { return kSize; }

    void set_mem_access(MemReadFn rd) { mem_read_ = std::move(rd); }
    void set_irq(IrqFn fn, int irq = 6) {
        irq_fn_ = std::move(fn);
        irq_num_ = irq;
        update_irq();
    }
    void set_frame(uint64_t frame) {
        while (frame_ < frame) {
            ++frame_;
            frame_step();
        }
    }
    uint64_t completed_descriptors() const { return completed_descriptors_; }

    uint32_t mmio_read(uint32_t addr, int size) override {
        uint32_t off = addr - kBase;
        if (off < 0x100) return nam_read(off, size);
        if (off < 0x180) return nabm_read(off - 0x100, size);
        log_unknown("rd", off);
        return 0;
    }
    void mmio_write(uint32_t addr, int size, uint32_t value) override {
        uint32_t off = addr - kBase;
        if (off < 0x100) {
            nam_write(off, size, value);
            return;
        }
        if (off < 0x180) {
            nabm_write(off - 0x100, size, value);
            return;
        }
        log_unknown("wr", off);
    }

private:
    enum { SR_FIFOE = 16, SR_BCIS = 8, SR_LVBCI = 4, SR_CELV = 2, SR_DCH = 1 };
    static constexpr uint32_t kSrValidMask = 0x1f;
    static constexpr uint32_t kSrWclearMask = SR_FIFOE | SR_BCIS | SR_LVBCI;
    static constexpr uint32_t kSrRoMask = SR_DCH | SR_CELV;
    enum { CR_IOCE = 16, CR_FEIE = 8, CR_LVBIE = 4, CR_RR = 2, CR_RPBM = 1 };
    static constexpr uint32_t kCrValidMask = 0x1f;
    static constexpr uint32_t kGcValidMask = 0x3f;
    static constexpr uint32_t kGcWr = 4, kGcCr = 2;
    static constexpr uint32_t kGsS0cr = 1u << 8;
    static constexpr uint32_t kGsPiint = 1u << 5, kGsPoint = 1u << 6;
    static constexpr uint32_t kGsMint = 1u << 7, kGsSoint = 1u << 4;
    static constexpr uint32_t kGsWclearMask = (1u << 15) | (1u << 11) | (1u << 10) | 1u;
    static constexpr uint32_t kGsRoMask = 0x7f7c;
    static constexpr uint32_t kGsValidMask = (1u << 18) - 1;
    static constexpr uint32_t kBdIoc = 1u << 31;

    struct BmRegs {
        uint32_t bdbar = 0;
        uint8_t civ = 0, lvi = 0, piv = 0, cr = 0;
        uint16_t sr = SR_DCH, picb = 0;
        uint32_t bd_addr = 0, bd_ctl_len = 0;
        bool bd_valid = false;
    };

    void reset() {
        reset_mixer();
        for (auto& r : bm_) r = BmRegs{};
        glob_cnt_ = glob_sta_ = cas_ = 0;
        update_irq();
    }

    void reset_mixer() {
        std::memset(mixer_, 0, sizeof(mixer_));
        mixer_store(0x26, 0x000f);
        mixer_store(0x7c, 0x8384);
        mixer_store(0x7e, 0x7600);
        mixer_store(0x28, 0x0809);
        mixer_store(0x2a, 0x0009);
        mixer_store(0x2c, 0xbb80);
        mixer_store(0x2e, 0xbb80);
        mixer_store(0x30, 0xbb80);
        mixer_store(0x32, 0xbb80);
        mixer_store(0x34, 0xbb80);
        mixer_store(0x02, 0x8000);
        mixer_store(0x18, 0x8808);
        mixer_store(0x1c, 0x8808);
    }

    void mixer_store(uint32_t idx, uint16_t value) {
        mixer_[idx & 0xfe] = static_cast<uint8_t>(value);
        mixer_[(idx & 0xfe) + 1] = static_cast<uint8_t>(value >> 8);
    }
    uint16_t mixer_load(uint32_t idx) const {
        return static_cast<uint16_t>(mixer_[idx & 0xfe] |
                                     (mixer_[(idx & 0xfe) + 1] << 8));
    }

    uint32_t nam_read(uint32_t off, int size) {
        cas_ = 0;
        if (size == 2) return mixer_load(off);
        return 0xffffffffu >> (32 - 8 * size);
    }
    void nam_write(uint32_t off, int size, uint32_t value) {
        cas_ = 0;
        if (size != 2) return;
        switch (off) {
            case 0x00:
                reset_mixer();
                break;
            case 0x26:
                mixer_store(off, static_cast<uint16_t>((value & ~0x800fu) |
                                                       (mixer_load(off) & 0xf)));
                break;
            default:
                mixer_store(off, static_cast<uint16_t>(value));
                break;
        }
    }

    BmRegs& bm(uint32_t off) { return bm_[(off >> 4) & 7]; }

    uint32_t nabm_read(uint32_t off, int size) {
        switch (off) {
            case 0x2c: return glob_cnt_;
            case 0x30: return glob_sta_ | kGsS0cr;
            case 0x34: {
                uint32_t value = cas_;
                cas_ = 1;
                return value;
            }
            default: break;
        }
        BmRegs& r = bm(off);
        switch (off & 0xf) {
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
            case 0xa: return r.piv;
            case 0xb: return r.cr;
            default:
                log_unknown("nabm rd", off + 0x100);
                return 0;
        }
    }

    void nabm_write(uint32_t off, int size, uint32_t value) {
        const bool exact_dword = (off & 0xf) == 0 || off == 0x2c || off == 0x30;
        if (size == 4 && !exact_dword) {
            nabm_write(off, 2, value & 0xffff);
            nabm_write(off + 2, 2, value >> 16);
            return;
        }
        const bool exact_word = (off & 0xf) == 6;
        if (size == 2 && !exact_word) {
            nabm_write(off, 1, value & 0xff);
            nabm_write(off + 1, 1, value >> 8);
            return;
        }

        switch (off) {
            case 0x2c:
                if (!(value & (kGcWr | kGcCr))) glob_cnt_ = value & kGcValidMask;
                return;
            case 0x30:
                glob_sta_ &= ~(value & kGsWclearMask);
                glob_sta_ |=
                    (value & ~(kGsWclearMask | kGsRoMask)) & kGsValidMask;
                update_irq();
                return;
            default: break;
        }

        BmRegs& r = bm(off);
        switch (off & 0xf) {
            case 0x4:  // CIV is read-only; combined CIV/LVI/SR stores are common.
            case 0x8:  // PICB is read-only.
            case 0x9:
            case 0xa:  // PIV is read-only.
                return;
            case 0x0:
                r.bdbar = value & ~3u;
                return;
            case 0x5:
                if ((r.cr & CR_RPBM) && (r.sr & SR_DCH)) {
                    r.sr &= ~(SR_DCH | SR_CELV);
                    r.civ = r.piv;
                    r.piv = (r.piv + 1) % 32;
                    if (!fetch_bd(r)) return;
                }
                r.lvi = value % 32;
                return;
            case 0x6:
                r.sr = static_cast<uint16_t>(
                    (r.sr | (value & ~(kSrRoMask | kSrWclearMask))) &
                    ~(value & kSrWclearMask) & kSrValidMask);
                update_irq();
                return;
            case 0xb:
                if (value & CR_RR) {
                    const uint8_t enables = r.cr & (CR_IOCE | CR_FEIE | CR_LVBIE);
                    r = BmRegs{};
                    r.cr = enables;
                } else {
                    r.cr = static_cast<uint8_t>(value & kCrValidMask);
                    if (r.cr & CR_RPBM) {
                        r.civ = r.piv;
                        r.piv = (r.piv + 1) % 32;
                        if (!fetch_bd(r)) return;
                        r.sr &= ~SR_DCH;
                    } else {
                        r.sr |= SR_DCH;
                    }
                }
                update_irq();
                return;
            default:
                log_unknown("nabm wr", off + 0x100);
                return;
        }
    }

    bool fetch_bd(BmRegs& r) {
        uint32_t bd[2] = {};
        if (!mem_read_ || !mem_read_(r.bdbar + r.civ * 8, bd, sizeof(bd))) {
            device_fault("buffer descriptor read failed");
            return false;
        }
        r.bd_addr = bd[0] & ~3u;
        r.bd_ctl_len = bd[1];
        r.picb = static_cast<uint16_t>(r.bd_ctl_len);
        r.bd_valid = true;
        return true;
    }

    void update_irq() {
        constexpr uint32_t status_bits[8] = {
            kGsPiint, kGsPoint, kGsMint, 0, 0, 0, 0, kGsSoint
        };
        glob_sta_ &= ~(kGsPiint | kGsPoint | kGsMint | kGsSoint);
        bool level = false;
        for (size_t i = 0; i < 8; ++i) {
            const BmRegs& r = bm_[i];
            const bool active =
                ((r.sr & SR_LVBCI) && (r.cr & CR_LVBIE)) ||
                ((r.sr & SR_BCIS) && (r.cr & CR_IOCE)) ||
                ((r.sr & SR_FIFOE) && (r.cr & CR_FEIE));
            if (active) {
                glob_sta_ |= status_bits[i];
                level = true;
            }
        }
        if (level != irq_level_) {
            irq_level_ = level;
            if (irq_fn_) irq_fn_(irq_num_, level);
        }
    }

    void frame_step() {
        if (faulted_) return;
        // PCM out is 48 kHz stereo signed-16. One 5.333 ms frame consumes 512 words.
        BmRegs& r = bm_[1];
        if (!(r.cr & CR_RPBM) || (r.sr & SR_DCH)) return;
        uint32_t words = 512;
        while (words && !(r.sr & SR_DCH)) {
            const uint32_t consumed = std::min<uint32_t>(words, r.picb);
            r.picb = static_cast<uint16_t>(r.picb - consumed);
            r.bd_addr += consumed * 2;
            words -= consumed;
            if (r.picb) continue;

            uint16_t new_sr = r.sr & ~SR_CELV;
            if (r.bd_ctl_len & kBdIoc) new_sr |= SR_BCIS;
            ++completed_descriptors_;
            if (r.civ == r.lvi) {
                new_sr |= SR_LVBCI | SR_DCH | SR_CELV;
            } else {
                r.civ = r.piv;
                r.piv = (r.piv + 1) % 32;
                if (!fetch_bd(r)) return;
            }
            r.sr = new_sr;
            update_irq();
        }
    }

    void log_unknown(const char* what, uint32_t off) {
        if (unk_logged_ < 16) {
            std::printf("[aci] %s off=0x%03x — outside modeled NAM/NABM, open (0)\n",
                        what, off);
            if (++unk_logged_ == 16) std::printf("[aci] (log capped)\n");
        }
    }
    void device_fault(const std::string& why) {
        if (!faulted_) {
            faulted_ = true;
            fault_(std::string("mcpx-aci: ") + why);
        }
    }

    FaultFn fault_;
    MemReadFn mem_read_;
    IrqFn irq_fn_;
    uint8_t mixer_[0x100] = {};
    BmRegs bm_[8];
    uint32_t glob_cnt_ = 0, glob_sta_ = 0, cas_ = 0;
    int irq_num_ = 6;
    bool irq_level_ = false, faulted_ = false;
    uint64_t frame_ = 0, completed_descriptors_ = 0;
    int unk_logged_ = 0;
};

}  // namespace xlle
