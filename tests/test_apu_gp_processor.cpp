// Authored GP instruction/control stimuli consume the actual VP MIX bank.
// Constant PCM expectations and page/target ownership are independent of DSP
// donor outputs. These are mapped boots, not hardware boot/cadence evidence.
#include "machine/apu_gp_processor.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"GP processor FAIL %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)

struct Fixture {
    static constexpr uint32_t voice_table=0x1000,voice_sge=0xa000,gp_sge=0xc000;
    std::vector<uint8_t> ram=std::vector<uint8_t>(0x90000,0xa5);
    std::string apu_error,owner_error;
    uint32_t fail_read=UINT32_MAX;
    unsigned boot_reads=0;
    bool guard_exit=false;
    bool unsafe_host_publication=false;
    std::vector<uint32_t> guarded_p;
    xlle::McpxApu apu{[this](const std::string& why){apu_error=why;}};
    Fixture() {
        apu.set_mem_access(
            [this](uint32_t a,void* out,size_t n){
                if(a==fail_read || uint64_t(a)+n>ram.size())return false;
                if(unsafe_host_publication && a>=0x60000)apu.mmio_write(0xfe83a020,4,0x654321);
                if(n==4096 && (a==0x60000 || a==0x64000 || a==0x74000 || a==0x78000))++boot_reads;
                std::memcpy(out,ram.data()+a,n);return true;
            },
            [this](uint32_t a,const void* in,size_t n){
                if(uint64_t(a)+n>ram.size())return false;
                std::memcpy(ram.data()+a,in,n);return true;
            });
        reg(0x2040,gp_sge);reg(0x20d4,3);
        for(unsigned page=0;page<4;++page)put(gp_sge+page*8,0x60000+page*0x4000);
        reg(0x202c,voice_table);reg(0x2030,voice_sge);
        for(unsigned page=0;page<64;++page)put(voice_sge+page*8,0x10000+page*4096);
        reg(0x2054,0xffff);reg(0x2060,0xffff);reg(0x206c,0xffff);
        reg(0x2000,8);reg(0x1100,0);
    }
    void owner_fault(const std::string& why) {
        owner_error=why;
        if(guard_exit) {
            CHECK(!guarded_p.empty());
            CHECK(std::equal(guarded_p.begin(),guarded_p.end(),apu.gp_word(0x2800)));
            CHECK(!apu.gp_dma_nodes());
            std::fprintf(stderr,"guarded before P/node publication: %s; APU: %s\n",why.c_str(),apu_error.c_str());
            std::exit(2);
        }
    }
    void put(unsigned address,uint32_t word){std::memcpy(ram.data()+address,&word,4);}
    uint32_t word(unsigned address)const{uint32_t word;std::memcpy(&word,ram.data()+address,4);return word;}
    void reg(unsigned offset,uint32_t value){apu.mmio_write(0xfe800000+offset,4,value);}
    void method(unsigned offset,uint32_t value){reg(0x20000+offset,value);}
    unsigned physical(unsigned linear)const{return word(gp_sge+(linear>>12)*8)+(linear&4095);}
    void scratch_word(unsigned linear,uint32_t value) {
        for(unsigned b=0;b<4;++b)ram[physical(linear+b)]=uint8_t(value>>(8*b));
    }
    uint32_t scratch_word(unsigned linear)const {
        uint32_t value=0;for(unsigned b=0;b<4;++b)value|=uint32_t(ram[physical(linear+b)])<<(8*b);return value;
    }
    void boot_image(const std::vector<uint32_t>& words,bool remap=false) {
        CHECK(words.size()<=2048);
        if(remap){put(gp_sge,0x78000);put(gp_sge+8,0x74000);}
        for(unsigned i=0;i<2048;++i)scratch_word(i*4,0xa7000000|(i<words.size()?words[i]:0x87));
    }
    void descriptor(uint32_t target,unsigned count,uint32_t control,unsigned linear=0x2ffd) {
        const uint32_t fields[]={0x4000,control,count,target,0,linear,count*4-1};
        for(unsigned i=0;i<7;++i)*apu.gp_word(0x20+i)=fields[i];
    }
    void add_voice(unsigned voice,bool stereo,unsigned bin,int16_t left,int16_t right=0) {
        const unsigned object=voice_table+voice*128,base=(voice-64)*1024;
        std::fill(ram.begin()+object,ram.begin()+object+128,0);
        for(unsigned n=0;n<256;++n) {
            std::memcpy(ram.data()+0x10000+base+n*(stereo?4:2),&left,2);
            if(stereo)std::memcpy(ram.data()+0x10000+base+n*4+2,&right,2);
        }
        method(0x2f8,voice);
        method(0x304,0x5000e000|(1u<<25)|(stereo?1u<<27|1u<<16:0)|6|(7<<5));
        method(0x300,bin|(3<<5)|(2<<10)|(4<<16)|(5<<21)|(6<<26));
        method(0x360,stereo?0x000f000f:0xffff000f);
        method(0x364,0xffffffff);method(0x368,0xffffffff);
        method(0x3a0,base);method(0x3dc,255);
        method(0x30c,0xff000000);
        method(0x120,0x1ffff);method(0x124,voice);
        CHECK(apu_error.empty());
    }
};

static std::vector<uint32_t> copy_mix_program(unsigned target) {
    std::vector<uint32_t> p;
    // Every copied word is read by an actual DSP MOVE from X:MIX. No host
    // supplies a direct-speaker bank or substitutes a CPU/peripheral call.
    for(unsigned bin:{0u,3u,31u})for(unsigned n=0;n<32;++n) {
        const uint32_t source=0x1400+bin*32+n;
        const uint32_t destination=target+unsigned(p.size()/6);
        const uint32_t move[]={0x65f400,source-4,0x021594,0x65f400,destination-4,0x021584};
        p.insert(p.end(),std::begin(move),std::end(move));
    }
    const uint32_t dma[]={0x65f400,0xffffd0,0x44f400,0x20,0x021584,
                         0x65f400,0xffffd2,0x44f400,1,0x021584,0x87};
    p.insert(p.end(),std::begin(dma),std::end(dma));return p;
}
static void mapped_mix_lifetimes() {
    for(unsigned life=0;life<3;++life) {
        Fixture f;f.add_voice(71,false,0,4096);f.add_voice(96,true,0,8192,-16384);
        xlle::ApuGpProcessor owner(f.apu,[&](const std::string& why){f.owner_fault(why);});
        CHECK(owner.phase()==xlle::ApuGpPhase::Held);
        *f.apu.gp_word(0x3000)=0x654321;*f.apu.gp_word(0x1900)=0x234567;
        for(unsigned q=0;q<10;++q) {
            xlle::ApuVoiceQuantum quantum;CHECK(f.apu.process_2d_quantum(quantum));
            CHECK(quantum.published && quantum.contributed==2 && !quantum.completed);
            const unsigned target=q<5?0x500:0x580;
            const auto program=copy_mix_program(target);f.boot_image(program,q==5);
            f.descriptor(target,96,0x4000|(6<<10)|(14<<5)|2);
            CHECK(owner.reset_cpu() && owner.bootstrap_from_scratch());
            CHECK(owner.bootstraps()==q+1 && f.boot_reads==2*(q+1));
            CHECK(*f.apu.gp_word(0x2800+2047)==0x87 && *f.apu.gp_word(0x3000)==0x654321);
            xlle::ApuGpRun result;CHECK(owner.run(10000,result));
            CHECK(result.returned && result.power==2 && !result.sp && result.pc==program.size());
            CHECK(result.sequence==q+1 && owner.phase()==xlle::ApuGpPhase::Stopped);
            unsigned at=0;
            for(unsigned bin:{0u,3u,31u})for(unsigned n=0;n<32;++n,++at) {
                const uint32_t actual=*f.apu.gp_word(0x1400+bin*32+n);
                CHECK(*f.apu.gp_word(target+at)==actual);
                CHECK(f.scratch_word(0x2ffd+at*4)==actual);
                if(q>=8) {
                    const uint32_t expected=bin==0?0x300000:bin==3?0xc00000:0;
                    // Existing source-rate/quantum constant-signal policy is
                    // 2e-6 normalized (17 PCM24 units). Copies above remain
                    // exact; this does not introduce a silicon rounding claim.
                    if(std::abs(int(actual)-int(expected))>17)
                        std::fprintf(stderr,"steady PCM q%u bin%u sample%u: %06x expected %06x\n",q,bin,n,actual,expected);
                    CHECK(std::abs(int(actual)-int(expected))<=17);
                }
            }
            CHECK(*f.apu.gp_word(0x1900)==0x234567 && f.apu.gp_dma_nodes()==q+1);
            CHECK(f.apu_error.empty() && f.owner_error.empty());
        }
    }
}
static std::vector<uint32_t> marker_loop(uint32_t value) {
    std::vector<uint32_t> p(17,0);
    p[0]=0x0c0008;
    p[8]=0x65f400;p[9]=0xfc;p[10]=0x44f400;p[11]=value;p[12]=0x021584;
    p[13]=0x0c0010;p[16]=0x0c0008;return p;
}
static void external_program_publication() {
    Fixture f;f.boot_image(marker_loop(0x123456));
    xlle::ApuGpProcessor owner(f.apu,[&](const std::string& why){f.owner_fault(why);});
    CHECK(owner.bootstrap_from_scratch());
    xlle::ApuGpRun result;
    auto reach_pc=[&](uint32_t target){
        // A cycle budget may stop at an instruction boundary inside a block;
        // do not turn a requested budget into an invented whole-block finish.
        for(unsigned steps=0;steps<32;++steps) {
            CHECK(owner.run(1,result) && result.power==0 && !result.sp);
            if(result.pc==target)return;
        }
        CHECK(false);
    };
    reach_pc(8);
    reach_pc(16);CHECK(*f.apu.gp_word(0x100)==0x123456);
    reach_pc(8);
    // P block8 has already been translated. A host update must execute its
    // new immediate on the next run, without resetting/rebooting the CPU.
    const uint64_t host_revision=f.apu.gp_program_revision();
    f.reg(0x3a000+11*4,0x654321);CHECK(f.apu.gp_program_revision()==host_revision+1);
    reach_pc(16);CHECK(*f.apu.gp_word(0x100)==0x654321);
    reach_pc(8);
    // Real input DMA between run calls publishes another executable word.
    f.scratch_word(0x2ffd,0x2468ac);f.descriptor(0x280b,1,0x4000|(6<<10)|(14<<5));
    const uint64_t dma_revision=f.apu.gp_program_revision();
    CHECK(f.apu.gp_write(0xffffd4,0x20) && f.apu.gp_write(0xffffd6,1) && f.apu.service_dsp_dma(xlle::ApuDspKind::Gp,4096));
    CHECK(f.apu.gp_program_revision()==dma_revision+1 && f.apu.gp_dma_nodes()==1);
    reach_pc(16);CHECK(*f.apu.gp_word(0x100)==0x2468ac);
    // Explicit CPU reset clears the actual CPU register state and preserves
    // caller-owned X/Y/MIX/P/peripheral banks; it is not whole-device GPRST.
    CHECK(owner.reset_cpu());
    f.boot_image({0x65f400,0xfc,0x021584,0x87});CHECK(owner.bootstrap_from_scratch());
    CHECK(owner.run(100,result) && result.power==2 && result.pc==4);
    CHECK(*f.apu.gp_word(0x100)==0 && f.apu.gp_dma_nodes()==1);
    CHECK(f.apu_error.empty() && f.owner_error.empty());
}
static void owner_and_mapping_guards() {
    for(unsigned scenario=0;scenario<6;++scenario) {
        Fixture f;f.boot_image({0x87});
        xlle::ApuGpProcessor owner(f.apu,[&](const std::string& why){f.owner_fault(why);});
        std::vector<uint32_t> before(f.apu.gp_word(0x2800),f.apu.gp_word(0x2800)+4096);
        switch(scenario) {
        case 0:f.reg(0x20d4,0);break;
        case 1:f.put(Fixture::gp_sge+8,0x64001);break;
        case 2:f.fail_read=0x64000;break;
        case 3:f.reg(0x2040,0xfffffff8);break;
        case 4:f.reg(0x2040,Fixture::gp_sge+4);break;
        case 5:f.put(Fixture::gp_sge+8,0x90000);break;
        }
        CHECK(!owner.bootstrap_from_scratch() && owner.phase()==xlle::ApuGpPhase::Failed);
        CHECK(!owner.bootstraps() && !f.owner_error.empty() && !f.apu_error.empty());
        CHECK(std::equal(before.begin(),before.end(),f.apu.gp_word(0x2800)));
        xlle::ApuGpRun result{true,1,1,1,1};CHECK(!owner.run(100,result) && !result.returned && !result.sequence);
        CHECK(!owner.reset_cpu());
    }
    Fixture f;f.boot_image({0x87});
    {
        xlle::ApuGpProcessor first(f.apu,[&](const std::string& why){f.owner_fault(why);});
        {
            xlle::ApuGpProcessor duplicate(f.apu,[&](const std::string& why){f.owner_fault(why);});
            CHECK(duplicate.phase()==xlle::ApuGpPhase::Failed);
        }
        CHECK(first.bootstrap_from_scratch());
        xlle::ApuGpRun result;CHECK(first.run(100,result) && result.power==2);
    }
    // A destroyed owner detaches its callback; a new owner may use the same
    // APU banks without keeping a dangling old CPU or resetting device RAM.
    f.owner_error.clear();
    xlle::ApuGpProcessor next(f.apu,[&](const std::string& why){f.owner_fault(why);});
    CHECK(next.bootstrap_from_scratch());xlle::ApuGpRun result;CHECK(next.run(100,result));
    CHECK(f.apu_error.empty() && f.owner_error.empty());
    Fixture failed;failed.boot_image({0x87});
    xlle::ApuGpProcessor unavailable(failed.apu,[&](const std::string& why){failed.owner_fault(why);});
    CHECK(unavailable.bootstrap_from_scratch());
    CHECK(!failed.apu.gp_write(0xffffc0,0) && failed.apu.faulted());
    result={true,1,1,1,1};
    CHECK(!unavailable.run(100,result) && !result.returned && !result.sequence);
    CHECK(unavailable.phase()==xlle::ApuGpPhase::Failed && !unavailable.reset_cpu());
}
// Unknown bit0 combined with bit9 stays outside the admitted GP input profiles.
static void cpu_publication_guard(bool bit9) {
    Fixture f;
    f.boot_image({0x65f400,0xffffd0,0x44f400,0x20,0x021584,
                  0x65f400,0xffffd2,0x44f400,1,0x021584,0x87});
    xlle::ApuGpProcessor owner(f.apu,[&](const std::string& why){f.owner_fault(why);});
    CHECK(owner.bootstrap_from_scratch());f.scratch_word(0x2ffd,0x123456);
    f.descriptor(bit9?0x100:0x2800,1,bit9?0x004bd1:0x4000|(6<<10)|(14<<5));
    f.guarded_p.assign(f.apu.gp_word(0x2800),f.apu.gp_word(0x2800)+4096);f.guard_exit=true;f.unsafe_host_publication=!bit9;
    xlle::ApuGpRun result;owner.run(1000,result);CHECK(false);
}
static void original_boot_boundary(const char* path) {
    FILE* input=std::fopen(path,"rb");CHECK(input);
    std::vector<uint32_t> words(0x5cc/4);
    CHECK(std::fread(words.data(),4,words.size(),input)==words.size());
    CHECK(std::fgetc(input)==EOF);std::fclose(input);
    Fixture f;f.boot_image(words);
    xlle::ApuGpProcessor owner(f.apu,[&](const std::string& why){f.owner_fault(why);});
    CHECK(owner.bootstrap_from_scratch());
    for(unsigned i=0;i<words.size();++i)CHECK(*f.apu.gp_word(0x2800+i)==(words[i]&0xffffff));
    f.guarded_p.assign(f.apu.gp_word(0x2800),f.apu.gp_word(0x2800)+4096);f.guard_exit=true;
    xlle::ApuGpRun result;owner.run(10000,result);CHECK(false);
}
int main(int argc,char** argv) {
    if(argc==3 && std::string(argv[1])=="--original-boot"){original_boot_boundary(argv[2]);return 1;}
    if(argc==2){cpu_publication_guard(std::string(argv[1])=="bit9");return 1;}
    CHECK(argc==1);
    mapped_mix_lifetimes();external_program_publication();owner_and_mapping_guards();
    std::puts("GP processor:30 mapped mixed boots, fresh remap, live program publication/reset and owner/mapping guards passed");
    return 0;
}
