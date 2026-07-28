// xlle — focused PFIFO recorder / NV097 semaphore DMA test.
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

int main() {
    std::string fault;
    xlle::Nv2a gpu([&](const std::string& why) { fault = why; });
    std::vector<uint8_t> ram(0x10000, 0);
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
    gpu.mmio_write(xlle::Nv2a::kBase + 0x700100, 4, 0x0000003D);
    gpu.mmio_write(xlle::Nv2a::kBase + 0x700104, 4, 0x00000FFF);
    gpu.mmio_write(xlle::Nv2a::kBase + 0x700108, 4, 0x00003000);

    // CACHE1's pusher DMA object maps zero-based GET/PUT offsets onto the pushbuffer at
    // physical 0x2000. This is distinct from the semaphore DMA object above.
    gpu.mmio_write(xlle::Nv2a::kBase + 0x700180, 4, 0x0000003D);
    gpu.mmio_write(xlle::Nv2a::kBase + 0x700184, 4, 0x00000FFF);
    gpu.mmio_write(xlle::Nv2a::kBase + 0x700188, 4, 0x00002000);

    uint32_t pb = 0x2000;
    put32(ram, pb + 0x00, (1u << 18) | 0x01A4u);
    put32(ram, pb + 0x04, 0x00000000u);
    put32(ram, pb + 0x08, (2u << 18) | 0x1D6Cu);
    put32(ram, pb + 0x0C, 0x00000040u);
    put32(ram, pb + 0x10, 0x12345678u);

    // Enable channel 0's DMA pusher, initialize CACHE1_DMA_GET through PFIFO, then submit
    // zero-based offsets through USER DMA_PUT.
    gpu.mmio_write(xlle::Nv2a::kBase + 0x002504, 4, 1);
    gpu.mmio_write(xlle::Nv2a::kBase + 0x003204, 4, 0x100);
    gpu.mmio_write(xlle::Nv2a::kBase + 0x00322C, 4, 0x18);
    gpu.mmio_write(xlle::Nv2a::kBase + 0x003244, 4, 0);
    gpu.mmio_write(xlle::Nv2a::kBase + 0x003220, 4, 1);
    gpu.mmio_write(xlle::Nv2a::kBase + 0x003250, 4, 1);
    gpu.mmio_write(xlle::Nv2a::kBase + 0x003200, 4, 1);
    gpu.mmio_write(xlle::Nv2a::kBase + 0x800040, 4, 0x14);

    CHECK(fault.empty());
    CHECK(gpu.mmio_read(xlle::Nv2a::kBase + 0x800044, 4) == 0x14);
    CHECK(gpu.mmio_read(xlle::Nv2a::kBase + 0x003244, 4) == 0x14);
    CHECK(gpu.push_methods() == 3);
    CHECK(gpu.semaphore_writes() == 1);
    CHECK(get32(ram, 0x3040) == 0x12345678u);

    // A two-deep flip queue catches up (READ=0, WRITE=1, then increment WRITE -> 0), so
    // FLIP_STALL must hold the following semaphore until the vblank ISR increments READ.
    put32(ram, pb + 0x14, (5u << 18) | 0x0120u);
    put32(ram, pb + 0x18, 0);  // SET_FLIP_READ
    put32(ram, pb + 0x1C, 1);  // SET_FLIP_WRITE
    put32(ram, pb + 0x20, 2);  // SET_FLIP_MODULO
    put32(ram, pb + 0x24, 0);  // FLIP_INCREMENT_WRITE
    put32(ram, pb + 0x28, 0);  // FLIP_STALL
    put32(ram, pb + 0x2C, (1u << 18) | 0x1D70u);
    put32(ram, pb + 0x30, 0xCAFEBABEu);
    gpu.mmio_write(xlle::Nv2a::kBase + 0x800040, 4, 0x34);

    CHECK(fault.empty());
    CHECK(gpu.flip_increments() == 1);
    CHECK(gpu.flip_stalls() == 1);
    CHECK(gpu.waiting_for_flip());
    CHECK(gpu.mmio_read(xlle::Nv2a::kBase + 0x800044, 4) == 0x34);
    CHECK(gpu.mmio_read(xlle::Nv2a::kBase + 0x003244, 4) == 0x34);
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
    put32(ram, pb + 0x34, (1u << 18) | 0x0100u);
    put32(ram, pb + 0x38, 0x00000304u);
    put32(ram, pb + 0x3C, (1u << 18) | 0x1D70u);
    put32(ram, pb + 0x40, 0x0BADF00Du);
    gpu.mmio_write(xlle::Nv2a::kBase + 0x800040, 4, 0x44);

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
