// Independent rational examples and bounded integer oracle; no device/host API.
#include "machine/rational_source_clock.hpp"
#include <algorithm>
#include <cstdio>
#include <tuple>

using Clock=xlle::RationalSourceClock;
using Error=Clock::Error;
static int failures;
#define CHECK(c) do {if(!(c)){std::printf("FAIL line%d: %s\n",__LINE__,#c);++failures;}}while(0)
static constexpr uint64_t largest=UINT64_MAX;
static auto snapshot(const Clock& clock) {
    const auto s=clock.state();
    return std::make_tuple(s.bound,s.owner,s.ticks,s.frequency,s.edges,
        s.rate.numerator,s.rate.denominator,s.per_tick.numerator,s.per_tick.denominator,
        s.phase.numerator,s.phase.denominator);
}
static void phase(const Clock& clock,uint64_t numerator,uint64_t denominator) {
    const auto s=clock.state();
    CHECK(s.phase.numerator==numerator && s.phase.denominator==denominator);
}
int main() {
    {
        Clock clock;uint64_t delta=0xcccc;
        const auto empty=snapshot(clock);
        CHECK(clock.advance({1,0,10},delta)==Error::NotBound);
        CHECK(delta==0xcccc && snapshot(clock)==empty);
        CHECK(clock.bind({0,0,10},{1,1})==Error::InvalidOwner);
        CHECK(clock.bind({1,0,0},{1,1})==Error::InvalidFrequency);
        CHECK(clock.bind({1,0,10},{0,1})==Error::InvalidRate);
        CHECK(clock.bind({1,0,10},{1,0})==Error::InvalidRate);
        CHECK(snapshot(clock)==empty);
        CHECK(clock.bind({1,100,10},{3,2})==Error::None);
        CHECK(clock.advance({1,100,10},delta)==Error::None && delta==0);
        CHECK(clock.advance({1,101,10},delta)==Error::None && delta==0);phase(clock,3,20);
        CHECK(clock.advance({1,110,10},delta)==Error::None && delta==1);phase(clock,1,2);
        CHECK(clock.advance({1,120,10},delta)==Error::None && delta==2);phase(clock,0,1);
        CHECK(clock.state().edges==3);
        const auto before=snapshot(clock);delta=0xcccc;
        CHECK(clock.bind({2,0,20},{2,1})==Error::AlreadyBound);
        CHECK(clock.advance({2,121,10},delta)==Error::OwnerChanged);
        CHECK(clock.advance({1,121,11},delta)==Error::FrequencyChanged);
        CHECK(clock.advance({1,121,0},delta)==Error::FrequencyChanged);
        CHECK(clock.advance({1,119,10},delta)==Error::BackwardsTime);
        CHECK(clock.change_rate({1,119,10},{1,1})==Error::BackwardsTime);
        CHECK(clock.change_rate({1,121,10},{1,1})==Error::TimeNotAdvanced);
        CHECK(clock.change_rate({1,120,10},{0,1})==Error::InvalidRate);
        CHECK(delta==0xcccc && snapshot(clock)==before);
    }
    {
        // Five old-rate ticks yield3/4 edge. Five new-rate ticks add1/4.
        Clock clock;uint64_t delta=0;
        CHECK(clock.bind({7,0,10},{3,2})==Error::None);
        CHECK(clock.advance({7,5,10},delta)==Error::None && delta==0);phase(clock,3,4);
        const auto before=snapshot(clock);
        CHECK(clock.change_rate({7,6,10},{1,2})==Error::TimeNotAdvanced);
        CHECK(snapshot(clock)==before);
        CHECK(clock.change_rate({7,5,10},{1,2})==Error::None);phase(clock,3,4);
        CHECK(clock.advance({7,10,10},delta)==Error::None && delta==1);phase(clock,0,1);
        CHECK(clock.state().edges==1);
        CHECK(clock.advance({7,10,10},delta)==Error::None && delta==0);
    }
    {
        // Different phase denominators:1/2 +1/3 +1/3 =1+1/6.
        Clock clock;uint64_t delta=0;
        CHECK(clock.bind({1,0,6},{1,1})==Error::None);
        CHECK(clock.advance({1,3,6},delta)==Error::None);phase(clock,1,2);
        CHECK(clock.change_rate({1,3,6},{2,1})==Error::None);
        CHECK(clock.advance({1,4,6},delta)==Error::None && delta==0);phase(clock,5,6);
        CHECK(clock.advance({1,5,6},delta)==Error::None && delta==1);phase(clock,1,6);
    }
    {
        // Prescribed encoder rate. One second contains24419586+93/512 edges.
        Clock single,chunked;uint64_t once=0,sum=0,delta=0;
        const Clock::Time origin{91,9000,1125000};
        CHECK(single.bind(origin,{12502828125ull,512})==Error::None);
        CHECK(chunked.bind(origin,{12502828125ull,512})==Error::None);
        CHECK(single.advance({91,1134000,1125000},once)==Error::None && once==24419586);
        phase(single,93,512);
        uint64_t now=9000;
        for(unsigned i=0;now<1134000;++i) {
            now+=std::min(uint64_t(1134000-now),uint64_t((i*197u)%3001u+1u));
            CHECK(chunked.advance({91,now,1125000},delta)==Error::None);sum+=delta;
        }
        CHECK(sum==once && snapshot(chunked)==snapshot(single));
        CHECK(single.advance({91,2259000,1125000},delta)==Error::None && delta==24419586);
        phase(single,93,256);
    }
    {
        // Independent small integer oracle: cumulative floor(n*t/(d*f)).
        // Bounds here make its direct product safe; it shares no algorithm.
        for(uint64_t numerator=1;numerator<=17;++numerator)
        for(uint64_t denominator=1;denominator<=11;++denominator)
        for(uint64_t frequency:{1u,7u,19u}) {
            Clock clock;uint64_t delta=0,previous=0;
            CHECK(clock.bind({5,37,frequency},{numerator,denominator})==Error::None);
            for(uint64_t elapsed=0;elapsed<=103;elapsed+=1+elapsed%7) {
                const uint64_t product=elapsed*numerator,divisor=denominator*frequency;
                const uint64_t expected=product/divisor;
                CHECK(clock.advance({5,37+elapsed,frequency},delta)==Error::None);
                CHECK(delta==expected-previous && clock.state().edges==expected);
                const auto s=clock.state();
                CHECK(s.phase.numerator*divisor==(product%divisor)*s.phase.denominator);
                previous=expected;
            }
        }
    }
    {
        // Raw frequency*rate denominator overflows, but cross-cancellation
        // leaves a representable per-tick rate1/(MAX-1).
        Clock clock;uint64_t delta=0;
        CHECK(clock.bind({1,0,largest},{largest,largest-1})==Error::None);
        CHECK(clock.advance({1,largest-1,largest},delta)==Error::None && delta==1);
        phase(clock,0,1);
        Clock wide;
        CHECK(wide.bind({1,0,largest},{largest-1,1})==Error::None);
        CHECK(wide.advance({1,largest,largest},delta)==Error::None && delta==largest-1);
        phase(wide,0,1); // Full product exceeds64bits; quotient does not.
    }
    {
        // The fractional sum exceeds64bits before modular carry reduction.
        Clock clock;uint64_t delta=0;
        CHECK(clock.bind({1,0,1},{largest-1,largest})==Error::None);
        CHECK(clock.advance({1,1,1},delta)==Error::None && delta==0);phase(clock,largest-1,largest);
        CHECK(clock.advance({1,2,1},delta)==Error::None && delta==1);phase(clock,largest-2,largest);
        CHECK(clock.advance({1,largest,1},delta)==Error::None && delta==largest-2);phase(clock,0,1);
        CHECK(clock.state().edges==largest-1);
    }
    {
        Clock clock;uint64_t delta=0xcccc;
        const auto empty=snapshot(clock);
        CHECK(clock.bind({1,0,largest},{1,largest})==Error::RateRange);
        CHECK(snapshot(clock)==empty);
        CHECK(clock.bind({1,0,1},{largest,1})==Error::None);
        auto before=snapshot(clock);
        CHECK(clock.advance({1,2,1},delta)==Error::EdgeRange);
        CHECK(delta==0xcccc && snapshot(clock)==before);
        Clock total;
        CHECK(total.bind({1,0,1},{2,1})==Error::None);
        CHECK(total.advance({1,largest/2,1},delta)==Error::None && delta==largest-1);
        before=snapshot(total);delta=0xcccc;
        CHECK(total.advance({1,largest/2+1,1},delta)==Error::EdgeRange);
        CHECK(delta==0xcccc && snapshot(total)==before);
        Clock grid;
        CHECK(grid.bind({1,0,1},{1,largest})==Error::None);
        CHECK(grid.advance({1,1,1},delta)==Error::None);phase(grid,1,largest);
        before=snapshot(grid);
        CHECK(grid.change_rate({1,1,1},{1,largest-1})==Error::PhaseRange);
        CHECK(snapshot(grid)==before);
        CHECK(grid.change_rate({1,1,1},{1,largest})==Error::None);
        CHECK(grid.advance({1,largest,1},delta)==Error::None && delta==1);phase(grid,0,1);
    }
    std::printf("%s: portable rational source clock, independent rates/oracle, phase changes and atomic failures\n",failures?"FAIL":"PASS");
    return failures?1:0;
}
