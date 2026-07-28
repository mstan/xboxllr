// xlle — focused NVNet reset, descriptor-pointer, MDIO, link, and IRQ lifecycle test.
#include <array>
#include <cstdio>
#include <cstring>
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

    // Two TX fragments form one packet. Completion clears VALID/error flags, wraps the
    // descriptor pointer, restores IDLE, and raises the masked TX interrupt.
    nic.mmio_write(base + 0x004, 4, 0);
    nic.set_link(true);
    nic.mmio_write(base + 0x180, 4, 8);
    nic.mmio_write(base + 0x000, 4, 0x40);
    irq.clear();
    CHECK(nic.link_up());
    struct Descriptor {
        uint32_t buffer;
        uint16_t length;
        uint16_t flags;
    };
    static_assert(sizeof(Descriptor) == 8, "test descriptor layout");
    std::array<uint8_t, 0x4000> ram{};
    std::vector<uint8_t> packet;
    nic.set_mem_access(
        [&](uint32_t addr, void* dst, size_t size) {
            if (static_cast<uint64_t>(addr) + size > ram.size()) return false;
            std::memcpy(dst, ram.data() + addr, size);
            return true;
        },
        [&](uint32_t addr, const void* src, size_t size) {
            if (static_cast<uint64_t>(addr) + size > ram.size()) return false;
            std::memcpy(ram.data() + addr, src, size);
            return true;
        });
    nic.set_packet_sink(
        [&](const uint8_t* data, size_t size) { packet.assign(data, data + size); });
    const Descriptor first{0x2000, 2, static_cast<uint16_t>(0x8000 | 0x4000)};
    const Descriptor last{0x2010, 1, static_cast<uint16_t>(0x8000 | 0x2000 | 1)};
    std::memcpy(ram.data() + 0x1000, &first, sizeof(first));
    std::memcpy(ram.data() + 0x1008, &last, sizeof(last));
    std::memcpy(ram.data() + 0x2000, "abc", 3);
    std::memcpy(ram.data() + 0x2010, "de", 2);
    nic.mmio_write(base + 0x100, 4, 0x1000);
    nic.mmio_write(base + 0x108, 4, 1);       // two TX descriptors
    nic.mmio_write(base + 0x144, 4, 0x10);    // reset ring pointers
    nic.mmio_write(base + 0x084, 4, 1);       // transmitter start
    nic.mmio_write(base + 0x004, 4, 0x10);    // TX IRQ mask
    nic.mmio_write(base + 0x144, 4, 1);       // kick
    Descriptor first_done{}, last_done{};
    std::memcpy(&first_done, ram.data() + 0x1000, sizeof(first_done));
    std::memcpy(&last_done, ram.data() + 0x1008, sizeof(last_done));
    CHECK(packet == std::vector<uint8_t>({'a', 'b', 'c', 'd', 'e'}));
    CHECK(first_done.flags == 0);
    CHECK(last_done.flags == 1);
    CHECK(nic.tx_kicks() == 1);
    CHECK(nic.tx_descriptors() == 2);
    CHECK(nic.tx_packets() == 1);
    CHECK(nic.tx_bytes() == 5);
    CHECK(nic.mmio_read(base + 0x11c, 4) == 0x1008);
    CHECK(nic.mmio_read(base + 0x134, 4) == 0x1000);
    CHECK(nic.tx_rx_control() == 9);
    CHECK(nic.irq_status() == 0x10);
    CHECK(irq.size() == 1 && irq[0] == std::make_pair(4, true));
    nic.mmio_write(base + 0x000, 4, 0x10);
    CHECK(irq.size() == 2 && irq[1] == std::make_pair(4, false));
    nic.mmio_write(base + 0x144, 4, 1);  // empty-ring kick, as exercised by the dashboard
    CHECK(nic.tx_kicks() == 2);
    CHECK(nic.tx_descriptors() == 2);
    CHECK(nic.tx_packets() == 1);
    CHECK(nic.irq_status() == 0);
    CHECK(nic.tx_rx_control() == 9);
    CHECK(irq.size() == 2);
    CHECK(fault.empty());

    // The host mapping is page-padded, but hardware still decodes only 0x400 bytes.
    std::string padding_fault;
    xlle::NvNet padding([&](const std::string& why) { padding_fault = why; });
    CHECK(padding.mmio_read(base + 0x400, 4) == 0xffffffffu);
    CHECK(!padding_fault.empty());

    std::printf(fails ? "test_nvnet: %d checks failed\n" : "test_nvnet: ok\n", fails);
    return fails ? 1 : 0;
}
