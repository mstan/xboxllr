// xlle — PCI configuration space (0xCF8/0xCFC mechanism) + the Xbox bus-0 device table (A2c).
//
// Topology and identities harvested from xemu/hw/xbox/xbox_pci.c + xbox.c (verified against
// source). Config space is a faithful 256-byte-per-function model: reads return reset values,
// an absent function returns 0xFFFFFFFF (the architectural signal enumeration relies on).
// Reset values ARE hardware behavior, so config reads are not fault-stopped; a write with a
// modeled side effect (e.g. MCPX ROM-overlay disable at LPC offset 0x80) is handled explicitly.
#pragma once
#include <cstdint>
#include <cstring>
#include <vector>

#include "machine/io_device.hpp"

namespace xlle {

struct PciFunction {
    uint8_t bus = 0, dev = 0, fn = 0;
    const char* name = "";
    uint8_t cfg[256] = {0};

    void set16(uint8_t off, uint16_t v) { cfg[off] = v & 0xff; cfg[off + 1] = v >> 8; }
    void set32(uint8_t off, uint32_t v) {
        cfg[off] = v & 0xff; cfg[off + 1] = (v >> 8) & 0xff;
        cfg[off + 2] = (v >> 16) & 0xff; cfg[off + 3] = (v >> 24) & 0xff;
    }
    uint32_t get32(uint8_t off) const {
        return cfg[off] | (cfg[off + 1] << 8) | (cfg[off + 2] << 16) |
               (static_cast<uint32_t>(cfg[off + 3]) << 24);
    }
};

// PCI host bridge: CONFIG_ADDRESS at 0xCF8 (dword latch, bit31=enable), CONFIG_DATA at 0xCFC.
// Occupies the contiguous 8-port range 0xCF8..0xCFF.
class PciHost : public IoPortDevice {
public:
    explicit PciHost(FaultFn fault);

    const char* name() const override { return "pci-host"; }
    uint16_t io_base() const override { return 0xCF8; }
    uint16_t io_size() const override { return 8; }
    uint32_t io_read(uint16_t port, int size) override;
    void io_write(uint16_t port, int size, uint32_t value) override;

    // Was the MCPX boot-ROM overlay disabled by firmware (LPC cfg 0x80 bit1)? Observability.
    bool mcpx_rom_disabled() const { return mcpx_rom_disabled_; }

private:
    PciFunction* find(uint8_t bus, uint8_t dev, uint8_t fn);

    FaultFn fault_;
    uint32_t config_addr_ = 0;
    bool mcpx_rom_disabled_ = false;
    std::vector<PciFunction> funcs_;
};

}  // namespace xlle
