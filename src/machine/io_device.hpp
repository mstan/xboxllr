// xlle — device model interfaces for the guest I/O fabric (A2c platform init).
//
// Doctrine (PRINCIPLES §3): a device that is asked for something it does not yet model does
// NOT invent a plausible default — it calls its FaultFn, which halts the machine loudly with
// the exact context, so the gap is characterized (against the hardware oracle) and modelled.
// A *bus-level* error the firmware is designed to poll for (absent slave, NAK) is different:
// that is a real, faithful state reported through the device's status registers, not a fault.
#pragma once
#include <cstdint>
#include <functional>
#include <string>

namespace xlle {

// Called by a device to request a loud fault-stop with a human-readable reason.
using FaultFn = std::function<void(const std::string&)>;

// A device occupying a contiguous I/O-port range [base, base+size). `read`/`write` receive the
// absolute port and the access width in bytes (1/2/4).
class IoPortDevice {
public:
    virtual ~IoPortDevice() = default;
    virtual const char* name() const = 0;
    virtual uint16_t io_base() const = 0;
    virtual uint16_t io_size() const = 0;
    virtual uint32_t io_read(uint16_t port, int size) = 0;
    virtual void io_write(uint16_t port, int size, uint32_t value) = 0;

    // Does this device answer `port`? Defaults to the [io_base, io_base+io_size) window; a
    // device spanning several disjoint ranges (e.g. the dual PIC's master/slave/ELCR ports)
    // overrides this to claim all of them.
    virtual bool owns(uint16_t port) const {
        return port >= io_base() && port < static_cast<uint32_t>(io_base()) + io_size();
    }
};

}  // namespace xlle
