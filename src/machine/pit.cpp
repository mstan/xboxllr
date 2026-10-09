// xlle — bounded i8254 PIT. See pit.hpp and Intel 8254 datasheet 231164-005.
#include "machine/pit.hpp"

namespace xlle {

uint16_t Pit8254::current_count(const Channel& c) const {
    uint32_t reload = c.reload ? c.reload : 0x10000;
    uint64_t elapsed = now_ - c.load_tick;
    // Mode 0 continues counting after terminal count, without another output edge.
    // Mode 3 count readback remains outside the qualified mode-2 clock profile.
    uint32_t rem = c.mode == 0 ? reload - static_cast<uint32_t>(elapsed)
                              : reload - static_cast<uint32_t>(elapsed % reload);
    return static_cast<uint16_t>(rem);
}

void Pit8254::load_count(Channel& c, uint32_t v) {
    c.reload = (v == 0) ? 0x10000 : v;
    c.load_tick = now_;
    // If this is channel 0 in a periodic mode, (re)arm the next IRQ0.
    if (&c == &ch_[0]) ch0_next_irq_ =
        (c.mode == 0 || c.mode == 2 || c.mode == 3) ? now_ + c.reload : 0;
}

void Pit8254::set_time(uint64_t now) {
    if (now < now_) return;
    now_ = now;
    Channel& c0 = ch_[0];
    uint32_t reload = c0.reload ? c0.reload : 0x10000;
    // Periodic tick for the common rate-generator / square-wave modes. One pulse per period;
    // if several periods elapsed in one step, the intermediate ticks are coalesced (lost-tick
    // approximation — acceptable while the step is small vs the reload).
    if (c0.mode == 2 || c0.mode == 3 || c0.mode == 0) {
        if (ch0_next_irq_ && now_ >= ch0_next_irq_) {
            if (irq0_pulse_) irq0_pulse_();
            // Advance to the next future boundary.
            uint64_t behind = now_ - ch0_next_irq_;
            ch0_next_irq_ = c0.mode == 0 ? 0 : ch0_next_irq_ + reload * (1 + behind / reload);
        }
    }
}

uint32_t Pit8254::io_read(uint16_t port, int) {
    uint16_t idx = port - 0x40;
    if (idx == 3) return 0;  // mode/command register is write-only
    Channel& c = ch_[idx];
    uint16_t val = c.latched ? c.latched_value : current_count(c);
    uint8_t ret;
    switch (c.rw) {
        case 1: ret = val & 0xff; c.latched = false; break;               // LSB
        case 2: ret = val >> 8; c.latched = false; break;                 // MSB
        default:  // LSB then MSB
            if (c.read_state == 0) { ret = val & 0xff; c.read_state = 1; }
            else { ret = val >> 8; c.read_state = 0; c.latched = false; }
            break;
    }
    return ret;
}

void Pit8254::io_write(uint16_t port, int, uint32_t value) {
    uint8_t v = static_cast<uint8_t>(value);
    uint16_t idx = port - 0x40;
    if (idx == 3) {  // mode/command register
        int channel = v >> 6;
        if (channel == 3) return;  // read-back command (unmodelled; boot uses latch instead)
        Channel& c = ch_[channel];
        int access = (v >> 4) & 3;
        if (access == 0) {  // counter-latch command
            if (!c.latched) {
                c.latched_value = current_count(c);
                c.latched = true;
                c.read_state = 0;
            }
        } else {
            c.rw = access;
            c.read_state = 0;
            c.write_state = 0;
            c.mode = (v >> 1) & 7;
            if (c.mode >= 6) c.mode -= 4;
            c.latched = false;
            if (channel == 0) ch0_next_irq_ = 0;
        }
        return;
    }
    Channel& c = ch_[idx];
    switch (c.rw) {
        case 1: load_count(c, v); break;               // LSB only
        case 2: load_count(c, v << 8); break;          // MSB only
        default:  // LSB then MSB
            if (c.write_state == 0) { c.write_latch = v; c.write_state = 1; }
            else { load_count(c, c.write_latch | (v << 8)); c.write_state = 0; }
            break;
    }
}

}  // namespace xlle
