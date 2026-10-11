// Raw SSL staging, bank ownership and fresh shared-RAM reads. Expected bytes
// come from authored PIO packets; no stream decoder or donor output is used.
#include "machine/apu.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <utility>

static unsigned failures=0;
#define CHECK(x) do{if(!(x)){std::printf("FAIL line %d: %s\n",__LINE__,#x);++failures;}}while(0)
struct Fixture {
    static constexpr unsigned table=0x8000;
    std::vector<uint8_t> ram=std::vector<uint8_t>(0x22000,0xa7);
    std::string error;
    std::vector<std::pair<uint32_t,size_t>> reads,writes;
    uint32_t fail_read=UINT32_MAX,fail_write=UINT32_MAX;
    unsigned irqs=0;
    xlle::McpxApu apu{[this](const std::string& why){error=why;}};
    Fixture() {
        apu.set_mem_access(
            [this](uint32_t a,void* p,size_t n){
                reads.emplace_back(a,n);
                if(a==fail_read || uint64_t(a)+n>ram.size())return false;
                std::memcpy(p,ram.data()+a,n);return true;
            },
            [this](uint32_t a,const void* p,size_t n){
                writes.emplace_back(a,n);
                if(a==fail_write || uint64_t(a)+n>ram.size())return false;
                std::memcpy(ram.data()+a,p,n);return true;
            });
        apu.set_irq([this](int line,bool){CHECK(line==5);++irqs;});
        reg(0x2034,table);
    }
    void reg(unsigned off,uint32_t value,int size=4){apu.mmio_write(0xfe800000+off,size,value);}
    void method(unsigned off,uint32_t value,int size=4){reg(0x20000+off,value,size);}
    void put(unsigned a,uint32_t value){for(unsigned i=0;i<4;++i)ram[a+i]=uint8_t(value>>(i*8));}
    void stage(unsigned slot,uint32_t offset,uint32_t control) {
        method(0x600+slot*8,offset);method(0x604+slot*8,control);
    }
    bool read(unsigned voice,unsigned bank,unsigned segment,xlle::ApuSslDescriptor& out) {
        return apu.read_ssl_segment(uint16_t(voice),bank,segment,out);
    }
};

static void staging_lifetimes() {
    for(unsigned life=0;life<8;++life) {
        Fixture f;auto expected=f.ram;
        for(unsigned vi=0;vi<4;++vi)for(unsigned bank=0;bank<2;++bank) {
            const unsigned voice[]={1,71,128,255};
            const unsigned first=(vi*2+bank)*64;
            f.method(0x190,first);f.method(0x2f8,voice[vi]);
            f.method(bank?0x35c:0x320,((first+7)<<8)|16);
            for(unsigned segment:{0u,5u,15u}) {
                const uint32_t offset=0x12340000|life<<12|vi<<8|bank<<7|segment;
                const uint32_t control=0xf0800000|life<<12|vi<<8|bank<<7|segment;
                f.stage(7+segment,offset,control);
                const unsigned physical=Fixture::table+(first+7+segment)*8;
                for(unsigned i=0;i<4;++i) {
                    expected[physical+i]=uint8_t(offset>>(i*8));
                    expected[physical+4+i]=uint8_t(control>>(i*8));
                }
                xlle::ApuSslDescriptor out;
                CHECK(f.read(voice[vi],bank,segment,out));
                CHECK(out.offset==offset && out.control==control);
                CHECK(f.reads.back()==std::make_pair(uint32_t(physical),size_t(8)));
            }
        }
        // Read all banks after the selected voice/window changed. A single
        // global binding cannot accidentally satisfy these independent owners.
        for(unsigned vi=0;vi<4;++vi)for(unsigned bank=0;bank<2;++bank) {
            const unsigned voice[]={1,71,128,255};
            xlle::ApuSslDescriptor out;
            CHECK(f.read(voice[vi],bank,5,out));
            CHECK(out.offset==(0x12340000u|life<<12|vi<<8|bank<<7|5));
            CHECK(out.control==(0xf0800000u|life<<12|vi<<8|bank<<7|5));
        }
        CHECK(f.ram==expected && f.error.empty() && !f.irqs);
        CHECK(f.writes.size()==48 && f.reads.size()==32);
        // Both voice and notifier storage are included in the full-RAM check.
        // No cursor, ACTIVE bit, output sample or completion was synthesized.
    }
}

static void rewrite_rebind_and_boundary() {
    Fixture f;
    f.method(0x2f8,71);f.method(0x320,(8176u<<8)|16);f.method(0x35c,(64u<<8)|1);
    f.method(0x190,8128);f.stage(63,0xffffffff,0xfedcba98);
    xlle::ApuSslDescriptor out;
    CHECK(f.read(71,0,15,out) && out.offset==0xffffffff && out.control==0xfedcba98);
    // Live CPU rewrite and table remap must both be visible without a cache.
    f.put(Fixture::table+8191*8,0x76543210);
    CHECK(f.read(71,0,15,out) && out.offset==0x76543210);
    const unsigned second=0x12000;
    f.put(second+8191*8,0x11223344);f.put(second+8191*8+4,0x55667788);
    f.reg(0x2034,second);
    CHECK(f.read(71,0,15,out) && out.offset==0x11223344 && out.control==0x55667788);
    auto expected=f.ram;
    f.stage(63,0xa1b2c3d4,0x09182736);
    for(unsigned i=0;i<4;++i) {
        expected[second+8191*8+i]=uint8_t(0xa1b2c3d4u>>(i*8));
        expected[second+8191*8+4+i]=uint8_t(0x09182736u>>(i*8));
    }
    CHECK(f.ram==expected && f.error.empty() && !f.irqs);
    // Separate B binding still resolves its own descriptor in the new table.
    f.put(second+64*8,0x778899aa);f.put(second+64*8+4,0x12345678);
    CHECK(f.read(71,1,0,out) && out.offset==0x778899aa && out.control==0x12345678);
    // Fresh device lifetime cannot inherit either bank or a staging window.
    Fixture fresh;out={0x13572468,0x24681357};
    CHECK(!fresh.read(71,0,0,out) && !fresh.error.empty());
    CHECK(out.offset==0x13572468 && out.control==0x24681357 && fresh.reads.empty());
}

static void failed_write(unsigned scenario) {
    Fixture f;f.method(0x2f8,71);
    switch(scenario) {
    case 0:break; // no explicit selected staging window
    case 1:f.method(0x190,1);break;
    case 2:f.method(0x190,8192);break;
    case 3:f.method(0x190,0);f.reg(0x2034,3);break;
    case 4:f.method(0x190,64);f.reg(0x2034,0xfffffe00);break;
    case 5:f.method(0x190,0);f.fail_write=Fixture::table;break;
    case 6:f.method(0x190,0);f.method(0x320,17);break;
    case 7:f.method(0x190,0);f.method(0x320,(8191u<<8)|2);break;
    case 8:f.method(0x190,0);f.method(0x18c,0);break;
    }
    auto before=f.ram;
    f.method(0x600,0x12345678);
    CHECK(!f.error.empty() && f.ram==before && !f.irqs && f.reads.empty());
    // A fault is terminal; later otherwise valid commands cannot mutate RAM.
    f.method(0x190,0);f.stage(0,1,2);CHECK(f.ram==before);
}

static void failed_reads_and_shapes() {
    for(unsigned scenario=0;scenario<7;++scenario) {
        Fixture f;f.method(0x2f8,71);f.method(0x320,(64u<<8)|1);
        unsigned voice=71,bank=0,segment=0;
        switch(scenario) {
        case 0:voice=256;break;
        case 1:bank=2;break;
        case 2:segment=1;break;
        case 3:f.method(0x320,64u<<8);break; // empty packet withdraws the bank
        case 4:f.reg(0x2034,0xfffffe00);break;
        case 5:f.fail_read=Fixture::table+64*8;break;
        case 6:f.reg(0x2034,1);break;
        }
        auto before=f.ram;xlle::ApuSslDescriptor out{123,456};
        CHECK(!f.read(voice,bank,segment,out));
        CHECK(!f.error.empty() && out.offset==123 && out.control==456);
        CHECK(f.ram==before && f.writes.empty() && !f.irqs);
    }
    for(unsigned method:{0x190u,0x320u,0x35cu,0x600u,0x604u,0x601u}) {
        Fixture f;auto before=f.ram;
        f.method(method,0x11223344,method==0x601?4:2);
        CHECK(!f.error.empty() && f.ram==before && f.writes.empty());
    }
    Fixture stream;stream.reg(0x202c,0x1000);stream.method(0x2f8,71);
    const unsigned object=0x1000+71*128;
    stream.put(object+4,0x51000000);stream.put(object+0x54,1u<<21);
    auto before=stream.ram;float samples[4]={9,9,9,9};uint32_t count=99;bool ended=true;
    CHECK(!stream.apu.source_frames(71,samples,2,count,ended));
    CHECK(count==0 && !ended && !stream.error.empty() && stream.ram==before);
    CHECK(samples[0]==9 && samples[3]==9); // no stream decoder was admitted
}

int main() {
    staging_lifetimes();rewrite_rebind_and_boundary();
    for(unsigned scenario=0;scenario<9;++scenario)failed_write(scenario);
    failed_reads_and_shapes();
    if(failures)return 1;
    std::puts("PASS: SSL raw RAM staging; 8 lifetimes, 256 fresh descriptors, distinct voice/A-B ownership, rewrite/rebind, last entry, terminal guards; no stream advancement/output qualification");
}
