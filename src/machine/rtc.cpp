// xlle — bounded RTC / CMOS storage. See rtc.hpp for unimplemented clock scope.
#include "machine/rtc.hpp"

namespace xlle {

enum {
    REG_A = 0x0A, REG_B = 0x0B, REG_C = 0x0C, REG_D = 0x0D,
    REG_XBOX_CENTURY = 0x7F,
};

Mc146818Rtc::Mc146818Rtc() {
    // Status registers (xemu rtc_realizefn defaults).
    cmos_[REG_A] = 0x26;  // divider normal, periodic rate 1024 Hz (only active once PIE set)
    cmos_[REG_B] = 0x02;  // 24-hour, BCD, no interrupts enabled
    cmos_[REG_C] = 0x00;
    cmos_[REG_D] = 0x80;  // valid-RAM bit

    // Fixed, deterministic wall clock (BCD): 2026-07-16 12:00:00, Thursday.
    cmos_[0x00] = 0x00;  // seconds
    cmos_[0x02] = 0x00;  // minutes
    cmos_[0x04] = 0x12;  // hours (24h)
    cmos_[0x06] = 0x05;  // day of week (1=Sun .. 5=Thu)
    cmos_[0x07] = 0x16;  // day of month
    cmos_[0x08] = 0x07;  // month
    cmos_[0x09] = 0x26;  // year (00-99)
    cmos_[REG_XBOX_CENTURY] = 0x20;

    // Xbox "CMOS valid" signature: alternating 0x55/0xAA across the user-RAM ranges.
    uint8_t bits = 0x55;
    for (int i = 0x10; i < 0x70; ++i) { cmos_[i] = bits; bits = ~bits; }
    bits = 0x55;
    for (int i = 0x80; i < 0x100; ++i) { cmos_[i] = static_cast<uint8_t>(bits); bits = ~bits; }
}

uint32_t Mc146818Rtc::io_read(uint16_t port, int) {
    if (port == 0x70 || port == 0x72) return 0xFF;  // write-only index ports
    uint8_t val = cmos_[index_];
    if (index_ == REG_C) {
        // Register C is read-and-clear (and would lower IRQ8). No RTC IRQ modelled yet, so just
        // clear the flags on read.
        cmos_[REG_C] = 0x00;
    }
    // Register A UIP (bit 7) is left clear: no update is ever "in progress" in our model, so the
    // kernel's "wait for UIP==0" loop passes immediately.
    return val;
}

void Mc146818Rtc::io_write(uint16_t port, int, uint32_t value) {
    uint8_t v = static_cast<uint8_t>(value);
    if (port == 0x70) { index_ = v & 0x7F; return; }  // select register (NMI bit ignored)
    if (port == 0x72) { index_ = v; return; }  // extended full-byte selector
    switch (index_) {
        case REG_C: case REG_D: return;  // read-only status registers
        default: cmos_[index_] = v; return;
    }
}

}  // namespace xlle
