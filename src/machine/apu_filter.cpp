#include "machine/apu.hpp"

namespace xlle {
bool McpxApu::prepare_2d_filter(uint16_t voice,uint32_t format,uint32_t misc,
                              uint32_t route,uint32_t control50,uint32_t control70,
                              bool& enabled,ApuDls2Coefficients& coefficients) {
    enabled=false;
    const bool adjacent=control50 || control70 || ((format>>13)&7)!=7 ||
        (format&((1u<<21)|(1u<<22)|(1u<<24))) || (route&0x80008000);
    const unsigned mode=(misc>>16)&3;
    if(!adjacent && !mode)return true; // preserve bypass reads and arithmetic
    if(!adjacent && mode==1 && signal_model_!=ApuSignalModel::NominalDls2LinearV1) {
        device_fault("VP DLS2 requires explicit nominal signal-model policy; strict profile remains guarded");return false;
    }
    uint32_t envelope1=0,a=0,b=0;
    if(!read_voice_word(voice,0x10,envelope1) || !read_voice_word(voice,0x74,a) ||
       !read_voice_word(voice,0x78,b)) {
        device_fault("VP filter configuration DMA failed");return false;
    }
    if(adjacent || mode!=1 || (format&((1u<<16)|(1u<<27))) || (envelope1>>24)) {
        char reason[320];std::snprintf(reason,sizeof(reason),
            "VP filter profile unsupported: voice=%u format=%08x misc=%08x route=%08x control50=%08x control70=%08x envelope1=%08x filterA=%08x filterB=%08x",
            unsigned(voice),format,misc,route,control50,control70,envelope1,a,b);
        device_fault(reason);return false;
    }
    if(!voice_filter_owned_[voice]) {device_fault("DLS2 nominal filter requires actual VOICE_ON history");return false;}
    coefficients=ApuDls2Coefficients::decode(a);enabled=true;return true;
}
} // namespace xlle
