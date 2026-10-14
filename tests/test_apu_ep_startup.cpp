// Original driver's owned RAM/SG tables -> the sole maintained EP CPU.
// Kernel allocation and initial clock context are explicit caller inputs.
#include "machine/apu_ep_processor.hpp"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"EP startup FAIL %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
static std::vector<unsigned char> load(const char* path) {
    FILE* file=std::fopen(path,"rb");CHECK(file);CHECK(!std::fseek(file,0,SEEK_END));
    const auto bytes=std::ftell(file);CHECK(bytes>0 && bytes<=0x400000);std::rewind(file);
    std::vector<unsigned char> data(bytes);CHECK(std::fread(data.data(),1,data.size(),file)==data.size());std::fclose(file);return data;
}
struct Fixture {
    std::vector<unsigned char> ram,registers,map,before;
    std::vector<unsigned> program;
    std::string device_error,owner_error;
    unsigned writes=0,reads=0;bool expect_descriptor=false,revoke=false;
    std::vector<std::array<unsigned,5>> completed;
    xlle::McpxApu apu{[this](const std::string& s){device_error=s;}};
    static unsigned word(const std::vector<unsigned char>& bytes,unsigned at) {
        CHECK(uint64_t(at)+4<=bytes.size());unsigned value;std::memcpy(&value,bytes.data()+at,4);return value;
    }
    bool owned(unsigned at,size_t bytes)const {
        if(uint64_t(at)+bytes>ram.size())return false;
        for(unsigned i=0;i<map.size();i+=8)if(at>=word(map,i) && uint64_t(at)+bytes<=uint64_t(word(map,i))+word(map,i+4))return true;
        return false;
    }
    unsigned reg(unsigned address)const {
        for(unsigned i=0;i<registers.size();i+=8)if(word(registers,i)==address)return word(registers,i+4);
        CHECK(false);return 0;
    }
    unsigned physical(unsigned offset)const {
        CHECK(offset/4096<reg(0xfe8020dc));const auto entry=reg(0xfe802048)+offset/4096*8;
        CHECK(owned(entry,8));const auto at=word(ram,entry)+(offset&4095);CHECK(owned(at,4));return at;
    }
    Fixture(char** argv):ram(load(argv[1])),registers(load(argv[2])),map(load(argv[3])),before(ram) {
        CHECK(ram.size()==0x400000 && !(registers.size()%8) && !(map.size()%8));
        apu.set_mem_access([this](unsigned a,void* p,size_t n){++reads;if(revoke || !owned(a,n))return false;std::memcpy(p,ram.data()+a,n);return true;},
            [this](unsigned a,const void* p,size_t n){++writes;if(revoke || !owned(a,n))return false;std::memcpy(ram.data()+a,p,n);return true;});
        for(unsigned i=0;i<registers.size();i+=8) {
            const auto a=word(registers,i),v=word(registers,i+4);
            // Use actual original mappings. Reset/core writes still require
            // their own startup contract, and are not invented here.
            if(a==0xfe802040 || a==0xfe802044 || a==0xfe802048 || a==0xfe80204c ||
               a==0xfe8020d4 || a==0xfe8020d8 || a==0xfe8020dc || a==0xfe8020e0 ||
               (a>=0xfe803024 && a<=0xfe80307c) || (a>=0xfe804024 && a<=0xfe80407c))apu.mmio_write(a,4,v);
        }
        for(unsigned i=0;i<2048;++i)program.push_back(word(ram,physical(i*4))&0xffffff);
        CHECK(program[0x100]==0x24 && program[0x101]==6 && program[0x114]==0x1cfe && program[0x115]==24);
        CHECK(!apu.faulted());
    }
    void fault(const std::string& why) {
        owner_error=why;if(!expect_descriptor)return;
        CHECK(device_error.find("unqualified EP DMA descriptor format")!=std::string::npos);
        CHECK(ram==before && !writes && !apu.gp_dma_nodes() && apu.ep_frame_events()==1);
        CHECK(apu.ep_dma_nodes()==7 && completed.size()==7);
        for(unsigned i=0;i<0x3800;++i)CHECK(!*(apu.gp_word(0)+i));
        std::printf("{\"stage\":\"original-first-event-next-boundary\",\"descriptor\":[");
        for(unsigned i=0;i<5;++i)std::printf("%s%u",i?",":"",*apu.ep_word(0x29a2+i));
        std::printf("],\"completed_descriptors\":[");
        for(unsigned n=0;n<completed.size();++n) {
            std::printf("%s[",n?",":"");
            for(unsigned i=0;i<5;++i)std::printf("%s%u",i?",":"",completed[n][i]);
            std::printf("]");
        }
        std::printf("],\"clock_events\":1,\"dma_nodes\":7,\"all_input_words_verified\":true,\"RAM_unchanged\":true,\"writes\":0,\"dynamic_table_preserved\":true}\n");
        std::fflush(stdout);std::fprintf(stderr,"expected original EP transfer boundary: %s; %s\n",why.c_str(),device_error.c_str());std::exit(2);
    }
    void verify_transfers() {
        if(apu.ep_dma_nodes()==completed.size())return;
        CHECK(apu.ep_dma_nodes()==completed.size()+1);
        std::array<unsigned,5> node;
        for(unsigned i=0;i<5;++i)node[i]=*apu.ep_word(0x29a2+i);
        CHECK(node[0]==0x41a2 && node[1]==0x5be0 && node[2] && node[2]<=4096);
        for(unsigned i=0;i<node[2];++i)CHECK(*apu.ep_word(node[3]+i)==(word(ram,physical(node[4]+i*4))&0xffffff));
        CHECK(ram==before && !writes && !apu.gp_dma_nodes());
        for(unsigned i=0x100;i<0x122;++i)CHECK(*apu.ep_word(0x2800+i)==program[i]);
        completed.push_back(node);
        std::fprintf(stderr,"verified original EP node%zu: %x,%x,%x,%x,%x\n",completed.size(),node[0],node[1],node[2],node[3],node[4]);
    }
};
int main(int argc,char** argv) {
    CHECK(argc==5);Fixture f(argv);const std::string mode=argv[4];
    CHECK(mode=="startup" || mode=="first-event" || mode=="revoked");
    xlle::ApuEpProcessor ep(f.apu,[&](const std::string& s){f.fault(s);});
    if(mode=="revoked") {
        f.revoke=true;CHECK(!ep.bootstrap_from_scratch() && !ep.bootstraps());
        const auto reads=f.reads;CHECK(!ep.bootstrap_from_scratch() && f.reads==reads && f.ram==f.before && !f.writes);
        for(unsigned i=0;i<4096;++i)CHECK(!*f.apu.ep_word(0x2800+i));
        std::puts("{\"stage\":\"revoked\",\"terminal_before_P_publication\":true}");return 0;
    }
    CHECK(ep.bootstrap_from_scratch() && std::equal(f.program.begin(),f.program.end(),f.apu.ep_word(0x2800)));
    xlle::ApuEpRun r;CHECK(ep.run(20000,r) && !r.power && !r.sp && !r.yielded && (r.pc==0x31 || r.pc==0x32));
    CHECK(f.ram==f.before && !f.writes && !f.apu.ep_dma_nodes() && !f.apu.ep_frame_events());
    for(unsigned i=0x100;i<0x122;++i)CHECK(*f.apu.ep_word(0x2800+i)==f.program[i]);
    if(mode=="startup") {
        std::puts("{\"stage\":\"original-startup-poll\",\"dynamic_table_preserved\":true,\"DMA_nodes\":0,\"clock_events\":0,\"RAM_unchanged\":true}");return 0;
    }
    f.expect_descriptor=true;
    // Observed empty-list clock profile, supplied by this caller. This does
    // not qualify the title's still-guarded initial SECTL/reset context.
    for(unsigned a:{0x2054u,0x2060u,0x206cu})f.apu.mmio_write(0xfe800000+a,4,0xffff);
    f.apu.mmio_write(0xfe801100,4,0x7130f);f.apu.mmio_write(0xfe802000,4,15);
    f.apu.set_time(18000,3375000);f.apu.set_frame(1);
    CHECK(f.apu.ep_frame_events()==1);
    for(unsigned i=0;i<200000;++i){CHECK(ep.run(1,r));f.verify_transfers();}
    CHECK(false); // A budget exhaustion cannot pass as the observed boundary.
}
