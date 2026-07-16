// xlle — XboxMachine implementation.
#include "machine/machine.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <vector>

#include "cpu/unicorn_floor.hpp"

namespace xlle {

XboxMachine::XboxMachine() : cpu_(std::make_unique<UnicornFloor>()) {}

bool XboxMachine::ok() const { return static_cast<UnicornFloor*>(cpu_.get())->ok(); }

bool XboxMachine::init_ram() {
    ram_ok_ = cpu_->map_mem(0x0, kRamSize);
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
    io_devices_.push_back(std::move(smbus));

    // ACPI/PM controller behind the LPC I/O BAR at 0x8000 (kernel-init timing, GPIO, GPE).
    io_devices_.push_back(std::make_unique<XboxPm>(fault));

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
    for (auto& d : io_devices_) {
        uint16_t base = d->io_base();
        if (port >= base && port < static_cast<uint32_t>(base) + d->io_size()) return d.get();
    }
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

uint64_t XboxMachine::run(uint32_t s, uint64_t m, std::string& e) { return cpu_->run(s, m, e); }

}  // namespace xlle
