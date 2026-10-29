// xlle — XboxMachine implementation.
#include "machine/machine.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <optional>
#include <vector>

#include "cpu/unicorn_floor.hpp"
#include "machine/aci.hpp"
#include "machine/apu.hpp"
#include "machine/open_bus.hpp"
#include "machine/perf_accounting.hpp"
#include "machine/perf_excl.hpp"
#include "machine/port61.hpp"
#include "machine/rtc.hpp"
#ifdef XLLE_MACHINE_DSP
#include "machine/apu_device_clock.hpp"
#include "machine_ep_y.aot-private.h"
#endif

namespace xlle {

struct XboxMachine::AudioDspOwner {
#ifdef XLLE_MACHINE_DSP
    static constexpr uint64_t kClockHz=1125000;
    ApuGpProcessor gp;
    ApuEpProcessor ep;
    ApuDeviceClock clock;
    AudioDspOwner(McpxApu& apu,McpxAci& aci,FaultFn fault)
        :gp(apu,fault),ep(apu,fault),clock(apu,gp,ep,aci,0,kClockHz,std::move(fault)) {}
#endif
};

XboxMachine::~XboxMachine() = default;

bool XboxMachine::service_audio_time(uint64_t instruction_clock) {
#ifdef XLLE_MACHINE_DSP
    if(audio_dsp_) {
        // Explicit nominal device profile shared with the original-audio
        // reference. The machine's CPU/PIT clock keeps its existing units.
        const uint64_t ticks=(instruction_clock/kCpuHz)*AudioDspOwner::kClockHz+
            (instruction_clock%kCpuHz)*AudioDspOwner::kClockHz/kCpuHz;
        if(audio_dsp_->clock.advance(ticks))return true;
        const auto& gp=audio_dsp_->clock.last(false);
        const auto& ep=audio_dsp_->clock.last(true);
        std::fprintf(stderr,"[audio] failed at tick=%llu CPU=%llu GP phase=%u boot=%llu pc=%06x cycles=%u EP phase=%u boot=%llu pc=%06x cycles=%u\n",
            static_cast<unsigned long long>(audio_dsp_->clock.ticks()),
            static_cast<unsigned long long>(instruction_clock),unsigned(audio_dsp_->gp.phase()),
            static_cast<unsigned long long>(audio_dsp_->gp.bootstraps()),gp.pc,gp.cycles,
            unsigned(audio_dsp_->ep.phase()),static_cast<unsigned long long>(audio_dsp_->ep.bootstraps()),ep.pc,ep.cycles);
        return false;
    }
#else
    (void)instruction_clock;
#endif
    return true;
}

XboxMachine::XboxMachine() : XboxMachine(Config{}) {}

XboxMachine::XboxMachine(Config config)
    : cpu_(std::make_unique<UnicornFloor>()),
      config_(config),
      cpu_chunk_insns_(std::max<uint64_t>(1, config.cpu_chunk_insns)) {}

bool XboxMachine::ok() const { return static_cast<UnicornFloor*>(cpu_.get())->ok(); }

bool XboxMachine::register_aot_region(const xlle_aot_region& region) {
    return static_cast<UnicornFloor*>(cpu_.get())->register_aot_region(region);
}
bool XboxMachine::register_aot_replacement(const AotReplacement& replacement) {
    return static_cast<UnicornFloor*>(cpu_.get())->register_aot_replacement(replacement);
}
void XboxMachine::set_aot_profile(uint32_t profile) {
    static_cast<UnicornFloor*>(cpu_.get())->set_aot_profile(profile);
}
AotExecutionStats XboxMachine::aot_stats() const {
    return static_cast<UnicornFloor*>(cpu_.get())->aot_stats();
}
std::vector<AotRegionStats> XboxMachine::aot_region_stats() const {
    return static_cast<UnicornFloor*>(cpu_.get())->aot_region_stats();
}

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
    FaultFn fault = [this](const std::string& why) {
        if(device_fault_.empty()) {
            device_fault_=why;
            cpu_->request_stop(why);
        }
    };

    auto pci = std::make_unique<PciHost>(fault);
    pci_ = pci.get();
    io_devices_.push_back(std::move(pci));

    auto smbus = std::make_unique<Amd756Smbus>(0xC000, fault);
    auto smc = std::make_unique<XboxSmc>(fault);
    // Drive-backed tray state: xemu SMC 0x10 open / 0x60 media detected;
    // retain the captured 0x00 empty-drive reset state for the dashboard.
    smc->set_tray_state_reader([this]() { return ide_ ? ide_->dvd_tray_state() : 0; });
    smbus->add_slave(std::move(smc));
    auto encoder=std::make_unique<Cx25871>();
    encoder->observe_writes([this](uint8_t reg,uint8_t value) {
        bool pending=false;
        for(const auto& dump:det_ram_dumps_)pending|=!dump.done;
        if(!pending)return;
        if(captured_encoder_writes_.size()>=65536) {
            cpu_->request_stop("encoder write capture capacity exhausted");return;
        }
        captured_encoder_writes_.emplace_back(reg,value);
    });
    smbus->add_slave(std::move(encoder));
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
    // The RTC owns70..73. Original CMOS validation/repair selects all eight
    // index bits through72 and accesses the shared data register through71.

    // IDE: primary channel (HDD master, ATAPI DVD slave) + bus master at BAR4 (0xFF60,
    // silicon). Completion IRQ 14, level-triggered, same delivery shape as SMBus IRQ 11.
    auto ide = std::make_unique<IdeController>(fault);
    ide_ = ide.get();
    ide_->set_irq([this](int irq, bool level) { pic_->raise(irq, level); }, 14);
    ide_->set_dma_mem(
        [this](uint32_t addr, void* buf, size_t n) { return cpu_->read_mem(addr, buf, n); },
        [this](uint32_t addr, const void* buf, size_t n) {
            return dev_dma_write(kDmaIde, addr, buf, n);
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
    auto nv2a = std::make_unique<Nv2a>(fault, config_.nv2a_renderer);
    nv2a_ = nv2a.get();
    if(!nv2a_->initialize_board_vga_reset())return;
    nv2a_->set_mem_access(
        [this](uint32_t addr, void* buf, size_t n) { return cpu_->read_mem(addr, buf, n); },
        [this](uint32_t addr, const void* buf, size_t n) {
            return dev_dma_write(kDmaNv2a, addr, buf, n);
        },
        [this](uint32_t addr, size_t n) -> uint8_t* {
            const uint64_t end = static_cast<uint64_t>(addr) + n;
            if (end <= kRamSize) return ram_.data() + addr;
            if (addr >= kRamSize && end <= static_cast<uint64_t>(kRamSize) * 2u) {
                return ram_.data() + (addr - kRamSize);
            }
            return nullptr;
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
    auto apu = std::make_unique<McpxApu>(fault,config_.apu_signal_model);
    apu_ = apu.get();
    std::printf("[audio] signal-model=%s; immutable construction profile\n",
                apu_signal_model_name(config_.apu_signal_model));
    if (!apu_->initialize_board_audio_reset()) {
        apu_=nullptr; // initializer already requested a loud CPU stop
        return;
    }
    apu_->set_mem_access(
        [this](uint32_t addr, void* buf, size_t n) { return cpu_->read_mem(addr, buf, n); },
        [this](uint32_t addr, const void* buf, size_t n) {
            return dev_dma_write(kDmaApu, addr, buf, n);
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
            return dev_dma_write(kDmaNvNet, addr, buf, n);
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
#ifdef XLLE_MACHINE_DSP
    // The immutable lookup is an explicit private input; the original kernel
    // supplies the executable bootstrap through actual scratch DMA later.
    if(!apu_->install_ep_y_lookup(kMachineEpYLookup,2048))return;
    audio_dsp_=std::make_unique<AudioDspOwner>(*apu_,*aci_,fault);
    std::printf("[audio] sole GP/EP processors; nominal clock=%lluHz; codec output=headless\n",
                static_cast<unsigned long long>(AudioDspOwner::kClockHz));
#endif

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
            [this, index](uint32_t addr, const void* buf, size_t n) {
                return dev_dma_write(index ? kDmaOhci1 : kDmaOhci0, addr, buf, n);
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
    // Exclusive-time region at the MMIO funnel (beads-xbox.1.14.3). Unicorn calls this
    // synchronously from inside uc_emu_start, so opening it here stops the TCG_EXEC clock
    // and the guest's device excursions stop being charged to translated code.
    perf_excl::Scope region(perf_excl::kMmioDispatch);
    MmioDevice* d = find_mmio(addr);
    uint32_t val = 0;
    if (d) {
        // The NV2A register body stays in MMIO_DISPATCH and carves its own heavy work
        // (PFIFO, raster, GL) out from inside; every other MMIO device body is charged to
        // OTHER_DEVICE here, because those models are not editable from this workstream and
        // this is the single funnel all of them pass through.
        perf_excl::ScopeIf device(perf_excl::kOtherDevice,
                                  d != static_cast<MmioDevice*>(nv2a_));
        if(audio_dsp_ && (d==static_cast<MmioDevice*>(apu_) || d==static_cast<MmioDevice*>(aci_))) {
            uint64_t clock;
            if(!cpu_->mmio_instruction_clock(clock)) {
                cpu_->request_stop("audio MMIO requires the exact synchronous instruction clock");return 0;
            }
            if(!service_audio_time(clock))return 0;
        }
        val = d->mmio_read(addr, size);
    } else {
        char b[96];
        std::snprintf(b, sizeof(b), "unhandled MMIO read addr=0x%08x size=%d — no device", addr,
                      size);
        cpu_->request_stop(b);
    }
    det_fold_dev((static_cast<uint64_t>(addr) << 8) | static_cast<uint32_t>(size),
                 (static_cast<uint64_t>(val) << 1) | 0u);
    if (mmio_event_cb_)
        mmio_event_cb_(MemEvent{mmio_seq_++, 0, addr, static_cast<uint8_t>(size), val});
    else
        mmio_seq_++;
    return val;
}

void XboxMachine::mmio_write(uint32_t addr, int size, uint32_t value) {
    perf_excl::Scope region(perf_excl::kMmioDispatch);
    MmioDevice* d = find_mmio(addr);
    if (d) {
        perf_excl::ScopeIf device(perf_excl::kOtherDevice,
                                  d != static_cast<MmioDevice*>(nv2a_));
        if(audio_dsp_ && (d==static_cast<MmioDevice*>(apu_) || d==static_cast<MmioDevice*>(aci_))) {
            uint64_t clock;
            if(!cpu_->mmio_instruction_clock(clock)) {
                cpu_->request_stop("audio MMIO requires the exact synchronous instruction clock");return;
            }
            if(!service_audio_time(clock))return;
        }
        d->mmio_write(addr, size, value);
    } else {
        char b[96];
        std::snprintf(b, sizeof(b), "unhandled MMIO write addr=0x%08x size=%d value=0x%08x — "
                      "no device", addr, size, value);
        cpu_->request_stop(b);
    }
    det_fold_dev((static_cast<uint64_t>(addr) << 8) | static_cast<uint32_t>(size),
                 (static_cast<uint64_t>(value) << 1) | 1u);
    if (mmio_event_cb_)
        mmio_event_cb_(MemEvent{mmio_seq_++, 1, addr, static_cast<uint8_t>(size), value});
    else
        mmio_seq_++;
}

IoPortDevice* XboxMachine::find_io(uint16_t port) {
    for (auto& d : io_devices_)
        if (d->owns(port)) return d.get();
    return nullptr;
}

uint32_t XboxMachine::io_in(uint16_t port, int size) {
    // Guest IN. One region at the funnel covers the port router and every I/O-port device
    // body behind it (PIC, PIT, IDE, SMBus, PM, RTC, PCI config).
    perf_excl::Scope region(perf_excl::kIoPort);
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
    det_fold_dev((static_cast<uint64_t>(port) << 8) | static_cast<uint32_t>(size),
                 (static_cast<uint64_t>(val) << 1) | 0u);
    if (io_event_cb_)
        io_event_cb_(IoEvent{io_seq_++, 0, port, static_cast<uint8_t>(size), val});
    else
        io_seq_++;
    return val;
}

void XboxMachine::io_out(uint16_t port, int size, uint32_t value) {
    perf_excl::Scope region(perf_excl::kIoPort);
    IoPortDevice* d = find_io(port);
    if (d) {
        d->io_write(port, size, value);
    } else {
        char b[96];
        std::snprintf(b, sizeof(b), "unhandled I/O OUT port=0x%04x size=%d value=0x%08x — "
                      "no device", port, size, value);
        cpu_->request_stop(b);
    }
    det_fold_dev((static_cast<uint64_t>(port) << 8) | static_cast<uint32_t>(size),
                 (static_cast<uint64_t>(value) << 1) | 1u);
    if (io_event_cb_)
        io_event_cb_(IoEvent{io_seq_++, 1, port, static_cast<uint8_t>(size), value});
    else
        io_seq_++;
}

bool XboxMachine::attach_hdd(const std::string& path, bool writeback) {
    return ide_ && ide_->attach_hdd(path, writeback);
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

// Smallest instruction count strictly greater than `insns` at which the device clock
// `floor(n * rate_num / rate_den)` takes a new value. Every device clock in this machine is
// exactly such a function of the retired-instruction count. 128-bit intermediates because
// (k+1) * rate_den overflows 64 bits near the 12 B budget.
static inline uint64_t next_rate_boundary(uint64_t insns, uint64_t rate_num,
                                          uint64_t rate_den) {
    const unsigned __int128 k =
        (static_cast<unsigned __int128>(insns) * rate_num) / rate_den;
    const unsigned __int128 target = (k + 1) * rate_den;  // need n * rate_num >= target
    const unsigned __int128 n = (target + rate_num - 1) / rate_num;
    return n > static_cast<unsigned __int128>(UINT64_MAX) ? UINT64_MAX
                                                         : static_cast<uint64_t>(n);
}

// Instruction count at which the device clock value `ticks` is first reached.
static inline uint64_t insns_at_tick(uint64_t ticks, uint64_t rate_num, uint64_t rate_den) {
    const unsigned __int128 target = static_cast<unsigned __int128>(ticks) * rate_den;
    const unsigned __int128 n = (target + rate_num - 1) / rate_num;
    return n > static_cast<unsigned __int128>(UINT64_MAX) ? UINT64_MAX
                                                         : static_cast<uint64_t>(n);
}

// The next instruction count at which a device EVENT falls due -- an interrupt edge or a
// periodic service with a side effect. The slice loop never lets a slice run past this, so
// raising --machine-chunk-insns can never step over one (the ndsrecomp bounded-rounds-to-
// next-deadline model). It is deliberately the set of events, not the set of clock-argument
// changes: the PIT input clock ticks every ~652 instructions and the NV2A retrace phase
// every ~12,222, but neither is an event -- they are continuous values the guest samples by
// MMIO, and they stay sampled at the slice cadence exactly as they were before this change
// (making them exact needs lazy, read-time evaluation, which is a separate change).
// The optional sole audio processors add a boundary every651/652 instructions;
// their execution and shared-memory ordering require that finer service cadence.
uint64_t XboxMachine::next_device_deadline(uint64_t insns) const {
    uint64_t d = UINT64_MAX;
    const auto take = [&d, insns](uint64_t n) {
        if (n > insns && n < d) d = n;
    };
    // 1 kHz host service (SDL pump, input, frame-series checkpoint) and the OHCI
    // start-of-frame that shares its rate.
    take(next_rate_boundary(insns, 1000, kCpuHz));
    // NV2A frame boundary: raises the vblank interrupt.
    take(next_rate_boundary(insns, 60, kCpuHz));
    // APU / AC'97 256-sample frame (187.5 Hz).
    take(next_rate_boundary(insns, 375, kCpuHz * 2));
#ifdef XLLE_MACHINE_DSP
    // A release inside the slice can create runnable work. Bound even held
    // slices so elapsed audio work never sees CPU stores from later quanta.
    if(audio_dsp_)take(next_rate_boundary(insns,AudioDspOwner::kClockHz,kCpuHz));
#endif
    // PIT channel 0's next IRQ0 edge, asked of the device because the period is whatever the
    // guest programmed (the stock dashboard runs it at ~998.7 Hz).
    if (pit_) {
        const uint64_t tick = pit_->next_irq0_tick();
        if (tick) take(insns_at_tick(tick, Pit8254::kPitFreq, kCpuHz));
    }
    return d;
}

// ---- determinism trace (beads-xbox.1.14.11) -------------------------------------------
// Capture is unconditional; only the report sinks below are opt-in.
void XboxMachine::det_fold_dev(uint64_t a, uint64_t b) {
    const auto fold = [this](uint64_t v) {
        for (int i = 0; i < 8; ++i) {
            dev_digest_ ^= static_cast<uint8_t>(v >> (i * 8));
            dev_digest_ *= 0x100000001b3ull;
        }
    };
    fold(a);
    fold(b);
    if (det_dev_ring_.empty()) det_dev_ring_.resize(kDetDevRing);
    DetDev& d = det_dev_ring_[det_dev_head_];
    d.insns = total_insns_;
    d.addr = static_cast<uint32_t>(a >> 8);
    d.size = static_cast<uint16_t>(a & 0xff);
    d.value = static_cast<uint32_t>(b >> 1);
    d.flags = static_cast<uint16_t>(b & 1u);
    if (++det_dev_head_ == kDetDevRing) {
        det_dev_head_ = 0;
        det_dev_wrapped_ = true;
    }
    if (!det_dev_dumped_ && !det_dev_dump_path_.empty() &&
        total_insns_ >= det_dev_dump_at_) {
        det_dev_dumped_ = true;
        dump_det_dev_ring(det_dev_dump_path_);
    }
}

const char* XboxMachine::dma_source_name(uint32_t source) {
    switch (source) {
        case kDmaIde: return "ide";
        case kDmaNv2a: return "nv2a";
        case kDmaApu: return "apu";
        case kDmaNvNet: return "nvnet";
        case kDmaOhci0: return "ohci0";
        case kDmaOhci1: return "ohci1";
        default: return "?";
    }
}

bool XboxMachine::dev_dma_write(uint32_t source, uint32_t addr, const void* buf, size_t n) {
    // Content hash, 8 bytes at a time so a multi-megabyte surface resolve costs ~0.1 ns/byte.
    uint64_t h = 0xcbf29ce484222325ull;
    const uint8_t* p = static_cast<const uint8_t*>(buf);
    size_t i = 0;
    for (; i + 8 <= n; i += 8) {
        uint64_t w;
        std::memcpy(&w, p + i, 8);
        h = (h ^ w) * 0x100000001b3ull;
    }
    for (; i < n; ++i) h = (h ^ p[i]) * 0x100000001b3ull;
    dma_digest_ = (dma_digest_ ^ h) * 0x100000001b3ull;
    dma_digest_ = (dma_digest_ ^ (static_cast<uint64_t>(addr) << 32 | n)) * 0x100000001b3ull;
    dma_writes_++;
    if (det_dma_ring_.empty()) det_dma_ring_.resize(kDetDmaRing);
    DetDma& d = det_dma_ring_[det_dma_head_];
    d.insns = total_insns_;
    d.hash = h;
    d.addr = addr;
    d.size = static_cast<uint32_t>(n);
    d.source = source;
    if (++det_dma_head_ == kDetDmaRing) {
        det_dma_head_ = 0;
        det_dma_wrapped_ = true;
    }
    if (!det_dma_dumped_ && !det_dma_dump_path_.empty() &&
        total_insns_ >= det_dma_dump_at_) {
        det_dma_dumped_ = true;
        dump_det_dma_ring(det_dma_dump_path_);
    }
    return cpu_->write_mem(addr, buf, n);
}

bool XboxMachine::dump_det_dma_ring(const std::string& path) const {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    std::fprintf(f, "# det-dma-ring writes=%llu capacity=%llu wrapped=%d\n",
                 static_cast<unsigned long long>(dma_writes_),
                 static_cast<unsigned long long>(kDetDmaRing),
                 det_dma_wrapped_ ? 1 : 0);
    std::fprintf(f, "# insns source addr size hash\n");
    const size_t count = det_dma_wrapped_ ? kDetDmaRing : det_dma_head_;
    const size_t start = det_dma_wrapped_ ? det_dma_head_ : 0;
    for (size_t i = 0; i < count; ++i) {
        const DetDma& d = det_dma_ring_[(start + i) % kDetDmaRing];
        std::fprintf(f, "%llu %s %08x %u %016llx\n",
                     static_cast<unsigned long long>(d.insns),
                     dma_source_name(d.source), d.addr, d.size,
                     static_cast<unsigned long long>(d.hash));
    }
    std::fclose(f);
    return true;
}

bool XboxMachine::dump_det_dev_ring(const std::string& path) const {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    std::fprintf(f, "# det-dev-ring events=%llu capacity=%llu wrapped=%d\n",
                 static_cast<unsigned long long>(io_seq_ + mmio_seq_),
                 static_cast<unsigned long long>(kDetDevRing),
                 det_dev_wrapped_ ? 1 : 0);
    std::fprintf(f, "# insns addr size rw value\n");
    const size_t count = det_dev_wrapped_ ? kDetDevRing : det_dev_head_;
    const size_t start = det_dev_wrapped_ ? det_dev_head_ : 0;
    for (size_t i = 0; i < count; ++i) {
        const DetDev& d = det_dev_ring_[(start + i) % kDetDevRing];
        std::fprintf(f, "%llu %08x %u %c %08x\n",
                     static_cast<unsigned long long>(d.insns), d.addr, d.size,
                     (d.flags & 1u) ? 'W' : 'R', d.value);
    }
    std::fclose(f);
    return true;
}

void XboxMachine::det_record_slice(const DetSlice& s) {
    const auto fold = [this](uint64_t v) {
        for (int b = 0; b < 8; ++b) {
            det_digest_ ^= static_cast<uint8_t>(v >> (b * 8));
            det_digest_ *= 0x100000001b3ull;
        }
    };
    fold(s.insns);
    fold((static_cast<uint64_t>(s.eip_in) << 32) | s.retired);
    fold((static_cast<uint64_t>(s.eip_out) << 32) | s.flags);
    det_slices_++;
    if (det_ring_.empty()) det_ring_.resize(kDetRingSlices);
    det_ring_[det_ring_head_] = s;
    if (++det_ring_head_ == kDetRingSlices) {
        det_ring_head_ = 0;
        det_ring_wrapped_ = true;
    }
    if (det_log_ && total_insns_ >= det_log_next_) {
        std::fprintf(det_log_, "%llu %llu %016llx %016llx %llu %08x %016llx %llu\n",
                     static_cast<unsigned long long>(total_insns_),
                     static_cast<unsigned long long>(det_slices_),
                     static_cast<unsigned long long>(det_digest_),
                     static_cast<unsigned long long>(dev_digest_),
                     static_cast<unsigned long long>(dev_events()), s.eip_out,
                     static_cast<unsigned long long>(dma_digest_),
                     static_cast<unsigned long long>(dma_writes_));
        while (det_log_next_ <= total_insns_) det_log_next_ += det_log_interval_;
    }
    if (!det_ring_dumped_ && !det_ring_dump_path_.empty() &&
        total_insns_ >= det_ring_dump_at_) {
        det_ring_dumped_ = true;
        dump_det_ring(det_ring_dump_path_);
    }
    for (auto& dump : det_ram_dumps_) {
        if (dump.done || total_insns_ < dump.at_insns) continue;
        dump.done = true;
        std::FILE* ram_file=std::fopen(dump.path.c_str(),"wb");
        if(!ram_file){cpu_->request_stop("RAM capture could not be opened");return;}
        const size_t written=std::fwrite(ram_.data(),1,ram_.size(),ram_file);
        const int closed=std::fclose(ram_file);
        if(written!=ram_.size() || closed) {
            cpu_->request_stop("RAM capture could not be completed");return;
        }
        // Same stopped CPU boundary as RAM, but recorded as actual writes,
        // not an allegedly faithful snapshot of the legacy encoder model.
        const std::string encoder_path=dump.path+".encoder.aot-private.json";
        if(std::FILE* f=std::fopen(encoder_path.c_str(),"wb")) {
            std::fprintf(f,"{\"schema\":1,\"retired\":%llu,\"writes\":[",
                         static_cast<unsigned long long>(total_insns_));
            for(size_t i=0;i<captured_encoder_writes_.size();++i) {
                const auto& write=captured_encoder_writes_[i];
                std::fprintf(f,"%s[%u,%u]",i?",":"",unsigned(write.first),unsigned(write.second));
            }
            std::fputs("]}\n",f);
            bool failed=std::ferror(f)!=0;
            if(std::fclose(f))failed=true;
            if(failed){cpu_->request_stop("encoder write capture could not be completed");return;}
        } else {cpu_->request_stop("encoder write capture could not be opened");return;}
        std::printf("[det] ram dump %s at insns=%llu slices=%llu digest=%016llx\n",
                    dump.path.c_str(),
                    static_cast<unsigned long long>(total_insns_),
                    static_cast<unsigned long long>(det_slices_),
                    static_cast<unsigned long long>(det_digest_));
        std::fflush(stdout);
    }
}

bool XboxMachine::open_det_log(const std::string& path, uint64_t interval) {
    if (det_log_) std::fclose(det_log_);
    det_log_ = std::fopen(path.c_str(), "wb");
    det_log_interval_ = interval ? interval : 1;
    det_log_next_ = det_log_interval_;
    return det_log_ != nullptr;
}

bool XboxMachine::dump_det_ring(const std::string& path) const {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    std::fprintf(f, "# det-ring slices=%llu capacity=%llu wrapped=%d\n",
                 static_cast<unsigned long long>(det_slices_),
                 static_cast<unsigned long long>(kDetRingSlices),
                 det_ring_wrapped_ ? 1 : 0);
    std::fprintf(f, "# insns eip_in retired eip_out flags dev_events dev_hash32\n");
    const size_t count = det_ring_wrapped_ ? kDetRingSlices : det_ring_head_;
    const size_t start = det_ring_wrapped_ ? det_ring_head_ : 0;
    for (size_t i = 0; i < count; ++i) {
        const DetSlice& s = det_ring_[(start + i) % kDetRingSlices];
        std::fprintf(f, "%llu %08x %u %08x %x %u %08x\n",
                     static_cast<unsigned long long>(s.insns), s.eip_in,
                     s.retired, s.eip_out, s.flags, s.dev_events, s.dev_hash32);
    }
    std::fclose(f);
    return true;
}

uint64_t XboxMachine::run(uint32_t start, uint64_t max, std::string& err) {
    if(!device_fault_.empty()) {err=device_fault_;return 0;}
    // Execute in short chunks so the interrupt controller can deliver external IRQs at clean
    // instruction boundaries between chunks (Unicorn has no native IRQ path — see the CPU
    // floor's inject_interrupt). The PIT clock advances with retired instructions.
    uint32_t eip = start;
    uint64_t done = 0;
    while (done < max) {
#if XLLE_RENDERER_HLE
        if (!hle_allocation_observers_installed_)
            install_hle_allocation_observers();
#if XLLE_XDK_DRAW_SPIKE
        if (!hle_xdk_draw_installed_) install_hle_xdk_draw();
#endif
#endif
        // ONE exclusive region per slice for the whole loop body (beads-xbox.1.14.3).
        // TCG_EXEC is opened inside cpu_->run(), which stops this clock and resumes it on
        // return, so SLICE_OVERHEAD's self time is exactly the loop machinery: the PIC
        // pending check, the device frame clocks, interrupt-injection bookkeeping,
        // get_regs() and the idle census. This replaces three separate host_poll_devices
        // ScopedAdd scopes whose six steady_clock reads per slice were most of what that
        // bucket reported (71.7 ns/scope, https://github.com/mstan/xboxllr/blob/5faaafad269d671b39737edbebaed66c81b2b9fb/docs/PERF-AUDIT-2026-09-02.md §6).
        perf_excl::Scope slice_region(perf_excl::kSliceOverhead);
        bool irq_armed = false;
        {
            const uint64_t guest_millisecond = total_insns_ * 1000 / kCpuHz;
            if (host_poll_cb_ && guest_millisecond != host_poll_millisecond_) {
                host_poll_millisecond_ = guest_millisecond;
                // The 1 kHz host service (SDL pump, input, network, and the
                // --frame-series checkpoint, which opens DIAG_CHECKPOINT of its own).
                perf_excl::Scope poll_region(perf_excl::kHostPoll);
                host_poll_cb_();
            }
            // Arm the floor to stop as soon as IF opens if an IRQ is already waiting (so we hit
            // the guest's brief sti window). Otherwise the chunk runs to completion.
            irq_armed = pic_ && pic_->intr_pending();
            if (irq_armed) pending_boundaries_++;
            cpu_->set_irq_pending(irq_armed);
        }
        // Slice budget = the tightest of: the configured chunk, what is left of the run, and
        // the next device event deadline. The last term is what lets the chunk grow without
        // stepping over an interrupt edge or an active audio processor quantum.
        const uint64_t deadline = next_device_deadline(total_insns_);
        uint64_t chunk = std::min(cpu_chunk_insns_, max - done);
        if (deadline > total_insns_ && deadline - total_insns_ < chunk)
            chunk = deadline - total_insns_;
        std::string cerr;
        // ---- idle fast-forward (beads-xbox.1.4.7) ---------------------------------
        // Replaces the EXECUTION of the slice, not the slice: everything below this point
        // (device clocks, interrupt delivery, the census) runs exactly as it would have.
        // The skip is always a whole number of idle-loop iterations ending at the same
        // instruction boundary, and never crosses `deadline`, so the state the guest or
        // a device can observe is bit-identical to the executed path.
        // Determinism trace: this slice's entry identity, completed below (beads-xbox.1.14.11).
        DetSlice det{};
        det.insns = total_insns_;
        det.eip_in = eip;
        if (irq_armed) det.flags |= kDetIrqArmed;
        uint64_t n = 0;
        uint64_t skipped = 0;
        if (try_idle_fast_forward(eip, irq_armed, deadline, max - done, skipped)) {
            det.flags |= kDetFastForward;
            if (!cpu_->advance_instruction_clock(skipped)) {
                err = "idle fast-forward: floor has no advanceable instruction clock";
                break;
            }
            n = skipped;
            idle_census_.ff_skips++;
            idle_census_.ff_insns += skipped;
            idle_census_.ff_iters += skipped / kIdleLoopInsns;
            if (skipped > idle_census_.ff_max_insns) idle_census_.ff_max_insns = skipped;
        } else {
            // Both CPU tiers honor the exact deadline. Exceptions, stops and
            // interrupt windows may end early; `n` is authoritative, never `chunk`.
            n = cpu_->run(eip, chunk, cerr);
        }
        done += n;
        total_insns_ += n;
        progress_insns_atomic_.store(total_insns_, std::memory_order_relaxed);

        // A host/device safety stop can retire zero instructions. Surface it before
        // advancing device clocks or allocating any optional diagnostic buffers.
        if (!cerr.empty()) { err = cerr; break; }

        // The surviving legacy host_poll_devices scope. The three per-slice scopes this
        // bucket used to carry collapse to this one (beads-xbox.1.14.3 item 5): it keeps the
        // `[perf] host_poll_devices_ns=` field of the existing report populated with a real
        // number covering the device frame clocks and interrupt injection, while
        // the authoritative measurement of the whole slice body is now SLICE_OVERHEAD above.
        // Its narrowed span is a deliberate change and is recorded on the bead.
        {
            std::optional<perf::ScopedAdd> perf_t;
            if (perf::g_enabled) {
                perf_t.emplace(perf::g_ns_host_poll_devices,
                               perf::g_calls_host_poll_devices);
            }
            if (pit_) pit_->set_time(total_insns_ * Pit8254::kPitFreq / kCpuHz);
            // Frame clock: ~60 Hz VBLANK derived from the same retired-instruction time base,
            // plus the sub-frame phase for the VGA retrace status bit.
            if (nv2a_) {
                uint64_t t60 = total_insns_ * 60;
                // PTIMER shares this machine clock. Fixed reset oscillator;
                // PLL reprogramming remains outside the qualified model.
                constexpr uint64_t gpu_hz = 233333324;
                nv2a_->set_timer_clock((total_insns_ / kCpuHz) * gpu_hz +
                                      (total_insns_ % kCpuHz) * gpu_hz / kCpuHz);
                nv2a_->set_frame(t60 / kCpuHz);
                nv2a_->set_frame_phase(static_cast<uint32_t>((t60 % kCpuHz) * 1000 / kCpuHz));
            }
            // APU setup-engine frame: 256 samples / 48 kHz = 5.333 ms (187.5 Hz).
            if (audio_dsp_) {
                if(!service_audio_time(total_insns_)) {
                    err=device_fault_.empty()?"shared audio device clock failed":device_fault_;
                    break;
                }
            } else if (apu_) {
                apu_->set_time(total_insns_,kCpuHz);
                apu_->set_frame(total_insns_ * 375 / (kCpuHz * 2));
            }
            // AC'97 PCM output consumes the same 256-sample frames, without host audio output.
            if (aci_ && !audio_dsp_) {
                aci_->set_time(total_insns_,kCpuHz);
                aci_->set_frame(total_insns_ * 375 / (kCpuHz * 2));
            }
            // USB OHCI start-of-frame cadence is 1 kHz.
            if (ohci0_) ohci0_->set_frame(total_insns_ * 1000 / kCpuHz);
            if (ohci1_) ohci1_->set_frame(total_insns_ * 1000 / kCpuHz);
            // Deliver one pending, unmasked external interrupt at this boundary. Confirm the
            // guest can take it (inject succeeds) BEFORE committing the PIC's ISR/ack
            // bookkeeping — an ack without delivery would wedge the line in-service.
            if (pic_ && pic_->intr_pending() && cpu_->interrupts_enabled()) {
                int vec = pic_->peek();
                if (vec >= 0) {
                    std::string ierr;
                    if (cpu_->inject_interrupt(static_cast<uint8_t>(vec), ierr)) {
                        pic_->acknowledge();
                        injected_++;
                        det.flags |= kDetInjected |
                                     (static_cast<uint32_t>(vec & 0xff) << 8);
                    } else if (!ierr.empty()) { err = ierr; break; }
                }
            }
        }
        eip = cpu_->get_regs().eip;
        det.retired = static_cast<uint32_t>(n);
        det.eip_out = eip;
        det.dev_events = static_cast<uint32_t>(dev_events() - det_dev_events_at_slice_);
        det_dev_events_at_slice_ = dev_events();
        det.dev_hash32 = static_cast<uint32_t>(dev_digest_);
        det_record_slice(det);
        record_idle_census(eip, n, chunk, irq_armed);
    }
    cpu_->set_irq_pending(false);
    return done;
}

// ---------------------------------------------------------------------------------------
// Idle fast-forward (beads-xbox.1.4.7).
//
// DETERMINISM ARGUMENT. When the boundary EIP is the idle-loop head, one iteration is the
// fixed 8-instruction shape pinned in machine.hpp (verified byte for byte here, every time),
// and its ONLY effects are:
//   * EIP, which returns to the head;
//   * EFLAGS, whose final value comes from `cmp dword [ebx+0x2c],0` on a zero operand plus
//     the `cli`, i.e. exactly kIdleFlagsValue -- a FIXED POINT, so entering with those bits
//     means leaving with those bits and no fixup is possible to get wrong;
//   * the retired-instruction clock, which the guest can observe only through RDTSC. That
//     clock is now instruction-derived (beads-xbox.1.14.10), so advancing it by the exact
//     number of instructions skipped leaves it identical to the executed path.
// It reads two guest cells -- DpcListHead at [ebp] and the ready-thread field at [ebx+0x2c] --
// and both are mutated only by an ISR or a DPC. An ISR requires interrupt delivery, and the
// interruptible points are 0x8001e3d0 and 0x8001e3d1 (IF is opened by `sti` and closed by
// `cli` two instructions later). So the skip is exact iff no interrupt can be delivered
// inside it, and that is precisely what bounding it by next_device_deadline() guarantees:
// every source that can make pic_->intr_pending() true -- PIT IRQ0, the 60 Hz vblank, the
// 187.5 Hz APU/AC'97 frame, the 1 kHz OHCI SOF and host service -- is in that deadline set,
// and no IRQ is pending on entry (irq_armed false).
//
// The LANDING POINT is the phase the skip started from, never a different point in the
// iteration -- the skip is always a whole multiple of 8 instructions. The remaining
// (deadline - total) mod 8 instructions are then executed by the normal path, which ends the
// slice at the same instruction boundary the un-skipped run would have ended it at, and the machine
// loop delivers the interrupt at exactly the boundary it would have used. That equality is
// exact, not approximate: an un-skipped run covers the same stretch in slices of exactly 4096
// (a multiple of 8, so every intermediate boundary is the same phase), and its final
// pre-deadline stop is at R, where R is the distance to the deadline. The skip lands at
// 8*floor(R/8), and the exact CPU path executes the remaining R mod 8 instructions. Intermediate slice
// boundaries are skipped, but a slice boundary has no side effect of its own: the host poll is
// gated on the guest millisecond (in the deadline set), the device clocks are pure functions
// of total_insns_ that nothing sampled in between, and no interrupt was deliverable.
bool XboxMachine::try_idle_fast_forward(uint32_t eip, bool irq_armed, uint64_t deadline,
                                        uint64_t remaining, uint64_t& skipped) {
    skipped = 0;
    if (!idle_fast_forward_ || idle_ff_bytes_bad_) return false;
    // Each skipped iteration returns to the same instruction boundary.
    unsigned phase = kIdlePhases;
    for (unsigned i = 0; i < kIdlePhases; ++i) {
        if (eip == kIdlePhaseEip[i]) { phase = i; break; }
    }
    if (phase == kIdlePhases) return false;
    // An armed IRQ means the very next open window delivers; never skip over that.
    if (irq_armed) return false;
    idle_census_.ff_at_head++;
    if (!cpu_->idle_skip_context_ok(phase == 1)) {
        idle_census_.ff_rej_context++;
        return false;
    }

    // How far may we go? Never past the next device event, never past the run budget.
    uint64_t budget = remaining;
    if (deadline <= total_insns_) return false;
    if (deadline - total_insns_ < budget)
        budget = deadline - total_insns_;
    const uint64_t iters = budget / kIdleLoopInsns;
    if (iters == 0) { idle_census_.ff_rej_short++; return false; }

    const X86Regs r = cpu_->get_regs();
    const uint32_t want = kIdleFlagsValue | (kIdlePhaseIf[phase] ? kIdleFlagIf : 0u);
    if ((r.eflags & kIdleFlagsMask) != want) {
        // Either the first entry into the loop (flags left by the caller) or a guest state
        // this analysis does not cover. The next head boundary will qualify.
        idle_census_.ff_rej_flags++;
        return false;
    }

    // The pinned loop shape, re-verified on every skip. A kernel that is not build 4983, a
    // patched idle loop, or a remapped page must disable the skip rather than assume.
    uint8_t code[kIdleLoopBytes] = {};
    if (!cpu_->read_cached_ram(kIdleLoopHead, code, sizeof(code), true)) {
        idle_census_.ff_rej_read++;
        return false;
    }
    static const uint8_t kIdleLoopShape[kIdleLoopBytes] = {
        0xfb,                                // sti
        0x90, 0x90,                          // nop; nop
        0xfa,                                // cli
        0x3b, 0x6d, 0x00,                    // cmp ebp,[ebp]
        0x74, 0x0c,                          // je +0x0c -> 0x8001e3e3
        0xb1, 0x02,                          // mov cl,2
        0xe8, 0xde, 0x8d, 0xff, 0xff,        // call 0x800171bc
        0xe8, 0x4c, 0x00, 0x00, 0x00,        // call 0x8001e42f
        0x83, 0x7b, 0x2c, 0x00,              // cmp dword [ebx+0x2c],0
        0x74, 0xe5,                          // je -0x1b -> 0x8001e3ce
    };
    if (std::memcmp(code, kIdleLoopShape, sizeof(code)) != 0) {
        idle_census_.ff_rej_bytes++;
        idle_ff_bytes_bad_ = true;
        std::fprintf(stderr,
                     "[host] idle fast-forward DISABLED: the loop at 0x%08x is not the pinned "
                     "kernel-4983 shape\n", kIdleLoopHead);
        return false;
    }

    // Both branches of the iteration must be the idle ones, or the shape is not 8 instructions.
    uint32_t dpc_head = 0, next_thread = 0;
    if (!cpu_->read_cached_ram(r.ebp, &dpc_head, 4, false) ||
        !cpu_->read_cached_ram(r.ebx + 0x2cu, &next_thread, 4, false)) {
        idle_census_.ff_rej_read++;
        return false;
    }
    if (dpc_head != r.ebp || next_thread != 0) {
        idle_census_.ff_rej_state++;
        return false;
    }

    skipped = iters * kIdleLoopInsns;
    idle_census_.ff_phase[phase]++;
    return true;
}

// Slice-granularity idle/thread census. Called once per CPU slice with the boundary EIP that
// was already read for the next uc_emu_start, so it adds no register reads on the hot path.
void XboxMachine::record_idle_census(uint32_t eip, uint64_t retired, uint64_t chunk,
                                     bool irq_armed) {
    IdleCensus& c = idle_census_;
    c.slices++;
    c.insns += retired;
    if (eip >= idle_eip_lo_ && eip <= idle_eip_hi_) {
        c.idle_slices++;
        c.idle_insns += retired;
    }
    // Instructions the slice left on the table because the next whole translation block did
    // not fit its budget. Zero on a per-instruction floor; bounded by one block otherwise.
    if (retired < chunk) c.budget_slack += chunk - retired;
    // An early stop is the floor stopping because the guest opened its interrupt window
    // (the `sti` window of the idle loop), not because the budget ran out. `retired < chunk`
    // used to be an exact proxy for that; on a block-granularity floor it is not, because a
    // slice normally ends one partial block short of its budget, so ask the floor directly.
    if (cpu_->stopped_for_irq_window()) {
        c.early_stops++;
        if (irq_armed) c.early_stops_irq_armed++;
    }

    // Boundary-EIP histogram by 4KB page. Exact while the table has room; the scan stops at the
    // first free slot, and in practice only a handful of pages ever appear at slice boundaries.
    const uint32_t page = eip & ~0xFFFu;
    bool placed = false;
    for (uint32_t i = 0; i < c.page_count; ++i) {
        if (c.pages[i].page == page) {
            c.pages[i].slices++;
            c.pages[i].insns += retired;
            placed = true;
            break;
        }
    }
    if (!placed) {
        if (c.page_count < kIdleCensusPages) {
            c.pages[c.page_count].page = page;
            c.pages[c.page_count].slices = 1;
            c.pages[c.page_count].insns = retired;
            c.page_count++;
        } else {
            c.pages_dropped++;
        }
    }

    // Thread attribution at slice granularity: one 4-byte guest read per slice, not per insn.
    uint32_t thread = 0;
    if (!read(kKpcrCurrentThreadPhys, &thread, 4) || !(thread & 0x80000000u)) return;
    c.thread_reads++;
    for (uint32_t i = 0; i < c.thread_count; ++i) {
        if (c.threads[i].thread == thread) {
            c.threads[i].slices++;
            c.threads[i].insns += retired;
            return;
        }
    }
    if (c.thread_count < kIdleCensusThreads) {
        c.threads[c.thread_count].thread = thread;
        c.threads[c.thread_count].slices = 1;
        c.threads[c.thread_count].insns = retired;
        c.thread_count++;
    } else {
        c.threads_dropped++;
    }
}

}  // namespace xlle
