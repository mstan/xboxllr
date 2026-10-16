// Portable conversion of one declared time owner's absolute ticks to edges.
// This owns arithmetic/phase only: no device routing, gating, PLL lock, reset,
// synchronization or activation is inferred. No wider-than-64-bit type needed.
#pragma once
#include <cstdint>
#include <limits>
#include <numeric>

namespace xlle {
class RationalSourceClock {
public:
    struct Time { uint64_t owner, ticks, frequency; };
    struct Rate { uint64_t numerator, denominator; }; // positive edges/second
    struct State {
        bool bound=false;
        uint64_t owner=0,ticks=0,frequency=0,edges=0;
        Rate rate{0,1},per_tick{0,1},phase{0,1};
    };
    enum class Error {
        None,NotBound,AlreadyBound,InvalidOwner,InvalidFrequency,InvalidRate,
        OwnerChanged,FrequencyChanged,BackwardsTime,TimeNotAdvanced,
        RateRange,PhaseRange,EdgeRange
    };

    State state() const { return state_; }

    // Binding declares an origin; it does not manufacture edges before it.
    // Time identity/frequency are immutable. A different owner needs a fresh
    // object, not a silent rebase of an existing oscillator's phase.
    Error bind(Time time,Rate rate) {
        if(state_.bound)return Error::AlreadyBound;
        if(!time.owner)return Error::InvalidOwner;
        if(!time.frequency)return Error::InvalidFrequency;
        State next;
        const Error error=prepare_rate(time.frequency,rate,next.rate,next.per_tick);
        if(error!=Error::None)return error;
        next.bound=true;next.owner=time.owner;next.ticks=time.ticks;
        next.frequency=time.frequency;state_=next;return Error::None;
    }

    // Exact floor of elapsed integral edges plus the carried fractional edge.
    // On ANY failure both state and caller's delta remain unchanged.
    Error advance(Time time,uint64_t& delta) {
        const Error error=validate_time(time);
        if(error!=Error::None)return error;
        uint64_t edges=0,remainder=0;
        if(!multiply_divide(time.ticks-state_.ticks,state_.per_tick.numerator,
                            state_.per_tick.denominator,edges,remainder))return Error::EdgeRange;
        Rate phase=state_.phase;
        if(remainder) {
            Rate part{remainder,state_.per_tick.denominator};reduce(part);
            if(!phase.numerator)phase=part;
            else {
                uint64_t denominator=0;
                if(!common_denominator(phase.denominator,part.denominator,denominator))
                    return Error::PhaseRange;
                const uint64_t left=phase.numerator*(denominator/phase.denominator);
                const uint64_t right=part.numerator*(denominator/part.denominator);
                // Both summands are <denominator. Avoid their possibly
                // overflowing sum, including denominator==UINT64_MAX.
                const bool carry=left>=denominator-right;
                phase={carry ? left-(denominator-right) : left+right,denominator};
                if(carry) {
                    if(edges==maximum)return Error::EdgeRange;
                    ++edges;
                }
                reduce(phase);
            }
        }
        if(edges>maximum-state_.edges)return Error::EdgeRange;
        State next=state_;
        next.ticks=time.ticks;next.edges+=edges;next.phase=phase;
        state_=next;delta=edges;return Error::None;
    }

    // Explicit continuous-edge-phase policy. The caller MUST first advance
    // and deliver OLD-rate edges through this exact timestamp, then change
    // the rate. The fraction of an output edge is preserved, not rounded or
    // interpreted as a hardware PLL phase/lock policy.
    //
    // Representation is bounded: a reduced per-tick denominator and the LCM
    // with the current reduced phase denominator must each fit uint64_t.
    // Rates outside that exact common phase grid reject before any mutation.
    Error change_rate(Time time,Rate rate) {
        const Error error=validate_time(time);
        if(error!=Error::None)return error;
        if(time.ticks!=state_.ticks)return Error::TimeNotAdvanced;
        Rate normalized{},per_tick{};
        const Error prepared=prepare_rate(time.frequency,rate,normalized,per_tick);
        if(prepared!=Error::None)return prepared;
        uint64_t grid=0;
        if(!common_denominator(state_.phase.denominator,per_tick.denominator,grid))
            return Error::PhaseRange;
        State next=state_;next.rate=normalized;next.per_tick=per_tick;
        state_=next;return Error::None;
    }

private:
    static constexpr uint64_t maximum=std::numeric_limits<uint64_t>::max();
    State state_;

    Error validate_time(Time time) const {
        if(!state_.bound)return Error::NotBound;
        if(time.owner!=state_.owner)return Error::OwnerChanged;
        if(time.frequency!=state_.frequency)return Error::FrequencyChanged;
        if(time.ticks<state_.ticks)return Error::BackwardsTime;
        return Error::None;
    }
    static void reduce(Rate& rate) {
        const uint64_t divisor=std::gcd(rate.numerator,rate.denominator);
        rate.numerator/=divisor;rate.denominator/=divisor;
    }
    static Error prepare_rate(uint64_t frequency,Rate rate,Rate& normalized,Rate& per_tick) {
        if(!rate.numerator || !rate.denominator)return Error::InvalidRate;
        reduce(rate);normalized=rate;
        // Cross-cancel before forming frequency*denominator. A large raw
        // product alone is not evidence that the reduced rate is unsupported.
        const uint64_t divisor=std::gcd(rate.numerator,frequency);
        frequency/=divisor;rate.numerator/=divisor;
        if(rate.denominator>maximum/frequency)return Error::RateRange;
        per_tick={rate.numerator,rate.denominator*frequency};return Error::None;
    }
    static bool common_denominator(uint64_t a,uint64_t b,uint64_t& result) {
        const uint64_t reduced=a/std::gcd(a,b);
        if(reduced>maximum/b)return false;
        result=reduced*b;return true;
    }
    // Long multiplication/division with a modular remainder, 64 iterations.
    // At each binary-prefix step: q*d+r == prefix*b, 0<=r<d.
    // Every addition/doubling is checked or reduced BEFORE it can overflow.
    static bool multiply_divide(uint64_t a,uint64_t b,uint64_t d,
                                uint64_t& quotient,uint64_t& remainder) {
        uint64_t q=0,r=0;
        const uint64_t whole=b/d,part=b%d;
        for(int bit=63;bit>=0;--bit) {
            if(q>maximum/2)return false;
            q*=2;
            const bool carry=r>=d-r;
            r=carry ? r-(d-r) : r+r;
            if(carry) { if(q==maximum)return false;++q; }
            if((a>>bit)&1u) {
                if(whole>maximum-q)return false;
                q+=whole;
                const bool extra=r>=d-part;
                r=extra ? r-(d-part) : r+part;
                if(extra) { if(q==maximum)return false;++q; }
            }
        }
        quotient=q;remainder=r;return true;
    }
};
} // namespace xlle
