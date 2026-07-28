// xlle — MCPX APU (audio processing unit) at 0xFE800000 (512 KB, PCI 00:05.0 BAR0).
//
// Harvested from xemu/hw/xbox/mcpx/apu. The top level is a RAM-backed register file with
// special handling for interrupt status, FEMEMDATA, and XGSCNT. The first load-bearing VP
// method is implemented end-to-end: VOICE_OFF clears ACTIVE in the guest-resident voice
// structure and posts its notifier; the 5.333 ms APU frame walker later encounters that
// inactive voice in the 2D/3D/MP list and emits SE2FE_IDLE_VOICE. With FETFORCE1 enabled,
// that publishes the front-end trap and drives the APU's level IRQ according to IEN/ISTS.
//
// Other VP/GP/EP subregion accesses remain logged so the next load-bearing method is
// characterized rather than silently treated as implemented. No audio is produced yet.
#pragma once
#include <array>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

#include "machine/mmio_device.hpp"

namespace xlle {

// A RAM-backed MMIO register file: reads return the last written value (reset 0).
class RegFileMmio : public MmioDevice {
public:
    RegFileMmio(const char* name, uint32_t base, uint32_t size, FaultFn fault)
        : name_(name), base_(base), size_(size), fault_(std::move(fault)), regs_(size, 0) {}

    const char* name() const override { return name_; }
    uint32_t mmio_base() const override { return base_; }
    uint32_t mmio_size() const override { return size_; }

    uint32_t mmio_read(uint32_t addr, int size) override {
        uint32_t off = addr - base_;
        uint32_t value = 0;
        for (int i = 0; i < size && off + i < size_; ++i)
            value |= static_cast<uint32_t>(regs_[off + i]) << (8 * i);
        return value;
    }
    void mmio_write(uint32_t addr, int size, uint32_t value) override {
        uint32_t off = addr - base_;
        for (int i = 0; i < size && off + i < size_; ++i)
            regs_[off + i] = static_cast<uint8_t>(value >> (8 * i));
    }

protected:
    uint32_t reg32(uint32_t off) const {
        return regs_[off] | (regs_[off + 1] << 8) | (regs_[off + 2] << 16) |
               (static_cast<uint32_t>(regs_[off + 3]) << 24);
    }
    void set_reg32(uint32_t off, uint32_t value) {
        regs_[off] = static_cast<uint8_t>(value);
        regs_[off + 1] = static_cast<uint8_t>(value >> 8);
        regs_[off + 2] = static_cast<uint8_t>(value >> 16);
        regs_[off + 3] = static_cast<uint8_t>(value >> 24);
    }

    const char* name_;
    uint32_t base_, size_;
    FaultFn fault_;
    std::vector<uint8_t> regs_;
};

class McpxApu : public RegFileMmio {
public:
    static constexpr uint32_t kBase = 0xFE800000u;
    static constexpr uint32_t kSize = 0x80000u;

    explicit McpxApu(FaultFn fault)
        : RegFileMmio("mcpx-apu", kBase, kSize, std::move(fault)) {}

    using MemReadFn = std::function<bool(uint32_t, void*, size_t)>;
    using MemWriteFn = std::function<bool(uint32_t, const void*, size_t)>;
    using IrqFn = std::function<void(int, bool)>;

    void set_mem_access(MemReadFn rd, MemWriteFn wr) {
        mem_read_ = std::move(rd);
        mem_write_ = std::move(wr);
    }
    void set_irq(IrqFn fn, int irq = 5) {
        irq_fn_ = std::move(fn);
        irq_num_ = irq;
        update_irq();
    }

    // Advance to an absolute 5.333 ms APU-frame number.
    void set_frame(uint64_t frame) {
        while (frame_ < frame) {
            ++frame_;
            frame_step();
        }
    }

    uint32_t mmio_read(uint32_t addr, int size) override {
        uint32_t off = addr - base_;
        if (off == kXgscnt)
            set_reg32(kXgscnt, static_cast<uint32_t>(xgscnt_ += 0x400));
        // VP PIO_FREE: the unsimulated queue is always empty, matching xemu.
        if (off == kVpBase + 0x10) return 0x80;
        log_subregion("rd", off);
        return RegFileMmio::mmio_read(addr, size);
    }

    void mmio_write(uint32_t addr, int size, uint32_t value) override {
        uint32_t off = addr - base_;
        if (off == kIsts && size == 4) {
            set_reg32(kIsts, reg32(kIsts) & ~value);  // W1C
            update_irq();
            return;
        }
        if (size == 4 && off >= kVpBase) {
            uint32_t method = off - kVpBase;
            if (method == kSetAntecedent) {
                set_reg32(kFeav, value);
                set_reg32(kFedecmeth, method);
                set_reg32(kFedecparam, value);
                return;
            }
            if (method == kVoiceOn) {
                voice_on(value);
                return;
            }
            if (method == kVoiceOff) {
                voice_off(static_cast<uint16_t>(value & 0xffff));
                return;
            }
        }

        const bool releases_method_trap =
            off == kFectl && size == 4 &&
            (reg32(kFectl) & kFectlMethodMode) == kFectlMethodTrapped &&
            (value & kFectlMethodMode) != kFectlMethodTrapped;
        // Releasing the FE consumes the decoded method. Leaving REQUESTED latched makes a
        // level-IRQ retry redispatch a stale voice handle after the kernel has freed it.
        if (releases_method_trap) value &= ~kFectlTrapReason;

        log_subregion("wr", off);
        RegFileMmio::mmio_write(addr, size, value);
        if (off == kFememdata && size == 4) {
            uint32_t dst = reg32(kFememaddr);
            if (!write_mem(dst, &value, 4)) device_fault("FEMEMDATA guest write failed");
        }
        if ((off == kIen || off == kFectl) && size == 4) update_irq();
    }

    uint64_t voice_off_methods() const { return voice_off_methods_; }
    uint64_t idle_voice_traps() const { return idle_voice_traps_; }

private:
    // Top-level PAPU registers.
    static constexpr uint32_t kIsts = 0x1000, kIen = 0x1004;
    static constexpr uint32_t kFectl = 0x1100, kFecv = 0x1110, kFeav = 0x1118;
    static constexpr uint32_t kFenaddr = 0x115c;
    static constexpr uint32_t kFedecmeth = 0x1300, kFedecparam = 0x1304;
    static constexpr uint32_t kFememaddr = 0x1324, kFememdata = 0x1334;
    static constexpr uint32_t kFetforce1 = 0x1504;
    static constexpr uint32_t kSectl = 0x2000, kXgscnt = 0x200c, kVpvaddr = 0x202c;
    static constexpr std::array<uint32_t, 3> kVoiceTop = {0x2054, 0x2060, 0x206c};
    static constexpr std::array<uint32_t, 3> kVoiceCurrent = {0x2058, 0x2064, 0x2070};
    static constexpr std::array<uint32_t, 3> kVoiceNext = {0x205c, 0x2068, 0x2074};

    // Interrupt and front-end fields.
    static constexpr uint32_t kIstsGint = 1u << 0, kIstsFet = 1u << 4;
    static constexpr uint32_t kIstsFen = 1u << 5, kIstsFev = 1u << 6;
    static constexpr uint32_t kFectlMethodMode = 0x000000e0;
    static constexpr uint32_t kFectlMethodTrapped = 0x000000e0;
    static constexpr uint32_t kFectlTrapReason = 0x00000f00;
    static constexpr uint32_t kFectlTrapRequested = 0x00000f00;
    static constexpr uint32_t kForceIdleVoice = 1u << 15;

    // VP method and guest voice structure fields.
    static constexpr uint32_t kVpBase = 0x20000, kSetAntecedent = 0x120;
    static constexpr uint32_t kVoiceOn = 0x124, kVoiceOff = 0x128;
    static constexpr uint32_t kSe2feIdleVoice = 0x8000;
    static constexpr uint32_t kVoiceSize = 0x80, kVoiceCfgFmt = 0x04;
    static constexpr uint32_t kVoiceState = 0x54, kVoicePitchLink = 0x7c;
    static constexpr uint32_t kVoiceActive = 1u << 21, kVoiceStream = 1u << 24;
    static constexpr uint16_t kNullVoice = 0xffff;
    static constexpr uint32_t kMaxVoices = 256;

    bool read_mem(uint32_t addr, void* dst, size_t size) {
        return mem_read_ && mem_read_(addr, dst, size);
    }
    bool write_mem(uint32_t addr, const void* src, size_t size) {
        return mem_write_ && mem_write_(addr, src, size);
    }
    bool read_u32(uint32_t addr, uint32_t& value) {
        return read_mem(addr, &value, sizeof(value));
    }
    bool write_u32(uint32_t addr, uint32_t value) {
        return write_mem(addr, &value, sizeof(value));
    }

    void device_fault(const std::string& why) {
        if (!faulted_) {
            faulted_ = true;
            fault_(std::string("mcpx-apu: ") + why);
        }
    }

    void update_irq() {
        // A trapped front end owns FET status until FECTL is released.
        if ((reg32(kFectl) & kFectlMethodMode) == kFectlMethodTrapped)
            set_reg32(kIsts, reg32(kIsts) | kIstsFet);

        uint32_t ists = reg32(kIsts);
        uint32_t ien = reg32(kIen);
        bool level = (ien & kIstsGint) && (((ists & ~kIstsGint) & ien) != 0);
        if (level)
            ists |= kIstsGint;
        else
            ists &= ~kIstsGint;
        set_reg32(kIsts, ists);

        if (level != irq_level_) {
            irq_level_ = level;
            if (irq_fn_) irq_fn_(irq_num_, level);
        }
    }

    void post_voice_notifier(uint16_t voice, bool stream) {
        // 16-byte records: two hardware records first, then four per voice. Byte 14 is
        // envelope state and byte 15 is completion status. The initial SSL index is A/zero.
        constexpr uint32_t kNotifierBaseOffset = 2;
        constexpr uint32_t kNotifiersPerVoice = 4;
        constexpr uint8_t kDoneSuccess = 1;
        uint32_t notifier = 0;  // SSLA_DONE
        (void)stream;
        uint32_t addr = reg32(kFenaddr) +
                        16u * (kNotifierBaseOffset + voice * kNotifiersPerVoice + notifier);
        uint8_t envelope = 1;
        if (!write_mem(addr + 14, &envelope, 1) ||
            !write_mem(addr + 15, &kDoneSuccess, 1)) {
            device_fault("voice notifier guest write failed");
            return;
        }
        set_reg32(kIsts, reg32(kIsts) | kIstsFev | kIstsFen);
    }

    bool read_voice_word(uint16_t voice, uint32_t field, uint32_t& value) {
        return read_u32(reg32(kVpvaddr) + voice * kVoiceSize + field, value);
    }
    bool write_voice_word(uint16_t voice, uint32_t field, uint32_t value) {
        return write_u32(reg32(kVpvaddr) + voice * kVoiceSize + field, value);
    }
    bool set_voice_next(uint16_t voice, uint16_t next) {
        uint32_t link = 0;
        return read_voice_word(voice, kVoicePitchLink, link) &&
               write_voice_word(voice, kVoicePitchLink, (link & 0xffff0000u) | next);
    }

    void voice_on(uint32_t argument) {
        uint16_t voice = static_cast<uint16_t>(argument);
        if (voice >= kMaxVoices) {
            device_fault("VOICE_ON handle out of range");
            return;
        }

        uint32_t antecedent = reg32(kFeav);
        uint32_t list = (antecedent >> 16) & 3;
        if (list != 0) {
            size_t index = list - 1;
            uint16_t old_top = static_cast<uint16_t>(reg32(kVoiceTop[index]));
            if (!set_voice_next(voice, old_top)) {
                device_fault("VOICE_ON top-list link write failed");
                return;
            }
            set_reg32(kVoiceTop[index], voice);
        } else {
            uint16_t previous = static_cast<uint16_t>(antecedent);
            if (previous >= kMaxVoices) {
                device_fault("VOICE_ON inherited antecedent out of range");
                return;
            }
            uint32_t previous_link = 0;
            if (!read_voice_word(previous, kVoicePitchLink, previous_link) ||
                !set_voice_next(voice, static_cast<uint16_t>(previous_link)) ||
                !set_voice_next(previous, voice)) {
                device_fault("VOICE_ON inherited-list link write failed");
                return;
            }
        }

        uint32_t state = 0;
        if (!read_voice_word(voice, kVoiceState, state) ||
            !write_voice_word(voice, kVoiceState, state | kVoiceActive)) {
            device_fault("VOICE_ON guest state update failed");
            return;
        }
        set_reg32(kFedecmeth, kVoiceOn);
        set_reg32(kFedecparam, argument);
        ++voice_on_methods_;
    }

    void voice_off(uint16_t voice) {
        if (voice >= kMaxVoices) {
            device_fault("VOICE_OFF handle out of range");
            return;
        }
        uint32_t base = reg32(kVpvaddr) + voice * kVoiceSize;
        uint32_t state = 0, format = 0;
        if (!read_u32(base + kVoiceState, state) ||
            !read_u32(base + kVoiceCfgFmt, format)) {
            device_fault("VOICE_OFF guest voice read failed");
            return;
        }
        if (!write_u32(base + kVoiceState, state & ~kVoiceActive)) {
            device_fault("VOICE_OFF guest voice write failed");
            return;
        }

        set_reg32(kFedecmeth, kVoiceOff);
        set_reg32(kFedecparam, voice);
        post_voice_notifier(voice, (format & kVoiceStream) != 0);
        ++voice_off_methods_;
        // xemu defers IRQ aggregation to the APU worker/frame thread. This matters: the
        // next frame also publishes IDLE_VOICE, so the kernel observes FET + notifier causes
        // together and services the voice-idle completion before notifier teardown.
    }

    void emit_idle_voice(uint16_t voice) {
        set_reg32(kFecv, voice);
        set_reg32(kFedecmeth, kSe2feIdleVoice);
        set_reg32(kFedecparam, voice);
        if (!(reg32(kFetforce1) & kForceIdleVoice)) {
            device_fault("SE2FE_IDLE_VOICE emitted without FETFORCE1 enable");
            return;
        }

        uint32_t fectl = reg32(kFectl);
        fectl = (fectl & ~kFectlMethodMode) | kFectlMethodTrapped;
        fectl = (fectl & ~kFectlTrapReason) | kFectlTrapRequested;
        set_reg32(kFectl, fectl);
        ++idle_voice_traps_;
        update_irq();
    }

    void frame_step() {
        if (faulted_) return;
        // The setup engine is stopped while XCNTMODE is off or the FE is halted/trapped.
        if ((reg32(kSectl) & 0x18) == 0 || (reg32(kFectl) & kFectlMethodMode) != 0)
            return;

        uint32_t voice_base = reg32(kVpvaddr);
        for (size_t list = 0; list < kVoiceTop.size(); ++list) {
            uint16_t voice = static_cast<uint16_t>(reg32(kVoiceTop[list]));
            set_reg32(kVoiceCurrent[list], voice);
            for (uint32_t count = 0; voice != kNullVoice; ++count) {
                if (voice >= kMaxVoices || count >= kMaxVoices) {
                    device_fault("voice list contains an invalid entry or cycle");
                    return;
                }

                uint32_t state = 0, link = 0;
                uint32_t addr = voice_base + voice * kVoiceSize;
                if (!read_u32(addr + kVoiceState, state) ||
                    !read_u32(addr + kVoicePitchLink, link)) {
                    device_fault("APU frame guest voice read failed");
                    return;
                }
                uint16_t next = static_cast<uint16_t>(link);
                set_reg32(kVoiceNext[list], next);
                if (!(state & kVoiceActive)) {
                    emit_idle_voice(voice);
                    return;
                }
                voice = next;
                set_reg32(kVoiceCurrent[list], voice);
            }
        }
    }

    void log_subregion(const char* what, uint32_t off) {
        if (off >= 0x20000 && sub_logged_ < 16) {
            std::printf("[apu] %s DSP subregion off=0x%05x — register-file default\n", what, off);
            if (++sub_logged_ == 16) std::printf("[apu] (subregion log capped)\n");
        }
    }

    MemReadFn mem_read_;
    MemWriteFn mem_write_;
    IrqFn irq_fn_;
    int irq_num_ = 5;
    bool irq_level_ = false;
    bool faulted_ = false;
    uint64_t frame_ = 0;
    uint64_t voice_on_methods_ = 0;
    uint64_t voice_off_methods_ = 0;
    uint64_t idle_voice_traps_ = 0;
    uint64_t xgscnt_ = 0;
    int sub_logged_ = 0;
};

}  // namespace xlle
