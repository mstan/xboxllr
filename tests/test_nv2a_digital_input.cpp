// Fixed-profile digital active pixels at the encoder input. These authored
// checks qualify neither analog filters/waveforms nor a complete DDR timing
// stream with porches/DE. Expected color bars and packing are independent of
// the production encoder helper (ITU601/Khronos16.4.2;100381B Table1-2).
#include "machine/nv2a.hpp"
#include "machine/cx25871_control.hpp"
#include <array>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

static unsigned failures;
#define CHECK(c) do { if(!(c)){std::printf("FAIL line%d: %s\n",__LINE__,#c);++failures;} } while(0)
using Pixel=xlle::Sd601Pixel;
static bool same(Pixel::YCrCb a,Pixel::YCrCb b) {
    return a.y==b.y && a.cr==b.cr && a.cb==b.cb;
}
static bool same(const std::vector<Pixel::Ddr12>& a,const std::vector<Pixel::Ddr12>& b) {
    if(a.size()!=b.size())return false;
    for(size_t i=0;i<a.size();++i)
        if(a[i].falling!=b[i].falling || a[i].rising!=b[i].rising)return false;
    return true;
}
struct DigitalFixture {
    static constexpr uint32_t start=0x1000,bytes=640*480*4;
    std::string fault,error;
    unsigned reads=0,writes=0,fail_read=0;
    std::vector<uint8_t> ram=std::vector<uint8_t>(start+bytes,0xcc);
    std::unique_ptr<xlle::Nv2a> gpu;
    DigitalFixture(unsigned omit_command=11,unsigned omit_timing=14,bool gamma=true)
        : gpu(std::make_unique<xlle::Nv2a>([this](const std::string& why){fault=why;})) {
        CHECK(gpu->establish_display_clock_routing(0x100a0000));
        constexpr uint32_t offsets[]={0x630,0x680,0x684,0x688,0x68c,0x690,0x84c,0x898,0x89c,0x8c0,0x8c4};
        constexpr uint32_t values[]={2,0xc6ed0,525,155,620,0,0x00801080,0x10000000,0x10000000,0,0x40801080};
        for(unsigned i=0;i<11;++i)if(i!=omit_command)write(0x680000+offsets[i],4,values[i]);
        constexpr uint32_t timing[]={479,524,479,490,493,0,479,639,775,599,653,685,0,639};
        for(unsigned i=0;i<14;++i)if(i!=omit_timing)
            write(0x680000+(i<7?0x800+4*i:0x820+4*(i-7)),4,timing[i]);
        write(0x680848,4,0x10100111);write(0x680880,4,0x21101100);
        crtc(0x13,0x40);crtc(0x19,0x20);crtc(0x25,0);crtc(0x28,0x83);
        write(0x600800,4,start);write(0x680600,4,0x00100030);
        write(0x0c03c4,1,1);write(0x0c03c5,1,1);
        // Component fixture supplies the established clock owner's signals;
        // no register read, AV ACK or guest clock is synthesized by the copy.
        gpu->display_clock_signals(true,false,false,false);
        if(gamma)upload_gamma();
        gpu->set_mem_access([this](uint32_t a,void* p,size_t n){return read(a,p,n);},
            [this](uint32_t,const void*,size_t){++writes;return false;});
        CHECK(fault.empty());
    }
    void write(uint32_t offset,int size,uint32_t value){gpu->mmio_write(0xfd000000+offset,size,value);}
    void crtc(uint32_t index,uint32_t value){write(0x6013d4,1,index);write(0x6013d5,1,value);}
    void upload_gamma(bool identity=true,unsigned count=768) {
        write(0x6813c8,1,0);
        for(unsigned n=0;n<count;++n)write(0x6813c9,1,(!identity && n==0)?1:n/3);
    }
    bool read(uint32_t address,void* output,size_t size) {
        ++reads;
        if((fail_read && reads==fail_read) || uint64_t(address)+size>ram.size())return false;
        std::memcpy(output,ram.data()+address,size);return true;
    }
    bool copy(std::vector<Pixel::Ddr12>& output) {
        return gpu->copy_digital_encoder_input(output,
            [this](uint32_t a,void* p,size_t n){return read(a,p,n);},error);
    }
    void rejects_without_effects(const char* reason,bool no_reads=true) {
        const auto before=gpu->framebuffer_diagnostic_state();
        const auto memory=ram;const unsigned prior_reads=reads,prior_writes=writes;
        std::vector<Pixel::Ddr12> output{{0x123,0x456},{0x789,0xabc}};
        const auto original=output;const auto prior_fault=fault;
        CHECK(!copy(output));CHECK(error.find(reason)!=std::string::npos);
        CHECK(same(output,original) && ram==memory && writes==prior_writes && fault==prior_fault);
        if(no_reads)CHECK(reads==prior_reads);
        const auto after=gpu->framebuffer_diagnostic_state();
        CHECK(before.vblanks==after.vblanks && before.starts==after.starts &&
              before.methods==after.methods && before.published==after.published && before.retired==after.retired);
        CHECK(before.gamma==after.gamma && before.commands.known==after.commands.known &&
              before.commands.received==after.commands.received && before.tiles==after.tiles);
    }
};
static void receiver_cases() {
    std::string fault;
    xlle::Cx25871Control encoder([&](const std::string& why){fault=why;});
    Pixel::YCrCb output{1,2,3};
    CHECK(!encoder.receive_sd601_pixel({0xcda,0xbef},output) && same(output,{1,2,3}));
    CHECK(encoder.write_byte(0x32,0x28));
    CHECK(!encoder.receive_sd601_pixel({0xcda,0xbef},output) && same(output,{1,2,3}));
    CHECK(encoder.write_byte(0xc6,0x9c));
    // Mode alone does not establish the edge-correction controls. Actual
    // original74=01/32=28 disables DATDLY/DATSWP/DATDLY_RE (100381B2-14).
    CHECK(!encoder.receive_sd601_pixel({0xcda,0xbef},output) && same(output,{1,2,3}));
    CHECK(encoder.write_byte(0x74,1));
    const auto before=encoder.control_observation();const uint64_t epoch=encoder.timing_epoch();
    CHECK(encoder.receive_sd601_pixel({0xcda,0xbef},output) && same(output,{0xab,0xcd,0xef}));
    CHECK(encoder.receive_sd601_pixel({0xff0,0x0ff},output) && same(output,{1,254,254}));
    CHECK(encoder.receive_sd601_pixel({0x00f,0xf00},output) && same(output,{254,1,1}));
    CHECK(encoder.receive_sd601_pixel({0xfe0,0x101},output) && same(output,{1,254,1}));
    for(const auto invalid:{Pixel::Ddr12{0x1000,0},Pixel::Ddr12{0,0x1000}}) {
        const auto retained=output;CHECK(!encoder.receive_sd601_pixel(invalid,output));
        CHECK(same(output,retained));
    }
    const auto after=encoder.control_observation();
    CHECK(before.values==after.values && before.known==after.known && encoder.timing_epoch()==epoch && fault.empty());
    const uint8_t corrections[][2]={{0x74,0x41},{0x74,0x81},{0x32,0x2c}};
    for(const auto& correction:corrections) {
        CHECK(encoder.write_byte(correction[0],correction[1]));
        const auto retained=output;
        CHECK(!encoder.receive_sd601_pixel({0xcda,0xbef},output));CHECK(same(output,retained));
        CHECK(encoder.write_byte(correction[0],correction[0]==0x74?1:0x28));
    }
    CHECK(encoder.write_byte(0x32,0x20)); // known high IN_MODE bit0: not1100
    const auto retained=output;CHECK(!encoder.receive_sd601_pixel({0xcda,0xbef},output));CHECK(same(output,retained));
    CHECK(encoder.write_byte(0xba,0x80)); // real reset invalidates the input selection
    CHECK(!encoder.receive_sd601_pixel({0xcda,0xbef},output));CHECK(same(output,retained));
}
int main() {
    receiver_cases();
    {
        DigitalFixture f;
        constexpr std::array<Pixel::Rgb,8> rgb{{{255,255,255},{255,255,0},{0,255,255},{0,255,0},
                                              {255,0,255},{255,0,0},{0,0,255},{0,0,0}}};
        // Expected Y,Cr,Cb words from the standard bars, manually DDR-packed.
        constexpr std::array<Pixel::Ddr12,8> expected{{{0x80e,0xb80},{0x92d,0x210},{0x10a,0xaa6},{0x229,0x136},
                                                     {0xde6,0xaca},{0xf05,0x15a},{0x6e2,0x9f0},{0x801,0x080}}};
        for(unsigned i=0;i<640*480;++i) {
            const auto color=rgb[i%8];auto* p=f.ram.data()+f.start+4*i;
            p[0]=color.b;p[1]=color.g;p[2]=color.r;p[3]=uint8_t(i); // alpha is storage, not an input channel
        }
        const auto memory=f.ram;std::vector<Pixel::Ddr12> pixels;
        CHECK(f.copy(pixels) && f.error.empty());CHECK(pixels.size()==640*480);
        for(size_t i=0;i<pixels.size();++i)
            CHECK(pixels[i].falling==expected[i%8].falling && pixels[i].rising==expected[i%8].rising);
        CHECK(f.reads==480 && f.writes==0 && f.ram==memory && f.fault.empty());
        // Legacy full-output route remains guarded even when digital input is available.
        std::vector<uint8_t> legacy{1,2,3};xlle::Nv2a::ScanoutInfo info;
        const unsigned reads=f.reads;CHECK(!f.gpu->capture_scanout(legacy,info,f.error));
        CHECK(f.error.find("display processing")!=std::string::npos && f.reads==reads && legacy==std::vector<uint8_t>({1,2,3}));
        // Known screen-off produces nominal digital black with no memory owner/read.
        f.write(0x0c03c4,1,1);f.write(0x0c03c5,1,0x21);
        CHECK(f.gpu->copy_digital_encoder_input(pixels,{},f.error));
        for(const auto pixel:pixels)CHECK(pixel.falling==0x801 && pixel.rising==0x080);
        CHECK(f.reads==reads && f.ram==memory && f.writes==0);
    }
    {DigitalFixture f(10);f.rejects_without_effects("fixed NTSC");}
    {DigitalFixture f(11,13);f.rejects_without_effects("fixed NTSC");}
    {DigitalFixture f;f.write(0x680630,4,0);f.rejects_without_effects("fixed NTSC");}
    {DigitalFixture f;f.write(0x680880,4,0x21121111);f.rejects_without_effects("fixed NTSC");}
    {DigitalFixture f;f.write(0x680848,4,0x10000111);f.rejects_without_effects("fixed NTSC");}
    {DigitalFixture f;f.gpu->display_clock_signals(false,false,false,false);f.rejects_without_effects("clock/routing");}
    {DigitalFixture f;f.crtc(0x28,3);f.rejects_without_effects("clock/routing");}
    {DigitalFixture f;f.crtc(0x13,0x41);f.rejects_without_effects("storage format");}
    {DigitalFixture f;f.write(0x680600,4,0x30);f.rejects_without_effects("storage format");}
    {DigitalFixture f(11,14,false);f.rejects_without_effects("identity DAC");}
    {DigitalFixture f;f.upload_gamma(true,767);f.rejects_without_effects("identity DAC");}
    {DigitalFixture f;f.upload_gamma(false);f.rejects_without_effects("identity DAC");}
    {DigitalFixture f;f.fail_read=2;f.rejects_without_effects("RAM copy failed",false);CHECK(f.reads==2);}
    std::printf("NV2A fixed-profile digital input and actual CX receiver: %s\n",failures?"FAIL":"PASS");
    return failures?1:0;
}
