// Offline original-instruction caller supplies only its currently owned RAM.
// Device transactions, bootstrap and CPU lifetime use the maintained owners.
#include "audio_device_apu_contract.hpp"
#include "machine/apu_gp_processor.hpp"
#include "machine/apu_ep_processor.hpp"
#include <cstdio>
#include <iostream>
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
int main() {
    std::string error;uint64_t reads=0,writes=0;
    xlle::McpxApu apu([&](const std::string& s){if(error.empty())error=s;});
    if(!apu.initialize_kernel_audio_handoff())return 2;
    xlle::ApuGpProcessor gp(apu,[&](const std::string& s){if(error.empty())error=s;});
    xlle::ApuEpProcessor ep(apu,[&](const std::string& s){if(error.empty())error=s;});
    apu.set_mem_access([&](uint32_t a,void* data,size_t n) {
        if(!n || n>4096 || uint64_t(a)+n>0x4000000)return false;
        ++reads;std::printf("MEMR %08x %zu\n",a,n);std::fflush(stdout);
        std::string line;if(!std::getline(std::cin,line) || line.rfind("DATA ",0)!=0)return false;
        return decode(line.substr(5),static_cast<uint8_t*>(data),n);
    },[&](uint32_t a,const void* data,size_t n) {
        if(!n || n>4096 || uint64_t(a)+n>0x4000000)return false;
        ++writes;std::printf("MEMW %08x %zu %s\n",a,n,hex(static_cast<const uint8_t*>(data),n).c_str());std::fflush(stdout);
        std::string line;return std::getline(std::cin,line) && line=="DONE";
    });
    if(!error.empty()){std::printf("FAIL %s\n",error.c_str());return 2;}
    std::puts("READY");std::fflush(stdout);
    std::string line;
    while(std::getline(std::cin,line)) {
        std::istringstream in(line);std::string op,extra;in>>op;
        if(op=="STATE") {
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
    return error.empty()?0:2;
}
