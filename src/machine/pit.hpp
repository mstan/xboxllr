// xlle — bounded i8254 PIT system-clock model. See SCHEDULING-LLE.md.
//
// Ports 0x40-0x43. Channel 0's output drives IRQ0 (the system tick). The Xbox PIT input clock
// is 1,125,000 Hz. xemu uses a +1 Hz workaround; that workaround is not used here.
//
// Time base: we have no cycle counter, so "PIT ticks" are derived from retired instructions by
// the machine (retired * PIT_FREQ / CPU_HZ) and pushed in via set_time(). Channel 0 pulses
// IRQ0 once per programmed reload period. The exact instructions-per-tick ratio is an
// approximation. Native qualification instead supplies explicit PIT ticks. This model
// qualifies the steady mode-2 clock, not every gate, waveform or programming-edge phase.
#pragma once
#include <cstdint>
#include <functional>

#include "machine/io_device.hpp"

namespace xlle {

class Pit8254 : public IoPortDevice {
public:
    static constexpr uint32_t kPitFreq = 1125000;  // Xbox PIT input clock (Hz)

    const char* name() const override { return "i8254-pit"; }
    uint16_t io_base() const override { return 0x40; }
    uint16_t io_size() const override { return 4; }

    // Called on each IRQ0 period boundary to pulse the line (rising then falling edge).
    void set_irq0(std::function<void()> pulse) { irq0_pulse_ = std::move(pulse); }

    uint32_t io_read(uint16_t port, int size) override;
    void io_write(uint16_t port, int size, uint32_t value) override;

    // Advance monotonically to `now`. A late caller gets one pending edge, just as an
    // edge-triggered PIC retains one request. Service each next_irq0_tick() to preserve
    // distinct interrupts; a large jump cannot recover intervening CPU/EOI activity.
    void set_time(uint64_t now);

    // Absolute tick of the next channel-0 IRQ0 edge, or 0 when the programmed mode raises
    // none. This is the PIT's next guest-visible EVENT, as opposed to the input clock that
    // ticks 1725x more often; the machine's slice loop uses it to bound a slice so no
    // interrupt edge can be stepped over (see XboxMachine::next_device_deadline).
    uint64_t next_irq0_tick() const {
        return ch0_next_irq_;
    }

private:
    struct Channel {
        uint32_t reload = 0x10000;  // programmed count (0 -> 0x10000)
        uint8_t mode = 3;           // 0..5
        uint8_t rw = 3;             // access: 1=LSB, 2=MSB, 3=LSB-then-MSB
        uint8_t write_state = 0;    // for rw==3: 0 expecting LSB, 1 expecting MSB
        uint8_t read_state = 0;
        uint16_t write_latch = 0;
        bool latched = false;
        uint16_t latched_value = 0;
        uint64_t load_tick = 0;     // absolute tick when the current count was loaded
    };

    uint16_t current_count(const Channel& c) const;  // live down-counter value
    void load_count(Channel& c, uint32_t v);

    Channel ch_[3];
    uint64_t now_ = 0;
    uint64_t ch0_next_irq_ = 0;  // absolute tick of the next IRQ0
    std::function<void()> irq0_pulse_;
};

}  // namespace xlle
