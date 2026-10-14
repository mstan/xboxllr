// Original frozen driver/scratch fixture continued by the production GP owner.
// Existing diagnostic PCM input is retained; no original bytes/event are replaced.
#include "machine/apu_gp_processor.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"GP continuation fail %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
static std::vector<uint8_t> load(const char* name) {
    FILE* f=std::fopen(name,"rb");CHECK(f);CHECK(!std::fseek(f,0,SEEK_END));auto n=std::ftell(f);CHECK(n>=0);
    std::rewind(f);std::vector<uint8_t> b(n);CHECK(std::fread(b.data(),1,b.size(),f)==b.size());std::fclose(f);return b;
}
struct Fixture {
    std::vector<uint8_t> ram,map,registers,before,capture,input;
    std::vector<std::pair<unsigned,unsigned>> written;
    std::string mode,error;unsigned reads=0,writes=0,voice,sge,notifier;bool starved=false;
    xlle::McpxApu apu{[this](const std::string& why){error=why;}};
    Fixture(char** argv):ram(load(argv[1])),map(load(argv[3])),registers(load(argv[2])),
        mode(argv[7]),voice(std::strtoul(argv[4],nullptr,0)),sge(std::strtoul(argv[5],nullptr,0)),
        notifier(std::strtoul(argv[6],nullptr,0)) {
        CHECK(ram.size()==0x400000 && !(map.size()%8) && !(registers.size()%8));
        apu.set_mem_access([this](unsigned a,void* out,size_t n){
            ++reads;if(!owned(a,n) || (starved && a>=source() && a<source()+4096))return false;
            std::memcpy(out,ram.data()+a,n);return true;
        },[this](unsigned a,const void* in,size_t n){
            ++writes;if(!owned(a,n))return false;written.emplace_back(a,unsigned(n));std::memcpy(ram.data()+a,in,n);return true;
        });
        for(unsigned i=0;i<registers.size();i+=8) {
            const auto a=word(registers,i),v=word(registers,i+4);
            // Qualified mapping and actual original FE/SE controls only.
            // Hardware reset/core/IRQ observations are not dispatched.
            if(a==0xfe801100 || a==0xfe802000 || a==0xfe802040 || a==0xfe802044 ||
               a==0xfe802048 || a==0xfe80204c || a==0xfe8020d4 || a==0xfe8020d8 ||
               a==0xfe8020dc || a==0xfe8020e0 || (a>=0xfe803024 && a<=0xfe80307c) ||
               (a>=0xfe804024 && a<=0xfe80407c))apu.mmio_write(a,4,v);
        }
        // Diagnostic voice/source input resolves the original driver's actual
        // owned allocations. This is not an original wave-buffer constructor.
        reg(0x202c,voice);reg(0x2030,sge);reg(0x115c,notifier);
        reg(0x2054,0xffff);reg(0x2060,0xffff);reg(0x206c,0xffff);
        reg(0x1504,1u<<15);
        const unsigned object=voice+71*128;CHECK(owned(object,128));
        std::fill(ram.begin()+object,ram.begin()+object+128,0);
        method(0x2f8,71);method(0x304,0x5200e000|6|(7<<5));
        method(0x300,0|(1<<5)|(2<<10)|(3<<16)|(4<<21)|(5<<26));
        method(0x360,64u<<20);method(0x364,(128u<<4)|(192u<<20));method(0x368,(256u<<4)|(320u<<20));
        method(0x3a0,0);method(0x3dc,255);method(0x30c,0xff000000);
        method(0x120,0x1ffff);method(0x124,71);
        CHECK(error.empty() && !apu.faulted());before=ram;
    }
    static unsigned word(const std::vector<uint8_t>& b,unsigned at){CHECK(at+4<=b.size());unsigned v;std::memcpy(&v,b.data()+at,4);return v;}
    unsigned get(unsigned a)const{return word(ram,a);}
    unsigned source()const{return get(sge);}
    bool owned(unsigned a,size_t n)const {
        if(uint64_t(a)+n>ram.size())return false;
        for(unsigned i=0;i<map.size();i+=8){auto base=word(map,i),extent=word(map,i+4);
            if(a>=base && uint64_t(a)+n<=uint64_t(base)+extent)return true;}return false;
    }
    unsigned reg(unsigned a)const{for(unsigned i=0;i<registers.size();i+=8)if(word(registers,i)==a)return word(registers,i+4);CHECK(false);return 0;}
    void reg(unsigned a,unsigned v){apu.mmio_write(0xfe800000+a,4,v);}
    void method(unsigned a,unsigned v){reg(0x20000+a,v);}
    unsigned physical(bool ep,unsigned at)const {
        const auto table=reg(ep?0xfe802048:0xfe802040),max=reg(ep?0xfe8020dc:0xfe8020d4);
        CHECK(at/4096<max && owned(table+(at/4096)*8,8));
        const auto a=get(table+(at/4096)*8)+(at&4095);CHECK(owned(a,4));return a;
    }
    void append(unsigned a,unsigned expected){CHECK(get(a)==expected);capture.insert(capture.end(),ram.begin()+a,ram.begin()+a+4);}
};
static void observe(Fixture& f,const char* capture) {
    CHECK(f.mode=="frames");std::string owner_error;
    xlle::ApuGpProcessor owner(f.apu,[&](const std::string& s){owner_error=s;});
    CHECK(owner.bootstrap_from_scratch());
    for(unsigned i=0;i<2048;++i)CHECK(*f.apu.gp_word(0x2800+i)==(f.get(f.physical(false,i*4))&0xffffff));
    CHECK(f.get(f.physical(false,0x810))==3);
    const unsigned code_words=f.get(f.physical(false,0x804));
    CHECK(code_words==1535);
    std::vector<unsigned> expected_program(f.apu.gp_word(0x2800),f.apu.gp_word(0x2800)+2048);
    for(unsigned n=0;n<code_words;++n)expected_program[0x171+n]=f.get(f.physical(false,0x818+n*4))&0xffffff;
    // Original P0086..0094: destination X0080 + packet[0], source
    // packet[2] in owned scratch, count packet[3]. Check before P0037 calls
    // the graph, which legitimately mutates its state thereafter.
    const unsigned parameter_offset=f.get(f.physical(false,0x800));
    const unsigned parameter_source=f.get(f.physical(false,0x808));
    const unsigned parameter_words=f.get(f.physical(false,0x80c));
    CHECK(parameter_offset==0 && parameter_source==0x818+code_words*4 && parameter_words==1712);
    std::vector<unsigned> expected_parameters;
    for(unsigned n=0;n<parameter_words;++n)expected_parameters.push_back(f.get(f.physical(false,parameter_source+n*4))&0xffffff);
    bool parameter_state_checked=false;
    xlle::ApuGpRun result;CHECK(owner.run(20000,result));
    CHECK(result.yielded && result.pc==0x2b && !result.sp && !result.power);
    CHECK(!f.apu.gp_dma_nodes() && !f.apu.gp_frame_outstanding() && f.ram==f.before);
    xlle::ApuVoiceQuantum q;CHECK(owner.start_frame(q));
    CHECK(q.published && q.sequence==1 && q.visited==1 && q.contributed==1);
    unsigned calls=0,last_pc=result.pc;
    do {
        if(!owner.run(1,result)) {
            std::printf("{\"status\":\"blocked_original_gp_publication\",\"last_pc\":%u,\"dma_nodes\":%llu,\"context_flags\":%u,\"owner_error\":\"%s\",\"device_error\":\"%s\",\"original_acknowledged\":false}\n",last_pc,(unsigned long long)f.apu.gp_dma_nodes(),f.get(f.physical(false,0x810)),owner_error.c_str(),f.error.c_str());
            return;
        }
        if(result.pc==0x37 && !parameter_state_checked) {
            CHECK(f.apu.gp_dma_nodes()==4 && *f.apu.gp_word(4)==0);
            CHECK(f.get(f.physical(false,0x810))==0);
            CHECK(std::equal(expected_parameters.begin(),expected_parameters.end(),f.apu.gp_word(0x80+parameter_offset)));
            CHECK(std::equal(expected_program.begin(),expected_program.end(),f.apu.gp_word(0x2800)));
            parameter_state_checked=true;
        }
        last_pc=result.pc;CHECK(++calls<400000 && !result.power);
    }while(!result.yielded);
    CHECK(parameter_state_checked);
    CHECK(result.pc==0x2b && !result.sp && !f.apu.gp_frame_outstanding());
    CHECK(f.get(f.physical(false,0x810))==0 && *f.apu.gp_word(4)==0);
    CHECK(owner_error.empty() && f.error.empty());
    CHECK(std::equal(expected_program.begin(),expected_program.end(),f.apu.gp_word(0x2800)));
    for(unsigned a=0;a<f.ram.size();++a)if(f.ram[a]!=f.before[a]) {
        bool recorded=false;for(const auto& w:f.written)recorded|=a>=w.first && a<w.first+w.second;
        CHECK(recorded);
    }
    const std::string writes_path=std::string(capture)+".writes.aot-private.bin";
    FILE* ranges=std::fopen(writes_path.c_str(),"wb");CHECK(ranges);
    for(const auto& w:f.written){unsigned fields[]={w.first,w.second};CHECK(std::fwrite(fields,4,2,ranges)==2);}CHECK(!std::fclose(ranges));
    FILE* file=std::fopen(capture,"wb");CHECK(file);
    CHECK(std::fwrite(f.ram.data(),1,f.ram.size(),file)==f.ram.size());CHECK(!std::fclose(file));
    std::printf("{\"status\":\"original_gp_publication_frame_continued\",\"final_pc\":43,\"dma_nodes\":%llu,\"context_flags\":0,\"real_vp_frames\":1,\"pre_effects_pc\":55,\"loaded_parameter_words\":1712,\"parameter_state_matches_original_source\":true,\"full_shell_qualified\":false,\"ep_started\":false}\n",(unsigned long long)f.apu.gp_dma_nodes());
}
int main(int argc,char** argv){CHECK(argc==9);Fixture f(argv);observe(f,argv[8]);}
