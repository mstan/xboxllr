// Physical zero publications and ordinary null target through the RAM owner.
#include "machine/apu.hpp"
#include "../pilots/native-title/audio_device_apu_contract.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"publication FAIL %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
struct Fixture {
    std::vector<uint8_t> ram=std::vector<uint8_t>(0x10000,0xa5);
    std::string error;unsigned writes=0;bool reject=false;
    xlle::McpxApu apu{[this](const std::string& e){error=e;}};
    Fixture() {
        apu.set_mem_access([this](uint32_t a,void* p,size_t n){
            if(uint64_t(a)+n>ram.size())return false;
            std::memcpy(p,ram.data()+a,n);return true;
        },[this](uint32_t a,const void* p,size_t n){
            if(reject || uint64_t(a)+n>ram.size())return false;
            std::memcpy(ram.data()+a,p,n);++writes;return true;
        });
        apu.mmio_write(0xfe80202c,4,0x1000);apu.mmio_write(0xfe8202f8,4,64);
    }
};
int main() {
    for(unsigned voice:{64u,96u,255u}) {
        Fixture f;f.apu.mmio_write(0xfe8202f8,4,voice);
        auto expected=f.ram;
        for(unsigned method:{0x350u,0x370u}) {
            CHECK(pilot_original_apu_admitted(0x20000+method,4,1,0));
            f.apu.mmio_write(0xfe820000+method,4,0);
            std::memset(expected.data()+0x1000+voice*128+(method-0x300),0,4);
            CHECK(f.ram==expected && f.error.empty());
        }
        CHECK(f.writes==2);
        CHECK(pilot_original_apu_admitted(0x2031c,4,1,0xffff));
        f.apu.mmio_write(0xfe82031c,4,0xffff);
        // Low16-bit field is independently defined by the reference; hardware
        // corroborates the reached initial0 -> FFFF case, not arbitrary high bits.
        expected[0x1000+voice*128+0x1c]=0xff;
        expected[0x1000+voice*128+0x1d]=0xff;
        CHECK(f.ram==expected && f.error.empty() && f.writes==3);
    }
    for(unsigned method:{0x350u,0x370u})for(unsigned bad=0;bad<6;++bad) {
        Fixture f;const auto before=f.ram;unsigned address=0xfe820000+method;
        if(bad==0)f.apu.mmio_write(address,4,1);
        if(bad==1)f.apu.mmio_write(address,1,0);
        if(bad==2)f.apu.mmio_write(address+1,4,0);
        if(bad==3)(void)f.apu.mmio_read(address,4);
        if(bad==4){f.reject=true;f.apu.mmio_write(address,4,0);}
        if(bad==5){f.apu.mmio_write(0xfe80202c,4,0xfffff000);f.apu.mmio_write(address,4,0);}
        CHECK(!f.error.empty() && f.ram==before && !f.writes);
        f.apu.mmio_write(address,4,0);CHECK(f.ram==before && !f.writes);
    }
    for(unsigned method:{0x350u,0x370u}) {
        CHECK(!pilot_original_apu_admitted(0x20000+method,4,1,1));
        CHECK(!pilot_original_apu_admitted(0x20000+method,4,0,0));
        CHECK(!pilot_original_apu_admitted(0x20000+method,1,1,0));
    }
    for(unsigned bad=0;bad<7;++bad) {
        Fixture f;const auto before=f.ram;
        if(bad==0)f.apu.mmio_write(0xfe82031c,4,0);
        if(bad==1)f.apu.mmio_write(0xfe82031c,1,0xff);
        if(bad==2)f.apu.mmio_write(0xfe82031d,4,0xffff);
        if(bad==3)(void)f.apu.mmio_read(0xfe82031c,4);
        if(bad==4){f.reject=true;f.apu.mmio_write(0xfe82031c,4,0xffff);}
        if(bad==5){f.apu.mmio_write(0xfe80202c,4,0xfffff000);f.apu.mmio_write(0xfe82031c,4,0xffff);}
        if(bad==6){f.apu.mmio_write(0xfe8202f8,4,63);f.apu.mmio_write(0xfe82031c,4,0xffff);}
        CHECK(!f.error.empty() && f.ram==before && !f.writes);
    }
    CHECK(!pilot_original_apu_admitted(0x2031c,4,1,0));
    CHECK(!pilot_original_apu_admitted(0x2031c,4,0,0xffff));
    std::puts("PASS owned zero/null voice publications, isolation, failed DMA and retained non-null/spatial guards");
}
