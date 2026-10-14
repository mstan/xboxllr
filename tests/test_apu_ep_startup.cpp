// Original driver's owned RAM/SG tables -> the sole maintained EP CPU.
// Kernel allocation and initial clock context are explicit caller inputs.
#include "machine/apu_ep_processor.hpp"
#include <algorithm>
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
    std::vector<unsigned char> ram,registers,map,before,expected_ram;
    std::vector<unsigned> program;
    std::string device_error,owner_error;
    unsigned writes=0,reads=0,output_nodes=0,fifo_cursor=0,fifo1_cursor=0,last_pc=0;bool expect_boundary=false,revoke=false,has_lookup=false;
    std::vector<std::vector<unsigned>> completed;
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
    unsigned fifo_physical(unsigned offset)const {
        CHECK(offset/4096<=reg(0xfe8020e0));const auto entry=reg(0xfe80204c)+offset/4096*8;
        CHECK(owned(entry,8));const auto at=word(ram,entry)+(offset&4095);CHECK(owned(at,1));return at;
    }
    Fixture(char** argv):ram(load(argv[1])),registers(load(argv[2])),map(load(argv[3])),before(ram),expected_ram(ram) {
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
        fifo_cursor=reg(0xfe80402c);fifo1_cursor=reg(0xfe80403c);CHECK(!apu.faulted());
    }
    void fault(const std::string& why) {
        owner_error=why;if(!expect_boundary)return;
        CHECK(!has_lookup);
        CHECK(why=="unmapped DSP read at 000fc9" && device_error.empty() && last_pc==0x31e);
        CHECK(completed.size()==10 && output_nodes==2 && writes==2);
        CHECK(*apu.ep_word(0x29a3)==0x5be0 && *apu.ep_word(0x29a4)==0x862);
        std::fprintf(stderr,"last original PC=%x\n",last_pc);
        report("original-first-event-next-boundary");
        std::fflush(stdout);std::fprintf(stderr,"original EP boundary: %s; %s\n",why.c_str(),device_error.c_str());std::exit(2);
    }
    void report(const char* stage) {
        CHECK(ram==expected_ram && !apu.gp_dma_nodes() && apu.ep_frame_events()==1);
        CHECK(apu.ep_dma_nodes()==completed.size() && completed.size()>=7);
        for(unsigned i=0;i<0x3800;++i)CHECK(!*(apu.gp_word(0)+i));
        std::printf("{\"stage\":\"%s\",\"descriptor\":[",stage);
        const unsigned fields=((*apu.ep_word(0x29a3)>>5)&15)<14?4:5;
        for(unsigned i=0;i<fields;++i)std::printf("%s%u",i?",":"",*apu.ep_word(0x29a2+i));
        std::printf("],\"completed_descriptors\":[");
        for(unsigned n=0;n<completed.size();++n) {
            std::printf("%s[",n?",":"");
            for(unsigned i=0;i<completed[n].size();++i)std::printf("%s%u",i?",":"",completed[n][i]);
            std::printf("]");
        }
        std::printf("],\"clock_events\":1,\"dma_nodes\":%zu,\"output_nodes\":%u,\"all_input_words_verified\":true,\"all_output_words_verified\":true,\"RAM_unchanged\":%s,\"writes\":%u,\"dynamic_table_preserved\":true}\n",completed.size(),output_nodes,ram==before?"true":"false",writes);
    }
    void verify_transfers() {
        if(apu.ep_dma_nodes()==completed.size())return;
        CHECK(apu.ep_dma_nodes()==completed.size()+1);
        const bool stereo=*apu.ep_word(0x29a3)==0x400603,fifo1=*apu.ep_word(0x29a3)==0x4622,fifo=stereo || fifo1;
        std::vector<unsigned> node(fifo?4:5);
        for(unsigned i=0;i<node.size();++i)node[i]=*apu.ep_word(0x29a2+i);
        CHECK(node[0]==0x41a2 && (node[1]==0x5be0 || node[1]==0x5be2 || fifo));
        if(fifo) {
            if(stereo)CHECK(node[2]==0x1001 && node[3]+512<=0xc00);
            else CHECK(node[2] && node[2]<=4096 && !(node[2]&1) && (node[3]==0x401f || node[3]+node[2]<=0xc00));
            const unsigned ring=0xfe804024+(fifo1?16:0),base=reg(ring),end=reg(ring+4),bytes=stereo?1024:node[2]*2;
            unsigned& cursor=fifo1?fifo1_cursor:fifo_cursor;
            CHECK(base<end && cursor>=base && cursor<end);
            for(unsigned i=0;i<bytes;++i) {
                const unsigned at=fifo_physical(cursor);
                const unsigned value=node[3]==0x401f?0:*apu.ep_word(node[3]+(stereo?(i/2%2)*256+i/4:i/2));
                const unsigned char byte=static_cast<unsigned char>(value>>(8+8*(i%2)));
                CHECK(ram[at]==byte);expected_ram[at]=byte;
                if(++cursor==end)cursor=base;
            }
            CHECK(apu.RegFileMmio::mmio_read(ring+8,4)==cursor);++output_nodes;
        } else if(node[1]==0x5be2) {
            CHECK(node[2] && node[2]<=4096);
            for(unsigned i=0;i<node[2];++i) {
                const unsigned value=*apu.ep_word(node[3]+i)&0xffffff;
                CHECK(word(ram,physical(node[4]+i*4))==value);
                std::memcpy(expected_ram.data()+physical(node[4]+i*4),&value,4);
            }
            ++output_nodes;
        } else {CHECK(node[2] && node[2]<=4096);for(unsigned i=0;i<node[2];++i)CHECK(*apu.ep_word(node[3]+i)==(word(ram,physical(node[4]+i*4))&0xffffff));}
        CHECK(ram==expected_ram && !apu.gp_dma_nodes());
        for(unsigned i=0x100;i<0x122;++i)CHECK(*apu.ep_word(0x2800+i)==program[i]);
        completed.push_back(node);
        std::fprintf(stderr,"verified original EP node%zu:",completed.size());
        for(auto field:node)std::fprintf(stderr," %x",field);std::fprintf(stderr,"\n");
    }
};
int main(int argc,char** argv) {
    CHECK(argc==5 || argc==6);Fixture f(argv);const std::string mode=argv[4];
    if(argc==6){const auto bytes=load(argv[5]);CHECK(bytes.size()==8192);std::vector<unsigned> image(2048);std::memcpy(image.data(),bytes.data(),bytes.size());CHECK(f.apu.install_ep_y_lookup(image.data(),image.size()));f.has_lookup=true;}
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
    f.expect_boundary=true;
    // Observed empty-list clock profile, supplied by this caller. This does
    // not qualify the title's still-guarded initial SECTL/reset context.
    for(unsigned a:{0x2054u,0x2060u,0x206cu})f.apu.mmio_write(0xfe800000+a,4,0xffff);
    f.apu.mmio_write(0xfe801100,4,0x7130f);f.apu.mmio_write(0xfe802000,4,15);
    f.apu.set_time(18000,3375000);f.apu.set_frame(1);
    CHECK(f.apu.ep_frame_events()==1);
    for(unsigned i=0;i<(f.has_lookup?1000000u:200000u);++i){
        f.last_pc=r.pc;CHECK(ep.run(1,r));f.verify_transfers();
        if(f.completed.size()>=8 && (r.pc==0x31 || r.pc==0x32) && !r.sp && !r.power) {
            if(f.has_lookup && f.completed[1][2]==754)CHECK(f.completed.size()==36 && f.output_nodes==18 && f.writes==22);
            else CHECK(f.completed.size()==8 && f.output_nodes==1 && f.writes==1);
            CHECK(!f.apu.dsp_work_pending(xlle::ApuDspKind::Ep) && f.device_error.empty() && f.owner_error.empty());
            unsigned events=0,control=0;CHECK(f.apu.ep_read(0xffffc5,events) && events==0x100);
            CHECK(f.apu.ep_control_publication(control) && control==1);
            const auto banks=std::vector<unsigned>(f.apu.ep_word(0),f.apu.ep_word(0)+0x3800);
            CHECK(ep.run(1000,r) && (r.pc==0x31 || r.pc==0x32) && !r.sp && !r.power && !r.yielded);
            CHECK(std::equal(banks.begin(),banks.end(),f.apu.ep_word(0)) && !f.apu.dsp_work_pending(xlle::ApuDspKind::Ep));
            f.report("original-first-event-returned");return 0;
        }
    }
    std::fprintf(stderr,"budget boundary:pc=%x sp=%u nodes=%zu outputs=%u descriptor=%x,%x,%x,%x,%x\n",r.pc,r.sp,f.completed.size(),f.output_nodes,*f.apu.ep_word(0x29a2),*f.apu.ep_word(0x29a3),*f.apu.ep_word(0x29a4),*f.apu.ep_word(0x29a5),*f.apu.ep_word(0x29a6));
    CHECK(false); // A budget exhaustion cannot pass as the observed boundary.
}
