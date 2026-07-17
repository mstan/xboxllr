// xlle — PCI config space + Xbox bus-0 device table. See pci.hpp.
#include "machine/pci.hpp"

#include <cstdio>

namespace xlle {

namespace {
constexpr uint16_t kNvidia = 0x10DE;

// Add a bus-0 function with its standard header populated. `io_bar_off`/`io_bar_base` set one
// I/O BAR to a fixed real-hardware base (bit0=1 = I/O space); pass 0 to leave BARs at reset.
PciFunction mkfn(uint8_t dev, uint8_t fn, const char* name, uint16_t devid, uint16_t cls,
                 uint8_t rev, uint8_t io_bar_off = 0, uint32_t io_bar_base = 0) {
    PciFunction f;
    f.bus = 0; f.dev = dev; f.fn = fn; f.name = name;
    f.set16(0x00, kNvidia);
    f.set16(0x02, devid);
    f.set16(0x04, 0x0000);   // command (reset: decode disabled until firmware enables)
    f.set16(0x06, 0x0000);   // status
    f.cfg[0x08] = rev;
    f.set16(0x0A, cls);      // subclass:class (class code high 16 bits)
    if (io_bar_off) f.set32(io_bar_off, io_bar_base | 0x1);  // I/O-space BAR
    return f;
}
}  // namespace

PciHost::PciHost(FaultFn fault) : fault_(std::move(fault)) {
    // Xbox bus 0 (identities verified vs xemu/hw/xbox/xbox_pci.c + xbox.c). Class codes are the
    // upper 16 bits (subclass<<8 | base class). I/O bases are the fixed real-hardware bases:
    // ACPI/PM at 0x8000 (LPC BAR0/off 0x10) and SMBus at 0xC000 (SMBus BAR1/off 0x14).
    {
        // Host bridge / NV2A northbridge memory controller. Config reg 0x84 is the RAM-size
        // register: the real boot's X-codes program it (xc_pci_write 80000084 <top-of-RAM>),
        // and the kernel reads it back to size physical memory. We inject the 2BL directly and
        // never run the X-codes, so we must supply the value ourselves. Silicon (agent, 64MB
        // retail v1.1) reads 0x03FFFFFF here; without it the kernel defaults to 128MB and walks
        // the PFN database off the end of RAM. (0x07FFFFFF would be a 128MB devkit.)
        PciFunction hb = mkfn(0x00, 0, "host-bridge", 0x02A5, 0x0600, 0xA1);
        hb.set32(0x84, 0x03FFFFFF);
        funcs_.push_back(hb);
    }
    // LPC. This board is a v1.1: PM base lives in config reg 0x84 (=0x8001), and the reg-0x10
    // BAR is 0 (captured from silicon 2026-07-16: reg 0x10 -> 0, reg 0x84 -> 0x8001; reg 0x00
    // -> 0x01B210DE). The 2BL's `cmp [cfg 0x10], 0x8001` is a board-revision detector — v1.0
    // decodes PM via the BAR, v1.1 via reg 0x84 — so leaving the BAR 0 takes the v1.1 path.
    {
        PciFunction lpc = mkfn(0x01, 0, "lpc", 0x01B2, 0x0601, 0xB2);
        lpc.set32(0x84, 0x00008001);
        // IRQ-routing block, captured from the live console 2026-07-16: 0x64 = 0x0B0C
        // (SMBus IRQ 11 / SCI IRQ 12 — re-confirmed), 0x6C = 0x0E065491 (contains the
        // dev-9 IDE nibble 0xE = IRQ 14), plus neighbours 0x60/0x68 for completeness.
        lpc.set32(0x60, 0x00000400);
        lpc.set32(0x64, 0x00000B0C);
        lpc.set32(0x68, 0x00031000);
        lpc.set32(0x6C, 0x0E065491);
        funcs_.push_back(lpc);
    }
    funcs_.push_back(mkfn(0x01, 1, "smbus",       0x01B4, 0x0C05, 0xB1, 0x14, 0xC000));
    funcs_.push_back(mkfn(0x02, 0, "usb0",        0x01C2, 0x0C03, 0xB1));
    funcs_.push_back(mkfn(0x03, 0, "usb1",        0x01C2, 0x0C03, 0xB1));
    funcs_.push_back(mkfn(0x04, 0, "nic",         0x01C3, 0x0200, 0xB1));
    funcs_.push_back(mkfn(0x05, 0, "apu",         0x01B0, 0x0401, 0xB1));
    funcs_.push_back(mkfn(0x06, 0, "aci",         0x01B1, 0x0401, 0xB1));
    // IDE (00:09.0). Captured from the live console 2026-07-16 (agent EXEC PCI reads): rev
    // 0xD4, prog-if 0x8A (both channels legacy mode, bus-master capable), command 0x0005
    // (I/O + bus master enabled), status 0x00B0, BAR0-3 = 0 (legacy decode), BAR4 = 0xFF61
    // (bus-master base 0xFF60), 0x3C dword = 0x01030000 (no INTx line/pin: legacy IRQ 14),
    // and the ATA timing registers 0x44-0x60. The init-table X-codes never touch dev 9
    // (decoded 2026-07-16), so this is the kernel-visible state we boot into.
    {
        PciFunction ide = mkfn(0x09, 0, "ide", 0x01BC, 0x0101, 0xD4);
        ide.cfg[0x09] = 0x8A;               // prog-if
        ide.set16(0x04, 0x0005);            // command: I/O space + bus master
        ide.set16(0x06, 0x00B0);            // status
        ide.set32(0x20, 0x0000FF61);        // BAR4: bus-master I/O base 0xFF60
        ide.set32(0x3C, 0x01030000);        // min_gnt/max_lat, no INTx
        ide.set32(0x44, 0x00020001);
        ide.set32(0x4C, 0x00000900);
        ide.set32(0x50, 0x00000002);
        ide.set32(0x58, 0x20202020);
        ide.set32(0x5C, 0xFFFF00FF);
        ide.set32(0x60, 0xC0C0C0C0);
        funcs_.push_back(ide);
    }
    funcs_.push_back(mkfn(0x1E, 0, "agp-bridge",  0x01B7, 0x0604, 0xA1));
}

PciFunction* PciHost::find(uint8_t bus, uint8_t dev, uint8_t fn) {
    for (auto& f : funcs_)
        if (f.bus == bus && f.dev == dev && f.fn == fn) return &f;
    return nullptr;
}

uint32_t PciHost::io_read(uint16_t port, int size) {
    // CONFIG_ADDRESS (0xCF8..0xCFB): return the latched address.
    if (port < 0xCFC) {
        uint32_t shift = (port - 0xCF8) * 8;
        uint32_t mask = (size >= 4) ? 0xFFFFFFFFu : ((1u << (size * 8)) - 1);
        return (config_addr_ >> shift) & mask;
    }
    // CONFIG_DATA (0xCFC..0xCFF): decode the latched address and read config space.
    if (!(config_addr_ & 0x80000000u)) return 0xFFFFFFFFu;  // enable bit clear
    uint8_t bus = (config_addr_ >> 16) & 0xFF;
    uint8_t dev = (config_addr_ >> 11) & 0x1F;
    uint8_t fn  = (config_addr_ >> 8) & 0x07;
    uint8_t reg = (config_addr_ & 0xFC) | ((port - 0xCFC) & 0x3);
    PciFunction* f = find(bus, dev, fn);
    if (!f) return 0xFFFFFFFFu;  // absent function — enumeration relies on all-ones
    uint32_t v = 0;
    for (int i = 0; i < size; ++i) {
        uint8_t off = reg + i;
        v |= static_cast<uint32_t>(off < 256 ? f->cfg[off] : 0xFF) << (8 * i);
    }
    return v;
}

void PciHost::io_write(uint16_t port, int size, uint32_t value) {
    // CONFIG_ADDRESS latch.
    if (port < 0xCFC) {
        uint32_t shift = (port - 0xCF8) * 8;
        uint32_t mask = (size >= 4) ? 0xFFFFFFFFu : ((1u << (size * 8)) - 1);
        config_addr_ = (config_addr_ & ~(mask << shift)) | ((value & mask) << shift);
        return;
    }
    // CONFIG_DATA write.
    if (!(config_addr_ & 0x80000000u)) return;
    uint8_t bus = (config_addr_ >> 16) & 0xFF;
    uint8_t dev = (config_addr_ >> 11) & 0x1F;
    uint8_t fn  = (config_addr_ >> 8) & 0x07;
    uint8_t reg = (config_addr_ & 0xFC) | ((port - 0xCFC) & 0x3);
    PciFunction* f = find(bus, dev, fn);
    if (!f) return;
    for (int i = 0; i < size; ++i) {
        uint8_t off = reg + i;
        if (off < 256) f->cfg[off] = static_cast<uint8_t>(value >> (8 * i));
    }
    // Modeled side effect: LPC (00:01.0) config 0x80 bit1 disables the MCPX boot-ROM overlay
    // (xemu xbox_lpc_config_write). We have no overlay yet (flash is always mapped; authentic
    // MCPX cold boot is deferred, M9), so this is a no-op we simply record.
    if (bus == 0 && dev == 1 && fn == 0 && reg <= 0x80 && (reg + size) > 0x80) {
        if (f->cfg[0x80] & 0x02) mcpx_rom_disabled_ = true;
    }
    for (auto& cb : cfg_write_observers_) cb(bus, dev, fn, reg, size, value);
}

}  // namespace xlle
