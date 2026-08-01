// xlle — MCPX integrated Ethernet controller register/PHY floor.
//
// This models the NVNet register file needed by the retail kernel and dashboard:
// interrupt aggregation, descriptor-ring reset, synchronous MII/MDIO transactions against
// the integrated PHY at address 1, and transmit/receive descriptor DMA. The PHY resets with
// link and autonegotiation complete, matching xemu's boot witness. Completed TX packets are
// explicitly counted even without a host sink; receive_packet() is the bounded ingress
// boundary used by an optional host Ethernet transport.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>

#include "machine/io_device.hpp"
#include "machine/mmio_device.hpp"

namespace xlle {

class NvNet : public MmioDevice {
public:
    enum class RxResult {
        Accepted,
        Filtered,
        NotReady,
        Oversized,
        Fault,
    };

    static constexpr uint32_t kBase = 0xfef00000u;
    // Unicorn requires page-sized MMIO mappings. Hardware decodes only the first 0x400
    // bytes; accesses in the page padding are rejected by valid_access().
    static constexpr uint32_t kSize = 0x1000u;
    static constexpr uint32_t kRegisterSize = 0x400u;

    explicit NvNet(FaultFn fault) : fault_(std::move(fault)) { reset(); }

    const char* name() const override { return "nvnet"; }
    uint32_t mmio_base() const override { return kBase; }
    uint32_t mmio_size() const override { return kSize; }
    uint32_t mmio_read(uint32_t addr, int size) override;
    void mmio_write(uint32_t addr, int size, uint32_t value) override;

    void set_irq(std::function<void(int, bool)> fn, int irq) {
        irq_fn_ = std::move(fn);
        irq_num_ = irq;
        update_irq();
    }
    void set_mem_access(std::function<bool(uint32_t, void*, size_t)> read,
                        std::function<bool(uint32_t, const void*, size_t)> write) {
        mem_read_ = std::move(read);
        mem_write_ = std::move(write);
    }
    void set_packet_sink(std::function<void(const uint8_t*, size_t)> fn) {
        packet_sink_ = std::move(fn);
    }
    bool rx_ready();
    RxResult receive_packet(const uint8_t* data, size_t size);
    void set_link(bool up);

    uint64_t mdio_reads() const { return mdio_reads_; }
    uint64_t mdio_writes() const { return mdio_writes_; }
    uint64_t tx_kicks() const { return tx_kicks_; }
    uint64_t tx_descriptors() const { return tx_descriptors_; }
    uint64_t tx_packets() const { return tx_packets_; }
    uint64_t tx_bytes() const { return tx_bytes_; }
    uint64_t rx_offers() const { return rx_offers_; }
    uint64_t rx_descriptors() const { return rx_descriptors_; }
    uint64_t rx_packets() const { return rx_packets_; }
    uint64_t rx_bytes() const { return rx_bytes_; }
    uint64_t rx_filtered() const { return rx_filtered_; }
    uint64_t rx_oversized() const { return rx_oversized_; }
    uint64_t rx_not_ready() const { return rx_not_ready_; }
    bool link_up() const;
    uint32_t irq_status() const;
    uint32_t irq_mask() const;
    uint32_t tx_rx_control() const;
    uint32_t receiver_control() const { return read_reg(kReceiverControl); }
    uint32_t rx_current_descriptor() const { return read_reg(kRxCurrentDesc); }
    uint32_t rx_next_descriptor() const { return read_reg(kRxNextDesc); }

private:
    static constexpr uint32_t kIrqStatus = 0x000;
    static constexpr uint32_t kIrqMask = 0x004;
    static constexpr uint32_t kTransmitterControl = 0x084;
    static constexpr uint32_t kTxRingAddress = 0x100;
    static constexpr uint32_t kRxRingAddress = 0x104;
    static constexpr uint32_t kRingSize = 0x108;
    static constexpr uint32_t kTxCurrentDesc = 0x11c;
    static constexpr uint32_t kRxCurrentDesc = 0x120;
    static constexpr uint32_t kSetupReg5 = 0x130;
    static constexpr uint32_t kTxNextDesc = 0x134;
    static constexpr uint32_t kRxNextDesc = 0x138;
    static constexpr uint32_t kTxRxControl = 0x144;
    static constexpr uint32_t kMiiStatus = 0x180;
    static constexpr uint32_t kAdapterControl = 0x188;
    static constexpr uint32_t kMdioAddress = 0x190;
    static constexpr uint32_t kMdioData = 0x194;

    static constexpr uint32_t kIrqMiiEvent = 0x40;
    static constexpr uint32_t kIrqTx = 0x10;
    static constexpr uint32_t kIrqRx = 0x02;
    static constexpr uint32_t kTxStart = 0x01;
    static constexpr uint32_t kReceiverControl = 0x094;
    static constexpr uint32_t kRxStart = 0x01;
    static constexpr uint32_t kPacketFilter = 0x08c;
    static constexpr uint32_t kPacketFilterMyAddress = 0x20;
    static constexpr uint32_t kMacAddressA = 0x0a8;
    static constexpr uint32_t kMacAddressB = 0x0ac;
    static constexpr uint32_t kMulticastAddressA = 0x0b0;
    static constexpr uint32_t kMulticastAddressB = 0x0b4;
    static constexpr uint32_t kMulticastMaskA = 0x0b8;
    static constexpr uint32_t kMulticastMaskB = 0x0bc;
    static constexpr uint32_t kControlKick = 0x01;
    static constexpr uint32_t kControlClearIrq = 0x02;
    static constexpr uint32_t kControlDmaDisable = 0x04;
    static constexpr uint32_t kControlIdle = 0x08;
    static constexpr uint32_t kControlReset = 0x10;
    static constexpr uint32_t kMiiLinkChange = 0x08;
    static constexpr uint32_t kAdapterLinkUp = 0x04;
    static constexpr uint32_t kMdioInUse = 0x8000;
    static constexpr uint32_t kMdioWrite = 0x0400;
    static constexpr uint16_t kTxLastPacket = 1u << 0;
    static constexpr uint16_t kTxRetryError = 1u << 3;
    static constexpr uint16_t kTxDeferred = 1u << 10;
    static constexpr uint16_t kTxCarrierLost = 1u << 11;
    static constexpr uint16_t kTxLateCollision = 1u << 12;
    static constexpr uint16_t kTxUnderflow = 1u << 13;
    static constexpr uint16_t kTxError = 1u << 14;
    static constexpr uint16_t kTxValid = 1u << 15;
    static constexpr uint16_t kRxDescriptorValid = 1u << 0;
    static constexpr uint16_t kRxBit4 = 1u << 4;
    static constexpr uint16_t kRxAvailable = 1u << 15;
    static constexpr size_t kTxBufferSize = 1500 + 128;
    static constexpr size_t kRxBufferSize = 1500 + 128;

    struct RingDescriptor {
        uint32_t buffer_address;
        uint16_t length;
        uint16_t flags;
    };

    void reset();
    bool valid_access(uint32_t addr, int size, const char* operation);
    uint32_t read_ext(uint32_t offset, int size) const;
    void write_ext(uint32_t offset, int size, uint32_t value);
    uint32_t read_reg(uint32_t offset) const;
    void write_reg(uint32_t offset, uint32_t value);
    void reset_descriptor_pointers();
    bool transmit();
    bool load_descriptor(uint32_t address, RingDescriptor& descriptor);
    bool store_descriptor(uint32_t address, const RingDescriptor& descriptor);
    bool next_tx_descriptor(uint32_t& address);
    bool next_rx_descriptor(uint32_t& address);
    bool receive_filter(const uint8_t* data, size_t size) const;
    void set_dma_idle(bool idle);
    void mdio_transaction();
    void update_irq();

    FaultFn fault_;
    std::array<uint8_t, kRegisterSize> regs_{};
    std::array<uint16_t, 32> phy_{};
    std::function<void(int, bool)> irq_fn_;
    std::function<bool(uint32_t, void*, size_t)> mem_read_;
    std::function<bool(uint32_t, const void*, size_t)> mem_write_;
    std::function<void(const uint8_t*, size_t)> packet_sink_;
    int irq_num_ = 4;
    bool irq_level_ = false;
    std::array<uint8_t, kTxBufferSize> tx_buffer_{};
    size_t tx_buffer_offset_ = 0;
    uint64_t mdio_reads_ = 0;
    uint64_t mdio_writes_ = 0;
    uint64_t tx_kicks_ = 0;
    uint64_t tx_descriptors_ = 0;
    uint64_t tx_packets_ = 0;
    uint64_t tx_bytes_ = 0;
    uint64_t rx_offers_ = 0;
    uint64_t rx_descriptors_ = 0;
    uint64_t rx_packets_ = 0;
    uint64_t rx_bytes_ = 0;
    uint64_t rx_filtered_ = 0;
    uint64_t rx_oversized_ = 0;
    uint64_t rx_not_ready_ = 0;
};

}  // namespace xlle
