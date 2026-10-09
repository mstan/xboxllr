// Hardware-derived fixture: abaire/nxdk_pgraph_tests BlitBeyondWidth.
// Test concept/source pattern: nxdk_pgraph_tests and pbkitplusplus contributors
// (MIT). Expected pixels come from the independent Xbox 1.0 CC0 capture,
// commit 06b041d6a50c060b4974d93617405f86a03a0b8f. See attribution inventory.
// This qualifies address layout; it is not a test of the complete GPU engine.
#include "machine/nv2a_tile_layout.hpp"
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>

static int failures;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
using Pixel = std::array<uint8_t, 4>;
static Pixel source_pixel(uint32_t x, uint32_t y) {
    // Original test source is 1024 x 512. Integer arithmetic is exact for
    // these power-of-two denominators and the original float range.
    return {uint8_t(255-y*255/512),uint8_t(x*255/1024),uint8_t(y*255/512),255};
}
static std::vector<Pixel> hardware_fixture() {
    constexpr uint32_t pixels = 640*480, guard_pixels = 1024;
    xlle::Nv2aTileLayout layout;
    CHECK(layout.configure(2560,1));
    std::vector<Pixel> raw(pixels+guard_pixels,Pixel{204,204,204,204});
    // Source-copy rows exceed the destination stride. The tile's inclusive
    // bound clips writes; later rows overwrite earlier row overflow.
    for(uint32_t y=0;y<512;++y)for(uint32_t x=0;x<1024;++x) {
        uint32_t linear=(y*640+x)*4;
        if(linear>=pixels*4)continue;
        uint32_t physical=0;
        CHECK(layout.translate(linear,physical));
        if(physical>=pixels*4){CHECK(false);continue;}
        raw[physical/4]=source_pixel(x,y);
    }
    for(uint32_t i=pixels;i<raw.size();++i)CHECK((raw[i]==Pixel{204,204,204,204}));
    // Hardware test then removes the tile and blits from raw memory with a
    // 4096-byte source offset. The final 1024 pixels expose the guard.
    return {raw.begin()+guard_pixels,raw.end()};
}
static void layout_properties() {
    uint32_t translated=0x12345678;
    xlle::Nv2aTileLayout layout;
    CHECK(!layout.translate(0,translated) && translated==0x12345678);
    for(uint32_t pitch : {512u,768u,1024u,1280u,1792u,2560u,3328u,5120u,32768u}) {
        for(uint32_t bank : {0u,1u}) {
            CHECK(layout.configure(pitch,bank));
            // Two complete tile-row groups: no aliasing, including non-power
            // of two pitches and each partition/bank/row crossing.
            std::vector<bool> seen(pitch*32,false);
            for(uint32_t linear=0;linear<seen.size();++linear) {
                CHECK(layout.translate(linear,translated));
                if(translated>=seen.size()){CHECK(false);break;}
                CHECK(!seen[translated]);seen[translated]=true;
                CHECK((translated&15)==(linear&15));
            }
        }
    }
    for(uint32_t pitch : {0u,1u,256u,513u,2816u,65536u,0xffffffffu}) {
        CHECK(!layout.configure(pitch,0));translated=0x12345678;
        CHECK(!layout.translate(0,translated) && translated==0x12345678);
    }
    CHECK(!layout.configure(2560,2));
    CHECK(layout.configure(2560,1));translated=0x12345678;
    CHECK(!layout.translate(0xffffa600u,translated) && translated==0x12345678);
}
int main(int argc,char** argv) {
    layout_properties();
    const auto output=hardware_fixture();
    struct Sample {uint32_t x,y;Pixel expected;};
    const Sample samples[]={
        {0,0,{255,0,0,255}}, {639,0,{250,6,5,255}},
        {0,1,{252,10,3,255}}, {0,16,{248,15,7,255}},
        {639,40,{234,118,21,255}}, {0,300,{108,146,147,255}},
        {300,300,{105,143,150,255}}, {0,479,{204,204,204,204}},
        {639,479,{204,204,204,204}}};
    for(const auto& sample:samples)CHECK(output[sample.y*640+sample.x]==sample.expected);
    if(argc==3 && !std::strcmp(argv[1],"--hardware-rgba")) {
        std::ofstream file(argv[2],std::ios::binary);
        file.write(reinterpret_cast<const char*>(output.data()),output.size()*sizeof(Pixel));
        CHECK(bool(file));
    } else if(argc!=1) {std::fprintf(stderr,"usage: test_nv2a_tile_layout [--hardware-rgba OUTPUT]\n");return 2;}
    if(!failures)std::puts("test_nv2a_tile_layout: ok");
    return failures?1:0;
}
