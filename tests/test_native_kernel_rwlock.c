/* Authored Windows host contention and guest ABI fixture; no retail data. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned char memory[65536];
#ifdef _MSC_VER
static __declspec(thread) uint32_t g_esp;
#else
static _Thread_local uint32_t g_esp;
#endif
#define MEM32(a) (*(uint32_t *)(memory+(uint32_t)(a)))
#define CHECK(x) do {if(!(x)){fprintf(stderr,"CHECK line %d: %s\n",__LINE__,#x);ExitProcess(1);}}while(0)
static void stop(const char *reason,uint32_t value){fprintf(stderr,"guard: %s %u\n",reason,value);ExitProcess(42);}
static CRITICAL_SECTION cpu_token;
static HANDLE blocked_event;
static volatile LONG blocks,unblocks;
static int scheduler_block_begin(void){InterlockedIncrement(&blocks);LeaveCriticalSection(&cpu_token);SetEvent(blocked_event);return 1;}
static void scheduler_block_end(int blocked){CHECK(blocked);EnterCriticalSection(&cpu_token);InterlockedIncrement(&unblocks);}
#include "kernel_rwlock.inc"
static const uint32_t lock_va=0x1000;
static uint32_t g_eax=0x11223344,g_eflags=0x246;
static void invoke(unsigned ordinal,uint32_t stack)
{
    g_esp=stack;MEM32(stack)=0x12345678;MEM32(stack+4)=lock_va;
    CHECK(kernel_rwlock_dispatch(ordinal));CHECK(g_esp==stack+8);CHECK(MEM32(stack)==0x12345678);
    CHECK(g_eax==0x11223344 && g_eflags==0x246);
}
struct Work { unsigned ordinal,stack;HANDLE acquired,release; };
static DWORD WINAPI worker(void *opaque)
{
    struct Work *work=(struct Work*)opaque;
    EnterCriticalSection(&cpu_token);invoke(work->ordinal,work->stack);
    SetEvent(work->acquired);LeaveCriticalSection(&cpu_token);
    CHECK(WaitForSingleObject(work->release,5000)==WAIT_OBJECT_0);
    EnterCriticalSection(&cpu_token);invoke(28,work->stack);LeaveCriticalSection(&cpu_token);return 0;
}
static HANDLE launch_work(struct Work *work,unsigned ordinal,unsigned stack)
{
    work->ordinal=ordinal;work->stack=stack;
    work->acquired=CreateEventW(NULL,TRUE,FALSE,NULL);work->release=CreateEventW(NULL,TRUE,FALSE,NULL);
    HANDLE thread=CreateThread(NULL,0,worker,work,0,NULL);CHECK(thread);return thread;
}
static void finish_work(struct Work *work,HANDLE thread)
{
    SetEvent(work->release);CHECK(WaitForSingleObject(thread,5000)==WAIT_OBJECT_0);
    CloseHandle(thread);CloseHandle(work->acquired);CloseHandle(work->release);
}
int main(int argc,char **argv)
{
    InitializeCriticalSection(&cpu_token);blocked_event=CreateEventW(NULL,FALSE,FALSE,NULL);
    memory[lock_va-1]=0xa5;memory[lock_va+PILOT_ERWLOCK_BYTES]=0x5a;
    EnterCriticalSection(&cpu_token);invoke(18,0x2000);
    CHECK(memory[lock_va-1]==0xa5 && memory[lock_va+PILOT_ERWLOCK_BYTES]==0x5a);
    CHECK(MEM32(lock_va)==0xffffffffu && MEM32(lock_va+4)==0 && MEM32(lock_va+12)==0);
    CHECK(MEM32(lock_va+24)==lock_va+24 && MEM32(lock_va+48)==0x7fffffffu);
    CHECK(kernel_rwlock_dispatcher_object(lock_va+16));
    CHECK(kernel_rwlock_dispatcher_object(lock_va+32));
    CHECK(!kernel_rwlock_dispatcher_object(lock_va));
    if(argc>1) {
        if(!strcmp(argv[1],"exclusive-recursion")){invoke(12,0x2000);invoke(12,0x2000);}
        if(!strcmp(argv[1],"upgrade")){invoke(13,0x2000);invoke(12,0x2000);}
        if(!strcmp(argv[1],"release"))invoke(28,0x2000);
        if(!strcmp(argv[1],"mutation")){MEM32(lock_va+4)=1;invoke(12,0x2000);}
        if(!strcmp(argv[1],"reinit")){invoke(13,0x2000);invoke(18,0x2000);}
        if(!strcmp(argv[1],"reused-tid") || !strcmp(argv[1],"reused-tid-shared")) {
            DWORD tid=GetCurrentThreadId();invoke(!strcmp(argv[1],"reused-tid")?12:13,0x2000);
            uint64_t prior=kernel_rw_thread_token();
            kernel_rw_tls_token=0; /* simulate retirement/new lifetime */
            CHECK(GetCurrentThreadId()==tid && kernel_rw_thread_token()!=prior);
            invoke(28,0x2000); /* new lifetime must not inherit old ownership */
        }
        if(!strcmp(argv[1],"token-wrap")) {
            kernel_rw_token_sequence=INT64_MAX-1;kernel_rw_tls_token=0;
            CHECK(kernel_rw_thread_token()==(uint64_t)INT64_MAX);
            CHECK(kernel_rw_token_sequence==INT64_MAX);
            kernel_rw_tls_token=0;
            (void)kernel_rw_thread_token();
        }
        return 1;
    }
    CHECK(!kernel_rwlock_dispatch(340));
    /* Shared recursion without queued writer is an actual supported path. */
    invoke(13,0x2000);invoke(13,0x2000);
    CHECK(MEM32(lock_va)==1 && MEM32(lock_va+12)==2);
    invoke(28,0x2000);invoke(28,0x2000);CHECK(MEM32(lock_va)==0xffffffffu);
    /* An idle record can be reused under a new lifetime with the same OS TID. */
    DWORD tid=GetCurrentThreadId();uint64_t prior=kernel_rw_thread_token();
    kernel_rw_tls_token=0;
    CHECK(GetCurrentThreadId()==tid && kernel_rw_thread_token()!=prior);
    invoke(12,0x2000);invoke(28,0x2000);CHECK(MEM32(lock_va)==0xffffffffu);
    /* Queue a writer behind the current reader. Blocking must surrender the
     * sole CPU token, otherwise this main thread cannot run the release. */
    invoke(13,0x2000);
    struct Work writer,reader;HANDLE wt=launch_work(&writer,12,0x2100);
    LeaveCriticalSection(&cpu_token);
    CHECK(WaitForSingleObject(blocked_event,5000)==WAIT_OBJECT_0);
    EnterCriticalSection(&cpu_token);
    CHECK(MEM32(lock_va+4)==1 && MEM32(lock_va+12)==1);
    /* A later reader must queue behind that writer, not barge into readers. */
    HANDLE rt=launch_work(&reader,13,0x2200);LeaveCriticalSection(&cpu_token);
    CHECK(WaitForSingleObject(blocked_event,5000)==WAIT_OBJECT_0);
    EnterCriticalSection(&cpu_token);CHECK(MEM32(lock_va+8)==1);
    invoke(28,0x2000);LeaveCriticalSection(&cpu_token);
    CHECK(WaitForSingleObject(writer.acquired,5000)==WAIT_OBJECT_0);
    CHECK(WaitForSingleObject(reader.acquired,0)==WAIT_TIMEOUT);
    finish_work(&writer,wt);
    CHECK(WaitForSingleObject(reader.acquired,5000)==WAIT_OBJECT_0);
    finish_work(&reader,rt);
    EnterCriticalSection(&cpu_token);
    CHECK(MEM32(lock_va)==0xffffffffu && MEM32(lock_va+12)==0 && MEM32(lock_va+4)==0 && MEM32(lock_va+8)==0);
    CHECK(blocks==2 && unblocks==2);
    invoke(18,0x2000);
    /* Releasing an exclusive owner grants all queued readers before a later
     * writer, matching the original release branch rather than SRW fairness. */
    invoke(12,0x2000);
    struct Work r1,r2,w2;
    HANDLE r1t=launch_work(&r1,13,0x2300);LeaveCriticalSection(&cpu_token);
    CHECK(WaitForSingleObject(blocked_event,5000)==WAIT_OBJECT_0);
    EnterCriticalSection(&cpu_token);
    HANDLE r2t=launch_work(&r2,13,0x2400);LeaveCriticalSection(&cpu_token);
    CHECK(WaitForSingleObject(blocked_event,5000)==WAIT_OBJECT_0);
    EnterCriticalSection(&cpu_token);
    HANDLE w2t=launch_work(&w2,12,0x2500);LeaveCriticalSection(&cpu_token);
    CHECK(WaitForSingleObject(blocked_event,5000)==WAIT_OBJECT_0);
    EnterCriticalSection(&cpu_token);CHECK(MEM32(lock_va+8)==2 && MEM32(lock_va+4)==1);
    invoke(28,0x2000);LeaveCriticalSection(&cpu_token);
    CHECK(WaitForSingleObject(r1.acquired,5000)==WAIT_OBJECT_0);
    CHECK(WaitForSingleObject(r2.acquired,5000)==WAIT_OBJECT_0);
    CHECK(WaitForSingleObject(w2.acquired,0)==WAIT_TIMEOUT);
    finish_work(&r1,r1t);CHECK(WaitForSingleObject(w2.acquired,0)==WAIT_TIMEOUT);
    finish_work(&r2,r2t);CHECK(WaitForSingleObject(w2.acquired,5000)==WAIT_OBJECT_0);
    finish_work(&w2,w2t);
    EnterCriticalSection(&cpu_token);CHECK(MEM32(lock_va)==0xffffffffu);
    CHECK(blocks==5 && unblocks==5);LeaveCriticalSection(&cpu_token);
    /* Run guards in isolated children: stop() is fatal in the real runtime,
     * and deliberately does not attempt recovery after a corrupted object. */
    const char *negative[]={"exclusive-recursion","upgrade","release","mutation","reinit","reused-tid","reused-tid-shared","token-wrap"};
    for(unsigned i=0;i<sizeof(negative)/sizeof(negative[0]);++i) {
        char command[4096];STARTUPINFOA si;PROCESS_INFORMATION pi;
        memset(&si,0,sizeof(si));si.cb=sizeof(si);memset(&pi,0,sizeof(pi));
        CHECK(snprintf(command,sizeof(command),"\"%s\" %s",argv[0],negative[i])>0);
        CHECK(CreateProcessA(NULL,command,NULL,NULL,FALSE,CREATE_NO_WINDOW,NULL,NULL,&si,&pi));
        CHECK(WaitForSingleObject(pi.hProcess,5000)==WAIT_OBJECT_0);
        DWORD code;CHECK(GetExitCodeProcess(pi.hProcess,&code));CHECK(code==42);
        CloseHandle(pi.hProcess);CloseHandle(pi.hThread);
    }
    puts("native ERWLOCK ABI/contention/guards PASS");return 0;
}
