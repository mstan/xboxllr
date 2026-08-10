// xlle — focused PFIFO recorder / NV097 semaphore DMA test.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

#include "machine/nv2a.hpp"

static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL: %s (line %d)\n", #c, __LINE__); ++fails; } } while (0)

static void put32(std::vector<uint8_t>& mem, uint32_t addr, uint32_t value) {
    std::memcpy(mem.data() + addr, &value, 4);
}

static uint32_t get32(const std::vector<uint8_t>& mem, uint32_t addr) {
    uint32_t value = 0;
    std::memcpy(&value, mem.data() + addr, 4);
    return value;
}

static uint32_t swizzled_pixel_index(uint32_t x, uint32_t y,
                                     uint32_t width, uint32_t height) {
    uint32_t result = 0;
    uint32_t output_bit = 1;
    for (uint32_t input_bit = 1;
         input_bit < width || input_bit < height;
         input_bit <<= 1) {
        if (input_bit < width) {
            if (x & input_bit) result |= output_bit;
            output_bit <<= 1;
        }
        if (input_bit < height) {
            if (y & input_bit) result |= output_bit;
            output_bit <<= 1;
        }
    }
    return result;
}

int main() {
    std::string fault;
    xlle::Nv2a gpu([&](const std::string& why) { fault = why; });
    std::vector<uint8_t> ram(0x4000000, 0);
    gpu.set_mem_access(
        [&](uint32_t addr, void* dst, size_t n) {
            if (addr + n > ram.size()) return false;
            std::memcpy(dst, ram.data() + addr, n);
            return true;
        },
        [&](uint32_t addr, const void* src, size_t n) {
            if (addr + n > ram.size()) return false;
            std::memcpy(ram.data() + addr, src, n);
            return true;
        });

    // RAMHT handle 0 resolves to PRAMIN DMA object @0x100, which maps unified RAM @0x3000.
    gpu.mmio_write(xlle::Nv2a::kBase + 0x700000, 4, 0x00000000);
    gpu.mmio_write(xlle::Nv2a::kBase + 0x700004, 4, 0x80010010);
    // Handle 1 binds a Kelvin graphics object at PRAMIN 0x200.
    gpu.mmio_write(xlle::Nv2a::kBase + 0x700008, 4, 0x00000001);
    gpu.mmio_write(xlle::Nv2a::kBase + 0x70000C, 4, 0x80010020);
    gpu.mmio_write(xlle::Nv2a::kBase + 0x700100, 4, 0x0000003D);
    gpu.mmio_write(xlle::Nv2a::kBase + 0x700104, 4, 0x00000FFF);
    gpu.mmio_write(xlle::Nv2a::kBase + 0x700108, 4, 0x00003000);
    gpu.mmio_write(xlle::Nv2a::kBase + 0x700200, 4, 0x00000097);

    // CACHE1's pusher DMA object maps zero-based GET/PUT offsets onto the pushbuffer at
    // physical 0x2000. This is distinct from the semaphore DMA object above.
    gpu.mmio_write(xlle::Nv2a::kBase + 0x700180, 4, 0x0000003D);
    gpu.mmio_write(xlle::Nv2a::kBase + 0x700184, 4, 0x0000FFFF);
    gpu.mmio_write(xlle::Nv2a::kBase + 0x700188, 4, 0x00002000);

    uint32_t pb = 0x2000;
    put32(ram, pb + 0x00, (1u << 18) | 0x0000u);
    put32(ram, pb + 0x04, 0x00000001u);
    put32(ram, pb + 0x08, (1u << 18) | 0x01A4u);
    put32(ram, pb + 0x0C, 0x00000000u);
    put32(ram, pb + 0x10, (2u << 18) | 0x1D6Cu);
    put32(ram, pb + 0x14, 0x00000040u);
    put32(ram, pb + 0x18, 0x12345678u);

    // Enable channel 0's DMA pusher, initialize CACHE1_DMA_GET through PFIFO, then submit
    // zero-based offsets through USER DMA_PUT.
    gpu.mmio_write(xlle::Nv2a::kBase + 0x002504, 4, 1);
    gpu.mmio_write(xlle::Nv2a::kBase + 0x003204, 4, 0x100);
    gpu.mmio_write(xlle::Nv2a::kBase + 0x00322C, 4, 0x18);
    gpu.mmio_write(xlle::Nv2a::kBase + 0x003244, 4, 0);
    gpu.mmio_write(xlle::Nv2a::kBase + 0x003220, 4, 1);
    gpu.mmio_write(xlle::Nv2a::kBase + 0x003250, 4, 1);
    gpu.mmio_write(xlle::Nv2a::kBase + 0x003200, 4, 1);
    gpu.mmio_write(xlle::Nv2a::kBase + 0x800040, 4, 0x1C);

    CHECK(fault.empty());
    CHECK(gpu.mmio_read(xlle::Nv2a::kBase + 0x800044, 4) == 0x1C);
    CHECK(gpu.mmio_read(xlle::Nv2a::kBase + 0x003244, 4) == 0x1C);
    CHECK(gpu.push_methods() == 4);
    CHECK(gpu.semaphore_writes() == 1);
    CHECK(get32(ram, 0x3040) == 0x12345678u);

    // A two-deep flip queue catches up (READ=0, WRITE=1, then increment WRITE -> 0), so
    // FLIP_STALL must hold the following semaphore until the vblank ISR increments READ.
    put32(ram, pb + 0x1C, (5u << 18) | 0x0120u);
    put32(ram, pb + 0x20, 0);  // SET_FLIP_READ
    put32(ram, pb + 0x24, 1);  // SET_FLIP_WRITE
    put32(ram, pb + 0x28, 2);  // SET_FLIP_MODULO
    put32(ram, pb + 0x2C, 0);  // FLIP_INCREMENT_WRITE
    put32(ram, pb + 0x30, 0);  // FLIP_STALL
    put32(ram, pb + 0x34, (1u << 18) | 0x1D70u);
    put32(ram, pb + 0x38, 0xCAFEBABEu);
    gpu.mmio_write(xlle::Nv2a::kBase + 0x800040, 4, 0x3C);

    CHECK(fault.empty());
    CHECK(gpu.flip_increments() == 1);
    CHECK(gpu.flip_stalls() == 1);
    CHECK(gpu.waiting_for_flip());
    CHECK(gpu.mmio_read(xlle::Nv2a::kBase + 0x800044, 4) == 0x3C);
    CHECK(gpu.mmio_read(xlle::Nv2a::kBase + 0x003244, 4) == 0x3C);
    CHECK(gpu.semaphore_writes() == 1);
    CHECK(get32(ram, 0x3040) == 0x12345678u);

    gpu.mmio_write(xlle::Nv2a::kBase + 0x40071C, 4, 2);
    CHECK(!gpu.waiting_for_flip());
    CHECK(gpu.pgraph_read_increments() == 1);
    CHECK(gpu.semaphore_writes() == 2);
    CHECK(get32(ram, 0x3040) == 0xCAFEBABEu);
    CHECK(gpu.mmio_read(xlle::Nv2a::kBase + 0x800044, 4) == 0x3C);
    CHECK(gpu.pgraph_surface() == 0x21000000u);  // modulo=2, read=1, write=0

    // A nonzero NV097 NOP is the kernel's software-method notification. It raises PGRAPH
    // ERROR, traps the parameter, and holds following methods until the ISR clears it.
    bool irq = false;
    gpu.set_irq([&](int n, bool level) {
        CHECK(n == 3);
        irq = level;
    }, 3);
    gpu.mmio_write(xlle::Nv2a::kBase + 0x000140, 4, 1);
    gpu.mmio_write(xlle::Nv2a::kBase + 0x400140, 4, 1u << 20);
    put32(ram, pb + 0x3C, (1u << 18) | 0x0100u);
    put32(ram, pb + 0x40, 0x00000304u);
    put32(ram, pb + 0x44, (1u << 18) | 0x1D70u);
    put32(ram, pb + 0x48, 0x0BADF00Du);
    gpu.mmio_write(xlle::Nv2a::kBase + 0x800040, 4, 0x4C);

    CHECK(irq);
    CHECK(gpu.pgraph_notifications() == 1);
    CHECK(gpu.mmio_read(xlle::Nv2a::kBase + 0x800044, 4) == 0x4C);
    CHECK(gpu.mmio_read(xlle::Nv2a::kBase + 0x000100, 4) == (1u << 12));
    CHECK(gpu.mmio_read(xlle::Nv2a::kBase + 0x400100, 4) == (1u << 20));
    CHECK(gpu.mmio_read(xlle::Nv2a::kBase + 0x400708, 4) == 0x304u);
    CHECK(gpu.semaphore_writes() == 2);

    gpu.mmio_write(xlle::Nv2a::kBase + 0x400720, 4, 0);
    gpu.mmio_write(xlle::Nv2a::kBase + 0x400100, 4, 1u << 20);
    CHECK(!irq);
    CHECK(gpu.semaphore_writes() == 2);
    gpu.mmio_write(xlle::Nv2a::kBase + 0x400720, 4, 1);
    CHECK(gpu.semaphore_writes() == 3);
    CHECK(get32(ram, 0x3040) == 0x0BADF00Du);
    CHECK(gpu.mmio_read(xlle::Nv2a::kBase + 0x800044, 4) == 0x4C);

    // The D3D runtime disables CACHE1_PUSH0 before rebuilding a channel. PUT is cleared
    // before GET; that transient mismatch must not execute stale pushbuffer data.
    const uint64_t methods_before_reset = gpu.push_methods();
    gpu.mmio_write(xlle::Nv2a::kBase + 0x003200, 4, 0);
    gpu.mmio_write(xlle::Nv2a::kBase + 0x003240, 4, 0);
    CHECK(fault.empty());
    CHECK(gpu.push_methods() == methods_before_reset);
    gpu.mmio_write(xlle::Nv2a::kBase + 0x003244, 4, 0);
    gpu.mmio_write(xlle::Nv2a::kBase + 0x003200, 4, 1);
    CHECK(fault.empty());
    CHECK(gpu.push_methods() == methods_before_reset);

    // NV_CONTEXT_PATTERN class 0x44 SET_MONOCHROME_COLOR0 updates the guest-readable
    // NV_PGRAPH_PATT_COLOR0 shadow even though 2D rendering itself remains unimplemented.
    gpu.mmio_write(xlle::Nv2a::kBase + 0x700200, 4, 0x00000044);
    put32(ram, pb + 0x00, (1u << 18) | 0x0000u);
    put32(ram, pb + 0x04, 0x00000001u);
    put32(ram, pb + 0x08, (1u << 18) | 0x0310u);
    put32(ram, pb + 0x0C, 0x89ABCDEFu);
    gpu.mmio_write(xlle::Nv2a::kBase + 0x800040, 4, 0x10);
    CHECK(fault.empty());
    CHECK(gpu.mmio_read(xlle::Nv2a::kBase + 0x400B10, 4) == 0x89ABCDEFu);
    CHECK(gpu.mmio_read(xlle::Nv2a::kBase + 0x800044, 4) == 0x10);

    // Restore and rebind Kelvin for the remaining 3D tests.
    gpu.mmio_write(xlle::Nv2a::kBase + 0x700200, 4, 0x00000097);
    put32(ram, pb + 0x10, (1u << 18) | 0x0000u);
    put32(ram, pb + 0x14, 0x00000001u);
    gpu.mmio_write(xlle::Nv2a::kBase + 0x800040, 4, 0x18);
    CHECK(fault.empty());
    CHECK(gpu.mmio_read(xlle::Nv2a::kBase + 0x800044, 4) == 0x18);
    gpu.mmio_write(xlle::Nv2a::kBase + 0x003200, 4, 0);
    gpu.mmio_write(xlle::Nv2a::kBase + 0x003240, 4, 0);
    gpu.mmio_write(xlle::Nv2a::kBase + 0x003244, 4, 0);
    gpu.mmio_write(xlle::Nv2a::kBase + 0x003200, 4, 1);

    // Kelvin resources share zero-based unified-memory DMA objects.
    gpu.mmio_write(xlle::Nv2a::kBase + 0x700010, 4, 0x00000002);
    gpu.mmio_write(xlle::Nv2a::kBase + 0x700014, 4, 0x80010030);
    gpu.mmio_write(xlle::Nv2a::kBase + 0x700018, 4, 0x00000003);
    gpu.mmio_write(xlle::Nv2a::kBase + 0x70001C, 4, 0x80010040);
    gpu.mmio_write(xlle::Nv2a::kBase + 0x700300, 4, 0x0000003D);
    gpu.mmio_write(xlle::Nv2a::kBase + 0x700304, 4, 0x03FFFFFF);
    gpu.mmio_write(xlle::Nv2a::kBase + 0x700308, 4, 0x00000000);
    gpu.mmio_write(xlle::Nv2a::kBase + 0x700400, 4, 0x0000003D);
    gpu.mmio_write(xlle::Nv2a::kBase + 0x700404, 4, 0x03FFFFFF);
    gpu.mmio_write(xlle::Nv2a::kBase + 0x700408, 4, 0x00000000);
    uint32_t cursor = 0;
    auto emit = [&](uint32_t method, uint32_t parameter) {
        put32(ram, pb + cursor, (1u << 18) | method);
        put32(ram, pb + cursor + 4, parameter);
        cursor += 8;
    };
    emit(0x0000, 1);       // bind Kelvin class 0x97
    emit(0x0194, 2);       // color DMA object
    emit(0x0198, 3);       // zeta DMA object
    emit(0x0208, 0x0128);  // non-AA pitch A8R8G8B8 + Z24S8
    emit(0x020C, 0x00100010);  // four color/zeta pixels per row
    emit(0x0210, 0x5000);  // color offset
    emit(0x0214, 0x6000);  // zeta offset
    emit(0x1D90, 0xA1B2C3D4);
    emit(0x1D8C, 0xABCDEF42);
    emit(0x1D98, 0x00020001);  // x=1..2 inclusive
    emit(0x1D9C, 0x00020001);  // y=1..2 inclusive
    emit(0x1D94, 0x000000F0);  // clear RGBA
    put32(ram, 0x5000, 0x11223344);
    emit(0x1D90, 0xAABBCCDD);
    emit(0x1D98, 0x00000000);
    emit(0x1D9C, 0x00000000);
    emit(0x1D94, 0x00000013);  // clear red + Z24 + stencil
    gpu.mmio_write(xlle::Nv2a::kBase + 0x800040, 4, cursor);
    CHECK(fault.empty());
    CHECK(gpu.color_clears() == 2);
    CHECK(gpu.color_clear_pixels() == 5);
    CHECK(gpu.last_color_clear_address() == 0x5000);
    CHECK(gpu.zeta_clears() == 1);
    CHECK(gpu.zeta_clear_pixels() == 1);
    CHECK(gpu.last_zeta_clear_address() == 0x6000);
    CHECK(gpu.method_count(0x1D94) == 2);
    CHECK(gpu.class_method_count(0x44) == 2);
    CHECK(gpu.class_method_count(0x97) + gpu.class_method_count(0x44) ==
          gpu.push_methods());
    CHECK(gpu.class_method_count(0) == 0);
    CHECK(get32(ram, 0x5000) == 0x11BB3344);
    CHECK(get32(ram, 0x5010) == 0);
    CHECK(get32(ram, 0x5014) == 0xA1B2C3D4);
    CHECK(get32(ram, 0x5018) == 0xA1B2C3D4);
    CHECK(get32(ram, 0x5024) == 0xA1B2C3D4);
    CHECK(get32(ram, 0x5028) == 0xA1B2C3D4);
    CHECK(get32(ram, 0x502C) == 0);
    CHECK(get32(ram, 0x6000) == 0xABCDEF42);
    CHECK(gpu.mmio_read(xlle::Nv2a::kBase + 0x401A88, 4) == 0xABCDEF42);
    CHECK(gpu.mmio_read(xlle::Nv2a::kBase + 0x40186C, 4) == 0xAABBCCDD);

    // The opt-in trace ignores a clear to another surface, then selects the first Kelvin
    // BEGIN_END(nonzero) after a matching clear.  Its 32-record preamble ends at BEGIN and
    // it closes only at that channel/subchannel's matching END.
    gpu.arm_color_clear_trace(0x5000);
    emit(0x0210, 0x5040);
    emit(0x1D94, 0x000000F0);  // successful, but nonmatching color clear
    gpu.mmio_write(xlle::Nv2a::kBase + 0x800040, 4, cursor);
    CHECK(fault.empty());
    CHECK(!gpu.color_clear_trace_triggered());
    std::vector<xlle::Nv2a::PushMethod> clear_trace;
    CHECK(gpu.color_clear_trace(clear_trace) == 0);

    emit(0x0210, 0x5000);
    emit(0x1D94, 0x000000F0);  // first matching successful color clear
    gpu.mmio_write(xlle::Nv2a::kBase + 0x800040, 4, cursor);
    CHECK(fault.empty());
    CHECK(gpu.color_clear_trace_triggered());
    CHECK(!gpu.color_clear_trace_begin_triggered());
    CHECK(!gpu.color_clear_trace_complete());

    // A BEGIN on another object class must not select the trace.
    gpu.mmio_write(xlle::Nv2a::kBase + 0x700200, 4, 0x00000062);
    emit(0x0000, 1);
    emit(0x17FC, 5);
    emit(0x17FC, 0);
    gpu.mmio_write(xlle::Nv2a::kBase + 0x800040, 4, cursor);
    CHECK(fault.empty());
    CHECK(!gpu.color_clear_trace_begin_triggered());

    gpu.mmio_write(xlle::Nv2a::kBase + 0x700200, 4, 0x00000097);
    emit(0x0000, 1);
    for (uint32_t i = 0; i < 31; ++i)
        emit(0x1E94, i);  // exactly 31 records before the selected BEGIN
    emit(0x17FC, 5);
    emit(0x1880, 0x3f800000);  // generic immediate attribute: trace it without committing
    emit(0x17FC, 0);
    gpu.mmio_write(xlle::Nv2a::kBase + 0x800040, 4, cursor);
    CHECK(fault.empty());
    CHECK(gpu.color_clear_trace_begin_triggered());
    CHECK(gpu.color_clear_trace_complete());
    CHECK(!gpu.color_clear_trace_truncated());
    CHECK(gpu.color_clear_trace_pre_count() == 32);
    CHECK(gpu.color_clear_trace(clear_trace) == 34);
    CHECK(clear_trace.size() == 34);
    CHECK(clear_trace.front().method == 0x1E94 && clear_trace.front().parameter == 0);
    CHECK(clear_trace[30].method == 0x1E94 && clear_trace[30].parameter == 30);
    CHECK(clear_trace[31].method == 0x17FC && clear_trace[31].parameter == 5);
    CHECK(clear_trace[31].seq == gpu.color_clear_trace_begin_seq());
    CHECK(clear_trace[32].method == 0x1880 && clear_trace[32].parameter == 0x3f800000);
    CHECK(clear_trace.back().method == 0x17FC && clear_trace.back().parameter == 0);
    CHECK(clear_trace.front().object_class == 0x97 &&
          clear_trace.back().object_class == 0x97);
    CHECK(clear_trace.front().parser_put == clear_trace.back().parser_put);
    CHECK(clear_trace.front().parser_get < clear_trace.back().parser_get);

    const uint64_t trigger_seq = gpu.color_clear_trace_trigger_seq();
    const uint64_t begin_seq = gpu.color_clear_trace_begin_seq();
    emit(0x1D94, 0x000000F0);  // matching again, but the completed trace must not retrigger
    emit(0x17FC, 5);
    emit(0x17FC, 0);
    gpu.mmio_write(xlle::Nv2a::kBase + 0x800040, 4, cursor);
    CHECK(fault.empty());
    CHECK(gpu.color_clear_trace_trigger_seq() == trigger_seq);
    CHECK(gpu.color_clear_trace_begin_seq() == begin_seq);
    CHECK(gpu.color_clear_trace(clear_trace) == 34);

    // Dominant dashboard draw path: vertex DMA A/B, array formats/offsets, BEGIN/END,
    // packed 16-bit indices plus one 32-bit index, unified-memory fetch, and assembly.
    const float positions[] = {
        10.0f, 20.0f, 0.0f,
        30.0f, 20.0f, 0.0f,
        20.0f, 40.0f, 0.0f,
    };
    const uint32_t colors[] = {0xff0000ffu, 0xff00ff00u, 0xffff0000u};
    std::memcpy(ram.data() + 0x7000, positions, sizeof(positions));
    std::memcpy(ram.data() + 0x7100, colors, sizeof(colors));
    emit(0x019C, 2);       // vertex DMA A
    emit(0x01A0, 3);       // vertex DMA B
    emit(0x1720, 0x00007000);  // position slot 0, DMA A
    emit(0x1760, 0x00000C32);  // float3, stride 12
    emit(0x172C, 0x80007100);  // diffuse slot 3, DMA B
    emit(0x176C, 0x00000440);  // UB_D3D4, stride 4
    emit(0x1E9C, 0);       // transform program load pointer
    emit(0x0B00, 0x00000000);  // MOV oPos, v0 (final)
    emit(0x0B04, 0x0020001B);
    emit(0x0B08, 0x0836106C);
    emit(0x0B0C, 0x2070F801);
    emit(0x1EA0, 0);       // transform program start
    emit(0x1EA4, 0);       // transform constant load pointer
    emit(0x0B80, 0x3f800000);
    emit(0x0B84, 0x40000000);
    emit(0x0B88, 0x40400000);
    emit(0x0B8C, 0x40800000);
    emit(0x1E94, 2);       // programmable transform mode
    emit(0x17FC, 5);       // triangles
    emit(0x1800, 0x00010000);  // indices 0, 1
    emit(0x1808, 2);       // index 2
    emit(0x17FC, 0);       // END: fetch and assemble
    gpu.mmio_write(xlle::Nv2a::kBase + 0x800040, 4, cursor);
    CHECK(fault.empty());
    CHECK(gpu.vertex_batches() == 1);
    CHECK(gpu.submitted_vertices() == 3);
    CHECK(gpu.vertex_attribute_fetches() == 6);
    CHECK(gpu.assembled_primitives() == 1);
    CHECK(gpu.assembled_triangles() == 1);
    CHECK(gpu.largest_vertex_index() == 2);
    CHECK(gpu.primitive_mode_count(5) == 1);
    CHECK(gpu.vertex_format_fetches(0) == 3);
    CHECK(gpu.vertex_format_fetches(2) == 3);
    CHECK(gpu.vertex_slot_fetches(0) == 3);
    CHECK(gpu.vertex_slot_fetches(3) == 3);
    CHECK(gpu.transform_mode_batches(2) == 1);
    CHECK(gpu.transformed_vertices() == 3);
    CHECK(gpu.transform_program_words() == 4);
    CHECK(gpu.transform_constant_words() == 4);
    CHECK(gpu.transform_output_hash() != 1469598103934665603ull);

    // Exercised dashboard fixed-function subset: no skinning/lights/texgen,
    // separate specular, composite transform, ambient/material color.
    const float specular_values[] = {
        0.1f, 0.2f, 0.3f, 0.125f,
        0.2f, 0.3f, 0.4f, 0.125f,
        0.3f, 0.4f, 0.5f, 0.125f,
    };
    std::memcpy(ram.data() + 0x7200, specular_values,
                sizeof(specular_values));
    emit(0x1730, 0x00007200);  // specular slot 4, DMA A
    emit(0x1770, 0x00001042);  // float4, stride 16
    emit(0x0294, 1);       // separate specular
    emit(0x0298, 0);       // all color sources are material
    emit(0x02A4, 0);       // fog disabled
    emit(0x0314, 1);       // lighting enabled
    emit(0x0328, 0);       // skinning disabled
    emit(0x03A4, 0);       // normalization disabled
    emit(0x03A8, 0x3F000000);  // material emission = (0.5, 1, 2)
    emit(0x03AC, 0x3F800000);
    emit(0x03B0, 0x40000000);
    emit(0x03B4, 0x3E800000);  // material alpha = 0.25
    emit(0x03B8, 1);       // specular enabled
    emit(0x03BC, 0);       // all lights off
    const uint32_t composite[] = {
        0x40000000, 0, 0, 0,
        0, 0x40400000, 0, 0,
        0, 0, 0x3F800000, 0,
        0, 0, 0, 0x40000000,
    };
    for (uint32_t i = 0; i < 16; ++i)
        emit(0x0680 + i * 4, composite[i]);
    emit(0x0A10, 0x3E4CCCCD);  // scene ambient = (0.2, 0.3, 0.4)
    emit(0x0A14, 0x3E99999A);
    emit(0x0A18, 0x3ECCCCCD);
    emit(0x0A20, 0x3DCCCCCD);  // viewport offset = (0.1, -0.1)
    emit(0x0A24, 0xBDCCCCCD);
    emit(0x1E94, 0);       // fixed-function transform mode
    emit(0x17FC, 5);
    emit(0x1800, 0x00010000);
    emit(0x1808, 2);
    emit(0x17FC, 0);
    gpu.mmio_write(xlle::Nv2a::kBase + 0x800040, 4, cursor);
    CHECK(fault.empty());
    CHECK(gpu.vertex_batches() == 2);
    CHECK(gpu.transform_mode_batches(0) == 1);
    CHECK(gpu.fixed_skin_batches(0) == 1);
    CHECK(gpu.fixed_lighting_batches() == 1);
    CHECK(gpu.fixed_specular_batches() == 1);
    CHECK(gpu.fixed_light_type_batches(0) == 8);
    CHECK(gpu.fixed_color_material_batches(0) == 1);
    CHECK(gpu.fixed_light_control_batches(1) == 1);
    CHECK(gpu.transformed_vertices() == 6);
    const auto fixed_position = gpu.last_transformed_position();
    CHECK(fixed_position[0] == 20.0625f);
    CHECK(fixed_position[1] == 59.875f);
    CHECK(fixed_position[2] == 0.0f);
    CHECK(fixed_position[3] == 2.0f);
    const auto fixed_diffuse = gpu.last_transformed_diffuse();
    CHECK(std::abs(fixed_diffuse[0] - 0.3f) < 0.000001f);
    CHECK(std::abs(fixed_diffuse[1] - 0.6f) < 0.000001f);
    CHECK(std::abs(fixed_diffuse[2] - 1.2f) < 0.000001f);
    CHECK(fixed_diffuse[3] == 0.25f);
    const std::array<float, 4> fixed_specular =
        {0.0f, 0.0f, 0.0f, 1.0f};
    CHECK(gpu.last_transformed_specular() == fixed_specular);

    // The dashboard also briefly disables lighting/specular while retaining
    // alpha-from-material-specular in LIGHT_CONTROL.
    emit(0x0294, 0x00020001);
    emit(0x0314, 0);
    emit(0x03B8, 0);
    emit(0x17FC, 5);
    emit(0x1800, 0x00010000);
    emit(0x1808, 2);
    emit(0x17FC, 0);
    gpu.mmio_write(xlle::Nv2a::kBase + 0x800040, 4, cursor);
    CHECK(fault.empty());
    CHECK(gpu.vertex_batches() == 3);
    CHECK(gpu.transform_mode_batches(0) == 2);
    CHECK(gpu.fixed_lighting_batches() == 1);
    CHECK(gpu.fixed_specular_batches() == 1);
    CHECK(gpu.fixed_light_type_batches(0) == 16);
    CHECK(gpu.fixed_light_control_batches(5) == 1);
    CHECK(gpu.transformed_vertices() == 9);
    const std::array<float, 4> unlit_diffuse =
        {0.0f, 0.0f, 1.0f, 1.0f};
    CHECK(gpu.last_transformed_diffuse() == unlit_diffuse);
    CHECK(gpu.last_transformed_specular() == fixed_specular);

    // First software triangle path: the dashboard's untextured pitch-Z16
    // depth prepass with CW/back-face culling and LEQUAL writes.
    const float depth_positions[] = {
        10.0f, 20.0f, 100.75f,
        30.0f, 20.0f, 100.75f,
        20.0f, 40.0f, 100.75f,
    };
    std::memcpy(ram.data() + 0x7000, depth_positions,
                sizeof(depth_positions));
    std::memset(ram.data() + 0x8000, 0xFF, 128 * 64);
    emit(0x0200, 0x00400000);  // x=0, width=64
    emit(0x0204, 0x00400000);  // y=0, height=64
    emit(0x0208, 0x00000113);  // pitch R5G6B5 + fixed Z16
    emit(0x020C, 0x00800080);  // color/zeta pitch = 128
    emit(0x0214, 0x00008000);  // zeta offset
    emit(0x0290, 1);           // fixed Z, non-perspective
    emit(0x02C0, 0x003F0000);  // window x=0..63
    emit(0x02E0, 0x003F0000);  // window y=0..63
    emit(0x0300, 0);           // alpha test disabled
    emit(0x0304, 0);           // blending disabled
    emit(0x0308, 1);           // culling enabled
    emit(0x030C, 1);           // depth test enabled
    emit(0x0310, 0);
    emit(0x032C, 0);           // stencil test disabled
    emit(0x0338, 1);           // polygon offset fill enabled
    emit(0x0354, 0x0203);      // LEQUAL
    emit(0x0358, 0);           // no color writes
    emit(0x035C, 1);           // depth writes enabled
    emit(0x037C, 0x1D01);      // smooth shading
    emit(0x0384, 0x00000000);  // slope factor 0
    emit(0x0388, 0x41200000);  // constant Z bias 10
    emit(0x038C, 0x1B02);      // front fill
    emit(0x0390, 0x1B02);      // back fill
    emit(0x0394, 0x00000000);  // Z clip 0..65535
    emit(0x0398, 0x477FFF00);
    emit(0x039C, 0x0405);      // cull back
    emit(0x03A0, 0x0900);      // CW front face
    emit(0x1D78, 1);           // cull outside Z clip
    emit(0x1E94, 2);           // programmable MOV oPos,v0
    emit(0x17FC, 5);
    emit(0x1800, 0x00010000);
    emit(0x1808, 2);
    emit(0x17FC, 0);
    gpu.mmio_write(xlle::Nv2a::kBase + 0x800040, 4, cursor);
    CHECK(fault.empty());
    CHECK(gpu.vertex_batches() == 4);
    CHECK(gpu.depth_raster_batches() == 1);
    CHECK(gpu.rasterized_triangles() == 1);
    CHECK(gpu.rasterized_fragments() > 0);
    CHECK(gpu.depth_test_passes() == gpu.depth_writes());
    CHECK(gpu.depth_writes() > 0);
    uint16_t inside_depth = 0, outside_depth = 0;
    std::memcpy(&inside_depth, ram.data() + 0x8000 + 25 * 128 + 20 * 2, 2);
    std::memcpy(&outside_depth, ram.data() + 0x8000 + 5 * 128 + 5 * 2, 2);
    CHECK(inside_depth == 110);  // floor(100.75 + 10)
    CHECK(outside_depth == 0xFFFF);
    CHECK(gpu.last_raster_address() >= 0x8000);
    CHECK(gpu.last_raster_address() < 0xA000);
    CHECK(gpu.raster_output_hash() != 1469598103934665603ull);

    const uint64_t front_writes = gpu.depth_writes();
    emit(0x17FC, 5);
    emit(0x1800, 0x00020000);  // reverse winding: 0, 2
    emit(0x1808, 1);
    emit(0x17FC, 0);
    gpu.mmio_write(xlle::Nv2a::kBase + 0x800040, 4, cursor);
    CHECK(fault.empty());
    CHECK(gpu.depth_raster_batches() == 2);
    CHECK(gpu.rasterized_triangles() == 1);
    CHECK(gpu.depth_writes() == front_writes);
    std::memcpy(&inside_depth, ram.data() + 0x8000 + 25 * 128 + 20 * 2, 2);
    CHECK(inside_depth == 110);

    // Dominant color profile: a swizzled A8R8G8B8/Z24S8 target and the
    // dashboard's untextured three-stage radial register combiner. Different
    // vertex W values make the expected diffuse interpolation perspective-correct.
    const float radial_positions[] = {
        10.0f, 20.0f, 100.0f, 1.0f,
        60.0f, 40.0f, 200.0f, 2.0f,
        80.0f, 160.0f, 400.0f, 4.0f,
    };
    const uint32_t radial_colors[] = {
        0xff0000ffu, 0xff00ff00u, 0xffff0000u,
    };
    const float radial_texcoords[] = {
        0.75f, 0.5f, 0.5f, 1.0f,
        0.75f, 0.5f, 0.5f, 1.0f,
        0.75f, 0.5f, 0.5f, 1.0f,
    };
    std::memcpy(ram.data() + 0x7000, radial_positions,
                sizeof(radial_positions));
    std::memcpy(ram.data() + 0x7100, radial_colors,
                sizeof(radial_colors));
    std::memcpy(ram.data() + 0x7400, radial_texcoords,
                sizeof(radial_texcoords));
    std::memset(ram.data() + 0xA000, 0, 64 * 64 * 4);
    for (uint32_t offset = 0; offset < 64 * 64 * 4; offset += 4)
        put32(ram, 0xE000 + offset, 0xFFFFFF5Au);

    emit(0x0200, 0x00400000);
    emit(0x0204, 0x00400000);
    emit(0x0208, 0x06060228);  // 64x64 swizzled A8R8G8B8 + Z24S8
    emit(0x020C, 0x01000100);
    emit(0x0210, 0x0000A000);
    emit(0x0214, 0x0000E000);
    emit(0x0290, 0x00100001);  // fixed Z, preserve stencil
    emit(0x02C0, 0x003F0000);
    emit(0x02E0, 0x003F0000);
    emit(0x0300, 1);           // alpha GEQUAL 1; final alpha is one
    emit(0x0304, 1);           // blend ONE/ZERO ADD
    emit(0x0308, 1);
    emit(0x030C, 1);
    emit(0x0310, 0);
    emit(0x032C, 0);
    emit(0x0330, 0);
    emit(0x0334, 0);
    emit(0x0338, 0);
    emit(0x033C, 0x0206);
    emit(0x0340, 1);
    emit(0x0344, 1);
    emit(0x0348, 0);
    emit(0x0350, 0x8006);
    emit(0x0354, 0x0203);
    emit(0x0358, 0x01010101);
    emit(0x035C, 1);
    emit(0x037C, 0x1D01);
    emit(0x038C, 0x1B02);
    emit(0x0390, 0x1B02);
    emit(0x0394, 0x00000000);
    emit(0x0398, 0x4B7FFFFF);
    emit(0x039C, 0x0405);
    emit(0x03A0, 0x0900);
    emit(0x1D78, 1);
    emit(0x1E70, 0x00000004);  // stage 0 passthrough
    emit(0x1E74, 0);
    emit(0x1E78, 0);
    emit(0x1E60, 0x00011103);
    emit(0x0AC0, 0x48480000);
    emit(0x0AC4, 0x2D2D0000);
    emit(0x0AC8, 0xC4CD0000);
    emit(0x0260, 0x00000000);
    emit(0x0264, 0x3D3D1010);
    emit(0x0268, 0xD4DD1010);
    emit(0x1E40, 0x000820D0);
    emit(0x1E44, 0x000000D0);
    emit(0x1E48, 0x000000C0);
    emit(0x0AA0, 0x00000000);
    emit(0x0AA4, 0x000000D0);
    emit(0x0AA8, 0x000000C0);
    emit(0x0288, 0x0000000C);
    emit(0x028C, 0x00002080);
    emit(0x1720, 0x00007000);
    emit(0x1760, 0x00001042);  // float4 position, stride 16
    emit(0x172C, 0x80007100);
    emit(0x176C, 0x00000440);
    emit(0x1744, 0x00007400);  // texture 0, slot 9
    emit(0x1784, 0x00001042);
    const uint32_t identity[] = {
        0x3F800000, 0, 0, 0,
        0, 0x3F800000, 0, 0,
        0, 0, 0x3F800000, 0,
        0, 0, 0, 0x3F800000,
    };
    for (uint32_t i = 0; i < 16; ++i)
        emit(0x0680 + i * 4, identity[i]);
    emit(0x0A20, 0);
    emit(0x0A24, 0);
    emit(0x1E94, 0);
    emit(0x17FC, 5);
    emit(0x1800, 0x00010000);
    emit(0x1808, 2);
    emit(0x17FC, 0);
    gpu.mmio_write(xlle::Nv2a::kBase + 0x800040, 4, cursor);
    CHECK(fault.empty());
    CHECK(gpu.radial_color_batches() == 1);
    CHECK(gpu.radial_color_triangles() == 1);
    CHECK(gpu.radial_color_fragments() > 0);
    CHECK(gpu.radial_color_depth_passes() ==
          gpu.radial_color_depth_writes());
    CHECK(gpu.radial_color_depth_writes() == gpu.radial_color_writes());
    CHECK(gpu.radial_color_writes() > 0);
    const uint32_t inside_swizzled =
        swizzled_pixel_index(20, 25, 64, 64) * 4;
    const uint32_t outside_swizzled =
        swizzled_pixel_index(5, 5, 64, 64) * 4;
    CHECK(get32(ram, 0xA000 + inside_swizzled) == 0xFF512E10u);
    CHECK(get32(ram, 0xE000 + inside_swizzled) == 0x0000645Au);
    CHECK(get32(ram, 0xA000 + outside_swizzled) == 0);
    CHECK(get32(ram, 0xE000 + outside_swizzled) == 0xFFFFFF5Au);
    CHECK(gpu.last_color_raster_address() >= 0xA000);
    CHECK(gpu.last_color_raster_address() < 0xE000);
    CHECK(gpu.radial_color_output_hash() != 1469598103934665603ull);

    const uint64_t radial_writes = gpu.radial_color_writes();
    float farther_positions[12];
    std::memcpy(farther_positions, radial_positions, sizeof(farther_positions));
    farther_positions[2] = 200.0f;
    farther_positions[6] = 400.0f;
    farther_positions[10] = 800.0f;
    std::memcpy(ram.data() + 0x7000, farther_positions,
                sizeof(farther_positions));
    emit(0x17FC, 5);
    emit(0x1800, 0x00010000);
    emit(0x1808, 2);
    emit(0x17FC, 0);
    gpu.mmio_write(xlle::Nv2a::kBase + 0x800040, 4, cursor);
    CHECK(fault.empty());
    CHECK(gpu.radial_color_batches() == 2);
    CHECK(gpu.radial_color_triangles() == 2);
    CHECK(gpu.radial_color_writes() == radial_writes);
    CHECK(get32(ram, 0xA000 + inside_swizzled) == 0xFF512E10u);
    CHECK(get32(ram, 0xE000 + inside_swizzled) == 0x0000645Au);

    // High-volume textured profile: stages 1 and 2 cube-remap distinct
    // directions into the same clamp-to-edge, bilinear X8R8G8B8 image.
    // The remaining enabled stages are NONE and shadow-NEVER, so they fetch
    // nothing. Four deliberately different texels verify the Morton address
    // and half-texel bilinear convention before the exact eight-stage combiner.
    const uint32_t textured_colors[] = {
        0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu,
    };
    const float texture1_coords[] = {
        17.0f / 64.0f, 17.0f / 64.0f, 1.0f, 1.0f,
        17.0f / 64.0f, 17.0f / 64.0f, 1.0f, 1.0f,
        17.0f / 64.0f, 17.0f / 64.0f, 1.0f, 1.0f,
    };
    const float texture2_coords[] = {
        49.0f / 64.0f, 49.0f / 64.0f, 1.0f, 1.0f,
        49.0f / 64.0f, 49.0f / 64.0f, 1.0f, 1.0f,
        49.0f / 64.0f, 49.0f / 64.0f, 1.0f, 1.0f,
    };
    std::memcpy(ram.data() + 0x7000, radial_positions,
                sizeof(radial_positions));
    std::memcpy(ram.data() + 0x7100, textured_colors,
                sizeof(textured_colors));
    std::memcpy(ram.data() + 0x7500, texture1_coords,
                sizeof(texture1_coords));
    std::memcpy(ram.data() + 0x7600, texture2_coords,
                sizeof(texture2_coords));
    std::memset(ram.data() + 0x20000, 0, 256 * 256 * 4);
    for (uint32_t offset = 0; offset < 256 * 256 * 4; offset += 4)
        put32(ram, 0x60000 + offset, 0xFFFFFF5Au);
    std::memset(ram.data() + 0xA0000, 0, 64 * 64 * 4);
    const auto put_texture = [&](uint32_t x, uint32_t y, uint8_t blue) {
        put32(ram, 0xA0000 + swizzled_pixel_index(x, y, 64, 64) * 4,
              0xFF000000u | blue);
    };
    put_texture(16, 16, 128);
    put_texture(17, 16, 255);
    put_texture(16, 17, 128);
    put_texture(17, 17, 253);  // bilinear blue = 191
    put_texture(48, 48, 255);
    put_texture(49, 48, 255);
    put_texture(48, 49, 255);
    put_texture(49, 49, 255);

    emit(0x0184, 2);           // texture DMA A
    emit(0x0188, 3);           // texture DMA B
    emit(0x0200, 0x01000000);  // 256x256 clip
    emit(0x0204, 0x01000000);
    emit(0x0208, 0x08080228);  // 256x256 swizzled A8R8G8B8 + Z24S8
    emit(0x020C, 0x04000400);
    emit(0x0210, 0x00020000);
    emit(0x0214, 0x00060000);
    emit(0x02C0, 0x00FF0000);
    emit(0x02E0, 0x00FF0000);
    emit(0x0300, 0);           // alpha test disabled
    emit(0x0304, 0);           // blending disabled
    emit(0x033C, 0x0207);      // ALWAYS, reference zero
    emit(0x0340, 0);
    emit(0x1E6C, 0);           // shadow compare NEVER
    emit(0x1E70, 0x00010C60);  // NONE, CUBEMAP, CUBEMAP, PROJECT3D
    emit(0x1E74, 0);
    emit(0x1E78, 0);
    emit(0x1E60, 0x00011108);

    for (uint32_t stage = 0; stage < 4; ++stage) {
        const uint32_t base = 0x1B00 + stage * 0x40;
        emit(base + 0x00, stage == 1 || stage == 2 ? 0x000A0000 : 0);
        emit(base + 0x04,
             stage == 1 || stage == 2 ? 0x0661072E :
             stage == 0 ? 0x0771062A : 0x0001302A);
        emit(base + 0x08,
             stage == 1 || stage == 2 ? 0x00030303 : 0);
        emit(base + 0x0C, 0x4003FFC0);
        emit(base + 0x14, 0x02022000);
        emit(base + 0x20, 0);
        emit(base + 0x24, stage == 3 ? 0xFFFFFFFF : 0);
    }
    const uint32_t textured_color_icw[] = {
        0xC149C14A, 0x0D0D0000, 0xCDCD0000, 0xCDCD0000,
        0xCDCD0000, 0xCDCD0000, 0xC10CC20D, 0xC40520C1,
    };
    const uint32_t textured_alpha_icw[] = {
        0xD159D15A, 0, 0, 0, 0, 0, 0, 0,
    };
    const uint32_t textured_color_ocw[] = {
        0x000C30CD, 0x000000D0, 0x000000D0, 0x000000D0,
        0x000000D0, 0x000000D0, 0x000005CD, 0x00000C00,
    };
    const uint32_t textured_factor0[] = {
        0xFF0000FF, 0, 0, 0, 0, 0, 0xFF072103, 0xFF000000,
    };
    const uint32_t textured_factor1[] = {
        0, 0, 0, 0, 0, 0, 0xFF35FF1A, 0,
    };
    for (uint32_t stage = 0; stage < 8; ++stage) {
        emit(0x0AC0 + stage * 4, textured_color_icw[stage]);
        emit(0x0260 + stage * 4, textured_alpha_icw[stage]);
        emit(0x1E40 + stage * 4, textured_color_ocw[stage]);
        emit(0x0AA0 + stage * 4, 0);
        emit(0x0A60 + stage * 4, textured_factor0[stage]);
        emit(0x0A80 + stage * 4, textured_factor1[stage]);
    }
    emit(0x0288, 0x0C0B000F);
    emit(0x028C, 0x0C011180);
    emit(0x1E20, 0xFF3F3F3F);
    emit(0x1E24, 0);
    emit(0x172C, 0x80007100);
    emit(0x1748, 0x00007500);  // texture 1, slot 10
    emit(0x174C, 0x00007600);  // texture 2, slot 11
    emit(0x1788, 0x00001042);
    emit(0x178C, 0x00001042);
    emit(0x17FC, 5);
    emit(0x1800, 0x00010000);
    emit(0x1808, 2);
    emit(0x17FC, 0);
    gpu.mmio_write(xlle::Nv2a::kBase + 0x800040, 4, cursor);
    CHECK(fault.empty());
    CHECK(gpu.textured_color_batches() == 1);
    CHECK(gpu.textured_color_triangles() == 1);
    CHECK(gpu.textured_color_fragments() > 0);
    CHECK(gpu.textured_color_depth_passes() ==
          gpu.textured_color_depth_writes());
    CHECK(gpu.textured_color_depth_writes() ==
          gpu.textured_color_writes());
    CHECK(gpu.texture_samples() == gpu.textured_color_writes() * 2);
    CHECK(gpu.textured_color_writes() > 0);
    const uint32_t textured_inside =
        swizzled_pixel_index(20, 25, 256, 256) * 4;
    CHECK(get32(ram, 0x20000 + textured_inside) == 0xFF0E3F07u);
    CHECK(get32(ram, 0x60000 + textured_inside) == 0x0000645Au);
    CHECK(gpu.last_textured_raster_address() >= 0x20000);
    CHECK(gpu.last_textured_raster_address() < 0x60000);
    CHECK(gpu.textured_color_output_hash() != 1469598103934665603ull);

    const uint64_t textured_writes = gpu.textured_color_writes();
    std::memcpy(ram.data() + 0x7000, farther_positions,
                sizeof(farther_positions));
    emit(0x17FC, 5);
    emit(0x1800, 0x00010000);
    emit(0x1808, 2);
    emit(0x17FC, 0);
    gpu.mmio_write(xlle::Nv2a::kBase + 0x800040, 4, cursor);
    CHECK(fault.empty());
    CHECK(gpu.textured_color_batches() == 2);
    CHECK(gpu.textured_color_triangles() == 2);
    CHECK(gpu.textured_color_writes() == textured_writes);
    CHECK(get32(ram, 0x20000 + textured_inside) == 0xFF0E3F07u);

    // The dominant pitch-surface sibling keeps the same cube samples and
    // eight-stage combiner, but varies the stage-6 RGB weights and targets
    // linear A8R8G8B8/Z24S8 buffers. Its enabled PROJECT3D Y16 shadow stage
    // is output-inert because no combiner input references texture 3.
    std::memcpy(ram.data() + 0x7000, radial_positions,
                sizeof(radial_positions));
    std::memset(ram.data() + 0x20000, 0, 64 * 64 * 4);
    for (uint32_t offset = 0; offset < 64 * 64 * 4; offset += 4)
        put32(ram, 0x60000 + offset, 0xFFFFFF5Au);
    emit(0x0200, 0x00400000);  // 64x64 clip
    emit(0x0204, 0x00400000);
    emit(0x0208, 0x00000128);  // pitch A8R8G8B8 + Z24S8
    emit(0x020C, 0x01000100);
    emit(0x0210, 0x00020000);
    emit(0x0214, 0x00060000);
    emit(0x02C0, 0x003F0000);
    emit(0x02E0, 0x003F0000);
    emit(0x033C, 0x0206);      // GEQUAL, reference one
    emit(0x0340, 1);
    emit(0x035C, 0xFFFFFFFF);  // witnessed TRUE encoding
    emit(0x1E6C, 4);           // GREATER shadow comparison
    emit(0x1BC0, 0);
    emit(0x1BC4, 0x0001302A);  // linear Y16 fixed
    emit(0x1BC8, 0x00030404);
    emit(0x1BCC, 0x4003FFC0);
    emit(0x1BD0, 0x04000000);  // 1024-byte pitch
    emit(0x1BD4, 0x02022000);
    emit(0x1BDC, 0x02000200);  // 512x512 rectangle
    emit(0x0A78, 0xFF0D3D06);  // witnessed dynamic stage-6 c0
    emit(0x0A98, 0xFF61FF2F);  // witnessed dynamic stage-6 c1
    emit(0x17FC, 5);
    emit(0x1800, 0x00010000);
    emit(0x1808, 2);
    emit(0x17FC, 0);
    gpu.mmio_write(xlle::Nv2a::kBase + 0x800040, 4, cursor);
    CHECK(fault.empty());
    CHECK(gpu.pitch_textured_color_batches() == 1);
    CHECK(gpu.pitch_textured_color_triangles() == 1);
    CHECK(gpu.pitch_textured_color_fragments() > 0);
    CHECK(gpu.pitch_textured_color_depth_passes() ==
          gpu.pitch_textured_color_depth_writes());
    CHECK(gpu.pitch_textured_color_depth_writes() ==
          gpu.pitch_textured_color_writes());
    CHECK(gpu.pitch_texture_samples() ==
          gpu.pitch_textured_color_writes() * 2);
    CHECK(gpu.pitch_textured_color_writes() > 0);
    const uint32_t pitch_textured_inside = 25 * 256 + 20 * 4;
    CHECK(get32(ram, 0x20000 + pitch_textured_inside) == 0xFF1A3F0Cu);
    CHECK(get32(ram, 0x60000 + pitch_textured_inside) == 0x0000645Au);
    CHECK(gpu.last_pitch_textured_raster_address() >= 0x20000);
    CHECK(gpu.last_pitch_textured_raster_address() < 0x24000);
    CHECK(gpu.pitch_textured_color_output_hash() !=
          1469598103934665603ull);
    CHECK(gpu.textured_color_batches() == 2);
    CHECK(gpu.textured_color_writes() == textured_writes);

    // The lower-volume sibling projects stage 0 with repeat addressing, then
    // dots its expanded RGB against both cube-remapped samples. Coordinates
    // just above one wrap to the first four 128x128 texels; unequal W makes
    // the PROJECT2D division observable.
    const float projected_texture0_coords[] = {
        2.015625f, 2.015625f, 0.0f, 2.0f,
        2.015625f, 2.015625f, 0.0f, 2.0f,
        2.015625f, 2.015625f, 0.0f, 2.0f,
    };
    std::memcpy(ram.data() + 0x7000, radial_positions,
                sizeof(radial_positions));
    std::memcpy(ram.data() + 0x7400, projected_texture0_coords,
                sizeof(projected_texture0_coords));
    emit(0x0200, 0x01000000);
    emit(0x0204, 0x01000000);
    emit(0x0208, 0x08080228);
    emit(0x020C, 0x04000400);
    emit(0x02C0, 0x00FF0000);
    emit(0x02E0, 0x00FF0000);
    emit(0x033C, 0x0207);
    emit(0x0340, 0);
    emit(0x035C, 1);
    emit(0x1E6C, 0);
    std::memset(ram.data() + 0xA0000, 0, 64 * 64 * 4);
    std::memset(ram.data() + 0xB0000, 0, 128 * 128 * 4);
    const auto put_rgb_texture =
        [&](uint32_t base, uint32_t width, uint32_t height,
            uint32_t x, uint32_t y, uint32_t color) {
            put32(ram, base +
                       swizzled_pixel_index(x, y, width, height) * 4,
                  color);
        };
    for (const auto& origin :
         {std::array<uint32_t, 2>{{16, 16}},
          std::array<uint32_t, 2>{{48, 48}}}) {
        put_rgb_texture(0xA0000, 64, 64, origin[0], origin[1],
                        0xFFFF0000);
        put_rgb_texture(0xA0000, 64, 64, origin[0] + 1, origin[1],
                        0xFFFFFF00);
        put_rgb_texture(0xA0000, 64, 64, origin[0], origin[1] + 1,
                        0xFFFF00FF);
        put_rgb_texture(0xA0000, 64, 64, origin[0] + 1, origin[1] + 1,
                        0xFFFFFFFF);
    }
    put_rgb_texture(0xB0000, 128, 128, 0, 0, 0xFFFF0000);
    put_rgb_texture(0xB0000, 128, 128, 1, 0, 0xFFFFFF00);
    put_rgb_texture(0xB0000, 128, 128, 0, 1, 0xFFFF00FF);
    put_rgb_texture(0xB0000, 128, 128, 1, 1, 0xFFFFFFFF);

    emit(0x1B00, 0x000B0000);
    emit(0x1B04, 0x0771062A);
    emit(0x1B08, 0x00010101);
    emit(0x1B0C, 0x4003FFF0);
    emit(0x1B10, 0x14000000);
    emit(0x1B14, 0x02022000);
    emit(0x1B20, 0);
    emit(0x1B24, 0);
    emit(0x1E70, 0x00010C61);
    emit(0x1E60, 0x00011107);
    const uint32_t projected_color_icw[] = {
        0x4849484A, 0x0D0D0000, 0xCDCD0000, 0xCDCD0000,
        0xCDCD0000, 0xC10CC20D, 0xC40520C1,
    };
    const uint32_t projected_alpha_icw[] = {
        0x5859585A, 0, 0, 0, 0, 0, 0,
    };
    const uint32_t projected_color_ocw[] = {
        0x000C30CD, 0x000000D0, 0x000000D0, 0x000000D0,
        0x000000D0, 0x000005CD, 0x00000C00,
    };
    const uint32_t projected_factor0[] = {
        0, 0, 0, 0, 0, 0xFF072103, 0xFF000000,
    };
    const uint32_t projected_factor1[] = {
        0, 0, 0, 0, 0, 0xFF35FF1A, 0,
    };
    for (uint32_t stage = 0; stage < 7; ++stage) {
        emit(0x0AC0 + stage * 4, projected_color_icw[stage]);
        emit(0x0260 + stage * 4, projected_alpha_icw[stage]);
        emit(0x1E40 + stage * 4, projected_color_ocw[stage]);
        emit(0x0AA0 + stage * 4, 0);
        emit(0x0A60 + stage * 4, projected_factor0[stage]);
        emit(0x0A80 + stage * 4, projected_factor1[stage]);
    }
    emit(0x17FC, 5);
    emit(0x1800, 0x00010000);
    emit(0x1808, 2);
    emit(0x17FC, 0);
    gpu.mmio_write(xlle::Nv2a::kBase + 0x800040, 4, cursor);
    CHECK(fault.empty());
    CHECK(gpu.projected_color_batches() == 1);
    CHECK(gpu.projected_color_triangles() == 1);
    CHECK(gpu.projected_color_fragments() > 0);
    CHECK(gpu.projected_color_depth_passes() ==
          gpu.projected_color_depth_writes());
    CHECK(gpu.projected_color_depth_writes() ==
          gpu.projected_color_writes());
    CHECK(gpu.projected_texture_samples() ==
          gpu.projected_color_writes() * 3);
    CHECK(gpu.projected_color_writes() > 0);
    CHECK(get32(ram, 0x20000 + textured_inside) == 0xFF0F3F07u);
    CHECK(get32(ram, 0x60000 + textured_inside) == 0x0000645Au);
    CHECK(gpu.last_projected_raster_address() >= 0x20000);
    CHECK(gpu.last_projected_raster_address() < 0x60000);
    CHECK(gpu.projected_color_output_hash() != 1469598103934665603ull);

    const uint64_t projected_writes = gpu.projected_color_writes();
    std::memcpy(ram.data() + 0x7000, farther_positions,
                sizeof(farther_positions));
    emit(0x17FC, 5);
    emit(0x1800, 0x00010000);
    emit(0x1808, 2);
    emit(0x17FC, 0);
    gpu.mmio_write(xlle::Nv2a::kBase + 0x800040, 4, cursor);
    CHECK(fault.empty());
    CHECK(gpu.projected_color_batches() == 2);
    CHECK(gpu.projected_color_triangles() == 2);
    CHECK(gpu.projected_color_writes() == projected_writes);
    CHECK(get32(ram, 0x20000 + textured_inside) == 0xFF0F3F07u);

    // INLINE_ARRAY interleaves every enabled attribute in slot order using
    // the programmed component formats, with no vertex DMA fetch. Reuse the
    // projected profile so the decoded position/color/three texture vectors
    // must traverse the existing transform and raster paths unchanged.
    constexpr size_t kInlineVertexBytes = 84;
    std::vector<uint32_t> inline_words(
        3 * kInlineVertexBytes / sizeof(uint32_t), 0);
    auto* inline_bytes = reinterpret_cast<uint8_t*>(inline_words.data());
    for (size_t vertex = 0; vertex < 3; ++vertex) {
        const size_t offset = vertex * kInlineVertexBytes;
        std::memcpy(inline_bytes + offset,
                    radial_positions + vertex * 4, 4 * sizeof(float));
        std::memcpy(inline_bytes + offset + 16,
                    &textured_colors[vertex], sizeof(uint32_t));
        const std::array<float, 4> inline_specular{{0, 0, 0, 1}};
        std::memcpy(inline_bytes + offset + 20,
                    inline_specular.data(), 4 * sizeof(float));
        std::memcpy(inline_bytes + offset + 36,
                    projected_texture0_coords + vertex * 4,
                    4 * sizeof(float));
        std::memcpy(inline_bytes + offset + 52,
                    texture1_coords + vertex * 4, 4 * sizeof(float));
        std::memcpy(inline_bytes + offset + 68,
                    texture2_coords + vertex * 4, 4 * sizeof(float));
    }
    const uint64_t vertices_before_inline = gpu.submitted_vertices();
    const uint64_t projected_writes_before_inline =
        gpu.projected_color_writes();
    emit(0x17FC, 5);
    for (uint32_t word : inline_words) emit(0x1818, word);
    emit(0x17FC, 0);
    gpu.mmio_write(xlle::Nv2a::kBase + 0x800040, 4, cursor);
    CHECK(fault.empty());
    CHECK(gpu.inline_array_batches() == 1);
    CHECK(gpu.inline_array_words() == inline_words.size());
    CHECK(gpu.inline_array_vertices() == 3);
    CHECK(gpu.inline_attribute_decodes() == 18);
    CHECK(gpu.submitted_vertices() == vertices_before_inline + 3);
    CHECK(gpu.projected_color_batches() == 3);
    CHECK(gpu.projected_color_triangles() == 3);
    CHECK(gpu.projected_color_writes() > projected_writes_before_inline);
    CHECK(get32(ram, 0x20000 + textured_inside) == 0xFF0F3F07u);

    // The pitch sibling reuses all three projected/cube samples but writes
    // linear A8R8G8B8/Z24S8 and supplies per-state stage-5 RGB weights.
    // Texture 3 remains output-inert under its GREATER shadow comparison.
    std::memcpy(ram.data() + 0x7000, radial_positions,
                sizeof(radial_positions));
    std::memset(ram.data() + 0x20000, 0, 64 * 64 * 4);
    for (uint32_t offset = 0; offset < 64 * 64 * 4; offset += 4)
        put32(ram, 0x60000 + offset, 0xFFFFFF5Au);
    emit(0x0200, 0x00400000);
    emit(0x0204, 0x00400000);
    emit(0x0208, 0x00000128);
    emit(0x020C, 0x01000100);
    emit(0x02C0, 0x003F0000);
    emit(0x02E0, 0x003F0000);
    emit(0x033C, 0x0206);
    emit(0x0340, 1);
    emit(0x035C, 0xFFFFFFFF);
    emit(0x1E6C, 4);
    emit(0x1B24, 0xFF000000);  // inert under REPEAT addressing
    emit(0x0A74, 0xFF0D3D06);
    emit(0x0A94, 0xFF61FF2F);
    emit(0x17FC, 5);
    emit(0x1800, 0x00010000);
    emit(0x1808, 2);
    emit(0x17FC, 0);
    gpu.mmio_write(xlle::Nv2a::kBase + 0x800040, 4, cursor);
    CHECK(fault.empty());
    CHECK(gpu.pitch_projected_color_batches() == 1);
    CHECK(gpu.pitch_projected_color_triangles() == 1);
    CHECK(gpu.pitch_projected_color_fragments() > 0);
    CHECK(gpu.pitch_projected_color_depth_passes() ==
          gpu.pitch_projected_color_depth_writes());
    CHECK(gpu.pitch_projected_color_depth_writes() ==
          gpu.pitch_projected_color_writes());
    CHECK(gpu.pitch_projected_texture_samples() ==
          gpu.pitch_projected_color_writes() * 3);
    CHECK(gpu.pitch_projected_color_writes() > 0);
    CHECK(get32(ram, 0x20000 + pitch_textured_inside) == 0xFF1B3F0Du);
    CHECK(get32(ram, 0x60000 + pitch_textured_inside) == 0x0000645Au);
    CHECK(gpu.last_pitch_projected_raster_address() >= 0x20000);
    CHECK(gpu.last_pitch_projected_raster_address() < 0x24000);
    CHECK(gpu.pitch_projected_color_output_hash() !=
          1469598103934665603ull);
    CHECK(gpu.projected_color_batches() == 3);
    CHECK(gpu.projected_color_writes() >
          projected_writes_before_inline);

    // The next pitch family draws four-vertex fans with one PROJECT2D
    // 1024x1024 texture. U/V use opaque-black CLAMP_TO_BORDER; sampling
    // exactly at zero blends one in-range texel with three border texels.
    const float pitch_single_positions[] = {
        10.0f, 10.0f, 100.0f, 1.0f,
        50.0f, 10.0f, 100.0f, 1.0f,
        50.0f, 50.0f, 100.0f, 1.0f,
        10.0f, 50.0f, 100.0f, 1.0f,
    };
    const uint32_t pitch_single_colors[] = {
        0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,
    };
    const float pitch_single_texcoords[] = {
        0.0f, 0.0f, 0.0f, 1.0f,
        0.0f, 0.0f, 0.0f, 1.0f,
        0.0f, 0.0f, 0.0f, 1.0f,
        0.0f, 0.0f, 0.0f, 1.0f,
    };
    std::memcpy(ram.data() + 0x7000, pitch_single_positions,
                sizeof(pitch_single_positions));
    std::memcpy(ram.data() + 0x7100, pitch_single_colors,
                sizeof(pitch_single_colors));
    std::memcpy(ram.data() + 0x7400, pitch_single_texcoords,
                sizeof(pitch_single_texcoords));
    std::memset(ram.data() + 0x100000, 0, 1024 * 1024 * 4);
    put32(ram, 0x100000, 0xFF804020);
    std::memset(ram.data() + 0x20000, 0, 64 * 64 * 4);
    for (uint32_t offset = 0; offset < 64 * 64 * 4; offset += 4)
        put32(ram, 0x60000 + offset, 0xFFFFFF5Au);

    emit(0x0200, 0x00400000);
    emit(0x0204, 0x00400000);
    emit(0x0208, 0x00000128);
    emit(0x020C, 0x01000100);
    emit(0x0210, 0x00020000);
    emit(0x0214, 0x00060000);
    emit(0x02C0, 0x003F0000);
    emit(0x02E0, 0x003F0000);
    emit(0x0300, 0);
    emit(0x0304, 1);
    emit(0x0308, 1);
    emit(0x030C, 1);
    emit(0x0310, 0);
    emit(0x033C, 0x0206);
    emit(0x0340, 1);
    emit(0x0344, 1);
    emit(0x0348, 0);
    emit(0x0350, 0x8006);
    emit(0x0354, 0x0203);
    emit(0x0358, 0x01010101);
    emit(0x035C, 0);
    emit(0x1D78, 1);
    emit(0x1720, 0x00007000);
    emit(0x172C, 0x80007100);
    emit(0x1744, 0x00007400);
    emit(0x1B00, 0x00100000);
    emit(0x1B04, 0x0AA1062A);
    emit(0x1B08, 0x00030404);
    emit(0x1B0C, 0x4003FFC0);
    emit(0x1B10, 0x14000000);
    emit(0x1B14, 0x02022000);
    emit(0x1B1C, 0x050001E0);
    emit(0x1B20, 0);
    emit(0x1B24, 0xFF000000);
    emit(0x1B4C, 0);
    emit(0x1B8C, 0);
    emit(0x1BCC, 0);
    emit(0x1E6C, 7);
    emit(0x1E70, 0x00000001);
    emit(0x1E74, 0);
    emit(0x1E78, 0x00210000);
    emit(0x1E60, 0x00000001);
    emit(0x0AC0, 0x08200000);
    emit(0x0260, 0x11200000);
    emit(0x1E40, 0x00000C00);
    emit(0x0AA0, 0x00000C00);
    emit(0x0A60, 0xFFFFFFFF);
    emit(0x0A80, 0xFFFFFFFF);
    emit(0x0288, 0x0000000C);
    emit(0x028C, 0x00001C80);
    emit(0x1E20, 0);
    emit(0x1E24, 0);
    emit(0x17FC, 7);
    emit(0x1800, 0x00010000);
    emit(0x1800, 0x00030002);
    emit(0x17FC, 0);
    gpu.mmio_write(xlle::Nv2a::kBase + 0x800040, 4, cursor);
    CHECK(fault.empty());
    CHECK(gpu.pitch_single_texture_batches() == 1);
    CHECK(gpu.pitch_single_texture_triangles() == 2);
    CHECK(gpu.pitch_single_texture_fragments() > 0);
    CHECK(gpu.pitch_single_texture_depth_passes() ==
          gpu.pitch_single_texture_writes());
    CHECK(gpu.pitch_single_texture_samples() ==
          gpu.pitch_single_texture_writes());
    CHECK(gpu.pitch_single_texture_writes() > 0);
    CHECK(get32(ram, 0x20000 + pitch_textured_inside) == 0xFF201008u);
    CHECK(get32(ram, 0x60000 + pitch_textured_inside) == 0xFFFFFF5Au);
    CHECK(gpu.last_pitch_single_texture_raster_address() >= 0x20000);
    CHECK(gpu.last_pitch_single_texture_raster_address() < 0x24000);
    CHECK(gpu.pitch_single_texture_output_hash() !=
          1469598103934665603ull);

    // The leading multi-texture pitch state cube-remaps stages 0/1 into one
    // 64x64 image. Their expanded-RGB dot drives alpha through
    // 1-(1-max(dot,0))^2; RGB is the stage-4 c0 tint. GEQUAL-one alpha then
    // gates SRC_ALPHA/ONE_MINUS_SRC_ALPHA blending and writable LEQUAL Z24;
    // the programmed polygon-offset values remain disabled by raster state.
    const float pitch_multi_positions[] = {
        10.0f, 10.0f, 100.0f, 1.0f,
        50.0f, 10.0f, 100.0f, 1.0f,
        10.0f, 50.0f, 100.0f, 1.0f,
    };
    std::memcpy(ram.data() + 0x7000, pitch_multi_positions,
                sizeof(pitch_multi_positions));
    std::memcpy(ram.data() + 0x7500, texture1_coords,
                sizeof(texture1_coords));
    std::memcpy(ram.data() + 0x7600, texture2_coords,
                sizeof(texture2_coords));
    std::memset(ram.data() + 0xA0000, 0, 64 * 64 * 4);
    for (uint32_t y : {16u, 17u})
        for (uint32_t x : {16u, 17u})
            put_rgb_texture(0xA0000, 64, 64, x, y, 0xFFFFFFFF);
    for (uint32_t y : {48u, 49u})
        for (uint32_t x : {48u, 49u})
            put_rgb_texture(0xA0000, 64, 64, x, y, 0xFF959595);
    for (uint32_t offset = 0; offset < 64 * 64 * 4; offset += 4) {
        put32(ram, 0x20000 + offset, 0x80402010);
        put32(ram, 0x60000 + offset, 0xFFFFFF5A);
    }

    emit(0x0200, 0x00400000);
    emit(0x0204, 0x00400000);
    emit(0x0208, 0x00000128);
    emit(0x020C, 0x01000100);
    emit(0x0210, 0x00020000);
    emit(0x0214, 0x00060000);
    emit(0x02C0, 0x003F0000);
    emit(0x02E0, 0x003F0000);
    emit(0x0300, 1);
    emit(0x0304, 1);
    emit(0x0308, 1);
    emit(0x030C, 1);
    emit(0x0310, 0);
    emit(0x033C, 0x0206);
    emit(0x0340, 1);
    emit(0x0344, 0x0302);
    emit(0x0348, 0x0303);
    emit(0x0350, 0x8006);
    emit(0x0354, 0x0203);
    emit(0x0358, 0x01010101);
    emit(0x035C, 1);
    emit(0x0384, 0x40800000);
    emit(0x0388, 0x41200000);
    emit(0x1D78, 1);
    emit(0x1720, 0x00007000);
    emit(0x1744, 0x00007500);
    emit(0x1748, 0x00007600);
    emit(0x1784, 0x00001042);
    emit(0x1788, 0x00001042);
    for (uint32_t stage = 0; stage < 4; ++stage) {
        const uint32_t base = 0x1B00 + stage * 0x40;
        emit(base + 0x00, stage < 2 ? 0x000A0000 : 0);
        emit(base + 0x04, stage < 2 ? 0x0661072E : 0);
        emit(base + 0x08, stage < 2 ? 0x00030303 : 0);
        emit(base + 0x0C, stage < 2 ? 0x4003FFC0 : 0);
        emit(base + 0x10, stage == 0 ? 0x14000000 : 0);
        emit(base + 0x14, stage < 2 ? 0x02022000 : 0);
        emit(base + 0x1C, stage == 0 ? 0x050001E0 : 0);
        emit(base + 0x20, 0);
        emit(base + 0x24, 0);
    }
    emit(0x1E6C, 7);
    emit(0x1E70, 0x00000063);
    emit(0x1E74, 0);
    emit(0x1E78, 0);
    emit(0x1E60, 0x00011106);
    const uint32_t pitch_multi_color_icw[] = {
        0x48490000, 0x0C200000, 0x2D2D0000,
        0x2CC10000, 0xCC20C120, 0x00000000,
    };
    const uint32_t pitch_multi_alpha_icw[] = {
        0x00000000, 0x1C301010, 0x3D3D1010,
        0x3CD11010, 0x00000000, 0xDCD11010,
    };
    const uint32_t pitch_multi_color_ocw[] = {
        0x000820C0, 0x000000D0, 0x000000C0,
        0x000000C0, 0x00000C00, 0x00000000,
    };
    const uint32_t pitch_multi_alpha_ocw[] = {
        0x00000000, 0x000000D0, 0x000000C0,
        0x000000C0, 0x00000000, 0x000000C0,
    };
    const uint32_t pitch_multi_factor0[] = {
        0, 0, 0, 0xFF000000, 0xFF0D3308, 0xFFFFFFFF,
    };
    for (uint32_t stage = 0; stage < 6; ++stage) {
        emit(0x0AC0 + stage * 4, pitch_multi_color_icw[stage]);
        emit(0x0260 + stage * 4, pitch_multi_alpha_icw[stage]);
        emit(0x1E40 + stage * 4, pitch_multi_color_ocw[stage]);
        emit(0x0AA0 + stage * 4, pitch_multi_alpha_ocw[stage]);
        emit(0x0A60 + stage * 4, pitch_multi_factor0[stage]);
        emit(0x0A80 + stage * 4, 0);
    }
    emit(0x0288, 0x0000000C);
    emit(0x028C, 0x00001C80);
    emit(0x1E20, 0);
    emit(0x1E24, 0);
    emit(0x17FC, 6);
    emit(0x1800, 0x00010000);
    emit(0x1808, 2);
    emit(0x17FC, 0);
    gpu.mmio_write(xlle::Nv2a::kBase + 0x800040, 4, cursor);
    CHECK(fault.empty());
    CHECK(gpu.pitch_multi_texture_batches() == 1);
    CHECK(gpu.pitch_multi_texture_triangles() == 1);
    CHECK(gpu.pitch_multi_texture_fragments() > 0);
    CHECK(gpu.pitch_multi_texture_depth_passes() ==
          gpu.pitch_multi_texture_alpha_passes());
    CHECK(gpu.pitch_multi_texture_alpha_passes() ==
          gpu.pitch_multi_texture_color_writes());
    CHECK(gpu.pitch_multi_texture_color_writes() ==
          gpu.pitch_multi_texture_depth_writes());
    CHECK(gpu.pitch_multi_texture_samples() ==
          gpu.pitch_multi_texture_depth_passes() * 2);
    CHECK(get32(ram, 0x20000 + pitch_textured_inside) == 0xB1192E0Au);
    CHECK(get32(ram, 0x60000 + pitch_textured_inside) == 0x0000645Au);
    CHECK(gpu.last_pitch_multi_texture_raster_address() >= 0x20000);
    CHECK(gpu.last_pitch_multi_texture_raster_address() < 0x24000);
    CHECK(gpu.pitch_multi_texture_output_hash() !=
          1469598103934665603ull);

    const uint64_t pitch_multi_alpha =
        gpu.pitch_multi_texture_alpha_passes();
    const uint64_t pitch_multi_writes =
        gpu.pitch_multi_texture_color_writes();
    const uint64_t pitch_multi_samples =
        gpu.pitch_multi_texture_samples();
    for (uint32_t y : {48u, 49u})
        for (uint32_t x : {48u, 49u})
            put_rgb_texture(0xA0000, 64, 64, x, y, 0xFF000000);
    emit(0x17FC, 6);
    emit(0x1800, 0x00010000);
    emit(0x1808, 2);
    emit(0x17FC, 0);
    gpu.mmio_write(xlle::Nv2a::kBase + 0x800040, 4, cursor);
    CHECK(fault.empty());
    CHECK(gpu.pitch_multi_texture_batches() == 2);
    CHECK(gpu.pitch_multi_texture_triangles() == 2);
    CHECK(gpu.pitch_multi_texture_alpha_passes() == pitch_multi_alpha);
    CHECK(gpu.pitch_multi_texture_color_writes() == pitch_multi_writes);
    CHECK(gpu.pitch_multi_texture_depth_writes() == pitch_multi_writes);
    CHECK(gpu.pitch_multi_texture_samples() > pitch_multi_samples);
    CHECK(gpu.pitch_multi_texture_samples() ==
          gpu.pitch_multi_texture_depth_passes() * 2);
    CHECK(get32(ram, 0x20000 + pitch_textured_inside) == 0xB1192E0Au);
    CHECK(get32(ram, 0x60000 + pitch_textured_inside) == 0x0000645Au);

    // Pitch-multi retains the legacy Z path for the same X/Y tie.  Use an
    // X/Y/Z-distinct texture and compare it with a one-sided Z-major draw.
    for (uint32_t y = 0; y < 64; ++y) {
        for (uint32_t x = 0; x < 64; ++x) {
            put_rgb_texture(0xA0000, 64, 64, x, y,
                            x < 32 ? 0xFF2060A0u
                            : y < 32 ? 0xFF20A060u : 0xFFA02040u);
            put32(ram, 0x20000 + y * 256 + x * 4, 0x80402010);
            put32(ram, 0x60000 + y * 256 + x * 4, 0xFFFFFF5A);
        }
    }
    const float pitch_multi_z_reference[12] = {
        1, 1, 1.0001f, 1, 1, 1, 1.0001f, 1, 1, 1, 1.0001f, 1};
    const float pitch_multi_xy_tie[12] = {
        1, 1, .5f, 1, 1, 1, .5f, 1, 1, 1, .5f, 1};
    std::memcpy(ram.data() + 0x7500, pitch_multi_z_reference,
                sizeof(pitch_multi_z_reference));
    std::memcpy(ram.data() + 0x7600, pitch_multi_z_reference,
                sizeof(pitch_multi_z_reference));
    const uint64_t pitch_multi_tie0 = gpu.pitch_three_texture_xy_tie_direction0();
    const uint64_t pitch_multi_tie1 = gpu.pitch_three_texture_xy_tie_direction1();
    emit(0x17FC, 6); emit(0x1800, 0x00010000); emit(0x1808, 2); emit(0x17FC, 0);
    gpu.mmio_write(xlle::Nv2a::kBase + 0x800040, 4, cursor);
    CHECK(fault.empty());
    const uint32_t pitch_multi_z_color = get32(ram, 0x20000 + pitch_textured_inside);
    std::memcpy(ram.data() + 0x7500, pitch_multi_xy_tie,
                sizeof(pitch_multi_xy_tie));
    std::memcpy(ram.data() + 0x7600, pitch_multi_xy_tie,
                sizeof(pitch_multi_xy_tie));
    for (uint32_t y = 0; y < 64; ++y)
        for (uint32_t x = 0; x < 64; ++x) {
            put32(ram, 0x20000 + y * 256 + x * 4, 0x80402010);
            put32(ram, 0x60000 + y * 256 + x * 4, 0xFFFFFF5A);
        }
    emit(0x17FC, 6); emit(0x1800, 0x00010000); emit(0x1808, 2); emit(0x17FC, 0);
    gpu.mmio_write(xlle::Nv2a::kBase + 0x800040, 4, cursor);
    CHECK(fault.empty());
    CHECK(get32(ram, 0x20000 + pitch_textured_inside) == pitch_multi_z_color);
    CHECK(gpu.pitch_three_texture_xy_tie_direction0() == pitch_multi_tie0);
    CHECK(gpu.pitch_three_texture_xy_tie_direction1() == pitch_multi_tie1);

    // The zero-vector tie-break is deliberately not shared with pitch-multi.
    const float pitch_multi_zero_cube[12] = {};
    std::memcpy(ram.data() + 0x7500, pitch_multi_zero_cube,
                sizeof(pitch_multi_zero_cube));
    std::memcpy(ram.data() + 0x7600, pitch_multi_zero_cube,
                sizeof(pitch_multi_zero_cube));
    emit(0x17FC, 6); emit(0x1800, 0x00010000); emit(0x1808, 2); emit(0x17FC, 0);
    gpu.mmio_write(xlle::Nv2a::kBase + 0x800040, 4, cursor);
    CHECK(fault == "NV2A pitch cube-pair remap is invalid");
    CHECK(gpu.pitch_multi_texture_batches() == 4);
    fault.clear();
    // Pitch-multi likewise retains its existing overflow fault behavior.
    const float pitch_multi_overflow_positions[] = {
        10, 10, 100, 1, 50, 10, 100, 1, -10, -50, -100, -1};
    const float pitch_multi_huge = std::numeric_limits<float>::max();
    const float pitch_multi_overflow_cube[12] = {
        pitch_multi_huge * .25f, pitch_multi_huge * .125f,
        pitch_multi_huge * .0625f, 1,
        pitch_multi_huge * .5f, pitch_multi_huge * .25f,
        pitch_multi_huge * .125f, 1,
        pitch_multi_huge * .75f, pitch_multi_huge * .375f,
        pitch_multi_huge * .1875f, 1};
    std::memcpy(ram.data() + 0x7000, pitch_multi_overflow_positions,
                sizeof(pitch_multi_overflow_positions));
    std::memcpy(ram.data() + 0x7500, pitch_multi_overflow_cube,
                sizeof(pitch_multi_overflow_cube));
    std::memcpy(ram.data() + 0x7600, pitch_multi_overflow_cube,
                sizeof(pitch_multi_overflow_cube));
    emit(0x17FC, 6); emit(0x1800, 0x00010000); emit(0x1808, 2); emit(0x17FC, 0);
    gpu.mmio_write(xlle::Nv2a::kBase + 0x800040, 4, cursor);
    CHECK(fault == "NV2A pitch cube-pair remap is invalid");
    CHECK(gpu.pitch_multi_texture_batches() == 4);
    fault.clear();
    std::memcpy(ram.data() + 0x7000, pitch_multi_positions,
                sizeof(pitch_multi_positions));
    std::memcpy(ram.data() + 0x7500, texture1_coords,
                sizeof(texture1_coords));
    std::memcpy(ram.data() + 0x7600, texture2_coords,
                sizeof(texture2_coords));

    // The remaining three-enabled-texture shader=0x63 states reuse the
    // stage-0/1 cube pair; stage 2 is shader-inert. Alpha testing is disabled,
    // and dynamic stage-3 c0 RGB adds source_alpha*c0 to the fixed stage-4
    // tint before SRC_ALPHA/ONE_MINUS_SRC_ALPHA blending and writable LEQUAL
    // Z24. Exercise the leading factor and one later retained variant.
    constexpr uint32_t pitch_three_color = 0x280000;
    constexpr uint32_t pitch_three_depth = 0x010000;
    constexpr uint32_t pitch_three_texture = 0x600000;
    constexpr uint32_t pitch_three_stride = 0x1400;
    for (uint32_t y = 10; y < 50; ++y) {
        for (uint32_t x = 10; x < 50; ++x) {
            const uint32_t offset = y * pitch_three_stride + x * 4;
            put32(ram, pitch_three_color + offset, 0x80402010);
            put32(ram, pitch_three_depth + offset, 0xFFFFFF5A);
        }
    }
    std::memset(ram.data() + pitch_three_texture, 0, 64 * 64 * 4);
    for (uint32_t y : {16u, 17u})
        for (uint32_t x : {16u, 17u})
            put_rgb_texture(pitch_three_texture, 64, 64, x, y, 0xFFFFFFFF);
    for (uint32_t y : {48u, 49u})
        for (uint32_t x : {48u, 49u})
            put_rgb_texture(pitch_three_texture, 64, 64, x, y, 0xFF959595);

    emit(0x0200, 0x05000000);
    emit(0x0204, 0x01E00000);
    emit(0x0208, 0x00000128);
    emit(0x020C, 0x14001400);
    emit(0x0210, pitch_three_color);
    emit(0x0214, pitch_three_depth);
    emit(0x02C0, 0x04FF0000);
    emit(0x02E0, 0x01DF0000);
    emit(0x0300, 0);
    emit(0x0304, 1);
    emit(0x0308, 1);
    emit(0x030C, 1);
    emit(0x0310, 0);
    emit(0x033C, 0x0206);
    emit(0x0340, 1);
    emit(0x0344, 0x0302);
    emit(0x0348, 0x0303);
    emit(0x0350, 0x8006);
    emit(0x0354, 0x0203);
    emit(0x0358, 0x01010101);
    emit(0x035C, 1);
    emit(0x0384, 0x40800000);
    emit(0x0388, 0x41200000);
    emit(0x1D78, 1);
    emit(0x1720, 0x00007000);
    emit(0x1744, 0x00007500);
    emit(0x1748, 0x00007600);
    emit(0x1784, 0x00001042);
    emit(0x1788, 0x00001042);
    for (uint32_t stage = 0; stage < 4; ++stage) {
        const uint32_t base = 0x1B00 + stage * 0x40;
        emit(base + 0x00,
             stage < 2 ? pitch_three_texture
                       : stage == 2 ? 0x007FF000 : 0);
        emit(base + 0x04,
             stage < 2 ? 0x0661072E
                       : stage == 2 ? 0x0771072E : 0);
        emit(base + 0x08, stage < 3 ? 0x00030303 : 0);
        emit(base + 0x0C, stage < 3 ? 0x4003FFC0 : 0);
        emit(base + 0x10, stage == 0 ? 0x14000000 : 0);
        emit(base + 0x14, stage < 3 ? 0x02022000 : 0);
        emit(base + 0x1C, stage == 0 ? 0x050001E0 : 0);
        emit(base + 0x20, 0);
        emit(base + 0x24, 0);
    }
    emit(0x1E6C, 7);
    emit(0x1E70, 0x00000063);
    emit(0x1E74, 0);
    emit(0x1E78, 0);
    emit(0x1E60, 0x00011106);
    for (uint32_t stage = 0; stage < 6; ++stage) {
        emit(0x0AC0 + stage * 4, pitch_multi_color_icw[stage]);
        emit(0x0260 + stage * 4, pitch_multi_alpha_icw[stage]);
        emit(0x1E40 + stage * 4, pitch_multi_color_ocw[stage]);
        emit(0x0AA0 + stage * 4, pitch_multi_alpha_ocw[stage]);
        emit(0x0A60 + stage * 4,
             stage == 3 ? 0xFF000100 : pitch_multi_factor0[stage]);
        emit(0x0A80 + stage * 4, 0);
    }
    emit(0x0288, 0x0000000C);
    emit(0x028C, 0x00001C80);
    emit(0x1E20, 0);
    emit(0x1E24, 0);
    emit(0x17FC, 6);
    emit(0x1800, 0x00010000);
    emit(0x1808, 2);
    emit(0x17FC, 0);
    gpu.mmio_write(xlle::Nv2a::kBase + 0x800040, 4, cursor);
    CHECK(fault.empty());
    CHECK(gpu.pitch_three_texture_batches() == 1);
    CHECK(gpu.pitch_three_texture_triangles() == 1);
    CHECK(gpu.pitch_three_texture_fragments() > 0);
    CHECK(gpu.pitch_three_texture_depth_passes() ==
          gpu.pitch_three_texture_fragments());
    CHECK(gpu.pitch_three_texture_color_writes() ==
          gpu.pitch_three_texture_depth_passes());
    CHECK(gpu.pitch_three_texture_depth_writes() ==
          gpu.pitch_three_texture_color_writes());
    CHECK(gpu.pitch_three_texture_samples() ==
          gpu.pitch_three_texture_depth_passes() * 2);
    const uint32_t pitch_three_inside =
        20 * pitch_three_stride + 20 * 4;
    CHECK(get32(ram, pitch_three_color + pitch_three_inside) ==
          0xB1192F0Au);
    CHECK(get32(ram, pitch_three_depth + pitch_three_inside) ==
          0x0000645Au);
    CHECK(gpu.last_pitch_three_texture_raster_address() >=
          pitch_three_color);
    CHECK(gpu.last_pitch_three_texture_raster_address() <
          pitch_three_color + pitch_three_stride * 480);
    CHECK(gpu.pitch_three_texture_output_hash() !=
          1469598103934665603ull);
    CHECK(gpu.pitch_multi_texture_batches() == 4);
    put32(ram, pitch_three_color + pitch_three_inside, 0x80402010);
    put32(ram, pitch_three_depth + pitch_three_inside, 0xFFFFFF5A);
    emit(0x0A6C, 0xFF12480B);
    emit(0x17FC, 6);
    emit(0x1800, 0x00010000);
    emit(0x1808, 2);
    emit(0x17FC, 0);
    gpu.mmio_write(xlle::Nv2a::kBase + 0x800040, 4, cursor);
    CHECK(fault.empty());
    CHECK(gpu.pitch_three_texture_batches() == 2);
    CHECK(gpu.pitch_three_texture_triangles() == 2);
    CHECK(get32(ram, pitch_three_color + pitch_three_inside) ==
          0xB1245710u);
    CHECK(get32(ram, pitch_three_depth + pitch_three_inside) ==
          0x0000645Au);
    CHECK(gpu.pitch_multi_texture_batches() == 4);

    // Establish the exact 32-batch boot factor with border zero, then rerun
    // only the border as opaque black. Shader 0x63 cube sampling cannot consult
    // the 2D border, so output and every unit of raster work must match.
    put32(ram, pitch_three_color + pitch_three_inside, 0x80402010);
    put32(ram, pitch_three_depth + pitch_three_inside, 0xFFFFFF5A);
    emit(0x0A6C, 0xFF16590D);
    const uint64_t before_border_fragments =
        gpu.pitch_three_texture_fragments();
    const uint64_t before_border_passes =
        gpu.pitch_three_texture_depth_passes();
    const uint64_t before_border_writes =
        gpu.pitch_three_texture_color_writes();
    const uint64_t before_border_samples =
        gpu.pitch_three_texture_samples();
    emit(0x17FC, 6);
    emit(0x1800, 0x00010000);
    emit(0x1808, 2);
    emit(0x17FC, 0);
    gpu.mmio_write(xlle::Nv2a::kBase + 0x800040, 4, cursor);
    CHECK(fault.empty());
    CHECK(gpu.pitch_three_texture_batches() == 3);
    CHECK(gpu.pitch_three_texture_triangles() == 3);
    const uint64_t border_zero_fragments =
        gpu.pitch_three_texture_fragments() - before_border_fragments;
    const uint64_t border_zero_passes =
        gpu.pitch_three_texture_depth_passes() - before_border_passes;
    const uint64_t border_zero_writes =
        gpu.pitch_three_texture_color_writes() - before_border_writes;
    const uint64_t border_zero_samples =
        gpu.pitch_three_texture_samples() - before_border_samples;
    const uint32_t border_zero_color =
        get32(ram, pitch_three_color + pitch_three_inside);
    const uint32_t border_zero_depth =
        get32(ram, pitch_three_depth + pitch_three_inside);

    put32(ram, pitch_three_color + pitch_three_inside, 0x80402010);
    put32(ram, pitch_three_depth + pitch_three_inside, 0xFFFFFF5A);
    emit(0x1B24, 0xFF000000);
    emit(0x17FC, 6);
    emit(0x1800, 0x00010000);
    emit(0x1808, 2);
    emit(0x17FC, 0);
    gpu.mmio_write(xlle::Nv2a::kBase + 0x800040, 4, cursor);
    CHECK(fault.empty());
    CHECK(gpu.pitch_three_texture_batches() == 4);
    CHECK(gpu.pitch_three_texture_triangles() == 4);
    CHECK(gpu.pitch_three_texture_fragments() - before_border_fragments ==
          border_zero_fragments * 2);
    CHECK(gpu.pitch_three_texture_depth_passes() - before_border_passes ==
          border_zero_passes * 2);
    CHECK(gpu.pitch_three_texture_color_writes() - before_border_writes ==
          border_zero_writes * 2);
    CHECK(gpu.pitch_three_texture_samples() - before_border_samples ==
          border_zero_samples * 2);
    CHECK(get32(ram, pitch_three_color + pitch_three_inside) ==
          border_zero_color);
    CHECK(get32(ram, pitch_three_depth + pitch_three_inside) ==
          border_zero_depth);

    // Do not turn output-inertness into a broad wildcard: no other border value
    // has been witnessed for this retained profile.
    emit(0x1B24, 0xFF010203);
    emit(0x17FC, 6);
    emit(0x1800, 0x00010000);
    emit(0x1808, 2);
    emit(0x17FC, 0);
    gpu.mmio_write(xlle::Nv2a::kBase + 0x800040, 4, cursor);
    CHECK(fault.empty());
    CHECK(gpu.pitch_three_texture_batches() == 4);
    CHECK(gpu.pitch_three_texture_triangles() == 4);

    // The black-border table is prior FF16590D plus ten captured RS2/RS5-RS13
    // additions. Exercise every member, then adjacent and plausible unlisted
    // values, so this cannot regress into a high-byte wildcard.
    emit(0x1B24, 0xFF000000);
    const uint32_t pitch_three_black_factors[] = {
        0xFF16590D, 0xFF175B0E, 0xFF1F7D13, 0xFF16580D, 0xFF18610F,
        0xFF185F0E, 0xFF175C0E, 0xFF144F0C, 0xFF175D0E, 0xFF16570D,
        0xFF18600E};
    for (uint32_t factor : pitch_three_black_factors) {
        emit(0x0A6C, factor);
        emit(0x17FC, 6); emit(0x1800, 0x00010000); emit(0x1808, 2); emit(0x17FC, 0);
        gpu.mmio_write(xlle::Nv2a::kBase + 0x800040, 4, cursor);
        CHECK(fault.empty());
    }
    CHECK(gpu.pitch_three_texture_batches() == 15);
    emit(0x0A6C, 0xFF175B0F);
    emit(0x17FC, 6); emit(0x1800, 0x00010000); emit(0x1808, 2); emit(0x17FC, 0);
    gpu.mmio_write(xlle::Nv2a::kBase + 0x800040, 4, cursor);
    CHECK(fault.empty());
    CHECK(gpu.last_pitch_three_zero_factor3() == 0);
    CHECK(gpu.pitch_three_texture_batches() == 15);
    emit(0x0A6C, 0xFF165A0D);
    emit(0x17FC, 6); emit(0x1800, 0x00010000); emit(0x1808, 2); emit(0x17FC, 0);
    gpu.mmio_write(xlle::Nv2a::kBase + 0x800040, 4, cursor);
    CHECK(fault.empty());
    CHECK(gpu.pitch_three_texture_batches() == 15);
    emit(0x1B24, 0);  // zero border remains admitted under the original state.
    emit(0x17FC, 6); emit(0x1800, 0x00010000); emit(0x1808, 2); emit(0x17FC, 0);
    gpu.mmio_write(xlle::Nv2a::kBase + 0x800040, 4, cursor);
    CHECK(fault.empty());
    CHECK(gpu.pitch_three_texture_batches() == 16);

    // The exact-zero policy is the deterministic +Z-axis tie-break. Compare
    // it with explicit +Z and signed-zero inputs using an asymmetric origin.
    put_rgb_texture(pitch_three_texture, 64, 64, 0, 0, 0xFF804020);
    const float plus_z_cube_coordinates[12] = {
        0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1};
    const float zero_cube_coordinates[12] = {};
    const float negative_zero_cube_coordinates[12] = {
        -0.0f, -0.0f, -0.0f, 1, -0.0f, -0.0f, -0.0f, 1,
        -0.0f, -0.0f, -0.0f, 1};
    auto prepare_zero_cube_target = [&] {
        for (uint32_t y = 10; y < 50; ++y) {
            for (uint32_t x = 10; x < 50; ++x) {
                const uint32_t offset = y * pitch_three_stride + x * 4;
                put32(ram, pitch_three_color + offset, 0x80402010);
                put32(ram, pitch_three_depth + offset, 0xFFFFFF5A);
            }
        }
    };
    emit(0x1B24, 0xFF000000); emit(0x0A6C, 0xFF175B0E);
    std::memcpy(ram.data() + 0x7500, plus_z_cube_coordinates,
                sizeof(plus_z_cube_coordinates));
    std::memcpy(ram.data() + 0x7600, plus_z_cube_coordinates,
                sizeof(plus_z_cube_coordinates));
    prepare_zero_cube_target();
    emit(0x17FC, 6); emit(0x1800, 0x00010000); emit(0x1808, 2); emit(0x17FC, 0);
    gpu.mmio_write(xlle::Nv2a::kBase + 0x800040, 4, cursor);
    CHECK(fault.empty());
    CHECK(gpu.last_pitch_three_zero_factor3() == 0);
    const uint32_t plus_z_color = get32(ram, pitch_three_color + pitch_three_inside);
    const uint32_t plus_z_depth = get32(ram, pitch_three_depth + pitch_three_inside);
    const uint64_t zero_cube_passes = gpu.pitch_three_texture_depth_passes();
    const uint64_t zero_cube_writes = gpu.pitch_three_texture_color_writes();
    const uint64_t zero_cube_samples = gpu.pitch_three_texture_samples();
    const uint64_t zero_cube_direction0 = gpu.pitch_three_texture_zero_direction0();
    const uint64_t zero_cube_direction1 = gpu.pitch_three_texture_zero_direction1();
    const uint64_t zero_cube_hash = gpu.pitch_three_texture_output_hash();
    std::memcpy(ram.data() + 0x7500, zero_cube_coordinates,
                sizeof(zero_cube_coordinates));
    std::memcpy(ram.data() + 0x7600, zero_cube_coordinates,
                sizeof(zero_cube_coordinates));
    prepare_zero_cube_target();
    emit(0x17FC, 6); emit(0x1800, 0x00010000); emit(0x1808, 2); emit(0x17FC, 0);
    gpu.mmio_write(xlle::Nv2a::kBase + 0x800040, 4, cursor);
    CHECK(fault.empty());
    const uint64_t zero_cube_pass_delta =
        gpu.pitch_three_texture_depth_passes() - zero_cube_passes;
    CHECK(zero_cube_pass_delta > 0);
    CHECK(gpu.pitch_three_texture_color_writes() - zero_cube_writes ==
          zero_cube_pass_delta);
    CHECK(gpu.pitch_three_texture_samples() - zero_cube_samples ==
          zero_cube_pass_delta * 2);
    CHECK(gpu.pitch_three_texture_zero_direction0() - zero_cube_direction0 ==
          zero_cube_pass_delta);
    CHECK(gpu.pitch_three_texture_zero_direction1() - zero_cube_direction1 ==
          zero_cube_pass_delta);
    CHECK(gpu.last_pitch_three_zero_factor3() == 0xFF175B0Eu);
    CHECK(gpu.pitch_three_texture_output_hash() != zero_cube_hash);
    CHECK(get32(ram, pitch_three_color + pitch_three_inside) == plus_z_color);
    CHECK(get32(ram, pitch_three_depth + pitch_three_inside) == plus_z_depth);
    std::memcpy(ram.data() + 0x7500, negative_zero_cube_coordinates,
                sizeof(negative_zero_cube_coordinates));
    std::memcpy(ram.data() + 0x7600, negative_zero_cube_coordinates,
                sizeof(negative_zero_cube_coordinates));
    prepare_zero_cube_target();
    emit(0x17FC, 6); emit(0x1800, 0x00010000); emit(0x1808, 2); emit(0x17FC, 0);
    gpu.mmio_write(xlle::Nv2a::kBase + 0x800040, 4, cursor);
    CHECK(fault.empty());
    CHECK(get32(ram, pitch_three_color + pitch_three_inside) == plus_z_color);
    CHECK(get32(ram, pitch_three_depth + pitch_three_inside) == plus_z_depth);

    const uint64_t invalid_cube_batches = gpu.pitch_three_texture_batches();
    float invalid_cube_coordinates[12] = {};
    invalid_cube_coordinates[0] = std::numeric_limits<float>::quiet_NaN();
    std::memcpy(ram.data() + 0x7500, invalid_cube_coordinates,
                sizeof(invalid_cube_coordinates));
    emit(0x17FC, 6); emit(0x1800, 0x00010000); emit(0x1808, 2); emit(0x17FC, 0);
    gpu.mmio_write(xlle::Nv2a::kBase + 0x800040, 4, cursor);
    CHECK(!fault.empty());
    CHECK(gpu.pitch_three_texture_batches() == invalid_cube_batches);
    fault.clear();
    invalid_cube_coordinates[0] = -std::numeric_limits<float>::infinity();
    std::memcpy(ram.data() + 0x7500, invalid_cube_coordinates,
                sizeof(invalid_cube_coordinates));
    emit(0x17FC, 6); emit(0x1800, 0x00010000); emit(0x1808, 2); emit(0x17FC, 0);
    gpu.mmio_write(xlle::Nv2a::kBase + 0x800040, 4, cursor);
    CHECK(!fault.empty());
    CHECK(gpu.pitch_three_texture_batches() == invalid_cube_batches);
    fault.clear();
    invalid_cube_coordinates[0] = std::numeric_limits<float>::infinity();
    std::memcpy(ram.data() + 0x7500, invalid_cube_coordinates,
                sizeof(invalid_cube_coordinates));
    emit(0x17FC, 6); emit(0x1800, 0x00010000); emit(0x1808, 2); emit(0x17FC, 0);
    gpu.mmio_write(xlle::Nv2a::kBase + 0x800040, 4, cursor);
    CHECK(!fault.empty());
    CHECK(gpu.pitch_three_texture_batches() == invalid_cube_batches);
    fault.clear();

    // A projective denominator near zero can make finite interpolated cube
    // coordinates exceed float range. Pitch-three rescales the full vector,
    // preserving cube direction and therefore matching an ordinary
    // proportional reference vector at the same pixel.
    const float rescale_positions[] = {
        10, 10, 100, 1, 50, 10, 100, 1, -10, -50, -100, -1};
    const float reference_cube_coordinates[12] = {
        .25f, .125f, .0625f, 1,
        .5f, .25f, .125f, 1,
        .75f, .375f, .1875f, 1};
    const float huge = std::numeric_limits<float>::max();
    const float huge_cube_coordinates[12] = {
        huge * .25f, huge * .125f, huge * .0625f, 1,
        huge * .5f, huge * .25f, huge * .125f, 1,
        huge * .75f, huge * .375f, huge * .1875f, 1};
    std::memcpy(ram.data() + 0x7000, rescale_positions,
                sizeof(rescale_positions));
    std::memcpy(ram.data() + 0x7500, reference_cube_coordinates,
                sizeof(reference_cube_coordinates));
    std::memcpy(ram.data() + 0x7600, reference_cube_coordinates,
                sizeof(reference_cube_coordinates));
    prepare_zero_cube_target();
    const uint64_t rescale0_before = gpu.pitch_three_texture_rescale_direction0();
    const uint64_t rescale1_before = gpu.pitch_three_texture_rescale_direction1();
    emit(0x17FC, 6); emit(0x1800, 0x00010000); emit(0x1808, 2); emit(0x17FC, 0);
    gpu.mmio_write(xlle::Nv2a::kBase + 0x800040, 4, cursor);
    CHECK(fault.empty());
    CHECK(gpu.pitch_three_texture_rescale_direction0() == rescale0_before);
    CHECK(gpu.pitch_three_texture_rescale_direction1() == rescale1_before);
    const uint32_t reference_cube_color =
        get32(ram, pitch_three_color + pitch_three_inside);
    std::memcpy(ram.data() + 0x7500, huge_cube_coordinates,
                sizeof(huge_cube_coordinates));
    std::memcpy(ram.data() + 0x7600, huge_cube_coordinates,
                sizeof(huge_cube_coordinates));
    prepare_zero_cube_target();
    emit(0x17FC, 6); emit(0x1800, 0x00010000); emit(0x1808, 2); emit(0x17FC, 0);
    gpu.mmio_write(xlle::Nv2a::kBase + 0x800040, 4, cursor);
    CHECK(fault.empty());
    CHECK(gpu.pitch_three_texture_rescale_direction0() > rescale0_before);
    CHECK(gpu.pitch_three_texture_rescale_direction1() > rescale1_before);
    CHECK(gpu.last_pitch_three_rescale_factor3() == 0xFF175B0Eu);
    CHECK(get32(ram, pitch_three_color + pitch_three_inside) ==
          reference_cube_color);

    // Exact X/Y major ties select X in pitch-three. The one-sided X-major
    // reference shares the left-edge sample; the historical Z fallback would
    // instead reach the distinct right edge.
    for (uint32_t y = 0; y < 64; ++y) {
        for (uint32_t x = 0; x < 64; ++x) {
            put_rgb_texture(pitch_three_texture, 64, 64, x, y,
                            x < 32 ? 0xFF2060A0u
                            : y < 32 ? 0xFF20A060u : 0xFFA02040u);
        }
    }
    const float x_major_reference[12] = {
        1.0001f, 1, .25f, 1, 1.0001f, 1, .25f, 1,
        1.0001f, 1, .25f, 1};
    const float xy_major_tie[12] = {
        1, 1, .25f, 1, 1, 1, .25f, 1, 1, 1, .25f, 1};
    std::memcpy(ram.data() + 0x7500, x_major_reference,
                sizeof(x_major_reference));
    std::memcpy(ram.data() + 0x7600, x_major_reference,
                sizeof(x_major_reference));
    prepare_zero_cube_target();
    const uint64_t tie_zero0 = gpu.pitch_three_texture_zero_direction0();
    const uint64_t tie_zero1 = gpu.pitch_three_texture_zero_direction1();
    const uint64_t tie_rescale0 = gpu.pitch_three_texture_rescale_direction0();
    const uint64_t tie_rescale1 = gpu.pitch_three_texture_rescale_direction1();
    const uint64_t tie0 = gpu.pitch_three_texture_xy_tie_direction0();
    const uint64_t tie1 = gpu.pitch_three_texture_xy_tie_direction1();
    emit(0x17FC, 6); emit(0x1800, 0x00010000); emit(0x1808, 2); emit(0x17FC, 0);
    gpu.mmio_write(xlle::Nv2a::kBase + 0x800040, 4, cursor);
    CHECK(fault.empty());
    CHECK(gpu.pitch_three_texture_xy_tie_direction0() == tie0);
    CHECK(gpu.pitch_three_texture_xy_tie_direction1() == tie1);
    const uint32_t x_major_color =
        get32(ram, pitch_three_color + pitch_three_inside);
    std::memcpy(ram.data() + 0x7500, xy_major_tie, sizeof(xy_major_tie));
    std::memcpy(ram.data() + 0x7600, xy_major_tie, sizeof(xy_major_tie));
    prepare_zero_cube_target();
    emit(0x17FC, 6); emit(0x1800, 0x00010000); emit(0x1808, 2); emit(0x17FC, 0);
    gpu.mmio_write(xlle::Nv2a::kBase + 0x800040, 4, cursor);
    CHECK(fault.empty());
    CHECK(get32(ram, pitch_three_color + pitch_three_inside) == x_major_color);
    CHECK(gpu.pitch_three_texture_xy_tie_direction0() > tie0);
    CHECK(gpu.pitch_three_texture_xy_tie_direction1() > tie1);
    CHECK(gpu.last_pitch_three_xy_tie_factor3() == 0xFF175B0Eu);
    CHECK(gpu.pitch_three_texture_zero_direction0() == tie_zero0);
    CHECK(gpu.pitch_three_texture_zero_direction1() == tie_zero1);
    CHECK(gpu.pitch_three_texture_rescale_direction0() == tie_rescale0);
    CHECK(gpu.pitch_three_texture_rescale_direction1() == tie_rescale1);

    // Signed-X tie picks the normal -X mapping, rather than Y or legacy Z.
    const float negative_x_reference[12] = {
        -1.0001f, 1, .5f, 1, -1.0001f, 1, .5f, 1,
        -1.0001f, 1, .5f, 1};
    const float negative_x_tie[12] = {
        -1, 1, .5f, 1, -1, 1, .5f, 1, -1, 1, .5f, 1};
    std::memcpy(ram.data() + 0x7500, negative_x_reference,
                sizeof(negative_x_reference));
    std::memcpy(ram.data() + 0x7600, negative_x_reference,
                sizeof(negative_x_reference));
    prepare_zero_cube_target();
    emit(0x17FC, 6); emit(0x1800, 0x00010000); emit(0x1808, 2); emit(0x17FC, 0);
    gpu.mmio_write(xlle::Nv2a::kBase + 0x800040, 4, cursor);
    CHECK(fault.empty());
    const uint32_t negative_x_color =
        get32(ram, pitch_three_color + pitch_three_inside);
    const uint64_t negative_tie0 = gpu.pitch_three_texture_xy_tie_direction0();
    const uint64_t negative_tie1 = gpu.pitch_three_texture_xy_tie_direction1();
    std::memcpy(ram.data() + 0x7500, negative_x_tie,
                sizeof(negative_x_tie));
    std::memcpy(ram.data() + 0x7600, negative_x_tie,
                sizeof(negative_x_tie));
    prepare_zero_cube_target();
    emit(0x17FC, 6); emit(0x1800, 0x00010000); emit(0x1808, 2); emit(0x17FC, 0);
    gpu.mmio_write(xlle::Nv2a::kBase + 0x800040, 4, cursor);
    CHECK(fault.empty());
    CHECK(get32(ram, pitch_three_color + pitch_three_inside) ==
          negative_x_color);
    CHECK(gpu.pitch_three_texture_xy_tie_direction0() > negative_tie0);
    CHECK(gpu.pitch_three_texture_xy_tie_direction1() > negative_tie1);

    // All-axis ties retain the legacy Z choice and therefore do not count as
    // the narrower X/Y-major tie family.
    const float z_major_reference[12] = {
        1, 1, 1.0001f, 1, 1, 1, 1.0001f, 1, 1, 1, 1.0001f, 1};
    const float all_axis_tie[12] = {
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
    std::memcpy(ram.data() + 0x7500, z_major_reference,
                sizeof(z_major_reference));
    std::memcpy(ram.data() + 0x7600, z_major_reference,
                sizeof(z_major_reference));
    prepare_zero_cube_target();
    emit(0x17FC, 6); emit(0x1800, 0x00010000); emit(0x1808, 2); emit(0x17FC, 0);
    gpu.mmio_write(xlle::Nv2a::kBase + 0x800040, 4, cursor);
    CHECK(fault.empty());
    const uint32_t z_major_color =
        get32(ram, pitch_three_color + pitch_three_inside);
    const uint64_t all_axis_tie0 = gpu.pitch_three_texture_xy_tie_direction0();
    const uint64_t all_axis_tie1 = gpu.pitch_three_texture_xy_tie_direction1();
    std::memcpy(ram.data() + 0x7500, all_axis_tie, sizeof(all_axis_tie));
    std::memcpy(ram.data() + 0x7600, all_axis_tie, sizeof(all_axis_tie));
    prepare_zero_cube_target();
    emit(0x17FC, 6); emit(0x1800, 0x00010000); emit(0x1808, 2); emit(0x17FC, 0);
    gpu.mmio_write(xlle::Nv2a::kBase + 0x800040, 4, cursor);
    CHECK(fault.empty());
    CHECK(get32(ram, pitch_three_color + pitch_three_inside) == z_major_color);
    CHECK(gpu.pitch_three_texture_xy_tie_direction0() == all_axis_tie0);
    CHECK(gpu.pitch_three_texture_xy_tie_direction1() == all_axis_tie1);

    // The leading tex=5 pitch family uses FRONT culling. Its projected path
    // bilinearly samples stage 0 and copies RGB with c0 alpha one while the
    // programmed stage-2 cube is output-inert. Its shader-off sibling copies
    // diffuse RGBA. Both replace color and write passing LEQUAL Z24.
    const float pitch_tex5_back_positions[] = {
        10.0f, 10.0f, 100.0f, 1.0f,
        10.0f, 50.0f, 100.0f, 1.0f,
        50.0f, 10.0f, 100.0f, 1.0f,
    };
    const uint32_t pitch_tex5_colors[] = {
        0x80402010, 0x80402010, 0x80402010,
    };
    const float pitch_tex5_texcoords[] = {
        0.0f, 0.0f, 0.0f, 1.0f,
        0.0f, 0.0f, 0.0f, 1.0f,
        0.0f, 0.0f, 0.0f, 1.0f,
    };
    std::memcpy(ram.data() + 0x7000, pitch_tex5_back_positions,
                sizeof(pitch_tex5_back_positions));
    std::memcpy(ram.data() + 0x7100, pitch_tex5_colors,
                sizeof(pitch_tex5_colors));
    std::memcpy(ram.data() + 0x7400, pitch_tex5_texcoords,
                sizeof(pitch_tex5_texcoords));
    for (uint32_t y = 10; y < 50; ++y) {
        for (uint32_t x = 10; x < 50; ++x) {
            const uint32_t offset = y * pitch_three_stride + x * 4;
            put32(ram, pitch_three_color + offset, 0);
            put32(ram, pitch_three_depth + offset, 0xFFFFFF5A);
        }
    }
    std::memset(ram.data() + pitch_three_texture, 0, 16 * 128 * 4);
    put_rgb_texture(pitch_three_texture, 16, 128, 0, 0, 0xFF804020);

    emit(0x0200, 0x05000000);
    emit(0x0204, 0x01E00000);
    emit(0x0208, 0x00000128);
    emit(0x020C, 0x14001400);
    emit(0x0210, pitch_three_color);
    emit(0x0214, pitch_three_depth);
    emit(0x02C0, 0x04FF0000);
    emit(0x02E0, 0x01DF0000);
    emit(0x0300, 0);
    emit(0x0304, 0);
    emit(0x0308, 1);
    emit(0x030C, 1);
    emit(0x0310, 0);
    emit(0x033C, 0x0206);
    emit(0x0340, 1);
    emit(0x0344, 0x0302);
    emit(0x0348, 0x0303);
    emit(0x0350, 0x8006);
    emit(0x0354, 0x0203);
    emit(0x0358, 0x01010101);
    emit(0x035C, 1);
    emit(0x0384, 0x40800000);
    emit(0x0388, 0x41200000);
    emit(0x039C, 0x0404);
    emit(0x03A0, 0x0900);
    emit(0x1D78, 1);
    emit(0x1720, 0x00007000);
    emit(0x172C, 0x80007100);
    emit(0x1744, 0x00007400);
    emit(0x1760, 0x00001042);
    emit(0x176C, 0x00000440);
    emit(0x1784, 0x00001042);
    for (uint32_t stage = 0; stage < 4; ++stage) {
        const uint32_t base = 0x1B00 + stage * 0x40;
        emit(base + 0x00,
             stage == 0 ? pitch_three_texture
                        : stage == 2 ? 0x007FF000 : 0);
        emit(base + 0x04,
             stage == 0 ? 0x0741062A
                        : stage == 2 ? 0x0771072E : 0);
        emit(base + 0x08,
             stage == 0 ? 0x00030404
                        : stage == 2 ? 0x00030303 : 0);
        emit(base + 0x0C,
             stage == 0 || stage == 2 ? 0x4003FFC0 : 0);
        emit(base + 0x10, stage == 0 ? 0x14000000 : 0);
        emit(base + 0x14,
             stage == 0 || stage == 2 ? 0x02022000 : 0);
        emit(base + 0x1C, stage == 0 ? 0x050001E0 : 0);
        emit(base + 0x20, 0);
        emit(base + 0x24, stage == 0 ? 0xFF000000 : 0);
    }
    emit(0x1E6C, 7);
    emit(0x1E70, 0x00000C01);
    emit(0x1E74, 0);
    emit(0x1E78, 0x00210000);
    emit(0x1E60, 0x00000001);
    emit(0x0AC0, 0x08200000);
    emit(0x0260, 0x11200000);
    emit(0x1E40, 0x00000C00);
    emit(0x0AA0, 0x00000C00);
    emit(0x0A60, 0xFFA0FF40);
    emit(0x0A80, 0xFFA0FF40);
    emit(0x0288, 0x0000000C);
    emit(0x028C, 0x00001C80);
    emit(0x1E20, 0);
    emit(0x1E24, 0);
    emit(0x17FC, 5);
    emit(0x1800, 0x00010000);
    emit(0x1808, 2);
    emit(0x17FC, 0);
    gpu.mmio_write(xlle::Nv2a::kBase + 0x800040, 4, cursor);
    CHECK(fault.empty());
    CHECK(gpu.pitch_tex5_batches() == 1);
    CHECK(gpu.pitch_tex5_triangles() == 1);
    CHECK(gpu.pitch_tex5_fragments() > 0);
    CHECK(gpu.pitch_tex5_depth_passes() ==
          gpu.pitch_tex5_fragments());
    CHECK(gpu.pitch_tex5_color_writes() ==
          gpu.pitch_tex5_depth_writes());
    CHECK(gpu.pitch_tex5_depth_writes() ==
          gpu.pitch_tex5_depth_passes());
    CHECK(gpu.pitch_tex5_samples() ==
          gpu.pitch_tex5_depth_passes());
    CHECK(get32(ram, pitch_three_color + pitch_three_inside) ==
          0xFF201008u);
    CHECK(get32(ram, pitch_three_depth + pitch_three_inside) ==
          0x0000645Au);
    CHECK(gpu.last_pitch_tex5_raster_address() >=
          pitch_three_color);
    CHECK(gpu.last_pitch_tex5_raster_address() <
          pitch_three_color + pitch_three_stride * 480);
    CHECK(gpu.pitch_tex5_output_hash() !=
          1469598103934665603ull);

    const uint64_t pitch_tex5_fragments =
        gpu.pitch_tex5_fragments();
    const uint64_t pitch_tex5_writes =
        gpu.pitch_tex5_color_writes();
    const uint64_t pitch_tex5_samples =
        gpu.pitch_tex5_samples();
    std::memcpy(ram.data() + 0x7000, pitch_multi_positions,
                sizeof(pitch_multi_positions));
    emit(0x17FC, 5);
    emit(0x1800, 0x00010000);
    emit(0x1808, 2);
    emit(0x17FC, 0);
    gpu.mmio_write(xlle::Nv2a::kBase + 0x800040, 4, cursor);
    CHECK(fault.empty());
    CHECK(gpu.pitch_tex5_batches() == 2);
    CHECK(gpu.pitch_tex5_triangles() == 1);
    CHECK(gpu.pitch_tex5_fragments() == pitch_tex5_fragments);
    CHECK(gpu.pitch_tex5_color_writes() == pitch_tex5_writes);
    CHECK(gpu.pitch_tex5_samples() == pitch_tex5_samples);

    std::memcpy(ram.data() + 0x7000, pitch_tex5_back_positions,
                sizeof(pitch_tex5_back_positions));
    put32(ram, pitch_three_color + pitch_three_inside, 0);
    put32(ram, pitch_three_depth + pitch_three_inside, 0xFFFFFF5A);
    emit(0x1B00, 0x007FF000);
    emit(0x1E70, 0);
    emit(0x1E78, 0);
    emit(0x1E60, 0x00011101);
    emit(0x0AC0, 0xC4200000);
    emit(0x0260, 0xD4301010);
    emit(0x1E40, 0x000000C0);
    emit(0x0AA0, 0x000000C0);
    emit(0x17FC, 5);
    emit(0x1800, 0x00010000);
    emit(0x1808, 2);
    emit(0x17FC, 0);
    gpu.mmio_write(xlle::Nv2a::kBase + 0x800040, 4, cursor);
    CHECK(fault.empty());
    CHECK(gpu.pitch_tex5_batches() == 3);
    CHECK(gpu.pitch_tex5_triangles() == 2);
    CHECK(gpu.pitch_tex5_color_writes() ==
          pitch_tex5_writes * 2);
    CHECK(gpu.pitch_tex5_depth_writes() ==
          gpu.pitch_tex5_color_writes());
    CHECK(gpu.pitch_tex5_samples() == pitch_tex5_samples);
    CHECK(get32(ram, pitch_three_color + pitch_three_inside) ==
          0x80102040u);
    CHECK(get32(ram, pitch_three_depth + pitch_three_inside) ==
          0x0000645Au);

    // The leading remaining inline state is a linear resolve: PROJECT2D
    // coordinates in texels bilinearly sample a 1280x480 pitch texture into
    // a 640x480 pitch target. RGB comes from t0 and alpha from v0; only
    // dithering is enabled, which is inert for A8R8G8B8. Reversed winding
    // verifies the witnessed disabled-cull state.
    const float inline_blit_positions[] = {
        10.0f, 10.0f, 100.0f, 1.0f,
        50.0f, 10.0f, 100.0f, 1.0f,
        10.0f, 50.0f, 100.0f, 1.0f,
    };
    const uint32_t inline_blit_colors[] = {
        0x80000000, 0x80000000, 0x80000000,
    };
    const float inline_blit_texcoords[] = {
        2.0f, 2.0f, 0.0f, 2.0f,
        2.0f, 2.0f, 0.0f, 2.0f,
        2.0f, 2.0f, 0.0f, 2.0f,
    };
    std::memcpy(ram.data() + 0x7000, inline_blit_positions,
                sizeof(inline_blit_positions));
    std::memcpy(ram.data() + 0x7100, inline_blit_colors,
                sizeof(inline_blit_colors));
    std::memcpy(ram.data() + 0x7400, inline_blit_texcoords,
                sizeof(inline_blit_texcoords));
    std::memset(ram.data() + 0x100000, 0, 0x1400 * 480);
    put32(ram, 0x100000 + 0 * 0x1400 + 0 * 4, 0xFF000000);
    put32(ram, 0x100000 + 0 * 0x1400 + 1 * 4, 0xFF400000);
    put32(ram, 0x100000 + 1 * 0x1400 + 0 * 4, 0xFF004000);
    put32(ram, 0x100000 + 1 * 0x1400 + 1 * 4, 0xFF404000);
    std::memset(ram.data() + 0x400000, 0, 0x0A00 * 480);
    put32(ram, 0x60000, 0xDEADBEEF);

    emit(0x0200, 0x02800000);
    emit(0x0204, 0x01E00000);
    emit(0x0208, 0x00000128);
    emit(0x020C, 0x0A000A00);
    emit(0x0210, 0x00400000);
    emit(0x0214, 0);
    emit(0x02C0, 0x027F0000);
    emit(0x02E0, 0x01DF0000);
    emit(0x0300, 0);
    emit(0x0304, 0);
    emit(0x0308, 0);
    emit(0x030C, 0);
    emit(0x0310, 1);
    emit(0x0318, 0);
    emit(0x031C, 0);
    emit(0x0320, 0);
    emit(0x0324, 0);
    emit(0x032C, 0);
    emit(0x0330, 0);
    emit(0x0334, 0);
    emit(0x0338, 0);
    emit(0x033C, 0x0206);
    emit(0x0340, 1);
    emit(0x0344, 1);
    emit(0x0348, 0);
    emit(0x0350, 0x8006);
    emit(0x0354, 0x0203);
    emit(0x0358, 0x01010101);
    emit(0x035C, 1);
    emit(0x0384, 0x80000000);
    emit(0x0388, 0x80000000);
    emit(0x038C, 0x1B02);
    emit(0x0390, 0x1B02);
    emit(0x0394, 0);
    emit(0x0398, 0x4B7FFFFF);
    emit(0x039C, 0x0405);
    emit(0x03A0, 0x0900);
    emit(0x17BC, 0);
    emit(0x1D78, 1);
    emit(0x1720, 0x00007000);
    emit(0x172C, 0x80007100);
    emit(0x1744, 0x00007400);
    emit(0x1760, 0x00001042);
    emit(0x176C, 0x00000440);
    emit(0x1784, 0x00001042);
    for (uint32_t stage = 0; stage < 4; ++stage) {
        const uint32_t base = 0x1B00 + stage * 0x40;
        emit(base + 0x00, stage == 0 ? 0x00100000 : 0);
        emit(base + 0x04, stage == 0 ? 0x0001122A : 0);
        emit(base + 0x08, stage == 0 ? 0x00030303 : 0);
        emit(base + 0x0C, stage == 0 ? 0x4003FFC0 : 0);
        emit(base + 0x10, stage == 0 ? 0x14000000 : 0);
        emit(base + 0x14, stage == 0 ? 0x02022000 : 0);
        emit(base + 0x1C, stage == 0 ? 0x050001E0 : 0);
        emit(base + 0x20, 0);
        emit(base + 0x24, stage == 0 ? 0xFF000000 : 0);
    }
    emit(0x1E6C, 7);
    emit(0x1E70, 0x00000001);
    emit(0x1E74, 0);
    emit(0x1E78, 0x00210000);
    emit(0x1E60, 0x00000001);
    emit(0x0AC0, 0x08200000);
    emit(0x0260, 0x14200000);
    emit(0x1E40, 0x00000C00);
    emit(0x0AA0, 0x00000C00);
    emit(0x0A60, 0xFFFFFFFF);
    emit(0x0A80, 0xFFFFFFFF);
    emit(0x0288, 0x0000000C);
    emit(0x028C, 0x00001C80);
    emit(0x1E20, 0);
    emit(0x1E24, 0);
    emit(0x17FC, 5);
    emit(0x1800, 0x00010000);
    emit(0x1808, 2);
    emit(0x17FC, 0);
    gpu.mmio_write(xlle::Nv2a::kBase + 0x800040, 4, cursor);
    CHECK(fault.empty());
    CHECK(gpu.inline_linear_blit_batches() == 1);
    CHECK(gpu.inline_linear_blit_triangles() == 1);
    CHECK(gpu.inline_linear_blit_fragments() > 0);
    CHECK(gpu.inline_linear_blit_writes() ==
          gpu.inline_linear_blit_fragments());
    CHECK(gpu.inline_linear_blit_samples() ==
          gpu.inline_linear_blit_writes());
    const uint32_t inline_blit_inside = 20 * 0x0A00 + 20 * 4;
    CHECK(get32(ram, 0x400000 + inline_blit_inside) == 0x80202000);
    CHECK(get32(ram, 0x400000 + 5 * 0x0A00 + 5 * 4) == 0);
    CHECK(get32(ram, 0x60000) == 0xDEADBEEF);
    CHECK(gpu.last_inline_linear_blit_raster_address() >= 0x400000);
    CHECK(gpu.last_inline_linear_blit_raster_address() < 0x52C000);
    CHECK(gpu.inline_linear_blit_output_hash() !=
          1469598103934665603ull);

    const float inline_blit_reversed[] = {
        10.0f, 10.0f, 100.0f, 1.0f,
        10.0f, 50.0f, 100.0f, 1.0f,
        50.0f, 10.0f, 100.0f, 1.0f,
    };
    const uint64_t inline_blit_first_writes =
        gpu.inline_linear_blit_writes();
    std::memcpy(ram.data() + 0x7000, inline_blit_reversed,
                sizeof(inline_blit_reversed));
    emit(0x17FC, 5);
    emit(0x1800, 0x00010000);
    emit(0x1808, 2);
    emit(0x17FC, 0);
    gpu.mmio_write(xlle::Nv2a::kBase + 0x800040, 4, cursor);
    CHECK(fault.empty());
    CHECK(gpu.inline_linear_blit_batches() == 2);
    CHECK(gpu.inline_linear_blit_triangles() == 2);
    CHECK(gpu.inline_linear_blit_writes() ==
          inline_blit_first_writes * 2);
    CHECK(get32(ram, 0x400000 + inline_blit_inside) == 0x80202000);
    CHECK(get32(ram, 0x60000) == 0xDEADBEEF);

    // A later retained variant changes c0/c1, which this combiner never
    // references, plus blend factors while blending is disabled. Both captured
    // differences must leave output and coverage exact.
    emit(0x0A60, 0xC4A0FF60);
    emit(0x0A80, 0xC4A0FF60);
    emit(0x0344, 0x0302);
    emit(0x0348, 0x0303);
    emit(0x17FC, 5);
    emit(0x1800, 0x00010000);
    emit(0x1808, 2);
    emit(0x17FC, 0);
    gpu.mmio_write(xlle::Nv2a::kBase + 0x800040, 4, cursor);
    CHECK(fault.empty());
    CHECK(gpu.inline_linear_blit_batches() == 3);
    CHECK(gpu.inline_linear_blit_triangles() == 3);
    CHECK(gpu.inline_linear_blit_writes() ==
          inline_blit_first_writes * 3);
    CHECK(get32(ram, 0x400000 + inline_blit_inside) == 0x80202000);
    CHECK(get32(ram, 0x60000) == 0xDEADBEEF);

    // CLAMP_TO_EDGE never samples the border color. The boot animation's
    // otherwise-identical resolve uses transparent black instead.
    emit(0x1B24, 0);
    emit(0x17FC, 5);
    emit(0x1800, 0x00010000);
    emit(0x1808, 2);
    emit(0x17FC, 0);
    gpu.mmio_write(xlle::Nv2a::kBase + 0x800040, 4, cursor);
    CHECK(fault.empty());
    CHECK(gpu.inline_linear_blit_batches() == 4);
    CHECK(gpu.inline_linear_blit_triangles() == 4);
    CHECK(gpu.inline_linear_blit_writes() ==
          inline_blit_first_writes * 4);
    CHECK(get32(ram, 0x400000 + inline_blit_inside) == 0x80202000);

    // PROJECT2D ignores the P address mode; the boot resolve leaves it at WRAP.
    emit(0x1B08, 0x00010303);
    emit(0x17FC, 5);
    emit(0x1800, 0x00010000);
    emit(0x1808, 2);
    emit(0x17FC, 0);
    gpu.mmio_write(xlle::Nv2a::kBase + 0x800040, 4, cursor);
    CHECK(fault.empty());
    CHECK(gpu.inline_linear_blit_batches() == 5);
    CHECK(gpu.inline_linear_blit_triangles() == 5);
    CHECK(gpu.inline_linear_blit_writes() ==
          inline_blit_first_writes * 5);
    CHECK(get32(ram, 0x400000 + inline_blit_inside) == 0x80202000);

    // PROJECT2D also ignores the retained shadow-depth comparison function.
    emit(0x1E6C, 0);
    emit(0x17FC, 5);
    emit(0x1800, 0x00010000);
    emit(0x1808, 2);
    emit(0x17FC, 0);
    gpu.mmio_write(xlle::Nv2a::kBase + 0x800040, 4, cursor);
    CHECK(fault.empty());
    CHECK(gpu.inline_linear_blit_batches() == 6);
    CHECK(gpu.inline_linear_blit_triangles() == 6);
    CHECK(gpu.inline_linear_blit_writes() ==
          inline_blit_first_writes * 6);
    CHECK(get32(ram, 0x400000 + inline_blit_inside) == 0x80202000);

    // Inline dashboard geometry uses a distinct linear color profile:
    // PROJECT2D texture * c0, alpha GEQUAL 1, then SRC_ALPHA/ONE ADD.
    // The ALWAYS depth state must neither read nor write the zeta plane.
    const float inline_color_positions[] = {
        10.0f, 10.0f, 100.0f, 1.0f,
        50.0f, 10.0f, 100.0f, 1.0f,
        10.0f, 50.0f, 100.0f, 1.0f,
    };
    const float inline_color_texcoords[] = {
        1.0f / 256.0f, 1.0f / 256.0f, 0.0f, 1.0f,
        1.0f / 256.0f, 1.0f / 256.0f, 0.0f, 1.0f,
        1.0f / 256.0f, 1.0f / 256.0f, 0.0f, 1.0f,
    };
    for (uint32_t offset = 0; offset < 64 * 64 * 4; offset += 4)
        put32(ram, 0x20000 + offset, 0x10203040);
    std::memset(ram.data() + 0xC0000, 0, 256 * 256 * 4);
    put_rgb_texture(0xC0000, 256, 256, 0, 0, 0x80808080);
    put_rgb_texture(0xC0000, 256, 256, 1, 0, 0x80808080);
    put_rgb_texture(0xC0000, 256, 256, 0, 1, 0x80808080);
    put_rgb_texture(0xC0000, 256, 256, 1, 1, 0x80808080);
    const uint32_t zeta_before_inline_color =
        get32(ram, 0x60000 + 20 * 256 + 20 * 4);

    emit(0x0200, 0x00400000);
    emit(0x0204, 0x00400000);
    emit(0x0208, 0x00000128);
    emit(0x020C, 0x01000100);
    emit(0x0210, 0x00020000);
    emit(0x0214, 0x00060000);
    emit(0x02C0, 0x003F0000);
    emit(0x02E0, 0x003F0000);
    emit(0x0300, 1);
    emit(0x0304, 1);
    emit(0x0308, 1);
    emit(0x030C, 1);
    emit(0x0310, 0);
    emit(0x033C, 0x0206);
    emit(0x0340, 1);
    emit(0x0344, 0x0302);
    emit(0x0348, 1);
    emit(0x0350, 0x8006);
    emit(0x0354, 0x0207);
    emit(0x0358, 0x01010101);
    emit(0x035C, 0);
    emit(0x0394, 0);
    emit(0x0398, 0x4B7FFFFF);
    emit(0x1D78, 1);
    emit(0x1B00, 0x000C0000);
    emit(0x1B04, 0x0888062A);
    emit(0x1B08, 0x00010303);
    emit(0x1B0C, 0x4003FFC0);
    emit(0x1B10, 0x14000000);
    emit(0x1B14, 0x02022000);
    emit(0x1B20, 0);
    emit(0x1B24, 0);
    emit(0x1B4C, 0);
    emit(0x1B8C, 0);
    emit(0x1BCC, 0);
    emit(0x1E70, 0x00000001);
    emit(0x1E78, 0x00210000);
    emit(0x1E60, 0x00000001);
    emit(0x0AC0, 0x01080000);
    emit(0x0260, 0x11180000);
    emit(0x1E40, 0x00000C00);
    emit(0x0AA0, 0x00000C00);
    emit(0x0A60, 0x80808080);
    emit(0x0A80, 0x80808080);
    emit(0x0288, 0x0000000C);
    emit(0x028C, 0x00001C80);
    emit(0x1E20, 0);
    emit(0x1E24, 0);

    std::vector<uint32_t> inline_color_words(
        3 * kInlineVertexBytes / sizeof(uint32_t), 0);
    auto* inline_color_bytes =
        reinterpret_cast<uint8_t*>(inline_color_words.data());
    for (size_t vertex = 0; vertex < 3; ++vertex) {
        const size_t offset = vertex * kInlineVertexBytes;
        std::memcpy(inline_color_bytes + offset,
                    inline_color_positions + vertex * 4, 4 * sizeof(float));
        std::memcpy(inline_color_bytes + offset + 16,
                    &textured_colors[vertex], sizeof(uint32_t));
        const std::array<float, 4> inline_specular{{0, 0, 0, 1}};
        std::memcpy(inline_color_bytes + offset + 20,
                    inline_specular.data(), 4 * sizeof(float));
        std::memcpy(inline_color_bytes + offset + 36,
                    inline_color_texcoords + vertex * 4,
                    4 * sizeof(float));
        std::memcpy(inline_color_bytes + offset + 52,
                    texture1_coords + vertex * 4, 4 * sizeof(float));
        std::memcpy(inline_color_bytes + offset + 68,
                    texture2_coords + vertex * 4, 4 * sizeof(float));
    }
    emit(0x17FC, 5);
    for (uint32_t word : inline_color_words) emit(0x1818, word);
    emit(0x17FC, 0);
    gpu.mmio_write(xlle::Nv2a::kBase + 0x800040, 4, cursor);
    CHECK(fault.empty());
    CHECK(gpu.inline_color_batches() == 1);
    CHECK(gpu.inline_color_triangles() == 1);
    CHECK(gpu.inline_color_fragments() > 0);
    CHECK(gpu.inline_texture_samples() == gpu.inline_color_fragments());
    CHECK(gpu.inline_color_alpha_passes() == gpu.inline_color_writes());
    CHECK(gpu.inline_color_writes() > 0);
    CHECK(get32(ram, 0x20000 + 20 * 256 + 20 * 4) == 0x20304050u);
    CHECK(get32(ram, 0x20000 + 5 * 256 + 5 * 4) == 0x10203040u);
    CHECK(get32(ram, 0x60000 + 20 * 256 + 20 * 4) ==
          zeta_before_inline_color);
    CHECK(gpu.last_inline_raster_address() >= 0x20000);
    CHECK(gpu.last_inline_raster_address() < 0x24000);
    CHECK(gpu.inline_color_output_hash() != 1469598103934665603ull);

    const uint64_t inline_color_writes = gpu.inline_color_writes();
    emit(0x1B08, 0x00030303);  // later dashboard variant: unused P clamps
    emit(0x0A60, 0x00808080);  // zero final alpha: fail GEQUAL 1
    emit(0x0A80, 0x00808080);
    emit(0x17FC, 5);
    for (uint32_t word : inline_color_words) emit(0x1818, word);
    emit(0x17FC, 0);
    gpu.mmio_write(xlle::Nv2a::kBase + 0x800040, 4, cursor);
    CHECK(fault.empty());
    CHECK(gpu.inline_color_batches() == 2);
    CHECK(gpu.inline_color_writes() == inline_color_writes);
    CHECK(get32(ram, 0x20000 + 20 * 256 + 20 * 4) == 0x20304050u);

    // A later inline pass enables read-only Z24S8 LESS testing. Passing and
    // rejected fragments must leave both depth and stencil untouched.
    for (uint32_t offset = 0; offset < 64 * 64 * 4; offset += 4)
        put32(ram, 0x60000 + offset, (200u << 8) | 0x5Au);
    emit(0x0A60, 0x80808080);
    emit(0x0A80, 0x80808080);
    emit(0x0354, 0x0202);
    emit(0x1B24, 0xFF000000);  // inert with U/V CLAMP_TO_EDGE
    emit(0x17FC, 5);
    for (uint32_t word : inline_color_words) emit(0x1818, word);
    emit(0x17FC, 0);
    gpu.mmio_write(xlle::Nv2a::kBase + 0x800040, 4, cursor);
    CHECK(fault.empty());
    CHECK(gpu.inline_color_batches() == 3);
    CHECK(get32(ram, 0x20000 + 20 * 256 + 20 * 4) == 0x30405060u);
    CHECK(get32(ram, 0x60000 + 20 * 256 + 20 * 4) ==
          ((200u << 8) | 0x5Au));

    for (uint32_t offset = 0; offset < 64 * 64 * 4; offset += 4)
        put32(ram, 0x60000 + offset, (50u << 8) | 0x5Au);
    const uint64_t inline_depth_reject_writes = gpu.inline_color_writes();
    emit(0x17FC, 5);
    for (uint32_t word : inline_color_words) emit(0x1818, word);
    emit(0x17FC, 0);
    gpu.mmio_write(xlle::Nv2a::kBase + 0x800040, 4, cursor);
    CHECK(fault.empty());
    CHECK(gpu.inline_color_batches() == 4);
    CHECK(gpu.inline_color_writes() == inline_depth_reject_writes);
    CHECK(get32(ram, 0x20000 + 20 * 256 + 20 * 4) == 0x30405060u);
    CHECK(get32(ram, 0x60000 + 20 * 256 + 20 * 4) ==
          ((50u << 8) | 0x5Au));

    // The leading RS0 state from probe-reduced-diag-6.4b is a four-vertex
    // fan.  Its one combiner modulates PROJECT2D t0 by C0 (A0/FF/40); t1/t2
    // are enabled but do not feed that combiner.  Keep their captured state
    // exact so a near miss faults rather than silently widening the profile.
    constexpr uint32_t rs0_color = 0x00020000;
    constexpr uint32_t rs0_depth = 0x00300000;
    constexpr uint32_t rs0_texture = 0x001C0000;
    constexpr uint32_t rs0_pitch = 0x1400;
    std::memset(ram.data() + rs0_color, 0, rs0_pitch * 480);
    for (uint32_t offset = 0; offset < rs0_pitch * 480; offset += 4)
        put32(ram, rs0_depth + offset, 0x00FFFF00);
    std::memset(ram.data() + rs0_texture, 0, 256 * 256 * 4);
    for (uint32_t y = 0; y < 256; ++y)
        for (uint32_t x = 0; x < 256; ++x)
            put_rgb_texture(rs0_texture, 256, 256, x, y, 0xFF010203);
    put_rgb_texture(rs0_texture, 256, 256, 0, 0, 0xFF804020);
    const uint32_t rs0_inside = 20 * rs0_pitch + 20 * 4;
    put32(ram, rs0_color + rs0_inside, 0x10203040);
    const float rs0_positions[] = {
        10, 10, 100, 1, 50, 10, 100, 1,
        50, 50, 100, 1, 10, 50, 100, 1,
    };
    const float rs0_texcoords[] = {
        -4.0f / 256, -4.0f / 256, 0, 1,
        -4.0f / 256, -4.0f / 256, 0, 1,
        -4.0f / 256, -4.0f / 256, 0, 1,
        -4.0f / 256, -4.0f / 256, 0, 1,
    };
    std::vector<uint32_t> rs0_words(4 * kInlineVertexBytes / 4, 0);
    auto* rs0_bytes = reinterpret_cast<uint8_t*>(rs0_words.data());
    for (size_t vertex = 0; vertex < 4; ++vertex) {
        const size_t offset = vertex * kInlineVertexBytes;
        std::memcpy(rs0_bytes + offset, rs0_positions + vertex * 4, 16);
        std::memcpy(rs0_bytes + offset + 36, rs0_texcoords + vertex * 4, 16);
        std::memcpy(rs0_bytes + offset + 52, rs0_texcoords + vertex * 4, 16);
        std::memcpy(rs0_bytes + offset + 68, rs0_texcoords + vertex * 4, 16);
    }
    emit(0x0200, 0x05000000); emit(0x0204, 0x01E00000);
    emit(0x0208, 0x00000128); emit(0x020C, 0x14001400);
    emit(0x0210, rs0_color); emit(0x0214, rs0_depth);
    emit(0x02C0, 0x04FF0000); emit(0x02E0, 0x01DF0000);
    emit(0x0300, 0); emit(0x0304, 1); emit(0x0308, 1); emit(0x030C, 1);
    emit(0x0310, 0); emit(0x033C, 0x0206); emit(0x0340, 1);
    emit(0x0344, 0x0302); emit(0x0348, 1); emit(0x0350, 0x8006);
    emit(0x0354, 0x0203); emit(0x0358, 0x01010101); emit(0x035C, 0);
    emit(0x0384, 0x40800000); emit(0x0388, 0x41200000);
    emit(0x038C, 0x1B02); emit(0x0390, 0x1B02); emit(0x0394, 0);
    emit(0x0398, 0x4B7FFFFF); emit(0x039C, 0x0405); emit(0x03A0, 0x0900);
    emit(0x1D78, 1);
    for (uint32_t stage = 0; stage < 4; ++stage) {
        const uint32_t base = 0x1B00 + stage * 0x40;
        const bool t0 = stage == 0, cube = stage == 1 || stage == 2;
        emit(base, t0 ? rs0_texture : cube ? 0x00240000 : 0);
        emit(base + 4, t0 ? 0x0888062A : cube ? 0x0771072E : 0);
        emit(base + 8, t0 || cube ? 0x00030303 : 0);
        emit(base + 12, t0 || cube ? 0x4003FFC0 : 0);
        emit(base + 16, t0 ? 0x14000000 : 0);
        emit(base + 20, t0 || cube ? 0x02022000 : 0);
        emit(base + 28, t0 ? 0x050001E0 : 0); emit(base + 32, 0);
        emit(base + 36, t0 ? 0xFF000000 : 0);
    }
    emit(0x1E6C, 7); emit(0x1E70, 0x00000C61); emit(0x1E74, 0);
    emit(0x1E78, 0x00210000); emit(0x1E60, 1);
    emit(0x0AC0, 0x08010000); emit(0x0260, 0x11200000);
    emit(0x1E40, 0x00000C00); emit(0x0AA0, 0x00000C00);
    emit(0x0A60, 0xFFA0FF40); emit(0x0A80, 0xFFA0FF40);
    emit(0x0288, 0x0000000C); emit(0x028C, 0x00001C80);
    emit(0x1E20, 0xFF3F3F3F); emit(0x1E24, 0);
    emit(0x17FC, 7); for (uint32_t word : rs0_words) emit(0x1818, word);
    emit(0x17FC, 0);
    gpu.mmio_write(xlle::Nv2a::kBase + 0x800040, 4, cursor);
    CHECK(fault.empty());
    CHECK(gpu.tex7_fan_batches() == 1);
    CHECK(gpu.tex7_fan_triangles() == 2);
    CHECK(gpu.tex7_fan_depth_passes() == gpu.tex7_fan_writes());
    CHECK(gpu.tex7_fan_samples() == gpu.tex7_fan_writes());
    CHECK(gpu.tex7_fan_output_hash() != UINT64_C(1469598103934665603));
    CHECK(gpu.last_tex7_fan_raster_address() >= rs0_color);
    CHECK(gpu.last_tex7_fan_raster_address() <
          rs0_color + rs0_pitch * 480);
    CHECK(get32(ram, rs0_color + rs0_inside) == 0xFF707048u);
    CHECK(get32(ram, rs0_depth + rs0_inside) == 0x00FFFF00u);

    // The later 30-batch state changes only the unused border register. With
    // out-of-range coordinates and a distinct edge texel, identical output
    // proves the exact 0x30303 address mode clamps rather than samples border.
    const uint64_t rs0_first_fragments = gpu.tex7_fan_fragments();
    const uint64_t rs0_first_passes = gpu.tex7_fan_depth_passes();
    const uint64_t rs0_first_writes = gpu.tex7_fan_writes();
    const uint64_t rs0_first_samples = gpu.tex7_fan_samples();
    put32(ram, rs0_color + rs0_inside, 0x10203040);
    emit(0x1B24, 0);
    emit(0x17FC, 7);
    for (uint32_t word : rs0_words) emit(0x1818, word);
    emit(0x17FC, 0);
    gpu.mmio_write(xlle::Nv2a::kBase + 0x800040, 4, cursor);
    CHECK(fault.empty());
    CHECK(gpu.tex7_fan_batches() == 2);
    CHECK(gpu.tex7_fan_triangles() == 4);
    CHECK(gpu.tex7_fan_fragments() == rs0_first_fragments * 2);
    CHECK(gpu.tex7_fan_depth_passes() == rs0_first_passes * 2);
    CHECK(gpu.tex7_fan_writes() == rs0_first_writes * 2);
    CHECK(gpu.tex7_fan_samples() == rs0_first_samples * 2);
    CHECK(get32(ram, rs0_color + rs0_inside) == 0xFF707048u);
    CHECK(get32(ram, rs0_depth + rs0_inside) == 0x00FFFF00u);

    // No other border value has been witnessed for this exact profile.
    emit(0x1B24, 0xFF010203);
    emit(0x17FC, 7);
    for (uint32_t word : rs0_words) emit(0x1818, word);
    emit(0x17FC, 0);
    gpu.mmio_write(xlle::Nv2a::kBase + 0x800040, 4, cursor);
    CHECK(fault.empty());
    CHECK(gpu.tex7_fan_batches() == 2);
    CHECK(gpu.tex7_fan_triangles() == 4);
    emit(0x1B24, 0xFF000000);

    // The witnessed cull state accepts clockwise fans only. Reversing all
    // four vertices must neither touch color nor the read-only Z24S8 plane.
    std::vector<uint32_t> rs0_reversed_words = rs0_words;
    auto* rs0_reversed_bytes =
        reinterpret_cast<uint8_t*>(rs0_reversed_words.data());
    for (size_t vertex = 0; vertex < 4; ++vertex)
        std::memcpy(rs0_reversed_bytes + vertex * kInlineVertexBytes,
                    rs0_positions + (3 - vertex) * 4, 16);
    const uint64_t rs0_writes = gpu.tex7_fan_writes();
    emit(0x17FC, 7);
    for (uint32_t word : rs0_reversed_words) emit(0x1818, word);
    emit(0x17FC, 0);
    gpu.mmio_write(xlle::Nv2a::kBase + 0x800040, 4, cursor);
    CHECK(fault.empty());
    CHECK(gpu.tex7_fan_batches() == 3);
    CHECK(gpu.tex7_fan_triangles() == 4);
    CHECK(gpu.tex7_fan_writes() == rs0_writes);
    CHECK(get32(ram, rs0_color + rs0_inside) == 0xFF707048u);
    CHECK(get32(ram, rs0_depth + rs0_inside) == 0x00FFFF00u);

    // Alpha testing is disabled in RS0; reject a near state rather than
    // widening this recognized combiner profile.
    emit(0x033C, 0x0207); emit(0x17FC, 7);
    for (uint32_t word : rs0_words) emit(0x1818, word);
    emit(0x17FC, 0); gpu.mmio_write(xlle::Nv2a::kBase + 0x800040, 4, cursor);
    CHECK(fault == "NV2A tex7 fan draw exceeds the witnessed RS0 subset");

    // RS0/RS1 in probe-rs2-ohci-6.4b: tex=F, shader 8421, four PROJECT2D
    // stages and a six-stage combiner. Xemu's register-combiner semantics
    // reduce this exact state to black RGB with alpha one, so the strict
    // profile must only cover its alpha channel and leave Z24S8 untouched.
    fault.clear();
    put32(ram, rs0_color + rs0_inside, 0x12345678);
    const uint32_t texf_zeta = get32(ram, rs0_depth + rs0_inside);
    emit(0x033C, 0x0206); emit(0x0340, 1); emit(0x0354, 0x0207);
    emit(0x0300, 1); emit(0x0304, 1); emit(0x0308, 1); emit(0x030C, 1);
    for (uint32_t stage = 0; stage < 4; ++stage) {
        const uint32_t base = 0x1B00 + stage * 0x40;
        emit(base, stage == 0 ? 0x02C05000 : 0x029F3000 - (stage - 1) * 0x10000);
        emit(base + 4, stage == 0 ? 0x09A1062A : 0x0881192A);
        emit(base + 8, stage == 0 ? 0x00030303 : 0x00030101);
        emit(base + 12, 0x4003FFC0);
        emit(base + 16, stage == 0 ? 0x14000000 : stage == 3 ? 0x04000000 : 0);
        emit(base + 20, 0x02022000);
        emit(base + 28, stage == 0 ? 0x050001E0 : stage == 3 ? 0x02000200 : 0);
        emit(base + 32, 0); emit(base + 36, stage == 3 ? 0xFFFFFFFF : 0);
    }
    const uint32_t texf_ci[] = {0xD920DA20,0xCD20DB20,0xCDC10000,0xC8CD0000,0xCC20C120,0};
    const uint32_t texf_ai[] = {0xD930DA30,0xDD30DB30,0xDDD11010,0,0,0x30301010};
    const uint32_t texf_co[] = {0x00000D00,0x00000D00,0x000000D0,0x000000C0,0x00000C00,0};
    const uint32_t texf_ao[] = {0x00000D00,0x00000D00,0x000000D0,0,0,0x000000C0};
    for (uint32_t stage = 0; stage < 6; ++stage) {
        emit(0x0AC0 + stage * 4, texf_ci[stage]);
        emit(0x0260 + stage * 4, texf_ai[stage]);
        emit(0x1E40 + stage * 4, texf_co[stage]);
        emit(0x0AA0 + stage * 4, texf_ao[stage]);
        emit(0x0A60 + stage * 4, stage == 2 ? 0xFF000000 : 0);
        emit(0x0A80 + stage * 4, 0);
    }
    emit(0x1E6C, 7); emit(0x1E70, 0x00008421); emit(0x1E74, 0);
    emit(0x1E78, 0); emit(0x1E60, 0x00011106);
    emit(0x0288, 0x0000000C); emit(0x028C, 0x00001C80);
    emit(0x1E20, 0); emit(0x1E24, 0);
    emit(0x17FC, 7); for (uint32_t word : rs0_words) emit(0x1818, word);
    emit(0x17FC, 0); gpu.mmio_write(xlle::Nv2a::kBase + 0x800040, 4, cursor);
    CHECK(fault.empty());
    CHECK(gpu.texf_projected_fan_batches() == 1);
    CHECK(gpu.texf_projected_fan_triangles() == 2);
    CHECK(gpu.texf_projected_fan_writes() > 0);
    CHECK(gpu.texf_projected_fan_samples() == 0); // statically output-inert.
    CHECK(get32(ram, rs0_color + rs0_inside) == 0xFF345678u);
    CHECK(get32(ram, rs0_depth + rs0_inside) == texf_zeta);
    CHECK(gpu.texf_projected_fan_output_hash() != UINT64_C(1469598103934665603));

    // Texture values are output-inert, but the enabled image ranges are not:
    // a too-small DMA-B object must fail preflight before raster writes.
    const uint64_t texf_writes = gpu.texf_projected_fan_writes();
    gpu.mmio_write(xlle::Nv2a::kBase + 0x700404, 4, 0x007FFFFF);
    emit(0x17FC, 7); for (uint32_t word : rs0_words) emit(0x1818, word);
    emit(0x17FC, 0); gpu.mmio_write(xlle::Nv2a::kBase + 0x800040, 4, cursor);
    CHECK(fault == "NV2A raster tex=f texture DMA range is invalid");
    CHECK(gpu.texf_projected_fan_writes() == texf_writes);
    gpu.mmio_write(xlle::Nv2a::kBase + 0x700404, 4, 0x03FFFFFF);
    fault.clear();

    // Exact window and depth clip bounds are part of RS0/RS1 admission.
    // Each near state remains recorder-only and cannot change counters or output.
    const uint64_t texf_batches_before_clip = gpu.texf_projected_fan_batches();
    const uint64_t texf_hash_before_clip = gpu.texf_projected_fan_output_hash();
    emit(0x02C0, 0x04FE0000); emit(0x17FC, 7);
    for (uint32_t word : rs0_words) emit(0x1818, word);
    emit(0x17FC, 0); gpu.mmio_write(xlle::Nv2a::kBase + 0x800040, 4, cursor);
    CHECK(fault.empty());
    CHECK(gpu.texf_projected_fan_batches() == texf_batches_before_clip);
    CHECK(gpu.texf_projected_fan_writes() == texf_writes);
    CHECK(gpu.texf_projected_fan_output_hash() == texf_hash_before_clip);
    emit(0x02C0, 0x04FF0000);
    emit(0x02E0, 0x01DE0000); emit(0x17FC, 7);
    for (uint32_t word : rs0_words) emit(0x1818, word);
    emit(0x17FC, 0); gpu.mmio_write(xlle::Nv2a::kBase + 0x800040, 4, cursor);
    CHECK(fault.empty());
    CHECK(gpu.texf_projected_fan_batches() == texf_batches_before_clip);
    CHECK(gpu.texf_projected_fan_writes() == texf_writes);
    CHECK(gpu.texf_projected_fan_output_hash() == texf_hash_before_clip);
    emit(0x02E0, 0x01DF0000);
    emit(0x0394, 1); emit(0x17FC, 7);
    for (uint32_t word : rs0_words) emit(0x1818, word);
    emit(0x17FC, 0); gpu.mmio_write(xlle::Nv2a::kBase + 0x800040, 4, cursor);
    CHECK(fault.empty());
    CHECK(gpu.texf_projected_fan_batches() == texf_batches_before_clip);
    CHECK(gpu.texf_projected_fan_writes() == texf_writes);
    CHECK(gpu.texf_projected_fan_output_hash() == texf_hash_before_clip);
    emit(0x0394, 0);
    emit(0x0398, 0x4B7FFFFE); emit(0x17FC, 7);
    for (uint32_t word : rs0_words) emit(0x1818, word);
    emit(0x17FC, 0); gpu.mmio_write(xlle::Nv2a::kBase + 0x800040, 4, cursor);
    CHECK(fault.empty());
    CHECK(gpu.texf_projected_fan_batches() == texf_batches_before_clip);
    CHECK(gpu.texf_projected_fan_writes() == texf_writes);
    CHECK(gpu.texf_projected_fan_output_hash() == texf_hash_before_clip);
    emit(0x0398, 0x4B7FFFFF);

    // Nearby sampler offsets, shadow=0, and non-white T3 border remain
    // recorder-only: none may widen this exact RS0/RS1 admission.
    const uint64_t texf_batches = gpu.texf_projected_fan_batches();
    emit(0x1B00 + 0x40, 0x029F4000); emit(0x17FC, 7);
    for (uint32_t word : rs0_words) emit(0x1818, word);
    emit(0x17FC, 0); gpu.mmio_write(xlle::Nv2a::kBase + 0x800040, 4, cursor);
    CHECK(fault.empty()); CHECK(gpu.texf_projected_fan_batches() == texf_batches);
    emit(0x1B00 + 0x40, 0x029F3000);
    emit(0x1E6C, 0); emit(0x17FC, 7);
    for (uint32_t word : rs0_words) emit(0x1818, word);
    emit(0x17FC, 0); gpu.mmio_write(xlle::Nv2a::kBase + 0x800040, 4, cursor);
    CHECK(fault.empty()); CHECK(gpu.texf_projected_fan_batches() == texf_batches);
    emit(0x1E6C, 7); emit(0x1B00 + 3 * 0x40 + 36, 0); emit(0x17FC, 7);
    for (uint32_t word : rs0_words) emit(0x1818, word);
    emit(0x17FC, 0); gpu.mmio_write(xlle::Nv2a::kBase + 0x800040, 4, cursor);
    CHECK(fault.empty()); CHECK(gpu.texf_projected_fan_batches() == texf_batches);

    // AvSetDisplayMode's scanout path: VGA CRTC pitch/depth, PRAMDAC dimensions/format,
    // and PCRTC_START resolve unified-RAM RGB565 into deterministic RGBA output.
    auto crtc_write = [&](uint8_t index, uint8_t value) {
        gpu.mmio_write(xlle::Nv2a::kBase + 0x6013D4, 1, index);
        gpu.mmio_write(xlle::Nv2a::kBase + 0x6013D5, 1, value);
    };
    crtc_write(0x13, 1);  // pitch = 1 * 8 bytes
    crtc_write(0x28, 2);  // 16-bit depth
    gpu.mmio_write(xlle::Nv2a::kBase + 0x680600, 4, 1u << 12);  // RGB565
    gpu.mmio_write(xlle::Nv2a::kBase + 0x680800, 4, 1);         // height = 2
    gpu.mmio_write(xlle::Nv2a::kBase + 0x680820, 4, 1);         // width = 2
    gpu.mmio_write(xlle::Nv2a::kBase + 0x600800, 4, 0x4000);
    const uint16_t scanout_pixels[] = {0xf800, 0x07e0, 0, 0, 0x001f, 0xffff};
    std::memcpy(ram.data() + 0x4000, scanout_pixels, sizeof(scanout_pixels));
    xlle::Nv2a::ScanoutInfo scanout;
    std::vector<uint8_t> rgba;
    std::string scanout_error;
    CHECK(gpu.capture_scanout(rgba, scanout, scanout_error));
    CHECK(scanout.start == 0x4000);
    CHECK(scanout.pitch == 8);
    CHECK(scanout.width == 2 && scanout.height == 2 && scanout.bits_per_pixel == 16);
    const std::vector<uint8_t> expected_rgba = {
        255, 0, 0, 255, 0, 255, 0, 255,
        0, 0, 255, 255, 255, 255, 255, 255,
    };
    CHECK(rgba == expected_rgba);
    CHECK(gpu.mmio_read(xlle::Nv2a::kBase + 0x600800, 4) == 0x4000);
    CHECK(gpu.mmio_read(xlle::Nv2a::kBase + 0x6013D5, 1) == 2);
    CHECK(gpu.scanout_writes() == 1);
    CHECK(gpu.scanout_nonzero_writes() == 1);
    CHECK(gpu.last_nonzero_scanout_start() == 0x4000);
    std::vector<uint32_t> starts;
    CHECK(gpu.recent_scanout_starts(4, starts) == 1);
    CHECK(starts.size() == 1 && starts[0] == 0x4000);

    // The dashboard's first immediate draw is a Kelvin quad: generic texture slot 9 and
    // diffuse slot 3 are persistent state, while legacy SET_VERTEX4F position W commits
    // each vertex. Keep this fixture independent so existing absolute counters stay stable.
    {
        std::string immediate_fault;
        xlle::Nv2a immediate_gpu(
            [&](const std::string& why) { immediate_fault = why; });
        std::vector<uint8_t> immediate_ram(0x10000, 0);
        immediate_gpu.set_mem_access(
            [&](uint32_t addr, void* dst, size_t n) {
                if (addr > immediate_ram.size() || n > immediate_ram.size() - addr)
                    return false;
                std::memcpy(dst, immediate_ram.data() + addr, n);
                return true;
            },
            [&](uint32_t addr, const void* src, size_t n) {
                if (addr > immediate_ram.size() || n > immediate_ram.size() - addr)
                    return false;
                std::memcpy(immediate_ram.data() + addr, src, n);
                return true;
            });

        // RAMHT handle 1 -> Kelvin object. Handles 2/3 are distinct texture DMA
        // bases so the first-base evidence probe can prove A/B selection.
        immediate_gpu.mmio_write(xlle::Nv2a::kBase + 0x700008, 4, 1);
        immediate_gpu.mmio_write(xlle::Nv2a::kBase + 0x70000C, 4, 0x80010020);
        immediate_gpu.mmio_write(xlle::Nv2a::kBase + 0x700010, 4, 2);
        immediate_gpu.mmio_write(xlle::Nv2a::kBase + 0x700014, 4, 0x80010030);
        immediate_gpu.mmio_write(xlle::Nv2a::kBase + 0x700018, 4, 3);
        immediate_gpu.mmio_write(xlle::Nv2a::kBase + 0x70001C, 4, 0x80010040);
        immediate_gpu.mmio_write(xlle::Nv2a::kBase + 0x700200, 4, 0x97);
        immediate_gpu.mmio_write(xlle::Nv2a::kBase + 0x700300, 4, 0x3D);
        immediate_gpu.mmio_write(xlle::Nv2a::kBase + 0x700304, 4, 0xFFFF);
        immediate_gpu.mmio_write(xlle::Nv2a::kBase + 0x700308, 4, 0);
        immediate_gpu.mmio_write(xlle::Nv2a::kBase + 0x700400, 4, 0x3D);
        immediate_gpu.mmio_write(xlle::Nv2a::kBase + 0x700404, 4, 0xFFFF);
        immediate_gpu.mmio_write(xlle::Nv2a::kBase + 0x700408, 4, 0x1000);
        immediate_gpu.mmio_write(xlle::Nv2a::kBase + 0x700180, 4, 0x3D);
        immediate_gpu.mmio_write(xlle::Nv2a::kBase + 0x700184, 4, 0xFFFF);
        immediate_gpu.mmio_write(xlle::Nv2a::kBase + 0x700188, 4, 0x2000);
        immediate_gpu.mmio_write(xlle::Nv2a::kBase + 0x002504, 4, 1);
        immediate_gpu.mmio_write(xlle::Nv2a::kBase + 0x003204, 4, 0x100);
        immediate_gpu.mmio_write(xlle::Nv2a::kBase + 0x00322C, 4, 0x18);
        immediate_gpu.mmio_write(xlle::Nv2a::kBase + 0x003244, 4, 0);
        immediate_gpu.mmio_write(xlle::Nv2a::kBase + 0x003220, 4, 1);
        immediate_gpu.mmio_write(xlle::Nv2a::kBase + 0x003250, 4, 1);
        immediate_gpu.mmio_write(xlle::Nv2a::kBase + 0x003200, 4, 1);

        uint32_t immediate_cursor = 0;
        constexpr uint32_t immediate_pb = 0x2000;
        auto emit_immediate = [&](uint32_t method, uint32_t parameter) {
            put32(immediate_ram, immediate_pb + immediate_cursor,
                  (1u << 18) | method);
            put32(immediate_ram, immediate_pb + immediate_cursor + 4, parameter);
            immediate_cursor += 8;
        };
        auto kick_immediate = [&]() {
            immediate_gpu.mmio_write(xlle::Nv2a::kBase + 0x800040, 4,
                                     immediate_cursor);
        };
        auto float_bits = [](float value) {
            uint32_t bits = 0;
            std::memcpy(&bits, &value, sizeof(bits));
            return bits;
        };
        for (uint8_t value = 0; value < 16; ++value) {
            immediate_ram[0xC000 + value] = static_cast<uint8_t>(0x20 + value);
            immediate_ram[0xD000 + value] = static_cast<uint8_t>(0x40 + value);
            immediate_ram[0xB000 + value] = static_cast<uint8_t>(0x80 + value);
        }
        put32(immediate_ram, 0xA040, 0x11223344);
        put32(immediate_ram, 0xA044, 0x55667788);
        put32(immediate_ram, 0xA048, 0x99AABBCC);
        put32(immediate_ram, 0xA04C, 0xDDEEFF00);

        emit_immediate(0x0000, 1);       // SET_OBJECT Kelvin
        emit_immediate(0x0188, 3);       // texture DMA B for the captured T0 format
        emit_immediate(0x1E9C, 0);       // MOV oPos, v0 (final)
        emit_immediate(0x0B00, 0x00000000);
        emit_immediate(0x0B04, 0x0020001B);
        emit_immediate(0x0B08, 0x0836106C);
        emit_immediate(0x0B0C, 0x2070F801);
        emit_immediate(0x1EA0, 0);
        emit_immediate(0x1E94, 2);       // programmable transform

        // Establish the witnessed inline-color profile.  A mode-8 batch must bypass
        // this otherwise recognizable raster state before profile validation or output.
        emit_immediate(0x0200, 0x00400000);
        emit_immediate(0x0204, 0x00400000);
        emit_immediate(0x0208, 0x00000128);
        emit_immediate(0x020C, 0x01000100);
        emit_immediate(0x0210, 0x00020000);
        emit_immediate(0x0214, 0x00060000);
        emit_immediate(0x02C0, 0x003F0000);
        emit_immediate(0x02E0, 0x003F0000);
        emit_immediate(0x0300, 1);
        emit_immediate(0x0304, 1);
        emit_immediate(0x0308, 1);
        emit_immediate(0x030C, 1);
        emit_immediate(0x0310, 0);
        emit_immediate(0x033C, 0x0206);
        emit_immediate(0x0340, 1);
        emit_immediate(0x0344, 0x0302);
        emit_immediate(0x0348, 1);
        emit_immediate(0x0350, 0x8006);
        emit_immediate(0x0354, 0x0207);
        emit_immediate(0x0358, 0x01010101);
        emit_immediate(0x035C, 0);
        emit_immediate(0x0394, 0);
        emit_immediate(0x0398, 0x4B7FFFFF);
        emit_immediate(0x1D78, 1);
        emit_immediate(0x1B00, 0x0000C000);  // bounded T0 evidence probe
        emit_immediate(0x1B04, 0x0888062A);
        emit_immediate(0x1B08, 0x00010303);
        emit_immediate(0x1B0C, 0x4003FFC0);
        emit_immediate(0x1B10, 0x14000000);
        emit_immediate(0x1B14, 0x02022000);
        emit_immediate(0x1B20, 0);
        emit_immediate(0x1B24, 0);
        emit_immediate(0x1B4C, 0);
        emit_immediate(0x1B8C, 0);
        emit_immediate(0x1BCC, 0);
        emit_immediate(0x1E70, 0x00000001);
        emit_immediate(0x1E78, 0x00210000);
        emit_immediate(0x1E60, 0x00000001);
        emit_immediate(0x0AC0, 0x01080000);
        emit_immediate(0x0260, 0x11180000);
        emit_immediate(0x1E40, 0x00000C00);
        emit_immediate(0x0AA0, 0x00000C00);
        emit_immediate(0x0A60, 0x80808080);
        emit_immediate(0x0A80, 0x80808080);
        emit_immediate(0x0288, 0x0000000C);
        emit_immediate(0x028C, 0x00001C80);
        emit_immediate(0x1E20, 0);
        emit_immediate(0x1E24, 0);

        const uint64_t immediate_raster_hash = immediate_gpu.raster_output_hash();
        const uint64_t immediate_color_clears = immediate_gpu.color_clears();
        const uint64_t immediate_raster_triangles = immediate_gpu.rasterized_triangles();
        const std::array<std::array<float, 4>, 4> quad{{
            {{269.5f, 157.5f, 0.0f, 0.0f}}, {{449.5f, 157.5f, 0.0f, 0.0f}},
            {{449.5f, 371.5f, 0.0f, 0.0f}}, {{269.5f, 371.5f, 0.0f, 0.0f}},
        }};
        const std::array<std::array<float, 2>, 4> quad_uv{{
            {{0.0f, 0.0f}}, {{1.0f, 0.0f}}, {{1.0f, 1.0f}}, {{0.0f, 1.0f}},
        }};
        auto emit_position = [&](const std::array<float, 4>& position) {
            emit_immediate(0x1518, float_bits(position[0]));
            emit_immediate(0x151C, float_bits(position[1]));
            emit_immediate(0x1520, float_bits(position[2]));
            emit_immediate(0x1524, float_bits(position[3]));
        };
        emit_immediate(0x17FC, 8);       // quads
        for (size_t vertex = 0; vertex < quad.size(); ++vertex) {
            const auto& position = quad[vertex];
            emit_immediate(0x18C8, float_bits(quad_uv[vertex][0]));  // attr9.x
            emit_immediate(0x18CC, float_bits(quad_uv[vertex][1]));  // attr9.y
            emit_immediate(0x194C, 0xFFFFFFFF);                     // attr3 diffuse
            emit_immediate(0x1518, float_bits(position[0]));
            emit_immediate(0x151C, float_bits(position[1]));
            emit_immediate(0x1520, float_bits(position[2]));
            emit_immediate(0x1524, float_bits(position[3]));  // position commit
        }
        emit_immediate(0x17FC, 0);
        kick_immediate();
        CHECK(immediate_fault.empty());
        CHECK(immediate_gpu.immediate_methods() == 28);
        CHECK(immediate_gpu.immediate_vertices() == 4);
        CHECK(immediate_gpu.immediate_batches() == 1);
        CHECK(immediate_gpu.vertex_batches() == 1);
        CHECK(immediate_gpu.submitted_vertices() == 4);
        CHECK(immediate_gpu.vertex_attribute_fetches() == 0);
        CHECK(immediate_gpu.assembled_primitives() == 1);
        CHECK(immediate_gpu.assembled_triangles() == 2);
        CHECK(immediate_gpu.primitive_mode_count(8) == 1);
        CHECK(immediate_gpu.color_clears() == immediate_color_clears);
        CHECK(immediate_gpu.rasterized_triangles() == immediate_raster_triangles);
        CHECK(immediate_gpu.raster_output_hash() == immediate_raster_hash);
        std::vector<xlle::Nv2a::RasterStateSample> immediate_samples;
        immediate_gpu.raster_state_samples(immediate_samples);
        const xlle::Nv2a::RasterStateSample* immediate_sample = nullptr;
        for (const auto& sample : immediate_samples) {
            if (sample.immediate_quad_batches) {
                immediate_sample = &sample;
                break;
            }
        }
        CHECK(immediate_sample != nullptr);
        if (immediate_sample) {
            CHECK(immediate_sample->immediate_quad_batches == 1);
            CHECK(immediate_sample->immediate_quad_min_vertices == 4);
            CHECK(immediate_sample->immediate_quad_max_vertices == 4);
            CHECK(immediate_sample->immediate_quad_nonmultiple_of_four == 0);
            CHECK(immediate_sample->immediate_quad_first_captured);
            for (size_t vertex = 0; vertex < quad.size(); ++vertex) {
                CHECK(immediate_sample->immediate_quad_decoded_input0[vertex] ==
                      quad[vertex]);
                CHECK(immediate_sample->immediate_quad_decoded_input3[vertex] ==
                      (std::array<float, 4>{{1.0f, 1.0f, 1.0f, 1.0f}}));
                CHECK(immediate_sample->immediate_quad_decoded_input9[vertex] ==
                      (std::array<float, 4>{{quad_uv[vertex][0], quad_uv[vertex][1],
                                              0.0f, 1.0f}}));
                CHECK(immediate_sample->immediate_quad_opos[vertex] == quad[vertex]);
                CHECK(immediate_sample->immediate_quad_od0[vertex] ==
                      (std::array<float, 4>{}));
                CHECK(immediate_sample->immediate_quad_ot0[vertex] ==
                      (std::array<float, 4>{}));
                CHECK(immediate_sample->immediate_quad_raster_opos[vertex][0] ==
                      quad[vertex][0]);
                CHECK(immediate_sample->immediate_quad_raster_opos[vertex][1] ==
                      quad[vertex][1]);
                CHECK(immediate_sample->immediate_quad_raster_opos[vertex][2] ==
                      quad[vertex][2]);
                // The VSH sees the exact submitted W=0; the local raster safety
                // clamp is recorded separately and must not overwrite that evidence.
                CHECK(immediate_sample->immediate_quad_decoded_input0[vertex][3] == 0.0f);
                CHECK(immediate_sample->immediate_quad_opos[vertex][3] == 0.0f);
                CHECK(immediate_sample->immediate_quad_raster_opos[vertex][3] > 0.0f);
            }
            CHECK(immediate_sample->immediate_quad_program_hash != 0);
            CHECK(immediate_sample->immediate_quad_constants_hash != 0);
            CHECK(immediate_sample->immediate_quad_transform_variant_count == 1);
            CHECK(immediate_sample->immediate_quad_first_transform_batches == 1);
            CHECK(immediate_sample->immediate_quad_later_transform_batches == 0);
            CHECK(immediate_sample->immediate_quad_t0_dma_resolved);
            CHECK(immediate_sample->immediate_quad_t0_probe_read);
            CHECK(immediate_sample->immediate_quad_t0_probe_size == 16);
            CHECK(immediate_sample->immediate_quad_t0_address == 0xD000);
            CHECK(immediate_sample->immediate_quad_t0_probe[0] == 0x40);
            CHECK(immediate_sample->immediate_quad_t0_probe[15] == 0x4F);
            CHECK(!immediate_sample->immediate_quad_palette_relevant);
        }

        // The same state tracks variable mode-8 batch sizes without rasterizing.
        emit_immediate(0x17FC, 8);
        for (size_t vertex = 0; vertex < 8; ++vertex)
            emit_position(quad[vertex % quad.size()]);
        emit_immediate(0x17FC, 0);
        kick_immediate();
        emit_immediate(0x17FC, 8);
        for (size_t vertex = 0; vertex < 3; ++vertex)
            emit_position(quad[vertex]);
        emit_immediate(0x17FC, 0);
        kick_immediate();
        CHECK(immediate_fault.empty());
        immediate_gpu.raster_state_samples(immediate_samples);
        immediate_sample = nullptr;
        for (const auto& sample : immediate_samples) {
            if (sample.immediate_quad_batches &&
                !sample.immediate_quad_palette_relevant) {
                immediate_sample = &sample;
                break;
            }
        }
        CHECK(immediate_sample != nullptr);
        if (immediate_sample) {
            CHECK(immediate_sample->immediate_quad_batches == 3);
            CHECK(immediate_sample->immediate_quad_min_vertices == 3);
            CHECK(immediate_sample->immediate_quad_max_vertices == 8);
            CHECK(immediate_sample->immediate_quad_nonmultiple_of_four == 1);
            CHECK(immediate_sample->immediate_quad_first_transform_batches == 3);
            CHECK(immediate_sample->immediate_quad_later_transform_batches == 0);
        }
        CHECK(immediate_gpu.color_clears() == immediate_color_clears);
        CHECK(immediate_gpu.rasterized_triangles() == immediate_raster_triangles);
        CHECK(immediate_gpu.raster_output_hash() == immediate_raster_hash);

        // Transform identity is independent of the raster-state key. Change an
        // unused constant vector, submit the same mode-8 state, and retain both
        // transform identities on that one raster-state sample.
        emit_immediate(0x1EA4, 1);
        emit_immediate(0x0B80, float_bits(42.0f));
        emit_immediate(0x0B84, float_bits(0.0f));
        emit_immediate(0x0B88, float_bits(0.0f));
        emit_immediate(0x0B8C, float_bits(1.0f));
        emit_immediate(0x17FC, 8);
        for (const auto& position : quad) emit_position(position);
        emit_immediate(0x17FC, 0);
        kick_immediate();
        CHECK(immediate_fault.empty());
        immediate_gpu.raster_state_samples(immediate_samples);
        immediate_sample = nullptr;
        for (const auto& sample : immediate_samples) {
            if (sample.immediate_quad_batches &&
                !sample.immediate_quad_palette_relevant) {
                immediate_sample = &sample;
                break;
            }
        }
        CHECK(immediate_sample != nullptr);
        if (immediate_sample) {
            CHECK(immediate_sample->immediate_quad_batches == 4);
            CHECK(immediate_sample->immediate_quad_transform_variant_count == 2);
            CHECK(immediate_sample->immediate_quad_first_transform_batches == 3);
            CHECK(immediate_sample->immediate_quad_later_transform_batches == 1);
            CHECK(immediate_sample->immediate_quad_transform_variant_overflow_batches == 0);
        }

        // I8 + palette is a separate passive probe: format bit clear selects DMA A,
        // while the palette offset remains 64-byte aligned and also selects DMA A.
        emit_immediate(0x0184, 2);
        emit_immediate(0x1B00, 0x0000B000);
        emit_immediate(0x1B04, 0x05710B29);
        emit_immediate(0x1B20, 0x0000A040);
        emit_immediate(0x1B0C, 0x4003FFC4);
        emit_immediate(0x17FC, 8);
        for (const auto& position : quad) emit_position(position);
        emit_immediate(0x17FC, 0);
        kick_immediate();
        CHECK(immediate_fault.empty());
        immediate_gpu.raster_state_samples(immediate_samples);
        const xlle::Nv2a::RasterStateSample* palette_sample = nullptr;
        for (const auto& sample : immediate_samples) {
            if (sample.immediate_quad_palette_relevant) {
                palette_sample = &sample;
                break;
            }
        }
        CHECK(palette_sample != nullptr);
        if (palette_sample) {
            CHECK(palette_sample->immediate_quad_t0_dma_resolved);
            CHECK(palette_sample->immediate_quad_t0_probe_read);
            CHECK(palette_sample->immediate_quad_t0_address == 0xB000);
            CHECK(palette_sample->immediate_quad_t0_probe[0] == 0x80);
            CHECK(palette_sample->immediate_quad_palette_relevant);
            CHECK(palette_sample->immediate_quad_palette_dma_resolved);
            CHECK(palette_sample->immediate_quad_palette_probe_read);
            CHECK(palette_sample->immediate_quad_palette_address == 0xA040);
            CHECK(palette_sample->immediate_quad_palette_probe_entries == 4);
            CHECK(palette_sample->immediate_quad_palette_probe[0] == 0x11223344u);
            CHECK(palette_sample->immediate_quad_palette_probe[1] == 0x55667788u);
            CHECK(palette_sample->immediate_quad_palette_probe[2] == 0x99AABBCCu);
            CHECK(palette_sample->immediate_quad_palette_probe[3] == 0xDDEEFF00u);
        }
        CHECK(immediate_gpu.color_clears() == immediate_color_clears);
        CHECK(immediate_gpu.rasterized_triangles() == immediate_raster_triangles);
        CHECK(immediate_gpu.raster_output_hash() == immediate_raster_hash);

        const uint64_t immediate_methods_before_generic =
            immediate_gpu.immediate_methods();

        // Generic SET_VERTEX_DATA streams persist their attribute values like every
        // other slot, but a write that completes attribute slot 0 (position) now
        // commits a vertex exactly like the legacy SET_VERTEX4F family's W write
        // always has -- matching real hardware, where position completion always
        // latches a vertex regardless of which method family produced it.
        uint64_t generic_verts0 = immediate_gpu.immediate_vertices();
        uint64_t generic_batches0 = immediate_gpu.immediate_batches();
        uint64_t generic_vbatches0 = immediate_gpu.vertex_batches();
        emit_immediate(0x17FC, 1);
        emit_immediate(0x1880, float_bits(1.0f));  // slot0.x: no commit
        emit_immediate(0x1884, float_bits(2.0f));  // slot0.y: commits (1,2,0,1)
        emit_immediate(0x1940, 0xFF804000u);       // slot0 (UB): commits again
        emit_immediate(0x17FC, 0);
        kick_immediate();
        CHECK(immediate_fault.empty());
        CHECK(immediate_gpu.immediate_methods() == immediate_methods_before_generic + 3);
        CHECK(immediate_gpu.immediate_vertices() == generic_verts0 + 2);
        CHECK(immediate_gpu.immediate_batches() == generic_batches0 + 1);
        CHECK(immediate_gpu.vertex_batches() == generic_vbatches0 + 1);

        // A slot-0 completion commits with whatever the attribute currently holds; a
        // following legacy position-W write commits again with the same value (only W
        // changes, and W was already 1 from the SET_VERTEX_DATA2F_M write above).
        generic_verts0 = immediate_gpu.immediate_vertices();
        generic_batches0 = immediate_gpu.immediate_batches();
        emit_immediate(0x17FC, 1);
        emit_immediate(0x1880, float_bits(5.0f));
        emit_immediate(0x1884, float_bits(6.0f));  // commits (5,6,0,1)
        emit_immediate(0x1524, float_bits(1.0f));  // commits again, unchanged
        emit_immediate(0x17FC, 0);
        kick_immediate();
        CHECK(immediate_fault.empty());
        CHECK(immediate_gpu.immediate_vertices() == generic_verts0 + 2);
        CHECK(immediate_gpu.immediate_batches() == generic_batches0 + 1);
        CHECK(immediate_gpu.last_transformed_position() ==
              (std::array<float, 4>{{5.0f, 6.0f, 0.0f, 1.0f}}));

        generic_verts0 = immediate_gpu.immediate_vertices();
        generic_batches0 = immediate_gpu.immediate_batches();
        emit_immediate(0x17FC, 1);
        emit_immediate(0x1940, 0xFF804000u);       // slot0 (UB): commits
        emit_immediate(0x1524, float_bits(1.0f));  // commits again, unchanged
        emit_immediate(0x17FC, 0);
        kick_immediate();
        CHECK(immediate_fault.empty());
        CHECK(immediate_gpu.immediate_vertices() == generic_verts0 + 2);
        CHECK(immediate_gpu.immediate_batches() == generic_batches0 + 1);
        const auto converted_position = immediate_gpu.last_transformed_position();
        CHECK(converted_position[0] == 0.0f);
        CHECK(converted_position[1] == 0.25f);  // screen coordinate is truncated to 1/16
        CHECK(std::abs(converted_position[2] - 128.0f / 255.0f) < 0.000001f);
        CHECK(converted_position[3] == 1.0f);

        // Position completion is illegal without BEGIN, and immediate data cannot mix with
        // indexed or NV097_INLINE_ARRAY sources in a single batch.
        emit_immediate(0x1518, float_bits(1.0f));
        emit_immediate(0x151C, float_bits(2.0f));
        emit_immediate(0x1520, float_bits(3.0f));
        emit_immediate(0x1524, float_bits(1.0f));
        kick_immediate();
        CHECK(immediate_fault == "NV2A immediate vertex submitted outside BEGIN/END");
        immediate_fault.clear();

        emit_immediate(0x17FC, 1);
        emit_immediate(0x1518, float_bits(1.0f));
        emit_immediate(0x151C, float_bits(2.0f));
        emit_immediate(0x1520, float_bits(3.0f));
        emit_immediate(0x1524, float_bits(1.0f));
        emit_immediate(0x1808, 0);
        emit_immediate(0x17FC, 0);
        kick_immediate();
        CHECK(immediate_fault ==
              "NV2A batch mixes indexed, inline-array, or immediate vertices");
        immediate_fault.clear();

        emit_immediate(0x17FC, 1);
        emit_immediate(0x1518, float_bits(1.0f));
        emit_immediate(0x151C, float_bits(2.0f));
        emit_immediate(0x1520, float_bits(3.0f));
        emit_immediate(0x1524, float_bits(1.0f));
        emit_immediate(0x1760, 0x00001042);  // float4 array attr0 for INLINE_ARRAY decode
        emit_immediate(0x1818, float_bits(4.0f));
        emit_immediate(0x1818, float_bits(5.0f));
        emit_immediate(0x1818, float_bits(6.0f));
        emit_immediate(0x1818, float_bits(1.0f));
        emit_immediate(0x17FC, 0);
        kick_immediate();
        CHECK(immediate_fault ==
              "NV2A batch mixes indexed, inline-array, or immediate vertices");
    }

    // The first pixel-producing immediate profile is the exact dashboard RS1/RS2
    // A4R4G4B4 family. Exercise the retained state independently from the passive
    // front-end fixture above, including DMA wrapping, quad winding, alpha, and a
    // nearby recorder-only state.
    {
        std::string quad_fault;
        xlle::Nv2a quad_gpu([&](const std::string& why) { quad_fault = why; });
        std::vector<uint8_t> quad_ram(0x900000, 0);
        quad_gpu.set_mem_access(
            [&](uint32_t addr, void* dst, size_t n) {
                if (addr > quad_ram.size() || n > quad_ram.size() - addr) {
                    return false;
                }
                std::memcpy(dst, quad_ram.data() + addr, n);
                return true;
            },
            [&](uint32_t addr, const void* src, size_t n) {
                if (addr > quad_ram.size() || n > quad_ram.size() - addr)
                    return false;
                std::memcpy(quad_ram.data() + addr, src, n);
                return true;
            });

        // RAMHT handle 1 -> Kelvin; handle 2 -> a 128 MiB DMA window. Its frame
        // maps the retained high guest offsets into this compact test RAM without
        // changing any command-stream-visible offset.
        quad_gpu.mmio_write(xlle::Nv2a::kBase + 0x700008, 4, 1);
        quad_gpu.mmio_write(xlle::Nv2a::kBase + 0x70000C, 4, 0x80010020);
        quad_gpu.mmio_write(xlle::Nv2a::kBase + 0x700010, 4, 2);
        quad_gpu.mmio_write(xlle::Nv2a::kBase + 0x700014, 4, 0x80010030);
        quad_gpu.mmio_write(xlle::Nv2a::kBase + 0x700200, 4, 0x97);
        quad_gpu.mmio_write(xlle::Nv2a::kBase + 0x700300, 4, 0x3D);
        quad_gpu.mmio_write(xlle::Nv2a::kBase + 0x700304, 4, 0x07FFFFFF);
        quad_gpu.mmio_write(xlle::Nv2a::kBase + 0x700308, 4, 0x04414000);
        quad_gpu.mmio_write(xlle::Nv2a::kBase + 0x700180, 4, 0x3D);
        quad_gpu.mmio_write(xlle::Nv2a::kBase + 0x700184, 4, 0xFFFF);
        quad_gpu.mmio_write(xlle::Nv2a::kBase + 0x700188, 4, 0x2000);
        // I8's captured guest offsets are lower than the A4 family. Bind a
        // separate texture DMA-A frame that wraps both adjacent captured ranges
        // into this compact RAM while retaining DMA 2 for color/A4 coverage.
        quad_gpu.mmio_write(xlle::Nv2a::kBase + 0x700018, 4, 3);
        quad_gpu.mmio_write(xlle::Nv2a::kBase + 0x70001C, 4, 0x80010040);
        quad_gpu.mmio_write(xlle::Nv2a::kBase + 0x700400, 4, 0x3D);
        quad_gpu.mmio_write(xlle::Nv2a::kBase + 0x700404, 4, 0x07FFFFFF);
        quad_gpu.mmio_write(xlle::Nv2a::kBase + 0x700408, 4, 0x05313000);
        quad_gpu.mmio_write(xlle::Nv2a::kBase + 0x002504, 4, 1);
        quad_gpu.mmio_write(xlle::Nv2a::kBase + 0x003204, 4, 0x100);
        quad_gpu.mmio_write(xlle::Nv2a::kBase + 0x00322C, 4, 0x18);
        quad_gpu.mmio_write(xlle::Nv2a::kBase + 0x003244, 4, 0);
        quad_gpu.mmio_write(xlle::Nv2a::kBase + 0x003220, 4, 1);
        quad_gpu.mmio_write(xlle::Nv2a::kBase + 0x003250, 4, 1);
        quad_gpu.mmio_write(xlle::Nv2a::kBase + 0x003200, 4, 1);

        constexpr uint32_t quad_pb = 0x2000;
        uint32_t quad_cursor = 0;
        auto emit_quad = [&](uint32_t method, uint32_t parameter) {
            put32(quad_ram, quad_pb + quad_cursor, (1u << 18) | method);
            put32(quad_ram, quad_pb + quad_cursor + 4, parameter);
            quad_cursor += 8;
        };
        auto kick_quad = [&]() {
            quad_gpu.mmio_write(xlle::Nv2a::kBase + 0x800040, 4, quad_cursor);
        };
        auto float_bits = [](float value) {
            uint32_t bits = 0;
            std::memcpy(&bits, &value, sizeof(bits));
            return bits;
        };

        constexpr uint32_t texture_address = 0x00020000;
        constexpr uint32_t rs2_color_address = 0x000A0000;
        constexpr uint32_t color_address = 0x00208000;
        auto morton_index = [](uint32_t x, uint32_t y) {
            uint32_t result = 0;
            for (uint32_t bit = 0; bit < 8; ++bit) {
                result |= ((x >> bit) & 1u) << (bit * 2);
                result |= ((y >> bit) & 1u) << (bit * 2 + 1);
            }
            return result;
        };
        auto fill_quad_texture = [&](uint16_t alpha) {
            for (uint32_t y = 0; y < 256; ++y) {
                for (uint32_t x = 0; x < 256; ++x) {
                    uint16_t rgb = 0;
                    if (x < 128 && y < 128)
                        rgb = 0x0F00;  // red
                    else if (x >= 128 && y < 128)
                        rgb = 0x00F0;  // green
                    else if (x < 128)
                        rgb = 0x000F;  // blue
                    else
                        rgb = 0x0FFF;  // white
                    const uint16_t pixel =
                        static_cast<uint16_t>((alpha << 12) | rgb);
                    const size_t offset =
                        static_cast<size_t>(morton_index(x, y)) *
                        sizeof(pixel);
                    std::memcpy(quad_ram.data() + texture_address + offset,
                                &pixel, sizeof(pixel));
                }
            }
        };
        fill_quad_texture(0xF);

        emit_quad(0x0000, 1);
        emit_quad(0x0184, 2);
        emit_quad(0x0188, 2);
        emit_quad(0x0194, 2);
        emit_quad(0x1E9C, 0);
        emit_quad(0x0B00, 0x00000000);
        emit_quad(0x0B04, 0x0020061B);
        emit_quad(0x0B08, 0x0836106C);
        emit_quad(0x0B0C, 0x2070F818);  // MOV oD0, v3
        emit_quad(0x0B00, 0x00000000);
        emit_quad(0x0B04, 0x002000BB);
        emit_quad(0x0B08, 0x0836106C);
        emit_quad(0x0B0C, 0x2070F848);  // MOV oT0, v0.zwzw
        emit_quad(0x0B00, 0x00000000);
        emit_quad(0x0B04, 0x0020001B);
        emit_quad(0x0B08, 0x0836106C);
        emit_quad(0x0B0C, 0x2070C801);  // MOV oPos.xy, v0 (final)
        emit_quad(0x1EA0, 0);
        emit_quad(0x1E94, 2);

        emit_quad(0x0200, 0x00B4010E);
        emit_quad(0x0204, 0x00D7009E);
        emit_quad(0x0208, 0x00000128);
        emit_quad(0x020C, 0x00400C00);
        emit_quad(0x0210, 0x03DF4000);
        emit_quad(0x0214, 0);
        emit_quad(0x0290, 0x00100001);
        emit_quad(0x02C0, 0x02D00000);
        emit_quad(0x02E0, 0x01E00000);
        emit_quad(0x0300, 1);
        emit_quad(0x0304, 1);
        emit_quad(0x0308, 1);
        emit_quad(0x030C, 0);
        emit_quad(0x033C, 0x0206);
        emit_quad(0x0340, 8);
        emit_quad(0x0344, 0x0302);
        emit_quad(0x0348, 0x0303);
        emit_quad(0x0350, 0x8006);
        emit_quad(0x0354, 0x0203);
        emit_quad(0x0358, 0x01010101);
        emit_quad(0x035C, 1);
        emit_quad(0x0360, 0xFFFFFFFF);
        emit_quad(0x0364, 0x0207);
        emit_quad(0x0368, 0);
        emit_quad(0x036C, 0xFFFFFFFF);
        emit_quad(0x0370, 0x1E00);
        emit_quad(0x0374, 0x1E00);
        emit_quad(0x0378, 0x1E00);
        emit_quad(0x037C, 0x1D01);
        emit_quad(0x0384, 0x80000000);
        emit_quad(0x0388, 0x80000000);
        emit_quad(0x038C, 0x1B02);
        emit_quad(0x0390, 0x1B02);
        emit_quad(0x0394, 0);
        emit_quad(0x0398, 0);
        emit_quad(0x039C, 0x0405);
        emit_quad(0x03A0, 0x0900);
        emit_quad(0x1D78, 1);

        emit_quad(0x1E70, 1);
        emit_quad(0x1E78, 0);
        emit_quad(0x1E60, 1);
        emit_quad(0x0260, 0x18140000);
        emit_quad(0x0AC0, 0x08040000);
        emit_quad(0x0AA0, 0x000000C0);
        emit_quad(0x1E40, 0x000000C0);
        emit_quad(0x0A60, 0);
        emit_quad(0x0A80, 0);
        emit_quad(0x0288, 0x010C0002);
        emit_quad(0x028C, 0x00001C00);
        emit_quad(0x1E20, 0x00DADADA);
        emit_quad(0x1E24, 0x00101010);

        emit_quad(0x1B00, 0x03C0C000);
        emit_quad(0x1B04, 0x08810429);
        emit_quad(0x1B08, 0x00010303);
        emit_quad(0x1B0C, 0x4003FFC0);
        emit_quad(0x1B10, 0x01C00000);
        emit_quad(0x1B14, 0x02022000);
        emit_quad(0x1B1C, 0x00640011);
        emit_quad(0x1B20, 0x02DED080);
        emit_quad(0x1B24, 0);

        const std::array<std::array<float, 4>, 4> quad{{
            {{279.0f, 184.0f, 0.25f, 0.75f}},
            {{279.0f, 160.0f, 0.25f, 0.25f}},
            {{295.0f, 160.0f, 0.75f, 0.25f}},
            {{295.0f, 184.0f, 0.75f, 0.75f}},
        }};
        auto submit_seven_quads = [&]() {
            emit_quad(0x17FC, 8);
            emit_quad(0x194C, 0xFFFFFFFF);  // persistent v3 / oD0 white
            for (size_t repeat = 0; repeat < 7; ++repeat) {
                for (const auto& position : quad) {
                    emit_quad(0x1518, float_bits(position[0]));
                    emit_quad(0x151C, float_bits(position[1]));
                    emit_quad(0x1520, float_bits(position[2]));
                    emit_quad(0x1524, float_bits(position[3]));
                }
            }
            emit_quad(0x17FC, 0);
            kick_quad();
        };

        submit_seven_quads();
        CHECK(quad_fault.empty());
        CHECK(quad_gpu.immediate_quad_raster_batches() == 1);
        CHECK(quad_gpu.immediate_quad_raster_triangles() == 14);
        CHECK(quad_gpu.immediate_quad_raster_fragments() == 7 * 16 * 24);
        CHECK(quad_gpu.immediate_quad_raster_alpha_passes() == 7 * 16 * 24);
        CHECK(quad_gpu.immediate_quad_raster_writes() == 7 * 16 * 24);
        CHECK(quad_gpu.immediate_quad_texture_samples() == 7 * 16 * 24);
        CHECK(quad_gpu.rasterized_triangles() == 0);
        auto surface_pixel = [&](uint32_t base, uint32_t x, uint32_t y) {
            uint32_t pixel = 0;
            std::memcpy(&pixel,
                        quad_ram.data() + base + y * 0x0C00 + x * 4,
                        sizeof(pixel));
            return pixel;
        };
        CHECK(surface_pixel(color_address, 282, 163) == 0xFFEA1010);
        CHECK(surface_pixel(color_address, 292, 163) == 0xFF10EA10);
        CHECK(surface_pixel(color_address, 282, 180) == 0xFF1010EA);
        CHECK(surface_pixel(color_address, 292, 180) == 0xFFEAEAEA);
        CHECK(quad_gpu.last_immediate_quad_raster_address() ==
              color_address + 183 * 0x0C00 + 294 * 4);
        CHECK(quad_gpu.immediate_quad_raster_output_hash() !=
              1469598103934665603ull);
        std::vector<xlle::Nv2a::RasterStateSample> quad_samples;
        quad_gpu.raster_state_samples(quad_samples);
        CHECK(std::any_of(
            quad_samples.begin(), quad_samples.end(),
            [](const xlle::Nv2a::RasterStateSample& sample) {
                return sample.immediate_quad_batches == 1 &&
                       sample.executed_batches == 1;
            }));

        // A one-word state change remains recorder-only and must not touch the
        // dedicated counters or target.
        const size_t color_bytes = 0x0C00u * 480u;
        const std::vector<uint8_t> before_rejected(
            quad_ram.begin() + color_address,
            quad_ram.begin() + color_address + color_bytes);
        emit_quad(0x0340, 9);
        submit_seven_quads();
        CHECK(quad_fault.empty());
        CHECK(quad_gpu.immediate_quad_raster_batches() == 1);
        CHECK(quad_gpu.immediate_quad_raster_triangles() == 14);
        CHECK(quad_gpu.immediate_quad_raster_writes() == 7 * 16 * 24);
        CHECK(std::equal(before_rejected.begin(), before_rejected.end(),
                         quad_ram.begin() + color_address));

        // Restore RS1, make the same texture fully transparent, and prove that
        // fragments/samples execute while GEQUAL rejects every color write.
        emit_quad(0x0340, 8);
        fill_quad_texture(0);
        const std::vector<uint8_t> before_transparent(
            quad_ram.begin() + color_address,
            quad_ram.begin() + color_address + color_bytes);
        submit_seven_quads();
        CHECK(quad_fault.empty());
        CHECK(quad_gpu.immediate_quad_raster_batches() == 2);
        CHECK(quad_gpu.immediate_quad_raster_triangles() == 28);
        CHECK(quad_gpu.immediate_quad_raster_fragments() == 2 * 7 * 16 * 24);
        CHECK(quad_gpu.immediate_quad_texture_samples() == 2 * 7 * 16 * 24);
        CHECK(quad_gpu.immediate_quad_raster_alpha_passes() == 7 * 16 * 24);
        CHECK(quad_gpu.immediate_quad_raster_writes() == 7 * 16 * 24);
        CHECK(std::equal(before_transparent.begin(), before_transparent.end(),
                         quad_ram.begin() + color_address));

        // RS2 differs only by the alternating color target. Restore opaque T0,
        // switch the exact retained offset, and prove writes land exclusively in
        // the second surface.
        fill_quad_texture(0xF);
        const std::vector<uint8_t> rs1_before_rs2(
            quad_ram.begin() + color_address,
            quad_ram.begin() + color_address + color_bytes);
        emit_quad(0x0210, 0x03C8C000);
        submit_seven_quads();
        CHECK(quad_fault.empty());
        CHECK(quad_gpu.immediate_quad_raster_batches() == 3);
        CHECK(quad_gpu.immediate_quad_raster_triangles() == 42);
        CHECK(quad_gpu.immediate_quad_raster_fragments() == 3 * 7 * 16 * 24);
        CHECK(quad_gpu.immediate_quad_texture_samples() == 3 * 7 * 16 * 24);
        CHECK(quad_gpu.immediate_quad_raster_alpha_passes() == 2 * 7 * 16 * 24);
        CHECK(quad_gpu.immediate_quad_raster_writes() == 2 * 7 * 16 * 24);
        CHECK(surface_pixel(rs2_color_address, 282, 163) == 0xFFEA1010);
        CHECK(surface_pixel(rs2_color_address, 292, 180) == 0xFFEAEAEA);
        CHECK(std::equal(rs1_before_rs2.begin(), rs1_before_rs2.end(),
                         quad_ram.begin() + color_address));

        // A half-alpha source over a nonzero destination exercises the retained
        // SRC_ALPHA / ONE_MINUS_SRC_ALPHA blend instead of the opaque idempotent
        // case above. Seven overlapping quads deliberately expose cache coherence
        // between writes inside one batch.
        fill_quad_texture(8);
        for (uint32_t y = 160; y < 184; ++y) {
            for (uint32_t x = 279; x < 295; ++x) {
                put32(quad_ram, rs2_color_address + y * 0x0C00 + x * 4,
                      0xFF204060);
            }
        }
        auto blend_expected = []() {
            std::array<uint32_t, 4> destination{{32, 64, 96, 255}};
            const std::array<float, 4> source{{
                234.0f / 255.0f, 16.0f / 255.0f,
                16.0f / 255.0f, 8.0f / 15.0f,
            }};
            auto pack = [](float value) {
                return static_cast<uint32_t>(std::floor(
                    std::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f));
            };
            for (size_t draw = 0; draw < 7; ++draw) {
                for (size_t component = 0; component < 4; ++component) {
                    destination[component] = pack(
                        source[component] * source[3] +
                        (static_cast<float>(destination[component]) / 255.0f) *
                            (1.0f - source[3]));
                }
            }
            return (destination[3] << 24) | (destination[0] << 16) |
                   (destination[1] << 8) | destination[2];
        };
        submit_seven_quads();
        CHECK(quad_fault.empty());
        CHECK(quad_gpu.immediate_quad_raster_batches() == 4);
        CHECK(quad_gpu.immediate_quad_raster_triangles() == 56);
        CHECK(quad_gpu.immediate_quad_raster_fragments() == 4 * 7 * 16 * 24);
        CHECK(quad_gpu.immediate_quad_texture_samples() == 4 * 7 * 16 * 24);
        CHECK(quad_gpu.immediate_quad_raster_alpha_passes() == 3 * 7 * 16 * 24);
        CHECK(quad_gpu.immediate_quad_raster_writes() == 3 * 7 * 16 * 24);
        CHECK(surface_pixel(rs2_color_address, 282, 163) == blend_expected());
        CHECK(quad_gpu.immediate_quad_rs12_candidates() == 4);
        CHECK(quad_gpu.last_immediate_quad_rs12_reject_mask() == 0);
        CHECK(quad_gpu.last_immediate_quad_rs12_raster_reject_mask() == 0);

        // RS0/RS3 are a separate four-vertex I8 + A8R8G8B8-palette profile.
        // Start with a zero-alpha entry to prove TEXCTL0 alpha-kill runs after
        // palette lookup/filtering and before the captured blend.
        emit_quad(0x0300, 0);
        emit_quad(0x033C, 0x0207);
        emit_quad(0x0340, 0);
        emit_quad(0x1E78, 0x00210000);
        emit_quad(0x0AA0, 0x00000C00);
        emit_quad(0x1E40, 0x00000C00);
        emit_quad(0x0A60, 0xFFFFFFFF);
        emit_quad(0x0A80, 0xFFFFFFFF);
        emit_quad(0x0288, 0x0000000C);
        emit_quad(0x028C, 0x00001C80);
        emit_quad(0x0184, 3);
        emit_quad(0x1B00, 0x02DED480);
        emit_quad(0x1B04, 0x05710B29);
        emit_quad(0x1B0C, 0x4003FFC4);
        emit_quad(0x1B20, 0x02DED080);
        const std::array<std::array<float, 4>, 4> i8_quad{{
            {{270.0f, 158.0f, 0.0f, 0.0f}},
            {{450.0f, 158.0f, 1.0f, 0.0f}},
            {{450.0f, 187.0f, 1.0f, 1.0f}},
            {{270.0f, 187.0f, 0.0f, 1.0f}},
        }};
        auto submit_i8_quad = [&](const auto& vertices) {
            emit_quad(0x17FC, 8);
            emit_quad(0x194C, 0xFFFFFFFF);
            for (const auto& position : vertices) {
                emit_quad(0x1518, float_bits(position[0]));
                emit_quad(0x151C, float_bits(position[1]));
                emit_quad(0x1520, float_bits(position[2]));
                emit_quad(0x1524, float_bits(position[3]));
            }
            emit_quad(0x17FC, 0);
            kick_quad();
        };
        const std::vector<uint8_t> i8_zero_alpha_before(
            quad_ram.begin() + rs2_color_address,
            quad_ram.begin() + rs2_color_address + color_bytes);
        submit_i8_quad(i8_quad);
        CHECK(quad_fault.empty());
        CHECK(quad_gpu.immediate_quad_i8_raster_batches() == 1);
        CHECK(quad_gpu.immediate_quad_i8_raster_alpha_passes() == 0);
        CHECK(quad_gpu.immediate_quad_i8_raster_writes() == 0);
        CHECK(std::equal(i8_zero_alpha_before.begin(), i8_zero_alpha_before.end(),
                         quad_ram.begin() + rs2_color_address));
        std::vector<xlle::Nv2a::RasterStateSample> i8_samples;
        quad_gpu.raster_state_samples(i8_samples);
        const xlle::Nv2a::RasterStateSample* i8_sample = nullptr;
        for (const auto& sample : i8_samples) {
            if (sample.immediate_quad_palette_relevant) {
                i8_sample = &sample;
                break;
            }
        }
        CHECK(i8_sample != nullptr);
        if (i8_sample) {
            CHECK(i8_sample->immediate_quad_t0_dma_resolved);
            CHECK(i8_sample->immediate_quad_palette_dma_resolved);
            CHECK(i8_sample->immediate_quad_t0_address == 0x00100480);
            CHECK(i8_sample->immediate_quad_palette_address == 0x00100080);
        }
        const uint32_t i8_texture_address = i8_sample
            ? i8_sample->immediate_quad_t0_address : 0;
        const uint32_t i8_palette_address = i8_sample
            ? i8_sample->immediate_quad_palette_address : 0;
        CHECK(i8_texture_address + 128 * 32 <= quad_ram.size());
        CHECK(i8_palette_address + 256 * sizeof(uint32_t) <= quad_ram.size());
        auto i8_morton = [](uint32_t x, uint32_t y) {
            uint32_t result = 0;
            uint32_t output_bit = 0;
            for (uint32_t input_bit = 1; input_bit < 128 || input_bit < 32;
                 input_bit <<= 1) {
                if (input_bit < 128) {
                    result |= ((x & input_bit) != 0) << output_bit;
                    ++output_bit;
                }
                if (input_bit < 32) {
                    result |= ((y & input_bit) != 0) << output_bit;
                    ++output_bit;
                }
            }
            return result;
        };
        std::fill(quad_ram.begin() + i8_texture_address,
                  quad_ram.begin() + i8_texture_address + 128 * 32, 1);
        for (uint32_t y = 0; y < 32; ++y)
            quad_ram[i8_texture_address + i8_morton(2, y)] = 2;
        for (uint32_t index = 0; index < 256; ++index)
            put32(quad_ram, i8_palette_address + index * sizeof(uint32_t), 0);
        put32(quad_ram, i8_palette_address + 1 * sizeof(uint32_t), 0x80C81428);
        put32(quad_ram, i8_palette_address + 2 * sizeof(uint32_t), 0x400A1EDC);
        put32(quad_ram, i8_palette_address + 3 * sizeof(uint32_t), 0xFF123456);
        for (uint32_t y = 0; y < 32; ++y) {
            quad_ram[i8_texture_address + i8_morton(66, y)] = 3;
            quad_ram[i8_texture_address + i8_morton(67, y)] = 3;
        }
        put32(quad_ram, rs2_color_address + 160 * 0x0C00 + 272 * 4,
              0xFF204060);
        put32(quad_ram, rs2_color_address + 160 * 0x0C00 + 364 * 4, 0);
        submit_i8_quad(i8_quad);
        CHECK(quad_fault.empty());
        CHECK(quad_gpu.immediate_quad_i8_raster_batches() == 2);
        CHECK(quad_gpu.immediate_quad_i8_raster_writes() > 0);
        // At x=272, the bilinear sample mixes palette entries 1 and 2. This
        // would produce a different result if indices were filtered first.
        auto i8_expected = []() {
            const float mix = 5.0f / 18.0f;
            const std::array<float, 4> p1{{200 / 255.0f, 20 / 255.0f,
                                             40 / 255.0f, 128 / 255.0f}};
            const std::array<float, 4> p2{{10 / 255.0f, 30 / 255.0f,
                                            220 / 255.0f, 64 / 255.0f}};
            std::array<float, 4> source{};
            for (size_t c = 0; c < source.size(); ++c)
                source[c] = p1[c] + (p2[c] - p1[c]) * mix;
            const std::array<float, 4> destination{{32 / 255.0f, 64 / 255.0f,
                                                      96 / 255.0f, 1.0f}};
            auto pack = [](float value) {
                return static_cast<uint32_t>(std::floor(
                    std::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f));
            };
            std::array<uint32_t, 4> out{};
            for (size_t c = 0; c < out.size(); ++c)
                out[c] = pack(source[c] * source[3] +
                              destination[c] * (1.0f - source[3]));
            return (out[3] << 24) | (out[0] << 16) | (out[1] << 8) | out[2];
        };
        CHECK(surface_pixel(rs2_color_address, 272, 160) == i8_expected());
        // x=364 lands on the packed high-X tail (x=66/67), proving the
        // rectangular 128x32 swizzle rather than a square Morton layout.
        CHECK(surface_pixel(rs2_color_address, 364, 160) == 0xFF123456);

        // RS0 is the same profile on the alternating first target.
        emit_quad(0x0210, 0x03DF4000);
        put32(quad_ram, color_address + 160 * 0x0C00 + 272 * 4,
              0xFF204060);
        submit_i8_quad(i8_quad);
        CHECK(quad_fault.empty());
        CHECK(quad_gpu.immediate_quad_i8_raster_batches() == 3);
        CHECK(surface_pixel(color_address, 272, 160) == i8_expected());

        // A fifth vertex and a one-word blend mutation are both
        // recorder-only I8 candidates and leave profile telemetry/output intact.
        const auto i8_batches = quad_gpu.immediate_quad_i8_raster_batches();
        const auto i8_writes = quad_gpu.immediate_quad_i8_raster_writes();
        const auto i8_hash = quad_gpu.immediate_quad_i8_raster_output_hash();
        emit_quad(0x17FC, 8);
        emit_quad(0x194C, 0xFFFFFFFF);
        for (const auto& position : i8_quad) {
            emit_quad(0x1518, float_bits(position[0]));
            emit_quad(0x151C, float_bits(position[1]));
            emit_quad(0x1520, float_bits(position[2]));
            emit_quad(0x1524, float_bits(position[3]));
        }
        emit_quad(0x1518, float_bits(270.0f));
        emit_quad(0x151C, float_bits(158.0f));
        emit_quad(0x1520, float_bits(0.0f));
        emit_quad(0x1524, float_bits(0.0f));
        emit_quad(0x17FC, 0);
        kick_quad();
        CHECK(quad_fault.empty());
        CHECK(quad_gpu.immediate_quad_i8_raster_batches() == i8_batches);
        emit_quad(0x0344, 0x0301);
        submit_i8_quad(i8_quad);
        CHECK(quad_fault.empty());
        CHECK(quad_gpu.immediate_quad_i8_raster_batches() == i8_batches);
        CHECK(quad_gpu.immediate_quad_i8_raster_writes() == i8_writes);
        CHECK(quad_gpu.immediate_quad_i8_raster_output_hash() == i8_hash);
        CHECK(quad_gpu.immediate_quad_i8_candidates() == 4);
        CHECK(quad_gpu.last_immediate_quad_i8_reject_mask() != 0);
        CHECK(quad_gpu.last_immediate_quad_i8_raster_reject_mask() != 0);

        // RS8/RS13 retain the same four-immediate-vertex pixel pipeline but
        // sample direct format-6 A8R8G8B8 texels.  Palette0 is stale state for
        // this format: changing it must not turn a valid direct-color draw
        // into the indexed profile or affect its result.
        const auto before_a8_a4_batches = quad_gpu.immediate_quad_raster_batches();
        const auto before_a8_a4_writes = quad_gpu.immediate_quad_raster_writes();
        const auto before_a8_a4_hash = quad_gpu.immediate_quad_raster_output_hash();
        const auto before_a8_i8_batches = quad_gpu.immediate_quad_i8_raster_batches();
        const auto before_a8_i8_writes = quad_gpu.immediate_quad_i8_raster_writes();
        const auto before_a8_i8_hash = quad_gpu.immediate_quad_i8_raster_output_hash();
        constexpr uint32_t kA8TextureOffset = 0x02DEF080;
        constexpr uint32_t kI8TextureOffset = 0x02DED480;
        const uint32_t a8_texture_address =
            i8_texture_address + (kA8TextureOffset - kI8TextureOffset);
        CHECK(a8_texture_address + 128 * 32 * sizeof(uint32_t) <= quad_ram.size());
        emit_quad(0x0184, 3);
        emit_quad(0x0210, 0x03C8C000);
        emit_quad(0x0344, 0x0302);
        emit_quad(0x1B00, kA8TextureOffset);
        emit_quad(0x1B04, 0x05710629);
        emit_quad(0x1B20, 0xDEADBEEF);
        std::fill(quad_ram.begin() + a8_texture_address,
                  quad_ram.begin() + a8_texture_address +
                      128 * 32 * sizeof(uint32_t),
                  0);
        const std::vector<uint8_t> a8_zero_alpha_before(
            quad_ram.begin() + rs2_color_address,
            quad_ram.begin() + rs2_color_address + color_bytes);
        submit_i8_quad(i8_quad);
        CHECK(quad_fault.empty());
        CHECK(quad_gpu.immediate_quad_a8_raster_batches() == 1);
        CHECK(quad_gpu.immediate_quad_a8_raster_alpha_passes() == 0);
        CHECK(quad_gpu.immediate_quad_a8_raster_writes() == 0);
        CHECK(std::equal(a8_zero_alpha_before.begin(), a8_zero_alpha_before.end(),
                         quad_ram.begin() + rs2_color_address));

        // Make the direct texels non-uniform.  The 272 sample verifies
        // bilinear interpolation and partial alpha blending; the high-X tail
        // catches a square-Morton interpretation of this 128x32 texture.
        for (uint32_t index = 0; index < 128 * 32; ++index)
            put32(quad_ram, a8_texture_address + index * sizeof(uint32_t),
                  0x80C81428);
        for (uint32_t y = 0; y < 32; ++y) {
            put32(quad_ram,
                  a8_texture_address + i8_morton(2, y) * sizeof(uint32_t),
                  0x400A1EDC);
            put32(quad_ram,
                  a8_texture_address + i8_morton(66, y) * sizeof(uint32_t),
                  0xFF123456);
            put32(quad_ram,
                  a8_texture_address + i8_morton(67, y) * sizeof(uint32_t),
                  0xFF123456);
        }
        put32(quad_ram, rs2_color_address + 160 * 0x0C00 + 272 * 4,
              0xFF204060);
        put32(quad_ram, rs2_color_address + 160 * 0x0C00 + 364 * 4, 0);
        submit_i8_quad(i8_quad);
        CHECK(quad_fault.empty());
        CHECK(quad_gpu.immediate_quad_a8_raster_batches() == 2);
        CHECK(quad_gpu.immediate_quad_a8_raster_writes() > 0);
        CHECK(surface_pixel(rs2_color_address, 272, 160) == i8_expected());
        CHECK(surface_pixel(rs2_color_address, 364, 160) == 0xFF123456);

        // RS8 is the alternating first target with the exact same texture
        // state.  Keep Palette0 stale here as a second admission check.
        emit_quad(0x0210, 0x03DF4000);
        put32(quad_ram, color_address + 160 * 0x0C00 + 272 * 4,
              0xFF204060);
        submit_i8_quad(i8_quad);
        CHECK(quad_fault.empty());
        CHECK(quad_gpu.immediate_quad_a8_raster_batches() == 3);
        CHECK(surface_pixel(color_address, 272, 160) == i8_expected());

        // The profile admits the exact four-vertex immediate source, not a
        // frozen post-VSH geometry tuple.  A small valid UV change executes;
        // adding a fifth immediate vertex is recorder-only.
        auto a8_variant = i8_quad;
        a8_variant[0][2] = 0.02f;
        submit_i8_quad(a8_variant);
        CHECK(quad_fault.empty());
        CHECK(quad_gpu.immediate_quad_a8_raster_batches() == 4);
        const auto a8_before_fifth_batches =
            quad_gpu.immediate_quad_a8_raster_batches();
        auto a8_five = std::array<std::array<float, 4>, 5>{};
        for (size_t index = 0; index < i8_quad.size(); ++index)
            a8_five[index] = i8_quad[index];
        a8_five[4] = {{270.0f, 158.0f, 0.0f, 0.0f}};
        submit_i8_quad(a8_five);
        CHECK(quad_fault.empty());
        CHECK(quad_gpu.immediate_quad_a8_raster_batches() == a8_before_fifth_batches);
        CHECK(quad_gpu.immediate_quad_a8_candidates() == 4);
        CHECK(quad_gpu.last_immediate_quad_a8_reject_mask() == 0);

        // A one-word blend mutation remains a recorder-only A8
        // candidate.  It cannot change the separate A8 output accounting.
        const auto a8_batches = quad_gpu.immediate_quad_a8_raster_batches();
        const auto a8_writes = quad_gpu.immediate_quad_a8_raster_writes();
        const auto a8_hash = quad_gpu.immediate_quad_a8_raster_output_hash();
        emit_quad(0x0344, 0x0301);
        submit_i8_quad(i8_quad);
        CHECK(quad_fault.empty());
        CHECK(quad_gpu.immediate_quad_a8_raster_batches() == a8_batches);
        CHECK(quad_gpu.immediate_quad_a8_raster_writes() == a8_writes);
        CHECK(quad_gpu.immediate_quad_a8_raster_output_hash() == a8_hash);
        CHECK(quad_gpu.immediate_quad_a8_candidates() == 5);
        CHECK(quad_gpu.last_immediate_quad_a8_reject_mask() != 0);
        CHECK(quad_gpu.last_immediate_quad_a8_raster_reject_mask() != 0);

        // An undersized direct-texture DMA window fails before sampling or
        // writing the color target, just as the indexed path below does.
        emit_quad(0x0344, 0x0302);
        const std::vector<uint8_t> before_a8_dma_reject(
            quad_ram.begin() + color_address,
            quad_ram.begin() + color_address + color_bytes);
        quad_gpu.mmio_write(xlle::Nv2a::kBase + 0x700020, 4, 4);
        quad_gpu.mmio_write(xlle::Nv2a::kBase + 0x700024, 4, 0x80010050);
        quad_gpu.mmio_write(xlle::Nv2a::kBase + 0x700500, 4, 0x3D);
        quad_gpu.mmio_write(xlle::Nv2a::kBase + 0x700504, 4, 0);
        quad_gpu.mmio_write(xlle::Nv2a::kBase + 0x700508, 4, 0x05313000);
        emit_quad(0x0184, 4);
        submit_i8_quad(i8_quad);
        CHECK(quad_fault ==
              "NV2A immediate-quad A8R8G8B8 texture DMA range is invalid");
        CHECK(quad_gpu.immediate_quad_a8_raster_batches() == a8_batches);
        CHECK(quad_gpu.immediate_quad_a8_raster_writes() == a8_writes);
        CHECK(quad_gpu.immediate_quad_a8_raster_output_hash() == a8_hash);
        CHECK(std::equal(before_a8_dma_reject.begin(), before_a8_dma_reject.end(),
                         quad_ram.begin() + color_address));
        CHECK(quad_gpu.immediate_quad_a8_candidates() == 6);
        CHECK(quad_gpu.last_immediate_quad_a8_reject_mask() == 0);
        quad_fault.clear();

        // Restore the indexed state exactly; its own DMA rejection test is
        // intentionally kept below so its historical counters/hashes remain
        // independently comparable.
        emit_quad(0x0184, 3);
        emit_quad(0x1B00, kI8TextureOffset);
        emit_quad(0x1B04, 0x05710B29);
        emit_quad(0x1B20, 0x02DED080);
        CHECK(quad_gpu.immediate_quad_raster_batches() == before_a8_a4_batches);
        CHECK(quad_gpu.immediate_quad_raster_writes() == before_a8_a4_writes);
        CHECK(quad_gpu.immediate_quad_raster_output_hash() == before_a8_a4_hash);
        CHECK(quad_gpu.immediate_quad_i8_raster_batches() == before_a8_i8_batches);
        CHECK(quad_gpu.immediate_quad_i8_raster_writes() == before_a8_i8_writes);
        CHECK(quad_gpu.immediate_quad_i8_raster_output_hash() == before_a8_i8_hash);

        // RS16/RS17 are not the 128x32 I8 row family: format 0x0b is an
        // independently witnessed 64x128 I8+A8R8G8B8-palette menu column.
        // Map its captured offsets into a separate compact-RAM range.
        const auto before_menu_a4_batches = quad_gpu.immediate_quad_raster_batches();
        const auto before_menu_a4_writes = quad_gpu.immediate_quad_raster_writes();
        const auto before_menu_a4_hash = quad_gpu.immediate_quad_raster_output_hash();
        const auto before_menu_i8_batches = quad_gpu.immediate_quad_i8_raster_batches();
        const auto before_menu_i8_writes = quad_gpu.immediate_quad_i8_raster_writes();
        const auto before_menu_i8_hash = quad_gpu.immediate_quad_i8_raster_output_hash();
        const auto before_menu_a8_batches = quad_gpu.immediate_quad_a8_raster_batches();
        const auto before_menu_a8_writes = quad_gpu.immediate_quad_a8_raster_writes();
        const auto before_menu_a8_hash = quad_gpu.immediate_quad_a8_raster_output_hash();
        constexpr uint32_t menu_texture_address = 0x00200800;
        constexpr uint32_t menu_palette_address = 0x00200400;
        quad_gpu.mmio_write(xlle::Nv2a::kBase + 0x700028, 4, 5);
        quad_gpu.mmio_write(xlle::Nv2a::kBase + 0x70002C, 4, 0x80010060);
        quad_gpu.mmio_write(xlle::Nv2a::kBase + 0x700600, 4, 0x3D);
        quad_gpu.mmio_write(xlle::Nv2a::kBase + 0x700604, 4, 0x07FFFFFF);
        quad_gpu.mmio_write(xlle::Nv2a::kBase + 0x700608, 4, 0x04802000);
        emit_quad(0x0184, 5);
        emit_quad(0x0200, 0x02D00000);
        emit_quad(0x0204, 0x01E00000);
        emit_quad(0x0210, 0x03C8C000);
        emit_quad(0x0344, 0x0302);
        emit_quad(0x1B00, 0x039FE800);
        emit_quad(0x1B04, 0x07610B29);
        emit_quad(0x1B20, 0x039FE400);
        emit_quad(0x0288, 0x0000000C);
        emit_quad(0x028C, 0x00001C80);
        emit_quad(0x1E20, 0x00DADADA);
        emit_quad(0x1E24, 0x00101010);
        emit_quad(0x0A60, 0xFFFFFFFF);
        emit_quad(0x0A80, 0xFFFFFFFF);
        const std::array<std::array<float, 4>, 4> menu_quad{{
            {{270.0f, 158.0f, 0.0f, 0.0f}},
            {{450.0f, 158.0f, 1.0f, 0.0f}},
            {{450.0f, 372.0f, 1.0f, 1.0f}},
            {{270.0f, 372.0f, 0.0f, 1.0f}},
        }};
        auto submit_menu_quad = [&](const auto& vertices, uint32_t diffuse = 0xFFFFFFFF) {
            emit_quad(0x17FC, 8);
            emit_quad(0x194C, diffuse);
            for (const auto& position : vertices) {
                emit_quad(0x1518, float_bits(position[0]));
                emit_quad(0x151C, float_bits(position[1]));
                emit_quad(0x1520, float_bits(position[2]));
                emit_quad(0x1524, float_bits(position[3]));
            }
            emit_quad(0x17FC, 0);
            kick_quad();
        };
        auto menu_morton = [](uint32_t x, uint32_t y) {
            uint32_t result = 0;
            uint32_t output_bit = 0;
            for (uint32_t input_bit = 1; input_bit < 64 || input_bit < 128;
                 input_bit <<= 1) {
                if (input_bit < 64) {
                    result |= ((x & input_bit) != 0) << output_bit;
                    ++output_bit;
                }
                if (input_bit < 128) {
                    result |= ((y & input_bit) != 0) << output_bit;
                    ++output_bit;
                }
            }
            return result;
        };
        std::fill(quad_ram.begin() + menu_texture_address,
                  quad_ram.begin() + menu_texture_address + 64 * 128, 0);
        for (uint32_t index = 0; index < 256; ++index)
            put32(quad_ram, menu_palette_address + index * sizeof(uint32_t), 0);
        const std::vector<uint8_t> menu_zero_alpha_before(
            quad_ram.begin() + rs2_color_address,
            quad_ram.begin() + rs2_color_address + color_bytes);
        submit_menu_quad(menu_quad);
        CHECK(quad_fault.empty());
        CHECK(quad_gpu.immediate_quad_menu_i8_raster_batches() == 1);
        CHECK(quad_gpu.immediate_quad_menu_i8_raster_alpha_passes() == 0);
        CHECK(quad_gpu.immediate_quad_menu_i8_raster_writes() == 0);
        CHECK(std::equal(menu_zero_alpha_before.begin(), menu_zero_alpha_before.end(),
                         quad_ram.begin() + rs2_color_address));

        // Index 1/2 must palette-expand before filtering. The chosen pixel
        // has a 7/18 mix of the two colors, with non-opaque alpha exercising
        // the captured source-alpha blend and little-endian AARRGGBB entries.
        std::fill(quad_ram.begin() + menu_texture_address,
                  quad_ram.begin() + menu_texture_address + 64 * 128, 1);
        for (uint32_t y = 0; y < 128; ++y)
            quad_ram[menu_texture_address + menu_morton(1, y)] = 2;
        put32(quad_ram, menu_palette_address + sizeof(uint32_t), 0x80C81428);
        put32(quad_ram, menu_palette_address + 2 * sizeof(uint32_t), 0x400A1EDC);
        put32(quad_ram, menu_palette_address + 3 * sizeof(uint32_t), 0xFF123456);
        for (uint32_t x = 0; x < 64; ++x) {
            quad_ram[menu_texture_address + menu_morton(x, 99)] = 3;
            quad_ram[menu_texture_address + menu_morton(x, 100)] = 3;
        }
        auto menu_blend_expected = []() {
            const float mix = 7.0f / 18.0f;
            const std::array<float, 4> p1{{200 / 255.0f, 20 / 255.0f,
                                             40 / 255.0f, 128 / 255.0f}};
            const std::array<float, 4> p2{{10 / 255.0f, 30 / 255.0f,
                                            220 / 255.0f, 64 / 255.0f}};
            std::array<float, 4> source{};
            for (size_t c = 0; c < source.size(); ++c)
                source[c] = p1[c] + (p2[c] - p1[c]) * mix;
            const std::array<float, 4> destination{{32 / 255.0f, 64 / 255.0f,
                                                      96 / 255.0f, 1.0f}};
            auto pack = [](float value) {
                return static_cast<uint32_t>(std::floor(
                    std::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f));
            };
            std::array<uint32_t, 4> out{};
            for (size_t c = 0; c < out.size(); ++c)
                out[c] = pack(source[c] * source[3] +
                              destination[c] * (1.0f - source[3]));
            return (out[3] << 24) | (out[0] << 16) | (out[1] << 8) | out[2];
        };
        put32(quad_ram, rs2_color_address + 160 * 0x0C00 + 272 * 4,
              0xFF204060);
        put32(quad_ram, rs2_color_address + 325 * 0x0C00 + 360 * 4, 0);
        submit_menu_quad(menu_quad);
        CHECK(quad_fault.empty());
        CHECK(quad_gpu.immediate_quad_menu_i8_raster_batches() == 2);
        CHECK(surface_pixel(rs2_color_address, 272, 160) == menu_blend_expected());
        CHECK(surface_pixel(rs2_color_address, 360, 325) == 0xFF123456);

        // The alternate target and a non-white diffuse remain semantic inputs.
        emit_quad(0x0210, 0x03DF4000);
        put32(quad_ram, color_address + 160 * 0x0C00 + 272 * 4, 0xFF204060);
        submit_menu_quad(menu_quad);
        CHECK(quad_fault.empty());
        CHECK(quad_gpu.immediate_quad_menu_i8_raster_batches() == 3);
        CHECK(surface_pixel(color_address, 272, 160) == menu_blend_expected());
        for (uint32_t index = 0; index < 64 * 128; ++index)
            quad_ram[menu_texture_address + index] = 3;
        put32(quad_ram, color_address + 200 * 0x0C00 + 300 * 4, 0);
        submit_menu_quad(menu_quad, 0xFF808080);
        CHECK(quad_fault.empty());
        CHECK(quad_gpu.immediate_quad_menu_i8_raster_batches() == 4);
        CHECK(surface_pixel(color_address, 300, 200) == 0xFF091A2B);

        // Varyings stay dynamic, while source shape remains exactly four.
        auto menu_variant = menu_quad;
        menu_variant[0][2] = 0.02f;
        submit_menu_quad(menu_variant);
        CHECK(quad_fault.empty());
        CHECK(quad_gpu.immediate_quad_menu_i8_raster_batches() == 5);
        auto menu_five = std::array<std::array<float, 4>, 5>{};
        for (size_t index = 0; index < menu_quad.size(); ++index)
            menu_five[index] = menu_quad[index];
        menu_five[4] = {{270.0f, 158.0f, 0.0f, 0.0f}};
        submit_menu_quad(menu_five);
        CHECK(quad_fault.empty());
        CHECK(quad_gpu.immediate_quad_menu_i8_raster_batches() == 5);

        // A narrow clip, wrong texture state, or the zero-final-factor one-off
        // must stay outside the exact family. The latter is not even a profile
        // candidate.
        const auto menu_candidates = quad_gpu.immediate_quad_menu_i8_candidates();
        emit_quad(0x0200, 0x00B4010E);
        emit_quad(0x0204, 0x00D7009E);
        submit_menu_quad(menu_quad);
        CHECK(quad_fault.empty());
        CHECK(quad_gpu.immediate_quad_menu_i8_raster_batches() == 5);
        CHECK(quad_gpu.immediate_quad_menu_i8_candidates() == menu_candidates + 1);
        CHECK(quad_gpu.last_immediate_quad_menu_i8_reject_mask() != 0);
        emit_quad(0x0200, 0x02D00000);
        emit_quad(0x0204, 0x01E00000);
        emit_quad(0x1B04, 0x07610B28);
        submit_menu_quad(menu_quad);
        CHECK(quad_fault.empty());
        CHECK(quad_gpu.immediate_quad_menu_i8_raster_batches() == 5);
        CHECK(quad_gpu.immediate_quad_menu_i8_candidates() == menu_candidates + 1);
        emit_quad(0x1B04, 0x07610B29);
        emit_quad(0x1B20, 0x039FE440);
        submit_menu_quad(menu_quad);
        CHECK(quad_fault.empty());
        CHECK(quad_gpu.immediate_quad_menu_i8_raster_batches() == 5);
        CHECK(quad_gpu.immediate_quad_menu_i8_candidates() == menu_candidates + 2);
        CHECK(quad_gpu.last_immediate_quad_menu_i8_reject_mask() != 0);
        emit_quad(0x1B20, 0x039FE400);
        emit_quad(0x1E20, 0);
        emit_quad(0x1E24, 0);
        submit_menu_quad(menu_quad);
        CHECK(quad_fault.empty());
        CHECK(quad_gpu.immediate_quad_menu_i8_raster_batches() == 5);
        CHECK(quad_gpu.immediate_quad_menu_i8_candidates() == menu_candidates + 2);
        emit_quad(0x1E20, 0x00DADADA);
        emit_quad(0x1E24, 0x00101010);

        // DMA preflight fails before color writes, then legacy I8 state is
        // restored for its existing isolated rejection test below.
        const std::vector<uint8_t> before_menu_dma_reject(
            quad_ram.begin() + color_address,
            quad_ram.begin() + color_address + color_bytes);
        quad_gpu.mmio_write(xlle::Nv2a::kBase + 0x700030, 4, 6);
        quad_gpu.mmio_write(xlle::Nv2a::kBase + 0x700034, 4, 0x80010070);
        quad_gpu.mmio_write(xlle::Nv2a::kBase + 0x700700, 4, 0x3D);
        quad_gpu.mmio_write(xlle::Nv2a::kBase + 0x700704, 4, 0);
        quad_gpu.mmio_write(xlle::Nv2a::kBase + 0x700708, 4, 0x04802000);
        emit_quad(0x0184, 6);
        submit_menu_quad(menu_quad);
        CHECK(quad_fault == "NV2A immediate-quad menu I8 texture DMA range is invalid");
        CHECK(quad_gpu.immediate_quad_menu_i8_raster_batches() == 5);
        CHECK(std::equal(before_menu_dma_reject.begin(), before_menu_dma_reject.end(),
                         quad_ram.begin() + color_address));
        quad_fault.clear();
        CHECK(quad_gpu.immediate_quad_raster_batches() == before_menu_a4_batches);
        CHECK(quad_gpu.immediate_quad_raster_writes() == before_menu_a4_writes);
        CHECK(quad_gpu.immediate_quad_raster_output_hash() == before_menu_a4_hash);
        CHECK(quad_gpu.immediate_quad_i8_raster_batches() == before_menu_i8_batches);
        CHECK(quad_gpu.immediate_quad_i8_raster_writes() == before_menu_i8_writes);
        CHECK(quad_gpu.immediate_quad_i8_raster_output_hash() == before_menu_i8_hash);
        CHECK(quad_gpu.immediate_quad_a8_raster_batches() == before_menu_a8_batches);
        CHECK(quad_gpu.immediate_quad_a8_raster_writes() == before_menu_a8_writes);
        CHECK(quad_gpu.immediate_quad_a8_raster_output_hash() == before_menu_a8_hash);
        emit_quad(0x0184, 3);
        emit_quad(0x0200, 0x00B4010E);
        emit_quad(0x0204, 0x00D7009E);
        emit_quad(0x1B00, kI8TextureOffset);
        emit_quad(0x1B04, 0x05710B29);
        emit_quad(0x1B20, 0x02DED080);

        // A DMA-A object with an undersized window fails the I8 texture range
        // before sampling or color-surface writes. The profile's pixel-state
        // candidate telemetry still records this as an otherwise exact attempt.
        const std::vector<uint8_t> before_i8_dma_reject(
            quad_ram.begin() + color_address,
            quad_ram.begin() + color_address + color_bytes);
        quad_gpu.mmio_write(xlle::Nv2a::kBase + 0x700020, 4, 4);
        quad_gpu.mmio_write(xlle::Nv2a::kBase + 0x700024, 4, 0x80010050);
        quad_gpu.mmio_write(xlle::Nv2a::kBase + 0x700500, 4, 0x3D);
        quad_gpu.mmio_write(xlle::Nv2a::kBase + 0x700504, 4, 0);
        quad_gpu.mmio_write(xlle::Nv2a::kBase + 0x700508, 4, 0x05313000);
        emit_quad(0x0184, 4);
        emit_quad(0x0344, 0x0302);
        submit_i8_quad(i8_quad);
        CHECK(quad_fault == "NV2A immediate-quad I8 texture DMA range is invalid");
        CHECK(quad_gpu.immediate_quad_i8_raster_batches() == i8_batches);
        CHECK(quad_gpu.immediate_quad_i8_raster_writes() == i8_writes);
        CHECK(quad_gpu.immediate_quad_i8_raster_output_hash() == i8_hash);
        CHECK(std::equal(before_i8_dma_reject.begin(), before_i8_dma_reject.end(),
                         quad_ram.begin() + color_address));
        CHECK(quad_gpu.immediate_quad_i8_candidates() == 5);
        CHECK(quad_gpu.last_immediate_quad_i8_reject_mask() == 0);

        // IQRS3/IQRS7 are the independent 512x128 indexed upper-left artwork
        // pair.  Their palette immediately precedes T0 in the captured DMA-A
        // span; retain the high guest offsets while mapping into compact RAM.
        quad_fault.clear();
        const auto before_artwork_a4_batches = quad_gpu.immediate_quad_raster_batches();
        const auto before_artwork_a4_writes = quad_gpu.immediate_quad_raster_writes();
        const auto before_artwork_a4_hash = quad_gpu.immediate_quad_raster_output_hash();
        const auto before_artwork_i8_batches = quad_gpu.immediate_quad_i8_raster_batches();
        const auto before_artwork_i8_writes = quad_gpu.immediate_quad_i8_raster_writes();
        const auto before_artwork_i8_hash = quad_gpu.immediate_quad_i8_raster_output_hash();
        const auto before_artwork_a8_batches = quad_gpu.immediate_quad_a8_raster_batches();
        const auto before_artwork_a8_writes = quad_gpu.immediate_quad_a8_raster_writes();
        const auto before_artwork_a8_hash = quad_gpu.immediate_quad_a8_raster_output_hash();
        const auto before_artwork_menu_batches = quad_gpu.immediate_quad_menu_i8_raster_batches();
        const auto before_artwork_menu_writes = quad_gpu.immediate_quad_menu_i8_raster_writes();
        const auto before_artwork_menu_hash = quad_gpu.immediate_quad_menu_i8_raster_output_hash();
        constexpr uint32_t artwork_texture_address = 0x00104480;
        constexpr uint32_t artwork_palette_address = 0x00104080;
        quad_gpu.mmio_write(xlle::Nv2a::kBase + 0x700038, 4, 7);
        quad_gpu.mmio_write(xlle::Nv2a::kBase + 0x70003C, 4, 0x80010080);
        quad_gpu.mmio_write(xlle::Nv2a::kBase + 0x700800, 4, 0x3D);
        quad_gpu.mmio_write(xlle::Nv2a::kBase + 0x700804, 4, 0x07FFFFFF);
        quad_gpu.mmio_write(xlle::Nv2a::kBase + 0x700808, 4, 0x05310000);
        emit_quad(0x0184, 7);
        emit_quad(0x0200, 0x02D00000);
        emit_quad(0x0204, 0x01E00000);
        emit_quad(0x0210, 0x03DF4000);
        emit_quad(0x0344, 0x0302);
        emit_quad(0x1B00, 0x02DF4480);
        emit_quad(0x1B04, 0x07910B29);
        emit_quad(0x1B20, 0x02DF4080);
        emit_quad(0x0288, 0x0000000C);
        emit_quad(0x028C, 0x00001C80);
        emit_quad(0x1E20, 0x00DADADA);
        emit_quad(0x1E24, 0x00101010);
        emit_quad(0x0A60, 0xFFFFFFFF);
        emit_quad(0x0A80, 0xFFFFFFFF);
        const std::array<std::array<float, 4>, 4> artwork_quad{{
            {{100.0f, 120.0f, 0.0f, 0.0f}},
            {{280.0f, 120.0f, 1.0f, 0.0f}},
            {{280.0f, 300.0f, 1.0f, 1.0f}},
            {{100.0f, 300.0f, 0.0f, 1.0f}},
        }};
        auto submit_artwork_quad = [&](const auto& vertices, uint32_t diffuse = 0xFFFFFFFF) {
            emit_quad(0x17FC, 8);
            emit_quad(0x194C, diffuse);
            for (const auto& position : vertices) {
                emit_quad(0x1518, float_bits(position[0]));
                emit_quad(0x151C, float_bits(position[1]));
                emit_quad(0x1520, float_bits(position[2]));
                emit_quad(0x1524, float_bits(position[3]));
            }
            emit_quad(0x17FC, 0);
            kick_quad();
        };
        auto artwork_morton = [](uint32_t x, uint32_t y) {
            uint32_t result = 0;
            uint32_t output_bit = 0;
            for (uint32_t input_bit = 1; input_bit < 512 || input_bit < 128;
                 input_bit <<= 1) {
                if (input_bit < 512) {
                    result |= ((x & input_bit) != 0) << output_bit;
                    ++output_bit;
                }
                if (input_bit < 128) {
                    result |= ((y & input_bit) != 0) << output_bit;
                    ++output_bit;
                }
            }
            return result;
        };
        CHECK(artwork_texture_address + 512 * 128 <= quad_ram.size());
        std::fill(quad_ram.begin() + artwork_texture_address,
                  quad_ram.begin() + artwork_texture_address + 512 * 128, 0);
        for (uint32_t index = 0; index < 256; ++index)
            put32(quad_ram, artwork_palette_address + index * sizeof(uint32_t), 0);
        const std::vector<uint8_t> artwork_zero_alpha_before(
            quad_ram.begin() + color_address,
            quad_ram.begin() + color_address + color_bytes);
        submit_artwork_quad(artwork_quad);
        CHECK(quad_fault.empty());
        CHECK(quad_gpu.immediate_quad_artwork_i8_raster_batches() == 1);
        CHECK(quad_gpu.immediate_quad_artwork_i8_raster_alpha_passes() == 0);
        CHECK(quad_gpu.immediate_quad_artwork_i8_raster_writes() == 0);
        CHECK(std::equal(artwork_zero_alpha_before.begin(), artwork_zero_alpha_before.end(),
                         quad_ram.begin() + color_address));

        // Filtering expands palette entries before interpolation. At the first
        // pixel the 512-wide half-texel rule makes index 2's exact weight 7/90.
        std::fill(quad_ram.begin() + artwork_texture_address,
                  quad_ram.begin() + artwork_texture_address + 512 * 128, 1);
        for (uint32_t y = 0; y < 128; ++y)
            quad_ram[artwork_texture_address + artwork_morton(0, y)] = 2;
        put32(quad_ram, artwork_palette_address + sizeof(uint32_t), 0x80C81428);
        put32(quad_ram, artwork_palette_address + 2 * sizeof(uint32_t), 0x400A1EDC);
        put32(quad_ram, artwork_palette_address + 3 * sizeof(uint32_t), 0xFF123456);
        for (uint32_t y : {108u, 109u})
            for (uint32_t x : {404u, 405u})
                quad_ram[artwork_texture_address + artwork_morton(x, y)] = 3;
        auto artwork_blend_expected = [](float index2_weight) {
            const std::array<float, 4> p1{{200 / 255.0f, 20 / 255.0f,
                                             40 / 255.0f, 128 / 255.0f}};
            const std::array<float, 4> p2{{10 / 255.0f, 30 / 255.0f,
                                            220 / 255.0f, 64 / 255.0f}};
            std::array<float, 4> source{};
            for (size_t c = 0; c < source.size(); ++c)
                source[c] = p1[c] + (p2[c] - p1[c]) * index2_weight;
            const std::array<float, 4> destination{{32 / 255.0f, 64 / 255.0f,
                                                      96 / 255.0f, 1.0f}};
            auto pack = [](float value) {
                return static_cast<uint32_t>(std::floor(
                    std::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f));
            };
            std::array<uint32_t, 4> out{};
            for (size_t c = 0; c < out.size(); ++c)
                out[c] = pack(source[c] * source[3] +
                              destination[c] * (1.0f - source[3]));
            return (out[3] << 24) | (out[0] << 16) | (out[1] << 8) | out[2];
        };
        put32(quad_ram, color_address + 120 * 0x0C00 + 100 * 4, 0xFF204060);
        put32(quad_ram, color_address + 273 * 0x0C00 + 242 * 4, 0);
        submit_artwork_quad(artwork_quad);
        CHECK(quad_fault.empty());
        CHECK(quad_gpu.immediate_quad_artwork_i8_raster_batches() == 2);
        CHECK(surface_pixel(color_address, 100, 120) == artwork_blend_expected(7.0f / 90.0f));
        CHECK(surface_pixel(color_address, 242, 273) == 0xFF123456);

        // IQRS3 and IQRS7 alternate the two color targets, and diffuse remains
        // a dynamic semantic input rather than an admission tuple.
        emit_quad(0x0210, 0x03C8C000);
        put32(quad_ram, rs2_color_address + 120 * 0x0C00 + 100 * 4, 0xFF204060);
        submit_artwork_quad(artwork_quad);
        CHECK(quad_fault.empty());
        CHECK(quad_gpu.immediate_quad_artwork_i8_raster_batches() == 3);
        CHECK(surface_pixel(rs2_color_address, 100, 120) == artwork_blend_expected(7.0f / 90.0f));
        std::fill(quad_ram.begin() + artwork_texture_address,
                  quad_ram.begin() + artwork_texture_address + 512 * 128, 3);
        put32(quad_ram, rs2_color_address + 200 * 0x0C00 + 160 * 4, 0);
        submit_artwork_quad(artwork_quad, 0xFF808080);
        CHECK(quad_fault.empty());
        CHECK(quad_gpu.immediate_quad_artwork_i8_raster_batches() == 4);
        CHECK(surface_pixel(rs2_color_address, 160, 200) == 0xFF091A2B);

        auto artwork_variant = artwork_quad;
        artwork_variant[0][2] = 0.02f;
        submit_artwork_quad(artwork_variant);
        CHECK(quad_fault.empty());
        CHECK(quad_gpu.immediate_quad_artwork_i8_raster_batches() == 5);
        auto artwork_five = std::array<std::array<float, 4>, 5>{};
        for (size_t index = 0; index < artwork_quad.size(); ++index)
            artwork_five[index] = artwork_quad[index];
        artwork_five[4] = {{100.0f, 120.0f, 0.0f, 0.0f}};
        submit_artwork_quad(artwork_five);
        CHECK(quad_fault.empty());
        CHECK(quad_gpu.immediate_quad_artwork_i8_raster_batches() == 5);

        const auto artwork_candidates = quad_gpu.immediate_quad_artwork_i8_candidates();
        emit_quad(0x0200, 0x00B4010E);
        emit_quad(0x0204, 0x00D7009E);
        submit_artwork_quad(artwork_quad);
        CHECK(quad_fault.empty());
        CHECK(quad_gpu.immediate_quad_artwork_i8_raster_batches() == 5);
        CHECK(quad_gpu.immediate_quad_artwork_i8_candidates() == artwork_candidates + 1);
        CHECK(quad_gpu.last_immediate_quad_artwork_i8_reject_mask() != 0);
        emit_quad(0x0200, 0x02D00000);
        emit_quad(0x0204, 0x01E00000);
        emit_quad(0x1B04, 0x07910B28);
        submit_artwork_quad(artwork_quad);
        CHECK(quad_fault.empty());
        CHECK(quad_gpu.immediate_quad_artwork_i8_raster_batches() == 5);
        emit_quad(0x1B04, 0x07910B29);
        emit_quad(0x1B20, 0x02DF40C0);
        submit_artwork_quad(artwork_quad);
        CHECK(quad_fault.empty());
        CHECK(quad_gpu.immediate_quad_artwork_i8_raster_batches() == 5);
        CHECK(quad_gpu.immediate_quad_artwork_i8_candidates() == artwork_candidates + 2);
        CHECK(quad_gpu.last_immediate_quad_artwork_i8_reject_mask() != 0);
        emit_quad(0x1B20, 0x02DF4080);
        emit_quad(0x1E20, 0);
        emit_quad(0x1E24, 0);
        submit_artwork_quad(artwork_quad);
        CHECK(quad_fault.empty());
        CHECK(quad_gpu.immediate_quad_artwork_i8_raster_batches() == 5);
        CHECK(quad_gpu.immediate_quad_artwork_i8_candidates() == artwork_candidates + 2);
        emit_quad(0x1E20, 0x00DADADA);
        emit_quad(0x1E24, 0x00101010);

        const std::vector<uint8_t> artwork_dma_before(
            quad_ram.begin() + rs2_color_address,
            quad_ram.begin() + rs2_color_address + color_bytes);
        quad_gpu.mmio_write(xlle::Nv2a::kBase + 0x700040, 4, 8);
        quad_gpu.mmio_write(xlle::Nv2a::kBase + 0x700044, 4, 0x80010090);
        quad_gpu.mmio_write(xlle::Nv2a::kBase + 0x700900, 4, 0x3D);
        quad_gpu.mmio_write(xlle::Nv2a::kBase + 0x700904, 4, 0);
        quad_gpu.mmio_write(xlle::Nv2a::kBase + 0x700908, 4, 0x05310000);
        emit_quad(0x0184, 8);
        submit_artwork_quad(artwork_quad);
        CHECK(quad_fault == "NV2A immediate-quad artwork I8 texture DMA range is invalid");
        CHECK(quad_gpu.immediate_quad_artwork_i8_raster_batches() == 5);
        CHECK(std::equal(artwork_dma_before.begin(), artwork_dma_before.end(),
                         quad_ram.begin() + rs2_color_address));
        CHECK(quad_gpu.immediate_quad_raster_batches() == before_artwork_a4_batches);
        CHECK(quad_gpu.immediate_quad_raster_writes() == before_artwork_a4_writes);
        CHECK(quad_gpu.immediate_quad_raster_output_hash() == before_artwork_a4_hash);
        CHECK(quad_gpu.immediate_quad_i8_raster_batches() == before_artwork_i8_batches);
        CHECK(quad_gpu.immediate_quad_i8_raster_writes() == before_artwork_i8_writes);
        CHECK(quad_gpu.immediate_quad_i8_raster_output_hash() == before_artwork_i8_hash);
        CHECK(quad_gpu.immediate_quad_a8_raster_batches() == before_artwork_a8_batches);
        CHECK(quad_gpu.immediate_quad_a8_raster_writes() == before_artwork_a8_writes);
        CHECK(quad_gpu.immediate_quad_a8_raster_output_hash() == before_artwork_a8_hash);
        CHECK(quad_gpu.immediate_quad_menu_i8_raster_batches() == before_artwork_menu_batches);
        CHECK(quad_gpu.immediate_quad_menu_i8_raster_writes() == before_artwork_menu_writes);
        CHECK(quad_gpu.immediate_quad_menu_i8_raster_output_hash() == before_artwork_menu_hash);

        // IQRS5/IQRS10 direct 512x512 main-panel image. Palette state is
        // intentionally stale/passive for A8R8G8B8.
        quad_fault.clear();
        const auto old_a4_batches = quad_gpu.immediate_quad_raster_batches();
        const auto old_a4_writes = quad_gpu.immediate_quad_raster_writes();
        const auto old_a4 = quad_gpu.immediate_quad_raster_output_hash();
        const auto old_i8_batches = quad_gpu.immediate_quad_i8_raster_batches();
        const auto old_i8_writes = quad_gpu.immediate_quad_i8_raster_writes();
        const auto old_i8 = quad_gpu.immediate_quad_i8_raster_output_hash();
        const auto old_a8_batches = quad_gpu.immediate_quad_a8_raster_batches();
        const auto old_a8_writes = quad_gpu.immediate_quad_a8_raster_writes();
        const auto old_a8 = quad_gpu.immediate_quad_a8_raster_output_hash();
        const auto old_menu_batches = quad_gpu.immediate_quad_menu_i8_raster_batches();
        const auto old_menu_writes = quad_gpu.immediate_quad_menu_i8_raster_writes();
        const auto old_menu = quad_gpu.immediate_quad_menu_i8_raster_output_hash();
        const auto old_art_batches = quad_gpu.immediate_quad_artwork_i8_raster_batches();
        const auto old_art_writes = quad_gpu.immediate_quad_artwork_i8_raster_writes();
        const auto old_art = quad_gpu.immediate_quad_artwork_i8_raster_output_hash();
        constexpr uint32_t panel_texture_address = 0x00100080;
        quad_gpu.mmio_write(xlle::Nv2a::kBase + 0x700048, 4, 9);
        quad_gpu.mmio_write(xlle::Nv2a::kBase + 0x70004C, 4, 0x800100A0);
        quad_gpu.mmio_write(xlle::Nv2a::kBase + 0x700A00, 4, 0x3D);
        quad_gpu.mmio_write(xlle::Nv2a::kBase + 0x700A04, 4, 0x07FFFFFF);
        quad_gpu.mmio_write(xlle::Nv2a::kBase + 0x700A08, 4, 0x04C04000);
        emit_quad(0x0184, 9); emit_quad(0x0200, 0x02D00000); emit_quad(0x0204, 0x01E00000);
        emit_quad(0x0210, 0x03DF4000); emit_quad(0x0344, 0x0302);
        emit_quad(0x1B00, 0x034FC080); emit_quad(0x1B04, 0x09910629);
        emit_quad(0x1B20, 0xDEADBEEF); emit_quad(0x0288, 0x0000000C);
        emit_quad(0x028C, 0x00001C80); emit_quad(0x1E20, 0x00DADADA);
        emit_quad(0x1E24, 0x00101010); emit_quad(0x0A60, 0xFFFFFFFF); emit_quad(0x0A80, 0xFFFFFFFF);
        const std::array<std::array<float, 4>, 4> panel_quad{{
            {{100.0f, 120.0f, 0.0f, 0.0f}}, {{260.0f, 120.0f, 1.0f, 0.0f}},
            {{260.0f, 280.0f, 1.0f, 1.0f}}, {{100.0f, 280.0f, 0.0f, 1.0f}},
        }};
        auto submit_panel = [&](const auto& vertices, uint32_t diffuse = 0xFFFFFFFF) {
            emit_quad(0x17FC, 8); emit_quad(0x194C, diffuse);
            for (const auto& p : vertices) { emit_quad(0x1518, float_bits(p[0])); emit_quad(0x151C, float_bits(p[1])); emit_quad(0x1520, float_bits(p[2])); emit_quad(0x1524, float_bits(p[3])); }
            emit_quad(0x17FC, 0); kick_quad();
        };
        auto panel_morton = [](uint32_t x, uint32_t y) {
            uint32_t r = 0, bit = 0;
            for (uint32_t in = 1; in < 512; in <<= 1) { r |= ((x & in) != 0) << bit++; r |= ((y & in) != 0) << bit++; }
            return r;
        };
        CHECK(panel_texture_address + 512 * 512 * 4 <= quad_ram.size());
        for (uint32_t i = 0; i < 512 * 512; ++i) put32(quad_ram, panel_texture_address + i * 4, 0);
        const std::vector<uint8_t> panel_zero_before(quad_ram.begin() + color_address,
                                                      quad_ram.begin() + color_address + color_bytes);
        submit_panel(panel_quad);
        CHECK(quad_fault.empty());
        CHECK(quad_gpu.immediate_quad_main_panel_raster_batches() == 1);
        CHECK(quad_gpu.immediate_quad_main_panel_raster_alpha_passes() == 0);
        CHECK(quad_gpu.immediate_quad_main_panel_raster_writes() == 0);
        CHECK(std::equal(panel_zero_before.begin(), panel_zero_before.end(),
                         quad_ram.begin() + color_address));
        for (uint32_t i = 0; i < 512 * 512; ++i) put32(quad_ram, panel_texture_address + i * 4, 0x80C81428);
        put32(quad_ram, panel_texture_address + panel_morton(1, 1) * 4, 0x400A1EDC);
        for (uint32_t y : {490u, 491u}) for (uint32_t x : {455u, 456u}) put32(quad_ram, panel_texture_address + panel_morton(x, y) * 4, 0xFF123456);
        put32(quad_ram, color_address + 120 * 0x0C00 + 100 * 4, 0xFF204060);
        put32(quad_ram, color_address + 273 * 0x0C00 + 242 * 4, 0);
        submit_panel(panel_quad);
        CHECK(quad_fault.empty());
        CHECK(quad_gpu.immediate_quad_main_panel_raster_batches() == 2);
        CHECK(quad_gpu.immediate_quad_main_panel_raster_triangles() == 4);
        CHECK(quad_gpu.immediate_quad_main_panel_raster_writes() > 0);
        CHECK(quad_gpu.immediate_quad_main_panel_texture_samples() > 0);
        CHECK(quad_gpu.immediate_quad_main_panel_raster_output_hash() != 1469598103934665603ull);
        CHECK(surface_pixel(color_address, 100, 120) == artwork_blend_expected(0.81f));
        CHECK(surface_pixel(color_address, 242, 273) == 0xFF123456);
        emit_quad(0x0210, 0x03C8C000); put32(quad_ram, rs2_color_address + 120 * 0x0C00 + 100 * 4, 0xFF204060); submit_panel(panel_quad);
        CHECK(quad_fault.empty()); CHECK(quad_gpu.immediate_quad_main_panel_raster_batches() == 3);
        CHECK(surface_pixel(rs2_color_address, 100, 120) == artwork_blend_expected(0.81f));
        for (uint32_t i = 0; i < 512 * 512; ++i) put32(quad_ram, panel_texture_address + i * 4, 0xFF123456);
        put32(quad_ram, rs2_color_address + 200 * 0x0C00 + 160 * 4, 0);
        submit_panel(panel_quad, 0xFF808080);
        CHECK(quad_fault.empty()); CHECK(quad_gpu.immediate_quad_main_panel_raster_batches() == 4);
        CHECK(surface_pixel(rs2_color_address, 160, 200) == 0xFF091A2B);
        auto panel_variant = panel_quad; panel_variant[0][2] = 0.02f; submit_panel(panel_variant);
        CHECK(quad_gpu.immediate_quad_main_panel_raster_batches() == 5);
        auto panel_five = std::array<std::array<float, 4>, 5>{}; for (size_t i = 0; i < 4; ++i) panel_five[i] = panel_quad[i]; submit_panel(panel_five);
        CHECK(quad_gpu.immediate_quad_main_panel_raster_batches() == 5);
        const auto panel_candidates = quad_gpu.immediate_quad_main_panel_candidates();
        emit_quad(0x0200, 0x00B4010E); emit_quad(0x0204, 0x00D7009E); submit_panel(panel_quad);
        CHECK(quad_gpu.immediate_quad_main_panel_raster_batches() == 5);
        CHECK(quad_gpu.immediate_quad_main_panel_candidates() == panel_candidates + 1);
        CHECK(quad_gpu.last_immediate_quad_main_panel_reject_mask() != 0);
        emit_quad(0x0200, 0x02D00000); emit_quad(0x0204, 0x01E00000); emit_quad(0x1B04, 0x09910628); submit_panel(panel_quad);
        CHECK(quad_gpu.immediate_quad_main_panel_raster_batches() == 5); emit_quad(0x1B04, 0x09910629);
        emit_quad(0x1E20, 0); emit_quad(0x1E24, 0); submit_panel(panel_quad);
        CHECK(quad_gpu.immediate_quad_main_panel_raster_batches() == 5); emit_quad(0x1E20, 0x00DADADA); emit_quad(0x1E24, 0x00101010);
        quad_gpu.mmio_write(xlle::Nv2a::kBase + 0x700050, 4, 10); quad_gpu.mmio_write(xlle::Nv2a::kBase + 0x700054, 4, 0x800100B0);
        const std::vector<uint8_t> panel_dma_before(quad_ram.begin() + rs2_color_address,
                                                     quad_ram.begin() + rs2_color_address + color_bytes);
        quad_gpu.mmio_write(xlle::Nv2a::kBase + 0x700B00, 4, 0x3D); quad_gpu.mmio_write(xlle::Nv2a::kBase + 0x700B04, 4, 0); quad_gpu.mmio_write(xlle::Nv2a::kBase + 0x700B08, 4, 0x04C04000);
        emit_quad(0x0184, 10); submit_panel(panel_quad);
        CHECK(quad_fault == "NV2A immediate-quad main-panel A8R8G8B8 texture DMA range is invalid");
        CHECK(quad_gpu.immediate_quad_main_panel_raster_batches() == 5);
        CHECK(std::equal(panel_dma_before.begin(), panel_dma_before.end(), quad_ram.begin() + rs2_color_address));
        CHECK(quad_gpu.immediate_quad_raster_batches() == old_a4_batches); CHECK(quad_gpu.immediate_quad_raster_writes() == old_a4_writes); CHECK(quad_gpu.immediate_quad_raster_output_hash() == old_a4);
        CHECK(quad_gpu.immediate_quad_i8_raster_batches() == old_i8_batches); CHECK(quad_gpu.immediate_quad_i8_raster_writes() == old_i8_writes); CHECK(quad_gpu.immediate_quad_i8_raster_output_hash() == old_i8);
        CHECK(quad_gpu.immediate_quad_a8_raster_batches() == old_a8_batches); CHECK(quad_gpu.immediate_quad_a8_raster_writes() == old_a8_writes); CHECK(quad_gpu.immediate_quad_a8_raster_output_hash() == old_a8);
        CHECK(quad_gpu.immediate_quad_menu_i8_raster_batches() == old_menu_batches); CHECK(quad_gpu.immediate_quad_menu_i8_raster_writes() == old_menu_writes); CHECK(quad_gpu.immediate_quad_menu_i8_raster_output_hash() == old_menu);
        CHECK(quad_gpu.immediate_quad_artwork_i8_raster_batches() == old_art_batches); CHECK(quad_gpu.immediate_quad_artwork_i8_raster_writes() == old_art_writes); CHECK(quad_gpu.immediate_quad_artwork_i8_raster_output_hash() == old_art);

        // IQRS4/IQRS7 direct 1024x1024 full-screen A8 image.  Its 4 MiB
        // DMA range is deliberately mapped disjointly from both compact color
        // surfaces; palette state remains stale/passive for format 6.
        quad_fault.clear();
        const auto full_old_a4_batches = quad_gpu.immediate_quad_raster_batches();
        const auto full_old_a4_writes = quad_gpu.immediate_quad_raster_writes();
        const auto full_old_a4_hash = quad_gpu.immediate_quad_raster_output_hash();
        const auto full_old_i8_batches = quad_gpu.immediate_quad_i8_raster_batches();
        const auto full_old_i8_writes = quad_gpu.immediate_quad_i8_raster_writes();
        const auto full_old_i8_hash = quad_gpu.immediate_quad_i8_raster_output_hash();
        const auto full_old_a8_batches = quad_gpu.immediate_quad_a8_raster_batches();
        const auto full_old_a8_writes = quad_gpu.immediate_quad_a8_raster_writes();
        const auto full_old_a8_hash = quad_gpu.immediate_quad_a8_raster_output_hash();
        const auto full_old_menu_batches = quad_gpu.immediate_quad_menu_i8_raster_batches();
        const auto full_old_menu_writes = quad_gpu.immediate_quad_menu_i8_raster_writes();
        const auto full_old_menu_hash = quad_gpu.immediate_quad_menu_i8_raster_output_hash();
        const auto full_old_art_batches = quad_gpu.immediate_quad_artwork_i8_raster_batches();
        const auto full_old_art_writes = quad_gpu.immediate_quad_artwork_i8_raster_writes();
        const auto full_old_art_hash = quad_gpu.immediate_quad_artwork_i8_raster_output_hash();
        const auto full_old_panel_batches = quad_gpu.immediate_quad_main_panel_raster_batches();
        const auto full_old_panel_writes = quad_gpu.immediate_quad_main_panel_raster_writes();
        const auto full_old_panel_hash = quad_gpu.immediate_quad_main_panel_raster_output_hash();
        constexpr uint32_t full_texture_address = 0x00400080;
        quad_gpu.mmio_write(xlle::Nv2a::kBase + 0x700058, 4, 11);
        quad_gpu.mmio_write(xlle::Nv2a::kBase + 0x70005C, 4, 0x800100C0);
        quad_gpu.mmio_write(xlle::Nv2a::kBase + 0x700C00, 4, 0x3D);
        quad_gpu.mmio_write(xlle::Nv2a::kBase + 0x700C04, 4, 0x07FFFFFF);
        quad_gpu.mmio_write(xlle::Nv2a::kBase + 0x700C08, 4, 0x04E03000);
        emit_quad(0x0184, 11); emit_quad(0x0200, 0x02D00000); emit_quad(0x0204, 0x01E00000);
        emit_quad(0x0210, 0x03DF4000); emit_quad(0x0344, 0x0302);
        emit_quad(0x1B00, 0x035FD080); emit_quad(0x1B04, 0x0AA10629);
        emit_quad(0x1B20, 0xDEADBEEF); emit_quad(0x0288, 0x0000000C);
        emit_quad(0x028C, 0x00001C80); emit_quad(0x1E20, 0x00DADADA);
        emit_quad(0x1E24, 0x00101010); emit_quad(0x0A60, 0xFFFFFFFF); emit_quad(0x0A80, 0xFFFFFFFF);
        const std::array<std::array<float, 4>, 4> full_quad{{
            {{100.0f, 120.0f, 0.0f, 0.0f}}, {{140.0f, 120.0f, 1.0f, 0.0f}},
            {{140.0f, 160.0f, 1.0f, 1.0f}}, {{100.0f, 160.0f, 0.0f, 1.0f}},
        }};
        auto submit_full = [&](const auto& vertices, uint32_t diffuse = 0xFFFFFFFF) {
            emit_quad(0x17FC, 8); emit_quad(0x194C, diffuse);
            for (const auto& p : vertices) { emit_quad(0x1518, float_bits(p[0])); emit_quad(0x151C, float_bits(p[1])); emit_quad(0x1520, float_bits(p[2])); emit_quad(0x1524, float_bits(p[3])); }
            emit_quad(0x17FC, 0); kick_quad();
        };
        auto full_morton = [](uint32_t x, uint32_t y) {
            uint32_t r = 0, bit = 0;
            for (uint32_t in = 1; in < 1024; in <<= 1) { r |= ((x & in) != 0) << bit++; r |= ((y & in) != 0) << bit++; }
            return r;
        };
        constexpr uint32_t full_texture_bytes = 1024 * 1024 * 4;
        CHECK(full_texture_address + full_texture_bytes <= quad_ram.size());
        std::fill(quad_ram.begin() + full_texture_address,
                  quad_ram.begin() + full_texture_address + full_texture_bytes, 0);
        const std::vector<uint8_t> full_zero_before(quad_ram.begin() + color_address,
                                                     quad_ram.begin() + color_address + color_bytes);
        submit_full(full_quad);
        CHECK(quad_fault.empty()); CHECK(quad_gpu.immediate_quad_fullscreen_raster_batches() == 1);
        CHECK(quad_gpu.immediate_quad_fullscreen_raster_alpha_passes() == 0);
        CHECK(quad_gpu.immediate_quad_fullscreen_raster_writes() == 0);
        CHECK(std::equal(full_zero_before.begin(), full_zero_before.end(), quad_ram.begin() + color_address));
        for (uint32_t i = 0; i < 1024 * 1024; ++i)
            put32(quad_ram, full_texture_address + i * 4, 0x80C81428);
        put32(quad_ram, full_texture_address + full_morton(524, 524) * 4, 0x400A1EDC);
        for (uint32_t y : {908u, 909u}) for (uint32_t x : {908u, 909u})
            put32(quad_ram, full_texture_address + full_morton(x, y) * 4, 0xFF123456);
        put32(quad_ram, color_address + 140 * 0x0C00 + 120 * 4, 0xFF204060);
        put32(quad_ram, color_address + 155 * 0x0C00 + 135 * 4, 0);
        submit_full(full_quad);
        CHECK(quad_fault.empty()); CHECK(quad_gpu.immediate_quad_fullscreen_raster_batches() == 2);
        CHECK(quad_gpu.immediate_quad_fullscreen_raster_triangles() == 4);
        CHECK(quad_gpu.immediate_quad_fullscreen_texture_samples() > 0);
        CHECK(quad_gpu.immediate_quad_fullscreen_raster_writes() > 0);
        CHECK(quad_gpu.immediate_quad_fullscreen_raster_output_hash() != 1469598103934665603ull);
        CHECK(surface_pixel(color_address, 120, 140) == artwork_blend_expected(0.49f));
        CHECK(surface_pixel(color_address, 135, 155) == 0xFF123456);
        emit_quad(0x0210, 0x03C8C000); put32(quad_ram, rs2_color_address + 140 * 0x0C00 + 120 * 4, 0xFF204060); submit_full(full_quad);
        CHECK(quad_fault.empty()); CHECK(quad_gpu.immediate_quad_fullscreen_raster_batches() == 3);
        CHECK(surface_pixel(rs2_color_address, 120, 140) == artwork_blend_expected(0.49f));
        for (uint32_t i = 0; i < 1024 * 1024; ++i) put32(quad_ram, full_texture_address + i * 4, 0xFF123456);
        put32(quad_ram, rs2_color_address + 140 * 0x0C00 + 120 * 4, 0); submit_full(full_quad, 0xFF808080);
        CHECK(quad_fault.empty()); CHECK(quad_gpu.immediate_quad_fullscreen_raster_batches() == 4);
        CHECK(surface_pixel(rs2_color_address, 120, 140) == 0xFF091A2B);
        auto full_variant = full_quad; full_variant[0][2] = 0.02f; submit_full(full_variant);
        CHECK(quad_gpu.immediate_quad_fullscreen_raster_batches() == 5);
        auto full_five = std::array<std::array<float, 4>, 5>{}; for (size_t i = 0; i < 4; ++i) full_five[i] = full_quad[i]; submit_full(full_five);
        CHECK(quad_gpu.immediate_quad_fullscreen_raster_batches() == 5);
        const auto full_candidates = quad_gpu.immediate_quad_fullscreen_candidates();
        emit_quad(0x0200, 0x00B4010E); emit_quad(0x0204, 0x00D7009E); submit_full(full_quad);
        CHECK(quad_gpu.immediate_quad_fullscreen_raster_batches() == 5);
        CHECK(quad_gpu.immediate_quad_fullscreen_candidates() == full_candidates + 1);
        CHECK(quad_gpu.last_immediate_quad_fullscreen_reject_mask() != 0);
        emit_quad(0x0200, 0x02D00000); emit_quad(0x0204, 0x01E00000); emit_quad(0x1B04, 0x0AA10628); submit_full(full_quad);
        CHECK(quad_gpu.immediate_quad_fullscreen_raster_batches() == 5); emit_quad(0x1B04, 0x0AA10629);
        emit_quad(0x1E20, 0); emit_quad(0x1E24, 0); submit_full(full_quad);
        CHECK(quad_gpu.immediate_quad_fullscreen_raster_batches() == 5);
        CHECK(quad_gpu.immediate_quad_fullscreen_candidates() == full_candidates + 1);
        emit_quad(0x1E20, 0x00DADADA); emit_quad(0x1E24, 0x00101010);
        // Aliased resolved DMA spans are deliberately recorder-only.
        quad_gpu.mmio_write(xlle::Nv2a::kBase + 0x700060, 4, 12); quad_gpu.mmio_write(xlle::Nv2a::kBase + 0x700064, 4, 0x800100D0);
        quad_gpu.mmio_write(xlle::Nv2a::kBase + 0x700D00, 4, 0x3D); quad_gpu.mmio_write(xlle::Nv2a::kBase + 0x700D04, 4, 0x07FFFFFF); quad_gpu.mmio_write(xlle::Nv2a::kBase + 0x700D08, 4, 0x04C0A000);
        const std::vector<uint8_t> full_alias_before(quad_ram.begin() + rs2_color_address, quad_ram.begin() + rs2_color_address + color_bytes);
        const auto full_alias_candidates = quad_gpu.immediate_quad_fullscreen_candidates();
        const auto full_alias_batches = quad_gpu.immediate_quad_fullscreen_raster_batches();
        const auto full_alias_writes = quad_gpu.immediate_quad_fullscreen_raster_writes();
        const auto full_alias_hash = quad_gpu.immediate_quad_fullscreen_raster_output_hash();
        emit_quad(0x0184, 12); submit_full(full_quad);
        CHECK(quad_fault.empty()); CHECK(quad_gpu.immediate_quad_fullscreen_raster_batches() == full_alias_batches);
        CHECK(quad_gpu.immediate_quad_fullscreen_candidates() == full_alias_candidates + 1);
        CHECK(quad_gpu.immediate_quad_fullscreen_raster_writes() == full_alias_writes);
        CHECK(quad_gpu.immediate_quad_fullscreen_raster_output_hash() == full_alias_hash);
        CHECK((quad_gpu.last_immediate_quad_fullscreen_raster_reject_mask() & (1u << 21)) != 0);
        CHECK(std::equal(full_alias_before.begin(), full_alias_before.end(), quad_ram.begin() + rs2_color_address));
        // A short DMA mapping faults before drawing and leaves the target intact.
        quad_gpu.mmio_write(xlle::Nv2a::kBase + 0x700068, 4, 13); quad_gpu.mmio_write(xlle::Nv2a::kBase + 0x70006C, 4, 0x800100E0);
        quad_gpu.mmio_write(xlle::Nv2a::kBase + 0x700E00, 4, 0x3D); quad_gpu.mmio_write(xlle::Nv2a::kBase + 0x700E04, 4, 0); quad_gpu.mmio_write(xlle::Nv2a::kBase + 0x700E08, 4, 0x04E03000);
        emit_quad(0x0184, 13); submit_full(full_quad);
        CHECK(quad_fault == "NV2A immediate-quad full-screen A8R8G8B8 texture DMA range is invalid");
        CHECK(quad_gpu.immediate_quad_fullscreen_raster_batches() == full_alias_batches);
        CHECK(std::equal(full_alias_before.begin(), full_alias_before.end(), quad_ram.begin() + rs2_color_address));
        CHECK(quad_gpu.immediate_quad_raster_batches() == full_old_a4_batches); CHECK(quad_gpu.immediate_quad_raster_writes() == full_old_a4_writes); CHECK(quad_gpu.immediate_quad_raster_output_hash() == full_old_a4_hash);
        CHECK(quad_gpu.immediate_quad_i8_raster_batches() == full_old_i8_batches); CHECK(quad_gpu.immediate_quad_i8_raster_writes() == full_old_i8_writes); CHECK(quad_gpu.immediate_quad_i8_raster_output_hash() == full_old_i8_hash);
        CHECK(quad_gpu.immediate_quad_a8_raster_batches() == full_old_a8_batches); CHECK(quad_gpu.immediate_quad_a8_raster_writes() == full_old_a8_writes); CHECK(quad_gpu.immediate_quad_a8_raster_output_hash() == full_old_a8_hash);
        CHECK(quad_gpu.immediate_quad_menu_i8_raster_batches() == full_old_menu_batches); CHECK(quad_gpu.immediate_quad_menu_i8_raster_writes() == full_old_menu_writes); CHECK(quad_gpu.immediate_quad_menu_i8_raster_output_hash() == full_old_menu_hash);
        CHECK(quad_gpu.immediate_quad_artwork_i8_raster_batches() == full_old_art_batches); CHECK(quad_gpu.immediate_quad_artwork_i8_raster_writes() == full_old_art_writes); CHECK(quad_gpu.immediate_quad_artwork_i8_raster_output_hash() == full_old_art_hash);
        CHECK(quad_gpu.immediate_quad_main_panel_raster_batches() == full_old_panel_batches); CHECK(quad_gpu.immediate_quad_main_panel_raster_writes() == full_old_panel_writes); CHECK(quad_gpu.immediate_quad_main_panel_raster_output_hash() == full_old_panel_hash);
    }

    // Root-cause NV2A fix: SET_VERTEX_DATA2F_M/2S/4UB/4S_M/4F_M must commit an immediate
    // vertex when their write completes attribute slot 0 (position), exactly like the
    // legacy SET_VERTEX4F family already did. Before the fix, these methods only stored
    // attribute values and never advanced immediate_vertices_, silently discarding all
    // geometry submitted through them -- the exact path the stock dashboard uses to
    // draw its menu. Exercise every family end to end: a non-zero attribute slot (9,
    // texcoord0) must never commit, and the write completing slot 0 must commit and
    // feed a real, rasterized triangle through the depth-only witnessed profile.
    {
        std::string fam_fault;
        xlle::Nv2a fam_gpu([&](const std::string& why) { fam_fault = why; });
        std::vector<uint8_t> fam_ram(0x20000, 0);
        fam_gpu.set_mem_access(
            [&](uint32_t addr, void* dst, size_t n) {
                if (addr > fam_ram.size() || n > fam_ram.size() - addr) return false;
                std::memcpy(dst, fam_ram.data() + addr, n);
                return true;
            },
            [&](uint32_t addr, const void* src, size_t n) {
                if (addr > fam_ram.size() || n > fam_ram.size() - addr) return false;
                std::memcpy(fam_ram.data() + addr, src, n);
                return true;
            });

        fam_gpu.mmio_write(xlle::Nv2a::kBase + 0x700008, 4, 1);
        fam_gpu.mmio_write(xlle::Nv2a::kBase + 0x70000C, 4, 0x80010020);
        fam_gpu.mmio_write(xlle::Nv2a::kBase + 0x700200, 4, 0x97);
        fam_gpu.mmio_write(xlle::Nv2a::kBase + 0x700010, 4, 2);
        fam_gpu.mmio_write(xlle::Nv2a::kBase + 0x700014, 4, 0x80010030);
        fam_gpu.mmio_write(xlle::Nv2a::kBase + 0x700018, 4, 3);
        fam_gpu.mmio_write(xlle::Nv2a::kBase + 0x70001C, 4, 0x80010040);
        fam_gpu.mmio_write(xlle::Nv2a::kBase + 0x700300, 4, 0x3D);
        fam_gpu.mmio_write(xlle::Nv2a::kBase + 0x700304, 4, 0x03FFFFFF);
        fam_gpu.mmio_write(xlle::Nv2a::kBase + 0x700308, 4, 0x00000000);
        fam_gpu.mmio_write(xlle::Nv2a::kBase + 0x700400, 4, 0x3D);
        fam_gpu.mmio_write(xlle::Nv2a::kBase + 0x700404, 4, 0x03FFFFFF);
        fam_gpu.mmio_write(xlle::Nv2a::kBase + 0x700408, 4, 0x00000000);
        fam_gpu.mmio_write(xlle::Nv2a::kBase + 0x700180, 4, 0x3D);
        fam_gpu.mmio_write(xlle::Nv2a::kBase + 0x700184, 4, 0xFFFF);
        fam_gpu.mmio_write(xlle::Nv2a::kBase + 0x700188, 4, 0x2000);
        fam_gpu.mmio_write(xlle::Nv2a::kBase + 0x002504, 4, 1);
        fam_gpu.mmio_write(xlle::Nv2a::kBase + 0x003204, 4, 0x100);
        fam_gpu.mmio_write(xlle::Nv2a::kBase + 0x00322C, 4, 0x18);
        fam_gpu.mmio_write(xlle::Nv2a::kBase + 0x003244, 4, 0);
        fam_gpu.mmio_write(xlle::Nv2a::kBase + 0x003220, 4, 1);
        fam_gpu.mmio_write(xlle::Nv2a::kBase + 0x003250, 4, 1);
        fam_gpu.mmio_write(xlle::Nv2a::kBase + 0x003200, 4, 1);

        constexpr uint32_t fam_pb = 0x2000;
        uint32_t fam_cursor = 0;
        auto emit_fam = [&](uint32_t method, uint32_t parameter) {
            put32(fam_ram, fam_pb + fam_cursor, (1u << 18) | method);
            put32(fam_ram, fam_pb + fam_cursor + 4, parameter);
            fam_cursor += 8;
        };
        auto kick_fam = [&]() {
            fam_gpu.mmio_write(xlle::Nv2a::kBase + 0x800040, 4, fam_cursor);
        };
        auto fam_float_bits = [](float value) {
            uint32_t bits = 0;
            std::memcpy(&bits, &value, sizeof(bits));
            return bits;
        };
        auto pack_s16 = [](int16_t lo, int16_t hi) {
            return (static_cast<uint32_t>(static_cast<uint16_t>(hi)) << 16) |
                   static_cast<uint32_t>(static_cast<uint16_t>(lo));
        };
        auto pack_ub4 = [](uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
            return static_cast<uint32_t>(r) | (static_cast<uint32_t>(g) << 8) |
                   (static_cast<uint32_t>(b) << 16) | (static_cast<uint32_t>(a) << 24);
        };

        // Depth-only witnessed profile (the same subset the dashboard's fixed-function
        // depth prepass above uses), reused here so every family's commit can be proven
        // with a real rasterized triangle rather than just a raw method counter.
        emit_fam(0x0000, 1);
        emit_fam(0x0194, 2);  // color DMA object
        emit_fam(0x0198, 3);  // zeta DMA object
        emit_fam(0x0200, 0x00400000);
        emit_fam(0x0204, 0x00400000);
        emit_fam(0x0208, 0x00000113);
        emit_fam(0x020C, 0x00800080);
        emit_fam(0x0214, 0x00008000);
        emit_fam(0x0290, 1);
        emit_fam(0x02C0, 0x003F0000);
        emit_fam(0x02E0, 0x003F0000);
        emit_fam(0x0300, 0);
        emit_fam(0x0304, 0);
        emit_fam(0x0308, 1);
        emit_fam(0x030C, 1);
        emit_fam(0x0310, 0);
        emit_fam(0x032C, 0);
        emit_fam(0x0338, 1);
        emit_fam(0x0354, 0x0203);
        emit_fam(0x0358, 0);
        emit_fam(0x035C, 1);
        emit_fam(0x037C, 0x1D01);
        emit_fam(0x0384, 0x00000000);
        emit_fam(0x0388, 0x41200000);
        emit_fam(0x038C, 0x1B02);
        emit_fam(0x0390, 0x1B02);
        emit_fam(0x0394, 0x00000000);
        emit_fam(0x0398, 0x477FFF00);
        emit_fam(0x039C, 0x0405);
        emit_fam(0x03A0, 0x0900);
        emit_fam(0x1D78, 1);
        emit_fam(0x1E9C, 0);
        emit_fam(0x0B00, 0x00000000);  // MOV oPos, v0 (final)
        emit_fam(0x0B04, 0x0020001B);
        emit_fam(0x0B08, 0x0836106C);
        emit_fam(0x0B0C, 0x2070F801);
        emit_fam(0x1EA0, 0);
        emit_fam(0x1E94, 2);
        kick_fam();
        CHECK(fam_fault.empty());

        auto reset_zeta = [&]() { std::memset(fam_ram.data() + 0x8000, 0xFF, 128 * 64); };

        // Real pixel-scale positions, matching the winding/sign of the proven
        // fixed-function depth-only draw above, for the families whose encoding
        // carries full-range values.
        const float px[3] = {10.0f, 30.0f, 20.0f};
        const float py[3] = {20.0f, 20.0f, 40.0f};
        const float pz = 100.75f;

        struct FamilySnapshot {
            uint64_t batches, verts, vbatches, prims, tris, rtris, rfrags, dwrites, methods;
        };
        auto snapshot = [&]() -> FamilySnapshot {
            return {fam_gpu.immediate_batches(), fam_gpu.immediate_vertices(),
                    fam_gpu.vertex_batches(),     fam_gpu.assembled_primitives(),
                    fam_gpu.assembled_triangles(), fam_gpu.rasterized_triangles(),
                    fam_gpu.rasterized_fragments(), fam_gpu.depth_writes(),
                    fam_gpu.immediate_methods()};
        };

        // --- SET_VERTEX_DATA2F_M (0x1880-0x1900): 2 methods/vertex land on slot 0. ---
        {
            reset_zeta();
            const FamilySnapshot s = snapshot();
            emit_fam(0x17FC, 5);
            for (int v = 0; v < 3; ++v) {
                emit_fam(0x18C8, fam_float_bits(0.5f));   // texcoord0 (slot9).x: no commit
                emit_fam(0x18CC, fam_float_bits(0.5f));   // texcoord0 (slot9).y: no commit
                emit_fam(0x1880, fam_float_bits(px[v]));  // position (slot0).x: no commit
                emit_fam(0x1884, fam_float_bits(py[v]));  // position (slot0).y: commits
            }
            emit_fam(0x17FC, 0);
            kick_fam();
            CHECK(fam_fault.empty());
            CHECK(fam_gpu.immediate_methods() == s.methods + 12);
            CHECK(fam_gpu.immediate_batches() == s.batches + 1);
            CHECK(fam_gpu.immediate_vertices() == s.verts + 3);
            CHECK(fam_gpu.vertex_batches() == s.vbatches + 1);
            CHECK(fam_gpu.assembled_primitives() == s.prims + 1);
            CHECK(fam_gpu.assembled_triangles() == s.tris + 1);
            CHECK(fam_gpu.rasterized_triangles() == s.rtris + 1);
            CHECK(fam_gpu.rasterized_fragments() > s.rfrags);
            CHECK(fam_gpu.depth_writes() > s.dwrites);
        }

        // --- SET_VERTEX_DATA2S (0x1900-0x1940): 1 method/vertex lands on slot 0. ---
        {
            reset_zeta();
            const FamilySnapshot s = snapshot();
            emit_fam(0x17FC, 5);
            for (int v = 0; v < 3; ++v) {
                emit_fam(0x1924, pack_s16(1, 1));  // texcoord0 (slot9): no commit
                emit_fam(0x1900,                   // position (slot0): commits
                         pack_s16(static_cast<int16_t>(px[v]),
                                  static_cast<int16_t>(py[v])));
            }
            emit_fam(0x17FC, 0);
            kick_fam();
            CHECK(fam_fault.empty());
            CHECK(fam_gpu.immediate_methods() == s.methods + 6);
            CHECK(fam_gpu.immediate_batches() == s.batches + 1);
            CHECK(fam_gpu.immediate_vertices() == s.verts + 3);
            CHECK(fam_gpu.vertex_batches() == s.vbatches + 1);
            CHECK(fam_gpu.assembled_primitives() == s.prims + 1);
            CHECK(fam_gpu.assembled_triangles() == s.tris + 1);
            CHECK(fam_gpu.rasterized_triangles() == s.rtris + 1);
            CHECK(fam_gpu.rasterized_fragments() > s.rfrags);
            CHECK(fam_gpu.depth_writes() > s.dwrites);
        }

        // --- SET_VERTEX_DATA4UB (0x1940-0x1980): 1 method/vertex lands on slot 0.
        // Its unorm-byte encoding only spans [0,1], so the resulting triangle is
        // sub-pixel; assert the method/commit/assembly accounting plus that the
        // rasterizer was actually reached (rather than fragment/depth-write counts,
        // which a sub-pixel triangle can legitimately leave at zero). ---
        {
            reset_zeta();
            const FamilySnapshot s = snapshot();
            emit_fam(0x17FC, 5);
            for (int v = 0; v < 3; ++v) {
                emit_fam(0x1964, pack_ub4(10, 20, 30, 40));  // texcoord0 (slot9): no commit
                const uint8_t bx = static_cast<uint8_t>(px[v] * 2.0f);
                const uint8_t by = static_cast<uint8_t>(py[v] * 2.0f);
                emit_fam(0x1940, pack_ub4(bx, by, 128, 255));  // position (slot0): commits
            }
            emit_fam(0x17FC, 0);
            kick_fam();
            CHECK(fam_fault.empty());
            CHECK(fam_gpu.immediate_methods() == s.methods + 6);
            CHECK(fam_gpu.immediate_batches() == s.batches + 1);
            CHECK(fam_gpu.immediate_vertices() == s.verts + 3);
            CHECK(fam_gpu.vertex_batches() == s.vbatches + 1);
            CHECK(fam_gpu.assembled_primitives() == s.prims + 1);
            CHECK(fam_gpu.assembled_triangles() == s.tris + 1);
            CHECK(fam_gpu.rasterized_triangles() == s.rtris + 1);
        }

        // --- SET_VERTEX_DATA4S_M (0x1980-0x1A00): 2 methods/vertex land on slot 0;
        // only the second (part 1, z/w) completes the vertex. ---
        {
            reset_zeta();
            const FamilySnapshot s = snapshot();
            emit_fam(0x17FC, 5);
            for (int v = 0; v < 3; ++v) {
                emit_fam(0x19C8, pack_s16(1, 1));  // texcoord0 (slot9) part 0: no commit
                emit_fam(0x19CC, pack_s16(1, 1));  // texcoord0 (slot9) part 1: no commit
                emit_fam(0x1980,                   // position (slot0) part 0 (x,y): no commit
                         pack_s16(static_cast<int16_t>(px[v]),
                                  static_cast<int16_t>(py[v])));
                emit_fam(0x1984, pack_s16(static_cast<int16_t>(pz), 1));  // part 1 (z,w): commits
            }
            emit_fam(0x17FC, 0);
            kick_fam();
            CHECK(fam_fault.empty());
            CHECK(fam_gpu.immediate_methods() == s.methods + 12);
            CHECK(fam_gpu.immediate_batches() == s.batches + 1);
            CHECK(fam_gpu.immediate_vertices() == s.verts + 3);
            CHECK(fam_gpu.vertex_batches() == s.vbatches + 1);
            CHECK(fam_gpu.assembled_primitives() == s.prims + 1);
            CHECK(fam_gpu.assembled_triangles() == s.tris + 1);
            CHECK(fam_gpu.rasterized_triangles() == s.rtris + 1);
            CHECK(fam_gpu.rasterized_fragments() > s.rfrags);
            CHECK(fam_gpu.depth_writes() > s.dwrites);
        }

        // --- SET_VERTEX_DATA4F_M (0x1A00-0x1B00): 4 methods/vertex land on slot 0;
        // only the fourth (component 3, w) completes the vertex. ---
        {
            reset_zeta();
            const FamilySnapshot s = snapshot();
            emit_fam(0x17FC, 5);
            for (int v = 0; v < 3; ++v) {
                emit_fam(0x1A90, fam_float_bits(0.5f));    // texcoord0 (slot9).x: no commit
                emit_fam(0x1A94, fam_float_bits(0.5f));    // texcoord0 (slot9).y: no commit
                emit_fam(0x1A98, fam_float_bits(0.0f));    // texcoord0 (slot9).z: no commit
                emit_fam(0x1A9C, fam_float_bits(1.0f));    // texcoord0 (slot9).w: no commit
                emit_fam(0x1A00, fam_float_bits(px[v]));   // position (slot0).x: no commit
                emit_fam(0x1A04, fam_float_bits(py[v]));   // position (slot0).y: no commit
                emit_fam(0x1A08, fam_float_bits(pz));      // position (slot0).z: no commit
                emit_fam(0x1A0C, fam_float_bits(1.0f));    // position (slot0).w: commits
            }
            emit_fam(0x17FC, 0);
            kick_fam();
            CHECK(fam_fault.empty());
            CHECK(fam_gpu.immediate_methods() == s.methods + 24);
            CHECK(fam_gpu.immediate_batches() == s.batches + 1);
            CHECK(fam_gpu.immediate_vertices() == s.verts + 3);
            CHECK(fam_gpu.vertex_batches() == s.vbatches + 1);
            CHECK(fam_gpu.assembled_primitives() == s.prims + 1);
            CHECK(fam_gpu.assembled_triangles() == s.tris + 1);
            CHECK(fam_gpu.rasterized_triangles() == s.rtris + 1);
            CHECK(fam_gpu.rasterized_fragments() > s.rfrags);
            CHECK(fam_gpu.depth_writes() > s.dwrites);
        }

        // Writing only a non-zero slot (texcoord0/slot9) through any of the families
        // must never commit: the method counter rises but no vertex, batch, or
        // primitive is ever produced.
        {
            reset_zeta();
            const FamilySnapshot s = snapshot();
            emit_fam(0x17FC, 5);
            for (int v = 0; v < 3; ++v) {
                emit_fam(0x18C8, fam_float_bits(0.25f));  // texcoord0 (slot9).x
                emit_fam(0x18CC, fam_float_bits(0.75f));  // texcoord0 (slot9).y
            }
            emit_fam(0x17FC, 0);
            kick_fam();
            CHECK(fam_fault.empty());
            CHECK(fam_gpu.immediate_methods() == s.methods + 6);
            CHECK(fam_gpu.immediate_batches() == s.batches);
            CHECK(fam_gpu.immediate_vertices() == s.verts);
            CHECK(fam_gpu.vertex_batches() == s.vbatches);
            CHECK(fam_gpu.assembled_primitives() == s.prims);
            CHECK(fam_gpu.assembled_triangles() == s.tris);
            CHECK(fam_gpu.rasterized_triangles() == s.rtris);
        }
    }

    // Generic NV2A register-combiner evaluator (rasterize_generic_combiner_batch).
    // Every Profile above is an exact-match fingerprint; anything else used to fall
    // through with executed=false and zero fragment writes. These tests exercise the
    // decoder/evaluator that now replaces that silent drop for the states none of the
    // profiles above claim.
    {
        std::string gc_fault;
        xlle::Nv2a gc_gpu([&](const std::string& why) { gc_fault = why; });
        std::vector<uint8_t> gc_ram(0x40000, 0);
        gc_gpu.set_mem_access(
            [&](uint32_t addr, void* dst, size_t n) {
                if (addr > gc_ram.size() || n > gc_ram.size() - addr) return false;
                std::memcpy(dst, gc_ram.data() + addr, n);
                return true;
            },
            [&](uint32_t addr, const void* src, size_t n) {
                if (addr > gc_ram.size() || n > gc_ram.size() - addr) return false;
                std::memcpy(gc_ram.data() + addr, src, n);
                return true;
            });

        gc_gpu.mmio_write(xlle::Nv2a::kBase + 0x700008, 4, 1);
        gc_gpu.mmio_write(xlle::Nv2a::kBase + 0x70000C, 4, 0x80010020);
        gc_gpu.mmio_write(xlle::Nv2a::kBase + 0x700200, 4, 0x97);
        gc_gpu.mmio_write(xlle::Nv2a::kBase + 0x700010, 4, 2);
        gc_gpu.mmio_write(xlle::Nv2a::kBase + 0x700014, 4, 0x80010030);
        gc_gpu.mmio_write(xlle::Nv2a::kBase + 0x700018, 4, 3);
        gc_gpu.mmio_write(xlle::Nv2a::kBase + 0x70001C, 4, 0x80010040);
        gc_gpu.mmio_write(xlle::Nv2a::kBase + 0x700300, 4, 0x3D);
        gc_gpu.mmio_write(xlle::Nv2a::kBase + 0x700304, 4, 0x03FFFFFF);
        gc_gpu.mmio_write(xlle::Nv2a::kBase + 0x700308, 4, 0x00000000);
        gc_gpu.mmio_write(xlle::Nv2a::kBase + 0x700400, 4, 0x3D);
        gc_gpu.mmio_write(xlle::Nv2a::kBase + 0x700404, 4, 0x03FFFFFF);
        gc_gpu.mmio_write(xlle::Nv2a::kBase + 0x700408, 4, 0x00000000);
        gc_gpu.mmio_write(xlle::Nv2a::kBase + 0x700180, 4, 0x3D);
        gc_gpu.mmio_write(xlle::Nv2a::kBase + 0x700184, 4, 0xFFFF);
        gc_gpu.mmio_write(xlle::Nv2a::kBase + 0x700188, 4, 0x2000);
        gc_gpu.mmio_write(xlle::Nv2a::kBase + 0x002504, 4, 1);
        gc_gpu.mmio_write(xlle::Nv2a::kBase + 0x003204, 4, 0x100);
        gc_gpu.mmio_write(xlle::Nv2a::kBase + 0x00322C, 4, 0x18);
        gc_gpu.mmio_write(xlle::Nv2a::kBase + 0x003244, 4, 0);
        gc_gpu.mmio_write(xlle::Nv2a::kBase + 0x003220, 4, 1);
        gc_gpu.mmio_write(xlle::Nv2a::kBase + 0x003250, 4, 1);
        gc_gpu.mmio_write(xlle::Nv2a::kBase + 0x003200, 4, 1);

        constexpr uint32_t gc_pb = 0x2000;
        uint32_t gc_cursor = 0;
        auto emit_gc = [&](uint32_t method, uint32_t parameter) {
            put32(gc_ram, gc_pb + gc_cursor, (1u << 18) | method);
            put32(gc_ram, gc_pb + gc_cursor + 4, parameter);
            gc_cursor += 8;
        };
        auto kick_gc = [&]() {
            gc_gpu.mmio_write(xlle::Nv2a::kBase + 0x800040, 4, gc_cursor);
        };
        auto gc_float_bits = [](float value) {
            uint32_t bits = 0;
            std::memcpy(&bits, &value, sizeof(bits));
            return bits;
        };
        auto gc_pack_ub4 = [](uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
            return static_cast<uint32_t>(r) | (static_cast<uint32_t>(g) << 8) |
                   (static_cast<uint32_t>(b) << 16) | (static_cast<uint32_t>(a) << 24);
        };
        // Fixed-function unlit transform with an identity 4x4 matrix and zero viewport
        // offset, so screen position == raw vertex position and diffuse == the raw
        // SET_VERTEX_DATA4UB write (vertex_attributes_[3]), exactly like the proven
        // radial-combiner/inline-linear-blit profiles' own transform setup above.
        emit_gc(0x0000, 1);
        emit_gc(0x0194, 2);
        emit_gc(0x0198, 3);
        const uint32_t gc_identity[] = {
            0x3F800000, 0, 0, 0,
            0, 0x3F800000, 0, 0,
            0, 0, 0x3F800000, 0,
            0, 0, 0, 0x3F800000,
        };
        for (uint32_t i = 0; i < 16; ++i) emit_gc(0x0680 + i * 4, gc_identity[i]);
        emit_gc(0x0A20, 0);
        emit_gc(0x0A24, 0);
        emit_gc(0x0294, 0x00020001);  // LIGHT_CONTROL -> unlit_subset index 5
        emit_gc(0x0314, 0);           // lighting disabled
        emit_gc(0x03B8, 0);           // specular disabled
        emit_gc(0x1E94, 0);           // fixed-function transform mode
        kick_gc();
        CHECK(gc_fault.empty());

        auto submit_triangle = [&](float x0, float y0, float x1, float y1,
                                   float x2, float y2, float z, float w,
                                   uint32_t rgba) {
            emit_gc(0x17FC, 5);
            for (auto xy : {std::pair{x0, y0}, std::pair{x1, y1}, std::pair{x2, y2}}) {
                emit_gc(0x1518, gc_float_bits(xy.first));
                emit_gc(0x151C, gc_float_bits(xy.second));
                emit_gc(0x1520, gc_float_bits(z));
                emit_gc(0x194C, rgba);  // SET_VERTEX_DATA4UB slot3 (diffuse)
                emit_gc(0x1524, gc_float_bits(w));  // completes slot0 -> commits
            }
            emit_gc(0x17FC, 0);
            kick_gc();
        };

        // --- (a) Exact dashboard fingerprint: 1-stage combiner, shader=00000000/
        // 00210000, comb=00000001, tex=0, textureless. Reproduces the raster-state
        // that was silently recorder-only (executed=false, zero writes) before this
        // evaluator existed; see build/probe-stockdash-vfix-11.5b.stdout.log RS0-RS7. ---
        {
            emit_gc(0x0200, 0x00400000);  // clip 64 wide, x=0
            emit_gc(0x0204, 0x00400000);  // clip 64 tall, y=0
            emit_gc(0x0208, 0x00000128);  // pitch A8R8G8B8 + Z24S8
            emit_gc(0x020C, 0x01000100);  // pitch 256 (color and zeta)
            emit_gc(0x0210, 0x00010000);  // color offset
            emit_gc(0x0214, 0x00020000);  // zeta offset
            emit_gc(0x0290, 0x00100001);  // control0
            emit_gc(0x02C0, 0x003F0000);  // window horizontal
            emit_gc(0x02E0, 0x003F0000);  // window vertical
            emit_gc(0x033C, 0x0207);      // alpha ALWAYS
            emit_gc(0x0340, 0);
            emit_gc(0x0344, 0x0302);      // blend SRC_ALPHA
            emit_gc(0x0348, 0x0303);      // blend ONE_MINUS_SRC_ALPHA
            emit_gc(0x0350, 0x8006);      // FUNC_ADD
            emit_gc(0x0354, 0x0203);      // depth LEQUAL
            emit_gc(0x0358, 0x01010101);  // color mask
            emit_gc(0x035C, 0);           // depth writes disabled (matches the log)
            emit_gc(0x037C, 0x1D01);      // smooth shading
            emit_gc(0x038C, 0x1B02);      // front fill
            emit_gc(0x0390, 0x1B02);      // back fill
            emit_gc(0x0394, 0);           // clip min
            emit_gc(0x0398, 0x4B7FFFFF);  // clip max (16777215.0f)
            emit_gc(0x039C, 0x0405);      // cull back
            emit_gc(0x03A0, 0x0900);      // CW front
            emit_gc(0x1D78, 1);           // zminmax control
            emit_gc(0x1E70, 0);           // shader stage program
            emit_gc(0x1E74, 0);           // dot-RGB mapping
            emit_gc(0x1E78, 0x00210000);  // shader other-stage input
            emit_gc(0x1E60, 0x00000001);  // combiner control: 1 stage
            emit_gc(0x0AC0, 0x04200000);  // ColorIcw0: D=PRIMARY_COLOR identity, C=ZERO invert
            emit_gc(0x0260, 0x14200000);  // AlphaIcw0: D=PRIMARY_COLOR.a identity, C=ZERO invert
            emit_gc(0x1E40, 0x00000C00);  // ColorOcw0: SUM -> R0
            emit_gc(0x0AA0, 0x00000C00);  // AlphaOcw0: SUM -> R0
            emit_gc(0x0288, 0x0000000C);  // SpecularFog0: identity final combiner
            emit_gc(0x028C, 0x00001C80);  // SpecularFog1: identity final combiner
            emit_gc(0x1E20, 0);           // FinalFactor0
            emit_gc(0x1E24, 0);           // FinalFactor1
            for (uint32_t offset = 0; offset < 256u * 64u; offset += 4)
                put32(gc_ram, 0x20000 + offset, 0xFFFFFFFFu);  // zeta: LEQUAL always passes
            std::memset(gc_ram.data() + 0x10000, 0, 256 * 64);

            const uint64_t before_generic_batches = gc_gpu.generic_combiner_batches();
            const uint64_t before_generic_executed =
                gc_gpu.generic_combiner_executed_batches();
            const uint64_t before_generic_writes = gc_gpu.generic_combiner_writes();
            submit_triangle(0.0f, 0.0f, 40.0f, 0.0f, 0.0f, 40.0f, 50.0f, 1.0f,
                            gc_pack_ub4(0x11, 0x22, 0x33, 0xCC));
            CHECK(gc_fault.empty());
            CHECK(gc_gpu.generic_combiner_batches() == before_generic_batches + 1);
            CHECK(gc_gpu.generic_combiner_executed_batches() ==
                  before_generic_executed + 1);
            CHECK(gc_gpu.generic_combiner_fragments() > 0);
            CHECK(gc_gpu.generic_combiner_writes() > before_generic_writes);
            const uint32_t gc_a_offset = 5 * 256 + 5 * 4;
            CHECK(get32(gc_ram, 0x10000 + gc_a_offset) != 0);
            CHECK(gc_gpu.last_generic_combiner_raster_address() >= 0x10000);
            CHECK(gc_gpu.last_generic_combiner_raster_address() < 0x10000 + 256u * 64u);
            CHECK(gc_gpu.generic_combiner_output_hash() != 1469598103934665603ull);
            // Depth writes stayed off (DepthMask==0, matching the witnessed log).
            CHECK(get32(gc_ram, 0x20000 + gc_a_offset) == 0xFFFFFFFFu);
        }

        // --- (b) Two-stage combiner: CONSTANT_COLOR0 (Factor0) modulated by
        // UNSIGNED_INVERT, then multiplied by PRIMARY_COLOR (diffuse) in stage 1.
        // Factor0 = 0x00404040 (R=G=B=0x40=64), diffuse = (R=0x80,G=0x60,B=0x40,A=0xFF).
        // Stage 0: R0.rgb = UNSIGNED_INVERT(Factor0) = (255-64)/255 = 191/255 for all
        // three channels (A = CONSTANT_COLOR0 invert, B = ZERO invert = 1, AB = A*B).
        // R0.a = 1 (A = B = ZERO invert = 1, AB = 1).
        // Stage 1: R0.rgb *= diffuse.rgb (componentwise), R0.a *= diffuse.a.
        //   R = floor(191*128/255 + 0.5) = floor(96.3745) = 96 = 0x60
        //   G = floor(191*96/255 + 0.5)  = floor(72.4059) = 72 = 0x48
        //   B = floor(191*64/255 + 0.5)  = floor(48.4373) = 48 = 0x30
        //   A = 1*1 = 1 -> 255 = 0xFF
        // Final combiner is the same identity passthrough as (a), blend disabled
        // (ONE/ZERO) and depth/alpha set to ALWAYS with depth writes off, so no zeta
        // surface is needed at all: expected pixel is exactly 0xFF604830. ---
        {
            emit_gc(0x0200, 0x00400000);
            emit_gc(0x0204, 0x00400000);
            emit_gc(0x0208, 0x00000108);  // pitch A8R8G8B8, no zeta component
            emit_gc(0x020C, 0x01000100);
            emit_gc(0x0210, 0x00010000);
            emit_gc(0x0290, 0x00100001);
            emit_gc(0x02C0, 0x003F0000);
            emit_gc(0x02E0, 0x003F0000);
            emit_gc(0x033C, 0x0207);      // alpha ALWAYS
            emit_gc(0x0340, 0);
            emit_gc(0x0344, 1);           // blend ONE
            emit_gc(0x0348, 0);           // blend ZERO (disabled)
            emit_gc(0x0350, 0x8006);
            emit_gc(0x0354, 0x0207);      // depth ALWAYS
            emit_gc(0x0358, 0x01010101);
            emit_gc(0x035C, 0);           // depth writes off -> uses_depth==false
            emit_gc(0x037C, 0x1D01);
            emit_gc(0x038C, 0x1B02);
            emit_gc(0x0390, 0x1B02);
            emit_gc(0x0394, 0);
            emit_gc(0x0398, 0x4B7FFFFF);
            emit_gc(0x039C, 0x0405);
            emit_gc(0x03A0, 0x0900);
            emit_gc(0x1D78, 1);
            emit_gc(0x1E70, 0);
            emit_gc(0x1E74, 0);
            emit_gc(0x1E78, 0);
            emit_gc(0x1E60, 0x00000002);  // combiner control: 2 stages
            // ICW byte order is A(bits31:24)/B(23:16)/C(15:8)/D(7:0) (matching xemu's
            // parse_combiner_inputs() exactly); AB is the pair that actually reaches
            // ab_dst below, so the meaningful inputs live in A/B, not C/D.
            emit_gc(0x0AC0, 0x21200000);  // stage0 ColorIcw: A=C0 invert, B=ZERO invert
            emit_gc(0x0260, 0x20200000);  // stage0 AlphaIcw: A=ZERO invert, B=ZERO invert
            emit_gc(0x1E40, 0x000000C0);  // stage0 ColorOcw: AB -> R0
            emit_gc(0x0AA0, 0x000000C0);  // stage0 AlphaOcw: AB -> R0
            emit_gc(0x0AC4, 0x0C040000);  // stage1 ColorIcw: A=R0, B=PRIMARY_COLOR
            emit_gc(0x0264, 0x1C140000);  // stage1 AlphaIcw: A=R0.a, B=PRIMARY_COLOR.a
            emit_gc(0x1E44, 0x000000C0);  // stage1 ColorOcw: AB -> R0
            emit_gc(0x0AA4, 0x000000C0);  // stage1 AlphaOcw: AB -> R0
            emit_gc(0x0A60, 0x00404040);  // Factor0 (stage0): R=G=B=0x40
            emit_gc(0x0288, 0x0000000C);
            emit_gc(0x028C, 0x00001C80);
            emit_gc(0x1E20, 0);
            emit_gc(0x1E24, 0);
            std::memset(gc_ram.data() + 0x10000, 0, 256 * 64);

            submit_triangle(0.0f, 0.0f, 40.0f, 0.0f, 0.0f, 40.0f, 10.0f, 1.0f,
                            gc_pack_ub4(0x80, 0x60, 0x40, 0xFF));
            CHECK(gc_fault.empty());
            const uint32_t gc_b_offset = 5 * 256 + 5 * 4;
            CHECK(get32(gc_ram, 0x10000 + gc_b_offset) == 0xFF604830u);
        }

        // --- (c) Unsupported feature: same fingerprint as (a), but the final
        // combiner's D input references the FOG register (3) instead of R0. Per
        // xemu's psh.c, FOG is a real register (pFog = vec4(fogColor.rgb,
        // clamp(vtxFog,0,1))) that this evaluator does not implement -- no witnessed
        // profile above implements fog either, and this evaluator has no FOG_COLOR
        // latch or per-vertex fog coordinate. register_supported() already rejects
        // register 3 unconditionally, so a final combiner that references it must
        // land in the kFinalCombiner skip-reason census, not silently drop: no
        // fragment/color writes, executed stays false, and the skip counter advances
        // by exactly one. (NV097_SET_SPECULAR_FOG_FACTOR / FinalFactor0/1 is NOT fog
        // by itself -- it is the final combiner's own CONSTANT_COLOR0/1 source, now
        // implemented and exercised by test (k) below.) ---
        {
            emit_gc(0x0200, 0x00400000);
            emit_gc(0x0204, 0x00400000);
            emit_gc(0x0208, 0x00000128);
            emit_gc(0x020C, 0x01000100);
            emit_gc(0x0210, 0x00010000);
            emit_gc(0x0214, 0x00020000);
            emit_gc(0x0290, 0x00100001);
            emit_gc(0x02C0, 0x003F0000);
            emit_gc(0x02E0, 0x003F0000);
            emit_gc(0x033C, 0x0207);
            emit_gc(0x0340, 0);
            emit_gc(0x0344, 0x0302);
            emit_gc(0x0348, 0x0303);
            emit_gc(0x0350, 0x8006);
            emit_gc(0x0354, 0x0203);
            emit_gc(0x0358, 0x01010101);
            emit_gc(0x035C, 0);
            emit_gc(0x037C, 0x1D01);
            emit_gc(0x038C, 0x1B02);
            emit_gc(0x0390, 0x1B02);
            emit_gc(0x0394, 0);
            emit_gc(0x0398, 0x4B7FFFFF);
            emit_gc(0x039C, 0x0405);
            emit_gc(0x03A0, 0x0900);
            emit_gc(0x1D78, 1);
            emit_gc(0x1E70, 0);
            emit_gc(0x1E74, 0);
            emit_gc(0x1E78, 0x00210000);
            emit_gc(0x1E60, 0x00000001);
            emit_gc(0x0AC0, 0x04200000);
            emit_gc(0x0260, 0x14200000);
            emit_gc(0x1E40, 0x00000C00);
            emit_gc(0x0AA0, 0x00000C00);
            emit_gc(0x0288, 0x00000003);  // SpecularFogCW0: D=FOG (register 3, unsupported)
            emit_gc(0x028C, 0x00001C80);  // SpecularFogCW1: G=R0.alpha (unused: skips first)
            emit_gc(0x1E20, 0);
            emit_gc(0x1E24, 0);
            std::memset(gc_ram.data() + 0x10000, 0xAB, 256 * 64);
            const std::vector<uint8_t> before_pixels(
                gc_ram.begin() + 0x10000, gc_ram.begin() + 0x10000 + 256 * 64);

            const size_t final_combiner_reason =
                static_cast<size_t>(xlle::Nv2a::GenericSkipReason::kFinalCombiner);
            const uint64_t before_skip =
                gc_gpu.generic_skip_reason_batches(final_combiner_reason);
            const uint64_t before_generic_batches = gc_gpu.generic_combiner_batches();
            const uint64_t before_generic_executed =
                gc_gpu.generic_combiner_executed_batches();
            submit_triangle(0.0f, 0.0f, 40.0f, 0.0f, 0.0f, 40.0f, 50.0f, 1.0f,
                            gc_pack_ub4(0x11, 0x22, 0x33, 0xCC));
            CHECK(gc_fault.empty());
            CHECK(gc_gpu.generic_skip_reason_batches(final_combiner_reason) ==
                  before_skip + 1);
            CHECK(gc_gpu.generic_combiner_batches() == before_generic_batches);
            CHECK(gc_gpu.generic_combiner_executed_batches() ==
                  before_generic_executed);
            CHECK(std::equal(before_pixels.begin(), before_pixels.end(),
                             gc_ram.begin() + 0x10000));
        }

        // --- (d) Exact witnessed stock-dashboard fingerprint (coordinator-supplied
        // RS0-RS15 evidence): sfmt=00000124 (X8R8G8B8 color / Z24S8 zeta, pitch-linear,
        // no AA -- color_format=4, not 8), en=00000016 (blend+cull+dither enabled;
        // alpha test/depth test are OFF via their own enable bits, so AlphaFunc==0207
        // and DepthFunc==0203 are hardware-unread), blend=302/303/8006, comb=00000001,
        // shader=00000000/00210000, tex=0, surface color offset=03aa4000. Before the
        // per-feature semantic gate, every one of these 12,106 witnessed batches hit
        // kUnsupportedColorFormat (color_format 4 was never accepted). The witnessed
        // pitch word 0x00401400 packs an internally-consistent color pitch (0x1400 ==
        // 1280 * 4bpp, matching clip width 1280) with a zeta pitch (0x0040) that this
        // evaluator never validates here because depth test/write are both disabled
        // for this exact state, so hardware -- and this evaluator -- never touch zeta. ---
        {
            std::string d_fault;
            xlle::Nv2a d_gpu([&](const std::string& why) { d_fault = why; });
            std::vector<uint8_t> d_ram(0x04000000, 0);
            d_gpu.set_mem_access(
                [&](uint32_t addr, void* dst, size_t n) {
                    if (addr > d_ram.size() || n > d_ram.size() - addr) return false;
                    std::memcpy(dst, d_ram.data() + addr, n);
                    return true;
                },
                [&](uint32_t addr, const void* src, size_t n) {
                    if (addr > d_ram.size() || n > d_ram.size() - addr) return false;
                    std::memcpy(d_ram.data() + addr, src, n);
                    return true;
                });
            d_gpu.mmio_write(xlle::Nv2a::kBase + 0x700008, 4, 1);
            d_gpu.mmio_write(xlle::Nv2a::kBase + 0x70000C, 4, 0x80010020);
            d_gpu.mmio_write(xlle::Nv2a::kBase + 0x700200, 4, 0x97);
            d_gpu.mmio_write(xlle::Nv2a::kBase + 0x700010, 4, 2);
            d_gpu.mmio_write(xlle::Nv2a::kBase + 0x700014, 4, 0x80010030);
            d_gpu.mmio_write(xlle::Nv2a::kBase + 0x700300, 4, 0x3D);
            d_gpu.mmio_write(xlle::Nv2a::kBase + 0x700304, 4, 0x03FFFFFF);
            d_gpu.mmio_write(xlle::Nv2a::kBase + 0x700308, 4, 0x00000000);
            d_gpu.mmio_write(xlle::Nv2a::kBase + 0x700180, 4, 0x3D);
            d_gpu.mmio_write(xlle::Nv2a::kBase + 0x700184, 4, 0xFFFF);
            d_gpu.mmio_write(xlle::Nv2a::kBase + 0x700188, 4, 0x2000);
            d_gpu.mmio_write(xlle::Nv2a::kBase + 0x002504, 4, 1);
            d_gpu.mmio_write(xlle::Nv2a::kBase + 0x003204, 4, 0x100);
            d_gpu.mmio_write(xlle::Nv2a::kBase + 0x00322C, 4, 0x18);
            d_gpu.mmio_write(xlle::Nv2a::kBase + 0x003244, 4, 0);
            d_gpu.mmio_write(xlle::Nv2a::kBase + 0x003220, 4, 1);
            d_gpu.mmio_write(xlle::Nv2a::kBase + 0x003250, 4, 1);
            d_gpu.mmio_write(xlle::Nv2a::kBase + 0x003200, 4, 1);

            constexpr uint32_t d_pb = 0x2000;
            uint32_t d_cursor = 0;
            auto emit_d = [&](uint32_t method, uint32_t parameter) {
                put32(d_ram, d_pb + d_cursor, (1u << 18) | method);
                put32(d_ram, d_pb + d_cursor + 4, parameter);
                d_cursor += 8;
            };
            auto kick_d = [&]() {
                d_gpu.mmio_write(xlle::Nv2a::kBase + 0x800040, 4, d_cursor);
            };
            auto d_float_bits = [](float value) {
                uint32_t bits = 0;
                std::memcpy(&bits, &value, sizeof(bits));
                return bits;
            };
            auto d_pack_ub4 = [](uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
                return static_cast<uint32_t>(r) | (static_cast<uint32_t>(g) << 8) |
                       (static_cast<uint32_t>(b) << 16) | (static_cast<uint32_t>(a) << 24);
            };

            emit_d(0x0000, 1);
            emit_d(0x0194, 2);
            const uint32_t d_identity[] = {
                0x3F800000, 0, 0, 0,
                0, 0x3F800000, 0, 0,
                0, 0, 0x3F800000, 0,
                0, 0, 0, 0x3F800000,
            };
            for (uint32_t i = 0; i < 16; ++i) emit_d(0x0680 + i * 4, d_identity[i]);
            emit_d(0x0A20, 0);
            emit_d(0x0A24, 0);
            emit_d(0x0294, 0x00020001);
            emit_d(0x0314, 0);
            emit_d(0x03B8, 0);
            emit_d(0x1E94, 0);

            emit_d(0x0200, 0x05000000);   // clip: width=1280, x=0
            emit_d(0x0204, 0x03C00000);   // clip: height=960, y=0
            emit_d(0x0208, 0x00000124);   // sfmt: X8R8G8B8 + Z24S8, pitch-linear, no AA
            emit_d(0x020C, 0x00401400);   // pitch: color=0x1400, zeta=0x0040 (unused)
            emit_d(0x0210, 0x03AA4000);   // color offset -- the witnessed surf address
            emit_d(0x0290, 0x00100001);   // control0
            emit_d(0x02C0, 0x04FF0000);   // window horizontal: 0..1279
            emit_d(0x02E0, 0x03BF0000);   // window vertical: 0..959
            emit_d(0x0300, 0);            // alpha test disabled (en bit0==0)
            emit_d(0x0304, 1);            // blend enabled (en bit1)
            emit_d(0x0308, 1);            // cull enabled (en bit2)
            emit_d(0x030C, 0);            // depth test disabled (en bit3==0)
            emit_d(0x0310, 1);            // dither enabled (inert here)
            emit_d(0x033C, 0x0207);       // AlphaFunc ALWAYS (unread: test disabled)
            emit_d(0x0340, 0);
            emit_d(0x0344, 0x0302);       // blend SRC_ALPHA
            emit_d(0x0348, 0x0303);       // blend ONE_MINUS_SRC_ALPHA
            emit_d(0x0350, 0x8006);       // FUNC_ADD
            emit_d(0x0354, 0x0203);       // DepthFunc LEQUAL (unread: test disabled)
            emit_d(0x0358, 0x01010101);   // color mask: all channels enabled
            emit_d(0x035C, 0);            // depth writes disabled
            emit_d(0x037C, 0x1D01);       // smooth shading
            emit_d(0x038C, 0x1B02);       // front fill
            emit_d(0x0390, 0x1B02);       // back fill
            emit_d(0x0394, 0);
            emit_d(0x0398, 0x4B7FFFFF);
            emit_d(0x039C, 0x0405);       // cull back
            emit_d(0x03A0, 0x0900);       // CW front
            emit_d(0x1D78, 1);
            emit_d(0x1E70, 0);            // shader stage program
            emit_d(0x1E74, 0);
            emit_d(0x1E78, 0x00210000);   // shader other-stage input
            emit_d(0x1E60, 0x00000001);   // combiner control: 1 stage
            emit_d(0x0AC0, 0x04200000);   // ColorIcw0: D=PRIMARY_COLOR, C=ZERO invert
            emit_d(0x0260, 0x14200000);   // AlphaIcw0: D=PRIMARY_COLOR.a, C=ZERO invert
            emit_d(0x1E40, 0x00000C00);   // ColorOcw0: SUM -> R0
            emit_d(0x0AA0, 0x00000C00);   // AlphaOcw0: SUM -> R0
            emit_d(0x0288, 0x0000000C);   // SpecularFog0: identity final combiner
            emit_d(0x028C, 0x00001C80);   // SpecularFog1: identity final combiner
            emit_d(0x1E20, 0);
            emit_d(0x1E24, 0);

            auto submit_d_triangle = [&](float x0, float y0, float x1, float y1,
                                        float x2, float y2, float z, float w,
                                        uint32_t rgba) {
                emit_d(0x17FC, 5);
                for (auto xy : {std::pair{x0, y0}, std::pair{x1, y1}, std::pair{x2, y2}}) {
                    emit_d(0x1518, d_float_bits(xy.first));
                    emit_d(0x151C, d_float_bits(xy.second));
                    emit_d(0x1520, d_float_bits(z));
                    emit_d(0x194C, rgba);
                    emit_d(0x1524, d_float_bits(w));
                }
                emit_d(0x17FC, 0);
                kick_d();
            };

            submit_d_triangle(50.0f, 50.0f, 150.0f, 50.0f, 50.0f, 150.0f, 100.0f, 1.0f,
                              d_pack_ub4(0x40, 0x60, 0x90, 0x80));
            CHECK(d_fault.empty());
            CHECK(d_gpu.generic_combiner_batches() == 1);
            CHECK(d_gpu.generic_combiner_executed_batches() == 1);
            CHECK(d_gpu.generic_combiner_fragments() > 0);
            CHECK(d_gpu.generic_combiner_writes() > 0);
            const uint32_t d_offset = 90u * 5120u + 90u * 4u;
            CHECK(get32(d_ram, 0x03AA4000u + d_offset) != 0);
            CHECK(d_gpu.last_generic_combiner_raster_address() >= 0x03AA4000u);
            CHECK(d_gpu.last_generic_combiner_raster_address() <
                  0x03AA4000u + 5120u * 960u);
            // X8R8G8B8: the stored alpha byte is always forced to 0xFF, never the
            // combiner's computed alpha (there is no alpha channel on real hardware).
            CHECK((get32(d_ram, 0x03AA4000u + d_offset) >> 24) == 0xFFu);
        }

        // --- (e) TEXTURE0 modulated by diffuse (PROJECT2D/linear sampler,
        // ShaderStageProgram==1, matching kInlineLinearBlitColor's own texture layout).
        // Stage0: A=TEXTURE0 identity, B=PRIMARY_COLOR identity, AB=TEXTURE0*diffuse
        // (componentwise) -> R0; alpha: A=PRIMARY_COLOR.a identity, B=ZERO invert=1,
        // AB=diffuse.a*1=diffuse.a -> R0.a (texture alpha is never available in the
        // linear sampling path, matching every witnessed profile's own AlphaIcw, which
        // always substitutes PRIMARY_COLOR.a there instead of reading the texture).
        // A 4x4 texture is sampled dead-center at texel (2,2) (s=2.5,t=2.5, q=1) so
        // bilinear filtering reduces to that exact texel with no blending:
        //   texel(2,2) = (R=0xC0,G=0xA0,B=0x80), diffuse = (R=0x30,G=0x50,B=0x70,A=0xFF)
        //   R = floor(0xC0*0x30/255 + 0.5) = floor(36.14 + 0.5) = 36  = 0x24
        //   G = floor(0xA0*0x50/255 + 0.5) = floor(50.20 + 0.5) = 50  = 0x32
        //   B = floor(0x80*0x70/255 + 0.5) = floor(56.22 + 0.5) = 56  = 0x38
        //   A = diffuse.a = 0xFF
        // Expected pixel: 0xFF243238. Blend/alpha-test/depth-test/cull all disabled
        // (enables==0) so this is a straight overwrite. ---
        {
            std::string e_fault;
            xlle::Nv2a e_gpu([&](const std::string& why) { e_fault = why; });
            std::vector<uint8_t> e_ram(0x40000, 0);
            e_gpu.set_mem_access(
                [&](uint32_t addr, void* dst, size_t n) {
                    if (addr > e_ram.size() || n > e_ram.size() - addr) return false;
                    std::memcpy(dst, e_ram.data() + addr, n);
                    return true;
                },
                [&](uint32_t addr, const void* src, size_t n) {
                    if (addr > e_ram.size() || n > e_ram.size() - addr) return false;
                    std::memcpy(e_ram.data() + addr, src, n);
                    return true;
                });
            e_gpu.mmio_write(xlle::Nv2a::kBase + 0x700008, 4, 1);
            e_gpu.mmio_write(xlle::Nv2a::kBase + 0x70000C, 4, 0x80010020);
            e_gpu.mmio_write(xlle::Nv2a::kBase + 0x700200, 4, 0x97);
            e_gpu.mmio_write(xlle::Nv2a::kBase + 0x700010, 4, 2);
            e_gpu.mmio_write(xlle::Nv2a::kBase + 0x700014, 4, 0x80010030);
            e_gpu.mmio_write(xlle::Nv2a::kBase + 0x700300, 4, 0x3D);
            e_gpu.mmio_write(xlle::Nv2a::kBase + 0x700304, 4, 0x03FFFFFF);
            e_gpu.mmio_write(xlle::Nv2a::kBase + 0x700308, 4, 0x00000000);
            // Handle 4: texture DMA B object at PRAMIN instance 0x50.
            e_gpu.mmio_write(xlle::Nv2a::kBase + 0x700020, 4, 4);
            e_gpu.mmio_write(xlle::Nv2a::kBase + 0x700024, 4, 0x80010050);
            e_gpu.mmio_write(xlle::Nv2a::kBase + 0x700500, 4, 0x3D);
            e_gpu.mmio_write(xlle::Nv2a::kBase + 0x700504, 4, 0x03FFFFFF);
            e_gpu.mmio_write(xlle::Nv2a::kBase + 0x700508, 4, 0x00000000);
            e_gpu.mmio_write(xlle::Nv2a::kBase + 0x700180, 4, 0x3D);
            e_gpu.mmio_write(xlle::Nv2a::kBase + 0x700184, 4, 0xFFFF);
            e_gpu.mmio_write(xlle::Nv2a::kBase + 0x700188, 4, 0x2000);
            e_gpu.mmio_write(xlle::Nv2a::kBase + 0x002504, 4, 1);
            e_gpu.mmio_write(xlle::Nv2a::kBase + 0x003204, 4, 0x100);
            e_gpu.mmio_write(xlle::Nv2a::kBase + 0x00322C, 4, 0x18);
            e_gpu.mmio_write(xlle::Nv2a::kBase + 0x003244, 4, 0);
            e_gpu.mmio_write(xlle::Nv2a::kBase + 0x003220, 4, 1);
            e_gpu.mmio_write(xlle::Nv2a::kBase + 0x003250, 4, 1);
            e_gpu.mmio_write(xlle::Nv2a::kBase + 0x003200, 4, 1);

            constexpr uint32_t e_pb = 0x2000;
            uint32_t e_cursor = 0;
            auto emit_e = [&](uint32_t method, uint32_t parameter) {
                put32(e_ram, e_pb + e_cursor, (1u << 18) | method);
                put32(e_ram, e_pb + e_cursor + 4, parameter);
                e_cursor += 8;
            };
            auto kick_e = [&]() {
                e_gpu.mmio_write(xlle::Nv2a::kBase + 0x800040, 4, e_cursor);
            };
            auto e_float_bits = [](float value) {
                uint32_t bits = 0;
                std::memcpy(&bits, &value, sizeof(bits));
                return bits;
            };
            auto e_pack_ub4 = [](uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
                return static_cast<uint32_t>(r) | (static_cast<uint32_t>(g) << 8) |
                       (static_cast<uint32_t>(b) << 16) | (static_cast<uint32_t>(a) << 24);
            };

            emit_e(0x0000, 1);
            emit_e(0x0194, 2);
            emit_e(0x0188, 4);  // texture DMA B
            const uint32_t e_identity[] = {
                0x3F800000, 0, 0, 0,
                0, 0x3F800000, 0, 0,
                0, 0, 0x3F800000, 0,
                0, 0, 0, 0x3F800000,
            };
            for (uint32_t i = 0; i < 16; ++i) emit_e(0x0680 + i * 4, e_identity[i]);
            emit_e(0x0A20, 0);
            emit_e(0x0A24, 0);
            emit_e(0x0294, 0x00020001);
            emit_e(0x0314, 0);
            emit_e(0x03B8, 0);
            emit_e(0x1E94, 0);

            // 4x4 A8R8G8B8 texture, all zero except texel(2,2).
            std::vector<uint8_t> e_texture(4 * 4 * 4, 0);
            put32(e_texture, (2u * 4u + 2u) * 4u, 0x11C0A080u);
            std::memcpy(e_ram.data() + 0x20000, e_texture.data(), e_texture.size());

            emit_e(0x0200, 0x00080000);   // clip: 8 wide, x=0
            emit_e(0x0204, 0x00080000);   // clip: 8 tall, y=0
            emit_e(0x0208, 0x00000108);   // A8R8G8B8, pitch-linear, no zeta
            emit_e(0x020C, 0x00200020);   // pitch=32
            emit_e(0x0210, 0x00010000);   // color offset
            emit_e(0x0290, 0x00100001);   // control0
            emit_e(0x02C0, 0x00070000);   // window horizontal 0..7
            emit_e(0x02E0, 0x00070000);   // window vertical 0..7
            emit_e(0x0300, 0);            // alpha test disabled
            emit_e(0x0304, 0);            // blend disabled
            emit_e(0x0308, 0);            // cull disabled
            emit_e(0x030C, 0);            // depth test disabled
            emit_e(0x033C, 0x0207);
            emit_e(0x0340, 0);
            emit_e(0x0344, 1);            // blend ONE
            emit_e(0x0348, 0);            // blend ZERO
            emit_e(0x0350, 0x8006);
            emit_e(0x0354, 0x0207);       // depth ALWAYS
            emit_e(0x0358, 0x01010101);
            emit_e(0x035C, 0);
            emit_e(0x037C, 0x1D01);
            emit_e(0x038C, 0x1B02);
            emit_e(0x0390, 0x1B02);
            emit_e(0x0394, 0);
            emit_e(0x0398, 0x4B7FFFFF);
            emit_e(0x039C, 0x0405);
            emit_e(0x03A0, 0x0900);
            emit_e(0x1D78, 1);
            emit_e(0x1E70, 1);            // ShaderStageProgram: PROJECT2D (stage0)
            emit_e(0x1E74, 0);
            emit_e(0x1E78, 0x00210000);
            emit_e(0x1E60, 0x00000001);   // combiner control: 1 stage
            // ICW byte order is A(31:24)/B(23:16)/C(15:8)/D(7:0); AB reaches ab_dst
            // below, so TEXTURE0/PRIMARY_COLOR must be A/B, not C/D.
            emit_e(0x0AC0, 0x08040000);   // ColorIcw0: A=TEXTURE0, B=PRIMARY_COLOR
            emit_e(0x0260, 0x14200000);   // AlphaIcw0: A=PRIMARY_COLOR.a, B=ZERO invert
            emit_e(0x1E40, 0x000000C0);   // ColorOcw0: AB -> R0
            emit_e(0x0AA0, 0x000000C0);   // AlphaOcw0: AB -> R0
            emit_e(0x0288, 0x0000000C);
            emit_e(0x028C, 0x00001C80);
            emit_e(0x1E20, 0);
            emit_e(0x1E24, 0);
            // TEXTURE0 setup: 4x4 pitch-16 linear texture at 0x20000, enable stage0.
            emit_e(0x1B00, 0x00020000);   // TextureOffset0
            emit_e(0x1B04, 0);            // TextureFormat0 (unread by the linear path)
            emit_e(0x1B08, 0);            // TextureAddress0 (unread by the linear path)
            emit_e(0x1B0C, 0x40000000);   // TextureControl0_0: enable bit30
            emit_e(0x1B10, 0x00100000);   // TextureControl1_0: pitch=16
            emit_e(0x1B14, 0);
            emit_e(0x1B1C, 0x00040004);   // TextureImageRect0: 4x4
            emit_e(0x1B20, 0);
            emit_e(0x1B24, 0);

            emit_e(0x17FC, 5);
            const float ex[3] = {0.0f, 8.0f, 0.0f};
            const float ey[3] = {0.0f, 0.0f, 8.0f};
            for (int v = 0; v < 3; ++v) {
                emit_e(0x1A90, e_float_bits(2.5f));   // texcoord0.s (texel space)
                emit_e(0x1A94, e_float_bits(2.5f));   // texcoord0.t
                emit_e(0x1A98, e_float_bits(0.0f));   // texcoord0.r (unused)
                emit_e(0x1A9C, e_float_bits(1.0f));   // texcoord0.q
                emit_e(0x1518, e_float_bits(ex[v]));
                emit_e(0x151C, e_float_bits(ey[v]));
                emit_e(0x1520, e_float_bits(10.0f));
                emit_e(0x194C, e_pack_ub4(0x30, 0x50, 0x70, 0xFF));  // diffuse
                emit_e(0x1524, e_float_bits(1.0f));   // completes slot0 -> commits
            }
            emit_e(0x17FC, 0);
            kick_e();
            CHECK(e_fault.empty());
            CHECK(e_gpu.generic_combiner_batches() == 1);
            CHECK(e_gpu.generic_combiner_executed_batches() == 1);
            CHECK(e_gpu.generic_combiner_texture_samples() > 0);
            const uint32_t e_offset = 3u * 32u + 3u * 4u;
            CHECK(get32(e_ram, 0x10000u + e_offset) == 0xFF243238u);
        }

        // --- (e2) Exact witnessed dashboard present fingerprint: the dashboard binds
        // the front buffer as render target (surf=03978000) and samples the back
        // buffer (T0 off=03aa4000) as TEXTURE0, ci=08200000/ai=14200000 (the same
        // constants as kInlineLinearBlitColor: D=TEXTURE0 identity, C=ZERO invert ->
        // R0=TEXTURE0 passthrough; alpha D=PRIMARY_COLOR.a -> R0.a=diffuse.a), comb=1,
        // en=0010 (dither only; blend/alpha-test/depth-test all off), clip=02800000/
        // 01e00000 (640x480). Sampled texel 0x224466AA unpacks to (R=0x44,G=0x66,
        // B=0xAA); diffuse alpha is 0xFF, so the expected output is 0xFF4466AA. Known
        // texel content at the sampled location proves the sample lands at the
        // front-buffer address. ---
        {
            std::string e2_fault;
            xlle::Nv2a e2_gpu([&](const std::string& why) { e2_fault = why; });
            std::vector<uint8_t> e2_ram(0x04000000, 0);
            e2_gpu.set_mem_access(
                [&](uint32_t addr, void* dst, size_t n) {
                    if (addr > e2_ram.size() || n > e2_ram.size() - addr) return false;
                    std::memcpy(dst, e2_ram.data() + addr, n);
                    return true;
                },
                [&](uint32_t addr, const void* src, size_t n) {
                    if (addr > e2_ram.size() || n > e2_ram.size() - addr) return false;
                    std::memcpy(e2_ram.data() + addr, src, n);
                    return true;
                });
            e2_gpu.mmio_write(xlle::Nv2a::kBase + 0x700008, 4, 1);
            e2_gpu.mmio_write(xlle::Nv2a::kBase + 0x70000C, 4, 0x80010020);
            e2_gpu.mmio_write(xlle::Nv2a::kBase + 0x700200, 4, 0x97);
            e2_gpu.mmio_write(xlle::Nv2a::kBase + 0x700010, 4, 2);
            e2_gpu.mmio_write(xlle::Nv2a::kBase + 0x700014, 4, 0x80010030);
            e2_gpu.mmio_write(xlle::Nv2a::kBase + 0x700300, 4, 0x3D);
            e2_gpu.mmio_write(xlle::Nv2a::kBase + 0x700304, 4, 0x03FFFFFF);
            e2_gpu.mmio_write(xlle::Nv2a::kBase + 0x700308, 4, 0x00000000);
            e2_gpu.mmio_write(xlle::Nv2a::kBase + 0x700020, 4, 4);
            e2_gpu.mmio_write(xlle::Nv2a::kBase + 0x700024, 4, 0x80010050);
            e2_gpu.mmio_write(xlle::Nv2a::kBase + 0x700500, 4, 0x3D);
            e2_gpu.mmio_write(xlle::Nv2a::kBase + 0x700504, 4, 0x03FFFFFF);
            e2_gpu.mmio_write(xlle::Nv2a::kBase + 0x700508, 4, 0x00000000);
            e2_gpu.mmio_write(xlle::Nv2a::kBase + 0x700180, 4, 0x3D);
            e2_gpu.mmio_write(xlle::Nv2a::kBase + 0x700184, 4, 0xFFFF);
            e2_gpu.mmio_write(xlle::Nv2a::kBase + 0x700188, 4, 0x2000);
            e2_gpu.mmio_write(xlle::Nv2a::kBase + 0x002504, 4, 1);
            e2_gpu.mmio_write(xlle::Nv2a::kBase + 0x003204, 4, 0x100);
            e2_gpu.mmio_write(xlle::Nv2a::kBase + 0x00322C, 4, 0x18);
            e2_gpu.mmio_write(xlle::Nv2a::kBase + 0x003244, 4, 0);
            e2_gpu.mmio_write(xlle::Nv2a::kBase + 0x003220, 4, 1);
            e2_gpu.mmio_write(xlle::Nv2a::kBase + 0x003250, 4, 1);
            e2_gpu.mmio_write(xlle::Nv2a::kBase + 0x003200, 4, 1);

            constexpr uint32_t e2_pb = 0x2000;
            uint32_t e2_cursor = 0;
            auto emit_e2 = [&](uint32_t method, uint32_t parameter) {
                put32(e2_ram, e2_pb + e2_cursor, (1u << 18) | method);
                put32(e2_ram, e2_pb + e2_cursor + 4, parameter);
                e2_cursor += 8;
            };
            auto kick_e2 = [&]() {
                e2_gpu.mmio_write(xlle::Nv2a::kBase + 0x800040, 4, e2_cursor);
            };
            auto e2_float_bits = [](float value) {
                uint32_t bits = 0;
                std::memcpy(&bits, &value, sizeof(bits));
                return bits;
            };
            auto e2_pack_ub4 = [](uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
                return static_cast<uint32_t>(r) | (static_cast<uint32_t>(g) << 8) |
                       (static_cast<uint32_t>(b) << 16) | (static_cast<uint32_t>(a) << 24);
            };

            emit_e2(0x0000, 1);
            emit_e2(0x0194, 2);
            emit_e2(0x0188, 4);
            const uint32_t e2_identity[] = {
                0x3F800000, 0, 0, 0,
                0, 0x3F800000, 0, 0,
                0, 0, 0x3F800000, 0,
                0, 0, 0, 0x3F800000,
            };
            for (uint32_t i = 0; i < 16; ++i) emit_e2(0x0680 + i * 4, e2_identity[i]);
            emit_e2(0x0A20, 0);
            emit_e2(0x0A24, 0);
            emit_e2(0x0294, 0x00020001);
            emit_e2(0x0314, 0);
            emit_e2(0x03B8, 0);
            emit_e2(0x1E94, 0);

            // 640x480 "back buffer" source texture at 0x03AA4000, pitch 2560 (640*4).
            // Only the sampled texel needs a known value.
            const uint32_t e2_back_buffer_pitch = 640u * 4u;
            put32(e2_ram, 0x03AA4000u + 100u * e2_back_buffer_pitch + 100u * 4u,
                  0x224466AAu);

            emit_e2(0x0200, 0x02800000);   // clip: 640 wide, x=0
            emit_e2(0x0204, 0x01E00000);   // clip: 480 tall, y=0
            emit_e2(0x0208, 0x00000108);   // A8R8G8B8, pitch-linear, no zeta
            emit_e2(0x020C, 0x0A000A00);   // color pitch 2560 (front buffer)
            emit_e2(0x0210, 0x03978000);   // color offset -- the witnessed front-buffer
            emit_e2(0x0290, 0x00100001);
            emit_e2(0x02C0, 0x027F0000);
            emit_e2(0x02E0, 0x01DF0000);
            emit_e2(0x0300, 0);
            emit_e2(0x0304, 0);            // blend disabled: en=0010 sets only bit4
                                           // (dither); bit1 (blend enable) is off
            emit_e2(0x0308, 0);
            emit_e2(0x030C, 0);
            emit_e2(0x0310, 1);            // dither (en bit4, inert)
            emit_e2(0x033C, 0x0207);
            emit_e2(0x0340, 0);
            emit_e2(0x0344, 1);            // blend ONE
            emit_e2(0x0348, 0);            // blend ZERO
            emit_e2(0x0350, 0x8006);
            emit_e2(0x0354, 0x0207);
            emit_e2(0x0358, 0x01010101);
            emit_e2(0x035C, 0);
            emit_e2(0x037C, 0x1D01);
            emit_e2(0x038C, 0x1B02);
            emit_e2(0x0390, 0x1B02);
            emit_e2(0x0394, 0);
            emit_e2(0x0398, 0x4B7FFFFF);
            emit_e2(0x039C, 0x0405);
            emit_e2(0x03A0, 0x0900);
            emit_e2(0x1D78, 1);
            emit_e2(0x1E70, 1);
            emit_e2(0x1E74, 0);
            emit_e2(0x1E78, 0x00210000);
            emit_e2(0x1E60, 0x00000001);
            emit_e2(0x0AC0, 0x08200000);   // ColorIcw0: D=TEXTURE0 identity, C=ZERO invert
            emit_e2(0x0260, 0x14200000);   // AlphaIcw0: D=PRIMARY_COLOR.a, C=ZERO invert
            emit_e2(0x1E40, 0x00000C00);   // ColorOcw0: SUM -> R0
            emit_e2(0x0AA0, 0x00000C00);   // AlphaOcw0: SUM -> R0
            emit_e2(0x0288, 0x0000000C);
            emit_e2(0x028C, 0x00001C80);
            emit_e2(0x1E20, 0);
            emit_e2(0x1E24, 0);
            emit_e2(0x1B00, 0x03AA4000);   // TextureOffset0 -- samples the back buffer
            emit_e2(0x1B04, 0);
            emit_e2(0x1B08, 0);
            emit_e2(0x1B0C, 0x40000000);
            emit_e2(0x1B10, static_cast<uint32_t>(e2_back_buffer_pitch) << 16);
            emit_e2(0x1B14, 0);
            emit_e2(0x1B1C, (640u << 16) | 480u);
            emit_e2(0x1B20, 0);
            emit_e2(0x1B24, 0);

            emit_e2(0x17FC, 5);
            const float e2x[3] = {0.0f, 640.0f, 0.0f};
            const float e2y[3] = {0.0f, 0.0f, 480.0f};
            for (int v = 0; v < 3; ++v) {
                emit_e2(0x1A90, e2_float_bits(100.5f));  // texel-space s
                emit_e2(0x1A94, e2_float_bits(100.5f));  // texel-space t
                emit_e2(0x1A98, e2_float_bits(0.0f));
                emit_e2(0x1A9C, e2_float_bits(1.0f));
                emit_e2(0x1518, e2_float_bits(e2x[v]));
                emit_e2(0x151C, e2_float_bits(e2y[v]));
                emit_e2(0x1520, e2_float_bits(10.0f));
                emit_e2(0x194C, e2_pack_ub4(0, 0, 0, 0xFF));
                emit_e2(0x1524, e2_float_bits(1.0f));
            }
            emit_e2(0x17FC, 0);
            kick_e2();
            CHECK(e2_fault.empty());
            CHECK(e2_gpu.generic_combiner_batches() == 1);
            CHECK(e2_gpu.generic_combiner_executed_batches() == 1);
            CHECK(e2_gpu.generic_combiner_texture_samples() > 0);
            const uint32_t e2_offset =
                100u * 2560u + 100u * 4u;
            CHECK(get32(e2_ram, 0x03978000u + e2_offset) == 0xFF4466AAu);
            CHECK(e2_gpu.last_generic_combiner_raster_address() >= 0x03978000u);
        }

        // --- (f) Unsupported texture configuration: a swizzled 2D stage with a COLOR
        // code (0x3D) this evaluator's whitelist does not recognize (only 6/7, both
        // cross-checked raw-ARGB8 codes from the pinned profiles, are accepted) must
        // land in the kUnsupportedTextureFormat census, never a silent drop. ---
        {
            std::string f_fault;
            xlle::Nv2a f_gpu([&](const std::string& why) { f_fault = why; });
            std::vector<uint8_t> f_ram(0x40000, 0);
            f_gpu.set_mem_access(
                [&](uint32_t addr, void* dst, size_t n) {
                    if (addr > f_ram.size() || n > f_ram.size() - addr) return false;
                    std::memcpy(dst, f_ram.data() + addr, n);
                    return true;
                },
                [&](uint32_t addr, const void* src, size_t n) {
                    if (addr > f_ram.size() || n > f_ram.size() - addr) return false;
                    std::memcpy(f_ram.data() + addr, src, n);
                    return true;
                });
            f_gpu.mmio_write(xlle::Nv2a::kBase + 0x700008, 4, 1);
            f_gpu.mmio_write(xlle::Nv2a::kBase + 0x70000C, 4, 0x80010020);
            f_gpu.mmio_write(xlle::Nv2a::kBase + 0x700200, 4, 0x97);
            f_gpu.mmio_write(xlle::Nv2a::kBase + 0x700010, 4, 2);
            f_gpu.mmio_write(xlle::Nv2a::kBase + 0x700014, 4, 0x80010030);
            f_gpu.mmio_write(xlle::Nv2a::kBase + 0x700300, 4, 0x3D);
            f_gpu.mmio_write(xlle::Nv2a::kBase + 0x700304, 4, 0x03FFFFFF);
            f_gpu.mmio_write(xlle::Nv2a::kBase + 0x700308, 4, 0x00000000);
            f_gpu.mmio_write(xlle::Nv2a::kBase + 0x700020, 4, 4);
            f_gpu.mmio_write(xlle::Nv2a::kBase + 0x700024, 4, 0x80010050);
            f_gpu.mmio_write(xlle::Nv2a::kBase + 0x700500, 4, 0x3D);
            f_gpu.mmio_write(xlle::Nv2a::kBase + 0x700504, 4, 0x03FFFFFF);
            f_gpu.mmio_write(xlle::Nv2a::kBase + 0x700508, 4, 0x00000000);
            f_gpu.mmio_write(xlle::Nv2a::kBase + 0x700180, 4, 0x3D);
            f_gpu.mmio_write(xlle::Nv2a::kBase + 0x700184, 4, 0xFFFF);
            f_gpu.mmio_write(xlle::Nv2a::kBase + 0x700188, 4, 0x2000);
            f_gpu.mmio_write(xlle::Nv2a::kBase + 0x002504, 4, 1);
            f_gpu.mmio_write(xlle::Nv2a::kBase + 0x003204, 4, 0x100);
            f_gpu.mmio_write(xlle::Nv2a::kBase + 0x00322C, 4, 0x18);
            f_gpu.mmio_write(xlle::Nv2a::kBase + 0x003244, 4, 0);
            f_gpu.mmio_write(xlle::Nv2a::kBase + 0x003220, 4, 1);
            f_gpu.mmio_write(xlle::Nv2a::kBase + 0x003250, 4, 1);
            f_gpu.mmio_write(xlle::Nv2a::kBase + 0x003200, 4, 1);

            constexpr uint32_t f_pb = 0x2000;
            uint32_t f_cursor = 0;
            auto emit_f = [&](uint32_t method, uint32_t parameter) {
                put32(f_ram, f_pb + f_cursor, (1u << 18) | method);
                put32(f_ram, f_pb + f_cursor + 4, parameter);
                f_cursor += 8;
            };
            auto kick_f = [&]() {
                f_gpu.mmio_write(xlle::Nv2a::kBase + 0x800040, 4, f_cursor);
            };
            auto f_float_bits = [](float value) {
                uint32_t bits = 0;
                std::memcpy(&bits, &value, sizeof(bits));
                return bits;
            };
            auto f_pack_ub4 = [](uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
                return static_cast<uint32_t>(r) | (static_cast<uint32_t>(g) << 8) |
                       (static_cast<uint32_t>(b) << 16) | (static_cast<uint32_t>(a) << 24);
            };

            emit_f(0x0000, 1);
            emit_f(0x0194, 2);
            emit_f(0x0188, 4);
            const uint32_t f_identity[] = {
                0x3F800000, 0, 0, 0,
                0, 0x3F800000, 0, 0,
                0, 0, 0x3F800000, 0,
                0, 0, 0, 0x3F800000,
            };
            for (uint32_t i = 0; i < 16; ++i) emit_f(0x0680 + i * 4, f_identity[i]);
            emit_f(0x0A20, 0);
            emit_f(0x0A24, 0);
            emit_f(0x0294, 0x00020001);
            emit_f(0x0314, 0);
            emit_f(0x03B8, 0);
            emit_f(0x1E94, 0);

            std::memset(f_ram.data() + 0x20000, 0, 4 * 4 * 4);
            std::memset(f_ram.data() + 0x10000, 0xAB, 8 * 32);

            emit_f(0x0200, 0x00080000);
            emit_f(0x0204, 0x00080000);
            emit_f(0x0208, 0x00000108);
            emit_f(0x020C, 0x00200020);
            emit_f(0x0210, 0x00010000);
            emit_f(0x0290, 0x00100001);
            emit_f(0x02C0, 0x00070000);
            emit_f(0x02E0, 0x00070000);
            emit_f(0x0300, 0);
            emit_f(0x0304, 0);
            emit_f(0x0308, 0);
            emit_f(0x030C, 0);
            emit_f(0x033C, 0x0207);
            emit_f(0x0340, 0);
            emit_f(0x0344, 1);
            emit_f(0x0348, 0);
            emit_f(0x0350, 0x8006);
            emit_f(0x0354, 0x0207);
            emit_f(0x0358, 0x01010101);
            emit_f(0x035C, 0);
            emit_f(0x037C, 0x1D01);
            emit_f(0x038C, 0x1B02);
            emit_f(0x0390, 0x1B02);
            emit_f(0x0394, 0);
            emit_f(0x0398, 0x4B7FFFFF);
            emit_f(0x039C, 0x0405);
            emit_f(0x03A0, 0x0900);
            emit_f(0x1D78, 1);
            emit_f(0x1E70, 0);            // ShaderStageProgram 0 -> swizzled 2D path
            emit_f(0x1E74, 0);
            emit_f(0x1E78, 0x00210000);
            emit_f(0x1E60, 0x00000001);
            emit_f(0x0AC0, 0x08040000);   // ColorIcw0: A=TEXTURE0, B=PRIMARY_COLOR
            emit_f(0x0260, 0x14200000);
            emit_f(0x1E40, 0x000000C0);
            emit_f(0x0AA0, 0x000000C0);
            emit_f(0x0288, 0x0000000C);
            emit_f(0x028C, 0x00001C80);
            emit_f(0x1E20, 0);
            emit_f(0x1E24, 0);
            emit_f(0x1B00, 0x00020000);
            // Format: dimension bits ==2 (swizzled) but COLOR code 0x3D -- not in the
            // {6,7} whitelist this evaluator's swizzled sampler is cross-checked
            // against, so this must be a named skip, not a guess at a new pixel decode.
            emit_f(0x1B04, (2u << 20) | (2u << 24) | (0x3Du << 8) | 2u);
            emit_f(0x1B08, 0x00030303);
            emit_f(0x1B0C, 0x40000000);
            emit_f(0x1B10, 0);
            emit_f(0x1B14, 0);
            emit_f(0x1B1C, 0);
            emit_f(0x1B20, 0);
            emit_f(0x1B24, 0);

            const size_t unsupported_format_reason = static_cast<size_t>(
                xlle::Nv2a::GenericSkipReason::kUnsupportedTextureFormat);
            const uint64_t before_skip =
                f_gpu.generic_skip_reason_batches(unsupported_format_reason);
            const uint64_t before_batches = f_gpu.generic_combiner_batches();
            const std::vector<uint8_t> before_pixels(
                f_ram.begin() + 0x10000, f_ram.begin() + 0x10000 + 8 * 32);

            emit_f(0x17FC, 5);
            const float fx_pos[3] = {0.0f, 8.0f, 0.0f};
            const float fy_pos[3] = {0.0f, 0.0f, 8.0f};
            for (int v = 0; v < 3; ++v) {
                emit_f(0x1A90, f_float_bits(0.5f));
                emit_f(0x1A94, f_float_bits(0.5f));
                emit_f(0x1A98, f_float_bits(0.0f));
                emit_f(0x1A9C, f_float_bits(1.0f));
                emit_f(0x1518, f_float_bits(fx_pos[v]));
                emit_f(0x151C, f_float_bits(fy_pos[v]));
                emit_f(0x1520, f_float_bits(10.0f));
                emit_f(0x194C, f_pack_ub4(0x30, 0x50, 0x70, 0xFF));
                emit_f(0x1524, f_float_bits(1.0f));
            }
            emit_f(0x17FC, 0);
            kick_f();
            CHECK(f_fault.empty());
            CHECK(f_gpu.generic_skip_reason_batches(unsupported_format_reason) ==
                  before_skip + 1);
            CHECK(f_gpu.generic_combiner_batches() == before_batches);
            CHECK(std::equal(before_pixels.begin(), before_pixels.end(),
                             f_ram.begin() + 0x10000));
        }

        // --- (g) Regression test for the guest-memory-corruption bug: a textured
        // generic-combiner batch whose triangle/clip extends far beyond the surface
        // extent, AND whose zeta context is bound to a DMA object that was never
        // actually programmed with a real frame/base (SET_CONTEXT_DMA_ZETA to a
        // "class 0x3D, base 0, huge limit" identity object, with SURFACE_ZETA_OFFSET
        // left at its default of 0) while DepthMask requests depth writes. Before the
        // fix, dma_object_address() resolved this -- correctly, per the descriptor's
        // own all-zero bits -- to physical address 0, and rasterize_generic_combiner_
        // batch wrote depth data through PagedRasterSurface(address=0, ...), i.e.
        // into the guest's low memory (where 2BL/kernel code lives on real boot).
        // This must no longer happen: resolve_surface now rejects a resolved address
        // of exactly 0, and the zeta failure now degrades to color-only rendering
        // instead of either corrupting memory or hard-faulting the whole batch.
        // A canary at guest address 0 (proxy for "the low memory a real boot depends
        // on") and a canary immediately after the color surface's own extent must
        // both survive untouched; only the color surface's own bytes may change. ---
        {
            std::string g_fault;
            xlle::Nv2a g_gpu([&](const std::string& why) { g_fault = why; });
            std::vector<uint8_t> g_ram(0x40000, 0);
            g_gpu.set_mem_access(
                [&](uint32_t addr, void* dst, size_t n) {
                    if (addr > g_ram.size() || n > g_ram.size() - addr) return false;
                    std::memcpy(dst, g_ram.data() + addr, n);
                    return true;
                },
                [&](uint32_t addr, const void* src, size_t n) {
                    if (addr > g_ram.size() || n > g_ram.size() - addr) return false;
                    std::memcpy(g_ram.data() + addr, src, n);
                    return true;
                });
            g_gpu.mmio_write(xlle::Nv2a::kBase + 0x700008, 4, 1);
            g_gpu.mmio_write(xlle::Nv2a::kBase + 0x70000C, 4, 0x80010020);
            g_gpu.mmio_write(xlle::Nv2a::kBase + 0x700200, 4, 0x97);
            g_gpu.mmio_write(xlle::Nv2a::kBase + 0x700010, 4, 2);
            g_gpu.mmio_write(xlle::Nv2a::kBase + 0x700014, 4, 0x80010030);
            g_gpu.mmio_write(xlle::Nv2a::kBase + 0x700300, 4, 0x3D);
            g_gpu.mmio_write(xlle::Nv2a::kBase + 0x700304, 4, 0x03FFFFFF);
            g_gpu.mmio_write(xlle::Nv2a::kBase + 0x700308, 4, 0x00000000);
            // Zeta context (handle 3): a "linear, whole-of-RAM, base 0" identity DMA
            // object -- exactly the degenerate binding the real dashboard/XBMC boot
            // animation hit. Combined with SURFACE_ZETA_OFFSET left at 0 (never
            // programmed below), this resolves to physical address 0.
            g_gpu.mmio_write(xlle::Nv2a::kBase + 0x700018, 4, 3);
            g_gpu.mmio_write(xlle::Nv2a::kBase + 0x70001C, 4, 0x80010040);
            g_gpu.mmio_write(xlle::Nv2a::kBase + 0x700400, 4, 0x3D);
            g_gpu.mmio_write(xlle::Nv2a::kBase + 0x700404, 4, 0x03FFFFFF);
            g_gpu.mmio_write(xlle::Nv2a::kBase + 0x700408, 4, 0x00000000);
            // Texture DMA B (handle 4).
            g_gpu.mmio_write(xlle::Nv2a::kBase + 0x700020, 4, 4);
            g_gpu.mmio_write(xlle::Nv2a::kBase + 0x700024, 4, 0x80010050);
            g_gpu.mmio_write(xlle::Nv2a::kBase + 0x700500, 4, 0x3D);
            g_gpu.mmio_write(xlle::Nv2a::kBase + 0x700504, 4, 0x03FFFFFF);
            g_gpu.mmio_write(xlle::Nv2a::kBase + 0x700508, 4, 0x00000000);
            g_gpu.mmio_write(xlle::Nv2a::kBase + 0x700180, 4, 0x3D);
            g_gpu.mmio_write(xlle::Nv2a::kBase + 0x700184, 4, 0xFFFF);
            g_gpu.mmio_write(xlle::Nv2a::kBase + 0x700188, 4, 0x2000);
            g_gpu.mmio_write(xlle::Nv2a::kBase + 0x002504, 4, 1);
            g_gpu.mmio_write(xlle::Nv2a::kBase + 0x003204, 4, 0x100);
            g_gpu.mmio_write(xlle::Nv2a::kBase + 0x00322C, 4, 0x18);
            g_gpu.mmio_write(xlle::Nv2a::kBase + 0x003244, 4, 0);
            g_gpu.mmio_write(xlle::Nv2a::kBase + 0x003220, 4, 1);
            g_gpu.mmio_write(xlle::Nv2a::kBase + 0x003250, 4, 1);
            g_gpu.mmio_write(xlle::Nv2a::kBase + 0x003200, 4, 1);

            constexpr uint32_t g_pb = 0x2000;
            uint32_t g_cursor = 0;
            auto emit_g = [&](uint32_t method, uint32_t parameter) {
                put32(g_ram, g_pb + g_cursor, (1u << 18) | method);
                put32(g_ram, g_pb + g_cursor + 4, parameter);
                g_cursor += 8;
            };
            auto kick_g = [&]() {
                g_gpu.mmio_write(xlle::Nv2a::kBase + 0x800040, 4, g_cursor);
            };
            auto g_float_bits = [](float value) {
                uint32_t bits = 0;
                std::memcpy(&bits, &value, sizeof(bits));
                return bits;
            };
            auto g_pack_ub4 = [](uint8_t r, uint8_t g2, uint8_t b, uint8_t a) {
                return static_cast<uint32_t>(r) | (static_cast<uint32_t>(g2) << 8) |
                       (static_cast<uint32_t>(b) << 16) | (static_cast<uint32_t>(a) << 24);
            };

            emit_g(0x0000, 1);
            emit_g(0x0194, 2);
            emit_g(0x0198, 3);  // zeta DMA -> the degenerate identity object
            emit_g(0x0188, 4);  // texture DMA B
            const uint32_t g_identity[] = {
                0x3F800000, 0, 0, 0,
                0, 0x3F800000, 0, 0,
                0, 0, 0x3F800000, 0,
                0, 0, 0, 0x3F800000,
            };
            for (uint32_t i = 0; i < 16; ++i) emit_g(0x0680 + i * 4, g_identity[i]);
            emit_g(0x0A20, 0);
            emit_g(0x0A24, 0);
            emit_g(0x0294, 0x00020001);
            emit_g(0x0314, 0);
            emit_g(0x03B8, 0);
            emit_g(0x1E94, 0);

            // Small 4x4 A8R8G8B8 linear texture, all zero.
            std::vector<uint8_t> g_texture(4 * 4 * 4, 0);
            std::memcpy(g_ram.data() + 0x20000, g_texture.data(), g_texture.size());

            // Canary immediately below address 0 doesn't exist; canary AT and around
            // address 0 stands in for "the low memory a real 2BL/kernel depends on".
            std::memset(g_ram.data(), 0xCC, 4096);
            constexpr uint32_t kColorOffset = 0x10000;
            constexpr uint32_t kColorPitch = 32;
            constexpr uint32_t kColorHeight = 8;
            constexpr uint32_t kColorBytes = kColorPitch * kColorHeight;
            std::memset(g_ram.data() + kColorOffset, 0, kColorBytes);
            std::memset(g_ram.data() + kColorOffset + kColorBytes, 0xDD, 4096);
            const std::vector<uint8_t> g_low_canary_before(g_ram.begin(),
                                                           g_ram.begin() + 4096);
            const std::vector<uint8_t> g_high_canary_before(
                g_ram.begin() + kColorOffset + kColorBytes,
                g_ram.begin() + kColorOffset + kColorBytes + 4096);

            emit_g(0x0200, (kColorPitch / 4) << 16);   // clip: 8 wide, x=0
            emit_g(0x0204, kColorHeight << 16);        // clip: 8 tall, y=0
            emit_g(0x0208, 0x00000128);                // A8R8G8B8 + Z24S8, pitch-linear
            emit_g(0x0210, kColorOffset);               // color offset
            // Zeta pitch matches color pitch so the uses_depth gate is reached (the
            // point of this test is exercising the zeta-resolves-to-0 path, not the
            // pitch-mismatch path already covered elsewhere).
            emit_g(0x020C, (kColorPitch << 16) | kColorPitch);
            // SURFACE_ZETA_OFFSET (0x0214) deliberately never emitted: stays at its
            // default of 0, which is exactly what resolves to physical address 0
            // through the degenerate identity zeta DMA object bound above.
            emit_g(0x0290, 0x00100001);   // control0
            emit_g(0x02C0, 0x00070000);   // window horizontal 0..7
            emit_g(0x02E0, 0x00070000);   // window vertical 0..7
            emit_g(0x0300, 0);            // alpha test disabled
            emit_g(0x0304, 0);            // blend disabled
            emit_g(0x0308, 0);            // cull disabled (so the huge triangle below
                                          // still rasterizes regardless of winding)
            emit_g(0x030C, 0);            // depth test disabled (en bit3==0)
            emit_g(0x033C, 0x0207);
            emit_g(0x0340, 0);
            emit_g(0x0344, 1);            // blend ONE
            emit_g(0x0348, 0);            // blend ZERO
            emit_g(0x0350, 0x8006);
            emit_g(0x0354, 0x0207);       // DepthFunc ALWAYS (unread: test disabled)
            emit_g(0x0358, 0x01010101);
            emit_g(0x035C, 1);            // DepthMask=1: depth WRITES requested --
                                          // this is what reaches the zeta resolve
            emit_g(0x037C, 0x1D01);
            emit_g(0x038C, 0x1B02);
            emit_g(0x0390, 0x1B02);
            emit_g(0x0394, 0);
            emit_g(0x0398, 0x4B7FFFFF);
            emit_g(0x039C, 0x0405);
            emit_g(0x03A0, 0x0900);
            emit_g(0x1D78, 1);
            emit_g(0x1E70, 1);            // ShaderStageProgram: PROJECT2D (stage0)
            emit_g(0x1E74, 0);
            emit_g(0x1E78, 0x00210000);
            emit_g(0x1E60, 0x00000001);   // combiner control: 1 stage
            emit_g(0x0AC0, 0x08200000);   // ColorIcw0: D=TEXTURE0 identity
            emit_g(0x0260, 0x14200000);   // AlphaIcw0: D=PRIMARY_COLOR.a identity
            emit_g(0x1E40, 0x00000C00);   // ColorOcw0: SUM -> R0
            emit_g(0x0AA0, 0x00000C00);   // AlphaOcw0: SUM -> R0
            emit_g(0x0288, 0x0000000C);
            emit_g(0x028C, 0x00001C80);
            emit_g(0x1E20, 0);
            emit_g(0x1E24, 0);
            emit_g(0x1B00, 0x00020000);   // TextureOffset0
            emit_g(0x1B04, 0);
            emit_g(0x1B08, 0);
            emit_g(0x1B0C, 0x40000000);   // enable bit30
            emit_g(0x1B10, 0x00100000);   // pitch=16
            emit_g(0x1B14, 0);
            emit_g(0x1B1C, 0x00040004);   // 4x4
            emit_g(0x1B20, 0);
            emit_g(0x1B24, 0);

            emit_g(0x17FC, 5);
            // Vertex positions extend far beyond the 8x8 clip in every direction --
            // triangle-setup clamping (tri_xmin/xmax/ymin/ymax against the clip- and
            // window-derived surface bounds) must still confine every write to the
            // surface's own extent.
            // Right triangle with hypotenuse x+y=1000: covers the whole 8x8 clip
            // (every point there has x+y <= 14) while still extending far beyond it.
            const float gx[3] = {-1000.0f, 2000.0f, -1000.0f};
            const float gy[3] = {-1000.0f, -1000.0f, 2000.0f};
            for (int v = 0; v < 3; ++v) {
                emit_g(0x1A90, g_float_bits(0.5f));
                emit_g(0x1A94, g_float_bits(0.5f));
                emit_g(0x1A98, g_float_bits(0.0f));
                emit_g(0x1A9C, g_float_bits(1.0f));
                emit_g(0x1518, g_float_bits(gx[v]));
                emit_g(0x151C, g_float_bits(gy[v]));
                emit_g(0x1520, g_float_bits(10.0f));
                emit_g(0x194C, g_pack_ub4(0x30, 0x50, 0x70, 0xFF));
                emit_g(0x1524, g_float_bits(1.0f));
            }
            emit_g(0x17FC, 0);
            kick_g();

            CHECK(g_fault.empty());
            // The batch still executes (color-only: the degenerate zeta must not
            // abort the whole draw).
            CHECK(g_gpu.generic_combiner_batches() == 1);
            CHECK(g_gpu.generic_combiner_writes() > 0);
            // Every write must land strictly inside [color_address, color_address +
            // color_bytes64) -- i.e. within the canaries, never touching them.
            CHECK(std::equal(g_low_canary_before.begin(), g_low_canary_before.end(),
                             g_ram.begin()));
            CHECK(std::equal(g_high_canary_before.begin(), g_high_canary_before.end(),
                             g_ram.begin() + kColorOffset + kColorBytes));
            // Physical address 0 in particular -- the exact corruption this
            // regression produced -- must be untouched.
            CHECK(get32(g_ram, 0) == 0xCCCCCCCCu);
        }

        // --- (h) Texture-alpha-modulated fingerprint: the exact witnessed dashboard
        // menu-chrome combiner (RS0 of probe-stockdash-vfix5-11.5b.stdout.log):
        // ci=04080000 (A=PRIMARY_COLOR identity, B=TEXTURE0 identity -> AB=diffuse*
        // texture), ai=14180000 (A=PRIMARY_COLOR.a identity, B=TEXTURE0.a identity ->
        // AB=diffuse.a*texture.a), co=ao=00000c00 (SUM -> R0), shader=00000001
        // (PROJECT2D/linear). Before this increment, any AlphaIcw referencing
        // TEXTURE0's alpha in the linear sampling path was named-skipped
        // (kTextureStage detail 0x100) on the theory that the linear sampler had no
        // real alpha channel; it does (sample_linear already unpacked all 4 bytes),
        // so this must now execute with the texture's actual alpha byte, not a
        // fallback. Texel = (R=0xC0,G=0xA0,B=0x80,A=0x40), diffuse=(0x30,0x50,0x70,0x80):
        //   R = floor(0xC0*0x30/255 + 0.5) = 36  = 0x24
        //   G = floor(0xA0*0x50/255 + 0.5) = 50  = 0x32
        //   B = floor(0x80*0x70/255 + 0.5) = 56  = 0x38
        //   A = floor(0x40*0x80/255 + 0.5) = floor(32.13+0.5) = 32 = 0x20
        // Expected pixel (blend disabled): 0x20243238. ---
        {
            gc_gpu.mmio_write(xlle::Nv2a::kBase + 0x700020, 4, 4);
            gc_gpu.mmio_write(xlle::Nv2a::kBase + 0x700024, 4, 0x80010050);
            gc_gpu.mmio_write(xlle::Nv2a::kBase + 0x700500, 4, 0x3D);
            gc_gpu.mmio_write(xlle::Nv2a::kBase + 0x700504, 4, 0x03FFFFFF);
            gc_gpu.mmio_write(xlle::Nv2a::kBase + 0x700508, 4, 0x00000000);
            emit_gc(0x0188, 4);  // texture DMA B

            emit_gc(0x0200, 0x00080000);
            emit_gc(0x0204, 0x00080000);
            emit_gc(0x0208, 0x00000108);
            emit_gc(0x020C, 0x00200020);
            emit_gc(0x0210, 0x00010000);
            emit_gc(0x0290, 0x00100001);
            emit_gc(0x02C0, 0x00070000);
            emit_gc(0x02E0, 0x00070000);
            emit_gc(0x0300, 0);
            emit_gc(0x0304, 0);
            emit_gc(0x0308, 0);
            emit_gc(0x030C, 0);
            emit_gc(0x033C, 0x0207);
            emit_gc(0x0340, 0);
            emit_gc(0x0344, 1);
            emit_gc(0x0348, 0);
            emit_gc(0x0350, 0x8006);
            emit_gc(0x0354, 0x0207);
            emit_gc(0x0358, 0x01010101);
            emit_gc(0x035C, 0);
            emit_gc(0x037C, 0x1D01);
            emit_gc(0x038C, 0x1B02);
            emit_gc(0x0390, 0x1B02);
            emit_gc(0x0394, 0);
            emit_gc(0x0398, 0x4B7FFFFF);
            emit_gc(0x039C, 0x0405);
            emit_gc(0x03A0, 0x0900);
            emit_gc(0x1D78, 1);
            emit_gc(0x1E70, 1);
            emit_gc(0x1E74, 0);
            emit_gc(0x1E78, 0x00210000);
            emit_gc(0x1E60, 0x00000001);
            emit_gc(0x0AC0, 0x04080000);  // ColorIcw0: A=PRIMARY, B=TEXTURE0
            emit_gc(0x0260, 0x14180000);  // AlphaIcw0: A=PRIMARY.a, B=TEXTURE0.a
            emit_gc(0x1E40, 0x00000C00);  // ColorOcw0: SUM -> R0
            emit_gc(0x0AA0, 0x00000C00);  // AlphaOcw0: SUM -> R0
            emit_gc(0x0288, 0x0000000C);
            emit_gc(0x028C, 0x00001C80);
            emit_gc(0x1E20, 0);
            emit_gc(0x1E24, 0);
            emit_gc(0x1B00, 0x00030000);   // TextureOffset0 (distinct scratch region)
            emit_gc(0x1B04, 0);
            emit_gc(0x1B08, 0);
            emit_gc(0x1B0C, 0x40000000);
            emit_gc(0x1B10, 0x00100000);   // pitch=16
            emit_gc(0x1B14, 0);
            emit_gc(0x1B1C, 0x00040004);   // 4x4
            emit_gc(0x1B20, 0);
            emit_gc(0x1B24, 0);

            std::vector<uint8_t> h_texture(4 * 4 * 4, 0);
            put32(h_texture, (2u * 4u + 2u) * 4u, 0x40C0A080u);
            std::memcpy(gc_ram.data() + 0x30000, h_texture.data(), h_texture.size());
            std::memset(gc_ram.data() + 0x10000, 0, 8 * 32);

            const uint64_t before_h_samples = gc_gpu.generic_combiner_texture_samples();
            emit_gc(0x17FC, 5);
            for (auto xy : {std::pair{0.0f, 0.0f}, std::pair{8.0f, 0.0f},
                            std::pair{0.0f, 8.0f}}) {
                emit_gc(0x1A90, gc_float_bits(2.5f));
                emit_gc(0x1A94, gc_float_bits(2.5f));
                emit_gc(0x1A98, gc_float_bits(0.0f));
                emit_gc(0x1A9C, gc_float_bits(1.0f));
                emit_gc(0x1518, gc_float_bits(xy.first));
                emit_gc(0x151C, gc_float_bits(xy.second));
                emit_gc(0x1520, gc_float_bits(10.0f));
                emit_gc(0x194C, gc_pack_ub4(0x30, 0x50, 0x70, 0x80));
                emit_gc(0x1524, gc_float_bits(1.0f));
            }
            emit_gc(0x17FC, 0);
            kick_gc();
            CHECK(gc_fault.empty());
            CHECK(gc_gpu.generic_combiner_texture_samples() > before_h_samples);
            const uint32_t h_offset = 3u * 32u + 3u * 4u;
            CHECK(get32(gc_ram, 0x10000u + h_offset) == 0x20243238u);
        }

        // --- (i) OCW SHIFTLEFTBY1: stage0 computes AB = C0*ONE (C0 identity, ZERO
        // invert = 1), then the output scale/bias field (co bits[17:15] = 0x10 in the
        // raw OCW word, i.e. NV097 SHIFTLEFTBY1) doubles it before the clamp to
        // [-1,1] and the store to R0. C0 = (R=G=B=0x30=48); 48/255 doubled = 96/255
        // exactly (no rounding ambiguity: 2*(48/255)*255 = 96 exactly). Final combiner
        // is the identity pass-through (D=R0), so the expected pixel is
        // 0xFF606060 (blend disabled, alpha forced to 0xFF via the AlphaOcw's own
        // SUM path with ZERO/invert inputs, matching test (a)'s alpha derivation). ---
        {
            emit_gc(0x0200, 0x00080000);
            emit_gc(0x0204, 0x00080000);
            emit_gc(0x0208, 0x00000108);
            emit_gc(0x020C, 0x00200020);
            emit_gc(0x0210, 0x00010000);
            emit_gc(0x0290, 0x00100001);
            emit_gc(0x02C0, 0x00070000);
            emit_gc(0x02E0, 0x00070000);
            emit_gc(0x0300, 0);
            emit_gc(0x0304, 0);
            emit_gc(0x0308, 0);
            emit_gc(0x030C, 0);
            emit_gc(0x033C, 0x0207);
            emit_gc(0x0340, 0);
            emit_gc(0x0344, 1);
            emit_gc(0x0348, 0);
            emit_gc(0x0350, 0x8006);
            emit_gc(0x0354, 0x0207);
            emit_gc(0x0358, 0x01010101);
            emit_gc(0x035C, 0);
            emit_gc(0x037C, 0x1D01);
            emit_gc(0x038C, 0x1B02);
            emit_gc(0x0390, 0x1B02);
            emit_gc(0x0394, 0);
            emit_gc(0x0398, 0x4B7FFFFF);
            emit_gc(0x039C, 0x0405);
            emit_gc(0x03A0, 0x0900);
            emit_gc(0x1D78, 1);
            emit_gc(0x1E70, 0);
            emit_gc(0x1E74, 0);
            emit_gc(0x1E78, 0);
            emit_gc(0x1E60, 0x00000001);   // 1 stage
            emit_gc(0x0AC0, 0x01200000);   // ColorIcw0: A=C0 identity, B=ZERO invert
            emit_gc(0x0260, 0x20200000);   // AlphaIcw0: A=ZERO invert, B=ZERO invert
            emit_gc(0x1E40, 0x000100C0);   // ColorOcw0: AB -> R0 (0xC0), scale=SHIFTLEFTBY1 (0x10<<12=0x10000)
            emit_gc(0x0AA0, 0x000000C0);   // AlphaOcw0: AB -> R0 (no scale)
            emit_gc(0x0A60, 0x00303030);   // Factor0: R=G=B=0x30
            emit_gc(0x0288, 0x0000000C);
            emit_gc(0x028C, 0x00001C80);
            emit_gc(0x1E20, 0);
            emit_gc(0x1E24, 0);
            std::memset(gc_ram.data() + 0x10000, 0, 8 * 32);

            submit_triangle(0.0f, 0.0f, 8.0f, 0.0f, 0.0f, 8.0f, 10.0f, 1.0f,
                            gc_pack_ub4(0, 0, 0, 0));
            CHECK(gc_fault.empty());
            const uint32_t i_offset = 3u * 32u + 3u * 4u;
            CHECK(get32(gc_ram, 0x10000u + i_offset) == 0xFF606060u);
        }

        // --- (j) MUX_SUM: stage0's color output uses MUX (co bit14 set) instead of
        // AB+CD. R0.alpha starts this pixel's evaluation at 0 (regs are zeroed per
        // pixel), so with mux_on_msb (combiner control bit8) clear, the LSB-of-R0.alpha
        // test selects AB (condition false since R0.a's LSB is 0). AB = C0(identity)*
        // ZERO-invert(1) = C0 = (0x50,0x60,0x70). CD is deliberately a different,
        // distinguishable value (C1(identity)*ONE) that must NOT be the one selected.
        // Alpha SUM (plain, no mux on the alpha side) writes R0.a = 1 from the same
        // ZERO-invert*ZERO-invert pattern as test (a). Expected pixel: 0xFF506070. ---
        {
            emit_gc(0x0200, 0x00080000);
            emit_gc(0x0204, 0x00080000);
            emit_gc(0x0208, 0x00000108);
            emit_gc(0x020C, 0x00200020);
            emit_gc(0x0210, 0x00010000);
            emit_gc(0x0290, 0x00100001);
            emit_gc(0x02C0, 0x00070000);
            emit_gc(0x02E0, 0x00070000);
            emit_gc(0x0300, 0);
            emit_gc(0x0304, 0);
            emit_gc(0x0308, 0);
            emit_gc(0x030C, 0);
            emit_gc(0x033C, 0x0207);
            emit_gc(0x0340, 0);
            emit_gc(0x0344, 1);
            emit_gc(0x0348, 0);
            emit_gc(0x0350, 0x8006);
            emit_gc(0x0354, 0x0207);
            emit_gc(0x0358, 0x01010101);
            emit_gc(0x035C, 0);
            emit_gc(0x037C, 0x1D01);
            emit_gc(0x038C, 0x1B02);
            emit_gc(0x0390, 0x1B02);
            emit_gc(0x0394, 0);
            emit_gc(0x0398, 0x4B7FFFFF);
            emit_gc(0x039C, 0x0405);
            emit_gc(0x03A0, 0x0900);
            emit_gc(0x1D78, 1);
            emit_gc(0x1E70, 0);
            emit_gc(0x1E74, 0);
            emit_gc(0x1E78, 0);
            emit_gc(0x1E60, 0x00000001);   // mux_on_msb (bit8) clear -> LSB test
            // ColorIcw0: A=C0 identity, B=ZERO invert, C=C1 identity, D=ZERO invert.
            emit_gc(0x0AC0, 0x01200220);
            emit_gc(0x0260, 0x20200000);   // AlphaIcw0: A=B=ZERO invert (SUM=1)
            emit_gc(0x1E40, 0x00004C00);   // ColorOcw0: sum_dst=R0 (0xC00), MUX bit14 set (0x4000)
            emit_gc(0x0AA0, 0x000000C0);   // AlphaOcw0: AB -> R0 (plain, no mux)
            emit_gc(0x0A60, 0x00506070);   // Factor0 (C0): R=0x50,G=0x60,B=0x70
            emit_gc(0x0A80, 0x00909090);   // Factor1 (C1): a distinguishable non-match
            emit_gc(0x0288, 0x0000000C);
            emit_gc(0x028C, 0x00001C80);
            emit_gc(0x1E20, 0);
            emit_gc(0x1E24, 0);
            std::memset(gc_ram.data() + 0x10000, 0, 8 * 32);

            submit_triangle(0.0f, 0.0f, 8.0f, 0.0f, 0.0f, 8.0f, 10.0f, 1.0f,
                            gc_pack_ub4(0, 0, 0, 0));
            CHECK(gc_fault.empty());
            const uint32_t j_offset = 3u * 32u + 3u * 4u;
            CHECK(get32(gc_ram, 0x10000u + j_offset) == 0xFF506070u);
        }

        // --- (k) Real final combiner: RGB = D + A*B + (1-A)*C, alpha = G (no longer
        // only the identity passthrough). Stage0 leaves R0 = diffuse (same passthrough
        // as test (a)). Final combiner: A=C1(identity, a mix factor), B=C0(identity),
        // C=R0(identity, i.e. the stage's diffuse result), D=ZERO, G=R0.alpha.
        // A=0.5 exactly (0x80/255=0.5019..., close enough won't hit an exact half --
        // pick A=64/255 instead for an exact computation): with diffuse=(0x60,0x40,
        // 0x20) and C0=(0xF0,0xF0,0xF0), A=64/255:
        //   R = 0 + (64/255)*(0xF0/255) + (1-64/255)*(0x60/255)
        // Computed precisely below in the assertion via the same formula, packed to
        // an exact byte to avoid float/int rounding ambiguity in the test itself. ---
        {
            emit_gc(0x0200, 0x00080000);
            emit_gc(0x0204, 0x00080000);
            emit_gc(0x0208, 0x00000108);
            emit_gc(0x020C, 0x00200020);
            emit_gc(0x0210, 0x00010000);
            emit_gc(0x0290, 0x00100001);
            emit_gc(0x02C0, 0x00070000);
            emit_gc(0x02E0, 0x00070000);
            emit_gc(0x0300, 0);
            emit_gc(0x0304, 0);
            emit_gc(0x0308, 0);
            emit_gc(0x030C, 0);
            emit_gc(0x033C, 0x0207);
            emit_gc(0x0340, 0);
            emit_gc(0x0344, 1);
            emit_gc(0x0348, 0);
            emit_gc(0x0350, 0x8006);
            emit_gc(0x0354, 0x0207);
            emit_gc(0x0358, 0x01010101);
            emit_gc(0x035C, 0);
            emit_gc(0x037C, 0x1D01);
            emit_gc(0x038C, 0x1B02);
            emit_gc(0x0390, 0x1B02);
            emit_gc(0x0394, 0);
            emit_gc(0x0398, 0x4B7FFFFF);
            emit_gc(0x039C, 0x0405);
            emit_gc(0x03A0, 0x0900);
            emit_gc(0x1D78, 1);
            emit_gc(0x1E70, 0);
            emit_gc(0x1E74, 0);
            emit_gc(0x1E78, 0);
            emit_gc(0x1E60, 0x00000001);
            emit_gc(0x0AC0, 0x04200000);  // ColorIcw0: A=PRIMARY identity, B=ZERO invert
            emit_gc(0x0260, 0x14200000);  // AlphaIcw0: A=PRIMARY.a identity, B=ZERO invert
            emit_gc(0x1E40, 0x000000C0);  // ColorOcw0: AB -> R0
            emit_gc(0x0AA0, 0x000000C0);  // AlphaOcw0: AB -> R0
            // Final combiner: A=C1 identity, B=C0, C=R0, D=ZERO, G=R0.alpha.
            emit_gc(0x0288, 0x02010C00);  // SpecularFogCW0: A=C1,B=C0,C=R0,D=ZERO
            emit_gc(0x028C, 0x00001C00);  // SpecularFogCW1: E=0,F=0,G=R0.alpha,flags=0
            // The final combiner's own CONSTANT_COLOR0/1 (registers 1/2) come from
            // FinalFactor0/1 (NV097_SET_SPECULAR_FOG_FACTOR[0/1]), NOT the per-stage
            // Factor0_0[stage]/Factor1_0[stage] array -- matching xemu's
            // pgraph_glsl_set_psh_uniform_values() (final combiner is constant slot
            // index 8, sourced from SPECFOGFACTOR0/1 instead of
            // COMBINEFACTOR{0,1}+i).
            emit_gc(0x1E20, 0x00F0F0F0);  // FinalFactor0 (C0 for the final combiner): 0xF0
            emit_gc(0x1E24, 0x00404040);  // FinalFactor1 (C1 for the final combiner): 0x40
            std::memset(gc_ram.data() + 0x10000, 0, 8 * 32);

            submit_triangle(0.0f, 0.0f, 8.0f, 0.0f, 0.0f, 8.0f, 10.0f, 1.0f,
                            gc_pack_ub4(0x60, 0x40, 0x20, 0xFF));
            CHECK(gc_fault.empty());
            const uint32_t k_offset = 3u * 32u + 3u * 4u;
            const uint32_t k_pixel = get32(gc_ram, 0x10000u + k_offset);
            auto expect_channel = [](uint8_t diffuse_channel) {
                const float a = 64.0f / 255.0f;
                const float b = 0xF0 / 255.0f;
                const float c = diffuse_channel / 255.0f;
                const float rgb = a * b + (1.0f - a) * c;
                return static_cast<uint32_t>(std::floor(rgb * 255.0f + 0.5f));
            };
            const uint32_t expected_k =
                0xFF000000u | (expect_channel(0x60) << 16) |
                (expect_channel(0x40) << 8) | expect_channel(0x20);
            CHECK(k_pixel == expected_k);
        }

        // --- (l) Cull GL_FRONT (0x0404): the mirror image of every other test's
        // GL_BACK convention -- a front-facing (area>0, CW) triangle must now be
        // culled (no writes), while the SAME triangle with reversed winding
        // (back-facing) must render. ---
        {
            emit_gc(0x0200, 0x00080000);
            emit_gc(0x0204, 0x00080000);
            emit_gc(0x0208, 0x00000108);
            emit_gc(0x020C, 0x00200020);
            emit_gc(0x0210, 0x00010000);
            emit_gc(0x0290, 0x00100001);
            emit_gc(0x02C0, 0x00070000);
            emit_gc(0x02E0, 0x00070000);
            emit_gc(0x0300, 0);
            emit_gc(0x0304, 0);
            emit_gc(0x0308, 1);            // cull enabled
            emit_gc(0x030C, 0);
            emit_gc(0x033C, 0x0207);
            emit_gc(0x0340, 0);
            emit_gc(0x0344, 1);
            emit_gc(0x0348, 0);
            emit_gc(0x0350, 0x8006);
            emit_gc(0x0354, 0x0207);
            emit_gc(0x0358, 0x01010101);
            emit_gc(0x035C, 0);
            emit_gc(0x037C, 0x1D01);
            emit_gc(0x038C, 0x1B02);
            emit_gc(0x0390, 0x1B02);
            emit_gc(0x0394, 0);
            emit_gc(0x0398, 0x4B7FFFFF);
            emit_gc(0x039C, 0x0404);       // GL_FRONT
            emit_gc(0x03A0, 0x0900);
            emit_gc(0x1D78, 1);
            emit_gc(0x1E70, 0);
            emit_gc(0x1E74, 0);
            emit_gc(0x1E78, 0);
            emit_gc(0x1E60, 0x00000001);
            emit_gc(0x0AC0, 0x04200000);
            emit_gc(0x0260, 0x14200000);
            emit_gc(0x1E40, 0x00000C00);
            emit_gc(0x0AA0, 0x00000C00);
            emit_gc(0x0288, 0x0000000C);
            emit_gc(0x028C, 0x00001C80);
            emit_gc(0x1E20, 0);
            emit_gc(0x1E24, 0);
            std::memset(gc_ram.data() + 0x10000, 0, 8 * 32);

            const uint64_t before_l_writes = gc_gpu.generic_combiner_writes();
            // Front-facing (CW, area>0): must be culled -- no new writes.
            submit_triangle(0.0f, 0.0f, 8.0f, 0.0f, 0.0f, 8.0f, 10.0f, 1.0f,
                            gc_pack_ub4(0x10, 0x20, 0x30, 0xFF));
            CHECK(gc_fault.empty());
            CHECK(gc_gpu.generic_combiner_writes() == before_l_writes);
            const uint32_t l_offset = 3u * 32u + 3u * 4u;
            CHECK(get32(gc_ram, 0x10000u + l_offset) == 0);
            // Back-facing (reversed winding, area<0): must render.
            submit_triangle(0.0f, 0.0f, 0.0f, 8.0f, 8.0f, 0.0f, 10.0f, 1.0f,
                            gc_pack_ub4(0x10, 0x20, 0x30, 0xFF));
            CHECK(gc_fault.empty());
            CHECK(gc_gpu.generic_combiner_writes() > before_l_writes);
            CHECK(get32(gc_ram, 0x10000u + l_offset) == 0xFF102030u);
        }

        // --- (m) Zeta pitch/format mismatch degrades to color-only instead of
        // dropping the whole batch: DepthMask=1 (depth writes requested) but the
        // zeta pitch (0x0080) does not match the color pitch (0x0020), exactly the
        // witnessed 0x00400a00-style mismatch (color pitch 0xa00, zeta pitch 0x40)
        // that used to hit kUnsupportedSurface for the whole batch. Must still
        // execute and write color; zeta is simply not touched. ---
        {
            emit_gc(0x0200, 0x00080000);
            emit_gc(0x0204, 0x00080000);
            emit_gc(0x0208, 0x00000128);   // A8R8G8B8 + Z24S8
            emit_gc(0x020C, 0x00800020);   // color pitch=0x20, zeta pitch=0x80 (mismatch)
            emit_gc(0x0210, 0x00010000);
            emit_gc(0x0290, 0x00100001);
            emit_gc(0x02C0, 0x00070000);
            emit_gc(0x02E0, 0x00070000);
            emit_gc(0x0300, 0);
            emit_gc(0x0304, 0);
            emit_gc(0x0308, 0);
            emit_gc(0x030C, 0);            // depth test disabled
            emit_gc(0x033C, 0x0207);
            emit_gc(0x0340, 0);
            emit_gc(0x0344, 1);
            emit_gc(0x0348, 0);
            emit_gc(0x0350, 0x8006);
            emit_gc(0x0354, 0x0207);
            emit_gc(0x0358, 0x01010101);
            emit_gc(0x035C, 1);            // DepthMask=1: requests a depth write
            emit_gc(0x037C, 0x1D01);
            emit_gc(0x038C, 0x1B02);
            emit_gc(0x0390, 0x1B02);
            emit_gc(0x0394, 0);
            emit_gc(0x0398, 0x4B7FFFFF);
            emit_gc(0x039C, 0x0405);
            emit_gc(0x03A0, 0x0900);
            emit_gc(0x1D78, 1);
            emit_gc(0x1E70, 0);
            emit_gc(0x1E74, 0);
            emit_gc(0x1E78, 0);
            emit_gc(0x1E60, 0x00000001);
            emit_gc(0x0AC0, 0x04200000);
            emit_gc(0x0260, 0x14200000);
            emit_gc(0x1E40, 0x00000C00);
            emit_gc(0x0AA0, 0x00000C00);
            emit_gc(0x0288, 0x0000000C);
            emit_gc(0x028C, 0x00001C80);
            emit_gc(0x1E20, 0);
            emit_gc(0x1E24, 0);
            std::memset(gc_ram.data() + 0x10000, 0, 8 * 32);

            const uint64_t before_m_batches = gc_gpu.generic_combiner_batches();
            submit_triangle(0.0f, 0.0f, 8.0f, 0.0f, 0.0f, 8.0f, 10.0f, 1.0f,
                            gc_pack_ub4(0x40, 0x50, 0x60, 0xFF));
            CHECK(gc_fault.empty());
            CHECK(gc_gpu.generic_combiner_batches() == before_m_batches + 1);
            const uint32_t m_offset = 3u * 32u + 3u * 4u;
            CHECK(get32(gc_ram, 0x10000u + m_offset) == 0xFF405060u);
        }

        // --- (n) AB_BLUE_TO_ALPHA (ColorOcw bit19, the exact feature behind the
        // dominant ocw-feature=447 detail 0x000820c0...): stage0 computes AB =
        // C0*C1 -> R0.rgb, and because AB_BLUE_TO_ALPHA is set, R0's blue channel is
        // ALSO copied into R0's alpha channel (the same already-clamped value, so the
        // two bytes must match exactly). AlphaOcw discards all outputs (dst=0) so it
        // cannot clobber the alpha byte AB_BLUE_TO_ALPHA just wrote. C0=(0xFF,0xFF,
        // 0x80), C1=(0xFF,0xFF,0xFF) (identity 1.0): AB=C0*1=C0, so blue=alpha=0x80.
        // Expected pixel: 0x80FFFF80. ---
        {
            emit_gc(0x0200, 0x00080000);
            emit_gc(0x0204, 0x00080000);
            emit_gc(0x0208, 0x00000108);
            emit_gc(0x020C, 0x00200020);
            emit_gc(0x0210, 0x00010000);
            emit_gc(0x0290, 0x00100001);
            emit_gc(0x02C0, 0x00070000);
            emit_gc(0x02E0, 0x00070000);
            emit_gc(0x0300, 0);
            emit_gc(0x0304, 0);
            emit_gc(0x0308, 0);
            emit_gc(0x030C, 0);
            emit_gc(0x033C, 0x0207);
            emit_gc(0x0340, 0);
            emit_gc(0x0344, 1);
            emit_gc(0x0348, 0);
            emit_gc(0x0350, 0x8006);
            emit_gc(0x0354, 0x0207);
            emit_gc(0x0358, 0x01010101);
            emit_gc(0x035C, 0);
            emit_gc(0x037C, 0x1D01);
            emit_gc(0x038C, 0x1B02);
            emit_gc(0x0390, 0x1B02);
            emit_gc(0x0394, 0);
            emit_gc(0x0398, 0x4B7FFFFF);
            emit_gc(0x039C, 0x0405);
            emit_gc(0x03A0, 0x0900);
            emit_gc(0x1D78, 1);
            emit_gc(0x1E70, 0);
            emit_gc(0x1E74, 0);
            emit_gc(0x1E78, 0);
            emit_gc(0x1E60, 0x00000001);
            emit_gc(0x0AC0, 0x01020000);   // ColorIcw0: A=C0 identity, B=C1 identity
            emit_gc(0x0260, 0x00000000);   // AlphaIcw0: unused (discarded below)
            emit_gc(0x1E40, 0x000800C0);   // ColorOcw0: AB -> R0, AB_BLUE_TO_ALPHA (bit19)
            emit_gc(0x0AA0, 0x00000000);   // AlphaOcw0: all outputs discarded
            emit_gc(0x0A60, 0x00FFFF80);   // Factor0 (C0): R=G=0xFF, B=0x80
            emit_gc(0x0A80, 0xFFFFFFFF);   // Factor1 (C1): all 0xFF (identity 1.0)
            emit_gc(0x0288, 0x0000000C);
            emit_gc(0x028C, 0x00001C80);
            emit_gc(0x1E20, 0);
            emit_gc(0x1E24, 0);
            std::memset(gc_ram.data() + 0x10000, 0, 8 * 32);

            submit_triangle(0.0f, 0.0f, 8.0f, 0.0f, 0.0f, 8.0f, 10.0f, 1.0f,
                            gc_pack_ub4(0, 0, 0, 0));
            CHECK(gc_fault.empty());
            const uint32_t n_offset = 3u * 32u + 3u * 4u;
            CHECK(get32(gc_ram, 0x10000u + n_offset) == 0x80FFFF80u);
        }

        // --- (o) Texture-shader program 0x00000063 (CUBEMAP): the exact production
        // ShaderStageProgram witnessed for the stock-dashboard menu scene's 6-stage
        // combiners (comb=00011106). stage0 and stage1 both decode to CUBEMAP mode
        // (0x63 & 0x1F == 3, (0x63 >> 5) & 0x1F == 3); TEXTURE0 is referenced only by
        // stage0's ICW, so only that stage's texture is set up (stage1 is a plain R0
        // passthrough that never touches a texture register). The interpolated
        // texcoord (s=0.625, t=0.625, r=1.0) is a direction vector whose Z axis is
        // strictly dominant (|z|=1.0 > |x|=|y|=0.625), so divisor=1.0, u=s=0.625
        // (r>=0), v=t=0.625 -- landing exactly on texel(2,2) of a 4x4 swizzled texture
        // (texel_x = 0.625*4-0.5 = 2.0 exactly, fx=0; same for y), so bilinear
        // filtering reduces to that single texel with no blending. Stage0: AB =
        // TEXTURE0 * invert(ZERO) = TEXTURE0 (color and alpha) -> R0. Stage1: AB =
        // R0 * invert(ZERO) = R0 (identity passthrough). Final combiner is the same
        // A=B=C=ZERO/D=R0/G=R0.alpha identity every other test above uses. Texel(2,2)
        // = (R=0x60,G=0x40,B=0x20,A=0xFF) -- expected pixel: 0xFF604020 exactly (a
        // pure cubemap-direction-and-swizzle-addressing correctness check, no
        // combiner math involved). ---
        {
            gc_gpu.mmio_write(xlle::Nv2a::kBase + 0x700020, 4, 4);
            gc_gpu.mmio_write(xlle::Nv2a::kBase + 0x700024, 4, 0x80010050);
            gc_gpu.mmio_write(xlle::Nv2a::kBase + 0x700500, 4, 0x3D);
            gc_gpu.mmio_write(xlle::Nv2a::kBase + 0x700504, 4, 0x03FFFFFF);
            gc_gpu.mmio_write(xlle::Nv2a::kBase + 0x700508, 4, 0x00000000);
            emit_gc(0x0188, 4);  // texture DMA B

            emit_gc(0x0200, 0x00080000);
            emit_gc(0x0204, 0x00080000);
            emit_gc(0x0208, 0x00000108);
            emit_gc(0x020C, 0x00200020);
            emit_gc(0x0210, 0x00010000);
            emit_gc(0x0290, 0x00100001);
            emit_gc(0x02C0, 0x00070000);
            emit_gc(0x02E0, 0x00070000);
            emit_gc(0x0300, 0);
            emit_gc(0x0304, 0);
            emit_gc(0x0308, 0);
            emit_gc(0x030C, 0);
            emit_gc(0x033C, 0x0207);
            emit_gc(0x0340, 0);
            emit_gc(0x0344, 1);
            emit_gc(0x0348, 0);
            emit_gc(0x0350, 0x8006);
            emit_gc(0x0354, 0x0207);
            emit_gc(0x0358, 0x01010101);
            emit_gc(0x035C, 0);
            emit_gc(0x037C, 0x1D01);
            emit_gc(0x038C, 0x1B02);
            emit_gc(0x0390, 0x1B02);
            emit_gc(0x0394, 0);
            emit_gc(0x0398, 0x4B7FFFFF);
            emit_gc(0x039C, 0x0405);
            emit_gc(0x03A0, 0x0900);
            emit_gc(0x1D78, 1);
            emit_gc(0x1E70, 0x00000063);  // ShaderStageProgram: exact production value
            emit_gc(0x1E74, 0);
            emit_gc(0x1E78, 0x00210000);
            emit_gc(0x1E60, 0x00000002);   // combiner control: 2 stages
            emit_gc(0x0AC0, 0x08200000);   // stage0 ColorIcw0: A=TEXTURE0, B=ZERO invert
            emit_gc(0x0260, 0x18200000);   // stage0 AlphaIcw0: A=TEXTURE0.a, B=ZERO invert
            emit_gc(0x1E40, 0x000000C0);   // stage0 ColorOcw0: AB -> R0
            emit_gc(0x0AA0, 0x000000C0);   // stage0 AlphaOcw0: AB -> R0
            emit_gc(0x0AC4, 0x0C200000);   // stage1 ColorIcw1: A=R0, B=ZERO invert
            emit_gc(0x0264, 0x1C200000);   // stage1 AlphaIcw1: A=R0.a, B=ZERO invert
            emit_gc(0x1E44, 0x000000C0);   // stage1 ColorOcw1: AB -> R0
            emit_gc(0x0AA4, 0x000000C0);   // stage1 AlphaOcw1: AB -> R0
            emit_gc(0x0288, 0x0000000C);   // SpecularFogCW0: A=B=C=ZERO, D=R0 identity
            emit_gc(0x028C, 0x00001C80);   // SpecularFogCW1: G=R0.alpha identity
            emit_gc(0x1E20, 0);
            emit_gc(0x1E24, 0);
            emit_gc(0x1B00, 0x00030000);   // TextureOffset0 (distinct scratch region)
            emit_gc(0x1B04, 0x02200602);   // TextureFormat0: 4x4, dma=2, COLOR code 6
            emit_gc(0x1B08, 0x00000303);   // TextureAddress0: U=V=CLAMP_TO_EDGE
            emit_gc(0x1B0C, 0x40000000);   // TextureControl0_0: enable bit30
            emit_gc(0x1B10, 0x00100000);   // TextureControl1_0: pitch=16
            emit_gc(0x1B14, 0);
            emit_gc(0x1B1C, 0x00040004);   // TextureImageRect0 (unread by this path)
            emit_gc(0x1B20, 0);            // TexturePalette0: none
            emit_gc(0x1B24, 0);            // TextureBorderColor0

            std::vector<uint8_t> o_texture(4 * 4 * 4, 0);
            put32(o_texture, 12u * 4u, 0xFF604020u);  // swizzle.index(2,2) == 12
            std::memcpy(gc_ram.data() + 0x30000, o_texture.data(), o_texture.size());
            std::memset(gc_ram.data() + 0x10000, 0, 8 * 32);

            const uint64_t before_o_samples = gc_gpu.generic_combiner_texture_samples();
            emit_gc(0x17FC, 5);
            for (auto xy : {std::pair{0.0f, 0.0f}, std::pair{8.0f, 0.0f},
                            std::pair{0.0f, 8.0f}}) {
                emit_gc(0x1A90, gc_float_bits(0.625f));  // texcoord0.s
                emit_gc(0x1A94, gc_float_bits(0.625f));  // texcoord0.t
                emit_gc(0x1A98, gc_float_bits(1.0f));    // texcoord0.r
                emit_gc(0x1A9C, gc_float_bits(1.0f));    // texcoord0.q
                emit_gc(0x1518, gc_float_bits(xy.first));
                emit_gc(0x151C, gc_float_bits(xy.second));
                emit_gc(0x1520, gc_float_bits(10.0f));
                emit_gc(0x194C, gc_pack_ub4(0, 0, 0, 0xFF));
                emit_gc(0x1524, gc_float_bits(1.0f));
            }
            emit_gc(0x17FC, 0);
            kick_gc();
            CHECK(gc_fault.empty());
            CHECK(gc_gpu.generic_combiner_texture_samples() > before_o_samples);
            const uint32_t o_offset = 3u * 32u + 3u * 4u;
            CHECK(get32(gc_ram, 0x10000u + o_offset) == 0xFF604020u);
        }

        // --- (p) Texture-shader program 0x00008421 (four independent PROJECT2D
        // stages): the exact production ShaderStageProgram witnessed for the same
        // stock-dashboard menu scene. Every stage decodes to PROJECT2D (mode 1):
        // 0x8421 & 0x1F == 1, (0x8421 >> 5) & 0x1F == 1, (0x8421 >> 10) & 0x1F == 1,
        // (0x8421 >> 15) & 0x1F == 1. TEXTURE0 is referenced only by stage0, so this
        // exercises the general (non-linear, non-cubemap) swizzled-2D sampling branch
        // -- the "else" arm that reads texcoord[0]/texcoord[1] directly as a normalized
        // UV pair (no perspective divide), unlike PROJECT2D's own narrowly-proven
        // linear special case (ShaderStageProgram==1, test (e) above). UV (0.625,
        // 0.625) lands exactly on texel(2,2) of a 4x4 swizzled texture with fx=fy=0,
        // same reasoning as test (o). Stage0: AB = TEXTURE0 * invert(ZERO) = TEXTURE0
        // -> R0. Stage1: AB = R0 * invert(ZERO) = R0 (identity passthrough). Texel
        // (2,2) = (R=0x50,G=0x30,B=0x10,A=0xFF) -- expected pixel: 0xFF503010. ---
        {
            gc_gpu.mmio_write(xlle::Nv2a::kBase + 0x700020, 4, 4);
            gc_gpu.mmio_write(xlle::Nv2a::kBase + 0x700024, 4, 0x80010050);
            gc_gpu.mmio_write(xlle::Nv2a::kBase + 0x700500, 4, 0x3D);
            gc_gpu.mmio_write(xlle::Nv2a::kBase + 0x700504, 4, 0x03FFFFFF);
            gc_gpu.mmio_write(xlle::Nv2a::kBase + 0x700508, 4, 0x00000000);
            emit_gc(0x0188, 4);  // texture DMA B

            emit_gc(0x0200, 0x00080000);
            emit_gc(0x0204, 0x00080000);
            emit_gc(0x0208, 0x00000108);
            emit_gc(0x020C, 0x00200020);
            emit_gc(0x0210, 0x00010000);
            emit_gc(0x0290, 0x00100001);
            emit_gc(0x02C0, 0x00070000);
            emit_gc(0x02E0, 0x00070000);
            emit_gc(0x0300, 0);
            emit_gc(0x0304, 0);
            emit_gc(0x0308, 0);
            emit_gc(0x030C, 0);
            emit_gc(0x033C, 0x0207);
            emit_gc(0x0340, 0);
            emit_gc(0x0344, 1);
            emit_gc(0x0348, 0);
            emit_gc(0x0350, 0x8006);
            emit_gc(0x0354, 0x0207);
            emit_gc(0x0358, 0x01010101);
            emit_gc(0x035C, 0);
            emit_gc(0x037C, 0x1D01);
            emit_gc(0x038C, 0x1B02);
            emit_gc(0x0390, 0x1B02);
            emit_gc(0x0394, 0);
            emit_gc(0x0398, 0x4B7FFFFF);
            emit_gc(0x039C, 0x0405);
            emit_gc(0x03A0, 0x0900);
            emit_gc(0x1D78, 1);
            emit_gc(0x1E70, 0x00008421);  // ShaderStageProgram: exact production value
            emit_gc(0x1E74, 0);
            emit_gc(0x1E78, 0x00210000);
            emit_gc(0x1E60, 0x00000002);   // combiner control: 2 stages
            emit_gc(0x0AC0, 0x08200000);   // stage0 ColorIcw0: A=TEXTURE0, B=ZERO invert
            emit_gc(0x0260, 0x18200000);   // stage0 AlphaIcw0: A=TEXTURE0.a, B=ZERO invert
            emit_gc(0x1E40, 0x000000C0);   // stage0 ColorOcw0: AB -> R0
            emit_gc(0x0AA0, 0x000000C0);   // stage0 AlphaOcw0: AB -> R0
            emit_gc(0x0AC4, 0x0C200000);   // stage1 ColorIcw1: A=R0, B=ZERO invert
            emit_gc(0x0264, 0x1C200000);   // stage1 AlphaIcw1: A=R0.a, B=ZERO invert
            emit_gc(0x1E44, 0x000000C0);   // stage1 ColorOcw1: AB -> R0
            emit_gc(0x0AA4, 0x000000C0);   // stage1 AlphaOcw1: AB -> R0
            emit_gc(0x0288, 0x0000000C);   // SpecularFogCW0: A=B=C=ZERO, D=R0 identity
            emit_gc(0x028C, 0x00001C80);   // SpecularFogCW1: G=R0.alpha identity
            emit_gc(0x1E20, 0);
            emit_gc(0x1E24, 0);
            emit_gc(0x1B00, 0x00030000);   // TextureOffset0 (distinct scratch region)
            emit_gc(0x1B04, 0x02200602);   // TextureFormat0: 4x4, dma=2, COLOR code 6
            emit_gc(0x1B08, 0x00000303);   // TextureAddress0: U=V=CLAMP_TO_EDGE
            emit_gc(0x1B0C, 0x40000000);   // TextureControl0_0: enable bit30
            emit_gc(0x1B10, 0x00100000);   // TextureControl1_0: pitch=16
            emit_gc(0x1B14, 0);
            emit_gc(0x1B1C, 0x00040004);   // TextureImageRect0 (unread by this path)
            emit_gc(0x1B20, 0);            // TexturePalette0: none
            emit_gc(0x1B24, 0);            // TextureBorderColor0

            std::vector<uint8_t> p_texture(4 * 4 * 4, 0);
            put32(p_texture, 12u * 4u, 0xFF503010u);  // swizzle.index(2,2) == 12
            std::memcpy(gc_ram.data() + 0x30000, p_texture.data(), p_texture.size());
            std::memset(gc_ram.data() + 0x10000, 0, 8 * 32);

            const uint64_t before_p_samples = gc_gpu.generic_combiner_texture_samples();
            emit_gc(0x17FC, 5);
            for (auto xy : {std::pair{0.0f, 0.0f}, std::pair{8.0f, 0.0f},
                            std::pair{0.0f, 8.0f}}) {
                emit_gc(0x1A90, gc_float_bits(0.625f));  // texcoord0.s (normalized UV)
                emit_gc(0x1A94, gc_float_bits(0.625f));  // texcoord0.t
                emit_gc(0x1A98, gc_float_bits(0.0f));    // texcoord0.r (unused)
                emit_gc(0x1A9C, gc_float_bits(1.0f));    // texcoord0.q
                emit_gc(0x1518, gc_float_bits(xy.first));
                emit_gc(0x151C, gc_float_bits(xy.second));
                emit_gc(0x1520, gc_float_bits(10.0f));
                emit_gc(0x194C, gc_pack_ub4(0, 0, 0, 0xFF));
                emit_gc(0x1524, gc_float_bits(1.0f));
            }
            emit_gc(0x17FC, 0);
            kick_gc();
            CHECK(gc_fault.empty());
            CHECK(gc_gpu.generic_combiner_texture_samples() > before_p_samples);
            const uint32_t p_offset = 3u * 32u + 3u * 4u;
            CHECK(get32(gc_ram, 0x10000u + p_offset) == 0xFF503010u);
        }

        // --- (q) COLOR code 0x19 (NV097_SET_TEXTURE_FORMAT_COLOR_SZ_A8): the exact
        // production texture format witnessed on the menu scene's PROJECT2D texture
        // chain (T1-T3 of RS0/RS1, format word 0x0881192a -- same COLOR code, this
        // test just uses a small 4x4 texture instead of the real 256x256 one). SZ_A8
        // is swizzled, 1 byte per texel, alpha-only: per xemu's own kelvin_color_
        // format_info_map + GL sampler-swizzle table, the stored byte becomes the
        // alpha channel while RGB reads back as white (1,1,1) (GL_R8 with swizzle
        // {ONE,ONE,ONE,RED}). Same structure as test (p): stage0 samples TEXTURE0
        // (PROJECT2D, general swizzled branch, ShaderStageProgram=0x00008421),
        // stage1 is an R0 passthrough. Texel(2,2)'s stored byte is 0x60, so TEXTURE0
        // decodes to (R=1,G=1,B=1,A=0x60/255); stage0's AB = TEXTURE0 * invert(ZERO)
        // = TEXTURE0 -> R0 unchanged by stage1 or the identity final combiner.
        // Expected pixel: 0x60FFFFFF (A=0x60, R=G=B=0xFF). ---
        {
            gc_gpu.mmio_write(xlle::Nv2a::kBase + 0x700020, 4, 4);
            gc_gpu.mmio_write(xlle::Nv2a::kBase + 0x700024, 4, 0x80010050);
            gc_gpu.mmio_write(xlle::Nv2a::kBase + 0x700500, 4, 0x3D);
            gc_gpu.mmio_write(xlle::Nv2a::kBase + 0x700504, 4, 0x03FFFFFF);
            gc_gpu.mmio_write(xlle::Nv2a::kBase + 0x700508, 4, 0x00000000);
            emit_gc(0x0188, 4);  // texture DMA B

            emit_gc(0x0200, 0x00080000);
            emit_gc(0x0204, 0x00080000);
            emit_gc(0x0208, 0x00000108);
            emit_gc(0x020C, 0x00200020);
            emit_gc(0x0210, 0x00010000);
            emit_gc(0x0290, 0x00100001);
            emit_gc(0x02C0, 0x00070000);
            emit_gc(0x02E0, 0x00070000);
            emit_gc(0x0300, 0);
            emit_gc(0x0304, 0);
            emit_gc(0x0308, 0);
            emit_gc(0x030C, 0);
            emit_gc(0x033C, 0x0207);
            emit_gc(0x0340, 0);
            emit_gc(0x0344, 1);
            emit_gc(0x0348, 0);
            emit_gc(0x0350, 0x8006);
            emit_gc(0x0354, 0x0207);
            emit_gc(0x0358, 0x01010101);
            emit_gc(0x035C, 0);
            emit_gc(0x037C, 0x1D01);
            emit_gc(0x038C, 0x1B02);
            emit_gc(0x0390, 0x1B02);
            emit_gc(0x0394, 0);
            emit_gc(0x0398, 0x4B7FFFFF);
            emit_gc(0x039C, 0x0405);
            emit_gc(0x03A0, 0x0900);
            emit_gc(0x1D78, 1);
            emit_gc(0x1E70, 0x00008421);  // ShaderStageProgram: exact production value
            emit_gc(0x1E74, 0);
            emit_gc(0x1E78, 0x00210000);
            emit_gc(0x1E60, 0x00000002);   // combiner control: 2 stages
            emit_gc(0x0AC0, 0x08200000);   // stage0 ColorIcw0: A=TEXTURE0, B=ZERO invert
            emit_gc(0x0260, 0x18200000);   // stage0 AlphaIcw0: A=TEXTURE0.a, B=ZERO invert
            emit_gc(0x1E40, 0x000000C0);   // stage0 ColorOcw0: AB -> R0
            emit_gc(0x0AA0, 0x000000C0);   // stage0 AlphaOcw0: AB -> R0
            emit_gc(0x0AC4, 0x0C200000);   // stage1 ColorIcw1: A=R0, B=ZERO invert
            emit_gc(0x0264, 0x1C200000);   // stage1 AlphaIcw1: A=R0.a, B=ZERO invert
            emit_gc(0x1E44, 0x000000C0);   // stage1 ColorOcw1: AB -> R0
            emit_gc(0x0AA4, 0x000000C0);   // stage1 AlphaOcw1: AB -> R0
            emit_gc(0x0288, 0x0000000C);   // SpecularFogCW0: A=B=C=ZERO, D=R0 identity
            emit_gc(0x028C, 0x00001C80);   // SpecularFogCW1: G=R0.alpha identity
            emit_gc(0x1E20, 0);
            emit_gc(0x1E24, 0);
            emit_gc(0x1B00, 0x00030000);   // TextureOffset0 (distinct scratch region)
            emit_gc(0x1B04, 0x02201902);   // TextureFormat0: 4x4, dma=2, COLOR code 0x19 (SZ_A8)
            emit_gc(0x1B08, 0x00000303);   // TextureAddress0: U=V=CLAMP_TO_EDGE
            emit_gc(0x1B0C, 0x40000000);   // TextureControl0_0: enable bit30
            emit_gc(0x1B10, 0x00100000);   // TextureControl1_0 (unread by this path)
            emit_gc(0x1B14, 0);
            emit_gc(0x1B1C, 0x00040004);   // TextureImageRect0 (unread by this path)
            emit_gc(0x1B20, 0);            // TexturePalette0: none
            emit_gc(0x1B24, 0);            // TextureBorderColor0

            std::vector<uint8_t> q_texture(4 * 4, 0);  // 1 byte/texel, 4x4
            q_texture[12] = 0x60;  // swizzle.index(2,2) == 12
            std::memcpy(gc_ram.data() + 0x30000, q_texture.data(), q_texture.size());
            std::memset(gc_ram.data() + 0x10000, 0, 8 * 32);

            const uint64_t before_q_samples = gc_gpu.generic_combiner_texture_samples();
            emit_gc(0x17FC, 5);
            for (auto xy : {std::pair{0.0f, 0.0f}, std::pair{8.0f, 0.0f},
                            std::pair{0.0f, 8.0f}}) {
                emit_gc(0x1A90, gc_float_bits(0.625f));  // texcoord0.s (normalized UV)
                emit_gc(0x1A94, gc_float_bits(0.625f));  // texcoord0.t
                emit_gc(0x1A98, gc_float_bits(0.0f));    // texcoord0.r (unused)
                emit_gc(0x1A9C, gc_float_bits(1.0f));    // texcoord0.q
                emit_gc(0x1518, gc_float_bits(xy.first));
                emit_gc(0x151C, gc_float_bits(xy.second));
                emit_gc(0x1520, gc_float_bits(10.0f));
                emit_gc(0x194C, gc_pack_ub4(0, 0, 0, 0xFF));
                emit_gc(0x1524, gc_float_bits(1.0f));
            }
            emit_gc(0x17FC, 0);
            kick_gc();
            CHECK(gc_fault.empty());
            CHECK(gc_gpu.generic_combiner_texture_samples() > before_q_samples);
            const uint32_t q_offset = 3u * 32u + 3u * 4u;
            CHECK(get32(gc_ram, 0x10000u + q_offset) == 0x60FFFFFFu);
        }
    }

    std::printf(fails ? "test_nv2a: %d checks failed\n" : "test_nv2a: ok\n", fails);
    return fails ? 1 : 0;
}
