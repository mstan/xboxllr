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
#include <array>
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
    // Borrowed little-endian PCM16 bytes, in stereo pairs. True accepts the
    // whole chunk; rejection must leave the consumer unchanged. Without an
    // observer the device still fetches and consumes the actual RAM bytes.
    using PcmOutputFn = std::function<bool(const uint8_t*, size_t)>;

    explicit McpxAci(FaultFn fault) : fault_(std::move(fault)) { reset(); }

    const char* name() const override { return "mcpx-aci"; }
    uint32_t mmio_base() const override { return kBase; }
    uint32_t mmio_size() const override { return kSize; }

    void set_mem_access(MemReadFn rd) {
        if (pcm_inflight_) { device_fault("RAM owner changed during PCM consumption"); return; }
        mem_read_ = std::move(rd);
    }
    void set_pcm_output(PcmOutputFn fn) {
        if (pcm_inflight_) { device_fault("PCM consumer changed during consumption"); return; }
        pcm_output_ = std::move(fn);
    }
    void set_irq(IrqFn fn, int irq = 6) {
        irq_fn_ = std::move(fn);
        irq_num_ = irq;
        update_irq();
    }
    void set_frame(uint64_t frame) {
        if (servicing_ || pcm_inflight_) { device_fault("reentrant PCM device work"); return; }
        frame_clock_primed_ = true;
        servicing_ = true;
        while (!faulted_ && frame_ < frame) {
            ++frame_;
            frame_step();
        }
        servicing_ = false;
    }
    uint64_t completed_descriptors() const { return completed_descriptors_; }
    uint64_t consumed_pcm_words() const { return consumed_pcm_words_; }
    uint64_t current_frame() const { return frame_; }
    bool faulted() const { return faulted_; }
    // Prime once before configuration or consumption, without traversing elapsed
    // frames before this codec owner's explicit initialized-kernel handoff.
    bool prime_frame_clock(uint64_t frame) {
        if (faulted_ || !kernel_handoff_ || state_used_ || frame_clock_primed_ ||
            servicing_ || pcm_inflight_) {
            device_fault("codec frame clock requires a fresh stopped kernel handoff");return false;
        }
        frame_ = frame;frame_clock_primed_ = true;return true;
    }

    // Explicit post-kernel handoff, not a cold-reset default. The owned4983
    // kernel releases AC_RESET# before waiting for the physical codec. The
    // stopped console observation is GLOB_CNT=2, GLOB_STA=00300100.
    bool initialize_kernel_audio_handoff() {
        if (faulted_ || kernel_handoff_ || state_used_ || frame_clock_primed_ || frame_ || clock_known_) {
            device_fault("codec handoff requires a fresh stopped owner"); return false;
        }
        kernel_handoff_ = true;
        glob_cnt_ = 2; glob_sta_ = 0x00300100;
        return true;
    }

    // The shared guest clock services AC-link control transactions separately
    // from the existing 256-sample PCM batching. Polling CAS never advances it.
    // Nominal completion is the next48kHz link boundary; exact bus/edge phase
    // remains a replaceable timing policy, not a silicon-cycle claim.
    void set_time(uint64_t ticks, uint64_t hz) {
        if (faulted_) return;
        if (servicing_ || pcm_inflight_ || hz < 48000 || hz > UINT64_MAX/48000 ||
            (clock_known_ && (hz != clock_hz_ || ticks < clock_ticks_))) {
            device_fault("invalid or reentrant codec clock"); return;
        }
        clock_known_ = true; clock_ticks_ = ticks; clock_hz_ = hz;
        link_frame_ = (ticks/hz)*48000 + ((ticks%hz)*48000)/hz;
        if (nam_pending_ && link_frame_ >= nam_complete_frame_) {
            nam_pending_ = false; cas_ = 0;
        }
    }

    uint32_t mmio_read(uint32_t addr, int size) override {
        uint32_t off = addr - kBase;
        if (off < 0x100) return nam_read(off, size);
        if (off < 0x180) return nabm_read(off - 0x100, size);
        log_unknown("rd", off);
        return 0;
    }
    void mmio_write(uint32_t addr, int size, uint32_t value) override {
        if (faulted_) return;
        if (pcm_inflight_) { device_fault("register mutation during PCM consumption"); return; }
        state_used_ = true;
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
        // Wolfson WM9709, production datasheet pp9/13 and physical vendor
        // reads. Ordinary AC97 mixer/rate/power registers are unsupported,
        // read zero and ignore writes. The DAC is fixed48kHz.
        mixer_store(0x7c, 0x574d);
        mixer_store(0x7e, 0x4c09);
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
        if (size != 2 || (off & 1) || off >= 0x80 || !(glob_sta_ & kGsS0cr)) {
            device_fault("unqualified codec read width/address/readiness"); return 0;
        }
        // NAM reads synchronously return the link response and release CAS.
        state_used_ = true; nam_pending_ = false; cas_ = 0;
        return mixer_load(off);
    }
    void nam_write(uint32_t off, int size, uint32_t value) {
        (void)value;
        if (size != 2 || (off & 1) || off >= 0x80 || off == 0x5a ||
            !(glob_sta_ & kGsS0cr) || !clock_known_ || nam_pending_ || link_frame_ == UINT64_MAX) {
            device_fault("unqualified codec write context/width/address"); return;
        }
        // A real write is asynchronous even when the DAC ignores its data.
        // The physical immediate-CAS experiment returned1; the original
        //20us wait then observed completion. Never clear this on a CAS poll.
        cas_ = 1; nam_pending_ = true; nam_complete_frame_ = link_frame_ + 1;
    }

    BmRegs& bm(uint32_t off) { return bm_[(off >> 4) & 7]; }

    uint32_t nabm_read(uint32_t off, int size) {
        switch (off) {
            case 0x2c: return glob_cnt_;
            case 0x30: return glob_sta_;
            case 0x34: {
                state_used_ = true;
                uint32_t value = cas_;
                cas_ = 1;
                return value;
            }
            case 0x7c:
                if (size == 4 && spdif_control_known_) return spdif_control_;
                device_fault("unknown SPDIF control or unsupported width"); return 0;
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
        const bool exact_dword = (off & 0xf) == 0 || off == 0x2c || off == 0x30 || off == 0x7c;
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
                // Active/cold/warm reset needs its own link/reset owner.
                // Do not silently discard the original driver's reset write.
                if (size != 4 || !kernel_handoff_ || value != glob_cnt_)
                    device_fault("AC97 link reset/control transition remains unqualified");
                return;
            case 0x30:
                glob_sta_ &= ~(value & kGsWclearMask);
                glob_sta_ |=
                    (value & ~(kGsWclearMask | kGsRoMask)) & kGsValidMask;
                update_irq();
                return;
            case 0x7c:
                if (size != 4 || (value != 0x02000000 && value != 0x02000002) ||
                    (bm_[7].cr & CR_RPBM) || bm_[7].bd_valid) {
                    device_fault("unqualified SPDIF format/context"); return;
                }
                spdif_control_ = value; spdif_control_known_ = true; return;
            default: break;
        }

        BmRegs& r = bm(off);
        switch (off & 0xf) {
            case 0x4:
                // Reached original startup writes zero after reset on both
                // rings. Active/nonzero CIV rewrites are not established.
                if (value || r.civ || r.bd_valid || (r.cr & CR_RPBM))
                    device_fault("active/nonzero CIV rewrite remains unqualified");
                return;
            case 0x8:  // PICB is read-only.
            case 0x9:
            case 0xa:  // PIV is read-only.
                return;
            case 0x0:
                r.bdbar = value & ~3u;
                return;
            case 0x5:
                r.lvi = value % 32;
                if ((r.cr & CR_RPBM) && (r.sr & SR_CELV) && r.lvi != r.civ) {
                    if (!advance_bd(r)) return;
                    r.sr &= ~(SR_DCH | SR_CELV);
                }
                return;
            case 0x6:
                r.sr = static_cast<uint16_t>(
                    (r.sr | (value & ~(kSrRoMask | kSrWclearMask))) &
                    ~(value & kSrWclearMask) & kSrValidMask);
                update_irq();
                return;
            case 0xb:
                if ((value & CR_RPBM) && !(value & CR_RR) &&
                    (!(glob_sta_ & kGsS0cr) || &r == &bm_[7])) {
                    device_fault("codec not ready or active SPDIF processing unqualified"); return;
                }
                if (value & CR_RR) {
                    const uint8_t enables = r.cr & (CR_IOCE | CR_FEIE | CR_LVBIE);
                    r = BmRegs{};
                    r.cr = enables;
                } else {
                    const bool was_running = (r.cr & CR_RPBM) != 0;
                    r.cr = static_cast<uint8_t>(value & kCrValidMask);
                    if (r.cr & CR_RPBM) {
                        if (!r.bd_valid) {
                            if (!advance_bd(r)) return;
                        }
                        // Pause retains the current descriptor and offset;
                        // changing interrupt enables while RUN stays set also
                        // must not fetch a different buffer.
                        if (!was_running && !(r.sr & SR_CELV)) r.sr &= ~SR_DCH;
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

    bool fetch_bd(BmRegs& r, bool pcm_output) {
        uint32_t bd[2] = {};
        const uint64_t address = uint64_t(r.bdbar) + r.civ * 8;
        if (address + sizeof(bd) > (uint64_t(1) << 32)) {
            device_fault("buffer descriptor read failed");
            return false;
        }
        pcm_inflight_ = true;
        const bool fetched = mem_read_ && mem_read_(uint32_t(address), bd, sizeof(bd));
        pcm_inflight_ = false;
        if (faulted_) return false;
        if (!fetched) { device_fault("buffer descriptor read failed"); return false; }
        if (pcm_output && ((bd[1] & 0x3fff0000u) || (bd[1] & 1u))) {
            device_fault("unqualified PCM descriptor format or odd stereo count");
            return false;
        }
        if (uint64_t(bd[0] & ~3u) + uint16_t(bd[1]) * 2 > (uint64_t(1) << 32)) {
            device_fault("PCM buffer address wraps"); return false;
        }
        r.bd_addr = bd[0] & ~3u;
        r.bd_ctl_len = bd[1];
        r.picb = static_cast<uint16_t>(r.bd_ctl_len);
        r.bd_valid = true;
        return true;
    }

    bool advance_bd(BmRegs& r) {
        BmRegs next = r;
        next.civ = r.piv;
        next.piv = (r.piv + 1) % 32;
        if (!fetch_bd(next, &r == &bm_[1])) return false;
        r = next;
        return true;
    }

    bool consume_pcm(BmRegs& r, uint32_t words) {
        if (!words) return true;
        // WM9709 has no programmable rate register; ordinary NAM writes do
        // not alter this fixed48kHz PCM consumer.
        const uint32_t bytes = words * 2;
        if (uint64_t(r.bd_addr) + bytes > (uint64_t(1) << 32)) {
            device_fault("PCM buffer address wraps"); return false;
        }
        std::array<uint8_t, 1024> pcm{};
        pcm_inflight_ = true;
        const bool fetched = mem_read_ && mem_read_(r.bd_addr, pcm.data(), bytes);
        bool accepted = false;
        if (fetched && !faulted_) accepted = !pcm_output_ || pcm_output_(pcm.data(), bytes);
        pcm_inflight_ = false;
        if (faulted_) return false;
        if (!fetched) { device_fault("PCM buffer read failed"); return false; }
        if (!accepted) { device_fault("PCM consumer rejected bytes"); return false; }
        // No cursor, count, completion or interrupt precedes accepted bytes.
        r.picb = static_cast<uint16_t>(r.picb - words);
        r.bd_addr += bytes;
        consumed_pcm_words_ += words;
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
            if (!consume_pcm(r, consumed)) return;
            words -= consumed;
            if (r.picb) continue;

            uint16_t new_sr = r.sr & ~SR_CELV;
            if (r.bd_ctl_len & kBdIoc) new_sr |= SR_BCIS;
            ++completed_descriptors_;
            const bool last = r.civ == r.lvi;
            if (last) {
                new_sr |= SR_LVBCI | SR_DCH | SR_CELV;
            }
            r.sr = new_sr;
            if (!last && !advance_bd(r)) {
                // A successfully consumed old descriptor still owns its
                // completion, even if fetching the successor fails.
                update_irq(); return;
            }
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
    PcmOutputFn pcm_output_;
    IrqFn irq_fn_;
    uint8_t mixer_[0x100] = {};
    BmRegs bm_[8];
    uint32_t glob_cnt_ = 0, glob_sta_ = 0, cas_ = 0;
    uint32_t spdif_control_ = 0;
    bool kernel_handoff_ = false, state_used_ = false, spdif_control_known_ = false;
    bool clock_known_ = false, nam_pending_ = false;
    uint64_t clock_ticks_ = 0, clock_hz_ = 0, link_frame_ = 0, nam_complete_frame_ = 0;
    int irq_num_ = 6;
    bool irq_level_ = false, faulted_ = false;
    bool servicing_ = false, pcm_inflight_ = false;
    bool frame_clock_primed_ = false;
    uint64_t frame_ = 0, completed_descriptors_ = 0;
    uint64_t consumed_pcm_words_ = 0;
    int unk_logged_ = 0;
};

}  // namespace xlle
