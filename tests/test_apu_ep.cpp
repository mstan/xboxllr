// Coherent actual VP/GP producer -> physical scratch -> EP CPU -> output RAM.
// Authored supported programs do not replace original timer/bit9/cadence paths.
#include "machine/apu_ep_processor.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"APU EP FAIL %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
struct Fixture {
    std::vector<uint8_t> ram=std::vector<uint8_t>(0x140000,0xa5);
    std::string apu_error,owner_error;
    uint32_t fail_read=UINT32_MAX,fail_write=UINT32_MAX;
    bool exit_guard=false;std::string expected_guard;
    bool unsafe_host_publication=false;
    std::vector<uint32_t> saved_p,saved_gp;
    std::vector<uint8_t> saved_ram;
    uint32_t saved_cursor=0;
    uint64_t saved_nodes=0;
    xlle::McpxApu apu{[this](const std::string& s){apu_error=s;}};
    Fixture() {
        apu.set_mem_access([this](uint32_t a,void* out,size_t n){
            if(a==fail_read || uint64_t(a)+n>ram.size())return false;
            if(unsafe_host_publication && a>=0x60000)apu.mmio_write(0xfe85a020,4,0x654321);
            std::memcpy(out,ram.data()+a,n);return true;
        },[this](uint32_t a,const void* in,size_t n){
            if(a==fail_write || uint64_t(a)+n>ram.size())return false;
            std::memcpy(ram.data()+a,in,n);return true;
        });
        reg(0x2040,0xc000);reg(0x20d4,7);reg(0x2048,0xd000);reg(0x20dc,7);reg(0x204c,0xe000);reg(0x20e0,3);
        for(unsigned i=0;i<8;++i){put(0xc000+i*8,0x60000+i*0x5000);put(0xd000+i*8,0x90000+i*0x4000);}
        for(unsigned i=0;i<4;++i)put(0xe000+i*8,0xb0000+i*0x6000);
        // GP and EP share only these physical scratch pages, through distinct
        // SGE tables; their internal X/Y/P banks remain different owners.
        put(0xd010,get(0xc010));put(0xd018,get(0xc018));
        reg(0x4024,0);reg(0x4028,0x2000);reg(0x402c,0x1ffd);
        reg(0x202c,0x1000);reg(0x2030,0xa000);
        for(unsigned i=0;i<64;++i)put(0xa000+i*8,0x10000+i*4096);
        reg(0x2054,0xffff);reg(0x2060,0xffff);reg(0x206c,0xffff);reg(0x2000,8);reg(0x1100,0);
    }
    void fault(const std::string& s) {
        owner_error=s;
        if(exit_guard) {
            const auto combined=s+"; APU: "+apu_error;
            CHECK(combined.find(expected_guard)!=std::string::npos);
            CHECK(apu.ep_dma_nodes()==saved_nodes && apu.RegFileMmio::mmio_read(0xfe80402c,4)==saved_cursor);
            if(saved_nodes){uint32_t events=0;CHECK(apu.ep_read(0xffffc5,events) && events==0x80);}
            CHECK(std::equal(saved_gp.begin(),saved_gp.end(),apu.gp_word(0x1400)));
            CHECK(ram==saved_ram);
            if(!saved_p.empty())CHECK(std::equal(saved_p.begin(),saved_p.end(),apu.ep_word(0x2800)));
            std::fprintf(stderr,"expected EP guard before sample/node/output publication: %s\n",combined.c_str());std::exit(2);
        }
    }
    void put(unsigned a,uint32_t v){std::memcpy(ram.data()+a,&v,4);}
    uint32_t get(unsigned a)const{uint32_t v;std::memcpy(&v,ram.data()+a,4);return v;}
    void reg(unsigned a,uint32_t v){apu.mmio_write(0xfe800000+a,4,v);}
    void method(unsigned a,uint32_t v){reg(0x20000+a,v);}
    unsigned physical(unsigned table,unsigned a)const{return get(table+(a>>12)*8)+(a&4095);}
    void scratch(unsigned table,unsigned a,uint32_t v){for(unsigned b=0;b<4;++b)ram[physical(table,a+b)]=uint8_t(v>>(b*8));}
    uint32_t scratch(unsigned table,unsigned a)const{uint32_t v=0;for(unsigned b=0;b<4;++b)v|=uint32_t(ram[physical(table,a+b)])<<(b*8);return v;}
    void boot(bool ep,const std::vector<uint32_t>& p){CHECK(p.size()<=2048);for(unsigned i=0;i<2048;++i)scratch(ep?0xd000:0xc000,i*4,i<p.size()?p[i]:0x87);}
    uint32_t events(bool ep){uint32_t v;CHECK(ep?apu.ep_read(0xffffc5,v):apu.gp_read(0xffffc5,v));return v;}
    void descriptor(bool ep,unsigned at,unsigned next,unsigned control,unsigned count,unsigned target,unsigned offset=0) {
        const uint32_t node[]={next,control,count,target,offset};
        for(unsigned i=0;i<((control>>5&15)>=14?5u:4u);++i)*(ep?apu.ep_word(at+i):apu.gp_word(at+i))=node[i];
    }
    void voice(unsigned id,bool stereo,unsigned bin,int16_t left,int16_t right=0) {
        const unsigned object=0x1000+id*128,base=(id-64)*1024;
        std::fill(ram.begin()+object,ram.begin()+object+128,0);
        for(unsigned n=0;n<256;++n){std::memcpy(ram.data()+0x10000+base+n*(stereo?4:2),&left,2);if(stereo)std::memcpy(ram.data()+0x10000+base+n*4+2,&right,2);}
        method(0x2f8,id);method(0x304,0x5200e000|(stereo?(1u<<27)|(1u<<16):0)|6|(7<<5));
        method(0x300,bin|(3<<5)|(2<<10)|(4<<16)|(5<<21)|(6<<26));
        method(0x360,stereo?0x000f000f:0xffff000f);method(0x364,0xffffffff);method(0x368,0xffffffff);
        method(0x3a0,base);method(0x3dc,255);method(0x30c,0xff000000);method(0x120,0x1ffff);method(0x124,id);
        CHECK(apu_error.empty());
    }
};
static void move(std::vector<uint32_t>& p,unsigned from,unsigned to) {
    const uint32_t words[]={0x65f400,from-4,0x021594,0x65f400,to-4,0x021584};p.insert(p.end(),std::begin(words),std::end(words));
}
static void control(std::vector<uint32_t>& p,unsigned address,unsigned value) {
    CHECK(address>=0xffffc0 && address<=0xffffff);p.push_back(0x08f480+(address-0xffffc0));p.push_back(value);
}
static std::vector<uint32_t> gp_program() {
    std::vector<uint32_t> p={0x08f485,0xfff,0x08f484,1};const unsigned wait=unsigned(p.size());
    p.insert(p.end(),{0x0a8581,wait,0x08f485,2});
    for(unsigned i=0;i<32;++i){move(p,0x1400+i,0x200+i);move(p,0x1460+i,0x220+i);}
    control(p,0xffffd6,2);control(p,0xffffd4,0x20);control(p,0xffffd6,1);control(p,0xffffc4,1);
    p.insert(p.end(),{0x0af080,wait});return p;
}
static std::vector<uint32_t> ep_program() {
    std::vector<uint32_t> p;control(p,0xffffd6,2);control(p,0xffffd4,0x2e00);control(p,0xffffd6,1);
    const unsigned wait=unsigned(p.size());p.insert(p.end(),{0x0a8587,wait});control(p,0xffffc5,0x80);control(p,0xffffd6,2);
    // EP consumes the real planar packet through its X bank, then actual DSP
    // instructions place stereo pairs in its separate output buffer.
    for(unsigned i=0;i<32;++i){move(p,0x200+i,0x600+i*2);move(p,0x220+i,0x601+i*2);}
    control(p,0xffffd4,0x20);control(p,0xffffd6,1);const unsigned output_wait=unsigned(p.size());p.insert(p.end(),{0x0a8587,output_wait});
    control(p,0xffffc5,0x80);control(p,0xffffd6,2);p.push_back(0x87);
    // The descriptor is stored in P, as in the original EP helpers. It does
    // not overlap translated instructions and has known bit9 clear.
    p.resize(0x605,0x87);const uint32_t node[]={0x4000,0x59e0,64,0x200,0x2ffd};std::copy(std::begin(node),std::end(node),p.begin()+0x600);return p;
}
static void executable_publication_and_yield() {
    Fixture f;std::vector<uint32_t> p(17,0);
    p[0]=0x0c0008;const uint32_t block[]={0x65f400,0xfc,0x44f400,0x123456,0x021584,0x0c0010};
    std::copy(std::begin(block),std::end(block),p.begin()+8);p[16]=0x0c0008;
    f.boot(true,p);xlle::ApuEpProcessor ep(f.apu,[&](const std::string& s){f.fault(s);});CHECK(ep.bootstrap_from_scratch());
    const auto gp_revision=f.apu.gp_program_revision();xlle::ApuEpRun e;
    auto reach=[&](unsigned pc){for(unsigned n=0;n<32;++n){CHECK(ep.run(1,e) && !e.power && !e.sp);if(e.pc==pc)return;}CHECK(false);};
    reach(8);reach(16);CHECK(*f.apu.ep_word(0x100)==0x123456);reach(8);
    auto revision=f.apu.ep_program_revision();f.reg(0x5a000+11*4,0x654321);
    CHECK(f.apu.ep_program_revision()==revision+1);reach(16);CHECK(*f.apu.ep_word(0x100)==0x654321);reach(8);
    f.scratch(0xd000,0x2ffd,0x2468ac);f.descriptor(true,0x20,0x4000,0x59e0,1,0x280b,0x2ffd);
    revision=f.apu.ep_program_revision();CHECK(f.apu.ep_write(0xffffd4,0x20) && f.apu.ep_write(0xffffd6,1) && f.apu.service_dsp_dma(xlle::ApuDspKind::Ep,4096));
    CHECK(f.apu.ep_program_revision()==revision+1 && f.apu.ep_dma_nodes()==1);reach(16);CHECK(*f.apu.ep_word(0x100)==0x2468ac);
    CHECK(f.apu.gp_program_revision()==gp_revision && !*f.apu.gp_word(0x100));

    // An actual EP vendor yield does not release an outstanding GP MIX lease.
    Fixture leased;leased.voice(71,false,0,4096);leased.boot(false,gp_program());
    leased.descriptor(false,0x20,0x4000,0x59e2,64,0x200,0x2ffd);
    xlle::ApuGpProcessor gp(leased.apu,[&](const std::string& s){leased.fault(s);});CHECK(gp.bootstrap_from_scratch());
    xlle::ApuGpRun g;CHECK(gp.run(1000,g) && g.yielded);xlle::ApuVoiceQuantum q;CHECK(gp.start_frame(q) && q.published);
    leased.boot(true,{0x08f485,0xfff,0x08f484,1,0x87});xlle::ApuEpProcessor yielding(leased.apu,[&](const std::string& s){leased.fault(s);});
    CHECK(yielding.bootstrap_from_scratch() && yielding.run(1000,e) && e.yielded && e.unused_cycles>900);
    CHECK(leased.apu.gp_frame_outstanding() && leased.events(false)==2 && !leased.events(true));
    CHECK(gp.run(5000,g) && g.yielded && !leased.apu.gp_frame_outstanding());
    CHECK(f.apu_error.empty() && f.owner_error.empty() && leased.apu_error.empty() && leased.owner_error.empty());
    // An external write cannot borrow the CPU's bootstrap admission, even
    // with an empty EP event word and an existing yielded owner.
    CHECK(!leased.apu.ep_write(0xffffc5,0xfff) && leased.apu.faulted());
}
static void coherent_lifetimes() {
    for(unsigned life=0;life<3;++life) {
        Fixture f;f.voice(71,false,0,4096);f.voice(96,true,0,8192,-16384);
        f.descriptor(false,0x20,0x4000,0x59e2,64,0x200,0x2ffd);f.boot(false,gp_program());
        xlle::ApuGpProcessor gp(f.apu,[&](const std::string& s){f.fault(s);});CHECK(gp.bootstrap_from_scratch());
        xlle::ApuGpRun g;CHECK(gp.run(1000,g) && g.yielded);
        auto ep=std::make_unique<xlle::ApuEpProcessor>(f.apu,[&](const std::string& s){f.fault(s);});
        *f.apu.ep_word(0x18ff)=0x654321;*f.apu.ep_word(0x3000)=0x456789;
        for(unsigned frame=0;frame<10;++frame) {
            if(frame==5) {
                // Rebind source and output physical pages, retaining real CUR
                // and device banks. Replace the EP CPU without replacing APU.
                f.put(0xc010,0xd0000);f.put(0xc018,0xcf000);f.put(0xd010,0xd0000);f.put(0xd018,0xcf000);
                f.put(0xe000,0xe0000);f.put(0xe008,0xda000);
                ep.reset();ep=std::make_unique<xlle::ApuEpProcessor>(f.apu,[&](const std::string& s){f.fault(s);});
            }
            xlle::ApuVoiceQuantum q;CHECK(gp.start_frame(q) && q.published && q.sequence==frame+1);
            // Reset/bootstrap EP while GP owns its active MIX lease is legal:
            // the hardware instances have separate executable/cache owners.
            f.boot(true,ep_program());CHECK(ep->reset_cpu() && ep->bootstrap_from_scratch());
            CHECK(*f.apu.ep_word(0x3000)==0x456789 && *f.apu.ep_word(0x18ff)==0x654321);
            CHECK(gp.run(5000,g) && g.yielded && !f.apu.gp_frame_outstanding());
            std::vector<uint32_t> expected;for(unsigned i=0;i<32;++i){expected.push_back(*f.apu.gp_word(0x1400+i));expected.push_back(*f.apu.gp_word(0x1460+i));}
            for(unsigned i=0;i<32;++i){CHECK(f.scratch(0xd000,0x2ffd+i*4)==expected[i*2]);CHECK(f.scratch(0xd000,0x307d+i*4)==expected[i*2+1]);}
            f.descriptor(true,0x20,0x4000,0x4402,64,0x600);
            const auto ram=f.ram;const auto cursor=f.apu.mmio_read(0xfe80402c,4);
            const auto gp_bank=std::vector<uint32_t>(f.apu.gp_word(0x1400),f.apu.gp_word(0x1400)+1024);
            xlle::ApuEpRun e;CHECK(ep->run(5000,e) && e.returned && e.power==2 && !e.yielded && !e.sp);
            CHECK(f.events(true)==0 && f.events(false)==0x80 && f.apu.ep_dma_nodes()==2*(frame+1) && f.apu.gp_dma_nodes()==frame+1);
            std::vector<uint8_t> expected_ram=ram;
            for(unsigned i=0;i<64;++i) {
                CHECK(*f.apu.ep_word(0x600+i)==expected[i]);
                for(unsigned b=0;b<2;++b)expected_ram[f.physical(0xe000,(cursor+i*2+b)%0x2000)]=uint8_t(expected[i]>>(8+b*8));
                if(frame>=7){const double sample=(int32_t(expected[i])-((expected[i]&0x800000)?0x1000000:0))/8388608.0;CHECK(std::fabs(sample-(i&1?-0.5:0.375))<0.000002);}
            }
            CHECK(f.ram==expected_ram && f.apu.mmio_read(0xfe80402c,4)==(cursor+128)%0x2000);
            CHECK(std::equal(gp_bank.begin(),gp_bank.end(),f.apu.gp_word(0x1400)) && f.apu_error.empty() && f.owner_error.empty());
        }
        CHECK(f.apu.gp_write(0xffffc5,0x80) && !f.events(false));
    }
}
static void contexts_and_failures() {
    Fixture f;CHECK(f.apu.ep_word(0xbff) && f.apu.ep_word(0x18ff) && f.apu.ep_word(0x37ff));
    for(unsigned hole:{0xc00u,0x1400u,0x17ffu,0x1900u,0x2000u,0x3800u})CHECK(!f.apu.ep_word(hole));
    CHECK(f.apu.ep_word(0)!=f.apu.gp_word(0) && f.apu.ep_word(0x1800)!=f.apu.gp_word(0x1800));
    f.reg(0x50000,0xff123456);CHECK(*f.apu.ep_word(0)==0x123456 && !*f.apu.gp_word(0));
    CHECK(f.apu.mmio_read(0xfe850000,4)==0x123456);
    f.boot(true,{0x87});xlle::ApuEpProcessor ep(f.apu,[&](const std::string& s){f.fault(s);});
    {xlle::ApuEpProcessor duplicate(f.apu,[&](const std::string& s){f.owner_error=s;});CHECK(duplicate.phase()==xlle::ApuEpPhase::Failed);}
    CHECK(ep.bootstrap_from_scratch());xlle::ApuEpRun r;CHECK(ep.run(100,r) && r.power==2);
    // Missing second boot page leaves all existing EP P words intact. No GP
    // bootstrap, mapped MIX access or core is substituted for the EP profile.
    Fixture bad;bad.boot(true,{0x87});std::fill(bad.apu.ep_word(0x2800),bad.apu.ep_word(0x2800)+4096,0x555555);
    const auto p=std::vector<uint32_t>(bad.apu.ep_word(0x2800),bad.apu.ep_word(0x2800)+4096);bad.reg(0x20dc,0);
    xlle::ApuEpProcessor failed(bad.apu,[&](const std::string& s){bad.fault(s);});CHECK(!failed.bootstrap_from_scratch() && !failed.bootstraps());
    CHECK(std::equal(p.begin(),p.end(),bad.apu.ep_word(0x2800)) && !failed.run(100,r) && !r.returned && !failed.reset_cpu());
    // A failed output page reports real preceding bytes, no node/CUR update.
    // Failed input staging leaves its destination and node unowned.
    for(unsigned output=0;output<2;++output) {
        Fixture fault;for(unsigned i=0;i<4;++i){*fault.apu.ep_word(0x200+i)=0x123456+i;fault.scratch(0xd000,0x2ffd+i*4,0x654321+i);}
        fault.descriptor(true,0x20,0x4000,output?0x4402:0x59e0,4,0x200,0x2ffd);
        if(output)fault.fail_write=fault.physical(0xe000,0);else fault.fail_read=fault.physical(0xd000,0x3000);
        const auto before=std::vector<uint32_t>(fault.apu.ep_word(0x200),fault.apu.ep_word(0x200)+4);
        CHECK(fault.apu.ep_write(0xffffd4,0x20) && fault.apu.ep_write(0xffffd6,1) && !fault.apu.service_dsp_dma(xlle::ApuDspKind::Ep,4096));
        CHECK(!fault.apu.ep_dma_nodes() && std::equal(before.begin(),before.end(),fault.apu.ep_word(0x200)));
        CHECK(!fault.apu.ep_write(0xffffd6,1) && fault.apu.faulted());
        if(output)CHECK(fault.ram[fault.physical(0xe000,0x1ffd)]==0x34 && fault.apu.RegFileMmio::mmio_read(0xfe80402c,4)==0x1ffd);
    }
    // A four-word FIFO object may end exactly at the EP X-bank boundary;
    // adjacent unowned words must not be fetched as circular metadata.
    Fixture edge;*edge.apu.ep_word(0x200)=0x123456;*edge.apu.ep_word(0x201)=0x654321;
    edge.descriptor(true,0xbfc,0x4000,0x4402,2,0x200);
    CHECK(edge.apu.ep_write(0xffffd4,0xbfc) && edge.apu.ep_write(0xffffd6,1) && edge.apu.service_dsp_dma(xlle::ApuDspKind::Ep,4096) && edge.apu.ep_dma_nodes()==1);
    CHECK(edge.ram[edge.physical(0xe000,0x1ffd)]==0x34 && edge.ram[edge.physical(0xe000,0x1ffe)]==0x12 &&
          edge.ram[edge.physical(0xe000,0x1fff)]==0x43 && edge.ram[edge.physical(0xe000,0)]==0x65);
    // Partial/unknown EP apertures remain explicit, including EPRST and MIX.
    for(unsigned address:{0x53000u,0x56400u,0x5fffcu}){Fixture fault;fault.reg(address,0x123456);CHECK(fault.apu.faulted() && !*fault.apu.gp_word(0));}
}
static std::vector<uint32_t> original(const char* path) {
    FILE* file=std::fopen(path,"rb");CHECK(file);std::vector<uint32_t> p(2048);CHECK(std::fread(p.data(),4,p.size(),file)==p.size() && std::fgetc(file)==EOF);std::fclose(file);for(auto& v:p)v&=0xffffff;return p;
}
static void cpu_guard(const std::string& mode,const char* path) {
    Fixture f;std::vector<uint32_t> p;
    if(mode=="--original-entry") {p=original(path);}
    else if(mode=="--original-bit9") {
        p=original(path);p[0]=0x0af080;p[1]=0x700;
        // Original276 and2E1 remain unchanged; builder owns its actual P node.
        const uint32_t caller[]={0x308000,0x61f400,0xc00,0x64f400,0x1a2,0x382000,0x3d0100,0x0bf080,0x276,0x0bf080,0x2e1,0x87};
        std::copy(std::begin(caller),std::end(caller),p.begin()+0x700);f.expected_guard="unqualified EP DMA descriptor format";
    } else if(mode=="startup-busy") {p={0x08f485,0xfff,0x87};f.expected_guard="startup event clear requires an empty initial context";}
    else if(mode=="bank") {p={0x65f400,0xbfc,0x021594,0x87};f.expected_guard="unmapped DSP read";}
    else {p={0x08f494,0x20,0x08f496,1,0x87};f.expected_guard="EP executable host publication during DMA work";}
    f.boot(true,p);xlle::ApuEpProcessor ep(f.apu,[&](const std::string& s){f.fault(s);});CHECK(ep.bootstrap_from_scratch());
    if(mode=="--original-entry") {
        const auto ram=f.ram;const auto gp=std::vector<uint32_t>(f.apu.gp_word(0),f.apu.gp_word(0)+0x3800);
        xlle::ApuEpRun r;CHECK(ep.run(10000,r) && r.yielded && r.pc==0x2f3 && r.sp==2 && !r.power);
        CHECK(!f.events(true) && !f.apu.ep_dma_nodes() && !f.apu.gp_dma_nodes() && f.ram==ram);
        CHECK(std::equal(gp.begin(),gp.end(),f.apu.gp_word(0)) && f.apu_error.empty() && f.owner_error.empty());
        const auto banks=std::vector<uint32_t>(f.apu.ep_word(0),f.apu.ep_word(0)+0x3800);
        CHECK(!ep.run(10000,r) && ep.phase()==xlle::ApuEpPhase::Failed && !r.returned);
        CHECK(f.owner_error.find("run requires a bootstrapped running CPU")!=std::string::npos);
        CHECK(std::equal(banks.begin(),banks.end(),f.apu.ep_word(0)) && f.ram==ram);
        std::puts("original EP first C4 yield: PC02F3 SP2, empty C5, no wake/DMA/output; unqualified resume rejected without bank changes");return;
    }
    if(mode=="program"){f.descriptor(true,0x20,0x4000,0x59e0,1,0x2800,0x2ffd);f.unsafe_host_publication=true;}
    if(mode=="startup-busy") {
        f.descriptor(true,0x20,0x4000,0x59e0,1,0x200,0x2ffd);
        CHECK(f.apu.ep_write(0xffffd4,0x20) && f.apu.ep_write(0xffffd6,1) && f.apu.service_dsp_dma(xlle::ApuDspKind::Ep,4096));
        CHECK(f.apu.ep_dma_nodes()==1 && f.events(true)==0x80);f.saved_nodes=1;
    }
    if(mode!="--original-bit9" && mode!="--original-entry")f.saved_p.assign(f.apu.ep_word(0x2800),f.apu.ep_word(0x2800)+4096);
    f.saved_gp.assign(f.apu.gp_word(0x1400),f.apu.gp_word(0x1400)+1024);f.saved_ram=f.ram;f.saved_cursor=f.apu.mmio_read(0xfe80402c,4);f.exit_guard=true;
    xlle::ApuEpRun r;ep.run(10000,r);CHECK(false);
}
int main(int argc,char** argv) {
    if(argc==3){cpu_guard(argv[1],argv[2]);return std::string(argv[1])=="--original-entry"?0:1;}
    if(argc==2){cpu_guard(argv[1],nullptr);return 1;}
    CHECK(argc==1);coherent_lifetimes();executable_publication_and_yield();contexts_and_failures();
    std::puts("APU EP:30 real VP/GP/scratch/EP/FIFO PCM packets, static distinct banks, owner replacement/remap, exact output RAM and terminal failures passed; cadence/AC97/original boot remain guarded");
}
