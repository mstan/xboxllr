// Authored protocol tests: expectations from ATA-5 table 9 / section 8.34.
#include "machine/ide.hpp"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

static void require(bool ok, const char* message) {
    if (!ok) { std::fprintf(stderr, "IDE security: %s\n", message); std::exit(1); }
}
using Config = xlle::HddSecurityConfig;
static Config credentials() {
    Config c;
    c.mode = Config::Mode::Locked;
    for (unsigned i = 0; i < 32; ++i) { c.user[i] = i; c.master[i] = i + 80; }
    return c;
}
struct Drive {
    std::vector<std::string> faults;
    bool irq = false;
    unsigned memory_calls = 0;
    xlle::IdeController ide{[this](const std::string& m) { faults.push_back(m); }};
    Drive(const std::string& path, const Config& c) {
        require(ide.configure_hdd_security(c), "configure before attach");
        require(ide.attach_hdd(path), "attach fixture");
        require(!ide.configure_hdd_security(c), "no changing policy after attach");
        ide.set_irq([this](int n, bool level) { require(n == 14, "IRQ14"); irq = level; }, 14);
        ide.set_dma_mem([this](uint32_t, void*, size_t) { ++memory_calls; return false; },
                        [this](uint32_t, const void*, size_t) { ++memory_calls; return false; });
    }
    void cmd(uint8_t c) { ide.io_write(0x1f7, 1, c); }
    void ack() { (void)ide.io_read(0x1f7, 1); }
    void srst() { ide.io_write(0x3f6, 1, 4); ide.io_write(0x3f6, 1, 0); }
    void aborted() {
        require((ide.io_read(0x3f6, 1) & 0x89) == 1, "ABRT without BSY/DRQ");
        require(ide.io_read(0x1f1, 1) == 4, "ABRT error register");
        require(irq, "completion interrupt on hardware rejection");
        ack(); require(!irq, "status read acknowledges interrupt");
    }
    std::array<uint16_t, 256> identify() {
        cmd(0xec);
        require((ide.io_read(0x3f6, 1) & 8) != 0, "IDENTIFY DRQ");
        std::array<uint16_t, 256> words{};
        for (auto& w : words) w = static_cast<uint16_t>(ide.io_read(0x1f0, 2));
        ack(); return words;
    }
    void state(uint16_t expected) {
        const auto id = identify();
        require(id[128] == expected, "IDENTIFY security state");
        require((id[82] & 2) == 2, "security supported");
        require((id[85] & 2) == (expected & 2), "security enabled matches word128");
        // Compare against independent protocol expectations, not just cached data.
        require(ide.hdd_security_status() == expected, "diagnostic agrees with guest state");
    }
    void payload(const std::array<uint8_t, 32>& password, bool master = false, unsigned words = 256) {
        std::array<uint8_t, 512> p{};
        p[0] = master ? 1 : 0;
        for (unsigned i = 0; i < 32; ++i) p[i + 2] = password[i];
        for (unsigned i = 0; i < words; ++i)
            ide.io_write(0x1f0, 2, p[2*i] | (uint32_t(p[2*i+1]) << 8));
    }
    void unlock(const std::array<uint8_t, 32>& password, bool master = false) {
        const auto interrupts = ide.irq_pulses();
        cmd(0xf2);
        require((ide.io_read(0x3f6, 1) & 8) != 0, "UNLOCK requests parameter sector");
        require(ide.irq_pulses() == interrupts, "no premature unlock completion");
        payload(password, master);
        require(ide.irq_pulses() == interrupts + 1, "exactly one unlock completion");
    }
    void lba0() {
        ide.io_write(0x1f6, 1, 0xe0); ide.io_write(0x1f2, 1, 1);
        ide.io_write(0x1f3, 1, 0); ide.io_write(0x1f4, 1, 0); ide.io_write(0x1f5, 1, 0);
    }
    void read_and_write() {
        lba0(); cmd(0x20);
        require((ide.io_read(0x3f6, 1) & 9) == 8, "unlocked read DRQ");
        for (unsigned i = 0; i < 256; ++i) require(ide.io_read(0x1f0, 2) == 0, "blank fixture read");
        ack(); lba0(); cmd(0x30);
        for (unsigned i = 0; i < 256; ++i) ide.io_write(0x1f0, 2, 0x1234);
        ack(); lba0(); cmd(0x20);
        for (unsigned i = 0; i < 256; ++i) require(ide.io_read(0x1f0, 2) == 0x1234, "read guest write");
        ack(); require(ide.overlay_sectors() == 1 && !ide.hdd_writeback(), "COW preserved");
    }
};

int main() {
    const auto path = std::filesystem::absolute("ide-security-test.img");
    require(!std::filesystem::exists(path), "refuse to overwrite existing fixture");
    { std::ofstream out(path, std::ios::binary); std::array<char, 8192> blank{};
      out.write(blank.data(), blank.size()); require(out.good(), "create fixture"); }
    struct Cleanup { std::filesystem::path p; ~Cleanup() { std::filesystem::remove(p); } } cleanup{path};
    const auto c = credentials();
    {
        Drive d(path.string(), c); d.state(7);
        // All currently implemented media read/write/verify/flush families.
        for (uint8_t cmd : {0x20,0x21,0x24,0x29,0x30,0x31,0x34,0x39,0x40,0x41,0x42,
                           0xc4,0xc5,0xc8,0xc9,0x25,0xca,0xcb,0x35,0xe7,0xea,0xf1,0xf5,0xf6}) {
            d.lba0(); d.cmd(cmd); d.aborted(); d.state(7);
        }
        d.ide.io_write(0xff60, 1, 9); // bus master cannot run rejected DMA commands
        require(d.memory_calls == 0 && d.ide.sectors_read() == 0 && d.ide.sectors_written() == 0,
                "locked I/O never touches media or guest DMA memory");
        d.ide.io_write(0xff60, 1, 0);
        d.unlock(c.user); require((d.ide.io_read(0x3f6, 1) & 9) == 0, "correct user accepted");
        d.ack(); d.state(3); d.read_and_write();
        d.cmd(0xf5); d.ack(); d.state(11); // enabled/unlocked/frozen
        d.cmd(0xf5); d.ack(); d.state(11); // idempotent freeze
        for (uint8_t cmd : {0xf1,0xf2,0xf3,0xf4,0xf6}) { d.cmd(cmd); d.aborted(); }
        d.srst(); d.state(11); // software reset must not thaw/relock
        d.ide.hardware_reset(); d.state(7); // hardware reset thaws and relocks
        require(d.faults.empty(), "implemented states do not fault-stop");
    }
    {
        Drive d(path.string(), c);
        // Every one of the 32 bytes participates, including the padded tail.
        for (unsigned byte = 0; byte < 32; ++byte) {
            auto wrong = c.user; wrong[byte] ^= 1;
            d.unlock(wrong); d.aborted(); d.state(7); d.ide.hardware_reset();
        }
        auto wrong = c.user; wrong[0] ^= 1;
        for (unsigned i = 0; i < 5; ++i) { d.unlock(wrong); d.aborted(); }
        d.state(23); d.cmd(0xf2); d.aborted(); d.cmd(0xf4); d.aborted();
        d.srst(); d.state(23); d.cmd(0xf2); d.aborted();
        d.ide.hardware_reset(); d.state(7); d.unlock(c.user); d.ack(); d.state(3);
        for (unsigned i = 0; i < 6; ++i) { d.unlock(wrong); d.aborted(); }
        d.state(3); // unlocked failures do not exhaust the counter
        require(d.faults.empty(), "bad passwords are hardware rejection, not unmodeled");
    }
    {
        Drive d(path.string(), c); d.unlock(c.user, true); d.aborted(); d.state(7);
        d.unlock(c.master, true); d.ack(); d.state(3);
        d.ide.hardware_reset(); d.cmd(0xf2); d.payload(c.user, false, 255);
        require(!d.irq, "partial password block cannot complete");
        d.srst(); d.state(7); d.payload(c.user); d.state(7);
        require(d.faults.empty(), "master / partial block tests");
    }
    {
        auto maximum = c; maximum.maximum = true;
        Drive d(path.string(), maximum); d.state(263);
        for (unsigned i = 0; i < 6; ++i) { d.unlock(c.master, true); d.aborted(); }
        d.state(263); d.unlock(c.user); d.ack(); d.state(259);
    }
    {
        Config disabled; disabled.mode = Config::Mode::Disabled;
        Drive d(path.string(), disabled); d.state(1); d.read_and_write();
        d.cmd(0xf5); d.ack(); d.state(9); d.srst(); d.state(9);
        d.ide.hardware_reset(); d.state(1);
        d.cmd(0xf2); require(d.faults.size() == 1, "unprovisioned disabled-drive auth stops");
        d.state(1);
    }
    // Bounded storage-write floor: gather both PRDs into real disk storage
    // before completion, then read those bytes through the ATA data port.
    for (bool fail_second : {false,true}) {
        Config disabled;disabled.mode=Config::Mode::Disabled;
        Drive d(path.string(),disabled);std::vector<uint8_t> ram(0x5000,0xcc);
        auto put32=[&](unsigned at,uint32_t value){std::memcpy(ram.data()+at,&value,4);};
        put32(0x1000,0x2000);put32(0x1004,300);
        put32(0x1008,0x3000);put32(0x100c,0x80000000u|724);
        std::array<uint8_t,1024> expected{};
        for(unsigned i=0;i<expected.size();++i)expected[i]=uint8_t(i*29+(i>>8));
        std::memcpy(ram.data()+0x2000,expected.data(),300);
        std::memcpy(ram.data()+0x3000,expected.data()+300,724);
        unsigned chunks=0;
        d.ide.set_dma_mem([&](uint32_t at,void* out,size_t n){
            require(!d.irq && !d.ide.sectors_written(),"DMA cannot signal before gathering and storing data");
            if(at==0x2000 || at==0x3000){++chunks;if(fail_second && at==0x3000)return false;}
            if(uint64_t(at)+n>ram.size())return false;
            std::memcpy(out,ram.data()+at,n);return true;
        },[](uint32_t,const void*,size_t){return false;});
        d.lba0();d.ide.io_write(0x1f2,1,2);d.ide.io_write(0x1f3,1,2);d.cmd(0xca);
        require(!d.irq && !d.ide.sectors_written(),"armed write is not completed");
        d.ide.io_write(0xff64,4,0x1000);d.ide.io_write(0xff60,1,1);
        require(chunks==2,"both scatter fragments consumed");
        if(fail_second) {
            require(!d.irq && !d.ide.sectors_written() && !d.ide.overlay_sectors() && d.faults.size()==1,
                    "failed gather cannot invent success or modify media");
        } else {
            require(d.irq && d.ide.sectors_written()==2 && d.ide.overlay_sectors()==2 && d.faults.empty(),
                    "write completion follows actual two-sector storage");
            d.ack();d.ide.io_write(0xff60,1,0);
            d.lba0();d.ide.io_write(0x1f2,1,2);d.ide.io_write(0x1f3,1,2);d.cmd(0x20);
            for(unsigned i=0;i<512;++i)
                require(d.ide.io_read(0x1f0,2)==(expected[2*i]|uint32_t(expected[2*i+1])<<8),"independent PIO readback of DMA write");
            d.ack();
        }
    }
    {
        Drive d(path.string(), Config{}); d.state(7); d.cmd(0xf2);
        require(d.faults.size() == 1 && d.faults[0].find("unmodeled") != std::string::npos,
                "missing credentials explicitly stop, never trust guest");
        require(!d.irq, "no fabricated authentication completion"); d.state(7);
    }
    {
        Drive d(path.string(), c); d.ide.io_write(0x3f6, 1, 2); // nIEN
        d.unlock(c.user); require(!d.irq, "masked completion does not assert PIC"); d.state(3);
        d.ide.io_write(0x3f6, 1, 0); d.ide.io_write(0x1f6, 1, 0xb0);
        d.cmd(0xf2); d.aborted(); d.cmd(0xf5); d.aborted();
        require(d.faults.empty(), "HDD security commands reject on ATAPI drive");
    }
    { std::ifstream in(path, std::ios::binary); std::array<char, 8192> bytes{};
      in.read(bytes.data(), bytes.size());
      for (char b : bytes) require(b == 0, "backing file untouched"); }
    std::puts("IDE: provisioned authentication, lockout, freeze, reset, protected I/O and unlocked policy pass");
}
