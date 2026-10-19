// Authored CPU/DSP programs exercise the board's real MMIO, SG bootstrap and
// clock integration. No dashboard, firmware or original DSP program is used.
#include "machine/machine.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
static void check(bool ok,const char* why) {
    if(!ok){std::fprintf(stderr,"machine audio: %s\n",why);std::exit(1);}
}
static void put(xlle::XboxMachine& m,uint32_t at,uint32_t value) {
    check(m.cpu().write_mem(at,&value,4),"fixture write");
}
static uint32_t get(xlle::XboxMachine& m,uint32_t at) {
    uint32_t value;check(m.cpu().read_mem(at,&value,4),"fixture read");return value;
}
int main() {
    for(uint64_t chunk:{117u,4096u}) for(bool late:{false,true}) {
        xlle::XboxMachine::Config config;config.cpu_chunk_insns=chunk;
        xlle::XboxMachine m(config);check(m.ok() && m.init_ram(),"machine RAM");
        check(m.cpu().setup_flat_segments(0x18000),"flat CPU segments");m.init_devices();
        put(m,0x1000,0x8000);put(m,0x1008,0x3000);
        put(m,0x2000,0xa000);put(m,0x2008,0x5000);
        for(bool ep:{false,true}) {
            const uint32_t program[]={0x44f400,ep?0x654321u:0x123456u,0x447000,0x100,0x87};
            for(unsigned i=0;i<2048;++i)put(m,(i<1024?(ep?0xa000:0x8000):(ep?0x5000:0x3000))+
                (i%1024)*4,i<5?program[i]:0x87);
        }
        std::vector<uint8_t> code;uint64_t instructions=0;
        const auto word=[&](uint32_t value){for(unsigned i=0;i<4;++i)code.push_back(uint8_t(value>>(8*i)));};
        const auto write=[&](uint32_t address,uint32_t value) {
            code.insert(code.end(),{0xc7,0x05});word(address);word(value);++instructions;
        };
        const auto read=[&](uint32_t address,uint32_t output) {
            code.push_back(0xa1);word(address);code.push_back(0xa3);word(output);instructions+=2;
        };
        write(0xfe85fffc,1); // actual cold EP hold
        write(0xfe801100,0x88);write(0xfe802000,7);
        write(0xfe802040,0x1000);write(0xfe8020d4,1);
        write(0xfe802048,0x2000);write(0xfe8020dc,1);
        if(late) {
            // The first audio edge follows CPU instruction652. Releasing
            // both cores just before it must preserve that same phase.
            code.insert(code.end(),650-instructions,0x90);instructions=650;
        }
        write(0xfe83fffc,3);write(0xfe85fffc,3); // real SG bootstraps
        read(0xfe830400,0x15000);read(0xfe850400,0x15004);
        // This interval supplies genuine instruction time, not a read-count
        // completion. Both sole DSPs execute their own store and STOP.
        code.insert(code.end(),1000,0x90);instructions+=1000;
        read(0xfe830400,0x15008);read(0xfe850400,0x1500c);
        write(0xfe83fffc,0);write(0xfe85fffc,1);
        check(m.cpu().write_mem(0x10000,code.data(),code.size()),"authored CPU program");
        xlle::X86Regs regs;regs.eip=0x10000;regs.esp=0x20000;regs.eflags=2;m.cpu().set_regs(regs);
        std::string error;const auto retired=m.run(regs.eip,instructions,error);
        if(!error.empty())std::fprintf(stderr,"%s\n",error.c_str());
        check(error.empty() && retired==instructions,"clocked board execution");
        check(get(m,0x15000)==(late?0x123456u:0) && get(m,0x15004)==(late?0x654321u:0),
              "release phase controls readiness, not read count or CPU batch size");
        check(get(m,0x15008)==0x123456 && get(m,0x1500c)==0x654321,"both real DSP stores retire");
        check(get(m,0x1000)==0x8000 && get(m,0x2000)==0xa000,"SG descriptors retained");
    }
    std::puts("PASS machine audio: cold board, actual SG bootstraps, sole GP/EP execution, clock partitions");
}
