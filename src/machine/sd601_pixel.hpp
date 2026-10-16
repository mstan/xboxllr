// Nominal SD digital color contract, not NV2A coefficient/rounding emulation.
// ITU-R BT.601-7 sections 2.5.1-2.5.4 define colorimetry and narrow ranges:
// https://www.itu.int/dms_pubrec/itu-r/rec/bt/R-REC-BT.601-7-201103-I!!PDF-E.pdf
// Khronos Data Format 1.4 sections 15.1.2/16.4.2 give the full-8-bit RGB
// to narrow YCbCr equations and inverse used here (without transfer functions):
// https://registry.khronos.org/DataFormat/specs/1.4/dataformat.1.4.html
// Conexant 100381B Table 1-2 specifies IN_MODE1100 edge/pin packing. The
// independent X-HD receiver uses that DDR style and SD601 interpretation:
// https://github.com/Team-Resurgent/X-HD/blob/69babd44aaf45e2e9c0afb90efa912e681441396/src/shared/adv7511_xbox.c
#pragma once
#include <cstdint>

namespace xlle {
class Sd601Pixel {
public:
    struct Rgb { uint8_t r, g, b; };
    struct YCrCb { uint8_t y, cr, cb; };
    struct Ddr12 { uint16_t falling, rising; };

    // R',G',B' are already gamma encoded, with black0/white255. Every channel
    // is rounded once to nearest (positive ties up), using exact rationals.
    static constexpr YCrCb encode(Rgb rgb) {
        const int64_t r=rgb.r, g=rgb.g, b=rgb.b;
        return {
            byte(16+nearest(219*(299*r+587*g+114*b),1000*255)),
            byte(128+nearest(224*(701*r-587*g-114*b),1402*255)),
            byte(128+nearest(224*(-299*r-587*g+886*b),1772*255))
        };
    }

    // Excursions are accepted and the resulting RGB is clamped to0..255.
    // Encoder-specific input0/255 remapping is deliberately outside this API.
    static constexpr Rgb decode(YCrCb pixel) {
        const int64_t y=int64_t(pixel.y)-16;
        const int64_t cr=int64_t(pixel.cr)-128, cb=int64_t(pixel.cb)-128;
        constexpr int64_t common=1000LL*219*224;
        return {
            byte(nearest(255*(1000*224*y+1402*219*cr),common)),
            byte(nearest(255*(1000*587*224*y-299*1402*219*cr-114*1772*219*cb),587*common)),
            byte(nearest(255*(1000*224*y+1772*219*cb),common))
        };
    }

    static constexpr Ddr12 pack(YCrCb pixel) {
        return {uint16_t((uint16_t(pixel.cr)<<4)|(pixel.y>>4)),
                uint16_t((uint16_t(pixel.y&15)<<8)|pixel.cb)};
    }
    static constexpr bool unpack(Ddr12 words,YCrCb& output) {
        if(words.falling>0xfff || words.rising>0xfff)return false;
        output={uint8_t(((words.falling&15)<<4)|(words.rising>>8)),
                uint8_t(words.falling>>4),uint8_t(words.rising&255)};
        return true;
    }
private:
    // Signed nearest division, ties toward +infinity as floor(n/d+1/2).
    // Fixed API bounds keep all arithmetic below 2^44; no signed shifts,
    // floating-point environment, SIMD or extended integer type required.
    static constexpr int64_t nearest(int64_t n,int64_t d) {
        return n>=0 ? (n+d/2)/d : -((-n+(d-1)/2)/d);
    }
    static constexpr uint8_t byte(int64_t n) {
        return uint8_t(n<0 ? 0 : n>255 ? 255 : n);
    }
};
} // namespace xlle
