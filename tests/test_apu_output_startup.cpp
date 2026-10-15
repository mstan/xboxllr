// Physical stopped-core observations, independent of the reference emulator's
// ignored OUTBUF methods. No playback, clock advance or hardware operations.
#include "machine/apu.hpp"
#include <cstdio>
#include <cstdlib>
#include <string>
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"output startup FAIL %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
struct Fixture {
    std::string error;
    xlle::McpxApu apu{[this](const std::string& value){error=value;}};
    Fixture(){CHECK(apu.initialize_kernel_audio_handoff());}
    void put(uint32_t off,uint32_t value){apu.mmio_write(0xfe800000+off,4,value);}
    uint32_t get(uint32_t off){return apu.mmio_read(0xfe800000+off,4);}
    void release(){put(0x1510,0);put(0x1100,0x100f);}
};
int main() {
    // Literal physical prefix: base0 resets end to100, length2000 restores
    // end2000, and base100 resets end200/cursor100 despite the preceding length.
    struct Row {uint32_t method,value,base,end,cursor;};
    const Row observed[]={{0x1000,0,0,0x100,0},{0x1004,0x2000,0,0x2000,0},
                          {0x1000,0x100,0x100,0x200,0x100}};
    for(unsigned life=0;life<3;++life) {
        Fixture f;f.put(0x3024,0);f.put(0x3028,0x2000);f.put(0x302c,0);
        // Distinct surrounding values detect accidentally touching another ring.
        f.put(0x3034,0x6000);f.put(0x3038,0x8000);f.put(0x303c,0x6100);
        f.put(0x4024,0x2000);f.put(0x4028,0x6000);f.put(0x402c,0x2200);
        f.release();
        for(const auto& row:observed) {
            f.put(0x20000+row.method,row.value);
            CHECK(f.error.empty() && f.get(0x3024)==row.base && f.get(0x3028)==row.end && f.get(0x302c)==row.cursor);
            CHECK(f.get(0x1300)==row.method && f.get(0x1304)==row.value && f.get(0x20010)==0x80);
            CHECK(f.get(0x3034)==0x6000 && f.get(0x3038)==0x8000 && f.get(0x303c)==0x6100);
            CHECK(f.get(0x4024)==0x2000 && f.get(0x4028)==0x6000 && f.get(0x402c)==0x2200);
            CHECK(f.get(0x3fffc)==0 && f.get(0x5fffc)==1 && f.get(0x2000)==7);
        }
        f.put(0x21004,0x2100);CHECK(f.apu.faulted()); // Observed physical trap is outside the admitted pair.
    }
    {Fixture f;for(uint32_t off:{0x3034u,0x3044u,0x3054u})CHECK(f.get(off)==0);
     f.get(0x3024);CHECK(f.apu.faulted());} // FIFO0 requires actual source configuration.
    {std::string error;xlle::McpxApu apu([&](const std::string& s){error=s;});
     apu.mmio_read(0xfe803034,4);CHECK(!error.empty());} // No implicit cold floor.
    {Fixture f;f.release();f.put(0x21004,0x2000);CHECK(f.apu.faulted());}
    {Fixture f;f.put(0x21000,0);CHECK(f.apu.faulted());} // FE is still held.
    {Fixture f;f.release();f.put(0x21000,0);f.put(0x3028,0x3000);f.put(0x21004,0x2000);CHECK(f.apu.faulted());}
    {Fixture f;f.apu.mmio_write(0xfe821001,2,0);CHECK(f.apu.faulted());}
    {Fixture f;f.apu.mmio_read(0xfe803035,1);CHECK(f.apu.faulted());}
    puts("PASS physical output-method composition, canonical FIFO ownership and startup guards");
}
