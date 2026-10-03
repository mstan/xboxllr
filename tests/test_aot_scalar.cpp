#include "scalar_cases.hpp"
#include "cpu/unicorn_floor.hpp"
#include "machine/machine.hpp"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace xlle;
static void check(bool ok,const char* why) {
    if(!ok){std::fprintf(stderr,"AOT scalar: %s\n",why);std::exit(1);}
}
struct Access {uint32_t address;unsigned size;bool write;
    bool operator==(const Access& b)const{return address==b.address&&size==b.size&&write==b.write;}};
struct Memory {
    std::vector<uint8_t> bytes=std::vector<uint8_t>(0x4000);
    std::vector<Access> accesses;
    xlle_aot_bus bus{};
    bool reject=false, event_after_write=false, event_after_read=false, code_after_read=false;
    explicit Memory(const xlle_aot_region& r) {
        std::copy(r.code,r.code+r.code_size,bytes.begin());
        bus.context=XLLE_AOT_CONTEXT_FLAT32_RAM;bus.code_base=r.base;
        bus.code=bytes.data();bus.code_size=r.code_size;bus.opaque=this;
        bus.read32=read32;bus.write32=write32;bus.read_narrow=narrow;
    }
    bool range(uint32_t address,unsigned size)const {
        return !reject && address>=0x1000 && uint64_t(address)+size<=0x1000+bytes.size();
    }
    static int read(void* opaque,uint32_t address,unsigned size,uint32_t* value) {
        auto& m=*static_cast<Memory*>(opaque);
        if(!m.range(address,size))return 0;
        uint32_t result=0;
        for(unsigned i=0;i<size;++i)result|=uint32_t(m.bytes[address-0x1000+i])<<(8*i);
        *value=result;m.accesses.push_back({address,size,false});
        if(m.event_after_read)m.bus.event_pending=1;
        if(m.code_after_read)m.bytes[0]^=1;
        return 1;
    }
    static int narrow(void* p,uint32_t a,unsigned n,uint32_t* v) {
        return (n==1||n==2)&&read(p,a,n,v);
    }
    static int read32(void* p,uint32_t a,uint32_t* v){return read(p,a,4,v);}
    static int write32(void* p,uint32_t a,uint32_t v) {
        auto& m=*static_cast<Memory*>(p);if(!m.range(a,4))return 0;
        for(unsigned i=0;i<4;++i)m.bytes[a-0x1000+i]=uint8_t(v>>(8*i));
        m.accesses.push_back({a,4,true});if(m.event_after_write)m.bus.event_pending=1;return 1;
    }
};
static X86Regs regs(const xlle_aot_state& s) {
    return {s.gpr[0],s.gpr[1],s.gpr[2],s.gpr[3],s.gpr[4],s.gpr[5],s.gpr[6],s.gpr[7],s.eip,s.eflags};
}
static xlle_aot_state state(const X86Regs& r) {
    return {{r.eax,r.ecx,r.edx,r.ebx,r.esp,r.ebp,r.esi,r.edi},r.eip,r.eflags,0};
}
static void equal(const xlle_aot_state& a,const xlle_aot_state& b,uint32_t mask,const char* why) {
    if(!std::equal(a.gpr,a.gpr+8,b.gpr)||a.eip!=b.eip||((a.eflags^b.eflags)&mask)) {
        std::fprintf(stderr,"EIP %08x/%08x flags %08x/%08x\n",a.eip,b.eip,a.eflags,b.eflags);
        check(false,why);
    }
}
static xlle_aot_state oracle(const xlle_aot_state& initial,uint64_t count,const Memory& input,
                             std::vector<uint8_t>* output=nullptr,std::vector<Access>* accesses=nullptr) {
    UnicornFloor cpu;check(cpu.ok()&&cpu.map_mem(0x1000,input.bytes.size()),"reference RAM");
    cpu.on_retired([](const RetiredInsn&){});
    if(accesses)cpu.on_mem([&](const MemEvent& e){accesses->push_back({e.addr,e.size,e.write!=0});});
    check(cpu.write_mem(0x1000,input.bytes.data(),input.bytes.size()),"reference contents");
    cpu.set_regs(regs(initial));std::string error;
    check(cpu.run(initial.eip,count,error)==count&&error.empty(),"reference exact retirement");
    if(output){output->resize(input.bytes.size());check(cpu.read_mem(0x1000,output->data(),output->size()),"reference result");}
    return state(cpu.get_regs());
}
static xlle_aot_state seed(const xlle_aot_region& r,uint32_t flags=0x8d7) {
    xlle_aot_state s{};s.eip=r.base;s.eflags=flags;
    for(unsigned i=0;i<8;++i)s.gpr[i]=0x81234567+0x1020304u*i;
    return s;
}
static uint32_t arithmetic_expected(unsigned operation,uint32_t left,uint32_t right,uint32_t& flags) {
    uint32_t result=0,bits=0;
    const bool subtract=operation==1||operation==5||operation==6;
    if(operation==0||subtract) {
        const int64_t a=int64_t(left)-(left>>31?INT64_C(4294967296):0);
        const int64_t b=int64_t(right)-(right>>31?INT64_C(4294967296):0);
        const int64_t signed_result=subtract?a-b:a+b;
        const int64_t unsigned_result=subtract?int64_t(left)-right:int64_t(left)+right;
        result=uint32_t(unsigned_result);
        if(unsigned_result<0||unsigned_result>UINT32_MAX)bits|=1;
        if(signed_result<INT64_C(-2147483648)||signed_result>INT64_C(2147483647))bits|=0x800;
        if(subtract?(left&15)<(right&15):(left&15)+(right&15)>15)bits|=0x10;
    } else {
        result=operation==2?left^right:operation==4?left|right:left&right;
        // Logical AF is undefined architecturally. The existing native policy
        // preserves it; differential checks must exclude this one flag.
    }
    unsigned ones=0;for(unsigned i=0;i<8;++i)ones+=(result>>i)&1u;
    if(!(ones&1))bits|=4;
    if(!result)bits|=0x40;
    if(result>>31)bits|=0x80;
    flags=(flags&~(operation==0||subtract?0x8d5u:0x8c5u))|bits;
    return result;
}
static void put_word(Memory& m,uint32_t address,uint32_t value) {
    check(m.range(address,4),"authored word address");
    for(unsigned i=0;i<4;++i)m.bytes[address-0x1000+i]=uint8_t(value>>(8*i));
}
static void unary_integer() {
    const uint32_t values[]={0,1,2,0xf,0x10,0x11,0x7fffffff,0x80000000,0x80000001,
                             0xffffffff,0xfffffff0,0x12345678};
    for(const auto& c:unary_cases)for(auto value:values)for(auto flags:{2u,0x8d7u,0x843u}) {
        Memory m(*c.r);auto initial=seed(*c.r,flags);initial.gpr[c.reg]=value;
        auto actual=initial,expected=initial;
        expected.gpr[c.reg]=c.operation==0?arithmetic_expected(1,0,value,expected.eflags):~value;
        expected.eip+=2;
        check(c.r->run(c.r,&actual,&m.bus,1)==XLLE_AOT_COMPLETE,"unary exact completion");
        equal(actual,expected,0xffffffffu,"unary independent arithmetic and flags");
        equal(actual,oracle(initial,1,m),0xffffffffu,"unary floor comparison");
        check(actual.retired==1&&m.accesses.empty(),"unary exact retirement and no memory");
    }
}
static void read_integer() {
    const uint32_t values[]={0,1,15,16,0x7fffffff,0x80000000,0xffffffff,0x81234567};
    for(const auto& c:alu_read_cases) {
        Memory m(*c.r);
        for(auto left:values)for(auto right:values)for(auto flags:{2u,0x8d7u}) {
            auto initial=seed(*c.r,flags);initial.gpr[c.reg]=left;
            uint32_t address=0x2800;
            if(c.form<8)initial.gpr[c.form]=address;
            if(c.form==9){initial.gpr[3]=0x2800;initial.gpr[6]=3;address=0x2808;}
            put_word(m,address,right);m.accesses.clear();
            auto actual=initial,expected=initial;
            const auto result=arithmetic_expected(c.operation,
                c.operation==6?right:initial.gpr[c.reg],
                c.operation==6?initial.gpr[c.reg]:right,expected.eflags);
            if(c.operation<5)expected.gpr[c.reg]=result;
            expected.eip+=uint32_t(c.r->code_size);
            check(c.r->run(c.r,&actual,&m.bus,1)==XLLE_AOT_COMPLETE,"read ALU completes");
            if(actual.eflags!=expected.eflags)
                std::fprintf(stderr,"read ALU op=%u reg=%u form=%u left=%08x right=%08x flags=%08x\n",
                             c.operation,c.reg,c.form,initial.gpr[c.reg],right,flags);
            equal(actual,expected,0xffffffffu,"read ALU independent arithmetic/flags/address aliases");
            check(actual.retired==1&&m.accesses==std::vector<Access>{{address,4,false}},
                  "read ALU exact single read and retirement");
            if(left==0x7fffffff&&right==0x80000000&&flags==0x8d7) {
                std::vector<Access> access;std::vector<uint8_t> bytes;
                const uint32_t defined=c.operation==2||c.operation==3||c.operation==4||c.operation==7
                    ?~0x10u:0xffffffffu;
                equal(actual,oracle(initial,1,m,&bytes,&access),defined,"read ALU floor");
                check(access==m.accesses&&bytes==m.bytes,"read ALU floor memory effects");
                m.reject=true;m.accesses.clear();actual=initial;
                check(c.r->run(c.r,&actual,&m.bus,1)==XLLE_AOT_MEMORY,"read ALU refusal");
                equal(actual,initial,0xffffffffu,"refused ALU keeps registers and flags");
                check(actual.retired==0&&m.accesses.empty(),"refused ALU has no effects");
                m.reject=false;
            }
        }
    }
    for(const auto& c:alu_immediate_cases)for(auto value:values)for(auto flags:{2u,0x8d7u}) {
        Memory m(*c.r);auto initial=seed(*c.r,flags);initial.gpr[3]=0x2800;initial.gpr[6]=3;
        put_word(m,0x2808,value);auto expected=initial,actual=initial;
        arithmetic_expected(c.operation,value,c.immediate,expected.eflags);
        expected.eip+=uint32_t(c.r->code_size);
        check(c.r->run(c.r,&actual,&m.bus,1)==XLLE_AOT_COMPLETE,"immediate read comparison");
        equal(actual,expected,0xffffffffu,"immediate sign extension and operand order");
        equal(actual,oracle(initial,1,m),c.operation==7?~0x10u:0xffffffffu,"immediate compare/test floor");
        check(actual.retired==1&&m.accesses==std::vector<Access>{{0x2808,4,false}},"immediate read effects");
    }
}
static void epilogue_and_read_exits() {
    const auto& leave=xlle_aot_leave32;
    for(auto frame:{0x2000u,0x2001u,0x2ffcu,0x2fffu})for(auto value:{0u,0x12345678u,0xffffffffu}) {
        Memory m(leave);auto initial=seed(leave,0x8d7);initial.gpr[4]=0x4800;initial.gpr[5]=frame;
        put_word(m,frame,value);auto expected=initial,actual=initial;
        expected.gpr[4]=frame+4;expected.gpr[5]=value;expected.eip++;
        check(leave.run(&leave,&actual,&m.bus,1)==XLLE_AOT_COMPLETE,"LEAVE completes");
        equal(actual,expected,0xffffffffu,"LEAVE preserves flags and uses old EBP");
        equal(actual,oracle(initial,1,m),0xffffffffu,"LEAVE floor");
        check(actual.retired==1&&m.accesses==std::vector<Access>{{frame,4,false}},"LEAVE stack access");
        m.reject=true;m.accesses.clear();actual=initial;
        check(leave.run(&leave,&actual,&m.bus,1)==XLLE_AOT_MEMORY,"LEAVE declined stack read");
        equal(actual,initial,0xffffffffu,"LEAVE does not move ESP before certification");
        check(actual.retired==0&&m.accesses.empty(),"LEAVE refusal has no effects");
    }
    for(const auto* r:{&xlle_aot_read_then_nop,&xlle_aot_leave_then_nop})for(unsigned mode=0;mode<4;++mode) {
        Memory m(*r);auto initial=seed(*r,0x8d7);initial.gpr[6]=initial.gpr[5]=0x2800;
        initial.gpr[4]=0x4800;put_word(m,0x2800,0x12345678);
        auto actual=initial,expected=initial;
        if(mode==0)m.bus.event_pending=1;
        if(mode==2)m.event_after_read=true;
        if(mode==3)m.code_after_read=true;
        const auto result=r->run(r,&actual,&m.bus,mode==1?0:2);
        if(mode>=2) {
            expected.eip+=r==&xlle_aot_read_then_nop?2:1;
            if(r==&xlle_aot_read_then_nop)
                expected.gpr[0]=arithmetic_expected(0,initial.gpr[0],0x12345678,expected.eflags);
            else {expected.gpr[4]=0x2804;expected.gpr[5]=0x12345678;}
        }
        const auto exit=mode==1?XLLE_AOT_BUDGET:mode==3?XLLE_AOT_CODE_CHANGED:XLLE_AOT_EVENT;
        check(result==exit,"read boundary explicit exit");
        equal(actual,expected,0xffffffffu,"read boundary exact visible state");
        check(actual.retired==(mode>=2?1u:0u)&&m.accesses.size()==(mode>=2?1u:0u),
              "read boundary commits only the completed instruction");
    }
}
static void shifts() {
    const uint32_t patterns[]={0,1,0x80000000,0xffffffff,0x12345678,0xa5a55a5a};
    for(const auto& c:shift_cases) for(unsigned raw=0;raw<(c.mode==0?256u:1u);++raw)
        for(auto value:patterns)for(auto flags:{2u,0x8d7u}) {
            Memory m(*c.r);auto initial=seed(*c.r,flags);initial.gpr[c.reg]=value;
            const unsigned count=c.mode==0?raw:c.count,n=count%32;
            if(c.mode==0)initial.gpr[1]=(initial.gpr[1]&0xffffff00u)|count;
            const uint32_t operand=initial.gpr[c.reg];
            auto expected=initial;expected.eip+=uint32_t(c.r->code_size);
            uint32_t result=0;
            // Independent bit placement, including sign-fill beyond bit 31.
            for(int bit=0;bit<32;++bit) {
                const int source=c.operation==0?bit-int(n):bit+int(n);
                const bool set=source>=0&&source<32 ? (operand&(1u<<source))!=0 :
                    source>=32&&c.operation==2&&(operand&0x80000000u);
                if(set)result|=1u<<bit;
            }
            expected.gpr[c.reg]=result;uint32_t defined=0xffffffffu;
            if(n) {
                unsigned ones=0;for(unsigned bit=0;bit<8;++bit)ones+=(result>>bit)&1u;
                const unsigned carry=(operand>>(c.operation==0?32-n:n-1))&1u;
                expected.eflags=(flags&~0x8d5u)|carry|(ones%2?0:4)|(result?0:0x40)|
                    (result&0x80000000u?0x80:0);
                if(n==1) {
                    const bool high=(operand&0x80000000u)!=0,second=(operand&0x40000000u)!=0;
                    if(c.operation==0?high!=second:c.operation==1&&high)expected.eflags|=0x800;
                } else defined&=~0x800u;
                defined&=~0x10u;
            }
            auto actual=initial;
            check(c.r->run(c.r,&actual,&m.bus,1)==XLLE_AOT_COMPLETE,"shift completes once");
            equal(actual,expected,defined,"shift independently expected bits/flags");
            check(actual.retired==1&&m.accesses.empty(),"shift has no memory effects");
            if(value==0x12345678 && flags==0x8d7 && (c.mode!=0||raw<3||raw==31||raw==32||raw==33||raw==255))
                equal(actual,oracle(initial,1,m),0xffffffffu,"shift full floor flags including undefined policy");
        }
}
static void conditions() {
    for(const auto& c:branch_cases) for(unsigned bits=0;bits<64;++bits) {
        const bool carry=bits&1,parity=bits&2,zero=bits&4,sign=bits&8,overflow=bits&16;
        const bool truths[]={overflow,!overflow,carry,!carry,zero,!zero,carry||zero,!carry&&!zero,
            sign,!sign,parity,!parity,sign!=overflow,sign==overflow,zero||sign!=overflow,!zero&&sign==overflow};
        auto initial=seed(*c.r,2u|(carry?1:0)|(parity?4:0)|(zero?0x40:0)|(sign?0x80:0)|
                          (overflow?0x800:0)|(bits&32?0x10:0));
        Memory m(*c.r);auto expected=initial,actual=initial;
        if(c.limited) {
            // The taken target is one byte beyond CS; the fallthrough NOP
            // remains fetchable. Exercise the generated branch, not a helper.
            m.bus.context=XLLE_AOT_CONTEXT_MACHINE_FLAT32_RAM;
            m.bus.code_limit=c.r->base+uint32_t(c.r->code_size)-1;
            const bool taken=truths[c.condition];
            if(!taken)expected.eip+=c.length;
            check(c.r->run(c.r,&actual,&m.bus,1)==(taken?XLLE_AOT_CONTEXT:XLLE_AOT_BUDGET),
                  "taken CS-limit decline versus valid fallthrough");
            equal(actual,expected,0xffffffffu,"CS-limit precise branch state");
            check(actual.retired==(taken?0u:1u)&&m.accesses.empty(),"CS-limit retirement");
            continue;
        }
        expected.eip+=truths[c.condition]?(c.backward?0:c.length+1):c.length;
        check(c.r->run(c.r,&actual,&m.bus,1)==XLLE_AOT_BUDGET,"conditional branch exact boundary");
        equal(actual,expected,0xffffffffu,"condition independent truth table");
        equal(actual,oracle(initial,1,m),0xffffffffu,"condition floor");
        check(actual.retired==1&&m.accesses.empty(),"condition no data access");
    }
}
static void extensions() {
    for(const auto& c:extend_cases) for(auto value:{0u,0xffffffffu,0x12345678u,0x800080ffu}) {
        Memory m(*c.r);auto initial=seed(*c.r);initial.gpr[c.source&3]=value;
        if(c.width==16)initial.gpr[c.source]=value;
        auto expected=initial,actual=initial;expected.eip+=3;
        expected.gpr[c.destination]=c.width==16?value%65536u:
            (c.source<4?value%256u:(value/256u)%256u);
        check(c.r->run(c.r,&actual,&m.bus,1)==XLLE_AOT_COMPLETE,"MOVZX register completes");
        equal(actual,expected,0xffffffffu,"MOVZX exact source and zero extension");
        equal(actual,oracle(initial,1,m),0xffffffffu,"MOVZX register floor");
    }
    const xlle_aot_region* immediates[]={&xlle_aot_immediate_reg_0,&xlle_aot_immediate_reg_1,
        &xlle_aot_immediate_reg_2,&xlle_aot_immediate_reg_3,&xlle_aot_immediate_reg_4,
        &xlle_aot_immediate_reg_5,&xlle_aot_immediate_reg_6,&xlle_aot_immediate_reg_7};
    for(unsigned i=0;i<8;++i){Memory m(*immediates[i]);auto initial=seed(*immediates[i]),actual=initial;
        auto expected=initial;expected.gpr[i]=0x12345678;expected.eip+=6;
        check(immediates[i]->run(immediates[i],&actual,&m.bus,1)==XLLE_AOT_COMPLETE,"MOV C7 register");
        equal(actual,expected,0xffffffffu,"MOV C7 exact destination");}
}
static void memory_forms() {
    struct Case {const xlle_aot_region* r;unsigned width,destination;uint32_t address;bool write;};
    const Case cases[]={
        {&xlle_aot_narrow_byte,1,0,0x4fff,false},{&xlle_aot_narrow_word,2,0,0x4ffe,false},
        {&xlle_aot_narrow_alias,1,0,0x2101,false},{&xlle_aot_narrow_word_sib,2,0,0x2404,false},
        {&xlle_aot_narrow_word_stack,2,1,0x2400,false},
        {&xlle_aot_immediate_store,4,0,0x2201,true},{&xlle_aot_immediate_store_sib,4,0,0x2404,true},
        {&xlle_aot_immediate_store_stack,4,0,0x2400,true}};
    for(const auto& c:cases) {
        Memory m(*c.r);auto initial=seed(*c.r);
        initial.gpr[0]=0x2101;initial.gpr[3]=0x2400;initial.gpr[6]=2;
        initial.gpr[4]=initial.gpr[5]=0x2404;
        if(c.r==&xlle_aot_narrow_byte||c.r==&xlle_aot_narrow_word||c.r==&xlle_aot_immediate_store)
            initial.gpr[6]=c.address;
        m.bytes[c.address-0x1000]=0x81;
        if(c.width>1)m.bytes[c.address-0x1000+1]=0xfe;
        const auto bytes=m.bytes;auto actual=initial,expected=initial;
        expected.eip+=uint32_t(c.r->code_size);
        if(!c.write)expected.gpr[c.destination]=c.width==1?0x81:0xfe81;
        std::vector<uint8_t> floor_bytes;std::vector<Access> floor_access;
        const auto floor=oracle(initial,1,m,&floor_bytes,&floor_access);
        check(c.r->run(c.r,&actual,&m.bus,1)==XLLE_AOT_COMPLETE,"memory form completes");
        equal(actual,expected,0xffffffffu,"memory independent state");
        equal(actual,floor,0xffffffffu,"memory floor state");
        check(m.bytes==floor_bytes&&m.accesses==floor_access,"exact-width memory effects and order");
        check(m.accesses.size()==1&&m.accesses[0]==Access{c.address,c.width,c.write},"no widened access");
        m.reject=true;m.bytes=bytes;m.accesses.clear();actual=initial;
        check(c.r->run(c.r,&actual,&m.bus,1)==XLLE_AOT_MEMORY,"declined memory explicit exit");
        equal(actual,initial,0xffffffffu,"memory decline has no state effects");
        check(actual.retired==0&&m.bytes==bytes&&m.accesses.empty(),"decline has no memory effects");
        if(c.write) {
            m.reject=false;m.event_after_write=true;actual=initial;
            check(c.r->run(c.r,&actual,&m.bus,2)==XLLE_AOT_EVENT,"store exposes pending event");
            equal(actual,expected,0xffffffffu,"event follows completed immediate store");
            check(actual.retired==1&&m.bytes==floor_bytes&&m.accesses==floor_access,
                  "event does not duplicate or undo the store");
        }
    }
    Memory m(xlle_aot_narrow_word);auto s=seed(xlle_aot_narrow_word);s.gpr[6]=0x4fff;const auto before=s;
    check(xlle_aot_narrow_word.run(&xlle_aot_narrow_word,&s,&m.bus,1)==XLLE_AOT_MEMORY,"word needs both bytes");
    equal(s,before,0xffffffffu,"partial-span word preserves state");
    uint32_t value=0xdeadbeef;
    check(!xlle_aot_read_narrow(&m.bus,0x2000,4,&value)&&value==0xdeadbeef,"narrow ABI rejects width four");
    const auto& smc=xlle_aot_immediate_smc;Memory code(smc);s=seed(smc);s.gpr[6]=0x1006;
    check(smc.run(&smc,&s,&code.bus,2)==XLLE_AOT_CODE_CHANGED,"immediate store invalidates before stale INC");
    check(s.retired==1&&s.eip==0x1006&&s.gpr[0]==0x81234567,"SMC exact continuation");
}
static void cycle() {
    const auto& r=xlle_aot_scalar_cycle;Memory m(r);auto initial=seed(r);
    initial.gpr[6]=0x2200;initial.gpr[7]=0x2400;
    for(unsigned i=0;i<64;++i){m.bytes[0x1200+i]=uint8_t(0x80+i);m.bytes[0x1400+i]=uint8_t(0xf0-i);}
    for(unsigned count=1;count<=30;++count){auto s=initial;
        check(r.run(&r,&s,&m.bus,count)==XLLE_AOT_BUDGET&&s.retired==count,"scalar cycle exact boundary");
        equal(s,oracle(initial,count,m),~0x10u,"scalar cycle memory/flags across boundaries");}
}

static UnicornFloor& floor(XboxMachine& m) {
    auto* cpu=dynamic_cast<UnicornFloor*>(&m.cpu());check(cpu!=nullptr,"machine floor");return *cpu;
}
static void prepare(XboxMachine& m,const xlle_aot_region& r) {
    check(m.ok()&&m.init_ram()&&m.cpu().setup_flat_segments(0x8000),"machine RAM/segments");
    check(m.cpu().write_mem(r.base,r.code,r.code_size),"machine code");
    const uint8_t jump[]={0xe9,0xfb,0,0,0};
    check(m.cpu().write_mem(0xf00,jump,sizeof(jump)),"machine bootstrap");
    X86Regs initial{};initial.eip=0xf00;initial.esp=0x9000;initial.eflags=2;
    m.cpu().set_regs(initial);
}
static void run(XboxMachine& m,uint64_t count) {
    std::string error;const auto n=m.run(m.cpu().get_regs().eip,count,error);
    check(error.empty(),error.c_str());check(n==count,"machine exact instruction count");
}
static void machine_cycle() {
    XboxMachine native,reference;
    for(auto* m:{&native,&reference}) {
        prepare(*m,xlle_aot_scalar_cycle);
        auto initial=m->cpu().get_regs();initial.esi=0x2200;initial.edi=0x2400;
        initial.ecx=0x87654321;initial.eflags=0x00203cd7;m->cpu().set_regs(initial);
        uint8_t data[64];for(unsigned i=0;i<64;++i)data[i]=uint8_t(0x80+i);
        check(m->cpu().write_mem(0x2200,data,sizeof(data)),"byte sequence");
        for(unsigned i=0;i<64;++i)data[i]=uint8_t(0xf0-i);
        check(m->cpu().write_mem(0x2400,data,sizeof(data)),"word sequence");
        run(*m,31);
    }
    check(native.register_aot_region(xlle_aot_scalar_cycle),"scalar cycle registration");
    const auto before=native.aot_stats();const auto blocks=floor(native).block_stats();
    floor(native).require_native_execution(true);
    for(unsigned i=0;i<121;++i) {
        run(native,1);run(reference,1);
        equal(state(native.cpu().get_regs()),state(reference.cpu().get_regs()),~0x10u,
              "strict machine scalar boundary matches floor");
    }
    const auto after=native.aot_stats();const auto end=floor(native).block_stats();
    check(after.retired-before.retired==121&&after.fallback_insns==before.fallback_insns&&
          after.fallback_runs==before.fallback_runs,"scalar scope has no fallback");
    check(end.tb_entries==blocks.tb_entries&&end.insn_mode_runs==blocks.insn_mode_runs&&
          end.block_mode_runs==blocks.block_mode_runs,"scalar scope has no hidden guest execution");
    check(after.memory_reads-before.memory_reads==25,"strict scope includes exact narrow reads");
}
static void paged(XboxMachine& m,const xlle_aot_region& r,uint32_t second) {
    std::array<uint32_t,1024> pt{},pd{};
    for(unsigned i=0;i<1024;++i)pt[i]=i*4096+3;
    pt[1]=0x21003;pt[2]=0x22003;pt[3]=second?second|3:0;pd[0]=0x18003;
    check(m.cpu().write_mem(0x21000,r.code,r.code_size),"paged code");
    check(m.cpu().write_mem(0x18000,pt.data(),sizeof(pt))&&
          m.cpu().write_mem(0x19000,pd.data(),sizeof(pd)),"page structures");
    uint32_t cr3=0x19000,cr0=0;
    check(m.cpu().read_reg(XReg::CR0,&cr0,4),"read CR0");cr0|=0x80010001;
    check(m.cpu().write_reg(XReg::CR3,&cr3,4)&&m.cpu().write_reg(XReg::CR0,&cr0,4),"enable paging");
}
static void page_widths() {
    struct Case {unsigned width;uint32_t address,second;bool succeeds;};
    const Case cases[]={{1,0x2fff,0,true},{2,0x2ffe,0,true},{2,0x2fff,0,false},
        {2,0x2fff,0x23000,true},{2,0x2fff,0x26000,false},
        // Narrow success must not certify a four-byte read at the same address.
        {4,0x2fff,0,false}};
    for(const auto& c:cases) {
        XboxMachine m;const auto& r=c.width==1?xlle_aot_narrow_byte_loop:
            c.width==2?xlle_aot_narrow_word_loop:xlle_aot_narrow_then_wide;
        const auto configure=[&](XboxMachine& machine) {
            prepare(machine,r);paged(machine,r,c.second);
            const uint8_t lo[]={0x91,0xa5},hi=0x7c;
            check(machine.cpu().write_mem(0x22ffe,lo,2),"last two mapped bytes");
            if(c.second)check(machine.cpu().write_mem(c.second,&hi,1),"next page first byte");
            auto initial=machine.cpu().get_regs();initial.esi=c.width==4?0x2ff8:0x2ffe;
            machine.cpu().set_regs(initial);run(machine,c.width==4?4:3);
            // Warm both translations through real execution before testing the
            // conservative contiguous-span rule. No such warmup is possible
            // for the absent-page cases.
            if(c.second) {
                initial=machine.cpu().get_regs();initial.esi=0x2fff;
                machine.cpu().set_regs(initial);run(machine,2);
            }
            initial=machine.cpu().get_regs();initial.esi=c.address;initial.eax=0xdeadbeef;
            initial.ecx=0x81234567;machine.cpu().set_regs(initial);
        };
        configure(m);const auto initial=m.cpu().get_regs();
        check(m.register_aot_region(r),"narrow machine registration");
        floor(m).require_native_execution(true);
        const auto before=m.aot_stats();const auto blocks=floor(m).block_stats();
        uint32_t pte_before[2];check(m.cpu().read_mem(0x18008,pte_before,8),"PTEs before strict read");
        std::string error;const auto n=m.run(initial.eip,c.width==4?2:1,error);
        auto expected=state(initial);
        const bool narrow_success=c.succeeds||c.width==4;
        if(narrow_success) {
            expected.eip+=3;expected.gpr[0]=c.width==1||c.width==4?0xa5:
                c.address==0x2ffe?0xa591:0x7ca5;
        }
        check(n==(narrow_success?1u:0u),"page width exact retirement");
        if(c.succeeds)check(error.empty(),error.c_str());
        else check(error.find("native-required fallback refused")!=std::string::npos,
                   "unsafe span explicitly refused");
        equal(state(m.cpu().get_regs()),expected,0xffffffffu,"page boundary state");
        const auto after=m.aot_stats();const auto end=floor(m).block_stats();
        check(after.retired-before.retired==n&&after.memory_reads-before.memory_reads==n,
              "only completed accesses count");
        check(after.fallback_insns==before.fallback_insns&&after.fallback_runs==before.fallback_runs&&
              end.tb_entries==blocks.tb_entries&&end.insn_mode_runs==blocks.insn_mode_runs&&
              end.block_mode_runs==blocks.block_mode_runs,"page checks have no hidden execution");
        check(c.succeeds||after.memory_declines>before.memory_declines,"unsafe read declined");
        uint32_t pte_after[2];check(m.cpu().read_mem(0x18008,pte_after,8),"PTEs after strict read");
        check(std::equal(pte_before,pte_before+2,pte_after),"strict certification never fills page tables");
        if(!c.succeeds) {
            // A strict refusal is intentionally sticky. Validate the floor on
            // a separate equally initialized machine; never clear a loud stop.
            XboxMachine reference;configure(reference);error.clear();
            const auto count=reference.run(initial.eip,c.width==4?2:1,error);
            if(c.second) {
                check(count==1&&error.empty()&&reference.cpu().get_regs().eax==0x7ca5,
                      "noncontiguous mapping executes correctly through explicit floor");
            } else {
                check(count==n&&error.find("exception vector 14")!=std::string::npos&&
                      reference.cpu().get_regs().eip==expected.eip,"real floor reports precise missing-page fault");
                equal(state(reference.cpu().get_regs()),expected,0xffffffffu,"fault preserves unfinished load");
            }
        }
    }
}

static void memory_integer_machine() {
    XboxMachine native,reference;
    for(auto* m:{&native,&reference}) {
        prepare(*m,xlle_aot_alu_read_cycle);
        auto initial=m->cpu().get_regs();initial.esi=0x2200;initial.eax=0x7fffffff;
        initial.ecx=0x87654321;initial.eflags=0x00203cd7;m->cpu().set_regs(initial);
        uint32_t value=0x80000001;check(m->cpu().write_mem(0x2200,&value,4),"ALU machine operand");
        run(*m,29); // bootstrap and two complete fourteen-instruction cycles
    }
    check(native.register_aot_region(xlle_aot_alu_read_cycle),"ALU cycle registration");
    const auto before=native.aot_stats();const auto blocks=floor(native).block_stats();
    floor(native).require_native_execution(true);
    uint32_t defined=0xffffffffu;
    for(unsigned i=0;i<140;++i) {
        const uint32_t offset=reference.cpu().get_regs().eip-xlle_aot_alu_read_cycle.base;
        // Undefined AF remains unqualified through flag-preserving operations;
        // arithmetic/comparison defines it again. All other bits always agree.
        if(offset==4||offset==6||offset==8||offset==14||offset==25)defined&=~0x10u;
        else if(offset!=33&&offset!=35)defined=0xffffffffu;
        run(native,1);run(reference,1);
        equal(state(native.cpu().get_regs()),state(reference.cpu().get_regs()),defined,
              "strict ALU machine boundary equals floor");
    }
    const auto after=native.aot_stats();const auto end=floor(native).block_stats();
    check(after.retired-before.retired==140&&after.memory_reads-before.memory_reads==110,
          "strict ALU scope includes every executed instruction and read");
    check(after.fallback_insns==before.fallback_insns&&after.fallback_runs==before.fallback_runs&&
          end.tb_entries==blocks.tb_entries&&end.insn_mode_runs==blocks.insn_mode_runs&&
          end.block_mode_runs==blocks.block_mode_runs,"strict ALU has no hidden guest execution");
}
static void memory_integer_pages() {
    struct Case {uint32_t address,second;bool cold,readonly,success;};
    const Case cases[]={{0x2ffc,0,false,false,true},{0x2ffc,0,false,true,true},
        {0x2ffd,0,false,false,false},{0x2ffd,0x23000,false,false,true},
        {0x2ffd,0x26000,false,false,false},{0x2ffc,0,true,false,false}};
    for(bool leave:{false,true})for(const auto& c:cases) {
        const auto& r=leave?xlle_aot_leave_cycle:xlle_aot_read_cmp_loop;
        const auto configure=[&](XboxMachine& m) {
            prepare(m,r);paged(m,r,c.second);
            if(c.readonly) {
                uint32_t pte=0x22001;check(m.cpu().write_mem(0x18008,&pte,4),"read-only data PTE");
            }
            const uint8_t lo[]={0x11,0x22,0x33,0x44},hi=0x55;
            check(m.cpu().write_mem(0x22ffc,lo,4),"ALU page-end operand");
            if(c.second)check(m.cpu().write_mem(c.second,&hi,1),"ALU split operand");
            auto s=m.cpu().get_regs();s.eip=r.base+(leave?1:2);s.esi=s.ebp=0x2ffc;m.cpu().set_regs(s);
            run(m,1); // warm code through its real JMP; data remains cold
            if(!c.cold) {
                s=m.cpu().get_regs();s.esi=s.ebp=c.second?0x2ffd:0x2ffc;m.cpu().set_regs(s);run(m,2);
            }
            s=m.cpu().get_regs();s.eip=r.base;s.esi=s.ebp=c.address;s.esp=0x9000;s.eax=0x12345678;s.eflags=0xed7;
            m.cpu().set_regs(s);
        };
        XboxMachine m;configure(m);const auto initial=m.cpu().get_regs();
        check(m.register_aot_region(r),"paged ALU registration");floor(m).require_native_execution(true);
        const auto before=m.aot_stats();const auto blocks=floor(m).block_stats();
        uint32_t ptes[2];check(m.cpu().read_mem(0x18008,ptes,8),"ALU PTEs before");
        std::string error;const auto n=m.run(initial.eip,1,error);
        check(n==(c.success?1u:0u),"paged ALU retirement");
        auto expected=state(initial);
        const auto completed=[&](uint32_t value) {
            if(leave){expected.gpr[4]=c.address+4;expected.gpr[5]=value;}
            else arithmetic_expected(5,initial.eax,value,expected.eflags);
            expected.eip+=leave?1:2;
        };
        if(c.success) {
            completed(c.address==0x2ffc?0x44332211:0x55443322);
            check(error.empty(),error.c_str());
        } else check(error.find("native-required fallback refused")!=std::string::npos,"paged ALU loud decline");
        equal(state(m.cpu().get_regs()),expected,0xffffffffu,"paged ALU precise state");
        const auto after=m.aot_stats();const auto end=floor(m).block_stats();
        check(after.retired-before.retired==n&&after.memory_reads-before.memory_reads==n,
              "paged ALU counts only completed reads");
        check(after.fallback_insns==before.fallback_insns&&after.fallback_runs==before.fallback_runs&&
              end.tb_entries==blocks.tb_entries&&end.insn_mode_runs==blocks.insn_mode_runs&&
              end.block_mode_runs==blocks.block_mode_runs,"paged ALU has no hidden execution");
        uint32_t ptes_after[2];check(m.cpu().read_mem(0x18008,ptes_after,8),"ALU PTEs after");
        check(std::equal(ptes,ptes+2,ptes_after),"ALU certification never walks page tables");
        if(!c.success) {
            XboxMachine reference;configure(reference);error.clear();
            const auto count=reference.run(initial.eip,1,error);
            if(c.second||c.cold) {
                check(count==1&&error.empty(),"declined cold/noncontiguous read is real floor work");
                completed(c.second?0x55443322:0x44332211);
            } else check(count==0&&error.find("exception vector 14")!=std::string::npos,
                         "missing operand page produces a precise floor fault");
            equal(state(reference.cpu().get_regs()),expected,0xffffffffu,"floor after strict read refusal");
        }
    }
}
static void leave_high_stack() {
    XboxMachine native,reference;const auto& r=xlle_aot_leave_cycle;
    for(auto* m:{&native,&reference}) {
        prepare(*m,r);paged(*m,r,0);
        uint32_t pde=0x1a003,pte=0x23003;
        check(m->cpu().write_mem(0x19000+1023*4,&pde,4)&&
              m->cpu().write_mem(0x1a000+1023*4,&pte,4),"high stack translation");
        const uint32_t saved=0x1234dead;check(m->cpu().write_mem(0x23ffc,&saved,4),"high saved frame");
        auto s=m->cpu().get_regs();s.eip=r.base;s.ebp=0xfffffffc;s.esp=0x9000;
        m->cpu().set_regs(s);run(*m,2); // LEAVE/JMP warm both real translations
        s=m->cpu().get_regs();s.eip=r.base;s.ebp=0xfffffffc;s.esp=0x9000;s.eflags=0xed7;
        m->cpu().set_regs(s);
    }
    check(native.register_aot_region(r),"high LEAVE registration");floor(native).require_native_execution(true);
    const auto before=native.aot_stats();const auto blocks=floor(native).block_stats();
    run(native,1);run(reference,1);
    const auto result=native.cpu().get_regs();
    equal(state(result),state(reference.cpu().get_regs()),0xffffffffu,"high LEAVE equals floor");
    check(result.esp==0&&result.ebp==0x1234dead&&result.eflags==0xed7,"LEAVE wraps only its final stack pointer");
    const auto after=native.aot_stats();const auto end=floor(native).block_stats();
    check(after.retired-before.retired==1&&after.memory_reads-before.memory_reads==1&&
          after.fallback_insns==before.fallback_insns&&after.fallback_runs==before.fallback_runs&&
          end.tb_entries==blocks.tb_entries&&end.insn_mode_runs==blocks.insn_mode_runs&&
          end.block_mode_runs==blocks.block_mode_runs,"high LEAVE is strictly native");
}
int main(){shifts();conditions();extensions();memory_forms();cycle();machine_cycle();page_widths();
    unary_integer();read_integer();epilogue_and_read_exits();memory_integer_machine();memory_integer_pages();leave_high_stack();
    std::puts("scalar AOT: independent bits/conditions, exact widths, faults and strict machine scope pass");}
