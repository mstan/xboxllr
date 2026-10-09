// Strict C transport for the maintained APU configuration/source floor. No register
// defaults, sample progress or completion are substituted by this adapter.
#include "machine/apu.hpp"
#include <cstring>
#include <new>
#include <string>
#ifdef _WIN32
#define EXPORT extern "C" __declspec(dllexport)
#else
#define EXPORT extern "C"
#endif
namespace {
struct Reference {
    std::string failure;
    xlle::McpxApu apu;
    Reference(void* memory, uint32_t bytes)
        : apu([this](const std::string& why) { failure=why; }) {
        apu.set_mem_access(
            [memory,bytes](uint32_t address,void* dst,size_t n) {
                if(uint64_t(address)+n>bytes)return false;
                std::memcpy(dst,static_cast<uint8_t*>(memory)+address,n);return true;
            },
            [memory,bytes](uint32_t address,const void* src,size_t n) {
                if(uint64_t(address)+n>bytes)return false;
                std::memcpy(static_cast<uint8_t*>(memory)+address,src,n);return true;
            });
    }
};
bool qualified_write(uint32_t offset) {
    switch(offset) {
    case 0x202c:case 0x2030: // voice table and source scatter/gather physical addresses
    case 0x202f8:case 0x202fc:case 0x20140:
    case 0x20300:case 0x20304:case 0x20308:case 0x2030c:
    case 0x20310:case 0x20314:case 0x20318:
    case 0x20360:case 0x20364:case 0x20368:case 0x2036c:
    case 0x20374:case 0x20378:case 0x2037c:
    case 0x203a0:case 0x203a4:case 0x203d8:case 0x203dc:return true;
    default:return false;
    }
}
}
EXPORT void* xlle_apu_reference_create(void* ram,uint32_t bytes) {
    if(!ram || !bytes || bytes>64u*1024*1024)return nullptr;
    return new(std::nothrow) Reference(ram,bytes);
}
EXPORT void xlle_apu_reference_destroy(void* instance) {delete static_cast<Reference*>(instance);}
EXPORT const char* xlle_apu_reference_error(void* instance) {
    return instance?static_cast<Reference*>(instance)->failure.c_str():"APU reference missing instance";
}
EXPORT int xlle_apu_reference_write(void* instance,uint32_t address,int size,uint32_t value) {
    if(!instance)return 0;
    auto& ref=*static_cast<Reference*>(instance);
    if(!ref.failure.empty())return 0;
    if(address<xlle::McpxApu::kBase || size!=4 || !qualified_write(address-xlle::McpxApu::kBase)) {
        ref.failure="APU reference outside qualified configuration transaction";return 0;
    }
    ref.apu.mmio_write(address,size,value);return ref.failure.empty();
}
EXPORT int xlle_apu_reference_source(void* instance,uint32_t voice,float* samples,uint32_t capacity,
                                    uint32_t* count,int* ended) {
    if(!instance)return 0;
    auto& ref=*static_cast<Reference*>(instance);
    if(!ref.failure.empty())return 0;
    if(voice>0xffff || !count || !ended || capacity>65536) {
        ref.failure="invalid APU source transport shape";return 0;
    }
    bool last=false;
    const bool ok=ref.apu.source_frames(static_cast<uint16_t>(voice),samples,capacity,*count,last);
    *ended=last;return ok;
}
