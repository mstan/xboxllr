// Execute PFIFO -> vertex fetch -> transform -> raster -> physical framebuffer.
// Independent byte/pixel expectations distinguish UB_D3D (BGRA) from UB_OGL
// (RGBA). Surface reuse follows nxdk_pgraph_tests' SurfaceAsVertexArrayTests;
// pinned sources and the absence of a matching hardware golden are documented
// in pilots/native-title/GRAPHICS-LLE.md. No reference decoder computes output.
#include "machine/nv2a.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <limits>
#include <string>
#include <vector>

static unsigned failures, cases;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
static uint32_t bits(float value) {
    uint32_t word; std::memcpy(&word, &value, 4); return word;
}

struct Fixture {
    static constexpr uint32_t frame = 0x10000, positions = 0x20000;
    static constexpr uint32_t colors = 0x30000, report = 0x9000;
    std::vector<uint8_t> ram = std::vector<uint8_t>(0x80000, 0xcc);
    std::string fault;
    uint32_t cursor = 0;
    std::unique_ptr<xlle::Nv2a> gpu;

    explicit Fixture(bool direct) : gpu(std::make_unique<xlle::Nv2a>(
        [this](const std::string& why) { if (fault.empty()) fault = why; })) {
        gpu->set_mem_access(
            [this](uint32_t a, void* p, size_t n) {
                if (uint64_t(a) + n > ram.size()) return false;
                std::memcpy(p, ram.data() + a, n); return true;
            },
            [this](uint32_t a, const void* p, size_t n) {
                if (uint64_t(a) + n > ram.size()) return false;
                std::memcpy(ram.data() + a, p, n); return true;
            }, direct ? std::function<uint8_t*(uint32_t, size_t)>(
                [this](uint32_t a, size_t n) {
                    return uint64_t(a) + n <= ram.size() ? ram.data() + a : nullptr;
                }) : std::function<uint8_t*(uint32_t, size_t)>{});
        const uint32_t setup[][2] = {
            {0x700008, 1}, {0x70000c, 0x80010020}, {0x700200, 0x97},
            {0x700010, 2}, {0x700014, 0x80010030},
            {0x700300, 0x3d}, {0x700304, uint32_t(ram.size()-1)}, {0x700308, 0},
            {0x700180, 0x3d}, {0x700184, 0x7fff}, {0x700188, 0x1000},
            {0x002504, 1}, {0x003204, 0x100}, {0x00322c, 0x18},
            {0x003244, 0}, {0x003220, 1}, {0x003250, 1}, {0x003200, 1},
            {0x400720, 1}, {0x400880, 0x28c3ff}};
        for (const auto& p : setup) write(p[0], p[1]);
        emit(0, 1); emit(0x194, 2); emit(0x198, 2); emit(0x1a8, 2);
        emit(0x19c, 2); emit(0x1a0, 2);
        emit(0x208, 0x128); emit(0x20c, 0x01000100);
        emit(0x210, frame); emit(0x214, 0x18000);
        for (unsigned i=0; i<16; ++i) emit(0x680+i*4, i%5==0 ? bits(1) : 0);
        const uint32_t state[][2] = {
            {0x0294, 0x00020001}, {0x1e94, 0}, {0x0200, 64u<<16}, {0x0204, 64u<<16},
            {0x0290, 0x00100001}, {0x02c0, 63u<<16}, {0x02e0, 63u<<16},
            {0x033c, 0x207}, {0x0350, 0x8006}, {0x0354, 0x201}, {0x0358, 0x01010101},
            {0x035c, 0}, {0x030c, 0}, {0x037c, 0x1d01}, {0x038c, 0x1b02}, {0x0390, 0x1b02},
            {0x0394, 0}, {0x0398, 0x4b7fffff}, {0x039c, 0x405}, {0x03a0, 0x900},
            {0x1d78, 1}, {0x1e78, 0x00210000}, {0x1e60, 1},
            {0x0ac0, 0x04200000}, {0x0260, 0x14200000}, {0x1e40, 0xc00},
            {0x0aa0, 0xc00}, {0x0288, 0xc}, {0x028c, 0x1c80}};
        for (const auto& p : state) emit(p[0], p[1]);
        const float xy[][2] = {{-200,-200}, {8,8}, {56,8}, {8,56}};
        for (unsigned i=0; i<4; ++i) {
            put(positions+i*16, bits(xy[i][0])); put(positions+i*16+4, bits(xy[i][1]));
            put(positions+i*16+8, bits(10)); put(positions+i*16+12, bits(1));
        }
        emit(0x1720, positions); emit(0x1760, 0x1042);
        kick(); CHECK(fault.empty());
    }
    void put(uint32_t a, uint32_t v) { std::memcpy(ram.data()+a, &v, 4); }
    uint32_t get(uint32_t a) const { uint32_t v; std::memcpy(&v, ram.data()+a, 4); return v; }
    void write(uint32_t r, uint32_t v) { gpu->mmio_write(xlle::Nv2a::kBase+r, 4, v); }
    void emit(uint32_t m, uint32_t v) { put(0x1000+cursor, 0x40000|m); put(0x1004+cursor, v); cursor+=8; }
    void kick() { write(0x800040, cursor); }
    void clear() {
        emit(0x1d90, 0xff102030); emit(0x1d98, 63u<<16); emit(0x1d9c, 63u<<16);
        emit(0x1d94, 0xf0);
    }
    void draw(unsigned route, uint32_t packed=0) {
        clear(); emit(0x17c8, 1); emit(0x17cc, 1);
        put(report+12, 0xffffffff); emit(0x17fc, 5);
        if (route==0) emit(0x1810, 1 | (2u<<24)); // DRAW_ARRAYS, first=1
        else if (route==1) { emit(0x1800, 0x00020001); emit(0x1808, 3); }
        else if (route==2) for (unsigned i=1; i<=3; ++i) {
            for (unsigned lane=0; lane<4; ++lane) emit(0x1818, get(positions+i*16+lane*4));
            emit(0x1818, packed); // INLINE_ARRAY: position float4 + packed color
        } else for (unsigned i=1; i<=3; ++i) {
            for (unsigned lane=0; lane<3; ++lane) emit(0x1518+lane*4, get(positions+i*16+lane*4));
            emit(0x194c, packed); // SET_VERTEX_DATA4UB remains RGBA, regardless of array format.
            emit(0x1524, bits(1));
        }
        emit(0x17fc, 0); emit(0x17d0, 0x01000000|report); kick();
    }
    void expect(uint32_t expected) {
        ++cases;
        if (!fault.empty()) std::printf("GPU fault: %s\n", fault.c_str());
        CHECK(fault.empty());
        const auto actual=get(frame+(16*64+16)*4);
        if (actual!=expected) std::printf("pixel=%08x expected=%08x\n", actual, expected);
        CHECK(actual==expected);
        CHECK(get(frame)==0xff102030 && get(frame+(56*64+56)*4)==0xff102030);
        CHECK(get(report+12)==0 && get(report+8)>0);
        const auto diffuse=gpu->last_transformed_diffuse();
        const unsigned shifts[]={16,8,0,24};
        for (unsigned lane=0; lane<4; ++lane)
            CHECK(std::abs(diffuse[lane]-float((expected>>shifts[lane])&255)/255)<1e-6f);
    }
};

static void packed_formats() {
    // DMA A/B, direct/callback reads, array/indexed/inline submissions. A padded
    // color stride and nonzero first index expose offset/stride mistakes.
    for (bool direct : {false,true}) for (bool dma_b : {false,true})
    for (unsigned route=0; route<3; ++route) for (unsigned format : {0u,4u}) {
        Fixture f(direct);
        f.emit(0x172c, Fixture::colors | (dma_b ? 0x80000000u : 0));
        f.emit(0x176c, 0x840|format);
        for (uint32_t argb : {0xffff0000u,0xff0000ffu,0x7f123456u,0x019be327u}) {
            // Declare input bytes explicitly, rather than using the decoder.
            const uint32_t packed=format==0 ? argb :
                (argb&0xff00ff00u)|((argb&255)<<16)|((argb>>16)&255);
            for (unsigned i=1; i<=3; ++i) f.put(Fixture::colors+i*8, packed);
            f.draw(route, packed); f.expect(argb);
        }
    }
}

static void surface_reuse() {
    // GPU CLEAR_SURFACE writes A8R8G8B8 into already-bound diffuse storage.
    // No CPU color upload, allocation replacement or attribute rebind occurs.
    for (bool direct : {false,true}) for (bool dma_b : {false,true})
    for (unsigned route=0; route<2; ++route) {
        Fixture f(direct);
        f.emit(0x172c, Fixture::colors | (dma_b ? 0x80000000u : 0));
        f.emit(0x176c, 0x440);
        for (uint32_t color : {0xffff0000u,0xff00ff00u,0xff0000ffu,0xffffff00u,0x7f123456u}) {
            f.emit(0x210, Fixture::colors); f.emit(0x1d90, color);
            f.emit(0x1d98, 3u<<16); f.emit(0x1d9c, 0); f.emit(0x1d94, 0xf0);
            f.emit(0x210, Fixture::frame); f.draw(route); f.expect(color);
            CHECK(f.get(Fixture::colors+4)==color && f.get(Fixture::colors+16)==0xcccccccc);
        }
    }
}

static void immediate_control() {
    for (bool direct : {false,true}) for (unsigned format : {0u,4u}) {
        Fixture f(direct);f.emit(0x176c, 0x440|format);
        f.draw(3, 0x7f563412);f.expect(0x7f123456);
    }
}

static void noise_control_boundary() {
    for(bool direct : {false,true}) {
        // The controls have a real disabled route after enable/disable, not
        // a fabricated success. Actual immediate pixels retain their oracle.
        Fixture disabled(direct);
        for(uint32_t value : {1u,0u})for(uint32_t address : {0xe00050u,0xdf0008u}) {
            disabled.write(0x400750,address);disabled.write(0x400754,value);
        }
        disabled.draw(3,0x7f563412);disabled.expect(0x7f123456);
        for(bool complete : {false,true})for(unsigned route=0;route<4;++route) {
            Fixture f(direct);
            f.write(0x400750,0xe00050);f.write(0x400754,1);
            if(complete){f.write(0x400750,0xdf0008);f.write(0x400754,1);}
            f.draw(route,0x7f563412);
            CHECK(f.fault==(complete?"NV2A DXT1 noise pixel behavior remains unqualified"
                                   :"NV2A DXT1 noise control banks disagree"));
            CHECK(!f.gpu->generic_combiner_executed_batches() && !f.gpu->generic_combiner_writes());
            for(unsigned i=0;i<64*64;++i)CHECK(f.get(Fixture::frame+i*4)==0xff102030);
            CHECK(f.get(Fixture::report+12)==0xffffffff);
        }
    }
}

static void fog_program(Fixture& f, unsigned mask, bool varying=false) {
    // Authored MOV ISA fields, without asking either shader implementation
    // to encode or compute the expected scalar destination.
    const uint32_t code[]={
        0,0x0020001b,0x08000000,0x0000f800, // oPos = v0
        0,0x0020001b|(96u<<13),0x0c000000,0x0000f818, // oD0 = c96
        0,varying?0x0020021bu:(0x0020001b|(97u<<13)),
        varying?0x08000000u:0x0c000000u,(mask<<12)|0x828u|1u}; // oFog
    f.emit(0x1e94,2);f.emit(0x1e9c,0);f.emit(0x1ea0,0);
    for(unsigned i=0;i<12;++i)f.emit(0xb00+i*4,code[i]);
    f.emit(0x1ea4,96);
    const float constants[]={.8f,.2f,.1f,.6f,.2f,.4f,.6f,.8f};
    for(unsigned i=0;i<8;++i)f.emit(0xb80+i*4,bits(constants[i]));
    f.emit(0x176c,0); // no diffuse fetch: the shader supplies it
    if(varying){f.emit(0x1724,Fixture::colors);f.emit(0x1764,0x1042);}
    f.emit(0x2a0,2);f.emit(0x2a8,0x99563412); // ABGR; color alpha is ignored
    f.emit(0xac0,0x03200000);f.emit(0x260,0x13200000); // FOG RGB/alpha * ONE
    f.emit(0x288,0xc);f.emit(0x28c,0x1c80);
}

static void expect_fog(Fixture& f,double factor,bool blend=false) {
    ++cases;
    if(!f.fault.empty())std::printf("fog fault: %s\n",f.fault.c_str());
    CHECK(f.fault.empty());
    const auto pixel=f.get(Fixture::frame+(16*64+16)*4);
    const double color[]={18.0/255,52.0/255,86.0/255};
    const double diffuse[]={.8,.2,.1};
    factor=std::clamp(factor,0.0,1.0);
    for(unsigned i=0;i<4;++i) {
        double value=i==3?(blend?.6:factor):(blend?factor*diffuse[i]+(1-factor)*color[i]:color[i]);
        unsigned shift=i==3?24:16-i*8;
        CHECK(std::abs(int((pixel>>shift)&255)-int(std::lround(value*255)))<=1);
    }
    CHECK(f.gpu->generic_combiner_executed_batches()>0 && f.get(Fixture::report+12)==0);
    CHECK(f.get(Fixture::frame)==0xff102030);
}

static void programmable_fog() {
    for(bool direct:{false,true})for(bool compiled:{false,true}) {
        Fixture f(direct);f.gpu->set_vsh_compile_enabled(compiled);
        for(unsigned mask=1;mask<16;++mask) {
            fog_program(f,mask);f.emit(0x2a4,1);f.emit(0x29c,0x2601);
            f.emit(0x9c0,bits(2));f.emit(0x9c4,bits(-1));f.emit(0x9c8,0);
            f.draw(0);unsigned lane=0;while(!(mask&(8u>>lane)))++lane;
            const double value[]={.2,.4,.6,.8};expect_fog(f,1-value[lane]);
            CHECK(std::abs(f.gpu->last_transformed_batch()[0].fog-value[lane])<1e-6);
        }
        for(unsigned mode:{0x2601u,0x800u,0x801u,0x802u,0x803u,0x804u}) {
            fog_program(f,8);
            const float multiplier=mode==0x2601||mode==0x804?-.5f:
                (mode==0x800||mode==0x802?float(-.75/(2*std::log(256.0))):float(-.75/(2*std::sqrt(std::log(256.0)))));
            f.emit(0x29c,mode);f.emit(0x9c0,bits(mode==0x2601||mode==0x804?2.5f:1.5f));
            f.emit(0x9c4,bits(multiplier));f.emit(0x9c8,0);
            for(float distance:{-2.0f,0.0f,.5f,1.5f,3.0f}) {
                f.emit(0x1ea4,97);f.emit(0xb80,bits(distance));
                f.emit(0x288,0x13040300);f.emit(0x28c,0x1480); // f*diffuse+(1-f)*fog
                f.draw(0);
                double expected=mode==0x2601||mode==0x804?(3-distance)/2:
                    (mode==0x800||mode==0x802?std::exp(-.75*distance):std::exp(-std::pow(.75*distance,2)));
                if(mode>=0x802&&mode<=0x804)expected=std::abs(expected);
                expect_fog(f,expected,true);
            }
        }
        // Clamp after perspective interpolation, not at each vertex. State
        // changes reuse the same input/program cache; no rebinding occurs.
        fog_program(f,1,true);f.emit(0x29c,0x2601);f.emit(0x2a4,1);
        f.emit(0x9c0,bits(2));f.emit(0x9c4,bits(-1));f.emit(0x9c8,0);
        const float distances[]={-1,3,0};const float w[]={1,2,4};
        for(unsigned i=1;i<4;++i){f.put(Fixture::colors+i*16+12,bits(distances[i-1]));f.put(Fixture::positions+i*16+12,bits(w[i-1]));}
        f.draw(0);
        // Sample center (16.5,16.5) in triangle (8,8),(56,8),(8,56).
        const double b=8.5/48,a=1-2*b;
        expect_fog(f,(a*2+b/2*(-2)+b/4)/(a+b/2+b/4));
        f.put(Fixture::positions+16,bits(-8));f.draw(0);
        const double clip_b=(24.5-16*b)/64,clip_a=1-b-clip_b;
        expect_fog(f,(clip_a*2-clip_b+b/4)/(clip_a+clip_b/2+b/4));
        f.emit(0x2a4,0);f.draw(0);expect_fog(f,1);
        for(unsigned i=1;i<4;++i)f.put(Fixture::colors+i*16+12,bits(1));
        f.emit(0x2a4,1);f.draw(0);expect_fog(f,0); // persistent payload mutation
    }
    for(unsigned invalid=0;invalid<3;++invalid) {
        Fixture f(false);fog_program(f,8);f.emit(0x2a4,1);f.emit(0x29c,invalid==0?0x999:0x800);
        f.emit(0x9c0,bits(1.5f));f.emit(0x9c4,bits(-.1f));f.emit(0x9c8,invalid==1?bits(1):0);
        if(invalid==2){f.emit(0x1ea4,97);f.emit(0xb80,bits(std::numeric_limits<float>::infinity()));}
        f.draw(0);CHECK(!f.fault.empty());CHECK(!f.gpu->generic_combiner_writes());
        CHECK(f.get(Fixture::report+12)==0xffffffff);
        for(unsigned i=0;i<64*64;++i)CHECK(f.get(Fixture::frame+i*4)==0xff102030);
    }
}

int main() {
    packed_formats(); surface_reuse(); immediate_control();
    noise_control_boundary();
    programmable_fog();
    std::printf("nv2a_vertex_colors: %u draw cases, %u failures\n", cases, failures);
    return failures ? 1 : 0;
}
