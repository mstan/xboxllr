#include "machine/sd601_pixel.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>

using Pixel=xlle::Sd601Pixel;
static unsigned failures;
#define CHECK(c) do { if(!(c)) { std::printf("FAIL line%d: %s\n",__LINE__,#c); ++failures; } } while(0)
static bool same(Pixel::YCrCb a,Pixel::YCrCb b) {
    return a.y==b.y && a.cr==b.cr && a.cb==b.cb;
}
static int quantize(long double value) {
    return std::max(0,std::min(255,int(std::floor(value+0.5L))));
}
// Independent floating-point evaluation of the published normalized matrix,
// rather than the production integer coefficients/common-denominator layout.
static Pixel::YCrCb reference_encode(Pixel::Rgb rgb) {
    const long double r=rgb.r/255.0L,g=rgb.g/255.0L,b=rgb.b/255.0L;
    const long double y=.299L*r+.587L*g+.114L*b;
    return {uint8_t(quantize(16+219*y)),
            uint8_t(quantize(128+224*(r-y)/1.402L)),
            uint8_t(quantize(128+224*(b-y)/1.772L))};
}
static Pixel::Rgb reference_decode(Pixel::YCrCb pixel) {
    const long double y=(int(pixel.y)-16)/219.0L;
    const long double cr=(int(pixel.cr)-128)/224.0L;
    const long double cb=(int(pixel.cb)-128)/224.0L;
    const long double r=y+1.402L*cr,b=y+1.772L*cb;
    // Recover G from luminance; independent of the production expanded
    // integer inverse and its combined common denominator.
    const long double g=(y-.299L*r-.114L*b)/.587L;
    return {uint8_t(quantize(255*r)),uint8_t(quantize(255*g)),uint8_t(quantize(255*b))};
}
int main() {
    struct Bar { Pixel::Rgb rgb; Pixel::YCrCb ycrcb; };
    const Bar bars[]={
        {{255,255,255},{235,128,128}},{{255,255,0},{210,146,16}},
        {{0,255,255},{170,16,166}},{{0,255,0},{145,34,54}},
        {{255,0,255},{106,222,202}},{{255,0,0},{81,240,90}},
        {{0,0,255},{41,110,240}},{{0,0,0},{16,128,128}}
    };
    for(const auto& bar:bars)CHECK(same(Pixel::encode(bar.rgb),bar.ycrcb));
    for(unsigned i=0;i<256;++i) {
        const auto encoded=Pixel::encode({uint8_t(i),uint8_t(i),uint8_t(i)});
        CHECK(encoded.cr==128 && encoded.cb==128);
        CHECK(encoded.y==quantize(16+219.0L*i/255));
        const auto decoded=Pixel::decode(encoded);
        CHECK(std::abs(int(decoded.r)-int(i))<=1);
        CHECK(decoded.r==decoded.g && decoded.g==decoded.b);
    }
    struct Packing { Pixel::YCrCb pixel; Pixel::Ddr12 words; };
    const Packing packing[]={
        {{0xab,0xcd,0xef},{0xcda,0xbef}},{{0x10,0x80,0x80},{0x801,0x080}},
        {{0x00,0xff,0xff},{0xff0,0x0ff}},{{0xff,0x00,0x00},{0x00f,0xf00}}
    };
    for(const auto& test:packing) {
        const auto packed=Pixel::pack(test.pixel);
        CHECK(packed.falling==test.words.falling && packed.rising==test.words.rising);
        Pixel::YCrCb output{1,2,3};
        CHECK(Pixel::unpack(test.words,output));CHECK(same(output,test.pixel));
    }
    for(const auto invalid:{Pixel::Ddr12{0x1000,0},Pixel::Ddr12{0,0x1000},
                            Pixel::Ddr12{0xffff,0xffff}}) {
        Pixel::YCrCb output{1,2,3};
        CHECK(!Pixel::unpack(invalid,output));CHECK(same(output,{1,2,3}));
    }
    CHECK(Pixel::decode({0,128,128}).r==0);
    CHECK(Pixel::decode({255,128,128}).b==255);
    CHECK(Pixel::decode({16,255,128}).r==203);
    CHECK(Pixel::decode({16,128,255}).b==255);

    // Quantization bounds: encoder errors <=.5 in each Y/Cr/Cb channel;
    // inverse plus final rounding gives R<1.89, G<1.69, B<2.10 RGB levels.
    // Thus integer roundtrip error <=2 (clamping cannot increase that error).
    const unsigned values[]={0,1,15,16,17,31,63,64,65,127,128,129,191,192,235,240,254,255};
    unsigned samples=0,max_error=0;
    for(auto r:values)for(auto g:values)for(auto b:values) {
        const Pixel::Rgb input{uint8_t(r),uint8_t(g),uint8_t(b)};
        const auto encoded=Pixel::encode(input);
        CHECK(same(encoded,reference_encode(input)));
        CHECK(encoded.y>=16 && encoded.y<=235);
        CHECK(encoded.cr>=16 && encoded.cr<=240 && encoded.cb>=16 && encoded.cb<=240);
        const auto decoded=Pixel::decode(encoded);
        const auto inverse=reference_decode(encoded);
        CHECK(decoded.r==inverse.r && decoded.g==inverse.g && decoded.b==inverse.b);
        for(auto error:{std::abs(int(decoded.r)-int(r)),std::abs(int(decoded.g)-int(g)),
                        std::abs(int(decoded.b)-int(b))}) {
            CHECK(error<=2);max_error=std::max(max_error,unsigned(error));
        }
        ++samples;
    }
    std::printf("SD601 bars, gray ramp, DDR, excursions and %u RGB samples: %s (max error %u)\n",
                samples,failures?"FAIL":"PASS",max_error);
    return failures?1:0;
}
