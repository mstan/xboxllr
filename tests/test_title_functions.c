#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fenv.h>
#if defined(_M_X64) || defined(__x86_64__)
#include <xmmintrin.h>
#endif
#include "title_mod.h"
typedef void (*recomp_func_t)(void);
typedef struct{uint32_t lane[4];} RecompXmm;
typedef struct{uint32_t lane[2];} RecompMmx;
static uint32_t g_eax,g_ecx,g_edx,g_ebx,g_esp,g_ebp,g_esi,g_edi,g_seh_ebp,g_icall_saved_esp,g_icall_dispatch_form;
static uint32_t g_fs_base,next_kernel_ordinal;
static RecompXmm g_xmm0,g_xmm1,g_xmm2,g_xmm3,g_xmm4,g_xmm5,g_xmm6,g_xmm7;
static RecompMmx g_mm0,g_mm1,g_mm2,g_mm3,g_mm4,g_mm5,g_mm6,g_mm7;
static double g_fp_stack[8];static int g_fp_top,g_fp_cmp,g_df,g_force_return;
static uint16_t g_fp_control_word,g_fp_cc;
static unsigned char *memory;
static const struct PilotTitleMod *title_mod;
static void stop(const char *message,unsigned code){fprintf(stderr,"STOP %s %u\n",message,code);ExitProcess(2);}
#define PILOT_TITLE_MODS 1
#define PILOT_TITLE_FUNCTIONS 1
#define PILOT_SCHEDULING_LLE 1 /* the unrelated arithmetic-call fixture is unused */
#include "native_cpu_context.inc"
/* Production runtime includes the generated register-name aliases. A service
 * parameter named after an x86 register must not shadow its global register. */
#define ecx g_ecx
#include "title_functions.inc"
#undef ecx
#define CHECK(c) do{if(!(c)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#c);ExitProcess(1);}}while(0)
static unsigned calls,completed;
static void original(void)
{
    ++calls;CHECK(g_ecx==0x1234);g_eax=*(uint32_t*)(memory+g_esp+4)+7;g_esp+=8;
    memset(g_fp_stack,0,sizeof(g_fp_stack));g_fp_top=3;g_fp_control_word=0x127f;g_xmm7.lane[3]=99;g_mm7.lane[1]=66;
    g_seh_ebp=17;g_icall_saved_esp=19;g_icall_dispatch_form=4;g_df=1;g_force_return=1;fesetround(FE_TOWARDZERO);
}
recomp_func_t pilot_title_original_lookup(unsigned id,unsigned *cleanup)
{if(id!=5)return NULL;*cleanup=4;return original;}
static int callback(struct PilotTitleModCall *call)
{
    CHECK(call->id==5 && call->caller==0xabcd && call->entry[1]==0x1234 && call->arguments[0]==10);
    if(!call->phase) {
        if(completed){call->gpr[0]=35;call->gpr[4]=call->entry[4]+8;return 1;}
        return 0;
    }
    CHECK(call->gpr[0]==17);call->gpr[0]=27;return 0;
}
int main(void)
{
    memory=VirtualAlloc(NULL,1024*1024,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);CHECK(memory);
    uint32_t initial[]={1,0x4321,3,4,0x80000,6,7,8};title_gpr_set(initial);
    for(unsigned i=0;i<8;++i)g_fp_stack[i]=i+.25;g_fp_top=6;g_fp_control_word=0x27f;
    g_xmm7.lane[3]=123;g_mm7.lane[1]=456;g_seh_ebp=100;g_icall_saved_esp=200;g_icall_dispatch_form=2;
    memset(memory+g_esp-256,0x5a,68);uint32_t saved[17];memcpy(saved,memory+g_esp-256,68);
    CHECK(!fesetround(FE_DOWNWARD));uint32_t args[]={10},result=0,after[8];
    CHECK(title_mod_invoke(5,0x1234,args,1,&result) && result==17 && calls==1);title_gpr_get(after);
    CHECK(!memcmp(initial,after,sizeof(initial)) && !memcmp(saved,memory+g_esp-256,68));
    CHECK(g_fp_top==6 && g_fp_stack[7]==7.25 && g_fp_control_word==0x27f && g_xmm7.lane[3]==123 && g_mm7.lane[1]==456);
    CHECK(g_seh_ebp==100 && g_icall_saved_esp==200 && g_icall_dispatch_form==2 && !g_df && !g_force_return && fegetround()==FE_DOWNWARD);
    CHECK(!title_mod_invoke(6,0,args,1,&result) && !title_mod_invoke(5,0,NULL,0,&result));
    *(uint32_t*)(memory+g_esp)=0xabcd;*(uint32_t*)(memory+g_esp+4)=10;
    g_ecx=0x1234;
    static const struct PilotTitleMod mod={PILOT_TITLE_MOD_ABI,NULL,NULL,NULL,callback};title_mod=&mod;
    pilot_title_function(5,original,4);CHECK(calls==2 && g_eax==27 && g_esp==initial[4]+8);
    g_esp=initial[4];completed=1;
    pilot_title_function(5,original,4);CHECK(calls==2 && g_eax==35 && g_esp==initial[4]+8);
    CHECK(VirtualFree(memory,0,MEM_RELEASE));puts("PASS: original execution, caller ABI, nested stack/CPU/vector/x87/fenv preservation and unknown-call rejection");return 0;
}
