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
#include "machine/apu_startup_config.hpp"
#include "machine/apu_dsp_host.hpp"
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

    // Advance from the shared clock. Physical mode1 counts while FE is halted;
    // off retains its value. Reads do not service time or fabricate an epoch.
    void set_time(uint64_t ticks,uint64_t frequency) {
        if(faulted_)return;
        const uint32_t mode=reg32(kSectl)&0x18;
        // Modes2/3 were observed frozen with FE halted, not qualified as an
        // active processing route. Do not reuse the old nonzero-mode shortcut.
        if((mode==0x10 || mode==0x18) && (reg32(kFectl)&kFectlMethodMode)!=0x80) {
            device_fault("active APU sequencer modes2/3 remain unqualified");return;
        }
        if(mode==8 && !sample_counter_known_) {
            device_fault("APU sample counter requires its original zero initialization");return;
        }
        const uint64_t previous=sample_counter_.ticks();
        if(!sample_counter_.advance(ticks,frequency,mode==8)) {
            device_fault("invalid or nonmonotonic APU clock");return;
        }
        // Physical CPU EP status100 can return after an immediate clear,
        // even with the core held. Its cause/latency is not established.
        // Do not preserve a fabricated permanent zero across elapsed time.
        if(ticks>previous)ep_host_status_known_=false;
    }
    // Advance to an absolute 5.333 ms APU-frame number.
    void set_frame(uint64_t frame) {
        frame_clock_primed_=true;
        while (frame_ < frame) {
            ++frame_;
            frame_step();
        }
    }

    // Establish the absolute clock phase once, before original startup work.
    // Time before this owner's creation must not be replayed as device work.
    bool prime_frame_clock(uint64_t frame) {
        if(faulted_ || frame_clock_primed_ || !kernel_audio_control_idle()) {
            device_fault("APU frame clock requires a fresh stopped kernel handoff");return false;
        }
        frame_=frame;frame_clock_primed_=true;return true;
    }
    // Internal scheduling observes the canonical controls, not a guest FECTL
    // read whose active status bits still require independent qualification.
    bool setup_processing_enabled()const {
        return !faulted_ && (reg32(kSectl)&0x18)==8 && !(reg32(kFectl)&kFectlMethodMode);
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
    // Explicit method-owned state for a future spatial consumer. Ordinary
    // voices do not use this field; spatial processing keeps its own guard.
    // No hardware readback is invented for the write-only method aperture.
    bool hrtf_headroom(uint8_t& amount)const {
        if(!hrtf_headroom_known_)return false;
        amount=hrtf_headroom_;return true;
    }

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
    // Caller-owned board image, copied once before EP execution. Hardware
    // observations establish CPU reads at Y0800..0FFF independently of low
    // Y RAM. Keep the image separate: no writable RAM/DMA/host alias is implied.
    bool install_ep_y_lookup(const uint32_t* image,size_t words) {
        if(!image || words!=2048 || ep_y_lookup_ || faulted_ || ep_program_owner_ ||
           ep_program_revision_ || ep_control_written_ || ep_counter_profile_touched_ ||
           ep_dma_.nodes() || dsp_work_active() || dsp_work_pending(ApuDspKind::Ep))return false;
        for(size_t i=0;i<words;++i)if(image[i]&0xff000000)return false;
        auto copy=std::make_unique<std::array<uint32_t,2048>>();
        for(size_t i=0;i<words;++i)(*copy)[i]=image[i];
        ep_y_lookup_=std::move(copy);return true;
    }
    bool ep_y_lookup_read(uint32_t address,uint32_t& value)const {
        if(!ep_y_lookup_ || address<0x800 || address>=0x1000)return false;
        value=(*ep_y_lookup_)[address-0x800];return true;
    }
    bool ep_read(uint32_t address,uint32_t& value) {
        if(faulted_ || !ep_dma_.read(address,value))return false;
        if(address==0xffffc5)value|=ep_frame_status_;
        return true;
    }
    bool ep_write(uint32_t address,uint32_t value) {
        fe_context_known_=false;fe_state_used_=true;kernel_audio_startup_=false;
        if(!faulted_ && address==0xffffc5 && !(value&~0x80u))return queue_ep_ack(value);
        return !faulted_ && ep_dma_.write(address,value);
    }
    // Retained DSP-side C4 publication. No CPU MMIO alias/reset value is
    // inferred: a value exists only after the executing EP wrote it.
    bool ep_control_publication(uint32_t& value)const {
        if(!ep_control_written_)return false;
        value=ep_control_value_;return true;
    }
    uint64_t ep_frame_events()const{return ep_frame_events_;}
    uint64_t ep_dma_nodes()const{return ep_dma_.nodes();}
    bool dsp_dma_pending(ApuDspKind kind)const{return kind==ApuDspKind::Gp?gp_dma_.work_pending():ep_dma_.work_pending();}
    bool dsp_work_pending(ApuDspKind kind)const{return dsp_dma_pending(kind) || (kind==ApuDspKind::Ep && ep_ack_mask_);}
    bool dsp_work_active()const{return gp_dma_working_ || ep_dma_working_;}
    bool service_dsp_acknowledgements(ApuDspKind kind,uint32_t quanta=1) {
        if(faulted_)return false;
        if(kind!=ApuDspKind::Ep)return true;
        if(dsp_work_active() || quanta>4096){device_fault("invalid EP acknowledgement work dispatch");return false;}
        ep_dma_working_=true;bool done=true;
        for(uint32_t i=0;i<quanta && ep_ack_mask_;++i)if(--ep_ack_quanta_==0) {
            const auto mask=ep_ack_mask_;ep_ack_mask_=0;
            done=ep_dma_.write(0xffffc5,mask&0x80u);
            if(done)ep_frame_status_&=~(mask&2u);
        }
        ep_dma_working_=false;return done && !faulted_;
    }
    bool service_dsp_dma(ApuDspKind kind,uint32_t quanta=1) {
        if(faulted_)return false;
        if(dsp_work_active()){device_fault("reentrant DSP DMA device work");return false;}
        if(kind==ApuDspKind::Ep && ep_ack_mask_ && ep_dma_.work_pending()) {
            // The CPU orders acknowledgement retirement before new transfer
            // work. An explicit caller cannot erase a newer EOL by reversing
            // that order with an old queued acknowledgement.
            device_fault("EP DMA dispatch overlaps pending acknowledgement");return false;
        }
        bool& working=kind==ApuDspKind::Gp?gp_dma_working_:ep_dma_working_;
        working=true;
        const bool done=kind==ApuDspKind::Gp?gp_dma_.service(quanta):ep_dma_.service(quanta);
        working=false;return done && !faulted_;
    }
    uint64_t ep_program_revision()const{return ep_program_revision_;}
    using DspResetControl=std::function<bool(uint32_t,uint32_t)>;
    bool bind_ep_program_owner(void* owner,ApuDspDma::ProgramWrite guard,std::function<bool()> frame_owner={},DspResetControl reset={}) {
        if(!owner || !guard || ep_program_owner_)return false;
        fe_context_known_=false;fe_state_used_=true;
        ep_program_owner_=owner;ep_program_guard_=std::move(guard);ep_frame_owner_=std::move(frame_owner);
        ep_reset_control_=std::move(reset);return true;
    }
    void unbind_ep_program_owner(void* owner) {
        if(ep_program_owner_==owner){
            if(dsp_work_pending(ApuDspKind::Ep) && !faulted_)device_fault("EP CPU owner removed with pending peripheral work");
            ep_program_owner_=nullptr;ep_program_guard_={};ep_frame_owner_={};ep_reset_control_={};
        }
    }
    bool gp_read(uint32_t address,uint32_t& value) {
        if(faulted_ || !gp_dma_.read(address,value))return false;
        if(address==0xffffc5 && gp_frame_pending_)value|=2;
        return true;
    }
    bool gp_write(uint32_t address,uint32_t value) {
        if(faulted_)return false;
        fe_context_known_=false;fe_state_used_=true;kernel_audio_startup_=false;
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
    bool bind_gp_program_owner(void* owner,ApuGpDma::ProgramWrite guard,DspResetControl reset={}) {
        if(!owner || !guard || gp_program_owner_)return false;
        fe_context_known_=false;fe_state_used_=true;
        gp_program_owner_=owner;gp_program_guard_=std::move(guard);gp_reset_control_=std::move(reset);return true;
    }
    void unbind_gp_program_owner(void* owner) {
        if(gp_program_owner_==owner){
            if(gp_dma_.work_pending() && !faulted_)device_fault("GP CPU owner removed with pending DMA");
            gp_program_owner_=nullptr;gp_program_guard_={};gp_reset_control_={};
        }
    }
    // Frame flags are vendor polled events, distinct from core IPR/vector state.
    // A successful real VP transaction owns MIX until that CPU actually yields.
private:
    friend class ApuDspProcessor;
    bool queue_ep_ack(uint32_t mask) {
        uint32_t status=0;
        if(faulted_ || !ep_read(0xffffc5,status))return false;
        mask&=status;
        if(!mask)return true;
        if(ep_ack_mask_){device_fault("overlapping EP acknowledgements remain unqualified");return false;}
        ep_ack_mask_=mask;
        // Physical EP retains the flag at the next instruction's read and
        // clears it by a later observation, even with no intervening C4/read.
        // Two explicit work quanta are a nominal ordering floor, not measured
        // bus latency. The sole CPU supplies retirement; register reads do not.
        ep_ack_quanta_=2;return true;
    }
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
        gp_frame_pending_=true;gp_host_requested_=false;return true;
    }
    bool gp_yield_frame(void* owner) {
        if(faulted_)return false;
        if(!owner || owner!=gp_frame_owner_) {
            device_fault("GP frame yield has no matching owner");return false;
        }
        gp_mix_lease_=false;gp_host_requested_=true;return true;
    }
public:
    bool gp_frame_outstanding()const{return gp_mix_lease_ || gp_frame_pending_;}
    // Explicit observation input for a fresh held context. Native startup does
    // not borrow the dashboard's EP status: its original all-ones clear
    // establishes the immediate CPU result without guessing a warm default.
    bool capture_ep_host_status(uint32_t value) {
        if(faulted_)return false;
        if(!dsp_reset_context_known_ || dsp_host_used_ ||
           reg32(kGpReset)!=0 || reg32(kEpReset)!=1 ||
           (value!=0 && value!=0x100) || !dsp_host_idle(true)) {
            device_fault("EP host status capture requires a fresh held observation");return false;
        }
        dsp_host_used_=true;set_reg32(0x5ff14,value);ep_host_status_known_=true;return true;
    }
    // Explicit pinned-kernel application handoff, separate from board reset
    // and the captured FE notification experiment. Original init/retirement
    // establishes SECTL/IEN; retained physical idle control is exactly1.
    // The control bit may be read/toggled only before processing starts.
    bool initialize_kernel_audio_handoff() {
        if(faulted_)return false;
        if(fe_state_used_ || frame_clock_primed_ || frame_ || voice_mix_quanta_ || !fe_work_idle()) {
            device_fault("kernel audio handoff requires a fresh device");return false;
        }
        set_reg32(kSectl,7);set_reg32(kIen,0xafa);set_reg32(kFeFenceControl,1);
        // Original idle retirement and retained pre-video/network observations
        // agree. These are initialized-kernel values, not cold-reset defaults.
        set_reg32(kGpReset,0);set_reg32(kEpReset,1);dsp_reset_context_known_=true;
        // Original4983 retirement80067442(1) writes all nine list pointers
        // to FFFF. Blinx17C2AD repeats these writes before allocating audio.
        // This initialized handoff must not inherit zero (voice0) from the
        // generic register-file reset, including the unused3D/MP lists.
        for(size_t i=0;i<kVoiceTop.size();++i) {
            set_reg32(kVoiceTop[i],kNullVoice);set_reg32(kVoiceCurrent[i],kNullVoice);
            set_reg32(kVoiceNext[i],kNullVoice);
        }
        // Recovered running-probe observations supply the unused GP output
        // base fields for this explicit warm profile. They are not universal
        // cold-reset defaults. The original driver writes FIFO0 itself.
        for(uint32_t offset:{0x3034u,0x3044u,0x3054u})set_reg32(offset,0);
        gp_output_base_known_=0x0e;
        // The same pre-video/network entry capture supplies the retained FE
        // status/trap fields. Original kernel init/retirement supplies108F;
        // the capture supplies70000/F00. This is not a board-reset default.
        set_reg32(kFectl,kObservedIdleFectl&~0x70000u);fectl_initialized_=true;
        // The warm kernel handoff does not establish a counter value. The
        // original title writes zero while stopped before enabling mode1.
        sample_counter_known_=false;
        kernel_audio_startup_=true;fe_state_used_=true;update_irq();return true;
    }
    // Restore only the independently observed running-probe FE context. This
    // is snapshot input, not a cold-reset default or an admission of title work.
    // A used device cannot discard its history by importing this idle floor.
    bool capture_fe_idle_context(uint32_t fectl,uint32_t sectl,uint32_t control,uint32_t gp_reset) {
        if(faulted_)return false;
        if(fe_context_known_ || fe_state_used_ || frame_clock_primed_ || frame_ || voice_mix_quanta_ ||
           !fe_work_idle() || fectl!=kObservedIdleFectl || sectl!=kObservedIdleSectl ||
           control!=1 || gp_reset!=0) {
            device_fault("FE captured idle context remains unqualified");return false;
        }
        set_reg32(kFectl,fectl);set_reg32(kSectl,sectl);
        set_reg32(kFeFenceControl,control);set_reg32(0x3fffc,gp_reset);
        sample_counter_known_=false;
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
        kernel_audio_startup_=false;
        if(faulted_)return false;
        if((bytes && !data) || (reg32(0x2040)&7) ||
           !gp_sge(0x2040,0x20d4,address,data,bytes,output)) {
            device_fault("GP owned scratch transfer failed");return false;
        }
        return true;
    }
    bool ep_scratch(uint32_t address,void* data,uint32_t bytes,bool output) {
        kernel_audio_startup_=false;
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
        if(size>0 && off<0x20284 && uint64_t(off)+unsigned(size)>0x20280) {
            device_fault("HRTF headroom method has no qualified register readback");return 0;
        }
        if(touches_zero_voice_control(off,size)) {
            device_fault("voice control method has no qualified register readback");return 0;
        }
        if(touches_hrtf_target(off,size)) {
            device_fault("HRTF target method has no qualified register readback");return 0;
        }
        fe_state_used_=true;
        if(fe_fence_working_) {device_fault("reentrant FE fence device work");return 0;}
        if(touches_gp_output_base(off,size)) {
            const int index=gp_output_base_index(off);
            if(size!=4 || index<0 || !(gp_output_base_known_&(1u<<index)) || reg32(kGpReset)==3) {
                device_fault("GP output base read requires known held configuration");return 0;
            }
            return reg32(off);
        }
        const int startup_register=ApuStartupMethods::register_index(off);
        if(startup_register>=0) {
            if(size!=4 || !kernel_audio_control_idle() || !(startup_method_known_&(1u<<startup_register))) {
                device_fault("APU startup method result read remains unqualified");return 0;
            }
            return reg32(off);
        }
        if(ApuStartupMappings::touches(off,size)) {
            if(size!=4 || !kernel_audio_control_idle() || !startup_mappings_.known(off)) {
                device_fault("APU startup mapping read context/width remains unqualified");return 0;
            }
            return reg32(off);
        }
        if(ApuStartupConfiguration::touches(off,size)) {
            if(size!=4 || !kernel_audio_control_idle() || !startup_configuration_.known(off)) {
                device_fault("APU startup configuration read context/width remains unqualified");return 0;
            }
            return reg32(off); // one canonical register owner, including future active updates
        }
        if(off==kFectl && size==4 && dsp_reset_context_known_) {
            if(!fectl_initialized_ || !kernel_audio_control_idle()) {
                char diagnostic[192];
                std::snprintf(diagnostic,sizeof(diagnostic),
                    "original FE control/status remains unqualified: known=%u startup=%u fectl=%08x sectl=%08x gp=%x ep=%x",
                    unsigned(fectl_initialized_),unsigned(kernel_audio_startup_),reg32(kFectl),reg32(kSectl),reg32(kGpReset),reg32(kEpReset));
                device_fault(diagnostic);return 0;
            }
            // The original88/8/100F writes physically read70088/70008/7100F
            // with no active SE/DSP work. Do not export these empty-state bits
            // as an acknowledgement of unqualified active work.
            return (reg32(kFectl)&~0x70000u)|0x70000u;
        }
        if(touches_sample_counter(off,size)) {
            if(off!=kXgscnt || size!=4 || !sample_counter_known_) {
                device_fault("APU sample counter read context/width remains unqualified");return 0;
            }
            return sample_counter_.value();
        }
        if(touches_dsp_reset(off,size)) {
            if(size==4 && (off==kGpReset || off==kEpReset) && dsp_reset_context_known_)return reg32(off);
            device_fault("DSP reset read context/width remains unqualified");return 0;
        }
        if(ApuDspHostRegisters::touches(off,size))return dsp_host_read(off,size);
        if(touches_fe_fence(off,size)) {
            if(off==kFeFenceControl && size==4 && (kernel_audio_control_idle() || fe_context_idle()))return reg32(off);
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
        if(size>0 && off<0x20284 && uint64_t(off)+unsigned(size)>0x20280 &&
           (off!=0x20280 || size!=4 || value>7)) {
            device_fault("HRTF headroom requires the original masked DWORD amount");return;
        }
        if(touches_zero_voice_control(off,size) &&
           (size!=4 || (off!=0x20350 && off!=0x20370) || value)) {
            device_fault("voice control publication is qualified only for the original zero DWORD");return;
        }
        if(touches_hrtf_target(off,size) &&
           (off!=0x2031c || size!=4 || value!=0xffff || reg32(kFecv)<64)) {
            device_fault("HRTF target publication requires the original ordinary null DWORD");return;
        }
        fe_state_used_=true;
        if(fe_fence_working_) {device_fault("reentrant FE fence device work");return;}
        if(touches_gp_output_base(off,size)) {
            const int index=gp_output_base_index(off);
            if(size!=4 || index<0 || (value&0xff0000ffu)) {
                device_fault("GP output base requires an aligned low24 DWORD");return;
            }
            gp_output_base_known_|=1u<<index;
            if(index==0)startup_output_base_known_=false;
        }
        if(size>0 && off<0x3030 && uint64_t(off)+unsigned(size)>0x3028)startup_output_base_known_=false;
        if(ApuStartupMethods::touches(off,size)) {
            startup_method(off,size,value);return;
        }
        if(ApuStartupMappings::touches(off,size)) {
            if(size!=4 || !kernel_audio_control_idle() || !startup_mappings_.write(off,value)) {
                device_fault("APU startup mapping requires stopped aligned physical allocation");return;
            }
            set_reg32(off,value);fe_context_known_=false;return;
        }
        if(ApuStartupConfiguration::touches(off,size)) {
            if(size!=4 || !kernel_audio_control_idle() ||
               (reg32(kFectl)&kFectlMethodMode)!=0x80 ||
               !startup_configuration_.write(off,value)) {
                device_fault("APU configuration requires original value in stopped halted startup");return;
            }
            set_reg32(off,value);fe_context_known_=false;return;
        }
        if(touches_sample_counter(off,size)) {
            if(off!=kXgscnt || size!=4 || value || (reg32(kSectl)&0x18) || fe_fence_pending_) {
                device_fault("APU sample counter write requires stopped DWORD zero");return;
            }
            sample_counter_.clear();sample_counter_known_=true;fe_context_known_=false;return;
        }
        if(touches_dsp_reset(off,size)) {
            if(size!=4 || (off!=kGpReset && off!=kEpReset) || !dsp_reset_context_known_ ||
               (value!=1 && value!=3 && !(off==kGpReset && value==0)) || fe_fence_pending_) {
                device_fault("DSP reset write context/value/width remains unqualified");return;
            }
            auto& reset=off==kGpReset?gp_reset_control_:ep_reset_control_;
            if(!reset || !reset(reg32(off),value)) {
                device_fault("DSP reset requires its available sole CPU owner");return;
            }
            // Publish control only after the CPU accepted the transition and
            // a release fetched all actual bootstrap bytes. Holds do no DMA.
            set_reg32(off,value);fe_context_known_=false;
            if(off==kGpReset && value==0) {
                gp_host_requested_=false;set_reg32(0x3ff14,0);
            }
            if(off==kEpReset)ep_host_status_known_=false;
            if(value==3)kernel_audio_startup_=false;
            return;
        }
        if(ApuDspHostRegisters::touches(off,size)) {dsp_host_write(off,size,value);return;}
        // Guard the device itself, including partial/overlapping transactions;
        // reference-wrapper whitelists cannot establish unknown fence behavior.
        if(touches_fe_fence(off,size)) {
            // Knowing the startup control register supplies no notification
            // address/data admission or completion of pending work.
            if(off==kFeFenceControl && size==4 && value<=1 && kernel_audio_control_idle()) {
                set_reg32(off,value);return;
            }
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
        const uint64_t write_end=uint64_t(off)+unsigned(size);
        if(off>=kVpBase ||
           (off<kSectl+4 && write_end>kSectl && (off!=kSectl || size!=4 || (value&0x18))) ||
           (off<kFectl+4 && write_end>kFectl &&
            (off!=kFectl || size!=4 || (value&kFectlMethodMode)==kFectlMethodTrapped)))
            kernel_audio_startup_=false;
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
            if(method==0x280) {
                hrtf_headroom_=uint8_t(value);hrtf_headroom_known_=true;
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
        if(off==kFectl && size==4 && dsp_reset_context_known_) {
            value&=~0x70000u;
            const bool idle_mode=(value&kFectlMethodMode)==0 || (value&kFectlMethodMode)==0x80;
            const bool preserves=fectl_initialized_ && idle_mode &&
                ((value^reg32(kFectl))&~kFectlMethodMode)==0;
            fectl_initialized_=value==0x88 || value==8 || value==0x100f || preserves;
        }
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
    bool dsp_host_idle(bool ep)const {
        return dsp_reset_context_known_ && reg32(ep?kEpReset:kGpReset)!=3 &&
               !(reg32(kSectl)&0x18) && !fe_fence_pending_ && !dsp_work_active() &&
               !dsp_work_pending(ep?ApuDspKind::Ep:ApuDspKind::Gp) &&
               !(ep?ep_counter_profile_touched_:gp_counter_profile_touched_) &&
               !(ep?ep_control_written_:gp_frame_outstanding());
    }
    uint32_t dsp_host_read(uint32_t off,int size) {
        const bool ep=off>=0x50000;
        if(size!=4 || !ApuDspHostRegisters::contains(off) || !dsp_reset_context_known_) {
            device_fault("DSP host register read context/width remains unqualified");return 0;
        }
        dsp_host_used_=true;
        // Requests belong to the existing frame/event owner. Polling this
        // view never executes a DSP or advances an event/acknowledgement.
        if((off&0xffff)==0xff10)return ep?ep_frame_requested_:gp_host_requested_;
        if(!dsp_host_idle(ep) || (off==0x5ff14 && !ep_host_status_known_)) {
            device_fault("DSP host status/zero-field read requires known held state");return 0;
        }
        return reg32(off);
    }
    void dsp_host_write(uint32_t off,int size,uint32_t value) {
        const bool ep=off>=0x50000;
        if(size!=4 || !ApuDspHostRegisters::permits(off,value) || !dsp_host_idle(ep)) {
            device_fault("DSP host register write requires supported held startup state");return;
        }
        // CPU host control is not an alias of every DSP peripheral. In
        // particular CPU FF14 and DSP C5 have independently observed views.
        if(off==0x5ff5c) {
            uint32_t config=0;
            if(!ep_dma_.read(0xffffd7,config) || config) {
                device_fault("EP host zero requires unused DMA configuration");return;
            }
        }
        dsp_host_used_=true;fe_context_known_=false;
        if((off&0xffff)==0xff10) {
            if(value&1u)(ep?ep_frame_requested_:gp_host_requested_)=true;
            return; // A zero store does not withdraw an observed request.
        }
        if(off==0x5ff14) {
            set_reg32(off,0);ep_host_status_known_=true;return;
        }
        if(off==0x3ff14) {set_reg32(off,reg32(off)&~value);return;}
        set_reg32(off,0);
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
    static constexpr uint32_t kGpReset=0x3fffc,kEpReset=0x5fffc;
    static bool touches_dsp_reset(uint32_t off,int size) {
        if(size<=0)return false;
        const uint64_t end=uint64_t(off)+unsigned(size);
        return (off<kGpReset+4 && end>kGpReset) || (off<kEpReset+4 && end>kEpReset);
    }
    static bool touches_sample_counter(uint32_t off,int size) {
        return size>0 && off<kXgscnt+4 && uint64_t(off)+unsigned(size)>kXgscnt;
    }
    static constexpr uint32_t kObservedIdleFectl=0x00071f8f, kObservedIdleSectl=7;
    static int gp_output_base_index(uint32_t off) {
        return off>=0x3024 && off<=0x3054 && (off&15)==4?int((off-0x3024)/16):-1;
    }
    static bool touches_gp_output_base(uint32_t off,int size) {
        if(size<=0)return false;
        for(uint32_t base:{0x3024u,0x3034u,0x3044u,0x3054u})
            if(off<base+4 && uint64_t(off)+unsigned(size)>base)return true;
        return false;
    }
    void startup_method(uint32_t off,int size,uint32_t value) {
        if(size!=4 || (off&3) || !kernel_audio_control_idle() ||
           reg32(kGpReset)!=0 || reg32(kEpReset)!=1) {
            device_fault("APU startup method requires stopped SE and held DSPs");return;
        }
        fe_context_known_=false;
        if(off==0x1148) {
            if(value!=1){device_fault("APU output SGE limit remains unqualified");return;}
            set_reg32(off,value);startup_method_known_|=1u<<6;return;
        }
        // Physical control1 holds a decoded804 and queues808; clearing it
        // releases the real table write. Pending/active work stays guarded
        // until it has a shared command-service owner. The original startup
        // clears this control before emitting its methods.
        if((reg32(kFectl)&kFectlMethodMode) || reg32(kFeFenceControl)!=0) {
            device_fault("APU startup method held by FE control; queued work remains unqualified");return;
        }
        const uint32_t method=off-kVpBase;
        for(const auto& field:ApuStartupMethods::fields)if(method==field.method) {
            if(value!=0xfff){device_fault("APU non-null startup field consumer remains unqualified");return;}
            set_reg32(field.offset,value);
            startup_method_known_|=1u<<ApuStartupMethods::register_index(field.offset);
            set_reg32(kFedecmeth,method);set_reg32(kFedecparam,value);return;
        }
        if(method==0x2c0) {
            if(value&~0x1f1f1f1fu){device_fault("APU HRTF submix encoding remains unqualified");return;}
            set_reg32(0x20ac,value);startup_method_known_|=1u<<5;
        }else if(method==0x1000) {
            if(value!=0 && value!=0x100) {
                device_fault("APU output base method value remains unqualified");return;
            }
            // Physical stopped-core comparison: this command replaces all
            // three FIFO0 fields, even when a larger end was set previously.
            set_reg32(0x3024,value);set_reg32(0x3028,value+0x100);set_reg32(0x302c,value);
            gp_output_base_known_|=1;startup_output_base_known_=true;
        }else if(method==0x1004) {
            if(value!=0x2000 || !startup_output_base_known_ || reg32(0x3024)!=0 || reg32(0x302c)!=0) {
                device_fault("APU output length method requires observed original base0/length2000");return;
            }
            // The original pair restores end2000. A separate length2100
            // comparison trapped the physical FE; do not silently accept it.
            set_reg32(0x3028,0x2000);
        }else if(method==0x804 || method==0x1800) {
            const bool input=method==0x804;
            if((input && (!startup_configuration_.known(0x1138) || value>reg32(0x1138))) ||
               (!input && (!(startup_method_known_&(1u<<6)) || value>reg32(0x1148)))) {
                device_fault("APU startup SGE selector exceeds its qualified limit");return;
            }
            (input?startup_input_sge_:startup_output_sge_)=value;
            (input?startup_input_sge_known_:startup_output_sge_known_)=true;
        }else if(method==0x808 || method==0x1808) {
            const bool input=method==0x808;
            const uint32_t base=reg32(input?0x2030:0x2044);
            const uint32_t slot=input?startup_input_sge_:startup_output_sge_;
            if(!(input?startup_input_sge_known_:startup_output_sge_known_) ||
               (value&0xfff) || value>0x03fff000 || !base || (base&0x3fff) ||
               uint64_t(base)+uint64_t(slot)*8+8>0x04000000) {
                device_fault("APU startup SGE mapping/selection remains unqualified");return;
            }
            // Physical observation distinguishes VPSGE input from GPF output.
            // Preserve descriptor word1; only the hardware-owned pointer moves.
            const std::array<uint8_t,4> bytes={uint8_t(value),uint8_t(value>>8),uint8_t(value>>16),uint8_t(value>>24)};
            if(!mem_write_ || !mem_write_(base+slot*8,bytes.data(),bytes.size())) {
                device_fault("APU startup SGE publication requires owned writable RAM");return;
            }
        }else {device_fault("unknown APU startup method");return;}
        set_reg32(kFedecmeth,method);set_reg32(kFedecparam,value);
    }
    uint32_t startup_method_known_=0,startup_input_sge_=0,startup_output_sge_=0;
    uint8_t gp_output_base_known_=0;
    bool startup_output_base_known_=false;
    bool startup_input_sge_known_=false,startup_output_sge_known_=false;
    bool kernel_audio_control_idle()const {
        return kernel_audio_startup_ && !(reg32(kSectl)&0x18) && !(reg32(kIsts)&kIstsFet) &&
               !voice_mix_quanta_ && !processing_quantum_ && !gp_mix_admitting_ &&
               !gp_frame_outstanding() && !dsp_work_active() && !gp_dma_.work_pending() &&
               !dsp_work_pending(ApuDspKind::Ep) && !fe_fence_pending_ &&
               !gp_program_revision_ && !ep_program_revision_;
    }
    bool fe_work_idle()const {
        return !processing_quantum_ && !gp_mix_admitting_ && !gp_frame_outstanding() &&
               !dsp_work_active() && !gp_dma_.work_pending() && !dsp_work_pending(ApuDspKind::Ep) &&
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
    static bool touches_zero_voice_control(uint32_t off,int size) {
        return size>0 && ((off<0x20354 && uint64_t(off)+unsigned(size)>0x20350) ||
                         (off<0x20374 && uint64_t(off)+unsigned(size)>0x20370));
    }
    static bool touches_hrtf_target(uint32_t off,int size) {
        return size>0 && off<0x20320 && uint64_t(off)+unsigned(size)>0x2031c;
    }
    bool configure_voice(uint32_t method, uint32_t value) {
        struct Field { uint32_t method, offset, mask; };
        static constexpr Field fields[] = {
            {0x300,0x00,0xffffffff}, {0x304,0x04,0xffffffff},
            {0x308,0x08,0xffffffff}, {0x30c,0x0c,0xffffffff},
            {0x310,0x10,0xffffffff}, {0x314,0x14,0xffffffff},
            {0x318,0x18,0xffffffff},
            // Ordinary null target: physical zero->FFFF observation and the
            // reference's low16-bit field. Spatial processing stays guarded.
            {0x31c,0x1c,0x0000ffff},
            // Owned, stopped hardware observations establish these zero
            // publications. Nonzero values and their processing stay guarded.
            {0x350,0x50,0xffffffff}, {0x370,0x70,0xffffffff},
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
        if((reg32(kSectl)&0x18)!=8) {
            device_fault("active APU sequencer modes2/3 remain unqualified");return;
        }
        // The absolute cursor also advances while stopped. Only real active
        // processing (or the existing active register/CPU paths) revokes init.
        kernel_audio_startup_=false;

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
        // The existing absolute setup-engine clock owns EP event production.
        // Hardware produces102 at 256/48000s with GP held and empty voices.
        // C4 requests the next event; delayed C5 acknowledgement clears the
        // previous flag independently while retaining background bit8.
        if(ep_frame_requested_ && ep_program_owner_ && ep_frame_owner_ && ep_frame_owner_()) {
            if(!ep_control_written_) {
                device_fault("CPU-only EP frame request remains unqualified");return;
            }
            if(dsp_work_active() || dsp_work_pending(ApuDspKind::Ep)) {
                device_fault("EP frame publication overlaps unqualified DMA work");return;
            }
            ep_frame_requested_=false;ep_frame_status_=0x102;++ep_frame_events_;
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
    bool kernel_audio_startup_=false;
    bool fe_fence_pending_=false,fe_fence_working_=false;
    uint64_t fe_fences_published_=0;
    uint64_t frame_ = 0;
    bool frame_clock_primed_=false;
    uint64_t voice_on_methods_ = 0;
    uint64_t voice_off_methods_ = 0;
    uint64_t voice_release_methods_ = 0;
    uint64_t idle_voice_traps_ = 0;
    ApuSampleCounter sample_counter_;
    bool sample_counter_known_=true; // fresh device; warm imports revoke this
    ApuStartupConfiguration startup_configuration_;
    ApuStartupMappings startup_mappings_;
    bool fectl_initialized_=false;
    std::array<bool,kMaxVoices> voice_locked_{};
    std::array<std::shared_ptr<ApuRateState>,kMaxVoices> voice_rates_{};
    std::array<std::array<apu_envelope_curve::ReleaseHistory,2>,kMaxVoices> voice_envelopes_{};
    std::array<uint8_t,32> submix_headroom_{};
    uint8_t hrtf_headroom_=0;
    bool hrtf_headroom_known_=false;
    std::array<uint32_t,0x3800> gp_words_{};
    friend class ApuDspProcessor;
    // Retained across CPU detach/rebind. No qualified warm-reset profile exists.
    bool gp_counter_profile_touched_=false,ep_counter_profile_touched_=false;
    bool ep_control_written_=false;
    bool gp_host_requested_=false,dsp_host_used_=false,ep_host_status_known_=false;
    uint32_t ep_control_value_=0;
    bool ep_frame_requested_=false;
    uint32_t ep_frame_status_=0;
    uint32_t ep_ack_mask_=0,ep_ack_quanta_=0;
    uint64_t ep_frame_events_=0;
    uint64_t gp_program_revision_=0;
    void* gp_program_owner_=nullptr;
    ApuGpDma::ProgramWrite gp_program_guard_;
    DspResetControl gp_reset_control_,ep_reset_control_;
    bool dsp_reset_context_known_=false;
    void* gp_frame_owner_=nullptr;
    std::function<bool()> gp_startup_clear_;
    bool gp_mix_lease_=false,gp_frame_pending_=false,gp_mix_admitting_=false;
    ApuGpDma gp_dma_;
    std::unique_ptr<std::array<uint32_t,0x3800>> ep_words_;
    std::unique_ptr<const std::array<uint32_t,2048>> ep_y_lookup_;
    uint64_t ep_program_revision_=0;
    void* ep_program_owner_=nullptr;
    std::function<bool()> ep_frame_owner_;
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
