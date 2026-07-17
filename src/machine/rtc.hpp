// xlle — mc146818 RTC / CMOS (A2e), faithful to xemu/hw/rtc/mc146818rtc.c (Xbox variant).
//
// Ports 0x70 (index; bit7 = NMI-disable, ignored for indexing) / 0x71 (data). 256-byte CMOS.
// Xbox specifics (xemu): century byte relocated to index 0x7F; reset fills 0x10-0x6F and
// 0x80-0xFF with an alternating 0x55/0xAA pattern the Xbox BIOS checks as a "CMOS valid"
// signature. Status defaults: A=0x26, B=0x02, C=0x00, D=0x80. Time is seeded to a fixed,
// deterministic date (the real console has no RTC battery; the kernel resets the clock anyway).
#pragma once
#include <cstdint>

#include "machine/io_device.hpp"

namespace xlle {

class Mc146818Rtc : public IoPortDevice {
public:
    Mc146818Rtc();

    const char* name() const override { return "mc146818-rtc"; }
    uint16_t io_base() const override { return 0x70; }
    uint16_t io_size() const override { return 2; }
    uint32_t io_read(uint16_t port, int size) override;
    void io_write(uint16_t port, int size, uint32_t value) override;

private:
    uint8_t cmos_[256] = {0};
    uint8_t index_ = 0;
};

}  // namespace xlle
