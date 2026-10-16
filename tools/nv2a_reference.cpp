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
    std::unique_ptr<xlle::Nv2aEncoderClock> board_clock;
    void refresh_board_clock() {
        if(!board_clock || !failure.empty())return;
        board_clock->refresh(gpu.display_programming(),gpu.display_slaved(),gpu.display_clock_routing());
        if(!board_clock->active())gpu.display_clock_signals(false,false,false,false);
    }
    void (*external_irq)(int, int) = nullptr;
    // Qualification observes the last decoded parameter, not merely a recent
    // recorder window. A target bind can remain active across thousands of
    // subsequent methods. Collect each bounded transport advance before its
    // records age out; never invent a parameter for missing observation history.
    std::array<uint32_t, 0x2000 / 4> observed_methods{};
    uint64_t observed_method_count = 0;
    std::vector<xlle::Nv2a::PushMethod> observed_window;
    void observe_methods() {
        const uint64_t total = gpu.push_methods();
        if (total < observed_method_count) {
            observed_methods.fill(0); observed_method_count = 0;
        }
        const uint64_t delta = total - observed_method_count;
        if (!delta) return;
        if (delta > 4096) {
            failure = "NV2A reference method observation history overflow";
            return;
        }
        gpu.recent_methods(static_cast<size_t>(delta), observed_window);
        if (observed_window.size() != delta || observed_window.front().seq != observed_method_count ||
            observed_window.back().seq + 1 != total) {
            failure = "NV2A reference method observation sequence gap";
            return;
        }
        for (const auto& item : observed_window)
            if ((item.method & 3) == 0 && item.method < 0x2000)
                observed_methods[item.method / 4] = item.parameter;
        observed_method_count = total;
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
EXPORT int xlle_nv2a_reference_av_idle(void* instance, uint32_t phase) {
    auto& ref = *static_cast<Reference*>(instance);
    return ref.failure.empty() && ref.gpu.establish_av_idle_phase(phase);
}
EXPORT int xlle_nv2a_reference_board_start(void* instance,uint64_t owner,uint64_t ticks,uint64_t frequency) {
    auto& ref=*static_cast<Reference*>(instance);
    if(!ref.failure.empty())return 0;
    if(ref.board_clock) {ref.failure="display board clock already owned";return 0;}
    if(!ref.gpu.establish_display_clock_routing(0x100a0000u))return 0;
    // Original AV3's first attribute access is an index write. Preserve that
    // initialized-caller precondition explicitly, apart from clock routing.
    if(!ref.gpu.establish_vga_attribute_index_phase())return 0;
    ref.board_clock=std::make_unique<xlle::Nv2aEncoderClock>(*ref.encoder,
        [&ref](const std::string& why){if(ref.failure.empty())ref.failure=why;},
        [&ref](const xlle::Nv2aFpTiming::State& state,bool vblank) {
            ref.gpu.display_clock_signals(true,state.vsync_active,!state.display_active,vblank);
            if(ref.board_clock->encoder_live())ref.sample_encoder_field();
        });
    return ref.board_clock->bind({owner,ticks,frequency});
}
EXPORT int xlle_nv2a_reference_board_advance(void* instance,uint64_t owner,uint64_t ticks,uint64_t frequency) {
    auto& ref=*static_cast<Reference*>(instance);
    if(!ref.failure.empty())return 0;
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
EXPORT int xlle_nv2a_reference_aperture(void* instance,uint32_t offset,unsigned size,int write,uint64_t* value) {
    auto& ref=*static_cast<Reference*>(instance);
    if(!ref.failure.empty())return 0;
    if(!value || (size!=1 && size!=2 && size!=4 && size!=8) || (write!=0 && write!=1)) {
        ref.failure="invalid NV2A aperture bridge transaction";return 0;
    }
    uint8_t bytes[8]{};
    if(write)for(unsigned i=0;i<size;++i)bytes[i]=uint8_t(*value>>(8*i));
    if(!ref.gpu.cpu_aperture(offset,bytes,size,write!=0))return 0;
    if(!ref.failure.empty())return 0;
    if(!write) {
        uint64_t result=0;
        for(unsigned i=0;i<size;++i)result|=uint64_t(bytes[i])<<(8*i);
        *value=result;
    }
    return 1;
}
EXPORT uint32_t xlle_nv2a_reference_read(void* instance, uint32_t address, int size) {
    auto& ref = *static_cast<Reference*>(instance);
    if (!ref.failure.empty()) return 0;
    const uint32_t result = ref.gpu.mmio_read(address, size);
    ref.observe_methods(); return result;
}
EXPORT void xlle_nv2a_reference_write(void* instance, uint32_t address, int size, uint32_t value) {
    auto& ref = *static_cast<Reference*>(instance);
    if (ref.failure.empty()) {
        ref.gpu.mmio_write(address, size, value); ref.observe_methods();
        ref.refresh_board_clock();
    }
}
EXPORT void xlle_nv2a_reference_frame(void* instance, uint64_t frame) {
    auto& ref = *static_cast<Reference*>(instance);
    if(ref.failure.empty() && ref.board_clock) {ref.failure="NV2A manual frame injection conflicts with board clock owner";return;}
    if (ref.failure.empty()) ref.gpu.set_frame(frame);
}
// Qualification-only observation of the maintained transform output. No
// shader/fetch implementation is substituted, and this bridge is not shipped.
EXPORT void xlle_nv2a_reference_vertex(void* instance, float* output) {
    auto& ref = *static_cast<Reference*>(instance);
    const auto position = ref.gpu.last_transformed_position();
    const auto diffuse = ref.gpu.last_transformed_diffuse();
    const auto specular = ref.gpu.last_transformed_specular();
    std::memcpy(output, position.data(), 16);
    std::memcpy(output + 4, diffuse.data(), 16);
    std::memcpy(output + 8, specular.data(), 16);
}
// Seven vec4s per actually transformed vertex: raster position, diffuse,
// specular and four texture coordinates. Positions include NV2A raster rounding.
EXPORT uint32_t xlle_nv2a_reference_vertices(void* instance, float* output, uint32_t capacity) {
    auto& ref = *static_cast<Reference*>(instance);
    if (!ref.failure.empty()) return 0;
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
}
// Separate scalar observation preserves the existing seven-vec4 diagnostic ABI.
EXPORT uint32_t xlle_nv2a_reference_fog(void* instance, float* output, uint32_t capacity) {
    const auto& ref = *static_cast<Reference*>(instance);
    if (!ref.failure.empty()) return 0;
    const auto& vertices = ref.gpu.last_transformed_batch();
    if (!output || !capacity) return static_cast<uint32_t>(vertices.size());
    if (capacity < vertices.size()) return 0;
    for (size_t i = 0; i < vertices.size(); ++i) output[i] = vertices[i].fog;
    return static_cast<uint32_t>(vertices.size());
}
// Diagnostic census prevents a guarded/omitted draw from being called qualified.
// Index 0..2 observes attempts, executions and actual color writes; later indices
// observe named generic skip reasons without changing device behavior.
EXPORT uint64_t xlle_nv2a_reference_draw_count(void* instance, uint32_t index) {
    const auto& gpu = static_cast<Reference*>(instance)->gpu;
    if (index == 0) return gpu.generic_combiner_attempted_batches();
    if (index == 1) return gpu.generic_combiner_executed_batches();
    if (index == 2) return gpu.generic_combiner_writes();
    return gpu.generic_skip_reason_batches(index - 3);
}
EXPORT const char* xlle_nv2a_reference_draw_count_name(uint32_t index) {
    if (index == 0) return "attempts";
    if (index == 1) return "executions";
    if (index == 2) return "color-writes";
    return xlle::Nv2a::generic_skip_reason_name(index - 3);
}
EXPORT uint32_t xlle_nv2a_reference_method_value(void* instance, uint32_t method) {
    auto& ref = *static_cast<Reference*>(instance); ref.observe_methods();
    if (!ref.failure.empty() || (method & 3) || method >= 0x2000) return 0;
    return ref.observed_methods[method / 4];
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
}
EXPORT void xlle_nv2a_reference_field(void* instance, int level) {
    auto& ref=*static_cast<Reference*>(instance);
    if(!ref.failure.empty())return;
    if(ref.board_clock) {ref.failure="CX25871 manual FIELD injection conflicts with board clock owner";return;}
    if(ref.encoder->timing_selected()) {
        ref.failure="CX25871 programmed timing requires board clocks/sync, not nominal field injection";return;
    }
    ref.field.set_level(level != 0);
}
// Real board clock/sync events for the bounded synchronous profile. sync=0:
// clocks only; 1: leading HSYNC; 2: coincident leading HSYNC/VSYNC;
// 3: leading VSYNC before its associated HSYNC.
EXPORT int xlle_nv2a_reference_encoder_advance(void* instance,uint64_t clocks,unsigned sync) {
    auto& ref=*static_cast<Reference*>(instance);
    if(!ref.failure.empty())return 0;
    if(ref.board_clock) {ref.failure="CX25871 manual clocks/sync conflict with board clock owner";return 0;}
    if(sync>3) { ref.failure="invalid encoder sync event";return 0; }
    xlle::Cx25871Timing::State before;
    const bool known=ref.encoder->timing_state(before);
    if(!ref.encoder->advance_timing(clocks))return 0;
    if(sync==3 ? !ref.encoder->input_vsync() : sync && !ref.encoder->input_sync(sync==2))return 0;
    ref.sample_encoder_field(known?&before:nullptr);return 1;
}
EXPORT int xlle_nv2a_reference_encoder_timing(void* instance,uint64_t* values,int* signal) {
    auto& ref=*static_cast<Reference*>(instance);
    if(!ref.failure.empty())return 0;
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
