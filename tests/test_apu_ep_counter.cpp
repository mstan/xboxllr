// Observed EP passive composite-profile owner; cadence/threshold stay guarded.
#include "machine/apu_ep_processor.hpp"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"EP counter FAIL %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
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
static std::vector<uint32_t> observer() {
    std::vector<uint32_t> p;
    auto sample=[&](unsigned target){p.insert(p.end(),{0x44f000,0xffffb3,0x447000,target});};
    sample(0x100);p.insert(p.end(),16,0);sample(0x101);
    p.insert(p.end(),{0x07f432,0xffffff});sample(0x102);
    p.insert(p.end(),{0x07f430,1});sample(0x103);
    p.insert(p.end(),{0x07f431,1});sample(0x104);
    p.insert(p.end(),16,0);sample(0x105);p.insert(p.end(),256,0);sample(0x106);p.push_back(0x87);return p;
}
static void ep_progress() {
    for(unsigned budget:{1u,2u,64u,4096u}) {
        CounterFixture f;f.boot(observer());
        std::vector<unsigned> gp(f.apu.gp_word(0),f.apu.gp_word(0)+0x3800);
        xlle::ApuEpProcessor ep(f.apu,[&](const std::string& s){f.fault(s);});
        CHECK(ep.bootstrap_from_scratch());
        std::vector<unsigned> before(f.apu.ep_word(0),f.apu.ep_word(0)+0x3800);
        xlle::ApuGpRun r;unsigned calls=0;
        do {CHECK(++calls<1024 && ep.run(budget,r));}while(!r.power);
        CHECK(r.power==2 && !r.yielded && ep.phase()==xlle::ApuGpPhase::Stopped);
        const unsigned expected[]={0,0,0,0,0,20,280};
        for(unsigned i=0;i<0x3800;++i)CHECK(*(f.apu.ep_word(0)+i)==(i>=0x100 && i<0x107?expected[i-0x100]:before[i]));
        CHECK(std::equal(gp.begin(),gp.end(),f.apu.gp_word(0)) && f.ram==f.before);
        CHECK(!f.apu.gp_dma_nodes() && !f.apu.ep_dma_nodes() && f.owner_error.empty() && f.device_error.empty());
        const auto after=std::vector<unsigned>(f.apu.ep_word(0),f.apu.ep_word(0)+0x3800);
        CHECK(!ep.reset_cpu() && ep.phase()==xlle::ApuGpPhase::Failed);
        CHECK(std::equal(after.begin(),after.end(),f.apu.ep_word(0)) && f.ram==f.before);
    }
}
static void independent_lifetimes() {
    for(bool ep_first:{false,true}) {
        for(bool complete:{false,true}) {
            CounterFixture f;f.boot(complete?observer():std::vector<unsigned>{0x07f432,0xffffff,0x87});
            {
                std::unique_ptr<xlle::ApuGpProcessor> gp;
                std::unique_ptr<xlle::ApuEpProcessor> ep;
                if(ep_first)ep=std::make_unique<xlle::ApuEpProcessor>(f.apu,[&](const std::string& s){f.fault(s);});
                else gp=std::make_unique<xlle::ApuGpProcessor>(f.apu,[&](const std::string& s){f.fault(s);});
                xlle::ApuGpRun r;
                CHECK(ep_first?(ep->bootstrap_from_scratch() && ep->run(4096,r)):(gp->bootstrap_from_scratch() && gp->run(4096,r)));
                CHECK(r.power==2);
                CHECK(ep_first?!ep->reset_cpu():!gp->reset_cpu());
            }
            // Same-kind rebind rejects before source reads or program mutation.
            const unsigned reads=f.reads;
            const auto gp_saved=std::vector<unsigned>(f.apu.gp_word(0),f.apu.gp_word(0)+0x3800);
            const auto ep_saved=std::vector<unsigned>(f.apu.ep_word(0),f.apu.ep_word(0)+0x3800);
            if(ep_first){xlle::ApuEpProcessor next(f.apu,[&](const std::string& s){f.fault(s);});CHECK(next.phase()==xlle::ApuGpPhase::Failed && !next.bootstrap_from_scratch());}
            else {xlle::ApuGpProcessor next(f.apu,[&](const std::string& s){f.fault(s);});CHECK(next.phase()==xlle::ApuGpPhase::Failed && !next.bootstrap_from_scratch());}
            CHECK(f.reads==reads && f.owner_error.find("warm rebind")!=std::string::npos);
            CHECK(std::equal(gp_saved.begin(),gp_saved.end(),f.apu.gp_word(0)) && std::equal(ep_saved.begin(),ep_saved.end(),f.apu.ep_word(0)));
            // Other kind has its own untouched profile, never clears first kind.
            if(ep_first){xlle::ApuGpProcessor other(f.apu,[&](const std::string& s){f.fault(s);});CHECK(other.reset_cpu() && other.bootstrap_from_scratch());xlle::ApuGpRun r;CHECK(other.run(4096,r) && r.power==2);}
            else {xlle::ApuEpProcessor other(f.apu,[&](const std::string& s){f.fault(s);});CHECK(other.reset_cpu() && other.bootstrap_from_scratch());xlle::ApuGpRun r;CHECK(other.run(4096,r) && r.power==2);}
            CHECK(f.ram==f.before && !f.apu.gp_dma_nodes() && !f.apu.ep_dma_nodes());
            const unsigned final_reads=f.reads;
            if(ep_first){xlle::ApuEpProcessor last(f.apu,[&](const std::string& s){f.fault(s);});CHECK(last.phase()==xlle::ApuGpPhase::Failed);}
            else {xlle::ApuGpProcessor last(f.apu,[&](const std::string& s){f.fault(s);});CHECK(last.phase()==xlle::ApuGpPhase::Failed);}
            CHECK(f.reads==final_reads);
        }
    }
    CounterFixture f;f.boot({0x87});xlle::ApuEpProcessor ep(f.apu,[&](const std::string& s){f.fault(s);});
    CHECK(ep.reset_cpu() && ep.bootstrap_from_scratch());xlle::ApuGpRun r;CHECK(ep.run(32,r) && r.power==2 && ep.reset_cpu());
}
static void physical_observer(const char* path) {
    FILE* file=std::fopen(path,"rb");CHECK(file);std::vector<unsigned> image(4096);
    CHECK(std::fread(image.data(),4,4096,file)==4096 && std::fgetc(file)==EOF);std::fclose(file);
    CounterFixture f;f.boot(std::vector<unsigned>(image.begin(),image.begin()+2048));
    const auto gp=std::vector<unsigned>(f.apu.gp_word(0),f.apu.gp_word(0)+0x3800);
    xlle::ApuEpProcessor ep(f.apu,[&](const std::string& s){f.fault(s);});CHECK(ep.bootstrap_from_scratch());
    xlle::ApuGpRun r;CHECK(ep.run(20000,r) && r.power==2 && !r.yielded);
    const unsigned values[]={0,0,0,0,0,20,280,7,3,5,0x42334f,0,2};
    for(unsigned i=0;i<13;++i)CHECK(*f.apu.ep_word(0x100+i)==values[i]);
    unsigned witnesses[8]={};
    for(unsigned pc=0;pc+3<533;++pc)if(image[pc]==0x44f400 && image[pc+2]==0x447000 && image[pc+3]>=0x110 && image[pc+3]<0x118)witnesses[image[pc+3]-0x110]=image[pc+1];
    for(unsigned i=0;i<8;++i)CHECK(witnesses[i] && *f.apu.ep_word(0x110+i)==witnesses[i]);
    for(unsigned i=0;i<5;++i)CHECK(!*f.apu.ep_word(0x200+i));
    for(unsigned i=0;i<4;++i)CHECK(*f.apu.ep_word(0x400+i)==0x6b1000+i && *f.apu.ep_word(0x1840+i)==0x6b2000+i);
    CHECK(std::equal(image.begin(),image.begin()+2048,f.apu.ep_word(0x2800)));
    CHECK(std::equal(gp.begin(),gp.end(),f.apu.gp_word(0)) && f.ram==f.before && !f.apu.ep_dma_nodes());
    CHECK(f.owner_error.empty() && f.device_error.empty());
    std::puts("Frozen EP observer: all34 fields and2048 P match; exact seven values, no frame or threshold claim.");
}
static void rejected(const std::string& mode) {
    CounterFixture f;std::vector<unsigned> p;
    if(mode=="wrong-value")p={0x07f432,0xfffffe,0x87};
    else if(mode=="wrong-order")p={0x07f430,1,0x87};
    else if(mode=="repeat")p={0x07f432,0xffffff,0x07f432,0xffffff,0x87};
    else if(mode=="read-config")p={0x44f000,0xffffb2,0x87};
    else if(mode=="write-b3")p={0x07f433,1,0x87};
    else if(mode=="ca")p={0x08f48a,1,0x87};
    else if(mode=="d5-seed")p={0x08f495,0x4001,0x87};
    else p={0x08f485,0x7ff,0x87}; // Only exact FFF in the empty bootstrap context is admitted.
    f.boot(p);f.terminal_guard=true;
    f.expected_error=mode=="read-config"?"at ffffb2":mode=="ca"?"at ffffca":mode=="d5-seed"?"at ffffd5":mode=="c5"?"at ffffc5":"counter configuration profile";
    xlle::ApuEpProcessor ep(f.apu,[&](const std::string& s){f.fault(s);});CHECK(ep.bootstrap_from_scratch());xlle::ApuGpRun r;ep.run(64,r);CHECK(false);
}
int main(int argc,char** argv) {
    if(argc==3 && std::string(argv[1])=="--observer"){physical_observer(argv[2]);return 0;}
    if(argc==2){rejected(argv[1]);return 1;}
    CHECK(argc==1);ep_progress();independent_lifetimes();
    std::puts("EP profile: exact passive progress, independent per-DSP lifecycle and retained GP guards passed.");
}
