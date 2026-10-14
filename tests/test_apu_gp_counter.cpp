// GP original composite-profile owner. Timing precision remains nominal.
#include "machine/apu_ep_processor.hpp"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"GP counter FAIL %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
struct CounterFixture {
    std::vector<uint8_t> ram=std::vector<uint8_t>(0x90000,0xa5),before;
    std::string device_error,owner_error,expected_error;bool terminal_guard=false;
    unsigned reads=0;
    xlle::McpxApu apu{[this](const std::string& s){device_error=s;}};
    CounterFixture() {
        apu.set_mem_access([this](uint32_t a,void* p,size_t n){++reads;if(uint64_t(a)+n>ram.size())return false;std::memcpy(p,ram.data()+a,n);return true;},
            [this](uint32_t a,const void* p,size_t n){if(uint64_t(a)+n>ram.size())return false;std::memcpy(ram.data()+a,p,n);return true;});
        apu.mmio_write(0xfe802040,4,0xc000);apu.mmio_write(0xfe8020d4,4,3);
        apu.mmio_write(0xfe802048,4,0xc000);apu.mmio_write(0xfe8020dc,4,3);
        for(unsigned i=0;i<4;++i){const uint32_t page=0x60000+i*0x4000;std::memcpy(ram.data()+0xc000+i*8,&page,4);}
    }
    void boot(const std::vector<uint32_t>& p) {
        CHECK(p.size()<=2048);
        for(unsigned i=0;i<2048;++i){uint32_t value=i<p.size()?p[i]:0x87;
            std::memcpy(ram.data()+0x60000+(i/1024)*0x4000+(i%1024)*4,&value,4);}
        before=ram;
    }
    void fault(const std::string& s) {
        owner_error=s;
        if(terminal_guard) {
            CHECK(s.find(expected_error)!=std::string::npos && ram==before);
            CHECK(!apu.gp_dma_nodes() && !apu.ep_dma_nodes());
            std::fprintf(stderr,"expected counter boundary: %s\n",s.c_str());std::exit(2);
        }
    }
};
static void composite_boundaries() {
    xlle::ApuDspCounterProfile p;
    CHECK(!p.touched() && !p.active() && p.observe(123)==0);
    for(unsigned stage=0;stage<3;++stage) {
        const uint32_t a[]={0xffffb2,0xffffb0,0xffffb1},v[]={0xffffff,1,1};
        for(auto bad:std::vector<std::pair<uint32_t,uint32_t>>{{a[stage],v[stage]^1},{0xffffb3,1},{0xffffb0,0x1000001},{a[(stage+1)%3],v[(stage+1)%3]}}) {
            CHECK(!p.write(bad.first,bad.second));CHECK(p.permits(a[stage],v[stage]));CHECK(p.observe(100)==0);
        }
        CHECK(p.write(a[stage],v[stage]));CHECK(p.touched() && !p.active());
    }
    CHECK(p.observe(100)==0);p.retired(100);CHECK(p.active());
    CHECK(p.observe(100)==0 && p.observe(100)==0 && p.observe(120)==20);
    p.retired(200);CHECK(p.observe(120)==20); // Later retirement never re-epochs.
    CHECK(!p.write(0xffffb2,0xffffff) && p.observe(120)==20);
    CHECK(p.observe(100+0x1000005)==5); // Explicit nominal 24-bit exposure.
}
static std::vector<uint32_t> observer() {
    std::vector<uint32_t> p;
    auto sample=[&](unsigned target){p.insert(p.end(),{0x44f000,0xffffb3,0x447000,target});};
    sample(0x100);p.insert(p.end(),16,0);sample(0x101);
    p.insert(p.end(),{0x07f432,0xffffff});sample(0x102);
    p.insert(p.end(),{0x07f430,1});sample(0x103);
    p.insert(p.end(),{0x07f431,1});sample(0x104);
    p.insert(p.end(),16,0);sample(0x105);p.insert(p.end(),256,0);sample(0x106);p.push_back(0x87);return p;
}
static void real_retired_progress() {
    for(unsigned budget:{1u,2u,64u,4096u}) {
        CounterFixture f;f.boot(observer());xlle::ApuGpProcessor owner(f.apu,[&](const std::string& s){f.fault(s);});
        CHECK(owner.bootstrap_from_scratch());xlle::ApuGpRun r;unsigned calls=0;
        do{CHECK(++calls<1024 && owner.run(budget,r));}while(!r.power);
        CHECK(r.power==2 && owner.phase()==xlle::ApuGpPhase::Stopped && !r.yielded);
        const unsigned expected[]={0,0,0,0,0,20,280};
        for(unsigned i=0;i<7;++i)CHECK(*f.apu.gp_word(0x100+i)==expected[i]);
        CHECK(f.ram==f.before && !f.apu.gp_dma_nodes() && f.device_error.empty());
        auto banks=std::vector<uint32_t>(f.apu.gp_word(0),f.apu.gp_word(0)+0x1000);
        CHECK(!owner.reset_cpu() && owner.phase()==xlle::ApuGpPhase::Failed);
        CHECK(std::equal(banks.begin(),banks.end(),f.apu.gp_word(0)) && f.ram==f.before);
    }
}
static void lifecycle_guards() {
    for(bool complete:{false,true}) {
        CounterFixture f;f.boot(complete?observer():std::vector<uint32_t>{0x07f432,0xffffff,0x87});
        {
            xlle::ApuGpProcessor owner(f.apu,[&](const std::string& s){f.fault(s);});
            CHECK(owner.bootstrap_from_scratch());xlle::ApuGpRun r;CHECK(owner.run(4096,r) && r.power==2);
        }
        auto p=std::vector<uint32_t>(f.apu.gp_word(0x2800),f.apu.gp_word(0x2800)+4096);const unsigned reads=f.reads;
        xlle::ApuGpProcessor next(f.apu,[&](const std::string& s){f.fault(s);});
        CHECK(next.phase()==xlle::ApuGpPhase::Failed && !next.bootstrap_from_scratch() && f.reads==reads);
        CHECK(f.owner_error.find("warm rebind is unqualified")!=std::string::npos);
        CHECK(std::equal(p.begin(),p.end(),f.apu.gp_word(0x2800)) && f.ram==f.before);
    }
    CounterFixture untouched;untouched.boot({0x87});xlle::ApuGpProcessor gp(untouched.apu,[&](const std::string& s){untouched.fault(s);});
    CHECK(gp.reset_cpu() && gp.bootstrap_from_scratch());xlle::ApuGpRun r;CHECK(gp.run(32,r));CHECK(gp.reset_cpu());
}
static void frozen_observer(const char* path) {
    FILE* f=std::fopen(path,"rb");CHECK(f);std::vector<uint32_t> p(4096);
    CHECK(std::fread(p.data(),4,p.size(),f)==p.size() && std::fgetc(f)==EOF);std::fclose(f);p.resize(2048);
    CounterFixture fixture;fixture.boot(p);xlle::ApuGpProcessor owner(fixture.apu,[&](const std::string& s){fixture.fault(s);});
    CHECK(owner.bootstrap_from_scratch());xlle::ApuGpRun r;CHECK(owner.run(20000,r) && r.power==2 && !r.yielded);
    const unsigned expected[]={0,0,0,0,0,20,280,7,3,5,0x42334f,0,2};
    for(unsigned i=0;i<13;++i)CHECK(*fixture.apu.gp_word(0x100+i)==expected[i]);
    std::array<unsigned,8> witnesses{};
    for(unsigned pc=0;pc+3<533;++pc)
        if(p[pc]==0x44f400 && p[pc+2]==0x447000 && p[pc+3]>=0x110 && p[pc+3]<0x118)
            witnesses[p[pc+3]-0x110]=p[pc+1];
    for(unsigned i=0;i<8;++i)CHECK(witnesses[i] && *fixture.apu.gp_word(0x110+i)==witnesses[i]);
    CHECK(std::equal(p.begin(),p.end(),fixture.apu.gp_word(0x2800)));
    CHECK(fixture.ram==fixture.before && !fixture.apu.gp_dma_nodes());
    std::puts("Frozen hardware observer: passive nominal profile predicts all seven raw samples and preserves exact witnesses/P/RAM.");
}
static void original_gp_prefix(const char* path) {
    FILE* f=std::fopen(path,"rb");CHECK(f);std::vector<uint32_t> p(371);
    CHECK(std::fread(p.data(),4,p.size(),f)==p.size() && std::fgetc(f)==EOF);std::fclose(f);
    CounterFixture fixture;fixture.boot(p);xlle::ApuGpProcessor owner(fixture.apu,[&](const std::string& s){fixture.fault(s);});
    CHECK(owner.bootstrap_from_scratch());xlle::ApuGpRun r;CHECK(owner.run(20000,r));
    CHECK(r.yielded && r.pc==0x2b && r.power==0 && owner.phase()==xlle::ApuGpPhase::Yielded);
    CHECK(!fixture.apu.gp_dma_nodes() && !fixture.apu.gp_frame_outstanding() && fixture.ram==fixture.before);
    CHECK(fixture.owner_error.empty() && fixture.device_error.empty());
    const unsigned node[]={0x401e,0x49e2,0x280,0x1560,0xb000};
    for(unsigned i=0;i<5;++i)CHECK(*fixture.apu.gp_word(0x1e+i)==node[i]);
    // No fake frame/wake/resume: only actual original entry to its first C4 yield.
    std::puts("Original GP prefix: exact triplet continues to first C4 yield, no DMA/frame/output fabricated.");
}
static void rejected(const std::string& mode) {
    CounterFixture f;std::vector<uint32_t> p;
    if(mode=="wrong-value")p={0x07f432,0xfffffe,0x87};
    else if(mode=="wrong-order")p={0x07f430,1,0x87};
    else if(mode=="repeat")p={0x07f432,0xffffff,0x07f432,0xffffff,0x87};
    else if(mode=="read-config")p={0x44f000,0xffffb2,0x87};
    else p={0x07f432,0xfffffe,0x87};
    f.boot(p);f.terminal_guard=true;
    f.expected_error=mode=="read-config"?"at ffffb2":"counter configuration profile";
    if(mode=="ep") {xlle::ApuEpProcessor owner(f.apu,[&](const std::string& s){f.fault(s);});CHECK(owner.bootstrap_from_scratch());xlle::ApuGpRun r;owner.run(64,r);}
    else {xlle::ApuGpProcessor owner(f.apu,[&](const std::string& s){f.fault(s);});CHECK(owner.bootstrap_from_scratch());xlle::ApuGpRun r;owner.run(64,r);}
    CHECK(false);
}
int main(int argc,char** argv) {
    if(argc==3 && std::string(argv[1])=="--observer"){frozen_observer(argv[2]);return 0;}
    if(argc==3 && std::string(argv[1])=="--original"){original_gp_prefix(argv[2]);return 0;}
    if(argc==2){rejected(argv[1]);return 1;}
    CHECK(argc==1);composite_boundaries();real_retired_progress();lifecycle_guards();
    std::puts("GP counter: passive progress, retirement epoch, budgets, lifecycle/rebind and strict boundaries passed.");
}
