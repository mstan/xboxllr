// xlle — XboxMachine implementation.
#include "machine/machine.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <vector>

#include "cpu/unicorn_floor.hpp"
#include "machine/aci.hpp"
#include "machine/apu.hpp"
#include "machine/open_bus.hpp"
#include "machine/perf_accounting.hpp"
#include "machine/port61.hpp"
#include "machine/rtc.hpp"

namespace xlle {

XboxMachine::XboxMachine() : XboxMachine(Config{}) {}

XboxMachine::XboxMachine(Config config)
    : cpu_(std::make_unique<UnicornFloor>()), config_(config) {}

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
    smbus->add_slave(std::make_unique<Cx25871>());  // TV encoder (board is Conexant, silicon)
    Amd756Smbus* smbus_raw = smbus.get();
    smbus_ = smbus_raw;
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
    // Extended-CMOS index/data ports (0x72/0x73): not decoded by the MCPX RTC. The kernel
    // pokes them during shutdown/century handling; xemu leaves them unmapped ISA space and
    // boots this same retail kernel (writes discarded, reads float) — open bus, not a fault.
    io_devices_.push_back(std::make_unique<OpenBus>(0x0072, 2, "cmos-ext (absent)"));

    // IDE: primary channel (HDD master, ATAPI DVD slave) + bus master at BAR4 (0xFF60,
    // silicon). Completion IRQ 14, level-triggered, same delivery shape as SMBus IRQ 11.
    auto ide = std::make_unique<IdeController>(fault);
    ide_ = ide.get();
    ide_->set_irq([this](int irq, bool level) { pic_->raise(irq, level); }, 14);
    ide_->set_dma_mem(
        [this](uint32_t addr, void* buf, size_t n) { return cpu_->read_mem(addr, buf, n); },
        [this](uint32_t addr, const void* buf, size_t n) {
            return cpu_->write_mem(addr, buf, n);
        });
    io_devices_.push_back(std::move(ide));
    // The bus-master decode follows BAR4 if the kernel reprograms it. A BAR sizing probe
    // (all-ones write) is unmodeled in our config space — stop loudly so it gets modelled
    // instead of silently corrupting the stored base.
    pci_->on_cfg_write([this](uint8_t bus, uint8_t dev, uint8_t fn, uint8_t reg, int size,
                              uint32_t value) {
        if (bus != 0 || dev != 9 || fn != 0) return;
        if (reg <= 0x20 && reg + size > 0x20) {
            if (value == 0xFFFFFFFFu) {
                cpu_->request_stop("IDE BAR4 sizing write (0xFFFFFFFF) — BAR sizing unmodeled");
                return;
            }
            ide_->set_bm_base(static_cast<uint16_t>(value & 0xFFF0));
        }
    });

    // Route port I/O through the fabric.
    cpu_->on_io([this](uint16_t port, int size) { return io_in(port, size); },
                [this](uint16_t port, int size, uint32_t v) { io_out(port, size, v); });

    // MMIO devices. NV2A occupies 0xFD000000 (16 MB). Register the region with the CPU floor
    // so accesses route to the device model instead of faulting as unmapped memory.
    auto nv2a = std::make_unique<Nv2a>(fault);
    nv2a_ = nv2a.get();
    nv2a_->set_mem_access(
        [this](uint32_t addr, void* buf, size_t n) { return cpu_->read_mem(addr, buf, n); },
        [this](uint32_t addr, const void* buf, size_t n) {
            return cpu_->write_mem(addr, buf, n);
        });
    // GPU interrupt: Xbox routes the NV2A to ISA IRQ 3 (kernel unmasks it for VBLANK pacing).
    nv2a_->set_irq([this](int irq, bool level) { pic_->raise(irq, level); }, 3);
    uint32_t nbase = nv2a->mmio_base();
    uint32_t nsize = nv2a->mmio_size();
    mmio_devices_.push_back(std::move(nv2a));
    cpu_->map_mmio(nbase, nsize,
                   [this](uint32_t a, int s) { return mmio_read(a, s); },
                   [this](uint32_t a, int s, uint32_t v) { mmio_write(a, s, v); });

    // MCPX APU at 0xFE800000 (512 KB), including timed voice-idle completion.
    auto apu = std::make_unique<McpxApu>(fault);
    apu_ = apu.get();
    apu_->set_mem_access(
        [this](uint32_t addr, void* buf, size_t n) { return cpu_->read_mem(addr, buf, n); },
        [this](uint32_t addr, const void* buf, size_t n) {
            return cpu_->write_mem(addr, buf, n);
        });
    // MCPX PCI interrupt routing register 0x3c maps APU INTA to ISA IRQ 5.
    apu_->set_irq([this](int irq, bool level) { pic_->raise(irq, level); }, 5);
    uint32_t abase = apu->mmio_base();
    uint32_t asize = apu->mmio_size();
    mmio_devices_.push_back(std::move(apu));
    cpu_->map_mmio(abase, asize,
                   [this](uint32_t a, int s) { return mmio_read(a, s); },
                   [this](uint32_t a, int s, uint32_t v) { mmio_write(a, s, v); });

    // MCPX integrated Ethernet controller at 0xFEF00000. The kernel uses PCI INTA routed
    // to ISA IRQ4; MDIO is synchronous and the integrated PHY resides at address 1.
    auto nvnet = std::make_unique<NvNet>(fault);
    nvnet_ = nvnet.get();
    nvnet_->set_mem_access(
        [this](uint32_t addr, void* buf, size_t n) { return cpu_->read_mem(addr, buf, n); },
        [this](uint32_t addr, const void* buf, size_t n) {
            return cpu_->write_mem(addr, buf, n);
        });
    nvnet_->set_irq([this](int irq, bool level) { pic_->raise(irq, level); }, 4);
    mmio_devices_.push_back(std::move(nvnet));
    cpu_->map_mmio(NvNet::kBase, NvNet::kSize,
                   [this](uint32_t a, int s) { return mmio_read(a, s); },
                   [this](uint32_t a, int s, uint32_t v) { mmio_write(a, s, v); });

    // MCPX ACI (AC97) at 0xFEC00000, including headless PCM-out descriptor timing.
    auto aci = std::make_unique<McpxAci>(fault);
    aci_ = aci.get();
    aci_->set_mem_access(
        [this](uint32_t addr, void* buf, size_t n) { return cpu_->read_mem(addr, buf, n); });
    // LPC routing register 0x6e maps device 6 (ACI) INTA to ISA IRQ 6.
    aci_->set_irq([this](int irq, bool level) { pic_->raise(irq, level); }, 6);
    uint32_t cbase = aci->mmio_base();
    uint32_t csize = aci->mmio_size();
    mmio_devices_.push_back(std::move(aci));
    cpu_->map_mmio(cbase, csize,
                   [this](uint32_t a, int s) { return mmio_read(a, s); },
                   [this](uint32_t a, int s, uint32_t v) { mmio_write(a, s, v); });

    // Two standard four-port OHCI controllers. USB0 routes to IRQ1 and carries the
    // deterministic Controller S; USB1 routes to IRQ9 and has no downstream device.
    for (int index = 0; index < 2; ++index) {
        const uint32_t base = index ? 0xFED08000u : 0xFED00000u;
        const Ohci::UsbTopology topology =
            index == 0 ? config_.ohci0_topology : Ohci::UsbTopology::kDirectXid;
        auto ohci = std::make_unique<Ohci>(base, fault, topology);
        Ohci* raw = ohci.get();
        raw->set_mem_access(
            [this](uint32_t addr, void* buf, size_t n) {
                return cpu_->read_mem(addr, buf, n);
            },
            [this](uint32_t addr, const void* buf, size_t n) {
                return cpu_->write_mem(addr, buf, n);
            });
        raw->set_irq([this](int irq, bool level) { pic_->raise(irq, level); },
                     index ? 9 : 1);
        if (index) ohci1_ = raw;
        else ohci0_ = raw;
        mmio_devices_.push_back(std::move(ohci));
        cpu_->map_mmio(base, Ohci::kSize,
                       [this](uint32_t a, int s) { return mmio_read(a, s); },
                       [this](uint32_t a, int s, uint32_t v) { mmio_write(a, s, v); });
    }
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

bool XboxMachine::attach_hdd(const std::string& path) {
    return ide_ && ide_->attach_hdd(path);
}

bool XboxMachine::attach_eeprom(const std::string& path) {
    if (!smbus_) return false;
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)),
                              std::istreambuf_iterator<char>());
    if (data.size() != 256) return false;  // 24C02: exactly 256 bytes
    smbus_->add_slave(std::make_unique<XboxEeprom>(std::move(data)));
    return true;
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
        {
            perf::ScopedAdd perf_t(perf::g_ns_host_poll_devices, perf::g_calls_host_poll_devices);
            const uint64_t guest_millisecond = total_insns_ * 1000 / kCpuHz;
            if (host_poll_cb_ && guest_millisecond != host_poll_millisecond_) {
                host_poll_millisecond_ = guest_millisecond;
                host_poll_cb_();
            }
            // Arm the floor to stop as soon as IF opens if an IRQ is already waiting (so we hit
            // the guest's brief sti window). Otherwise the chunk runs to completion.
            if (pic_ && pic_->intr_pending()) pending_boundaries_++;
            cpu_->set_irq_pending(pic_ && pic_->intr_pending());
        }
        uint64_t chunk = std::min(kChunk, max - done);
        std::string cerr;
        uint64_t n = cpu_->run(eip, chunk, cerr);
        done += n;
        total_insns_ += n;

        {
            perf::ScopedAdd perf_t(perf::g_ns_host_poll_devices, perf::g_calls_host_poll_devices);
            if (pit_) pit_->set_time(total_insns_ * Pit8254::kPitFreq / kCpuHz);
            // Frame clock: ~60 Hz VBLANK derived from the same retired-instruction time base,
            // plus the sub-frame phase for the VGA retrace status bit.
            if (nv2a_) {
                uint64_t t60 = total_insns_ * 60;
                nv2a_->set_frame(t60 / kCpuHz);
                nv2a_->set_frame_phase(static_cast<uint32_t>((t60 % kCpuHz) * 1000 / kCpuHz));
            }
            // APU setup-engine frame: 256 samples / 48 kHz = 5.333 ms (187.5 Hz).
            if (apu_) apu_->set_frame(total_insns_ * 375 / (kCpuHz * 2));
            // AC'97 PCM output consumes the same 256-sample frames, without host audio output.
            if (aci_) aci_->set_frame(total_insns_ * 375 / (kCpuHz * 2));
            // USB OHCI start-of-frame cadence is 1 kHz.
            if (ohci0_) ohci0_->set_frame(total_insns_ * 1000 / kCpuHz);
            if (ohci1_) ohci1_->set_frame(total_insns_ * 1000 / kCpuHz);
        }
        if (!cerr.empty()) { err = cerr; break; }  // device fault-stop wins

        // Deliver one pending, unmasked external interrupt at this boundary. Confirm the guest
        // can take it (inject succeeds) BEFORE committing the PIC's ISR/ack bookkeeping — an
        // ack without delivery would wedge the line in-service.
        {
            perf::ScopedAdd perf_t(perf::g_ns_host_poll_devices, perf::g_calls_host_poll_devices);
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
        }
        eip = cpu_->get_regs().eip;
    }
    cpu_->set_irq_pending(false);
    return done;
}

}  // namespace xlle
