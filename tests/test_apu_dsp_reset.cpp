// Authored instructions check MMIO -> sole CPU -> real SG bootstrap ownership.
// Original reset sequences and retained physical loaded-P evidence are recorded
// separately by native_audio_reset_reference.py. No wall-clock timing claim.
#include "machine/apu_gp_processor.hpp"
#include "machine/apu_ep_processor.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"DSP reset FAIL %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
struct Fixture {
    std::vector<uint8_t> ram=std::vector<uint8_t>(0x10000,0xa5);
    std::string error;
    unsigned reads=0,writes=0;
    uint32_t fail_read=UINT32_MAX;
    bool expect_warm_counter_guard=false;
    xlle::McpxApu apu{[this](const std::string& why){error=why;}};
    Fixture() {
        CHECK(apu.initialize_kernel_audio_handoff());
        apu.set_mem_access([this](uint32_t a,void* p,size_t n){
            ++reads;if(a==fail_read || uint64_t(a)+n>ram.size())return false;
            std::memcpy(p,ram.data()+a,n);return true;
        },[this](uint32_t,const void*,size_t){++writes;return false;});
        for(bool ep:{false,true}) {
            const uint32_t table=ep?0x2000:0x1000;
            reg(ep?0x2048:0x2040,table);reg(ep?0x20dc:0x20d4,1);
            put(table,ep?0xa000:0x8000);put(table+8,ep?0x5000:0x3000);
        }
    }
    void put(uint32_t a,uint32_t v){std::memcpy(ram.data()+a,&v,4);}
    void reg(uint32_t a,uint32_t v){apu.mmio_write(0xfe800000+a,4,v);}
    uint32_t get(uint32_t a){return apu.mmio_read(0xfe800000+a,4);}
    uint32_t* bank(bool ep){return ep?apu.ep_word(0x2800):apu.gp_word(0x2800);}
    void program(bool ep,const std::vector<uint32_t>& p) {
        for(unsigned i=0;i<2048;++i)put((i<1024?(ep?0xa000:0x8000):(ep?0x5000:0x3000))+(i%1024)*4,
                                      0xa7000000u|(i<p.size()?p[i]:0x87));
    }
    void fault(const std::string& why){
        error=why;
        if(expect_warm_counter_guard) {
            CHECK(why.find("warm counter read before reconfiguration")!=std::string::npos);
            CHECK(!writes && !apu.gp_dma_nodes() && !apu.ep_dma_nodes());
            std::fprintf(stderr,"expected warm EP counter boundary\n");std::exit(2);
        }
    }
};
static void lifetimes() {
    for(unsigned life=0;life<3;++life) {
        Fixture f;xlle::ApuGpProcessor gp(f.apu,[&](const std::string& s){f.fault(s);});
        xlle::ApuEpProcessor ep(f.apu,[&](const std::string& s){f.fault(s);});
        CHECK(f.get(0x3fffc)==0 && f.get(0x5fffc)==1);
        for(uint32_t v:{0u,1u,0u,1u}){f.reg(0x3fffc,v);CHECK(f.get(0x3fffc)==v);}
        f.reg(0x5fffc,1);CHECK(!f.reads && !f.writes && f.get(0x1510)==1);
        CHECK(gp.phase()==xlle::ApuGpPhase::Held && ep.phase()==xlle::ApuGpPhase::Held);
        for(bool encode:{false,true}) {
            auto& cpu=encode?static_cast<xlle::ApuDspProcessor&>(ep):static_cast<xlle::ApuDspProcessor&>(gp);
            const uint32_t reset=encode?0x5fffc:0x3fffc,token=(encode?0x654320:0x123450)+life;
            const std::vector<uint32_t> code={0x44f400,token,0x447000,0x100,0x87};
            f.program(encode,code);std::fill(f.bank(encode),f.bank(encode)+4096,0x456789);
            const auto before=f.ram;const unsigned reads=f.reads;
            f.reg(reset,3);CHECK(f.error.empty() && f.get(reset)==3 && cpu.bootstraps()==1);
            CHECK(cpu.phase()==xlle::ApuGpPhase::Ready && f.reads==reads+4 && !f.writes);
            for(unsigned i=0;i<4096;++i)CHECK(f.bank(encode)[i]==(i<code.size()?code[i]:i<2048?0x87:0x456789));
            CHECK(*(encode?f.apu.ep_word(0x100):f.apu.gp_word(0x100))==0);
            // Reads/repeated release never execute or refetch. Changed source
            // only becomes executable after a new actual hold/release.
            f.reg(reset,3);for(unsigned i=0;i<5;++i)CHECK(f.get(reset)==3);
            CHECK(cpu.bootstraps()==1 && f.reads==reads+4);
            xlle::ApuGpRun result;CHECK(cpu.run(100,result) && result.power==2);
            CHECK(*(encode?f.apu.ep_word(0x100):f.apu.gp_word(0x100))==token && f.ram==before);
            f.reg(reset,encode?1:0);CHECK(f.error.empty() && cpu.phase()==xlle::ApuGpPhase::Held);
            CHECK(f.bank(encode)[1]==token && f.bank(encode)[2048]==0x456789);
            f.program(encode,{0x44f400,token+1,0x447000,0x100,0x87});
            f.reg(reset,3);CHECK(cpu.bootstraps()==2 && cpu.run(100,result) && result.power==2);
            CHECK(*(encode?f.apu.ep_word(0x100):f.apu.gp_word(0x100))==token+1);
        }
        CHECK(f.error.empty() && !f.writes && !f.apu.gp_dma_nodes());
    }
}
static void guards() {
    for(bool encode:{false,true}) {
        const uint32_t reset=encode?0x5fffc:0x3fffc;
        {Fixture f;f.reg(reset,3);CHECK(f.apu.faulted() && !f.reads);}
        for(unsigned variant=0;variant<5;++variant) {
            Fixture f;xlle::ApuGpProcessor gp(f.apu,[&](const std::string& s){f.fault(s);});
            xlle::ApuEpProcessor ep(f.apu,[&](const std::string& s){f.fault(s);});
            if(variant==0)f.reg(reset,7); // NMI is not a qualified reset request.
            if(variant==1)f.reg(reset,2);
            if(variant==2)f.apu.mmio_write(0xfe800000+reset-1,4,0);
            if(variant==3)f.apu.mmio_write(0xfe800000+reset,2,1);
            if(variant==4)f.apu.mmio_read(0xfe800000+reset+1,1);
            CHECK(f.apu.faulted() && !f.reads && !f.writes && !gp.bootstraps() && !ep.bootstraps());
        }
        {Fixture f;xlle::ApuGpProcessor gp(f.apu,[&](const std::string& s){f.fault(s);});
         xlle::ApuEpProcessor ep(f.apu,[&](const std::string& s){f.fault(s);});
         f.program(encode,{0x87});std::fill(f.bank(encode),f.bank(encode)+4096,0x543210);
         f.fail_read=encode?0x5000:0x3000;f.reg(reset,3);CHECK(f.apu.faulted());
         CHECK(!gp.bootstraps() && !ep.bootstraps() && !f.writes);
         for(unsigned i=0;i<4096;++i)CHECK(f.bank(encode)[i]==0x543210);}
        {Fixture f;xlle::ApuGpProcessor gp(f.apu,[&](const std::string& s){f.fault(s);});
         xlle::ApuEpProcessor ep(f.apu,[&](const std::string& s){f.fault(s);});
         auto& cpu=encode?static_cast<xlle::ApuDspProcessor&>(ep):static_cast<xlle::ApuDspProcessor&>(gp);
         f.program(encode,{0x07f432,0xffffff,0x87});f.reg(reset,3);xlle::ApuGpRun result;
         CHECK(cpu.run(100,result) && result.power==2);const unsigned before=f.reads;
         f.reg(reset,encode?1:0);CHECK(f.apu.faulted() && cpu.phase()==xlle::ApuGpPhase::Failed && f.reads==before);}
    }
    std::string error;xlle::McpxApu cold([&](const std::string& s){error=s;});
    cold.mmio_read(0xfe83fffc,4);CHECK(cold.faulted());
}
static void configured_reexecution(bool warm_read_guard=false) {
    for(bool encode:{false,true}) {
        Fixture f;xlle::ApuGpProcessor gp(f.apu,[&](const std::string& s){f.fault(s);});
        xlle::ApuEpProcessor ep(f.apu,[&](const std::string& s){f.fault(s);});
        auto& cpu=encode?static_cast<xlle::ApuDspProcessor&>(ep):static_cast<xlle::ApuDspProcessor&>(gp);
        const unsigned reset=encode?0x5fffc:0x3fffc;
        for(unsigned generation=0;generation<2;++generation) {
            // Same B2/B0/B1 and post-B1 observation used on hardware. EP's
            // unknown preconfiguration counter is deliberately not consumed.
            std::vector<unsigned> p={0x07f432,0xffffff,0x07f430,1,0x07f431,1,
                0x44f000,0xffffb3,0x447000,0x101};
            p.insert(p.end(),16,0);p.insert(p.end(),{0x44f000,0xffffb3,0x447000,0x102});
            if(encode)p.insert(p.end(),{0x08f48a,0,0x08f484,1});
            const unsigned marker=0x543210+generation;
            p.insert(p.end(),{0x44f400,marker,0x447000,0x100,0x0d10c0,0});
            f.program(encode,p);const auto source=f.ram;const unsigned reads=f.reads;
            f.reg(reset,3);CHECK(f.error.empty() && cpu.bootstraps()==generation+1);
            CHECK(f.reads==reads+4 && !f.writes);
            if(warm_read_guard && encode && generation==1) {
                // Same owner, real warm hold/reload, then a preconfiguration
                // B3 read. It must not fabricate the GP's known zero value.
                const unsigned read[]={0x44f000,0xffffb3,0x87};
                for(unsigned i=0;i<3;++i)f.reg(0x5a000+i*4,read[i]);
                f.expect_warm_counter_guard=true;xlle::ApuGpRun rejected;
                cpu.run(32,rejected);CHECK(false);
            }
            xlle::ApuGpRun run;CHECK(cpu.run(512,run) && !run.power && !run.yielded);
            auto* words=encode?f.apu.ep_word(0):f.apu.gp_word(0);
            CHECK(words[0x100]==marker && words[0x101]==0 && words[0x102]==20);
            CHECK(f.get(encode?0x5ff10:0x3ff10)==unsigned(encode));
            for(unsigned i=0;i<2048;++i)CHECK(f.bank(encode)[i]==(i<p.size()?p[i]:0x87));
            const unsigned before_hold=f.reads;
            f.reg(reset,encode?1:0);
            CHECK(f.error.empty() && cpu.phase()==xlle::ApuGpPhase::Held);
            CHECK(f.get(reset)==unsigned(encode) && f.reads==before_hold && f.ram==source);
            CHECK(f.get(encode?0x5ff10:0x3ff10)==unsigned(encode));
            CHECK(!f.apu.dsp_work_pending(encode?xlle::ApuDspKind::Ep:xlle::ApuDspKind::Gp));
        }
        CHECK(!f.writes && !f.apu.gp_dma_nodes() && !f.apu.ep_dma_nodes());
    }
}
static void configured_pending_frame() {
    // Stop SE after a real EP event, both before and after its acknowledgement.
    // Neither state permits replacing unfinished frame computation by reset.
    for(bool acknowledge:{false,true}) {
        Fixture f;xlle::ApuGpProcessor gp(f.apu,[&](const std::string& s){f.fault(s);});
        xlle::ApuEpProcessor ep(f.apu,[&](const std::string& s){f.fault(s);});
        std::vector<unsigned> p={0x07f432,0xffffff,0x07f430,1,0x07f431,1,0x08f485,0xfff,
            0x08f484,1,0x0a8581,10};
        if(acknowledge)p.insert(p.end(),{0x08f485,2});
        p.insert(p.end(),{0x0d10c0,0});f.program(true,p);
        f.reg(0x5fffc,3);xlle::ApuGpRun run;CHECK(ep.run(512,run) && f.error.empty());
        f.reg(0x1100,0x100f);f.reg(0x2000,15);f.apu.set_frame(1);
        CHECK(f.apu.ep_frame_events()==1 && f.get(0x5ff10)==0);
        if(acknowledge)CHECK(ep.run(512,run));
        uint32_t events=0;CHECK(f.apu.ep_read(0xffffc5,events));
        CHECK(events==(acknowledge?0x100u:0x102u));
        f.reg(0x2000,7);const auto before=f.ram;const auto boots=ep.bootstraps();
        f.reg(0x5fffc,1);
        CHECK(f.apu.faulted() && ep.phase()==xlle::ApuGpPhase::Failed &&
              ep.bootstraps()==boots && f.ram==before && !f.writes);
        CHECK(f.apu.RegFileMmio::mmio_read(0xfe85fffc,4)==3); // inspect retained register after terminal fault
    }
}
static void retirement_computation() {
    for(bool halted_fe:{false,true}) {
        Fixture f;xlle::ApuGpProcessor gp(f.apu,[&](const std::string& s){f.fault(s);});
        xlle::ApuEpProcessor ep(f.apu,[&](const std::string& s){f.fault(s);});
        // A real event is acknowledged, then this program deliberately keeps
        // computing without publishing another ready request. Reset, not a
        // fabricated ready/completion, must stop those instructions.
        const std::vector<unsigned> p={0x07f432,0xffffff,0x07f430,1,0x07f431,1,
            0x08f485,0xfff,0x08f484,1,0x0a8581,10,0x08f485,2,0x0d10c0,0};
        f.program(true,p);f.reg(0x5fffc,3);xlle::ApuGpRun run;
        CHECK(ep.run(512,run) && f.error.empty());
        f.reg(0x115c,0x4000);f.ram[0x400f]=0x80;f.reg(0x1510,0);
        f.reg(0x1100,0x100f);f.reg(0x2000,15);f.apu.set_frame(1);
        CHECK(ep.run(512,run) && f.apu.ep_frame_events()==1 && f.get(0x5ff10)==0);
        uint32_t status=0;CHECK(f.apu.ep_read(0xffffc5,status) && status==0x100);
        f.reg(0x2010c,0);f.reg(0x20104,2);CHECK(f.error.empty());
        const auto before=f.ram;const auto reads=f.reads;const auto boots=ep.bootstraps();
        CHECK(f.get(0x1510)==0);f.reg(0x1510,1);CHECK(f.get(0x1510)==1);
        if(halted_fe)f.reg(0x1100,0x108f);
        f.reg(0x3fffc,0);f.reg(0x5fffc,1);
        CHECK(f.ram==before && f.reads==reads && !f.writes && ep.bootstraps()==boots);
        if(!halted_fe) {
            CHECK(f.apu.faulted() && ep.phase()==xlle::ApuGpPhase::Failed &&
                  f.apu.retirement_notification_pending());continue;
        }
        CHECK(f.error.empty() && ep.phase()==xlle::ApuGpPhase::Held &&
              !f.apu.retirement_notification_pending() && f.get(0x5ff10)==0);
        CHECK(f.ram[0x400f]==0x80 && f.apu.ep_frame_events()==1);
        // A new bootstrap on the same owner reaches its own actual C4.
        f.reg(0x5fffc,3);CHECK(ep.run(512,run) && f.get(0x5ff10)==1);
        CHECK(ep.bootstraps()==boots+1 && f.error.empty());f.reg(0x5fffc,1);
        CHECK(ep.phase()==xlle::ApuGpPhase::Held && f.error.empty());
    }
}
static void original_reexecution(const char* gp_path,const char* ep_path) {
    Fixture f;xlle::ApuGpProcessor gp(f.apu,[&](const std::string& s){f.fault(s);});
    xlle::ApuEpProcessor ep(f.apu,[&](const std::string& s){f.fault(s);});
    for(bool encode:{false,true}) {
        FILE* file=std::fopen(encode?ep_path:gp_path,"rb");CHECK(file);
        std::vector<unsigned> words(2048);CHECK(std::fread(words.data(),4,2048,file)==2048 && std::fgetc(file)==EOF);
        CHECK(!std::fclose(file));f.program(encode,words);
        auto& cpu=encode?static_cast<xlle::ApuDspProcessor&>(ep):static_cast<xlle::ApuDspProcessor&>(gp);
        for(unsigned generation=0;generation<2;++generation) {
            f.reg(encode?0x5fffc:0x3fffc,3);CHECK(f.error.empty());
            xlle::ApuGpRun run;CHECK(cpu.run(20000,run));
            CHECK(encode?(!run.power && !run.yielded):(run.yielded && run.pc==0x2b));
            CHECK(cpu.bootstraps()==generation+1);
            f.reg(encode?0x5fffc:0x3fffc,encode?1:0);
            CHECK(f.error.empty() && cpu.phase()==xlle::ApuGpPhase::Held);
        }
    }
    CHECK(!f.writes && !f.apu.gp_dma_nodes() && !f.apu.ep_dma_nodes());
    puts("PASS unchanged original GP/EP entry, configured hold and fresh entry on the same CPU owners; no frame/DMA/active retirement claim");
}
static void original_images(const char* gp_path,const char* ep_path) {
    Fixture f;xlle::ApuGpProcessor gp(f.apu,[&](const std::string& s){f.fault(s);});
    xlle::ApuEpProcessor ep(f.apu,[&](const std::string& s){f.fault(s);});
    for(bool encode:{false,true}) {
        FILE* file=std::fopen(encode?ep_path:gp_path,"rb");CHECK(file);
        std::vector<uint32_t> words(2048);CHECK(std::fread(words.data(),4,2048,file)==2048 && std::fgetc(file)==EOF);
        CHECK(!std::fclose(file));
        for(unsigned i=0;i<2048;++i)f.put((i<1024?(encode?0xa000:0x8000):(encode?0x5000:0x3000))+(i%1024)*4,words[i]);
        f.reg(encode?0x5fffc:0x3fffc,1);f.reg(encode?0x5fffc:0x3fffc,3);
        CHECK(f.error.empty());
        for(unsigned i=0;i<2048;++i)CHECK(f.bank(encode)[i]==(words[i]&0xffffff));
    }
    CHECK(f.reads==8 && !f.writes && gp.bootstraps()==1 && ep.bootstraps()==1);
    puts("PASS unchanged original GP/EP bootstrap bytes through reset MMIO and noncontiguous SG pages; 4096 exact program words");
}
int main(int argc,char** argv){
    if(argc==2 && std::string(argv[1])=="--warm-ep-read"){configured_reexecution(true);return 1;}
    if(argc==4 && std::string(argv[1])=="--original-restart"){original_reexecution(argv[2],argv[3]);return 0;}
    if(argc==3){original_images(argv[1],argv[2]);return 0;}
    CHECK(argc==1);lifetimes();configured_reexecution();configured_pending_frame();retirement_computation();guards();
    puts("PASS GP/EP reset MMIO: six CPU lifetimes, twelve real mapped boots, instruction markers, preserved banks, failed-fetch atomicity and history/width/owner guards");
}
