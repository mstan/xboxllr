// Original FE headroom group, ordinary sends and guarded spatial scope.
#include "machine/apu.hpp"
#include "../pilots/native-title/audio_device_apu_contract.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#define CHECK(x) do {if(!(x)){std::fprintf(stderr,"headroom FAIL %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
struct Fixture {
    std::vector<uint8_t> ram=std::vector<uint8_t>(0x10000);
    std::string error;
    xlle::McpxApu apu{[this](const std::string& e){error=e;}};
    Fixture() {
        apu.set_mem_access([this](uint32_t a,void *p,size_t n){
            if(uint64_t(a)+n>ram.size())return false;
            std::memcpy(p,ram.data()+a,n);return true;
        },[this](uint32_t a,const void *p,size_t n){
            if(uint64_t(a)+n>ram.size())return false;
            std::memcpy(ram.data()+a,p,n);return true;
        });
        apu.mmio_write(0xfe80202c,4,0x1000);
        put(0x3004,0xe000);put(0x3060,0xffff000f);put(0x3064,0xffffffff);put(0x3068,0xffffffff);
    }
    void put(uint32_t a,uint32_t value){for(unsigned i=0;i<4;++i)ram[a+i]=uint8_t(value>>(8*i));}
    float mix(unsigned bin) {
        CHECK(bin<32);put(0x3000,bin);
        const auto before=ram;const float source[2]={1,-0.25f};float bins[1024]={};
        CHECK(apu.mix_2d_samples(64,source,1,bins));CHECK(before==ram);
        for(unsigned i=0;i<1024;++i)if(i!=bin*32)CHECK(bins[i]==0);
        return bins[bin*32];
    }
};
int main(int argc,char **argv) {
    if(argc==2 && !std::strcmp(argv[1],"--transport")) {
        Fixture f;std::puts("READY");std::fflush(stdout);
        char op;unsigned address,size,value;
        while(std::scanf(" %c %x %u %x",&op,&address,&size,&value)==4) {
            if(op=='H') {uint8_t amount=0xff;bool known=f.apu.hrtf_headroom(amount);std::printf("HEADROOM %u %u\n",unsigned(known),unsigned(amount));}
            else if(op=='M')std::printf("MIX %.9g\n",double(f.mix(address)));
            else {
                CHECK(op=='R' || op=='W');
                CHECK(address>=0xfe800000 && address<0xfe880000);
                CHECK(pilot_original_apu_admitted(address-0xfe800000,size,op=='W',value));
                if(op=='W')f.apu.mmio_write(address,int(size),value);
                else value=f.apu.mmio_read(address,int(size));
                CHECK(f.error.empty());std::printf("OK %08x\n",value);
            }
            std::fflush(stdout);
        }
        return 0;
    }
    CHECK(argc==1);
    Fixture f;uint8_t amount=0xa5;CHECK(!f.apu.hrtf_headroom(amount) && amount==0xa5);
    for(unsigned h=0;h<8;++h) {
        f.apu.mmio_write(0xfe820280,4,h);CHECK(f.apu.hrtf_headroom(amount) && amount==h);
        for(unsigned bin=0;bin<32;++bin) {
            unsigned ordinary=(bin+h)&7;f.apu.mmio_write(0xfe820200+bin*4,4,ordinary);
            CHECK(f.mix(bin)==1.0f/float(1u<<ordinary));
        }
    }
    for(unsigned bad=0;bad<5;++bad) {
        Fixture g;g.apu.mmio_write(0xfe820280,4,3);
        if(bad==0)g.apu.mmio_write(0xfe820280,4,8);
        if(bad==1)g.apu.mmio_write(0xfe820280,1,1);
        if(bad==2)g.apu.mmio_write(0xfe820281,4,1);
        if(bad==3)(void)g.apu.mmio_read(0xfe820280,4);
        if(bad==4){float source[2]={1,1},bins[1024]={};CHECK(!g.apu.mix_2d_samples(0,source,1,bins));}
        CHECK(!g.error.empty() && g.apu.hrtf_headroom(amount) && amount==3);
        g.apu.mmio_write(0xfe820280,4,2);CHECK(g.apu.hrtf_headroom(amount) && amount==3);
    }
    CHECK(pilot_original_apu_admitted(0x20280,4,1,0));
    CHECK(!pilot_original_apu_admitted(0x20280,4,1,8));
    CHECK(!pilot_original_apu_admitted(0x20280,4,0,0));
    CHECK(!pilot_original_apu_admitted(0x20280,1,1,0));
    CHECK(!pilot_original_apu_admitted(0x20284,4,1,0));
    std::puts("PASS explicit HRTF method state, ordinary bin gains/isolation, original widths and retained spatial guard");
}
