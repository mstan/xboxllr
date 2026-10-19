// Bounded VGA register transport for the original extended-color mode setup.
// Register facts: IBM VGA/XGA May1992 pp2-42..54,2-78..96; NV mirror addresses
// from envytools nv_vga.xml. This does not implement planar VGA memory or text.
#pragma once
#include <array>
#include <cstdint>

namespace xlle {
class Nv2aVgaRegisters {
public:
    static bool vio(uint32_t port) {
        return port==0x3c2 || port==0x3c3 || port==0x3c4 || port==0x3c5 ||
               port==0x3cc || port==0x3ce || port==0x3cf;
    }
    static bool attribute(uint32_t port) {return port==0x3c0 || port==0x3c1;}
    // Explicit cold-board attribute reset, separate from unknown retained
    // VGA state. Reference xemu vga_common_reset: index0, index phase, AR0.
    // Does not provide display signals, clocks or other VGA mode state.
    bool initialize_board_attribute_reset() {
        if(phase_known_ || attribute_index_known_ || sequencer_index_known_ ||
           graphics_index_known_ || misc_known_ || subsystem_enable_received_)
            return reject("NV2A VGA board reset requires fresh state");
        attribute_.fill(0);attribute_known_=(1u<<attribute_.size())-1;
        attribute_address_=0;attribute_index_known_=true;
        attribute_data_=false;phase_known_=true;return true;
    }
    // An initialized-mode caller precondition, not a power-on reset value.
    // Original AV3 starts with an index write without first reading INP1.
    bool establish_attribute_index_phase() {
        if(phase_known_ || attribute_index_known_)return reject("NV2A attribute phase already owned");
        attribute_data_=false;phase_known_=true;return true;
    }
    void input_status_read() {attribute_data_=false;phase_known_=true;}
    bool screen_off() const {return (sequencer_[1]&0x20)!=0;}
    bool subsystem_enable_received() const {return subsystem_enable_received_;}
    const char* error() const {return error_;}
    bool read_vio(uint32_t port,uint32_t& value) {
        switch(port) {
        case 0x3c4: if(!sequencer_index_known_)break;value=sequencer_index_;return true;
        case 0x3c5:
            if(!sequencer_index_known_ || !(sequencer_known_&(1u<<sequencer_index_)))break;
            value=sequencer_[sequencer_index_];return true;
        case 0x3ce: if(!graphics_index_known_)break;value=graphics_index_;return true;
        case 0x3cf:
            if(!graphics_index_known_ || !(graphics_known_&(1u<<graphics_index_)))break;
            value=graphics_[graphics_index_];return true;
        case 0x3cc: if(!misc_known_)break;value=misc_;return true;
        // VSE2 is reserved in the primary IBM reference. Receipt of the
        // original enable command alone does not justify invented readback.
        default:return reject("NV2A VGA read outside supported register contract");
        }
        return reject("NV2A VGA read before register programming");
    }
    bool write_vio(uint32_t port,uint32_t value) {
        if(value>255)return reject("NV2A VGA byte value out of range");
        switch(port) {
        case 0x3c3:
            // Address decoding is already enabled by this initialized profile;
            // only the idempotent enable command is admitted, never disable.
            if(value!=1)return reject("NV2A VGA subsystem command outside original enable profile");
            subsystem_enable_received_=true;return true;
        case 0x3c2:
            // Color aliases, enabled memory, original clock/polarity selection.
            // Slaved extended FP timing is selected independently by CRTC28.
            if(value!=0xe3)return reject("NV2A VGA miscellaneous mode outside original profile");
            misc_=uint8_t(value);misc_known_=true;return true;
        case 0x3c4:
            if(value>=sequencer_.size())return reject("NV2A sequencer index is unqualified");
            sequencer_index_=uint8_t(value);sequencer_index_known_=true;return true;
        case 0x3c5: {
            if(!sequencer_index_known_)return reject("NV2A sequencer data without owned index");
            constexpr uint8_t profile[]={3,1,15,0,6};
            if(value!=profile[sequencer_index_] && !(sequencer_index_==1 && value==0x21))
                return reject("NV2A sequencer mode outside original extended-color profile");
            // SR0=3 releases both resets; asserting either reset needs actual
            // sequencer/counter reset semantics and remains guarded.
            sequencer_[sequencer_index_]=uint8_t(value);sequencer_known_|=1u<<sequencer_index_;return true;
        }
        case 0x3ce:
            if(value>=graphics_.size())return reject("NV2A graphics controller index is unqualified");
            graphics_index_=uint8_t(value);graphics_index_known_=true;return true;
        case 0x3cf: {
            if(!graphics_index_known_)return reject("NV2A graphics data without owned index");
            constexpr uint8_t profile[]={0,0,0,0,0,0x40,5,15,255};
            if(value!=profile[graphics_index_])return reject("NV2A graphics mode outside original extended-color profile");
            graphics_[graphics_index_]=uint8_t(value);graphics_known_|=1u<<graphics_index_;return true;
        }
        default:return reject("NV2A VGA write outside supported register contract");
        }
    }
    bool read_attribute(uint32_t port,uint32_t& value) {
        if(!attribute_index_known_)return reject("NV2A attribute read without owned index");
        if(port==0x3c0){
            // VGA reference: address read during the data phase returns
            // zero; reads never consume/toggle the write flip-flop.
            value=attribute_data_?0:attribute_address_;return true;
        }
        const unsigned index=attribute_address_&31;
        if(index<16 && (attribute_address_&32))return reject("NV2A palette read while display owns palette");
        if(!(attribute_known_&(1u<<index)))return reject("NV2A attribute read before register programming");
        value=attribute_[index];return true;
    }
    bool write_attribute(uint32_t value) {
        if(value>255 || !phase_known_)return reject("NV2A attribute write without established byte/phase");
        if(!attribute_data_) {
            if((value&~63u) || (value&31)>=attribute_.size())return reject("NV2A attribute index is unqualified");
            attribute_address_=uint8_t(value);attribute_index_known_=true;attribute_data_=true;return true;
        }
        if(!attribute_index_known_)return reject("NV2A attribute data without owned index");
        const unsigned index=attribute_address_&31;
        if(index<16) {
            if((attribute_address_&32) || value>63)return reject("NV2A palette write outside CPU-owned six-bit profile");
        } else {
            constexpr uint8_t profile[]={1,0x4a,15,0,0};
            if(value!=profile[index-16])return reject("NV2A attribute mode outside original extended-color profile");
        }
        attribute_[index]=uint8_t(value);attribute_known_|=1u<<index;attribute_data_=false;return true;
    }
private:
    bool reject(const char* error){error_=error;return false;}
    // Preserve the existing digital scanout profile until SR1 is written;
    // fresh register reads still require an actual write.
    std::array<uint8_t,5> sequencer_{{0,1,0,0,0}};
    std::array<uint8_t,9> graphics_{};
    std::array<uint8_t,21> attribute_{};
    uint32_t sequencer_known_=0,graphics_known_=0,attribute_known_=0;
    uint8_t sequencer_index_=0,graphics_index_=0,attribute_address_=0,misc_=0;
    bool sequencer_index_known_=false,graphics_index_known_=false,attribute_index_known_=false;
    bool phase_known_=false,attribute_data_=false,misc_known_=false,subsystem_enable_received_=false;
    const char* error_=nullptr;
};
} // namespace xlle
