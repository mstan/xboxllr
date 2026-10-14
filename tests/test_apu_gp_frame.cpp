// Representative frame ownership over real VP/MIX/CPU/DMA, with independent
// guest-memory and instruction expectations. Original helpers load privately.
#include "machine/apu_gp_processor.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"GP frame FAIL %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
struct Fixture {
    std::vector<uint8_t> ram=std::vector<uint8_t>(0x90000,0xa5);
    std::string apu_error,owner_error;
    unsigned source_reads=0;uint32_t fail_write=UINT32_MAX;
    bool exit_guard=false;const char* expected_guard=nullptr;
    std::vector<uint32_t> guarded_p;
    xlle::McpxApu apu{[this](const std::string& s){apu_error=s;}};
    Fixture() {
        apu.set_mem_access([this](uint32_t a,void* out,size_t n){
            if(uint64_t(a)+n>ram.size())return false;
            if(a>=0x10000 && a<0x50000)++source_reads;
            std::memcpy(out,ram.data()+a,n);return true;
        },[this](uint32_t a,const void* in,size_t n){
            if(a==fail_write || uint64_t(a)+n>ram.size())return false;
            std::memcpy(ram.data()+a,in,n);return true;
        });
        reg(0x202c,0x1000);reg(0x2030,0xa000);
        for(unsigned i=0;i<64;++i)put(0xa000+i*8,0x10000+i*4096);
        reg(0x2040,0xc000);reg(0x20d4,3);
        for(unsigned i=0;i<4;++i)put(0xc000+i*8,0x60000+i*0x4000);
        reg(0x2054,0xffff);reg(0x2060,0xffff);reg(0x206c,0xffff);
        reg(0x2000,8);reg(0x1100,0);reg(0x1504,1u<<15);
    }
    void fault(const std::string& s) {
        owner_error=s;
        if(exit_guard) {
            CHECK(expected_guard && apu_error.find(expected_guard)!=std::string::npos);
            CHECK(!apu.gp_dma_nodes() && std::equal(guarded_p.begin(),guarded_p.end(),apu.gp_word(0x2800)));
            std::fprintf(stderr,"expected frame guard: %s; APU: %s\n",s.c_str(),apu_error.c_str());std::exit(2);
        }
    }
    void put(unsigned a,uint32_t v){std::memcpy(ram.data()+a,&v,4);}
    uint32_t get(unsigned a)const{uint32_t v;std::memcpy(&v,ram.data()+a,4);return v;}
    void reg(unsigned a,uint32_t v){apu.mmio_write(0xfe800000+a,4,v);}
    void method(unsigned a,uint32_t v){reg(0x20000+a,v);}
    void scratch(unsigned a,uint32_t v){for(unsigned b=0;b<4;++b)ram[get(0xc000+((a+b)>>12)*8)+((a+b)&4095)]=uint8_t(v>>(b*8));}
    uint32_t scratch(unsigned a)const{uint32_t v=0;for(unsigned b=0;b<4;++b)v|=uint32_t(ram[get(0xc000+((a+b)>>12)*8)+((a+b)&4095)])<<(b*8);return v;}
    void boot(const std::vector<uint32_t>& p){CHECK(p.size()<=2048);for(unsigned i=0;i<2048;++i)scratch(i*4,i<p.size()?p[i]:0x87);}
    void voice(unsigned id,bool stereo,unsigned bin,int16_t left,int16_t right=0,unsigned frames=256,bool loop=true) {
        const unsigned object=0x1000+id*128,base=(id-64)*1024;
        std::fill(ram.begin()+object,ram.begin()+object+128,0);
        for(unsigned n=0;n<frames;++n){std::memcpy(ram.data()+0x10000+base+n*(stereo?4:2),&left,2);if(stereo)std::memcpy(ram.data()+0x10000+base+n*4+2,&right,2);}
        method(0x2f8,id);method(0x304,0x5000e000|(loop?1u<<25:0)|(stereo?(1u<<27)|(1u<<16):0)|6|(7<<5));
        method(0x300,bin|(3<<5)|(2<<10)|(4<<16)|(5<<21)|(6<<26));
        method(0x360,stereo?0x000f000f:0xffff000f);method(0x364,0xffffffff);method(0x368,0xffffffff);
        method(0x3a0,base);method(0x3dc,frames-1);method(0x30c,0xff000000);
        method(0x120,0x1ffff);method(0x124,id);CHECK(apu_error.empty());
    }
    uint32_t events(){uint32_t v=0;CHECK(apu.gp_read(0xffffc5,v));return v;}
    void output_descriptor(){const uint32_t d[]={0x4000,0x59d2,32,0x200,0,0x2ffd,127};for(unsigned i=0;i<7;++i)*apu.gp_word(0x20+i)=d[i];}
};
static std::vector<uint32_t> frame_program() {
    std::vector<uint32_t> p={0x08f485,0xfff,0x08f484,1}; // initial clear/yield
    const unsigned wait=unsigned(p.size());
    const uint32_t poll[]={0x0a8581,wait,0x08f485,2};p.insert(p.end(),std::begin(poll),std::end(poll));
    // Actual CPU copies bin0 to X200; source and DMA results checked exactly.
    for(unsigned n=0;n<32;++n){const uint32_t move[]={0x65f400,0x1400+n-4,0x021594,0x65f400,0x200+n-4,0x021584};p.insert(p.end(),std::begin(move),std::end(move));}
    const uint32_t dma[]={0x08f494,0x20,0x08f496,1,0x08f484,1,0x0af080,wait};
    p.insert(p.end(),std::begin(dma),std::end(dma));return p;
}
static void start(xlle::ApuGpProcessor& gp,xlle::ApuVoiceQuantum& q){CHECK(gp.start_frame(q));CHECK(q.published && q.sequence);}
static void advance_to_ack(Fixture& f,xlle::ApuGpProcessor& gp,xlle::ApuGpRun& r) {
    for(unsigned attempt=0;attempt<4 && (f.events()&2);++attempt)
        CHECK(gp.run(8,r) && !r.yielded && !r.power && gp.phase()==xlle::ApuGpPhase::Running);
    CHECK(!(f.events()&2));
}
static void mixed_frames() {
    for(unsigned life=0;life<3;++life) {
        Fixture f;f.voice(71,false,0,4096);f.voice(96,true,0,8192,-16384);f.output_descriptor();
        f.boot(frame_program());xlle::ApuGpProcessor gp(f.apu,[&](const std::string& s){f.fault(s);});CHECK(gp.bootstrap_from_scratch());
        xlle::ApuGpRun r;CHECK(gp.run(1000,r) && r.yielded && r.pc==4 && !r.power && !r.sp);
        CHECK(r.cycles==6 && r.unused_cycles==994 && f.events()==0 && !f.apu.gp_frame_outstanding());
        for(unsigned frame=0;frame<10;++frame) {
            xlle::ApuVoiceQuantum q;start(gp,q);CHECK(q.sequence==frame+1 && f.events()==(frame?0x82:2));
            CHECK(f.apu.gp_frame_outstanding());
            const auto bank=std::vector<uint32_t>(f.apu.gp_word(0x1400),f.apu.gp_word(0x1400)+1024);
            const auto ram=f.ram;const auto reads=f.source_reads;
            // A small budget advances only the real poll/ack; no fake finish.
            advance_to_ack(f,gp,r);
            CHECK(f.events()==(frame?0x80:0) && f.apu.gp_frame_outstanding());
            CHECK(f.ram==ram && f.source_reads==reads && std::equal(bank.begin(),bank.end(),f.apu.gp_word(0x1400)));
            CHECK(gp.run(2000,r) && r.yielded && !r.power && !r.sp && r.unused_cycles>1000);
            CHECK(!f.apu.gp_frame_outstanding() && f.events()==0x80 && f.apu.gp_dma_nodes()==frame+1);
            for(unsigned n=0;n<32;++n){CHECK(*f.apu.gp_word(0x200+n)==bank[n]);CHECK(f.scratch(0x2ffd+n*4)==bank[n]);}
            if(frame>=7)for(unsigned n=0;n<32;++n){const int32_t raw=int32_t(bank[n])-((bank[n]&0x800000)?0x1000000:0);CHECK(std::fabs(raw/8388608.0-0.375)<0.000002);}
            const auto before=r.cycles;
            if(frame==9)CHECK(before<10000); // positive budgets did not accumulate
        }
        // Acknowledging DMA EOL cannot introduce another frame.
        CHECK(f.apu.gp_write(0xffffc5,0x80) && !f.events());
        CHECK(f.apu_error.empty() && f.owner_error.empty());
    }
}
static void overlap_guards() {
    for(unsigned mode=0;mode<3;++mode) {
        Fixture f;f.voice(71,false,0,4096);f.boot(frame_program());
        xlle::ApuGpProcessor gp(f.apu,[&](const std::string& s){f.fault(s);});CHECK(gp.bootstrap_from_scratch());
        xlle::ApuGpRun r;CHECK(gp.run(1000,r) && r.yielded);xlle::ApuVoiceQuantum q;start(gp,q);
        advance_to_ack(f,gp,r);CHECK(!f.events()); // ack did not release lease
        const auto ram=f.ram;const auto reads=f.source_reads;const auto seq=f.apu.voice_mix_quanta();
        const auto bank=std::vector<uint32_t>(f.apu.gp_word(0x1400),f.apu.gp_word(0x1400)+1024);
        if(mode==0){xlle::ApuVoiceQuantum blocked;CHECK(!f.apu.process_2d_quantum(blocked) && !blocked.published && !blocked.sequence);}
        if(mode==1)f.reg(0x35000,0x123456);
        if(mode==2){const uint32_t d[]={0x4000,0x59c0,1,0x1400,0,0x2ffd,3};for(unsigned i=0;i<7;++i)*f.apu.gp_word(0x20+i)=d[i];CHECK(f.apu.gp_write(0xffffd4,0x20));CHECK(f.apu.gp_write(0xffffd6,1) && !f.apu.service_dsp_dma(xlle::ApuDspKind::Gp,4096));}
        CHECK(f.apu_error.find("MIX publication overlaps")!=std::string::npos);
        CHECK(f.ram==ram && f.source_reads==reads && f.apu.voice_mix_quanta()==seq && !f.apu.gp_dma_nodes());
        CHECK(std::equal(bank.begin(),bank.end(),f.apu.gp_word(0x1400)));
        CHECK(f.apu.gp_frame_outstanding());CHECK(!gp.run(1000,r) && !r.returned && !r.sequence);
    }
}
static void admission_and_lifetimes() {
    // Disabled and real idle-trap paths cannot post an event or resume the CPU.
    for(unsigned mode=0;mode<3;++mode) {
        Fixture f;f.boot(frame_program());if(mode)f.voice(71,false,0,4096);
        xlle::ApuGpProcessor gp(f.apu,[&](const std::string& s){f.fault(s);});CHECK(gp.bootstrap_from_scratch());
        xlle::ApuGpRun r;CHECK(gp.run(1000,r) && r.yielded);
        if(mode==0)f.reg(0x2000,0);
        if(mode==1)f.put(0x1000+71*128+0x54,0);
        if(mode==2)f.reg(0x1100,0x80);
        const auto ram=f.ram;const auto reads=f.source_reads;xlle::ApuVoiceQuantum q;
        const bool started=gp.start_frame(q);
        CHECK(started && !q.published && !q.sequence && gp.phase()==xlle::ApuGpPhase::Yielded && !f.events());
        CHECK(!f.apu.gp_frame_outstanding() && f.ram==ram && f.source_reads==reads);
    }
    Fixture late;late.voice(71,false,0,16384,0,1,false);late.boot(frame_program());
    xlle::ApuGpProcessor failed(late.apu,[&](const std::string& s){late.fault(s);});CHECK(failed.bootstrap_from_scratch());
    xlle::ApuGpRun r;CHECK(failed.run(1000,r) && r.yielded);
    late.reg(0x115c,0x80000);late.fail_write=0x80000+16*(2+71*4)+15;xlle::ApuVoiceQuantum q;
    CHECK(!failed.start_frame(q) && q.published && !q.sequence && failed.phase()==xlle::ApuGpPhase::Failed);
    CHECK(!late.apu.gp_frame_outstanding());
    // Protect the publication transaction itself: a real retirement IRQ cannot
    // overwrite MIX between publication and posting the new frame event.
    Fixture irq;irq.voice(71,false,0,16384,0,1,false);irq.boot(frame_program());irq.reg(0x1004,0x61);
    xlle::ApuGpProcessor irq_owner(irq.apu,[&](const std::string& s){irq.fault(s);});CHECK(irq_owner.bootstrap_from_scratch());CHECK(irq_owner.run(1000,r) && r.yielded);
    bool called=false;irq.apu.set_irq([&](int line,bool high){if(high){CHECK(line==5);called=true;const auto bank=*irq.apu.gp_word(0x1400);irq.reg(0x35000,0x123456);CHECK(*irq.apu.gp_word(0x1400)==bank);}});
    CHECK(!irq_owner.start_frame(q) && called && q.published && !q.sequence && !irq.apu.gp_frame_outstanding());
    CHECK(irq.apu_error.find("MIX publication overlaps")!=std::string::npos);
    // A processor failure in that callback cannot be overwritten by the outer
    // admission returning. Preserve the actual VP prefix, post no frame event.
    Fixture reentrant;reentrant.voice(71,false,0,16384,0,1,false);reentrant.boot(frame_program());reentrant.reg(0x1004,0x61);
    xlle::ApuGpProcessor entered(reentrant.apu,[&](const std::string& s){reentrant.fault(s);});CHECK(entered.bootstrap_from_scratch());CHECK(entered.run(1000,r) && r.yielded);
    bool reset=false;reentrant.apu.set_irq([&](int line,bool high){if(high){CHECK(line==5);reset=true;CHECK(!entered.reset_cpu());}});
    CHECK(!entered.start_frame(q) && reset && q.published && q.sequence==1 && entered.phase()==xlle::ApuGpPhase::Failed);
    CHECK(!reentrant.events() && reentrant.apu.gp_frame_outstanding());
    // Destruction with an outstanding frame faults rather than forging yield.
    Fixture abandoned;abandoned.boot(frame_program());
    {xlle::ApuGpProcessor gp(abandoned.apu,[&](const std::string& s){abandoned.fault(s);});CHECK(gp.bootstrap_from_scratch());CHECK(gp.run(1000,r) && r.yielded);start(gp,q);}
    CHECK(abandoned.apu.faulted() && abandoned.apu_error.find("removed with outstanding")!=std::string::npos);
    // Yielded runs and overlapping starts fail before any new VP progress.
    for(unsigned mode=0;mode<3;++mode){Fixture f;f.boot(frame_program());xlle::ApuGpProcessor gp(f.apu,[&](const std::string& s){f.fault(s);});CHECK(gp.bootstrap_from_scratch());CHECK(gp.run(1000,r) && r.yielded);
        if(mode)start(gp,q);const auto ram=f.ram;const auto seq=f.apu.voice_mix_quanta();
        if(mode==0)CHECK(!gp.run(1000,r) && !r.returned);
        if(mode==1)CHECK(!gp.start_frame(q) && !q.published);
        if(mode==2)CHECK(!gp.reset_cpu());
        CHECK(f.ram==ram && f.apu.voice_mix_quanta()==seq && gp.phase()==xlle::ApuGpPhase::Failed);
    }
    // The original context registers store actual configuration, no work.
    Fixture context;uint32_t v=1;CHECK(context.apu.gp_write(0xffffd7,0));CHECK(context.apu.gp_write(0xffffd5,0x25));
    CHECK(context.apu.gp_read(0xffffd5,v) && v==0x25);CHECK(context.apu.gp_read(0xffffd7,v) && !v && !context.apu.gp_dma_nodes());
    CHECK(!context.apu.gp_write(0xffffd7,1));
    Fixture masks;CHECK(!masks.apu.gp_write(0xffffc5,0xfff)); // startup only with executing owner
    // Hardware STOP remains distinct from a vendor frame yield.
    Fixture stopped;stopped.boot({0x87});xlle::ApuGpProcessor stop(stopped.apu,[&](const std::string& s){stopped.fault(s);});CHECK(stop.bootstrap_from_scratch());CHECK(stop.run(100,r) && r.power==2 && !r.yielded);
    CHECK(!stop.start_frame(q) && !q.published && !stopped.apu.voice_mix_quanta());
}
static void debt_and_selected_eol() {
    Fixture f;f.boot({0x08f484,1,0x08f485,2,0x08f484,1});
    xlle::ApuGpProcessor gp(f.apu,[&](const std::string& s){f.fault(s);});CHECK(gp.bootstrap_from_scratch());xlle::ApuGpRun r;
    CHECK(gp.run(1,r) && r.yielded && r.cycles==3 && !r.unused_cycles && r.pc==2);
    xlle::ApuVoiceQuantum q;start(gp,q);
    for(unsigned i=0;i<2;++i){CHECK(gp.run(1,r) && !r.yielded && r.pc==2 && r.cycles==3 && f.events()==2);}
    CHECK(gp.run(1,r) && !r.yielded && r.pc==4 && r.cycles==6 && !f.events() && f.apu.gp_frame_outstanding());
    for(unsigned i=0;i<2;++i){CHECK(gp.run(1,r) && !r.yielded && r.pc==4 && r.cycles==6 && f.apu.gp_frame_outstanding());}
    CHECK(gp.run(1,r) && r.yielded && r.pc==6 && r.cycles==9 && !r.unused_cycles && !f.events() && !f.apu.gp_frame_outstanding());
    Fixture selected;selected.boot(frame_program());selected.output_descriptor();xlle::ApuGpProcessor owner(selected.apu,[&](const std::string& s){selected.fault(s);});CHECK(owner.bootstrap_from_scratch());CHECK(owner.run(100,r) && r.yielded);
    CHECK(selected.apu.gp_write(0xffffd4,0x20) && selected.apu.gp_write(0xffffd6,1) && selected.apu.service_dsp_dma(xlle::ApuDspKind::Gp,4096) && selected.events()==0x80);
    start(owner,q);CHECK(selected.events()==0x82);CHECK(selected.apu.gp_write(0xffffc5,0x80) && selected.events()==2 && selected.apu.gp_frame_outstanding());
    CHECK(owner.run(1000,r) && r.yielded && selected.events()==0x80 && !selected.apu.gp_frame_outstanding());
}
static std::vector<uint32_t> original(const char* path){FILE* file=std::fopen(path,"rb");CHECK(file);std::vector<uint32_t> p(371);CHECK(std::fread(p.data(),4,p.size(),file)==p.size() && std::fgetc(file)==EOF);std::fclose(file);for(auto& w:p)w&=0xffffff;return p;}
static void original_helpers(const char* path) {
    auto p=original(path);p.resize(0x800,0x87);p[0]=0x0af080;p[1]=0x700;
    // Authored caller at700 invokes unchanged original155 and D2 helpers.
    const uint32_t caller[]={0x0bf080,0x155,0x56f400,0x25,0x0bf080,0xd2,0x08f484,1,
        0x0bf080,0x160,0x44f400,0x123456,0x08f484,1,0x0af080,0x708};
    std::copy(std::begin(caller),std::end(caller),p.begin()+0x700);
    Fixture f;f.boot(p);std::fill(f.apu.gp_word(0),f.apu.gp_word(0)+4096,0x555555);std::fill(f.apu.gp_word(0x1800),f.apu.gp_word(0x1800)+2048,0xaaaaaa);
    xlle::ApuGpProcessor gp(f.apu,[&](const std::string& s){f.fault(s);});CHECK(gp.bootstrap_from_scratch());
    xlle::ApuGpRun r;CHECK(gp.run(20000,r) && r.yielded && r.pc==0x708 && !r.sp && !f.events());
    CHECK(std::all_of(f.apu.gp_word(0),f.apu.gp_word(0)+4096,[](uint32_t v){return !v;}) && std::all_of(f.apu.gp_word(0x1800),f.apu.gp_word(0x1800)+2048,[](uint32_t v){return !v;}));
    uint32_t v;CHECK(f.apu.gp_read(0xffffd5,v) && v==0x25);CHECK(f.apu.gp_read(0xffffd4,v) && v==0x25);
    // No-event spin is an actual CPU loop, entered without forced PC. A host
    // event acknowledgement after real publication removes the event only.
    xlle::ApuVoiceQuantum q;start(gp,q);CHECK(f.apu.gp_write(0xffffc5,2));CHECK(gp.run(100,r) && !r.yielded && r.pc==0x160 && r.sp==1 && f.apu.gp_frame_outstanding());
    CHECK(f.apu_error.empty() && f.owner_error.empty());
    // Separate successful lifetime leaves simultaneous real DMA EOL intact.
    Fixture yes;yes.boot(p);xlle::ApuGpProcessor owner(yes.apu,[&](const std::string& s){yes.fault(s);});CHECK(owner.bootstrap_from_scratch());CHECK(owner.run(20000,r) && r.yielded);yes.output_descriptor();
    CHECK(yes.apu.gp_write(0xffffd4,0x20) && yes.apu.gp_write(0xffffd6,4) && yes.apu.gp_write(0xffffd6,1) && yes.apu.service_dsp_dma(xlle::ApuDspKind::Gp,4096));CHECK(yes.events()==0x80);
    start(owner,q);CHECK(yes.events()==0x82);CHECK(owner.run(100,r) && r.yielded && r.pc==0x70e && !r.sp);
    CHECK(yes.events()==0x80 && !yes.apu.gp_frame_outstanding());
    // The sentinel after the yield has not run: PC remains at the actual JMP.
    CHECK(yes.apu_error.empty() && yes.owner_error.empty());
    std::puts("original GP helpers155/D2/160: actual clear/context/no-event spin/selective frame ack/yield passed; isolated caller, not complete firmware boot");
}
static void guarded_cpu(const std::string& mode,const char* path) {
    Fixture f;std::vector<uint32_t> p;
    if(mode=="--original-boot"){p=original(path);}
    else if(mode=="yield-mask"){p={0x08f484,2};f.expected_guard="unused";}
    else {p={0x08f484,1,0x08f485,0xfff};f.expected_guard="unqualified GP event acknowledgement mask";}
    f.boot(p);xlle::ApuGpProcessor gp(f.apu,[&](const std::string& s){
        if(mode=="yield-mask"){CHECK(s.find("vendor yield control")!=std::string::npos);std::fprintf(stderr,"expected frame guard: %s\n",s.c_str());std::exit(2);}f.fault(s);
    });CHECK(gp.bootstrap_from_scratch());f.guarded_p.assign(f.apu.gp_word(0x2800),f.apu.gp_word(0x2800)+4096);f.exit_guard=true;xlle::ApuGpRun r;
    if(mode=="--original-boot") {
        const auto ram=f.ram;f.exit_guard=false;
        CHECK(gp.run(20000,r) && r.yielded && r.pc==0x2b && r.power==0 && !r.sp);
        CHECK(!f.apu.gp_dma_nodes() && !f.apu.gp_frame_outstanding() && f.ram==ram);
        CHECK(std::equal(p.begin(),p.end(),f.apu.gp_word(0x2800)));
        CHECK(f.owner_error.empty() && f.apu_error.empty());
        std::puts("{\"boundary\":\"first-C4-yield\",\"pc\":43,\"power\":0,\"yielded\":true,\"dma_nodes\":0,\"frame_outstanding\":false,\"ram_unchanged\":true,\"original_prefix_loaded\":true}");
        return;
    }
    if(mode=="startup-late"){CHECK(gp.run(100,r) && r.yielded);xlle::ApuVoiceQuantum q;start(gp,q);}
    gp.run(20000,r);CHECK(false);
}
int main(int argc,char** argv) {
    if(argc==3 && std::string(argv[1])=="--original-helpers"){original_helpers(argv[2]);return 0;}
    if(argc==3 && std::string(argv[1])=="--original-boot"){guarded_cpu(argv[1],argv[2]);return 0;}
    if(argc==2){guarded_cpu(argv[1],nullptr);return 1;}
    CHECK(argc==1);mixed_frames();overlap_guards();admission_and_lifetimes();debt_and_selected_eol();
    std::puts("GP frame:30 real VP/GP/DMA frames, selected events, yield/budget/lease guards and lifetimes passed");
}
