// xlle — focused OHCI register, TD, done-head, IRQ, and XID lifecycle test.
#include <algorithm>
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
    const uint64_t wdh_assertions_before = hc.wdh_assertions();
    const uint64_t wdh_w1c_before = hc.wdh_w1c_acknowledgements();
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
    CHECK(hc.ep0_trace_count() == 4);
    CHECK(hc.ep0_trace_dropped() == 0);
    const auto& trace0 = hc.ep0_trace(0);
    CHECK(trace0.address == 0 && trace0.request_type == 0x80 &&
          trace0.request == 0x06 && trace0.value == 0x0100 &&
          trace0.index == 0 && trace0.length == 64);
    CHECK(trace0.outcome == xlle::Ohci::Ep0Outcome::kComplete &&
          trace0.td_completions == 3 && trace0.last_condition_code == 0 &&
          trace0.data_bytes == 18);
    const auto& trace1 = hc.ep0_trace(1);
    CHECK(trace1.address == 0 && trace1.request_type == 0x00 &&
          trace1.request == 0x05 && trace1.value == 5 &&
          trace1.length == 0);
    CHECK(trace1.outcome == xlle::Ohci::Ep0Outcome::kComplete &&
          trace1.td_completions == 2 && trace1.last_condition_code == 0 &&
          trace1.data_bytes == 0);
    const auto& trace2 = hc.ep0_trace(2);
    CHECK(trace2.address == 5 && trace2.request_type == 0x00 &&
          trace2.request == 0x09 && trace2.value == 1 &&
          trace2.length == 0);
    CHECK(trace2.outcome == xlle::Ohci::Ep0Outcome::kComplete &&
          trace2.td_completions == 2 && trace2.last_condition_code == 0 &&
          trace2.data_bytes == 0);
    const auto& trace3 = hc.ep0_trace(3);
    CHECK(trace3.address == 5 && trace3.request_type == 0xc1 &&
          trace3.request == 0x06 && trace3.value == 0x4200 &&
          trace3.index == 0 && trace3.length == 16);
    CHECK(trace3.outcome == xlle::Ohci::Ep0Outcome::kComplete &&
          trace3.td_completions == 3 && trace3.last_condition_code == 0 &&
          trace3.data_bytes == 16);
    CHECK(hc.wdh_assertions() == wdh_assertions_before + 4);
    CHECK(hc.wdh_w1c_acknowledgements() == wdh_w1c_before + 3);
    CHECK((hc.last_done_head() & 0xfffffff0u) != 0);
    CHECK((hc.interrupt_status() & kIntrWd) != 0);
    CHECK((hc.port_status(0) & 3u) == 3u);

    // A completed trace must not remain associated with later endpoint-zero
    // traffic. This deliberately submits a stray IN TD with no active control
    // transfer; it stalls, but must not mutate the completed XID record.
    const auto trace3_before_stray = hc.ep0_trace(3);
    const uint32_t stray_ed = allocate(16);
    const uint32_t stray_td = allocate(16);
    const uint32_t stray_tail = allocate(16);
    const uint32_t stray_buffer = allocate(1);
    write_td(stray_td, kTdInitial | kTdIn | kTdR,
             stray_buffer, stray_tail, stray_buffer);
    write_ed(stray_ed, 5 | (64u << 16), stray_tail, stray_td, 0);
    put32(ram, hcca + 0x84, 0);
    hc.mmio_write(base + 0x0c, 4, 0xffffffffu);
    hc.mmio_write(base + 0x20, 4, stray_ed);
    hc.mmio_write(base + 0x08, 4, 1u << 1);
    hc.mmio_write(base + 0x04, 4, 0x90);
    hc.set_frame(++frame);
    const auto& trace3_after_stray = hc.ep0_trace(3);
    CHECK(trace3_after_stray.outcome == trace3_before_stray.outcome);
    CHECK(trace3_after_stray.td_completions ==
          trace3_before_stray.td_completions);
    CHECK(trace3_after_stray.last_condition_code ==
          trace3_before_stray.last_condition_code);
    CHECK(trace3_after_stray.data_bytes == trace3_before_stray.data_bytes);

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
    CHECK(hc.xid_endpoint2_input_reports() == 1);

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

    // XID class GET_REPORT contributes to the historical aggregate, but not
    // to the endpoint-2-only counter used by deterministic input scripts.
    const uint64_t aggregate_before_control_report = hc.xid_input_reports();
    const uint64_t endpoint2_before_control_report = hc.xid_endpoint2_input_reports();
    response.clear();
    submit_control(setup_packet(0xa1, 0x01, 0x0100, 0, 20),
                   {}, 20, response, 5);
    CHECK(hc.xid_input_reports() == aggregate_before_control_report + 1);
    CHECK(hc.xid_endpoint2_input_reports() == endpoint2_before_control_report);

    // Configured peripheral capabilities cross the same real EP0 transport.
    // In particular a keyboard-backed virtual pad must not advertise motors.
    std::array<uint8_t,18> input_caps;input_caps.fill(0xff);input_caps[1]=0;
    const std::array<uint8_t,4> output_caps{{0,0,0,0}};
    hc.set_xid_capabilities(input_caps,output_caps);
    response.clear();
    submit_control(setup_packet(0xc1,0x01,0x0100,0,20),{},20,response,5);
    CHECK(response.size()==20 && response[0]==0 && response[1]==20);
    CHECK(std::equal(input_caps.begin(),input_caps.end(),response.begin()+2));
    response.clear();
    submit_control(setup_packet(0xc1,0x01,0x0200,0,6),{},6,response,5);
    CHECK(response==std::vector<uint8_t>({0,6,0,0,0,0}));

    // The boot-facing diagnostic history is fixed-size: further valid EP0
    // transfers execute normally but cannot grow the trace.
    const size_t trace_before_fill = hc.ep0_trace_count();
    for (size_t i = trace_before_fill; i <= xlle::Ohci::kEp0TraceCapacity; ++i) {
        response.clear();
        submit_control(setup_packet(0x80, 0x08, 0, 0, 1),
                       {}, 1, response, 5);
        CHECK(response.size() == 1 && response[0] == 1);
    }
    CHECK(hc.ep0_trace_count() == xlle::Ohci::kEp0TraceCapacity);
    CHECK(hc.ep0_trace_dropped() == 1);

    // A configuration descriptor shorter than the guest's IN TD is a normal
    // USB short packet. OHCI accepts it only when TD.R is set; without R it
    // retires the data TD with DataUnderrun and halts the ED before status.
    struct ShortPacketResult {
        std::string fault;
        xlle::Ohci::Ep0Trace trace{};
        uint32_t data_flags = 0;
        uint32_t status_flags = 0;
        uint32_t ed_head = 0;
        std::array<uint8_t, 32> descriptor{};
    };
    auto run_configuration_short_packet = [&](bool rounding) {
        ShortPacketResult result;
        std::vector<uint8_t> memory(0x5000, 0);
        xlle::Ohci probe(base, [&](const std::string& why) {
            result.fault = why;
        });
        probe.set_mem_access(
            [&](uint32_t addr, void* dst, size_t n) {
                if (static_cast<uint64_t>(addr) + n > memory.size())
                    return false;
                std::memcpy(dst, memory.data() + addr, n);
                return true;
            },
            [&](uint32_t addr, const void* src, size_t n) {
                if (static_cast<uint64_t>(addr) + n > memory.size())
                    return false;
                std::memcpy(memory.data() + addr, src, n);
                return true;
            });

        constexpr uint32_t probe_hcca = 0x1000;
        constexpr uint32_t ed = 0x2000;
        constexpr uint32_t setup_td = 0x2010;
        constexpr uint32_t data_td = 0x2020;
        constexpr uint32_t status_td = 0x2030;
        constexpr uint32_t tail_td = 0x2040;
        constexpr uint32_t setup_buffer = 0x2100;
        constexpr uint32_t data_buffer = 0x2200;
        const auto setup = setup_packet(0x80, 0x06, 0x0200, 0, 80);
        std::memcpy(memory.data() + setup_buffer, setup.data(), setup.size());
        put32(memory, setup_td + 0, kTdInitial);
        put32(memory, setup_td + 4, setup_buffer);
        put32(memory, setup_td + 8, data_td);
        put32(memory, setup_td + 12, setup_buffer + 7);
        put32(memory, data_td + 0,
              kTdInitial | kTdIn | (rounding ? kTdR : 0));
        put32(memory, data_td + 4, data_buffer);
        put32(memory, data_td + 8, status_td);
        put32(memory, data_td + 12, data_buffer + 79);
        put32(memory, status_td + 0, kTdInitial | kTdOut);
        put32(memory, status_td + 8, tail_td);
        put32(memory, ed + 0, 64u << 16);
        put32(memory, ed + 4, tail_td);
        put32(memory, ed + 8, setup_td);

        probe.mmio_write(base + 0x54, 4, 1u << 4);  // reset/enable port
        probe.mmio_write(base + 0x18, 4, probe_hcca);
        probe.mmio_write(base + 0x20, 4, ed);
        probe.mmio_write(base + 0x08, 4, 1u << 1);
        probe.mmio_write(base + 0x04, 4, 0x90);
        probe.set_frame(1);

        result.data_flags = get32(memory, data_td);
        result.status_flags = get32(memory, status_td);
        result.ed_head = get32(memory, ed + 8);
        std::copy_n(memory.begin() + data_buffer, result.descriptor.size(),
                    result.descriptor.begin());
        if (probe.ep0_trace_count() == 1) result.trace = probe.ep0_trace(0);
        return result;
    };

    const auto rejected_short = run_configuration_short_packet(false);
    CHECK(rejected_short.fault.empty());
    CHECK((rejected_short.data_flags >> 28) == 9);
    CHECK((rejected_short.status_flags >> 28) == 15);
    CHECK((rejected_short.ed_head & 1u) != 0);
    CHECK((rejected_short.ed_head & 0xfffffff0u) == 0x2030u);
    CHECK(rejected_short.descriptor[0] == 9 &&
          rejected_short.descriptor[1] == 2 &&
          rejected_short.descriptor[2] == 32);
    CHECK(rejected_short.trace.outcome == xlle::Ohci::Ep0Outcome::kError);
    CHECK(rejected_short.trace.td_completions == 2);
    CHECK(rejected_short.trace.last_condition_code == 9);
    CHECK(rejected_short.trace.data_bytes == 32);
    CHECK(rejected_short.trace.terminal_td_address == 0x2020u);
    CHECK((rejected_short.trace.terminal_td_flags >> 28) == 9);
    CHECK(((rejected_short.trace.terminal_td_flags >> 18) & 1u) == 0);
    CHECK(rejected_short.trace.ed_head_after == rejected_short.ed_head);

    const auto accepted_short = run_configuration_short_packet(true);
    CHECK(accepted_short.fault.empty());
    CHECK((accepted_short.data_flags >> 28) == 0);
    CHECK((accepted_short.status_flags >> 28) == 0);
    CHECK((accepted_short.ed_head & 1u) == 0);
    CHECK((accepted_short.ed_head & 0xfffffff0u) == 0x2040u);
    CHECK(accepted_short.trace.outcome == xlle::Ohci::Ep0Outcome::kComplete);
    CHECK(accepted_short.trace.td_completions == 3);
    CHECK(accepted_short.trace.last_condition_code == 0);
    CHECK(accepted_short.trace.data_bytes == 32);

    hc.mmio_write(base + 0x08, 4, 1);  // HCR
    CHECK(hc.mmio_read(base + 0x04, 4) == 0);
    CHECK(hc.mmio_read(base + 0x00, 4) == 0x10);
    CHECK(hc.mmio_read(base + 0x54, 4) == 0x00010101u);
    CHECK(fault.empty());

    // Physical attachment is independent of host-controller reset. Detach
    // reports connection/enable changes, and reconnection must enumerate anew.
    hc.mmio_write(base + 0x54, 4, 1u << 4);
    hc.mmio_write(base + 0x54, 4, 0x1f0000);
    hc.mmio_write(base + 0x0c, 4, 0xffffffffu);
    hc.mmio_write(base + 0x10, 4, kIntrMie | kIntrRhsc);
    hc.set_xid_connected(false);
    CHECK((hc.port_status(0)&0x30003u)==0x30000u);
    CHECK(irq.back()==std::make_pair(1,true));
    CHECK(!hc.xid_address() && !hc.xid_configuration());
    hc.mmio_write(base + 0x54, 4, 0x1f0000);
    hc.mmio_write(base + 0x0c, 4, 0xffffffffu);
    hc.set_xid_connected(false);
    CHECK(!(hc.interrupt_status()&kIntrRhsc));
    hc.mmio_write(base + 0x08, 4, 1);
    CHECK(hc.port_status(0)==0x100u);
    hc.set_xid_connected(true);
    CHECK(hc.port_status(0)==0x10101u);
    hc.mmio_write(base + 0x54, 4, 1u << 4);
    CHECK((hc.port_status(0)&3)==3);
    CHECK(fault.empty());

    std::printf(fails ? "test_ohci: %d checks failed\n"
                      : "test_ohci: ok\n",
                fails);
    return fails ? 1 : 0;
}
