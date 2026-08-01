// xlle — MCPX integrated Ethernet controller register/PHY floor.
#include "machine/nvnet.hpp"

#include <cstdio>
#include <cstring>

namespace xlle {

namespace {
constexpr unsigned kMiiBmcr = 0;
constexpr unsigned kMiiBmsr = 1;
constexpr unsigned kMiiAnar = 4;
constexpr unsigned kMiiAnlpar = 5;
constexpr uint16_t kBmcrFullDuplex = 1u << 8;
constexpr uint16_t kBmcrAutoEnable = 1u << 12;
constexpr uint16_t kBmsrLink = 1u << 2;
constexpr uint16_t kBmsrAutoneg = 1u << 3;
constexpr uint16_t kBmsrAutonegComplete = 1u << 5;
constexpr uint16_t kAnCapabilities =
    (1u << 5) | (1u << 6) | (1u << 7) | (1u << 8) | (1u << 9);
constexpr uint16_t kAnAck = 1u << 14;
constexpr uint8_t kPhyAddress = 1;
}  // namespace

void NvNet::reset() {
    regs_.fill(0);
    phy_.fill(0);
    write_reg(kTxRxControl, kControlIdle);
    phy_[kMiiBmcr] = kBmcrFullDuplex | kBmcrAutoEnable;
    phy_[kMiiBmsr] = kBmsrAutoneg | kBmsrAutonegComplete | kBmsrLink;
    phy_[kMiiAnar] = kAnCapabilities;
    phy_[kMiiAnlpar] = kAnCapabilities;
    tx_buffer_.fill(0);
    tx_buffer_offset_ = 0;
    update_irq();
}

bool NvNet::valid_access(uint32_t addr, int size, const char* operation) {
    const uint32_t offset = addr - kBase;
    if ((size != 1 && size != 2 && size != 4) || (offset & (size - 1)) ||
        offset + static_cast<uint32_t>(size) > kRegisterSize) {
        fault_(std::string("nvnet: invalid ") + operation + " addr/width");
        return false;
    }
    return true;
}

uint32_t NvNet::read_ext(uint32_t offset, int size) const {
    uint32_t value = 0;
    std::memcpy(&value, regs_.data() + offset, size);
    return value;
}

void NvNet::write_ext(uint32_t offset, int size, uint32_t value) {
    std::memcpy(regs_.data() + offset, &value, size);
}

uint32_t NvNet::read_reg(uint32_t offset) const {
    return read_ext(offset, 4);
}

void NvNet::write_reg(uint32_t offset, uint32_t value) {
    write_ext(offset, 4, value);
}

uint32_t NvNet::mmio_read(uint32_t addr, int size) {
    if (!valid_access(addr, size, "register read")) return 0xffffffffu;
    return read_ext(addr - kBase, size);
}

void NvNet::reset_descriptor_pointers() {
    const uint32_t tx = read_reg(kTxRingAddress);
    const uint32_t rx = read_reg(kRxRingAddress);
    write_reg(kTxCurrentDesc, tx);
    write_reg(kTxNextDesc, tx);
    write_reg(kRxCurrentDesc, rx);
    write_reg(kRxNextDesc, rx);
    tx_buffer_offset_ = 0;
}

bool NvNet::load_descriptor(uint32_t address, RingDescriptor& descriptor) {
    static_assert(sizeof(RingDescriptor) == 8, "NVNet ring descriptor layout");
    if (!mem_read_) {
        fault_("nvnet: descriptor DMA has no guest-memory reader");
        return false;
    }
    if (!mem_read_(address, &descriptor, sizeof(descriptor))) {
        char message[96];
        std::snprintf(message, sizeof(message),
                      "nvnet: descriptor DMA read failed at 0x%08x", address);
        fault_(message);
        return false;
    }
    return true;
}

bool NvNet::store_descriptor(uint32_t address, const RingDescriptor& descriptor) {
    if (!mem_write_) {
        fault_("nvnet: descriptor DMA has no guest-memory writer");
        return false;
    }
    if (!mem_write_(address, &descriptor, sizeof(descriptor))) {
        char message[96];
        std::snprintf(message, sizeof(message),
                      "nvnet: descriptor DMA write failed at 0x%08x", address);
        fault_(message);
        return false;
    }
    return true;
}

bool NvNet::next_tx_descriptor(uint32_t& address) {
    const uint32_t base = read_reg(kTxRingAddress);
    const uint32_t count = (read_reg(kRingSize) & 0xffffu) + 1u;
    const uint64_t end = static_cast<uint64_t>(base) +
                         static_cast<uint64_t>(count) * sizeof(RingDescriptor);
    if (end > 0x100000000ull) {
        fault_("nvnet: TX descriptor ring wraps physical address space");
        return false;
    }

    address = read_reg(kTxNextDesc);
    if (address < base ||
        static_cast<uint64_t>(address) + sizeof(RingDescriptor) > end)
        address = base;
    write_reg(kTxCurrentDesc, address);
    return true;
}

bool NvNet::next_rx_descriptor(uint32_t& address) {
    const uint32_t base = read_reg(kRxRingAddress);
    const uint32_t count = ((read_reg(kRingSize) >> 16) & 0xffffu) + 1u;
    const uint64_t end = static_cast<uint64_t>(base) +
                         static_cast<uint64_t>(count) * sizeof(RingDescriptor);
    if (end > 0x100000000ull) {
        fault_("nvnet: RX descriptor ring wraps physical address space");
        return false;
    }

    address = read_reg(kRxNextDesc);
    if (address < base ||
        static_cast<uint64_t>(address) + sizeof(RingDescriptor) > end)
        address = base;
    write_reg(kRxCurrentDesc, address);
    return true;
}

bool NvNet::receive_filter(const uint8_t* data, size_t size) const {
    if (size < 6) return false;

    bool broadcast = true;
    for (size_t i = 0; i < 6; ++i) broadcast &= data[i] == 0xff;
    if (broadcast) return true;

    if (!(read_reg(kPacketFilter) & kPacketFilterMyAddress)) return true;

    std::array<uint8_t, 8> address{};
    std::array<uint8_t, 8> mask{};
    const uint32_t multicast_a = read_reg(kMulticastAddressA);
    const uint32_t multicast_b = read_reg(kMulticastAddressB);
    const uint32_t multicast_mask_a = read_reg(kMulticastMaskA);
    const uint32_t multicast_mask_b = read_reg(kMulticastMaskB);
    std::memcpy(address.data(), &multicast_a, sizeof(multicast_a));
    std::memcpy(address.data() + 4, &multicast_b, sizeof(multicast_b));
    std::memcpy(mask.data(), &multicast_mask_a, sizeof(multicast_mask_a));
    std::memcpy(mask.data() + 4, &multicast_mask_b, sizeof(multicast_mask_b));

    bool multicast_is_broadcast = true;
    for (size_t i = 0; i < 6; ++i) multicast_is_broadcast &= address[i] == 0xff;
    if (!multicast_is_broadcast) {
        bool multicast_match = true;
        for (size_t i = 0; i < 6; ++i)
            multicast_match &= (data[i] & mask[i]) == address[i];
        if (multicast_match) return true;
    }

    const uint32_t mac_a = read_reg(kMacAddressA);
    const uint32_t mac_b = read_reg(kMacAddressB);
    std::memcpy(address.data(), &mac_a, sizeof(mac_a));
    std::memcpy(address.data() + 4, &mac_b, sizeof(mac_b));
    return std::memcmp(data, address.data(), 6) == 0;
}

bool NvNet::rx_ready() {
    const bool rx_enabled = (read_reg(kReceiverControl) & kRxStart) != 0;
    const bool dma_enabled = (read_reg(kTxRxControl) & kControlDmaDisable) == 0;
    if (!rx_enabled || !dma_enabled || !link_up()) return false;

    uint32_t descriptor_address = 0;
    if (!next_rx_descriptor(descriptor_address)) return false;
    RingDescriptor descriptor{};
    if (!load_descriptor(descriptor_address, descriptor)) return false;
    return (descriptor.flags & kRxAvailable) != 0;
}

NvNet::RxResult NvNet::receive_packet(const uint8_t* data, size_t size) {
    ++rx_offers_;
    if (!data && size != 0) {
        fault_("nvnet: null RX packet data");
        return RxResult::Fault;
    }
    if (size > kRxBufferSize) {
        ++rx_oversized_;
        return RxResult::Oversized;
    }
    if (!receive_filter(data, size)) {
        ++rx_filtered_;
        return RxResult::Filtered;
    }

    const bool rx_enabled = (read_reg(kReceiverControl) & kRxStart) != 0;
    const bool dma_enabled = (read_reg(kTxRxControl) & kControlDmaDisable) == 0;
    if (!rx_enabled || !dma_enabled || !link_up()) {
        ++rx_not_ready_;
        return RxResult::NotReady;
    }

    uint32_t descriptor_address = 0;
    if (!next_rx_descriptor(descriptor_address)) return RxResult::Fault;
    RingDescriptor descriptor{};
    if (!load_descriptor(descriptor_address, descriptor)) return RxResult::Fault;
    if (!(descriptor.flags & kRxAvailable)) {
        ++rx_not_ready_;
        return RxResult::NotReady;
    }

    const size_t capacity = static_cast<size_t>(descriptor.length) + 1;
    if (size > capacity) {
        char message[112];
        std::snprintf(message, sizeof(message),
                      "nvnet: RX packet size %zu exceeds descriptor capacity %zu",
                      size, capacity);
        fault_(message);
        return RxResult::Fault;
    }
    if (!mem_write_) {
        fault_("nvnet: RX buffer DMA has no guest-memory writer");
        return RxResult::Fault;
    }

    set_dma_idle(false);
    if (size != 0 && !mem_write_(descriptor.buffer_address, data, size)) {
        char message[96];
        std::snprintf(message, sizeof(message),
                      "nvnet: RX buffer DMA write failed at 0x%08x",
                      descriptor.buffer_address);
        fault_(message);
        set_dma_idle(true);
        return RxResult::Fault;
    }

    descriptor.length = static_cast<uint16_t>(size);
    descriptor.flags = kRxBit4 | kRxDescriptorValid;
    if (!store_descriptor(descriptor_address, descriptor)) {
        set_dma_idle(true);
        return RxResult::Fault;
    }

    ++rx_descriptors_;
    ++rx_packets_;
    rx_bytes_ += size;

    const uint32_t base = read_reg(kRxRingAddress);
    const uint32_t count = ((read_reg(kRingSize) >> 16) & 0xffffu) + 1u;
    const uint64_t end = static_cast<uint64_t>(base) +
                         static_cast<uint64_t>(count) * sizeof(RingDescriptor);
    const uint64_t next64 =
        static_cast<uint64_t>(descriptor_address) + sizeof(RingDescriptor);
    write_reg(kRxNextDesc,
              next64 >= end ? base : static_cast<uint32_t>(next64));

    set_dma_idle(true);
    write_reg(kIrqStatus, read_reg(kIrqStatus) | kIrqRx);
    update_irq();
    return RxResult::Accepted;
}

void NvNet::set_dma_idle(bool idle) {
    uint32_t control = read_reg(kTxRxControl);
    if (idle)
        control |= kControlIdle;
    else
        control &= ~kControlIdle;
    write_reg(kTxRxControl, control);
}

bool NvNet::transmit() {
    const bool tx_enabled = (read_reg(kTransmitterControl) & kTxStart) != 0;
    const bool dma_enabled = (read_reg(kTxRxControl) & kControlDmaDisable) == 0;
    if (!tx_enabled || !dma_enabled || !link_up()) return true;

    set_dma_idle(false);
    const uint32_t base = read_reg(kTxRingAddress);
    const uint32_t count = (read_reg(kRingSize) & 0xffffu) + 1u;
    bool packet_sent = false;

    for (uint32_t i = 0; i < count; ++i) {
        uint32_t descriptor_address = 0;
        if (!next_tx_descriptor(descriptor_address)) {
            set_dma_idle(true);
            return false;
        }

        RingDescriptor descriptor{};
        if (!load_descriptor(descriptor_address, descriptor)) {
            set_dma_idle(true);
            return false;
        }
        if (!(descriptor.flags & kTxValid)) break;

        const size_t length = static_cast<size_t>(descriptor.length) + 1;
        if (length > tx_buffer_.size() - tx_buffer_offset_) {
            fault_("nvnet: assembled TX packet exceeds the hardware buffer");
            set_dma_idle(true);
            return false;
        }
        if (!mem_read_(descriptor.buffer_address, tx_buffer_.data() + tx_buffer_offset_,
                       length)) {
            char message[96];
            std::snprintf(message, sizeof(message),
                          "nvnet: TX buffer DMA read failed at 0x%08x",
                          descriptor.buffer_address);
            fault_(message);
            set_dma_idle(true);
            return false;
        }
        tx_buffer_offset_ += length;
        ++tx_descriptors_;

        const bool last = (descriptor.flags & kTxLastPacket) != 0;
        if (last) {
            if (packet_sink_) packet_sink_(tx_buffer_.data(), tx_buffer_offset_);
            ++tx_packets_;
            tx_bytes_ += tx_buffer_offset_;
            tx_buffer_offset_ = 0;
            packet_sent = true;
        }

        descriptor.flags &=
            ~(kTxValid | kTxRetryError | kTxDeferred | kTxCarrierLost |
              kTxLateCollision | kTxUnderflow | kTxError);
        if (!store_descriptor(descriptor_address, descriptor)) {
            set_dma_idle(true);
            return false;
        }

        const uint64_t next64 =
            static_cast<uint64_t>(descriptor_address) + sizeof(RingDescriptor);
        const uint64_t end = static_cast<uint64_t>(base) +
                             static_cast<uint64_t>(count) * sizeof(RingDescriptor);
        const uint32_t next =
            next64 >= end ? base : static_cast<uint32_t>(next64);
        write_reg(kTxNextDesc, next);

        // xemu currently completes at most one packet per kick.
        if (last) break;
    }

    set_dma_idle(true);
    if (packet_sent) {
        write_reg(kIrqStatus, read_reg(kIrqStatus) | kIrqTx);
        update_irq();
    }
    return true;
}

void NvNet::mdio_transaction() {
    uint32_t address = read_reg(kMdioAddress);
    const uint8_t phy_address = static_cast<uint8_t>((address >> 5) & 0x1f);
    const uint8_t phy_register = static_cast<uint8_t>(address & 0x1f);

    if (address & kMdioWrite) {
        ++mdio_writes_;
        if (phy_address == kPhyAddress)
            phy_[phy_register] = static_cast<uint16_t>(read_reg(kMdioData));
    } else {
        ++mdio_reads_;
        write_reg(kMdioData, phy_address == kPhyAddress ? phy_[phy_register] : 0xffffffffu);
    }
    write_reg(kMdioAddress, address & ~kMdioInUse);
}

void NvNet::mmio_write(uint32_t addr, int size, uint32_t value) {
    if (!valid_access(addr, size, "register write")) return;
    const uint32_t offset = addr - kBase;

    if (offset == kMdioAddress) {
        if (size != 4) {
            fault_("nvnet: non-dword MDIO command");
            return;
        }
        write_reg(offset, value);
        mdio_transaction();
        return;
    }

    if (offset == kIrqStatus || offset == kMiiStatus) {
        write_ext(offset, size, read_ext(offset, size) & ~value);
        update_irq();
        return;
    }

    if (offset == kIrqMask) {
        write_ext(offset, size, value);
        update_irq();
        return;
    }

    if (offset == kTxRxControl) {
        if (size != 4) {
            fault_("nvnet: non-dword TX/RX control write");
            return;
        }
        const uint32_t old = read_reg(offset);
        write_reg(offset, (old & kControlIdle) | (value & ~kControlIdle));

        if (value & kControlKick) {
            ++tx_kicks_;
            transmit();
        }
        if (value & kControlReset) reset_descriptor_pointers();
        if (value & kControlClearIrq) {
            write_reg(kIrqStatus, 0);
            update_irq();
        } else if (value == 0) {
            write_reg(kSetupReg5, 0x80000000u);
        }
        return;
    }

    write_ext(offset, size, value);
}

void NvNet::update_irq() {
    const bool level = (read_reg(kIrqStatus) & read_reg(kIrqMask)) != 0;
    if (level != irq_level_) {
        irq_level_ = level;
        if (irq_fn_) irq_fn_(irq_num_, level);
    }
}

void NvNet::set_link(bool up) {
    if (up == link_up()) return;
    if (up) {
        phy_[kMiiBmsr] |= kBmsrLink;
        write_reg(kAdapterControl, read_reg(kAdapterControl) | kAdapterLinkUp);
    } else {
        phy_[kMiiBmsr] &= ~(kBmsrLink | kBmsrAutonegComplete);
        phy_[kMiiAnlpar] &= ~kAnAck;
        write_reg(kAdapterControl, read_reg(kAdapterControl) & ~kAdapterLinkUp);
    }
    write_reg(kMiiStatus, read_reg(kMiiStatus) | kMiiLinkChange);
    write_reg(kIrqStatus, read_reg(kIrqStatus) | kIrqMiiEvent);
    update_irq();
}

bool NvNet::link_up() const {
    return (phy_[kMiiBmsr] & kBmsrLink) != 0;
}

uint32_t NvNet::irq_status() const {
    return read_reg(kIrqStatus);
}

uint32_t NvNet::irq_mask() const {
    return read_reg(kIrqMask);
}

uint32_t NvNet::tx_rx_control() const {
    return read_reg(kTxRxControl);
}

}  // namespace xlle
