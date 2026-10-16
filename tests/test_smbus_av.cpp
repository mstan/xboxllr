#include "machine/cx25871_control.hpp"
#include "machine/mcpx_av_outputs.hpp"
#include "machine/nv2a_fp_timing.hpp"
#include <cstdio>
#include <string>
#include <vector>

static int fails;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++fails; } } while (0)

static void encoder_timing() {
    using State=xlle::Cx25871Timing::State;
    // These expectations use the independent table C-1 totals and the
    // datasheet's odd/even/two-clock FIELD and color-frame reset rules.
    for(bool pal:{false,true}) for(bool slave:{false,true}) for(unsigned lifetime=0;lifetime<3;++lifetime) {
        std::string fault;
        xlle::Cx25871Control encoder([&](const std::string& why){fault=why;},pal);
        State state;state.clocks=99;
        CHECK(!encoder.timing_state(state) && state.clocks==99 && encoder.field_signal()==-1);
        auto write=[&](uint8_t r,uint8_t v){CHECK(encoder.write_byte(r,v));};
        write(0xba,0x80);write(0xba,slave?0x20:0);write(0xc6,0x9c);write(0x6c,0x46);
        CHECK(encoder.field_signal()==-2);
        write(0x6c,0xc6);
        uint8_t control=0;CHECK(encoder.read_byte(0x6c,control) && control==0x46);
        CHECK(encoder.timing_state(state) && state.clocks==0 && state.subcarrier_phase==0);
        write(0xc4,1);CHECK(encoder.field_signal()==0);
        const uint32_t h=pal?944:784, v=pal?625:600, hout=pal?1888:1792;
        const uint64_t field=uint64_t(h)*v;
        const uint32_t increment=pal?0x26798c0c:0x20800000;
        uint32_t spent=0;
        for(unsigned frame=1;frame<=(pal?8u:4u);++frame) {
            if(slave) {
                for(unsigned line=1;line<=v;++line) {
                    CHECK(encoder.advance_timing(h-spent));spent=0;
                    CHECK(encoder.input_sync(line==v));
                }
            } else CHECK(encoder.advance_timing(field-spent));
            CHECK(encoder.timing_state(state) && state.clocks==field*frame);
            CHECK(state.input_x==0 && state.input_y==0);
            CHECK(state.output_x==((frame&1)?hout/2:0));
            CHECK(state.output_y==((frame&1)?(pal?312u:262u):0));
            CHECK(encoder.field_signal()==int((frame-1)&1)); // two-clock delay
            if(frame==(pal?8u:4u))CHECK(state.subcarrier_phase==0);
            else CHECK(state.subcarrier_phase==uint32_t(uint64_t(uint32_t(field*frame))*increment));
            CHECK(encoder.advance_timing(1));CHECK(encoder.field_signal()==int((frame-1)&1));
            CHECK(encoder.advance_timing(1));CHECK(encoder.field_signal()==int(frame&1));spent=2;
        }
        write(0xc6,0xbc);CHECK(encoder.field_signal()==1); // invert odd, no time advance
        write(0xc4,0);CHECK(encoder.field_signal()==-2);
        write(0xc4,1);CHECK(encoder.field_signal()==1);
        write(0x6c,0xc6);CHECK(encoder.timing_state(state));
        CHECK(state.clocks==0 && state.input_x==0 && state.input_y==0 &&
              state.output_x==0 && state.output_y==0 && state.subcarrier_phase==0);
        CHECK(encoder.field_signal()==1); // reset keeps FIELDI, selects odd
        CHECK(encoder.advance_timing(1));
        CHECK(!encoder.advance_timing(UINT64_MAX) && !fault.empty());fault.clear();
        CHECK(encoder.timing_state(state) && state.clocks==1);
        write(0x6c,0xc6);
        if(slave) {
            CHECK(!encoder.advance_timing(h+1) && !fault.empty());fault.clear();
            CHECK(encoder.timing_state(state) && state.clocks==0);
            CHECK(encoder.input_sync(true)); // a real first edge can coincide with reset
            CHECK(!encoder.input_sync(false) && !fault.empty());fault.clear();
            CHECK(encoder.advance_timing(h));
            CHECK(!encoder.input_sync(true) && !fault.empty());fault.clear();
            CHECK(encoder.input_sync(false)); // rejected edge did not consume it
        }
        write(0xba,0x80);
        CHECK(!encoder.timing_state(state) && encoder.field_signal()==-2);
        CHECK(fault.empty());
    }
    {
        std::string fault;
        xlle::Cx25871Control encoder([&](const std::string& why){fault=why;});
        auto write=[&](uint8_t r,uint8_t v){CHECK(encoder.write_byte(r,v));};
        write(0xba,0x80);write(0xba,0);write(0xc6,0x9c);
        write(0xa2,0x1a); // disable automatic color-frame subcarrier reset
        write(0xae,1); // deliberate phase residue distinguishes reset/no-reset
        write(0x6c,0xc6);
        CHECK(encoder.advance_timing(4ull*784*600));State state;
        CHECK(encoder.timing_state(state) && state.subcarrier_phase!=0);
        const uint32_t phase=state.subcarrier_phase;
        write(0x6c,0xc6);CHECK(encoder.timing_state(state) && state.subcarrier_phase==0 && phase!=0);
        write(0x88,1);CHECK(!encoder.timing_state(state)); // unsupported fractional line
        CHECK(!encoder.write_byte(0x6c,0xc6) && !fault.empty());fault.clear();
        CHECK(!encoder.timing_state(state));
    }
}

static void encoder_slave_phase() {
    using Timing=xlle::Cx25871Timing;
    // Specification-derived event schedules, independent of the model's
    // counters: reset can occur between HSYNCs and before any input line.
    for(bool pal:{false,true})for(bool free_color:{false,true}) {
        const uint32_t h=pal?944:784,v=pal?625:600,hout=pal?1888:1792;
        for(uint32_t first_h:{0u,1u,h/2,h-1})for(uint32_t lead:{0u,2u,h-2}) {
            std::string fault;Timing timing([&](const std::string& why){fault=why;});
            Timing::Config config{h,v,hout,pal?625u:525u,0x20800001,pal,free_color,true};
            for(unsigned lifetime=0;lifetime<2;++lifetime) {
                CHECK(timing.reset(config));uint64_t now=0;
                auto advance_to=[&](uint64_t target) {
                    CHECK(target>=now && timing.advance(target-now));now=target;
                };
                unsigned frames=0;
                for(unsigned line=0;line<=7+3*v;++line) {
                    const uint64_t edge=first_h+uint64_t(line)*h;
                    const bool frame=line>=7 && (line-7)%v==0;
                    if(frame && lead) {
                        advance_to(edge-lead);CHECK(timing.vertical_sync());
                        Timing::State before;CHECK(timing.state(before));
                        CHECK(before.field_even==bool(frames&1));
                    }
                    advance_to(edge);CHECK(timing.sync(frame && !lead));
                    Timing::State state;CHECK(timing.state(state));
                    CHECK(state.input_x==0 && state.input_y==(line<7?line+1:(line-7)%v));
                    CHECK(state.field_even==bool(frames&1));
                    if(frame) {
                        ++frames;
                        CHECK(state.output_phase_known==(frames>=2));
                        CHECK(state.subcarrier_phase_known==free_color);
                        if(frames>=2) {
                            CHECK(state.output_x==((frames&1)?hout/2:0));
                            CHECK(state.output_y==((frames&1)?(pal?312u:262u):0));
                        }
                        CHECK(timing.advance(1));++now;CHECK(timing.state(state));
                        CHECK(state.field_even==bool((frames-1)&1));
                        CHECK(timing.advance(1));++now;CHECK(timing.state(state));
                        CHECK(state.field_even==bool(frames&1) && state.field_transitions==frames);
                        if(free_color)CHECK(state.subcarrier_phase==uint32_t(uint32_t(now)*config.subcarrier_increment));
                    }
                }
                CHECK(frames==4 && fault.empty());
            }
        }
    }
    // Two real components connected without resetting the GPU to align the
    // encoder. These are explicit active-mode fixture origins, not inferred
    // guest mode-activation/FD6806A0 semantics or a replacement title seed.
    for(unsigned origin:{0u,123u,776u*500+700}) {
        std::string fault;auto fail=[&](const std::string& why){fault=why;};
        xlle::Nv2aFpTiming gpu(fail);Timing encoder(fail);
        xlle::Nv2aFpTiming::Mode mode{{639,775,653,685,0,639},{479,524,490,493,0,479},0x10100111};
        CHECK(gpu.establish(mode,origin%776,origin/776));
        const Timing::Config config{776,525,1552,525,0x12345678,false,true,true};
        xlle::Nv2aFpTiming::State prior;CHECK(gpu.state(prior));
        for(unsigned reset=0;reset<2;++reset) {
            CHECK(encoder.reset(config));
            uint64_t last_clock=prior.clocks;unsigned frames=0;
            bool vertical_pending=false;
            auto edge=[&](const xlle::Nv2aFpTiming::State& next) {
                CHECK(encoder.advance(next.clocks-last_clock));last_clock=next.clocks;
                if(next.vsync_active && !prior.vsync_active) {
                    CHECK(encoder.vertical_sync());vertical_pending=true;
                }
                if(next.hsync_active && !prior.hsync_active) {
                    CHECK(encoder.sync(false));
                    if(vertical_pending) { ++frames;vertical_pending=false; }
                }
                prior=next;
            };
            CHECK(gpu.advance(4ull*776*525+37,edge));
            xlle::Nv2aFpTiming::State end;CHECK(gpu.state(end));
            CHECK(encoder.advance(end.clocks-last_clock));prior=end;
            Timing::State state;CHECK(encoder.state(state));
            CHECK(frames>=4 && state.field_transitions==frames &&
                  state.field_even==bool(frames&1) && state.output_phase_known && state.subcarrier_phase_known);
            CHECK(fault.empty());
        }
    }
}

static void encoder_clocks() {
    using Encoder=xlle::Cx25871Control;
    const auto equals=[](Encoder::Rate r,uint64_t n,uint64_t d=1) {
        return r.numerator*d==n*r.denominator;
    };
    for(bool pal:{false,true}) for(unsigned lifetime=0;lifetime<3;++lifetime) {
        std::string fault;
        Encoder encoder([&](const std::string& why){fault=why;},pal);
        Encoder::Clocks clocks;
        clocks.encoder={123,7};
        CHECK(!encoder.clocks(clocks) && !fault.empty());fault.clear();
        CHECK(equals(clocks.encoder,123,7)); // unknown warm state is not zero Hz
        const auto write=[&](uint8_t reg,uint8_t value){CHECK(encoder.write_byte(reg,value));};
        write(0xba,0x80);
        CHECK(encoder.clocks(clocks) && !clocks.clko_driven);
        // Independent rational evaluation of table C-1's PLL equation, not
        // rates obtained from this model. PAL ~29.500 MHz; NTSC ~28.196 MHz.
        const uint64_t numerator=pal?60416015625ull:57744984375ull;
        CHECK(equals(clocks.encoder,numerator,2048));
        CHECK(equals(clocks.input,numerator,2048) && equals(clocks.clko,numerator,2048));
        write(0xc4,1);CHECK(encoder.clocks(clocks) && clocks.clko_driven);
        write(0x3a,8);CHECK(encoder.clocks(clocks)); // HALF_CLKO is pin-only
        CHECK(equals(clocks.clko,numerator,4096) && equals(clocks.input,numerator,2048));
        write(0x3a,4);CHECK(encoder.clocks(clocks)); // PLL divisor 6 -> 10
        CHECK(equals(clocks.encoder,numerator*3,10240));
        write(0x3a,0);write(0x38,0x10);CHECK(encoder.clocks(clocks));
        CHECK(equals(clocks.input,numerator,4096) && equals(clocks.encoder,numerator,2048));
        write(0x38,0x20);CHECK(encoder.clocks(clocks)); // 3:2 input/output clocks
        CHECK(equals(clocks.encoder,numerator,3072));
        CHECK(equals(clocks.input,numerator,2048) && equals(clocks.clko,numerator,2048));
        write(0x38,0);write(0xa0,0x40);CHECK(encoder.clocks(clocks));
        CHECK(equals(clocks.encoder,13500000) && clocks.clko_driven); // BY_PLL
        write(0xa0,0x80);
        CHECK(!encoder.clocks(clocks) && !fault.empty());fault.clear();
        CHECK(equals(clocks.encoder,13500000)); // failed query leaves output intact
        encoder.set_clki_hz(27000000);CHECK(encoder.clocks(clocks));
        CHECK(equals(clocks.encoder,27000000) && equals(clocks.input,27000000));
        CHECK(!clocks.clko_driven); // EN_XCLK disables CLKO regardless of EN_OUT
        write(0xa0,0x40);write(0xd4,0x20);CHECK(encoder.clocks(clocks));
        CHECK(equals(clocks.encoder,13500000) && equals(clocks.input,27000000));
        write(0xd4,0x60);CHECK(encoder.clocks(clocks));
        CHECK(equals(clocks.input,13500000)); // DIV2 aliases D4 bit 6
        write(0xd4,0x80);CHECK(!encoder.clocks(clocks) && !fault.empty());fault.clear();
        write(0xd4,0);write(0xa0,0xc0);
        CHECK(!encoder.clocks(clocks) && !fault.empty());fault.clear();
        write(0xa0,0);write(0x9c,0);write(0x9e,0);
        CHECK(!encoder.clocks(clocks) && !fault.empty());fault.clear();
        write(0xc4,0x40); // ESTATUS is a writable readback selector, not a clock mode
        CHECK(!encoder.write_byte(0xc4,2) && !fault.empty());fault.clear(); // DCHROMA remains guarded
        write(0xba,0x80);CHECK(encoder.clocks(clocks));
        CHECK(!clocks.clko_driven && equals(clocks.encoder,numerator,2048));
        CHECK(fault.empty());
    }
}

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

static void retail_fw_enable() {
    for(unsigned lifetime=0;lifetime<3;++lifetime) {
        std::string fault;
        xlle::McpxStaticOutputs output(1u<<15,[&](const std::string& why){fault=why;});
        CHECK(output.known()==0 && output.levels()==0);
        for(unsigned repeat=0;repeat<12;++repeat) {
            CHECK(output.write(0x80cf,5));
            CHECK(output.known()==(1u<<15) && output.levels()==(1u<<15));
            CHECK(output.write(0x80cf,4));
            CHECK(output.known()==(1u<<15) && output.levels()==0);
        }
        for(unsigned value=0;value<256;++value)if(value!=4 && value!=5) {
            CHECK(!output.write(0x80cf,value) && !fault.empty());fault.clear();
            CHECK(output.known()==(1u<<15) && output.levels()==0);
        }
        for(unsigned port:{0x80ceu,0x80d0u,0x80d3u,0x80bfu,0x80dau,0xffffu}) {
            CHECK(!output.write(uint16_t(port),5) && !fault.empty());fault.clear();
            CHECK(output.known()==(1u<<15) && output.levels()==0);
        }
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

static void documented_encoder_controls() {
    // Independently listed fields from Conexant 100381B tables 2-1/2-5;
    // values are the ordinary control profile reached by original AV code.
    // This checks register transport, not analog pixels or a warm-state seed.
    const uint8_t controls[][2] = {
        {0x2e,0}, {0x30,0}, {0x32,0x28}, {0x3c,0x80}, {0x3e,0x80}, {0x40,0x80},
        {0x5a,0xff}, {0x5c,1}, {0x60,0}, {0x62,0}, {0x64,0}, {0x66,0x15},
        {0x68,0x4b}, {0x6a,0}, {0x6e,0}, {0x70,0x0f}, {0x74,1}, {0xb6,0x5a},
        {0xca,0xc0}, {0xcc,0xc0}, {0xce,0x19}, {0xd6,0}, {0xd8,0x40}
    };
    for (unsigned life=0; life<3; ++life) {
        std::string fault;
        xlle::Cx25871Control encoder([&](const std::string& why){fault=why;});
        CHECK(encoder.write_byte(0x6c,0x46));
        for (const auto& entry:controls) {
            uint8_t value=0xa5;
            CHECK(!encoder.read_byte(entry[0],value) && value==0xa5);
            fault.clear();
            CHECK(encoder.write_byte(entry[0],entry[1]));
            CHECK(encoder.read_byte(entry[0],value) && value==entry[1]);
        }
        CHECK(fault.empty());
        // Rejected commands preserve both the prior latch and destinations.
        const uint8_t rejected[][2] = {
            {0x2e,0x80},{0x30,1},{0x32,0x80},{0x60,0x40},{0x60,0x10},
            {0x6a,0x10},{0x70,0},{0x70,1},{0x74,8},{0x74,0},{0xca,0x38},{0xcc,0x38},
            {0xd6,4},{0xd6,0x40}
        };
        for (const auto& entry:rejected) {
            uint8_t prior=0,after=0xa5;
            CHECK(encoder.read_byte(entry[0],prior));
            CHECK(!encoder.write_byte(entry[0],entry[1]) && !fault.empty());
            fault.clear();
            CHECK(encoder.read_byte(entry[0],after) && after==prior);
        }
        CHECK(encoder.write_byte(0x72,0));
        uint8_t reserved=0xa5;
        CHECK(!encoder.read_byte(0x72,reserved) && reserved==0xa5);fault.clear();
        CHECK(!encoder.write_byte(0x72,1) && !fault.empty());fault.clear();
        for (uint8_t reg:{0x01,0x03,0x05,0x31,0xdb,0xfe}) {
            uint8_t value=0xa5;
            CHECK(!encoder.write_byte(reg,0) && !fault.empty());fault.clear();
            CHECK(!encoder.read_byte(reg,value) && value==0xa5);fault.clear();
        }
        CHECK(encoder.write_byte(0xba,0x80));
        CHECK(encoder.write_byte(0x6c,0x46));
        // Reset discards the programmed latches. No unproved defaults survive.
        for (const auto& entry:controls) {
            uint8_t value=0xa5;
            CHECK(!encoder.read_byte(entry[0],value) && value==0xa5);fault.clear();
        }
        CHECK(encoder.write_byte(0xba,0));CHECK(encoder.write_byte(0xc6,0x9c));
        CHECK(encoder.write_byte(0x6c,0xc6));CHECK(encoder.advance_timing(19));
        xlle::Cx25871Timing::State state;
        CHECK(encoder.timing_state(state) && state.clocks==19);
        CHECK(encoder.write_byte(0x72,0));
        CHECK(encoder.timing_state(state) && state.clocks==19);
        CHECK(!encoder.write_byte(0x32,0x80));fault.clear();
        CHECK(encoder.timing_state(state) && state.clocks==19);
        CHECK(encoder.write_byte(0x32,0x28));
        CHECK(!encoder.timing_state(state)); // changed profile needs a real reset
        CHECK(encoder.write_byte(0x6c,0xc6));
        CHECK(encoder.timing_state(state) && state.clocks==0 && fault.empty());
        // Table 2-5 defines signed HSYNOFFSET[9:0] at 70[7:6]/6E.
        // Section 1.3.31 relates FIELD to that digital HSYNC edge. A master
        // offset cannot inherit the zero-offset phase; slave sync is external.
        CHECK(encoder.write_byte(0x6e,1));CHECK(encoder.write_byte(0x70,0x0f));
        CHECK(!encoder.write_byte(0x6c,0xc6) && !fault.empty());fault.clear();
        CHECK(!encoder.timing_state(state));
        CHECK(encoder.write_byte(0x6e,0));CHECK(encoder.write_byte(0x70,0x4f));
        CHECK(!encoder.write_byte(0x6c,0xc6) && !fault.empty());fault.clear();
        CHECK(!encoder.timing_state(state));
        CHECK(encoder.write_byte(0x70,0x0f));CHECK(encoder.write_byte(0x6c,0xc6));
        CHECK(encoder.timing_state(state) && state.clocks==0);
        CHECK(encoder.write_byte(0xba,0x20));
        CHECK(encoder.write_byte(0x6e,1));CHECK(encoder.write_byte(0x70,0x4f));
        CHECK(encoder.write_byte(0x6c,0xc6));
        CHECK(encoder.timing_state(state) && state.clocks==0 && fault.empty());
    }
}

static void encoder_serial_acceptance() {
    // Original AV3's entire extension table, retained as received bytes only.
    // ACK is specified by 100381B 3.8.1; extension functions are not specified.
    const uint8_t commands[][2]={
        {0xda,0x0f},{0xdc,0xfc},{0xde,0x20},{0xe0,0xd0},{0xe2,0x6f},{0xe4,0x0f},
        {0xe6,0x00},{0xe8,0x00},{0xea,0x0c},{0xec,0xf3},{0xee,0x09},{0xf0,0xbd},
        {0xf2,0x66},{0xf4,0xb5},{0xf6,0x90},{0xf8,0xb2},{0xfa,0x7d},{0xfc,0x00}};
    std::string fault;
    xlle::Cx25871Control encoder([&](const std::string& why){fault=why;});
    for(unsigned lifetime=0;lifetime<3;++lifetime) {
        CHECK(encoder.write_byte(0xba,0x80));CHECK(!encoder.extension_commands().received);
        CHECK(encoder.write_byte(0xba,0));CHECK(encoder.write_byte(0xc6,0x9c));
        CHECK(encoder.write_byte(0x6c,0x46));CHECK(encoder.write_byte(0xc4,1));
        CHECK(encoder.write_byte(0x6c,0xc6));CHECK(encoder.advance_timing(19));
        for(uint8_t reg:{0x00,0x02,0x04,0x06}) {
            for(uint8_t value:{0x00,0xff})CHECK(encoder.write_byte(reg,value));
            uint8_t value=0xa5;
            CHECK(!encoder.read_byte(reg,value) && value==0xa5);fault.clear();
        }
        xlle::Cx25871Timing::State state;
        xlle::Cx25871Control::Clocks clocks;
        CHECK(encoder.timing_state(state) && state.clocks==19);
        CHECK(encoder.clocks(clocks) && clocks.clko_driven);
        CHECK(encoder.field_signal()==0 && !encoder.extension_commands().received);
        for(const auto& command:commands) {
            CHECK(encoder.write_byte(command[0],command[1]) && fault.empty());
            uint8_t value=0xa5;
            CHECK(!encoder.read_byte(command[0],value) && value==0xa5);fault.clear();
        }
        CHECK(encoder.extension_commands().received==0x3ffff);
        for(unsigned i=0;i<18;++i)CHECK(encoder.extension_commands().bytes[i]==commands[i][1]);
        // Section1.3.6's documented clock equation and explicit reset/sync
        // still use their real inputs. These checks do not establish missing
        // extension functions or qualify connected video output.
        CHECK(encoder.timing_state(state) && state.clocks==19);
        CHECK(encoder.field_signal()==0 && encoder.clocks(clocks) && clocks.clko_driven);
        CHECK(clocks.encoder.numerator==57744984375ull && clocks.encoder.denominator==2048);
        CHECK(encoder.advance_timing(1));
        CHECK(encoder.write_byte(0x6c,0xc6));
        CHECK(encoder.timing_state(state) && state.clocks==0);
        CHECK(encoder.extension_commands().received==0x3ffff); // timing reset keeps command history
        CHECK(encoder.write_byte(0xb8,0)); // autoconfig only owns documented timing registers
        CHECK(encoder.clocks(clocks) && clocks.clko_driven);
        CHECK(encoder.extension_commands().received==0x3ffff);
        CHECK(encoder.write_byte(0xda,0xa5));
        CHECK(encoder.extension_commands().received==0x3ffff && encoder.extension_commands().bytes[0]==0xa5);
        CHECK(encoder.write_byte(0xba,0x80)); // full serial reset, not guessed extension defaults
        CHECK(!encoder.extension_commands().received && encoder.clocks(clocks) && !clocks.clko_driven);
        uint8_t value=0xa5;CHECK(encoder.write_byte(0x6c,0x46));
        CHECK(!encoder.read_byte(0xda,value) && value==0xa5);fault.clear();
        CHECK(fault.empty());
    }
}

int main() {
    encoder_timing();
    encoder_slave_phase();
    encoder_clocks();
    av_output_pins();
    retail_fw_enable();
    reset_and_configuration();
    documented_encoder_controls();
    encoder_serial_acceptance();
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
        // Three original ID-write attempts and extension commands complete
        // through the actual controller, including its normal status/IRQ path.
        // No direct register seeding or fabricated successful guest return.
        for(const auto& command:std::vector<std::pair<uint8_t,uint8_t>>{
                {0,0},{0,0},{0,0},{0xda,0x0f},{0xfc,0}}) {
            const size_t before=edges.size();
            write(command.first,command.second);
            CHECK(fault.empty() && bus.io_read(0xc000,1)==0x10);
            CHECK(edges.size()==before+1 && edges.back());
            bus.io_write(0xc000,1,0x10);
            CHECK(bus.io_read(0xc000,1)==0 && edges.size()==before+2 && !edges.back());
        }
        bus.io_write(0xc008,1,0xc8);
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
