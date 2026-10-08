// C ABI for private original-driver qualification against our maintained NV2A.
// This contains transport only. Unmodelled accesses fail the caller's run.
#include "machine/nv2a.hpp"
#include "machine/pic.hpp"
#include <cstdio>
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
    bool irq = false;
    uint8_t encoder_field = 0;
    xlle::DualPic pic;
    xlle::Nv2a gpu;
    Reference() : gpu([this](const std::string& reason) { failure = reason; }) {
        gpu.set_irq([this](int line, bool level) { irq = level; pic.raise(line, level); }, 3);
        gpu.on_unmodeled([this](bool write, uint32_t address, uint32_t,
                               const char*, uint32_t) {
            char message[96];
            std::snprintf(message, sizeof(message), "unmodelled NV2A %s at %08x",
                          write ? "write" : "read", address);
            failure = message;
        });
    }
};
}
EXPORT void* xlle_nv2a_reference_create() {
    try { return new Reference; } catch (...) { return nullptr; }
}
EXPORT void xlle_nv2a_reference_destroy(void* instance) {
    delete static_cast<Reference*>(instance);
}
EXPORT void xlle_nv2a_reference_memory(void* instance, uint8_t* ram, uint32_t bytes) {
    auto& ref = *static_cast<Reference*>(instance);
    ref.gpu.set_mem_access(
        [ram, bytes](uint32_t address, void* output, size_t size) {
            if (uint64_t(address) + size > bytes) return false;
            std::memcpy(output, ram + address, size); return true;
        },
        [ram, bytes](uint32_t address, const void* input, size_t size) {
            if (uint64_t(address) + size > bytes) return false;
            std::memcpy(ram + address, input, size); return true;
        });
}
EXPORT const char* xlle_nv2a_reference_error(void* instance) {
    auto& ref = *static_cast<Reference*>(instance);
    return ref.failure.empty() ? nullptr : ref.failure.c_str();
}
EXPORT uint32_t xlle_nv2a_reference_read(void* instance, uint32_t address, int size) {
    auto& ref = *static_cast<Reference*>(instance);
    return ref.failure.empty() ? ref.gpu.mmio_read(address, size) : 0;
}
EXPORT void xlle_nv2a_reference_write(void* instance, uint32_t address, int size, uint32_t value) {
    auto& ref = *static_cast<Reference*>(instance);
    if (ref.failure.empty()) ref.gpu.mmio_write(address, size, value);
}
EXPORT void xlle_nv2a_reference_frame(void* instance, uint64_t frame) {
    auto& ref = *static_cast<Reference*>(instance);
    if (ref.failure.empty()) ref.gpu.set_frame(frame);
}
EXPORT int xlle_nv2a_reference_irq(void* instance) {
    return static_cast<Reference*>(instance)->irq;
}
EXPORT void xlle_nv2a_reference_pic(void* instance, uint16_t mask, uint16_t level) {
    auto& pic = static_cast<Reference*>(instance)->pic;
    // The same original Xbox PIC bootstrap used by the maintained USB floor.
    pic.io_write(0x20,1,0x11); pic.io_write(0xa0,1,0x11);
    pic.io_write(0x21,1,0x30); pic.io_write(0xa1,1,0x38);
    pic.io_write(0x21,1,4); pic.io_write(0xa1,1,2);
    pic.io_write(0x21,1,1); pic.io_write(0xa1,1,1);
    pic.io_write(0x4d0,1,level&255); pic.io_write(0x4d1,1,level>>8);
    pic.io_write(0x21,1,mask&255); pic.io_write(0xa1,1,mask>>8);
}
EXPORT uint32_t xlle_nv2a_reference_port(void* instance, uint16_t port, int size, int write, uint32_t value) {
    auto& ref = *static_cast<Reference*>(instance);
    if (ref.pic.owns(port) && size == 1) {
        if (write) ref.pic.io_write(port,size,value);
        else return ref.pic.io_read(port,size);
    } else if (port == 0x80c0 && size == 1 && !write) {
        // External encoder field input supplied by this component's caller.
        // Reading a pin cannot itself advance the display clock.
        return uint32_t(ref.encoder_field) << 5;
    } else ref.failure = "unmodelled display reference port";
    return 0;
}
EXPORT void xlle_nv2a_reference_field(void* instance, int level) {
    static_cast<Reference*>(instance)->encoder_field = level != 0;
}
EXPORT int xlle_nv2a_reference_vector(void* instance, int acknowledge) {
    auto& pic = static_cast<Reference*>(instance)->pic;
    return acknowledge ? pic.acknowledge() : pic.peek();
}
