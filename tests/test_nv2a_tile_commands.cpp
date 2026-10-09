// Actual PFIFO/0x62/0x9F execution of the independently captured Xbox
// BlitBeyondWidth sequence. No tile-layout helper is called by this fixture.
// Source procedure: nxdk_pgraph_tests (MIT); hardware expectations: CC0
// Xbox 1.0 golden. Revisions/notices are in THIRD_PARTY_ATTRIBUTION.md.
#include "machine/nv2a.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

static int failures;
#define CHECK(c) do { if(!(c)){std::printf("FAIL line %d: %s\n",__LINE__,#c);++failures;} } while(0)
struct TileFixture {
    static constexpr uint32_t source=0x10000,target=0x300000,display=0x500000;
    static constexpr uint32_t size=640*480*4;
    std::vector<uint8_t> ram=std::vector<uint8_t>(8*1024*1024,0xcc);
    std::string failure;
    uint64_t writes=0;
    uint32_t cursor=0,deny_begin=0,deny_end=0;
    std::unique_ptr<xlle::Nv2a> gpu;
    TileFixture() : gpu(std::make_unique<xlle::Nv2a>([this](const std::string& why){failure=why;})) {
        gpu->set_mem_access(
            [this](uint32_t a,void* p,size_t n) {
                if(uint64_t(a)+n>ram.size())return false;
                std::memcpy(p,ram.data()+a,n);return true;
            },
            [this](uint32_t a,const void* p,size_t n) {
                if(uint64_t(a)+n>ram.size() || (a<deny_end && uint64_t(a)+n>deny_begin))return false;
                std::memcpy(ram.data()+a,p,n);writes+=n;return true;
            });
        for(uint32_t y=0;y<512;++y)for(uint32_t x=0;x<1024;++x)
            put(source+(y*1024+x)*4,0xff000000u|((255-y*255/512)<<16)|((x*255/1024)<<8)|(y*255/512));
        const uint32_t setup[][2]={
            {0x700008,1},{0x70000c,0x80010020},{0x700200,0x62},
            {0x700010,2},{0x700014,0x80010021},{0x700210,0x9f},
            {0x700018,3},{0x70001c,0x80010030},
            {0x700300,0x3d},{0x700304,uint32_t(ram.size()-1)},{0x700308,0},
            {0x700180,0x3d},{0x700184,0xffff},{0x700188,0x1000},
            {0x002504,1},{0x003204,0x100},{0x00322c,0x18},{0x003244,0},
            {0x003220,1},{0x003250,1},{0x003200,1},{0x400720,1},
            {0x400880,0x28c3ff}};
        for(const auto& pair:setup)write(pair[0],pair[1]);
    }
    void put(uint32_t a,uint32_t value){std::memcpy(ram.data()+a,&value,4);}
    uint32_t get(uint32_t a)const {uint32_t v;std::memcpy(&v,ram.data()+a,4);return v;}
    void write(uint32_t r,uint32_t v){gpu->mmio_write(0xfd000000u+r,4,v);}
    void emit(uint32_t m,uint32_t v){put(0x1000+cursor,0x40000u|m);put(0x1004+cursor,v);cursor+=8;}
    void kick(){write(0x800040,cursor);}
    void tile(unsigned index,uint32_t base,uint32_t bytes,uint32_t pitch,uint32_t flags) {
        const uint32_t words[]={base|flags,base+bytes-1,pitch};
        // Original driver programs limit/pitch before enabling the base.
        for(unsigned k : {1u,2u,0u}) {
            write(0x100240+index*16+k*4,words[k]);
            write(0x400900+index*16+k*4,words[k]);
            write(0x400750,0xea0010+index*4+k*0x20);write(0x400754,words[k]);
        }
    }
    void blit(uint32_t src,uint32_t dst,uint32_t src_pitch,uint32_t dst_pitch,
              uint32_t width,uint32_t height) {
        emit(0,1);emit(0x184,3);emit(0x188,3);emit(0x300,0x0a);
        emit(0x304,src_pitch|(dst_pitch<<16));emit(0x308,src);emit(0x30c,dst);
        emit(0,2);emit(0x19c,1);emit(0x2fc,3);
        emit(0x300,0);emit(0x304,0);emit(0x308,width|(height<<16));kick();
    }
};

static std::vector<uint8_t> hardware_sequence() {
    TileFixture f;
    f.tile(0,f.target,f.size,2560,3);
    f.blit(f.source,f.target,4096,2560,1024,512);
    CHECK(f.failure.empty());CHECK(f.gpu->image_blit_copies()==1);
    for(uint32_t i=0;i<4096;i+=4)CHECK(f.get(f.target+f.size+i)==0xcccccccc);
    // Non-tiled destination stride permits overdraw; this actual second
    // command must overwrite the nominal 128x128 destination's guard.
    constexpr uint32_t outside=TileFixture::source+1024*512*4;
    f.blit(f.source,outside,4096,512,256,256);
    CHECK(f.failure.empty());CHECK(f.get(outside+128*128*4)==0xffc0003f);
    // Removing the tile exposes physical bytes, without rearranging them.
    f.tile(0,0,1,0,0);
    f.blit(f.target+4096,f.display,2560,2560,640,480);
    CHECK(f.failure.empty());CHECK(f.gpu->image_blit_copies()==3);
    CHECK(f.gpu->mmio_read(0xfd800044,4)==f.cursor);
    std::vector<uint8_t> output(f.size);
    for(uint32_t i=0;i<f.size;i+=4) {
        const auto v=f.get(f.display+i);
        output[i]=uint8_t(v>>16);output[i+1]=uint8_t(v>>8);
        output[i+2]=uint8_t(v);output[i+3]=uint8_t(v>>24);
    }
    if(!f.failure.empty())std::printf("GPU failure: %s\n",f.failure.c_str());
    return output;
}

static void boundaries() {
    // Real reverse transfer: a tiled source reproduces the original pixels.
    TileFixture f;
    f.tile(0,f.target,f.size,2560,3);
    f.blit(f.source,f.target,4096,2560,640,480);
    f.blit(f.target,f.display,2560,2560,640,480);
    CHECK(f.failure.empty());
    for(uint32_t y : {0u,1u,15u,16u,479u})for(uint32_t x : {0u,3u,4u,63u,64u,639u})
        CHECK(f.get(f.display+(y*640+x)*4)==f.get(f.source+(y*1024+x)*4));
    // Inclusive limit: the last pixel changes; the following guard does not.
    f.blit(f.source,f.target+f.size-4,4096,2560,2,1);
    CHECK(f.failure.empty() && f.get(f.target+f.size)==0xcccccccc);
    f.blit(f.target+f.size-4,f.display,2560,2560,1,1);
    CHECK(f.failure.empty() && f.get(f.display)==f.get(f.source));
    // Bank mismatch must fail before changing output or claiming a copy.
    auto count=f.gpu->image_blit_copies();auto writes=f.writes;
    f.write(0x100248,1280);
    f.blit(f.source,f.target,4096,2560,1,1);
    CHECK(f.failure.find("banks disagree")!=std::string::npos);
    CHECK(f.writes==writes && f.gpu->image_blit_copies()==count);

    TileFixture protected_ram;
    protected_ram.tile(0,f.target,f.size,2560,3);
    protected_ram.deny_begin=f.target;protected_ram.deny_end=f.target+f.size;
    protected_ram.blit(f.source,f.target,4096,2560,1,1);
    CHECK(protected_ram.failure.find("write failed")!=std::string::npos);
    CHECK(!protected_ram.writes && !protected_ram.gpu->image_blit_copies());

    TileFixture unsupported;
    unsupported.tile(0,f.target,f.size,2560,1); // color-tile enable is bit 0
    unsupported.write(0x700200,0x97);
    unsupported.emit(0,1);unsupported.emit(0x17fc,5);unsupported.kick();
    CHECK(unsupported.failure.find("3D primitive")!=std::string::npos);
    CHECK(!unsupported.writes);
}

static void clear_and_scanout() {
    TileFixture f;
    constexpr uint32_t depth=0x680000;
    f.tile(0,f.target,f.size,2560,1); // bank zero exercises the color configuration
    f.tile(1,depth,f.size,2560,3);
    f.blit(f.source,f.target,4096,2560,640,480);
    f.write(0x700020,4);f.write(0x700024,0x80010022);f.write(0x700220,0x97);
    f.emit(0,4);f.emit(0x194,3);f.emit(0x198,3);
    f.emit(0x208,0x128);f.emit(0x20c,0x0a000a00);
    f.emit(0x210,f.target);f.emit(0x214,depth);
    f.emit(0x1d90,0xa1b2c3d4);f.emit(0x1d8c,0xabcdef42);
    f.emit(0x1d98,(66u<<16)|63);f.emit(0x1d9c,(17u<<16)|15);f.emit(0x1d94,0xf3);
    // Masked read/modify/write across partition and row-group boundaries.
    f.emit(0x1d90,0xffeeddcc);f.emit(0x1d98,(64u<<16)|64);
    f.emit(0x1d9c,(16u<<16)|16);f.emit(0x1d94,0x10);f.kick();
    CHECK(f.failure.empty());
    f.blit(f.target,f.display,2560,2560,640,480);
    CHECK(f.failure.empty());
    CHECK(f.get(f.display+(15*640+63)*4)==0xa1b2c3d4);
    CHECK(f.get(f.display+(16*640+64)*4)==0xa1eec3d4);
    CHECK(f.get(f.display+(17*640+66)*4)==0xa1b2c3d4);
    CHECK(f.get(f.display+(14*640+63)*4)==f.get(f.source+(14*1024+63)*4));
    f.blit(depth,f.display,2560,2560,640,480);
    CHECK(f.failure.empty());
    CHECK(f.get(f.display+(16*640+64)*4)==0xabcdef42);
    CHECK(f.get(f.display+(14*640+63)*4)==0xcccccccc);
    auto crtc=[&](uint8_t index,uint8_t value){
        f.gpu->mmio_write(0xfd6013d4,1,index);f.gpu->mmio_write(0xfd6013d5,1,value);
    };
    crtc(0x13,0x40);crtc(0x19,0x20);crtc(0x28,3); // 2560-byte pitch, 32 bpp
    f.write(0x680800,479);f.write(0x680820,639);f.write(0x600800,f.target);
    xlle::Nv2a::ScanoutInfo info;std::vector<uint8_t> rgba;std::string error;
    CHECK(f.gpu->capture_scanout(rgba,info,error));CHECK(error.empty());
    if(rgba.size()==f.size) {
        const uint8_t expected[]={0xee,0xc3,0xd4,255};
        CHECK(!std::memcmp(rgba.data()+(16*640+64)*4,expected,4));
        const uint8_t background[]={255,0,0,255};
        CHECK(!std::memcmp(rgba.data(),background,4));
    } else CHECK(false);
    size_t direct_bytes=0;
    CHECK(!f.gpu->scanout_direct_bytes(info,direct_bytes,error));
    CHECK(error.find("translated capture")!=std::string::npos && !direct_bytes);
    if(!f.failure.empty())std::printf("clear/scanout failure: %s\n",f.failure.c_str());
}

int main(int argc,char** argv) {
    const auto output=hardware_sequence();boundaries();clear_and_scanout();
    if(argc==3 && !std::strcmp(argv[1],"--hardware-rgba")) {
        std::ofstream file(argv[2],std::ios::binary);
        file.write(reinterpret_cast<const char*>(output.data()),output.size());CHECK(bool(file));
    } else if(argc!=1)return 2;
    if(!failures)std::puts("test_nv2a_tile_commands: ok");
    return failures?1:0;
}
