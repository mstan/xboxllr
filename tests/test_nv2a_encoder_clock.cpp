// Independent count/time expectations for actual FP -> encoder sync coupling.
#include "machine/nv2a_encoder_clock.hpp"
#include <cstdio>
#include <string>

static int failures;
#define CHECK(c) do {if(!(c)){std::printf("FAIL line%d: %s\n",__LINE__,#c);++failures;}}while(0)
using Board=xlle::Nv2aEncoderClock;
using Encoder=xlle::Cx25871Control;
static constexpr uint64_t frequency=12502828125ull;
static constexpr uint64_t line=776,frame=776*525;
static xlle::Nv2aFpRegisters programming() {
    xlle::Nv2aFpRegisters registers;
    const uint32_t words[]={479,524,479,490,493,0,479,639,775,599,653,685,0,639};
    for(unsigned i=0;i<14;++i)CHECK(registers.write(i<7?0x800+4*i:0x820+4*(i-7),words[i]));
    CHECK(registers.write(0x848,0x10100111));CHECK(registers.write(0x880,0x21121111));
    return registers;
}
static void configure(Encoder& encoder) {
    const uint8_t writes[][2]={{0xba,0x80},{0xba,0x20},{0xc6,0x9c},{0xc4,1},
        {0x9c,0x68},{0x9e,0xda},{0xa0,0x0a},{0x8a,8},{0x8e,3},
        {0x90,13},{0x96,2},{0x76,0x10},{0x86,6},{0xa2,0}};
    for(const auto& write:writes)CHECK(encoder.write_byte(write[0],write[1]));
}
int main() {
    for(unsigned split=0;split<2;++split) {
        std::string fault;
        Encoder encoder([&](const std::string& why){fault=why;});
        unsigned vblanks=0;
        Board board(encoder,[&](const std::string& why){fault=why;},
            [&](const xlle::Nv2aFpTiming::State& state,bool leading){
                if(leading){CHECK(state.clocks==480*line+vblanks*frame);++vblanks;}
            });
        CHECK(board.bind({7,0,frequency}));
        auto registers=programming();
        CHECK(board.refresh(registers.observe(),true,0x100a0000));
        CHECK(!board.active() && !board.encoder_live()); // no manufactured clock
        configure(encoder);
        CHECK(board.refresh(registers.observe(),true,0x100a0000));CHECK(board.active());
        uint64_t prior=0;
        const auto advance=[&](uint64_t edge) {
            if(split) for(uint64_t n=prior+17;n<edge;n+=17)CHECK(board.advance({7,n*512,frequency}));
            CHECK(board.advance({7,edge*512,frequency}));prior=edge;
        };
        advance(13);
        xlle::Nv2aFpTiming::State fp;CHECK(board.state(fp) && fp.x==13 && !board.encoder_live());
        CHECK(encoder.write_byte(0x6c,0xc6));
        CHECK(board.refresh(registers.observe(),true,0x100a0000));
        CHECK(board.encoder_live());CHECK(board.state(fp) && fp.x==13 && fp.clocks==13);
        xlle::Cx25871Timing::State state;CHECK(encoder.timing_state(state) && !state.clocks);
        // Positive VSYNC begins at line491, before that line's HSYNC at654.
        // The FIELD transition is two CLKI cycles AFTER that HSYNC.
        const uint64_t first=491*line+654;
        advance(first-1);CHECK(encoder.field_signal()==0);
        advance(first);CHECK(encoder.field_signal()==0);
        advance(first+1);CHECK(encoder.field_signal()==0);
        advance(first+2);CHECK(encoder.field_signal()==1);
        CHECK(encoder.timing_state(state) && state.clocks==first+2-13 && state.field_transitions==1);
        CHECK(!state.output_phase_known); // shifted reset does not invent analog phase
        CHECK(vblanks==1);
        // Neither DEBUG transport nor read-bank selection is an activation.
        CHECK(registers.write(0x880,0x21101100));CHECK(registers.write(0x848,0x10000111));
        CHECK(board.refresh(registers.observe(),true,0x100a0000));
        CHECK(board.state(fp) && fp.clocks==first+2);
        advance(first+2);CHECK(vblanks==1 && encoder.field_signal()==1);
        advance(first+frame+1);CHECK(encoder.field_signal()==1);
        advance(first+frame+2);CHECK(encoder.field_signal()==0);
        CHECK(encoder.timing_state(state) && state.field_transitions==2 && vblanks==2);
        // Actual reset leaves running FP counters intact; a second later
        // first-sync sequence starts from a different phase.
        CHECK(encoder.write_byte(0x6c,0xc6));CHECK(board.refresh(registers.observe(),true,0x100a0000));
        CHECK(board.state(fp) && fp.clocks==first+frame+2);
        advance(first+2*frame+2);CHECK(encoder.field_signal()==1 && vblanks==3);
        CHECK(fault.empty());
        auto before=fp;CHECK(board.state(before));
        CHECK(!board.advance({8,(first+2*frame+2)*512,frequency}));
        CHECK(fault=="display clock time owner/frequency/order changed");
    }
    {
        std::string fault;Encoder encoder([&](const std::string& why){fault=why;});configure(encoder);
        Board board(encoder,[&](const std::string& why){fault=why;},{});
        auto registers=programming();CHECK(board.bind({1,0,frequency}));
        CHECK(board.refresh(registers.observe(),true,0x100a0000));
        CHECK(board.advance({1,511,frequency}));xlle::Nv2aFpTiming::State fp;
        CHECK(board.state(fp) && !fp.clocks);
        CHECK(board.advance({1,512,frequency}));CHECK(board.state(fp) && fp.clocks==1);
        CHECK(registers.write(0x824,776));
        CHECK(!board.refresh(registers.observe(),true,0x100a0000));
        CHECK(fault=="display timing changed while slaved counters are active");
    }
    std::printf("%s: configured rational clock, sparse sync ordering, FIELD delay, frozen time and independent reset phase\n",failures?"FAIL":"PASS");
    return failures?1:0;
}
