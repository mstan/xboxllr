// xlle — XboxMachine implementation.
#include "machine/machine.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <vector>

#include "cpu/unicorn_floor.hpp"
#include "machine/open_bus.hpp"
#include "machine/port61.hpp"
#include "machine/rtc.hpp"

namespace xlle {

XboxMachine::XboxMachine() : cpu_(std::make_unique<UnicornFloor>()) {}

bool XboxMachine::ok() const { return static_cast<UnicornFloor*>(cpu_.get())->ok(); }

bool XboxMachine::init_ram() {
    ram_.assign(kRamSize, 0);
    // Map the 64 MB backing at 0x0 and ALIAS it at 0x04000000-0x07FFFFFF: the 64 MB Xbox does
    // not decode address bit 26, so the upper half mirrors RAM. The kernel's memory-size probe
    // depends on this wraparound to detect 64 MB (without it, it detects 128 MB and walks the
    // PFN database off the end of RAM).
    ram_ok_ = cpu_->map_mem_ptr(0x0, kRamSize, ram_.data()) &&
              cpu_->map_mem_ptr(kRamSize, kRamSize, ram_.data());
    return ram_ok_;
}

bool XboxMachine::load_flash(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)),
                              std::istreambuf_iterator<char>());
    if (data.empty()) return false;
    flash_size_ = data.size();

    if (!cpu_->map_mem(kFlashBase, kFlashWindow)) return false;
    // Mirror the image across the whole 16 MB window (256KB image -> 64 copies),
    // matching how the real flash appears mirrored at the top of the address space.
    for (uint32_t off = 0; off < kFlashWindow; off += static_cast<uint32_t>(data.size())) {
        uint32_t n = std::min<uint32_t>(static_cast<uint32_t>(data.size()), kFlashWindow - off);
        if (!cpu_->write_mem(kFlashBase + off, data.data(), n)) return false;
    }
    flash_ok_ = true;
    return true;
}

bool XboxMachine::load_blob(uint32_t addr, const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)),
                              std::istreambuf_iterator<char>());
    if (data.empty()) return false;
    return cpu_->write_mem(addr, data.data(), data.size());
}

void XboxMachine::init_devices() {
    // A device faults the machine by asking the CPU floor for a loud stop (PRINCIPLES §3).
    FaultFn fault = [this](const std::string& why) { cpu_->request_stop(why); };

    auto pci = std::make_unique<PciHost>(fault);
    pci_ = pci.get();
    io_devices_.push_back(std::move(pci));

    auto smbus = std::make_unique<Amd756Smbus>(0xC000, fault);
    smbus->add_slave(std::make_unique<XboxSmc>(fault));
    Amd756Smbus* smbus_raw = smbus.get();
    io_devices_.push_back(std::move(smbus));

    // ACPI/PM controller behind the LPC I/O BAR at 0x8000 (kernel-init timing, GPIO, GPE).
    io_devices_.push_back(std::make_unique<XboxPm>(fault));

    // LPC47M157 SuperIO ports: PRESENT ON DEVKITS ONLY. On the retail console these float —
    // silicon (agent) confirms 0x2e/0x2f/0x4e read 0xFF. The kernel's HAL probes for the chip
    // and must see "absent" (open bus), not a fault. (xemu's retail machine likewise omits it.)
    io_devices_.push_back(std::make_unique<OpenBus>(0x002E, 2, "superio-2e (absent)"));
    io_devices_.push_back(std::make_unique<OpenBus>(0x004E, 2, "superio-4e (absent)"));

    // Interrupt/timer stack: PIC (master/slave/ELCR), PIT (system tick -> IRQ0), port 0x61.
    auto pic = std::make_unique<DualPic>();
    pic_ = pic.get();
    io_devices_.push_back(std::move(pic));
    // Route the SMBus completion interrupt (IRQ 11, silicon LPC cfg 0x64) to the PIC now that
    // it exists — this unblocks the kernel's interrupt-driven HAL SMBus reads.
    smbus_raw->set_irq([this](int irq, bool level) { pic_->raise(irq, level); }, 11);

    auto pit = std::make_unique<Pit8254>();
    pit_ = pit.get();
    // Channel 0's period boundary pulses IRQ0 (a rising then falling edge on PIC line 0).
    pit_->set_irq0([this]() { irq0_pulses_++; pic_->raise(0, true); pic_->raise(0, false); });
    io_devices_.push_back(std::move(pit));

    io_devices_.push_back(std::make_unique<Port61>());
    io_devices_.push_back(std::make_unique<Mc146818Rtc>());

    // Route port I/O through the fabric.
    cpu_->on_io([this](uint16_t port, int size) { return io_in(port, size); },
                [this](uint16_t port, int size, uint32_t v) { io_out(port, size, v); });

    // MMIO devices. NV2A occupies 0xFD000000 (16 MB). Register the region with the CPU floor
    // so accesses route to the device model instead of faulting as unmapped memory.
    auto nv2a = std::make_unique<Nv2a>(fault);
    for (auto& d : mmio_devices_) (void)d;  // (kept explicit; list may grow with more devices)
    uint32_t nbase = nv2a->mmio_base();
    uint32_t nsize = nv2a->mmio_size();
    mmio_devices_.push_back(std::move(nv2a));
    cpu_->map_mmio(nbase, nsize,
                   [this](uint32_t a, int s) { return mmio_read(a, s); },
                   [this](uint32_t a, int s, uint32_t v) { mmio_write(a, s, v); });
}

MmioDevice* XboxMachine::find_mmio(uint32_t addr) {
    for (auto& d : mmio_devices_) {
        uint32_t base = d->mmio_base();
        if (addr >= base && addr < base + d->mmio_size()) return d.get();
    }
    return nullptr;
}

uint32_t XboxMachine::mmio_read(uint32_t addr, int size) {
    MmioDevice* d = find_mmio(addr);
    uint32_t val = 0;
    if (d) {
        val = d->mmio_read(addr, size);
    } else {
        char b[96];
        std::snprintf(b, sizeof(b), "unhandled MMIO read addr=0x%08x size=%d — no device", addr,
                      size);
        cpu_->request_stop(b);
    }
    if (mmio_event_cb_)
        mmio_event_cb_(MemEvent{mmio_seq_++, 0, addr, static_cast<uint8_t>(size), val});
    return val;
}

void XboxMachine::mmio_write(uint32_t addr, int size, uint32_t value) {
    MmioDevice* d = find_mmio(addr);
    if (d) {
        d->mmio_write(addr, size, value);
    } else {
        char b[96];
        std::snprintf(b, sizeof(b), "unhandled MMIO write addr=0x%08x size=%d value=0x%08x — "
                      "no device", addr, size, value);
        cpu_->request_stop(b);
    }
    if (mmio_event_cb_)
        mmio_event_cb_(MemEvent{mmio_seq_++, 1, addr, static_cast<uint8_t>(size), value});
}

IoPortDevice* XboxMachine::find_io(uint16_t port) {
    for (auto& d : io_devices_)
        if (d->owns(port)) return d.get();
    return nullptr;
}

uint32_t XboxMachine::io_in(uint16_t port, int size) {
    IoPortDevice* d = find_io(port);
    uint32_t val = 0;
    if (d) {
        val = d->io_read(port, size);
    } else {
        char b[96];
        std::snprintf(b, sizeof(b), "unhandled I/O IN port=0x%04x size=%d — no device", port,
                      size);
        cpu_->request_stop(b);
    }
    if (io_event_cb_)
        io_event_cb_(IoEvent{io_seq_++, 0, port, static_cast<uint8_t>(size), val});
    return val;
}

void XboxMachine::io_out(uint16_t port, int size, uint32_t value) {
    IoPortDevice* d = find_io(port);
    if (d) {
        d->io_write(port, size, value);
    } else {
        char b[96];
        std::snprintf(b, sizeof(b), "unhandled I/O OUT port=0x%04x size=%d value=0x%08x — "
                      "no device", port, size, value);
        cpu_->request_stop(b);
    }
    if (io_event_cb_)
        io_event_cb_(IoEvent{io_seq_++, 1, port, static_cast<uint8_t>(size), value});
}

bool XboxMachine::read(uint32_t addr, void* buf, size_t n) { return cpu_->read_mem(addr, buf, n); }

uint64_t XboxMachine::run(uint32_t start, uint64_t max, std::string& err) {
    // Execute in short chunks so the interrupt controller can deliver external IRQs at clean
    // instruction boundaries between chunks (Unicorn has no native IRQ path — see the CPU
    // floor's inject_interrupt). The PIT clock advances with retired instructions.
    constexpr uint64_t kChunk = 4096;
    uint32_t eip = start;
    uint64_t done = 0;
    while (done < max) {
        uint64_t chunk = std::min(kChunk, max - done);
        // Arm the floor to stop as soon as IF opens if an IRQ is already waiting (so we hit the
        // guest's brief sti window). Otherwise the chunk runs to completion.
        if (pic_ && pic_->intr_pending()) pending_boundaries_++;
        cpu_->set_irq_pending(pic_ && pic_->intr_pending());
        std::string cerr;
        uint64_t n = cpu_->run(eip, chunk, cerr);
        done += n;
        total_insns_ += n;

        if (pit_) pit_->set_time(total_insns_ * Pit8254::kPitFreq / kCpuHz);
        if (!cerr.empty()) { err = cerr; break; }  // device fault-stop wins

        // Deliver one pending, unmasked external interrupt at this boundary. Confirm the guest
        // can take it (inject succeeds) BEFORE committing the PIC's ISR/ack bookkeeping — an
        // ack without delivery would wedge the line in-service.
        if (pic_ && pic_->intr_pending() && cpu_->interrupts_enabled()) {
            int vec = pic_->peek();
            if (vec >= 0) {
                std::string ierr;
                if (cpu_->inject_interrupt(static_cast<uint8_t>(vec), ierr)) {
                    pic_->acknowledge();
                    injected_++;
                } else if (!ierr.empty()) { err = ierr; break; }
            }
        }
        eip = cpu_->get_regs().eip;
    }
    cpu_->set_irq_pending(false);
    return done;
}

}  // namespace xlle
