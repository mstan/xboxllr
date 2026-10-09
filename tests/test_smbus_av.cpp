#include "machine/cx25871_control.hpp"
#include "machine/mcpx_av_outputs.hpp"
#include <cstdio>
#include <string>
#include <vector>

static int fails;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++fails; } } while (0)

static void av_output_pins() {
    using Status=xlle::McpxAvOutputs::ScartStatus;
    for(unsigned lifetime=0;lifetime<3;++lifetime) {
        std::string fault;
        xlle::McpxAvOutputs outputs([&](const std::string& why){fault=why;});
        CHECK(outputs.known()==0 && outputs.scart_status()==Status::Unknown);
        CHECK(outputs.write(0x80d3,5));
        CHECK(outputs.known()==(1u<<19) && outputs.scart_status()==Status::Unknown);
        // Independently enumerated connector truth table, specification fig55.
        const struct {unsigned pin22,pin19;Status status;} rows[]={
            {0,0,Status::Standard},{1,0,Status::Widescreen},
            {1,1,Status::Television},{0,1,Status::Undefined}};
        for(unsigned repeat=0;repeat<3;++repeat) for(const auto& row:rows) {
            CHECK(outputs.write(0x80d6,4+row.pin22));
            CHECK(outputs.write(0x80d3,4+row.pin19));
            CHECK(outputs.scart_status()==row.status);
            for(unsigned unused=0;unused<2;++unused) {
                CHECK(outputs.write(0x80d8,4+unused));
                CHECK(outputs.scart_status()==row.status);
                CHECK(outputs.known()==((1u<<19)|(1u<<22)|(1u<<24)));
                CHECK(outputs.levels()==((row.pin19<<19)|(row.pin22<<22)|(unused<<24)));
            }
        }
        const auto known=outputs.known(),high=outputs.levels();
        for(unsigned value:{0u,1u,6u,7u,0x45u,0x105u}) {
            CHECK(!outputs.write(0x80d3,value) && !fault.empty());fault.clear();
            CHECK(outputs.known()==known && outputs.levels()==high);
        }
        CHECK(!outputs.write(0x80d5,4) && !fault.empty());
        CHECK(outputs.known()==known && outputs.levels()==high);
    }
}

static void reset_and_configuration() {
    for(bool pal : {false,true}) for(unsigned lifetime=0;lifetime<3;++lifetime) {
        std::string fault;
        xlle::Cx25871Control encoder([&](const std::string& why){fault=why;},pal);
        uint8_t value=0;
        auto read=[&](uint8_t reg) {
            value=0xa5;CHECK(encoder.read_byte(reg,value));return value;
        };
        auto write=[&](uint8_t reg,uint8_t data){CHECK(encoder.write_byte(reg,data));};
        write(0x6c,0x46);write(0x34,0x80);write(0x36,0xa4);write(0xc8,0x9b);
        write(0x96,0xcd);write(0xd4,0xff);write(0xba,0x3f);
        for(unsigned reset=0;reset<2;++reset) {
            write(0xba,0x80);
            value=0xa5;
            CHECK(!encoder.read_byte(0xc8,value) && value==0xa5); // readback disabled
            fault.clear();write(0x6c,0x46);
            CHECK(read(0x34)==0 && read(0x36)==0 && read(0xc8)==0);
            CHECK(read(0x96)==0x36 && read(0xb8)==unsigned(pal));
            // Independent table C-1 dimensions: 640x480 input, with the
            // specified total clocks/lines. Decode separate split registers.
            CHECK((unsigned(read(0x8a)) | ((read(0x8e)&7u)<<8))==(pal?944u:784u));
            CHECK((unsigned(read(0x90)) | ((read(0x96)&3u)<<8) | ((read(0x38)&2u)<<9))==(pal?625u:600u));
            CHECK((unsigned(read(0x94)) | ((read(0x96)&12u)<<6))==480);
            // Neither a disputed monitor-command default nor unknown caption
            // fields may be laundered into a successful read by software reset.
            value=0xa5;CHECK(!encoder.read_byte(0xba,value) && value==0xa5);fault.clear();
            CHECK(!encoder.read_byte(0xd4,value) && value==0xa5);fault.clear();
            write(0xba,0x3f);CHECK(read(0xba)==0x3f);
            write(0x34,0x80);write(0x36,0xa4);write(0xc8,0x9b);
            write(0xd4,0x95);write(0x38,0x13);CHECK(read(0xd4)==0xd5);
            write(0xd4,0x95);CHECK(read(0x38)==3); // preserve unrelated bits
            write(0x38,0x13);
            write(0xb8,pal?0:1);
            CHECK(read(0xb8)==unsigned(!pal) && read(0x38)==0 && read(0xd4)==0x95);
            CHECK(read(0x34)==0x80 && read(0x36)==0xa4 && read(0xc8)==0x9b);
            CHECK(read(0x6c)==0x46 && read(0xba)==0x3f); // autoconfig is not reset
            CHECK((unsigned(read(0x8a)) | ((read(0x8e)&7u)<<8))==(pal?784u:944u));
            CHECK(!encoder.write_byte(0xb8,2));fault.clear();
            CHECK(!encoder.write_byte(0x3a,0x10));fault.clear();
            CHECK(!encoder.write_byte(0xba,0xbf));fault.clear();
            CHECK(!encoder.write_byte(0x6c,0xc6));fault.clear(); // timing still guarded
            CHECK(read(0xb8)==unsigned(!pal) && read(0x3a)==0 && read(0xba)==0x3f);
            CHECK(read(0x6c)==0x46 && read(0xc8)==0x9b && fault.empty());
        }
    }
}

int main() {
    av_output_pins();
    reset_and_configuration();
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
