// Coherent actual VP/GP producer -> physical scratch -> EP CPU -> output RAM.
// Authored supported programs do not replace original timer/cadence paths.
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

    // EP C4 publication continues to the real STOP; it cannot halt the EP or
    // release the separate outstanding GP MIX lease.
    Fixture leased;leased.voice(71,false,0,4096);leased.boot(false,gp_program());
    leased.descriptor(false,0x20,0x4000,0x59e2,64,0x200,0x2ffd);
    xlle::ApuGpProcessor gp(leased.apu,[&](const std::string& s){leased.fault(s);});CHECK(gp.bootstrap_from_scratch());
    xlle::ApuGpRun g;CHECK(gp.run(1000,g) && g.yielded);xlle::ApuVoiceQuantum q;CHECK(gp.start_frame(q) && q.published);
    leased.boot(true,{0x08f485,0xfff,0x08f484,1,0x87});xlle::ApuEpProcessor yielding(leased.apu,[&](const std::string& s){leased.fault(s);});
    CHECK(yielding.bootstrap_from_scratch() && yielding.run(1000,e) && !e.yielded && e.power==2 && e.pc==5);
    uint32_t control_value=0;CHECK(leased.apu.ep_control_publication(control_value) && control_value==1);
    CHECK(leased.apu.gp_frame_outstanding() && leased.events(false)==2 && !leased.events(true));
    CHECK(gp.run(5000,g) && g.yielded && !leased.apu.gp_frame_outstanding());
    CHECK(f.apu_error.empty() && f.owner_error.empty() && leased.apu_error.empty() && leased.owner_error.empty());
    // An external write cannot borrow the CPU's bootstrap admission, even
    // with an empty EP event word and an existing stopped owner.
    CHECK(!yielding.reset_cpu() && leased.owner_error.find("control publication")!=std::string::npos);
    xlle::ApuEpProcessor rebound(leased.apu,[&](const std::string& s){leased.owner_error=s;});
    CHECK(rebound.phase()==xlle::ApuEpPhase::Failed && leased.owner_error.find("warm rebind")!=std::string::npos);
    CHECK(!leased.apu.ep_write(0xffffc5,0xfff) && leased.apu.faulted());
}
static void observed_empty_configuration() {
    Fixture f;unsigned value=~0u;
    CHECK(f.apu.ep_read(0xffffd6,value) && value==0);
    CHECK(f.apu.ep_read(0xffffca,value) && value==0);
    const auto ram=f.ram;
    CHECK(f.apu.ep_write(0xffffca,0) && f.apu.ep_write(0xffffd5,0x4000) &&
          f.apu.ep_write(0xffffd4,0x4000) && f.apu.ep_write(0xffffd7,0));
    for(unsigned address:{0xffffd4u,0xffffd5u})CHECK(f.apu.ep_read(address,value) && value==0x4000);
    for(unsigned address:{0xffffc5u,0xffffcau,0xffffd6u,0xffffd7u})CHECK(f.apu.ep_read(address,value) && value==0);
    CHECK(!f.apu.dsp_dma_pending(xlle::ApuDspKind::Ep) && !f.apu.ep_dma_nodes() && f.ram==ram);
    // Configuration cannot silently discard real pending DMA.
    Fixture busy;busy.descriptor(true,0x20,0x4000,0x59e0,1,0x200,0x2ffd);
    CHECK(busy.apu.ep_write(0xffffd4,0x20) && busy.apu.ep_write(0xffffd6,1));
    CHECK(!busy.apu.ep_write(0xffffca,0) && busy.apu.faulted() && !busy.apu.ep_dma_nodes());
    Fixture gp;CHECK(!gp.apu.gp_write(0xffffca,0) && gp.apu.faulted());
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
static void event_clock_and_ownership() {
    for(unsigned life=0;life<3;++life) {
        Fixture f;
        std::vector<uint32_t> p={0x08f485,0xfff,0x08f484,1};
        const unsigned first_wait=unsigned(p.size());p.insert(p.end(),{0x0a8581,first_wait});
        p.insert(p.end(),{0x44f000,0xffffc5,0x447000,0x100,0x08f485,2,
                         0x44f000,0xffffc5,0x447000,0x101});
        const unsigned next_control=unsigned(p.size());control(p,0xffffc4,1);
        const unsigned second_wait=unsigned(p.size());p.insert(p.end(),{0x0a8581,second_wait});
        p.insert(p.end(),{0x44f000,0xffffc5,0x447000,0x102,0x87});
        f.boot(true,p);const auto ram=f.ram;
        const auto gp=std::vector<uint32_t>(f.apu.gp_word(0),f.apu.gp_word(0)+0x3800);
        {
            xlle::ApuEpProcessor ep(f.apu,[&](const std::string& s){f.fault(s);});
            f.apu.set_frame(1);CHECK(!f.events(true) && !f.apu.ep_frame_events());
            CHECK(ep.bootstrap_from_scratch());
            f.apu.set_frame(2);CHECK(!f.events(true) && !f.apu.ep_frame_events());
            xlle::ApuEpRun r;
            auto reach=[&](unsigned pc){for(unsigned n=0;n<1024;++n){CHECK(ep.run(1,r) && !r.power && !r.sp);if(r.pc==pc)return;}CHECK(false);};
            reach(first_wait);
            for(unsigned i=0;i<100;++i)CHECK(!f.events(true));
            CHECK(ep.run(100,r) && r.pc==first_wait && !f.events(true));
            f.reg(0x2000,7);f.apu.set_frame(3);CHECK(!f.events(true));
            f.reg(0x2000,15);f.reg(0x1100,0x7138f);f.apu.set_frame(4);CHECK(!f.events(true));
            f.reg(0x1100,0x7930f);f.apu.set_frame(5);
            CHECK(f.events(true)==0x102 && f.apu.ep_frame_events()==1 && !f.events(false));
            f.apu.set_frame(5);CHECK(f.apu.ep_frame_events()==1);
            reach(next_control);
            CHECK(*f.apu.ep_word(0x100)==0x102 && *f.apu.ep_word(0x101)==0x102 && f.events(true)==0x100);
            reach(second_wait);CHECK(f.events(true)==0x100);
            f.apu.set_time(7*18000,3375000);
            for(unsigned i=0;i<100;++i)CHECK(f.events(true)==0x100);
            CHECK(ep.run(100,r) && r.pc==second_wait && f.apu.ep_frame_events()==1);
            f.apu.set_frame(6);CHECK(f.events(true)==0x102 && f.apu.ep_frame_events()==2);
            CHECK(ep.run(100,r) && r.power==2 && *f.apu.ep_word(0x102)==0x102);
            f.apu.set_frame(7);CHECK(f.apu.ep_frame_events()==2);
            CHECK(std::equal(p.begin(),p.end(),f.apu.ep_word(0x2800)));
        }
        // Removing the CPU clears its callback, not the device's retained
        // event/control state. A replacement cannot borrow a warm lifetime.
        f.apu.set_frame(8);CHECK(f.events(true)==0x102 && f.apu.ep_frame_events()==2);
        CHECK(std::equal(gp.begin(),gp.end(),f.apu.gp_word(0)) && f.ram==ram);
        CHECK(!f.apu.ep_dma_nodes() && !f.apu.gp_dma_nodes() && f.owner_error.empty() && f.apu_error.empty());
        xlle::ApuEpProcessor rebound(f.apu,[&](const std::string& s){f.fault(s);});
        CHECK(rebound.phase()==xlle::ApuEpPhase::Failed && f.owner_error.find("warm rebind")!=std::string::npos);
    }
    Fixture busy;busy.boot(true,{0x08f484,1,0x0a8581,2});
    xlle::ApuEpProcessor ep(busy.apu,[&](const std::string& s){busy.fault(s);});
    xlle::ApuEpRun r;CHECK(ep.bootstrap_from_scratch() && ep.run(100,r));
    busy.descriptor(true,0x20,0x4000,0x59e0,1,0x200,0x2ffd);
    CHECK(busy.apu.ep_write(0xffffd4,0x20) && busy.apu.ep_write(0xffffd6,1));
    const auto ram=busy.ram;busy.apu.set_frame(1);
    CHECK(busy.apu.faulted() && busy.apu_error.find("overlaps unqualified DMA")!=std::string::npos);
    CHECK(!busy.apu.ep_frame_events() && !busy.apu.ep_dma_nodes() && busy.ram==ram);
}
static void acknowledgement_ownership() {
    for(unsigned failure=0;failure<6;++failure) {
        Fixture f;std::unique_ptr<xlle::ApuEpProcessor> owner;
        f.boot(true,{0x87});
        if(failure>=3) {
            owner=std::make_unique<xlle::ApuEpProcessor>(f.apu,[&](const std::string& s){f.fault(s);});
            CHECK(owner->bootstrap_from_scratch());
        }
        f.descriptor(true,0x20,0x4000,0x59e0,1,0x200,0x2ffd);
        CHECK(f.apu.ep_write(0xffffd4,0x20) && f.apu.ep_write(0xffffd6,1));
        CHECK(f.apu.service_dsp_dma(xlle::ApuDspKind::Ep,2) && f.apu.ep_write(0xffffc5,0x80));
        const auto ram=f.ram;const std::vector<uint32_t> banks(f.apu.ep_word(0),f.apu.ep_word(0)+0x3800);
        CHECK(f.events(true)==0x80 && !f.apu.dsp_dma_pending(xlle::ApuDspKind::Ep));
        if(failure==0)CHECK(!f.apu.service_dsp_acknowledgements(xlle::ApuDspKind::Ep,4097));
        if(failure==1)CHECK(!f.apu.ep_write(0xffffc5,0x80));
        if(failure==2) {
            xlle::ApuEpProcessor blocked(f.apu,[&](const std::string& s){f.fault(s);});
            CHECK(blocked.phase()==xlle::ApuEpPhase::Failed && !blocked.bootstraps());
        }
        if(failure==3)CHECK(!owner->reset_cpu());
        if(failure==4){owner.reset();CHECK(f.apu.faulted());}
        if(failure==5) {
            CHECK(f.apu.ep_write(0xffffd4,0x20) && f.apu.ep_write(0xffffd6,1));
            CHECK(!f.apu.service_dsp_dma(xlle::ApuDspKind::Ep,4096));
        }
        CHECK(f.apu.dsp_work_pending(xlle::ApuDspKind::Ep) && f.apu.ep_dma_nodes()==1);
        CHECK(f.ram==ram && std::equal(banks.begin(),banks.end(),f.apu.ep_word(0)));
    }
}
static void lookup_ownership() {
    Fixture f;std::vector<uint32_t> image(2048);
    for(unsigned i=0;i<2048;++i)image[i]=(0x812345+i*0x10203)&0xffffff;
    uint32_t value=0xa5a5a5;CHECK(!f.apu.ep_y_lookup_read(0xfc9,value) && value==0xa5a5a5);
    CHECK(!f.apu.install_ep_y_lookup(nullptr,2048) && !f.apu.install_ep_y_lookup(image.data(),2047));
    const auto saved=image;image[17]|=0x1000000;
    CHECK(!f.apu.install_ep_y_lookup(image.data(),2048));image=saved;
    CHECK(f.apu.install_ep_y_lookup(image.data(),2048));std::fill(image.begin(),image.end(),0xffffff);
    CHECK(!f.apu.install_ep_y_lookup(image.data(),2048));
    for(unsigned a:{0x100u,0x7ffu,0x1000u})CHECK(!f.apu.ep_y_lookup_read(a,value));
    for(unsigned i=0;i<2048;++i)CHECK(f.apu.ep_y_lookup_read(0x800+i,value) && value==saved[i]);
    CHECK(!f.apu.ep_word(0x2000) && !f.apu.ep_word(0x27ff));
    *f.apu.ep_word(0x1840)=0x123456;
    f.boot(true,{0x60f400,0x800,0x61f400,0x200,0x060098,4,0x4cd800,0x445900,0,0x87});
    const auto ram=f.ram;
    xlle::ApuEpProcessor ep(f.apu,[&](const std::string& s){f.fault(s);});xlle::ApuEpRun r;
    CHECK(ep.bootstrap_from_scratch() && ep.run(30000,r) && r.power==2 && !r.sp);
    CHECK(std::equal(saved.begin(),saved.end(),f.apu.ep_word(0x200)) && *f.apu.ep_word(0x1840)==0x123456);
    CHECK(f.ram==ram && !f.apu.ep_dma_nodes() && !f.apu.gp_dma_nodes());
    Fixture late;late.boot(true,{0x87});xlle::ApuEpProcessor held(late.apu,[&](const std::string& s){late.fault(s);});
    CHECK(!late.apu.install_ep_y_lookup(image.data(),image.size()));
    CHECK(!late.apu.ep_y_lookup_read(0x800,value) && !late.apu.faulted());
}
static void physical_lookup(const char* program,const char* observations,const char* lookup) {
    std::vector<uint32_t> p(4096),fields(99),image(2048);
    const char* paths[]={program,observations,lookup};std::vector<uint32_t>* arrays[]={&p,&fields,&image};
    for(unsigned i=0;i<3;++i){FILE* f=std::fopen(paths[i],"rb");CHECK(f);auto& a=*arrays[i];CHECK(std::fread(a.data(),4,a.size(),f)==a.size() && std::fgetc(f)==EOF);std::fclose(f);}
    Fixture f;CHECK(f.apu.install_ep_y_lookup(image.data(),image.size()));
    f.boot(true,std::vector<uint32_t>(p.begin(),p.begin()+2048));
    xlle::ApuEpProcessor ep(f.apu,[&](const std::string& s){f.fault(s);});CHECK(ep.bootstrap_from_scratch());
    auto expected=std::vector<uint32_t>(f.apu.ep_word(0),f.apu.ep_word(0)+0x3800);
    const auto ram=f.ram;const auto gp=std::vector<uint32_t>(f.apu.gp_word(0),f.apu.gp_word(0)+0x3800);
    xlle::ApuEpRun r;CHECK(ep.run(20000,r) && !r.power && !r.sp && !r.yielded);
    unsigned slot=0;
    for(unsigned i=0x100;i<0x10d;++i)expected[i]=fields[slot++];
    for(unsigned i=0x110;i<0x118;++i)expected[i]=fields[slot++];
    for(unsigned i=0x200;i<0x205;++i)expected[i]=fields[slot++];
    for(unsigned i=0x400;i<0x404;++i)expected[i]=fields[slot++];
    for(unsigned i=0x1840;i<0x1844;++i)expected[i]=fields[slot++];
    for(unsigned start:{0x200u,0x220u,0x240u})for(unsigned i=0;i<16;++i)expected[start+i]=fields[slot++];
    expected[0x260]=fields[slot++];
    for(unsigned i=0;i<16;++i)expected[0x18c0+i]=fields[slot++];
    CHECK(slot==99 && fields[10]==0x455059 && fields[11]==0 && fields[12]==2);
    CHECK(std::equal(expected.begin(),expected.end(),f.apu.ep_word(0)) && std::equal(gp.begin(),gp.end(),f.apu.gp_word(0)));
    CHECK(f.ram==ram && !f.apu.ep_dma_nodes() && !f.apu.gp_dma_nodes() && !f.apu.ep_frame_events());
    CHECK(!f.apu.dsp_work_pending(xlle::ApuDspKind::Ep) && f.apu_error.empty() && f.owner_error.empty());
    std::puts("Physical EP Y lookup: exact99 captured fields/data, full bank/RAM preservation, distinct immutable image owner; no DMA, wake or output");
}
static void stereo_fifo_ownership() {
    for(unsigned control:{0x400403u,0x400603u})for(bool fail:{false,true}) {
        Fixture f;
        for(unsigned i=0;i<256;++i) {
            *f.apu.ep_word(0x400+i)=(0x1000f1+i*0x12300)&0xffffff;
            *f.apu.ep_word(0x500+i)=(0x9000a7+i*0x20100)&0xffffff;
        }
        // Four-word object at the bank end, with a byte-misaligned cursor
        // that wraps after three bytes into a different physical page.
        f.descriptor(true,0xbfc,0x4000,control,0x1001,0x400);
        const auto banks=std::vector<uint32_t>(f.apu.ep_word(0),f.apu.ep_word(0)+0x3800);
        auto expected=f.ram;
        for(unsigned i=0;i<(fail?3u:1024u);++i) {
            const unsigned frame=i/4,channel=(i/2)%2,byte=i%2;
            expected[f.physical(0xe000,(0x1ffd+i)%0x2000)]=uint8_t(*f.apu.ep_word(0x400+channel*256+frame)>>(8+8*byte));
        }
        if(fail)f.fail_write=f.physical(0xe000,0);
        CHECK(f.apu.ep_write(0xffffd4,0xbfc) && f.apu.ep_write(0xffffd6,1));
        CHECK(f.apu.service_dsp_dma(xlle::ApuDspKind::Ep,4096)==!fail);
        CHECK(f.ram==expected && std::equal(banks.begin(),banks.end(),f.apu.ep_word(0)));
        CHECK(f.apu.ep_dma_nodes()==(fail?0u:1u) && !f.apu.gp_dma_nodes());
        CHECK(f.apu.RegFileMmio::mmio_read(0xfe80402c,4)==(fail?0x1ffdu:0x3fdu));
        if(fail)CHECK(f.apu.faulted() && !f.apu.ep_write(0xffffd6,1));
        else CHECK(f.events(true)==0x80 && !f.apu.dsp_dma_pending(xlle::ApuDspKind::Ep));
    }
    // Neighbouring flags/counts/banks and GP interleave are not established
    // by the exact EP pair. Rejection must precede any destination write.
    const unsigned cases[][4]={
        {1,0x400603,0x1001,0x400},{0,0x400602,0x1001,0x400},
        {0,0x400601,0x1001,0x400},{0,0x400623,0x1001,0x400},
        {0,0x400613,0x1001,0x400},{0,0x400a03,0x1001,0x400},
        {0,0x404603,0x1001,0x400},{0,0x400603,0x1000,0x400},
        {0,0x400603,0x1002,0x400},{0,0x400603,0x0ff1,0x400},
        {0,0x400603,0x1001,0xa01},{0,0x400603,0x1001,0x1800},
        {0,0x400603,0x1001,0x2800}};
    for(const auto& c:cases) {
        Fixture f;const bool ep=!c[0];f.descriptor(ep,0x20,0x4000,c[1],c[2],c[3]);
        const auto ram=f.ram;
        if(ep)CHECK(f.apu.ep_write(0xffffd4,0x20) && f.apu.ep_write(0xffffd6,1));
        else CHECK(f.apu.gp_write(0xffffd4,0x20) && f.apu.gp_write(0xffffd6,1));
        CHECK(!f.apu.service_dsp_dma(ep?xlle::ApuDspKind::Ep:xlle::ApuDspKind::Gp,4096));
        CHECK(f.ram==ram && !f.apu.ep_dma_nodes() && !f.apu.gp_dma_nodes());
        CHECK(f.apu.RegFileMmio::mmio_read(0xfe80402c,4)==0x1ffd);
    }
}
static void fifo1_zero_ownership() {
    for(unsigned count:{24u,1498u})for(bool failure:{false,true}) {
        Fixture f;for(unsigned i=0;i<count;++i)*f.apu.ep_word(0x1f+i)=(0x9135a7+i*0x25103)&0xffffff;
        f.reg(0x4034,0);f.reg(0x4038,0x2000);f.reg(0x403c,0x1ffd);
        f.descriptor(true,0xbfc,0x4000,0x4622,count,0x401f);
        const auto banks=std::vector<unsigned>(f.apu.ep_word(0),f.apu.ep_word(0)+0x3800);
        auto expected=f.ram;const unsigned bytes=failure?3:count*2;
        for(unsigned i=0;i<bytes;++i)expected[f.physical(0xe000,(0x1ffd+i)%0x2000)]=0;
        if(failure)f.fail_write=f.physical(0xe000,0);
        CHECK(f.apu.ep_write(0xffffd4,0xbfc) && f.apu.ep_write(0xffffd6,1));
        CHECK(f.apu.service_dsp_dma(xlle::ApuDspKind::Ep,4096)==!failure);
        CHECK(f.ram==expected && std::equal(banks.begin(),banks.end(),f.apu.ep_word(0)));
        CHECK(f.apu.ep_dma_nodes()==(failure?0:1) && !f.apu.gp_dma_nodes());
        CHECK(f.apu.RegFileMmio::mmio_read(0xfe80403c,4)==(failure?0x1ffd:(0x1ffd+count*2)%0x2000));
        CHECK(f.apu.RegFileMmio::mmio_read(0xfe80402c,4)==0x1ffd);
        if(failure)CHECK(!f.apu.ep_write(0xffffd6,1) && f.apu.faulted());
        else CHECK(f.events(true)==0x80 && !f.apu.dsp_work_pending(xlle::ApuDspKind::Ep));
    }
    // Observing another source as zero does not silently admit the entire
    // flagged map, input direction, other formats or GP. Odd PCM16 counts
    // and missing banks retain the existing publication boundary.
    for(const auto& c:std::vector<std::vector<unsigned>>{
        {0x4622,24,0x441f},{0x4622,24,0x4000},{0x4622,24,0x401e},
        {0x4620,24,0x401f},{0x4a22,24,0x401f},{0x4622,0,0x401f},
        {0x4622,25,0x401f},{0x4622,4098,0x401f},{0x4622,24,0x1800},
        {0x4622,24,0x2800},{0x4622,24,0xbf0}}) {
        Fixture f;f.descriptor(true,0x20,0x4000,c[0],c[1],c[2]);const auto ram=f.ram;
        const auto banks=std::vector<unsigned>(f.apu.ep_word(0),f.apu.ep_word(0)+0x3800);
        CHECK(f.apu.ep_write(0xffffd4,0x20) && f.apu.ep_write(0xffffd6,1));
        CHECK(!f.apu.service_dsp_dma(xlle::ApuDspKind::Ep,4096) && !f.apu.ep_dma_nodes());
        CHECK(f.ram==ram && std::equal(banks.begin(),banks.end(),f.apu.ep_word(0)));
    }
    Fixture gp;gp.descriptor(false,0x20,0x4000,0x4622,24,0x401f);const auto ram=gp.ram;
    CHECK(gp.apu.gp_write(0xffffd4,0x20) && gp.apu.gp_write(0xffffd6,1));
    CHECK(!gp.apu.service_dsp_dma(xlle::ApuDspKind::Gp,4096) && !gp.apu.gp_dma_nodes() && gp.ram==ram);
}
static void physical_output(const char* image,const char* observations,unsigned profile=0) {
    CHECK(profile<=2);const bool interleaved=profile==1,source_mode=profile==2,fifo=profile!=0;
    const unsigned source_words=source_mode?26:interleaved?514:80;unsigned destination_words=fifo?256:78;
    std::vector<uint32_t> p(4096),values(source_mode?578:interleaved?1063:270);
    FILE* file=std::fopen(image,"rb");CHECK(file);CHECK(std::fread(p.data(),4,p.size(),file)==p.size() && std::fgetc(file)==EOF);std::fclose(file);
    file=std::fopen(observations,"rb");CHECK(file);
    if(source_mode){CHECK(!std::fseek(file,0,SEEK_END));const auto bytes=std::ftell(file);CHECK(bytes==578*4 || bytes==1564*4);std::rewind(file);values.resize(bytes/4);if(bytes==1564*4)destination_words=749;}
    CHECK(std::fread(values.data(),4,values.size(),file)==values.size() && std::fgetc(file)==EOF);std::fclose(file);
    CHECK(values[10]==(source_mode?0x455053u:interleaved?0x455046u:0x45504fu) && values[11]==0 && values[12]==2);
    CHECK(values[21]==0x4200 && (source_mode?(values[22]==0x4622 && values[23]==(destination_words==749?1498u:24u) && (values[24]==0x1f || values[24]==0x401f) && values[25]==0x6b4000):interleaved?((values[22]==0x400403 || values[22]==0x400603) && values[23]==0x1001 && values[24]==0x400 && values[25]==0x6b4000):((values[22]==0x59e2 || values[22]==0x5be2) && values[23]==78 && values[24]==0x63d)));
    const unsigned ring_slot=34+source_words+destination_words*2;
    const unsigned physical=(fifo?values[ring_slot]:p[1023])-0x6000;
    CHECK(physical && physical<=0x600000 && !(physical&0x3fff));
    if(!fifo)CHECK(values[25]==p[1023]);
    else CHECK(values[ring_slot+1]==physical+0x7000 && values[ring_slot+2]==physical+0x6000+(source_mode?values[23]*2:1024));
    for(unsigned policy:{1u,2u,4u}) {
        Fixture f;f.ram.resize(0x609000,0xa5);f.reg(0x2048,physical);f.reg(0x20dc,2047);
        for(unsigned i=0;i<2048;++i){f.put(physical+i*8,physical+(i<4?i+4:7)*4096);f.put(physical+i*8+4,0);}
        for(unsigned i=0;i<2048;++i)f.put(physical+0x4000+i*4,p[i]);
        for(unsigned i=0;i<destination_words;++i){f.put(physical+0x6000+i*4,0xaa100000+i*0x10203);f.put(physical+0x7000+i*4,0xbb400000+i*0x30405);}
        const unsigned ring=source_mode?0x4034:0x4024;
        if(fifo){f.reg(0x204c,physical);f.reg(0x20e0,2047);f.reg(ring,physical+0x6000);f.reg(ring+4,physical+0x7000);f.reg(ring+8,physical+0x6000);}
        if(source_mode)for(unsigned i=0;i<3;++i)f.reg(0x4024+i*4,values[ring_slot+3+i]);
        xlle::ApuEpProcessor ep(f.apu,[&](const std::string& s){f.fault(s);},{policy});CHECK(ep.bootstrap_from_scratch());
        auto expected_ram=f.ram;
        for(unsigned i=0;i<destination_words*2;++i) {
            const auto at=physical+(i<destination_words?0x6000:0x7000)+(i%destination_words)*4;
            std::memcpy(expected_ram.data()+at,&values[34+source_words+i],4);
        }
        auto expected=std::vector<uint32_t>(f.apu.ep_word(0),f.apu.ep_word(0)+0x3800);
        const auto gp=std::vector<uint32_t>(f.apu.gp_word(0),f.apu.gp_word(0)+0x3800);
        xlle::ApuEpRun r;CHECK(ep.run(20000,r) && !r.power && !r.sp && !r.yielded);
        unsigned slot=0;
        for(unsigned i=0x100;i<0x10d;++i)expected[i]=values[slot++];
        for(unsigned i=0x110;i<0x118;++i)expected[i]=values[slot++];
        for(unsigned i=0x200;i<0x205;++i)expected[i]=values[slot++];
        for(unsigned i=0;i<4;++i)expected[(fifo?0x700:0x400)+i]=values[slot++];
        for(unsigned i=0x1840;i<0x1844;++i)expected[i]=values[slot++];
        CHECK(slot==34);
        for(unsigned i=0;i<source_words;++i)expected[(source_mode?0x1e:interleaved?0x3ff:0x63c)+i]=values[34+i];
        for(unsigned i=0;i<expected.size();++i) {
            // Immediate busy visibility/poll count are separately required
            // timing questions; do not normalize them into a timing pass.
            if(i==0x104 || i==0x108)continue;
            CHECK(expected[i]==*(f.apu.ep_word(0)+i));
        }
        CHECK(f.ram==expected_ram && std::equal(gp.begin(),gp.end(),f.apu.gp_word(0)));
        if(fifo)CHECK(f.apu.RegFileMmio::mmio_read(0xfe800000+ring+8,4)==values[ring_slot+2]);
        if(source_mode)for(unsigned i=0;i<3;++i)CHECK(f.apu.RegFileMmio::mmio_read(0xfe804024+i*4,4)==values[ring_slot+3+i]);
        CHECK(f.apu.ep_dma_nodes()==1 && !f.apu.gp_dma_nodes() && !f.apu.ep_frame_events());
        CHECK(!f.apu.dsp_work_pending(xlle::ApuDspKind::Ep) && f.apu_error.empty() && f.owner_error.empty());
    }
    std::printf("Frozen EP %s output:%u exact destination DWORDs, untouched alternate,%u source/canary words,32 control fields and2048 P; policies1/2/4. Busy/poll timing remains open.\n",source_mode?"source-mode FIFO1":interleaved?"interleaved FIFO":"linear",destination_words,source_words);
}
static void physical_events(const char* image,const char* observations,bool delayed_ack=false) {
    const unsigned frames=delayed_ack?2:8;
    std::vector<uint32_t> p(4096),values(delayed_ack?37:34);
    FILE* file=std::fopen(image,"rb");CHECK(file);CHECK(std::fread(p.data(),4,p.size(),file)==p.size() && std::fgetc(file)==EOF);std::fclose(file);
    file=std::fopen(observations,"rb");CHECK(file);CHECK(std::fread(values.data(),4,values.size(),file)==values.size() && std::fgetc(file)==EOF);std::fclose(file);
    CHECK(values[8]==frames && values[9]==0x102 && values[10]==0x455057 && values[11]==0 && values[12]==2);
    CHECK(values[22]==frames+1 && values[23]==frames && values[24]==(delayed_ack?0x100u:0x102u) && values[25]==0x100);
    if(delayed_ack)CHECK(values[34]==0x102 && values[35]==0x100 && values[36]==0x100);
    Fixture f;f.boot(true,std::vector<uint32_t>(p.begin(),p.begin()+2048));
    xlle::ApuEpProcessor ep(f.apu,[&](const std::string& s){f.fault(s);});CHECK(ep.bootstrap_from_scratch());
    const auto ram=f.ram;const auto gp=std::vector<uint32_t>(f.apu.gp_word(0),f.apu.gp_word(0)+0x3800);
    auto expected=std::vector<uint32_t>(f.apu.ep_word(0),f.apu.ep_word(0)+0x3800);
    xlle::ApuEpRun r;CHECK(ep.run(1000,r) && !r.power && !r.sp && !f.events(true));
    CHECK(*f.apu.ep_word(0x201)==1 && !*f.apu.ep_word(0x108));
    f.reg(0x1100,0x7930f);f.reg(0x2000,15);
    for(unsigned frame=1;frame<=frames;++frame) {
        // Clock owns events; the CPU executes its actual polling/ack/C4 code.
        // This bounded control-flow check intentionally does not claim an
        // exact DSP clock budget, B3 epoch or 708000 processing threshold.
        f.apu.set_time(frame*18000,3375000);f.apu.set_frame(frame);
        CHECK(f.events(true)==0x102 && f.apu.ep_frame_events()==frame);
        CHECK(ep.run(1000,r) && !r.power && !r.sp && !r.yielded);
        CHECK(*f.apu.ep_word(0x108)==frame && *f.apu.ep_word(0x202)==frame && *f.apu.ep_word(0x201)==frame+1);
        CHECK(f.events(true)==0x100 && !*f.apu.ep_word(0x10b));
    }
    f.reg(0x2000,7);f.apu.set_frame(frames+1);CHECK(f.apu.ep_frame_events()==frames);
    unsigned slot=0;
    for(unsigned i=0x100;i<0x10d;++i)expected[i]=values[slot++];
    for(unsigned i=0x110;i<0x118;++i)expected[i]=values[slot++];
    for(unsigned i=0x200;i<0x205;++i)expected[i]=values[slot++];
    for(unsigned i=0x400;i<0x404;++i)expected[i]=values[slot++];
    for(unsigned i=0x1840;i<0x1844;++i)expected[i]=values[slot++];
    CHECK(slot==34);
    if(delayed_ack){expected[0x300]=values[34];expected[0x310]=values[35];expected[0x311]=values[36];}
    for(unsigned i=0;i<expected.size();++i) {
        if((i>=0x100 && i<0x100+frames) || i==0x200)continue; // Timer samples, separately open.
        if(expected[i]!=*(f.apu.ep_word(0)+i))std::fprintf(stderr,"EP event observer bank%04x expected%06x actual%06x\n",i,expected[i],*(f.apu.ep_word(0)+i));
        CHECK(expected[i]==*(f.apu.ep_word(0)+i));
    }
    CHECK(std::equal(p.begin(),p.begin()+2048,f.apu.ep_word(0x2800)) && std::equal(gp.begin(),gp.end(),f.apu.gp_word(0)));
    CHECK(f.ram==ram && !f.apu.ep_dma_nodes() && !f.apu.gp_dma_nodes() && f.apu_error.empty() && f.owner_error.empty());
    std::printf("Frozen EP event observer:%u clock-owned events,%u exact control/canary/ACK fields,2048 P words, other EP/GP banks and RAM preserved;%u timer samples/clock budget remain unqualified\n",frames,delayed_ack?34:25,frames+1);
}
static void physical_configuration(const char* image,const char* observations,bool c4=false) {
    std::vector<uint32_t> p(4096),values(34);
    FILE* file=std::fopen(image,"rb");CHECK(file);CHECK(std::fread(p.data(),4,4096,file)==4096 && std::fgetc(file)==EOF);std::fclose(file);
    file=std::fopen(observations,"rb");CHECK(file);CHECK(std::fread(values.data(),4,34,file)==34 && std::fgetc(file)==EOF);std::fclose(file);
    if(!c4)CHECK(values[4]==0x4000);
    Fixture f;f.boot(true,std::vector<uint32_t>(p.begin(),p.begin()+2048));
    // Captured pre-stimulus D5 is an input context, not a universal reset value.
    if(!c4)CHECK(f.apu.ep_write(0xffffd5,values[4]));
    xlle::ApuEpProcessor ep(f.apu,[&](const std::string& s){f.fault(s);});CHECK(ep.bootstrap_from_scratch());
    const auto ram=f.ram;const auto banks=std::vector<uint32_t>(f.apu.ep_word(0),f.apu.ep_word(0)+0x3800);
    const auto gp=std::vector<uint32_t>(f.apu.gp_word(0),f.apu.gp_word(0)+0x3800);
    xlle::ApuEpRun r;CHECK(ep.run(10000,r) && !r.power && !r.sp && !r.yielded);
    std::vector<uint32_t> expected=banks;unsigned slot=0;
    for(unsigned i=0x100;i<0x10d;++i)expected[i]=values[slot++];
    for(unsigned i=0x110;i<0x118;++i)expected[i]=values[slot++];
    for(unsigned i=0x200;i<0x205;++i)expected[i]=values[slot++];
    for(unsigned i=0x400;i<0x404;++i)expected[i]=values[slot++];
    for(unsigned i=0x1840;i<0x1844;++i)expected[i]=values[slot++];
    // C4 control-flow comparison excludes the two B3 samples. The inherited
    // pre-configuration input and branch timing are separately unqualified:
    // this physical prefix measured32, whereas retired-cycle modeling gives31.
    // Do not tune the counter to this fixture or claim a timer match.
    CHECK(slot==34);
    for(unsigned i=0;i<expected.size();++i) {
        if(c4 && (i==0x200 || i==0x204))continue;
        if(expected[i]!=*(f.apu.ep_word(0)+i))
            std::fprintf(stderr,"EP observer bank%04x expected%06x actual%06x\n",i,expected[i],*(f.apu.ep_word(0)+i));
        CHECK(expected[i]==*(f.apu.ep_word(0)+i));
    }
    CHECK(std::equal(p.begin(),p.begin()+2048,f.apu.ep_word(0x2800)) && std::equal(gp.begin(),gp.end(),f.apu.gp_word(0)));
    CHECK(f.ram==ram && !f.apu.ep_dma_nodes() && !f.apu.gp_dma_nodes() && !f.apu.dsp_dma_pending(xlle::ApuDspKind::Ep));
    CHECK(f.apu_error.empty() && f.owner_error.empty());
    if(c4) {
        unsigned control=0;CHECK(f.apu.ep_control_publication(control) && control==1);
        CHECK(values[11]==3 && values[12]==3 && values[22]==1 && values[23]==0);
        const auto stable=std::vector<uint32_t>(f.apu.ep_word(0),f.apu.ep_word(0)+0x3800);
        CHECK(ep.run(10000,r) && !r.yielded && !r.power && std::equal(stable.begin(),stable.end(),f.apu.ep_word(0)));
        std::printf("Frozen EP C4 observer:32 exact control/canary fields and2048 P words; C4 continues to C5=0/error3 without a frame; configured B3 hardware%u model%u UNQUALIFIED, inherited B3 excluded; other banks/RAM preserved\n",values[21],*f.apu.ep_word(0x200));
    } else std::puts("Frozen EP physical observer:34 exact fields,2048 P words, other EP/GP banks and RAM preserved; initial D5 supplied as captured context, no wake/DMA/output claim");
}
static void cpu_guard(const std::string& mode,const char* path) {
    Fixture f;std::vector<uint32_t> p;
    if(mode=="--original-entry" || mode=="--original-config-tail" || mode=="--original-event") {
        p=original(path);
        if(mode=="--original-config-tail") {
            p[0]=0x0af080;p[1]=0x700;
            // Authored caller of unchanged post-yield setup; no claimed wake.
            p[0x700]=0x0bf080;p[0x701]=0x2f3;p[0x702]=0x87;
        }
    }
    else if(mode=="--original-bit9") {
        p=original(path);p[0]=0x0af080;p[1]=0x700;
        // Original276 and2E1 remain unchanged; builder owns its actual P node.
        const uint32_t caller[]={0x308000,0x61f400,0xc00,0x64f400,0x1a2,0x382000,0x3d0100,0x0bf080,0x276,0x0bf080,0x2e1,0x87};
        std::copy(std::begin(caller),std::end(caller),p.begin()+0x700);
    } else if(mode=="startup-busy") {p={0x08f485,0xfff,0x87};f.expected_guard="startup event clear requires an empty initial context";}
    else if(mode=="startup-frame-unowned") {p={0x08f485,2,0x87};f.expected_guard="frame command requires idle control ownership";}
    else if(mode=="startup-frame-busy") {p={0x08f484,1,0x08f485,2,0x87};f.expected_guard="frame command requires idle control ownership";}
    else if(mode=="startup-clear-after-control") {p={0x08f484,1,0x08f485,0xfff,0x87};f.expected_guard="startup event clear requires an empty initial context";}
    else if(mode=="lookup-write") {
        std::vector<uint32_t> image(2048,0x123456);CHECK(f.apu.install_ep_y_lookup(image.data(),image.size()));
        p={0x44f400,0x654321,0x4c7000,0xfc9,0x87};f.expected_guard="unqualified EP high Y write at 000fc9";
    }
    else if(mode=="lookup-missing") {p={0x4cf000,0xfc9,0x87};f.expected_guard="unmapped DSP read at 000fc9";}
    else if(mode=="bank") {p={0x65f400,0xbfc,0x021594,0x87};f.expected_guard="unmapped DSP read";}
    else {p={0x08f494,0x20,0x08f496,1,0x87};f.expected_guard="EP executable host publication during DMA work";}
    f.boot(true,p);xlle::ApuEpProcessor ep(f.apu,[&](const std::string& s){f.fault(s);});CHECK(ep.bootstrap_from_scratch());
    if(mode=="--original-bit9") {
        // The unchanged builder/helper executes the independently admitted
        // input profile. Count32 is an offline extent check; silicon used24.
        for(unsigned i=0;i<32;++i)f.scratch(0xd000,0x3000+i*4,0xaa120000+i*0x10203);
        const auto ram=f.ram;const std::vector<uint32_t> gp(f.apu.gp_word(0),f.apu.gp_word(0)+0x3800);
        const auto before=std::vector<uint32_t>(f.apu.ep_word(0),f.apu.ep_word(0)+0x3800);
        xlle::ApuEpRun r;CHECK(ep.run(10000,r) && r.power==2 && !r.sp && !r.yielded && r.pc==0x70c);
        const unsigned expected[]={0x41a2,0x5be0,32,0x80,0x3000};
        for(unsigned i=0;i<5;++i) {
            const auto actual=*f.apu.ep_word(0x29a2+i);
            if(actual!=expected[i])std::fprintf(stderr,"original helper descriptor[%u]=%x expected%x\n",i,actual,expected[i]);
            CHECK(actual==expected[i]);
        }
        for(unsigned i=0;i<32;++i)CHECK(*f.apu.ep_word(0x80+i)==((0xaa120000+i*0x10203)&0xffffff));
        for(unsigned i=0;i<0x3800;++i)if(!(i>=0x80 && i<0xa0) && !(i>=0x29a2 && i<0x29a7))CHECK(f.apu.ep_word(0)[i]==before[i]);
        CHECK(f.apu.ep_dma_nodes()==1 && !f.apu.gp_dma_nodes() && f.ram==ram && !f.apu.ep_frame_events());
        CHECK(std::equal(gp.begin(),gp.end(),f.apu.gp_word(0)) && f.apu_error.empty() && f.owner_error.empty());
        std::puts("original EP linear input: unchanged276/2E1 copy32 exact words and return; authored caller, no boot/output/timing claim");return;
    }
    if(mode=="--original-config-tail") {
        const auto ram=f.ram;const auto banks=std::vector<uint32_t>(f.apu.ep_word(0),f.apu.ep_word(0)+0x3800);
        const auto gp=std::vector<uint32_t>(f.apu.gp_word(0),f.apu.gp_word(0)+0x3800);
        xlle::ApuEpRun r;CHECK(ep.run(1000,r) && r.pc==0x703 && r.power==2 && !r.sp && !r.yielded);
        unsigned v;for(unsigned a:{0xffffd4u,0xffffd5u})CHECK(f.apu.ep_read(a,v) && v==0x4000);
        for(unsigned a:{0xffffc5u,0xffffcau,0xffffd6u,0xffffd7u})CHECK(f.apu.ep_read(a,v) && !v);
        CHECK(!f.apu.ep_dma_nodes() && !f.apu.dsp_dma_pending(xlle::ApuDspKind::Ep) && f.ram==ram);
        CHECK(std::equal(banks.begin(),banks.end(),f.apu.ep_word(0)) && std::equal(gp.begin(),gp.end(),f.apu.gp_word(0)));
        CHECK(f.apu_error.empty() && f.owner_error.empty());
        std::puts("original EP configuration tail: CA0/D5=4000/D4=4000/D7=0, actual RET, no DMA/output; authored caller, no wake claim");return;
    }
    if(mode=="--original-event") {
        xlle::ApuEpRun r;CHECK(ep.run(10000,r) && !r.yielded && (r.pc==0x31 || r.pc==0x32) && !r.sp && !r.power);
        f.expected_guard="unqualified EP DMA descriptor format";
        f.saved_gp.assign(f.apu.gp_word(0x1400),f.apu.gp_word(0x1400)+1024);f.saved_ram=f.ram;f.saved_cursor=f.apu.mmio_read(0xfe80402c,4);f.exit_guard=true;
        f.apu.set_time(18000,3375000);f.apu.set_frame(1);
        CHECK(f.events(true)==0x102 && f.apu.ep_frame_events()==1);
        ep.run(10000,r);CHECK(false);
    }
    if(mode=="--original-entry") {
        const auto ram=f.ram;const auto gp=std::vector<uint32_t>(f.apu.gp_word(0),f.apu.gp_word(0)+0x3800);
        xlle::ApuEpRun r;CHECK(ep.run(10000,r) && !r.yielded && (r.pc==0x31 || r.pc==0x32) && !r.sp && !r.power);
        unsigned control=0;CHECK(f.apu.ep_control_publication(control) && control==1);
        for(unsigned a:{0xffffd4u,0xffffd5u})CHECK(f.apu.ep_read(a,control) && control==0x4000);
        for(unsigned a:{0xffffc5u,0xffffcau,0xffffd6u,0xffffd7u})CHECK(f.apu.ep_read(a,control) && !control);
        CHECK(!f.events(true) && !f.apu.ep_dma_nodes() && !f.apu.gp_dma_nodes() && f.ram==ram);
        CHECK(std::equal(gp.begin(),gp.end(),f.apu.gp_word(0)) && f.apu_error.empty() && f.owner_error.empty());
        const auto banks=std::vector<uint32_t>(f.apu.ep_word(0),f.apu.ep_word(0)+0x3800);
        CHECK(ep.run(10000,r) && ep.phase()==xlle::ApuEpPhase::Running && r.returned && !r.yielded && !r.sp && !r.power);
        CHECK((r.pc==0x31 || r.pc==0x32) && f.owner_error.empty());
        CHECK(std::equal(banks.begin(),banks.end(),f.apu.ep_word(0)) && f.ram==ram);
        std::puts("original EP startup reaches its real C5 wait: PC0031/0032 SP0, configuration returned, empty acknowledgement, no invented event/DMA/output; repeated execution preserves banks");return;
    }
    if(mode=="program"){f.descriptor(true,0x20,0x4000,0x59e0,1,0x2800,0x2ffd);f.unsafe_host_publication=true;}
    if(mode=="startup-busy" || mode=="startup-frame-busy") {
        f.descriptor(true,0x20,0x4000,0x59e0,1,0x200,0x2ffd);
        CHECK(f.apu.ep_write(0xffffd4,0x20) && f.apu.ep_write(0xffffd6,1) && f.apu.service_dsp_dma(xlle::ApuDspKind::Ep,4096));
        CHECK(f.apu.ep_dma_nodes()==1 && f.events(true)==0x80);f.saved_nodes=1;
    }
    if(mode!="--original-bit9" && mode!="--original-entry")f.saved_p.assign(f.apu.ep_word(0x2800),f.apu.ep_word(0x2800)+4096);
    f.saved_gp.assign(f.apu.gp_word(0x1400),f.apu.gp_word(0x1400)+1024);f.saved_ram=f.ram;f.saved_cursor=f.apu.mmio_read(0xfe80402c,4);f.exit_guard=true;
    xlle::ApuEpRun r;ep.run(10000,r);CHECK(false);
}
int main(int argc,char** argv) {
    if(argc==4 && std::string(argv[1])=="--physical-source"){physical_output(argv[2],argv[3],2);return 0;}
    if(argc==5 && std::string(argv[1])=="--physical-lookup"){physical_lookup(argv[2],argv[3],argv[4]);return 0;}
    if(argc==4 && std::string(argv[1])=="--physical-fifo"){physical_output(argv[2],argv[3],true);return 0;}
    if(argc==4 && std::string(argv[1])=="--physical-output"){physical_output(argv[2],argv[3]);return 0;}
    if(argc==4 && std::string(argv[1])=="--physical-events"){physical_events(argv[2],argv[3]);return 0;}
    if(argc==4 && std::string(argv[1])=="--physical-ack"){physical_events(argv[2],argv[3],true);return 0;}
    if(argc==4 && std::string(argv[1])=="--physical-config"){physical_configuration(argv[2],argv[3]);return 0;}
    if(argc==4 && std::string(argv[1])=="--physical-c4"){physical_configuration(argv[2],argv[3],true);return 0;}
    if(argc==3){cpu_guard(argv[1],argv[2]);return (std::string(argv[1])=="--original-entry" || std::string(argv[1])=="--original-config-tail" || std::string(argv[1])=="--original-bit9")?0:1;}
    if(argc==2){cpu_guard(argv[1],nullptr);return 1;}
    CHECK(argc==1);coherent_lifetimes();executable_publication_and_yield();contexts_and_failures();observed_empty_configuration();event_clock_and_ownership();acknowledgement_ownership();stereo_fifo_ownership();lookup_ownership();fifo1_zero_ownership();
    std::puts("APU EP:30 real VP/GP/scratch/EP/FIFO PCM packets;3 clock/C4/C5 lifetimes, disabled/held clock and DMA guards, exact RAM/banks and retained ownership passed; connected cadence/AC97/reset remain open");
}
