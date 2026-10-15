// Offline original-instruction caller supplies only its currently owned RAM.
// Device transactions, bootstrap and CPU lifetime use the maintained owners.
#include "audio_device_apu_contract.hpp"
#include "machine/apu_gp_processor.hpp"
#include "machine/apu_ep_processor.hpp"
#include "machine/apu_dsp_clock.hpp"
#include "machine/apu_device_clock.hpp"
#include <array>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>
static char digit(unsigned n){return "0123456789abcdef"[n&15];}
static int nibble(char c){return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:-1;}
static std::string hex(const uint8_t* bytes,size_t n) {
    std::string result;result.reserve(n*2);
    for(size_t i=0;i<n;++i){result+=digit(bytes[i]>>4);result+=digit(bytes[i]);}
    return result;
}
static bool decode(const std::string& line,uint8_t* bytes,size_t n) {
    if(line.size()!=n*2)return false;
    for(size_t i=0;i<n;++i) {
        const int hi=nibble(line[i*2]),lo=nibble(line[i*2+1]);
        if(hi<0 || lo<0)return false;
        bytes[i]=uint8_t(hi*16+lo);
    }
    return true;
}
int main(int argc,char** argv) {
    const bool active=argc==3 && std::string(argv[1])=="--active";
    if(argc!=1 && !active)return 2;
    std::string error;uint64_t reads=0,writes=0;
    auto fault=[&](const std::string& s){
        if(error.empty()) {
            error=s;std::fprintf(stderr,"FAIL device: %s\n",s.c_str());std::fflush(stderr);
        }
    };
    {
    // Destruction can reject pending ownership. Inspect its result before
    // choosing the final exit code, not after a successful return is fixed.
    xlle::McpxApu apu(fault);
    if(!apu.initialize_kernel_audio_handoff())return 2;
    xlle::McpxAci aci(fault);
    if(active) {
        // The source caller verifies the retained physical image's digest.
        // Install this immutable owner before constructing the sole EP CPU.
        std::ifstream image(argv[2],std::ios::binary|std::ios::ate);
        std::array<uint8_t,8192> bytes{};std::array<uint32_t,2048> words{};
        if(!image || image.tellg()!=std::streampos(bytes.size()))return 2;
        image.seekg(0);
        if(!image.read(reinterpret_cast<char*>(bytes.data()),std::streamsize(bytes.size())))return 2;
        for(size_t i=0;i<words.size();++i) {
            const auto* p=bytes.data()+i*4;
            words[i]=uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);
        }
        if(!apu.install_ep_y_lookup(words.data(),words.size()) || !aci.initialize_kernel_audio_handoff())return 2;
    }
    xlle::ApuGpProcessor gp(apu,fault);
    xlle::ApuEpProcessor ep(apu,fault);
    apu.set_time(0,1125000);
    std::unique_ptr<xlle::ApuDspClock> stopped_clock;
    std::unique_ptr<xlle::ApuDeviceClock> active_clock;
    if(active)active_clock=std::make_unique<xlle::ApuDeviceClock>(apu,gp,ep,aci,0,1125000,fault);
    else stopped_clock=std::make_unique<xlle::ApuDspClock>(apu,gp,ep,0,1125000,fault);
    auto ticks=[&]{return active?active_clock->ticks():stopped_clock->ticks();};
    auto retired=[&](bool ep){return active?active_clock->retired(ep):stopped_clock->retired(ep);};
    auto last=[&](bool ep)->const xlle::ApuGpRun& {return active?active_clock->last(ep):stopped_clock->last(ep);};
    auto read_memory=[&](uint32_t a,void* data,size_t n) {
        if(!n || n>4096 || uint64_t(a)+n>0x4000000)return false;
        ++reads;std::printf("MEMR %08x %zu\n",a,n);std::fflush(stdout);
        std::string line;if(!std::getline(std::cin,line) || line.rfind("DATA ",0)!=0)return false;
        return decode(line.substr(5),static_cast<uint8_t*>(data),n);
    };
    apu.set_mem_access(read_memory,[&](uint32_t a,const void* data,size_t n) {
        if(!n || n>4096 || uint64_t(a)+n>0x4000000)return false;
        ++writes;std::printf("MEMW %08x %zu %s\n",a,n,hex(static_cast<const uint8_t*>(data),n).c_str());std::fflush(stdout);
        std::string line;return std::getline(std::cin,line) && line=="DONE";
    });
    aci.set_mem_access(read_memory);
    if(!error.empty()){std::printf("FAIL %s\n",error.c_str());return 2;}
    std::puts("READY");std::fflush(stdout);
    std::string line;
    while(std::getline(std::cin,line)) {
        std::istringstream in(line);std::string op,extra;in>>op;
        if(op=="T") {
            uint64_t target=0;
            if(!(in>>target) || (in>>extra))return 2;
            const bool advanced=active?active_clock->advance(target):
                !(apu.mmio_read(0xfe802000,4)&0x18) && stopped_clock->advance(target);
            if(!advanced) {
                std::printf("FAIL %s DSP clock %s\n",active?"active":"stopped",error.c_str());std::fflush(stdout);return 2;
            }
            std::printf("TIME %llu %u %u %llu %llu %u %u %llu\n",
                static_cast<unsigned long long>(ticks()),unsigned(gp.phase()),unsigned(ep.phase()),
                static_cast<unsigned long long>(retired(false)),static_cast<unsigned long long>(retired(true)),
                last(false).pc,last(true).pc,static_cast<unsigned long long>(apu.ep_frame_events()));
        }else if(op=="ACTIVE") {
            if(!active || (in>>extra))return 2;
            std::printf("ACTIVE %llu %llu %llu %llu %llu %llu %u %u\n",
                static_cast<unsigned long long>(ticks()),static_cast<unsigned long long>(apu.voice_mix_quanta()),
                static_cast<unsigned long long>(apu.ep_frame_events()),static_cast<unsigned long long>(apu.gp_dma_nodes()),
                static_cast<unsigned long long>(apu.ep_dma_nodes()),static_cast<unsigned long long>(aci.consumed_pcm_words()),
                unsigned(apu.gp_frame_outstanding()),unsigned(apu.dsp_work_pending(xlle::ApuDspKind::Ep)));
        }else if(op=="EPSTATE") {
            if(in>>extra)return 2;
            uint32_t status=0,control=0;
            if(!apu.ep_read(0xffffc5,status) || !apu.ep_control_publication(control))return 2;
            const auto request=apu.mmio_read(apu.kBase+0x5ff10,4);
            std::printf("EPSTATE %06x %06x %u\n",status,control,request);
        }else if(op=="NOTIFY") {
            if(in>>extra)return 2;
            std::printf("NOTIFY %u %llu\n",unsigned(apu.retirement_notification_pending()),
                static_cast<unsigned long long>(apu.retirement_notifications()));
        }else if(op=="STATE") {
            if(in>>extra)return 2;
            std::printf("STATE %u %u %llu %llu %llu %llu\n",unsigned(gp.phase()),unsigned(ep.phase()),
                static_cast<unsigned long long>(gp.bootstraps()),static_cast<unsigned long long>(ep.bootstraps()),
                static_cast<unsigned long long>(reads),static_cast<unsigned long long>(writes));
        }else if(op=="P") {
            std::string kind;in>>kind;if((kind!="GP" && kind!="EP") || (in>>extra))return 2;
            const uint32_t* p=kind=="GP"?apu.gp_word(0x2800):apu.ep_word(0x2800);
            std::printf("P %s\n",hex(reinterpret_cast<const uint8_t*>(p),8192).c_str());
        }else {
            uint32_t address=0,value=0;unsigned width=0;
            if((op!="R" && op!="W") || !(in>>std::hex>>address>>std::dec>>width>>std::hex>>value) || (in>>extra))return 2;
            if(address<0xfe800000 || address>=0xfe880000 ||
               !pilot_original_apu_admitted(address-0xfe800000,width,op=="W",value)) {
                std::printf("FAIL admission %08x %s %08x\n",address,op.c_str(),value);std::fflush(stdout);return 2;
            }
            if(op=="W")apu.mmio_write(address,int(width),value);
            else value=apu.mmio_read(address,int(width));
            if(!error.empty()){std::printf("FAIL %s\n",error.c_str());std::fflush(stdout);return 2;}
            // A store acknowledgement is never substituted for register data.
            if(op=="W")std::puts("OK");else std::printf("OK %08x\n",value);
        }
        std::fflush(stdout);
    }
    }
    if(!error.empty())std::fprintf(stderr,"FAIL device owner disposal: %s\n",error.c_str());
    return error.empty()?0:2;
}
