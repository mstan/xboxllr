// xlle — focused OHCI register, TD, done-head, IRQ, and XID lifecycle test.
#include <array>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "machine/ohci.hpp"

static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL: %s (line %d)\n", #c, __LINE__); ++fails; } } while (0)

static void put32(std::vector<uint8_t>& memory, uint32_t address,
                  uint32_t value) {
    std::memcpy(memory.data() + address, &value, sizeof(value));
}

static uint32_t get32(const std::vector<uint8_t>& memory, uint32_t address) {
    uint32_t value = 0;
    std::memcpy(&value, memory.data() + address, sizeof(value));
    return value;
}

static std::array<uint8_t, 8> setup_packet(
    uint8_t request_type, uint8_t request, uint16_t value,
    uint16_t index, uint16_t length) {
    return {{
        request_type, request,
        static_cast<uint8_t>(value), static_cast<uint8_t>(value >> 8),
        static_cast<uint8_t>(index), static_cast<uint8_t>(index >> 8),
        static_cast<uint8_t>(length), static_cast<uint8_t>(length >> 8),
    }};
}

int main() {
    constexpr uint32_t base = 0xfed00000u;
    constexpr uint32_t hcca = 0x1000;
    constexpr uint32_t kIntrWd = 1u << 1;
    constexpr uint32_t kIntrSf = 1u << 2;
    constexpr uint32_t kIntrRhsc = 1u << 6;
    constexpr uint32_t kIntrMie = 1u << 31;
    constexpr uint32_t kTdR = 1u << 18;
    constexpr uint32_t kTdIn = 2u << 19;
    constexpr uint32_t kTdOut = 1u << 19;
    constexpr uint32_t kTdInitial = 0xfu << 28;

    std::string fault;
    std::vector<std::pair<int, bool>> irq;
    std::vector<uint8_t> ram(0x20000, 0);
    xlle::Ohci hc(base, [&](const std::string& why) { fault = why; });
    hc.set_mem_access(
        [&](uint32_t addr, void* dst, size_t n) {
            if (static_cast<uint64_t>(addr) + n > ram.size()) return false;
            std::memcpy(dst, ram.data() + addr, n);
            return true;
        },
        [&](uint32_t addr, const void* src, size_t n) {
            if (static_cast<uint64_t>(addr) + n > ram.size()) return false;
            std::memcpy(ram.data() + addr, src, n);
            return true;
        });
    hc.set_irq([&](int line, bool level) { irq.emplace_back(line, level); }, 1);

    CHECK(hc.mmio_read(base + 0x00, 4) == 0x10);
    CHECK(hc.mmio_read(base + 0x34, 4) == 0x27782edf);
    CHECK(hc.mmio_read(base + 0x48, 4) == 0x1204);
    CHECK(hc.mmio_read(base + 0x54, 4) == 0x00010101u);

    // The attached Controller S raises a root-hub connect change. Clear it,
    // reset/enable the port, then clear the reset-complete change.
    hc.mmio_write(base + 0x10, 4, kIntrMie | kIntrRhsc);
    CHECK(irq.size() == 1 && irq.back() == std::make_pair(1, true));
    hc.mmio_write(base + 0x54, 4, 1u << 16);
    hc.mmio_write(base + 0x0c, 4, kIntrRhsc);
    CHECK(irq.back() == std::make_pair(1, false));
    hc.mmio_write(base + 0x54, 4, 1u << 4);
    CHECK((hc.mmio_read(base + 0x54, 4) & 0x00100103u) ==
          0x00100103u);
    CHECK(irq.back() == std::make_pair(1, true));
    hc.mmio_write(base + 0x54, 4, 1u << 20);
    hc.mmio_write(base + 0x0c, 4, kIntrRhsc);
    CHECK(irq.back() == std::make_pair(1, false));

    // Base frame/HCCA/SF lifecycle.
    hc.mmio_write(base + 0x18, 4, hcca);
    hc.mmio_write(base + 0x10, 4, kIntrMie | kIntrSf);
    hc.mmio_write(base + 0x04, 4, 0x80);  // operational
    uint64_t frame = 1;
    hc.set_frame(frame);
    uint16_t frame_number = 0;
    std::memcpy(&frame_number, ram.data() + hcca + 0x80,
                sizeof(frame_number));
    CHECK(frame_number == 1);
    CHECK(hc.frames() == 1);
    CHECK((hc.mmio_read(base + 0x0c, 4) & kIntrSf) != 0);
    CHECK(irq.back() == std::make_pair(1, true));
    hc.mmio_write(base + 0x0c, 4, 0xffffffffu);
    hc.mmio_write(base + 0x14, 4, 0xffffffffu);
    hc.mmio_write(base + 0x10, 4, kIntrMie | kIntrWd);
    CHECK(irq.back() == std::make_pair(1, false));

    uint32_t allocation = 0x2000;
    auto allocate = [&](uint32_t size) {
        allocation = (allocation + 15u) & ~15u;
        const uint32_t address = allocation;
        allocation += (size + 15u) & ~15u;
        return address;
    };
    auto write_td = [&](uint32_t address, uint32_t flags, uint32_t cbp,
                        uint32_t next, uint32_t be) {
        put32(ram, address + 0, flags);
        put32(ram, address + 4, cbp);
        put32(ram, address + 8, next);
        put32(ram, address + 12, be);
    };
    auto write_ed = [&](uint32_t address, uint32_t flags, uint32_t tail,
                        uint32_t head, uint32_t next) {
        put32(ram, address + 0, flags);
        put32(ram, address + 4, tail);
        put32(ram, address + 8, head);
        put32(ram, address + 12, next);
    };

    auto submit_control =
        [&](const std::array<uint8_t, 8>& setup,
            const std::vector<uint8_t>& output_data,
            size_t input_capacity, std::vector<uint8_t>& input_data,
            uint8_t function_address) {
            const bool direction_in = (setup[0] & 0x80u) != 0;
            const uint16_t transfer_length =
                static_cast<uint16_t>(setup[6] | (setup[7] << 8));
            const uint32_t ed = allocate(16);
            const uint32_t setup_td = allocate(16);
            const uint32_t data_td =
                transfer_length ? allocate(16) : 0;
            const uint32_t status_td = allocate(16);
            const uint32_t tail_td = allocate(16);
            const uint32_t setup_buffer = allocate(8);
            std::memcpy(ram.data() + setup_buffer, setup.data(), setup.size());

            uint32_t data_buffer = 0;
            if (transfer_length) {
                const size_t buffer_size =
                    direction_in ? input_capacity : output_data.size();
                data_buffer = allocate(static_cast<uint32_t>(buffer_size));
                if (!direction_in && buffer_size)
                    std::memcpy(ram.data() + data_buffer,
                                output_data.data(), buffer_size);
                write_td(data_td,
                         kTdInitial |
                             (direction_in ? (kTdIn | kTdR) : kTdOut),
                         data_buffer, status_td,
                         data_buffer + static_cast<uint32_t>(buffer_size) - 1);
            }
            write_td(setup_td, kTdInitial, setup_buffer,
                     data_td ? data_td : status_td, setup_buffer + 7);
            write_td(status_td,
                     kTdInitial | (direction_in ? kTdOut : kTdIn),
                     0, tail_td, 0);
            write_ed(ed,
                     function_address | (64u << 16),
                     tail_td, setup_td, 0);

            put32(ram, hcca + 0x84, 0);
            hc.mmio_write(base + 0x0c, 4, 0xffffffffu);
            hc.mmio_write(base + 0x20, 4, ed);
            hc.mmio_write(base + 0x08, 4, 1u << 1);  // CLF
            hc.mmio_write(base + 0x04, 4, 0x90);      // operational + CLE
            hc.set_frame(++frame);

            CHECK(fault.empty());
            CHECK((get32(ram, setup_td) >> 28) == 0);
            CHECK((get32(ram, status_td) >> 28) == 0);
            CHECK((get32(ram, ed + 8) & 0xfffffff0u) == tail_td);
            CHECK((get32(ram, hcca + 0x84) & 0xfffffff0u) == status_td);
            CHECK((hc.mmio_read(base + 0x0c, 4) & kIntrWd) != 0);
            if (transfer_length) {
                CHECK((get32(ram, data_td) >> 28) == 0);
                if (direction_in) {
                    input_data.assign(ram.begin() + data_buffer,
                                      ram.begin() + data_buffer +
                                          input_capacity);
                }
            }
        };

    // Enumerate through ordinary control TD chains.
    std::vector<uint8_t> response;
    submit_control(setup_packet(0x80, 0x06, 0x0100, 0, 64),
                   {}, 64, response, 0);
    CHECK(response.size() == 64);
    CHECK(response[0] == 18 && response[1] == 1);
    CHECK(response[8] == 0x5e && response[9] == 0x04);
    CHECK(response[10] == 0x89 && response[11] == 0x02);

    response.clear();
    submit_control(setup_packet(0x00, 0x05, 5, 0, 0),
                   {}, 0, response, 0);
    CHECK(hc.xid_address() == 5);
    submit_control(setup_packet(0x00, 0x09, 1, 0, 0),
                   {}, 0, response, 5);
    CHECK(hc.xid_configuration() == 1);
    submit_control(setup_packet(0xc1, 0x06, 0x4200, 0, 16),
                   {}, 16, response, 5);
    CHECK(response[0] == 0x10 && response[1] == 0x42);
    CHECK(response[4] == 1 && response[5] == 2);
    CHECK(response[6] == 20 && response[7] == 6);
    CHECK(hc.control_requests() == 4);

    // Exercise the bulk-list walker with a configured endpoint-2 IN report.
    xlle::Ohci::XidGamepadState state;
    state.buttons = 0x1234;
    state.analog = {{1, 2, 3, 4, 5, 6, 7, 8}};
    state.thumb_lx = 0x1122;
    state.thumb_ly = -2;
    state.thumb_rx = 0x3344;
    state.thumb_ry = -4;
    hc.set_xid_gamepad_state(state);

    const uint32_t bulk_ed = allocate(16);
    const uint32_t bulk_td = allocate(16);
    const uint32_t bulk_tail = allocate(16);
    const uint32_t input_buffer = allocate(20);
    write_td(bulk_td, kTdInitial | kTdIn | kTdR,
             input_buffer, bulk_tail, input_buffer + 19);
    write_ed(bulk_ed, 5u | (2u << 7) | (2u << 11) | (32u << 16),
             bulk_tail, bulk_td, 0);
    put32(ram, hcca + 0x84, 0);
    hc.mmio_write(base + 0x0c, 4, 0xffffffffu);
    hc.mmio_write(base + 0x28, 4, bulk_ed);
    hc.mmio_write(base + 0x08, 4, 1u << 2);  // BLF
    hc.mmio_write(base + 0x04, 4, 0xb0);      // operational + CLE + BLE
    hc.set_frame(++frame);
    CHECK(fault.empty());
    CHECK((get32(ram, bulk_td) >> 28) == 0);
    CHECK(ram[input_buffer + 0] == 0 && ram[input_buffer + 1] == 20);
    CHECK(ram[input_buffer + 2] == 0x34 &&
          ram[input_buffer + 3] == 0x12);
    for (size_t i = 0; i < state.analog.size(); ++i)
        CHECK(ram[input_buffer + 4 + i] == state.analog[i]);
    CHECK(ram[input_buffer + 12] == 0x22 &&
          ram[input_buffer + 13] == 0x11);
    CHECK(hc.xid_input_reports() == 1);

    // Exercise the periodic HCCA list and endpoint-2 OUT rumble report.
    const uint32_t periodic_ed = allocate(16);
    const uint32_t periodic_td = allocate(16);
    const uint32_t periodic_tail = allocate(16);
    const uint32_t output_buffer = allocate(6);
    const std::array<uint8_t, 6> rumble{{0, 6, 0x34, 0x12, 0xcd, 0xab}};
    std::memcpy(ram.data() + output_buffer, rumble.data(), rumble.size());
    write_td(periodic_td, kTdInitial | kTdOut,
             output_buffer, periodic_tail, output_buffer + 5);
    write_ed(periodic_ed, 5u | (2u << 7) | (1u << 11) | (32u << 16),
             periodic_tail, periodic_td, 0);
    for (uint32_t i = 0; i < 32; ++i)
        put32(ram, hcca + i * 4, periodic_ed);
    hc.mmio_write(base + 0x0c, 4, 0xffffffffu);
    hc.mmio_write(base + 0x04, 4, 0xb4);  // operational + PLE/CLE/BLE
    hc.set_frame(++frame);
    CHECK(fault.empty());
    CHECK((get32(ram, periodic_td) >> 28) == 0);
    CHECK(hc.xid_output_reports() == 1);
    CHECK(hc.xid_left_rumble() == 0x1234);
    CHECK(hc.xid_right_rumble() == 0xabcd);
    CHECK(hc.processed_eds() >= 7);
    CHECK(hc.processed_tds() == hc.retired_tds());
    CHECK(hc.done_writebacks() >= 6);

    hc.mmio_write(base + 0x08, 4, 1);  // HCR
    CHECK(hc.mmio_read(base + 0x04, 4) == 0);
    CHECK(hc.mmio_read(base + 0x00, 4) == 0x10);
    CHECK(hc.mmio_read(base + 0x54, 4) == 0x00010101u);
    CHECK(fault.empty());

    std::printf(fails ? "test_ohci: %d checks failed\n"
                      : "test_ohci: ok\n",
                fails);
    return fails ? 1 : 0;
}
