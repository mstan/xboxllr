// Bounded original-driver configuration transport. The physical stopped
// observation establishes these exact latched values, not arbitrary field
// encodings or the active sequencer/FE effects of changing them.
#pragma once
#include <array>
#include <cstdint>
#include <initializer_list>

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

// The original caller gives each of these two slots a distinct, aligned 8 KiB
// allocation. Held-device write/readback establishes address transport only;
// the tables' active consumers and their unnamed fields remain unqualified.
class ApuStartupMappings {
public:
    static bool touches(uint32_t offset,int bytes) {
        return bytes>0 && offset<0x2040 && uint64_t(offset)+unsigned(bytes)>0x2038;
    }
    bool write(uint32_t offset,uint32_t value) {
        if((offset!=0x2038 && offset!=0x203c) || !value || (value&0x3fff) ||
           value>0x03ffe000)return false;
        known_|=1u<<((offset-0x2038)/4);return true;
    }
    bool known(uint32_t offset)const {
        return (offset==0x2038 || offset==0x203c) && (known_&(1u<<((offset-0x2038)/4)));
    }
private:
    uint32_t known_=0;
};

// Physical stopped-device differential observations establish this permutation.
// The non-null consumers of these unnamed fields are not part of startup.
class ApuStartupMethods {
public:
    struct Field {uint32_t method,offset;};
    inline static constexpr std::array<Field,5> fields={{
        {0x2a0,0x2078},{0x2a4,0x207c},{0x2b0,0x2080},
        {0x2ac,0x2084},{0x2a8,0x2088}
    }};
    static bool touches(uint32_t offset,int bytes) {
        if(bytes<=0)return false;
        const uint64_t end=uint64_t(offset)+unsigned(bytes);
        for(uint32_t field:{0x1148u,0x202a0u,0x202a4u,0x202a8u,0x202acu,0x202b0u,
                           0x202c0u,0x20804u,0x20808u,0x21000u,0x21004u,0x21800u,0x21808u})
            if(offset<field+4 && end>field)return true;
        return false;
    }
    static int register_index(uint32_t offset) {
        if(offset>=0x2078 && offset<=0x2088 && !(offset&3))return int((offset-0x2078)/4);
        if(offset==0x20ac)return 5;
        if(offset==0x1148)return 6;
        return -1;
    }
};
}
