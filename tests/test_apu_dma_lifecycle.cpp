// Actual queued transport, original helper control flow and dynamic P consumers.
// Descriptor bit9 and original bootstrap/timer/cadence remain separate guards.
#include "machine/apu_ep_processor.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"DSP DMA lifecycle FAIL %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
using xlle::ApuDspKind;
struct Fixture {
    std::vector<uint8_t> ram=std::vector<uint8_t>(0x140000,0xa5);
    std::string error,owner_error;std::function<void()> during_read;
    unsigned reads=0;uint32_t failed_read=UINT32_MAX;
    xlle::McpxApu apu{[this](const std::string& s){error=s;}};
    Fixture() {
        apu.set_mem_access([this](uint32_t a,void* out,size_t n){
            ++reads;if(a==failed_read || uint64_t(a)+n>ram.size())return false;
            if(during_read && a>=0x60000)during_read();
            std::memcpy(out,ram.data()+a,n);return true;
        },[this](uint32_t a,const void* in,size_t n){
            if(uint64_t(a)+n>ram.size())return false;std::memcpy(ram.data()+a,in,n);return true;
        });
        reg(0x2040,0xc000);reg(0x20d4,7);reg(0x2048,0xd000);reg(0x20dc,7);
        reg(0x2044,0xe000);reg(0x20d8,3);reg(0x204c,0xe000);reg(0x20e0,3);
        for(unsigned n=0;n<8;++n){put(0xc000+n*8,0x60000+n*0x5000);put(0xd000+n*8,0x90000+n*0x4000);}
        for(unsigned n=0;n<4;++n)put(0xe000+n*8,0xb0000+n*0x6000);
        reg(0x3024,0);reg(0x3028,0x2000);reg(0x302c,0x1ffd);
        reg(0x4024,0);reg(0x4028,0x2000);reg(0x402c,0x1ffd);
    }
    void put(unsigned a,uint32_t v){std::memcpy(ram.data()+a,&v,4);}
    uint32_t get(unsigned a)const{uint32_t v;std::memcpy(&v,ram.data()+a,4);return v;}
    void reg(unsigned a,uint32_t v){apu.mmio_write(0xfe800000+a,4,v);}
    unsigned physical(unsigned table,unsigned a)const{return get(table+(a>>12)*8)+(a&4095);}
    void scratch(bool ep,unsigned a,uint32_t v){for(unsigned b=0;b<4;++b)ram[physical(ep?0xd000:0xc000,a+b)]=uint8_t(v>>(b*8));}
    uint32_t* word(bool ep,unsigned a){return ep?apu.ep_word(a):apu.gp_word(a);}
    bool write(bool ep,unsigned a,uint32_t v){return ep?apu.ep_write(a,v):apu.gp_write(a,v);}
    uint32_t read(bool ep,unsigned a){uint32_t v;CHECK(ep?apu.ep_read(a,v):apu.gp_read(a,v));return v;}
    ApuDspKind kind(bool ep)const{return ep?ApuDspKind::Ep:ApuDspKind::Gp;}
    uint64_t nodes(bool ep)const{return ep?apu.ep_dma_nodes():apu.gp_dma_nodes();}
    void node(bool ep,unsigned at,unsigned next,unsigned control,unsigned count,unsigned target,unsigned offset=0) {
        const uint32_t fields[]={next,control,count,target,offset};
        for(unsigned n=0;n<((control>>5&15)>=14?5u:4u);++n)*word(ep,at+n)=fields[n];
    }
    void boot(bool ep,const std::vector<uint32_t>& p){CHECK(p.size()<=2048);for(unsigned n=0;n<2048;++n)scratch(ep,n*4,n<p.size()?p[n]:0x87);}
    void fault(const std::string& s){owner_error=s;}
};
static void control(std::vector<uint32_t>& p,unsigned a,unsigned v){p.push_back(0x08f480+a-0xffffc0);p.push_back(v);}
static void call(std::vector<uint32_t>& p,unsigned pc){p.push_back(0x0bf080);p.push_back(pc);}
static std::vector<uint32_t> block(unsigned left,unsigned right) {
    return {0x65f400,0x5fc,0x44f400,left,0x021584,0x65f400,0x5fd,0x44f400,right,0x021584,0x0c};
}
static std::vector<uint32_t> helper_source(const char* path) {
    if(path) {
        FILE* file=std::fopen(path,"rb");CHECK(file);std::vector<uint32_t> p(2048);
        CHECK(std::fread(p.data(),4,p.size(),file)==p.size() && std::fgetc(file)==EOF);std::fclose(file);
        for(auto& v:p)v&=0xffffff;return p;
    }
    // Independently authored bounded control stimuli. Original firmware bytes
    // are supplied only by the private reference run, never embedded here.
    std::vector<uint32_t> p(2048,0x87);
    const uint32_t idle[]={0x0a96a4,0x2d0,0x0c};
    const uint32_t start[]={0x08f496,1,0,0x0c};
    const uint32_t end[]={0x0a8587,0x2da,0x0c};
    std::copy(std::begin(idle),std::end(idle),p.begin()+0x2d0);
    std::copy(std::begin(start),std::end(start),p.begin()+0x2d5);
    std::copy(std::begin(end),std::end(end),p.begin()+0x2da);return p;
}
static void dynamic_consumers(const char* original) {
    unsigned completed=0;
    for(bool ep:{false,true})for(unsigned life=0;life<3;++life) {
        Fixture f;auto p=helper_source(original);
        const auto initial=block(0x123456,0x654321);std::copy(initial.begin(),initial.end(),p.begin()+0x780);
        p[0]=0x0bf080;p[1]=0x780;p[2]=0x0af080;p[3]=0x700;
        std::vector<uint32_t> caller;control(caller,0xffffd4,0x20);call(caller,0x2d5);
        caller.insert(caller.end(),16,0); // Work progresses without register polls.
        call(caller,0x2da);control(caller,0xffffc5,0x80);call(caller,0x2d0);call(caller,0x780);
        control(caller,0xffffd4,0x30);call(caller,0x2d5);call(caller,0x2da);control(caller,0xffffc5,0x80);call(caller,0x2d0);
        const unsigned boundary=0x700+unsigned(caller.size());caller.insert(caller.end(),{0x0af080,0x700});
        std::copy(caller.begin(),caller.end(),p.begin()+0x700);f.boot(ep,p);
        f.node(ep,0x20,0x4000,0x59e0,unsigned(initial.size()),0x2f80,0x2ffd);
        f.node(ep,0x30,0x4000,0x4402,2,0x600);
        xlle::ApuDmaDispatchPolicy policy{1u<<life};
        std::unique_ptr<xlle::ApuGpProcessor> gp;std::unique_ptr<xlle::ApuEpProcessor> processor;
        if(ep)processor=std::make_unique<xlle::ApuEpProcessor>(f.apu,[&](const std::string& s){f.fault(s);},policy);
        else gp=std::make_unique<xlle::ApuGpProcessor>(f.apu,[&](const std::string& s){f.fault(s);},policy);
        auto boot=[&]{return ep?processor->bootstrap_from_scratch():gp->bootstrap_from_scratch();};
        auto run=[&](uint32_t cycles,xlle::ApuGpRun& r){return ep?processor->run(cycles,r):gp->run(cycles,r);};CHECK(boot());
        xlle::ApuGpRun r;
        for(unsigned iteration=0;iteration<3;++iteration) {
            const unsigned left=0x234500+life*0x100+iteration*0x1000,right=0xdcba00-life*0x100-iteration*0x1000;
            const auto code=block(left,right);for(unsigned n=0;n<code.size();++n)f.scratch(ep,0x2ffd+n*4,code[n]);
            const auto before=f.ram;const auto cursor=f.apu.mmio_read(ep?0xfe80402c:0xfe80302c,4);
            const auto revision=ep?f.apu.ep_program_revision():f.apu.gp_program_revision();
            unsigned attempts=0;bool departed=iteration==0;
            do{const bool ok=run(1,r);if(!ok || r.power || r.yielded)std::fprintf(stderr,"ep=%u life=%u iteration=%u pc=%x power=%u sp=%u cycles=%u; %s; %s\n",ep,life,iteration,r.pc,r.power,r.sp,r.cycles,f.error.c_str(),f.owner_error.c_str());CHECK(ok && !r.power && !r.yielded);if(r.pc!=boundary)departed=true;CHECK(++attempts<4096);}while(!departed || r.pc!=boundary || r.sp);
            CHECK(*f.word(ep,0x600)==left && *f.word(ep,0x601)==right);
            CHECK(std::equal(code.begin(),code.end(),f.word(ep,0x2f80)));
            CHECK((ep?f.apu.ep_program_revision():f.apu.gp_program_revision())==revision+1);
            CHECK(!f.read(ep,0xffffd6) && !f.read(ep,0xffffc5) && f.nodes(ep)==2*(iteration+1));
            std::vector<uint8_t> expected=before;
            for(unsigned n=0;n<4;++n)expected[f.physical(0xe000,(cursor+n)%0x2000)]=uint8_t((n<2?left:right)>>(8+(n%2)*8));
            CHECK(f.ram==expected && f.apu.mmio_read(ep?0xfe80402c:0xfe80302c,4)==(cursor+4)%0x2000);
            const auto cycles=r.cycles;const auto ram=f.ram;
            for(unsigned n=0;n<4096;++n)CHECK(!f.read(ep,0xffffd6));
            CHECK(r.cycles==cycles && f.ram==ram && !f.apu.dsp_dma_pending(f.kind(ep)));
            CHECK(f.error.empty() && f.owner_error.empty());++completed;
        }
    }
    std::printf("DSP DMA lifecycle: %u actual GP/EP dynamic-code/PCM16 consumers across policies1/2/4; warm-cache reload, exact RAM, passive reads passed\n",completed);
    if(original)std::puts("unchanged2D5/2DA/2D0 return without STOP; authored caller/descriptors remain separate from original boot");
}
static void queued_lifetimes() {
    for(bool ep:{false,true}) {
        Fixture f;for(unsigned n=0;n<2;++n){f.scratch(ep,0x2ffd+n*4,0x123456+n);*f.word(ep,0x200+n)=0x555555;}
        f.node(ep,0x20,0x4000,0x59e0,2,0x200,0x2ffd);const auto bank=std::vector<uint32_t>(f.word(ep,0x200),f.word(ep,0x200)+2);const auto ram=f.ram;
        CHECK(f.write(ep,0xffffd4,0x20) && f.write(ep,0xffffd6,1));const auto reads=f.reads;
        for(unsigned n=0;n<4096;++n)CHECK(f.read(ep,0xffffd6)==16 && !f.read(ep,0xffffc5));
        CHECK(f.reads==reads && !f.nodes(ep) && f.ram==ram && std::equal(bank.begin(),bank.end(),f.word(ep,0x200)));
        CHECK(f.apu.service_dsp_dma(f.kind(ep))); // Real descriptor admission.
        CHECK(f.read(ep,0xffffd6)==16 && !f.nodes(ep) && f.reads==reads);
        CHECK(f.write(ep,0xffffd6,3) && f.apu.service_dsp_dma(f.kind(ep),4096));
        CHECK(f.read(ep,0xffffd6)==24 && !f.nodes(ep));
        CHECK(f.write(ep,0xffffd6,2) && !f.apu.dsp_dma_pending(f.kind(ep)) && f.read(ep,0xffffd6)==40);
        CHECK(!f.read(ep,0xffffc5) && f.ram==ram && std::equal(bank.begin(),bank.end(),f.word(ep,0x200)));
        CHECK(f.write(ep,0xffffd6,4) && f.write(ep,0xffffd6,1) && f.apu.service_dsp_dma(f.kind(ep),2));
        CHECK(!f.read(ep,0xffffd6) && f.read(ep,0xffffc5)==0x80 && f.nodes(ep)==1);
        CHECK(*f.word(ep,0x200)==0x123456 && *f.word(ep,0x201)==0x123457 && f.ram==ram);
        CHECK(f.write(ep,0xffffc5,0x80) && !f.read(ep,0xffffc5));
        // A later linked-node failure retains the actual completed first node,
        // never manufactures whole-list completion or a second destination.
        f.node(ep,0x20,0x30,0x59e0,2,0x200,0x2ffd);f.node(ep,0x30,0x4000,0x5be0,2,0x210,0x2ffd);
        *f.word(ep,0x210)=0x456789;CHECK(f.write(ep,0xffffd4,0x20) && f.write(ep,0xffffd6,1));
        CHECK(!f.apu.service_dsp_dma(f.kind(ep),4096) && f.nodes(ep)==2 && *f.word(ep,0x210)==0x456789);
        CHECK(f.apu.faulted() && !f.apu.service_dsp_dma(f.kind(ep)) && !f.write(ep,0xffffd6,2));
    }
    // A later physical P-input failure stops the real instruction/device loop
    // before the following marker and STOP, preserving the entire P bank.
    for(bool ep:{false,true}) {
        Fixture failed;std::vector<uint32_t> p={0x08f494,0x20,0x08f496,1,0,
            0x65f400,0x20c,0x44f400,0x123456,0x021584,0x87};failed.boot(ep,p);
        failed.node(ep,0x20,0x4000,0x59e0,2,0x3000,0x2ffd);
        failed.scratch(ep,0x2ffd,0x654321);failed.scratch(ep,0x3001,0x2468ac);
        std::unique_ptr<xlle::ApuGpProcessor> gp;std::unique_ptr<xlle::ApuEpProcessor> owner;
        if(ep)owner=std::make_unique<xlle::ApuEpProcessor>(failed.apu,[&](const std::string& s){failed.fault(s);});
        else gp=std::make_unique<xlle::ApuGpProcessor>(failed.apu,[&](const std::string& s){failed.fault(s);});
        CHECK(ep?owner->bootstrap_from_scratch():gp->bootstrap_from_scratch());
        const auto saved=std::vector<uint32_t>(failed.word(ep,0x2800),failed.word(ep,0x2800)+4096);
        const auto revision=ep?failed.apu.ep_program_revision():failed.apu.gp_program_revision();
        const auto ram=failed.ram;failed.failed_read=failed.physical(ep?0xd000:0xc000,0x3000);
        xlle::ApuGpRun stopped;CHECK(!(ep?owner->run(1000,stopped):gp->run(1000,stopped)) && !stopped.returned);
        CHECK(failed.apu.faulted() && !failed.nodes(ep) && !*failed.word(ep,0x210));
        CHECK(std::equal(saved.begin(),saved.end(),failed.word(ep,0x2800)) && failed.ram==ram);
        CHECK((ep?failed.apu.ep_program_revision():failed.apu.gp_program_revision())==revision);
        CHECK(!(ep?owner->reset_cpu():gp->reset_cpu()));
    }
    // A reentrant owner cannot execute while staged P input owns device work.
    Fixture f;std::vector<uint32_t> p={0x08f494,0x20,0x08f496,1,0x0af080,4};f.boot(true,p);
    f.node(true,0x20,0x4000,0x59e0,1,0x3000,0x2ffd);f.scratch(true,0x2ffd,0x87);
    xlle::ApuEpProcessor ep(f.apu,[&](const std::string& s){f.fault(s);});CHECK(ep.bootstrap_from_scratch());
    const auto saved=std::vector<uint32_t>(f.word(true,0x2800),f.word(true,0x2800)+4096);
    f.during_read=[&]{xlle::ApuEpRun nested;CHECK(!ep.run(1,nested) && !nested.returned);};
    xlle::ApuEpRun r;CHECK(!ep.run(100,r) && !r.returned && !f.nodes(true));
    CHECK(std::equal(saved.begin(),saved.end(),f.word(true,0x2800)) && f.apu.faulted());
    // Reset and abandonment do not turn an unfinished owned job into success.
    for(bool abandon:{false,true}) {
        Fixture held;held.boot(true,{0x87});auto owner=std::make_unique<xlle::ApuEpProcessor>(held.apu,[&](const std::string& s){held.fault(s);});
        CHECK(owner->bootstrap_from_scratch());held.node(true,0x20,0x4000,0x59e0,1,0x200,0x2ffd);
        CHECK(held.write(true,0xffffd4,0x20) && held.write(true,0xffffd6,1));
        if(abandon){owner.reset();CHECK(held.apu.faulted());}
        else CHECK(!owner->reset_cpu() && owner->phase()==xlle::ApuEpPhase::Failed && !owner->run(1,r));
        CHECK(!held.nodes(true) && !*held.word(true,0x200));
    }
}
int main(int argc,char** argv) {
    CHECK(argc==1 || (argc==3 && std::string(argv[1])=="--original"));
    if(argc==1)queued_lifetimes();dynamic_consumers(argc==3?argv[2]:nullptr);return 0;
}
