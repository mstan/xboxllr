// Authored transport tests against the ATAPI task-file/packet interface.
#include "machine/ide.hpp"
#include "machine/smbus.hpp"
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

static void check(bool ok, const char* why) {
    if (!ok) { std::fprintf(stderr, "DVD: %s\n", why); std::exit(1); }
}
using Packet = std::array<uint8_t, 12>;
struct Fixture {
    std::vector<std::string> faults;
    std::vector<uint8_t> ram = std::vector<uint8_t>(256 * 1024, 0xcc);
    bool irq = false;
    unsigned writes = 0;
    xlle::IdeController ide{[this](const std::string& s) { faults.push_back(s); }};
    Fixture() {
        ide.set_irq([this](int n, bool level) { check(n == 14, "IRQ number"); irq = level; }, 14);
        ide.set_dma_mem([this](uint32_t a, void* p, size_t n) {
            if (uint64_t(a) + n > ram.size()) return false;
            std::memcpy(p, ram.data() + a, n); return true;
        }, [this](uint32_t a, const void* p, size_t n) {
            if (uint64_t(a) + n > ram.size()) return false;
            std::memcpy(ram.data() + a, p, n); ++writes; return true;
        });
    }
    uint8_t status() { return uint8_t(ide.io_read(0x3f6, 1)); }
    void ack() { ide.io_read(0x1f7, 1); }
    void packet(Packet p, bool dma = false, uint16_t limit = 0xfffe) {
        ack();
        ide.io_write(0x1f6, 1, 0xb0);
        ide.io_write(0x1f1, 1, dma ? 1 : 0);
        ide.io_write(0x1f4, 1, uint8_t(limit));
        ide.io_write(0x1f5, 1, limit >> 8);
        ide.io_write(0x1f7, 1, 0xa0);
        check(status() & 8, "PACKET requests actual CDB");
        for (unsigned i = 0; i < 12; i += 2)
            ide.io_write(0x1f0, 2, p[i] | (uint32_t(p[i + 1]) << 8));
    }
    std::vector<uint8_t> reply() {
        std::vector<uint8_t> out;
        for (unsigned guard = 0; status() & 8; ++guard) {
            check(guard < 500, "bounded PIO phases");
            check(irq, "PIO data-ready IRQ"); ack();
            check((ide.io_read(0x1f2, 1) & 3) == 2, "PIO data phase reason");
            const unsigned n = ide.io_read(0x1f4, 1) | (ide.io_read(0x1f5, 1) << 8);
            check(n != 0, "nonzero PIO phase");
            for (unsigned i = 0; i < n; i += 2) {
                const uint32_t v = ide.io_read(0x1f0, 2);
                out.push_back(uint8_t(v));
                if (i + 1 < n) out.push_back(uint8_t(v >> 8));
            }
        }
        check((status() & 0x89) == 0, "PIO completes without error/busy/DRQ");
        check((ide.io_read(0x1f2, 1) & 3) == 3 && irq, "final completion reason and IRQ");
        ack(); return out;
    }
    void prd(uint32_t at, uint32_t data, uint16_t bytes, bool end) {
        for (unsigned i = 0; i < 4; ++i) ram[at + i] = uint8_t(data >> (i * 8));
        ram[at + 4] = uint8_t(bytes); ram[at + 5] = uint8_t(bytes >> 8);
        ram[at + 6] = 0; ram[at + 7] = end ? 0x80 : 0;
    }
    void dma_start() {
        ide.io_write(0xff64, 4, 0x1000);
        ide.io_write(0xff60, 1, 9);
    }
    std::vector<uint8_t> sense() { Packet p{}; p[0] = 3; p[4] = 18; packet(p); return reply(); }
};
static Packet read(uint8_t op, uint32_t lba, uint32_t n) {
    Packet p{}; p[0] = op;
    for (unsigned i = 0; i < 4; ++i) p[2 + i] = uint8_t(lba >> (24 - i * 8));
    if (op == 0x28) { p[7] = uint8_t(n >> 8); p[8] = uint8_t(n); }
    else for (unsigned i = 0; i < 4; ++i) p[6 + i] = uint8_t(n >> (24 - i * 8));
    return p;
}
int main() {
    const auto path = std::filesystem::temp_directory_path() /
        ("xlle-dvd-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".iso");
    std::vector<uint8_t> source(72 * 2048);
    for (size_t i = 0; i < source.size(); ++i) source[i] = uint8_t((i / 2048) ^ i ^ (i >> 8));
    { std::ofstream f(path, std::ios::binary); f.write(reinterpret_cast<const char*>(source.data()), source.size()); check(bool(f), "create owned image fixture"); }
    {
        Fixture d;
        d.packet(Packet{});
        check(d.status() & 1, "empty tray is not ready");
        auto sense = d.sense(); check(sense[2] == 2 && sense[12] == 0x3a, "no-medium sense");
        check(d.ide.attach_dvd(path.string()), "attach immutable image");
        check(!d.ide.attach_dvd(path.string()), "cannot reattach under guest");
        xlle::XboxSmc smc([&](const std::string& s) { d.faults.push_back(s); });
        smc.set_tray_state_reader([&]() { return d.ide.dvd_tray_state(); });
        uint8_t tray = 0; check(smc.read_byte(3, tray) && tray == 0x60, "SMC sees real mounted drive");
        Packet p{}; p[0] = 0x25; d.packet(p, false, 2);
        check(d.reply() == std::vector<uint8_t>({0,0,0,71,0,0,8,0}), "capacity through four PIO phases");
        p = {}; p[0] = 0x12; p[4] = 17; d.packet(p);
        check(d.reply().size() == 17, "odd reply uses bus padding without hanging");
        d.packet(read(0x28, 31, 3), false, 514);
        const auto data = d.reply();
        check(data == std::vector<uint8_t>(source.begin() + 31 * 2048, source.begin() + 34 * 2048), "multi-sector PIO bytes and chunk boundaries");
        check(d.ide.dvd_sectors_read() == 3, "real sector counter");
        d.packet(read(0xa8, UINT32_MAX, 2));
        check(d.status() & 1, "overflowing LBA rejected");
        sense = d.sense(); check(sense[2] == 5 && sense[12] == 0x21, "range sense");
        d.packet(read(0x28, UINT32_MAX, 0));
        check(!(d.status() & 9) && d.irq, "zero-length read does no transfer");

        d.prd(0x1000, 0x2000, 1500, false); d.prd(0x1008, 0x8000, 4644, true);
        d.packet(read(0xa8, 30, 3), true);
        check(!d.irq && d.writes == 0, "no DMA completion before bus master starts");
        d.dma_start();
        check(d.irq && d.writes == 2 && !(d.status() & 9), "DMA completes after both PRD writes");
        check(std::memcmp(d.ram.data() + 0x2000, source.data() + 30 * 2048, 1500) == 0 &&
              std::memcmp(d.ram.data() + 0x8000, source.data() + 30 * 2048 + 1500, 4644) == 0, "READ12 scatter DMA bytes");
        check(d.ram[0x2000 + 1500] == 0xcc && d.ram[0x8000 + 4644] == 0xcc, "DMA write extents");
        check((d.ide.io_read(0xff62, 1) & 5) == 4, "bus master completed interrupt without active");
        d.ide.io_write(0xff60, 1, 0);
        p = {}; p[0] = 0x1e; p[4] = 1; d.packet(p);
        p[0] = 0x1b; p[4] = 2; d.packet(p); check(d.status() & 1, "locked tray cannot eject");
        p[0] = 0x1e; p[4] = 0; d.packet(p);
        p[0] = 0x1b; p[4] = 2; d.packet(p);
        check(smc.read_byte(3, tray) && tray == 0x10, "SMC sees open tray");
        d.packet(read(0x28, 0, 1)); check(d.status() & 1, "open tray cannot read backing image");
        p = {}; p[0] = 0x1b; p[4] = 3; d.packet(p);
        check(d.ide.dvd_media_present(), "load retained mounted image");
        check(d.faults.empty(), "supported transport has no faults");
        p = {}; p[0] = 0x5a; p[2] = 0x3e; p[8] = 32; d.packet(p);
        check(d.faults.size() == 1 && d.faults[0].find("authentication") != std::string::npos, "unimplemented auth stops explicitly");
        check(!d.irq, "missing authentication does not signal success");
        smc.write_byte(0x0c, 1);
        check(d.faults.size() == 2 && d.ide.dvd_media_present(), "unmodeled SMC motor cannot pretend to eject");
    }
    {
        Fixture d; check(d.ide.attach_dvd(path.string()), "second independent drive");
        d.packet(read(0xa8, 0, 65), true);
        check(d.faults.size() == 1 && !d.irq && d.writes == 0, "oversized unimplemented transfer cannot succeed");
    }
    { std::ifstream f(path, std::ios::binary); std::vector<uint8_t> after(source.size()); f.read(reinterpret_cast<char*>(after.data()), after.size()); check(after == source, "image remains unchanged"); }
    std::filesystem::remove(path);
    std::puts("DVD: real media, PIO phases, scatter DMA, completion, tray and explicit gaps passed");
}
