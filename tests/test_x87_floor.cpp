#include "aot_x87_probe.h"
#include "x87_arithmetic_cases.hpp"
#include "../src/cpu/unicorn_metrics.h"
#include <unicorn/unicorn.h>
#include <unicorn/x86.h>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

static unsigned failures;
static void check(bool ok, const char* why) {
    if (!ok) { std::fprintf(stderr,"x87 floor: %s\n",why); ++failures; }
}
static void ok(uc_err e) { if(e!=UC_ERR_OK) { std::fprintf(stderr,"%s\n",uc_strerror(e)); std::exit(1); } }
static uc_engine* create(uint16_t control=0x37f) {
    uc_engine* uc=nullptr; ok(uc_open(UC_ARCH_X86,UC_MODE_32,&uc));
    ok(uc_mem_map(uc,0x1000,0x2000,UC_PROT_ALL));
    uint32_t cw=control,sw=0,tags=0,cr0=0;
    ok(uc_reg_write(uc,UC_X86_REG_FPCW,&cw));
    ok(uc_reg_write(uc,UC_X86_REG_FPSW,&sw));
    ok(uc_reg_write(uc,UC_X86_REG_FPTAG,&tags));
    ok(uc_reg_read(uc,UC_X86_REG_CR0,&cr0)); cr0|=0x20;
    ok(uc_reg_write(uc,UC_X86_REG_CR0,&cr0));
    return uc;
}
static void st(uc_engine* uc,unsigned n,uint64_t low,uint16_t high) {
    std::array<uint8_t,10> v{}; std::memcpy(v.data(),&low,8); std::memcpy(v.data()+8,&high,2);
    ok(uc_reg_write(uc,UC_X86_REG_ST0+n,v.data()));
}
static xlle_x87_snapshot snapshot(uc_engine* uc) {
    xlle_x87_snapshot s{}; xlle_probe_x87_snapshot(uc,&s); return s;
}
static uc_err run(uc_engine* uc,const std::vector<uint8_t>& code) {
    ok(uc_mem_write(uc,0x1000,code.data(),code.size()));
    return uc_emu_start(uc,0x1000,0x1000+code.size(),0,1);
}
static void status(uc_engine* uc,unsigned expected,const char* why) {
    const auto s=snapshot(uc);
    if ((s.status&0x82ff)!=expected) std::fprintf(stderr,"  FPSW=%04x expected=%04x\n",s.status,expected);
    check((s.status&0x82ff)==expected,why);
}
static void interrupt(uc_engine* uc,uint32_t vector,void* opaque) {
    *static_cast<uint32_t*>(opaque)=vector; ok(uc_emu_stop(uc));
}
static void paged_store(bool wide, bool fault) {
    uc_engine* uc=nullptr; ok(uc_open(UC_ARCH_X86,UC_MODE_32,&uc));
    ok(uc_mem_map(uc,0,0x10000,UC_PROT_ALL));
    std::array<uint32_t,1024> pd{},pt{};
    pd[512]=0x3003;
    pt[1]=0x1003; pt[4]=0x4003; pt[5]=fault?0:0x5003;
    ok(uc_mem_write(uc,0x2000,pd.data(),sizeof(pd)));
    ok(uc_mem_write(uc,0x3000,pt.data(),sizeof(pt)));
    uint32_t cr3=0x2000,cr0=0x80010021,cw=0x37f,sw=0,tags=0;
    ok(uc_reg_write(uc,UC_X86_REG_CR3,&cr3)); ok(uc_reg_write(uc,UC_X86_REG_CR0,&cr0));
    ok(uc_reg_write(uc,UC_X86_REG_FPCW,&cw)); ok(uc_reg_write(uc,UC_X86_REG_FPSW,&sw));
    ok(uc_reg_write(uc,UC_X86_REG_FPTAG,&tags));
    // Compile the destination's physical page first. A store through the high
    // virtual alias must retain precise SMC handling and physical mapping.
    const uint8_t nop=0x90; ok(uc_mem_write(uc,0x4000,&nop,1));
    ok(uc_emu_start(uc,0x80004000,0x80004001,0,1));
    const uint32_t address=fault?0x80004ffe:0x80004080;
    std::array<uint8_t,6> code{uint8_t(wide?0xdd:0xd9),0x1d};
    std::memcpy(code.data()+2,&address,4); ok(uc_mem_write(uc,0x1000,code.data(),code.size()));
    st(uc,0,0x800000c000000000ull,0x3fff);
    const auto before=snapshot(uc);
    std::array<uint8_t,8> memory{}; memory.fill(0x55);
    const uint32_t physical=address&0xffff;
    ok(uc_mem_write(uc,physical,memory.data(),memory.size()));
    uint32_t vector=0; uc_hook hook;
    ok(uc_hook_add(uc,&hook,UC_HOOK_INTR,reinterpret_cast<void*>(interrupt),&vector,1,0));
    const auto result=uc_emu_start(uc,0x80001000,0x80001006,0,1);
    std::array<uint8_t,8> output{}; ok(uc_mem_read(uc,physical,output.data(),output.size()));
    const auto after=snapshot(uc);
    if(fault) {
        check(vector==14,"split floating store raises page fault");
        check(output==memory,"split page fault leaves every destination byte intact");
        check(std::memcmp(&before,&after,sizeof(before))==0,"split page fault preserves complete FPU state");
    } else {
        check(result==UC_ERR_OK && vector==0,"paged floating store executes on compiled destination page");
        uint64_t value=0; std::memcpy(&value,output.data(),8);
        check(wide?value==0x3ff0000018000000ull:uint32_t(value)==0x3f800001,
              "nonidentity paged store writes independently calculated physical bytes");
        check(after.top==1 && after.tags[0]==1,"paged store pops exactly once");
    }
    ok(uc_close(uc));
}
static void long_block() {
    auto* uc=create();
    std::vector<uint8_t> code;
    for(unsigned i=0;i<512;++i) { code.push_back(0xd9); code.push_back(0xc8); }
    ok(uc_mem_write(uc,0x1000,code.data(),code.size()));
    st(uc,0,0xc000000000000000ull,0x4000);
    check(uc_emu_start(uc,0x1000,0x1400,0,512)==UC_ERR_OK,"long x87 block translates without leaking temporaries");
    const auto s=snapshot(uc);
    check(s.ip==0x13fe && s.opcode==0x1c8 && s.significand[0]==0xc000000000000000ull,
          "long x87 block retains final environment and value");
    ok(uc_close(uc));
}
static void free_registers() {
    for(unsigned top=0;top<8;++top) for(unsigned reg=0;reg<8;++reg) for(bool empty:{false,true}) {
        auto* uc=create(); uint32_t sw=(top<<11)|0x4700,tags=empty?0xffff:0;
        ok(uc_reg_write(uc,UC_X86_REG_FPSW,&sw)); ok(uc_reg_write(uc,UC_X86_REG_FPTAG,&tags));
        for(unsigned i=0;i<8;++i) st(uc,i,0x8000000000000000ull+i,0x3fff);
        const auto before=snapshot(uc);
        check(run(uc,{0xdd,uint8_t(0xc0+reg)})==UC_ERR_OK,"FFREE executes even on an empty register");
        const auto after=snapshot(uc);
        check(after.top==top && after.status==before.status && after.control==before.control,
              "FFREE does not pop or change control/status under this model");
        for(unsigned i=0;i<8;++i) {
            check(after.tags[i]==(i==((top+reg)&7)?1:before.tags[i]),"FFREE marks exactly the selected physical tag empty");
            check(after.significand[i]==before.significand[i] && after.exponent[i]==before.exponent[i],
                  "FFREE preserves all physical register bits");
        }
        ok(uc_close(uc));
    }
}
static void arithmetic_specials() {
    for(const auto& c:x87_arithmetic_cases) {
        auto* uc=create(); st(uc,0,c.a_low,c.a_high); st(uc,3,c.b_low,c.b_high);
        check(run(uc,c.subtract?std::vector<uint8_t>{0xde,0xeb}:std::vector<uint8_t>{0xd8,0xcb})==UC_ERR_OK,
              "masked register arithmetic special value executes");
        status(uc,c.status,"independent extended denormal/NaN/invalid exception priority");
        const auto s=snapshot(uc); const unsigned dest=c.subtract?3:0;
        check(s.significand[dest]==c.result_low && s.exponent[dest]==c.result_high && s.top==unsigned(c.subtract),
              "independent extended special-value result and pop");
        ok(uc_close(uc));
    }
    for(const auto& c:x87_multiply_memory_cases) for(bool wide:{false,true}) {
        auto* uc=create(); st(uc,0,c.st_low,c.st_high);
        const uint64_t operand=wide?c.memory64:c.memory32;
        ok(uc_mem_write(uc,0x2000,&operand,wide?8:4));
        check(run(uc,{uint8_t(wide?0xdc:0xd8),0x0d,0,0x20,0,0})==UC_ERR_OK,
              "masked memory multiply executes");
        status(uc,c.status,"memory conversion and arithmetic share exception priority");
        const auto s=snapshot(uc);
        check(s.significand[0]==c.result_low && s.exponent[0]==(wide?c.result_high64:c.result_high32),
              "memory multiply preserves signaling versus quiet NaN selection");
        ok(uc_close(uc));
    }
}
int main() {
    xlle_guest_tsc_ticks=0;
    for(bool wide:{false,true}) for(bool fault:{false,true}) paged_store(wide,fault);
    long_block();
    free_registers();
    arithmetic_specials();
    {
        auto* uc=create(); uint32_t v=1,tags=0xffff;
        ok(uc_reg_write(uc,UC_X86_REG_FPTAG,&tags)); ok(uc_mem_write(uc,0x2000,&v,4));
        check(run(uc,{0xd9,5,0,0x20,0,0})==UC_ERR_OK,"FLD32 subnormal executes");
        status(uc,2,"FLD32 publishes DE"); const auto s=snapshot(uc);
        check(s.top==7 && s.significand[7]==0x8000000000000000ull && s.exponent[7]==0x3f6a,
              "subnormal is preserved and normalized, never flushed"); ok(uc_close(uc));
        check(s.ip==0x1000 && s.dp==0x2000 && s.opcode==0x105,"x87 environment retains PC/data/opcode");
    }
    {
        auto* uc=create(); uint32_t v=0x7f800000; st(uc,0,0,0); ok(uc_mem_write(uc,0x2000,&v,4));
        check(run(uc,{0xd8,0x0d,0,0x20,0,0})==UC_ERR_OK,"masked zero times infinity executes");
        status(uc,1,"FMUL invalid publishes IE"); const auto s=snapshot(uc);
        check(s.significand[0]==0xc000000000000000ull && s.exponent[0]==0xffff,
              "masked invalid produces x87 indefinite"); ok(uc_close(uc));
    }
    for(unsigned mode=0;mode<4;++mode) {
        auto* uc=create(uint16_t(0x7f|mode*0x400)); // 24-bit precision, all four RC modes
        st(uc,0,0x8000000000000000ull,0x3fff); uint32_t v=0x33800000;
        ok(uc_mem_write(uc,0x2000,&v,4));
        check(run(uc,{0xd8,5,0,0x20,0,0})==UC_ERR_OK,"FADD half-ulp executes");
        status(uc,mode==2 ? 0x220 : 0x20,"FADD precision and round-up bit");
        const auto s=snapshot(uc);
        check(s.significand[0]==(mode==2 ? 0x8000010000000000ull : 0x8000000000000000ull),
              "FADD independently calculated rounded value"); ok(uc_close(uc));
    }
    struct Store {uint64_t low;uint16_t high;uint32_t result,status;};
    const Store cases[]={
        {0x8000008000000000ull,0x3fff,0x3f800000,0x20}, // exact halfway: nearest even down
        {0x800000c000000000ull,0x3fff,0x3f800001,0x220}, // above halfway: round up
        {0x8000000000000000ull,0x407f,0x7f800000,0x228}, // 2^128: overflow + precision
        {0x8000000000000000ull,0x3f69,0,0x30},           // 2^-150: underflow + precision
    };
    for(const auto& c:cases) {
        auto* uc=create(); st(uc,0,c.low,c.high);
        check(run(uc,{0xd9,0x1d,0,0x20,0,0})==UC_ERR_OK,"FSTP32 conversion executes");
        status(uc,c.status,"FSTP32 publishes exception and rounding flags"); uint32_t output=0;
        ok(uc_mem_read(uc,0x2000,&output,4)); const auto s=snapshot(uc);
        check(output==c.result && s.top==1 && s.tags[0]==1,"independent stored bits and pop"); ok(uc_close(uc));
    }
    for(const auto& code:std::vector<std::vector<uint8_t>>{{0xd9,0xc3},{0xd9,0xcb}}) {
        auto* uc=create(); st(uc,0,0x8000000000000000ull,0x3fff);
        st(uc,3,0xc000000000000000ull,0x4000); uint32_t sw=0x200,tags=0xc000;
        ok(uc_reg_write(uc,UC_X86_REG_FPSW,&sw)); ok(uc_reg_write(uc,UC_X86_REG_FPTAG,&tags));
        check(run(uc,code)==UC_ERR_OK,"register transfer executes");
        status(uc,0,"FLD/FXCH clear the defined C1 flag"); ok(uc_close(uc));
    }
    for(const auto& code:std::vector<std::vector<uint8_t>>{
            {0xd9,0xc3},{0xd8,0x0d,0,0x40,0,0},{0xde,0xc1},{0xd9,0x1d,0,0x40,0,0},
            {0xdd,0xc3},{0xd8,0xcb},{0xde,0xeb}}) {
        auto* uc=create(0x37e); st(uc,0,0x8000000000000000ull,0x3fff);
        uint32_t sw=0x8081; ok(uc_reg_write(uc,UC_X86_REG_FPSW,&sw));
        const auto before=snapshot(uc); uint32_t vector=0; uc_hook hook;
        ok(uc_hook_add(uc,&hook,UC_HOOK_INTR,reinterpret_cast<void*>(interrupt),&vector,1,0));
        run(uc,code); const auto after=snapshot(uc);
        check(vector==16,"pending #MF precedes data access and arithmetic");
        check(std::memcmp(&before,&after,sizeof(before))==0,"pending #MF preserves FPU state");
        ok(uc_close(uc));
    }
    {
        auto* uc=create(); st(uc,0,0x800000c000000000ull,0x3fff);
        const auto before=snapshot(uc);
        check(run(uc,{0xd9,0x1d,0,0x40,0,0})==UC_ERR_WRITE_UNMAPPED,"FSTP reports missing memory");
        const auto after=snapshot(uc);
        check(std::memcmp(&before,&after,sizeof(before))==0,"faulted conversion/store changes no FPU state");
        ok(uc_close(uc));
    }
    std::printf("x87 floor independent qualification: %u failures\n",failures);
    return failures ? 1 : 0;
}
