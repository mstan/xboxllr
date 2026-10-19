/* Authored clock arithmetic, guest ABI and independent publisher fixture. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "kernel_clock.h"
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line%d: %s\n",__LINE__,#x);ExitProcess(1);}}while(0)
static unsigned char memory[65536];
#define MEM32(a) (*(volatile uint32_t *)(memory+(uint32_t)(a)))
static uint32_t g_eax,g_edx,g_esp;
static int manual_clock=1,fail_thread,fail_timer,expected_guard;
static volatile LONG64 manual_now;
static int guard_state_valid(void);
static void stop(const char *reason,uint32_t value)
{
    if(!expected_guard || !guard_state_valid())ExitProcess(43);
    fprintf(stderr,"guard:%s/%u\n",reason,value);ExitProcess(42);
}
static uint64_t dispatch_monotonic(void)
{
    if(manual_clock)return (uint64_t)InterlockedCompareExchange64(&manual_now,0,0);
    LARGE_INTEGER q,f;QueryPerformanceCounter(&q);QueryPerformanceFrequency(&f);
    return (uint64_t)(q.QuadPart/f.QuadPart)*10000000+
           (uint64_t)(q.QuadPart%f.QuadPart)*10000000/(uint64_t)f.QuadPart;
}
static HANDLE test_create_thread(LPSECURITY_ATTRIBUTES attributes,SIZE_T stack,LPTHREAD_START_ROUTINE entry,LPVOID argument,DWORD flags,LPDWORD id)
{
    if(fail_thread){SetLastError(ERROR_NOT_ENOUGH_MEMORY);return NULL;}
    return CreateThread(attributes,stack,entry,argument,flags,id);
}
static BOOL test_set_timer(HANDLE timer,const LARGE_INTEGER *due,LONG period,PTIMERAPCROUTINE apc,LPVOID context,BOOL resume)
{
    if(fail_timer){SetLastError(ERROR_INVALID_HANDLE);return FALSE;}
    return SetWaitableTimer(timer,due,period,apc,context,resume);
}
#define CreateThread test_create_thread
#define SetWaitableTimer test_set_timer
#define PILOT_KERNEL_CLOCK_NATIVE 1
#include "kernel_clock.inc"
#undef CreateThread
#undef SetWaitableTimer
static int guard_state_valid(void)
{
    if(fail_thread)return kernel_clock_phase==KERNEL_CLOCK_OFF && MEM32(0x1000)==0xa5a5a5a5u;
    if(fail_timer)return kernel_clock_phase==KERNEL_CLOCK_FAULT;
    return 1;
}
static void invoke(unsigned ordinal,uint64_t expected)
{
    g_esp=0x2000;MEM32(g_esp)=0x12345678;
    CHECK(kernel_clock_dispatch(ordinal));CHECK(g_esp==0x2004 && MEM32(0x2000)==0x12345678);
    CHECK((((uint64_t)g_edx<<32)|g_eax)==expected);
}
static void arithmetic(void)
{
    PilotKernelClock c={0};uint64_t value=123;
    CHECK(pilot_kernel_clock_initialize(&c,100));
    CHECK(!pilot_kernel_clock_initialize(&c,200));
    CHECK(pilot_kernel_clock_sample(&c,99)==0);
    CHECK(pilot_kernel_clock_sample(&c,100+10000000)==10000000);
    CHECK(pilot_kernel_clock_sample(&c,100)==10000000); /* no backwards sample */
    CHECK(pilot_kernel_clock_scale(10000000,PILOT_CLOCK_PM_HZ,&value) && value==3375000);
    CHECK(pilot_kernel_clock_scale(10000000,PILOT_CLOCK_TSC_HZ,&value) && value==733333333);
    CHECK(pilot_kernel_clock_scale(5000000,PILOT_CLOCK_PM_HZ,&value) && value==1687500);
    CHECK(pilot_kernel_clock_scale(5000000,PILOT_CLOCK_TSC_HZ,&value) && value==366666666);
    CHECK(pilot_kernel_clock_scale(1,PILOT_CLOCK_PM_HZ,&value) && value==0);
    CHECK(pilot_kernel_clock_scale(1,PILOT_CLOCK_TSC_HZ,&value) && value==73);
    CHECK(pilot_kernel_clock_scale(3,PILOT_CLOCK_PM_HZ,&value) && value==1);
    CHECK(pilot_kernel_clock_scale(3,PILOT_CLOCK_TSC_HZ,&value) && value==219);
    value=123;CHECK(!pilot_kernel_clock_scale(UINT64_MAX,UINT32_MAX,&value) && value==123);
    CHECK(pilot_kernel_clock_next_tick(&c)==10000100+10000);
    c.latest=UINT64_MAX;CHECK(pilot_kernel_clock_next_tick(&c)==UINT64_MAX);
}
int main(int argc,char **argv)
{
    arithmetic();memset(memory,0xa5,sizeof(memory));manual_now=100000000;
    if(argc>1) {
        if(!strcmp(argv[1],"real-poll")) {
            manual_clock=0;kernel_clock_initialize(0x1000,dispatch_monotonic());
            uint64_t deadline=GetTickCount64()+5000;
            while(!MEM32(0x1000) && GetTickCount64()<deadline){} /* no guest APIs/safe points */
            CHECK(MEM32(0x1000)>0);kernel_clock_shutdown();
            MEM32(0x1000)=0xdeadbeef;Sleep(20);CHECK(MEM32(0x1000)==0xdeadbeef);
            return 0;
        }
        expected_guard=1;
        if(!strcmp(argv[1],"create-failure"))fail_thread=1;
        if(!strcmp(argv[1],"timer-failure"))fail_timer=1;
        if(!strcmp(argv[1],"address")){kernel_clock_initialize(0x1001,100000000);return 1;}
        if(!strcmp(argv[1],"future")){kernel_clock_initialize(0x1000,100000001);return 1;}
        if(!strcmp(argv[1],"before-init")){(void)kernel_clock_tsc_counter();return 1;}
        kernel_clock_initialize(0x1000,100000000);
        if(!strcmp(argv[1],"reinitialize"))kernel_clock_initialize(0x1000,100000000);
        if(!strcmp(argv[1],"after-stop")){kernel_clock_shutdown();(void)kernel_clock_tsc_counter();}
        return 1;
    }
    kernel_clock_initialize(0x1000,100000000);
    CHECK(MEM32(0x1000)==0 && memory[0xfff]==0xa5 && memory[0x1004]==0xa5);
    CHECK(!kernel_clock_dispatch(99) && !kernel_clock_dispatch(238));
    invoke(127,3375000);invoke(126,0);CHECK(kernel_clock_tsc_counter()==0);
    InterlockedExchange64(&manual_now,100000000+INT64_C(20000000000));
    invoke(126,UINT64_C(6750000000));CHECK(g_edx==1);
    CHECK(kernel_clock_tsc_counter()==UINT64_C(1466666666000));CHECK(MEM32(0x1000)==2000000);
    uint64_t wrapped=(UINT64_C(0x100000000)+7)*10000;
    InterlockedExchange64(&manual_now,(LONG64)(100000000+wrapped));
    (void)kernel_clock_tsc_counter();CHECK(MEM32(0x1000)==7);
    InterlockedExchange64(&manual_now,100000000);(void)kernel_clock_tsc_counter();CHECK(MEM32(0x1000)==7);
    kernel_clock_shutdown();kernel_clock_shutdown();CHECK(kernel_clock_phase==KERNEL_CLOCK_STOPPED);
    MEM32(0x1000)=0xdeadbeef;Sleep(20);CHECK(MEM32(0x1000)==0xdeadbeef);
    const char *cases[]={"real-poll","create-failure","timer-failure","address","future","before-init","reinitialize","after-stop"};
    for(unsigned i=0;i<sizeof(cases)/sizeof(cases[0]);++i) {
        char command[4096];STARTUPINFOA si;PROCESS_INFORMATION pi;
        memset(&si,0,sizeof(si));si.cb=sizeof(si);memset(&pi,0,sizeof(pi));
        CHECK(snprintf(command,sizeof(command),"\"%s\" %s",argv[0],cases[i])>0);
        CHECK(CreateProcessA(NULL,command,NULL,NULL,FALSE,CREATE_NO_WINDOW,NULL,NULL,&si,&pi));
        CHECK(WaitForSingleObject(pi.hProcess,10000)==WAIT_OBJECT_0);
        DWORD code;CHECK(GetExitCodeProcess(pi.hProcess,&code));CHECK(code==(i?42u:0u));
        CloseHandle(pi.hProcess);CloseHandle(pi.hThread);
    }
    puts("native clock arithmetic/ABI/independent publication/lifetime PASS");return 0;
}
