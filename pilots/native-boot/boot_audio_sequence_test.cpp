// SPDX-License-Identifier: GPL-3.0-or-later
// Authored sequencer input, not an original firmware asset or golden waveform.
#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>
extern "C" {
#include "core_internal.h"
}

namespace {
void require(bool ok,const char *reason) { if(!ok)throw std::runtime_error(reason); }
struct Fixture {
    alignas(XB_ALIGN) xb_state state{};
    std::vector<uint8_t> audio,sequence=std::vector<uint8_t>(0x6f8),
        pointers=std::vector<uint8_t>(0x64),envelopes=std::vector<uint8_t>(0x230),
        gp=std::vector<uint8_t>(0x1894);
    uint8_t *sound;
    static void u16(std::vector<uint8_t>& data,unsigned at,unsigned value) {
        data.at(at)=uint8_t(value);data.at(at+1)=uint8_t(value>>8);
    }
    static void u32(std::vector<uint8_t>& data,unsigned at,unsigned value) {
        u16(data,at,value);u16(data,at+2,value>>16);
    }
    Fixture():audio(xb_audio_size()+XB_ALIGN),sound(reinterpret_cast<uint8_t *>(
        (reinterpret_cast<uintptr_t>(audio.data())+XB_ALIGN-1)&~uintptr_t(XB_ALIGN-1))) {
        // Only audio subsystem state is constructed; this is not a full boot
        // admission fixture or evidence of graphics/game coverage.
        state.magic=STATE_MAGIC;
        state.pack[PACK_SEQUENCE]={sequence.data(),0x8006e908u,unsigned(sequence.size())};
        state.pack[PACK_SEQ_PTRS]={pointers.data(),0x800a7bb0u,unsigned(pointers.size())};
        state.pack[PACK_ENVBLOCKS]={envelopes.data(),0x800a7980u,unsigned(envelopes.size())};
        state.pack[PACK_GP_PROGRAM]={gp.data(),0x80053568u,unsigned(gp.size())};
        // One authored looping sine descriptor, two tracks, four notes each.
        u32(pointers,0,0x8006eb18u);
        u32(sequence,0x210,0x80093b78u);u16(sequence,0x214,256);u16(sequence,0x21a,1);
        u32(sequence,0x21c,0x800a7980u);u32(sequence,0x220,0x800a79a8u);
        u32(envelopes,0,1);u32(envelopes,4,2);
        u32(pointers,0x60,1);u32(pointers,0x2c,0x8006ebf8u);u32(pointers,0x30,0x8006ec58u);
        u16(sequence,0x6e0,1);u16(sequence,0x6e2,3);u32(sequence,0x6e4,0x800a7bdcu);
        for(unsigned track:{0x2f0u,0x350u}) {
            unsigned at=track;
            u16(sequence,at,5);u16(sequence,at+2,0);at+=4;
            for(unsigned pitch:{60u,62u,64u,65u}) {
                u16(sequence,at,1);u16(sequence,at+2,pitch);u16(sequence,at+4,0);at+=6;
            }
            u16(sequence,at,15);
        }
        require(xb_audio_init(&state,sound,xb_audio_size())==XB_OK,"audio fixture init");
    }
    xb_audio_native_out tick(uint32_t active=0,uint32_t idle=0) {
        xb_audio_native_out out{};
        do { require(xb_audio_native_block(&state,active,idle,&out)==XB_OK,"native block");idle=0; }
        while(!out.tick);
        return out;
    }
};
}
int main() {
    Fixture f;
    xb_audio_native_out out{};
    xb_audio_state before{},after{};
    require(xb_get_audio(&f.state,&before)==XB_OK,"state before reject");
    require(xb_audio_native_block(&f.state,0,1,&out)==XB_ERR_ARG,"unallocated ack rejected");
    require(xb_audio_native_block(&f.state,1u<<16,0,&out)==XB_ERR_ARG,"bad mask rejected");
    require(xb_get_audio(&f.state,&after)==XB_OK && after.blocks==before.blocks,"reject must not advance");
    require(f.tick().tick==2,"initial original event beat");
    out=f.tick();
    require(out.tick==3 && out.n_events==2,"two sample calls");
    require(out.event[0].call.channel==1 && out.event[1].call.channel==0,"original front insertion order");
    for(unsigned i=0;i<2;++i) {
        require(out.event[i].call.fn==XB_CALL_SAMPLE && out.event[i].table==XB_TABLE_SINE,"sample source snapshot");
        require(out.event[i].frames==128 && !out.event[i].voice.on,"sample is not note completion");
    }
    out=f.tick();
    require(out.tick==4 && out.n_events==2 && out.library_on==3,"first notes");
    require(out.event[0].call.effect==1 && out.event[1].call.effect==1,"first notes start");
    require(out.hardware_active==0,"pending notes do not fabricate hardware progress");
    out=f.tick(0); // Device is idle; original library has not acknowledged it.
    require(out.tick==5 && out.library_on==3,"hardware end alone preserves library owner");
    require(!out.event[0].call.effect && !out.event[1].call.effect,"no guessed next-beat restart");
    out=f.tick(0,1); // Actual consumer acknowledged only channel0.
    require(out.tick==6 && out.event[0].call.channel==1 && !out.event[0].call.effect,"unacknowledged channel remains on");
    require(out.event[1].call.channel==0 && out.event[1].call.effect,"acknowledged channel restarts");
    require(xb_get_audio(&f.state,&before)==XB_OK,"state before contradiction");
    require(xb_audio_native_block(&f.state,1,1,&out)==XB_ERR_ARG,"active/idle contradiction rejected");
    require(xb_get_audio(&f.state,&after)==XB_OK && before.blocks==after.blocks,"bad feedback preserves clock");
    out=f.tick(1,2);
    require(out.tick==7 && out.event[0].call.effect && !out.event[1].call.effect,"independent channel acknowledgements");
    xb_audio_out legacy{};
    require(xb_audio_block(&f.state,&legacy)==XB_ERR_ARG,"legacy VP cannot share native owner");
    int16_t sample=0;
    require(xb_audio(&f.state,&sample,0)==XB_ERR_ARG,"legacy PCM cannot share native owner");
    std::cout << "ordered original calls, explicit idle acknowledgements and retrigger ownership passed\n";
}
