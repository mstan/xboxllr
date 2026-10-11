// Authored nominal signed PCM24 boundary. Saturation/truncation is replaceable;
// exact MCPX accumulator precision and quantization are not qualified here.
#pragma once
#include <cmath>
#include <cstdint>
namespace xlle {
inline bool apu_mix_word(float sample,uint32_t& word) {
    if(!std::isfinite(sample))return false;
    const double scaled=double(sample)*8388608.0;
    const int32_t pcm=scaled>=8388607.0?8388607:scaled<=-8388608.0?-8388608:int32_t(scaled);
    word=uint32_t(pcm)&0xffffff;return true;
}
} // namespace xlle
