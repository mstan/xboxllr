#include <cstdio>
#include <cstdlib>
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"rate FAIL %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
#include "apu_rate_fixture.hpp"

int main() {
    for(unsigned channels:{1u,2u})for(int pitch:{-8192,-501,0,4096,8191}) {
        ApuRateFixture f(channels,4096,true);f.pitch(pitch);
        const auto output=f.render(4096);CHECK(output.size()==8192 && f.error.empty());
        const double rate=std::exp2(double(pitch)/4096.0),hz=48000*rate/64;
        for(unsigned c=0;c<2;++c) {
            const auto fit=apu_tone_fit(output.data()+512*2+c,3584,2,hz);
            CHECK(std::fabs(fit.amplitude-(channels==2 && c?0.1:0.2))<0.001);
            CHECK(fit.error<0.0003);
        }
        CHECK(f.fetched>=std::floor(4096*rate) && f.fetched<=std::ceil(4096*rate)+256);
    }
    for(int pitch:{-4096,-501,0,4096}) {
        ApuRateFixture a(2,4096,true),b(2,4096,true);a.pitch(pitch);b.pitch(pitch);
        const auto whole=a.render(4096,4096),pieces=b.render(4096,7);
        CHECK(whole.size()==pieces.size());
        for(unsigned i=0;i<whole.size();++i)CHECK(std::fabs(whole[i]-pieces[i])<2e-6f);
        CHECK(a.fetched==b.fetched);
    }
    for(bool adpcm:{false,true})for(unsigned frames:{1u,31u,32u,33u,64u,129u})for(int pitch:{-4096,0,4096}) {
        ApuRateFixture f(2,frames,false,adpcm);f.pitch(pitch);
        const auto output=f.render(1024,13);
        ApuRateFixture bulk(2,frames,false,adpcm);bulk.pitch(pitch);
        float guarded[2050];for(float& v:guarded)v=43;
        uint32_t actual=0;bool drained=false;
        CHECK(bulk.apu.resample_source(bulk.voice,guarded+1,1024,actual,drained));
        CHECK(drained && actual*2==output.size() && guarded[0]==43);
        for(unsigned i=0;i<output.size();++i)CHECK(std::fabs(guarded[i+1]-output[i])<2e-6f);
        for(unsigned i=actual*2+1;i<2050;++i)CHECK(guarded[i]==43);
        const double expected=frames*std::exp2(-double(pitch)/4096.0);
        CHECK(std::fabs(double(output.size()/2)-expected)<=1);
        CHECK(f.fetched==frames && (f.word(f.object+0x58)&0xffffff)==frames-1);
        CHECK(f.word(f.object+0x54)==(1u<<21)); // source drain is not completion
        float guard[2]={42,43};uint32_t n=99;bool ended=false;
        CHECK(f.apu.resample_source(f.voice,guard,1,n,ended));CHECK(n==0 && ended && guard[0]==42 && guard[1]==43);
    }
    for(bool adpcm:{false,true}) {
        ApuRateFixture a(2,4096,true,adpcm),b(2,4096,true,adpcm);
        a.pitch(-501);b.pitch(-501);a.render(75);b.render(75);
        for(unsigned method:{0x20140u,0x202fcu}) {
            a.reg(method,method==0x20140?a.voice|(1u<<18):1);
            const auto before=a.ram;const auto fetched=a.fetched;float output[2]={42};uint32_t n;bool ended;
            CHECK(a.apu.resample_source(a.voice,output,1,n,ended));CHECK(!n && !ended && output[0]==42);
            CHECK(a.fetched==fetched && a.ram==before);
            a.reg(method,method==0x20140?a.voice:0);
        }
        CHECK(a.render(256)==b.render(256));
        a.reg(0x203d8,0);ApuRateFixture fresh(2,4096,true,adpcm);fresh.pitch(-501);
        CHECK(a.render(256)==fresh.render(256));
        a.pitch(4096);a.render(256);const auto changed=a.render(4096);
        if(!adpcm) {
            const auto fit=apu_tone_fit(changed.data(),4096,2,1500);
            CHECK(std::fabs(fit.amplitude-0.2)<0.001 && fit.error<0.0003);
        } else for(unsigned i=512;i<changed.size()/2;++i) {
            CHECK(std::fabs(changed[i*2]-0.125f)<0.00003f);
            CHECK(std::fabs(changed[i*2+1]+0.25f)<0.00003f);
        }
    }
    for(unsigned kind=0;kind<4;++kind) {
        ApuRateFixture f(1,64,true);
        if(kind==0)f.put(f.sge,0x20000);
        if(kind==1)f.reg(0x20308,1u<<24); // envelope pitch modulation needs its owner
        if(kind==2)f.reg(0x20304,0x51000000); // streaming ownership not supplied
        if(kind==3)f.reg(0x203d8,65);
        f.fetched=0; // exclude the explicit programming write above
        float output[64];for(float& v:output)v=42;uint32_t n;bool ended;
        CHECK(!f.apu.resample_source(f.voice,output,32,n,ended));CHECK(!n && !ended && !f.error.empty());
        for(float v:output)CHECK(v==42);CHECK(!f.fetched);
    }
    for(unsigned lifetime=0;lifetime<64;++lifetime) {
        ApuRateFixture f(1,128,false);CHECK(f.render(256).size()==256 && f.fetched==128);
    }
    std::puts("PASS: real source/rate consumption, pitch spectra, chunking, finite PCM/ADPCM drain, pause/lock/seek/change, failures and 64 lifetimes");
}
