#include "cpu/unicorn_floor.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#ifdef XLLE_MMIO_AOT_FIXTURE
#include "aot_programs.h"
#endif

using namespace xlle;
static void check(bool value,const char* why) {
    if(!value){std::fprintf(stderr,"MMIO instruction clock: %s\n",why);std::exit(1);}
}
static void exercise(bool instruction_mode) {
    UnicornFloor cpu;
    check(cpu.ok() && cpu.map_mem(0x1000,0x3000) && cpu.setup_flat_segments(0x3000),"CPU setup");
    if(instruction_mode)cpu.force_insn_scheduling();
    // All three accesses occur inside one straight-line translated block.
    // Arithmetic flags remain lazy at the first store, exercising a query
    // that must not restore/canonicalize architectural state.
    const uint8_t code[]={
        0xb8,0xfe,0xff,0xff,0xff,       // MOV EAX,fffffffe (index0)
        0x83,0xc0,2,                   // ADD EAX,2 (index1)
        0xa3,0,0,0x10,0,               // MOV [00100000],EAX (index2)
        0x41,                          // INC ECX (index3)
        0x8b,0x15,4,0,0x10,0,         // MOV EDX,[00100004] (index4)
        0x89,0x15,8,0,0x10,0,         // MOV [00100008],EDX (index5)
        0xeb,0xe4                      // JMP start (index6)
    };
    check(cpu.write_mem(0x1000,code,sizeof(code)),"authored code");
    std::vector<uint64_t> clocks;
    bool in_guest=false,nested=false;
    auto observe=[&] {
        uint64_t clock=UINT64_MAX;
        if(!in_guest || nested) {
            check(!cpu.mmio_instruction_clock(clock) && clock==UINT64_MAX,"reject outside/nested callback");
            return;
        }
        const auto before=cpu.get_regs();
        check(cpu.mmio_instruction_clock(clock),"active callback exact prefix");
        uint64_t again=UINT64_MAX;
        check(cpu.mmio_instruction_clock(again) && clock==again,"repeat query stable");
        const auto after=cpu.get_regs();
        check(std::memcmp(&before,&after,sizeof(before))==0,"query preserves registers and flags");
        clocks.push_back(clock);
    };
    check(cpu.map_mmio(0x100000,0x1000,
        [&](uint32_t,int){observe();return 0x12345678u;},
        [&](uint32_t address,int,uint32_t value){
            observe();
            if(address==0x100000) {
                check(value==0,"completed ADD effect precedes MMIO");
                nested=true;uint32_t ignored=0;
                check(cpu.read_mem(0x100004,&ignored,sizeof(ignored)),"nested host MMIO read");
                nested=false;
            }else check(address==0x100008 && value==0x12345678,"actual read result reaches next store");
        }),"MMIO mapping");
    uint64_t outside=UINT64_MAX;
    check(!cpu.mmio_instruction_clock(outside) && outside==UINT64_MAX,"before execution refusal");
    X86Regs initial{};initial.eip=0x1000;initial.esp=0x2ff0;initial.eflags=2;cpu.set_regs(initial);
    uint64_t total=0;
    for(uint64_t budget:{uint64_t(7),uint64_t(3),uint64_t(4),uint64_t(14)}) {
        std::string error;in_guest=true;
        check(cpu.run(cpu.get_regs().eip,budget,error)==budget && error.empty(),"exact slice execution");
        in_guest=false;total+=budget;
        outside=UINT64_MAX;
        check(!cpu.mmio_instruction_clock(outside) && outside==UINT64_MAX,"no stale callback clock after run");
    }
    const std::vector<uint64_t> expected={2,4,5,9,11,12,16,18,19,23,25,26};
    check(clocks==expected,"all MMIO prefixes across full/partial blocks");
    const auto final=cpu.get_regs();
    check(total==28 && final.eax==0 && final.ecx==4 && final.edx==0x12345678 && final.eip==0x1000,
          "all ordinary guest effects retained");
    check((final.eflags&1)==1,"ADD carry survives subsequent INC and queries");
    uint32_t host=0;check(cpu.read_mem(0x100004,&host,sizeof(host)) && host==0x12345678,"host MMIO access refusal");
    uint64_t code_hooks=0,count_hooks=0,block_hooks=0;
    cpu.hook_region_counts(code_hooks,count_hooks,block_hooks);
    check(count_hooks==0 && block_hooks>0 && (instruction_mode || code_hooks==0),"no additional instruction hooks");
}
static void read_modify_write() {
    UnicornFloor cpu;
    check(cpu.ok() && cpu.map_mem(0x1000,0x3000) && cpu.setup_flat_segments(0x3000),"RMW CPU setup");
    // One instruction performs both MMIO transactions. Neither access may
    // count this instruction as already retired or expose its charged suffix.
    const uint8_t code[]={0xff,0x05,0,0,0x10,0,0x90,0xeb,0xf7};
    check(cpu.write_mem(0x1000,code,sizeof(code)),"RMW code");
    std::vector<uint64_t> clocks;
    auto record=[&]{uint64_t time=99;check(cpu.mmio_instruction_clock(time),"RMW query");clocks.push_back(time);};
    check(cpu.map_mmio(0x100000,0x1000,[&](uint32_t,int){record();return 41;},
        [&](uint32_t,int,uint32_t value){record();check(value==42,"RMW value");}),"RMW mapping");
    X86Regs r{};r.eip=0x1000;r.esp=0x2ff0;r.eflags=3;cpu.set_regs(r);
    std::string error;check(cpu.run(0x1000,6,error)==6 && error.empty(),"RMW execution");
    check(clocks==std::vector<uint64_t>({0,0,3,3}),"same-instruction RMW prefixes");
    check(cpu.get_regs().eflags&1,"RMW keeps carry");
}
#ifdef XLLE_MMIO_AOT_FIXTURE
static void native_memory_decline(bool native,bool bulk=false) {
    std::vector<uint8_t> owned_ram(0x10000);
    UnicornFloor cpu;
    check(cpu.ok() && cpu.map_mem_ptr(0,owned_ram.size(),owned_ram.data()) &&
          cpu.setup_flat_segments(0x8000),"native crossing CPU");
    const auto& region=xlle_aot_machine_store_fault;
    check(cpu.write_mem(region.base,region.code,region.code_size),"generated authored region");
    const uint8_t entry[]={0xe9,0xfb,0,0,0};
    check(cpu.write_mem(0xf00,entry,sizeof(entry)),"real fallback entry");
    if(native) {
        check(cpu.register_aot_region(region),"native registration");
        cpu.set_aot_bulk_registers(bulk);
    }
    std::vector<uint64_t> clocks;
    bool observed_native_prefix=false;
    constexpr uint32_t initial_eax=0x11223344;
    check(cpu.map_mmio(0x0e000000,0x1000,{},[&](uint32_t address,int size,uint32_t value){
        check(address==0x0e000000 && size==4 && value==initial_eax+clocks.size(),"one actual store per loop");
        const auto before=cpu.get_regs();
        uint64_t clock=UINT64_MAX;check(cpu.mmio_instruction_clock(clock),"native/fallback MMIO prefix");
        const auto after=cpu.get_regs();
        check(std::memcmp(&before,&after,sizeof(before))==0,"crossing query preserves state");
        check(before.ebx==clocks.size()+1 && before.eax==value && (before.eflags&1),"preceding effects and carry retained");
        clocks.push_back(clock);
        observed_native_prefix|=cpu.aot_stats().retired>0;
    }),"native-declined MMIO mapping");
    X86Regs r{};r.eip=0xf00;r.esp=0x9000;r.eax=initial_eax;r.eflags=3;cpu.set_regs(r);
    std::string error;check(cpu.run(r.eip,25,error)==25 && error.empty(),"mixed execution budget");
    check(clocks==std::vector<uint64_t>({2,6,10,14,18,22}),"exact clocks include real native retirement");
    const auto final=cpu.get_regs();
    check(final.eax==initial_eax+6 && final.ebx==6 && final.eip==region.base && (final.eflags&1),"mixed final state");
    const auto stats=cpu.aot_stats();
    if(native) {
        if(!observed_native_prefix || !stats.retired || !stats.memory_declines)
            std::fprintf(stderr,"native crossing: retired=%llu fallback=%llu memory_declines=%llu context=%llu code_tlb=%llu attempts=%llu\n",
                (unsigned long long)stats.retired,(unsigned long long)stats.fallback_insns,
                (unsigned long long)stats.memory_declines,(unsigned long long)stats.context_declines,
                (unsigned long long)stats.code_tlb_declines,(unsigned long long)stats.attempts);
        check(observed_native_prefix && stats.retired>0 && stats.memory_declines>0,"real native-to-MMIO decline exercised");
        check(stats.retired+stats.fallback_insns==25,"native/fallback exact disjoint clock");
    }else check(!stats.retired && !observed_native_prefix,"all-Unicorn control");
}
#endif
int main() {
    exercise(false);exercise(true);read_modify_write();
#ifdef XLLE_MMIO_AOT_FIXTURE
    native_memory_decline(false);native_memory_decline(true);native_memory_decline(true,true);
#endif
    std::puts("PASS: exact MMIO prefixes, partial/full TBs, both scheduling modes, state preservation and stale/nested refusal");
}
