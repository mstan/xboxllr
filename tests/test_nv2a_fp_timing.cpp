// Independent dense pixel-clock oracle for the sparse FP event generator.
// Modelines are specified as counts, not decoded back from implementation state.
#include "machine/nv2a_fp_timing.hpp"
#include <cstdio>
#include <string>
#include <vector>

static int failures;
#define CHECK(c) do {if(!(c)){std::printf("FAIL line %d: %s\n",__LINE__,#c);++failures;}}while(0)
using Timing=xlle::Nv2aFpTiming;
struct Modeline { uint32_t w,h,ht,vt,hs,he,vs,ve,left,top,control; };
static Timing::Mode program(const Modeline& m,unsigned polarity) {
    Timing::Mode mode;
    mode.horizontal={m.w-1,m.ht-1,m.hs-1,m.he-1,m.left,m.w-1};
    mode.vertical={m.h-1,m.vt-1,m.vs-1,m.ve-1,m.top,m.h-1};
    mode.control=m.control | ((polarity&1)?0x10:0) |
        ((polarity&2)?1:0) | ((polarity&4)?0x10000000:0);
    return mode;
}
static unsigned pins(const Timing::State& s) {
    return unsigned(s.hsync) | (unsigned(s.vsync)<<1) | (unsigned(s.display_enable)<<2) |
        (unsigned(s.vertical_blank)<<3);
}
struct Edge { uint64_t clock; uint32_t x,y; unsigned levels; };
int main() {
    // Ordinary 640x480; reached original Xbox FP counts; PAL-sized timing.
    // These qualify counter/signal behavior, not the board's clock routing.
    const Modeline modes[]={
        {640,480,800,525,656,752,490,492,0,0,0x01100200},
        {640,480,776,525,654,686,491,494,0,0,0x00100100},
        {720,576,864,625,732,796,581,586,8,3,0x01100200}};
    for(const auto& m:modes) for(unsigned polarity=0;polarity<8;++polarity) {
        std::string fault;
        Timing timing([&](const std::string& why){fault=why;});
        Timing::State state;state.clocks=0xcccc;
        CHECK(!timing.state(state) && state.clocks==0xcccc);
        CHECK(!timing.advance(1) && !fault.empty());fault.clear();
        const auto config=program(m,polarity);
        // Deliberately nonzero phase. Establishment must not silently align
        // the generator with an encoder's reset or a host frame boundary.
        const uint32_t origin_x=37,origin_y=m.vs-1;
        CHECK(timing.establish(config,origin_x,origin_y));
        std::vector<Edge> events,expected;
        const auto record=[&](const Timing::State& s){events.push_back({s.clocks,s.x,s.y,pins(s)});};
        CHECK(timing.advance(0,record) && events.empty());
        const uint64_t frame=uint64_t(m.ht)*m.vt, total=frame*2+111;
        CHECK(timing.advance(1,record));
        CHECK(timing.advance(651,record));
        CHECK(timing.advance(total-652,record));
        uint32_t x=origin_x,y=origin_y;uint64_t frames=0;
        const auto levels=[&]() {
            unsigned active=unsigned(x>=m.hs && x<m.he) |
                (unsigned(y>=m.vs && y<m.ve)<<1) |
                (unsigned(x>=m.left && x<m.w && y>=m.top && y<m.h)<<2);
            return (active ^ (polarity^7u)) | (unsigned(y>=m.h)<<3);
        };
        unsigned previous=levels();
        for(uint64_t clock=1;clock<=total;++clock) {
            if(++x==m.ht) { x=0;if(++y==m.vt){y=0;++frames;} }
            unsigned next=levels();
            if(next!=previous)expected.push_back({clock,x,y,next});
            previous=next;
        }
        CHECK(events.size()==expected.size());
        for(size_t i=0;i<std::min(events.size(),expected.size());++i) {
            CHECK(events[i].clock==expected[i].clock && events[i].x==expected[i].x &&
                  events[i].y==expected[i].y && events[i].levels==expected[i].levels);
        }
        CHECK(timing.state(state) && state.clocks==total && state.frames==frames &&
              state.x==x && state.y==y && pins(state)==previous);
        Timing fast([&](const std::string& why){fault=why;});
        CHECK(fast.establish(config,origin_x,origin_y) && fast.advance(total));
        Timing::State other;CHECK(fast.state(other));
        CHECK(other.clocks==state.clocks && other.frames==state.frames && other.x==x &&
              other.y==y && pins(other)==previous);
        CHECK(!timing.advance(UINT64_MAX) && !fault.empty());fault.clear();
        CHECK(timing.state(other) && other.clocks==state.clocks && pins(other)==pins(state));
        auto rejected=config;rejected.control|=2; // disabled VSYNC is not driven low by assumption
        CHECK(!timing.establish(rejected,0,0) && !fault.empty());fault.clear();
        rejected=config;rejected.horizontal.sync_end=rejected.horizontal.sync_start;
        CHECK(!timing.establish(rejected,0,0) && !fault.empty());fault.clear();
        CHECK(!timing.establish(config,m.ht,0) && !fault.empty());fault.clear();
        CHECK(timing.state(other) && other.clocks==state.clocks && other.x==x && other.y==y);
        CHECK(fault.empty());
    }
    // A fast observation-only advance still counts every complete period;
    // absence of an event consumer does not change the clock arithmetic.
    std::string fault;
    Timing large([&](const std::string& why){fault=why;});
    CHECK(large.establish(program(modes[0],7),0,0));
    CHECK(large.advance(UINT64_MAX));Timing::State s;CHECK(large.state(s));
    CHECK(s.frames==UINT64_MAX/420000 && s.y==(UINT64_MAX%420000)/800 && s.x==UINT64_MAX%800);
    CHECK(!large.advance(1) && !fault.empty());
    std::printf("%s: FP sparse events vs dense clocks, 24 mode/polarity lifetimes; explicit origin and limits\n",failures?"FAIL":"PASS");
    return failures?1:0;
}
