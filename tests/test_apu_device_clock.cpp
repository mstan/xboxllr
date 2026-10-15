// Authored inter-device scheduling checks. Original startup, DSP graph/output,
// physical event phase and stop/reset require their separate source evidence.
#include "machine/apu_device_clock.hpp"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"audio device clock FAIL %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)

struct Fixture {
    std::vector<uint8_t> ram=std::vector<uint8_t>(0x10000),pcm;
    std::vector<std::array<uint64_t,4>> stamps;
    std::string error;
    unsigned reads=0,writes=0;
    bool reenter=false;
    xlle::McpxApu apu{[this](const std::string& s){fault(s);}};
    xlle::McpxAci aci{[this](const std::string& s){fault(s);}};
    std::unique_ptr<xlle::ApuGpProcessor> gp;
    std::unique_ptr<xlle::ApuEpProcessor> ep;
    std::unique_ptr<xlle::ApuDeviceClock> clock;
    explicit Fixture(uint64_t epoch=60037) {
        CHECK(apu.initialize_kernel_audio_handoff() && aci.initialize_kernel_audio_handoff());
        auto read=[this](uint32_t a,void* p,size_t n) {
            if(uint64_t(a)+n>ram.size())return false;
            ++reads;std::memcpy(p,ram.data()+a,n);return true;
        };
        apu.set_mem_access(read,[this](uint32_t a,const void* p,size_t n) {
            if(uint64_t(a)+n>ram.size())return false;
            ++writes;std::memcpy(ram.data()+a,p,n);return true;
        });
        aci.set_mem_access(read);
        aci.set_pcm_output([this](const uint8_t* p,size_t n) {
            if(reenter) {CHECK(!clock->advance(clock->ticks()));return false;}
            stamps.push_back({clock->ticks(),aci.current_frame(),apu.voice_mix_quanta(),apu.ep_frame_events()});
            pcm.insert(pcm.end(),p,p+n);return true;
        });
        gp=std::make_unique<xlle::ApuGpProcessor>(apu,[this](const std::string& s){fault(s);});
        ep=std::make_unique<xlle::ApuEpProcessor>(apu,[this](const std::string& s){fault(s);});
        clock=std::make_unique<xlle::ApuDeviceClock>(apu,*gp,*ep,aci,epoch,1125000,
            [this](const std::string& s){fault(s);});
        CHECK(error.empty() && !clock->failed());
    }
    void fault(const std::string& s){if(error.empty())error=s;}
    void word(unsigned at,uint32_t value){std::memcpy(ram.data()+at,&value,4);}
    void reg(unsigned off,uint32_t value){apu.mmio_write(apu.kBase+off,4,value);}
    void codec(unsigned off,unsigned width,uint32_t value){aci.mmio_write(aci.kBase+off,width,value);}
    static std::vector<uint32_t> event_program() {
        // Actual clear/request, C5 bit1 wait, selective ack, request and loop.
        // GP requests yield; EP requests retain its running polling CPU.
        return {0x08f485,0xfff,0x08f484,1,0x0a8581,4,0x08f485,2,0x08f484,1,0x0af080,4};
    }
    void configure(const std::vector<uint32_t>& gp_program=event_program(),bool release_gp=true) {
        reg(0x2040,0x1000);reg(0x20d4,1);reg(0x2048,0x2000);reg(0x20dc,1);
        word(0x1000,0x4000);word(0x1008,0x5000);word(0x2000,0x6000);word(0x2008,0x7000);
        const auto ep_program=event_program();
        std::memcpy(ram.data()+0x4000,gp_program.data(),gp_program.size()*4);
        std::memcpy(ram.data()+0x6000,ep_program.data(),ep_program.size()*4);
        reg(0x200c,0);reg(0x1100,0x7130f);
        reg(0x2054,0xffff);reg(0x2060,0xffff);reg(0x206c,0xffff);
        if(release_gp){reg(0x3fffc,1);reg(0x3fffc,3);}
        reg(0x5fffc,3);CHECK(error.empty());
        CHECK(clock->advance(clock->ticks()+200));
        CHECK(ep->phase()==xlle::ApuGpPhase::Running);
    }
    void output() {
        for(unsigned i=0;i<3;++i) {
            word(0x3000+i*8,0x8000+i*1024);word(0x3004+i*8,0x80000200);
            for(unsigned b=0;b<1024;++b)ram[0x8000+i*1024+b]=uint8_t(i*53+b*17);
        }
        codec(0x110,4,0x3000);codec(0x115,1,2);codec(0x11b,1,0x1d);
        CHECK(error.empty());
    }
};

static void stopped_epoch() {
    Fixture f;
    for(unsigned offset=0x2054;offset<=0x2074;offset+=4)
        CHECK(f.apu.mmio_read(f.apu.kBase+offset,4)==0xffff);
    const auto before=f.apu.mmio_read(f.apu.kBase+0x1100,4);
    CHECK(f.clock->advance(66037)); // cross an absolute frame while stopped
    CHECK(f.aci.current_frame()==11 && !f.reads && !f.writes && f.pcm.empty());
    CHECK(!f.clock->retired(false) && !f.clock->retired(true) && !f.apu.ep_frame_events());
    CHECK(f.apu.mmio_read(f.apu.kBase+0x1100,4)==before && f.error.empty());
    CHECK(f.clock->advance(f.clock->ticks()) && !f.reads);
    // Priming is once-only even when the first epoch is zero.
    Fixture apu_zero(0);CHECK(!apu_zero.apu.prime_frame_clock(0) && apu_zero.apu.faulted());
    Fixture aci_zero(0);CHECK(!aci_zero.aci.prime_frame_clock(0) && aci_zero.aci.faulted());
}

static void partitioned_active_output() {
    std::vector<uint8_t> expected_pcm,expected_ram;
    std::vector<std::array<uint64_t,4>> expected_stamps;
    uint64_t expected_gp=0,expected_ep=0;
    for(uint64_t step:{1ull,750ull,17800ull}) {
        Fixture f;f.configure();CHECK(f.gp->phase()==xlle::ApuGpPhase::Yielded);
        f.output();f.reg(0x2000,15);CHECK(f.error.empty());
        const uint64_t end=78037;
        while(f.clock->ticks()<end)CHECK(f.clock->advance(std::min(end,f.clock->ticks()+step)));
        CHECK(f.stamps.size()==3 && f.pcm.size()==3072 && f.aci.completed_descriptors()==3);
        for(unsigned i=0;i<3;++i) {
            const std::array<uint64_t,4> stamp={66000+i*6000,11+i,(i+1)*8,i+1};
            CHECK(f.stamps[i]==stamp);
        }
        CHECK(f.pcm==std::vector<uint8_t>(f.ram.begin()+0x8000,f.ram.begin()+0x8c00));
        CHECK(f.apu.voice_mix_quanta()==24 && f.apu.ep_frame_events()==3);
        CHECK(f.gp->phase()==xlle::ApuGpPhase::Yielded && !f.apu.gp_frame_outstanding());
        CHECK(f.ep->phase()==xlle::ApuGpPhase::Running && f.error.empty());
        if(step==1) {
            expected_pcm=f.pcm;expected_ram=f.ram;expected_stamps=f.stamps;
            expected_gp=f.clock->retired(false);expected_ep=f.clock->retired(true);
        } else {
            CHECK(f.pcm==expected_pcm && f.ram==expected_ram && f.stamps==expected_stamps);
            CHECK(f.clock->retired(false)==expected_gp && f.clock->retired(true)==expected_ep);
        }
        const auto reads=f.reads;
        CHECK(f.clock->advance(end) && f.reads==reads && f.stamps==expected_stamps);
        CHECK(!f.clock->advance(end-1) && f.clock->ticks()==end);
        CHECK(!f.clock->advance(end+6000) && f.reads==reads && f.stamps==expected_stamps);
    }
}

static void admission_and_failure() {
    Fixture ep_only;ep_only.configure(Fixture::event_program(),false);ep_only.reg(0x2000,15);
    CHECK(ep_only.clock->advance(66037) && ep_only.apu.ep_frame_events()==1);
    CHECK(ep_only.gp->phase()==xlle::ApuGpPhase::Held && !ep_only.apu.voice_mix_quanta());
    CHECK(ep_only.error.empty());
    // A stopped or unfinished CPU must not become an invented GP completion.
    for(const auto& code:std::vector<std::vector<uint32_t>>{{0x87},{0x0af080,0}}) {
        Fixture f;f.configure(code);f.reg(0x2000,15);
        CHECK(!f.clock->advance(60750) && f.error.find("overrun remains unqualified")!=std::string::npos);
        CHECK(!f.apu.voice_mix_quanta() && !f.apu.ep_frame_events() && f.pcm.empty());
        const auto ticks=f.clock->ticks();CHECK(!f.clock->advance(66000) && f.clock->ticks()==ticks);
    }
    Fixture reentrant;reentrant.output();reentrant.reenter=true;
    CHECK(!reentrant.clock->advance(66000));
    CHECK(reentrant.error.find("reentrant audio device clock")!=std::string::npos);
    CHECK(reentrant.pcm.empty() && !reentrant.aci.consumed_pcm_words() && !reentrant.aci.completed_descriptors());
}

static void runtime_input_sge() {
    Fixture f;f.ram.resize(0x30000,0xa5);
    f.reg(0x1100,0x88);f.reg(0x1138,0x7ff);f.reg(0x2030,0x10000);
    f.reg(0x1510,0);f.reg(0x1100,0x100f);
    f.configure();f.reg(0x2000,15);
    CHECK(f.clock->advance(61537) && f.apu.voice_mix_quanta()==2);
    const auto before=f.ram;
    const auto gp=f.clock->retired(false),ep=f.clock->retired(true),tick=f.clock->ticks();
    f.reg(0x20804,17);f.reg(0x20808,0x20000);
    CHECK(f.error.empty() && f.clock->ticks()==tick && f.apu.voice_mix_quanta()==2);
    CHECK(f.clock->retired(false)==gp && f.clock->retired(true)==ep);
    auto expected=before;const uint32_t page=0x20000;
    std::memcpy(expected.data()+0x10000+17*8,&page,4);
    CHECK(f.ram==expected && f.gp->phase()==xlle::ApuGpPhase::Yielded && f.ep->phase()==xlle::ApuGpPhase::Running);
    CHECK(f.clock->advance(66037) && f.apu.voice_mix_quanta()==8 && f.apu.ep_frame_events()==1);
    CHECK(f.error.empty());
}

int main() {
    stopped_epoch();partitioned_active_output();admission_and_failure();runtime_input_sge();
    puts("PASS authored shared audio timeline: epoch, actual GP/EP events, partition-independent PCM bytes/stamps and failure retention");
}
