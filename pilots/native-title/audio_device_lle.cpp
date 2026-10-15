#include "audio_device_apu_contract.hpp"
#include "audio_device_aci_contract.hpp"
/* Original DirectSound's shared device owner. This connects qualified storage,
 * transactions, reset/bootstrap, stopped DSP execution and codec consumption. Active DSP cadence and
 * active FE ordering remain separate qualification work. */
#include "audio_device_lle.h"
extern "C" {
#include "io_device_lle.h"
#include "usb_lle.h"
}
#include "machine/apu_gp_processor.hpp"
#include "machine/apu_ep_processor.hpp"
#include "machine/apu_dsp_clock.hpp"
#include "machine/aci.hpp"
#include <memory>
#include <string>
#include <new>
#include <limits>
#include <cstdio>

namespace {
std::string failure;
uint64_t next_lifetime=0;
const char *fail(const char *why) {
    if(failure.empty())failure=why;
    return failure.c_str();
}
struct Owner {
    xlle::McpxApu apu{[](const std::string& why){fail(why.c_str());}};
    xlle::McpxAci aci{[](const std::string& why){fail(why.c_str());}};
    std::unique_ptr<xlle::ApuGpProcessor> gp;
    std::unique_ptr<xlle::ApuEpProcessor> ep;
    std::unique_ptr<xlle::ApuDspClock> dsp_clock;
    PilotAudioDevicePcm pcm=nullptr;
    void *context=nullptr;
    uint64_t ticks=0,reads=0,writes=0;
    uint64_t lifetime=0,epoch=0;
    bool apu_mapped=false,aci_mapped=false,work=false,busy=false;
};
std::unique_ptr<Owner> owner;
bool dma(uint32_t address,void *buffer,size_t bytes,int write) {
    if(!owner || !bytes || bytes>std::numeric_limits<uint32_t>::max() ||
       uint64_t(address)+bytes>64u*1024*1024)return false;
    if(write)++owner->writes;else ++owner->reads;
    return pilot_io_device_contiguous_dma(address,buffer,uint32_t(bytes),write,PILOT_IO_DMA_SYNC) ||
           pilot_io_device_pinned_dma(address,buffer,uint32_t(bytes),write);
}
/* The strict original helper/source proofs qualify these transactions, not
 * RegFileMmio's blanket zero/last-written fallback. Reset ownership is checked
 * by the device; active FE and unknown registers retain their guards. */

struct Busy {
    Busy(){owner->busy=true;}
    ~Busy(){owner->busy=false;}
};
}
extern "C" const char *pilot_audio_device_lle_error(void) {
    return failure.empty()?nullptr:failure.c_str();
}
extern "C" const char *pilot_audio_device_lle_close(void) {
    if(!owner)return nullptr;
    if(owner->busy)return fail("original audio device teardown during device work");
    if((owner->gp && owner->gp->phase()!=xlle::ApuGpPhase::Held) ||
       (owner->ep && owner->ep->phase()!=xlle::ApuGpPhase::Held))
        return fail("original audio device teardown requires held DSP owners");
    // Idle CPU execution is not descriptor retirement. Require a stopped,
    // drained codec; unknown active/paused teardown retains its DMA owner.
    if((owner->aci.mmio_read(xlle::McpxAci::kBase+0x11b,1)&1) ||
       owner->aci.mmio_read(xlle::McpxAci::kBase+0x118,2))
        return fail("original audio device teardown with active or paused PCM descriptor");
    const char *error=nullptr;
    if(owner->work) {
        error=pilot_io_device_work_unbind(pilot_audio_device_lle_sample);
        if(error)return fail(error);owner->work=false;
    }
    if(owner->aci_mapped) {
        error=pilot_io_device_mmio_bind(xlle::McpxAci::kBase,xlle::McpxAci::kSize,nullptr);
        if(error)return fail(error);owner->aci_mapped=false;
    }
    if(owner->apu_mapped) {
        error=pilot_io_device_mmio_bind(xlle::McpxApu::kBase,xlle::McpxApu::kSize,nullptr);
        if(error)return fail(error);owner->apu_mapped=false;
    }
    // If an engine failure forbids unbinding, retain the objects above rather
    // than leave a registered callback pointing into destroyed storage.
    owner->aci.set_irq({},6);owner->apu.set_irq({},5);
    pilot_usb_pic_raise(5,0);pilot_usb_pic_raise(6,0);
    owner.reset();return nullptr;
}
extern "C" const char *pilot_audio_device_lle_open(PilotAudioDevicePcm pcm,void *context) {
    if(!failure.empty())return failure.c_str();
    if(owner)return fail("original audio device already owned");
    if(next_lifetime==UINT64_MAX)return fail("original audio device lifetime exhausted");
    uint64_t ticks=0,next=0;
    const char *error=pilot_usb_pit_clock(&ticks,&next);
    if(error)return fail(error);
    owner.reset(new(std::nothrow) Owner);
    if(!owner)return fail("original audio device allocation failed");
    owner->pcm=pcm;owner->context=context;owner->ticks=ticks;
    owner->lifetime=++next_lifetime;owner->epoch=ticks;
    // This owner begins after the pinned4983 kernel's initialized idle handoff,
    // not at board cold reset. Original654DC clears SECTL then enables bits0..2;
    // original67442 clears processing bits0x18 without clearing those enables.
    // The same calls initialize IEN=AFA with its global interrupt bit clear.
    // Executable source proof and retained pre-video/network entry agree.
    // Retained physical idle control is1. The device admits its startup RMW
    // separately from FE completion; processing permanently revokes that scope.
    // See native_audio_handoff_reference.py. Reset writes are routed to the
    // sole DSP owners below; release fetches their actual guest bootstrap.
    owner->apu.initialize_kernel_audio_handoff();
    owner->aci.initialize_kernel_audio_handoff();
    owner->aci.set_time(ticks,1125000);
    // Prime the stopped codec before publication. Device creation must not
    // consume any clock interval preceding its ownership.
    owner->aci.set_frame(ticks/6000);
    owner->apu.set_mem_access([](uint32_t a,void *p,size_t n){return dma(a,p,n,0);},
        [](uint32_t a,const void *p,size_t n){return dma(a,const_cast<void*>(p),n,1);});
    owner->aci.set_mem_access([](uint32_t a,void *p,size_t n){return dma(a,p,n,0);});
    owner->apu.set_irq([](int irq,bool level){pilot_usb_pic_raise(irq,level);},5);
    owner->aci.set_irq([](int irq,bool level){pilot_usb_pic_raise(irq,level);},6);
    owner->aci.set_pcm_output([](const uint8_t *bytes,size_t n){
        if(!owner->pcm){fail("original audio device requires a real PCM consumer");return false;}
        const bool accepted=owner->pcm(owner->context,bytes,n)!=0;
        if(!accepted)fail("original audio PCM consumer rejected actual bytes");
        return accepted && failure.empty();
    });
    owner->gp=std::make_unique<xlle::ApuGpProcessor>(owner->apu,[](const std::string& why){fail(why.c_str());});
    owner->ep=std::make_unique<xlle::ApuEpProcessor>(owner->apu,[](const std::string& why){fail(why.c_str());});
    owner->apu.set_time(ticks,1125000);
    owner->dsp_clock=std::make_unique<xlle::ApuDspClock>(owner->apu,*owner->gp,*owner->ep,ticks,1125000,
        [](const std::string& why){fail(why.c_str());});
    if(failure.empty()) {
        error=pilot_io_device_mmio_bind(xlle::McpxApu::kBase,xlle::McpxApu::kSize,pilot_audio_device_lle_access);
        if(error)fail(error);else owner->apu_mapped=true;
    }
    if(failure.empty()) {
        error=pilot_io_device_mmio_bind(xlle::McpxAci::kBase,xlle::McpxAci::kSize,pilot_audio_device_lle_access);
        if(error)fail(error);else owner->aci_mapped=true;
    }
    if(failure.empty()) {
        error=pilot_io_device_work_bind(pilot_audio_device_lle_sample);
        if(error)fail(error);else owner->work=true;
    }
    if(!failure.empty())pilot_audio_device_lle_close();
    return pilot_audio_device_lle_error();
}
extern "C" const char *pilot_audio_device_lle_access(uint32_t address,unsigned size,int write,uint32_t *value) {
    if(!failure.empty())return failure.c_str();
    if(!owner || !value || (write!=0 && write!=1))return fail("invalid original audio device transaction");
    if(owner->busy)return fail("original audio device callback reentry");
    // Settle elapsed guest time under the preceding device configuration.
    // A read at unchanged time supplies no instruction budget or completion.
    if(pilot_audio_device_lle_sample())return failure.c_str();
    const uint32_t supplied=write?*value:0; // A read's output need not be initialized.
    const bool apu=address>=xlle::McpxApu::kBase && address<xlle::McpxApu::kBase+xlle::McpxApu::kSize;
    const bool aci=address>=xlle::McpxAci::kBase && address<xlle::McpxAci::kBase+xlle::McpxAci::kSize;
    if((!apu && !aci) || (apu && !pilot_original_apu_admitted(address-xlle::McpxApu::kBase,size,write,supplied)) ||
       (aci && !pilot_original_aci_admitted(address-xlle::McpxAci::kBase,size,write!=0,supplied)))
    {
        char diagnostic[256];
        if(write)std::snprintf(diagnostic,sizeof(diagnostic),
            "original audio device access outside qualified semantics (reset/wake/FE context remain guarded): address=%08x width=%u write value=%08x",
            address,size,*value);
        else std::snprintf(diagnostic,sizeof(diagnostic),
            "original audio device access outside qualified semantics (reset/wake/FE context remain guarded): address=%08x width=%u read",
            address,size);
        return fail(diagnostic);
    }
    if(aci && write && address==xlle::McpxAci::kBase+0x11b && (*value&1) && !(*value&2) && !owner->pcm)
        return fail("original audio device requires a real PCM consumer");
    Busy busy;
    if(apu) {
        if(write)owner->apu.mmio_write(address,int(size),*value);
        else {uint32_t observed=owner->apu.mmio_read(address,int(size));if(failure.empty())*value=observed;}
    } else {
        if(write)owner->aci.mmio_write(address,int(size),*value);
        else {uint32_t observed=owner->aci.mmio_read(address,int(size));if(failure.empty())*value=observed;}
    }
    return pilot_audio_device_lle_error();
}
extern "C" const char *pilot_audio_device_lle_sample(void) {
    if(!failure.empty())return failure.c_str();
    if(!owner)return fail("original audio device work without owner");
    if(owner->busy)return fail("original audio device work reentry");
    uint64_t ticks=0,next=0;
    const char *error=pilot_usb_pit_clock(&ticks,&next);
    if(error)return fail(error);
    if(ticks<owner->ticks)return fail("original audio device clock moved backwards");
    const bool released=owner->gp->phase()!=xlle::ApuGpPhase::Held || owner->ep->phase()!=xlle::ApuGpPhase::Held;
    if(released && (owner->apu.mmio_read(xlle::McpxApu::kBase+0x2000,4)&0x18))
        return fail("original active DSP frame cadence remains unqualified");
    Busy busy;
    if(!owner->dsp_clock->advance(ticks))return failure.c_str();
    owner->ticks=ticks;
    owner->aci.set_time(ticks,1125000);
    // Only the independently qualified, guest-configured codec consumes here.
    // This stopped-sequencer stage supplies actual bootstrap/poll execution,
    // without publishing VP/GP frames or EP events. Active composition follows.
    owner->aci.set_frame(ticks/6000);
    return pilot_audio_device_lle_error();
}
extern "C" void pilot_audio_device_lle_state(PilotAudioDeviceState *state) {
    if(!state)return;
    *state={};if(!owner)return;
    state->owned=1;state->apu_mapping=owner->apu_mapped;state->aci_mapping=owner->aci_mapped;state->work_binding=owner->work;
    state->clock_ticks=owner->ticks;state->dma_reads=owner->reads;state->dma_writes=owner->writes;
    state->lifetime_id=owner->lifetime;state->epoch_ticks=owner->epoch;state->codec_frame=owner->aci.current_frame();
    state->pcm_words=owner->aci.consumed_pcm_words();state->descriptors=owner->aci.completed_descriptors();
    state->gp_held=owner->gp && owner->gp->phase()==xlle::ApuGpPhase::Held;
    state->ep_held=owner->ep && owner->ep->phase()==xlle::ApuEpPhase::Held;
    if(owner->dsp_clock) {
        state->gp_cycles=owner->dsp_clock->retired(false);state->ep_cycles=owner->dsp_clock->retired(true);
        state->gp_phase=unsigned(owner->gp->phase());state->ep_phase=unsigned(owner->ep->phase());
    }
}
extern "C" int pilot_audio_device_lle_consumer_attached(void *context) {
    return owner && owner->pcm && owner->context==context;
}
