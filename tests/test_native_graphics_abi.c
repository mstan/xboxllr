/* Test the real register/stack bridge with nested service callbacks. */
#include <assert.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#ifdef _MSC_VER
#define RECOMP_TLS __declspec(thread)
#else
#define RECOMP_TLS _Thread_local
#endif
#define MEM_RESERVE 0x2000
#define MEM_COMMIT 0x1000
#define MEM_RELEASE 0x8000
#define PAGE_READWRITE 4
#define PILOT_D3D_COUNT 2
static const struct {unsigned stack_bytes;} pilot_d3d_calls[]={{24},{4}};
static unsigned char ram[65536],*memory=ram;
static uint32_t g_esp,g_ebx,g_esi,g_edi,g_ebp,g_eax,g_ecx,g_edx;
static unsigned allocations,releases,dispatches;
static jmp_buf fault_target;
/* The generated CPU header exposes these legacy instruction aliases. */
#define ebx g_ebx
#define esi g_esi
#define edi g_edi
#define MEM32(address) (*(uint32_t *)(void *)(ram+(address)))
static void stop(const char *reason,uint32_t value)
{(void)reason;(void)value;longjmp(fault_target,1);}
static uint32_t vm_allocate(uint32_t *address,unsigned bits,uint32_t *bytes,unsigned flags,unsigned protection)
{
    assert(!*address && !bits && *bytes==4096);
    assert(flags==(MEM_RESERVE|MEM_COMMIT) && protection==PAGE_READWRITE);
    *address=0x8000;++allocations;return 0;
}
static uint32_t vm_free(uint32_t *address,uint32_t *bytes,unsigned type)
{
    assert(*address==0x8000 && !*bytes && type==MEM_RELEASE);
    *address=0;*bytes=4096;++releases;return 0;
}
#include "graphics_abi.inc"
void pilot_d3d_dispatch(unsigned entry)
{
    ++dispatches;
    uint32_t frame=g_esp;
    if(entry==0) {
        assert(MEM32(frame)==0x12345678);
        assert(g_eax==0x10 && g_ecx==0x4321 && g_edx==0x9999);
        const uint32_t expected[]={0,1,0,0x10,0x2345,0x4321};
        assert(!memcmp(memory+frame+4,expected,sizeof(expected)));
        /* A service can call back into generated code on another guest stack. */
        uint32_t nested_args[]={0x1357};
        g_esp=0x3000;MEM32(g_esp)=0x87654321;
        pilot_d3d_dispatch_frame(1,nested_args,1,0);
        assert(g_esp==0x3004 && g_eax==0xabcdef01);
        g_esp=frame+28;
        g_ebx=1;g_esi=2;g_edi=3;g_ebp=4;g_eax=0x55667788;
    } else {
        assert(entry==1 && frame==0x8100);
        assert(MEM32(frame)==0x87654321 && MEM32(frame+4)==0x1357);
        g_esp+=8;g_eax=0xabcdef01;
    }
}
int main(void)
{
    g_esp=0x2000;MEM32(g_esp)=0x12345678;
    memset(ram+0x2004,0xa5,128);
    g_ebx=11;g_esi=22;g_edi=33;g_ebp=44;
    g_eax=0x10;g_ecx=0x4321;g_edx=0x9999;
    const uint32_t arguments[]={0,1,0,0x10,0x2345,0x4321};
    pilot_d3d_dispatch_frame(0,arguments,6,4);
    assert(g_esp==0x2008 && g_eax==0x55667788);
    assert(g_ebx==11 && g_esi==22 && g_edi==33 && g_ebp==44);
    assert(allocations==1 && dispatches==2);
    assert(MEM32(0x2000)==0x12345678);
    for(unsigned i=0;i<128;++i)assert(ram[0x2004+i]==0xa5);
    for(unsigned fault=0;fault<5;++fault) {
        g_esp=fault==4?UINT32_MAX-3:0x2000;
        const uint32_t previous=g_esp;
        if(!setjmp(fault_target)) {
            pilot_d3d_dispatch_frame(fault==0?2:0,
                fault==1?NULL:arguments,fault==2?5:6,fault==3?2:4);
            assert(0);
        }
        assert(g_esp==previous && dispatches==2);
    }
    graphics_abi_thread_retire();graphics_abi_thread_retire();
    assert(releases==1 && !graphics_abi_frames && !graphics_abi_depth);
    puts("native graphics ABI canonical arguments, nesting, cleanup, retirement and rejection PASS");
    return 0;
}
