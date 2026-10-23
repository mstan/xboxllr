// SPDX-License-Identifier: GPL-3.0-or-later
#include "boot_audio_apu.h"
#include <utility>

BootApuVoiceAdapter::BootApuVoiceAdapter(xlle::McpxApu &apu,
        const std::array<uint16_t,XB_AUDIO_CHANNELS>& handles,Resolve source,ReadWord read)
    :apu_(apu),handles_(handles),source_(std::move(source)),read_(std::move(read)) {
    if(!source_ || !read_)fail("Native boot VP requires source/RAM ownership callbacks");
}
bool BootApuVoiceAdapter::fail(const char *message) {
    if(error_.empty())error_=message;
    return false;
}
bool BootApuVoiceAdapter::method(uint32_t address,uint32_t value) {
    if(!error_.empty() || apu_.faulted())return fail("Native boot VP is faulted");
    apu_.mmio_write(xlle::McpxApu::kBase+0x20000+address,4,value);
    return !apu_.faulted() || fail("Shared native APU rejected original boot method");
}
bool BootApuVoiceAdapter::submit(const xb_audio_native_event &event) {
    if(!error_.empty() || apu_.faulted())return fail("Native boot VP is faulted");
    const unsigned channel=event.call.channel;
    if(channel>=XB_AUDIO_CHANNELS || event.call.fn>XB_CALL_B2)
        return fail("Invalid native boot voice event");
    if(!event.voice.alloc)return true; // Original pre-allocation level/filter only changes library state.
    const uint16_t handle=handles_[channel];
    if(handle<64 || handle>=256)return fail("Native boot voice lacks an owned ordinary handle");
    for(unsigned other=0;other<XB_AUDIO_CHANNELS;++other)
        if(other!=channel && (allocated_>>other&1u) && handles_[other]==handle)
            return fail("Native boot channels alias an active handle");
    Source source;
    if(event.call.fn==XB_CALL_SAMPLE) {
        if(event.table>=XB_TABLES || !event.frames || event.frames!=event.voice.ebo+1 ||
           !source_(event.table,event.frames,source) || source.frames<event.frames ||
           uint64_t(source.byte_offset)+uint64_t(event.frames)*2>0x1000000)
            return fail("Native boot sample has no actual bounded source mapping");
        uint32_t state=0;
        if(!read_(handle,0x54,state))return fail("Native boot sample owner cannot read voice state");
        if(state&(1u<<21))return fail("Native boot active sample replacement requires original stop ownership");
    } else if(!(allocated_>>channel&1u))
        return fail("Native boot call precedes native sample binding");
    if(!method(0x2f8,handle) || !method(0x2fc,1))return false;
    const xb_voice &v=event.voice;
    bool ok=true;
    switch(event.call.fn) {
    case XB_CALL_SAMPLE:
        ok=method(0x300,v.vbin) && method(0x304,v.fmt) && method(0x318,v.misc) &&
           method(0x308,v.env0) && method(0x30c,v.enva) && method(0x310,v.env1) &&
           method(0x314,v.envf) && method(0x36c,v.lfo_env) && method(0x374,v.fca) &&
           method(0x378,v.fca) && method(0x360,v.vola) && method(0x364,v.volb) &&
           method(0x368,v.volc) && method(0x37c,v.pitch) && method(0x31c,0xffff) &&
           method(0x3a0,source.byte_offset) && method(0x3a4,0) && method(0x3dc,v.ebo);
        if(ok)allocated_|=1u<<channel;
        break;
    case XB_CALL_LEVEL:
        ok=method(0x360,v.vola) && method(0x364,v.volb) && method(0x368,v.volc);
        break;
    case XB_CALL_FILTER:
        ok=method(0x318,v.misc) && method(0x374,v.fca) && method(0x378,v.fca);
        break;
    case XB_CALL_NOTE:
    case XB_CALL_PITCH:
        ok=method(0x37c,v.pitch);
        if(ok && event.call.fn==XB_CALL_NOTE) {
            ok=method(0x304,v.fmt) && method(0x3a4,0) && method(0x3dc,v.ebo);
            if(ok && event.call.effect)
                ok=method(0x120,0x1ffff) && method(0x124,event.start|handle) && method(0x140,handle);
        }
        break;
    case XB_CALL_STOP:
        if(event.call.effect)ok=method(0x12c,handle);
        break;
    case XB_CALL_B2:
        break; // Original sequencer byte has no hardware method of its own.
    }
    // A failure is terminal and retains exact device effects; no fabricated
    // unlock/rollback or success is issued after an actual method declined.
    return ok && method(0x2fc,0);
}
bool BootApuVoiceAdapter::feedback(uint32_t acknowledged,uint32_t &active,uint32_t &idle) {
    if(!error_.empty() || apu_.faulted())return fail("Native boot VP is faulted");
    if(acknowledged&~allocated_)return fail("Native boot idle acknowledgement has no allocated owner");
    uint32_t snapshot=0;
    for(unsigned c=0;c<XB_AUDIO_CHANNELS;++c)if(allocated_>>c&1u) {
        uint32_t state=0;
        if(!read_(handles_[c],0x54,state))return fail("Native boot feedback requires actual voice RAM");
        if(state&(1u<<21))snapshot|=1u<<c;
    }
    if(snapshot&acknowledged)return fail("Native boot active voice cannot be acknowledged idle");
    active=snapshot;idle=acknowledged;
    return true;
}
BootApuVoiceAdapter::Status BootApuVoiceAdapter::quantum(Bins &bins) {
    if(!error_.empty() || apu_.faulted()) {fail("Native boot VP is faulted");return Status::Failed;}
    xlle::ApuVoiceQuantum frame;
    if(!apu_.process_2d_quantum(frame)) {fail("Shared native boot VP processing declined");return Status::Failed;}
    if(!frame.published)return Status::NeedsIdleService;
    Bins produced{};
    constexpr unsigned selected[]={0,1,11};
    for(unsigned b=0;b<XB_AUDIO_BINS;++b)for(unsigned i=0;i<XB_AUDIO_BLOCK;++i) {
        const uint32_t *word=apu_.gp_word(0x1400+selected[b]*XB_AUDIO_BLOCK+i);
        if(!word) {fail("Native boot VP mix publication unavailable");return Status::Failed;}
        produced[b][i]=int32_t(*word&0xffffff)-((*word&0x800000)?0x1000000:0);
    }
    bins=produced;
    return Status::Produced;
}
