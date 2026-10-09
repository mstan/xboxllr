#include "machine/cx25871_control.hpp"
#include <cstdio>
#include <string>
#include <vector>

static int fails;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++fails; } } while (0)
int main() {
    for (unsigned lifetime=0; lifetime<3; ++lifetime) {
        std::string fault;
        std::vector<bool> edges;
        xlle::Amd756Smbus bus(0xc000,[&](const std::string& why){fault=why;});
        bus.add_slave(std::make_unique<xlle::Cx25871Control>([&](const std::string& why){fault=why;}));
        bus.set_irq([&](int irq,bool level){CHECK(irq==11);edges.push_back(level);},11);
        bus.io_write(0xc002,1,0x12); // interrupt enabled, no start
        bus.io_write(0xc000,2,0); // must not invent success or IRQ
        CHECK(bus.io_read(0xc000,2)==0 && edges.empty());
        auto write=[&](uint8_t reg,uint8_t data) {
            bus.io_write(0xc004,1,0x8a);bus.io_write(0xc008,1,reg);
            bus.io_write(0xc006,2,data);bus.io_write(0xc002,1,0x1a);
        };
        write(0x6c,0x46);
        CHECK(fault.empty() && bus.io_read(0xc000,2)==0x10);
        CHECK(bus.io_read(0xc002,1)==0x12 && edges.size()==1 && edges.back());
        bus.io_write(0xc000,1,0);CHECK(edges.size()==1 && bus.io_read(0xc000,1)==0x10);
        bus.io_write(0xc000,1,4);CHECK(edges.size()==1 && bus.io_read(0xc000,1)==0x10);
        // Accumulate a real NAK alongside completed status, then clear each
        // independently. No successful data transfer for an absent address.
        bus.io_write(0xc004,1,0xd4);bus.io_write(0xc002,1,0x1a);
        CHECK(bus.io_read(0xc000,1)==0x14 && fault.empty());
        bus.io_write(0xc000,1,0x10);CHECK(bus.io_read(0xc000,1)==4 && edges.size()==1);
        bus.io_write(0xc000,1,4);CHECK(bus.io_read(0xc000,1)==0 && edges.size()==2 && !edges.back());
        write(0xc8,0x9b);CHECK(fault.empty());
        bus.io_write(0xc000,1,0x10);
        bus.io_write(0xc004,1,0x8b);bus.io_write(0xc006,1,0xa5);bus.io_write(0xc002,1,0x1a);
        CHECK(bus.io_read(0xc006,1)==0x9b && bus.io_read(0xc000,1)==0x10);
        bus.io_write(0xc000,1,0x10);
        // Initialized standard readback still cannot supply uncaptured bytes.
        bus.io_write(0xc008,1,0x96);bus.io_write(0xc002,1,0x1a);
        CHECK(!fault.empty() && bus.io_read(0xc000,1)==4 && bus.io_read(0xc006,1)==0x9b);
        fault.clear();
        xlle::Cx25871Control unknown([&](const std::string& why){fault=why;});
        uint8_t untouched=0xa5;
        CHECK(!unknown.read_byte(0xc8,untouched) && untouched==0xa5 && !fault.empty());
        fault.clear();CHECK(!unknown.write_byte(0xb8,0) && !fault.empty());
        fault.clear();CHECK(!unknown.write_byte(0x6c,0xc6) && !fault.empty());
        fault.clear();CHECK(!unknown.write_byte(0x34,0x64) && !fault.empty());
    }
    std::printf("SMBus AV control/IRQ/unknown-state checks: %s\n",fails?"FAIL":"PASS");
    return fails?1:0;
}
