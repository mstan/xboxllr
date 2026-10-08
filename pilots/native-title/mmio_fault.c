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
int pilot_mmio_fault(EXCEPTION_POINTERS *exception,unsigned char *guest,
                     PilotMmioAccess access,const char **failure)
{
    EXCEPTION_RECORD *record=exception->ExceptionRecord;
    CONTEXT *context=exception->ContextRecord;
    if(record->ExceptionCode!=EXCEPTION_ACCESS_VIOLATION || record->NumberParameters<2 ||
       record->ExceptionInformation[0]>1 || !context)return 0;
    uint64_t fault=record->ExceptionInformation[1],base=(uintptr_t)guest;
    if(fault<base+0xfed00000ull || fault>=base+0xfed01000ull)return 0;
    unsigned char code[15];SIZE_T read=0;
    if(!ReadProcessMemory(GetCurrentProcess(),(void*)context->Rip,code,sizeof(code),&read) ||
       read!=sizeof(code))return 0;
    unsigned at=0,rex=0;
    if((code[at]&0xf0)==0x40)rex=code[at++];
    if(rex&8)return 0; // No 64-bit OHCI register accesses.
    unsigned opcode=code[at++];
    if(opcode!=0x8b && opcode!=0x89 && opcode!=0xc7)return 0;
    int write=opcode!=0x8b;
    if((unsigned)write!=record->ExceptionInformation[0])return 0;
    unsigned modrm=code[at++],mod=modrm>>6,rm=modrm&7;
    if(mod==3 || (opcode==0xc7 && ((modrm>>3)&7)))return 0;
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
    uint32_t value=(uint32_t)*host_register(context,reg);
    if(opcode==0xc7){memcpy(&value,code+at,4);at+=4;}
    if(rip_relative)address+=context->Rip+at;
    if(address!=fault || (address-base)&3)return 0;
    *failure=access((uint32_t)(address-base),write,&value);
    if(*failure)return -1;
    if(!write)*host_register(context,reg)=value; // x64 dword load clears upper bits.
    context->Rip+=at;
    return 1;
}
