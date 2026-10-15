#include "machine/apu.hpp"
#include "audio_device_apu_contract.hpp"
#include <cstdio>
#include <cstring>
#include <string>
#include <fstream>
#include <iterator>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);return 1;}}while(0)
static int startup_methods() {
    for(unsigned kind=0;kind<15;++kind) {
        std::string failure;std::vector<uint8_t> ram(65536,0xa5);unsigned writes=0;
        const uint32_t base=0x10000+(kind%3)*0x10000;
        xlle::McpxApu a([&](const std::string& why){failure=why;});
        a.set_mem_access({},[&](uint32_t address,const void* bytes,size_t n){
            ++writes;if(kind==10 || address<base || uint64_t(address-base)+n>ram.size())return false;
            std::memcpy(ram.data()+address-base,bytes,n);return true;
        });
        auto write=[&](uint32_t off,uint32_t value){a.mmio_write(0xfe800000+off,4,value);};
        auto read=[&](uint32_t off){return a.mmio_read(0xfe800000+off,4);};
        CHECK(a.initialize_kernel_audio_handoff());
        write(0x1138,0x7ff);write(0x1148,1);write(0x2030,base);write(0x2044,base+0x4000);
        write(0x1510,0);write(0x1100,0x100f);CHECK(failure.empty());
        if(kind<3) {
            const uint32_t expected[][2]={{0x2a4,0x207c},{0x2b0,0x2080},{0x2a0,0x2078},{0x2a8,0x2088},{0x2ac,0x2084}};
            for(const auto& field:expected){write(0x20000+field[0],0xfff);CHECK(read(field[1])==0xfff);}
            write(0x202c0,0x09070806);CHECK(read(0x20ac)==0x09070806);
            write(0x20804,0x7ff);write(0x20808,base+0xe000);
            write(0x21800,1);write(0x21808,base+0xf000);
            CHECK(failure.empty() && writes==2 && read(0x1300)==0x1808 && read(0x20010)==0x80);
            for(unsigned i=0;i<ram.size();++i) {
                uint8_t expected_byte=0xa5;
                if(i>=0x3ff8 && i<0x3ffc)expected_byte=uint8_t((base+0xe000)>>((i-0x3ff8)*8));
                if(i>=0x4008 && i<0x400c)expected_byte=uint8_t((base+0xf000)>>((i-0x4008)*8));
                CHECK(ram[i]==expected_byte);
            }
        }else {
            if(kind==3){write(0x1510,1);write(0x20804,3);}
            if(kind==4)write(0x202a4,0);
            if(kind==5)write(0x202c0,0x20);
            if(kind==6)write(0x20808,base+0xe000);
            if(kind==7)write(0x20804,0x800);
            if(kind==8)a.mmio_write(0xfe8202a4,2,0xfff);
            if(kind==9){write(0x200c,0);write(0x2000,15);write(0x202a4,0xfff);}
            if(kind==10){write(0x20804,3);write(0x20808,base+0xe000);}
            if(kind==11)write(0x21808,base+0xf000);
            if(kind==12){write(0x2030,base+0x1000);write(0x20804,3);write(0x20808,base+0xe000);}
            if(kind==13){write(0x20804,3);write(0x20808,base+0xe001);}
            if(kind==14){write(0x1510,1);write(0x202a4,0xfff);}
            CHECK(!failure.empty() && writes==(kind==10?1u:0u));
            for(uint8_t byte:ram)CHECK(byte==0xa5);
        }
    }
    return 0;
}
int main(int argc,char** argv) {
    std::string error;
    xlle::McpxApu apu([&](const std::string& why){error=why;});
    CHECK(apu.initialize_kernel_audio_handoff());
    if((argc==2 && !strcmp(argv[1],"--transport")) || (argc==5 && !strcmp(argv[1],"--transport-ram"))) {
        std::vector<uint8_t> ram;std::vector<std::array<uint32_t,2>> owners;
        if(argc==5) {
            std::ifstream data(argv[2],std::ios::binary);CHECK(data.good());
            ram.assign(std::istreambuf_iterator<char>(data),{});CHECK(!ram.empty() && ram.size()<=0x4000000);
            std::ifstream ownership(argv[3],std::ios::binary);CHECK(ownership.good());
            std::array<uint32_t,2> range;
            while(ownership.read(reinterpret_cast<char*>(range.data()),sizeof(range)))owners.push_back(range);
            CHECK(ownership.eof() && ownership.gcount()==0 && !owners.empty());
            apu.set_mem_access({},[&](uint32_t address,const void* bytes,size_t n){
                if(uint64_t(address)+n>ram.size())return false;
                for(const auto& owner:owners)if(address>=owner[0] && uint64_t(address)+n<=uint64_t(owner[0])+owner[1]) {
                    std::memcpy(ram.data()+address,bytes,n);return true;
                }
                return false;
            });
        }
        char operation=0;unsigned address=0,width=0,value=0;
        puts("READY");fflush(stdout);
        while(scanf(" %c %x %u %x",&operation,&address,&width,&value)==4) {
            CHECK(address>=0xfe800000u && address<0xfe880000u && (operation=='R' || operation=='W'));
            if(!pilot_original_apu_admitted(address-0xfe800000u,width,operation=='W',value)) {
                printf("FAIL original transaction outside admitted semantics: %08x\n",address);fflush(stdout);return 2;
            }
            if(operation=='W')apu.mmio_write(address,int(width),value);
            if(!error.empty()){printf("FAIL %s\n",error.c_str());fflush(stdout);return 2;}
            // Method ports have no qualified guest readback. The RAM transport
            // echoes their submitted argument; RAM/result registers prove effects.
            if(!(argc==5 && operation=='W' && address>=0xfe820000u))value=apu.mmio_read(address,int(width));
            if(!error.empty()){printf("FAIL %s\n",error.c_str());fflush(stdout);return 2;}
            printf("OK %08x\n",value);fflush(stdout);
        }
        if(argc==5){std::ofstream data(argv[4],std::ios::binary);data.write(reinterpret_cast<const char*>(ram.data()),ram.size());CHECK(data.good());}
        return 0;
    }
    CHECK(argc==1);
    CHECK(startup_methods()==0);
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
