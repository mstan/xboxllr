// xlle — Xbox ACPI/PM controller. See pm.hpp for the register map and value policy.
#include "machine/pm.hpp"

namespace xlle {

// Offsets from base 0x8000 (xemu acpi_xbox.c).
enum {
    PM1a_STS = 0x00, PM1a_EN = 0x02, PM1_CNT = 0x04, PM_TMR = 0x08,
    GPE0_BASE = 0x20, GPE0_END = 0x28,
    GPIO_BASE = 0xC0, GPIO_END = 0xDA,
    GPIO_ASPECT_RATIO = 0xC0 + 0x16,  // 0xD6
};

uint32_t XboxPm::io_read(uint16_t port, int size) {
    uint16_t off = port - kBase;

    if (off == PM1a_STS) return pm1_sts_;
    if (off == PM1a_EN) return pm1_en_;
    if (off == PM1_CNT) return pm1_cnt_;
    if (off == PM_TMR) {
        // Monotonic 24-bit timer. Stand-in rate (advances per read) — enough for the kernel's
        // wait/calibration loops to progress; real 3.375 MHz cadence is a calibration debt.
        pm_tmr_ = (pm_tmr_ + 1) & 0x00FFFFFF;
        return pm_tmr_;
    }
    if (off >= GPE0_BASE && off < GPE0_END) return 0;  // GPE status/enable: none pending
    if (off >= GPIO_BASE && off < GPIO_END) {
        if (off == GPIO_BASE) {  // GPIO[0]: TV field pin toggles bit5 (xemu)
            field_pin_ = (field_pin_ + 1) & 1;
            return static_cast<uint32_t>(field_pin_) << 5;
        }
        return 0;
    }

    if (unmodeled_cb_) unmodeled_cb_(false, off, 0);
    (void)size;
    return 0;
}

void XboxPm::io_write(uint16_t port, int size, uint32_t value) {
    uint16_t off = port - kBase;

    switch (off) {
        case PM1a_STS: pm1_sts_ &= ~static_cast<uint16_t>(value); return;  // write-1-clear
        case PM1a_EN:  pm1_en_ = value; return;
        case PM1_CNT:  pm1_cnt_ = value; return;  // SLP_EN handling deferred (no sleep in boot)
        default: break;
    }
    if (off >= GPE0_BASE && off < GPE0_END) return;         // GPE writes: accept
    if (off >= GPIO_BASE && off < GPIO_END) return;         // GPIO writes: accept (incl aspect)

    if (unmodeled_cb_) unmodeled_cb_(true, off, value);
    (void)size;
}

}  // namespace xlle
