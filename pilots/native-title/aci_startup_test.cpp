#include "machine/aci.hpp"
#include "audio_device_aci_contract.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#define CHECK(c) do { if(!(c)){std::fprintf(stderr,"ACI startup failure %d: %s\n",__LINE__,#c);std::exit(1);} } while(0)
int main(int argc,char** argv) {
    constexpr uint32_t base=xlle::McpxAci::kBase;
    std::string error;xlle::McpxAci aci([&](const std::string& why){error=why;});
    CHECK(aci.mmio_read(base+0x12c,4)==0 && aci.mmio_read(base+0x130,4)==0);
    CHECK(aci.initialize_kernel_audio_handoff());aci.set_time(0,1125000);
    const bool streams=argc==2 && !std::strcmp(argv[1],"--stream-transport");
    if(argc==2 && (!std::strcmp(argv[1],"--transport") || streams)) {
        if(streams) {
            // Candidate device proof only. Native title admission remains
            // guarded until its digital consumer and ownership are connected.
            aci.set_mem_access([](uint32_t a,void* out,size_t n){
                if(!n || n>4096 || uint64_t(a)+n>0x4000000)return false;
                std::printf("MEMR %08x %zu\n",a,n);std::fflush(stdout);
                char data[8193]={};if(std::scanf(" DATA %8192s",data)!=1 || std::strlen(data)!=n*2)return false;
                auto nibble=[](char c){return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:-1;};
                for(size_t i=0;i<n;++i){int hi=nibble(data[i*2]),lo=nibble(data[i*2+1]);
                    if(hi<0 || lo<0)return false;
                    static_cast<uint8_t*>(out)[i]=uint8_t(hi*16+lo);}
                return true;
            });
            auto output=[](char stream,const uint8_t* p,size_t n,uint32_t format){
                std::printf("OUTPUT %c %08x ",stream,format);
                for(size_t i=0;i<n;++i)std::printf("%02x",p[i]);
                std::puts("");std::fflush(stdout);
                char reply[16]={};return std::scanf(" %15s",reply)==1 && !std::strcmp(reply,"ACCEPTED");
            };
            aci.set_pcm_output([output](const uint8_t* p,size_t n){return output('P',p,n,0);});
            aci.set_spdif_output([output](const uint8_t* p,size_t n,uint32_t format){return output('S',p,n,format);});
        }
        char operation;unsigned address,width,value;std::puts("READY");std::fflush(stdout);
        while(std::scanf(" %c %x %u %x",&operation,&address,&width,&value)==4) {
            if(operation=='T') {aci.set_time(address,1125000);if(streams)aci.set_frame(address/6000);value=0;}
            else {
                CHECK(operation=='R' || operation=='W');
                CHECK(address>=base && address<base+aci.kSize);
                const unsigned off=address-base;
                const bool candidate=streams &&
                    (((off==0x116 || off==0x176) && width==1 && operation=='R') ||
                     (off==0x17b && width==1 && operation=='W' && value==0x1d));
                if(!candidate && !pilot_original_aci_admitted(off,width,operation=='W',value)) {
                    std::printf("FAIL admission %08x\n",address);std::fflush(stdout);return 2;
                }
                if(operation=='W')aci.mmio_write(address,width,value);
                else value=aci.mmio_read(address,width);
            }
            if(!error.empty()){std::printf("FAIL %s\n",error.c_str());std::fflush(stdout);return 2;}
            // Writes echo the submitted value. An extra NAM read would release
            // CAS and conceal the original driver's asynchronous wait.
            std::printf("OK %08x\n",value);std::fflush(stdout);
        }
        return 0;
    }
    CHECK(argc==1);
    for(unsigned life=0;life<3;++life) {
        std::string fault;xlle::McpxAci a([&](const std::string& why){fault=why;});
        CHECK(a.initialize_kernel_audio_handoff());a.set_time(0,1125000);
        unsigned dma=0;a.set_mem_access([&](uint32_t,void*,size_t){++dma;return false;});
        auto read=[&](unsigned off,unsigned size){return a.mmio_read(base+off,size);};
        auto write=[&](unsigned off,unsigned size,unsigned v){a.mmio_write(base+off,size,v);};
        CHECK(read(0x12c,4)==2 && read(0x130,4)==0x300100);
        CHECK(read(0x7c,2)==0x574d && read(0x7e,2)==0x4c09);
        uint64_t ticks=0;
        for(const auto& entry:{std::pair<unsigned,unsigned>{0x26,0},{2,0},{0x18,0x0808},{0x2c,48000},{0x2c,32000}}) {
            CHECK(read(0x134,1)==0 && read(0x134,1)==1);
            write(entry.first,2,entry.second);
            for(unsigned i=0;i<100;++i){a.set_time(ticks,1125000);CHECK(read(0x134,1)==1);}
            ticks+=24;a.set_time(ticks,1125000);CHECK(read(0x134,1)==0 && read(0x134,1)==1);
            CHECK(read(entry.first,2)==0); // WM9709 ignores ordinary mixer/rate writes.
        }
        for(unsigned stream:{0x110u,0x170u}) {
            write(stream,4,0x100000+life*0x10000+stream*16);
            write(stream+11,1,2);
            CHECK(read(stream,4)==0 && read(stream+11,1)==0 && read(stream+6,2)==1);
            CHECK(!read(stream+4,1) && !read(stream+5,1) && !read(stream+8,2) && !read(stream+10,1));
            write(stream,4,0x200000+life*0x10000+stream*16);
            write(stream+4,1,0);write(stream+5,1,0);
        }
        for(unsigned format:{0x02000000u,0x02000002u}){write(0x17c,4,format);CHECK(read(0x17c,4)==format);}
        a.set_frame(100);CHECK(fault.empty() && !dma && !a.consumed_pcm_words());
    }
    for(unsigned kind=0;kind<8;++kind) {
        std::string fault;xlle::McpxAci a([&](const std::string& why){fault=why;});
        if(kind!=0){CHECK(a.initialize_kernel_audio_handoff());a.set_time(0,1125000);}
        if(kind==0)a.mmio_write(base+0x11b,1,1);
        if(kind==1)a.mmio_write(base+0x12c,4,0);
        if(kind==2)a.mmio_write(base+0x5a,2,1);
        if(kind==3){a.mmio_write(base+0x18,2,0x0808);a.mmio_write(base+2,2,0);}
        if(kind==4)a.mmio_write(base+0x17b,1,1);
        if(kind==5)a.mmio_write(base+0x174,1,3);
        if(kind==6){a.set_time(10,1125000);a.set_time(9,1125000);}
        if(kind==7)a.initialize_kernel_audio_handoff();
        CHECK(!fault.empty() && !a.consumed_pcm_words());
    }
    CHECK(!pilot_original_aci_admitted(0x12c,4,true,2));
    CHECK(!pilot_original_aci_admitted(0x134,4,false,0));
    CHECK(!pilot_original_aci_admitted(0x17b,1,true,0x1d));
    CHECK(!pilot_original_aci_admitted(0x18,2,true,0));
    std::puts("PASS WM9709 startup, clock-owned CAS, two stopped rings and explicit unsupported-context guards");
}
