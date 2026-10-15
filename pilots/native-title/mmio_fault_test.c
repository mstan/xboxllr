/* Decoder-only fixture: the probe records bus transactions, not USB results. */
#include "mmio_fault.h"
#include <stdio.h>
#include <string.h>
#define CHECK(c) do{if(!(c)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#c);return 1;}}while(0)
static unsigned transactions;
static uint32_t last_address,last_value;
static int last_write;
static unsigned last_size;
static int reject_transaction;
static const char *probe(uint32_t address,int write,uint32_t *value)
{
    ++transactions;last_address=address;last_write=write;last_value=*value;
    if(!write)*value=0xf0123456u;
    return NULL;
}
static const char *probe_sized(uint32_t address,unsigned size,int write,uint32_t *value)
{last_size=size;probe(address,write,value);return reject_transaction?"device rejected transaction":NULL;}
int main(void)
{
    unsigned char code[15]={0x44,0x8b,0x53,0xf8};
    CONTEXT context={0};EXCEPTION_RECORD record={0};
    EXCEPTION_POINTERS exception={&record,&context};const char *error=NULL;
    unsigned char *guest=(unsigned char*)0x100000000ull;
    uint64_t address=(uintptr_t)guest+0xfed00000ull;
    record.ExceptionCode=EXCEPTION_ACCESS_VIOLATION;record.NumberParameters=2;
    record.ExceptionInformation[1]=address;
    context.Rip=(uintptr_t)code;context.Rbx=address+8;context.EFlags=0x246;
    context.R10=UINT64_MAX;
    CHECK(pilot_mmio_fault(&exception,guest,probe,&error)==1);
    CHECK(context.R10==0xf0123456u && context.Rip==(uintptr_t)code+4);
    CHECK(context.EFlags==0x246 && transactions==1 && !last_write && last_address==0xfed00000u);
    const unsigned char store[]={0x45,0x89,0x94,0x8b,0x10,0,0,0};
    memcpy(code,store,sizeof(store));context.Rip=(uintptr_t)code;
    context.R11=address-24;context.Rcx=2;context.R10=0xaabbccdd;
    record.ExceptionInformation[0]=1;
    CHECK(pilot_mmio_fault(&exception,guest,probe,&error)==1);
    CHECK(context.Rip==(uintptr_t)code+8 && transactions==2 && last_write && last_value==0xaabbccdd);
    const unsigned char immediate[]={0x41,0xc7,0x44,0x24,4,0x78,0x56,0x34,0x12};
    memcpy(code,immediate,sizeof(immediate));context.Rip=(uintptr_t)code;context.R12=address-4;
    CHECK(pilot_mmio_fault(&exception,guest,probe,&error)==1);
    CHECK(context.Rip==(uintptr_t)code+9 && transactions==3 && last_value==0x12345678);
    const unsigned char relative[]={0xc7,5,0,0,0,0,0x21,0x43,0x65,0x87};
    memcpy(code,relative,sizeof(relative));context.Rip=(uintptr_t)code;
    record.ExceptionInformation[1]=(uintptr_t)code+10;
    guest=(unsigned char*)((uintptr_t)code+10-0xfed00000ull);
    CHECK(pilot_mmio_fault(&exception,guest,probe,&error)==1);
    CHECK(context.Rip==(uintptr_t)code+10 && transactions==4 && last_value==0x87654321);
    /* Unsupported instruction, wrong exception direction/address, and REX.W
     * must leave the context and device untouched. */
    code[0]=0x90;context.Rip=(uintptr_t)code;
    CHECK(pilot_mmio_fault(&exception,guest,probe,&error)==0);
    memcpy(code,relative,sizeof(relative));record.ExceptionInformation[0]=0;
    CHECK(pilot_mmio_fault(&exception,guest,probe,&error)==0);
    record.ExceptionInformation[0]=1;++record.ExceptionInformation[1];
    CHECK(pilot_mmio_fault(&exception,guest,probe,&error)==0);
    code[0]=0x48;code[1]=0x89;code[2]=0x18;
    CHECK(pilot_mmio_fault(&exception,guest,probe,&error)==0);
    CHECK(transactions==4 && context.Rip==(uintptr_t)code && context.EFlags==0x246);
    /* NV2A's original vblank code preserves the VGA byte index register.
     * Exercise the actual x64 encodings, including legacy AH and REX SIL. */
    guest=(unsigned char*)0x100000000ull;address=(uintptr_t)guest+0xfd6013d4ull;
    record.ExceptionInformation[1]=address;record.ExceptionInformation[0]=0;
    context.Rbx=address;context.Rax=UINT64_MAX;
    const unsigned char byte_load[]={0x0f,0xb6,0x03};
    memcpy(code,byte_load,sizeof(byte_load));context.Rip=(uintptr_t)code;
    CHECK(pilot_mmio_fault_range(&exception,guest,0xfd000000,0xfdffffff,probe_sized,&error)==1);
    CHECK(context.Rax==0x56 && last_size==1 && context.Rip==(uintptr_t)code+3);
    code[0]=0x8a;code[1]=0x23;context.Rip=(uintptr_t)code;context.Rax=0x123456789abcdef0ull;
    CHECK(pilot_mmio_fault_range(&exception,guest,0xfd000000,0xfdffffff,probe_sized,&error)==1);
    CHECK(context.Rax==0x123456789abc56f0ull);
    code[0]=0x40;code[1]=0x88;code[2]=0x33;context.Rip=(uintptr_t)code;
    context.Rsi=0xaabbccdd;record.ExceptionInformation[0]=1;
    CHECK(pilot_mmio_fault_range(&exception,guest,0xfd000000,0xfdffffff,probe_sized,&error)==1);
    CHECK(last_size==1 && last_value==0xdd && context.Rip==(uintptr_t)code+3);
    code[0]=0xc6;code[1]=0x03;code[2]=0x2a;context.Rip=(uintptr_t)code;
    CHECK(pilot_mmio_fault_range(&exception,guest,0xfd000000,0xfdffffff,probe_sized,&error)==1);
    CHECK(last_value==0x2a && context.EFlags==0x246 && transactions==8);
    context.Rip=(uintptr_t)code;
    CHECK(pilot_mmio_fault_range(&exception,guest,0xfd000000,0xfd6013d3,probe_sized,&error)==0);
    CHECK(transactions==8 && context.Rip==(uintptr_t)code);
    /* AC97's ISR reads WORD status with MOVZX and acknowledges with MOV16.
     * Verify full x64 register semantics, not just the low returned bits. */
    address=(uintptr_t)guest+0xfec00116ull;context.Rbx=address;
    record.ExceptionInformation[1]=address;record.ExceptionInformation[0]=0;
    const unsigned char word_zx[]={0x0f,0xb7,0x03};
    memcpy(code,word_zx,sizeof(word_zx));context.Rip=(uintptr_t)code;context.Rax=UINT64_MAX;
    CHECK(pilot_mmio_fault_range(&exception,guest,0xfec00000,0xfec00fff,probe_sized,&error)==1);
    CHECK(context.Rax==0x3456 && last_size==2 && context.Rip==(uintptr_t)code+3 && context.EFlags==0x246);
    const unsigned char word_load[]={0x66,0x8b,0x03};
    memcpy(code,word_load,sizeof(word_load));context.Rip=(uintptr_t)code;context.Rax=0x123456789abcdef0ull;
    CHECK(pilot_mmio_fault_range(&exception,guest,0xfec00000,0xfec00fff,probe_sized,&error)==1);
    CHECK(context.Rax==0x123456789abc3456ull && context.Rip==(uintptr_t)code+3);
    const unsigned char word_rex[]={0x66,0x44,0x8b,0x53,0xf8};
    memcpy(code,word_rex,sizeof(word_rex));context.Rip=(uintptr_t)code;context.Rbx=address+8;
    context.R10=0xaabbccddeeff0011ull;
    CHECK(pilot_mmio_fault_range(&exception,guest,0xfec00000,0xfec00fff,probe_sized,&error)==1);
    CHECK(context.R10==0xaabbccddeeff3456ull && context.Rip==(uintptr_t)code+5);
    code[2]=0x89;context.Rip=(uintptr_t)code;record.ExceptionInformation[0]=1;
    CHECK(pilot_mmio_fault_range(&exception,guest,0xfec00000,0xfec00fff,probe_sized,&error)==1);
    CHECK(last_value==0x3456 && last_size==2 && last_write && context.R10==0xaabbccddeeff3456ull);
    const unsigned char word_immediate[]={0x66,0xc7,0x03,0xef,0xbe};
    memcpy(code,word_immediate,sizeof(word_immediate));context.Rip=(uintptr_t)code;context.Rbx=address;
    CHECK(pilot_mmio_fault_range(&exception,guest,0xfec00000,0xfec00fff,probe_sized,&error)==1);
    CHECK(last_value==0xbeef && context.Rip==(uintptr_t)code+5);
    const unsigned char word_relative[]={0x66,0xc7,5,0,0,0,0,0x78,0x56};
    memcpy(code,word_relative,sizeof(word_relative));context.Rip=(uintptr_t)code;
    record.ExceptionInformation[1]=(uintptr_t)code+9;
    guest=(unsigned char*)((uintptr_t)code+9-0xfec00116ull);
    CHECK(pilot_mmio_fault_range(&exception,guest,0xfec00000,0xfec00fff,probe_sized,&error)==1);
    CHECK(last_value==0x5678 && context.Rip==(uintptr_t)code+9);
    /* Exact host encoding retained at the title fault site. */
    const unsigned char title_word[]={0x0f,0xb7,0x04,0x08};
    memcpy(code,title_word,sizeof(title_word));context.Rip=(uintptr_t)code;
    guest=(unsigned char*)0x100000000ull;address=(uintptr_t)guest+0xfec00116ull;
    record.ExceptionInformation[0]=0;record.ExceptionInformation[1]=address;
    context.Rax=address-0x50;context.Rcx=0x50;
    CHECK(pilot_mmio_fault_range(&exception,guest,0xfec00000,0xfec00fff,probe_sized,&error)==1);
    CHECK(context.Rax==0x3456 && context.Rip==(uintptr_t)code+4);
    const unsigned before_reject=transactions;
    context.Rip=(uintptr_t)code;context.Rax=address-0x50;
    CHECK(pilot_mmio_fault_range(&exception,guest,0xfec00116,0xfec00116,probe_sized,&error)==0);
    ++record.ExceptionInformation[1];++context.Rax;
    CHECK(pilot_mmio_fault_range(&exception,guest,0xfec00000,0xfec00fff,probe_sized,&error)==0);
    --record.ExceptionInformation[1];--context.Rax;
    CHECK(pilot_mmio_fault(&exception,guest,probe,&error)==0);
    CHECK(transactions==before_reject && context.Rip==(uintptr_t)code && context.EFlags==0x246);
    CONTEXT saved=context;reject_transaction=1;
    CHECK(pilot_mmio_fault_range(&exception,guest,0xfec00000,0xfec00fff,probe_sized,&error)==-1);
    CHECK(error && !memcmp(&context,&saved,sizeof(context)) && transactions==before_reject+1);
    puts("PASS: MMIO MOV addressing, register width, exact instruction length, preserved flags and rejection guards");
    return 0;
}
