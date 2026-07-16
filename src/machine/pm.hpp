// xlle — Xbox ACPI/Power-Management controller (A2e kernel-init device).
//
// Lives behind the LPC function's I/O BAR0 at base 0x8000 (256 ports). Sub-blocks (offsets
// from base, per xemu/hw/xbox/acpi_xbox.c):
//   0x00 PM1a_STS/EN, 0x04 PM1_CNT, 0x08 PM_TMR (24-bit, 3.375 MHz),
//   0x20 GPE0 (len 4), 0xC0 GPIO (len 26; +0x16 = TV aspect-ratio strap).
//
// Same value policy as nv2a.hpp: known registers modelled faithfully; unknown-within-block
// reads return the xemu default (0) and are logged; writes accepted+logged. The PM timer is a
// monotonic stand-in (advances per read) so kernel wait/calibration loops make progress — its
// exact rate is a debt to calibrate against xemu/hardware (A2e).
#pragma once
#include <cstdint>
#include <functional>

#include "machine/io_device.hpp"

namespace xlle {

class XboxPm : public IoPortDevice {
public:
    static constexpr uint16_t kBase = 0x8000;
    static constexpr uint16_t kSize = 0x100;

    explicit XboxPm(FaultFn fault) : fault_(std::move(fault)) {}

    const char* name() const override { return "xbox-pm"; }
    uint16_t io_base() const override { return kBase; }
    uint16_t io_size() const override { return kSize; }
    uint32_t io_read(uint16_t port, int size) override;
    void io_write(uint16_t port, int size, uint32_t value) override;

    // Observability: fired for an access that hit an unmodelled PM register (xemu default).
    void on_unmodeled(std::function<void(bool write, uint16_t off, uint32_t value)> cb) {
        unmodeled_cb_ = std::move(cb);
    }

private:
    FaultFn fault_;
    std::function<void(bool, uint16_t, uint32_t)> unmodeled_cb_;

    uint16_t pm1_sts_ = 0, pm1_en_ = 0, pm1_cnt_ = 0;
    uint32_t pm_tmr_ = 0;        // 24-bit monotonic PM timer (stand-in rate)
    uint8_t field_pin_ = 0;      // GPIO[0] bit5 toggles per read (TV field, xemu)
};

}  // namespace xlle
