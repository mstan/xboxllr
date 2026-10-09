// Bounded MCPX GP DMA peripheral. Authored from the descriptor/register contract
// and published byte examples; no GPL implementation body is incorporated.
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <utility>

namespace xlle {
class ApuGpDma {
public:
    using Word=std::function<uint32_t*(uint32_t)>;
    using Bytes=std::function<bool(uint32_t,void*,uint32_t,bool)>;
    using Fault=std::function<void(const std::string&)>;
    ApuGpDma(Word word,Bytes bytes,Fault fault):word_(std::move(word)),bytes_(std::move(bytes)),fault_(std::move(fault)){}
    bool read(uint32_t address,uint32_t& value) {
        if(failed_)return false;
        switch(address) {
        case 0xffffc5:value=interrupts_;return true;
        case 0xffffd4:value=next_;return true;
        case 0xffffd6:value=control_;return true;
        default:return fail("unqualified GP peripheral read");
        }
    }
    bool write(uint32_t address,uint32_t value) {
        if(failed_)return false;
        switch(address) {
        case 0xffffc5:
            if(value&~0x80u)return fail("unqualified GP interrupt acknowledgement");
            interrupts_&=~value;return true;
        case 0xffffd4:
            if(value&~0x7fffu)return fail("invalid GP DMA node pointer");
            next_=value;return true;
        case 0xffffd6:
            switch(value) {
            case 1:control_=(control_&8)|16;return run();
            case 2:control_=(control_&8)|32;return true;
            case 3:control_|=8;return true;
            case 4:control_&=~8u;return run();
            default:return fail("unqualified GP DMA action");
            }
        default:return fail("unqualified GP peripheral write");
        }
    }
    uint64_t nodes()const{return nodes_;}
private:
    static uint32_t bank_end(uint32_t address) {
        if(address<0x1800)return 0x1800;
        if(address<0x2000)return 0x2000;
        if(address>=0x2800 && address<0x3800)return 0x3800;
        return 0;
    }
    bool fail(const char* why){failed_=true;fault_(why);return false;}
    bool run() {
        if(!(control_&16) || (control_&8))return true;
        for(unsigned limit=0;!(next_&0x4000);++limit) {
            if(limit==1024)return fail("GP DMA list exceeded node budget");
            if(next_+7>bank_end(next_))return fail("GP DMA descriptor crosses memory bank");
            uint32_t node[7];uint32_t* fields[7];
            for(unsigned i=0;i<7;++i) {
                fields[i]=word_(next_+i);
                if(!fields[i])return fail("GP DMA descriptor outside memory banks");
                node[i]=*fields[i]&0xffffff;
            }
            const uint32_t format=(node[1]>>10)&7,count=node[2];
            const bool output=(node[1]&2)!=0;
            // Reached effects use noninterleaved unit-stride circular scratch.
            // Other formats/buffers/unknown bits retain explicit boundaries.
            if((node[0]&~0x7fffu) || (node[1]&0x220d) || ((node[1]>>5)&15)!=14 ||
               (node[1]>>14)!=1 || (format!=2 && format!=6) || !count || count>4096)
                return fail("unqualified GP DMA descriptor format/extent");
            uint32_t* words[4096];
            if(uint64_t(node[3])+count>bank_end(node[3]))return fail("GP DMA samples cross memory bank");
            for(unsigned i=0;i<count;++i) {
                words[i]=word_(node[3]+i);
                if(!words[i])return fail("GP DMA sample outside memory banks");
            }
            const uint32_t size=node[6]+1;uint32_t offset=node[4];
            if(offset>=size)return fail("unqualified GP DMA initial circular offset");
            for(unsigned i=0;i<count;++i) {
                uint32_t sample=output?(*words[i]&0xffffff):0;
                if(output && format==2)sample<<=8;
                uint8_t bytes[4];
                for(unsigned b=0;b<4;++b)bytes[b]=uint8_t(sample>>(b*8));
                for(unsigned at=0;at<4;) {
                    const unsigned n=(size-offset)<(4-at)?size-offset:4-at;
                    if(!bytes_(node[5]+offset,bytes+at,n,output))return fail("GP scratch DMA failed");
                    at+=n;offset=(offset+n)%size;
                }
                if(!output) {
                    sample=uint32_t(bytes[0])|(uint32_t(bytes[1])<<8)|(uint32_t(bytes[2])<<16)|(uint32_t(bytes[3])<<24);
                    *words[i]=format==2?sample>>8:sample&0xffffff;
                }
            }
            if(node[1]&16)*fields[4]=offset;
            next_=node[0];++nodes_;
        }
        // End-of-list follows actual copies. Status reads never finish work or
        // stop an armed engine; STOP and interrupt acknowledgement are explicit.
        interrupts_|=0x80;return true;
    }
    Word word_;Bytes bytes_;Fault fault_;
    uint32_t next_=0,control_=32,interrupts_=0;
    uint64_t nodes_=0;bool failed_=false;
};
} // namespace xlle
