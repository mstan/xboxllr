/* Exercise the production bridge with hostile callees and real host fenv.
 * No game binary, Windows API or interpreted CPU is required. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fenv.h>

#define CHECK(c) do {if(!(c)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#c);exit(1);}}while(0)
typedef struct {uint32_t u[4];} RecompXmm;
typedef struct {uint32_t u[2];} RecompMmx;
typedef void (*recomp_func_t)(void);
static uint32_t g_eax,g_ecx,g_edx,g_esp,g_ebx,g_esi,g_edi,g_ebp,g_fs_base;
static uint32_t g_seh_ebp,g_icall_saved_esp,g_icall_dispatch_form,next_kernel_ordinal;
static RecompXmm g_xmm0,g_xmm1,g_xmm2,g_xmm3,g_xmm4,g_xmm5,g_xmm6,g_xmm7;
static RecompMmx g_mm0,g_mm1,g_mm2,g_mm3,g_mm4,g_mm5,g_mm6,g_mm7;
static double g_fp_stack[8];
static int g_fp_top,g_fp_cmp,g_df,dispatch_in_dpc;
static uint16_t g_fp_control_word,g_fp_cc;
static volatile uint32_t suspend_count;
static volatile uint32_t *g_pilot_suspend_count=&suspend_count;
static uint8_t memory[0x10000];
static unsigned argument_count,calls;
static const char *failure="";
static uint32_t words[]={0x10203040,0xa0b0c0d0,0x13579bdf,0x2468ace0};
static uint32_t load32(uint32_t at){uint32_t n;memcpy(&n,memory+at,4);return n;}
static void push32(uint32_t *sp,uint32_t n){*sp-=4;memcpy(memory+*sp,&n,4);}
#define PUSH32(sp,n) push32(&(sp),(uint32_t)(n))
static void stop(const char *why,uint32_t at)
{fprintf(stderr,"STOP %s at %x\n",why,at);exit(2);}
static int wait_range(uint32_t at,uint32_t bytes,int write)
{CHECK(write==1);return strcmp(failure,"range") && at>=0x1000 && at+bytes<=sizeof(memory);}
static void callee(void)
{
    ++calls;CHECK(!g_pilot_suspend_count && !g_fp_top);
    CHECK(g_ecx==0x12345678 && load32(g_esp)==0);
    for(unsigned i=0;i<argument_count;++i)CHECK(load32(g_esp+4+4*i)==words[i]);
    for(unsigned i=0;i<8;++i)CHECK(g_fp_stack[i]==0);
    /* A pending suspension survives this short nonblocking service, then is
     * visible through the restored pointer at the next ordinary checkpoint. */
    ++suspend_count;
    g_eax=0x87654321;g_ecx=7;g_edx=9;g_df=1;
    g_ebp=0x8899;g_seh_ebp=0x123;g_icall_saved_esp=0x456;g_icall_dispatch_form=0x789;
    memset(&g_xmm0,0xcd,sizeof(g_xmm0));memset(&g_mm7,0xab,sizeof(g_mm7));
    g_fp_stack[4]=123.75;g_fp_top=4;g_fp_cmp=2;g_fp_cc=0x4000;g_fp_control_word=0x37f;
    CHECK(!fesetround(FE_UPWARD));CHECK(!feclearexcept(FE_ALL_EXCEPT));
    CHECK(!feraiseexcept(FE_OVERFLOW));
    g_esp+=4+4*argument_count;
    if(!strcmp(failure,"esp"))g_esp-=4;
    if(!strcmp(failure,"ebx"))++g_ebx;
    if(!strcmp(failure,"esi"))++g_esi;
    if(!strcmp(failure,"edi"))++g_edi;
    if(!strcmp(failure,"fs"))++g_fs_base;
    if(!strcmp(failure,"ordinal"))++next_kernel_ordinal;
}
static recomp_func_t recomp_lookup(uint32_t at){return at==0x12340?callee:NULL;}
#include "../pilots/native-title/native_cpu_context.inc"

static void verify_restored(const struct NativeCpuContext *before)
{
    struct NativeCpuContext after;native_cpu_save(&after);
    CHECK(!memcmp(before->regs,after.regs,sizeof(after.regs)));
    CHECK(!memcmp(before->xmm,after.xmm,sizeof(after.xmm)));
    CHECK(!memcmp(before->mm,after.mm,sizeof(after.mm)));
    CHECK(!memcmp(before->fp,after.fp,sizeof(after.fp)));
    CHECK(before->fp_top==after.fp_top && before->fp_cmp==after.fp_cmp && before->df==after.df);
    CHECK(before->cw==after.cw && before->cc==after.cc);
    CHECK(fegetround()==FE_DOWNWARD && fetestexcept(FE_ALL_EXCEPT)==FE_DIVBYZERO);
    CHECK(g_pilot_suspend_count==&suspend_count);
}
int main(int argc,char **argv)
{
    if(argc==2)failure=argv[1];
    g_eax=1;g_ecx=2;g_edx=3;g_esp=0x2000;g_ebx=5;g_esi=6;g_edi=7;g_ebp=8;g_fs_base=9;
    g_seh_ebp=10;g_icall_saved_esp=11;g_icall_dispatch_form=12;next_kernel_ordinal=13;
    memset(&g_xmm0,0x55,sizeof(g_xmm0));memset(&g_mm7,0x66,sizeof(g_mm7));
    for(unsigned i=0;i<8;++i)g_fp_stack[i]=i+.25;
    g_fp_top=3;g_fp_cmp=-1;g_df=0;g_fp_control_word=0x77f;g_fp_cc=0x100;
    CHECK(!fesetround(FE_DOWNWARD));CHECK(!feclearexcept(FE_ALL_EXCEPT));
    CHECK(!feraiseexcept(FE_DIVBYZERO));
    if(*failure){
        argument_count=1;uint32_t entry=0x12340,count=1;const uint32_t *args=words;
        if(!strcmp(failure,"entry"))entry=0;
        if(!strcmp(failure,"dpc"))dispatch_in_dpc=1;
        if(!strcmp(failure,"underflow"))g_esp=511;
        if(!strcmp(failure,"count"))count=5;
        if(!strcmp(failure,"null"))args=NULL;
        (void)native_cpu_pure_call(entry,0x12345678,count,args);CHECK(0);
    }
    struct NativeCpuContext before;native_cpu_save(&before);
    for(argument_count=0;argument_count<=4;++argument_count){
        CHECK(native_cpu_pure_call(0x12340,0x12345678,argument_count,words)==0x87654321);
        verify_restored(&before);
    }
    argument_count=0;native_cpu_pure_thiscall(0x12340,0x12345678);verify_restored(&before);
    /* Guest-stack-backed argument lists must survive the bridge's pushes. */
    argument_count=4;memcpy(memory+g_esp-16,words,sizeof(words));
    CHECK(native_cpu_pure_call(0x12340,0x12345678,4,(const uint32_t*)(memory+g_esp-16))==0x87654321);
    verify_restored(&before);CHECK(calls==7 && suspend_count==7);
    puts("PASS native CPU service: stack arguments/aliasing, return value, full CPU/fenv restoration, suspension publication");
    return 0;
}
