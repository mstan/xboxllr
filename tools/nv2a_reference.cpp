// C ABI for private original-driver qualification against our maintained NV2A.
// This contains transport only. Unmodelled accesses fail the caller's run.
#include "machine/nv2a.hpp"
#include "machine/pic.hpp"
#include "machine/pci.hpp"
#include "machine/mcpx_field.hpp"
#include "machine/mcpx_av_outputs.hpp"
#include "machine/cx25871_control.hpp"
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
    xlle::McpxFieldInput field;
    xlle::McpxAvOutputs av_outputs;
    xlle::PciHost pci;
    xlle::DualPic pic;
    xlle::Nv2a gpu;
    xlle::Amd756Smbus smbus;
    xlle::Cx25871Control* encoder = nullptr;
    void (*external_irq)(int, int) = nullptr;
    Reference() : field([this](const std::string& reason) { failure = reason; }),
        av_outputs([this](const std::string& reason) { failure = reason; }),
        pci([this](const std::string& reason) { failure = reason; }),
        gpu([this](const std::string& reason) { failure = reason; }),
        smbus(0xc000, [this](const std::string& reason) { failure = reason; }) {
        auto control = std::make_unique<xlle::Cx25871Control>(
            [this](const std::string& reason) { failure = reason; });
        encoder = control.get(); smbus.add_slave(std::move(control));
        smbus.set_irq([this](int line, bool level) {
            if (external_irq) external_irq(line, level); else pic.raise(line, level);
        }, 11);
        pci.attach_function(1, 0, 0, [this](uint8_t reg, int size, bool write, uint32_t value) {
            if (write) { gpu.pci_write(reg, size, value); return 0u; }
            return gpu.pci_read(reg, size);
        });
        gpu.set_irq([this](int line, bool level) {
            irq = level;
            if (external_irq) external_irq(line, level); else pic.raise(line, level);
        }, 3);
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
EXPORT void xlle_nv2a_reference_bus(void* instance,
        int (*read)(uint32_t, void*, size_t),
        int (*write)(uint32_t, const void*, size_t), void (*irq)(int, int)) {
    auto& ref = *static_cast<Reference*>(instance);
    if (!read || !write || !irq || ref.irq) {
        ref.failure = "invalid NV2A reference bus binding"; return;
    }
    ref.external_irq = irq;
    ref.gpu.set_mem_access(
        [read](uint32_t address, void* output, size_t size) { return read(address, output, size) != 0; },
        [write](uint32_t address, const void* input, size_t size) { return write(address, input, size) != 0; });
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
EXPORT void xlle_nv2a_reference_clock(void* instance, uint64_t core_cycles, uint32_t phase) {
    auto& ref = *static_cast<Reference*>(instance);
    if (phase >= 1000) { ref.failure = "NV2A clock phase outside display interval"; return; }
    if (ref.failure.empty()) {
        ref.gpu.set_timer_clock(core_cycles);
        ref.gpu.set_frame_phase(phase);
    }
}
EXPORT int xlle_nv2a_reference_irq(void* instance) {
    return static_cast<Reference*>(instance)->irq;
}
EXPORT int xlle_nv2a_reference_scanout(void* instance, uint8_t* output, uint32_t capacity,
                                      uint32_t* width, uint32_t* height) {
    auto& ref = *static_cast<Reference*>(instance);
    if (!ref.failure.empty()) return 0;
    // Raw GPU fixtures observe the digital scanout before the encoder. Once
    // AV programming occurs, do not present that as an encoded video result.
    if (ref.encoder->programmed()) {
        ref.failure = "CX25871 programmed video output remains unqualified"; return 0;
    }
    std::vector<uint8_t> rgba;
    xlle::Nv2a::ScanoutInfo info;
    std::string error;
    if (!ref.gpu.capture_scanout(rgba, info, error)) { ref.failure = error; return 0; }
    if (!output || !width || !height || rgba.size() > capacity) {
        ref.failure = "NV2A scanout output capacity"; return 0;
    }
    std::memcpy(output, rgba.data(), rgba.size());
    *width = info.width; *height = info.height; return 1;
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
    if (!ref.failure.empty()) return 0;
    if (ref.pic.owns(port) && size == 1) {
        if (write) ref.pic.io_write(port,size,value);
        else return ref.pic.io_read(port,size);
    } else if (port == 0x80c0 && size == 1) {
        if (write) ref.field.write(value); else return ref.field.read();
    } else if (xlle::McpxAvOutputs::owns(port)) {
        if(write && size==1)ref.av_outputs.write(port,value);
        else ref.failure="MCPX AV GPIO readback/width remains unqualified";
    } else if (ref.pci.owns(port)) {
        if (write) ref.pci.io_write(port, size, value);
        else return ref.pci.io_read(port, size);
    } else if (ref.smbus.owns(port) &&
               ((size == 1 && (port == 0xc000 || port == 0xc002 || port == 0xc004 ||
                              port == 0xc006 || port == 0xc007 || port == 0xc008)) ||
                (size == 2 && (port == 0xc000 || port == 0xc006)))) {
        if (write) ref.smbus.io_write(port, size, value);
        else return ref.smbus.io_read(port, size);
    } else ref.failure = "unmodelled display reference port";
    return 0;
}
EXPORT void xlle_nv2a_reference_field(void* instance, int level) {
    static_cast<Reference*>(instance)->field.set_level(level != 0);
}
// Settled clock rates at the encoder boundary, not a PLL-lock assertion or
// generated sync/pixels. Six integers: encoder, input, CLKO numerator/denominator.
EXPORT int xlle_nv2a_reference_encoder_clocks(void* instance,uint64_t* rates,uint32_t* driven) {
    auto& ref=*static_cast<Reference*>(instance);
    if(!rates || !driven) { ref.failure="encoder clock observation has no destination";return 0; }
    if(!ref.failure.empty())return 0;
    xlle::Cx25871Control::Clocks clocks;
    if(!ref.encoder->clocks(clocks))return 0;
    rates[0]=clocks.encoder.numerator;rates[1]=clocks.encoder.denominator;
    rates[2]=clocks.input.numerator;rates[3]=clocks.input.denominator;
    rates[4]=clocks.clko.numerator;rates[5]=clocks.clko.denominator;
    *driven=clocks.clko_driven?1:0;return 1;
}
// Pin-level observation for AV integration. These are physical pin masks,
// not guest register values. Undefined/unknown SCART states stay distinguishable.
EXPORT int xlle_nv2a_reference_av_outputs(void* instance, uint32_t* known, uint32_t* high) {
    auto& ref=*static_cast<Reference*>(instance);
    if(!known || !high) { ref.failure="AV GPIO output observation has no destination";return -1; }
    *known=ref.av_outputs.known();*high=ref.av_outputs.levels();
    return static_cast<int>(ref.av_outputs.scart_status());
}
EXPORT int xlle_nv2a_reference_vector(void* instance, int acknowledge) {
    auto& pic = static_cast<Reference*>(instance)->pic;
    return acknowledge ? pic.acknowledge() : pic.peek();
}
