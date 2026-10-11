/* Authored state images and actual FXRSTOR/FXSAVE instructions. */
#include <unicorn/unicorn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define REQUIRE(c) do { if(!(c)) { fprintf(stderr,"FAIL FX state line%d: %s\n",__LINE__,#c);exit(1); } } while(0)
#define UC(c) REQUIRE((c)==UC_ERR_OK)
static void store16(unsigned char *p,uint16_t value) {p[0]=(unsigned char)value;p[1]=(unsigned char)(value>>8);}
static void store32(unsigned char *p,uint32_t value) {for(unsigned i=0;i<4;++i)p[i]=(unsigned char)(value>>(8*i));}
static void store64(unsigned char *p,uint64_t value) {for(unsigned i=0;i<8;++i)p[i]=(unsigned char)(value>>(8*i));}
static uint16_t load16(const unsigned char *p) {return (uint16_t)(p[0]|p[1]<<8);}
static uint32_t load32(const unsigned char *p) {return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void image(unsigned char out[512],unsigned id)
{
    memset(out,0,512);store16(out,(uint16_t)(id&1?0x077f:0x027f));out[4]=0xff;
    store16(out+6,(uint16_t)((0x100+id*0x111)&0x7ff));
    store32(out+8,0x12345000+id*0x100);store16(out+12,(uint16_t)(8+id*8));
    store32(out+16,0x23456000+id*0x200);store16(out+20,(uint16_t)(16+id*8));
    store32(out+24,0x1f80+((id&3)<<13));
    for(unsigned i=0;i<8;++i) {
        store64(out+32+i*16,0x8000000000000000ull+(uint64_t)id*0x0100000000000000ull+(uint64_t)i*0x0010000000000000ull);
        store16(out+40+i*16,0x3fff);
        for(unsigned lane=0;lane<16;++lane)out[160+i*16+lane]=(unsigned char)(id*67+i*17+lane);
    }
}
static void check(uc_engine *cpu,const unsigned char expected[512])
{
    unsigned char saved[512],guard[16],source[512];
    UC(uc_mem_read(cpu,0x3000,saved,512));UC(uc_mem_read(cpu,0x2000,source,512));
    REQUIRE(!memcmp(saved,expected,5));REQUIRE(!memcmp(saved+6,expected+6,8));
    REQUIRE(!memcmp(saved+16,expected+16,6));REQUIRE(!memcmp(saved+24,expected+24,4));
    for(unsigned i=0;i<8;++i)REQUIRE(!memcmp(saved+32+i*16,expected+32+i*16,10));
    REQUIRE(!memcmp(saved+160,expected+160,128));
    UC(uc_mem_read(cpu,0x2ff0,guard,16));for(unsigned i=0;i<16;++i)REQUIRE(guard[i]==0xa5);
    UC(uc_mem_read(cpu,0x3200,guard,16));for(unsigned i=0;i<16;++i)REQUIRE(guard[i]==0xa5);
    uint64_t pointer=0;uint16_t selector=0,opcode=0;
    UC(uc_reg_read(cpu,UC_X86_REG_FIP,&pointer));REQUIRE(pointer==load32(expected+8));
    UC(uc_reg_read(cpu,UC_X86_REG_FDP,&pointer));REQUIRE(pointer==load32(expected+16));
    UC(uc_reg_read(cpu,UC_X86_REG_FCS,&selector));REQUIRE(selector==load16(expected+12));
    UC(uc_reg_read(cpu,UC_X86_REG_FDS,&selector));REQUIRE(selector==load16(expected+20));
    UC(uc_reg_read(cpu,UC_X86_REG_FOP,&opcode));REQUIRE(opcode==load16(expected+6));
}
int main(void)
{
    for(unsigned lifetime=0;lifetime<4;++lifetime) {
        uc_engine *cpu=NULL;UC(uc_open(UC_ARCH_X86,UC_MODE_32,&cpu));
        UC(uc_mem_map(cpu,0x1000,0x4000,UC_PROT_ALL));
        const unsigned char code[]={0x0f,0xae,0x0d,0,0x20,0,0,0x0f,0xae,0x05,0,0x30,0,0};
        UC(uc_mem_write(cpu,0x1000,code,sizeof(code)));
        unsigned char first[512],second[512],guard[544],source[512];
        image(first,lifetime);image(second,lifetime+4);memset(guard,0xa5,sizeof(guard));
        UC(uc_mem_write(cpu,0x2ff0,guard,sizeof(guard)));UC(uc_mem_write(cpu,0x2000,first,512));
        UC(uc_emu_start(cpu,0x1000,0x100e,0,2));check(cpu,first);
        UC(uc_mem_read(cpu,0x2000,source,512));REQUIRE(!memcmp(first,source,512));
        uc_context *context=NULL;UC(uc_context_alloc(cpu,&context));UC(uc_context_save(cpu,context));
        UC(uc_mem_write(cpu,0x2000,second,512));UC(uc_emu_start(cpu,0x1000,0x100e,0,2));check(cpu,second);
        UC(uc_context_restore(cpu,context));UC(uc_emu_start(cpu,0x1007,0x100e,0,1));check(cpu,first);
        UC(uc_mem_read(cpu,0x2000,source,512));REQUIRE(!memcmp(second,source,512));
        UC(uc_context_free(context));UC(uc_close(cpu));
    }
    puts("PASS: eight independent 32-bit FXRSTOR/FXSAVE images and four saved contexts retain FOP/FIP/FCS/FDP/FDS, controls and all x87/XMM values; input and output guards unchanged");
    return 0;
}
