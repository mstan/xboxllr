// Authored source signals and analysis; shared setup for sequential LLE/native
// comparisons. No donor converter is used to generate expected frequencies.
#pragma once
#include "machine/apu.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

struct ApuRateFixture {
    static constexpr unsigned voice=71,object=0x1000+voice*128,sge=0x4000,base=4093;
    unsigned channels,frames;
    uint64_t fetched=0;
    std::vector<uint8_t> ram=std::vector<uint8_t>(0x20000),pcm;
    std::string error;
    xlle::McpxApu apu{[this](const std::string& why){error=why;}};
    ApuRateFixture(unsigned ch,unsigned size,bool loop,bool adpcm=false):channels(ch),frames(size) {
        apu.set_mem_access(
            [this](uint32_t a,void* p,size_t n){if(uint64_t(a)+n>ram.size())return false;std::memcpy(p,ram.data()+a,n);return true;},
            [this](uint32_t a,const void* p,size_t n){if(uint64_t(a)+n>ram.size())return false;std::memcpy(ram.data()+a,p,n);if(a==object+0x58)++fetched;return true;});
        const unsigned pages[]={0x8000,0x5000,0xb000,0xe000,0x12000};
        for(unsigned page=0;page<5;++page)put(sge+page*8,pages[page]);
        reg(0x202c,0x1000);reg(0x2030,sge);reg(0x202f8,voice);
        put(object+0x54,1u<<21);
        reg(0x20304,(adpcm?0xa0000000:0x50000000)|((channels-1)<<27)|((channels-1)<<16)|(loop?1u<<25:0));
        reg(0x203a0,base);reg(0x203dc,frames-1);
        if(adpcm) {
            pcm.resize(((frames+63)/64)*36*channels);
            for(unsigned block=0;block<(frames+63)/64;++block)for(unsigned c=0;c<channels;++c) {
                const int16_t value=c?-8192:4096;
                std::memcpy(pcm.data()+block*36*channels+c*4,&value,2);
                pcm[block*36*channels+c*4+3]=255;
                pcm[block*36*channels+32*channels+c*4+3]=0x70;
            }
        } else {
            pcm.resize(frames*channels*2);
            for(unsigned n=0;n<frames;++n)for(unsigned c=0;c<channels;++c) {
                const double angle=6.283185307179586*(double(n)/64.0+double(c)/4.0);
                const int16_t value=static_cast<int16_t>(std::lround((c?0.1:0.2)*32768*std::sin(angle)));
                std::memcpy(pcm.data()+(n*channels+c)*2,&value,2);
            }
        }
        for(unsigned i=0;i<pcm.size();++i)ram[word(sge+((base+i)>>12)*8)+((base+i)&4095)]=pcm[i];
        fetched=0;
    }
    void put(unsigned a,uint32_t v){std::memcpy(ram.data()+a,&v,4);}
    uint32_t word(unsigned a)const{uint32_t v;std::memcpy(&v,ram.data()+a,4);return v;}
    void reg(unsigned a,uint32_t v){apu.mmio_write(0xfe800000+a,4,v);}
    void pitch(int value){reg(0x2037c,uint32_t(uint16_t(value))<<16);}
    std::vector<float> render(unsigned wanted,unsigned chunk=32) {
        std::vector<float> out(wanted*2);unsigned at=0;
        while(at<wanted) {
            uint32_t n=0;bool ended=false;
            CHECK(apu.resample_source(voice,out.data()+at*2,std::min(chunk,wanted-at),n,ended));
            at+=n;if(ended)break;CHECK(n);
        }
        out.resize(at*2);return out;
    }
    std::vector<float> render_enveloped(unsigned quanta) {
        std::vector<float> out;out.reserve(quanta*64);
        for(unsigned i=0;i<quanta;++i) {
            float samples[64];uint32_t n=0;bool ended=false;
            CHECK(apu.source_envelope_quantum(voice,samples,n,ended));
            out.insert(out.end(),samples,samples+n*2);
            if(ended)break;CHECK(n==32);
        }
        return out;
    }
};

struct ApuToneFit {double amplitude,error;};
inline ApuToneFit apu_tone_fit(const float* data,unsigned frames,unsigned stride,double hz) {
    double ss=0,cc=0,sc=0,ys=0,yc=0;
    for(unsigned n=0;n<frames;++n) {
        const double angle=6.283185307179586*hz*n/48000,s=std::sin(angle),c=std::cos(angle),y=data[n*stride];
        ss+=s*s;cc+=c*c;sc+=s*c;ys+=y*s;yc+=y*c;
    }
    const double determinant=ss*cc-sc*sc,a=(ys*cc-yc*sc)/determinant,b=(yc*ss-ys*sc)/determinant;
    double error=0;
    for(unsigned n=0;n<frames;++n) {
        const double angle=6.283185307179586*hz*n/48000;
        const double d=data[n*stride]-a*std::sin(angle)-b*std::cos(angle);error+=d*d;
    }
    return {std::hypot(a,b),std::sqrt(error/frames)};
}
