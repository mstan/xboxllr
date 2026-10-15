#include "mmio_fault.h"
#include <string.h>

static DWORD64 *host_register(CONTEXT *context,unsigned index)
{
    switch(index) {
    case 0:return &context->Rax;case 1:return &context->Rcx;
    case 2:return &context->Rdx;case 3:return &context->Rbx;
    case 4:return &context->Rsp;case 5:return &context->Rbp;
    case 6:return &context->Rsi;case 7:return &context->Rdi;
    case 8:return &context->R8;case 9:return &context->R9;
    case 10:return &context->R10;case 11:return &context->R11;
    case 12:return &context->R12;case 13:return &context->R13;
    case 14:return &context->R14;default:return &context->R15;
    }
}
static int decode(EXCEPTION_POINTERS *exception,unsigned char *guest,
                  uint32_t first,uint32_t last,PilotMmioAccess access,
                  PilotMmioAccessSized sized,const char **failure)
{
    EXCEPTION_RECORD *record=exception->ExceptionRecord;
    CONTEXT *context=exception->ContextRecord;
    if(record->ExceptionCode!=EXCEPTION_ACCESS_VIOLATION || record->NumberParameters<2 ||
       record->ExceptionInformation[0]>1 || !context)return 0;
    uint64_t fault=record->ExceptionInformation[1],base=(uintptr_t)guest;
    if(fault<base+first || fault>base+last)return 0;
    unsigned char code[15];SIZE_T read=0;
    if(!ReadProcessMemory(GetCurrentProcess(),(void*)context->Rip,code,sizeof(code),&read) ||
       read!=sizeof(code))return 0;
    unsigned at=0,rex=0,operand16=0;
    if(code[at]==0x66){operand16=1;++at;}
    if((code[at]&0xf0)==0x40)rex=code[at++];
    if(rex&8)return 0; // No 64-bit register transactions or destinations.
    unsigned opcode=code[at++];
    if(opcode==0x0f){unsigned extension=code[at++];if(extension!=0xb6 && extension!=0xb7)return 0;opcode=0x0f00|extension;}
    if(opcode!=0x8b && opcode!=0x89 && opcode!=0xc7 &&
       opcode!=0x8a && opcode!=0x88 && opcode!=0xc6 && opcode!=0x0fb6 && opcode!=0x0fb7)return 0;
    // The reached word forms are MOV r/m16,r16 (including imm16) and
    // MOVZX r32,r/m16. Other operand-prefix combinations remain unhandled.
    if(operand16 && opcode!=0x8b && opcode!=0x89 && opcode!=0xc7)return 0;
    unsigned size=(opcode==0x8b || opcode==0x89 || opcode==0xc7)?(operand16?2:4):opcode==0x0fb7?2:1;
    if(!sized && size!=4)return 0;
    if(fault+size>base+(uint64_t)last+1)return 0;
    int write=opcode==0x89 || opcode==0xc7 || opcode==0x88 || opcode==0xc6;
    if((unsigned)write!=record->ExceptionInformation[0])return 0;
    unsigned modrm=code[at++],mod=modrm>>6,rm=modrm&7;
    if(mod==3 || ((opcode==0xc7 || opcode==0xc6) && ((modrm>>3)&7)))return 0;
    unsigned reg=((modrm>>3)&7)|((rex&4)?8:0);
    uint64_t address=0;int displacement=mod==1?1:mod==2?4:0,rip_relative=0;
    if(rm==4) {
        unsigned sib=code[at++],index=((sib>>3)&7)|((rex&2)?8:0),b=sib&7;
        if(index!=4)address+=*host_register(context,index)<<(sib>>6);
        if(mod==0 && b==5)displacement=4;
        else address+=*host_register(context,b|((rex&1)?8:0));
    } else if(mod==0 && rm==5){displacement=4;rip_relative=1;}
    else address=*host_register(context,rm|((rex&1)?8:0));
    if(displacement==1)address+=(int8_t)code[at++];
    else if(displacement==4){int32_t value;memcpy(&value,code+at,4);address+=value;at+=4;}
    unsigned shift=0;
    if(size==1 && opcode!=0x0fb6 && !rex && reg>=4){reg-=4;shift=8;}
    uint32_t value=(uint32_t)(*host_register(context,reg)>>shift);
    if(size==1)value&=255;
    if(size==2)value&=65535;
    if(opcode==0xc7){value=0;memcpy(&value,code+at,size);at+=size;}
    if(opcode==0xc6)value=code[at++];
    if(rip_relative)address+=context->Rip+at;
    if(address!=fault || ((address-base)&(size-1)))return 0;
    *failure=sized?sized((uint32_t)(address-base),size,write,&value):
                   access((uint32_t)(address-base),write,&value);
    if(*failure)return -1;
    if(!write) {
        DWORD64 *target=host_register(context,reg);
        if(opcode==0x8a)*target=(*target&~(0xffull<<shift))|((value&255ull)<<shift);
        else if(operand16)*target=(*target&~0xffffull)|(value&65535ull);
        else *target=size==1?value&255:size==2?value&65535:value; // Dword destination clears upper bits.
    }
    context->Rip+=at;
    return 1;
}
int pilot_mmio_fault(EXCEPTION_POINTERS *exception,unsigned char *guest,
                     PilotMmioAccess access,const char **failure)
{
    if(exception->ExceptionRecord->NumberParameters<2)return 0;
    uint64_t fault=exception->ExceptionRecord->ExceptionInformation[1];
    if(fault==(uintptr_t)guest+0xfe80200cull)
        return decode(exception,guest,0xfe80200c,0xfe80200f,access,NULL,failure);
    return decode(exception,guest,0xfed00000,0xfed00fff,access,NULL,failure);
}
int pilot_mmio_fault_range(EXCEPTION_POINTERS *exception,unsigned char *guest,
                          uint32_t first,uint32_t last,PilotMmioAccessSized access,
                          const char **failure)
{
    if(first>last)return 0;
    return decode(exception,guest,first,last,NULL,access,failure);
}
