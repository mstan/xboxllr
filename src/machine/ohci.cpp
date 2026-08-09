#include "machine/ohci.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>

namespace xlle {
namespace {

constexpr uint8_t kDirectionSetup = 0;
constexpr uint8_t kDirectionOut = 1;
constexpr uint8_t kDirectionIn = 2;
constexpr uint32_t kHubPortConnection = 1u << 0;
constexpr uint32_t kHubPortEnabled = 1u << 1;
constexpr uint32_t kHubPortConnectionChange = 1u << 16;
constexpr uint32_t kHubPortEnableChange = 1u << 17;
constexpr uint32_t kHubPortResetChange = 1u << 20;

constexpr std::array<uint8_t, 18> kDeviceDescriptor{{
    18, 0x01, 0x10, 0x01, 0x00, 0x00, 0x00, 0x40,
    0x5e, 0x04, 0x89, 0x02, 0x00, 0x01, 1, 2, 3, 1,
}};

constexpr std::array<uint8_t, 32> kConfigurationDescriptor{{
    9, 0x02, 32, 0, 1, 1, 0, 0x80, 50,
    9, 0x04, 0, 0, 2, 0x58, 0x42, 0, 0,
    7, 0x05, 0x82, 0x03, 0x20, 0, 4,
    7, 0x05, 0x02, 0x03, 0x20, 0, 4,
}};

constexpr std::array<uint8_t, 16> kXidDescriptor{{
    0x10, 0x42, 0x00, 0x01, 0x01, 0x02, 20, 6,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
}};

// Test fixture only.  Zero identity/string fields intentionally avoid
// representing this as a captured Controller S embedded hub.
constexpr std::array<uint8_t, 18> kSyntheticHubDeviceDescriptor{{
    18, 0x01, 0x10, 0x01, 0x09, 0x00, 0x00, 0x08,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0, 0, 0, 1,
}};
constexpr std::array<uint8_t, 25> kSyntheticHubConfigurationDescriptor{{
    9, 0x02, 25, 0, 1, 1, 0, 0x40, 0,
    9, 0x04, 0, 0, 1, 0x09, 0x00, 0x00, 0,
    7, 0x05, 0x81, 0x03, 1, 0, 12,
}};
constexpr std::array<uint8_t, 9> kSyntheticHubDescriptor{{
    9, 0x29, 3, 0x02, 0, 0, 0, 0, 0xff,
}};

uint16_t read16(const uint8_t* data) {
    return static_cast<uint16_t>(data[0]) |
           static_cast<uint16_t>(data[1] << 8);
}

void put16(std::vector<uint8_t>& output, size_t offset, uint16_t value) {
    output[offset] = static_cast<uint8_t>(value);
    output[offset + 1] = static_cast<uint8_t>(value >> 8);
}

}  // namespace

void Ohci::reset() {
    regs_.fill(0);
    regs_[0] = 0x10;         // HcRevision: OHCI 1.0
    regs_[4] = kIntrMie;     // shared HcInterruptEnable/Disable state
    regs_[13] = 0x27782edf;  // HcFmInterval (QEMU/Cxbx reset)
    regs_[17] = 0x628;       // HcLSThreshold
    regs_[18] = 0x1204;      // NOCP | NPS | four root ports

    xid_attached_ = base_ == 0xFED00000u;
    xid_device_ = {};
    hub_device_ = {};
    hub_port1_status_ = topology_ == UsbTopology::kSyntheticControllerSHubForTest
                            ? kHubPortConnection | kHubPortConnectionChange
                            : 0;
    ep0_active_trace_ = -1;
    done_count_ = 7;
    if (xid_attached_) {
        regs_[21] = kPortCcs | kPortCsc | kPortPps;
        regs_[3] |= kIntrRhsc;
    }
    update_irq();
}

uint32_t Ohci::read_reg(uint32_t index) const {
    if (index >= regs_.size()) return 0xffffffffu;
    if (index >= 21) return regs_[index] | kPortPps;
    return regs_[index];
}

uint32_t Ohci::mmio_read(uint32_t addr, int size) {
    const uint32_t off = addr - base_;
    if ((off & 3u) || size != 4) {
        fault_(std::string(name()) + ": unaligned/non-dword register read");
        return 0xffffffffu;
    }
    return read_reg(off >> 2);
}

void Ohci::write_reg(uint32_t index, uint32_t value) {
    switch (index) {
        case 0:   // HcRevision is read-only.
        case 7:   // HcPeriodCurrentED
        case 12:  // HcDoneHead
        case 14:  // HcFmRemaining
        case 15:  // HcFmNumber
            return;
        case 1: {  // HcControl
            const uint32_t old_state = regs_[1] & kCtlHcfs;
            const uint32_t next_state = value & kCtlHcfs;
            regs_[1] = value & 0x7ffu;
            if (old_state != next_state && next_state == 0) reset();
            return;
        }
        case 2:  // HcCommandStatus: set-on-write, HCR self-clears through reset.
            value &= ~0xc0u;
            regs_[2] |= value;
            if (regs_[2] & kStatusHcr) reset();
            return;
        case 3:  // HcInterruptStatus: W1C
            if ((value & kIntrWd) && (regs_[3] & kIntrWd))
                ++wdh_w1c_acknowledgements_;
            regs_[3] &= ~value;
            update_irq();
            return;
        case 4:  // HcInterruptEnable
            regs_[4] |= value;
            update_irq();
            return;
        case 5:  // HcInterruptDisable
            regs_[4] &= ~value;
            update_irq();
            return;
        case 6:
            regs_[6] = value & 0xffffff00u;
            return;
        case 8:
        case 9:
        case 10:
        case 11:
            regs_[index] = value & kDescriptorPointerMask;
            return;
        case 13:
            regs_[13] = value & ~0x0000c000u;
            return;
        case 16:
            regs_[16] = value & 0x3fffu;
            return;
        case 17:
            regs_[17] = value & 0x0fffu;
            return;
        case 18:  // Xbox root-hub topology is fixed.
        case 19:
            return;
        case 20:  // Root-hub status W1C/change-control subset.
            regs_[20] &= ~(value & 0x00030000u);
            if (value & (1u << 31)) regs_[20] &= ~(1u << 15);
            if (value & (1u << 15)) regs_[20] |= 1u << 15;
            return;
        case 21:
        case 22:
        case 23:
        case 24: {
            const uint32_t old = regs_[index];
            regs_[index] &= ~(value & kPortWtc);
            if (value & kPortCcs) {
                if (regs_[index] & kPortPes) {
                    regs_[index] &= ~kPortPes;
                    regs_[index] |= kPortPesc;
                }
            }
            if ((value & kPortPes) && (regs_[index] & kPortCcs))
                regs_[index] |= kPortPes;
            if ((value & kPortPss) && (regs_[index] & kPortCcs))
                regs_[index] |= kPortPss;
            if ((value & kPortPrs) && (regs_[index] & kPortCcs)) {
                regs_[index] &= ~kPortPrs;
                regs_[index] |= kPortPes | kPortPrsc;
                xid_device_ = {};
                if (topology_ == UsbTopology::kSyntheticControllerSHubForTest) {
                    hub_device_ = {};
                    hub_port1_status_ = kHubPortConnection | kHubPortConnectionChange;
                }
            }
            if (value & kPortPps) regs_[index] |= kPortPps;
            if (value & (1u << 9)) regs_[index] &= ~kPortPps;
            if (old != regs_[index]) {
                regs_[3] |= kIntrRhsc;
                update_irq();
            }
            return;
        }
        default:
            if (index < regs_.size()) regs_[index] = value;
            return;
    }
}

void Ohci::mmio_write(uint32_t addr, int size, uint32_t value) {
    const uint32_t off = addr - base_;
    if ((off & 3u) || size != 4) {
        fault_(std::string(name()) + ": unaligned/non-dword register write");
        return;
    }
    if ((off >> 2) >= regs_.size()) {
        fault_(std::string(name()) + ": register write outside OHCI set");
        return;
    }
    write_reg(off >> 2, value);
}

void Ohci::update_irq() {
    const bool level =
        (regs_[4] & kIntrMie) && (regs_[3] & regs_[4] & ~kIntrMie);
    if (level != irq_level_) {
        irq_level_ = level;
        if (irq_fn_) irq_fn_(irq_num_, level);
    }
}

bool Ohci::read_memory(uint32_t address, void* output, size_t size,
                       const char* what) {
    if (!mem_read_ || !mem_read_(address, output, size)) {
        char message[160];
        std::snprintf(message, sizeof(message),
                      "%s: %s DMA read failed at 0x%08x (%zu bytes)",
                      name(), what, address, size);
        fault_(message);
        return false;
    }
    return true;
}

bool Ohci::write_memory(uint32_t address, const void* input, size_t size,
                        const char* what) {
    if (!mem_write_ || !mem_write_(address, input, size)) {
        char message[160];
        std::snprintf(message, sizeof(message),
                      "%s: %s DMA write failed at 0x%08x (%zu bytes)",
                      name(), what, address, size);
        fault_(message);
        return false;
    }
    return true;
}

bool Ohci::td_buffer_length(const TransferDescriptor& td,
                            size_t& length) const {
    if (!td.cbp) {
        length = 0;
        return true;
    }
    if (!td.be) return false;
    const uint32_t cbp_page = td.cbp & 0xfffff000u;
    const uint32_t be_page = td.be & 0xfffff000u;
    if (cbp_page == be_page) {
        if (td.cbp > td.be) return false;
        length = static_cast<size_t>(td.be - td.cbp) + 1;
    } else {
        length = static_cast<size_t>(0x1000u - (td.cbp & 0xfffu)) +
                 static_cast<size_t>(td.be & 0xfffu) + 1;
    }
    return length <= 0x2000;
}

bool Ohci::read_td_buffer(const TransferDescriptor& td, size_t length,
                          uint8_t* output) {
    if (!length) return true;
    const size_t first =
        std::min(length, static_cast<size_t>(0x1000u - (td.cbp & 0xfffu)));
    if (!read_memory(td.cbp, output, first, "TD buffer")) return false;
    if (first == length) return true;
    const uint32_t second = td.be & 0xfffff000u;
    return read_memory(second, output + first, length - first, "TD buffer");
}

bool Ohci::write_td_buffer(const TransferDescriptor& td, size_t length,
                           const uint8_t* input) {
    if (!length) return true;
    const size_t first =
        std::min(length, static_cast<size_t>(0x1000u - (td.cbp & 0xfffu)));
    if (!write_memory(td.cbp, input, first, "TD buffer")) return false;
    if (first == length) return true;
    const uint32_t second = td.be & 0xfffff000u;
    return write_memory(second, input + first, length - first, "TD buffer");
}

bool Ohci::advance_td_buffer(TransferDescriptor& td, size_t amount,
                             size_t total_length) const {
    if (amount > total_length) return false;
    if (amount == total_length) {
        td.cbp = 0;
        return true;
    }
    const uint32_t page_offset = td.cbp & 0xfffu;
    if (page_offset + amount > 0xfffu) {
        td.cbp = (td.be & 0xfffff000u) |
                 static_cast<uint32_t>((page_offset + amount) & 0xfffu);
    } else {
        td.cbp += static_cast<uint32_t>(amount);
    }
    return true;
}

std::vector<uint8_t> Ohci::string_descriptor(const char* text) {
    const size_t characters = std::strlen(text);
    std::vector<uint8_t> output(2 + characters * 2, 0);
    output[0] = static_cast<uint8_t>(output.size());
    output[1] = 0x03;
    for (size_t i = 0; i < characters; ++i)
        output[2 + i * 2] = static_cast<uint8_t>(text[i]);
    return output;
}

std::vector<uint8_t> Ohci::xid_input_report() const {
    std::vector<uint8_t> report(20, 0);
    report[0] = 0;
    report[1] = 20;
    put16(report, 2, xid_state_.buttons);
    std::copy(xid_state_.analog.begin(), xid_state_.analog.end(),
              report.begin() + 4);
    put16(report, 12, static_cast<uint16_t>(xid_state_.thumb_lx));
    put16(report, 14, static_cast<uint16_t>(xid_state_.thumb_ly));
    put16(report, 16, static_cast<uint16_t>(xid_state_.thumb_rx));
    put16(report, 18, static_cast<uint16_t>(xid_state_.thumb_ry));
    return report;
}

Ohci::UsbDeviceState* Ohci::resolve_device(uint8_t address) {
    if (!xid_attached_ || !(regs_[21] & kPortCcs) || !(regs_[21] & kPortPes))
        return nullptr;
    if (topology_ == UsbTopology::kDirectXid)
        return address == xid_device_.address ? &xid_device_ : nullptr;

    if (address == hub_device_.address &&
        (address != 0 || hub_device_.address == 0))
        return &hub_device_;
    if (address != 0 && address == xid_device_.address &&
        (hub_port1_status_ & kHubPortEnabled))
        return &xid_device_;
    // Address zero belongs to the hub until its address transaction completes.
    // Only then, and only after port 1 reset/enables the child, does it route
    // to the child during that child's own enumeration.
    if (address == 0 && hub_device_.address != 0 &&
        xid_device_.address == 0 && (hub_port1_status_ & kHubPortEnabled))
        return &xid_device_;
    return nullptr;
}

Ohci::TransferResult Ohci::prepare_control_response(
    UsbDeviceState& device, bool is_hub, const SetupPacket& setup) {
    device.control_response.clear();
    device.control_output.clear();
    device.control_response_offset = 0;
    device.control_stalled = false;
    device.control_direction_in = (setup.request_type & 0x80u) != 0;
    device.control_active = true;
    ++control_requests_;

    const uint8_t descriptor_type = static_cast<uint8_t>(setup.value >> 8);
    const uint8_t descriptor_index = static_cast<uint8_t>(setup.value);
    if (is_hub && setup.request_type == 0xa0 && setup.request == 0x06 &&
        setup.value == 0x2900) {
        device.control_response.assign(kSyntheticHubDescriptor.begin(),
                                       kSyntheticHubDescriptor.end());
    } else if (is_hub && setup.request_type == 0xa0 &&
               setup.request == 0x00) {
        if (!device.control_direction_in) return TransferResult::kStall;
        // Test-only synthetic hub: no hub-wide status/change conditions.
        device.control_response = {0, 0, 0, 0};
    } else if (is_hub && setup.request_type == 0xa3 &&
               setup.request == 0x00 && setup.index >= 1 &&
               setup.index <= 3) {
        if (!device.control_direction_in) return TransferResult::kStall;
        const uint32_t status = setup.index == 1 ? hub_port1_status_ : 0;
        device.control_response = {
            static_cast<uint8_t>(status), static_cast<uint8_t>(status >> 8),
            static_cast<uint8_t>(status >> 16), static_cast<uint8_t>(status >> 24),
        };
    } else if (is_hub && setup.request_type == 0x23 &&
               (setup.request == 0x01 || setup.request == 0x03)) {
        if (device.control_direction_in || setup.index < 1 || setup.index > 3)
            return TransferResult::kStall;
        if (setup.request == 0x03 && setup.value == 4 && setup.index == 1) {
            // A downstream reset starts a fresh child-address-zero phase.
            xid_device_ = {};
            hub_port1_status_ |= kHubPortConnection | kHubPortEnabled |
                                kHubPortEnableChange | kHubPortResetChange;
        } else if (setup.request == 0x01 && setup.index == 1) {
            if (setup.value == 16)
                hub_port1_status_ &= ~kHubPortConnectionChange;
            else if (setup.value == 17)
                hub_port1_status_ &= ~kHubPortEnableChange;
            else if (setup.value == 20)
                hub_port1_status_ &= ~kHubPortResetChange;
            else
                return TransferResult::kStall;
        } else {
            return TransferResult::kStall;
        }
    } else if ((setup.request_type & 0x60u) == 0) {
        switch (setup.request) {
            case 0x00:  // GET_STATUS
                if (!device.control_direction_in) return TransferResult::kStall;
                device.control_response = {0, 0};
                break;
            case 0x01:  // CLEAR_FEATURE
            case 0x03:  // SET_FEATURE
                if (device.control_direction_in) return TransferResult::kStall;
                break;
            case 0x05:  // SET_ADDRESS
                if (device.control_direction_in || setup.value > 127)
                    return TransferResult::kStall;
                device.pending_address = static_cast<uint8_t>(setup.value);
                device.pending_address_valid = true;
                break;
            case 0x06:  // GET_DESCRIPTOR
                if (!device.control_direction_in) return TransferResult::kStall;
                if (descriptor_type == 0x01) {
                    if (is_hub)
                        device.control_response.assign(kSyntheticHubDeviceDescriptor.begin(),
                                                       kSyntheticHubDeviceDescriptor.end());
                    else
                        device.control_response.assign(kDeviceDescriptor.begin(),
                                                       kDeviceDescriptor.end());
                } else if (descriptor_type == 0x02) {
                    if (is_hub)
                        device.control_response.assign(kSyntheticHubConfigurationDescriptor.begin(),
                                                       kSyntheticHubConfigurationDescriptor.end());
                    else
                        device.control_response.assign(kConfigurationDescriptor.begin(),
                                                       kConfigurationDescriptor.end());
                } else if (descriptor_type == 0x03) {
                    if (is_hub) return TransferResult::kStall;
                    if (descriptor_index == 0) {
                        device.control_response = {4, 3, 0x09, 0x04};
                    } else if (descriptor_index == 1) {
                        device.control_response = string_descriptor("Microsoft");
                    } else if (descriptor_index == 2) {
                        device.control_response =
                            string_descriptor("Controller S");
                    } else if (descriptor_index == 3) {
                        device.control_response = string_descriptor("1");
                    } else {
                        return TransferResult::kStall;
                    }
                } else {
                    return TransferResult::kStall;
                }
                break;
            case 0x08:  // GET_CONFIGURATION
                if (!device.control_direction_in) return TransferResult::kStall;
                device.control_response = {device.configuration};
                break;
            case 0x09:  // SET_CONFIGURATION
                if (device.control_direction_in || setup.value > 1)
                    return TransferResult::kStall;
                device.configuration = static_cast<uint8_t>(setup.value);
                break;
            case 0x0a:  // GET_INTERFACE
                if (!device.control_direction_in) return TransferResult::kStall;
                device.control_response = {0};
                break;
            case 0x0b:  // SET_INTERFACE
                if (device.control_direction_in || setup.value != 0)
                    return TransferResult::kStall;
                break;
            default:
                return TransferResult::kStall;
        }
    } else if (!is_hub && setup.request_type == 0xc1 && setup.request == 0x06 &&
               setup.value == 0x4200) {
        device.control_response.assign(kXidDescriptor.begin(), kXidDescriptor.end());
    } else if (!is_hub && setup.request_type == 0xc1 && setup.request == 0x01) {
        if (setup.value == 0x0100) {
            device.control_response.assign(20, 0xff);
            device.control_response[0] = 0;
            device.control_response[1] = 20;
        } else if (setup.value == 0x0200) {
            device.control_response.assign(6, 0xff);
            device.control_response[0] = 0;
            device.control_response[1] = 6;
        } else {
            return TransferResult::kStall;
        }
    } else if (!is_hub && setup.request_type == 0xa1 && setup.request == 0x01 &&
               setup.value == 0x0100) {
        device.control_response = xid_input_report();
        ++xid_input_reports_;
    } else if (!is_hub && setup.request_type == 0x21 && setup.request == 0x09 &&
               setup.value == 0x0200) {
        if (setup.length != 6) return TransferResult::kStall;
    } else {
        return TransferResult::kStall;
    }

    if (device.control_response.size() > setup.length)
        device.control_response.resize(setup.length);
    return TransferResult::kSuccess;
}

Ohci::TransferResult Ohci::handle_setup(UsbDeviceState& device, bool is_hub,
                                        const uint8_t* data, size_t size) {
    if (size != 8) return TransferResult::kStall;
    device.control_setup.request_type = data[0];
    device.control_setup.request = data[1];
    device.control_setup.value = read16(data + 2);
    device.control_setup.index = read16(data + 4);
    device.control_setup.length = read16(data + 6);
    return prepare_control_response(device, is_hub, device.control_setup);
}

void Ohci::finish_control_transfer(UsbDeviceState& device, bool is_hub) {
    if (device.pending_address_valid) {
        device.address = device.pending_address;
        device.pending_address_valid = false;
    }
    if (!is_hub && device.control_setup.request_type == 0x21 &&
        device.control_setup.request == 0x09 &&
        device.control_setup.value == 0x0200 &&
        device.control_output.size() == 6) {
        xid_left_rumble_ = read16(device.control_output.data() + 2);
        xid_right_rumble_ = read16(device.control_output.data() + 4);
        ++xid_output_reports_;
    }
    device.control_active = false;
    device.control_stalled = false;
    device.control_response.clear();
    device.control_output.clear();
    device.control_response_offset = 0;
    if (ep0_active_trace_ >= 0) {
        Ep0Trace& trace = ep0_trace_[static_cast<size_t>(ep0_active_trace_)];
        if (trace.outcome == Ep0Outcome::kPending)
            trace.outcome = Ep0Outcome::kComplete;
    }
}

void Ohci::begin_ep0_trace(uint8_t address, const SetupPacket& setup,
                           TransferResult result) {
    if (ep0_active_trace_ >= 0) {
        Ep0Trace& previous =
            ep0_trace_[static_cast<size_t>(ep0_active_trace_)];
        if (previous.outcome == Ep0Outcome::kPending)
            previous.outcome = Ep0Outcome::kError;
    }
    ep0_active_trace_ = -1;
    if (ep0_trace_count_ == ep0_trace_.size()) {
        ++ep0_trace_dropped_;
        return;
    }
    Ep0Trace& trace = ep0_trace_[ep0_trace_count_];
    trace.address = address;
    trace.request_type = setup.request_type;
    trace.request = setup.request;
    trace.value = setup.value;
    trace.index = setup.index;
    trace.length = setup.length;
    trace.outcome = result == TransferResult::kStall
                        ? Ep0Outcome::kStalled
                        : Ep0Outcome::kPending;
    trace.td_completions = 0;
    trace.last_condition_code = 0;
    trace.data_bytes = 0;
    ep0_active_trace_ = static_cast<int>(ep0_trace_count_++);
}

void Ohci::record_ep0_td_completion(const EndpointDescriptor& ed,
                                    uint32_t td_address, uint32_t td_flags,
                                    uint32_t condition_code) {
    if (ep0_active_trace_ < 0 || ((ed.flags >> 7) & 0xfu) != 0)
        return;
    Ep0Trace& trace = ep0_trace_[static_cast<size_t>(ep0_active_trace_)];
    if ((ed.flags & 0x7fu) != trace.address) return;
    if (trace.td_completions != 0xffu) ++trace.td_completions;
    trace.last_condition_code = static_cast<uint8_t>(condition_code);
    trace.terminal_td_address = td_address;
    trace.terminal_td_flags = td_flags;
    trace.ed_head_after = ed.head;
    if (condition_code == kCcStall)
        trace.outcome = Ep0Outcome::kStalled;
    else if (condition_code != kCcNoError)
        trace.outcome = Ep0Outcome::kError;
    // A status stage marks the trace complete before retirement; stalls and
    // transfer errors are terminal at the TD that reports them.
    if (trace.outcome != Ep0Outcome::kPending)
        ep0_active_trace_ = -1;
}

Ohci::TransferResult Ohci::device_transfer(
    uint8_t address, uint8_t endpoint, uint8_t direction,
    const uint8_t* output, size_t output_size, uint8_t* input,
    size_t input_capacity, size_t& actual) {
    actual = 0;
    UsbDeviceState* device = resolve_device(address);
    if (!device) return TransferResult::kNak;
    const bool is_hub = device == &hub_device_;

    if (endpoint == 0) {
        if (direction == kDirectionSetup) {
            actual = output_size;
            return handle_setup(*device, is_hub, output, output_size);
        }
        if (!device->control_active || device->control_stalled)
            return TransferResult::kStall;
        if (direction == kDirectionIn) {
            if (!device->control_direction_in) {
                if (output_size != 0 || input_capacity != 0)
                    return TransferResult::kStall;
                finish_control_transfer(*device, is_hub);
                return TransferResult::kSuccess;
            }
            const size_t remaining =
                device->control_response.size() - device->control_response_offset;
            actual = std::min(remaining, input_capacity);
            if (actual) {
                std::memcpy(input,
                            device->control_response.data() + device->control_response_offset,
                            actual);
                device->control_response_offset += actual;
            }
            return TransferResult::kSuccess;
        }
        if (direction == kDirectionOut) {
            if (device->control_direction_in) {
                if (output_size != 0) return TransferResult::kStall;
                finish_control_transfer(*device, is_hub);
                return TransferResult::kSuccess;
            }
            if (device->control_setup.length == 0) {
                if (output_size != 0) return TransferResult::kStall;
                return TransferResult::kSuccess;
            }
            const size_t remaining =
                device->control_setup.length - device->control_output.size();
            actual = std::min(remaining, output_size);
            device->control_output.insert(device->control_output.end(), output,
                                          output + actual);
            return TransferResult::kSuccess;
        }
        return TransferResult::kStall;
    }

    if (is_hub) {
        if (!hub_device_.configuration || endpoint != 1 ||
            direction != kDirectionIn)
            return TransferResult::kStall;
        if (!(hub_port1_status_ &
              (kHubPortConnectionChange | kHubPortEnableChange |
               kHubPortResetChange)))
            return TransferResult::kNak;
        if (!input_capacity) return TransferResult::kStall;
        input[0] = 0x02;  // bitmap: downstream port 1 changed
        actual = 1;
        return TransferResult::kSuccess;
    }
    if (!xid_device_.configuration || endpoint != 2)
        return TransferResult::kStall;
    if (direction == kDirectionIn) {
        const auto report = xid_input_report();
        actual = std::min(report.size(), input_capacity);
        std::memcpy(input, report.data(), actual);
        ++xid_input_reports_;
        return TransferResult::kSuccess;
    }
    if (direction == kDirectionOut) {
        if (output_size != 6 || output[1] != 6)
            return TransferResult::kStall;
        xid_left_rumble_ = read16(output + 2);
        xid_right_rumble_ = read16(output + 4);
        actual = output_size;
        ++xid_output_reports_;
        return TransferResult::kSuccess;
    }
    return TransferResult::kStall;
}

bool Ohci::retire_td(uint32_t ed_address, EndpointDescriptor& ed,
                     uint32_t td_address, TransferDescriptor& td,
                     uint32_t condition_code) {
    td.flags &= ~kTdConditionMask;
    td.flags |= (condition_code & 0xfu) << 28;
    if (condition_code == kCcNoError) {
        td.flags |= kTdToggle1;
        td.flags ^= kTdToggle0;
        ed.head &= ~kEdCarry;
        if (td.flags & kTdToggle0) ed.head |= kEdCarry;
    } else {
        ed.head |= kEdHalted;
        done_count_ = 0;
    }
    ed.head &= ~kDescriptorPointerMask;
    ed.head |= td.next & kDescriptorPointerMask;
    td.next = regs_[12];
    regs_[12] = td_address;
    const uint8_t delay = static_cast<uint8_t>((td.flags >> 21) & 7u);
    if (delay < done_count_) done_count_ = delay;

    if (!write_memory(td_address, &td, sizeof(td), "TD") ||
        !write_memory(ed_address + 8, &ed.head, sizeof(ed.head), "ED head")) {
        return false;
    }
    record_ep0_td_completion(ed, td_address, td.flags, condition_code);
    ++retired_tds_;
    return true;
}

bool Ohci::process_td(uint32_t ed_address, EndpointDescriptor& ed,
                      bool& endpoint_blocked) {
    endpoint_blocked = false;
    const uint32_t td_address = ed.head & kDescriptorPointerMask;
    TransferDescriptor td{};
    if (!td_address ||
        !read_memory(td_address, &td, sizeof(td), "TD")) {
        return false;
    }
    ++processed_tds_;

    uint8_t direction = static_cast<uint8_t>((ed.flags >> 11) & 3u);
    if (direction == 0) direction = static_cast<uint8_t>((td.flags >> 19) & 3u);
    if (direction > kDirectionIn) {
        fault_(std::string(name()) + ": TD has reserved direction");
        return false;
    }
    const uint8_t endpoint = static_cast<uint8_t>((ed.flags >> 7) & 0xfu);
    if (direction == kDirectionSetup && endpoint != 0) {
        fault_(std::string(name()) + ": SETUP TD targets nonzero endpoint");
        return false;
    }

    size_t length = 0;
    if (!td_buffer_length(td, length)) {
        fault_(std::string(name()) + ": invalid TD buffer range");
        return false;
    }
    const size_t maximum_packet = (ed.flags >> 16) & 0x7ffu;
    size_t packet_length = length;
    if (direction != kDirectionIn && packet_length > maximum_packet)
        packet_length = maximum_packet;
    if (packet_length > 0x2000) {
        fault_(std::string(name()) + ": TD packet exceeds two-page buffer");
        return false;
    }

    std::vector<uint8_t> buffer(packet_length);
    if (direction != kDirectionIn &&
        !read_td_buffer(td, packet_length, buffer.data())) {
        return false;
    }
    size_t actual = 0;
    const auto result = device_transfer(
        static_cast<uint8_t>(ed.flags & 0x7fu), endpoint, direction,
        buffer.data(), packet_length, buffer.data(), packet_length, actual);
    if (endpoint == 0 && direction == kDirectionSetup &&
        result != TransferResult::kNak) {
        UsbDeviceState* device =
            resolve_device(static_cast<uint8_t>(ed.flags & 0x7fu));
        if (!device) return false;
        begin_ep0_trace(static_cast<uint8_t>(ed.flags & 0x7fu),
                        device->control_setup, result);
    }
    if (result == TransferResult::kNak) {
        endpoint_blocked = true;
        return true;
    }
    if (result == TransferResult::kStall) {
        return retire_td(ed_address, ed, td_address, td, kCcStall);
    }
    if (actual > packet_length) {
        fault_(std::string(name()) + ": XID returned more data than TD buffer");
        return false;
    }
    if (endpoint == 0 && direction != kDirectionSetup &&
        ep0_active_trace_ >= 0) {
        Ep0Trace& trace =
            ep0_trace_[static_cast<size_t>(ep0_active_trace_)];
        if ((ed.flags & 0x7fu) == trace.address)
            trace.data_bytes += static_cast<uint32_t>(actual);
    }
    if (direction == kDirectionIn && actual &&
        !write_td_buffer(td, actual, buffer.data())) {
        return false;
    }

    if (!advance_td_buffer(td, actual, length)) {
        fault_(std::string(name()) + ": TD buffer advance overflow");
        return false;
    }
    const bool short_in =
        direction == kDirectionIn && actual < packet_length;
    if (short_in && !(td.flags & kTdRounding)) {
        return retire_td(ed_address, ed, td_address, td, kCcDataUnderrun);
    }
    if (direction != kDirectionIn && actual < length) {
        if (!write_memory(td_address, &td, sizeof(td), "partial TD"))
            return false;
        return true;
    }
    return retire_td(ed_address, ed, td_address, td, kCcNoError);
}

bool Ohci::process_ed_list(uint32_t head, uint32_t current_reg,
                           bool periodic, bool& active) {
    uint32_t current = head & kDescriptorPointerMask;
    uint32_t links = 0;
    active = false;
    while (current) {
        if (++links > 32) {
            fault_(std::string(name()) + ": endpoint list exceeds 32 links");
            return false;
        }
        EndpointDescriptor ed{};
        if (!read_memory(current, &ed, sizeof(ed), "ED")) return false;
        ++processed_eds_;
        if (!periodic) regs_[current_reg] = current;
        const uint32_t next = ed.next & kDescriptorPointerMask;
        if (!(ed.head & kEdHalted) && !(ed.flags & (1u << 14))) {
            uint32_t td_guard = 0;
            while ((ed.head & kDescriptorPointerMask) !=
                   (ed.tail & kDescriptorPointerMask)) {
                active = true;
                if (++td_guard > 256) {
                    fault_(std::string(name()) +
                           ": endpoint exceeds 256 TDs per frame");
                    return false;
                }
                bool blocked = false;
                if (!process_td(current, ed, blocked)) return false;
                if (blocked || (ed.head & kEdHalted)) break;
            }
        }
        current = next;
    }
    if (!periodic) regs_[current_reg] = 0;
    return true;
}

void Ohci::frame_step() {
    if ((regs_[1] & kCtlHcfs) != kUsbOperational) return;

    const uint16_t old_number = static_cast<uint16_t>(regs_[15]);
    if (regs_[6] && mem_read_) {
        if (regs_[1] & kCtlPeriodicEnable) {
            uint32_t periodic_head = 0;
            if (!read_memory(regs_[6] + (old_number & 31u) * 4,
                             &periodic_head, sizeof(periodic_head),
                             "HCCA interrupt table")) {
                return;
            }
            bool active = false;
            if (!process_ed_list(periodic_head, 7, true, active)) return;
        }
        if ((regs_[1] & kCtlControlEnable) && (regs_[2] & kStatusClf)) {
            bool active = false;
            if (!process_ed_list(regs_[8], 9, false, active)) return;
            if (!active) regs_[2] &= ~kStatusClf;
        }
        if ((regs_[1] & kCtlBulkEnable) && (regs_[2] & kStatusBlf)) {
            bool active = false;
            if (!process_ed_list(regs_[10], 11, false, active)) return;
            if (!active) regs_[2] &= ~kStatusBlf;
        }
    }

    const uint16_t number = static_cast<uint16_t>(old_number + 1);
    regs_[15] = number;
    ++frames_;

    uint32_t done_writeback = 0;
    if (regs_[12] && done_count_ == 0 && !(regs_[3] & kIntrWd)) {
        done_writeback = regs_[12];
        if (regs_[3] & regs_[4] & ~kIntrMie) done_writeback |= 1u;
        regs_[12] = 0;
        done_count_ = 7;
        regs_[3] |= kIntrWd;
        ++done_writebacks_;
        ++wdh_assertions_;
        last_done_head_ = done_writeback;
    } else if (done_count_ != 0 && done_count_ != 7) {
        --done_count_;
    }

    if (regs_[6] && mem_write_) {
        if (!write_memory(regs_[6] + 0x80, &number, sizeof(number),
                          "HCCA frame")) {
            return;
        }
        const uint16_t pad = 0;
        if (!write_memory(regs_[6] + 0x82, &pad, sizeof(pad),
                          "HCCA pad")) {
            return;
        }
        if (done_writeback &&
            !write_memory(regs_[6] + 0x84, &done_writeback,
                          sizeof(done_writeback), "HCCA done head")) {
            return;
        }
    }

    regs_[3] |= kIntrSf;
    if (number == 0) regs_[3] |= kIntrFno;
    update_irq();
}

void Ohci::set_frame(uint64_t frame) {
    while (frame_ < frame) {
        ++frame_;
        frame_step();
    }
}

}  // namespace xlle
