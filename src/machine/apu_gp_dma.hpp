// Bounded MCPX GP DMA peripheral. Authored from the descriptor/register contract
// and published byte examples; no GPL implementation body is incorporated.
#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>
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
         fault_(std::move(fault)),program_write_(std::move(program_write)),mix_write_(std::move(mix_write)),kind_(kind),
         control_(0u){} // Observed fresh GP and held-to-running EP both report zero.
    bool read(uint32_t address,uint32_t& value) {
        if(failed_)return false;
        switch(address) {
        case 0xffffc5:value=interrupts_;return true;
        case 0xffffca:
            if(kind_!=ApuDspKind::Ep)return fail("unqualified GP context register");
            value=0;return true;
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
        case 0xffffca:
            // The reached EP zero profile reads back zero on hardware. No
            // nonzero meaning or effect on wake/DMA/interrupts is inferred.
            if(kind_!=ApuDspKind::Ep || value || servicing_ || active_)
                return fail("unqualified GP context register configuration");
            return true;
        case 0xffffd4:
            if(servicing_ || active_)return fail("GP DMA pointer publication overlaps active work");
            if(value&~0x7fffu)return fail("invalid GP DMA node pointer");
            next_=value;return true;
        case 0xffffd5:
            if(servicing_ || active_)return fail("GP DMA start publication overlaps active work");
            if((value&~0x3fffu) && !(kind_==ApuDspKind::Ep && value==0x4000))
                return fail("invalid GP DMA start pointer");
            start_=value;return true;
        case 0xffffd7:
            if(servicing_ || active_)return fail("GP DMA configuration overlaps active work");
            if(value)return fail("unqualified GP DMA configuration");
            return true;
        case 0xffffd6:
            if(servicing_)return fail("reentrant GP DMA action during device work");
            switch(value) {
            case 1:
                if(active_)return fail("GP DMA START overlaps active work");
                control_=(control_&8)|16;active_=true;list_nodes_=0;return true;
            case 2:
                // Fresh GP and an idle STOP both read zero on hardware. Do
                // not invent STOPPED for a command with no active transfer.
                // Busy STOP and EP retain their separate existing contract.
                control_=(control_&8)|((kind_==ApuDspKind::Gp && !active_)?0u:32u);
                active_=false;job_.reset();return true;
            case 3:control_|=8;return true;
            case 4:control_&=~8u;return true;
            default:return fail("unqualified GP DMA action");
            }
        default:return fail("unqualified GP peripheral write");
        }
    }
    uint64_t nodes()const{return nodes_;}
    bool work_pending()const{return active_;}
    // Real work quanta, never register reads: admit the descriptor, then copy
    // and publish it. The DSP owner supplies serialized instruction boundaries;
    // a caller without a CPU supplies explicit device work. Silicon phase/rate
    // is separate from this nominal fair dispatch policy.
    bool service(uint32_t quanta=1) {
        if(failed_)return false;
        if(servicing_)return fail("reentrant GP DMA device work");
        if(quanta>4096)return fail("GP DMA work quantum budget exceeded");
        servicing_=true;
        for(uint32_t i=0;i<quanta && active_ && !(control_&8) && !failed_;++i)
            if(!(job_?transfer():admit()))break;
        servicing_=false;return !failed_;
    }
private:
    struct Job {
        uint32_t node[7]{};
        uint32_t* fields[7]{};
        std::vector<uint32_t*> words;
        bool end=false,linear=false,circular=false,output=false;
        uint32_t format=0,buffer=0,count=0;
    };
    uint32_t bank_end(uint32_t address)const{return apu_dsp_bank_end(kind_,address);}
    bool fail(const char* why){
        failed_=true;std::string reason(why);
        if(kind_==ApuDspKind::Ep)for(size_t at=0;(at=reason.find("GP",at))!=std::string::npos;at+=2)reason.replace(at,2,"EP");
        fault_(reason);return false;
    }
    bool admit() {
            if(list_nodes_==1024)return fail("GP DMA list exceeded node budget");
            job_=std::make_unique<Job>();
            auto& job=*job_;
            if(next_&0x4000){job.end=true;return true;}
            // Original DB/EB linear builders own exactly five words. The
            // following words may already be another descriptor; circular
            // base/size fields are not part of a buffer15 transfer.
            if(next_+2>bank_end(next_))return fail("GP DMA descriptor crosses memory bank");
            auto& node=job.node;auto& fields=job.fields;
            for(unsigned i=0;i<2;++i) {
                fields[i]=word_(next_+i);
                if(!fields[i])return fail("GP DMA descriptor outside memory banks");
                node[i]=*fields[i]&0xffffff;
            }
            const uint32_t format=(node[1]>>10)&7,buffer=(node[1]>>5)&15;
            const bool linear=buffer==15,circular=buffer==14;
            job.linear=linear;job.circular=circular;job.buffer=buffer;job.format=format;
            const unsigned field_count=circular?7:(linear?5:4);
            if(next_+field_count>bank_end(next_))return fail("GP DMA descriptor crosses memory bank");
            for(unsigned i=2;i<field_count;++i) {
                fields[i]=word_(next_+i);
                if(!fields[i])return fail("GP DMA descriptor outside memory banks");
                node[i]=*fields[i]&0xffffff;
            }
            const uint32_t count=node[2];
            const bool output=(node[1]&2)!=0;
            job.output=output;job.count=count;
            // Noninterleaved unit-stride scratch and FIFO transport. Linear
            // offset writeback is not reached by the original builders and
            // lacks independent evidence; keep it guarded rather than apply
            // the circular cursor rule. Output FIFOs 0..3 and input FIFOs
            // 0..1 have independent cursor owners.
            // Unsupported input FIFO 2/3 is guarded, never silently completed.
            const bool scratch=linear || circular;
            // Paired physical GP observations support these exact scratch
            // profiles: linear/circular input and circular format2 output.
            // Four-word representatives do not establish arbitrary flags,
            // other output profiles, larger-count silicon timing or P-code
            // reload safety. Existing extent and publication owners still apply.
            // Separate physical EP count24 pairs establish linear format6
            // input to X and P, with bit9 clear/set and two successive exact
            // copies. Separate count78 X output pairs establish59E2/5BE2.
            // Keep EP Y/P output, circular and adjacent flag profiles guarded.
            // This admits byte transport only; busy/ACK timing is separate.
            const bool ep_input_bank=node[3]<0xc00 || (node[3]>=0x2800 && node[3]<0x3800);
            const bool qualified_bit9_profile=kind_==ApuDspKind::Gp ?
                (node[1]==0x5be0u || node[1]==0x4bd0u || node[1]==0x4bd2u) :
                ((node[1]==0x5be0u && ep_input_bank) ||
                 (node[1]==0x5be2u && node[3]<0xc00));
            if((node[0]&~0x7fffu) || (node[1]&0x200d) ||
               ((node[1]&0x200) && !qualified_bit9_profile) ||
               (linear && (node[1]&16)) ||
               (!scratch && buffer>=(output?4u:2u)) || (node[1]>>14)!=1 ||
               (format!=1 && format!=2 && format!=6) || !count || count>4096 ||
               (format==1 && (count&1)))
                return fail("unqualified GP DMA descriptor format/extent");
            job.words.resize(count);auto& words=job.words;
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
            if(circular && node[4]>=node[6]+1)return fail("unqualified GP DMA initial circular offset");
            return true;
    }
    bool transfer() {
            auto& job=*job_;
            if(job.end){finish();return true;}
            const auto& node=job.node;const auto& fields=job.fields;const auto& words=job.words;
            const bool output=job.output,linear=job.linear,circular=job.circular;
            const uint32_t count=job.count,format=job.format,buffer=job.buffer;
            const uint32_t size=node[6]+1,width=format==1?2:4;uint32_t offset=node[4];
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
            next_=node[0];++nodes_;++list_nodes_;job_.reset();
            if(next_&0x4000)finish();
            return true;
    }
    void finish() {
        // Natural idle follows the actual last publication. The measured GP
        // and EP input cases finish at D6=0. Busy STOP and intermediate status
        // timing remain separate; acknowledgement cannot replace device work.
        active_=false;control_&=~16u;interrupts_|=0x80;job_.reset();
    }
    Word word_;Bytes bytes_;Fifo fifo_;Fault fault_;ProgramWrite program_write_;MixWrite mix_write_;
    const ApuDspKind kind_;
    uint32_t next_=0,start_=0,control_=32,interrupts_=0;
    uint64_t nodes_=0;uint32_t list_nodes_=0;
    bool failed_=false,active_=false,servicing_=false;
    std::unique_ptr<Job> job_;
};
using ApuGpDma=ApuDspDma; // Source compatibility for the qualified GP interface.
} // namespace xlle
