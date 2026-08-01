// xlle — focused NVNet reset, descriptor-pointer, MDIO, link, and IRQ lifecycle test.
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "host/ethernet_udp.hpp"
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

    // RX consumes one available descriptor per complete Ethernet frame, stores the exact
    // byte count and completion flags, advances/wraps the RX ring, and raises IRQ 0x02.
    irq.clear();
    nic.mmio_write(base + 0x004, 4, 0);
    const Descriptor rx_first{0x2100, 63, 0x8000};
    const Descriptor rx_second{0x2200, 63, 0x8000};
    std::memcpy(ram.data() + 0x1100, &rx_first, sizeof(rx_first));
    std::memcpy(ram.data() + 0x1108, &rx_second, sizeof(rx_second));
    nic.mmio_write(base + 0x104, 4, 0x1100);
    nic.mmio_write(base + 0x108, 4, 0x00010001);  // two RX and two TX descriptors
    nic.mmio_write(base + 0x144, 4, 0x10);
    nic.mmio_write(base + 0x144, 4, 0);
    nic.mmio_write(base + 0x094, 4, 1);
    nic.mmio_write(base + 0x08c, 4, 0);           // reference accepts all when MYADDR is off
    nic.mmio_write(base + 0x004, 4, 0x02);
    CHECK(nic.rx_ready());

    const std::array<uint8_t, 14> unicast{
        0x00, 0x50, 0xf2, 0x10, 0x20, 0x30, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x08, 0x00};
    CHECK(nic.receive_packet(unicast.data(), unicast.size()) ==
          xlle::NvNet::RxResult::Accepted);
    Descriptor rx_first_done{};
    std::memcpy(&rx_first_done, ram.data() + 0x1100, sizeof(rx_first_done));
    CHECK(rx_first_done.buffer == 0x2100);
    CHECK(rx_first_done.length == unicast.size());
    CHECK(rx_first_done.flags == 0x0011);
    CHECK(std::memcmp(ram.data() + 0x2100, unicast.data(), unicast.size()) == 0);
    CHECK(nic.mmio_read(base + 0x120, 4) == 0x1100);
    CHECK(nic.mmio_read(base + 0x138, 4) == 0x1108);
    CHECK(nic.tx_rx_control() == 8);
    CHECK(nic.irq_status() == 0x02);
    CHECK(irq.size() == 1 && irq[0] == std::make_pair(4, true));
    nic.mmio_write(base + 0x000, 4, 0x02);
    CHECK(irq.size() == 2 && irq[1] == std::make_pair(4, false));

    std::array<uint8_t, 14> broadcast = unicast;
    std::fill(broadcast.begin(), broadcast.begin() + 6, 0xff);
    CHECK(nic.receive_packet(broadcast.data(), broadcast.size()) ==
          xlle::NvNet::RxResult::Accepted);
    Descriptor rx_second_done{};
    std::memcpy(&rx_second_done, ram.data() + 0x1108, sizeof(rx_second_done));
    CHECK(rx_second_done.length == broadcast.size());
    CHECK(rx_second_done.flags == 0x0011);
    CHECK(std::memcmp(ram.data() + 0x2200, broadcast.data(), broadcast.size()) == 0);
    CHECK(nic.mmio_read(base + 0x120, 4) == 0x1108);
    CHECK(nic.mmio_read(base + 0x138, 4) == 0x1100);
    nic.mmio_write(base + 0x000, 4, 0x02);

    // A ring with no AVAIL descriptor backpressures the transport without IRQ or DMA.
    CHECK(!nic.rx_ready());
    CHECK(nic.receive_packet(unicast.data(), unicast.size()) ==
          xlle::NvNet::RxResult::NotReady);
    CHECK(nic.irq_status() == 0);

    // MYADDR filtering does not consume a descriptor. Oversized frames are likewise
    // dropped before DMA, while a matching destination completes the re-armed descriptor.
    std::memcpy(ram.data() + 0x1100, &rx_first, sizeof(rx_first));
    nic.mmio_write(base + 0x0a8, 4, 0x13121110);
    nic.mmio_write(base + 0x0ac, 4, 0x00001514);
    nic.mmio_write(base + 0x0b0, 4, 0xffffffff);
    nic.mmio_write(base + 0x0b4, 4, 0xffffffff);
    nic.mmio_write(base + 0x08c, 4, 0x20);
    CHECK(nic.receive_packet(unicast.data(), unicast.size()) ==
          xlle::NvNet::RxResult::Filtered);
    std::vector<uint8_t> oversized(1500 + 129, 0xff);
    CHECK(nic.receive_packet(oversized.data(), oversized.size()) ==
          xlle::NvNet::RxResult::Oversized);
    std::array<uint8_t, 14> matching = unicast;
    for (size_t i = 0; i < 6; ++i) matching[i] = static_cast<uint8_t>(0x10 + i);
    CHECK(nic.receive_packet(matching.data(), matching.size()) ==
          xlle::NvNet::RxResult::Accepted);
    CHECK(nic.rx_offers() == 6);
    CHECK(nic.rx_descriptors() == 3);
    CHECK(nic.rx_packets() == 3);
    CHECK(nic.rx_bytes() == unicast.size() + broadcast.size() + matching.size());
    CHECK(nic.rx_filtered() == 1);
    CHECK(nic.rx_oversized() == 1);
    CHECK(nic.rx_not_ready() == 1);
    CHECK(fault.empty());

    // The optional host transport preserves one complete Ethernet frame per UDP datagram.
    xlle::EthernetUdpTransport tunnel_a;
    xlle::EthernetUdpTransport tunnel_b;
    std::string network_error;
    CHECK(tunnel_a.bind(0, network_error));
    CHECK(network_error.empty());
    CHECK(tunnel_b.bind(0, network_error));
    CHECK(tunnel_a.local_port() != 0 && tunnel_b.local_port() != 0);
    CHECK(tunnel_a.set_peer("127.0.0.1", tunnel_b.local_port(), network_error));
    CHECK(tunnel_b.set_peer("127.0.0.1", tunnel_a.local_port(), network_error));
    CHECK(tunnel_a.send(unicast.data(), unicast.size(), network_error));
    std::array<uint8_t, 65536> tunnel_frame{};
    int tunnel_size = 0;
    for (int attempt = 0; attempt < 100 && tunnel_size == 0; ++attempt) {
        tunnel_size = tunnel_b.receive(tunnel_frame.data(), tunnel_frame.size(), network_error);
        if (tunnel_size == 0) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK(tunnel_size == static_cast<int>(unicast.size()));
    CHECK(std::memcmp(tunnel_frame.data(), unicast.data(), unicast.size()) == 0);
    CHECK(tunnel_a.tx_packets() == 1 && tunnel_a.tx_bytes() == unicast.size());
    CHECK(tunnel_b.rx_packets() == 1 && tunnel_b.rx_bytes() == unicast.size());
    CHECK(network_error.empty());

    // The host mapping is page-padded, but hardware still decodes only 0x400 bytes.
    std::string padding_fault;
    xlle::NvNet padding([&](const std::string& why) { padding_fault = why; });
    CHECK(padding.mmio_read(base + 0x400, 4) == 0xffffffffu);
    CHECK(!padding_fault.empty());

    std::printf(fails ? "test_nvnet: %d checks failed\n" : "test_nvnet: ok\n", fails);
    return fails ? 1 : 0;
}
