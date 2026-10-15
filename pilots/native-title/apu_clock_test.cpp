#include "machine/apu.hpp"
#include <cstdio>
#include <string>
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);return 1;}}while(0)
int main()
{
    std::string failure;
    xlle::McpxApu apu([&](const std::string& reason){failure=reason;});
    auto sample=[&](){return apu.mmio_read(0xfe80200cu,4);};
    auto mode=[&](uint32_t value){apu.mmio_write(0xfe802000u,4,value);};
    CHECK(apu.initialize_kernel_audio_handoff());
    apu.mmio_write(0xfe80200c,4,0);
    apu.set_time(0,1000000000);CHECK(sample()==0);
    apu.set_time(1000000000,1000000000);CHECK(sample()==0); // stopped, not host uptime
    // Mode1 advances independently of the FE method engine's halt state.
    apu.mmio_write(0xfe801100,4,0x80);mode(15);
    apu.set_time(1000020833,1000000000);CHECK(sample()==0);
    apu.set_time(1000020834,1000000000);CHECK(sample()==1);
    apu.set_time(1001000000,1000000000);CHECK(sample()==48);
    for(int i=0;i<1000;++i)CHECK(sample()==48);
    mode(7);apu.set_time(1101000000,1000000000);CHECK(sample()==48);
    mode(15);apu.set_time(1102000000,1000000000);CHECK(sample()==96);
    mode(7);apu.mmio_write(0xfe80200c,4,0);CHECK(sample()==0);
    apu.set_time(1202000000,1000000000);CHECK(sample()==0);
    mode(15);apu.set_time(1203000000,1000000000);CHECK(sample()==48);
    for(uint32_t observed:{23u,31u}) {
        mode(observed);apu.set_time(observed==23?1303000000:1403000000,1000000000);
        CHECK(sample()==48); // physical modes2/3 with FE halted
    }
    mode(7);CHECK(failure.empty());
    apu.set_time(1402000000,1000000000);CHECK(!failure.empty());

    // Preserve arithmetic/rollover checks with a fresh, fixed-frequency clock.
    std::string wrap_failure;
    xlle::McpxApu wrap([&](const std::string& why){wrap_failure=why;});
    wrap.mmio_write(0xfe802000,4,15);
    wrap.set_time(0xffffffffull,48000);CHECK(wrap.mmio_read(0xfe80200c,4)==0xffffffffu);
    wrap.set_time(0x100000000ull,48000);CHECK(wrap.mmio_read(0xfe80200c,4)==0);
    wrap.set_time(0x100000003ull,48000);CHECK(wrap.mmio_read(0xfe80200c,4)==3);
    CHECK(wrap_failure.empty());
    wrap.set_time(0x100000004ull,48001);CHECK(!wrap_failure.empty());

    for(unsigned invalid=0;invalid<7;++invalid) {
        std::string error;xlle::McpxApu guarded([&](const std::string& why){error=why;});
        CHECK(guarded.initialize_kernel_audio_handoff());
        if(invalid==0)guarded.mmio_read(0xfe80200c,4); // no invented warm value
        else if(invalid==1)guarded.mmio_write(0xfe80200c,2,0);
        else if(invalid==2)guarded.mmio_write(0xfe80200b,4,0);
        else if(invalid==3)guarded.mmio_write(0xfe80200c,4,1);
        else {
            guarded.mmio_write(0xfe80200c,4,0);
            if(invalid==4) {guarded.mmio_write(0xfe802000,4,15);guarded.mmio_write(0xfe80200c,4,0);}
            if(invalid==5)guarded.set_time(1,0);
            if(invalid==6) {guarded.mmio_write(0xfe801100,4,8);guarded.mmio_write(0xfe802000,4,23);guarded.set_time(1,48000);}
        }
        CHECK(!error.empty());
    }
    // The conversion helper remains available to the explicitly running HLE
    // clock/USB profile. It is not a replacement for the device's gated owner.
    CHECK(xlle::apu_sample_counter(20833,1000000000)==0);
    CHECK(xlle::apu_sample_counter(20834,1000000000)==1);
    puts("PASS: physical counter gate/reset/resume, halted modes, pure reads, rollover and explicit warm/width/value/clock guards");
    return 0;
}
