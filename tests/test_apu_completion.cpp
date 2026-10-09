#include <cstdio>
#include <cstdlib>
#include <array>
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"APU COMPLETION FAIL %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
#include "apu_completion_fixture.hpp"

int main() {
    unsigned cases=0;
    for(bool adpcm:{false,true})for(unsigned channels:{1u,2u})
    for(unsigned frames:{1u,31u,32u,33u,64u,129u})for(int pitch:{-4096,0,4096}) {
        ApuCompletionFixture f(channels,frames,false,adpcm);f.pitch(pitch);
        unsigned total=0;bool completed=false;
        for(unsigned q=0;q<40 && !completed;++q) {
            std::array<float,1024> bins;bins.fill(3);uint32_t n=99;
            CHECK(f.apu.process_2d_voice(f.voice,bins.data(),n,completed));
            CHECK(n<=32);total+=n;
            for(unsigned bin=0;bin<32;++bin)for(unsigned at=0;at<32;++at)
                if(bin>=2 || at>=n)CHECK(bins[bin*32+at]==3);
            if(!completed)CHECK(!f.completions && f.ram[f.notify+15]==0x80 && (f.word(f.object+0x54)&(1u<<21)));
        }
        const unsigned wanted=pitch<0?frames*2:pitch>0?frames/2:frames;
        CHECK(completed && total==wanted && f.completions==1);
        CHECK((f.word(f.object+0x58)&0xffffff)==frames-1 && !f.apu.voice_off_methods());
        const auto before=f.ram;const auto fetched=f.fetched;
        std::array<float,1024> bins;bins.fill(7);uint32_t n=99;
        CHECK(f.apu.process_2d_voice(f.voice,bins.data(),n,completed));
        CHECK(!n && !completed && f.completions==1 && f.fetched==fetched && f.ram==before);
        for(float value:bins)CHECK(value==7);
        ++cases;
    }
    // Looping, paused and locked voices cannot be retired by the composition.
    ApuCompletionFixture loop(2,33,true);
    for(unsigned q=0;q<64;++q) {
        std::array<float,1024> bins{};uint32_t n;bool done;
        CHECK(loop.apu.process_2d_voice(loop.voice,bins.data(),n,done));CHECK(n==32 && !done && !loop.completions);
    }
    for(unsigned method:{0x20140u,0x202fcu}) {
        loop.reg(method,method==0x20140?loop.voice|(1<<18):1);
        const auto before=loop.ram;std::array<float,1024> bins{};uint32_t n;bool done;
        CHECK(loop.apu.process_2d_voice(loop.voice,bins.data(),n,done));CHECK(!n && !done && loop.ram==before);
        loop.reg(method,method==0x20140?loop.voice:0);
    }
    // A missing stage cannot consume input or manufacture a completed packet.
    for(unsigned mode=0;mode<4;++mode) {
        ApuCompletionFixture f(1,32);
        if(mode==0)f.reg(0x20318,1<<16);
        if(mode==1)f.reg(0x20304,f.word(f.object+4)&~0xe000u);
        if(mode==2)f.reg(0x20304,f.word(f.object+4)|(1<<24));
        if(mode==3)f.reg(0x20300,0x8000);
        const auto before=f.ram;std::array<float,1024> bins{};uint32_t n;bool done;
        CHECK(!f.apu.process_2d_voice(f.voice,bins.data(),n,done));CHECK(!n && !done && !f.completions && f.ram==before);
    }
    std::printf("PASS: %u finite PCM/ADPCM source-rate-envelope-send retirements, exact nominal frame counts, no padding/recompletion, loops/pause/locks and unsupported-stage guards\n",cases);
}
