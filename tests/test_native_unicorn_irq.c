/* Authored instructions test the compiled engine's actual hidden state. */
#include <unicorn/unicorn.h>
#include "unicorn_irq_boundary.h"
#include "x86_integer_flags.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define REQUIRE(c) do{if(!(c)){fprintf(stderr,"FAIL IRQ boundary line%d: %s\n",__LINE__,#c);exit(1);}}while(0)
#define UC(c) REQUIRE((c)==UC_ERR_OK)
static const int registers[]={UC_X86_REG_EAX,UC_X86_REG_ECX,UC_X86_REG_EDX,UC_X86_REG_EBX,
                             UC_X86_REG_ESP,UC_X86_REG_EBP,UC_X86_REG_ESI,UC_X86_REG_EDI};
struct Window {uint32_t pc;unsigned shadowed,eligible;};
static void irq_window(uc_engine *cpu,uint64_t address,uint32_t size,void *data)
{
    (void)size;
    struct Window *window=data;
    uint32_t flags;UC(uc_reg_read(cpu,UC_X86_REG_EFLAGS,&flags));
    if(!(flags&0x200u))return;
    if(xlle_unicorn_irq_inhibited(cpu)){++window->shadowed;return;}
    ++window->eligible;window->pc=(uint32_t)address;UC(uc_emu_stop(cpu));
}
static uc_engine *prepare(void)
{
    uc_engine *cpu=NULL;UC(uc_open(UC_ARCH_X86,UC_MODE_32,&cpu));
    UC(uc_mem_map(cpu,0x1000,4096,UC_PROT_ALL));
    /* CLI; STI; INC EAX; NOP; JMP self. */
    const uint8_t code[]={0xfa,0xfb,0x40,0x90,0xeb,0xfe};
    UC(uc_mem_write(cpu,0x1000,code,sizeof(code)));
    for(unsigned i=0;i<8;++i){uint32_t value=0x12345000u+i;UC(uc_reg_write(cpu,registers[i],&value));}
    uint32_t flags=2;UC(uc_reg_write(cpu,UC_X86_REG_EFLAGS,&flags));
    REQUIRE(!xlle_unicorn_irq_inhibited(cpu));return cpu;
}
static void check_registers(uc_engine *cpu,unsigned increments)
{
    for(unsigned i=0;i<8;++i){uint32_t value;UC(uc_reg_read(cpu,registers[i],&value));
        REQUIRE(value==0x12345000u+i+(i==0?increments:0));}
}
static void block_window(void)
{
    uc_engine *cpu=prepare();uc_hook hook;struct Window window={0};
    UC(uc_hook_add(cpu,&hook,UC_HOOK_BLOCK,irq_window,&window,1,0));
    UC(uc_emu_start(cpu,0x1000,0x2000,0,10));
    REQUIRE(window.shadowed==1 && window.eligible==1 && window.pc==0x1003);
    uint32_t pc;UC(uc_reg_read(cpu,UC_X86_REG_EIP,&pc));REQUIRE(pc==0x1003);
    REQUIRE(!xlle_unicorn_irq_inhibited(cpu));check_registers(cpu,1);UC(uc_close(cpu));
}
static void split_and_context(void)
{
    uc_engine *cpu=prepare();
    UC(uc_emu_start(cpu,0x1001,0x2000,0,1));
    uint32_t pc,flags;UC(uc_reg_read(cpu,UC_X86_REG_EIP,&pc));UC(uc_reg_read(cpu,UC_X86_REG_EFLAGS,&flags));
    REQUIRE(pc==0x1002 && (flags&0x200u) && xlle_unicorn_irq_inhibited(cpu));check_registers(cpu,0);
    uc_context *context=NULL;UC(uc_context_alloc(cpu,&context));UC(uc_context_save(cpu,context));
    UC(uc_emu_start(cpu,pc,0x2000,0,1));
    UC(uc_reg_read(cpu,UC_X86_REG_EIP,&pc));REQUIRE(pc==0x1003 && !xlle_unicorn_irq_inhibited(cpu));
    check_registers(cpu,1);
    UC(uc_context_restore(cpu,context));
    UC(uc_reg_read(cpu,UC_X86_REG_EIP,&pc));REQUIRE(pc==0x1002 && xlle_unicorn_irq_inhibited(cpu));
    check_registers(cpu,0);UC(uc_emu_start(cpu,pc,0x2000,0,1));
    REQUIRE(!xlle_unicorn_irq_inhibited(cpu));check_registers(cpu,1);
    UC(uc_context_free(context));UC(uc_close(cpu));
}
static void cmp_flags(void)
{
    uc_engine *cpu=NULL;UC(uc_open(UC_ARCH_X86,UC_MODE_32,&cpu));
    UC(uc_mem_map(cpu,0x1000,4096,UC_PROT_ALL));
    const uint8_t code[]={0x39,0xd0}; /* Actual CMP EAX,EDX. */
    UC(uc_mem_write(cpu,0x1000,code,sizeof(code)));
    const uint32_t values[]={0,1,7,15,16,127,128,255,256,0x7fffffff,0x80000000,0xfffffffe,0xffffffff};
    for(unsigned df=0;df<2;++df)for(unsigned a=0;a<13;++a)for(unsigned b=0;b<13;++b) {
        uint32_t previous=0x202u|(df<<10)|0x8d5u,pc,flags;
        UC(uc_reg_write(cpu,UC_X86_REG_EAX,values+a));UC(uc_reg_write(cpu,UC_X86_REG_EDX,values+b));
        UC(uc_reg_write(cpu,UC_X86_REG_EFLAGS,&previous));
        UC(uc_emu_start(cpu,0x1000,0x1002,0,1));
        UC(uc_reg_read(cpu,UC_X86_REG_EFLAGS,&flags));UC(uc_reg_read(cpu,UC_X86_REG_EIP,&pc));
        REQUIRE(pc==0x1002 && flags==xlle_x86_cmp32_flags(values[a],values[b],previous));
    }
    UC(uc_close(cpu));
}
int main(void)
{
    REQUIRE(xlle_unicorn_irq_inhibited(NULL));
    for(unsigned lifetime=0;lifetime<4;++lifetime){block_window();split_and_context();}
    cmp_flags();
    puts("PASS: true STI following-instruction inhibition, block boundary, split slices, saved context and GPR canaries over eight engine lifetimes");
    puts("PASS: 338 actual CMP instruction boundaries independently check six arithmetic flags and retained IF/DF");
    return 0;
}
