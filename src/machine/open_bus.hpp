// xlle — open-bus I/O region: a device that is genuinely ABSENT on this hardware.
//
// On the real retail console an access to an unpopulated I/O port floats the bus: reads return
// 0xFF, writes go nowhere. This is faithful hardware behavior, NOT the "unimplemented" case —
// so it is modelled explicitly (per silicon confirmation) rather than left to the machine's
// default loud fault-stop. Each OpenBus range therefore documents a deliberate, oracle-checked
// "nothing lives here" decision; the fault-stop default still guards genuinely-unknown ports.
#pragma once
#include <cstdint>

#include "machine/io_device.hpp"

namespace xlle {

class OpenBus : public IoPortDevice {
public:
    OpenBus(uint16_t base, uint16_t size, const char* name)
        : base_(base), size_(size), name_(name) {}

    const char* name() const override { return name_; }
    uint16_t io_base() const override { return base_; }
    uint16_t io_size() const override { return size_; }
    uint32_t io_read(uint16_t, int size) override {
        // All-ones across the access width (byte reads 0xFF, word 0xFFFF, dword 0xFFFFFFFF).
        return (size >= 4) ? 0xFFFFFFFFu : ((1u << (size * 8)) - 1);
    }
    void io_write(uint16_t, int, uint32_t) override {}  // floats the bus; write discarded

private:
    uint16_t base_, size_;
    const char* name_;
};

}  // namespace xlle
