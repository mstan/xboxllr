// xlle — NMI status & control / system-control port B (0x61).
//
// Bits (PC/Xbox southbridge): 0 = timer-2 gate (speaker), 1 = speaker data enable, 4 = refresh
// toggle (flips continuously as a coarse timing reference), 5 = timer-2 output. Writes store
// the low control bits; reads return them plus the live refresh/timer-2 bits. This is enough
// for the kernel's early HAL timing/speaker touches; a fuller model joins when a trace needs it.
#pragma once
#include <cstdint>

#include "machine/io_device.hpp"

namespace xlle {

class Port61 : public IoPortDevice {
public:
    const char* name() const override { return "sysctl-0x61"; }
    uint16_t io_base() const override { return 0x61; }
    uint16_t io_size() const override { return 1; }

    uint32_t io_read(uint16_t, int) override {
        refresh_ ^= 0x10;  // bit 4 toggles each read (refresh request line)
        return (ctrl_ & 0x03) | refresh_;
    }
    void io_write(uint16_t, int, uint32_t value) override {
        ctrl_ = static_cast<uint8_t>(value) & 0x03;
    }

private:
    uint8_t ctrl_ = 0;
    uint8_t refresh_ = 0;
};

}  // namespace xlle
