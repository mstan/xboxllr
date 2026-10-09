#pragma once
#include "apu_rate_fixture.hpp"

// Authored inputs only; independent native transport consumes the same PCM.
struct ApuCompletionFixture : ApuRateFixture {
    static constexpr unsigned notify=0x16000+16*(2+voice*4);
    unsigned completions=0;
    ApuCompletionFixture(unsigned ch,unsigned size,bool loop=false,bool adpcm=false)
        :ApuRateFixture(ch,size,loop,adpcm) {
        reg(0x20304,word(object+4)|0xe000|6|(7<<5));
        reg(0x20300,(1<<5)|(2<<10)|(3<<16)|(4<<21)|(5<<26));
        reg(0x20360,0x000f000f);reg(0x20364,0xffffffff);reg(0x20368,0xffffffff);
        reg(0x115c,0x16000);reg(0x1004,0x61);ram[notify+15]=0x80;
        apu.set_irq([this](int line,bool high){
            CHECK(line==5);
            if(high){CHECK(!(word(object+0x54)&(1u<<21)) && ram[notify+15]==1);++completions;}
        });
        if(!adpcm) {
            for(unsigned n=0;n<frames;++n)for(unsigned c=0;c<channels;++c) {
                const int16_t value=n+32>=frames?(c?-16384:24576):(c?-4096:8192);
                std::memcpy(pcm.data()+(n*channels+c)*2,&value,2);
            }
            for(unsigned i=0;i<pcm.size();++i)ram[word(sge+((base+i)>>12)*8)+((base+i)&4095)]=pcm[i];
        }
    }
};
