#include "machine/apu.hpp"
#include "audio_device_apu_contract.hpp"
#include <cstdio>
#include <cstring>
#include <string>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);return 1;}}while(0)
int main(int argc,char** argv) {
    std::string error;
    xlle::McpxApu apu([&](const std::string& why){error=why;});
    CHECK(apu.initialize_kernel_audio_handoff());
    if(argc==2 && !strcmp(argv[1],"--transport")) {
        char operation=0;unsigned address=0,width=0,value=0;
        puts("READY");fflush(stdout);
        while(scanf(" %c %x %u %x",&operation,&address,&width,&value)==4) {
            CHECK(address>=0xfe800000u && address<0xfe880000u && (operation=='R' || operation=='W'));
            if(!pilot_original_apu_admitted(address-0xfe800000u,width,operation=='W',value)) {
                printf("FAIL original transaction outside admitted semantics: %08x\n",address);fflush(stdout);return 2;
            }
            if(operation=='W')apu.mmio_write(address,int(width),value);
            if(!error.empty()){printf("FAIL %s\n",error.c_str());fflush(stdout);return 2;}
            value=apu.mmio_read(address,int(width));
            if(!error.empty()){printf("FAIL %s\n",error.c_str());fflush(stdout);return 2;}
            printf("OK %08x\n",value);fflush(stdout);
        }
        return 0;
    }
    CHECK(argc==1);
    for(uint32_t address:{0x2038u,0x203cu}) {
        const uint32_t physical=address==0x2038?0x00100000:0x00104000;
        apu.mmio_write(0xfe800000+address,4,physical);
        CHECK(apu.mmio_read(0xfe800000+address,4)==physical);
    }
    CHECK(apu.mmio_read(0xfe801100,4)==0x71f8f);
    apu.mmio_write(0xfe801100,4,0x71f0f);CHECK(apu.mmio_read(0xfe801100,4)==0x71f0f);
    apu.mmio_write(0xfe801100,4,0x71f8f);CHECK(apu.mmio_read(0xfe801100,4)==0x71f8f);
    apu.mmio_write(0xfe801100,4,0x88);
    CHECK(apu.mmio_read(0xfe801100,4)==0x70088);
    apu.mmio_write(0xfe802000,4,0);
    for(const auto& reg:xlle::ApuStartupConfiguration::registers) {
        apu.mmio_write(0xfe800000+reg.offset,4,reg.value);
        CHECK(apu.mmio_read(0xfe800000+reg.offset,4)==reg.value);
    }
    apu.mmio_write(0xfe801100,4,8);CHECK(apu.mmio_read(0xfe801100,4)==0x70008);
    apu.mmio_write(0xfe801100,4,0x100f);CHECK(apu.mmio_read(0xfe801100,4)==0x7100f);
    apu.mmio_write(0xfe801100,4,0x7100f);CHECK(apu.mmio_read(0xfe801100,4)==0x7100f);
    CHECK(error.empty());
    for(unsigned kind=0;kind<7;++kind) {
        std::string failure;xlle::McpxApu a([&](const std::string& why){failure=why;});
        CHECK(a.initialize_kernel_audio_handoff());
        if(kind==0)a.mmio_read(0xfe802038,4);
        if(kind==1)a.mmio_write(0xfe802038,4,0x101000);
        if(kind==2)a.mmio_write(0xfe802038,4,0x4000000);
        if(kind==3)a.mmio_write(0xfe802039,4,0x100000);
        if(kind==4)a.mmio_write(0xfe80203c,2,0x100000);
        if(kind>=5) {
            a.mmio_write(0xfe802038,4,0x100000);a.mmio_write(0xfe80200c,4,0);
            a.mmio_write(0xfe802000,4,15);
            if(kind==5)a.mmio_read(0xfe802038,4);
            else a.mmio_write(0xfe802038,4,0x104000);
        }
        CHECK(!failure.empty());
    }
    for(unsigned kind=0;kind<7;++kind) {
        std::string failure;xlle::McpxApu a([&](const std::string& why){failure=why;});
        CHECK(a.initialize_kernel_audio_handoff());
        if(kind==0){a.mmio_write(0xfe801100,4,0x4000);a.mmio_read(0xfe801100,4);} // unqualified control encoding
        else {
            a.mmio_write(0xfe801100,4,0x88);
            if(kind==1)a.mmio_read(0xfe801104,4); // no fabricated configuration
            if(kind==2)a.mmio_write(0xfe801104,4,0x100);
            if(kind==3)a.mmio_write(0xfe801103,4,0xff00);
            if(kind==4){a.mmio_write(0xfe801100,4,8);a.mmio_write(0xfe801104,4,0xff);}
            if(kind>=5){
                a.mmio_write(0xfe801104,4,0xff);a.mmio_write(0xfe80200c,4,0);
                a.mmio_write(0xfe802000,4,15);
                a.mmio_read(kind==5?0xfe801100:0xfe801104,4);
            }
        }
        CHECK(!failure.empty());
    }
    puts("PASS: stopped original configuration/mapping transport, physical FE control readback and explicit unknown/width/value/active guards");
    return 0;
}
