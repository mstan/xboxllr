// Authored NV2A FP register transport, separate from activation/clocks.
// Offsets/READ_PROG: envytools f102b823 nv3_pramdac.xml, Linuxv6.12 nvreg.h.
// The owner's bounded Xbox HVALID_START observation establishes writes to the
// programmed view regardless of READ_PROG. No copied implementation bodies.
#pragma once
#include <array>
#include <cstdint>
namespace xlle {
class Nv2aFpRegisters {
public:
    using Words=std::array<uint32_t,14>;
    static constexpr uint32_t read_programmed=1u<<20;
    static int index(uint32_t offset) {
        if(offset&3)return -1;
        if(offset>=0x800 && offset<=0x818)return int((offset-0x800)/4);
        if(offset>=0x820 && offset<=0x838)return 7+int((offset-0x820)/4);
        return -1;
    }
    static bool handles(uint32_t offset) {return offset==0x848 || offset==0x880 || index(offset)>=0;}
    bool control_known() const {return control_known_;}
    // Existing nominal scanout geometry consumes programmed extents. This is
    // not a register read, proof of reset defaults, or an active timing origin.
    uint32_t programmed_extent(bool horizontal) const {return programmed_[horizontal?7:0];}
    bool write(uint32_t offset,uint32_t value) {
        if(offset==0x880) {
            // Both original whole words have verified physical write/readback.
            // Admit that storage contract without masking unknown bits17/29 or
            // interpreting either word as activation, ready, clock or bank-copy.
            // Named scaling/interpolation effects remain an output-owner duty.
            if(value!=0x21121111u && value!=0x21101100u)
                return reject("NV2A FP DEBUG outside observed configuration words");
            debug_=value;debug_known_=true;return true;
        }
        if(offset==0x848) {
            // Only defined polarity, mode, read-view and interface fields.
            if((value&~0x31100333u) || (value&3u)==3 || ((value>>4)&3u)==3 ||
               ((value>>8)&3u)==3 || ((value>>28)&3u)==3)
                return reject("NV2A FP control outside defined transport fields");
            control_=value;control_known_=true;return true;
        }
        const int at=index(offset);
        // This is the bounded reached counter profile, not an assertion that
        // every silicon register has a twelve-bit physical write mask.
        if(at<0 || value>0xfff)return reject("NV2A FP counter outside bounded transport");
        programmed_[at]=value;programmed_known_|=uint16_t(1u<<at);return true;
    }
    bool read(uint32_t offset,uint32_t& value) {
        if(offset==0x880) {
            if(!debug_known_)return reject("NV2A FP DEBUG configuration is unknown");
            value=debug_;return true;
        }
        if(!control_known_)return reject("NV2A FP read view has unknown control state");
        if(offset==0x848){value=control_;return true;}
        const int at=index(offset);
        if(at<0)return reject("NV2A FP read outside timing-register transport");
        if(control_&read_programmed) {
            if(!(programmed_known_&(1u<<at)))return reject("NV2A FP programmed register is unknown");
            value=programmed_[at];return true;
        }
        if(!alternate_known_)return reject("NV2A FP alternate read view is unestablished");
        value=alternate_[at];return true;
    }
    // Explicit board input with independent provenance. Neither a guest write
    // nor an AV acknowledgment calls this. It makes no active-output or phase
    // claim; later activation must supply its own real event/owner contract.
    bool capture_alternate(const Words& words) {
        for(uint32_t value:words)if(value>0xfff)
            return reject("NV2A FP captured view outside bounded transport");
        alternate_=words;alternate_known_=true;return true;
    }
    const char* error() const {return error_;}
private:
    bool reject(const char* why){error_=why;return false;}
    Words programmed_{},alternate_{};
    uint16_t programmed_known_=0;
    uint32_t control_=0;
    bool control_known_=false,alternate_known_=false;
    uint32_t debug_=0;
    bool debug_known_=false;
    const char* error_=nullptr;
};
} // namespace xlle
