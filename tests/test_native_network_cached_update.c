/* Actual production guest dispatcher/core; authored absent-cache callback.
 * No original cache engine, filesystem writes, sockets, NIC or title run. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include "network_host.h"
static void check(int condition,const char *message){if(!condition){fprintf(stderr,"FAIL %s\n",message);exit(1);}}
static SRWLOCK lifecycle_lock=SRWLOCK_INIT;
static unsigned starts,cleanups,creates,destroys;
static void lock(void *p){(void)p;AcquireSRWLockExclusive(&lifecycle_lock);}
static void unlock(void *p){(void)p;ReleaseSRWLockExclusive(&lifecycle_lock);}
static int32_t startup(void *p){(void)p;++starts;return 0;}
static void cleanup(void *p){(void)p;++cleanups;}
static void *create(void *p){(void)p;++creates;return malloc(1);}
static void destroy(void *p,void *owner){(void)p;++destroys;free(owner);}
static const PilotNetworkOps test_ops={0,lock,unlock,startup,cleanup,create,destroy};
const PilotNetworkOps *pilot_network_host_operations(void){return &test_ops;}
typedef void (*recomp_func_t)(void);
static uint32_t g_eax,g_ebx,g_esi,g_edi,g_esp;
static unsigned char *memory;
#define MEM32(a) (*(uint32_t *)(memory+(a)))
#define PUSH32(sp,value) ((sp)-=4,MEM32(sp)=(value))
static jmp_buf guard_jump;static unsigned expected_guard,guards;
static void stop(const char *message,uint32_t value)
{
    (void)value;if(!expected_guard){fprintf(stderr,"unexpected guard: %s\n",message);exit(1);}
    ++guards;longjmp(guard_jump,1);
}
#define PILOT_NETWORK_OFFLINE 1
#include "network_offline.inc"
struct Probe {uint32_t status,output;unsigned calls,mutate,block;HANDLE entered,release;};
static uint32_t probe(void *context,unsigned partition,const char *relative)
{
    struct Probe *p=context;++p->calls;
    check(partition==1 && !strcmp(relative,"CACHE\\LocalCache08.bin"),"profile-selected mounted namespace");
    for(unsigned i=0;i<156;++i)check(!memory[p->output+i],"complete zero precedes probe");
    if(p->mutate)memory[p->output+7]=0xa5;
    if(p->block){SetEvent(p->entered);check(WaitForSingleObject(p->release,30000)==WAIT_OBJECT_0,"release held query");}
    return p->status;
}
static void set_last_error(void){check(0,"query must not invoke guest LastError helper");}
static void invoke(unsigned entry,uint32_t argument)
{
    g_esp=2048;MEM32(g_esp)=0xfeed;MEM32(g_esp+4)=argument;g_ebx=11;g_esi=12;g_edi=13;
    pilot_network_dispatch(entry,set_last_error);
    check(g_esp==2048u+4u+(entry==PILOT_NETWORK_TEST_CLEANUP?0u:4u),"original RET cleanup");
    check(g_ebx==11 && g_esi==12 && g_edi==13,"preserve guest nonvolatiles");
}
static void query_guard(uint32_t output)
{
    expected_guard=1;unsigned previous=guards;
    if(!setjmp(guard_jump))invoke(PILOT_NETWORK_TEST_QUERY,output);
    expected_guard=0;check(guards==previous+1,"expected query guard");
    check(TryAcquireSRWLockExclusive(&lifecycle_lock),"all guards release owner lease");ReleaseSRWLockExclusive(&lifecycle_lock);
}
static int clear_for_worker(void *context,unsigned bytes)
{uint32_t output=*(uint32_t*)context;return network_clear_cached_update(&output,bytes);}
struct Worker {struct Probe *probe;PilotNetworkResult result;HANDLE done;};
static DWORD WINAPI query_worker(void *context)
{
    struct Worker *w=context;w->result=pilot_network_offline_cached_update(&network_offline,
        clear_for_worker,&w->probe->output,probe,w->probe,1,"CACHE\\LocalCache08.bin");SetEvent(w->done);return 0;
}
static DWORD WINAPI cleanup_worker(void *context)
{struct Worker *w=context;w->result=pilot_network_offline_cleanup(&network_offline);SetEvent(w->done);return 0;}
int main(void)
{
    memory=VirtualAlloc(NULL,8192,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);check(memory!=NULL,"guest fixture mapping");
    struct Probe p={0xc0000034u,512,0,0,0,NULL,NULL};pilot_network_initialize(probe,&p);
    memset(memory+508,0xcd,164);invoke(PILOT_NETWORK_TEST_QUERY,0);
    check(g_eax==0x80150005u && !p.calls && memory[508]==0xcd && memory[512]==0xcd,"missing owner precedes NULL output/probe");
    invoke(PILOT_NETWORK_TEST_QUERY,0xfffffff0u);check(g_eax==0x80150005u && !p.calls,"missing owner precedes overflowing output");
    invoke(PILOT_NETWORK_TEST_STARTUP,0);check(!g_eax,"real lifecycle owner");
    for(unsigned i=0;i<2;++i) {
        p.status=i?0xc000003au:0xc0000034u;memset(memory+508,0xcd,164);invoke(PILOT_NETWORK_TEST_QUERY,p.output);
        check(g_eax==1 && memory[508]==0xcd && memory[668]==0xcd,"absent result and exact no-tail clear");
        for(unsigned j=0;j<156;++j)check(!memory[512+j],"156-byte result");
    }
    unsigned calls=p.calls;query_guard(0);query_guard(0xfffffff0u);check(p.calls==calls,"invalid output never probes");
    DWORD old;memset(memory+3996,0xcd,100);check(VirtualProtect(memory+4096,4096,PAGE_NOACCESS,&old),"protect trailing page");
    query_guard(3996);for(unsigned i=0;i<100;++i)check(memory[3996+i]==0xcd,"mapped preflight prevents partial writes");
    check(VirtualProtect(memory+4096,4096,PAGE_READWRITE,&old),"restore trailing page");check(p.calls==calls,"unmapped output never probes");
    const uint32_t failures[]={0,0xc0000022u,0xc000050bu,0x80000005u};
    for(unsigned i=0;i<sizeof(failures)/sizeof(failures[0]);++i) {
        p.status=failures[i];memset(memory+508,0xcd,164);query_guard(p.output);
        for(unsigned j=0;j<156;++j)check(!memory[512+j],"initialized unsupported probe retains cleared output");
        check(memory[508]==0xcd && memory[668]==0xcd,"guard no-tail writes");
    }
    p.status=0xc0000034u;p.output=2052;memset(memory+2047,0xcd,162);invoke(PILOT_NETWORK_TEST_QUERY,p.output);
    check(g_eax==1 && memory[2047]==0xcd && memory[2208]==0xcd,"output aliases incoming argument after pointer snapshot");
    p.output=512;p.mutate=1;memset(memory+508,0xcd,164);invoke(PILOT_NETWORK_TEST_QUERY,p.output);
    check(g_eax==1 && memory[519]==0xa5,"no post-probe clear undoes callback/concurrent mutation");p.mutate=0;
    p.block=1;p.entered=CreateEventA(NULL,TRUE,FALSE,NULL);p.release=CreateEventA(NULL,TRUE,FALSE,NULL);
    struct Worker q={&p,{0},CreateEventA(NULL,TRUE,FALSE,NULL)},c={&p,{0},CreateEventA(NULL,TRUE,FALSE,NULL)};
    HANDLE qt=CreateThread(NULL,0,query_worker,&q,0,NULL);check(qt && WaitForSingleObject(p.entered,30000)==WAIT_OBJECT_0,"query holds borrowed owner");
    HANDLE ct=CreateThread(NULL,0,cleanup_worker,&c,0,NULL);check(ct!=NULL,"cleanup contender");
    check(WaitForSingleObject(c.done,0)==WAIT_TIMEOUT && !destroys,"cleanup cannot retire live query view");
    SetEvent(p.release);check(WaitForSingleObject(qt,30000)==WAIT_OBJECT_0 && WaitForSingleObject(ct,30000)==WAIT_OBJECT_0,"query and cleanup complete");
    check(!q.result.guard && q.result.hresult==1 && !c.result.guard && !c.result.hresult && destroys==1,"view releases before final retirement");
    CloseHandle(qt);CloseHandle(ct);CloseHandle(q.done);CloseHandle(c.done);CloseHandle(p.entered);CloseHandle(p.release);
    calls=p.calls;memset(memory+508,0xcd,164);invoke(PILOT_NETWORK_TEST_QUERY,0);
    check(g_eax==0x80150005u && p.calls==calls && memory[512]==0xcd && starts==cleanups && creates==destroys,"final owner absence and balanced lifetime");
    check(VirtualFree(memory,0,MEM_RELEASE),"release guest fixture");
    printf("PASS cached-update production dispatcher/core:156-byte output, alias, absence, %u guards, query/cleanup lifetime; no cache engine IO\n",guards);return 0;
}
