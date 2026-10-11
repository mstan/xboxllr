// xlle — bounded RTC / CMOS storage model (A2e).
//
// Port70 selects seven index bits; port72 selects all eight. Ports71/73 share
// one data register and selector (pinned xemu Xbox RTC and original kernel
// CMOS validation/repair). Exact silicon alias/index behavior remains open.
// Xbox specifics (xemu): century byte relocated to index 0x7F; reset fills 0x10-0x6F and
// 0x80-0xFF with an alternating 0x55/0xAA pattern the Xbox BIOS checks as a "CMOS valid"
// signature. Status defaults: A=0x26, B=0x02, C=0x00, D=0x80. Time is seeded to a fixed,
// deterministic date. Oscillator progression, UIP timing and IRQ8 are not
// implemented here; the native-title adapter admits only qualified CMOS RAM.
#pragma once
#include <cstdint>

#include "machine/io_device.hpp"

namespace xlle {

class Mc146818Rtc : public IoPortDevice {
public:
    Mc146818Rtc();

    const char* name() const override { return "mc146818-rtc"; }
    uint16_t io_base() const override { return 0x70; }
    uint16_t io_size() const override { return 4; }
    uint32_t io_read(uint16_t port, int size) override;
    void io_write(uint16_t port, int size, uint32_t value) override;
    uint8_t selected_index() const { return index_; }
    static bool signature_ram(uint8_t index) {
        return (index >= 0x10 && index < 0x70) || index >= 0x80;
    }

private:
    uint8_t cmos_[256] = {0};
    uint8_t index_ = 0;
};

}  // namespace xlle
