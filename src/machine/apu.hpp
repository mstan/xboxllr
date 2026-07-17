// xlle — MCPX APU (audio processing unit) MMIO shell at 0xFE800000 (512 KB, PCI 00:05.0
// BAR0) + the generic register-file shell it is built on.
//
// Harvested from xemu/hw/xbox/mcpx/apu/apu.c: the top level is a RAM-backed register file
// (reads return the last write, reset 0) with three modelled specials — NV_PAPU_ISTS
// (0x1000) is write-1-clear interrupt status, NV_PAPU_FEMEMDATA (0x1334) is a "magic write"
// that stores the value to the guest-physical address held in NV_PAPU_FEMEMADDR (0x1324),
// and NV_PAPU_XGSCNT (0x200C) is a free-running counter. The VP/GP/EP DSP subregions
// (0x20000/0x30000/0x50000) have richer semantics in xemu; boot-time accesses there are
// logged so any load-bearing use gets characterized (same documented value policy as NV2A:
// xemu boots this same retail kernel, so register-file behavior is a boot-correct witness).
//
// No audio is produced — that is deliberate scope (A4 boots the dashboard; APU DSP fidelity
// is a later arc). Nothing here silently no-ops a *decoded* register: every access lands in
// the register file exactly as xemu's does.
#pragma once
#include <cstdint>
#include <cstdio>
#include <functional>
#include <vector>

#include "machine/mmio_device.hpp"

namespace xlle {

// A RAM-backed MMIO register file: reads return the last written value (reset 0). This is
// xemu's default behavior for several MCPX devices' register space and serves as the base
// for shells whose modelled registers are special-cased in subclasses.
class RegFileMmio : public MmioDevice {
public:
    RegFileMmio(const char* name, uint32_t base, uint32_t size, FaultFn fault)
        : name_(name), base_(base), size_(size), fault_(std::move(fault)),
          regs_(size, 0) {}

    const char* name() const override { return name_; }
    uint32_t mmio_base() const override { return base_; }
    uint32_t mmio_size() const override { return size_; }

    uint32_t mmio_read(uint32_t addr, int size) override {
        uint32_t off = addr - base_;
        uint32_t v = 0;
        for (int i = 0; i < size && off + i < size_; ++i)
            v |= static_cast<uint32_t>(regs_[off + i]) << (8 * i);
        return v;
    }
    void mmio_write(uint32_t addr, int size, uint32_t value) override {
        uint32_t off = addr - base_;
        for (int i = 0; i < size && off + i < size_; ++i)
            regs_[off + i] = static_cast<uint8_t>(value >> (8 * i));
    }

protected:
    uint32_t reg32(uint32_t off) const {
        return regs_[off] | (regs_[off + 1] << 8) | (regs_[off + 2] << 16) |
               (static_cast<uint32_t>(regs_[off + 3]) << 24);
    }
    void set_reg32(uint32_t off, uint32_t v) {
        regs_[off] = static_cast<uint8_t>(v);
        regs_[off + 1] = static_cast<uint8_t>(v >> 8);
        regs_[off + 2] = static_cast<uint8_t>(v >> 16);
        regs_[off + 3] = static_cast<uint8_t>(v >> 24);
    }

    const char* name_;
    uint32_t base_, size_;
    FaultFn fault_;
    std::vector<uint8_t> regs_;
};

class McpxApu : public RegFileMmio {
public:
    static constexpr uint32_t kBase = 0xFE800000u;
    static constexpr uint32_t kSize = 0x80000u;  // 512 KB BAR0 (xemu mcpx_apu_realize)

    explicit McpxApu(FaultFn fault) : RegFileMmio("mcpx-apu", kBase, kSize, std::move(fault)) {}

    // Guest-physical store for the FEMEMDATA magic write.
    void set_mem_write(std::function<bool(uint32_t, const void*, size_t)> wr) {
        mem_write_ = std::move(wr);
    }

    uint32_t mmio_read(uint32_t addr, int size) override {
        uint32_t off = addr - base_;
        if (off == kXgscnt) {  // free-running counter (xemu: virtual-clock ns / 100)
            set_reg32(kXgscnt, static_cast<uint32_t>(xgscnt_ += 0x400));
        }
        // VP NV1BA0_PIO_FREE (xemu vp_read): PIO FIFO free space — the queue is not
        // simulated, so it always reads "empty" (0x80). The kernel polls this before
        // submitting voice methods and spins forever on 0.
        if (off == 0x20000 + 0x10) return 0x80;
        log_subregion("rd", off);
        return RegFileMmio::mmio_read(addr, size);
    }

    void mmio_write(uint32_t addr, int size, uint32_t value) override {
        uint32_t off = addr - base_;
        log_subregion("wr", off);
        if (off == kIsts && size == 4) {  // write-1-clear interrupt status
            set_reg32(kIsts, reg32(kIsts) & ~value);
            return;
        }
        RegFileMmio::mmio_write(addr, size, value);
        if (off == kFememdata && size == 4) {  // magic write-through (xemu apu.c)
            uint32_t dst = reg32(kFememaddr);
            if (mem_write_) mem_write_(dst, &value, 4);
        }
    }

private:
    static constexpr uint32_t kIsts = 0x1000, kFememaddr = 0x1324, kFememdata = 0x1334;
    static constexpr uint32_t kXgscnt = 0x200C;

    void log_subregion(const char* what, uint32_t off) {
        // The VP/GP/EP DSP blocks have their own semantics in xemu — log boot accesses so a
        // load-bearing one gets characterized instead of silently register-filed.
        if (off >= 0x20000 && sub_logged_ < 16) {
            std::printf("[apu] %s DSP subregion off=0x%05x — register-file default\n", what, off);
            if (++sub_logged_ == 16) std::printf("[apu] (subregion log capped)\n");
        }
    }

    std::function<bool(uint32_t, const void*, size_t)> mem_write_;
    uint64_t xgscnt_ = 0;
    int sub_logged_ = 0;
};

}  // namespace xlle
