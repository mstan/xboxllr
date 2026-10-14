// Actual private guarded donor: hardware-resident SEN only, no spill or clock claim.
// Generated C header uses C23 typed enums; retain the same uint32 ABI in C++.
extern "C" {
#define __STDC_VERSION__ 202311L
#include "dsp56300.h"
#undef __STDC_VERSION__
}
#include <array>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#define CHECK(v) do{if(!(v)){std::fprintf(stderr,"SEN check %d: %s\n",__LINE__,#v);std::exit(1);}}while(0)
struct Fixture {
    std::array<uint32_t,3072> x{};
    std::array<uint32_t,256> y{};
    std::array<uint32_t,4096> p{},original{};
    Dsp56300State last{};Dsp56300Jit* cpu=nullptr;
    unsigned writes=0,max_depth=0;bool prefix=false;
    uint32_t workspace=0,workspace_words=0;
    static uint32_t read(void*,uint32_t){CHECK(false);return 0;}
    static void write(void* opaque,uint32_t a,uint32_t v) {
        auto& f=*static_cast<Fixture*>(opaque);
        if(f.prefix && a==0xffffc5 && v==0xfff) {
            CHECK(f.writes==3 && f.last.pc==0x2ef && f.last.registers[DSP56300_REG_SP]==2 && f.last.registers[DSP56300_REG_SC]==2);
            CHECK(f.max_depth==3 && f.last.registers[DSP56300_REG_EP]==0xbe0 && f.last.registers[DSP56300_REG_SZ]==32);
            CHECK(f.last.registers[DSP56300_REG_OMR]==0x100300);
            CHECK(f.p[1]==0 && f.p[2]==1 && f.p[3]==1 && f.p[4]==0 && f.p[5]==0);
            CHECK(f.p[0x19e]==f.workspace && f.p[0x19f]==f.workspace_words);
            for(unsigned i=0x122;i<0x140;++i)CHECK(f.p[i]==0);
            for(unsigned i=0;i<4096;++i){if(i==1 || i==2 || i==3 || (i>=0x122 && i<0x140) || i==0x19e || i==0x19f)continue;CHECK(f.p[i]==f.original[i]);}
            CHECK(std::all_of(f.x.begin(),f.x.end(),[](unsigned v){return v==0x654321;}));
            CHECK(std::all_of(f.y.begin(),f.y.end(),[](unsigned v){return v==0x123456;}));
            std::puts("{\"boundary\":\"original_EP_C5_FFF_attempt_before_yield\",\"pc\":751,\"sp\":2,\"sc\":2,\"maximum_depth\":3,\"workspace_P_words_cleared\":30,\"EP\":3040,\"SZ\":32,\"extension_memory_unchanged\":true,\"acknowledgement_supplied\":false}");
            std::exit(2);
        }
        const unsigned addresses[]={0xffffb2,0xffffb0,0xffffb1},values[]={0xffffff,1,1};
        CHECK(f.prefix && f.writes<3 && a==addresses[f.writes] && v==values[f.writes]);++f.writes;
    }
    Fixture() {
        x.fill(0x654321);y.fill(0x123456);p.fill(0x87);
    }
    void create() {
        Dsp56300MemoryRegion xr[2]{};xr[0]={0,3072,DSP56300_REGION_BUFFER};xr[0].data.buffer={x.data(),0};
        xr[1]={0xffff80,0x1000000,DSP56300_REGION_CALLBACK};xr[1].data.callback={this,read,write};
        Dsp56300MemoryRegion yr={0,256,DSP56300_REGION_BUFFER};yr.data.buffer={y.data(),0};
        Dsp56300MemoryRegion pr={0,4096,DSP56300_REGION_BUFFER};pr.data.buffer={p.data(),0};
        Dsp56300CreateInfo info{{xr,2,&yr,1,&pr,1}};cpu=dsp56300_create(&info);CHECK(cpu);
    }
    ~Fixture(){dsp56300_destroy(cpu);}
    void steps() {
        for(unsigned n=0;n<4000;++n) {
            dsp56300_get_state(cpu,&last);max_depth=std::max(max_depth,last.registers[DSP56300_REG_SC]);
            if(last.power_state==2)return;
            dsp56300_step(cpu);
        }
        CHECK(false);
    }
};
static void negative(const std::string& mode,bool block) {
    Fixture f;
    const uint32_t setup[]={0x0500bb,0x05f42a,0xbe0,0x0520b8,0x0afa74};
    std::copy(std::begin(setup),std::end(setup),f.p.begin());unsigned at=5;
    if(mode=="empty-disable"){f.p[at]=0x05f43a;f.p[at+1]=0x300;}
    else if(mode=="enable-unset-sz"){f.p[3]=0x0500b8;}
    else if(mode=="status-history"){f.p[at]=0x05f43a;f.p[at+1]=0x180300;}
    else if(mode=="sp")f.p[at]=0x0500bb;
    else if(mode=="sc")f.p[at]=0x0500b1;
    else if(mode=="sz")f.p[at]=0x0500b8;
    else if(mode=="ep")f.p[at]=0x0500aa;
    else if(mode=="ssh-write")f.p[at]=0x0500bc;
    else if(mode=="ssl-write")f.p[at]=0x0500bd;
    else if(mode=="ssh-read")f.p[at]=0x0444bc;
    else if(mode=="ssl-read")f.p[at]=0x0444bd;
    else if(mode=="underflow")f.p[at]=0x0c;
    else if(mode=="enddo")f.p[at]=0x00008c;
    else if(mode=="spill") {f.p[at]=0x0bf080;f.p[at+1]=0x20;f.p[0x20]=0x0bf080;f.p[0x21]=0x20;}
    else if(mode=="disable-occupied") {f.p[at]=0x0bf080;f.p[at+1]=0x20;f.p[0x20]=0x05f43a;f.p[0x21]=0x300;}
    else if(mode=="sz-overflow") {f.p[3]=0x0501b8;f.p[at]=0x0bf080;f.p[at+1]=0x20;f.p[0x20]=0x0bf080;f.p[0x21]=0x40;}
    else CHECK(false);
    f.create();if(block)dsp56300_run(f.cpu,2000);else f.steps();
    if(mode=="empty-disable") {
        dsp56300_get_state(f.cpu,&f.last);
        CHECK(f.last.power_state==2 && f.last.registers[DSP56300_REG_OMR]==0x300 && f.last.registers[DSP56300_REG_SP]==0 && f.last.registers[DSP56300_REG_SC]==0);
        CHECK(f.last.registers[DSP56300_REG_EP]==0xbe0 && f.last.registers[DSP56300_REG_SZ]==32);
        CHECK(std::all_of(f.x.begin(),f.x.end(),[](unsigned v){return v==0x654321;}) && std::all_of(f.y.begin(),f.y.end(),[](unsigned v){return v==0x123456;}));
        std::puts("Empty SEN enable/disable preserved stack/config/data");return;
    }
    CHECK(false);
}
int main(int argc,char** argv) {
    if(argc==3 && std::string(argv[1])=="--original") {
        Fixture f;f.prefix=true;FILE* in=std::fopen(argv[2],"rb");CHECK(in);
        CHECK(std::fread(f.p.data(),4,2048,in)==2048 && std::fgetc(in)==EOF);std::fclose(in);
        for(auto& v:f.p)v&=0xffffff;f.original=f.p;f.workspace=f.p[0x122];f.workspace_words=f.p[0x123];
        f.create();f.steps();CHECK(false);
    }
    CHECK(argc==3 && (std::string(argv[2])=="single" || std::string(argv[2])=="block"));negative(argv[1],std::string(argv[2])=="block");
}
