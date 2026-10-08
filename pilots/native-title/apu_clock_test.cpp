#include "machine/apu.hpp"
#include <cstdio>
#include <string>
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);return 1;}}while(0)
int main()
{
    std::string failure;
    xlle::McpxApu apu([&](const std::string& reason){failure=reason;});
    auto sample=[&](){return apu.mmio_read(0xfe80200cu,4);};
    apu.set_time(0,1000000000);CHECK(sample()==0);
    apu.set_time(20833,1000000000);CHECK(sample()==0);
    apu.set_time(20834,1000000000);CHECK(sample()==1);
    apu.set_time(1000000,1000000000);CHECK(sample()==48);
    for(int i=0;i<1000;++i)CHECK(sample()==48); // reads cannot advance time
    apu.set_time(1000000000,1000000000);CHECK(sample()==48000);
    apu.set_time(0xffffffffull,48000);CHECK(sample()==0xffffffffu);
    apu.set_time(0x100000000ull,48000);CHECK(sample()==0);
    apu.set_time(0x100000003ull,48000);CHECK(sample()==3);
    CHECK(failure.empty());
    apu.set_time(1,0);CHECK(!failure.empty());
    std::puts("PASS: shared APU sample clock, 48 samples/ms, sub-sample quantization, wrap, pure reads and frequency guard");
    return 0;
}
