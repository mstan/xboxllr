// xlle — dual i8259 PIC. Faithful to xemu/hw/intc/i8259.c. See pic.hpp.
#include "machine/pic.hpp"

namespace xlle {

void Pic8259::reset() {
    irr = imr = isr = priority_add = irq_base = last_irr = 0;
    init_state = read_reg_select = 0;
    init4 = single_mode = auto_eoi = poll = special_mask = special_fully_nested = false;
    elcr = 0;
}

// Highest-priority set bit in `mask`, honoring rotation. Returns 0..7 (priority order) or 8
// if none (xemu get_priority).
int Pic8259::priority(uint8_t mask) const {
    if (mask == 0) return 8;
    int p = 0;
    while ((mask & (1 << ((p + priority_add) & 7))) == 0) p++;
    return p;
}

// Highest-priority requesting line not blocked by an equal/higher in-service bit; -1 if none
// (xemu pic_get_irq).
int Pic8259::get_irq() const {
    uint8_t mask = irr & ~imr;
    int p = priority(mask);
    if (p == 8) return -1;
    uint8_t isr_mask = isr;
    if (special_mask) isr_mask &= ~imr;
    int cur = priority(isr_mask);
    if (p < cur) return (p + priority_add) & 7;
    return -1;
}

void Pic8259::set_irq(int line, bool level) {
    uint8_t mask = 1 << line;
    if (elcr & mask) {  // level-triggered
        if (level) irr |= mask; else irr &= ~mask;
        if (level) last_irr |= mask; else last_irr &= ~mask;
    } else {  // edge-triggered: a rising edge latches a request
        if (level && !(last_irr & mask)) irr |= mask;
        if (level) last_irr |= mask; else last_irr &= ~mask;
    }
}

void Pic8259::ack(int line) {
    if (!auto_eoi) isr |= (1 << line);
    if (!(elcr & (1 << line))) irr &= ~(1 << line);  // edge: request cleared on INTA
}

void Pic8259::command_write(uint8_t val) {
    if (val & 0x10) {  // ICW1: (re)start the init sequence
        irr = isr = imr = 0;
        priority_add = 0;
        read_reg_select = 0;
        special_mask = false;
        init_state = 1;
        init4 = val & 1;
        single_mode = val & 2;
        return;
    }
    if (val & 0x08) {  // OCW3
        if (val & 0x02) read_reg_select = val & 1;      // RR/RIS: select IRR vs ISR readback
        if (val & 0x40) special_mask = (val >> 5) & 1;  // ESMM/SMM
        if (val & 0x04) poll = true;
        return;
    }
    // OCW2 (EOI family)
    int cmd = val >> 5;
    int line;
    switch (cmd) {
        case 1: case 5: {  // non-specific EOI (+rotate)
            int p = priority(isr);
            if (p == 8) break;
            line = (p + priority_add) & 7;
            isr &= ~(1 << line);
            if (cmd == 5) priority_add = (line + 1) & 7;
            break;
        }
        case 3: case 7:  // specific EOI (+rotate)
            line = val & 7;
            isr &= ~(1 << line);
            if (cmd == 7) priority_add = (line + 1) & 7;
            break;
        case 6:  // set priority
            priority_add = (val + 1) & 7;
            break;
        default: break;  // 0/4: rotate-on-auto-eoi toggles (not modelled)
    }
}

void Pic8259::data_write(uint8_t val) {
    switch (init_state) {
        case 0: imr = val; break;                                   // OCW1 (IMR)
        case 1: irq_base = val & 0xf8;                              // ICW2 (vector base)
                init_state = single_mode ? (init4 ? 3 : 0) : 2; break;
        case 2: init_state = init4 ? 3 : 0; break;                  // ICW3 (cascade, not stored)
        case 3: special_fully_nested = (val >> 4) & 1;              // ICW4
                auto_eoi = (val >> 1) & 1;
                init_state = 0; break;
    }
}

uint8_t Pic8259::command_read() {
    if (poll) {  // poll mode: next read acks and reports the pending IRQ
        poll = false;
        int line = get_irq();
        if (line >= 0) { ack(line); return 0x80 | line; }
        return 0;
    }
    return read_reg_select ? isr : irr;
}

// --- DualPic --------------------------------------------------------------------------------
void DualPic::raise(int isa_irq, bool level) {
    if (isa_irq < 8) {
        master_.set_irq(isa_irq, level);
    } else {
        slave_.set_irq(isa_irq - 8, level);
        // Cascade: the slave's INT drives master line 2 while the slave has a pending request.
        master_.set_irq(2, slave_.get_irq() >= 0);
    }
}

bool DualPic::intr_pending() const { return master_.get_irq() >= 0; }

int DualPic::acknowledge() {
    int m = master_.get_irq();
    if (m < 0) return -1;
    if (m == 2) {  // cascaded slave IRQ
        int s = slave_.get_irq();
        master_.ack(2);
        if (s >= 0) {
            slave_.ack(s);
            // Re-evaluate the cascade line after acking the slave.
            master_.set_irq(2, slave_.get_irq() >= 0);
            return slave_.irq_base + s;
        }
        return slave_.irq_base + 7;  // spurious slave
    }
    master_.ack(m);
    return master_.irq_base + m;
}

uint32_t DualPic::io_read(uint16_t port, int) {
    switch (port) {
        case 0x20: return master_.command_read();
        case 0x21: return master_.data_read();
        case 0xA0: return slave_.command_read();
        case 0xA1: return slave_.data_read();
        case 0x4D0: return master_.elcr;
        case 0x4D1: return slave_.elcr;
        default: return 0xFF;
    }
}

void DualPic::io_write(uint16_t port, int, uint32_t value) {
    uint8_t v = static_cast<uint8_t>(value);
    switch (port) {
        case 0x20: master_.command_write(v); break;
        case 0x21: master_.data_write(v); break;
        case 0xA0: slave_.command_write(v); break;
        case 0xA1: slave_.data_write(v); break;
        case 0x4D0: master_.elcr = v; break;  // Xbox: all bits writable (elcr_mask 0xFF)
        case 0x4D1: slave_.elcr = v; break;
        default: break;
    }
}

}  // namespace xlle
