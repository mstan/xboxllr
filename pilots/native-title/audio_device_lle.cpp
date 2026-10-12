/* Original DirectSound's shared device owner. This connects qualified storage,
 * transactions and codec consumption, not unknown hardware DSP boot/wake. */
#include "audio_device_lle.h"
extern "C" {
#include "io_device_lle.h"
#include "usb_lle.h"
}
#include "machine/apu_gp_processor.hpp"
#include "machine/apu_ep_processor.hpp"
#include "machine/aci.hpp"
#include <memory>
#include <string>
#include <new>
#include <limits>

namespace {
std::string failure;
const char *fail(const char *why) {
    if(failure.empty())failure=why;
    return failure.c_str();
}
struct Owner {
    xlle::McpxApu apu{[](const std::string& why){fail(why.c_str());}};
    xlle::McpxAci aci{[](const std::string& why){fail(why.c_str());}};
    std::unique_ptr<xlle::ApuGpProcessor> gp;
    std::unique_ptr<xlle::ApuEpProcessor> ep;
    PilotAudioDevicePcm pcm=nullptr;
    void *context=nullptr;
    uint64_t ticks=0,reads=0,writes=0;
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
 * RegFileMmio's blanket zero/last-written fallback. Hardware reset/core/wake,
 * active FE fence and unknown registers fail before entering either owner. */
bool apu_admitted(uint32_t off,unsigned size,int write) {
    if(size!=4 || (off&3))return false;
    if(!write) {
        if(off>=0x2054 && off<=0x2074)return true;
        switch(off) {
        case 0x200c:case 0x1000:case 0x1100:case 0x1110:case 0x1300:case 0x1304:
        case 0x20010:return true;
        default:return false;
        }
    }
    // Original startup composition already qualifies these physical
    // scratch/SG/FIFO mappings. Mapping storage never releases held DSPs.
    if(((off>=0x3024 && off<=0x307c) || (off>=0x4024 && off<=0x407c)) &&
       (off&15))return true; // lane +4/+8/+C only; +0 is not a register
    if(off>=0x20200 && off<=0x2027c)return true;
    switch(off) {
    case 0x2040:case 0x2044:case 0x2048:case 0x204c:
    case 0x20d4:case 0x20d8:case 0x20dc:case 0x20e0:
    case 0x202c:case 0x2030:case 0x115c:case 0x1000:case 0x1004:
    case 0x1100:case 0x1504:case 0x2000:
    case 0x2054:case 0x2060:case 0x206c:
    case 0x2058:case 0x205c:case 0x2064:case 0x2068:case 0x2070:case 0x2074:
    case 0x20120:case 0x20124:case 0x20128:case 0x20140:
    case 0x202f8:case 0x202fc:
    case 0x20300:case 0x20304:case 0x20308:case 0x2030c:
    case 0x20310:case 0x20314:case 0x20318:
    case 0x20360:case 0x20364:case 0x20368:case 0x2036c:
    case 0x20374:case 0x20378:case 0x2037c:
    case 0x203a0:case 0x203a4:case 0x203d8:case 0x203dc:return true;
    default:return false;
    }
}
bool aci_admitted(uint32_t off,unsigned size,int write,uint32_t value) {
    if(off==0x2c)return size==2 && (!write || value==48000);
    if(!write) {
        switch(off) {
        case 0x110:case 0x130:return size==4;
        case 0x114:case 0x115:case 0x11a:case 0x11b:return size==1;
        case 0x116:case 0x118:return size==2;
        default:return false;
        }
    }
    switch(off) {
    case 0x110:return size==4 && !(value&3);
    case 0x115:return size==1 && value<32;
    case 0x116:return size==2 && !(value&~0x1cu);
    case 0x11b:return size==1 && !(value&~0x1fu);
    default:return false;
    }
}
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
    uint64_t ticks=0,next=0;
    const char *error=pilot_usb_pit_clock(&ticks,&next);
    if(error)return fail(error);
    owner.reset(new(std::nothrow) Owner);
    if(!owner)return fail("original audio device allocation failed");
    owner->pcm=pcm;owner->context=context;owner->ticks=ticks;
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
    const bool apu=address>=xlle::McpxApu::kBase && address<xlle::McpxApu::kBase+xlle::McpxApu::kSize;
    const bool aci=address>=xlle::McpxAci::kBase && address<xlle::McpxAci::kBase+xlle::McpxAci::kSize;
    if((!apu && !aci) || (apu && !apu_admitted(address-xlle::McpxApu::kBase,size,write)) ||
       (aci && !aci_admitted(address-xlle::McpxAci::kBase,size,write,*value)))
        return fail("original audio device access outside qualified semantics (reset/wake/FE context remain guarded)");
    if(aci && write && address==xlle::McpxAci::kBase+0x11b && (*value&1) && !(*value&2) && !owner->pcm)
        return fail("original audio device requires a real PCM consumer");
    Busy busy;
    if(apu) {
        if(write)owner->apu.mmio_write(address,int(size),*value);
        else *value=owner->apu.mmio_read(address,int(size));
    } else {
        if(write)owner->aci.mmio_write(address,int(size),*value);
        else *value=owner->aci.mmio_read(address,int(size));
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
    Busy busy;owner->ticks=ticks;owner->apu.set_time(ticks,1125000);
    // Only the independently qualified, guest-configured codec consumes here.
    // Held DSPs and VP sources are never started by periodic observer work.
    owner->aci.set_frame(ticks/6000);
    return pilot_audio_device_lle_error();
}
extern "C" void pilot_audio_device_lle_state(PilotAudioDeviceState *state) {
    if(!state)return;
    *state={};if(!owner)return;
    state->owned=1;state->apu_mapping=owner->apu_mapped;state->aci_mapping=owner->aci_mapped;state->work_binding=owner->work;
    state->clock_ticks=owner->ticks;state->dma_reads=owner->reads;state->dma_writes=owner->writes;
    state->pcm_words=owner->aci.consumed_pcm_words();state->descriptors=owner->aci.completed_descriptors();
    state->gp_held=owner->gp && owner->gp->phase()==xlle::ApuGpPhase::Held;
    state->ep_held=owner->ep && owner->ep->phase()==xlle::ApuEpPhase::Held;
}
