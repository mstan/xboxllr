// Original AV3's remaining PRAMDAC setup commands. Receipt is deliberately
// separate from hardware latch/readback, clock generation and pixel effects.
// The reached fixed profile can proceed to its documented FP timing consumers;
// full scanout stays guarded until the required output processing is resolved.
#pragma once
#include <array>
#include <cstdint>

namespace xlle {
class Nv2aDisplaySetupCommands {
public:
    struct Observation {std::array<uint32_t,11> received{};uint32_t known=0;};
    static int index(uint32_t offset) {
        for(unsigned i=0;i<offsets.size();++i)if(offsets[i]==offset)return int(i);
        return -1;
    }
    static bool handles(uint32_t offset){return index(offset)>=0;}
    bool write(uint32_t offset,uint32_t value) {
        const int at=index(offset);
        if(at<0)return false;
        // Fixed original NTSC profile, including its reached RGB-selector
        // branch's zero writes. No arbitrary register-file or other-mode claim.
        constexpr std::array<uint32_t,11> profile{{2,0xc6ed0,525,155,620,0,
                                                  0x00801080,0x10000000,0x10000000,0,0x40801080}};
        const bool color_zero=(offset==0x630 || offset==0x84c || offset==0x8c4) && value==0;
        if(value!=profile[unsigned(at)] && !color_zero)return false;
        state_.received[unsigned(at)]=value;state_.known|=1u<<unsigned(at);return true;
    }
    bool any() const{return state_.known!=0;}
    Observation observe() const{return state_;}
private:
    inline static constexpr std::array<uint32_t,11> offsets{{0x630,0x680,0x684,0x688,0x68c,0x690,
                                                            0x84c,0x898,0x89c,0x8c0,0x8c4}};
    Observation state_;
};
} // namespace xlle
