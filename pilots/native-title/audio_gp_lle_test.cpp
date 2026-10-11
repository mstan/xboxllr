// Real original DSP graph on the reused MIT CPU core, using maintained GP banks
// and physical SGE DMA. Compare native graph outputs and actual scratch bytes.
// This qualifies the graph/transport boundary, not the missing retail GP shell.
#include <windows.h>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include "machine/apu.hpp"
#include "audio_dsp.h"
#include "machine/apu_dsp_core.h"
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"GP LLE FAIL %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)

static std::vector<uint8_t> file(const char* path) {
    FILE* f=std::fopen(path,"rb");CHECK(f);CHECK(!std::fseek(f,0,SEEK_END));const long size=std::ftell(f);
    CHECK(size>0 && size<1024*1024);std::rewind(f);std::vector<uint8_t> data(size);
    CHECK(std::fread(data.data(),1,data.size(),f)==data.size());std::fclose(f);return data;
}
struct Floor {
    std::vector<uint8_t> ram;
    unsigned pages;
    std::string error;
    xlle::McpxApu apu{[this](const std::string& e){error=e;std::fprintf(stderr,"%s\n",e.c_str());}};
    XlleDspCore* core=nullptr;
    explicit Floor(unsigned scratch,const uint8_t* storage):pages((scratch+4095)/4096) {
        CHECK(pages<=512);ram.resize(0x10000+pages*4096);
        apu.set_mem_access(
            [this](uint32_t a,void* p,size_t n){if(uint64_t(a)+n>ram.size())return false;std::memcpy(p,ram.data()+a,n);return true;},
            [this](uint32_t a,const void* p,size_t n){if(uint64_t(a)+n>ram.size())return false;std::memcpy(ram.data()+a,p,n);return true;});
        for(unsigned i=0;i<pages;++i) {
            const uint32_t physical=0x10000+(pages-1-i)*4096;
            std::memcpy(ram.data()+0x1000+i*8,&physical,4);
        }
        apu.mmio_write(0xfe802040,4,0x1000);apu.mmio_write(0xfe8020d4,4,pages-1);
        for(unsigned i=0;i<scratch;++i)byte(i)=storage[0xa000+i];
        for(unsigned i=0;i<4096;++i) {
            uint32_t p,x;std::memcpy(&p,storage+i*4,4);std::memcpy(&x,storage+0x4000+i*4,4);
            apu.mmio_write(0xfe83a000+i*4,4,p);apu.mmio_write(0xfe830000+i*4,4,x);
        }
        for(unsigned i=0;i<2048;++i) {
            uint32_t y;std::memcpy(&y,storage+0x8000+i*4,4);apu.mmio_write(0xfe836000+i*4,4,y);
        }
        core=xlle_dsp_core_create(apu.gp_word(0x2800),apu.gp_word(0),apu.gp_word(0x1800),apu.gp_word(0x1400),
            this,read,write,fault);CHECK(core && error.empty());
    }
    ~Floor(){xlle_dsp_core_destroy(core);}
    uint8_t& byte(unsigned a){return ram[0x10000+(pages-1-(a>>12))*4096+(a&4095)];}
    static uint32_t read(void* opaque,uint32_t address) {
        auto& f=*static_cast<Floor*>(opaque);uint32_t value=0;CHECK(f.apu.gp_read(address,value));return value;
    }
    static void write(void* opaque,uint32_t address,uint32_t value) {
        auto& f=*static_cast<Floor*>(opaque);
        uint32_t next=0;CHECK(f.apu.gp_read(0xffffd4,next));
        if(!f.apu.gp_write(address,value)) {
            std::fprintf(stderr,"GP write %06x=%06x, node %06x:",address,value,next);
            for(unsigned i=0;i<7;++i)if(auto* word=f.apu.gp_word(next+i))std::fprintf(stderr," %06x",*word);
            std::fprintf(stderr,"\n");
        }
        CHECK(f.error.empty());
    }
    static void fault(void*,const char* message,uint32_t address) {
        std::fprintf(stderr,"%s: %06x\n",message,address);std::exit(2);
    }
    void run(uint32_t cycles) {
        // This bounded graph fixture owns explicit device work just like the
        // maintained processor. Passive peripheral reads cannot finish DMA.
        CHECK(xlle_dsp_core_add_budget(core,cycles));
        for(;;) {
            uint32_t pc,power,sp;xlle_dsp_core_result(core,&pc,&power,&sp);
            if(power || xlle_dsp_core_budget(core)<=0)break;
            CHECK(xlle_dsp_core_step_budget(core));
            CHECK(apu.service_dsp_dma(xlle::ApuDspKind::Gp));
        }
        // STOP halts the CPU, not the separately owned DMA engine. Finish only
        // real admitted work, with a finite bound; never write idle/status.
        unsigned quanta=0;
        while(apu.dsp_dma_pending(xlle::ApuDspKind::Gp)) {
            CHECK(++quanta<=4096);
            CHECK(apu.service_dsp_dma(xlle::ApuDspKind::Gp));
        }
    }
    void process(const uint32_t* mix,uint32_t* output) {
        // The existing bounded GP graph shell seeds direct bins, then executes
        // real effects. Original boot/framework/output-FIFO work stays open.
        for(unsigned i=0;i<1024;++i)*apu.gp_word(0x1400+i)=mix[i]&0xffffff;
        for(unsigned i=0;i<192;++i)*apu.gp_word(0xc00+i)=mix[i]&0xffffff;
        xlle_dsp_core_begin(core,0);run(1000000);
        uint32_t pc,power,sp;xlle_dsp_core_result(core,&pc,&power,&sp);
        CHECK(pc==2 && power==2 && !sp && error.empty());
        for(unsigned i=0;i<192;++i)output[i]=*apu.gp_word(0xc00+i);
    }
};

static void authored_dma_program() {
    for(bool fifo:{false,true})for(unsigned format:{1u,2u,6u}) {
        std::vector<uint8_t> storage(0xc000);
        // Independently authored program: real MOVEs set NEXT and START, then
        // STOP. The CPU executes the peripheral writes; no callback is skipped.
        const uint32_t program[]={0x65f400,0xffffd0,0x44f400,0x20,0x021584,
                                  0x65f400,0xffffd2,0x44f400,1,0x021584,0x87};
        std::memcpy(storage.data(),program,sizeof(program));
        Floor floor(8192,storage.data());
        const uint32_t control=0x4000|(format<<10)|16;
        const uint32_t nodes[]={0x40,control|((fifo?1:14)<<5)|2,2,0x80,0,4093,7};
        const uint32_t input[]={0x4000,control|((fifo?0:14)<<5),2,0x1880,0,4093,7};
        for(unsigned i=0;i<7;++i){*floor.apu.gp_word(0x20+i)=nodes[i];*floor.apu.gp_word(0x40+i)=input[i];}
        if(fifo) {
            floor.apu.mmio_write(0xfe802044,4,0x1000);floor.apu.mmio_write(0xfe8020d8,4,1);
            for(unsigned reg:{0xfe803034u,0xfe803064u}) {
                floor.apu.mmio_write(reg,4,0x100);floor.apu.mmio_write(reg+4,4,4096);
                floor.apu.mmio_write(reg+8,4,4093);
            }
        }
        *floor.apu.gp_word(0x80)=0x123456;*floor.apu.gp_word(0x81)=0xabcdef;
        xlle_dsp_core_begin(floor.core,0);floor.run(10000);
        uint32_t pc,power,sp;xlle_dsp_core_result(floor.core,&pc,&power,&sp);
        CHECK(pc==11 && power==2 && !sp && floor.error.empty());
        CHECK(floor.apu.gp_dma_nodes()==2);
        CHECK(*floor.apu.gp_word(0x1880)==(format==1?0x123400:0x123456));
        CHECK(*floor.apu.gp_word(0x1881)==(format==1?0xabcd00:0xabcdef));
        const uint8_t pcm16[]={0x34,0x12,0xcd,0xab};
        const uint8_t msb[]={0,0x56,0x34,0x12,0,0xef,0xcd,0xab};
        const uint8_t lsb[]={0x56,0x34,0x12,0,0xef,0xcd,0xab,0};
        const unsigned length=format==1?4:8;
        for(unsigned i=0;i<length;++i) {
            const unsigned offset=fifo && i>=3?0x100+i-3:4093+i;
            CHECK(floor.byte(offset)==(format==1?pcm16:format==2?msb:lsb)[i]);
        }
        if(fifo)for(unsigned reg:{0xfe80303cu,0xfe80306cu})CHECK(floor.apu.mmio_read(reg,4)==0x100+length-3);
        uint32_t status;CHECK(floor.apu.gp_read(0xffffc5,status) && status==0x80);
    }
    std::puts("PASS: authored DSP program performs real GP scratch/FIFO output/input in PCM16 and both PCM24 formats");
}

int main(int argc,char** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    if(argc==1){authored_dma_program();return 0;}
    if(argc!=4){std::fprintf(stderr,"usage: pilot_gp_lle_test image seed_hex original-decoded-image\n");return 2;}
    const auto raw=file(argv[1]),oracle=file(argv[3]);const char* error=nullptr;
    PilotDspImage* image=pilot_dsp_decode(raw.data(),unsigned(raw.size()),_strtoui64(argv[2],nullptr,16),&error);
    CHECK(image && !error && oracle.size()==raw.size());
    std::vector<uint8_t> native(pilot_dsp_storage_bytes(image));
    PilotDsp* dsp=pilot_dsp_create(image,native.data(),0x20000000);CHECK(dsp);
    uint32_t program_words;std::memcpy(&program_words,oracle.data()+0x804,4);
    CHECK(!std::memcmp(native.data()+0x1000,oracle.data()+0x818,program_words*4));
    const unsigned scratch=pilot_dsp_descriptor_offset(image)-0xa000;
    Floor floor(scratch,native.data());
    std::array<uint32_t,1024> input{};std::array<uint32_t,192> actual{},expected{};
    unsigned nonzero=0;
    for(unsigned frame=0;frame<1501;++frame) {
        if(frame==0)for(unsigned channel=0;channel<32;++channel)input[channel*32]=0x100000;
        else input.fill(0);
        pilot_dsp_process(dsp,input.data(),expected.data());floor.process(input.data(),actual.data());
        CHECK(actual==expected);for(auto v:actual)nonzero+=v!=0;
    }
    CHECK(nonzero && floor.apu.gp_dma_nodes()==pilot_dsp_dma_nodes(dsp) && floor.apu.gp_dma_nodes()>1000);
    unsigned changed=0;
    for(unsigned i=0;i<scratch;++i){CHECK(floor.byte(i)==native[0xa000+i]);changed+=floor.byte(i)!=0;}
    CHECK(changed);
    std::printf("PASS: actual original GP graph, 1501 blocks, %llu real DMA nodes, %u nonzero output words, %u retained scratch bytes; native/LLE output and physical SGE bytes match\n",
        floor.apu.gp_dma_nodes(),nonzero,changed);
    pilot_dsp_destroy(dsp);pilot_dsp_image_destroy(image);
}
