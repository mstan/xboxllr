// xlle — focused NVNet reset, descriptor-pointer, MDIO, link, and IRQ lifecycle test.
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "machine/nvnet.hpp"

static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL: %s (line %d)\n", #c, __LINE__); ++fails; } } while (0)

int main() {
    constexpr uint32_t base = xlle::NvNet::kBase;
    std::string fault;
    std::vector<std::pair<int, bool>> irq;
    xlle::NvNet nic([&](const std::string& why) { fault = why; });
    nic.set_irq([&](int line, bool level) { irq.emplace_back(line, level); }, 4);

    CHECK(nic.mmio_read(base + 0x144, 4) == 8);
    CHECK(nic.link_up());

    // PHY address 1, BMSR: link and autonegotiation reset complete.
    nic.mmio_write(base + 0x190, 4, 0x8000 | (1u << 5) | 1u);
    CHECK(nic.mmio_read(base + 0x190, 4) == ((1u << 5) | 1u));
    CHECK(nic.mmio_read(base + 0x194, 4) == 0x2c);
    CHECK(nic.mdio_reads() == 1);

    // An absent PHY returns the MDIO bus idle value.
    nic.mmio_write(base + 0x190, 4, 0x8000 | (2u << 5) | 1u);
    CHECK(nic.mmio_read(base + 0x194, 4) == 0xffffffffu);

    // MDIO write to ANAR, followed by a read through the same command path.
    nic.mmio_write(base + 0x194, 4, 0x1234);
    nic.mmio_write(base + 0x190, 4, 0x8000 | 0x400 | (1u << 5) | 4u);
    nic.mmio_write(base + 0x190, 4, 0x8000 | (1u << 5) | 4u);
    CHECK(nic.mmio_read(base + 0x194, 4) == 0x1234);
    CHECK(nic.mdio_writes() == 1);

    // RESET copies ring bases into both current and next descriptor pointers.
    nic.mmio_write(base + 0x100, 4, 0x00100000);
    nic.mmio_write(base + 0x104, 4, 0x00200000);
    nic.mmio_write(base + 0x144, 4, 0x10);
    CHECK(nic.mmio_read(base + 0x11c, 4) == 0x00100000);
    CHECK(nic.mmio_read(base + 0x134, 4) == 0x00100000);
    CHECK(nic.mmio_read(base + 0x120, 4) == 0x00200000);
    CHECK(nic.mmio_read(base + 0x138, 4) == 0x00200000);
    CHECK(nic.mmio_read(base + 0x144, 4) == 0x18);

    // Link changes publish MII status and aggregate through IRQ status/mask.
    nic.mmio_write(base + 0x004, 4, 0x40);
    nic.set_link(false);
    CHECK(!nic.link_up());
    CHECK(nic.mmio_read(base + 0x180, 4) == 8);
    CHECK(nic.irq_status() == 0x40);
    CHECK(irq.size() == 1 && irq[0] == std::make_pair(4, true));
    nic.mmio_write(base + 0x180, 4, 8);
    nic.mmio_write(base + 0x000, 4, 0x40);
    CHECK(irq.size() == 2 && irq[1] == std::make_pair(4, false));
    CHECK(fault.empty());

    // The host mapping is page-padded, but hardware still decodes only 0x400 bytes.
    std::string padding_fault;
    xlle::NvNet padding([&](const std::string& why) { padding_fault = why; });
    CHECK(padding.mmio_read(base + 0x400, 4) == 0xffffffffu);
    CHECK(!padding_fault.empty());

    std::printf(fails ? "test_nvnet: %d checks failed\n" : "test_nvnet: ok\n", fails);
    return fails ? 1 : 0;
}
