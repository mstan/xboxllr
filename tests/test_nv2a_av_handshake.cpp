// Physical request/ack sequences through the maintained MMIO and clock owners.
#include "machine/nv2a.hpp"
#include <cstdio>
#include <string>
static int failures;
#define CHECK(c) do{if(!(c)){std::printf("FAIL line%d: %s\n",__LINE__,#c);++failures;}}while(0)
#include "nv2a_timer_cases.hpp"
int main() {
    test_ptimer_reference(); // Same source-clock method; reuse the timer oracle.
    constexpr uint32_t av=0xfd6806a0,control=0xfd680848,valid=0xfd680834;
    for(uint32_t initial_phase:{0u,1u}) {
        uint32_t phase=initial_phase;
        std::string fault;xlle::Nv2a gpu([&](const std::string& why){fault=why;});
        CHECK(gpu.establish_av_idle_phase(phase));
        gpu.set_timer_clock(100);
        // The independently observed alternate bank is deliberately distinct.
        xlle::Nv2aFpRegisters::Words observed{};
        CHECK(gpu.capture_fp_read_view(observed));
        gpu.mmio_write(control,4,0x10100111);gpu.mmio_write(valid,4,1);
        uint64_t clock=100;
        for(unsigned transition=0;transition<2;++transition) {
            const uint32_t before=phase*0x11,request=phase^1;
            CHECK(gpu.mmio_read(av,4)==before);
            gpu.mmio_write(av,4,request);
            const uint32_t pending=request|(phase<<4);
            for(unsigned poll=0;poll<8;++poll)CHECK(gpu.mmio_read(av,4)==pending);
            gpu.set_timer_clock(clock);CHECK(gpu.mmio_read(av,4)==pending);
            gpu.set_frame(transition+1);gpu.set_frame_phase(999);
            CHECK(gpu.mmio_read(av,4)==pending);
            gpu.set_timer_clock(++clock); // PTIMER ratio remains unconfigured
            phase=request;CHECK(gpu.mmio_read(av,4)==phase*0x11);
            gpu.mmio_write(control,4,0x10000111);CHECK(gpu.mmio_read(valid,4)==0);
            gpu.mmio_write(control,4,0x10100111);CHECK(gpu.mmio_read(valid,4)==1);
            CHECK(fault.empty());
        }
        gpu.set_timer_clock(clock-1);CHECK(fault=="NV2A PTIMER source clock reversed");
        CHECK(gpu.mmio_read(av,4)==phase*0x11);
    }
    for(unsigned invalid=0;invalid<5;++invalid) {
        std::string fault;xlle::Nv2a gpu([&](const std::string& why){fault=why;});
        CHECK(gpu.establish_av_idle_phase(0));
        if(invalid==0)gpu.mmio_write(av,4,2);
        if(invalid==1)gpu.mmio_write(av,4,0); // unobserved same-phase submission
        if(invalid==2)gpu.mmio_write(av,1,1);
        if(invalid==3)gpu.mmio_write(av+1,4,1);
        if(invalid==4)CHECK(!gpu.establish_av_idle_phase(1));
        CHECK(!fault.empty());CHECK(gpu.mmio_read(av,4)==0);
    }
    {
        std::string fault;xlle::Nv2a gpu([&](const std::string& why){fault=why;});
        CHECK(gpu.establish_av_idle_phase(1));gpu.mmio_write(av,4,0);
        CHECK(gpu.mmio_read(av,4)==0x10);
        gpu.mmio_write(av,4,1);CHECK(!fault.empty());
        CHECK(gpu.mmio_read(av,4)==0x10); // pending request cannot be replaced
    }
    {
        std::string fault;xlle::Nv2a gpu([&](const std::string& why){fault=why;});
        bool unknown=false;gpu.on_unmodeled([&](bool,uint32_t,uint32_t,const char*,uint32_t){unknown=true;});
        gpu.mmio_read(av,4);CHECK(unknown); // no generic reset default added
        CHECK(!gpu.establish_av_idle_phase(2));CHECK(!fault.empty());
    }
    std::printf("AV both phases, passive pending reads, source-clock service, retained FP banks and guards: %s\n",failures?"FAIL":"PASS");
    return failures?1:0;
}
