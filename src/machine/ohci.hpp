// xlle — MCPX USB 1.1 OHCI host-controller register/timing floor.
//
// The Xbox exposes two standard PCI OHCI controllers at 0xFED00000 and 0xFED08000, each
// with four root-hub ports. This model implements the standard register lifecycle, 1 kHz
// frame counter/HCCA writeback, and level interrupt aggregation with no devices attached.
// Endpoint/TD execution and XID devices remain the next input milestone.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

#include "machine/io_device.hpp"
#include "machine/mmio_device.hpp"

namespace xlle {

class Ohci : public MmioDevice {
public:
    static constexpr uint32_t kSize = 0x1000;

    Ohci(uint32_t base, FaultFn fault) : base_(base), fault_(std::move(fault)) { reset(); }

    const char* name() const override { return base_ == 0xFED00000u ? "ohci0" : "ohci1"; }
    uint32_t mmio_base() const override { return base_; }
    uint32_t mmio_size() const override { return kSize; }
    uint32_t mmio_read(uint32_t addr, int size) override;
    void mmio_write(uint32_t addr, int size, uint32_t value) override;

    void set_mem_write(std::function<bool(uint32_t, const void*, size_t)> fn) {
        mem_write_ = std::move(fn);
    }
    void set_irq(std::function<void(int, bool)> fn, int irq) {
        irq_fn_ = std::move(fn);
        irq_num_ = irq;
        update_irq();
    }
    void set_frame(uint64_t frame);

    uint64_t frames() const { return frames_; }
    uint32_t control() const { return regs_[1]; }
    uint32_t hcca() const { return regs_[6]; }

private:
    static constexpr uint32_t kCtlHcfs = 3u << 6;
    static constexpr uint32_t kUsbOperational = 2u << 6;
    static constexpr uint32_t kStatusHcr = 1u << 0;
    static constexpr uint32_t kStatusClf = 1u << 1;
    static constexpr uint32_t kStatusBlf = 1u << 2;
    static constexpr uint32_t kIntrSf = 1u << 2;
    static constexpr uint32_t kIntrFno = 1u << 5;
    static constexpr uint32_t kIntrMie = 1u << 31;
    static constexpr uint32_t kPortPps = 1u << 8;
    static constexpr uint32_t kPortWtc = 0x001f0000u;

    void reset();
    void update_irq();
    void frame_step();
    uint32_t read_reg(uint32_t index) const;
    void write_reg(uint32_t index, uint32_t value);

    uint32_t base_;
    FaultFn fault_;
    std::array<uint32_t, 25> regs_{};
    std::function<bool(uint32_t, const void*, size_t)> mem_write_;
    std::function<void(int, bool)> irq_fn_;
    int irq_num_ = 1;
    bool irq_level_ = false;
    uint64_t frame_ = 0;
    uint64_t frames_ = 0;
};

}  // namespace xlle
