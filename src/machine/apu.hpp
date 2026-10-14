// xlle — MCPX APU (audio processing unit) at 0xFE800000 (512 KB, PCI 00:05.0 BAR0).
//
// Harvested from xemu/hw/xbox/mcpx/apu. The top level is a RAM-backed register file with
// special handling for interrupt status and XGSCNT. The FE fence/control route
// admits captured idle token transfers; busy ordering and cold state remain guarded. The 5.333 ms APU
// frame walker encounters inactive voices in the 2D/3D/MP list and emits
// SE2FE_IDLE_VOICE. With FETFORCE1 enabled,
// that publishes the front-end trap and drives the APU's level IRQ according to IEN/ISTS.
//
// Bounded PCM/ADPCM source consumption precedes resampling/mixing/GP effects.
// Whole device output and other VP/GP/EP accesses remain unqualified; the strict
// reference transport rejects them instead of admitting register-file defaults.
#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "machine/mmio_device.hpp"
#include "machine/apu_clock.hpp"
#include "machine/apu_adpcm.h"
#include "machine/apu_gp_dma.hpp"
#include "machine/apu_envelope_curve.hpp"
#include "machine/apu_ssl.hpp"

namespace xlle {

struct ApuRateState;
class ApuDspProcessor;
struct ApuEnvelopeStep {
    bool running=false;
    float amplitude=0,filter=0;
    // Only the owning voice processor retires an amplitude force-release.
    // A filter force-release supplies zero modulation, not voice completion.
    bool force_release=false;
};
struct ApuVoiceQuantum {
    uint32_t visited=0,contributed=0,completed=0;
    bool published=false;
    uint64_t sequence=0;
};

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
        : RegFileMmio("mcpx-apu", kBase, kSize, std::move(fault)),
          gp_dma_([this](uint32_t a){return gp_word(a);},
                  [this](uint32_t a,void* data,uint32_t n,bool output){return gp_sge(0x2040,0x20d4,a,data,n,output);},
                  [this](unsigned i,void* data,uint32_t n,bool output){return dsp_fifo(ApuDspKind::Gp,i,data,n,output);},
                  [this](const std::string& why){device_fault(why);},
                  [this]{return gp_program_write(true);},
                  [this]{return gp_mix_publication(false,true);}),
          ep_dma_([this](uint32_t a){return ep_word(a);},
                  [this](uint32_t a,void* data,uint32_t n,bool output){return gp_sge(0x2048,0x20dc,a,data,n,output);},
                  [this](unsigned i,void* data,uint32_t n,bool output){return dsp_fifo(ApuDspKind::Ep,i,data,n,output);},
                  [this](const std::string& why){device_fault(why);},
                  [this]{return ep_program_write(true);},{},ApuDspKind::Ep) {}
    McpxApu(const McpxApu&)=delete;
    McpxApu& operator=(const McpxApu&)=delete;

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

    // Final 2D VP send stage: source frames must already have undergone pitch,
    // filtering and envelope/voice scaling. Accumulate into 32 planar mix bins,
    // each with 32 frame slots. This stage neither pulls source nor retires it.
    // Slot order selects source channel; repeated destination bins add.
    bool mix_2d_samples(uint16_t voice,const float* stereo,uint32_t frames,float* bins);

    // Source -> nominal pitch conversion, with persistent interpolation history.
    // Real source end drains the filter; no silence is substituted for starvation.
    // Amplitude envelopes/filtering and notifier delivery remain downstream.
    bool resample_source(uint16_t voice,float* stereo,uint32_t capacity,uint32_t& count,bool& ended);

    // One 32-output-sample VP quantum. Envelopes run at 1500 Hz independently
    // of source pitch. Decay/release use an explicitly nominal replaceable
    // policy; invalid phases/rates fail before either envelope changes.
    bool step_envelopes(uint16_t voice,ApuEnvelopeStep& result);
    // Compose source, rate and amplitude envelopes only. Force-release returns
    // no source output; this stage does not retire the voice. Filtering, voice
    // headroom, final sends, GP and completion remain subsequent stages.
    bool source_envelope_quantum(uint16_t voice,float* stereo,uint32_t& count,bool& ended);
    // One real pre-GP voice quantum: source/rate/envelopes, bypass-filter path,
    // final sends and finite-buffer retirement. Accumulates into 32 planar bins.
    // The returned count owns actual output; no padding or GP output is supplied.
    bool process_2d_voice(uint16_t voice,float* bins,uint32_t& count,bool& completed);
    // One device-owned 32-sample quantum over the actual guest 2D list.
    // Publish the real GP mix bank before retiring completed buffer owners.
    // Caller controls admission/cadence; this neither starts nor completes GP.
    bool process_2d_quantum(ApuVoiceQuantum& result);
    uint64_t voice_mix_quanta()const{return voice_mix_quanta_;}

    // Fresh opaque descriptor reads for the later stream owner. This stages
    // real shared RAM, without consuming samples or decoding control fields.
    bool read_ssl_segment(uint16_t voice,unsigned bank,uint32_t segment,ApuSslDescriptor& descriptor) {
        if(faulted_)return false;
        uint32_t address=0;
        if(!ssl_.segment_address(reg32(0x2034),voice,bank,segment,address)) {
            device_fault("SSL segment binding/address remains unqualified");return false;
        }
        uint8_t raw[8];
        if(!read_mem(address,raw,sizeof(raw))) {
            device_fault("SSL descriptor DMA read failed");return false;
        }
        ApuSslDescriptor candidate;
        for(unsigned i=0;i<4;++i) {
            candidate.offset|=uint32_t(raw[i])<<(i*8);
            candidate.control|=uint32_t(raw[4+i])<<(i*8);
        }
        descriptor=candidate;return true;
    }

    // Stable GP RAM banks for the build-selected DSP core. Encoded addresses:
    // X, mix, Y+0x1800, P+0x2800. Unknown banks have no storage/default value.
    uint32_t* gp_word(uint32_t address) {
        if(address<0x1000 || (address>=0x1400 && address<0x2000) ||
           (address>=0x2800 && address<0x3800))return gp_words_.data()+address;
        return nullptr;
    }
    // EP retains its own smaller X/Y banks and P. Storage is allocated only
    // for an explicit EP access/owner; the fast native path constructs no CPU.
    uint32_t* ep_word(uint32_t address) {
        if(!apu_dsp_bank_end(ApuDspKind::Ep,address))return nullptr;
        if(!ep_words_)ep_words_=std::make_unique<std::array<uint32_t,0x3800>>();
        return ep_words_->data()+address;
    }
    bool ep_read(uint32_t address,uint32_t& value) {
        return !faulted_ && ep_dma_.read(address,value);
    }
    bool ep_write(uint32_t address,uint32_t value) {
        fe_context_known_=false;fe_state_used_=true;
        return !faulted_ && ep_dma_.write(address,value);
    }
    uint64_t ep_dma_nodes()const{return ep_dma_.nodes();}
    bool dsp_dma_pending(ApuDspKind kind)const{return kind==ApuDspKind::Gp?gp_dma_.work_pending():ep_dma_.work_pending();}
    bool dsp_work_active()const{return gp_dma_working_ || ep_dma_working_;}
    bool service_dsp_dma(ApuDspKind kind,uint32_t quanta=1) {
        if(faulted_)return false;
        if(dsp_work_active()){device_fault("reentrant DSP DMA device work");return false;}
        bool& working=kind==ApuDspKind::Gp?gp_dma_working_:ep_dma_working_;
        working=true;
        const bool done=kind==ApuDspKind::Gp?gp_dma_.service(quanta):ep_dma_.service(quanta);
        working=false;return done && !faulted_;
    }
    uint64_t ep_program_revision()const{return ep_program_revision_;}
    bool bind_ep_program_owner(void* owner,ApuDspDma::ProgramWrite guard) {
        if(!owner || !guard || ep_program_owner_)return false;
        fe_context_known_=false;fe_state_used_=true;
        ep_program_owner_=owner;ep_program_guard_=std::move(guard);return true;
    }
    void unbind_ep_program_owner(void* owner) {
        if(ep_program_owner_==owner){
            if(ep_dma_.work_pending() && !faulted_)device_fault("EP CPU owner removed with pending DMA");
            ep_program_owner_=nullptr;ep_program_guard_={};
        }
    }
    bool gp_read(uint32_t address,uint32_t& value) {
        if(faulted_ || !gp_dma_.read(address,value))return false;
        if(address==0xffffc5 && gp_frame_pending_)value|=2;
        return true;
    }
    bool gp_write(uint32_t address,uint32_t value) {
        if(faulted_)return false;
        fe_context_known_=false;fe_state_used_=true;
        if(address!=0xffffc5)return gp_dma_.write(address,value);
        if((value&~0x82u) && !(value==0xfff && gp_startup_clear_ && gp_startup_clear_())) {
            device_fault("unqualified GP event acknowledgement mask");return false;
        }
        if(!gp_dma_.write(address,value&0x80))return false;
        if(value&2)gp_frame_pending_=false;
        return true;
    }
    uint64_t gp_dma_nodes()const{return gp_dma_.nodes();}
    bool faulted()const{return faulted_;}
    // One serialized CPU owner observes executable host/DMA publication. This
    // epoch is invalidation bookkeeping, not DSP completion or device time.
    uint64_t gp_program_revision()const{return gp_program_revision_;}
    bool bind_gp_program_owner(void* owner,ApuGpDma::ProgramWrite guard) {
        if(!owner || !guard || gp_program_owner_)return false;
        fe_context_known_=false;fe_state_used_=true;
        gp_program_owner_=owner;gp_program_guard_=std::move(guard);return true;
    }
    void unbind_gp_program_owner(void* owner) {
        if(gp_program_owner_==owner){
            if(gp_dma_.work_pending() && !faulted_)device_fault("GP CPU owner removed with pending DMA");
            gp_program_owner_=nullptr;gp_program_guard_={};
        }
    }
    // Frame flags are vendor polled events, distinct from core IPR/vector state.
    // A successful real VP transaction owns MIX until that CPU actually yields.
private:
    friend class ApuDspProcessor;
    bool bind_gp_frame_owner(void* owner,std::function<bool()> startup_clear) {
        if(!owner || owner!=gp_program_owner_ || !startup_clear || gp_frame_owner_)return false;
        gp_frame_owner_=owner;gp_startup_clear_=std::move(startup_clear);return true;
    }
    void unbind_gp_frame_owner(void* owner) {
        if(gp_frame_owner_!=owner)return;
        if(gp_mix_lease_ || gp_frame_pending_)device_fault("GP frame owner removed with outstanding work");
        gp_frame_owner_=nullptr;gp_startup_clear_={};
    }
    bool gp_process_frame(void* owner,ApuVoiceQuantum& result) {
        result={};
        if(faulted_)return false;
        if(!owner || owner!=gp_frame_owner_ || gp_mix_lease_ || gp_frame_pending_ || gp_mix_admitting_) {
            device_fault("GP frame publication has no available owner");return false;
        }
        gp_mix_admitting_=true;
        struct Scope {bool& flag;~Scope(){flag=false;}} scope{gp_mix_admitting_};
        if(!process_2d_quantum(result))return false;
        if(!result.published)return true;
        if(!result.sequence){device_fault("GP frame publication lacks an actual VP sequence");return false;}
        gp_mix_lease_=true;return true;
    }
    bool gp_post_frame(void* owner) {
        if(faulted_)return false;
        if(!owner || owner!=gp_frame_owner_ || !gp_mix_lease_ || gp_frame_pending_) {
            device_fault("GP frame event has no published MIX owner");return false;
        }
        gp_frame_pending_=true;return true;
    }
    bool gp_yield_frame(void* owner) {
        if(faulted_)return false;
        if(!owner || owner!=gp_frame_owner_) {
            device_fault("GP frame yield has no matching owner");return false;
        }
        gp_mix_lease_=false;return true;
    }
public:
    bool gp_frame_outstanding()const{return gp_mix_lease_ || gp_frame_pending_;}
    // Restore only the independently observed running-probe FE context. This
    // is snapshot input, not a cold-reset default or an admission of title work.
    // A used device cannot discard its history by importing this idle floor.
    bool capture_fe_idle_context(uint32_t fectl,uint32_t sectl,uint32_t control,uint32_t gp_reset) {
        if(faulted_)return false;
        if(fe_context_known_ || fe_state_used_ || frame_ || voice_mix_quanta_ ||
           !fe_work_idle() || fectl!=kObservedIdleFectl || sectl!=kObservedIdleSectl ||
           control!=1 || gp_reset!=0) {
            device_fault("FE captured idle context remains unqualified");return false;
        }
        set_reg32(kFectl,fectl);set_reg32(kSectl,sectl);
        set_reg32(kFeFenceControl,control);set_reg32(0x3fffc,gp_reset);
        fe_context_known_=true;fe_state_used_=true;return true;
    }
    bool fe_fence_pending()const{return fe_fence_pending_;}
    uint64_t fe_fences_published()const{return fe_fences_published_;}
    // The serialized device owner supplies service boundaries, independently
    // of CPU polls. Exact latency is unspecified. A control release is neither
    // required for publication nor treated as proof that prior work finished.
    bool service_fe_fence() {
        if(faulted_)return false;
        if(fe_fence_working_) {device_fault("reentrant FE fence device work");return false;}
        if(!fe_fence_pending_)return true;
        if(!fe_context_idle()) {device_fault("FE fence outstanding work remains unqualified");return false;}
        fe_fence_working_=true;
        const uint32_t token=reg32(kFememdata);
        const uint8_t raw[4]={uint8_t(token),uint8_t(token>>8),uint8_t(token>>16),uint8_t(token>>24)};
        const bool done=write_mem(reg32(kFememaddr),raw,sizeof(raw));
        fe_fence_working_=false;
        if(!done) {device_fault("FE fence token DMA write failed");return false;}
        if(faulted_)return false;
        fe_fence_pending_=false;++fe_fences_published_;return true;
    }
    // Explicit scratch transport for the GP bootstrap/graph owner. It uses
    // the same physical SGE mapping as descriptor DMA and never starts GP.
    bool gp_scratch(uint32_t address,void* data,uint32_t bytes,bool output) {
        if(faulted_)return false;
        if((bytes && !data) || (reg32(0x2040)&7) ||
           !gp_sge(0x2040,0x20d4,address,data,bytes,output)) {
            device_fault("GP owned scratch transfer failed");return false;
        }
        return true;
    }
    bool ep_scratch(uint32_t address,void* data,uint32_t bytes,bool output) {
        if(faulted_)return false;
        if((bytes && !data) || (reg32(0x2048)&7) ||
           !gp_sge(0x2048,0x20dc,address,data,bytes,output)) {
            device_fault("EP owned scratch transfer failed");return false;
        }
        return true;
    }

    uint32_t mmio_read(uint32_t addr, int size) override {
        uint32_t off = addr - base_;
        if (faulted_) return 0;
        fe_state_used_=true;
        if(fe_fence_working_) {device_fault("reentrant FE fence device work");return 0;}
        if(touches_fe_fence(off,size)) {
            if(off==kFeFenceControl && size==4 && fe_context_idle())return reg32(off);
            device_fault("FE notification fence/control remains unqualified");return 0;
        }
        if(off>=0x30000 && off<0x40000) {
            uint32_t* word=gp_host_word(off,size);
            if(!word){device_fault("unqualified GP host read");return 0;}
            return *word&0xffffff;
        }
        if(off>=0x50000 && off<0x60000) {
            uint32_t* word=ep_host_word(off,size);
            if(!word){device_fault("unqualified EP host read");return 0;}
            return *word&0xffffff;
        }
        if (off == kXgscnt)
            set_reg32(kXgscnt, static_cast<uint32_t>(xgscnt_));
        // Synchronous FE model: the strict reference admits only implemented
        // methods whose effects precede this capacity read. Queue latency is
        // unqualified; this does not acknowledge unknown work as completed.
        if (off == kVpBase + 0x10) return 0x80;
        log_subregion("rd", off);
        return RegFileMmio::mmio_read(addr, size);
    }

    void mmio_write(uint32_t addr, int size, uint32_t value) override {
        uint32_t off = addr - base_;
        if (faulted_) return;
        fe_state_used_=true;
        if(fe_fence_working_) {device_fault("reentrant FE fence device work");return;}
        // Guard the device itself, including partial/overlapping transactions;
        // reference-wrapper whitelists cannot establish unknown fence behavior.
        if(touches_fe_fence(off,size)) {
            if(size==4 && (off==kFeFenceControl || off==kFememaddr || off==kFememdata) &&
               fe_context_idle()) {
                if(off==kFeFenceControl && value<=1) {set_reg32(off,value);return;}
                if(off==kFememaddr && !fe_fence_pending_ && !(value&3) && value<=0x03fffffc) {
                    set_reg32(off,value);fe_address_known_=true;return;
                }
                if(off==kFememdata && !fe_fence_pending_ && fe_address_known_ && reg32(kFeFenceControl)==1) {
                    set_reg32(off,value);fe_fence_pending_=true;return;
                }
            }
            device_fault("FE notification fence/control remains unqualified");return;
        }
        if(fe_fence_pending_) {device_fault("FE fence has pending publication");return;}
        // The captured observation covers these FE transactions only. Changing
        // other device state cannot recreate the observation just by later
        // restoring a few register values; busy-to-idle ordering is still open.
        fe_context_known_=false;
        if(off>=kVpBase) {
            const uint32_t method=off-kVpBase;
            const bool ssl=method==0x18c || method==0x190 || method==0x320 || method==0x35c ||
                           (method>=0x600 && method<0x800);
            if(ssl && (size!=4 || (method&3))) {
                device_fault("SSL transaction width/alignment remains unqualified");return;
            }
        }
        if(off>=0x30000 && off<0x40000) {
            uint32_t* word=gp_host_word(off,size);
            if(!word){device_fault("unqualified GP host write/reset");return;}
            if(off>=0x3a000 && off<0x3e000 && !gp_program_write()) {
                device_fault("GP executable host publication was rejected");return;
            }
            if(off>=0x35000 && off<0x36000 && !gp_mix_publication())return;
            *word=value&0xffffff;return;
        }
        if(off>=0x50000 && off<0x60000) {
            uint32_t* word=ep_host_word(off,size);
            if(!word){device_fault("unqualified EP host write/reset");return;}
            if(off>=0x5a000 && off<0x5e000 && !ep_program_write()) {
                device_fault("EP executable host publication was rejected");return;
            }
            *word=value&0xffffff;return;
        }
        if (off == kIsts && size == 4) {
            set_reg32(kIsts, reg32(kIsts) & ~value);  // W1C
            update_irq();
            return;
        }
        if (size == 4 && off >= kVpBase) {
            uint32_t method = off - kVpBase;
            if(configure_ssl(method,value))return;
            if (method == 0x2f8) {
                if (value >= kMaxVoices) device_fault("selected voice out of range");
                else set_reg32(kFecv, value);
                return;
            }
            if (configure_voice(method, value)) return;
            if(method>=0x200 && method<=0x27c && (method&3)==0) {
                submix_headroom_[(method-0x200)/4]=value&7;
                return;
            }
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
                voice_off(value);
                return;
            }
            if (method == kVoiceRelease) {
                voice_release(value);
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
        if(size==4 && (off==kVpvaddr || off==kVpsgeaddr)) {
            for(auto& rate:voice_rates_)rate.reset();
            for(auto& envelope:voice_envelopes_)envelope={};
        }
        if ((off == kIen || off == kFectl) && size == 4) update_irq();
    }

    uint64_t voice_off_methods() const { return voice_off_methods_; }
    uint64_t voice_release_methods() const { return voice_release_methods_; }
    uint64_t idle_voice_traps() const { return idle_voice_traps_; }

private:
    bool process_2d_voice_impl(uint16_t voice,float* bins,uint32_t& count,bool& completed,bool retire);
    uint64_t voice_mix_quanta_=0;
    bool processing_quantum_=false;
    bool configure_ssl(uint32_t method,uint32_t value) {
        if(method==0x190) {
            if(!ssl_.select(value))device_fault("SSL staging window remains unqualified");
            return true;
        }
        if(method==0x320 || method==0x35c) {
            if(!ssl_.bind(reg32(kFecv),method==0x35c,value))
                device_fault("SSL voice binding extent remains unqualified");
            return true;
        }
        if(method>=0x600 && method<0x800) {
            uint32_t address=0;
            if((method&3) || !ssl_.staged_address(reg32(0x2034),(method-0x600)/4,address)) {
                device_fault("SSL staging descriptor address remains unqualified");return true;
            }
            const uint8_t raw[]={uint8_t(value),uint8_t(value>>8),uint8_t(value>>16),uint8_t(value>>24)};
            if(!write_mem(address,raw,sizeof(raw)))device_fault("SSL descriptor DMA write failed");
            return true;
        }
        // Alternate context-DMA ownership cannot fall through to a successful
        // register-file write. The bounded table uses VPSSLADDR directly.
        if(method==0x18c) {
            device_fault("SSL context DMA selection remains unqualified");return true;
        }
        return false;
    }
    ApuSsl ssl_;
    bool gp_program_write(bool dma=false) {
        if(faulted_)return false;
        if(dsp_work_active() && !dma){device_fault("GP executable host publication during DMA work");return false;}
        if(gp_program_guard_ && !gp_program_guard_())return false;
        ++gp_program_revision_;return true;
    }
    bool ep_program_write(bool dma=false) {
        if(faulted_)return false;
        if(dsp_work_active() && !dma){device_fault("EP executable host publication during DMA work");return false;}
        if(ep_program_guard_ && !ep_program_guard_())return false;
        ++ep_program_revision_;return true;
    }
    bool gp_mix_publication(bool voice_processor=false,bool dma=false) {
        if(dsp_work_active() && !dma){device_fault("GP MIX publication overlaps DMA device work");return false;}
        if(!gp_mix_lease_ && (!gp_mix_admitting_ || (voice_processor && !processing_quantum_)))return true;
        device_fault("GP MIX publication overlaps an outstanding frame");return false;
    }
    // Unamplified source/rate output and separately evaluated control permit a
    // future owned filter between rate conversion and amplitude application.
    bool source_signal_quantum(uint16_t voice,float* stereo,uint32_t& count,
                               bool& ended,ApuEnvelopeStep& control);
    uint32_t* gp_host_word(uint32_t offset,int size) {
        if(size!=4 || (offset&3))return nullptr;
        offset-=0x30000;
        if(offset<0x4000 || (offset>=0x5000 && offset<0x8000) ||
           (offset>=0xa000 && offset<0xe000))return gp_word(offset/4);
        return nullptr;
    }
    uint32_t* ep_host_word(uint32_t offset,int size) {
        if(size!=4 || (offset&3))return nullptr;
        offset-=0x50000;
        if(offset<0x3000 || (offset>=0x6000 && offset<0x6400) ||
           (offset>=0xa000 && offset<0xe000))return ep_word(offset/4);
        return nullptr;
    }
    bool gp_sge(uint32_t table,uint32_t maximum,uint32_t linear,void* data,uint32_t bytes,bool output) {
        auto* next=static_cast<uint8_t*>(data);
        if(uint64_t(linear)+bytes>0x100000000ull)return false;
        while(bytes) {
            const uint32_t entry=linear>>12,offset=linear&4095;
            const uint64_t descriptor=uint64_t(reg32(table))+uint64_t(entry)*8;
            uint32_t page=0;
            if(entry>reg32(maximum) || descriptor>UINT32_MAX-3 ||
               !read_u32(static_cast<uint32_t>(descriptor),page) || (page&4095))return false;
            const uint32_t n=bytes<(4096-offset)?bytes:4096-offset;
            if((output?!write_mem(page+offset,next,n):!read_mem(page+offset,next,n)) || faulted_)return false;
            next+=n;linear+=n;bytes-=n;
        }
        return true;
    }
    bool dsp_fifo(ApuDspKind kind,unsigned index,void* data,uint32_t bytes,bool output) {
        if(index>=(output?4u:2u))return false;
        const uint32_t registers=(kind==ApuDspKind::Gp?0x3000:0x4000)+(output?0x24:0x64)+index*16;
        const uint32_t base=reg32(registers),end=reg32(registers+4);
        uint32_t cursor=reg32(registers+8);
        // Bounded low-24-bit address fields; BASE alignment is still uncertain
        // in the published register reference. Admit 256-byte aligned rings
        // (including original Blinx's 0..0x2000); do not guess reserved bits.
        if((base|end|cursor)&0xff000000 || ((base|end)&255) || base>=end || cursor>=end)
            return false;
        if(cursor<base)cursor=base;
        auto* next=static_cast<uint8_t*>(data);
        while(bytes) {
            const uint32_t n=bytes<(end-cursor)?bytes:end-cursor;
            if(!gp_sge(kind==ApuDspKind::Gp?0x2044:0x204c,kind==ApuDspKind::Gp?0x20d8:0x20e0,cursor,next,n,output))return false;
            next+=n;bytes-=n;cursor+=n;
            if(cursor==end)cursor=base;
        }
        // No fabricated progress on failed DMA. Physical writes before a later
        // page fault can remain; the strict device then faults without EOL.
        set_reg32(registers+8,cursor);return true;
    }
    bool resample_source_impl(uint16_t voice,float* stereo,uint32_t capacity,
                              uint32_t& count,bool& ended,double pitch_modulation);
    // Top-level PAPU registers.
    static constexpr uint32_t kIsts = 0x1000, kIen = 0x1004;
    static constexpr uint32_t kFectl = 0x1100, kFecv = 0x1110, kFeav = 0x1118;
    static constexpr uint32_t kFenaddr = 0x115c;
    static constexpr uint32_t kFedecmeth = 0x1300, kFedecparam = 0x1304;
    static constexpr uint32_t kFememaddr = 0x1324, kFememdata = 0x1334;
    static constexpr uint32_t kFeFenceControl = 0x1510;
    static constexpr uint32_t kObservedIdleFectl=0x00071f8f, kObservedIdleSectl=7;
    bool fe_work_idle()const {
        return !processing_quantum_ && !gp_mix_admitting_ && !gp_frame_outstanding() &&
               !dsp_work_active() && !gp_dma_.work_pending() && !ep_dma_.work_pending() &&
               !gp_program_owner_ && !gp_frame_owner_ && !ep_program_owner_ &&
               !gp_program_revision_ && !ep_program_revision_ &&
               reg32(0x3fffc)==0 && reg32(0x5fffc)==0;
    }
    bool fe_context_idle()const {
        return fe_context_known_ && reg32(kFectl)==kObservedIdleFectl &&
               reg32(kSectl)==kObservedIdleSectl && fe_work_idle();
    }
    static bool touches_fe_fence(uint32_t off,int size) {
        if(size<=0 || off>=kFeFenceControl+4)return false;
        const uint64_t end=uint64_t(off)+static_cast<unsigned>(size);
        // Only exact qualified DWORD transactions may reach the idle model.
        // No other lane may fabricate readback or mutate partial fields.
        return (off<kFememaddr+4 && end>kFememaddr) ||
               (off<kFememdata+4 && end>kFememdata) ||
               (off<kFeFenceControl+4 && end>kFeFenceControl);
    }
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
    static constexpr uint32_t kVoiceOn = 0x124, kVoiceOff = 0x128,
                              kVoiceRelease = 0x12c;
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
            else if((method==0x304 && ((previous^value)&0xfffffc00)) ||
                    method==0x3a0 || method==0x3a4 || method==0x3d8 || method==0x3dc) {
                voice_rates_[voice].reset();
                voice_envelopes_[voice]={};
            }
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

        // The command supplies both initial envelope phases. Only the four
        // documented start phases are admitted; no guessed phase/cursor reset.
        const unsigned amplitude=argument>>28,filter=(argument>>24)&15;
        uint32_t state=0,counters=0,cursor=0,configuration[4]{};
        if(amplitude>3 || filter>3 || (argument&0x00ff0000)) {
            device_fault("VOICE_ON unsupported initial envelope/flags");return;
        }
        if(!read_voice_word(voice,kVoiceState,state) || !read_voice_word(voice,0x34,counters) ||
           !read_voice_word(voice,0x58,cursor)) {
            device_fault("VOICE_ON envelope state DMA failed");return;
        }
        if(cursor&0xffffff) {
            device_fault("VOICE_ON nonzero initial cursor reset remains unqualified");return;
        }
        for(unsigned i=0;i<4;++i)if(!read_voice_word(voice,8+i*4,configuration[i])) {
            device_fault("VOICE_ON envelope configuration DMA failed");return;
        }
        for(unsigned i=0;i<2;++i) {
            const unsigned phase=i?filter:amplitude,shift=i*16;
            if(!phase)continue; // disabled counter is owned by the processing step
            const uint32_t count=phase==2?0:((configuration[i*2+(phase==3)]>>12)&0xfff)*16;
            counters=(counters&~(0xffffu<<shift))|(count<<shift);
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

        state=(state&0x00ffffff)|(argument&0xff000000)|kVoiceActive;
        if (!write_voice_word(voice,0x34,counters) ||
            !write_voice_word(voice, kVoiceState, state)) {
            device_fault("VOICE_ON guest state update failed");
            return;
        }
        set_reg32(kFedecmeth, kVoiceOn);
        set_reg32(kFedecparam, argument);
        voice_rates_[voice].reset();
        voice_envelopes_[voice]={};
        ++voice_on_methods_;
    }

    // Ordinary 2D buffer release is control work, not a source fetch or DONE.
    // Current readable levels are retained as the nominal release origins.
    void voice_release(uint32_t argument) {
        const uint32_t voice=argument&0xffff;
        if((argument&0xffff0000) || voice<64 || voice>=kMaxVoices) {
            device_fault("VOICE_RELEASE ordinary handle/flags remain unqualified");return;
        }
        uint32_t state=0,format=0,counts=0,misc=0,lfo=0,levels[2]{};
        if(!read_voice_word(voice,kVoiceState,state) || !read_voice_word(voice,4,format)) {
            device_fault("VOICE_RELEASE owner state DMA failed");return;
        }
        if(format&((1u<<21)|(1u<<22)|(1u<<24))) {
            device_fault("VOICE_RELEASE stream/linked/multipass ownership remains unqualified");return;
        }
        if(!(state&kVoiceActive))return;
        if(!read_voice_word(voice,0x34,counts) || !read_voice_word(voice,0x18,misc) ||
           !read_voice_word(voice,0x6c,lfo) || !read_voice_word(voice,0x58,levels[0]) ||
           !read_voice_word(voice,0x5c,levels[1])) {
            device_fault("VOICE_RELEASE configuration DMA failed");return;
        }
        std::array<apu_envelope_curve::ReleaseHistory,2> released{};
        for(unsigned i=0;i<2;++i) {
            const uint16_t n=uint16_t(((i?misc:lfo)&0xfff)*16);
            counts=(counts&~(0xffffu<<(i*16)))|(uint32_t(n)<<(i*16));
            released[i]={true,uint8_t(levels[i]>>24),n,n,n};
        }
        state=(state&0x00ffffff)|0x66000000;
        if(!write_voice_word(voice,0x34,counts) || !write_voice_word(voice,kVoiceState,state)) {
            device_fault("VOICE_RELEASE state DMA failed");return;
        }
        voice_envelopes_[voice]=released;
        set_reg32(kFedecmeth,kVoiceRelease);
        set_reg32(kFedecparam,argument);
        ++voice_release_methods_;
    }

    bool retire_buffer(uint32_t voice) {
        if (voice >= kMaxVoices) {
            device_fault("VOICE_OFF handle out of range");
            return false;
        }
        uint32_t state=0,format=0;
        if(!read_voice_word(voice,kVoiceState,state) || !read_voice_word(voice,4,format)) {
            device_fault("VOICE_OFF state DMA failed");return false;
        }
        if(format&((1u<<21)|(1u<<22)|(1u<<24))) {
            device_fault("VOICE_OFF stream/linked/multipass ownership remains unqualified");return false;
        }
        // Original static-buffer completion observes record A's status byte.
        // Envelope/timestamp payload fields are outside this bounded route;
        // preserve them, never invent the former literal envelope-info byte.
        const uint64_t address=uint64_t(reg32(kFenaddr))+16ull*(2+voice*4)+15;
        uint8_t prior=0;
        if(address>UINT32_MAX || !read_mem(static_cast<uint32_t>(address),&prior,1)) {
            device_fault("VOICE_OFF notifier address DMA failed");return false;
        }
        if(!write_voice_word(voice,kVoiceState,state&~kVoiceActive)) {
            device_fault("VOICE_OFF state write failed");return false;
        }
        voice_rates_[voice].reset();
        voice_envelopes_[voice]={};
        const uint8_t done=1;
        if(!write_mem(static_cast<uint32_t>(address),&done,1)) {
            device_fault("VOICE_OFF notifier status write failed");return false;
        }
        set_reg32(kIsts,reg32(kIsts)|kIstsFen|kIstsFev);
        update_irq();
        return true;
    }
    void voice_off(uint32_t voice) {
        if(retire_buffer(voice))++voice_off_methods_;
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
    bool fe_context_known_=false,fe_state_used_=false,fe_address_known_=false;
    bool fe_fence_pending_=false,fe_fence_working_=false;
    uint64_t fe_fences_published_=0;
    uint64_t frame_ = 0;
    uint64_t voice_on_methods_ = 0;
    uint64_t voice_off_methods_ = 0;
    uint64_t voice_release_methods_ = 0;
    uint64_t idle_voice_traps_ = 0;
    uint64_t xgscnt_ = 0;
    std::array<bool,kMaxVoices> voice_locked_{};
    std::array<std::shared_ptr<ApuRateState>,kMaxVoices> voice_rates_{};
    std::array<std::array<apu_envelope_curve::ReleaseHistory,2>,kMaxVoices> voice_envelopes_{};
    std::array<uint8_t,32> submix_headroom_{};
    std::array<uint32_t,0x3800> gp_words_{};
    friend class ApuDspProcessor;
    // Retained across CPU detach/rebind. No qualified warm-reset profile exists.
    bool gp_counter_profile_touched_=false,ep_counter_profile_touched_=false;
    uint64_t gp_program_revision_=0;
    void* gp_program_owner_=nullptr;
    ApuGpDma::ProgramWrite gp_program_guard_;
    void* gp_frame_owner_=nullptr;
    std::function<bool()> gp_startup_clear_;
    bool gp_mix_lease_=false,gp_frame_pending_=false,gp_mix_admitting_=false;
    ApuGpDma gp_dma_;
    std::unique_ptr<std::array<uint32_t,0x3800>> ep_words_;
    uint64_t ep_program_revision_=0;
    void* ep_program_owner_=nullptr;
    ApuDspDma::ProgramWrite ep_program_guard_;
    ApuDspDma ep_dma_;
    bool gp_dma_working_=false,ep_dma_working_=false;
    int sub_logged_ = 0;
};

inline bool McpxApu::mix_2d_samples(uint16_t voice,const float* stereo,uint32_t frames,float* bins) {
    if(faulted_)return false;
    if(voice<64 || voice>=kMaxVoices || frames>32 || (frames && (!stereo || !bins))) {
        device_fault("invalid or unqualified 2D mix-stage input");return false;
    }
    if(!frames)return true;
    uint32_t route=0,format=0,packed[3];
    if(!read_voice_word(voice,0,route) || !read_voice_word(voice,4,format) ||
       !read_voice_word(voice,0x60,packed[0]) || !read_voice_word(voice,0x64,packed[1]) ||
       !read_voice_word(voice,0x68,packed[2])) {
        device_fault("mix-stage configuration DMA failed");return false;
    }
    if(route&0x80008000) {
        device_fault("mix-stage phase/reserved routing bits remain unqualified");return false;
    }
    for(uint32_t i=0;i<frames*2;++i)if(!std::isfinite(stereo[i])) {
        device_fault("non-finite VP mixer input");return false;
    }
    const unsigned channels=(format&(1u<<27))?2:1;
    for(unsigned slot=0;slot<8;++slot) {
        const unsigned bin=slot<6?(route>>(slot*5+(slot>=3)))&31:(format>>((slot-6)*5))&31;
        unsigned attenuation=0;
        if(slot<6)attenuation=(packed[slot/2]>>(slot%2?20:4))&4095;
        else for(unsigned part=0;part<3;++part)
            attenuation|=((packed[part]>>(slot==6?0:16))&15)<<(part*4);
        // Hardware volume units are 1/64 dB; all ones is a muted send.
        const float gain=attenuation==4095?0.0f:
            static_cast<float>(std::pow(10.0,-double(attenuation)/1280.0)/double(1u<<submix_headroom_[bin]));
        for(uint32_t n=0;n<frames;++n)bins[bin*32+n]+=stereo[n*2+(slot%channels)]*gain;
    }
    return true;
}

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
    // Original NTSC DirectSound emits S24+ADPCM; the existing S16+ADPCM
    // reference form decodes identically. Container selects the codec.
    const bool adpcm=container==2 && (type==1 || type==2);
    // Streaming SSL, multipass and linked-voice processing have separate
    // ownership/pipeline semantics; do not report those as consumed PCM.
    if((format&((1u<<21)|(1u<<22)|(1u<<24))) || stride!=channels ||
       !(adpcm || (type==0 && container==0) || (type==1 && container==1) || (type>=2 && container==3))) {
        device_fault("source processing format remains unqualified");return false;
    }
    const unsigned width=type==0?1:type==1?2:4;
    uint32_t cursor=current&0xffffff,end=last&0xffffff,start=loop&0xffffff;
    base&=0xffffff;
    const bool looping=(format&(1u<<25))!=0;
    if(cursor>end || (looping && start>end)) {
        device_fault("source buffer extent remains unqualified");return false;
    }
    // Block decode state lives only for this pull. A later guest seek/write or
    // SGE remap is observed on the next pull, without a stale host-side cache.
    uint32_t decoded_block=UINT32_MAX;
    int16_t decoded[128];
    while(count<capacity) {
        float sample[2];
        if(adpcm) {
            const uint32_t block=cursor/64;
            if(decoded_block!=block) {
                uint8_t encoded[72];
                if(!source_read(uint64_t(base)+uint64_t(block)*36*channels,encoded,36*channels) ||
                   !xlle_apu_decode_adpcm(encoded,36*channels,channels,decoded,128)) {
                    device_fault("source ADPCM block DMA/decode failed");return false;
                }
                decoded_block=block;
            }
            for(unsigned channel=0;channel<channels;++channel)
                sample[channel]=decoded[(cursor%64)*channels+channel]*(1.0f/32768);
        } else {
            uint8_t data[8];
            if(!source_read(uint64_t(base)+uint64_t(cursor)*width*channels,data,width*channels)) {
                device_fault("source sample scatter/gather DMA failed");return false;
            }
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
