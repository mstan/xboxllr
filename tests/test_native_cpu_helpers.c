/* Authored call/diagnostic contracts; no title inputs or interpreted CPU. */
#define RECOMP_GENERATED_CODE
#include "recomp_types.h"
#include <stdio.h>
#include <stdlib.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr,"CPU helper line %d: %s\n",__LINE__,#x); exit(1); } } while (0)
RECOMP_TLS uint32_t g_eax,g_ecx,g_edx,g_esp,g_ebx,g_esi,g_edi;
RECOMP_TLS uint32_t g_icall_saved_esp,g_icall_dispatch_form;
RECOMP_TLS volatile uint32_t g_icall_trace[ICALL_TRACE_SIZE],g_icall_trace_idx;
RECOMP_TLS volatile uint64_t g_icall_count;
ptrdiff_t g_xbox_mem_offset;
uint32_t g_xbox_code_lo=0x10000,g_xbox_code_hi=0x20000;
static jmp_buf guard;
static unsigned calls,cleanup,clobber;
static uint32_t refused,refused_stack,diagnostic[7];

static void callee(void)
{
    ++calls; g_eax=0x13579bdf; g_esp+=4+cleanup;
    if(clobber==1)++g_ebx;
    if(clobber==2)++g_esi;
    if(clobber==3)++g_edi;
    if(clobber==4)g_esp+=4;
    if(clobber==5)g_esp-=4;
}
recomp_func_t recomp_lookup_manual(uint32_t at){return at==0x10010?callee:NULL;}
recomp_func_t recomp_lookup(uint32_t at){return at==0x10020?callee:NULL;}
recomp_func_t recomp_lookup_kernel(uint32_t at){return at==0xfe000010?callee:NULL;}
void recomp_icall_fail_log(uint32_t at){refused=at;longjmp(guard,1);}
void recomp_icall_not_code_log(uint32_t at,uint32_t stack)
{refused=at;refused_stack=stack;longjmp(guard,2);}
void recomp_abi_violation_log(uint32_t at,uint32_t b,uint32_t s,uint32_t d,uint32_t p)
{(void)at;(void)b;(void)s;(void)d;(void)p;longjmp(guard,3);}
void recomp_abi_pop_violation_log(uint32_t at,uint32_t b,uint32_t s,uint32_t d,uint32_t p,uint32_t n)
{
    diagnostic[0]=at;diagnostic[1]=b;diagnostic[2]=s;diagnostic[3]=d;
    diagnostic[4]=p;diagnostic[5]=n;diagnostic[6]=g_esp;longjmp(guard,4);
}
static void reset(void)
{g_esp=0x11000;g_ebx=11;g_esi=22;g_edi=33;cleanup=8;clobber=0;refused=refused_stack=0;}
int main(void)
{
    uint64_t memory[4]={0};
    g_xbox_mem_offset=(ptrdiff_t)(uintptr_t)memory-0x10000;
    MEM64(0x10008)=UINT64_C(0x0123456789abcdef);
    CHECK(memory[1]==UINT64_C(0x0123456789abcdef));
    reset();RECOMP_ABI_CALL_POP(0x1234,callee,8);
    CHECK(g_eax==0x13579bdf && g_esp==0x1100c && calls==1);
    reset();RECOMP_ICALL(0x10010);CHECK(g_esp==0x1100c);
    reset();RECOMP_ICALL_SAFE(0x10020,0x11010);CHECK(g_esp==0x1100c);
    reset();RECOMP_ICALL_SAFE_AT(0xfe000010,0x11010,0x10050);CHECK(g_esp==0x1100c);
    CHECK(calls==4 && g_icall_count==3);

    reset();if(!setjmp(guard)){RECOMP_ICALL(0x10030);CHECK(0);}
    CHECK(refused==0x10030 && g_icall_saved_esp==0x11000 && g_icall_dispatch_form==1);
    CHECK(g_esp==0x11000 && calls==4); /* A refusing owner does not return/clean up. */
    reset();if(!setjmp(guard)){RECOMP_ITAIL(0x10030);CHECK(0);}
    CHECK(refused==0x10030 && g_icall_saved_esp==0x11000 && g_icall_dispatch_form==2);
    reset();if(!setjmp(guard)){RECOMP_ICALL(0x40);CHECK(0);}
    CHECK(refused==0x40 && refused_stack==0x11004 && g_esp==0x11000);
    reset();if(!setjmp(guard)){RECOMP_ICALL_SAFE(0x40,0x11014);CHECK(0);}
    CHECK(refused_stack==0x11014 && g_esp==0x11000);
    reset();if(!setjmp(guard)){RECOMP_ICALL_SAFE_AT(0x40,0x11018,0x10050);CHECK(0);}
    CHECK(refused_stack==0x11018 && g_esp==0x11000);
#ifdef RECOMP_ABI_CHECK
    for(unsigned error=1;error<=5;++error){
        reset();clobber=error;
        if(!setjmp(guard)){RECOMP_ABI_CALL_POP(0x5432,callee,8);CHECK(0);}
        CHECK(diagnostic[0]==0x5432 && diagnostic[1]==11 && diagnostic[2]==22);
        CHECK(diagnostic[3]==33 && diagnostic[4]==0x11000 && diagnostic[5]==8);
    }
#else
    reset();clobber=1;RECOMP_ABI_CALL_POP(0x5432,callee,8);
    CHECK(g_ebx==12); /* Production has no added diagnostic hot-path work. */
#endif
    puts("PASS MIT CPU helpers: native calls, cleanup, refusing boundaries and diagnostics");
    return 0;
}
