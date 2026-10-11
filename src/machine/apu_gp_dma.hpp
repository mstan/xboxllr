// Bounded MCPX GP DMA peripheral. Authored from the descriptor/register contract
// and published byte examples; no GPL implementation body is incorporated.
#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include "machine/apu_dsp_layout.hpp"

namespace xlle {
class ApuDspDma {
public:
    using Word=std::function<uint32_t*(uint32_t)>;
    using Bytes=std::function<bool(uint32_t,void*,uint32_t,bool)>;
    using Fifo=std::function<bool(unsigned,void*,uint32_t,bool)>;
    using Fault=std::function<void(const std::string&)>;
    using ProgramWrite=std::function<bool()>;
    using MixWrite=std::function<bool()>;
    ApuDspDma(Word word,Bytes bytes,Fifo fifo,Fault fault,ProgramWrite program_write={},MixWrite mix_write={},ApuDspKind kind=ApuDspKind::Gp)
        :word_(std::move(word)),bytes_(std::move(bytes)),fifo_(std::move(fifo)),
         fault_(std::move(fault)),program_write_(std::move(program_write)),mix_write_(std::move(mix_write)),kind_(kind){}
    bool read(uint32_t address,uint32_t& value) {
        if(failed_)return false;
        switch(address) {
        case 0xffffc5:value=interrupts_;return true;
        case 0xffffd4:value=next_;return true;
        case 0xffffd5:value=start_;return true;
        case 0xffffd6:value=control_;return true;
        case 0xffffd7:value=0;return true;
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
        case 0xffffd5:
            if(value&~0x3fffu)return fail("invalid GP DMA start pointer");
            start_=value;return true;
        case 0xffffd7:
            if(value)return fail("unqualified GP DMA configuration");
            return true;
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
    uint32_t bank_end(uint32_t address)const{return apu_dsp_bank_end(kind_,address);}
    bool fail(const char* why){
        failed_=true;std::string reason(why);
        if(kind_==ApuDspKind::Ep)for(size_t at=0;(at=reason.find("GP",at))!=std::string::npos;at+=2)reason.replace(at,2,"EP");
        fault_(reason);return false;
    }
    bool run() {
        if(!(control_&16) || (control_&8))return true;
        for(unsigned limit=0;!(next_&0x4000);++limit) {
            if(limit==1024)return fail("GP DMA list exceeded node budget");
            // Original DB/EB linear builders own exactly five words. The
            // following words may already be another descriptor; circular
            // base/size fields are not part of a buffer15 transfer.
            if(next_+2>bank_end(next_))return fail("GP DMA descriptor crosses memory bank");
            uint32_t node[7]{};uint32_t* fields[7]{};
            for(unsigned i=0;i<2;++i) {
                fields[i]=word_(next_+i);
                if(!fields[i])return fail("GP DMA descriptor outside memory banks");
                node[i]=*fields[i]&0xffffff;
            }
            const uint32_t format=(node[1]>>10)&7,buffer=(node[1]>>5)&15;
            const bool linear=buffer==15,circular=buffer==14;
            const unsigned field_count=circular?7:(linear?5:4);
            if(next_+field_count>bank_end(next_))return fail("GP DMA descriptor crosses memory bank");
            for(unsigned i=2;i<field_count;++i) {
                fields[i]=word_(next_+i);
                if(!fields[i])return fail("GP DMA descriptor outside memory banks");
                node[i]=*fields[i]&0xffffff;
            }
            const uint32_t count=node[2];
            const bool output=(node[1]&2)!=0;
            // Noninterleaved unit-stride scratch and FIFO transport. Linear
            // offset writeback is not reached by the original builders and
            // lacks independent evidence; keep it guarded rather than apply
            // the circular cursor rule. Output FIFOs 0..3 and input FIFOs
            // 0..1 have independent cursor owners.
            // Unsupported input FIFO 2/3 is guarded, never silently completed.
            const bool scratch=linear || circular;
            if((node[0]&~0x7fffu) || (node[1]&0x220d) ||
               (linear && (node[1]&16)) ||
               (!scratch && buffer>=(output?4u:2u)) || (node[1]>>14)!=1 ||
               (format!=1 && format!=2 && format!=6) || !count || count>4096 ||
               (format==1 && (count&1)))
                return fail("unqualified GP DMA descriptor format/extent");
            uint32_t* words[4096];
            if(uint64_t(node[3])+count>bank_end(node[3]))return fail("GP DMA samples cross memory bank");
            for(unsigned i=0;i<count;++i) {
                words[i]=word_(node[3]+i);
                if(!words[i])return fail("GP DMA sample outside memory banks");
            }
            // The VP/host/input-DMA bank publisher cannot take a GP frame's
            // leased MIX. Check before any physical input or FIFO cursor work.
            if(kind_==ApuDspKind::Gp && !output && node[3]<0x1800 && uint64_t(node[3])+count>0x1400 &&
               mix_write_ && !mix_write_())
                return fail("GP MIX DMA publication was rejected");
            const uint32_t size=node[6]+1,width=format==1?2:4;uint32_t offset=node[4];
            if(circular && offset>=size)return fail("unqualified GP DMA initial circular offset");
            std::array<uint8_t,4096*4> packed{};
            for(unsigned i=0;i<count;++i) {
                uint32_t sample=output?(*words[i]&0xffffff):0;
                if(output && format==2)sample<<=8;
                if(output && format==1)sample>>=8;
                for(unsigned b=0;b<width;++b)packed[i*width+b]=uint8_t(sample>>(b*8));
            }
            const unsigned length=count*width;
            if(linear) {
                if(!bytes_(offset,packed.data(),length,output))return fail("GP linear scratch DMA failed");
            } else if(circular) {
                for(unsigned at=0;at<length;) {
                    const unsigned n=(size-offset)<(length-at)?size-offset:length-at;
                    if(!bytes_(node[5]+offset,packed.data()+at,n,output))return fail("GP scratch DMA failed");
                    at+=n;offset=(offset+n)%size;
                }
            } else if(!fifo_(buffer,packed.data(),length,output))return fail("GP FIFO DMA failed");
            // External P publication must be admitted by the CPU owner before
            // any executable word is stored. A blocked write cannot publish
            // this node or end-of-list status; actual earlier work remains.
            if(!output && node[3]>=0x2800 && program_write_ && !program_write_())
                return fail("GP executable DMA publication was rejected");
            if(!output)for(unsigned i=0;i<count;++i) {
                uint32_t sample=0;
                for(unsigned b=0;b<width;++b)sample|=uint32_t(packed[i*width+b])<<(b*8);
                *words[i]=format==1?sample<<8:(format==2?sample>>8:sample&0xffffff);
            }
            // FIFO addressing is owned by PAPU CUR, not descriptor words 4..6.
            if(circular && (node[1]&16))*fields[4]=offset;
            next_=node[0];++nodes_;
        }
        // End-of-list follows actual copies. Status reads never finish work or
        // stop an armed engine; STOP and interrupt acknowledgement are explicit.
        interrupts_|=0x80;return true;
    }
    Word word_;Bytes bytes_;Fifo fifo_;Fault fault_;ProgramWrite program_write_;MixWrite mix_write_;
    const ApuDspKind kind_;
    uint32_t next_=0,start_=0,control_=32,interrupts_=0;
    uint64_t nodes_=0;bool failed_=false;
};
using ApuGpDma=ApuDspDma; // Source compatibility for the qualified GP interface.
} // namespace xlle
