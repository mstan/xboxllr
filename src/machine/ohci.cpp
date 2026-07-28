#include "machine/ohci.hpp"

#include <cstring>

namespace xlle {

void Ohci::reset() {
    regs_.fill(0);
    regs_[0] = 0x10;        // HcRevision: OHCI 1.0
    regs_[4] = kIntrMie;    // shared HcInterruptEnable/Disable state
    regs_[13] = 0x27782edf; // HcFmInterval (QEMU/Cxbx reset)
    regs_[17] = 0x628;      // HcLSThreshold
    regs_[18] = 0x1204;     // NOCP | NPS | four root ports
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
        case 0:  // HcRevision is read-only.
        case 7:  // HcPeriodCurrentED
        case 12: // HcDoneHead
        case 14: // HcFmRemaining
        case 15: // HcFmNumber
            return;
        case 1: { // HcControl
            const uint32_t old_state = regs_[1] & kCtlHcfs;
            const uint32_t next_state = value & kCtlHcfs;
            regs_[1] = value & 0x7ffu;
            if (old_state != next_state && next_state == 0) reset();
            return;
        }
        case 2: // HcCommandStatus: set-on-write, HCR self-clears through reset.
            value &= ~0xc0u;
            regs_[2] |= value;
            if (regs_[2] & kStatusHcr) reset();
            return;
        case 3: // HcInterruptStatus: W1C
            regs_[3] &= ~value;
            update_irq();
            return;
        case 4: // HcInterruptEnable
            regs_[4] |= value;
            update_irq();
            return;
        case 5: // HcInterruptDisable
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
            regs_[index] = value & 0xfffffff0u;
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
        case 18: // Xbox root-hub topology is fixed.
        case 19:
            return;
        case 20: // Root-hub status W1C/change-control subset.
            regs_[20] &= ~(value & 0x00030000u);
            if (value & (1u << 31)) regs_[20] &= ~(1u << 15);
            if (value & (1u << 15)) regs_[20] |= 1u << 15;
            return;
        case 21:
        case 22:
        case 23:
        case 24:
            // No device is attached. Honor change-bit clearing and software power state;
            // reads still report hard-wired port power as on.
            regs_[index] &= ~(value & kPortWtc);
            if (value & 1u) regs_[index] &= ~(1u << 1);       // clear port enable
            if (value & (1u << 1)) regs_[index] |= 1u << 1;  // set port enable
            if (value & (1u << 8)) regs_[index] |= kPortPps;
            if (value & (1u << 9)) regs_[index] &= ~kPortPps;
            return;
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
    const bool level = (regs_[4] & kIntrMie) && (regs_[3] & regs_[4] & ~kIntrMie);
    if (level != irq_level_) {
        irq_level_ = level;
        if (irq_fn_) irq_fn_(irq_num_, level);
    }
}

void Ohci::frame_step() {
    if ((regs_[1] & kCtlHcfs) != kUsbOperational) return;

    const uint16_t old_number = static_cast<uint16_t>(regs_[15]);
    const uint16_t number = static_cast<uint16_t>(old_number + 1);
    regs_[15] = number;
    ++frames_;

    if (regs_[6] && mem_write_) {
        mem_write_(regs_[6] + 0x80, &number, sizeof(number));
        const uint16_t pad = 0;
        mem_write_(regs_[6] + 0x82, &pad, sizeof(pad));
        mem_write_(regs_[6] + 0x84, &regs_[12], sizeof(regs_[12]));
    }

    // With no attached devices or transfer executor, empty control/bulk lists drain.
    if (!regs_[8]) regs_[2] &= ~kStatusClf;
    if (!regs_[10]) regs_[2] &= ~kStatusBlf;
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
