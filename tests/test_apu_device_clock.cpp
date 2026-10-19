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
    explicit Fixture(uint64_t epoch=60037,bool cold=false) {
        if(cold)CHECK(apu.initialize_board_audio_reset());
        else CHECK(apu.initialize_kernel_audio_handoff() && aci.initialize_kernel_audio_handoff());
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
    static std::vector<uint32_t> counter_event_program() {
        return {0x07f432,0xffffff,0x07f430,1,0x07f431,1,
                0x08f485,0xfff,0x08f484,1,0x0a8581,10,0x08f485,2,0x08f484,1,0x0af080,10};
    }
    void configure(const std::vector<uint32_t>& gp_program=event_program(),bool release_gp=true,
                   const std::vector<uint32_t>& ep_program=event_program()) {
        reg(0x2040,0x1000);reg(0x20d4,1);reg(0x2048,0x2000);reg(0x20dc,1);
        word(0x1000,0x4000);word(0x1008,0x5000);word(0x2000,0x6000);word(0x2008,0x7000);
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

static void clocked_fence() {
    Fixture f;f.configure();f.reg(0x1100,0x100f);f.reg(0x1510,0);f.reg(0x2000,15);
    // Admit real GP work. The fence orders completed FE/VP accesses,
    // not the unrelated DSP computations on their own scratch/MIX owners.
    CHECK(f.clock->advance(66000) && f.apu.gp_frame_outstanding());
    const auto ep_events=f.apu.ep_frame_events(),mix=f.apu.voice_mix_quanta();
    f.word(0x9000,0xa5a5a5a5);f.reg(0x1510,1);f.reg(0x1324,0x9000);f.reg(0x1334,0x12345678);
    const auto writes=f.writes;
    CHECK(f.error.empty() && f.apu.fe_fence_pending());
    for(unsigned n=0;n<16;++n)CHECK(f.apu.mmio_read(f.apu.kBase+0x1510,4)==1);
    CHECK(f.clock->advance(66000) && f.writes==writes);
    CHECK(f.clock->advance(66001) && f.writes==writes+1 && f.apu.fe_fences_published()==1);
    uint32_t token=0;std::memcpy(&token,f.ram.data()+0x9000,4);CHECK(token==0x12345678);
    CHECK(!f.apu.fe_fence_pending() && f.apu.ep_frame_events()==ep_events && f.apu.voice_mix_quanta()==mix);
    CHECK(f.clock->advance(66001) && f.writes==writes+1);
    f.reg(0x1510,0);CHECK(f.error.empty());
}

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
            const std::array<uint64_t,4> stamp={66000+i*6000,11+i,(i+1)*8,i};
            CHECK(f.stamps[i]==stamp);
        }
        CHECK(f.pcm==std::vector<uint8_t>(f.ram.begin()+0x8000,f.ram.begin()+0x8c00));
        CHECK(f.apu.voice_mix_quanta()==24 && f.apu.ep_frame_events()==2);
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
    CHECK(ep_only.clock->advance(66037) && !ep_only.apu.ep_frame_events());
    CHECK(ep_only.clock->advance(66750) && ep_only.apu.ep_frame_events()==1);
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

static void cold_first_gp_ready() {
    constexpr uint64_t epoch=5999,first_boundary=6000,first_frame=6750;
    for(unsigned kind=0;kind<4;++kind) {
        const bool cold=kind!=2;
        Fixture f(epoch,cold);
        std::vector<uint32_t> program(300,0); // real NOP initialization >119 cycles
        if(kind==3)program={0x87}; // STOP before the first ready is still unsupported
        else if(kind==1)program.insert(program.end(),{0x08f485,0xfff,0x08f484,1,0x0af080,304});
        else {
            auto events=Fixture::event_program();events[5]+=300;events[11]+=300;
            program.insert(program.end(),events.begin(),events.end());
        }
        f.reg(0x5fffc,1);f.reg(0x1100,0x88);f.reg(0x2000,7);
        f.reg(0x2040,0x1000);f.reg(0x20d4,1);
        f.word(0x1000,0x4000);f.word(0x1008,0x5000);
        std::memcpy(f.ram.data()+0x4000,program.data(),program.size()*4);
        f.reg(0x200c,0);
        for(unsigned top:{0x2054u,0x2060u,0x206cu})f.reg(top,0xffff);
        f.reg(0x1100,0x100f);f.reg(0x1510,0);f.reg(0x3fffc,1);f.reg(0x3fffc,3);
        f.reg(0x2000,15);CHECK(f.error.empty());
        if(!cold || kind==3) {
            CHECK(!f.clock->advance(first_boundary));
            CHECK(f.error.find("overrun remains unqualified")!=std::string::npos);
            CHECK(!f.apu.voice_mix_quanta() && !f.apu.ep_frame_events());
            continue;
        }
        CHECK(f.clock->advance(first_boundary));
        CHECK(f.gp->phase()==xlle::ApuGpPhase::Running && f.gp->first_bootstrap_initializing());
        CHECK(f.clock->retired(false)>0 && !f.apu.voice_mix_quanta() && !f.apu.ep_frame_events());
        CHECK(f.aci.current_frame()==1); // independent codec cadence is not skipped
        CHECK(f.clock->advance(first_boundary+2));
        CHECK(f.gp->phase()==xlle::ApuGpPhase::Yielded && !f.gp->first_bootstrap_initializing());
        CHECK(!f.apu.voice_mix_quanta()); // the real first yield supplies readiness only
        CHECK(f.clock->advance(first_frame) && f.apu.voice_mix_quanta()==1);
        if(kind==1) {
            CHECK(!f.clock->advance(first_frame+750));
            CHECK(f.error.find("overrun remains unqualified")!=std::string::npos);
            CHECK(f.apu.voice_mix_quanta()==1 && f.apu.gp_frame_outstanding());
        }else {
            CHECK(f.clock->advance(first_frame+750));
            CHECK(f.apu.voice_mix_quanta()==2 && f.error.empty());
        }
    }
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
    CHECK(f.clock->advance(66787) && f.apu.voice_mix_quanta()==9 && f.apu.ep_frame_events()==1);
    CHECK(f.error.empty());
}

static void drained_control() {
    Fixture f;f.configure();f.reg(0x1100,0x100f);f.reg(0x2000,15);
    CHECK(f.clock->advance(66787) && f.apu.voice_mix_quanta()==9 && f.apu.ep_frame_events()==1);
    f.reg(0x2000,7);CHECK(f.clock->advance(67000));
    const auto gp=f.clock->retired(false),ep=f.clock->retired(true),tick=f.clock->ticks();
    const auto reads=f.reads,writes=f.writes;
    const auto value=f.apu.mmio_read(f.apu.kBase+0x1100,4);
    CHECK(value==0x7100f && f.error.empty());
    f.reg(0x1100,value|0x80);CHECK(f.apu.mmio_read(f.apu.kBase+0x1100,4)==0x7108f);
    CHECK(f.apu.mmio_read(f.apu.kBase+0x1510,4)==1);
    f.reg(0x1510,0);CHECK(f.apu.mmio_read(f.apu.kBase+0x1510,4)==0 && f.error.empty());
    CHECK(f.clock->ticks()==tick && f.clock->retired(false)==gp && f.clock->retired(true)==ep);
    CHECK(f.reads==reads && f.writes==writes && f.apu.voice_mix_quanta()==9 && f.apu.ep_frame_events()==1);
    // Stopping admission at a boundary leaves a real GP frame outstanding.
    // The physical status encoding is not a drained-work acknowledgement.
    // Its read must preserve pending work; the actual reset guards own drain.
    for(bool release_gp:{false,true}) {
        Fixture pending;pending.configure(Fixture::event_program(),release_gp);
        pending.reg(0x1100,0x100f);pending.reg(0x2000,15);
        CHECK(pending.clock->advance(66750) && pending.apu.gp_frame_outstanding()==release_gp);
        CHECK(pending.apu.ep_frame_events()==1);
        pending.reg(0x2000,7);const auto before=pending.ram;
        CHECK(pending.apu.mmio_read(pending.apu.kBase+0x1100,4)==0x7100f);
        CHECK(!pending.apu.faulted() && pending.apu.gp_frame_outstanding()==release_gp && pending.ram==before);
        pending.reg(0x5fffc,1);
        CHECK(pending.apu.faulted() && pending.ep->phase()==xlle::ApuGpPhase::Failed && pending.ram==before);
    }
}

static void retirement_timeout() {
    Fixture f;f.reg(0x1510,0);f.reg(0x115c,0x8000);
    f.ram[0x800f]=0x80;f.configure(Fixture::counter_event_program(),true,Fixture::counter_event_program());
    f.reg(0x1100,0x100f);f.reg(0x2000,15);
    CHECK(f.clock->advance(66750) && f.apu.gp_frame_outstanding());
    CHECK(f.apu.mmio_read(f.apu.kBase+0x1100,4)==0x7900f && f.error.empty());
    const auto before=f.ram;const auto gp=f.clock->retired(false),ep=f.clock->retired(true);
    f.reg(0x2010c,0);
    CHECK(f.apu.mmio_read(f.apu.kBase+0x2000,4)==7 && f.apu.gp_frame_outstanding());
    CHECK(f.apu.mmio_read(f.apu.kBase+0x1100,4)==0x7100f);
    f.reg(0x20104,2);CHECK(f.error.empty() && f.apu.retirement_notification_pending());
    CHECK(f.apu.retirement_notifications()==1 && f.ram==before && !f.writes);
    CHECK(f.clock->retired(false)==gp && f.clock->retired(true)==ep);
    CHECK(f.apu.mmio_read(f.apu.kBase+0x20010,4)==0x80);
    CHECK(f.clock->advance(67501) && !f.apu.gp_frame_outstanding());
    CHECK(f.apu.voice_mix_quanta()==9 && f.apu.ep_frame_events()==1);
    CHECK(f.ram==before && !f.writes && f.apu.retirement_notification_pending());
    CHECK(f.clock->retired(false)>gp && f.clock->retired(true)>ep);
    f.reg(0x3fffc,0);f.reg(0x5fffc,1);
    CHECK(f.error.empty() && !f.apu.retirement_notification_pending());
    CHECK(f.gp->phase()==xlle::ApuGpPhase::Held && f.ep->phase()==xlle::ApuGpPhase::Held);
    const auto reads=f.reads;
    CHECK(f.clock->advance(78000) && f.reads==reads && f.ram==before && !f.writes);
    for(unsigned variant=0;variant<6;++variant) {
        Fixture bad;bad.reg(0x1510,0);bad.reg(0x1100,0x100f);bad.reg(0x115c,0x8000);
        bad.ram[0x800f]=0x80;
        if(variant!=0)bad.reg(0x2010c,0);
        if(variant==1)bad.reg(0x1510,1);
        if(variant==2)bad.ram[0x800f]=1;
        if(variant==3)bad.reg(0x115c,0x10000); // outside the live callback owner
        const auto ram=bad.ram;
        if(variant==4)bad.apu.mmio_write(bad.apu.kBase+0x20103,4,2);
        else bad.reg(0x20104,variant==5?1:2);
        CHECK(bad.apu.faulted() && !bad.apu.retirement_notification_pending());
        CHECK(bad.ram==ram && !bad.writes && !bad.apu.retirement_notifications());
    }
    for(unsigned action=0;action<3;++action) {
        Fixture pending;pending.reg(0x1510,0);pending.reg(0x1100,0x100f);
        pending.reg(0x115c,0x8000);pending.ram[0x800f]=0x80;
        pending.reg(0x2010c,0);pending.reg(0x20104,2);CHECK(pending.error.empty());
        const auto ram=pending.ram;
        pending.reg(action==0?0x115c:action==1?0x2000:0x20104,action==0?0x4000:action==1?15:2);
        CHECK(pending.apu.faulted() && pending.apu.retirement_notification_pending() &&
              pending.apu.retirement_notifications()==1 && pending.ram==ram && !pending.writes);
    }
    {
        Fixture busy;busy.reg(0x1510,0);busy.reg(0x115c,0x8000);busy.ram[0x800f]=0x80;
        busy.configure(Fixture::counter_event_program(),true,Fixture::counter_event_program());
        busy.reg(0x1100,0x100f);busy.reg(0x2000,15);CHECK(busy.clock->advance(66750));
        busy.reg(0x2010c,0);busy.reg(0x20104,2);CHECK(busy.error.empty());
        const auto ram=busy.ram;const auto reads=busy.reads;
        busy.reg(0x3fffc,0);
        CHECK(busy.apu.faulted() && busy.apu.gp_frame_outstanding() &&
              busy.apu.retirement_notification_pending() && busy.apu.retirement_notifications()==1);
        CHECK(busy.gp->phase()==xlle::ApuGpPhase::Failed && busy.ram==ram && busy.reads==reads && !busy.writes);
    }
}
static void producer_groups() {
    // Startup immediately before an absolute codec edge used to publish EP
    // after only one GP chunk. Start phase must not change group integrity.
    for(uint64_t epoch:{0ull,60037ull,65700ull})for(bool release_gp:{false,true}) {
        Fixture f(epoch);f.configure(Fixture::event_program(),release_gp);
        const uint64_t first=(f.clock->ticks()/750+1)*750;
        f.reg(0x2000,15);
        CHECK(f.clock->advance(first+8*750-1) && !f.apu.ep_frame_events());
        CHECK(f.apu.voice_mix_quanta()==(release_gp?8:0));
        CHECK(f.clock->advance(first+8*750) && f.apu.ep_frame_events()==1);
        CHECK(f.clock->advance(first+16*750) && f.apu.ep_frame_events()==2);
        CHECK(f.clock->advance(first+16*750+20) && f.error.empty());
    }
    // A pause preserves three existing chunks; actual rebootstrap starts a
    // new ring generation. Neither case replays the stopped absolute frames.
    for(bool reset:{false,true}) {
        Fixture f;f.configure(Fixture::counter_event_program(),true,Fixture::counter_event_program());
        f.reg(0x2000,15);CHECK(f.clock->advance(62260));
        CHECK(f.apu.voice_mix_quanta()==3 && !f.apu.ep_frame_events());
        f.reg(0x2000,7);CHECK(f.clock->advance(81037));
        if(reset) {
            f.reg(0x3fffc,0);f.reg(0x5fffc,1);
            f.reg(0x3fffc,3);f.reg(0x5fffc,3);
            CHECK(f.clock->advance(81237) && f.error.empty());
            CHECK(f.gp->bootstraps()==2 && f.ep->bootstraps()==2);
        }
        const uint64_t first=(f.clock->ticks()/750+1)*750;
        const uint64_t event=first+(reset?8:5)*750;
        f.reg(0x2000,15);
        CHECK(f.clock->advance(event-1) && !f.apu.ep_frame_events());
        CHECK(f.clock->advance(event) && f.apu.ep_frame_events()==1);
        CHECK(f.clock->advance(event+20) && f.error.empty());
    }
    // One cursor cannot inherit the other engine's partially consumed ring.
    Fixture unpaired;
    unpaired.configure(Fixture::counter_event_program(),true,Fixture::counter_event_program());
    unpaired.reg(0x2000,15);CHECK(unpaired.clock->advance(62260));
    unpaired.reg(0x2000,7);unpaired.reg(0x3fffc,0);unpaired.reg(0x3fffc,3);
    CHECK(unpaired.clock->advance(62500) && unpaired.error.empty());
    unpaired.reg(0x2000,15);
    CHECK(!unpaired.clock->advance(63000));
    CHECK(unpaired.error.find("unpaired GP/EP ring restart")!=std::string::npos);
    CHECK(unpaired.apu.voice_mix_quanta()==3 && !unpaired.apu.ep_frame_events());
    {
        Fixture trapped;trapped.configure();trapped.reg(0x2000,15);
        CHECK(trapped.clock->advance(66020) && trapped.apu.voice_mix_quanta()==8);
        trapped.reg(0x202c,0x8000);trapped.word(0x8054,0);trapped.word(0x807c,0xffff);
        trapped.reg(0x1504,1u<<15);trapped.reg(0x2054,0);
        CHECK(trapped.clock->advance(66750) && trapped.apu.idle_voice_traps()==1);
        CHECK(!trapped.apu.ep_frame_events() && trapped.apu.voice_mix_quanta()==8);
        CHECK(trapped.clock->advance(78037) && trapped.apu.voice_mix_quanta()==8);
        trapped.reg(0x2054,0xffff);trapped.reg(0x1100,0x100f);
        CHECK(trapped.clock->advance(78750) && trapped.apu.ep_frame_events()==1);
        CHECK(trapped.apu.voice_mix_quanta()==9 && trapped.clock->advance(78770));
    }
    {
        const std::vector<uint32_t> no_rearm={0x08f485,0xfff,0x08f484,1,
            0x0a8581,4,0x08f485,2,0x0af080,8};
        Fixture late;late.configure(Fixture::event_program(),true,no_rearm);late.reg(0x2000,15);
        CHECK(!late.clock->advance(72750));
        CHECK(late.error.find("EP missed completed GP group")!=std::string::npos);
        CHECK(late.apu.voice_mix_quanta()==16 && late.apu.ep_frame_events()==1);
    }
}
int main() {
    stopped_epoch();clocked_fence();partitioned_active_output();admission_and_failure();cold_first_gp_ready();runtime_input_sge();drained_control();retirement_timeout();
    producer_groups();
    puts("PASS authored shared audio timeline: epoch, actual GP/EP events, partition-independent PCM bytes/stamps and failure retention");
}
