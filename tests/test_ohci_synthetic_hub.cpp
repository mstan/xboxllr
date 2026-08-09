// Focused mechanics fixture for the explicitly non-retail synthetic USB hub.
#include <array>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "machine/ohci.hpp"

static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL: %s (line %d)\n", #c, __LINE__); ++fails; } } while (0)

static void put32(std::vector<uint8_t>& memory, uint32_t address, uint32_t value) {
    std::memcpy(memory.data() + address, &value, sizeof(value));
}
static uint32_t get32(const std::vector<uint8_t>& memory, uint32_t address) {
    uint32_t value = 0;
    std::memcpy(&value, memory.data() + address, sizeof(value));
    return value;
}
static std::array<uint8_t, 8> setup_packet(uint8_t type, uint8_t request,
                                             uint16_t value, uint16_t index,
                                             uint16_t length) {
    return {{type, request, static_cast<uint8_t>(value),
             static_cast<uint8_t>(value >> 8), static_cast<uint8_t>(index),
             static_cast<uint8_t>(index >> 8), static_cast<uint8_t>(length),
             static_cast<uint8_t>(length >> 8)}};
}

int main() {
    constexpr uint32_t base = 0xfed00000u;
    constexpr uint32_t hcca = 0x1000;
    constexpr uint32_t kTdR = 1u << 18;
    constexpr uint32_t kTdIn = 2u << 19;
    constexpr uint32_t kTdOut = 1u << 19;
    constexpr uint32_t kTdInitial = 0xfu << 28;
    std::string fault;
    std::vector<uint8_t> ram(0x10000, 0);
    xlle::Ohci hc(base, [&](const std::string& why) { fault = why; },
                  xlle::Ohci::UsbTopology::kSyntheticControllerSHubForTest);
    hc.set_mem_access(
        [&](uint32_t address, void* dst, size_t size) {
            if (static_cast<uint64_t>(address) + size > ram.size()) return false;
            std::memcpy(dst, ram.data() + address, size);
            return true;
        },
        [&](uint32_t address, const void* src, size_t size) {
            if (static_cast<uint64_t>(address) + size > ram.size()) return false;
            std::memcpy(ram.data() + address, src, size);
            return true;
        });

    uint32_t next = 0x2000;
    auto allocate = [&](uint32_t size) {
        next = (next + 15u) & ~15u;
        const uint32_t result = next;
        next += (size + 15u) & ~15u;
        return result;
    };
    auto write_td = [&](uint32_t address, uint32_t flags, uint32_t cbp,
                        uint32_t next_td, uint32_t be) {
        put32(ram, address, flags); put32(ram, address + 4, cbp);
        put32(ram, address + 8, next_td); put32(ram, address + 12, be);
    };
    auto write_ed = [&](uint32_t address, uint32_t flags, uint32_t tail,
                        uint32_t head, uint32_t next_ed) {
        put32(ram, address, flags); put32(ram, address + 4, tail);
        put32(ram, address + 8, head); put32(ram, address + 12, next_ed);
    };
    uint64_t frame = 0;
    struct ControlResult { uint32_t setup_flags; std::vector<uint8_t> input; };
    auto control = [&](const std::array<uint8_t, 8>& setup, uint8_t address) {
        const bool in = (setup[0] & 0x80u) != 0;
        const uint16_t length = static_cast<uint16_t>(setup[6] | (setup[7] << 8));
        const uint32_t ed = allocate(16), setup_td = allocate(16);
        const uint32_t data_td = length ? allocate(16) : 0;
        const uint32_t status_td = allocate(16), tail_td = allocate(16);
        const uint32_t setup_buffer = allocate(8);
        std::memcpy(ram.data() + setup_buffer, setup.data(), setup.size());
        uint32_t data_buffer = 0;
        if (length) {
            data_buffer = allocate(length);
            write_td(data_td, kTdInitial | (in ? kTdIn | kTdR : kTdOut),
                     data_buffer, status_td, data_buffer + length - 1);
        }
        write_td(setup_td, kTdInitial, setup_buffer,
                 data_td ? data_td : status_td, setup_buffer + 7);
        write_td(status_td, kTdInitial | (in ? kTdOut : kTdIn), 0, tail_td, 0);
        write_ed(ed, address | (64u << 16), tail_td, setup_td, 0);
        put32(ram, hcca + 0x84, 0);
        hc.mmio_write(base + 0x18, 4, hcca);
        hc.mmio_write(base + 0x20, 4, ed);
        hc.mmio_write(base + 0x08, 4, 1u << 1);
        hc.mmio_write(base + 0x04, 4, 0x90);
        hc.set_frame(++frame);
        ControlResult result{get32(ram, setup_td), {}};
        if (in && length)
            result.input.assign(ram.begin() + data_buffer,
                                ram.begin() + data_buffer + length);
        return result;
    };
    auto ok = [](const ControlResult& result) { return (result.setup_flags >> 28) == 0; };

    // The root port exposes only the synthetic hub.  Child address zero is
    // invisible until a hub-port reset enables it.
    CHECK((hc.port_status(0) & 0x00010101u) == 0x00010101u);
    hc.mmio_write(base + 0x54, 4, 1u << 4);
    CHECK((hc.port_status(0) & 3u) == 3u);
    const auto hub_device = control(setup_packet(0x80, 0x06, 0x0100, 0, 18), 0);
    CHECK(ok(hub_device));
    CHECK(hub_device.input[0] == 18 && hub_device.input[4] == 9);
    CHECK(hub_device.input[8] == 0 && hub_device.input[9] == 0 &&
          hub_device.input[10] == 0 && hub_device.input[11] == 0);
    const auto child_before_reset = control(setup_packet(0xc1, 0x06, 0x4200, 0, 16), 0);
    CHECK((child_before_reset.setup_flags >> 28) == 4);

    CHECK(ok(control(setup_packet(0x00, 0x05, 7, 0, 0), 0)));
    CHECK(hc.synthetic_hub_address() == 7 && hc.xid_address() == 0);
    const auto hub_config = control(setup_packet(0x80, 0x06, 0x0200, 0, 25), 7);
    CHECK(ok(hub_config) && hub_config.input[0] == 9 && hub_config.input[2] == 25 &&
          hub_config.input[14] == 9 && hub_config.input[18] == 7);
    CHECK(ok(control(setup_packet(0x00, 0x09, 1, 0, 0), 7)));
    CHECK(hc.synthetic_hub_configuration() == 1 && hc.xid_configuration() == 0);
    const auto hub_class = control(setup_packet(0xa0, 0x06, 0x2900, 0, 9), 7);
    CHECK(ok(hub_class) && hub_class.input[0] == 9 && hub_class.input[1] == 0x29 &&
          hub_class.input[2] == 3 && hub_class.input[3] == 0x02 &&
          hub_class.input[4] == 0x00);
    const auto hub_status = control(setup_packet(0xa0, 0x00, 0, 0, 4), 7);
    CHECK(ok(hub_status) && hub_status.input == std::vector<uint8_t>(4, 0));
    const auto port1_before = control(setup_packet(0xa3, 0x00, 0, 1, 4), 7);
    CHECK(ok(port1_before) && (port1_before.input[0] & 1u) &&
          !(port1_before.input[0] & 2u) && (port1_before.input[2] & 1u));
    CHECK(ok(control(setup_packet(0x23, 0x01, 16, 1, 0), 7)));
    const auto port1_csc_cleared = control(setup_packet(0xa3, 0x00, 0, 1, 4), 7);
    CHECK(ok(port1_csc_cleared) && (port1_csc_cleared.input[0] & 1u) &&
          !(port1_csc_cleared.input[2] & 1u));
    const auto port2 = control(setup_packet(0xa3, 0x00, 0, 2, 4), 7);
    const auto port3 = control(setup_packet(0xa3, 0x00, 0, 3, 4), 7);
    CHECK(ok(port2) && ok(port3) && port2.input == std::vector<uint8_t>(4, 0) &&
          port3.input == std::vector<uint8_t>(4, 0));

    // Reset enables downstream port 1 and starts a fresh child address-zero
    // enumeration phase.  EP1 advertises that port change to the guest.
    CHECK(ok(control(setup_packet(0x23, 0x03, 4, 1, 0), 7)));
    CHECK((hc.synthetic_hub_port_status(0) & 0x00120003u) == 0x00120003u);
    const uint32_t ep1_ed = allocate(16), ep1_td = allocate(16), ep1_tail = allocate(16);
    const uint32_t ep1_buffer = allocate(1);
    write_td(ep1_td, kTdInitial | kTdIn | kTdR, ep1_buffer, ep1_tail, ep1_buffer);
    write_ed(ep1_ed, 7u | (1u << 7) | (2u << 11) | (1u << 16), ep1_tail, ep1_td, 0);
    for (uint32_t i = 0; i < 32; ++i) put32(ram, hcca + i * 4, ep1_ed);
    hc.mmio_write(base + 0x18, 4, hcca);
    hc.mmio_write(base + 0x04, 4, 0x94);
    hc.set_frame(++frame);
    CHECK((get32(ram, ep1_td) >> 28) == 0 && ram[ep1_buffer] == 0x02);

    const auto child_device = control(setup_packet(0x80, 0x06, 0x0100, 0, 18), 0);
    CHECK(ok(child_device) && child_device.input[8] == 0x5e && child_device.input[10] == 0x89);
    CHECK(ok(control(setup_packet(0x00, 0x05, 9, 0, 0), 0)));
    CHECK(hc.synthetic_hub_address() == 7 && hc.xid_address() == 9);
    const auto zero_after_address = control(setup_packet(0x80, 0x06, 0x0100, 0, 18), 0);
    CHECK((zero_after_address.setup_flags >> 28) == 15);  // NAK leaves TD pending.
    CHECK(ok(control(setup_packet(0x00, 0x09, 1, 0, 0), 9)));
    const auto xid_vendor = control(setup_packet(0xc1, 0x06, 0x4200, 0, 16), 9);
    CHECK(ok(xid_vendor) && xid_vendor.input[0] == 0x10 && xid_vendor.input[1] == 0x42);

    // Host state alone never produces a report; the configured child is read
    // only when the guest actually schedules its periodic endpoint-2 TD.
    xlle::Ohci::XidGamepadState state;
    state.buttons = 0x0005;  // D-pad up + left
    state.analog[0] = 0xff;  // A
    state.analog[1] = 0xff;  // B
    hc.set_xid_gamepad_state(state);
    CHECK(hc.xid_input_reports() == 0);
    const uint32_t xid_ed = allocate(16), xid_td = allocate(16), xid_tail = allocate(16);
    const uint32_t xid_buffer = allocate(20);
    write_td(xid_td, kTdInitial | kTdIn | kTdR, xid_buffer, xid_tail, xid_buffer + 19);
    write_ed(xid_ed, 9u | (2u << 7) | (2u << 11) | (32u << 16), xid_tail, xid_td, 0);
    for (uint32_t i = 0; i < 32; ++i) put32(ram, hcca + i * 4, xid_ed);
    hc.mmio_write(base + 0x18, 4, hcca);
    hc.mmio_write(base + 0x04, 4, 0x94);
    hc.set_frame(++frame);
    CHECK((get32(ram, xid_td) >> 28) == 0 && ram[xid_buffer + 0] == 0 &&
          ram[xid_buffer + 1] == 20 && ram[xid_buffer + 2] == 5 &&
          ram[xid_buffer + 3] == 0 && ram[xid_buffer + 4] == 0xff &&
          ram[xid_buffer + 5] == 0xff && hc.xid_input_reports() == 1);

    // A later downstream reset is independent of the configured hub and
    // returns the child to address zero for a new enumeration.
    CHECK(ok(control(setup_packet(0x23, 0x03, 4, 1, 0), 7)));
    CHECK(hc.synthetic_hub_address() == 7 && hc.synthetic_hub_configuration() == 1 &&
          hc.xid_address() == 0 && hc.xid_configuration() == 0);
    const auto child_after_reset = control(setup_packet(0x80, 0x06, 0x0100, 0, 18), 0);
    CHECK(ok(child_after_reset) && child_after_reset.input[8] == 0x5e &&
          child_after_reset.input[10] == 0x89);
    CHECK(fault.empty());
    std::printf(fails ? "test_ohci_synthetic_hub: %d checks failed\n"
                      : "test_ohci_synthetic_hub: ok\n", fails);
    return fails ? 1 : 0;
}
