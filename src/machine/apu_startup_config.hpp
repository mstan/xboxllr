// Bounded original-driver configuration transport. The physical stopped
// observation establishes these exact latched values, not arbitrary field
// encodings or the active sequencer/FE effects of changing them.
#pragma once
#include <array>
#include <cstdint>

namespace xlle {
class ApuStartupConfiguration {
public:
    struct Register {uint32_t offset,value;};
    inline static constexpr std::array<Register,23> registers={{
        {0x1500,0},{0x1508,0x800040},{0x150c,0},
        {0x2008,0x800},{0x2010,0x800},{0x2014,0x400},{0x2018,0x7ff},{0x201c,0},
        {0x2020,0x600},{0x2024,0x100},{0x2028,0x100},
        {0x1104,0xff},{0x1108,0x3f},{0x111c,0x7f},{0x1124,0x1fff},
        {0x1138,0x7ff},{0x1158,0x20},{0x112c,0},{0x1130,0x08000000},
        {0x1140,0},{0x1144,0x08000000},{0x1150,0},{0x1154,0x08000000}
    }};
    static bool touches(uint32_t offset,int bytes) {
        if(bytes<=0)return false;
        for(const auto& reg:registers)
            if(offset<reg.offset+4 && uint64_t(offset)+unsigned(bytes)>reg.offset)return true;
        return false;
    }
    bool write(uint32_t offset,uint32_t value) {
        for(unsigned i=0;i<registers.size();++i)
            if(offset==registers[i].offset && value==registers[i].value) {known_|=1u<<i;return true;}
        return false;
    }
    bool known(uint32_t offset)const {
        for(unsigned i=0;i<registers.size();++i)
            if(offset==registers[i].offset && (known_&(1u<<i)))return true;
        return false;
    }
private:
    uint32_t known_=0;
};
}
