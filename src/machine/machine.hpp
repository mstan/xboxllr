// xlle — XboxMachine: the guest memory map (and, later, devices) around the CPU floor.
// A2 foundation. Layout: 64 MB unified RAM at 0x0; the flash BIOS mirrored across
// 0xFF000000-0xFFFFFFFF (256KB image, 64 mirrors in the 16MB window).
#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "machine/io_device.hpp"
#include "machine/mmio_device.hpp"
#include "machine/nv2a.hpp"
#include "machine/pci.hpp"
#include "machine/pic.hpp"
#include "machine/pit.hpp"
#include "machine/pm.hpp"
#include "machine/smbus.hpp"
#include "xlle/cpu_floor.hpp"

namespace xlle {

class XboxMachine {
public:
    static constexpr uint32_t kRamSize     = 64u * 1024 * 1024;  // 0x0400_0000
    static constexpr uint32_t kFlashBase    = 0xFF000000u;
    static constexpr uint32_t kFlashWindow  = 0x01000000u;       // 16MB, ends at 0xFFFFFFFF

    XboxMachine();
    bool ok() const;

    bool init_ram();                              // map 64 MB at 0x0
    bool load_flash(const std::string& path);     // map + mirror the flash image into the window
    bool load_blob(uint32_t addr, const std::string& path);  // load a raw blob into mapped RAM
    void init_devices();  // build the I/O fabric (PCI host, SMBus+SMC) and wire dispatch

    bool read(uint32_t addr, void* buf, size_t n);
    uint64_t run(uint32_t start_eip, uint64_t max_insns, std::string& err);

    CpuFloor& cpu() { return *cpu_; }
    size_t flash_size() const { return flash_size_; }

    // Observability: fired for every serviced I/O-port access (see IoEvent). Installed once.
    void on_io_event(std::function<void(const IoEvent&)> cb) { io_event_cb_ = std::move(cb); }
    // Observability: fired for every serviced MMIO access (MemEvent-shaped; write flag reused).
    void on_mmio_event(std::function<void(const MemEvent&)> cb) { mmio_event_cb_ = std::move(cb); }
    PciHost* pci() { return pci_; }
    uint64_t injected_interrupts() const { return injected_; }
    uint64_t irq0_pulses() const { return irq0_pulses_; }
    uint64_t pending_boundaries() const { return pending_boundaries_; }
    DualPic* pic_dbg() { return pic_; }

private:
    uint32_t io_in(uint16_t port, int size);
    void io_out(uint16_t port, int size, uint32_t value);
    IoPortDevice* find_io(uint16_t port);

    uint32_t mmio_read(uint32_t addr, int size);
    void mmio_write(uint32_t addr, int size, uint32_t value);
    MmioDevice* find_mmio(uint32_t addr);

    std::unique_ptr<CpuFloor> cpu_;
    std::vector<uint8_t> ram_;  // 64 MB backing, mapped at 0x0 and aliased at 0x04000000
    size_t flash_size_ = 0;
    bool ram_ok_ = false, flash_ok_ = false;

    std::vector<std::unique_ptr<IoPortDevice>> io_devices_;  // owns PCI host, SMBus, PIC, PIT...
    std::vector<std::unique_ptr<MmioDevice>> mmio_devices_;  // owns NV2A, ...
    PciHost* pci_ = nullptr;                                 // non-owning convenience pointers
    DualPic* pic_ = nullptr;
    Pit8254* pit_ = nullptr;
    std::function<void(const IoEvent&)> io_event_cb_;
    std::function<void(const MemEvent&)> mmio_event_cb_;
    uint64_t io_seq_ = 0;
    uint64_t mmio_seq_ = 0;
    uint64_t total_insns_ = 0;  // retired-instruction clock feeding the PIT time base
    uint64_t injected_ = 0;     // external interrupts actually delivered to the guest
    uint64_t irq0_pulses_ = 0;  // PIT channel-0 period boundaries reached
    uint64_t pending_boundaries_ = 0;  // chunk boundaries with an unmasked IRQ pending

    // Guest CPU clock estimate (Hz) mapping retired instructions -> PIT ticks. The Xbox CPU is
    // 733.33 MHz; combined with ~1 IPC this sets the tick cadence (calibratable).
    static constexpr uint64_t kCpuHz = 733333333ull;
};

}  // namespace xlle
