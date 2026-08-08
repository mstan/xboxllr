// xlle — focused PFIFO recorder / NV097 semaphore DMA test.
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
    CHECK(gpu.class_method_count(0x97) == gpu.push_methods());
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
        const uint64_t immediate_vertices_before_generic =
            immediate_gpu.immediate_vertices();
        const uint64_t immediate_batches_before_generic =
            immediate_gpu.immediate_batches();
        const uint64_t vertex_batches_before_generic = immediate_gpu.vertex_batches();

        // Generic streams update persistent attributes but never commit a vertex in this
        // witnessed path, even for slot 0. Only legacy SET_VERTEX4F's W method commits.
        emit_immediate(0x17FC, 1);
        emit_immediate(0x1880, float_bits(1.0f));
        emit_immediate(0x1884, float_bits(2.0f));
        emit_immediate(0x1940, 0xFF804000u);
        emit_immediate(0x17FC, 0);
        kick_immediate();
        CHECK(immediate_fault.empty());
        CHECK(immediate_gpu.immediate_methods() == immediate_methods_before_generic + 3);
        CHECK(immediate_gpu.immediate_vertices() == immediate_vertices_before_generic);
        CHECK(immediate_gpu.immediate_batches() == immediate_batches_before_generic);
        CHECK(immediate_gpu.vertex_batches() == vertex_batches_before_generic);

        // The generic conversions become visible only when the witnessed legacy position-W
        // commit snapshots them; generic methods themselves still create no vertex.
        emit_immediate(0x17FC, 1);
        emit_immediate(0x1880, float_bits(5.0f));
        emit_immediate(0x1884, float_bits(6.0f));
        emit_immediate(0x1524, float_bits(1.0f));
        emit_immediate(0x17FC, 0);
        kick_immediate();
        CHECK(immediate_fault.empty());
        CHECK(immediate_gpu.immediate_vertices() == immediate_vertices_before_generic + 1);
        CHECK(immediate_gpu.immediate_batches() == immediate_batches_before_generic + 1);
        CHECK(immediate_gpu.last_transformed_position() ==
              (std::array<float, 4>{{5.0f, 6.0f, 0.0f, 1.0f}}));

        emit_immediate(0x17FC, 1);
        emit_immediate(0x1940, 0xFF804000u);
        emit_immediate(0x1524, float_bits(1.0f));
        emit_immediate(0x17FC, 0);
        kick_immediate();
        CHECK(immediate_fault.empty());
        CHECK(immediate_gpu.immediate_vertices() == immediate_vertices_before_generic + 2);
        CHECK(immediate_gpu.immediate_batches() == immediate_batches_before_generic + 2);
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

    std::printf(fails ? "test_nv2a: %d checks failed\n" : "test_nv2a: ok\n", fails);
    return fails ? 1 : 0;
}
