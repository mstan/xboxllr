// Representative transport/ownership checks; original activation stays open.
#include "machine/nv2a_fp_registers.hpp"
#include "machine/nv2a.hpp"
#include <cstdio>
#include <string>
static int failures;
#define CHECK(c) do{if(!(c)){std::printf("FAIL line%d: %s\n",__LINE__,#c);++failures;}}while(0)
using Registers=xlle::Nv2aFpRegisters;
static uint32_t offset(unsigned i){return i<7?0x800+i*4:0x820+(i-7)*4;}
int main() {
    {
        std::string fault;xlle::Nv2a gpu([&](const std::string& why){fault=why;});
        CHECK(!gpu.establish_display_clock_routing(0));fault.clear();
        CHECK(gpu.establish_display_clock_routing(0x100a0000));
        CHECK(gpu.mmio_read(0xfd68050c,4)==0x100a0000);
        gpu.mmio_write(0xfd68050c,4,0x100a0000);CHECK(fault.empty());
        gpu.mmio_read(0xfd6013da,1);
        CHECK(fault=="NV2A input status requires established display clock/signals");fault.clear();
        gpu.set_frame_phase(999); // nominal injection cannot supply this owner
        gpu.display_clock_signals(true,false,true,false);
        CHECK(gpu.mmio_read(0xfd6013da,1)==1);
        gpu.display_clock_signals(true,true,true,true);
        CHECK(gpu.mmio_read(0xfd6013da,1)==9 && gpu.mmio_read(0xfd600100,4)==1);
        gpu.mmio_write(0xfd600100,4,1);CHECK(gpu.mmio_read(0xfd600100,4)==0);
        CHECK(gpu.mmio_read(0xfd6013da,1)==9 && gpu.mmio_read(0xfd600100,4)==0);
        gpu.display_clock_signals(true,false,false,false);CHECK(gpu.mmio_read(0xfd6013da,1)==0);
        gpu.mmio_write(0xfd6013d4,1,0x28);gpu.mmio_write(0xfd6013d5,1,0x83);
        CHECK(gpu.display_slaved());
        gpu.mmio_write(0xfd68050c,4,0);CHECK(!fault.empty());
        CHECK(gpu.mmio_read(0xfd68050c,4)==0x100a0000);
    }
    // Actual whole-word readbacks from the physical differential sequence.
    // DEBUG can precede FP_CONTROL in original AV3. It is configuration storage,
    // not an invented ready/commit trigger or an assertion of reset defaults.
    for(unsigned life=0;life<3;++life) {
        Registers owner;uint32_t value=0xcccccccc;
        CHECK(!owner.read(0x880,value) && value==0xcccccccc);
        Registers::Words captured{};captured[10]=755;
        CHECK(owner.capture_alternate(captured));
        for(uint32_t word:{0x21121111u,0x21101100u}) {
            CHECK(owner.write(0x880,word));CHECK(owner.read(0x880,value) && value==word);
            CHECK(!owner.control_known());
        }
        CHECK(!owner.write(0x880,0));CHECK(owner.read(0x880,value) && value==0x21101100);
        CHECK(owner.write(0x848,0x10000111));
        CHECK(owner.read(0x82c,value) && value==755);
        CHECK(owner.read(0x834,value) && value==0);
    }
    // Both views are independently supplied and deliberately unequal. No
    // programmed word becomes an alternate word just because time/reads pass.
    for(unsigned life=0;life<3;++life) {
        Registers owner;uint32_t value=0xcccccccc;
        CHECK(!owner.read(0x834,value) && value==0xcccccccc);
        CHECK(owner.write(0x848,0x10100111));
        CHECK(!owner.read(0x834,value) && value==0xcccccccc);
        Registers::Words captured{};
        for(unsigned i=0;i<14;++i)captured[i]=100+life*211+i*7;
        CHECK(owner.capture_alternate(captured));
        for(unsigned i=0;i<14;++i)CHECK(owner.write(offset(i),600+life*109+i*13));
        for(unsigned pass=0;pass<2;++pass) {
            CHECK(owner.write(0x848,pass?0x10100111:0x10000111));
            for(unsigned i=0;i<14;++i) {
                CHECK(owner.read(offset(i),value));
                // The first selector pass writes HVALID_START; selecting the
                // programmed view must retain that earlier write.
                CHECK(value==(pass?(i==12?1u:600+life*109+i*13):captured[i]));
            }
            // A write with either selector reaches programmed storage only.
            CHECK(owner.write(0x834,1));CHECK(owner.read(0x834,value));
            CHECK(value==(pass?1:captured[12]));
        }
        CHECK(owner.write(0x848,0x10000111));
        for(unsigned i=0;i<14;++i){CHECK(owner.read(offset(i),value));CHECK(value==captured[i]);}
        auto bad=captured;bad[13]=0x1000;CHECK(!owner.capture_alternate(bad));
        CHECK(owner.read(0x838,value) && value==captured[13]);
        CHECK(!owner.write(0x834,0x1000));CHECK(owner.programmed_extent(true)==600+life*109+7*13);
        CHECK(!owner.write(0x848,0x10100113));CHECK(owner.read(0x848,value) && value==0x10000111);
    }
    // Compose the measured real HVALID sequence through actual Nv2a MMIO,
    // sharing storage with existing nominal scanout geometry.
    for(unsigned life=0;life<3;++life) {
        std::string fault;xlle::Nv2a gpu([&](const std::string& why){fault=why;});
        const auto read=[&](uint32_t at){return gpu.mmio_read(xlle::Nv2a::kBase+at,4);};
        const auto write=[&](uint32_t at,uint32_t v){gpu.mmio_write(xlle::Nv2a::kBase+at,4,v);};
        write(0x680800,479);write(0x680820,639);CHECK(fault.empty());
        CHECK(gpu.scanout_info().width==640 && gpu.scanout_info().height==480);
        write(0x680848,0x10100111);write(0x680834,0);CHECK(read(0x680834)==0);
        Registers::Words observed{};observed[0]=479;observed[7]=639;
        CHECK(gpu.capture_fp_read_view(observed));
        write(0x680848,0x10000111);CHECK(read(0x680834)==0);
        write(0x680834,1);CHECK(read(0x680834)==0);
        write(0x680848,0x10100111);CHECK(read(0x680834)==1);
        write(0x680834,0);write(0x680848,0x10000111);CHECK(read(0x680834)==0);
        write(0x680834,0);write(0x680848,0x10100111);CHECK(read(0x680834)==0 && fault.empty());
        CHECK(gpu.scanout_info().width==640 && gpu.scanout_info().height==480);
    }
    // Unknown alternate view and unsupported sizes cannot fabricate a value
    // or alter the programmed extent. Fresh device lifetimes inherit nothing.
    for(unsigned life=0;life<3;++life) {
        std::string fault;xlle::Nv2a gpu([&](const std::string& why){fault=why;});
        gpu.mmio_write(xlle::Nv2a::kBase+0x680820,4,639);
        gpu.mmio_write(xlle::Nv2a::kBase+0x680848,4,0x10000111);
        gpu.mmio_read(xlle::Nv2a::kBase+0x680834,4);
        CHECK(fault=="NV2A FP alternate read view is unestablished");
        fault.clear();gpu.mmio_write(xlle::Nv2a::kBase+0x680820,1,1);
        CHECK(fault=="NV2A FP register write size/alignment");CHECK(gpu.scanout_info().width==640);
    }
    {
        std::string fault;xlle::Nv2a gpu([&](const std::string& why){fault=why;});
        CHECK(gpu.establish_av_idle_phase(0));gpu.mmio_write(0xfd6806a0,4,1);
        gpu.mmio_write(0xfd680880,4,0x21121111);
        CHECK(gpu.mmio_read(0xfd680880,4)==0x21121111 && fault.empty());
        CHECK(gpu.mmio_read(0xfd6806a0,4)==1); // DEBUG cannot complete pending AV
        gpu.mmio_write(0xfd680880,4,0x21101100);
        CHECK(gpu.mmio_read(0xfd6806a0,4)==1);
        gpu.mmio_write(0xfd680880,1,0x11);
        CHECK(fault=="NV2A FP register write size/alignment");
        CHECK(gpu.mmio_read(0xfd680880,4)==0x21101100);
        fault.clear();gpu.mmio_read(0xfd680881,1);
        CHECK(fault=="NV2A FP register read size/alignment");
    }
    std::printf("FP register banks, measured MMIO sequence, fresh owners and retained activation guards: %s\n",failures?"FAIL":"PASS");
    return failures?1:0;
}
