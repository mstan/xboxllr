#include "aot_programs.h"
#include "x87_arithmetic_cases.hpp"
#include "cpu/unicorn_floor.hpp"
#include "machine/machine.hpp"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
using namespace xlle;
static void require(bool ok,const char* why) {
    if(!ok) { std::fprintf(stderr,"AOT x87: %s\n",why); std::exit(1); }
}
static UnicornFloor& floor(XboxMachine& m) { return static_cast<UnicornFloor&>(m.cpu()); }
template<class T> static T get(CpuFloor& c,XReg reg) {
    T v{}; require(c.read_reg(reg,&v,sizeof(v)),"read CPU state"); return v;
}
template<class T> static void put(CpuFloor& c,XReg reg,T v) {
    require(c.write_reg(reg,&v,sizeof(v)),"write CPU state");
}
static constexpr XReg stregs[]={XReg::ST0,XReg::ST1,XReg::ST2,XReg::ST3,XReg::ST4,XReg::ST5,XReg::ST6,XReg::ST7};
struct Snapshot {
    std::array<std::array<uint8_t,10>,8> st;
    std::array<uint16_t,6> words;
    uint64_t ip,dp;
    bool operator==(const Snapshot& b) const { return st==b.st && words==b.words && ip==b.ip && dp==b.dp; }
};
static Snapshot snapshot(CpuFloor& c) {
    Snapshot s{}; for(unsigned i=0;i<8;++i) s.st[i]=get<std::array<uint8_t,10>>(c,stregs[i]);
    const XReg words[]={XReg::FPCW,XReg::FPSW,XReg::FPTAG,XReg::FCS,XReg::FDS,XReg::FOP};
    for(unsigned i=0;i<6;++i) s.words[i]=get<uint16_t>(c,words[i]);
    s.ip=get<uint64_t>(c,XReg::FIP); s.dp=get<uint64_t>(c,XReg::FDP); return s;
}
static void exact(XboxMachine& m,uint32_t pc,uint64_t n) {
    std::string error; const auto retired=m.run(pc,n,error);
    if(!error.empty()||retired!=n) std::fprintf(stderr,"retired=%llu requested=%llu %s\n",
        (unsigned long long)retired,(unsigned long long)n,error.c_str());
    require(error.empty()&&retired==n,"exact execution");
}
static void seed(CpuFloor& c,uint16_t cw,bool empty) {
    put(c,XReg::FPSW,uint16_t(0));
    for(unsigned i=0;i<8;++i) {
        std::array<uint8_t,10> v{}; const uint64_t low=0xc000000000000000ull+i;
        const uint16_t high=0x3fff;
        std::memcpy(v.data(),&low,8); std::memcpy(v.data()+8,&high,2); put(c,stregs[i],v);
    }
    put(c,XReg::FPCW,cw); put(c,XReg::FPSW,uint16_t(0x4700));
    put(c,XReg::FPTAG,uint16_t(empty?0xffff:0xc000));
    put(c,XReg::FIP,uint64_t(0x123456)); put(c,XReg::FDP,uint64_t(0xabcdef));
    put(c,XReg::FOP,uint16_t(0x123));
}
static void prepare(XboxMachine& m,const xlle_aot_region& r,bool native,bool wide,
                    uint16_t cw=0x37f,bool split=false,bool empty=false) {
    require(m.ok()&&m.init_ram()&&m.cpu().setup_flat_segments(0x8000),"machine");
    if(!native) floor(m).force_insn_scheduling();
    require(m.cpu().write_mem(r.base,r.code,r.code_size),"authored program");
    const uint8_t warm[]={0x8b,6,0x8b,0x46,4,0x8b,0x46,8,0x8b,0x46,12,0x8b,0x46,16,0x8b,0x46,20,
                          0x89,7,0x89,0x47,4,0xeb,0xfe};
    require(m.cpu().write_mem(0x1800,warm,sizeof(warm)),"warm program");
    X86Regs regs{}; regs.eip=0x1800; regs.esp=0x9000; regs.eflags=2;
    regs.esi=split?0x2ffe:0x2001; regs.edi=split?0x4ffe:0x4001; m.cpu().set_regs(regs);
    exact(m,0x1800,25);
    const uint32_t narrow[]={0x3fc00000,0x40100000,0x3f000000}; // 1.5, 2.25, 0.5
    const uint64_t broad[]={0x3ff8000000000000ull,0x4002000000000000ull,0x3fe0000000000000ull};
    require(m.cpu().write_mem(regs.esi,wide?static_cast<const void*>(broad):narrow,wide?24:12),"operands");
    const uint64_t sentinel=0x5555555555555555ull;
    require(m.cpu().write_mem(regs.edi,&sentinel,8),"output sentinel");
    seed(m.cpu(),cw,empty);
    regs.eip=r.base; regs.eax=0x11223344; regs.ecx=0x55667788; regs.edx=0x1234;
    regs.ebp=(&r==&xlle_aot_x87_store_ss)?regs.edi:regs.esi; regs.eflags=0x8d7;
    m.cpu().set_regs(regs);
    if(native) { require(m.register_aot_region(r),"native region"); floor(m).require_native_execution(true); }
}
static void compare(const xlle_aot_region& r,uint64_t n,bool wide,uint16_t cw,bool split,bool cycle,
                    uint64_t expected=0) {
    XboxMachine a,b; prepare(a,r,true,wide,cw,split,cycle); prepare(b,r,false,wide,cw,split,cycle);
    const auto before=a.aot_stats(); const auto blocks=floor(a).block_stats();
    exact(a,r.base,n); exact(b,r.base,n);
    if(!(snapshot(a.cpu())==snapshot(b.cpu()))) {
        const auto x=snapshot(a.cpu()),y=snapshot(b.cpu());
        std::fprintf(stderr,"pc=%08x count=%llu native sw=%04x tag=%04x fop=%04x ip=%llx dp=%llx; floor sw=%04x tag=%04x fop=%04x ip=%llx dp=%llx\n",
            r.base,(unsigned long long)n,x.words[1],x.words[2],x.words[5],(unsigned long long)x.ip,(unsigned long long)x.dp,
            y.words[1],y.words[2],y.words[5],(unsigned long long)y.ip,(unsigned long long)y.dp);
    }
    require(snapshot(a.cpu())==snapshot(b.cpu()),"all x87 register/environment state");
    const auto ar=a.cpu().get_regs(),br=b.cpu().get_regs();
    require(std::memcmp(&ar,&br,sizeof(ar))==0,"all integer state preserved");
    uint64_t av=0,bv=0; require(a.cpu().read_mem(ar.edi,&av,8)&&b.cpu().read_mem(br.edi,&bv,8),"output bytes");
    require(av==bv,"all output bytes agree");
    if(cycle&&n>=7) require(wide?av==(expected?expected:0x4008000000000000ull):
        uint32_t(av)==(expected?expected:0x40400000),"independently calculated cycle output");
    const auto after=a.aot_stats(); const auto now=floor(a).block_stats();
    require(after.retired-before.retired==n&&after.fallback_insns==before.fallback_insns&&
            after.fallback_runs==before.fallback_runs,"strict native scope has zero fallback");
    require(now.tb_entries==blocks.tb_entries&&now.block_mode_runs==blocks.block_mode_runs&&
            now.insn_mode_runs==blocks.insn_mode_runs,"helpers execute no guest code");
}
static void refusal(unsigned reason,bool store) {
    const auto& r=store?xlle_aot_x87_pop64:xlle_aot_x87_load64;
    XboxMachine m; prepare(m,r,true,true);
    auto regs=m.cpu().get_regs();
    if(reason==0) { regs.esi=regs.edi=0x0e000000; m.cpu().set_regs(regs); }
    if(reason==1) { regs.esi=0x2ffc; regs.edi=0x4ffc; m.cpu().set_regs(regs); }
    if(reason==2) put(m.cpu(),XReg::FPCW,uint16_t(0x37e));
    if(reason==3) put(m.cpu(),XReg::CR0,get<uint32_t>(m.cpu(),XReg::CR0)|8u);
    if(reason==4) put(m.cpu(),XReg::CR0,get<uint32_t>(m.cpu(),XReg::CR0)|4u);
    if(reason==5) put(m.cpu(),XReg::FPSW,uint16_t(0x8081));
    if(reason==6) put(m.cpu(),XReg::FPTAG,uint16_t(store?0xffff:0));
    const auto before=snapshot(m.cpu()); const auto old=floor(m).block_stats();
    std::array<uint8_t,16> bytes{},after{};
    require(m.cpu().read_mem(0x4ff8,bytes.data(),bytes.size()),"split destination before");
    std::string error; require(m.run(r.base,1,error)==0&&error.find("native-required fallback refused")!=std::string::npos,"precise native refusal");
    require(snapshot(m.cpu())==before&&m.cpu().get_regs().eip==r.base,"refusal changes no FPU/EIP state");
    require(m.cpu().read_mem(0x4ff8,after.data(),after.size())&&bytes==after,"no partial store");
    require(floor(m).block_stats().tb_entries==old.tb_entries,"refusal executes no fallback");
}
static void smc() {
    XboxMachine m; prepare(m,xlle_aot_x87_smc,true,false);
    // Exactly representable single-precision bits 0x40404040 become four INC
    // instructions, written through the physical RAM alias into future code.
    std::array<uint8_t,10> st{}; const uint64_t low=0xc040400000000000ull; const uint16_t high=0x4000;
    std::memcpy(st.data(),&low,8); std::memcpy(st.data()+8,&high,2); put(m.cpu(),XReg::ST0,st);
    // Warm the destination alias with an executed store of the original bytes.
    floor(m).require_native_execution(false);
    const uint8_t warm[]={0xc7,5,2,0x10,0,4,0x90,0x90,0x90,0x90};
    require(m.cpu().write_mem(0x1800,warm,sizeof(warm)),"alias warm-up"); exact(m,0x1800,1);
    auto regs=m.cpu().get_regs(); regs.edi=0x04001002; regs.eip=0x1000; m.cpu().set_regs(regs);
    floor(m).require_native_execution(true); const auto before=m.aot_stats(); std::string error;
    require(m.run(0x1000,5,error)==1&&error.find("code-changed")!=std::string::npos,"x87 store invalidates following generated code");
    uint32_t v=0; require(m.cpu().read_mem(0x1002,&v,4)&&v==0x40404040,"x87 physical alias store committed once");
    require(m.aot_stats().retired-before.retired==1,"only store retired natively");
}
static void put_extended(CpuFloor& c,XReg reg,uint64_t low,uint16_t high) {
    std::array<uint8_t,10> value{};
    std::memcpy(value.data(),&low,8); std::memcpy(value.data()+8,&high,2); put(c,reg,value);
}
static void independent_arithmetic() {
    // Exact halfway additions cover every defined precision, rounding mode
    // and sign. The expected bits follow directly from the requested ULP.
    for(unsigned precision:{0u,2u,3u}) for(unsigned rc=0;rc<4;++rc) for(bool negative:{false,true})
      for(unsigned operation=0;operation<3;++operation) {
        XboxMachine m; const auto& r=operation==0?xlle_aot_x87_addp3:
            operation==1?xlle_aot_x87_subp3:xlle_aot_x87_mulreg3;
        prepare(m,r,true,false,uint16_t(0x7f|precision*0x100|rc*0x400));
        const unsigned bits=precision==0?24:precision==2?53:64;
        const uint16_t sign=negative?0x8000:0;
        uint64_t truncated=0x8000000000000000ull;
        if(operation==2) {
            // (1+2^-a)*(1+2^-b), a+b=precision, leaves exactly half an
            // output ULP beyond the representable sum 1+2^-a+2^-b.
            const unsigned a=bits/2,b=bits-a;
            const uint64_t abit=uint64_t(1)<<(63-a),bbit=uint64_t(1)<<(63-b);
            put_extended(m.cpu(),XReg::ST0,truncated+abit,uint16_t(0x3fff|sign));
            put_extended(m.cpu(),XReg::ST3,truncated+bbit,0x3fff);
            truncated+=abit+bbit;
        } else {
            const uint16_t source_sign=uint16_t(sign^(operation==1?0x8000:0));
            put_extended(m.cpu(),XReg::ST0,0x8000000000000000ull,uint16_t((0x3fff-bits)|source_sign));
            put_extended(m.cpu(),XReg::ST3,0x8000000000000000ull,uint16_t(0x3fff|sign));
        }
        const bool up=rc==(negative?1u:2u);
        const auto before=m.aot_stats(); exact(m,r.base,1);
        const auto value=get<std::array<uint8_t,10>>(m.cpu(),operation==2?XReg::ST0:XReg::ST2);
        uint64_t low=0; uint16_t high=0;
        std::memcpy(&low,value.data(),8); std::memcpy(&high,value.data()+8,2);
        require(low==(truncated+(up?(uint64_t(1)<<(64-bits)):0)) && high==(0x3fff|sign),
                "independent native halfway add/subtract/multiply bits");
        require((get<uint16_t>(m.cpu(),XReg::FPSW)&0x82ff)==(up?0x220:0x20),
                "independent native precision/C1 flags");
        require(m.aot_stats().retired-before.retired==1 && m.aot_stats().fallback_insns==before.fallback_insns,
                "independent rounding case executes natively");
    }
    for(bool wide:{false,true}) {
        XboxMachine m; const auto& r=wide?xlle_aot_x87_mul64:xlle_aot_x87_mul32;
        prepare(m,r,true,wide); put_extended(m.cpu(),XReg::ST0,0,0);
        const uint64_t infinity=wide?0x7ff0000000000000ull:0x7f800000;
        require(m.cpu().write_mem(m.cpu().get_regs().esi,&infinity,wide?8:4),"infinity operand");
        exact(m,r.base,1);
        const auto value=get<std::array<uint8_t,10>>(m.cpu(),XReg::ST0);
        uint64_t low=0; uint16_t high=0; std::memcpy(&low,value.data(),8); std::memcpy(&high,value.data()+8,2);
        require(low==0xc000000000000000ull && high==0xffff &&
                (get<uint16_t>(m.cpu(),XReg::FPSW)&0x82ff)==1,"native masked invalid produces indefinite and IE");
    }
    struct Store {uint64_t low;uint16_t high;uint32_t result,status;};
    for(const auto& c:std::array<Store,4>{{
        {0x8000008000000000ull,0x3fff,0x3f800000,0x20},
        {0x800000c000000000ull,0x3fff,0x3f800001,0x220},
        {0x8000000000000000ull,0x407f,0x7f800000,0x228},
        {0x8000000000000000ull,0x3f69,0,0x30}}}) {
        XboxMachine m; const auto& r=xlle_aot_x87_pop32; prepare(m,r,true,false);
        put_extended(m.cpu(),XReg::ST0,c.low,c.high); exact(m,r.base,1);
        uint32_t value=0; require(m.cpu().read_mem(m.cpu().get_regs().edi,&value,4),"conversion output");
        require(value==c.result && (get<uint16_t>(m.cpu(),XReg::FPSW)&0x82ff)==c.status,
                "native store independently qualifies rounding/overflow/underflow flags and bits");
    }
}
static void register_math_and_free() {
    for(const auto& c:x87_multiply_memory_cases) for(bool wide:{false,true}) {
        XboxMachine m; const auto& r=wide?xlle_aot_x87_mul64:xlle_aot_x87_mul32;
        prepare(m,r,true,wide); put_extended(m.cpu(),XReg::ST0,c.st_low,c.st_high);
        const uint64_t operand=wide?c.memory64:c.memory32;
        require(m.cpu().write_mem(m.cpu().get_regs().esi,&operand,wide?8:4),"memory multiply special operand");
        exact(m,r.base,1);
        const auto value=get<std::array<uint8_t,10>>(m.cpu(),XReg::ST0);
        uint64_t low=0; uint16_t high=0; std::memcpy(&low,value.data(),8); std::memcpy(&high,value.data()+8,2);
        require(low==c.result_low && high==(wide?c.result_high64:c.result_high32) &&
                (get<uint16_t>(m.cpu(),XReg::FPSW)&0x82ff)==c.status,
                "native memory conversion/arithmetic has joint exception priority and NaN result");
    }
    for(const auto& c:x87_arithmetic_cases) {
        XboxMachine m; const auto& r=c.subtract?xlle_aot_x87_subp3:xlle_aot_x87_mulreg3;
        prepare(m,r,true,false);
        put_extended(m.cpu(),XReg::ST0,c.a_low,c.a_high);
        put_extended(m.cpu(),XReg::ST3,c.b_low,c.b_high);
        exact(m,r.base,1);
        const auto value=get<std::array<uint8_t,10>>(m.cpu(),c.subtract?XReg::ST2:XReg::ST0);
        uint64_t low=0; uint16_t high=0; std::memcpy(&low,value.data(),8); std::memcpy(&high,value.data()+8,2);
        require(low==c.result_low && high==c.result_high &&
                (get<uint16_t>(m.cpu(),XReg::FPSW)&0x82ff)==c.status,
                "native independent extended denormal/NaN/invalid result and exception priority");
    }
    const xlle_aot_region* regions[][8]={
        {&xlle_aot_x87_mulreg0,&xlle_aot_x87_mulreg1,&xlle_aot_x87_mulreg2,&xlle_aot_x87_mulreg3,
         &xlle_aot_x87_mulreg4,&xlle_aot_x87_mulreg5,&xlle_aot_x87_mulreg6,&xlle_aot_x87_mulreg7},
        {&xlle_aot_x87_subp0,&xlle_aot_x87_subp1,&xlle_aot_x87_subp2,&xlle_aot_x87_subp3,
         &xlle_aot_x87_subp4,&xlle_aot_x87_subp5,&xlle_aot_x87_subp6,&xlle_aot_x87_subp7},
        {&xlle_aot_x87_free0,&xlle_aot_x87_free1,&xlle_aot_x87_free2,&xlle_aot_x87_free3,
         &xlle_aot_x87_free4,&xlle_aot_x87_free5,&xlle_aot_x87_free6,&xlle_aot_x87_free7}};
    for(unsigned family=0;family<3;++family) for(unsigned top=0;top<8;++top) for(unsigned reg=0;reg<8;++reg) {
        const auto& r=*regions[family][reg]; XboxMachine a,b;
        prepare(a,r,true,false); prepare(b,r,false,false);
        for(auto* m:{&a,&b}) {
            put(m->cpu(),XReg::FPSW,uint16_t(0x4700|(top<<11)));
            put(m->cpu(),XReg::FPTAG,uint16_t(family==2 && top%2?0xffff:0));
            put_extended(m->cpu(),XReg::ST0,0xc000000000000000ull,0x3fff); // 1.5
            if(reg) put_extended(m->cpu(),stregs[reg],0x9000000000000000ull,0x4002); // 9
        }
        const auto initial=snapshot(a.cpu()); const auto old=a.aot_stats(); const auto blocks=floor(a).block_stats();
        exact(a,r.base,1); exact(b,r.base,1);
        const auto result=snapshot(a.cpu());
        require(result==snapshot(b.cpu()),"register math/free matches complete floor state at every TOP/index");
        require(a.aot_stats().retired-old.retired==1 && a.aot_stats().fallback_insns==old.fallback_insns &&
                floor(a).block_stats().tb_entries==blocks.tb_entries,"register math/free strict native retirement");
        if(family<2) {
            // FMUL: 1.5*9=13.5 (or 1.5^2=2.25). FSUBP: 9-1.5=7.5;
            // ST(0)-ST(0)=0 is popped but its raw physical bits remain inspectable.
            const unsigned slot=family==0?0:reg?reg-1:7;
            const uint64_t expected_low=family==0?(reg?0xd800000000000000ull:0x9000000000000000ull):
                reg?0xf000000000000000ull:0;
            const uint16_t expected_high=family==0?(reg?0x4002:0x4000):reg?0x4001:0;
            uint64_t low=0; uint16_t high=0;
            std::memcpy(&low,result.st[slot].data(),8); std::memcpy(&high,result.st[slot].data()+8,2);
            require(low==expected_low && high==expected_high,"independent register multiply/subtract result and direction");
            require((result.words[1]&0x82ff)==0,"exact register math has no new exception or C1");
            require(((result.words[1]>>11)&7)==((top+(family==1))&7),"independent pop TOP");
        } else {
            require(result.st==initial.st && result.words[1]==initial.words[1],"FFREE preserves all values and TOP");
            const auto tagmask=uint16_t(3u<<(((top+reg)&7)*2));
            require(result.words[2]==uint16_t(initial.words[2]|tagmask),"FFREE sets only selected physical tag");
        }
    }
    // A pending fault or missing source must decline before tag, arithmetic or
    // environment changes. FFREE is permitted on empty data, but still waits.
    for(unsigned family=0;family<3;++family) for(unsigned reason=0;reason<(family==2?3u:4u);++reason) {
        const auto& r=*regions[family][3]; XboxMachine m; prepare(m,r,true,false);
        if(reason==0) put(m.cpu(),XReg::FPSW,uint16_t(0x8081));
        if(reason==1) put(m.cpu(),XReg::FPCW,uint16_t(0x37e));
        if(reason==2) put(m.cpu(),XReg::CR0,get<uint32_t>(m.cpu(),XReg::CR0)|8u);
        if(reason==3) put(m.cpu(),XReg::FPTAG,uint16_t(0xc0));
        const auto before=snapshot(m.cpu()); const auto blocks=floor(m).block_stats(); std::string error;
        require(m.run(r.base,1,error)==0 && error.find("native-required fallback refused")!=std::string::npos,
                "register math/free precise native refusal");
        require(snapshot(m.cpu())==before && m.cpu().get_regs().eip==r.base &&
                floor(m).block_stats().tb_entries==blocks.tb_entries,"decline preserves complete FPU/EIP with no hidden fallback");
    }
}
int main() {
    independent_arithmetic();
    register_math_and_free();
    for(unsigned precision:{0u,2u,3u}) for(unsigned rc=0;rc<4;++rc) {
        const uint16_t cw=uint16_t(0x7f|precision*0x100|rc*0x400);
        for(uint64_t n=1;n<=8;++n) compare(xlle_aot_x87_mulsub_cycle,n,false,cw,false,true,0x3fc00000);
        compare(xlle_aot_x87_mulsub_cycle,80,false,cw,true,true,0x3fc00000);
    }
    for(bool wide:{false,true}) for(unsigned precision:{0u,2u,3u}) for(unsigned rc=0;rc<4;++rc) {
        const uint16_t cw=uint16_t(0x7f|precision*0x100|rc*0x400);
        const auto& r=wide?xlle_aot_x87_cycle64:xlle_aot_x87_cycle32;
        for(uint64_t prefix=1;prefix<=9;++prefix) compare(r,prefix,wide,cw,false,true);
        compare(r,90,wide,cw,true,true);
    }
    for(const auto* r:{&xlle_aot_x87_load32,&xlle_aot_x87_load64,&xlle_aot_x87_mul32,&xlle_aot_x87_mul64,
                      &xlle_aot_x87_store32,&xlle_aot_x87_store64,&xlle_aot_x87_pop32,&xlle_aot_x87_pop64,
                      &xlle_aot_x87_fld3,&xlle_aot_x87_fxch3,&xlle_aot_x87_addp3,&xlle_aot_x87_fstp3,
                      &xlle_aot_x87_load_ss,&xlle_aot_x87_store_ss}) compare(*r,1,true,0x37f,false,false);
    for(unsigned i=0;i<7;++i) for(bool store:{false,true}) refusal(i,store);
    smc();
    std::puts("AOT x87: strict native precision/rounding, every boundary, environment, split spans, refusals and SMC pass");
}
