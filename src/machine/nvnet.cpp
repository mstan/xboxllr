// xlle — MCPX integrated Ethernet controller register/PHY floor.
#include "machine/nvnet.hpp"

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
            const bool tx_enabled = (read_reg(kTransmitterControl) & kTxStart) != 0;
            const bool dma_enabled = (read_reg(kTxRxControl) & kControlDmaDisable) == 0;
            if (tx_enabled && dma_enabled && link_up())
                fault_("nvnet: enabled TX descriptor kick is not implemented");
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
