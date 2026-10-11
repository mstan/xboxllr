// Authored SSL staging owner. Descriptor words stay opaque until a separate
// qualified stream decoder owns their format, advancement and notification.
#pragma once
#include <array>
#include <cstdint>

namespace xlle {
struct ApuSslDescriptor {
    uint32_t offset=0;
    uint32_t control=0;
};

class ApuSsl {
public:
    // The original interface reserves 2 * 16 descriptors for each of 256
    // voices. Selection addresses a 64-entry window; bindings select a subset.
    static constexpr uint32_t kEntries=256*2*16, kWindow=64, kPerBank=16;
    bool select(uint32_t first) {
        if((first&(kWindow-1)) || first>kEntries-kWindow)return false;
        selected_=first;selected_valid_=true;return true;
    }
    bool bind(uint32_t voice,unsigned bank,uint32_t argument) {
        const uint32_t first=argument>>8,count=argument&255;
        if(voice>=bindings_.size() || bank>=2 || count>kPerBank ||
           first>=kEntries || count>kEntries-first)return false;
        bindings_[voice][bank]={first,count};return true;
    }
    bool staged_address(uint32_t table,uint32_t word,uint32_t& address) const {
        if(!selected_valid_ || word>=kWindow*2)return false;
        return address_at(table,selected_*8+word*4,4,address);
    }
    bool segment_address(uint32_t table,uint32_t voice,unsigned bank,
                         uint32_t segment,uint32_t& address) const {
        if(voice>=bindings_.size() || bank>=2)return false;
        const auto& binding=bindings_[voice][bank];
        if(segment>=binding.count)return false;
        return address_at(table,(binding.first+segment)*8,8,address);
    }
private:
    struct Binding {uint32_t first=0,count=0;};
    static bool address_at(uint32_t table,uint32_t offset,uint32_t bytes,uint32_t& address) {
        const uint64_t candidate=uint64_t(table)+offset;
        if((table&7) || candidate+bytes>0x100000000ull)return false;
        address=static_cast<uint32_t>(candidate);return true;
    }
    std::array<std::array<Binding,2>,256> bindings_{};
    uint32_t selected_=0;
    bool selected_valid_=false;
};
} // namespace xlle
