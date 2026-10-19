/* Authored native HAL shutdown-registration ownership/ABI fixture. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
static unsigned char memory[65536],before[65536];
#define MEM32(a) (*(uint32_t *)(memory+(uint32_t)(a)))
#define MEM8(a) (*(uint8_t *)(memory+(uint32_t)(a)))
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line%d: %s\n",__LINE__,#x);ExitProcess(1);}}while(0)
static uint32_t g_esp,g_eax=0x12345678;
static int expect_guard,callback_calls;
static int failure_is_clean(void);
static void stop(const char *reason,uint32_t value)
{
    if(!expect_guard || !failure_is_clean())ExitProcess(43);
    fprintf(stderr,"guard:%s/%u\n",reason,value);ExitProcess(42);
}
static void authored_callback(void){++callback_calls;}
typedef void (*recomp_func_t)(void);
static recomp_func_t recomp_lookup(uint32_t address){return address==0x1234?authored_callback:NULL;}
#include "kernel_shutdown.inc"
static PilotShutdownRegistration before_entries[PILOT_SHUTDOWN_LIMIT];
static unsigned before_count;
static uint32_t before_esp;
static void snapshot(void)
{
    memcpy(before,memory,sizeof(memory));memcpy(before_entries,kernel_shutdown_entries,sizeof(before_entries));
    before_count=kernel_shutdown_count;before_esp=g_esp;
}
static int failure_is_clean(void)
{
    return !memcmp(before,memory,sizeof(memory)) && !memcmp(before_entries,kernel_shutdown_entries,sizeof(before_entries)) &&
        before_count==kernel_shutdown_count && before_esp==g_esp && !callback_calls && g_eax==0x12345678;
}
static void invoke(uint32_t registration,uint32_t registering)
{
    g_esp=0xf000;MEM32(g_esp)=0x4567;MEM32(g_esp+4)=registration;MEM32(g_esp+8)=registering;
    if(expect_guard)snapshot();
    CHECK(kernel_shutdown_dispatch(47));CHECK(g_esp==0xf00c && g_eax==0x12345678 && !callback_calls);
}
static void object(uint32_t registration,int32_t priority)
{
    MEM32(registration)=0x1234;memcpy(memory+registration+4,&priority,4);
    MEM32(registration+8)=0xaaaaaaaa;MEM32(registration+12)=0xbbbbbbbb;
}
static void order(const uint32_t *expected,unsigned count)
{
    uint32_t cursor=MEM32(kernel_shutdown_head),previous=kernel_shutdown_head;
    for(unsigned i=0;i<count;++i) {
        CHECK(cursor==expected[i]+8);CHECK(MEM32(cursor+4)==previous);
        previous=cursor;cursor=MEM32(cursor);
    }
    CHECK(cursor==kernel_shutdown_head && MEM32(kernel_shutdown_head+4)==previous);
    CHECK(kernel_shutdown_count==count);
}
int main(int argc,char **argv)
{
    memset(memory,0xa5,sizeof(memory));kernel_shutdown_initialize(0x8000);
    CHECK(MEM32(0x8000)==0x8000 && MEM32(0x8004)==0x8000);
    CHECK(memory[0x7fff]==0xa5 && memory[0x8008]==0xa5);
    CHECK(!kernel_shutdown_dispatch(49));
    object(0x1000,0);object(0x1020,INT32_MIN);object(0x1040,INT32_MAX);object(0x1060,0);
    if(argc>1) {
        if(!strcmp(argv[1],"capacity")) {
            for(unsigned i=0;i<PILOT_SHUTDOWN_LIMIT;++i){object(0x1000+i*32,(int32_t)i);invoke(0x1000+i*32,1);}
            object(0x1000+PILOT_SHUTDOWN_LIMIT*32,0);expect_guard=1;invoke(0x1000+PILOT_SHUTDOWN_LIMIT*32,1);
        }
        if(!strcmp(argv[1],"callback")){MEM32(0x1000)=0;expect_guard=1;invoke(0x1000,1);}
        if(!strcmp(argv[1],"address")){expect_guard=1;invoke(0xfffffff8,1);}
        invoke(0x1000,1);
        if(!strcmp(argv[1],"duplicate")){expect_guard=1;invoke(0x1000,1);}
        if(!strcmp(argv[1],"overlap")){expect_guard=1;invoke(0x1004,1);}
        if(!strcmp(argv[1],"priority")){MEM32(0x1004)=1;expect_guard=1;invoke(0x1020,1);}
        if(!strcmp(argv[1],"routine")){MEM32(0x1000)=0;expect_guard=1;invoke(0x1020,1);}
        if(!strcmp(argv[1],"links")){MEM32(0x1008)=0xdead0000;expect_guard=1;invoke(0x1020,0);}
        if(!strcmp(argv[1],"delivery")){expect_guard=1;snapshot();kernel_shutdown_delivery_guard();}
        if(!strcmp(argv[1],"reinitialize")){expect_guard=1;snapshot();kernel_shutdown_initialize(0x8000);}
        return 1;
    }
    invoke(0x1000,1);invoke(0x1020,1);invoke(0x1040,1);invoke(0x1060,2);
    const uint32_t expected[]={0x1040,0x1000,0x1060,0x1020};order(expected,4);
    CHECK(kernel_shutdown_owns_range(0x1000,1));CHECK(kernel_shutdown_owns_range(0x100f,1));
    CHECK(!kernel_shutdown_owns_range(0x1010,1));CHECK(kernel_shutdown_owns_range(0x7fff,2));
    CHECK(!kernel_shutdown_owns_range(0x1000,0));
    uint32_t oldnext=MEM32(0x1008),oldprev=MEM32(0x100c);
    invoke(0x1000,0x100); /* BOOLEAN low byte zero means unregister. */
    CHECK(MEM32(0x1008)==oldnext && MEM32(0x100c)==oldprev);
    const uint32_t removed[]={0x1040,0x1060,0x1020};order(removed,3);
    CHECK(!kernel_shutdown_owns_range(0x1000,16));
    invoke(0xdead0000,0);order(removed,3); /* absent removal does not dereference */
    invoke(0x1000,0);order(removed,3);
    invoke(0x1000,1); /* re-registration becomes newest member of its priority */
    const uint32_t reinserted[]={0x1040,0x1060,0x1000,0x1020};order(reinserted,4);
    invoke(0x1040,0);invoke(0x1000,0);invoke(0x1060,0);invoke(0x1020,0);order(NULL,0);
    kernel_shutdown_initialize(0x8000);CHECK(!callback_calls);
    const char *negative[]={"duplicate","overlap","priority","routine","links","callback","address","delivery","reinitialize","capacity"};
    for(unsigned i=0;i<sizeof(negative)/sizeof(negative[0]);++i) {
        char command[4096];STARTUPINFOA si;PROCESS_INFORMATION pi;
        memset(&si,0,sizeof(si));si.cb=sizeof(si);memset(&pi,0,sizeof(pi));
        CHECK(snprintf(command,sizeof(command),"\"%s\" %s",argv[0],negative[i])>0);
        CHECK(CreateProcessA(NULL,command,NULL,NULL,FALSE,CREATE_NO_WINDOW,NULL,NULL,&si,&pi));
        CHECK(WaitForSingleObject(pi.hProcess,5000)==WAIT_OBJECT_0);
        DWORD code;CHECK(GetExitCodeProcess(pi.hProcess,&code));CHECK(code==42);
        CloseHandle(pi.hProcess);CloseHandle(pi.hThread);
    }
    puts("native shutdown registration list/priority/ABI/guards PASS");return 0;
}
