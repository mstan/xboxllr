// Reached nForce digital transport, separate from analog PCM presentation.
// Register/word contracts: Linux intel8x0 nforce_regs and the owned original
// codec helpers. Physical active-stream equivalence is a separate checkpoint.
#include "machine/aci.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#define CHECK(c) do { if(!(c)) { std::fprintf(stderr,"SPDIF failure %d: %s\n",__LINE__,#c);std::exit(1); } } while(0)
struct Fixture {
    std::vector<uint8_t> ram=std::vector<uint8_t>(0x10000),pcm,digital;
    std::vector<bool> irq;
    std::vector<uint32_t> formats;
    std::string error;
    uint32_t failed=UINT32_MAX;
    unsigned reads=0,violation=0;
    xlle::McpxAci aci{[this](const std::string& s){error=s;}};
    Fixture() {
        CHECK(aci.initialize_kernel_audio_handoff());
        CHECK(aci.prime_frame_clock(0));aci.set_time(0,1125000);
        aci.set_mem_access([this](uint32_t a,void* p,size_t n){
            ++reads;if(a==failed || uint64_t(a)+n>ram.size())return false;
            std::memcpy(p,ram.data()+a,n);return true;
        });
        aci.set_pcm_output([this](const uint8_t* p,size_t n){pcm.insert(pcm.end(),p,p+n);return true;});
        aci.set_spdif_output([this](const uint8_t* p,size_t n,uint32_t format){
            CHECK(aci.consumed_spdif_words()*2==digital.size());
            CHECK(format==0x02000000 || format==0x02000002);
            if(violation==2)wr(0x17b,1,0);
            if(violation==3)aci.set_frame(999);
            if(violation==4)aci.set_mem_access({});
            if(violation==5)aci.set_pcm_output({});
            if(violation==6)aci.set_spdif_output({});
            if(violation)return false;
            digital.insert(digital.end(),p,p+n);formats.push_back(format);return true;
        });
        aci.set_irq([this](int line,bool level){CHECK(line==6);irq.push_back(level);});
    }
    void wr(unsigned off,unsigned n,uint32_t v){aci.mmio_write(aci.kBase+off,n,v);}
    uint32_t rd(unsigned off,unsigned n=1){return aci.mmio_read(aci.kBase+off,n);}
    void put(unsigned a,uint32_t v){for(unsigned i=0;i<4;++i)ram.at(a+i)=uint8_t(v>>(i*8));}
    void bd(unsigned ring,unsigned i,unsigned a,unsigned words,unsigned flags=0xc0000000){
        put(ring+i*8,a);put(ring+i*8+4,words|flags);
    }
    void start(unsigned base,unsigned ring,unsigned last=0){
        wr(base+11,1,2);wr(base,4,ring);wr(base+4,1,0);wr(base+5,1,last);wr(base+11,1,0x1d);
    }
    std::vector<uint8_t> bytes(unsigned a,unsigned n,unsigned seed) {
        for(unsigned i=0;i<n;++i)ram.at(a+i)=uint8_t(i*29+seed);
        return {ram.begin()+a,ram.begin()+a+n};
    }
};
static void independent_streams() {
    for(uint32_t format:{0x02000000u,0x02000002u}) {
        Fixture f;auto analog=f.bytes(0x4000,1024,1);auto digital=f.bytes(0x6000,1024,2);
        f.bd(0x1000,0,0x4000,512);f.bd(0x2000,0,0x6000,512);
        f.wr(0x17c,4,format);f.start(0x170,0x2000);f.start(0x110,0x1000);
        const unsigned reads=f.reads;
        for(unsigned i=0;i<100;++i){CHECK(!f.rd(0x176));CHECK(f.rd(0x178,2)==512);f.aci.set_frame(0);}
        CHECK(reads==f.reads && f.pcm.empty() && f.digital.empty());
        f.aci.set_frame(1);CHECK(f.error.empty() && f.pcm==analog && f.digital==digital);
        CHECK(f.formats==std::vector<uint32_t>{format});
        CHECK(f.aci.completed_descriptors()==1 && f.aci.completed_spdif_descriptors()==1);
        CHECK(f.rd(0x176)==15 && f.rd(0x176,2)==15 && !f.rd(0x178,2));
        CHECK((f.rd(0x130,4)&0x50)==0x50 && f.irq==std::vector<bool>{true});
        f.wr(0x176,1,0xc);CHECK(f.rd(0x176)==3 && (f.rd(0x130,4)&0x50)==0x40);
        CHECK(f.irq==std::vector<bool>{true}); // PCM still owns the shared level.
        f.wr(0x116,2,0xc);CHECK((f.irq==std::vector<bool>{true,false}));
        f.aci.set_frame(100);CHECK(f.digital==digital && f.pcm==analog);
    }
}
static void stop_resume_reset_and_reuse() {
    Fixture f;f.wr(0x17c,4,0x02000002);f.bytes(0x4000,2048,3);f.bd(0x2000,0,0x4000,1024);
    f.start(0x170,0x2000);f.aci.set_frame(1);CHECK(f.digital.size()==1024 && f.rd(0x178,2)==512);
    f.wr(0x17b,1,0);const auto reads=f.reads;f.aci.set_frame(9);
    CHECK(f.reads==reads && f.digital.size()==1024 && f.rd(0x178,2)==512 && f.rd(0x176)==1);
    auto changed=f.bytes(0x4400,1024,71);f.wr(0x17b,1,0x1d);f.aci.set_frame(10);
    CHECK(f.error.empty() && std::vector<uint8_t>(f.digital.begin()+1024,f.digital.end())==changed);
    CHECK(f.aci.completed_spdif_descriptors()==1 && !f.aci.completed_descriptors());
    // Extend an exhausted ring without fabricating completion from the write.
    auto next=f.bytes(0x6000,1024,4);f.bd(0x2000,1,0x6000,512);f.wr(0x175,1,1);
    CHECK(f.digital.size()==2048 && f.rd(0x174)==1);f.aci.set_frame(11);
    CHECK(std::vector<uint8_t>(f.digital.end()-1024,f.digital.end())==next);
    f.wr(0x17b,1,0);f.wr(0x17b,1,2);
    CHECK(!f.rd(0x170,4) && !f.rd(0x174) && !f.rd(0x175) && !f.rd(0x178,2) && !f.rd(0x17a) && f.rd(0x176)==1);
    f.wr(0x17c,4,0x02000000);f.bd(0x2000,0,0x6000,512);f.start(0x170,0x2000);f.aci.set_frame(12);
    CHECK(f.error.empty() && f.formats.back()==0x02000000 && f.aci.completed_spdif_descriptors()==3);
    CHECK(f.pcm.empty() && !f.aci.consumed_pcm_words());
}
static void failure_keeps_ownership() {
    for(unsigned kind=0;kind<7;++kind) {
        Fixture f;f.wr(0x17c,4,0x02000000);f.bd(0x2000,0,0x6000,512);f.start(0x170,0x2000);
        if(kind==0)f.failed=0x6000;else f.violation=kind;
        f.aci.set_frame(1);CHECK(!f.error.empty() && f.digital.empty());
        CHECK(!f.aci.completed_spdif_descriptors() && !f.aci.consumed_spdif_words());
        CHECK(f.rd(0x178,2)==512 && !f.rd(0x174) && f.rd(0x17a)==1 && !(f.rd(0x130,4)&0x10));
        const auto reads=f.reads;f.wr(0x17b,1,2);f.aci.set_frame(9);CHECK(f.reads==reads && f.rd(0x178,2)==512);
    }
    for(unsigned kind=0;kind<5;++kind) {
        Fixture f;if(kind!=0)f.wr(0x17c,4,0x02000000);
        f.bd(0x2000,0,kind==3?0xfffffffcu:0x6000,kind==2?3:512,kind==1?0x80010000:0xc0000000);
        if(kind==4)f.failed=0x2000;
        f.start(0x170,0x2000);
        CHECK(!f.error.empty() && f.digital.empty() && !f.aci.completed_spdif_descriptors() && !f.rd(0x178,2));
    }
    Fixture f;f.wr(0x17c,4,0x02000000);f.bd(0x2000,0,0x6000,1024);f.start(0x170,0x2000);
    f.wr(0x17b,1,0);f.wr(0x17c,4,0x02000002);
    CHECK(!f.error.empty() && f.rd(0x17c,4)==0x02000000 && f.rd(0x178,2)==1024);
}
int main() {
    independent_streams();stop_resume_reset_and_reuse();failure_keeps_ownership();
    std::puts("PASS separate digital bytes/formats, shared IRQ, live RAM, stop/resume/reset and terminal ownership");
}
