// Private original-kernel filesystem qualification transport. Reuses our
// maintained IDE/PIC models; no filesystem or completion is replaced here.
// The default is read-only/COW; the explicit writable constructor is reserved
// for separately provisioned runtime HDDs and persistence qualification.
#include "machine/ide.hpp"
#include "machine/pic.hpp"
#include "machine/pit.hpp"
#include <cstring>
#include <memory>
#include <string>

#ifdef _WIN32
#define EXPORT extern "C" __declspec(dllexport)
#else
#define EXPORT extern "C"
#endif
namespace {
struct Reference {
    std::string failure;
    xlle::DualPic pic;
    xlle::Pit8254 pit;
    uint64_t ticks=0;
    xlle::IdeController ide;
    void (*external_irq)(int,int)=nullptr;
    Reference() : ide([this](const std::string& why) { if (failure.empty()) failure=why; }) {}
};
}
static void* create_storage(const char* path,uint8_t* ram,uint32_t bytes,
                            uint16_t mask,uint16_t level,bool writeback) {
    if (!path || !ram || bytes!=64u*1024*1024) return nullptr;
    try {
        auto owned=std::make_unique<Reference>();
        auto ref=owned.get();
        // The capture is after boot-time HDD authentication. Disabled security
        // is an explicit provisioned policy, never a fabricated unlock reply.
        xlle::HddSecurityConfig security;
        security.mode=xlle::HddSecurityConfig::Mode::Disabled;
        if (!ref->ide.configure_hdd_security(security) || !ref->ide.attach_hdd(path, writeback)) {
            return nullptr;
        }
        auto& pic=ref->pic;
        pic.io_write(0x20,1,0x11); pic.io_write(0xa0,1,0x11);
        pic.io_write(0x21,1,0x30); pic.io_write(0xa1,1,0x38);
        pic.io_write(0x21,1,4); pic.io_write(0xa1,1,2);
        pic.io_write(0x21,1,1); pic.io_write(0xa1,1,1);
        pic.io_write(0x4d0,1,level&255); pic.io_write(0x4d1,1,level>>8);
        pic.io_write(0x21,1,mask&255); pic.io_write(0xa1,1,mask>>8);
        ref->pit.set_irq0([ref]() {
            if(ref->external_irq) { ref->external_irq(0,1);ref->external_irq(0,0); }
            else { ref->pic.raise(0,true);ref->pic.raise(0,false); }
        });
        ref->ide.set_irq([ref](int irq,bool high) {
            if(ref->external_irq)ref->external_irq(irq,high);else ref->pic.raise(irq,high);
        },14);
        ref->ide.set_dma_mem(
            [ram,bytes](uint32_t address,void* out,size_t size) {
                if (address>bytes || size>bytes-address) return false;
                std::memcpy(out,ram+address,size); return true;
            },
            [ram,bytes](uint32_t address,const void* in,size_t size) {
                if (address>bytes || size>bytes-address) return false;
                std::memcpy(ram+address,in,size); return true;
            });
        return owned.release();
    } catch (...) { return nullptr; }
}
EXPORT void* xlle_storage_create(const char* path,uint8_t* ram,uint32_t bytes,uint16_t mask,uint16_t level) {
    return create_storage(path,ram,bytes,mask,level,false);
}
EXPORT void* xlle_storage_create_writable(const char* path,uint8_t* ram,uint32_t bytes,uint16_t mask,uint16_t level) {
    return create_storage(path,ram,bytes,mask,level,true);
}
EXPORT void xlle_storage_bus(void* instance,int(*read)(uint32_t,void*,size_t),
                            int(*write)(uint32_t,const void*,size_t),void(*irq)(int,int)) {
    auto& ref=*static_cast<Reference*>(instance);
    if(!read || !write || !irq || ref.ide.irq_pulses()) {
        ref.failure="invalid/late storage bus binding";return;
    }
    ref.external_irq=irq;
    ref.ide.set_dma_mem(
        [read](uint32_t a,void* b,size_t n){return read(a,b,n)!=0;},
        [write](uint32_t a,const void* b,size_t n){return write(a,b,n)!=0;});
}
EXPORT void xlle_storage_destroy(void* instance) { delete static_cast<Reference*>(instance); }
EXPORT const char* xlle_storage_error(void* instance) {
    auto& ref=*static_cast<Reference*>(instance);
    return ref.failure.empty()?nullptr:ref.failure.c_str();
}
// The same IDE owner supplies SMC tray telemetry; do not copy mounted state
// into a second device. This read never runs guest code or changes the drive.
EXPORT int xlle_storage_tray(void* instance,uint8_t* value) {
    if (!instance || !value) return 0;
    auto& ref=*static_cast<Reference*>(instance);
    if (!ref.failure.empty()) return 0;
    *value=ref.ide.dvd_tray_state();
    return 1;
}
EXPORT int xlle_storage_port(void* instance,uint32_t port,int size,int write,uint32_t* value) {
    auto& ref=*static_cast<Reference*>(instance);
    if (!ref.failure.empty()) return 0;
    xlle::IoPortDevice* device=nullptr;
    if (port<=65535 && ref.pic.owns(static_cast<uint16_t>(port)) && size==1) device=&ref.pic;
    else if (port>=0x40 && port<=0x43 && size==1) device=&ref.pit;
    else if (port<=65535 && ref.ide.owns(static_cast<uint16_t>(port)) &&
             (size==1 || size==2 || size==4)) device=&ref.ide;
    if (!device || !value) { ref.failure="unmodelled storage port transaction"; return 0; }
    if (write) device->io_write(static_cast<uint16_t>(port),size,*value);
    else *value=device->io_read(static_cast<uint16_t>(port),size);
    return ref.failure.empty();
}
EXPORT int xlle_storage_vector(void* instance,int acknowledge) {
    auto& pic=static_cast<Reference*>(instance)->pic;
    return acknowledge?pic.acknowledge():pic.peek();
}
// Explicit clock input for private original-kernel qualification. No implicit
// wall clock or kernel-memory update; IRQ0 must traverse the original handler.
EXPORT int xlle_storage_clock(void* instance,uint64_t ticks) {
    auto& ref=*static_cast<Reference*>(instance);
    if(ticks<ref.ticks) { ref.failure="reference clock moved backwards";return 0; }
    ref.ticks=ticks;ref.pit.set_time(ticks);return ref.failure.empty();
}
EXPORT uint64_t xlle_storage_deadline(void* instance) {
    return static_cast<Reference*>(instance)->pit.next_irq0_tick();
}
EXPORT uint64_t xlle_storage_reads(void* instance) { return static_cast<Reference*>(instance)->ide.sectors_read(); }
EXPORT uint64_t xlle_storage_writes(void* instance) { return static_cast<Reference*>(instance)->ide.sectors_written(); }
EXPORT uint64_t xlle_storage_overlay(void* instance) { return static_cast<Reference*>(instance)->ide.overlay_sectors(); }
