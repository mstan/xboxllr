// xlle — MCPX integrated Ethernet controller register/PHY floor.
//
// This models the NVNet register file needed by the retail kernel and dashboard:
// interrupt aggregation, descriptor-ring reset, and synchronous MII/MDIO transactions
// against the integrated PHY at address 1. The PHY resets with link and autonegotiation
// complete, matching xemu's boot witness. Packet descriptor DMA and a host network backend
// are deliberately not implemented; an enabled transmit kick faults loudly at that boundary.
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>

#include "machine/io_device.hpp"
#include "machine/mmio_device.hpp"

namespace xlle {

class NvNet : public MmioDevice {
public:
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
    void set_link(bool up);

    uint64_t mdio_reads() const { return mdio_reads_; }
    uint64_t mdio_writes() const { return mdio_writes_; }
    uint64_t tx_kicks() const { return tx_kicks_; }
    bool link_up() const;
    uint32_t irq_status() const;
    uint32_t irq_mask() const;
    uint32_t tx_rx_control() const;

private:
    static constexpr uint32_t kIrqStatus = 0x000;
    static constexpr uint32_t kIrqMask = 0x004;
    static constexpr uint32_t kTransmitterControl = 0x084;
    static constexpr uint32_t kTxRingAddress = 0x100;
    static constexpr uint32_t kRxRingAddress = 0x104;
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
    static constexpr uint32_t kTxStart = 0x01;
    static constexpr uint32_t kControlKick = 0x01;
    static constexpr uint32_t kControlClearIrq = 0x02;
    static constexpr uint32_t kControlDmaDisable = 0x04;
    static constexpr uint32_t kControlIdle = 0x08;
    static constexpr uint32_t kControlReset = 0x10;
    static constexpr uint32_t kMiiLinkChange = 0x08;
    static constexpr uint32_t kAdapterLinkUp = 0x04;
    static constexpr uint32_t kMdioInUse = 0x8000;
    static constexpr uint32_t kMdioWrite = 0x0400;

    void reset();
    bool valid_access(uint32_t addr, int size, const char* operation);
    uint32_t read_ext(uint32_t offset, int size) const;
    void write_ext(uint32_t offset, int size, uint32_t value);
    uint32_t read_reg(uint32_t offset) const;
    void write_reg(uint32_t offset, uint32_t value);
    void reset_descriptor_pointers();
    void mdio_transaction();
    void update_irq();

    FaultFn fault_;
    std::array<uint8_t, kRegisterSize> regs_{};
    std::array<uint16_t, 32> phy_{};
    std::function<void(int, bool)> irq_fn_;
    int irq_num_ = 4;
    bool irq_level_ = false;
    uint64_t mdio_reads_ = 0;
    uint64_t mdio_writes_ = 0;
    uint64_t tx_kicks_ = 0;
};

}  // namespace xlle
