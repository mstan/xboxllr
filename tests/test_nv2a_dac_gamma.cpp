// Full RGB upload through the real MMIO owner, then independently computed
// scanout expectations. No framebuffer writes may be caused by gamma changes.
#include "machine/nv2a.hpp"
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

static int failures;
#define CHECK(c) do { if(!(c)){std::printf("FAIL line %d: %s\n",__LINE__,#c);++failures;} } while(0)

int main() {
    for(unsigned life=0;life<2;++life) {
        std::string fault,error;
        auto gpu=std::make_unique<xlle::Nv2a>([&](const std::string& why){fault=why;});
        std::vector<uint8_t> ram(8192),rgba;
        unsigned reads=0;
        gpu->set_mem_access([&](uint32_t a,void* out,size_t n) {
            ++reads;if(uint64_t(a)+n>ram.size())return false;
            std::memcpy(out,ram.data()+a,n);return true;
        },{},[&](uint32_t a,size_t n)->uint8_t* {
            return uint64_t(a)+n<=ram.size()?ram.data()+a:nullptr;
        });
        auto write=[&](uint32_t a,unsigned size,uint32_t v){gpu->mmio_write(a,size,v);};
        auto crtc=[&](uint32_t i,uint32_t v){write(0xfd6013d4,1,i);write(0xfd6013d5,1,v);};
        crtc(0x13,128);crtc(0x28,3);
        write(0xfd680800,4,0);write(0xfd680820,4,255);write(0xfd600800,4,4096);
        write(0xfd680600,4,0x100030);
        for(unsigned i=0;i<256;++i) {
            ram[4096+4*i]=uint8_t(i);ram[4096+4*i+1]=uint8_t(i+79);
            ram[4096+4*i+2]=uint8_t(255-i);ram[4096+4*i+3]=uint8_t(i^life);
        }
        const auto original=ram;
        xlle::Nv2a::ScanoutInfo info;size_t bytes=0;
        CHECK(gpu->scanout_direct_bytes(info,bytes,error)==ram.data()+4096 && bytes==1024);
        write(0xfd6813c9,1,42);CHECK(!fault.empty());fault.clear();
        write(0xfd6813c8,1,0);
        write(0xfd6813c9,1,255); // red of first entry
        write(0xfd6813c8,1,7);CHECK(!fault.empty());fault.clear();
        write(0xfd6813c9,2,0);CHECK(!fault.empty());fault.clear();
        write(0xfd6813c9,1,256);CHECK(!fault.empty());fault.clear();
        write(0xfd6813c9,1,0xa5);write(0xfd6813c9,1,0);
        for(unsigned i=1;i<256;++i) {
            write(0xfd6813c9,1,255-i);write(0xfd6813c9,1,i^0xa5);
            if(i!=255)write(0xfd6813c9,1,(i*17)&255);
        }
        // No pixels or guest-memory reads may escape from an incomplete table.
        rgba={1,2,3,4};const unsigned before=reads;
        CHECK(!gpu->capture_scanout(rgba,info,error));
        CHECK(!gpu->scanout_direct_bytes(info,bytes,error) && bytes==0);
        CHECK(reads==before && rgba==std::vector<uint8_t>({1,2,3,4}));
        write(0xfd6813c9,1,(255*17)&255);
        CHECK(fault.empty());CHECK(gpu->capture_scanout(rgba,info,error));
        CHECK(rgba.size()==1024);
        if(rgba.size()==1024)for(unsigned i=0;i<256;++i) {
            CHECK(rgba[4*i]==i);
            CHECK(rgba[4*i+1]==(uint8_t(i+79)^0xa5));
            CHECK(rgba[4*i+2]==uint8_t(i*17));CHECK(rgba[4*i+3]==255);
        }
        CHECK(!gpu->scanout_direct_bytes(info,bytes,error) && bytes==0);
        CHECK(ram==original);
        // The auto-incremented index wraps; the next complete RGB sweep can
        // restore identity without an index write. Both output routes agree.
        for(unsigned i=0;i<256;++i)for(unsigned c=0;c<3;++c)write(0xfd6813c9,1,i);
        CHECK(gpu->scanout_direct_bytes(info,bytes,error)==ram.data()+4096 && bytes==1024);
        CHECK(gpu->capture_scanout(rgba,info,error));
        if(rgba.size()==1024)for(unsigned i=0;i<256;++i) {
            CHECK(rgba[4*i]==255-i && rgba[4*i+1]==uint8_t(i+79));
            CHECK(rgba[4*i+2]==i && rgba[4*i+3]==255);
        }
        // Later mode changes cannot silently reinterpret an established DAC.
        write(0xfd680600,4,0x30);
        write(0xfd6813c8,1,0);CHECK(!fault.empty());fault.clear();
        CHECK(!gpu->capture_scanout(rgba,info,error));
        CHECK(!gpu->scanout_direct_bytes(info,bytes,error) && bytes==0);
        write(0xfd680600,4,0x100030);
        CHECK(gpu->scanout_direct_bytes(info,bytes,error)==ram.data()+4096);
        crtc(0x28,2);CHECK(!gpu->capture_scanout(rgba,info,error));crtc(0x28,3);
        // Re-entry resets component phase, and hardware blanking remains black
        // even when a newly started upload prevents active picture processing.
        write(0xfd6813c8,1,0);write(0xfd6813c9,1,17);write(0xfd6813c8,1,0);
        write(0xfd0c03c4,1,1);write(0xfd0c03c5,1,0x21);
        CHECK(gpu->capture_scanout(rgba,info,error));
        for(unsigned i=0;i<rgba.size();++i)CHECK(rgba[i]==(i%4==3?255:0));
        write(0xfd0c03c5,1,1);CHECK(!gpu->capture_scanout(rgba,info,error));
        for(unsigned i=0;i<256;++i)for(unsigned c=0;c<3;++c)write(0xfd6813c9,1,i);
        CHECK(gpu->scanout_direct_bytes(info,bytes,error)==ram.data()+4096);
        gpu->mmio_read(0xfd6813c8,1);CHECK(!fault.empty());
        CHECK(ram==original);
    }
    std::printf("DAC gamma: %d failures\n",failures);return failures?1:0;
}
