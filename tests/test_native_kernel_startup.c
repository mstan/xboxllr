/* Authored native selector-owner and guest ABI checks. No host GDT access. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "kernel_startup.h"
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line%d: %s\n",__LINE__,#x);ExitProcess(1);}}while(0)
static unsigned char memory[0x20000];
#define MEM32(a) (*(uint32_t *)(memory+(a)))
#define MEM16(a) (*(uint16_t *)(memory+(a)))
static uint32_t g_esp,g_eax,g_ecx,g_edx,g_fs_base;
static int cpu_interrupt_flag=1,cpu_interrupt_shadow;
static int failure_is_clean(void);
static void stop(const char *why,uint32_t value){if(!failure_is_clean())ExitProcess(43);fprintf(stderr,"guard:%s/%u\n",why,value);ExitProcess(42);}
#define PILOT_STARTUP_NATIVE 1
#define PILOT_NATIVE_KERNEL_EXPORT_ONLY 1
#define PILOT_NATIVE_KERNEL_BUILD 5838
#define PILOT_STARTUP_KERNEL_BUILD 5838
#define PILOT_STARTUP_HARDWARE_FLAGS 0
#define PILOT_STARTUP_INITIAL_DESCRIPTOR UINT64_C(0x00cb9b000000ffff)
#define PILOT_STARTUP_INIT_START 0x80051520u
#define PILOT_STARTUP_SAVED_LOW 0x1000u
#define PILOT_STARTUP_SAVED_HIGH 0x1004u
#define PILOT_STARTUP_HARDWARE_IMPORT 0x2000u
#define PILOT_STARTUP_VERSION_IMPORT 0x2004u
#define PILOT_STARTUP_APPLY_RETURN 0x3000u
#define PILOT_STARTUP_SETUP_EXCHANGE_RETURN 0x3004u
#define PILOT_STARTUP_RESTORE_EXCHANGE_RETURN 0x3008u
#include "startup.inc"
static int failure_is_clean(void)
{
    return !kernel_startup_owner.initialized && !MEM32(PILOT_STARTUP_SAVED_LOW) && !MEM32(PILOT_STARTUP_SAVED_HIGH);
}
int main(int argc,char **argv)
{
    PilotKernelStartup pure={0};uint64_t previous=0x1234;
    CHECK(!pilot_kernel_startup_initialize(&pure,PILOT_STARTUP_INITIAL_DESCRIPTOR,PILOT_STARTUP_INIT_START,4983,1));
    CHECK(!pure.initialized);
    CHECK(!pilot_kernel_startup_initialize(&pure,PILOT_STARTUP_INITIAL_DESCRIPTOR,PILOT_STARTUP_INIT_START,5838,0));
    CHECK(!pilot_kernel_startup_initialize(&pure,PILOT_STARTUP_INITIAL_DESCRIPTOR|UINT64_C(0x10000),PILOT_STARTUP_INIT_START,5838,1));
    CHECK(pilot_kernel_startup_initialize(&pure,PILOT_STARTUP_INITIAL_DESCRIPTOR,PILOT_STARTUP_INIT_START,5838,1));
    CHECK(pure.restricted==UINT64_C(0x00c89b0000000051));
    CHECK(pilot_kernel_startup_limit(pure.restricted)==0x80051fffu);
    CHECK(!pilot_kernel_startup_exchange(&pure,UINT64_C(0x00cf9b000000ffff),&previous));
    CHECK(previous==0x1234 && pure.current==pure.initial);
    CHECK(pilot_kernel_startup_exchange(&pure,pure.restricted,&previous));
    CHECK(previous==pure.initial);
    CHECK(pilot_kernel_startup_exchange(&pure,pure.initial,&previous));
    CHECK(previous==pure.restricted);
    CHECK(!pilot_kernel_startup_initialize(&pure,pure.initial,pure.init_start,5838,1));
    MEM32(PILOT_STARTUP_HARDWARE_IMPORT)=0x4000;MEM32(PILOT_STARTUP_VERSION_IMPORT)=0x4010;
    MEM16(0x4014)=5838;g_esp=0x5000;MEM32(g_esp)=PILOT_STARTUP_APPLY_RETURN;
    g_fs_base=0x6000;MEM32(g_fs_base+0x20)=g_fs_base+0x28;
    if(argc>1) {
        if(!strcmp(argv[1],"caller"))MEM32(g_esp)=0xdead;
        if(!strcmp(argv[1],"if"))cpu_interrupt_flag=0;
        if(!strcmp(argv[1],"shadow"))cpu_interrupt_shadow=1;
        if(!strcmp(argv[1],"version"))MEM16(0x4014)=4983;
        if(!strcmp(argv[1],"hardware"))MEM32(0x4000)=2;
        if(!strcmp(argv[1],"monitor"))MEM32(g_fs_base+0x28+0x250)=1;
        if(!strcmp(argv[1],"prcb"))MEM32(g_fs_base+0x20)=0xdead0000;
        if(!strcmp(argv[1],"fs-wrap"))g_fs_base=0xffffff00u;
        pilot_startup_apply();return 1;
    }
    memory[PILOT_STARTUP_SAVED_LOW-1]=0x5a;memory[PILOT_STARTUP_SAVED_HIGH+4]=0xa5;
    pilot_startup_apply();CHECK(g_esp==0x5004);
    CHECK(g_eax==0xffff && g_ecx==0xffff && g_edx==0x00cb9b00);
    CHECK(MEM32(PILOT_STARTUP_SAVED_LOW)==g_eax && MEM32(PILOT_STARTUP_SAVED_HIGH)==g_edx);
    CHECK(memory[PILOT_STARTUP_SAVED_LOW-1]==0x5a && memory[PILOT_STARTUP_SAVED_HIGH+4]==0xa5);
    CHECK(kernel_startup_owner.current==pure.restricted);
    g_esp=0x5000;MEM32(g_esp)=PILOT_STARTUP_RESTORE_EXCHANGE_RETURN;
    MEM32(g_esp+4)=0xffff;MEM32(g_esp+8)=0x00cb9b00;
    pilot_startup_exchange();CHECK(g_esp==0x500c);
    CHECK(g_eax==0x51 && g_ecx==0x51 && g_edx==0x00c89b00);
    CHECK(kernel_startup_owner.current==pure.initial);
    CHECK(cpu_interrupt_flag==1 && !cpu_interrupt_shadow);
    const char *negatives[]={"caller","if","shadow","version","hardware","monitor","prcb","fs-wrap"};
    for(unsigned i=0;i<sizeof(negatives)/sizeof(negatives[0]);++i) {
        char command[4096];STARTUPINFOA si;PROCESS_INFORMATION pi;
        memset(&si,0,sizeof(si));si.cb=sizeof(si);memset(&pi,0,sizeof(pi));
        CHECK(snprintf(command,sizeof(command),"\"%s\" %s",argv[0],negatives[i])>0);
        CHECK(CreateProcessA(NULL,command,NULL,NULL,FALSE,CREATE_NO_WINDOW,NULL,NULL,&si,&pi));
        CHECK(WaitForSingleObject(pi.hProcess,5000)==WAIT_OBJECT_0);
        DWORD code;CHECK(GetExitCodeProcess(pi.hProcess,&code));CHECK(code==42);
        CloseHandle(pi.hProcess);CloseHandle(pi.hThread);
    }
    puts("native startup selector owner/ABI/guards PASS");return 0;
}
