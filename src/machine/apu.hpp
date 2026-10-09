// xlle — MCPX APU (audio processing unit) at 0xFE800000 (512 KB, PCI 00:05.0 BAR0).
//
// Harvested from xemu/hw/xbox/mcpx/apu. The top level is a RAM-backed register file with
// special handling for interrupt status, FEMEMDATA, and XGSCNT. The 5.333 ms APU
// frame walker encounters inactive voices in the 2D/3D/MP list and emits
// SE2FE_IDLE_VOICE. With FETFORCE1 enabled,
// that publishes the front-end trap and drives the APU's level IRQ according to IEN/ISTS.
//
// Bounded PCM source consumption is executable before resampling/mixing/GP effects.
// Whole device output and other VP/GP/EP accesses remain unqualified; the strict
// reference transport rejects them instead of admitting register-file defaults.
#pragma once
#include <array>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

#include "machine/mmio_device.hpp"
#include "machine/apu_clock.hpp"

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

    // Independently advance the free-running audio sample clock.
    void set_time(uint64_t ticks,uint64_t frequency) {
        if(!frequency || frequency>UINT64_MAX/48000u){device_fault("invalid APU clock frequency");return;}
        xgscnt_=apu_sample_counter(ticks,frequency);
    }
    // Advance to an absolute 5.333 ms APU-frame number.
    void set_frame(uint64_t frame) {
        while (frame_ < frame) {
            ++frame_;
            frame_step();
        }
    }

    // Source-side pull for the VP's resampler. A frame is one source sample per
    // channel; output is stereo normalized PCM (mono duplicated). Count reports
    // frames actually fetched, not elapsed host time or padded silence.
    // ended is a pipeline handoff after the final real sample, not a guest
    // completion notification. The VP owner must retire it before pulling again.
    // This does not apply pitch, envelopes, volume, mix routing or GP effects.
    bool source_frames(uint16_t voice, float* stereo, uint32_t capacity, uint32_t& count, bool& ended);

    uint32_t mmio_read(uint32_t addr, int size) override {
        uint32_t off = addr - base_;
        if (faulted_) return 0;
        if (off == kXgscnt)
            set_reg32(kXgscnt, static_cast<uint32_t>(xgscnt_));
        // VP PIO_FREE: the unsimulated queue is always empty, matching xemu.
        if (off == kVpBase + 0x10) return 0x80;
        log_subregion("rd", off);
        return RegFileMmio::mmio_read(addr, size);
    }

    void mmio_write(uint32_t addr, int size, uint32_t value) override {
        uint32_t off = addr - base_;
        if (faulted_) return;
        if (off == kIsts && size == 4) {
            set_reg32(kIsts, reg32(kIsts) & ~value);  // W1C
            update_irq();
            return;
        }
        if (size == 4 && off >= kVpBase) {
            uint32_t method = off - kVpBase;
            if (method == 0x2f8) {
                if (value >= kMaxVoices) device_fault("selected voice out of range");
                else set_reg32(kFecv, value);
                return;
            }
            if (configure_voice(method, value)) return;
            if (method == 0x140) {
                const uint16_t voice=static_cast<uint16_t>(value);
                uint32_t state=0;
                if(voice>=kMaxVoices || !read_voice_word(voice,kVoiceState,state) ||
                   !write_voice_word(voice,kVoiceState,(state&~(1u<<18))|(value&(1u<<18))))
                    device_fault("VOICE_PAUSE guest state update failed");
                return;
            }
            if (method == 0x2fc) {
                const uint32_t voice=reg32(kFecv);
                if(voice>=kMaxVoices)device_fault("VOICE_LOCK handle out of range");
                else voice_locked_[voice]=(value&1)!=0;
                return;
            }
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
    static constexpr uint32_t kVpsgeaddr = 0x2030;
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
    bool source_read(uint64_t linear, void* output, uint32_t bytes) {
        if(linear+bytes>0x100000000ull)return false;
        auto* next=static_cast<uint8_t*>(output);
        while(bytes) {
            const uint64_t descriptor=uint64_t(reg32(kVpsgeaddr))+(linear>>12)*8;
            uint32_t page=0;
            if(descriptor>UINT32_MAX-3 || !read_u32(static_cast<uint32_t>(descriptor),page) || (page&4095))return false;
            const uint32_t offset=static_cast<uint32_t>(linear)&4095;
            const uint32_t part=bytes<4096-offset?bytes:4096-offset;
            if(!read_mem(page+offset,next,part))return false;
            next+=part;linear+=part;bytes-=part;
        }
        return true;
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

    bool read_voice_word(uint16_t voice, uint32_t field, uint32_t& value) {
        const uint64_t address=uint64_t(reg32(kVpvaddr))+voice*kVoiceSize+field;
        return address<=UINT32_MAX-3 && read_u32(static_cast<uint32_t>(address),value);
    }
    bool write_voice_word(uint16_t voice, uint32_t field, uint32_t value) {
        const uint64_t address=uint64_t(reg32(kVpvaddr))+voice*kVoiceSize+field;
        return address<=UINT32_MAX-3 && write_u32(static_cast<uint32_t>(address),value);
    }
    bool set_voice_next(uint16_t voice, uint16_t next) {
        uint32_t link = 0;
        return read_voice_word(voice, kVoicePitchLink, link) &&
               write_voice_word(voice, kVoicePitchLink, (link & 0xffff0000u) | next);
    }

    // MCPX method/register encodings cross-checked against the original XDK
    // emitter and xemu apu_regs.h/vp.c (see AUDIO-LLE.md). This updates real
    // voice-table RAM; it does not assert playback or sample consumption.
    bool configure_voice(uint32_t method, uint32_t value) {
        struct Field { uint32_t method, offset, mask; };
        static constexpr Field fields[] = {
            {0x300,0x00,0xffffffff}, {0x304,0x04,0xffffffff},
            {0x308,0x08,0xffffffff}, {0x30c,0x0c,0xffffffff},
            {0x310,0x10,0xffffffff}, {0x314,0x14,0xffffffff},
            {0x318,0x18,0xffffffff},
            {0x360,0x60,0xffffffff}, {0x364,0x64,0xffffffff},
            {0x368,0x68,0xffffffff}, {0x36c,0x6c,0xffffffff},
            {0x374,0x74,0xffffffff}, {0x378,0x78,0xffffffff},
            {0x37c,0x7c,0xffff0000},
            {0x3a0,0x20,0x00ffffff}, {0x3a4,0x24,0x00ffffff},
            {0x3d8,0x58,0x00ffffff}, {0x3dc,0x5c,0x00ffffff},
        };
        for (const auto& field : fields) {
            if (method != field.method) continue;
            const uint32_t voice = reg32(kFecv);
            if (voice >= kMaxVoices) {
                device_fault("voice configuration handle out of range");return true;
            }
            uint32_t previous = 0;
            if (!read_voice_word(static_cast<uint16_t>(voice), field.offset, previous) ||
                !write_voice_word(static_cast<uint16_t>(voice), field.offset,
                                  (previous & ~field.mask) | (value & field.mask)))
                device_fault("voice configuration DMA failed");
            return true;
        }
        return false;
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
        // The old xemu-derived route wrote a literal 1 into notifier byte 14
        // and assumed SSLA even for streams. Upstream itself marks the envelope
        // byte provisional. Do not publish completion from that placeholder.
        // Qualify the notifier record/SSL owner before restoring this method.
        device_fault("VOICE_OFF notifier metadata/ownership remains unqualified");
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
    std::array<bool,kMaxVoices> voice_locked_{};
    int sub_logged_ = 0;
};

inline bool McpxApu::source_frames(uint16_t voice,float* stereo,uint32_t capacity,uint32_t& count,bool& ended) {
    count=0;ended=false;
    if(faulted_)return false;
    if(voice>=kMaxVoices || (capacity && !stereo)) {
        device_fault("invalid source pull destination/voice");return false;
    }
    if(!capacity)return true;
    uint32_t state=0,format=0,base=0,current=0,last=0,loop=0;
    if(!read_voice_word(voice,0x54,state)) {
        device_fault("source voice state DMA failed");return false;
    }
    if(!(state&kVoiceActive) || (state&(1u<<18)) || voice_locked_[voice])return true;
    if(!read_voice_word(voice,0x04,format) || !read_voice_word(voice,0x20,base) ||
       !read_voice_word(voice,0x24,loop) || !read_voice_word(voice,0x58,current) ||
       !read_voice_word(voice,0x5c,last)) {
        device_fault("source voice parameters DMA failed");return false;
    }
    const unsigned channels=(format&(1u<<27))?2:1;
    const unsigned type=(format>>28)&3,container=(format>>30)&3;
    const unsigned stride=((format>>16)&31)+1;
    // Streaming SSL, multipass and linked-voice processing have separate
    // ownership/pipeline semantics; do not report those as consumed PCM.
    if((format&((1u<<21)|(1u<<22)|(1u<<24))) || stride!=channels ||
       !((type==0 && container==0) || (type==1 && container==1) || (type>=2 && container==3))) {
        device_fault("source processing format remains unqualified");return false;
    }
    const unsigned width=type==0?1:type==1?2:4;
    uint32_t cursor=current&0xffffff,end=last&0xffffff,start=loop&0xffffff;
    base&=0xffffff;
    const bool looping=(format&(1u<<25))!=0;
    if(cursor>end || (looping && start>end)) {
        device_fault("source buffer extent remains unqualified");return false;
    }
    while(count<capacity) {
        uint8_t data[8];
        if(!source_read(uint64_t(base)+uint64_t(cursor)*width*channels,data,width*channels)) {
            device_fault("source sample scatter/gather DMA failed");return false;
        }
        float sample[2];
        for(unsigned channel=0;channel<channels;++channel) {
            uint32_t raw=0;
            for(unsigned byte=0;byte<width;++byte)raw|=uint32_t(data[channel*width+byte])<<(byte*8);
            if(type==0)sample[channel]=float(int(raw)-128)*(1.0f/128);
            else {
                const unsigned bits=type==1?16:type==2?24:32;
                const uint64_t mask=(uint64_t(1)<<bits)-1,sign=uint64_t(1)<<(bits-1);
                const int64_t value=int64_t(raw&mask)-((raw&sign)?int64_t(uint64_t(1)<<bits):0);
                sample[channel]=static_cast<float>(double(value)/double(sign));
            }
        }
        if(channels==1)sample[1]=sample[0];
        // Publish the consumed position only after both channels were fetched.
        const bool finished=cursor==end && !looping;
        cursor=cursor==end?(looping?start:end):cursor+1;
        if(!write_voice_word(voice,0x58,(current&0xff000000)|cursor)) {
            device_fault("source cursor DMA failed");return false;
        }
        stereo[count*2]=sample[0];stereo[count*2+1]=sample[1];++count;
        if(finished){ended=true;return true;}
    }
    return true;
}

}  // namespace xlle
