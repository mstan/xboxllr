// CPU PCI/UMA transport: direct bytes, whole extent ownership, no fabricated
// register readback or tiled-client semantics. Host byte order is immaterial.
#include "machine/nv2a.hpp"
#include <array>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
static int failures;
#define CHECK(c) do{if(!(c)){std::printf("FAIL line%d: %s\n",__LINE__,#c);++failures;}}while(0)
int main() {
    std::string error;
    auto gpu=std::make_unique<xlle::Nv2a>([&](const std::string& why){error=why;});
    std::array<uint8_t,32> ram;ram.fill(0xcc);
    bool owned=true,writable=true;unsigned reads=0,writes=0;
    gpu->set_mem_access([&](uint32_t a,void* p,size_t n) {
        ++reads;if(!owned || a<0x1000 || uint64_t(a)+n>0x1020)return false;
        std::memcpy(p,ram.data()+a-0x1000,n);return true;
    },[&](uint32_t a,const void* p,size_t n) {
        ++writes;if(!owned || !writable || a<0x1000 || uint64_t(a)+n>0x1020)return false;
        std::memcpy(ram.data()+a-0x1000,p,n);return true;
    });
    uint8_t input[]={0x12,0x34,0x56,0x78,0x9a,0xbc,0xde,0xf0};
    for(size_t n:{1u,2u,4u,8u}) {
        CHECK(gpu->cpu_aperture(0x1007,input,n,true));
        uint8_t out[10];std::memset(out,0xa5,sizeof(out));
        CHECK(gpu->cpu_aperture(0x1007,out+1,n,false));
        CHECK(!std::memcmp(out+1,input,n) && out[0]==0xa5 && out[n+1]==0xa5);
        CHECK(ram[6]==0xcc && ram[15]==0xcc);
    }
    const auto saved=ram;
    writable=false;CHECK(!gpu->cpu_aperture(0x1007,input,8,true) && ram==saved);writable=true;
    CHECK(!gpu->cpu_aperture(0x101c,input,8,true) && ram==saved);
    owned=false;uint8_t out[8];std::memset(out,0xa5,8);
    CHECK(!gpu->cpu_aperture(0x1000,out,8,false) && out[0]==0xa5 && out[7]==0xa5);
    owned=true;unsigned before=reads+writes;
    CHECK(!gpu->cpu_aperture(0x03fffffc,input,8,true));
    CHECK(!gpu->cpu_aperture(0xfffffffcu,input,8,true));
    CHECK(!gpu->cpu_aperture(0x1000,input,3,true));
    CHECK(reads+writes==before && ram==saved);
    // An incomplete active PFB bank must also reject overlapping CPU accesses,
    // even if the PGRAPH/texture banks have not yet been programmed.
    gpu->mmio_write(0xfd100244,4,0x1fff);
    gpu->mmio_write(0xfd100248,4,0x200);
    gpu->mmio_write(0xfd100240,4,1);
    before=reads+writes;
    CHECK(!gpu->cpu_aperture(0x1000,input,8,true));
    CHECK(error=="NV2A CPU aperture overlaps unqualified tiled storage");
    CHECK(reads+writes==before && ram==saved);
    gpu->mmio_write(0xfd100240,4,0x300001);
    CHECK(!gpu->cpu_aperture(0x1000,input,8,true));
    CHECK(error=="NV2A CPU aperture has an invalid active tile extent");
    CHECK(reads+writes==before && ram==saved);
    return failures?1:0;
}
