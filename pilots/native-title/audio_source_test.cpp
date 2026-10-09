// Independent LLE source consumption versus actual XAudio2 mixer samples.
// Sequential reference/native observations; never duplicate production audio.
#include <windows.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include "audio_backend.h"
#include "machine/apu.hpp"
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"audio source comparison line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
int main() {
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    CHECK(pilot_audio_open()==0);
    for(unsigned bits:{8u,16u})for(unsigned channels:{1u,2u}) {
        const unsigned align=channels*bits/8;
        std::vector<uint8_t> data(2048*align),ram(0x12000);
        for(unsigned frame=0;frame<2048;++frame)for(unsigned c=0;c<channels;++c) {
            const int value=frame<512?-48:frame<1536?(c?32:16):80;
            unsigned offset=(frame*channels+c)*(bits/8);
            if(bits==8)data[offset]=uint8_t(value+128);
            else {int16_t sample=int16_t(value*256);std::memcpy(data.data()+offset,&sample,2);}
        }
        auto put=[&](unsigned a,uint32_t v){std::memcpy(ram.data()+a,&v,4);};
        std::string error;
        xlle::McpxApu apu([&](const std::string& why){error=why;});
        apu.set_mem_access(
            [&](uint32_t a,void* p,size_t n){if(uint64_t(a)+n>ram.size())return false;std::memcpy(p,ram.data()+a,n);return true;},
            [&](uint32_t a,const void* p,size_t n){if(uint64_t(a)+n>ram.size())return false;std::memcpy(ram.data()+a,p,n);return true;});
        auto reg=[&](uint32_t o,uint32_t v){apu.mmio_write(0xfe800000+o,4,v);};
        put(0x3000,0x8000);put(0x3008,0x5000);put(0x3010,0xb000);
        for(unsigned i=0;i<data.size();++i) {
            const unsigned linear=4093+i,page=linear>>12;
            uint32_t physical;std::memcpy(&physical,ram.data()+0x3000+page*8,4);
            ram[physical+(linear&4095)]=data[i];
        }
        reg(0x202c,0x1000);reg(0x2030,0x3000);reg(0x202f8,7);
        const unsigned voice=0x1000+7*128;
        put(voice+0x54,1u<<21);
        reg(0x20304,(bits==16?0x50000000:0)|0x02000000|((channels-1)<<27)|((channels-1)<<16));
        reg(0x203a0,4093);reg(0x203a4,512);reg(0x203dc,1535);
        std::vector<float> source(4096*2);uint32_t count=0;bool ended=true;
        CHECK(apu.source_frames(7,source.data(),4096,count,ended));CHECK(count==4096 && !ended && error.empty());
        float reference[2]={};
        for(unsigned n=3584;n<4096;++n)for(unsigned c=0;c<2;++c)reference[c]+=source[n*2+c]/512;
        CHECK(reference[0]==0.125f && reference[1]==(channels==2?0.25f:0.125f));

        void* native=nullptr;CHECK(pilot_audio_buffer_create(unsigned(data.size()),channels,48000,bits,&native)==0);
        CHECK(pilot_audio_buffer_copy(native,0,data.data(),unsigned(data.size()),0)==0);
        CHECK(pilot_audio_buffer_loop_region(native,512*align,1024*align)==0);
        uint32_t bins[]={0,1};int32_t volumes[]={0,0};
        CHECK(pilot_audio_buffer_mixbins(native,2,bins,volumes)==0);
        CHECK(pilot_audio_buffer_volume(native,0)==0);
        CHECK(pilot_audio_buffer_play(native,1)==0);
        Sleep(100);
        std::vector<float> actual(4096*32);
        CHECK(pilot_audio_capture_begin(actual.data(),4096,0)==0);
        Sleep(180);CHECK(pilot_audio_capture_end()==4096);
        for(unsigned n=3584;n<4096;++n)for(unsigned c=0;c<32;++c)
            CHECK(std::fabs(actual[n*32+c]-(c<2?reference[c]:0))<0.00003f);
        uint32_t play=0,write=0;CHECK(pilot_audio_buffer_position(native,&play,&write)==0);
        CHECK(play>=512*align && play<1536*align && write>=512*align && write<1536*align);
        CHECK(pilot_audio_buffer_stop(native)==0);pilot_audio_buffer_destroy(native);
    }
    pilot_audio_close();
    std::puts("PASS: LLE source versus real native mixer, PCM8/16 mono/stereo and interior loops; 3e-5 sample tolerance");
}
