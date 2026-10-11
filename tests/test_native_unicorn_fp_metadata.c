/* Authored legacy protected-mode images and actual P6 x87 instructions. */
#include <unicorn/unicorn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define UC(call) do { uc_err error=(call);if(error){fprintf(stderr,"engine: %s\n",uc_strerror(error));exit(2);} } while(0)
static unsigned failures,checks;
#define CHECK(c) do {++checks;if(!(c)){++failures;fprintf(stderr,"FAIL metadata line%d: %s\n",__LINE__,#c);}} while(0)
static void put16(unsigned char *p,uint16_t x){p[0]=(unsigned char)x;p[1]=(unsigned char)(x>>8);}
static void put32(unsigned char *p,uint32_t x){for(unsigned i=0;i<4;++i)p[i]=(unsigned char)(x>>(i*8));}
static uint16_t get16(const unsigned char *p){return p[0]|(uint16_t)p[1]<<8;}
static uint32_t get32(const unsigned char *p){return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void execute(uc_engine *cpu,uint32_t pc,const unsigned char *bytes,unsigned size)
{UC(uc_mem_write(cpu,pc,bytes,size));UC(uc_emu_start(cpu,pc,pc+size,0,32));}
static void save(uc_engine *cpu,unsigned char image[512])
{const unsigned char code[]={0x0f,0xae,0x05,0,0x30,0,0};execute(cpu,0x1800,code,sizeof(code));UC(uc_mem_read(cpu,0x3000,image,512));}
static void restore(uc_engine *cpu,unsigned lifetime)
{
    unsigned char image[512]={0};put16(image,0x037f);put16(image+6,0x123+lifetime);
    put32(image+8,0x12345000+lifetime*0x100);put16(image+12,8);
    put32(image+16,0x23456000+lifetime*0x100);put16(image+20,16);put32(image+24,0x1f80);
    const unsigned char code[]={0x0f,0xae,0x0d,0,0x20,0,0};UC(uc_mem_write(cpu,0x2000,image,512));execute(cpu,0x1800,code,sizeof(code));
}
int main(void)
{
    for(unsigned lifetime=0;lifetime<3;++lifetime){
        uc_engine *cpu=NULL;UC(uc_open(UC_ARCH_X86,UC_MODE_32,&cpu));UC(uc_ctl_set_cpu_model(cpu,UC_CPU_X86_PENTIUM3));
        UC(uc_mem_map(cpu,0x1000,0x5000,UC_PROT_ALL));
        const uint64_t gdt[]={0,0x00cf9b000000ffffull,0x00cf93000000ffffull,0x00cf93004000ffffull};
        UC(uc_mem_write(cpu,0x5000,gdt,sizeof(gdt)));uc_x86_mmr gdtr={0};gdtr.base=0x5000;gdtr.limit=sizeof(gdt)-1;UC(uc_reg_write(cpu,UC_X86_REG_GDTR,&gdtr));
        uint32_t selector=8;UC(uc_reg_write(cpu,UC_X86_REG_CS,&selector));selector=16;UC(uc_reg_write(cpu,UC_X86_REG_DS,&selector));selector=24;UC(uc_reg_write(cpu,UC_X86_REG_FS,&selector));
        const double value=2.0;UC(uc_mem_write(cpu,0x2100,&value,8));UC(uc_mem_write(cpu,0x4100,&value,8));
        unsigned char saved[512];restore(cpu,lifetime);
        const unsigned char fld[]={0xdd,0x05,0,0x21,0,0};execute(cpu,0x1000,fld,sizeof(fld));save(cpu,saved);
        CHECK(get16(saved+6)==0x505);CHECK(get32(saved+8)==0x1000 && get16(saved+12)==8);CHECK(get32(saved+16)==0x2100 && get16(saved+20)==16);
        const unsigned char add[]={0xd8,0xc0};execute(cpu,0x1020,add,sizeof(add));save(cpu,saved);
        CHECK(get16(saved+6)==0xc0 && get32(saved+8)==0x1020);
        const unsigned char fldfs[]={0x64,0xdd,0x05,0,1,0,0};execute(cpu,0x1040,fldfs,sizeof(fldfs));save(cpu,saved);
        CHECK(get16(saved+6)==0x505 && get32(saved+8)==0x1040);CHECK(get32(saved+16)==0x100 && get16(saved+20)==24);
        const unsigned char controls[]={0xd9,0x2d,0,0x20,0,0,0xdb,0xe2,0x9b};execute(cpu,0x1080,controls,sizeof(controls));save(cpu,saved);
        CHECK(get16(saved+6)==0x505 && get32(saved+8)==0x1040 && get32(saved+16)==0x100);
        restore(cpu,lifetime);const unsigned char init[]={0xdb,0xe3};execute(cpu,0x1080,init,sizeof(init));save(cpu,saved);
        CHECK(get16(saved)==0x37f && !saved[4]);CHECK(!get16(saved+6) && !get32(saved+8) && !get16(saved+12) && !get32(saved+16) && !get16(saved+20));
        unsigned char env[28]={0};put16(env,0x076f);put16(env+8,0xffff);put32(env+12,0x11112222+lifetime);put16(env+16,0x28);put16(env+18,0x321+lifetime);put32(env+20,0x33334444+lifetime);put16(env+24,0x30);
        UC(uc_mem_write(cpu,0x2200,env,sizeof(env)));const unsigned char load[]={0xd9,0x25,0,0x22,0,0};execute(cpu,0x1100,load,sizeof(load));save(cpu,saved);
        CHECK(get16(saved)==0x076f && !saved[4]);CHECK(get16(saved+6)==0x321+lifetime && get32(saved+8)==0x11112222+lifetime && get16(saved+12)==0x28);CHECK(get32(saved+16)==0x33334444+lifetime && get16(saved+20)==0x30);
        const unsigned char store[]={0xd9,0x35,0,0x23,0,0};execute(cpu,0x1120,store,sizeof(store));unsigned char output[28];UC(uc_mem_read(cpu,0x2300,output,28));
        CHECK(get32(output+12)==0x11112222+lifetime && get16(output+16)==0x28 && get16(output+18)==0x321+lifetime);CHECK(get32(output+20)==0x33334444+lifetime && get16(output+24)==0x30);
        save(cpu,saved);CHECK(get16(output)==0x076f && get16(saved)==0x077f);
        const unsigned char fsave[]={0xdd,0x35,0,0x24,0,0};execute(cpu,0x1140,fsave,sizeof(fsave));save(cpu,saved);CHECK(!get16(saved+6) && !get32(saved+8) && !get32(saved+16));
        const unsigned char frstor[]={0xdd,0x25,0,0x24,0,0};execute(cpu,0x1160,frstor,sizeof(frstor));save(cpu,saved);
        CHECK(get16(saved+6)==0x321+lifetime && get32(saved+8)==0x11112222+lifetime && get32(saved+16)==0x33334444+lifetime);
        const unsigned char sse2[]={0x66,0x0f,0x6f,0xc0};UC(uc_mem_write(cpu,0x1180,sse2,sizeof(sse2)));
        CHECK(uc_emu_start(cpu,0x1180,0x1184,0,1)==UC_ERR_INSN_INVALID);
        UC(uc_close(cpu));
    }
    printf("%s: P6 original opcodes, prefixed instruction/data ownership, control exclusion, init and protected-mode environment/save/restore; %u checks,%u failures across three engines\n",failures?"FAIL":"PASS",checks,failures);return failures?1:0;
}
