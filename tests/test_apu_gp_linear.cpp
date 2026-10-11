// Real GP linear scratch ownership and unchanged private original helpers.
// Expectations come from caller RAM/descriptor ownership, not donor output.
#include "machine/apu_gp_processor.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"GP linear FAIL %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
struct Fixture {
    std::vector<uint8_t> ram=std::vector<uint8_t>(0x50000,0xa5);
    std::string apu_error,owner_error;
    uint32_t fail_read=UINT32_MAX,fail_write=UINT32_MAX;
    unsigned scratch_reads=0,scratch_writes=0;
    bool expected_program_guard=false;
    std::vector<uint32_t> saved_p;
    xlle::McpxApu apu{[this](const std::string& s){apu_error=s;}};
    Fixture() {
        apu.set_mem_access([this](uint32_t a,void* out,size_t n){
            if(a==fail_read || uint64_t(a)+n>ram.size())return false;
            if(a>=0x10000)++scratch_reads;
            std::memcpy(out,ram.data()+a,n);return true;
        },[this](uint32_t a,const void* in,size_t n){
            if(a==fail_write || uint64_t(a)+n>ram.size())return false;
            if(a>=0x10000)++scratch_writes;
            std::memcpy(ram.data()+a,in,n);return true;
        });
        // Nonadjacent, reversed physical pages prevent a flat-RAM shortcut.
        reg(0x2040,0x1000);reg(0x20d4,15);
        for(unsigned i=0;i<16;++i)put(0x1000+i*8,0x40000-i*0x3000);
        reg(0x2054,0xffff);reg(0x2060,0xffff);reg(0x206c,0xffff);
        reg(0x2000,8);reg(0x1100,0);
    }
    void fault(const std::string& s) {
        owner_error=s;
        if(expected_program_guard) {
            CHECK(s.find("executable DMA/host writes during execution")!=std::string::npos);
            CHECK(!apu.gp_dma_nodes() && std::equal(saved_p.begin(),saved_p.end(),apu.gp_word(0x2800)));
            std::fprintf(stderr,"expected linear executable guard before P/node publication: %s\n",s.c_str());std::exit(2);
        }
    }
    void put(unsigned a,uint32_t v){std::memcpy(ram.data()+a,&v,4);}
    uint32_t get(unsigned a)const{uint32_t v;std::memcpy(&v,ram.data()+a,4);return v;}
    void reg(unsigned a,uint32_t v){apu.mmio_write(0xfe800000+a,4,v);}
    unsigned physical(unsigned a)const{return get(0x1000+(a>>12)*8)+(a&4095);}
    uint8_t& byte(unsigned a){return ram[physical(a)];}
    uint8_t byte(unsigned a)const{return ram[physical(a)];}
    void scratch(unsigned a,uint32_t v){for(unsigned b=0;b<4;++b)byte(a+b)=uint8_t(v>>(b*8));}
    uint32_t scratch(unsigned a)const{uint32_t v=0;for(unsigned b=0;b<4;++b)v|=uint32_t(byte(a+b))<<(b*8);return v;}
    uint32_t& word(unsigned a){auto* p=apu.gp_word(a);CHECK(p);return *p;}
    void node(unsigned at,unsigned next,unsigned control,unsigned count,unsigned target,unsigned offset) {
        const uint32_t d[]={next,control,count,target,offset};for(unsigned i=0;i<5;++i)word(at+i)=d[i];
    }
    void boot(const std::vector<uint32_t>& p){CHECK(p.size()<=2048);for(unsigned i=0;i<2048;++i)scratch(i*4,i<p.size()?p[i]:0x87);}
    uint32_t events(){uint32_t v;CHECK(apu.gp_read(0xffffc5,v));return v;}
    void go(unsigned at){CHECK(apu.gp_write(0xffffd4,at));CHECK(apu.gp_write(0xffffd6,1));}
};
static void mixed_transport() {
    for(unsigned life=0;life<3;++life) {
        Fixture f;
        const uint32_t samples[]={0x123456,0xabcdef,0xffffff,0x800000,0x000001,0x2468ac};
        for(unsigned i=0;i<6;++i)f.word(0x80+i)=samples[i];
        // Adjacent five-word descriptors: words5/6 of the first are the next
        // descriptor, so applying circular base/size would corrupt the copy.
        f.node(0x20,0x25,0x59e2,6,0x80,0x2ffd);
        f.node(0x25,0x4000,0x59e0,6,0x1880,0x2ffd);
        const auto before=f.ram;f.go(0x20);
        CHECK(f.apu.gp_dma_nodes()==2 && f.events()==0x80);
        CHECK(f.word(0x24)==0x2ffd && f.word(0x29)==0x2ffd);
        for(unsigned i=0;i<6;++i){CHECK(f.word(0x1880+i)==samples[i]);CHECK(f.scratch(0x2ffd+i*4)==samples[i]);}
        for(unsigned a=0;a<f.ram.size();++a) {
            const bool transferred=(a>=f.physical(0x2ffd) && a<f.physical(0x2ffd)+3) ||
                (a>=f.physical(0x3000) && a<f.physical(0x3000)+21);
            if(!transferred)CHECK(f.ram[a]==before[a]);
        }
        uint32_t v;for(unsigned i=0;i<8;++i)CHECK(f.apu.gp_read(0xffffd6,v) && v==16);
        CHECK(f.apu.gp_write(0xffffc5,0x80) && !f.events());
        // Five fields fit at the bank boundary; inaccessible words5/6 have no
        // role in a linear transfer. Host input into P publishes its real epoch.
        f.node(0xffb,0x4000,0x59e0,6,0x2880,0x2ffd);
        const auto revision=f.apu.gp_program_revision();f.go(0xffb);
        CHECK(f.apu.gp_program_revision()==revision+1 && f.apu.gp_dma_nodes()==3);
        for(unsigned i=0;i<6;++i)CHECK(f.word(0x2880+i)==samples[i]);
        // Independently specified PCM16 packing, with no wrap/base and ignored5/6.
        f.word(0x30+5)=0xffffff;f.word(0x30+6)=0;
        f.node(0x30,0x4000,0x45e2,6,0x1880,0x3ffd);f.go(0x30);
        for(unsigned i=0;i<6;++i){CHECK(f.byte(0x3ffd+i*2)==uint8_t(samples[i]>>8));CHECK(f.byte(0x3ffe+i*2)==uint8_t(samples[i]>>16));}
        CHECK(f.word(0x35)==0xffffff && !f.word(0x36) && f.apu_error.empty());
    }
}
static void publication_failures() {
    for(unsigned output=0;output<2;++output) {
        Fixture f;for(unsigned i=0;i<6;++i){f.word(0x80+i)=0x555555;f.scratch(0x2ffd+i*4,0x123456+i);}
        if(output)f.fail_write=f.physical(0x3000);else f.fail_read=f.physical(0x3000);
        const auto bank=std::vector<uint32_t>(f.apu.gp_word(0x80),f.apu.gp_word(0x80)+6);
        f.node(0x20,0x4000,output?0x59e2:0x59e0,6,0x80,0x2ffd);
        CHECK(f.apu.gp_write(0xffffd4,0x20));CHECK(!f.apu.gp_write(0xffffd6,1));
        CHECK(f.apu.faulted() && !f.apu.gp_dma_nodes() && f.word(0x24)==0x2ffd);
        CHECK(std::equal(bank.begin(),bank.end(),f.apu.gp_word(0x80)));
        // A real output prefix may persist; a staged input prefix does not
        // publish any sample bank, node or EOL. Preserve terminal failure.
        CHECK(f.byte(0x2ffd)==(output?0x55:0x56));CHECK(!f.apu.gp_write(0xffffd6,1));
    }
    for(unsigned mode=0;mode<4;++mode) {
        Fixture f;f.node(0x20,0x4000,0x59e2,6,0x80,0x2ffd);
        if(mode==0)f.word(0x21)|=16; // unqualified linear writeback
        if(mode==1)f.word(0x21)|=0x200; // unknown bit9
        if(mode==2)f.word(0x24)=0x10000; // beyond actual SGE owner
        if(mode==3){f.word(0xffc)=0x4000;f.word(0xffd)=0x59e2;f.word(0xffe)=1;f.word(0xfff)=0x80;}
        const auto ram=f.ram;CHECK(f.apu.gp_write(0xffffd4,mode==3?0xffc:0x20));CHECK(!f.apu.gp_write(0xffffd6,1));
        CHECK(f.ram==ram && !f.apu.gp_dma_nodes() && f.apu.faulted());
    }
    Fixture lease;lease.boot({0x08f484,1,0x08f485,2,0x08f484,1});
    xlle::ApuGpProcessor owner(lease.apu,[&](const std::string& s){lease.fault(s);});CHECK(owner.bootstrap_from_scratch());
    xlle::ApuGpRun run;CHECK(owner.run(100,run) && run.yielded);xlle::ApuVoiceQuantum q;CHECK(owner.start_frame(q) && q.published);
    lease.node(0x20,0x4000,0x59e0,6,0x1400,0x2ffd);
    const auto reads=lease.scratch_reads;const auto ram=lease.ram;const auto mix=std::vector<uint32_t>(lease.apu.gp_word(0x1400),lease.apu.gp_word(0x1400)+1024);
    CHECK(lease.apu.gp_write(0xffffd4,0x20) && !lease.apu.gp_write(0xffffd6,1));
    CHECK(lease.apu_error.find("MIX publication overlaps")!=std::string::npos && lease.scratch_reads==reads && lease.ram==ram);
    CHECK(std::equal(mix.begin(),mix.end(),lease.apu.gp_word(0x1400)) && !lease.apu.gp_dma_nodes());
}
static std::vector<uint32_t> original(const char* path) {
    FILE* file=std::fopen(path,"rb");CHECK(file);std::vector<uint32_t> p(371);
    CHECK(std::fread(p.data(),4,p.size(),file)==p.size() && std::fgetc(file)==EOF);std::fclose(file);
    for(auto& v:p)v&=0xffffff;p.resize(0x800,0x87);p[0]=0x0af080;p[1]=0x700;return p;
}
static void original_chain(const char* path,bool program_guard=false) {
    for(unsigned life=0;life<(program_guard?1u:3u);++life) {
        auto p=original(path);std::vector<uint32_t> caller;
        auto emit=[&](std::initializer_list<uint32_t> words){caller.insert(caller.end(),words);};
        auto builder=[&](unsigned node,unsigned target,unsigned offset,unsigned count,unsigned helper){
            emit({0x60f400,node,0x61f400,target,0x62f400,offset,0x63f400,count,0x0bf080,helper});
        };
        auto transfer=[&](unsigned node){emit({0x56f400,node,0x0bf080,0x126});};
        // Clear and both linear builders are unchanged original instructions.
        emit({0x0bf080,0x155});builder(6,0,0x800,6,0xdb);builder(0x18,0,0x800,6,0xeb);
        if(program_guard) {builder(0x20,0x2971,0x2ffd,6,0xdb);transfer(0x20);}
        else {
            emit({0x0bf080,0x5c}); // original control import + actual parameter import/ack
            builder(0x20,0x1880,0x2ffd,6,0xdb);transfer(0x20);
            builder(0x28,0x1880,0x3ffd,6,0xeb);
            // Original entry1B..1D selects format2 PCM24MSB after EB builds
            // format6 PCM24LSB. Execute those unchanged instructions here.
            emit({0x44f400,0x49e2,0x0200c4});transfer(0x28);
        }
        emit({0x08f484,1});std::copy(caller.begin(),caller.end(),p.begin()+0x700);
        Fixture f;f.boot(p);xlle::ApuGpProcessor gp(f.apu,[&](const std::string& s){f.fault(s);});CHECK(gp.bootstrap_from_scratch());
        // Boot is already copied to P. These real guest scratch controls now
        // occupy the original scratch800 parameter transport; no CPU hook.
        const uint32_t controls[]={0x10,0,0x2ffb,4,2,0x654321};
        for(unsigned i=0;i<6;++i)f.scratch(0x800+i*4,controls[i]);
        for(unsigned i=0;i<8;++i)f.scratch(0x2ffb+i*4,0x123450+life*16+i);
        f.expected_program_guard=program_guard;f.saved_p.assign(f.apu.gp_word(0x2800),f.apu.gp_word(0x2800)+4096);
        xlle::ApuGpRun r;CHECK(gp.run(20000,r) && r.yielded && !r.power && !r.sp && r.pc==0x700+caller.size());
        CHECK(!program_guard && f.apu.gp_dma_nodes()==5 && !f.events());
        for(unsigned i=0;i<4;++i)CHECK(f.word(0x90+i)==0x123450+life*16+i);
        CHECK(!f.word(4) && f.scratch(0x810)==0 && f.scratch(0x814)==controls[5]);
        for(unsigned i=0;i<6;++i){CHECK(f.word(0x1880+i)==(f.scratch(0x2ffd+i*4)&0xffffff));CHECK(f.scratch(0x3ffd+i*4)==(f.word(0x1880+i)<<8));}
        CHECK(f.word(0x20)==0x4020 && f.word(0x21)==0x59e0 && f.word(0x22)==6 && f.word(0x23)==0x1880 && f.word(0x24)==0x2ffd);
        CHECK(f.word(0x28)==0x4028 && f.word(0x29)==0x49e2 && f.word(0x2c)==0x3ffd);
        CHECK(f.apu_error.empty() && f.owner_error.empty());
    }
    std::puts("original GP linear DB/EB/0126/005C: real control/parameter/input/output copies and EOL ack passed across3lifetimes; isolated caller, timer/full boot unqualified");
}
int main(int argc,char** argv) {
    if(argc==3 && std::string(argv[1])=="--original-chain"){original_chain(argv[2]);return 0;}
    if(argc==3 && std::string(argv[1])=="--original-program-guard"){original_chain(argv[2],true);return 1;}
    CHECK(argc==1);mixed_transport();publication_failures();
    std::puts("GP linear: mixed real SGE/X/Y/P/PCM16, five-word descriptor ownership, failure prefixes and MIX/bit9/writeback guards passed");
}
