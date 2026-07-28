// xlle — focused OHCI reset/frame/HCCA/interrupt lifecycle test.
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "machine/ohci.hpp"

static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL: %s (line %d)\n", #c, __LINE__); ++fails; } } while (0)

int main() {
    constexpr uint32_t base = 0xfed00000u;
    std::string fault;
    std::vector<std::pair<int, bool>> irq;
    std::vector<uint8_t> ram(0x2000, 0);
    xlle::Ohci hc(base, [&](const std::string& why) { fault = why; });
    hc.set_mem_write([&](uint32_t addr, const void* src, size_t n) {
        if (addr + n > ram.size()) return false;
        std::memcpy(ram.data() + addr, src, n);
        return true;
    });
    hc.set_irq([&](int line, bool level) { irq.emplace_back(line, level); }, 1);

    CHECK(hc.mmio_read(base + 0x00, 4) == 0x10);
    CHECK(hc.mmio_read(base + 0x34, 4) == 0x27782edf);
    CHECK(hc.mmio_read(base + 0x48, 4) == 0x1204);
    CHECK(hc.mmio_read(base + 0x54, 4) == 0x100);

    hc.mmio_write(base + 0x18, 4, 0x1000);          // HCCA
    hc.mmio_write(base + 0x10, 4, 0x80000004u);     // MIE + start-of-frame
    hc.mmio_write(base + 0x04, 4, 0x80);            // operational
    hc.set_frame(1);

    uint16_t frame_number = 0;
    std::memcpy(&frame_number, ram.data() + 0x1080, sizeof(frame_number));
    CHECK(frame_number == 1);
    CHECK(hc.frames() == 1);
    CHECK((hc.mmio_read(base + 0x0c, 4) & 4) != 0);
    CHECK(irq.size() == 1 && irq[0] == std::make_pair(1, true));

    hc.mmio_write(base + 0x0c, 4, 4);
    CHECK(irq.size() == 2 && irq[1] == std::make_pair(1, false));

    hc.mmio_write(base + 0x08, 4, 1);  // HCR
    CHECK(hc.mmio_read(base + 0x04, 4) == 0);
    CHECK(hc.mmio_read(base + 0x00, 4) == 0x10);
    CHECK(fault.empty());

    std::printf(fails ? "test_ohci: %d checks failed\n" : "test_ohci: ok\n", fails);
    return fails ? 1 : 0;
}
