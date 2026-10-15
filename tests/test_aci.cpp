// xlle — focused MCPX ACI PCM-out descriptor/status/IRQ lifecycle test.
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "machine/aci.hpp"

static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL: %s (line %d)\n", #c, __LINE__); ++fails; } } while (0)

static void put32(std::vector<uint8_t>& mem, uint32_t addr, uint32_t value) {
    std::memcpy(mem.data() + addr, &value, sizeof(value));
}

int main() {
    constexpr uint32_t base = xlle::McpxAci::kBase;
    constexpr uint32_t bdbar = 0x1000;
    constexpr uint32_t buffer = 0x2000;

    std::string fault;
    std::vector<std::pair<int, bool>> irq;
    std::vector<uint8_t> ram(0x10000, 0);
    xlle::McpxAci aci([&](const std::string& why) { fault = why; });
    CHECK(aci.initialize_kernel_audio_handoff());
    aci.set_time(0,1125000);
    aci.set_mem_access([&](uint32_t addr, void* dst, size_t n) {
        if (addr + n > ram.size()) return false;
        std::memcpy(dst, ram.data() + addr, n);
        return true;
    });
    aci.set_irq([&](int line, bool level) { irq.emplace_back(line, level); }, 6);

    // One 0x1000-word descriptor, IOC+BUP, also the last valid descriptor.
    put32(ram, bdbar, buffer);
    put32(ram, bdbar + 4, 0xc0001000);
    aci.mmio_write(base + 0x110, 4, bdbar);
    aci.mmio_write(base + 0x114, 4, 0);       // combined CIV/LVI/SR write
    aci.mmio_write(base + 0x11b, 1, 0x1d);    // RUN + all completion enables

    CHECK(fault.empty());
    CHECK((aci.mmio_read(base + 0x116, 2) & 1) == 0);
    CHECK(aci.mmio_read(base + 0x118, 4) == 0x1d001000);
    CHECK(irq.empty());

    // Eight 5.333 ms frames consume 4096 stereo words (8192 bytes).
    aci.set_frame(7);
    CHECK(aci.mmio_read(base + 0x118, 2) == 0x0200);
    CHECK(aci.completed_descriptors() == 0);
    CHECK(irq.empty());

    aci.set_frame(8);
    CHECK(aci.completed_descriptors() == 1);
    CHECK((aci.mmio_read(base + 0x116, 2) & 0x0f) == 0x0c);
    CHECK(aci.mmio_read(base + 0x118, 2) == 0x1000);
    CHECK((aci.mmio_read(base + 0x130, 4) & (1u << 6)) != 0);
    CHECK(irq.size() == 1 && irq[0] == std::make_pair(6, true));

    // BCIS/LVBCI are W1C; clearing IRQ does not halt the repeating ring.
    aci.mmio_write(base + 0x116, 2, 0x0c);
    CHECK((aci.mmio_read(base + 0x116, 2) & 0x0f) == 0);
    CHECK((aci.mmio_read(base + 0x130, 4) & (1u << 6)) == 0);
    CHECK(irq.size() == 2 && irq[1] == std::make_pair(6, false));
    aci.set_frame(16);
    CHECK(aci.completed_descriptors() == 2 && aci.consumed_pcm_words() == 8192);
    CHECK(aci.mmio_read(base + 0x118, 2) == 0x1000 && fault.empty());

    std::printf(fails ? "test_aci: %d checks failed\n" : "test_aci: ok\n", fails);
    return fails ? 1 : 0;
}
