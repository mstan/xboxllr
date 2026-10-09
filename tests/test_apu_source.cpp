// VP source/DMA boundary before pitch, envelopes, mixing and completion delivery.
// Expected waveforms and index sequences are independent of the implementation.
#include "machine/apu.hpp"
#include <cmath>
#include <cstring>
#include <string>
#include <vector>
#include <cstdio>

static int failures;
#define CHECK(x) do {if(!(x)){std::printf("FAIL line %d: %s\n",__LINE__,#x);++failures;}}while(0)
struct Fixture {
    std::vector<uint8_t> ram=std::vector<uint8_t>(0x12000);
    std::string error;
    xlle::McpxApu apu{[this](const std::string& why){error=why;}};
    static constexpr uint32_t voice=7,table=0x1000,object=table+voice*128,sge=0x3000;
    unsigned reads=0;
    Fixture() {
        apu.set_mem_access(
            [this](uint32_t a,void* out,size_t n){
                if(uint64_t(a)+n>ram.size())return false;
                std::memcpy(out,ram.data()+a,n);++reads;return true;
            },
            [this](uint32_t a,const void* in,size_t n){
                if(uint64_t(a)+n>ram.size())return false;
                std::memcpy(ram.data()+a,in,n);return true;
            });
        reg(0x202c,table);reg(0x2030,sge);reg(0x202f8,voice);
        put(sge,0x8000);put(sge+8,0x5000);put(sge+16,0xb000);
        put(object+0x54,1u<<21);put(object+0x58,0xa5000000);
    }
    void put(uint32_t a,uint32_t v){std::memcpy(ram.data()+a,&v,4);}
    uint32_t word(uint32_t a){uint32_t v;std::memcpy(&v,ram.data()+a,4);return v;}
    void reg(uint32_t offset,uint32_t value){apu.mmio_write(0xfe800000+offset,4,value);}
    uint32_t physical(uint32_t a){return word(sge+(a>>12)*8)+(a&4095);}
    void sample(uint32_t a,int64_t value,unsigned width){
        for(unsigned b=0;b<width;++b)ram[physical(a+b)]=uint8_t(uint64_t(value)>>(8*b));
    }
    bool pull(float* out,uint32_t capacity,uint32_t& count,bool& ended){
        return apu.source_frames(voice,out,capacity,count,ended);
    }
};

static void waveforms() {
    for(unsigned type=0;type<4;++type)for(unsigned channels:{1u,2u}) {
        Fixture f;
        const unsigned width=type==0?1:type==1?2:4,bits=type==0?8:type==1?16:type==2?24:32;
        const unsigned container=type==0?0:type==1?1:3;
        const uint32_t format=(container<<30)|(type<<28)|((channels-1)<<27)|((channels-1)<<16);
        const uint32_t first=4093; // Split channels and even a single sample across nonadjacent pages.
        f.reg(0x20304,format);f.reg(0x203a0,first);f.reg(0x203dc,64);
        const int64_t amplitude=int64_t(1)<<(bits-1);
        const int64_t values[]={-amplitude,-amplitude/2,0,amplitude/2,amplitude-1};
        for(unsigned i=0;i<65;++i)for(unsigned c=0;c<channels;++c) {
            int64_t value=values[(i+c)%5];if(type==0)value+=128;
            // High container byte is irrelevant for S24.
            if(type==2)value=int64_t((uint64_t(value)&0xffffff)|0x6d000000);
            f.sample(first+(i*channels+c)*width,value,width);
        }
        auto before=f.ram;
        unsigned consumed=0;
        for(unsigned request:{1u,30u,1u,32u,3u}) {
            float output[70];for(float& v:output)v=123;
            uint32_t count=99;bool ended=true;
            CHECK(f.pull(output,request,count,ended));
            const unsigned wanted=request<65-consumed?request:65-consumed;
            CHECK(count==wanted);CHECK(ended==(consumed+wanted==65));
            for(unsigned n=0;n<count;++n)for(unsigned c=0;c<2;++c) {
                float expected=float(double(values[(consumed+n+(channels==2?c:0))%5])/double(amplitude));
                CHECK(output[n*2+c]==expected);
            }
            for(unsigned i=count*2;i<70;++i)CHECK(output[i]==123); // no fabricated padding
            consumed+=count;
            CHECK(f.word(Fixture::object+0x58)==(0xa5000000| (consumed==65?64:consumed)));
            // Source pull is not yet the envelope/notifier/IRQ pipeline owner.
            CHECK(f.word(Fixture::object+0x54)==(1u<<21));
        }
        CHECK(consumed==65 && f.error.empty());
        for(unsigned i=0;i<f.ram.size();++i)
            if(i<Fixture::object+0x58 || i>=Fixture::object+0x5c)CHECK(f.ram[i]==before[i]);
    }
}

static void controls() {
    Fixture f;f.reg(0x20304,0x52000000); // mono S16 loop
    f.reg(0x203a0,0);f.reg(0x203a4,2);f.reg(0x203dc,4);
    for(unsigned n=0;n<5;++n)f.sample(n*2,int64_t(n)*4096,2);
    float output[40];uint32_t count;bool ended;
    CHECK(f.pull(output,14,count,ended));CHECK(count==14 && !ended);
    const unsigned expected[]={0,1,2,3,4,2,3,4,2,3,4,2,3,4};
    for(unsigned n=0;n<14;++n)CHECK(output[n*2]==expected[n]*0.125f && output[n*2+1]==output[n*2]);
    CHECK(f.word(Fixture::object+0x58)==0xa5000002);
    f.reg(0x20140,Fixture::voice|(1u<<18));auto before=f.ram;
    output[0]=91;CHECK(f.pull(output,5,count,ended));CHECK(!count && !ended && output[0]==91 && f.ram==before);
    f.reg(0x20140,Fixture::voice);f.reg(0x202fc,1);before=f.ram;
    CHECK(f.pull(output,5,count,ended));CHECK(!count && !ended && f.ram==before);
    f.reg(0x203d8,3);f.reg(0x202fc,0);
    f.sample(6,-8192,2); // caller write is observed on the next source consumption
    CHECK(f.pull(output,1,count,ended));CHECK(count==1 && !ended && output[0]==-0.25f);
    CHECK(f.word(Fixture::object+0x58)==0xa5000004);
    f.put(Fixture::object+0x54,0);before=f.ram;
    CHECK(f.pull(output,10,count,ended));CHECK(!count && !ended && f.ram==before);
    // Stop/restart ownership is an input to this stage; seek restores the desired cursor.
    f.reg(0x203d8,0);f.put(Fixture::object+0x54,1u<<21);
    CHECK(f.pull(output,1,count,ended));CHECK(count==1 && output[0]==0 && !ended);
    CHECK(f.error.empty());
}

static void boundaries() {
    // End index zero is one actual frame, including a repeated one-frame loop.
    for(bool loop:{false,true}) {
        Fixture f;f.reg(0x20304,loop?0x02000000:0);f.reg(0x203dc,0);f.sample(0,255,1);
        float out[8];uint32_t n;bool end;
        CHECK(f.pull(out,4,n,end));CHECK(n==(loop?4u:1u) && end==!loop);
        for(unsigned i=0;i<n*2;++i)CHECK(out[i]==127.0f/128);
    }
    for(unsigned kind=0;kind<6;++kind) {
        Fixture f;f.reg(0x20304,0x50000000);f.reg(0x203dc,2);
        if(kind==0)f.reg(0x20304,0x90000000); // ADPCM not admitted as PCM
        if(kind==1)f.reg(0x20304,0x51000000); // SSL stream
        if(kind==2)f.reg(0x203d8,3);         // cursor outside source extent
        if(kind==3)f.reg(0x2030,0xfffffff8),f.reg(0x203a0,4096); // descriptor wrap
        if(kind==4)f.put(Fixture::sge,0x12000); // data outside RAM
        if(kind==5)f.put(Fixture::sge,0x8001); // unqualified SGE low bits
        const auto before=f.ram;float out[8]={42};uint32_t n=99;bool end=true;
        CHECK(!f.pull(out,4,n,end));CHECK(!n && !end && !f.error.empty());
        CHECK(out[0]==42 && f.ram==before);
        CHECK(!f.pull(out,1,n,end));CHECK(f.ram==before);
    }
    // A failed second page read cannot invent the second stereo sample or advance CBO.
    Fixture f;f.reg(0x20304,0x58010000);f.reg(0x203a0,4094);f.reg(0x203dc,1);
    f.sample(4094,1234,2);f.put(Fixture::sge+8,0x12000);
    float out[4]={42};uint32_t n;bool end;auto before=f.ram;
    CHECK(!f.pull(out,1,n,end));CHECK(!n && !end && out[0]==42 && f.ram==before);
}

int main() {
    waveforms();controls();boundaries();
    std::printf("APU source: %s (PCM formats/channels, scatter DMA, exact end, loops, pause/lock/seek, faults)\n",failures?"FAILED":"PASS");
    return failures?1:0;
}
