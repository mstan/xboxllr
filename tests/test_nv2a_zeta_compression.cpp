#include "machine/nv2a_zeta_compression.hpp"
#include "machine/nv2a_tile_layout.hpp"
#include <cstdio>
#include <fstream>
#include <set>
#include <string>
using Codec=xlle::Nv2aZetaCompression;
static int failures;
#define CHECK(c) do {if(!(c)){std::printf("FAIL %d: %s\n",__LINE__,#c);++failures;}}while(0)

static void format_cases() {
    Codec::Encoded bytes{};Codec::Pixels pixels{},decoded{};
    // Published NV20_ZCOMP_TILE_32 field layout: base at (1,1), signed
    // 15-bit X/Y slopes, bit 63 selects 8 bytes (not a constant-color flag).
    Codec::store64(bytes.data(),0x8000000000000000ull|uint64_t(0x7ffe)<<32|
                   uint64_t(3)<<47|0x1234567f);
    bytes[8]=0xff;bytes[15]=0xff; // ignored for the 8-byte representation
    Codec::decode(bytes,pixels);
    for(int y=0;y<4;++y)for(int x=0;x<4;++x)
        CHECK(pixels[y*4+x]==uint32_t((0x123456-2*(x-1)+3*(y-1))<<8|0x7f));
    CHECK(Codec::encode(pixels,bytes)==8);Codec::decode(bytes,decoded);CHECK(decoded==pixels);
    // Explicit packed residuals across the 63-bit split: pixel (0,0) = -16,
    // (1,0) = +15. Anchors remain unchanged.
    Codec::store64(bytes.data(),0x001234567full);
    Codec::store64(bytes.data()+8,8u|(15u<<4));
    Codec::decode(bytes,pixels);
    CHECK(pixels[0]==0x1234467f);CHECK(pixels[1]==0x1234657f);
    CHECK(pixels[5]==0x1234567f && pixels[6]==pixels[5] && pixels[9]==pixels[5]);
    CHECK(Codec::encode(pixels,bytes)==16);Codec::decode(bytes,decoded);CHECK(decoded==pixels);
    // Modular 24-bit depth, negative slopes and varying-stencil fallback.
    for(int y=0;y<4;++y)for(int x=0;x<4;++x)pixels[y*4+x]=(uint32_t(2-x-y)<<8)|0xa5;
    CHECK(Codec::encode(pixels,bytes)==8);Codec::decode(bytes,decoded);CHECK(decoded==pixels);
    pixels[0]^=1;CHECK(Codec::encode(pixels,bytes)==0);
    pixels.fill(0x10000042);pixels[0]+=0x1000;CHECK(Codec::encode(pixels,bytes)==0);
    pixels.fill(0x10000042);pixels[6]+=0x400000;CHECK(Codec::encode(pixels,bytes)==0);
}

static void tag_layout() {
    for(uint32_t bank:{0u,1u}) {
        xlle::Nv2aTileLayout layout;CHECK(layout.configure(2560,bank));
        std::set<uint32_t> tags;
        for(uint32_t y=0;y<480;y+=4)for(uint32_t x=0;x<2560;x+=16) {
            uint32_t part=9,tag=0;CHECK(layout.compression_tag(y*2560+x,part,tag));
            CHECK(part<4 && tags.insert(tag*4+part).second);
            for(uint32_t dy=0;dy<4;++dy)for(uint32_t dx=0;dx<16;++dx) {
                uint32_t p=0,t=0;CHECK(layout.compression_tag((y+dy)*2560+x+dx,p,t));
                CHECK(p==part && t==tag);
            }
        }
        CHECK(tags.size()==640*480/16);
    }
}

int main(int argc,char** argv) {
    if(argc==4 && std::string(argv[1])=="--encode-blocks") {
        std::ifstream in(argv[2],std::ios::binary);std::ofstream out(argv[3],std::ios::binary);
        if(!in || !out)return 2;
        Codec::Pixels pixels;
        while(in.read(reinterpret_cast<char*>(pixels.data()),sizeof(pixels))) {
            Codec::Encoded encoded{};const uint32_t size=Codec::encode(pixels,encoded);
            out.write(reinterpret_cast<const char*>(&size),4);
            out.write(reinterpret_cast<const char*>(encoded.data()),encoded.size());
        }
        return in.eof() && in.gcount()==0 && out?0:2;
    }
    if(argc==4 && std::string(argv[1])=="--decode-blocks") {
        std::ifstream in(argv[2],std::ios::binary);std::ofstream out(argv[3],std::ios::binary);
        if(!in || !out)return 2;
        Codec::Encoded encoded;Codec::Pixels pixels;
        while(in.read(reinterpret_cast<char*>(encoded.data()),encoded.size())) {
            Codec::decode(encoded,pixels);
            out.write(reinterpret_cast<const char*>(pixels.data()),sizeof(pixels));
        }
        return in.eof() && in.gcount()==0 && out?0:2;
    }
    if(argc!=1)return 2;
    format_cases();tag_layout();
    if(!failures)std::puts("test_nv2a_zeta_compression: ok");
    return failures?1:0;
}
