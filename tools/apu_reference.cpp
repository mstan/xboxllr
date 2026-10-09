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
    if(offset>=0x20200 && offset<=0x2027c && !(offset&3))return true;
    switch(offset) {
    case 0x202c:case 0x2030: // voice table and source scatter/gather physical addresses
    case 0x115c:case 0x1000:case 0x1004: // notifier table and real interrupt ownership
    case 0x1100:case 0x1504:case 0x2000: // FE trap ownership and setup frame enable
    case 0x2054:case 0x2060:case 0x206c: // actual list heads
    case 0x2058:case 0x205c:case 0x2064:case 0x2068:case 0x2070:case 0x2074:
    case 0x20120:case 0x20124:case 0x20128: // antecedent and bounded static-voice control
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
EXPORT int xlle_apu_reference_read(void* instance,uint32_t address,uint32_t* value) {
    if(!instance)return 0;
    auto& ref=*static_cast<Reference*>(instance);
    if(!ref.failure.empty())return 0;
    const bool list=address>=0xfe802054 && address<=0xfe802074 && !(address&3);
    const bool trap=address==0xfe801100 || address==0xfe801110 || address==0xfe801300 || address==0xfe801304;
    if(!value || (!list && !trap && address!=0xfe820010 && address!=0xfe801000)) {
        ref.failure="unqualified APU read transaction";return 0;
    }
    // This bounded model consumes every admitted command synchronously. Thus
    // its PIO queue is empty after actual work; unknown methods stop on write.
    *value=ref.apu.mmio_read(address,4);return ref.failure.empty();
}
EXPORT int xlle_apu_reference_frame(void* instance,uint64_t frame) {
    if(!instance)return 0;
    auto& ref=*static_cast<Reference*>(instance);
    if(!ref.failure.empty())return 0;
    ref.apu.set_frame(frame);return ref.failure.empty();
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
EXPORT int xlle_apu_reference_mix(void* instance,uint32_t voice,const float* samples,uint32_t frames,float* bins) {
    if(!instance)return 0;
    auto& ref=*static_cast<Reference*>(instance);
    if(!ref.failure.empty())return 0;
    if(voice>0xffff) {ref.failure="invalid APU mix transport voice";return 0;}
    return ref.apu.mix_2d_samples(static_cast<uint16_t>(voice),samples,frames,bins);
}
EXPORT int xlle_apu_reference_rate(void* instance,uint32_t voice,float* samples,uint32_t capacity,
                                  uint32_t* count,int* ended) {
    if(!instance)return 0;
    auto& ref=*static_cast<Reference*>(instance);
    if(!ref.failure.empty())return 0;
    if(voice>0xffff || !count || !ended) {ref.failure="invalid APU rate transport shape";return 0;}
    bool last=false;
    const bool ok=ref.apu.resample_source(static_cast<uint16_t>(voice),samples,capacity,*count,last);
    *ended=last;return ok;
}
EXPORT int xlle_apu_reference_quantum(void* instance,uint32_t voice,float* samples,uint32_t* count,int* ended) {
    if(!instance)return 0;
    auto& ref=*static_cast<Reference*>(instance);
    if(!ref.failure.empty())return 0;
    if(voice>0xffff || !count || !ended) {ref.failure="invalid APU quantum transport shape";return 0;}
    bool last=false;const bool ok=ref.apu.source_envelope_quantum(static_cast<uint16_t>(voice),samples,*count,last);
    *ended=last;return ok;
}
EXPORT int xlle_apu_reference_process(void* instance,uint32_t voice,float* bins,uint32_t* count,int* completed) {
    if(!instance)return 0;
    auto& ref=*static_cast<Reference*>(instance);
    if(!ref.failure.empty())return 0;
    if(voice>0xffff || !count || !completed) {ref.failure="invalid APU processing transport shape";return 0;}
    bool last=false;const bool ok=ref.apu.process_2d_voice(static_cast<uint16_t>(voice),bins,*count,last);
    *completed=last;return ok;
}
