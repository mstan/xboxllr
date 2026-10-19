// Independent LLE source consumption versus actual XAudio2 mixer samples.
// Sequential reference/native observations; never duplicate production audio.
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include "audio_backend.h"
#include "machine/apu.hpp"
#ifdef PILOT_AUDIO_ADPCM
#include "xbox_adpcm.h"
#endif
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"audio source comparison line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
int main() {
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    CHECK(pilot_audio_open()==0);
    const unsigned formats[]={8,16,
#ifdef PILOT_AUDIO_ADPCM
        4, // native binding decodes Xbox blocks to PCM16 before the backend
#endif
    };
    for(unsigned bits:formats)for(unsigned channels:{1u,2u}) {
        const bool adpcm=bits==4;
        const unsigned native_bits=adpcm?16:bits,align=channels*native_bits/8;
        std::vector<uint8_t> data(adpcm?32*36*channels:2048*align),ram(0x12000);
        std::vector<int16_t> native_pcm(adpcm?2048*channels:0);
        for(unsigned frame=0;frame<2048;++frame)for(unsigned c=0;c<channels;++c) {
            const int value=frame<512?-48:frame<1536?(c?32:16):80;
            unsigned offset=(frame*channels+c)*(native_bits/8);
            if(adpcm) {
                if(frame%64)continue;
                offset=(frame/64)*36*channels+c*4;
                const int16_t sample=int16_t(value*256);std::memcpy(data.data()+offset,&sample,2);
                data[offset+3]=0xff; // reserved byte is ignored
                data[(frame/64)*36*channels+32*channels+c*4+3]=0x70; // unused final high nibble
            } else if(bits==8)data[offset]=uint8_t(value+128);
            else {int16_t sample=int16_t(value*256);std::memcpy(data.data()+offset,&sample,2);}
        }
#ifdef PILOT_AUDIO_ADPCM
        if(adpcm)for(unsigned block=0;block<32;++block)
            CHECK(xbox_adpcm_decode_block(data.data()+block*36*channels,36*channels,channels,
                  native_pcm.data()+block*64*channels,64*channels));
#endif
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
        reg(0x202c,0x1000);reg(0x2030,0x3000);reg(0x202f8,71);
        const unsigned voice=0x1000+71*128;
        put(voice+0x54,1u<<21);
        const uint32_t format=(adpcm?0xa0000000:bits==16?0x50000000:0)|0x02000000|((channels-1)<<27)|((channels-1)<<16);
        reg(0x20304,format);
        reg(0x203a0,4093);reg(0x203a4,512);reg(0x203dc,1535);
        std::vector<float> source(4096*2);uint32_t count=0;bool ended=true;
        CHECK(apu.source_frames(71,source.data(),4096,count,ended));CHECK(count==4096 && !ended && error.empty());
        float reference[2]={};
        for(unsigned n=3584;n<4096;++n)for(unsigned c=0;c<2;++c)reference[c]+=source[n*2+c]/512;
        CHECK(reference[0]==0.125f && reference[1]==(channels==2?0.25f:0.125f));

        void* native=nullptr;CHECK(pilot_audio_buffer_create(2048*align,channels,48000,native_bits,&native)==0);
        CHECK(pilot_audio_buffer_copy(native,0,adpcm?(void*)native_pcm.data():(void*)data.data(),2048*align,0)==0);
        CHECK(pilot_audio_buffer_loop_region(native,512*align,1024*align)==0);
        CHECK(pilot_audio_buffer_play(native,1)==0);
        for(unsigned scenario=0;scenario<4;++scenario) {
            for(unsigned bin=0;bin<32;++bin) {
                unsigned amount=scenario==1?bin+8:0;
                reg(0x20200+4*bin,amount);
                CHECK(pilot_audio_mixbin_headroom(bin,amount)==0);
            }
            const std::vector<unsigned> bins=scenario==0?std::vector<unsigned>{0,1}:
                scenario==1?std::vector<unsigned>{31,3,7,5,0,1,2,6}:
                scenario==2?std::vector<unsigned>{7,7,31,2}:std::vector<unsigned>{};
            const int32_t overall=scenario==1?-1800:scenario==2?-6399:0;
            const int32_t db100[]={-1200,-31,0,-600};
            int32_t volumes[8]={};unsigned attenuation[8];float muted_bound[32]={};
            for(unsigned slot=0;slot<8;++slot) {
                if(slot>=bins.size())attenuation[slot]=4095;
                else {
                    volumes[slot]=scenario?db100[bins[slot]%4]:0;
                    attenuation[slot]=unsigned(-(overall+volumes[slot]))*64/100;
                    if(attenuation[slot]>=4095) {
                        attenuation[slot]=4095;
                        muted_bound[bins[slot]]+=std::fabs(reference[slot%channels])*0.000632f;
                    }
                }
            }
            // Pack authored inputs using the independently qualified original
            // helper's register layout. The native side keeps continuous dB.
            std::vector<unsigned> complete=bins;
            for(unsigned bin=0;complete.size()<8;++bin)
                if(std::find(complete.begin(),complete.end(),bin)==complete.end())complete.push_back(bin);
            uint32_t routes=0;
            for(unsigned slot=0;slot<6;++slot)routes|=complete[slot]<<(slot*5+(slot>=3));
            reg(0x20300,routes);reg(0x20304,format|complete[6]|(complete[7]<<5));
            for(unsigned part=0;part<3;++part)
                reg(0x20360+part*4,(attenuation[part*2]<<4)|(attenuation[part*2+1]<<20)|
                    ((attenuation[6]>>(part*4))&15)|(((attenuation[7]>>(part*4))&15)<<16));
            float expected[1024]={};
            CHECK(apu.mix_2d_samples(71,source.data()+4064*2,32,expected));
            CHECK(pilot_audio_buffer_mixbins(native,unsigned(bins.size()),bins.data(),volumes)==0);
            CHECK(pilot_audio_buffer_volume(native,overall)==0);
            Sleep(100);
            std::vector<float> actual(4096*32);
            CHECK(pilot_audio_capture_begin(actual.data(),4096,0)==0);
            CHECK(pilot_audio_capture_wait(5000)==1);CHECK(pilot_audio_capture_end()==4096);
            for(unsigned n=3584;n<4096;++n)for(unsigned bin=0;bin<32;++bin) {
                const float wanted=expected[bin*32+n%32];
                // <0.2% amplitude covers 1/64-dB quantization. The explicit
                // muted-send bound covers continuous HLE below hardware mute.
                const float tolerance=0.00003f+std::fabs(wanted)*0.002f+muted_bound[bin];
                CHECK(std::fabs(actual[n*32+bin]-wanted)<tolerance);
            }
        }
        uint32_t play=0,write=0;CHECK(pilot_audio_buffer_position(native,&play,&write)==0);
        CHECK(play>=512*align && play<1536*align && write>=512*align && write<1536*align);
        CHECK(pilot_audio_buffer_stop(native)==0);pilot_audio_buffer_destroy(native);
    }
    pilot_audio_close();
    std::puts("PASS: real PCM/ADPCM source and 2D sends versus native mixer; four route/gain cases per format/channel, all-bin headroom and live reset; bounded dB quantization/mute tolerance");
}
