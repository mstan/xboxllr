/* Maintained original-code calendar implementation for the private LLE build.
 * No instruction/callee hooks. The same original kernel bodies used by the
 * independent fixture execute for real title calls, not only in a test.
 * These pure routines observe buffer values/overlap, not their virtual address.
 * Scratch relocation preserves alignment and every possible buffer overlap.
 * System clock generation, interrupts and scheduling are separate services. */
#include <stdint.h>
#include <string.h>
#include <unicorn/unicorn.h>
#include "calendar_lle.h"
#include "calendar_kernel.h"

static uint32_t calendar_word(unsigned offset)
{
    uint32_t value;memcpy(&value,pilot_calendar_kernel+offset,4);return value;
}

const char *pilot_calendar_lle(unsigned ordinal,unsigned char *guest,
    uint32_t source,uint32_t destination,uint32_t *result)
{
    if(ordinal!=304 && ordinal!=305)return "unsupported LLE calendar ordinal";
    unsigned input_size=ordinal==304?16:8,output_size=ordinal==304?8:16;
    uint32_t pe=calendar_word(0x3c),exports=calendar_word(pe+120);
    uint32_t table=calendar_word(exports+28),base=calendar_word(exports+16);
    uint32_t entry=0x80010000u+calendar_word(table+4*(ordinal-base));
    uint32_t in=0x01001000u+(source&4095),out=0x01004000u+(destination&4095);
    if((uint64_t)source+input_size>destination && (uint64_t)destination+output_size>source)
        out=(uint32_t)((int64_t)in+(int64_t)destination-source);
    uint32_t stack=0x03008000u,end=0x03009000u,words[]={end,in,out};
    uint32_t registers[]={0x12345678,0x23456789,0x3456789a,0x456789ab};
    int saved[]={UC_X86_REG_EBX,UC_X86_REG_ESI,UC_X86_REG_EDI,UC_X86_REG_EBP};
    uc_engine *cpu=NULL;uc_err error=uc_open(UC_ARCH_X86,UC_MODE_32,&cpu);
    if(error)return uc_strerror(error);
    const char *failure=NULL;
    unsigned char output[16];uint32_t observed=0;
#define CALENDAR_UC(call) do {error=(call);if(error){failure=uc_strerror(error);goto done;}} while(0)
    CALENDAR_UC(uc_mem_map(cpu,0x80010000u,0x70000,UC_PROT_ALL));
    CALENDAR_UC(uc_mem_write(cpu,0x80010000u,pilot_calendar_kernel,sizeof(pilot_calendar_kernel)));
    CALENDAR_UC(uc_mem_protect(cpu,0x80010000u,0x70000,UC_PROT_READ|UC_PROT_EXEC));
    CALENDAR_UC(uc_mem_map(cpu,0x01000000u,0x8000,UC_PROT_READ|UC_PROT_WRITE));
    CALENDAR_UC(uc_mem_map(cpu,0x03000000u,0x10000,UC_PROT_ALL));
    CALENDAR_UC(uc_mem_write(cpu,in,guest+source,input_size));
    CALENDAR_UC(uc_mem_write(cpu,out,guest+destination,output_size));
    CALENDAR_UC(uc_mem_write(cpu,stack,words,sizeof(words)));
    CALENDAR_UC(uc_reg_write(cpu,UC_X86_REG_ESP,&stack));
    for(unsigned i=0;i<4;++i)CALENDAR_UC(uc_reg_write(cpu,saved[i],registers+i));
    /* Bound a defect visibly; the instruction limit never fabricates return. */
    CALENDAR_UC(uc_emu_start(cpu,entry,end,0,100000));
    CALENDAR_UC(uc_reg_read(cpu,UC_X86_REG_EIP,&observed));
    if(observed!=end){failure="LLE calendar did not return";goto done;}
    CALENDAR_UC(uc_reg_read(cpu,UC_X86_REG_ESP,&observed));
    if(observed!=stack+12){failure="LLE calendar stack imbalance";goto done;}
    for(unsigned i=0;i<4;++i) {
        CALENDAR_UC(uc_reg_read(cpu,saved[i],&observed));
        if(observed!=registers[i]){failure="LLE calendar preserved register changed";goto done;}
    }
    CALENDAR_UC(uc_mem_read(cpu,out,output,output_size));
    CALENDAR_UC(uc_reg_read(cpu,UC_X86_REG_EAX,&observed));
done:
    error=uc_close(cpu);
    if(!failure && error)failure=uc_strerror(error);
    if(!failure) {
        memcpy(guest+destination,output,output_size);
        if(ordinal==304)*result=observed&255; /* Original BOOLEAN return is AL. */
    }
    return failure;
#undef CALENDAR_UC
}
