// xlle — memory-mapped I/O device interface (A2c/A4).
//
// Same doctrine as IoPortDevice (io_device.hpp): an unmodeled register faults the machine
// loudly via FaultFn rather than returning a plausible default (PRINCIPLES §3).
#pragma once
#include <cstdint>

#include "machine/io_device.hpp"  // FaultFn

namespace xlle {

// A device occupying a contiguous MMIO region [base, base+size). read/write receive the
// absolute guest-physical address and access width in bytes (1/2/4).
class MmioDevice {
public:
    virtual ~MmioDevice() = default;
    virtual const char* name() const = 0;
    virtual uint32_t mmio_base() const = 0;
    virtual uint32_t mmio_size() const = 0;
    virtual uint32_t mmio_read(uint32_t addr, int size) = 0;
    virtual void mmio_write(uint32_t addr, int size, uint32_t value) = 0;
};

}  // namespace xlle
