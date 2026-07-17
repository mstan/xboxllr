// xlle — dual i8259 PIC (A2e interrupt controller), faithful to xemu/hw/intc/i8259*.c.
//
// Two cascaded 8259s: master at 0x20/0x21, slave at 0xA0/0xA1, with ELCR at 0x4D0/0x4D1.
// The slave cascades into master IRQ2. Devices raise IRQ lines; the PIC resolves the highest-
// priority unmasked request and hands the CPU the vector (irq_base + line) on acknowledge.
//
// Xbox quirk (xemu i8259_common.c:101-103): elcr_mask is 0xFF on both chips (every line is
// ELCR-configurable), unlike the PC's fixed 0xf8/0xde.
#pragma once
#include <cstdint>

#include "machine/io_device.hpp"

namespace xlle {

// One 8259 core (master or slave). Register semantics per xemu hw/intc/i8259.c.
struct Pic8259 {
    uint8_t irr = 0;          // interrupt request register (raised, not yet served)
    uint8_t imr = 0;          // interrupt mask register (1 = masked)
    uint8_t isr = 0;          // in-service register
    uint8_t priority_add = 0; // rotation: lowest-priority IRQ index + ...
    uint8_t irq_base = 0;     // vector base programmed by ICW2 (bits 7:3)
    uint8_t last_irr = 0;     // edge detection: previous IRR pin state
    uint8_t elcr = 0;         // edge/level control (1 = level-triggered)
    uint8_t init_state = 0;   // 0 normal; 1 awaiting ICW2; 2 ICW3; 3 ICW4
    uint8_t read_reg_select = 0;  // OCW3: 0 = read IRR, 1 = read ISR
    bool init4 = false, single_mode = false, auto_eoi = false;
    bool poll = false, special_mask = false, special_fully_nested = false;

    void reset();
    void set_irq(int line, bool level);   // raise/lower a request line (edge/level per ELCR)
    int  get_irq() const;                 // highest-priority requesting line, or -1
    int  priority(uint8_t mask) const;    // highest-priority set bit accounting for rotation
    void ack(int line);                   // move line from IRR to ISR on INTA
    void command_write(uint8_t val);      // write to the command port (ICW1/OCW2/OCW3)
    void data_write(uint8_t val);         // write to the data port (ICW2/3/4 or OCW1/IMR)
    uint8_t command_read();               // read command port (IRR/ISR, or poll)
    uint8_t data_read() const { return imr; }
};

// The dual-PIC controller as an I/O device. Occupies 0x20/0x21 (master) — the machine also
// routes the slave ports (0xA0/0xA1) and ELCR (0x4D0/0x4D1) to this device (see io_range()).
class DualPic : public IoPortDevice {
public:
    DualPic() { master_.reset(); slave_.reset(); }

    const char* name() const override { return "i8259-pic"; }
    // Primary declared range is the master pair; the machine additionally maps the slave and
    // ELCR ranges to this same device via extra_ranges().
    uint16_t io_base() const override { return 0x20; }
    uint16_t io_size() const override { return 2; }
    // Master (0x20/0x21), slave (0xA0/0xA1), and ELCR (0x4D0/0x4D1) all route here.
    bool owns(uint16_t p) const override {
        return p == 0x20 || p == 0x21 || p == 0xA0 || p == 0xA1 || p == 0x4D0 || p == 0x4D1;
    }
    uint32_t io_read(uint16_t port, int size) override;
    void io_write(uint16_t port, int size, uint32_t value) override;

    // A device raises/lowers an ISA IRQ line (0..15). 2 is the cascade (internal).
    void raise(int isa_irq, bool level);

    // The vector the next INTA would deliver, WITHOUT side effects (so delivery can be confirmed
    // before committing the ISR bookkeeping). -1 if none pending.
    int peek() const;

    // INTA acknowledge: returns the vector to deliver for the highest-priority pending IRQ, or
    // -1 if none is pending. Performs the ISR/EOI bookkeeping a real INTA cycle would.
    int acknowledge();

    // Is any unmasked interrupt pending (would the CPU's INTR line be asserted)?
    bool intr_pending() const;

    Pic8259& master() { return master_; }
    Pic8259& slave() { return slave_; }

private:
    // Recompute the cascade: the slave's INT output drives master line 2. Must run after ANY
    // slave state change — a raise, but also IMR/EOI/ELCR register writes (a request latched
    // while masked asserts INT only when the kernel later unmasks it; qemu's pic_update_irq
    // runs after every write and propagates through the cascade qemu_irq the same way).
    void update_cascade() { master_.set_irq(2, slave_.get_irq() >= 0); }

    Pic8259 master_, slave_;
};

}  // namespace xlle
