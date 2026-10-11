#include <cstdio>
#include <cstdlib>
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"envelope FAIL %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
#include "apu_rate_fixture.hpp"

int main() {
    // All documented VOICE_ON start pairs program independent 16-bit counters.
    for(unsigned a=0;a<4;++a)for(unsigned b=0;b<4;++b) {
        ApuRateFixture f(2,64,true);
        f.put(f.object+0x54,0xab145678);f.put(f.object+0x58,0x5a000000);
        f.put(f.object+0x34,0x1234abcd);
        f.reg(0x20308,0x013007);f.reg(0x2030c,0x017009);
        f.reg(0x20310,0x02500b);f.reg(0x20314,0x02900d);
        f.reg(0x2054,0xffff);f.reg(0x20120,0x1ffff);
        f.reg(0x20124,f.voice|(a<<28)|(b<<24));CHECK(f.error.empty());
        const unsigned ca=a==0?0xabcd:a==1?0x130:a==2?0:0x170;
        const unsigned cb=b==0?0x1234:b==1?0x250:b==2?0:0x290;
        CHECK(f.word(f.object+0x34)==(ca|(cb<<16)));
        CHECK(f.word(f.object+0x54)==(0x00345678|(a<<28)|(b<<24)));
        CHECK(f.word(f.object+0x58)==0x5a000000);
        CHECK((f.word(f.object+0x7c)&0xffff)==0xffff);
        CHECK(f.apu.mmio_read(0xfe802054,4)==f.voice);
    }
    // OFF is identity, including clearing stale counters/levels on processing.
    {
        ApuRateFixture f(2,128,true),plain(2,128,true);
        f.put(f.object+0x34,0xabcdef01);f.put(f.object+0x58,0x11000000);
        f.put(f.object+0x5c,0x2200007f);
        CHECK(f.render_enveloped(64)==plain.render(2048));
        CHECK(f.word(f.object+0x34)==0);
        CHECK((f.word(f.object+0x58)>>24)==255 && f.word(f.object+0x5c)==0xff00007f);
    }
    // Envelope quanta follow the output clock, never source pitch/consumption.
    for(int pitch:{-4096,4096}) {
        ApuRateFixture f(1,4096,true);f.pitch(pitch);
        f.reg(0x20308,1);f.reg(0x2030c,2<<12);
        f.put(f.object+0x54,(1u<<21)|(1u<<28));f.put(f.object+0x34,2);
        unsigned q=0;
        for(unsigned expected:{1u,0u,0u}) {
            f.render_enveloped(1);CHECK((f.word(f.object+0x34)&0xffff)==expected);
            CHECK((f.word(f.object+0x58)>>24)==0);++q;
        }
        CHECK((f.word(f.object+0x54)>>28)==2);
        for(unsigned n=0;n<=16;++n) {
            f.render_enveloped(1);CHECK((f.word(f.object+0x58)>>24)==n*255/16);++q;
        }
        CHECK(q==20 && (f.word(f.object+0x54)>>28)==3 && (f.word(f.object+0x34)&0xffff)==32);
        for(unsigned n=0;n<32;++n)f.render_enveloped(1);
        CHECK((f.word(f.object+0x54)>>28)==3 && (f.word(f.object+0x34)&0xffff)==0);
        f.render_enveloped(1);CHECK((f.word(f.object+0x54)>>28)==4);
        // Zero decay is an immediate nominal transition to its zero sustain.
        const auto silent=f.render_enveloped(1);
        CHECK((f.word(f.object+0x54)>>28)==5 && !(f.word(f.object+0x34)&0xffff));
        CHECK((f.word(f.object+0x58)>>24)==0);
        for(float sample:silent)CHECK(sample==0);
    }
    // Both stable envelopes independently respond to live level changes.
    for(unsigned a:{0u,17u,128u,255u})for(unsigned b:{0u,127u,255u}) {
        ApuRateFixture f(2,4096,true);
        f.put(f.object+0x54,(1u<<21)|0x55000000);
        f.reg(0x2030c,a<<24);f.reg(0x20314,b<<24);f.reg(0x20308,0xc0000000);
        f.put(f.object+0x34,0xffffffff);
        const auto all=f.render_enveloped(144);
        const double hz=750*std::exp2((-64*32*double(b)/255)/4096);
        for(unsigned ch=0;ch<2;++ch) {
            const auto fit=apu_tone_fit(all.data()+1024+ch,4096,2,hz);
            CHECK(std::fabs(fit.amplitude-(ch?0.1:0.2)*a/255)<0.001 && fit.error<0.0003);
        }
        CHECK(!f.word(f.object+0x34));
        CHECK((f.word(f.object+0x58)>>24)==a && (f.word(f.object+0x5c)>>24)==b);
        f.reg(0x2030c,(255-a)<<24);xlle::ApuEnvelopeStep e;
        CHECK(f.apu.step_envelopes(f.voice,e) && e.running);
        CHECK(e.amplitude==float(255-a)/255 && e.filter==float(b)/255);
    }
    // Paused/locked/inactive voices leave guest state and sample storage alone.
    for(unsigned mode=0;mode<3;++mode) {
        ApuRateFixture f(1,64,true);f.put(f.object+0x54,mode==0?0:(1u<<21)|((mode==1?1u:0u)<<18));
        if(mode==2)f.reg(0x202fc,1);
        const auto before=f.ram;float out[64];for(float& v:out)v=42;uint32_t count;bool ended;
        CHECK(f.apu.source_envelope_quantum(f.voice,out,count,ended));
        CHECK(!count && !ended && f.ram==before && !f.fetched);
        for(float v:out)CHECK(v==42);
    }
    // Unsupported second envelope cannot partially advance the first one.
    for(unsigned phase:{8u,15u}) {
        ApuRateFixture f(1,64,true);f.put(f.object+0x54,(1u<<21)|(phase<<24)|(1u<<28));
        f.put(f.object+0x34,2);const auto before=f.ram;xlle::ApuEnvelopeStep e;
        CHECK(!f.apu.step_envelopes(f.voice,e));CHECK(!e.running && f.ram==before && !f.fetched);
    }
    for(unsigned rate:{0u,1u}) {
        ApuRateFixture f(1,64,true);f.reg(0x20308,rate);f.put(f.object+0x54,(1u<<21)|(2u<<28));
        f.put(f.object+0x34,17);const auto before=f.ram;xlle::ApuEnvelopeStep e;
        CHECK(!f.apu.step_envelopes(f.voice,e) && !e.running && f.ram==before);
    }
    for(unsigned bad:{0x40000000u,0x04000000u,0x00010000u,1u}) {
        ApuRateFixture f(1,64,true);
        if(bad==1)f.put(f.object+0x58,1);
        f.reg(0x2054,0xffff);f.reg(0x20120,0x1ffff);const auto before=f.ram;
        f.reg(0x20124,f.voice|(bad==1?0:bad));CHECK(!f.error.empty());
        CHECK(f.ram==before && f.apu.mmio_read(0xfe802054,4)==0); // faulted reads stop
    }
    std::puts("PASS: 16 start pairs, disabled identity, clocked delay/attack/hold, nominal zero-decay sustain, stable gains/modulation and guarded invalid phases/attack");
}
