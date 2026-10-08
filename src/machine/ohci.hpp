// xlle — MCPX USB 1.1 OHCI host-controller register/timing floor.
//
// The Xbox exposes two standard PCI OHCI controllers at 0xFED00000 and 0xFED08000, each
// with four root-hub ports. This model implements the standard register lifecycle, 1 kHz
// frame counter/HCCA writeback, general-TD scheduling, and level interrupt aggregation.
// OHCI0 port 1 carries a full-speed Xbox Controller S XID peripheral; OHCI1 remains empty.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "machine/io_device.hpp"
#include "machine/mmio_device.hpp"

namespace xlle {

class Ohci : public MmioDevice {
public:
    static constexpr uint32_t kSize = 0x1000;

    // The default is the replay-observed, root-attached XID device.  The hub
    // profile exists only for focused OHCI mechanics tests; its descriptor is
    // deliberately synthetic and carries no claimed retail identity.
    enum class UsbTopology : uint8_t {
        kDirectXid,
        kSyntheticControllerSHubForTest,
    };

    struct XidGamepadState {
        uint16_t buttons = 0;
        std::array<uint8_t, 8> analog{};
        int16_t thumb_lx = 0;
        int16_t thumb_ly = 0;
        int16_t thumb_rx = 0;
        int16_t thumb_ry = 0;
    };

    // Capped, passive trace of guest EP0 setup traffic.  This deliberately
    // records only traffic the guest has already scheduled; it never creates
    // endpoint work or changes transfer completion behavior.
    enum class Ep0Outcome : uint8_t {
        kPending,
        kComplete,
        kStalled,
        kError,
    };
    struct Ep0Trace {
        uint8_t address = 0;
        uint8_t request_type = 0;
        uint8_t request = 0;
        uint16_t value = 0;
        uint16_t index = 0;
        uint16_t length = 0;
        Ep0Outcome outcome = Ep0Outcome::kPending;
        uint8_t td_completions = 0;
        uint8_t last_condition_code = 0;
        uint32_t data_bytes = 0;
        uint32_t terminal_td_address = 0;
        uint32_t terminal_td_flags = 0;
        uint32_t ed_head_after = 0;
    };
    static constexpr size_t kEp0TraceCapacity = 32;

    Ohci(uint32_t base, FaultFn fault,
         UsbTopology topology = UsbTopology::kDirectXid)
        : base_(base), fault_(std::move(fault)), topology_(topology) {
        xid_attached_ = base_ == 0xFED00000u;
        reset();
    }

    const char* name() const override { return base_ == 0xFED00000u ? "ohci0" : "ohci1"; }
    uint32_t mmio_base() const override { return base_; }
    uint32_t mmio_size() const override { return kSize; }
    uint32_t mmio_read(uint32_t addr, int size) override;
    void mmio_write(uint32_t addr, int size, uint32_t value) override;

    void set_mem_access(
        std::function<bool(uint32_t, void*, size_t)> read,
        std::function<bool(uint32_t, const void*, size_t)> write) {
        mem_read_ = std::move(read);
        mem_write_ = std::move(write);
    }
    void set_irq(std::function<void(int, bool)> fn, int irq) {
        irq_fn_ = std::move(fn);
        irq_num_ = irq;
        update_irq();
    }
    void set_frame(uint64_t frame);
    void set_xid_gamepad_state(const XidGamepadState& state) {
        xid_state_ = state;
    }
    void set_xid_connected(bool connected);

    uint64_t frames() const { return frames_; }
    uint32_t control() const { return regs_[1]; }
    uint32_t hcca() const { return regs_[6]; }
    uint64_t processed_eds() const { return processed_eds_; }
    uint64_t processed_tds() const { return processed_tds_; }
    uint64_t retired_tds() const { return retired_tds_; }
    uint64_t done_writebacks() const { return done_writebacks_; }
    uint64_t control_requests() const { return control_requests_; }
    uint64_t xid_input_reports() const { return xid_input_reports_; }
    // Endpoint-2 IN reports only.  Keep xid_input_reports() as the historical
    // aggregate, which also includes XID class GET_REPORT control requests.
    uint64_t xid_endpoint2_input_reports() const {
        return xid_endpoint2_input_reports_;
    }
    uint64_t xid_output_reports() const { return xid_output_reports_; }
    uint16_t xid_left_rumble() const { return xid_left_rumble_; }
    uint16_t xid_right_rumble() const { return xid_right_rumble_; }
    uint8_t xid_address() const { return xid_device_.address; }
    uint8_t xid_configuration() const { return xid_device_.configuration; }
    uint8_t synthetic_hub_address() const { return hub_device_.address; }
    uint8_t synthetic_hub_configuration() const {
        return hub_device_.configuration;
    }
    uint32_t synthetic_hub_port_status(size_t port) const {
        return port == 0 && topology_ == UsbTopology::kSyntheticControllerSHubForTest
                   ? hub_port1_status_
                   : 0;
    }
    size_t ep0_trace_count() const { return ep0_trace_count_; }
    uint64_t ep0_trace_dropped() const { return ep0_trace_dropped_; }
    const Ep0Trace& ep0_trace(size_t index) const { return ep0_trace_[index]; }
    uint64_t wdh_assertions() const { return wdh_assertions_; }
    uint64_t wdh_w1c_acknowledgements() const { return wdh_w1c_acknowledgements_; }
    uint32_t last_done_head() const { return last_done_head_; }
    uint32_t interrupt_status() const { return regs_[3]; }
    uint32_t interrupt_enable() const { return regs_[4]; }
    uint32_t port_status(size_t port) const {
        return port < 4 ? read_reg(static_cast<uint32_t>(21 + port)) : 0;
    }

private:
    struct EndpointDescriptor {
        uint32_t flags;
        uint32_t tail;
        uint32_t head;
        uint32_t next;
    };
    struct TransferDescriptor {
        uint32_t flags;
        uint32_t cbp;
        uint32_t next;
        uint32_t be;
    };
    struct SetupPacket {
        uint8_t request_type;
        uint8_t request;
        uint16_t value;
        uint16_t index;
        uint16_t length;
    };
    struct UsbDeviceState {
        uint8_t address = 0;
        uint8_t pending_address = 0;
        bool pending_address_valid = false;
        uint8_t configuration = 0;
        SetupPacket control_setup{};
        bool control_active = false;
        bool control_direction_in = false;
        bool control_stalled = false;
        std::vector<uint8_t> control_response;
        size_t control_response_offset = 0;
        std::vector<uint8_t> control_output;
    };
    enum class TransferResult {
        kSuccess,
        kNak,
        kStall,
    };

    static constexpr uint32_t kCtlHcfs = 3u << 6;
    static constexpr uint32_t kUsbOperational = 2u << 6;
    static constexpr uint32_t kCtlPeriodicEnable = 1u << 2;
    static constexpr uint32_t kCtlControlEnable = 1u << 4;
    static constexpr uint32_t kCtlBulkEnable = 1u << 5;
    static constexpr uint32_t kStatusHcr = 1u << 0;
    static constexpr uint32_t kStatusClf = 1u << 1;
    static constexpr uint32_t kStatusBlf = 1u << 2;
    static constexpr uint32_t kIntrWd = 1u << 1;
    static constexpr uint32_t kIntrSf = 1u << 2;
    static constexpr uint32_t kIntrFno = 1u << 5;
    static constexpr uint32_t kIntrRhsc = 1u << 6;
    static constexpr uint32_t kIntrMie = 1u << 31;
    static constexpr uint32_t kPortCcs = 1u << 0;
    static constexpr uint32_t kPortPes = 1u << 1;
    static constexpr uint32_t kPortPss = 1u << 2;
    static constexpr uint32_t kPortPrs = 1u << 4;
    static constexpr uint32_t kPortPps = 1u << 8;
    static constexpr uint32_t kPortCsc = 1u << 16;
    static constexpr uint32_t kPortPesc = 1u << 17;
    static constexpr uint32_t kPortPssc = 1u << 18;
    static constexpr uint32_t kPortPrsc = 1u << 20;
    static constexpr uint32_t kPortWtc = 0x001f0000u;
    static constexpr uint32_t kDescriptorPointerMask = 0xfffffff0u;
    static constexpr uint32_t kEdHalted = 1u << 0;
    static constexpr uint32_t kEdCarry = 1u << 1;
    static constexpr uint32_t kTdRounding = 1u << 18;
    static constexpr uint32_t kTdToggle0 = 1u << 24;
    static constexpr uint32_t kTdToggle1 = 1u << 25;
    static constexpr uint32_t kTdConditionMask = 0xf0000000u;
    static constexpr uint32_t kCcNoError = 0u;
    static constexpr uint32_t kCcStall = 4u;
    static constexpr uint32_t kCcDataUnderrun = 9u;

    void reset();
    void update_irq();
    void frame_step();
    bool process_ed_list(uint32_t head, uint32_t current_reg,
                         bool periodic, bool& active);
    bool process_td(uint32_t ed_address, EndpointDescriptor& ed,
                    bool& endpoint_blocked);
    bool retire_td(uint32_t ed_address, EndpointDescriptor& ed,
                   uint32_t td_address, TransferDescriptor& td,
                   uint32_t condition_code);
    TransferResult device_transfer(uint8_t address, uint8_t endpoint,
                                   uint8_t direction,
                                   const uint8_t* output, size_t output_size,
                                   uint8_t* input, size_t input_capacity,
                                   size_t& actual);
    UsbDeviceState* resolve_device(uint8_t address);
    TransferResult handle_setup(UsbDeviceState& device, bool is_hub,
                                const uint8_t* data, size_t size);
    TransferResult prepare_control_response(UsbDeviceState& device, bool is_hub,
                                            const SetupPacket& setup);
    void finish_control_transfer(UsbDeviceState& device, bool is_hub);
    void begin_ep0_trace(uint8_t address, const SetupPacket& setup,
                         TransferResult result);
    void record_ep0_td_completion(const EndpointDescriptor& ed,
                                  uint32_t td_address, uint32_t td_flags,
                                  uint32_t condition_code);
    std::vector<uint8_t> xid_input_report() const;
    static std::vector<uint8_t> string_descriptor(const char* text);
    bool td_buffer_length(const TransferDescriptor& td, size_t& length) const;
    bool read_td_buffer(const TransferDescriptor& td, size_t length,
                        uint8_t* output);
    bool write_td_buffer(const TransferDescriptor& td, size_t length,
                         const uint8_t* input);
    bool advance_td_buffer(TransferDescriptor& td, size_t amount,
                           size_t total_length) const;
    bool read_memory(uint32_t address, void* output, size_t size,
                     const char* what);
    bool write_memory(uint32_t address, const void* input, size_t size,
                      const char* what);
    uint32_t read_reg(uint32_t index) const;
    void write_reg(uint32_t index, uint32_t value);

    uint32_t base_;
    FaultFn fault_;
    std::array<uint32_t, 25> regs_{};
    std::function<bool(uint32_t, void*, size_t)> mem_read_;
    std::function<bool(uint32_t, const void*, size_t)> mem_write_;
    std::function<void(int, bool)> irq_fn_;
    int irq_num_ = 1;
    bool irq_level_ = false;
    uint64_t frame_ = 0;
    uint64_t frames_ = 0;
    uint8_t done_count_ = 7;

    UsbTopology topology_ = UsbTopology::kDirectXid;
    bool xid_attached_ = false;
    XidGamepadState xid_state_{};
    uint16_t xid_left_rumble_ = 0;
    uint16_t xid_right_rumble_ = 0;
    uint32_t hub_port1_status_ = 0;
    UsbDeviceState xid_device_{};
    UsbDeviceState hub_device_{};

    std::array<Ep0Trace, kEp0TraceCapacity> ep0_trace_{};
    size_t ep0_trace_count_ = 0;
    uint64_t ep0_trace_dropped_ = 0;
    int ep0_active_trace_ = -1;
    uint64_t wdh_assertions_ = 0;
    uint64_t wdh_w1c_acknowledgements_ = 0;
    uint32_t last_done_head_ = 0;

    uint64_t processed_eds_ = 0;
    uint64_t processed_tds_ = 0;
    uint64_t retired_tds_ = 0;
    uint64_t done_writebacks_ = 0;
    uint64_t control_requests_ = 0;
    uint64_t xid_input_reports_ = 0;
    uint64_t xid_endpoint2_input_reports_ = 0;
    uint64_t xid_output_reports_ = 0;
};

}  // namespace xlle
