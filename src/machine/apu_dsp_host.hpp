// CPU-facing GP/EP startup words, distinct from DSP peripheral addresses.
#pragma once
#include <array>
#include <cstdint>
namespace xlle {
struct ApuDspHostRegisters {
    static constexpr std::array<uint32_t,9> offsets={
        0x3ff00,0x3ff04,0x3ff10,0x3ff14,
        0x5ff00,0x5ff04,0x5ff10,0x5ff14,0x5ff5c};
    static bool contains(uint32_t off) {
        for(auto field:offsets)if(field==off)return true;
        return false;
    }
    static bool touches(uint32_t off,int size) {
        if(size<=0)return false;
        const uint64_t end=uint64_t(off)+unsigned(size);
        for(auto field:offsets)if(off<field+4 && end>field)return true;
        return false;
    }
    static bool permits(uint32_t off,uint32_t value) {
        switch(off) {
        case 0x3ff00:case 0x3ff04:case 0x5ff00:case 0x5ff04:case 0x5ff5c:return value==0;
        // The original aligned owner supplies upper bits, clears bit3 and
        // sets bit0. Physical30000001/FFFFFFF1 both read1. Lower bits1..3
        // remain outside this supported profile.
        case 0x3ff10:return value==0 || (value&15u)==1;
        case 0x5ff10:return value<=1;
        case 0x3ff14:return value==0xff;
        case 0x5ff14:return value==0xffffffffu;
        default:return false;
        }
    }
};
} // namespace xlle
