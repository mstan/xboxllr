// Representative authored nominal envelopes and real ordinary-buffer owners.
// No hardware curve, native waveform, GP drain or notifier payload claim.
#include <cstdio>
#include <cstdlib>
#include <array>
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"APU ENVELOPE LIFECYCLE FAIL %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
#include "apu_completion_fixture.hpp"

struct Fixture : ApuCompletionFixture {
    unsigned source_reads=0,reject_write=0;
    std::vector<uint32_t> writes;
    Fixture():ApuCompletionFixture(2,2048,true) {
        apu.set_mem_access([this](uint32_t a,void* p,size_t n) {
            if(uint64_t(a)+n>ram.size())return false;
            if(a>=0x5000 && a<0x15000)++source_reads;
            std::memcpy(p,ram.data()+a,n);return true;
        },[this](uint32_t a,const void* p,size_t n) {
            if(uint64_t(a)+n>ram.size() || a==reject_write)return false;
            writes.push_back(a);std::memcpy(ram.data()+a,p,n);return true;
        });
        apu.set_irq([this](int line,bool high) {
            CHECK(line==5);
            if(high) {
                CHECK(!(word(object+0x54)&(1u<<21)) && ram[notify+15]==1);
                CHECK(writes.size()>=2 && writes[writes.size()-2]==object+0x54 && writes.back()==notify+15);
                ++completions;
            }
        });
    }
    void rates(unsigned a,unsigned f) {reg(0x2036c,a);reg(0x20318,f);}
    void release(){reg(0x2012c,voice);CHECK(error.empty());}
    void current(unsigned a,unsigned f) {
        put(object+0x58,(word(object+0x58)&0xffffff)|(a<<24));
        put(object+0x5c,(word(object+0x5c)&0xffffff)|(f<<24));
    }
    std::array<float,1024> quantum(uint32_t& n,bool& done,float initial=0) {
        std::array<float,1024> bins;bins.fill(initial);
        CHECK(apu.process_2d_voice(voice,bins.data(),n,done));return bins;
    }
};

int main() {
    using xlle::apu_envelope_curve::exponential_level;
    using xlle::apu_envelope_curve::decay_level;
    // -60dB means 1/1000. Halfway gives 1/sqrt(1000): authored byte bounds
    // 8<255/sqrt(1000)<9 and4<128/sqrt(1000)<5. These are no renderer goldens.
    CHECK(exponential_level(255,16,16)==255);
    CHECK(exponential_level(255,8,16)==8);
    CHECK(exponential_level(128,8,16)==4);
    CHECK(exponential_level(255,12,16)==45);
    CHECK(exponential_level(255,0,16)==0 && exponential_level(255,0,0)==0);
    CHECK(decay_level(64,12,16)==64 && decay_level(64,0,16)==64);
    CHECK(decay_level(0,8,16)==8 && decay_level(64,0,0)==64);

    // Both decay controls advance together, preserve source extents, and clamp
    // amplitude to64 while the independent filter envelope continues downward.
    {
        Fixture f;f.reg(0x2030c,(64u<<24)|1);f.reg(0x20314,1);
        f.put(f.object+0x54,(1u<<21)|0x44000000);f.put(f.object+0x34,0x00100010);
        const unsigned cbo=f.word(f.object+0x58)&0xffffff,ebo=f.word(f.object+0x5c)&0xffffff;
        xlle::ApuEnvelopeStep e;
        CHECK(f.apu.step_envelopes(f.voice,e) && e.amplitude==1 && e.filter==1);
        for(unsigned q=1;q<=4;++q)CHECK(f.apu.step_envelopes(f.voice,e));
        CHECK(e.amplitude==64.f/255 && e.filter==45.f/255);
        CHECK((f.word(f.object+0x54)>>28)==5 && ((f.word(f.object+0x54)>>24)&15)==4);
        CHECK((f.word(f.object+0x34)&0xffff)==0 && (f.word(f.object+0x34)>>16)==11);
        CHECK((f.word(f.object+0x58)&0xffffff)==cbo && (f.word(f.object+0x5c)&0xffffff)==ebo);
        CHECK(!f.source_reads && !f.completions && f.ram[f.notify+15]==0x80);
    }
    // Command release snapshots CURRENT128/64, deliberately distinct from
    // sustain255/255. It changes only phases/counters, never CBO/EBO or DONE.
    {
        Fixture f;f.rates(1,2);f.current(128,64);
        f.reg(0x2030c,255u<<24);f.reg(0x20314,255u<<24);
        f.put(f.object+0x54,(1u<<21)|0x55000000);
        const auto before=f.ram;f.release();auto expected=before;
        const uint32_t state=(1u<<21)|0x66000000,counts=0x00200010;
        std::memcpy(expected.data()+f.object+0x54,&state,4);
        std::memcpy(expected.data()+f.object+0x34,&counts,4);
        CHECK(f.ram==expected && !f.source_reads && !f.completions && f.apu.voice_release_methods()==1);
        xlle::ApuEnvelopeStep e;CHECK(f.apu.step_envelopes(f.voice,e));
        CHECK(e.amplitude==128.f/255 && e.filter==64.f/255 && !e.force_release);
        for(unsigned q=1;q<=8;++q)CHECK(f.apu.step_envelopes(f.voice,e));
        CHECK(e.amplitude==4.f/255 && (f.word(f.object+0x34)&0xffff)==7);
        // Reissuing release reanchors to CURRENT4, without pulling source.
        f.release();CHECK(f.apu.step_envelopes(f.voice,e) && e.amplitude==4.f/255);
        CHECK(!f.source_reads && !f.completions);
    }
    // A real looping PCM stream fades while its source/rate clock stays equal
    // to an independent unity-envelope instance. The final force-release calls
    // the existing ordinary state/status/IRQ owner before any more source DMA.
    {
        Fixture f,plain;f.rates(1,1);
        f.reg(0x2030c,128u<<24);f.reg(0x20314,255u<<24);
        f.put(f.object+0x54,(1u<<21)|0x55000000);
        uint32_t n,pn;bool done,pdone;
        for(unsigned q=0;q<2;++q){f.quantum(n,done);plain.quantum(pn,pdone);CHECK(n==pn && !done && !pdone);}
        f.release();float previous=1;
        for(unsigned q=0;q<16;++q) {
            const auto out=f.quantum(n,done),reference=plain.quantum(pn,pdone);
            CHECK(n==32 && pn==32 && !done && !pdone && !f.completions);
            const unsigned code=f.word(f.object+0x58)>>24;
            if(q==0)CHECK(code==128);if(q==8)CHECK(code==4);
            const float gain=float(code)/255;CHECK(gain<=previous);previous=gain;
            for(unsigned bin=0;bin<2;++bin)for(unsigned at=0;at<32;++at)
                CHECK(std::fabs(out[bin*32+at]-reference[bin*32+at]*gain)<1e-7f);
            CHECK((f.word(f.object+0x58)&0xffffff)==(plain.word(plain.object+0x58)&0xffffff));
            CHECK(f.ram[f.notify+15]==0x80);
        }
        const unsigned reads=f.source_reads,cbo=f.word(f.object+0x58)&0xffffff;
        const auto terminal=f.quantum(n,done,7);
        CHECK(!n && done && f.completions==1 && !f.apu.voice_off_methods());
        CHECK(f.source_reads==reads && (f.word(f.object+0x58)&0xffffff)==cbo);
        for(float value:terminal)CHECK(value==7);
        const auto stopped=f.ram;f.quantum(n,done,9);
        CHECK(!n && !done && f.completions==1 && f.ram==stopped && f.source_reads==reads);
        // A stopped converter must not leak old positive PCM into a new owner.
        f.reg(0x1000,0x60);f.reg(0x2054,0xffff);
        CHECK((f.word(f.object+0x58)&0xffffff)!=0);
        const uint32_t old_cursor_word=f.word(f.object+0x58),old_end_word=f.word(f.object+0x5c);
        for(unsigned i=0;i<f.pcm.size();i+=2) {
            // Only the first256 source frames are negative. A retained old
            // cursor would read positive samples even with converter reset.
            const int16_t sample=i/4<256?-8192:8192;
            for(unsigned lane=0;lane<2;++lane) {
                const unsigned a=f.base+i+lane;
                f.ram[f.word(f.sge+(a>>12)*8)+(a&4095)]=uint8_t(uint16_t(sample)>>(lane*8));
            }
        }
        f.ram[f.notify+15]=0x80;f.reg(0x20120,0x1ffff);f.reg(0x202fc,1);
        const unsigned before_reads=f.source_reads,before_completions=f.completions;
        f.reg(0x20124,f.voice);
        CHECK(f.error.empty() && f.word(f.object+0x58)==(old_cursor_word&0xff000000));
        CHECK(f.source_reads==before_reads && f.completions==before_completions && f.ram[f.notify+15]==0x80);
        CHECK(f.word(f.object+0x5c)==old_end_word);
        f.reg(0x202fc,0);
        const auto restarted=f.quantum(n,done);CHECK(n==32 && !done && f.error.empty());
        for(unsigned at=0;at<32;++at)CHECK(restarted[at]<-0.2f && restarted[32+at]<-0.2f);
        // VOICE_ON also resets the release origin, not only the converter.
        f.release();xlle::ApuEnvelopeStep e;CHECK(f.apu.step_envelopes(f.voice,e) && e.amplitude==1);
    }
    // Filter force-release never retires an otherwise active amplitude voice.
    {
        Fixture f;f.put(f.object+0x54,(1u<<21)|0x07000000);
        uint32_t n;bool done;f.quantum(n,done);
        CHECK(n==32 && !done && !f.completions && (f.word(f.object+0x54)&(1u<<21)));
        CHECK((f.word(f.object+0x5c)>>24)==0);
    }
    // A nonzero start is admitted only for the reached stopped, locked PCM16
    // owner. Unsupported ownership cannot alter links/cursors/notifiers.
    {
        Fixture f;f.put(f.object+0x58,0x7b000123);
        f.put(f.object+0x54,f.word(f.object+0x54)&~(1u<<21));
        f.reg(0x20120,0x1ffff);const auto before=f.ram;
        f.reg(0x20124,f.voice); // inactive ordinary PCM16, deliberately unlocked
        CHECK(!f.error.empty() && f.ram==before && !f.source_reads && !f.completions);
    }
    for(unsigned bad:{0u,1u<<21,1u<<22,1u<<24,0x80000000u}) {
        Fixture f;f.put(f.object+0x58,0x7b000123);f.reg(0x202fc,1);
        if(bad) {
            f.put(f.object+0x54,f.word(f.object+0x54)&~(1u<<21));
            f.put(f.object+4,f.word(f.object+4)^bad);
        }
        f.reg(0x20120,0x1ffff);const auto before=f.ram;
        f.reg(0x20124,f.voice);
        CHECK(!f.error.empty() && f.ram==before && !f.source_reads && !f.completions);
    }
    // Zero-rate release is a nominal immediate force-release; pause and lock
    // freeze both envelope clocks and resume the same pending ordinary owner.
    for(unsigned hold:{0x20140u,0x202fcu}) {
        Fixture f;f.current(128,64);f.rates(0,0);f.release();
        f.reg(hold,hold==0x20140?f.voice|(1u<<18):1);
        const auto before=f.ram;uint32_t n;bool done;f.quantum(n,done,3);
        CHECK(!n && !done && f.ram==before && !f.source_reads && !f.completions);
        f.reg(hold,hold==0x20140?f.voice:0);f.quantum(n,done,3);
        CHECK(!n && done && f.completions==1 && !f.source_reads);
    }
    // Unknown phase in the other envelope cannot partially advance decay.
    {
        Fixture f;f.put(f.object+0x54,(1u<<21)|0x4f000000);
        f.reg(0x2030c,1);f.put(f.object+0x34,16);
        const auto before=f.ram;xlle::ApuEnvelopeStep e;
        CHECK(!f.apu.step_envelopes(f.voice,e) && f.ram==before && !e.running && !f.source_reads);
    }
    // Ownership/flags and failed state DMA never publish release completion.
    for(unsigned bad:{1u<<24,1u<<22,1u<<21}) {
        Fixture f;f.reg(0x20304,f.word(f.object+4)|bad);const auto before=f.ram;
        f.reg(0x2012c,f.voice);CHECK(!f.error.empty() && f.ram==before && !f.completions && !f.source_reads);
    }
    {
        Fixture f;const auto before=f.ram;f.reg(0x2012c,0x10000|f.voice);
        CHECK(!f.error.empty() && f.ram==before && !f.completions);
    }
    {
        Fixture f;f.rates(1,2);f.reject_write=f.object+0x54;
        const unsigned old_state=f.word(f.object+0x54);f.reg(0x2012c,f.voice);
        CHECK(!f.error.empty() && f.word(f.object+0x54)==old_state);
        CHECK(f.word(f.object+0x34)==0x00200010 && !f.apu.voice_release_methods()); // Terminal partial DMA.
        CHECK(!f.completions && !f.source_reads && f.ram[f.notify+15]==0x80);
    }
    std::puts("PASS: nominal envelope transitions/current-level release, real stream fade, ordered ordinary retirement, restart/pause/lock and fail-closed ownership");
}
