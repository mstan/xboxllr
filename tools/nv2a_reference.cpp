// C ABI for private original-driver qualification against our maintained NV2A.
// This contains transport only. Unmodelled accesses fail the caller's run.
#include "machine/nv2a.hpp"
#include "machine/pic.hpp"
#include "machine/pci.hpp"
#include "machine/mcpx_field.hpp"
#include "machine/mcpx_av_outputs.hpp"
#include "machine/cx25871_control.hpp"
#include "machine/nv2a_encoder_clock.hpp"
#include <cstdio>
#include <cstring>
#include <array>
#include <new>
#include <string>
#include <exception>
#include <typeinfo>

#ifdef _WIN32
#define EXPORT extern "C" __declspec(dllexport)
#else
#define EXPORT extern "C"
#endif
namespace {
struct Reference {
    std::string failure;
    // Reporting bad_alloc must not allocate again. A processing exception is
    // sticky failure, never permission to resume partially completed work.
    std::array<char,384> bridge_failure{};
    bool failed() const noexcept { return !failure.empty() || bridge_failure[0]; }
    const char* error_text() const noexcept {
        if(bridge_failure[0])return bridge_failure.data();
        return failure.empty()?nullptr:failure.c_str();
    }
    void record_exception(const char* operation,const char* kind,const char* message) noexcept {
        if(failed())return;
        std::snprintf(bridge_failure.data(),bridge_failure.size(),
                      "NV2A reference %s exception %s: %s",operation,kind,message?message:"");
    }
    bool irq = false;
    xlle::McpxFieldInput field;
    xlle::McpxAvOutputs av_outputs;
    xlle::PciHost pci;
    xlle::DualPic pic;
    xlle::Nv2a gpu;
    xlle::Amd756Smbus smbus;
    xlle::Cx25871Control* encoder = nullptr;
    std::unique_ptr<xlle::Nv2aEncoderClock> board_clock;
    void refresh_board_clock() {
        if(!board_clock || failed())return;
        board_clock->refresh(gpu.display_programming(),gpu.display_slaved(),gpu.display_clock_routing());
        if(!board_clock->active())gpu.display_clock_signals(false,false,false,false);
    }
    void (*external_irq)(int, int) = nullptr;
    // Qualification observes the last dispatched parameter. One transport
    // advance can exceed the recent recorder window, so consume the actual
    // ordered records as they arrive using fixed storage, without polling.
    std::array<uint32_t, 0x2000 / 4> observed_methods{};
    uint64_t observed_method_count = 0;
    void observe_method(const xlle::Nv2a::PushMethod& item) {
        if (failed()) return;
        if (item.seq != observed_method_count) {
            failure = "NV2A reference method observation sequence gap";
            return;
        }
        if ((item.method & 3) == 0 && item.method < 0x2000)
            observed_methods[item.method / 4] = item.parameter;
        ++observed_method_count;
    }
    void sample_encoder_field(const xlle::Cx25871Timing::State* before=nullptr) {
        const int signal=encoder->field_signal();
        if(signal<0)return; // unresolved/high-Z pins cannot supply a guessed level
        xlle::Cx25871Timing::State after;
        if(before && encoder->timing_state(after) &&
           after.field_transitions-before->field_transitions>1) {
            // MCPX has a level latch, not an edge FIFO. Preserve intervening
            // active levels even when the board advances multiple fields.
            field.set_level(signal==0);
        }
        field.set_level(signal!=0);
    }
    Reference() : field([this](const std::string& reason) { failure = reason; }),
        av_outputs([this](const std::string& reason) { failure = reason; }),
        pci([this](const std::string& reason) { failure = reason; }),
        gpu([this](const std::string& reason) { if(failure.empty())failure = reason; }),
        smbus(0xc000, [this](const std::string& reason) { failure = reason; }) {
        if (!gpu.on_method_recorded([this](const xlle::Nv2a::PushMethod& item) { observe_method(item); }))
            failure = "NV2A reference method observer attachment failed";
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
// Keep C++ unwinding inside this DLL, including callbacks. A foreign C/MSVC
// caller receives the existing failure status and operation/message instead.
// Pure snapshot observers retain their separate local-failure contract below.
#if defined(__cpp_rtti)
#define REFERENCE_EXCEPTION_KIND(error) typeid(error).name()
#else
#define REFERENCE_EXCEPTION_KIND(error) "std::exception"
#endif
#define REFERENCE_CATCH(result) \
    catch(const std::exception& error) { \
        static_cast<Reference*>(instance)->record_exception( \
            __func__,REFERENCE_EXCEPTION_KIND(error),error.what()); \
        return result; \
    } catch(...) { \
        static_cast<Reference*>(instance)->record_exception( \
            __func__,"non-standard C++","no std::exception message"); \
        return result; \
    }
EXPORT void* xlle_nv2a_reference_create() {
    try { return new Reference; } catch (...) { return nullptr; }
}
EXPORT void xlle_nv2a_reference_destroy(void* instance) {
    delete static_cast<Reference*>(instance);
}
EXPORT int xlle_nv2a_reference_av_idle(void* instance, uint32_t phase) noexcept try {
    if(static_cast<Reference*>(instance)->failed())return 0;
    auto& ref = *static_cast<Reference*>(instance);
    return !ref.failed() && ref.gpu.establish_av_idle_phase(phase);
} REFERENCE_CATCH(0)
EXPORT int xlle_nv2a_reference_board_start(void* instance,uint64_t owner,uint64_t ticks,uint64_t frequency) noexcept try {
    if(static_cast<Reference*>(instance)->failed())return 0;
    auto& ref=*static_cast<Reference*>(instance);
    if(ref.failed())return 0;
    if(ref.board_clock) {ref.failure="display board clock already owned";return 0;}
    if(!ref.gpu.establish_display_clock_routing(0x100a0000u))return 0;
    // Original AV3's first attribute access is an index write. Preserve that
    // initialized-caller precondition explicitly, apart from clock routing.
    if(!ref.gpu.establish_vga_attribute_index_phase())return 0;
    ref.board_clock=std::make_unique<xlle::Nv2aEncoderClock>(*ref.encoder,
        [&ref](const std::string& why){if(!ref.failed())ref.failure=why;},
        [&ref](const xlle::Nv2aFpTiming::State& state,bool vblank) {
            ref.gpu.display_clock_signals(true,state.vsync_active,!state.display_active,vblank);
            if(ref.board_clock->encoder_live())ref.sample_encoder_field();
        });
    return ref.board_clock->bind({owner,ticks,frequency});
} REFERENCE_CATCH(0)
EXPORT int xlle_nv2a_reference_board_advance(void* instance,uint64_t owner,uint64_t ticks,uint64_t frequency) noexcept try {
    if(static_cast<Reference*>(instance)->failed())return 0;
    auto& ref=*static_cast<Reference*>(instance);
    if(ref.failed())return 0;
    if(!ref.board_clock) {ref.failure="display board clock has no owner";return 0;}
    const uint64_t prior=ref.encoder->field_transitions();
    if(!ref.board_clock->advance({owner,ticks,frequency}))return 0;
    // The encoder output clock also runs while FP sync acquisition is absent.
    // Preserve the MCPX activity latch across a large clock advance as before.
    if(ref.board_clock->encoder_live()) {
        const int signal=ref.encoder->field_signal();
        if(signal>=0) {
            if(ref.encoder->field_transitions()-prior>1)ref.field.set_level(signal==0);
            ref.field.set_level(signal!=0);
        }
    }
    return 1;
} REFERENCE_CATCH(0)
EXPORT void xlle_nv2a_reference_memory(void* instance, uint8_t* ram, uint32_t bytes) noexcept try {
    if(static_cast<Reference*>(instance)->failed())return ;
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
} REFERENCE_CATCH()
EXPORT void xlle_nv2a_reference_bus(void* instance,
        int (*read)(uint32_t, void*, size_t),
        int (*write)(uint32_t, const void*, size_t), void (*irq)(int, int)) noexcept try {
    if(static_cast<Reference*>(instance)->failed())return ;
    auto& ref = *static_cast<Reference*>(instance);
    if (!read || !write || !irq || ref.irq) {
        ref.failure = "invalid NV2A reference bus binding"; return;
    }
    ref.external_irq = irq;
    ref.gpu.set_mem_access(
        [read](uint32_t address, void* output, size_t size) { return read(address, output, size) != 0; },
        [write](uint32_t address, const void* input, size_t size) { return write(address, input, size) != 0; });
} REFERENCE_CATCH()
EXPORT const char* xlle_nv2a_reference_error(void* instance) {
    auto& ref = *static_cast<Reference*>(instance);
    return ref.error_text();
}
EXPORT int xlle_nv2a_reference_aperture(void* instance,uint32_t offset,unsigned size,int write,uint64_t* value) noexcept try {
    if(static_cast<Reference*>(instance)->failed())return 0;
    auto& ref=*static_cast<Reference*>(instance);
    if(ref.failed())return 0;
    if(!value || (size!=1 && size!=2 && size!=4 && size!=8) || (write!=0 && write!=1)) {
        ref.failure="invalid NV2A aperture bridge transaction";return 0;
    }
    uint8_t bytes[8]{};
    if(write)for(unsigned i=0;i<size;++i)bytes[i]=uint8_t(*value>>(8*i));
    if(!ref.gpu.cpu_aperture(offset,bytes,size,write!=0))return 0;
    if(ref.failed())return 0;
    if(!write) {
        uint64_t result=0;
        for(unsigned i=0;i<size;++i)result|=uint64_t(bytes[i])<<(8*i);
        *value=result;
    }
    return 1;
} REFERENCE_CATCH(0)
EXPORT uint32_t xlle_nv2a_reference_read(void* instance, uint32_t address, int size) noexcept try {
    if(static_cast<Reference*>(instance)->failed())return 0;
    auto& ref = *static_cast<Reference*>(instance);
    if (ref.failed()) return 0;
    return ref.gpu.mmio_read(address, size);
} REFERENCE_CATCH(0)
EXPORT void xlle_nv2a_reference_write(void* instance, uint32_t address, int size, uint32_t value) noexcept try {
    if(static_cast<Reference*>(instance)->failed())return ;
    auto& ref = *static_cast<Reference*>(instance);
    if (!ref.failed()) {
        ref.gpu.mmio_write(address, size, value);
        ref.refresh_board_clock();
    }
} REFERENCE_CATCH()
EXPORT void xlle_nv2a_reference_frame(void* instance, uint64_t frame) noexcept try {
    if(static_cast<Reference*>(instance)->failed())return ;
    auto& ref = *static_cast<Reference*>(instance);
    if(!ref.failed() && ref.board_clock) {ref.failure="NV2A manual frame injection conflicts with board clock owner";return;}
    if (!ref.failed()) ref.gpu.set_frame(frame);
} REFERENCE_CATCH()
// Qualification-only observation of the maintained transform output. No
// shader/fetch implementation is substituted, and this bridge is not shipped.
EXPORT void xlle_nv2a_reference_vertex(void* instance, float* output) noexcept try {
    auto& ref = *static_cast<Reference*>(instance);
    const auto position = ref.gpu.last_transformed_position();
    const auto diffuse = ref.gpu.last_transformed_diffuse();
    const auto specular = ref.gpu.last_transformed_specular();
    std::memcpy(output, position.data(), 16);
    std::memcpy(output + 4, diffuse.data(), 16);
    std::memcpy(output + 8, specular.data(), 16);
} REFERENCE_CATCH()
// Seven vec4s per actually transformed vertex: raster position, diffuse,
// specular and four texture coordinates. Positions include NV2A raster rounding.
EXPORT uint32_t xlle_nv2a_reference_vertices(void* instance, float* output, uint32_t capacity) noexcept try {
    auto& ref = *static_cast<Reference*>(instance);
    if (ref.failed()) return 0;
    const auto& vertices = ref.gpu.last_transformed_batch();
    if (!output && !capacity) return static_cast<uint32_t>(vertices.size());
    if (!output || capacity < vertices.size()) {
        ref.failure = "NV2A transformed observation capacity"; return 0;
    }
    for (const auto& vertex : vertices) {
        std::memcpy(output, vertex.position.data(), 16);
        std::memcpy(output + 4, vertex.diffuse.data(), 16);
        std::memcpy(output + 8, vertex.specular.data(), 16);
        std::memcpy(output + 12, vertex.texture.data(), 64);
        output += 28;
    }
    return static_cast<uint32_t>(vertices.size());
} REFERENCE_CATCH(0)
// Separate scalar observation preserves the existing seven-vec4 diagnostic ABI.
EXPORT uint32_t xlle_nv2a_reference_fog(void* instance, float* output, uint32_t capacity) noexcept try {
    const auto& ref = *static_cast<Reference*>(instance);
    if (ref.failed()) return 0;
    const auto& vertices = ref.gpu.last_transformed_batch();
    if (!output || !capacity) return static_cast<uint32_t>(vertices.size());
    if (capacity < vertices.size()) return 0;
    for (size_t i = 0; i < vertices.size(); ++i) output[i] = vertices[i].fog;
    return static_cast<uint32_t>(vertices.size());
} REFERENCE_CATCH(0)
// Diagnostic census prevents a guarded/omitted draw from being called qualified.
// Index 0..2 observes attempts, executions and actual color writes; later indices
// observe named generic skip reasons without changing device behavior.
EXPORT uint64_t xlle_nv2a_reference_draw_count(void* instance, uint32_t index) noexcept try {
    const auto& gpu = static_cast<Reference*>(instance)->gpu;
    if (index == 0) return gpu.generic_combiner_attempted_batches();
    if (index == 1) return gpu.generic_combiner_executed_batches();
    if (index == 2) return gpu.generic_combiner_writes();
    return gpu.generic_skip_reason_batches(index - 3);
} REFERENCE_CATCH(0)
EXPORT const char* xlle_nv2a_reference_draw_count_name(uint32_t index) {
    if (index == 0) return "attempts";
    if (index == 1) return "executions";
    if (index == 2) return "color-writes";
    return xlle::Nv2a::generic_skip_reason_name(index - 3);
}
EXPORT uint32_t xlle_nv2a_reference_method_value(void* instance, uint32_t method) noexcept try {
    auto& ref = *static_cast<Reference*>(instance);
    if (ref.failed() || (method & 3) || method >= 0x2000) return 0;
    return ref.observed_methods[method / 4];
} REFERENCE_CATCH(0)
EXPORT void xlle_nv2a_reference_clock(void* instance, uint64_t core_cycles, uint32_t phase) noexcept try {
    if(static_cast<Reference*>(instance)->failed())return ;
    auto& ref = *static_cast<Reference*>(instance);
    if (phase >= 1000) { ref.failure = "NV2A clock phase outside display interval"; return; }
    if (!ref.failed()) {
        ref.gpu.set_timer_clock(core_cycles);
        ref.gpu.set_frame_phase(phase);
    }
} REFERENCE_CATCH()
EXPORT int xlle_nv2a_reference_irq(void* instance) noexcept try {
    return static_cast<Reference*>(instance)->irq;
} REFERENCE_CATCH(0)
EXPORT int xlle_nv2a_reference_scanout(void* instance, uint8_t* output, uint32_t capacity,
                                      uint32_t* width, uint32_t* height) noexcept try {
    auto& ref = *static_cast<Reference*>(instance);
    if (ref.failed()) return 0;
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
} REFERENCE_CATCH(0)
EXPORT void xlle_nv2a_reference_pic(void* instance, uint16_t mask, uint16_t level) noexcept try {
    if(static_cast<Reference*>(instance)->failed())return ;
    auto& pic = static_cast<Reference*>(instance)->pic;
    // The same original Xbox PIC bootstrap used by the maintained USB floor.
    pic.io_write(0x20,1,0x11); pic.io_write(0xa0,1,0x11);
    pic.io_write(0x21,1,0x30); pic.io_write(0xa1,1,0x38);
    pic.io_write(0x21,1,4); pic.io_write(0xa1,1,2);
    pic.io_write(0x21,1,1); pic.io_write(0xa1,1,1);
    pic.io_write(0x4d0,1,level&255); pic.io_write(0x4d1,1,level>>8);
    pic.io_write(0x21,1,mask&255); pic.io_write(0xa1,1,mask>>8);
} REFERENCE_CATCH()

// FBD1/version1: 32 LE DWORDs, 11 display-command DWORDs, 8x4 tile DWORDs,
// 768 DAC bytes, 256 encoder values + 256 known-bit masks, extension mask +
// 20 bytes (18 commands, 2 zero pad), 256-byte error, then packed storage rows.
// Stage1 is framebuffer BEFORE all display processing, never qualified output.
// The stopped owner supplies a pure physical-RAM copy. This does not use the
// emulation DMA callbacks or change failure state, clocks, FIFO, IRQ or owners.
EXPORT int xlle_nv2a_reference_framebuffer_snapshot(void* instance,
        uint8_t* output,uint32_t capacity,uint32_t* required,
        int (*read)(uint32_t,void*,size_t)) {
    if(!instance || !required)return 0;
    *required=0;
    try {
        constexpr uint32_t header=1860;
        const auto& ref=*static_cast<const Reference*>(instance);
        const auto state=ref.gpu.framebuffer_diagnostic_state();
        const auto& info=state.info;
        const bool geometry=info.width && info.height && info.width<=1920 && info.height<=1080 &&
            (info.bits_per_pixel==16 || info.bits_per_pixel==32);
        const uint32_t maximum=geometry?info.width*info.height*(info.bits_per_pixel/8):0;
        *required=header+maximum;
        if(!output || capacity<*required)return 0; // no RAM copy on capacity query
        std::vector<uint8_t> pixels;
        std::string error=ref.error_text()?ref.error_text():"";
        bool copied=false;
        if(error.empty()) {
            if(!read)error="framebuffer diagnostic has no copy-only RAM reader";
            else copied=ref.gpu.copy_framebuffer_diagnostic(pixels,
                [read](uint32_t a,void* p,size_t n){return read(a,p,n)!=0;},error);
        }
        uint8_t* cursor=output;
        auto put=[&](uint32_t value){for(unsigned s=0;s<32;s+=8)*cursor++=uint8_t(value>>s);};
        auto put64=[&](uint64_t value){put(uint32_t(value));put(uint32_t(value>>32));};
        put(0x31444246);put(1);put(header);put(copied);put(uint32_t(pixels.size()));
        put(info.start);put(info.pitch);put(info.width);put(info.height);put(info.bits_per_pixel);
        put(info.rgb565);put(state.general_control);put(state.screen_off);put(state.dac_started);
        put(state.dac_complete);put(state.dac_identity);put(state.commands.known);
        put(ref.encoder->programmed());put(ref.encoder->timing_known());put(ref.encoder->field_known());
        put64(state.vblanks);put64(state.starts);put64(state.methods);put64(state.published);put64(state.retired);
        put(1);put(0);
        for(auto value:state.commands.received)put(value);
        for(const auto& tile:state.tiles)for(auto value:tile)put(value);
        std::memcpy(cursor,state.gamma.data(),state.gamma.size());cursor+=state.gamma.size();
        const auto encoder=ref.encoder->control_observation();
        std::memcpy(cursor,encoder.values.data(),256);cursor+=256;
        std::memcpy(cursor,encoder.known.data(),256);cursor+=256;
        const auto& extensions=ref.encoder->extension_commands();put(extensions.received);
        std::memcpy(cursor,extensions.bytes.data(),18);cursor+=18;*cursor++=0;*cursor++=0;
        std::memset(cursor,0,256);std::memcpy(cursor,error.data(),std::min(error.size(),size_t(255)));cursor+=256;
        if(!pixels.empty())std::memcpy(cursor,pixels.data(),pixels.size());
        *required=header+uint32_t(pixels.size());return 1;
    } catch(...) {return 0;}
}
// GPD1/version1: 16 LE DWORDs, 256-byte local error, paired 12-bit DDR words
// (falling then rising, each LE16), then RGBA8 host preview. Stage2 is the
// encoder DIGITAL INPUT, before decimation/flicker/analog processing. Model1
// uses nominal SD601 arithmetic; it does not claim silicon coefficient bits.
// Contains active-picture samples only, not porch/DE or a timed bus waveform.
// Reuses the stopped pure-RAM observer contract of FBD1. No device mutation.
EXPORT int xlle_nv2a_reference_digital_snapshot(void* instance,
        uint8_t* output,uint32_t capacity,uint32_t* required,
        int (*read)(uint32_t,void*,size_t)) {
    if(!instance || !required)return 0;
    *required=0;
    try {
        constexpr uint32_t header=320, maximum=640*480*8;
        *required=header+maximum;
        if(!output || capacity<*required)return 0;
        const auto& ref=*static_cast<const Reference*>(instance);
        const auto state=ref.gpu.framebuffer_diagnostic_state();
        std::string error=ref.error_text()?ref.error_text():"";
        std::vector<xlle::Sd601Pixel::Ddr12> pins;
        std::vector<uint8_t> rgba;
        xlle::Sd601Pixel::YCrCb sample{};
        bool copied=false;
        if(error.empty()) {
            if(!ref.board_clock || !ref.board_clock->active() || !ref.board_clock->encoder_live() ||
               !ref.encoder->timing_known())error="digital input requires active board clock and acquired encoder timing";
            else if(!ref.encoder->receive_sd601_pixel({0,0},sample))error="digital input requires known encoder mode1100 and edge-correction bypass";
            else if(!read)error="digital input has no copy-only RAM reader";
            else copied=ref.gpu.copy_digital_encoder_input(pins,
                [read](uint32_t a,void* p,size_t n){return read(a,p,n)!=0;},error);
        }
        if(copied) {
            rgba.reserve(pins.size()*4);
            for(const auto words:pins) {
                if(!ref.encoder->receive_sd601_pixel(words,sample)) {
                    copied=false;error="digital input receiver rejected DDR sample";break;
                }
                const auto rgb=xlle::Sd601Pixel::decode(sample);
                rgba.insert(rgba.end(),{rgb.r,rgb.g,rgb.b,255});
            }
        }
        if(!copied) {pins.clear();rgba.clear();}
        uint8_t* cursor=output;
        auto put=[&](uint32_t value){for(unsigned s=0;s<32;s+=8)*cursor++=uint8_t(value>>s);};
        put(0x31445047);put(1);put(header);put(copied);
        put(state.info.width);put(state.info.height);put(uint32_t(pins.size()*4));put(uint32_t(rgba.size()));
        put(1);put(copied?12:0xffffffffu);put(state.info.start);put(state.info.pitch);
        put(state.dac_identity);put(state.screen_off);put(0);put(2);
        std::memset(cursor,0,256);std::memcpy(cursor,error.data(),std::min(error.size(),size_t(255)));cursor+=256;
        for(const auto words:pins) {put(uint32_t(words.falling)|(uint32_t(words.rising)<<16));}
        if(!rgba.empty())std::memcpy(cursor,rgba.data(),rgba.size());
        *required=header+uint32_t(pins.size()*4+rgba.size());return 1;
    } catch(...) {return 0;}
}
EXPORT uint32_t xlle_nv2a_reference_port(void* instance, uint16_t port, int size, int write, uint32_t value) noexcept try {
    if(static_cast<Reference*>(instance)->failed())return 0;
    auto& ref = *static_cast<Reference*>(instance);
    if (ref.failed()) return 0;
    if (ref.pic.owns(port) && size == 1) {
        if (write) ref.pic.io_write(port,size,value);
        else return ref.pic.io_read(port,size);
    } else if (port == 0x80c0 && size == 1) {
        if (write) ref.field.write(value);
        else {
            if(ref.board_clock && ref.encoder->timing_selected() && !ref.board_clock->encoder_live()) {
                ref.failure="CX25871 FIELD requires a continuously clocked encoder epoch";return 0;
            }
            if(ref.encoder->timing_selected()) {
                if(ref.encoder->field_signal()<0) {
                    ref.failure="CX25871 FIELD input has unknown/high-impedance board level";return 0;
                }
                ref.sample_encoder_field();
            }
            return ref.field.read();
        }
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
        if (write) {
            ref.smbus.io_write(port, size, value);
            ref.refresh_board_clock();
            if(ref.encoder->timing_selected() && (!ref.board_clock || ref.board_clock->encoder_live()))
                ref.sample_encoder_field();
        }
        else return ref.smbus.io_read(port, size);
    } else ref.failure = "unmodelled display reference port";
    return 0;
} REFERENCE_CATCH(0)
EXPORT void xlle_nv2a_reference_field(void* instance, int level) noexcept try {
    if(static_cast<Reference*>(instance)->failed())return ;
    auto& ref=*static_cast<Reference*>(instance);
    if(ref.failed())return;
    if(ref.board_clock) {ref.failure="CX25871 manual FIELD injection conflicts with board clock owner";return;}
    if(ref.encoder->timing_selected()) {
        ref.failure="CX25871 programmed timing requires board clocks/sync, not nominal field injection";return;
    }
    ref.field.set_level(level != 0);
} REFERENCE_CATCH()
// Real board clock/sync events for the bounded synchronous profile. sync=0:
// clocks only; 1: leading HSYNC; 2: coincident leading HSYNC/VSYNC;
// 3: leading VSYNC before its associated HSYNC.
EXPORT int xlle_nv2a_reference_encoder_advance(void* instance,uint64_t clocks,unsigned sync) noexcept try {
    if(static_cast<Reference*>(instance)->failed())return 0;
    auto& ref=*static_cast<Reference*>(instance);
    if(ref.failed())return 0;
    if(ref.board_clock) {ref.failure="CX25871 manual clocks/sync conflict with board clock owner";return 0;}
    if(sync>3) { ref.failure="invalid encoder sync event";return 0; }
    xlle::Cx25871Timing::State before;
    const bool known=ref.encoder->timing_state(before);
    if(!ref.encoder->advance_timing(clocks))return 0;
    if(sync==3 ? !ref.encoder->input_vsync() : sync && !ref.encoder->input_sync(sync==2))return 0;
    ref.sample_encoder_field(known?&before:nullptr);return 1;
} REFERENCE_CATCH(0)
EXPORT int xlle_nv2a_reference_encoder_timing(void* instance,uint64_t* values,int* signal) noexcept try {
    auto& ref=*static_cast<Reference*>(instance);
    if(ref.failed())return 0;
    xlle::Cx25871Timing::State state;
    if(!values || !signal || !ref.encoder->timing_state(state)) {
        ref.failure="encoder timing observation lacks established state/destination";return 0;
    }
    if(!state.output_phase_known || !state.subcarrier_phase_known) {
        ref.failure="encoder full timing observation requires qualified raster/color phase";return 0;
    }
    values[0]=state.clocks;values[1]=state.input_x;values[2]=state.input_y;
    values[3]=state.output_x;values[4]=state.output_y;values[5]=state.subcarrier_phase;
    values[6]=state.field_transitions;*signal=ref.encoder->field_signal();return 1;
} REFERENCE_CATCH(0)
// Settled clock rates at the encoder boundary, not a PLL-lock assertion or
// generated sync/pixels. Six integers: encoder, input, CLKO numerator/denominator.
EXPORT int xlle_nv2a_reference_encoder_clocks(void* instance,uint64_t* rates,uint32_t* driven) noexcept try {
    auto& ref=*static_cast<Reference*>(instance);
    if(!rates || !driven) { ref.failure="encoder clock observation has no destination";return 0; }
    if(ref.failed())return 0;
    xlle::Cx25871Control::Clocks clocks;
    if(!ref.encoder->clocks(clocks))return 0;
    rates[0]=clocks.encoder.numerator;rates[1]=clocks.encoder.denominator;
    rates[2]=clocks.input.numerator;rates[3]=clocks.input.denominator;
    rates[4]=clocks.clko.numerator;rates[5]=clocks.clko.denominator;
    *driven=clocks.clko_driven?1:0;return 1;
} REFERENCE_CATCH(0)
// Pin-level observation for AV integration. These are physical pin masks,
// not guest register values. Undefined/unknown SCART states stay distinguishable.
EXPORT int xlle_nv2a_reference_av_outputs(void* instance, uint32_t* known, uint32_t* high) noexcept try {
    auto& ref=*static_cast<Reference*>(instance);
    if(!known || !high) { ref.failure="AV GPIO output observation has no destination";return -1; }
    *known=ref.av_outputs.known();*high=ref.av_outputs.levels();
    return static_cast<int>(ref.av_outputs.scart_status());
} REFERENCE_CATCH(0)
EXPORT int xlle_nv2a_reference_vector(void* instance, int acknowledge) noexcept try {
    if(static_cast<Reference*>(instance)->failed())return 0;
    auto& pic = static_cast<Reference*>(instance)->pic;
    return acknowledge ? pic.acknowledge() : pic.peek();
} REFERENCE_CATCH(0)

// Passive host diagnostic wire GFC1/version1, explicit little-endian DWORDs.
// Caller owns the stopped CPU/Nv2a throughout. Retained guards are untouched.
EXPORT int xlle_nv2a_reference_first_context_snapshot(void* instance,
        uint8_t* output,uint32_t capacity,uint32_t* required) {
    if(!instance || !required)return 0;
    try {
        const auto& ref=*static_cast<const Reference*>(instance);
        const auto state=ref.gpu.first_context_diagnostic_snapshot();
        if(state.pramin.size()!=0x100000 || state.pending.size()>256 || state.pfifo.size()>8192)return 0;
        const uint32_t bytes=21*4+32*13*4+uint32_t(state.pfifo.size())*8+
            uint32_t(state.pending.size())*9*4+uint32_t(state.pramin.size());
        *required=bytes;
        if(!output || capacity<bytes)return 0;
        uint8_t* cursor=output;
        auto put=[&](uint32_t value) {
            for(unsigned shift=0;shift<32;shift+=8)*cursor++=uint8_t(value>>shift);
        };
        put(0x31434647);put(1);put(uint32_t(state.pramin.size()));put(32);
        put(uint32_t(state.pfifo.size()));put(uint32_t(state.pending.size()));
        put(state.context_control);put(state.context_user);put(state.context_table);put(state.ffintfc);
        put(state.fifo_access);put(state.executing);put(state.waiting_for_nop);put(state.waiting_for_flip);
        put(state.status);put(state.intr);put(state.intr_en);put(state.nsource);
        put(state.trapped_addr);put(state.trapped_data);put(state.pmc_enable);
        for(const auto& c:state.channels) {
            put(c.get);put(c.put);put(c.dma_instance);put(c.method);put(c.method_count);put(c.subroutine);
            put(c.reference);put(c.engines);put(c.last_engine);put(c.subchannel);
            put(c.non_incrementing);put(c.subroutine_active);put(c.get_initialized);
        }
        for(const auto& r:state.pfifo){put(r.offset);put(r.value);}
        for(const auto& p:state.pending) {
            put(uint32_t(p.seq));put(uint32_t(p.seq>>32));put(p.channel);put(p.subchannel);
            put(p.object_class);put(p.method);put(p.parameter);put(p.parser_get);put(p.parser_put);
        }
        std::memcpy(cursor,state.pramin.data(),state.pramin.size());
        return 1;
    } catch(...) {return 0;}
}
