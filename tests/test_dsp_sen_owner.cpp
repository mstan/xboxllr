// Production EP owner reaches the first yield after the resident SEN prefix.
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
int main(int argc,char** argv) {
    CHECK(argc==2);FILE* file=std::fopen(argv[1],"rb");CHECK(file);std::vector<unsigned> p(2048);
    CHECK(std::fread(p.data(),4,p.size(),file)==p.size() && std::fgetc(file)==EOF);std::fclose(file);
    for(auto& v:p)v&=0xffffff;
    const unsigned workspace=p[0x122],workspace_words=p[0x123];
    CounterFixture f;f.boot(p);
    const auto gp=std::vector<unsigned>(f.apu.gp_word(0),f.apu.gp_word(0)+0x3800);
    xlle::ApuEpProcessor ep(f.apu,[&](const std::string& why) {
        std::fprintf(stderr,"unexpected original EP failure: %s\n",why.c_str());CHECK(false);
    });
    CHECK(ep.bootstrap_from_scratch());xlle::ApuGpRun result;
    CHECK(ep.run(20000,result) && result.yielded && result.pc==0x2f3 && result.sp==2 && !result.power);
    CHECK(f.ram==f.before);
    CHECK(!f.apu.gp_dma_nodes() && !f.apu.ep_dma_nodes());
    CHECK(std::equal(gp.begin(),gp.end(),f.apu.gp_word(0)));
    CHECK(*f.apu.ep_word(0x2801)==0 && *f.apu.ep_word(0x2802)==1 && *f.apu.ep_word(0x2803)==1 && *f.apu.ep_word(0x2804)==0 && *f.apu.ep_word(0x2805)==0);
    CHECK(*f.apu.ep_word(0x2800+0x19e)==workspace && *f.apu.ep_word(0x2800+0x19f)==workspace_words);
    for(unsigned i=0x122;i<0x140;++i)CHECK(!*f.apu.ep_word(0x2800+i));
    for(unsigned i=0;i<2048;++i){if(i==1 || i==2 || i==3 || (i>=0x122 && i<0x140) || i==0x19e || i==0x19f)continue;CHECK(*f.apu.ep_word(0x2800+i)==p[i]);}
    for(unsigned i=0;i<0xc00;++i)CHECK(*f.apu.ep_word(i)==0);
    for(unsigned i=0;i<0x100;++i)CHECK(*f.apu.ep_word(0x1800+i)==0);
    uint32_t events=~0u;CHECK(f.apu.ep_read(0xffffc5,events) && !events);
    std::puts("Original production EP owner: SEN resident prefix reached first C4 yield PC02F3/SP2; empty event clear, no wake/DMA/output, P workspace30words cleared, X/Y/GP/sourceRAM unchanged.");
}
