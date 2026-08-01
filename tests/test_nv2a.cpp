// xlle — focused PFIFO recorder / NV097 semaphore DMA test.
#include <cmath>
#include <cstdio>
#include <cstring>
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
    std::vector<uint8_t> ram(0x20000, 0);
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
    gpu.mmio_write(xlle::Nv2a::kBase + 0x700184, 4, 0x00000FFF);
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

    // Kelvin pitch-surface clear: handle 2 maps a zero-based 64 KiB color DMA object.
    gpu.mmio_write(xlle::Nv2a::kBase + 0x700010, 4, 0x00000002);
    gpu.mmio_write(xlle::Nv2a::kBase + 0x700014, 4, 0x80010030);
    gpu.mmio_write(xlle::Nv2a::kBase + 0x700018, 4, 0x00000003);
    gpu.mmio_write(xlle::Nv2a::kBase + 0x70001C, 4, 0x80010040);
    gpu.mmio_write(xlle::Nv2a::kBase + 0x700300, 4, 0x0000003D);
    gpu.mmio_write(xlle::Nv2a::kBase + 0x700304, 4, 0x0001FFFF);
    gpu.mmio_write(xlle::Nv2a::kBase + 0x700308, 4, 0x00000000);
    gpu.mmio_write(xlle::Nv2a::kBase + 0x700400, 4, 0x0000003D);
    gpu.mmio_write(xlle::Nv2a::kBase + 0x700404, 4, 0x0001FFFF);
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

    std::printf(fails ? "test_nv2a: %d checks failed\n" : "test_nv2a: ok\n", fails);
    return fails ? 1 : 0;
}
