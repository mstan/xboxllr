/* Portable injected core checks; Windows additionally exercises the actual
 * guest dispatcher/preflight and a real cleanup contender, without sockets. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "network_offline.h"
static void check(int ok,const char *what){if(!ok){fprintf(stderr,"FAIL %s\n",what);exit(1);}}
struct Fixture {unsigned held,starts,ends,live,stages,entropy,commits,fail_stage,fail_entropy;unsigned char bytes[66];};
static void lock_core(void *p){struct Fixture *f=p;check(!f->held,"nonrecursive lifetime lock");f->held=1;}
static void unlock_core(void *p){struct Fixture *f=p;check(f->held,"balanced lock");f->held=0;}
static int32_t start_core(void *p){struct Fixture *f=p;++f->starts;return 0;}
static void end_core(void *p){struct Fixture *f=p;++f->ends;}
static void *create_core(void *p){struct Fixture *f=p;f->live=1;return f;}
static void destroy_core(void *p,void *owner){struct Fixture *f=p;check(f==owner && f->held,"retire owned object under lock");f->live=0;}
static int stage_core(void *p,unsigned bytes,unsigned char *output)
{struct Fixture *f=p;check(f->held && f->live,"stage owns lifecycle lease");++f->stages;if(f->fail_stage)return 0;memcpy(output,f->bytes+1,bytes);return 1;}
static int entropy_core(void *p,unsigned bytes,unsigned char *output)
{struct Fixture *f=p;check(f->held && f->live,"entropy owns lifecycle lease");++f->entropy;for(unsigned i=0;i<bytes;++i)output[i]=(unsigned char)(i+1);return !f->fail_entropy;}
static void commit_core(void *p,unsigned bytes,const unsigned char *output)
{struct Fixture *f=p;check(f->held && f->live,"commit owns lifecycle lease");++f->commits;memcpy(f->bytes+1,output,bytes);}
static PilotNetworkResult random_core(PilotNetworkOffline *s,struct Fixture *f,unsigned bytes)
{return pilot_network_offline_random(s,bytes,stage_core,commit_core,f,entropy_core,f);}
static void core_cases(void)
{
    struct Fixture f={0};PilotNetworkOps ops={&f,lock_core,unlock_core,start_core,end_core,create_core,destroy_core};
    PilotNetworkOffline s={0};check(pilot_network_offline_initialize(&s,&ops),"core initialize");
    PilotNetworkResult r=pilot_network_offline_random(&s,UINT32_MAX,NULL,NULL,NULL,NULL,NULL);
    check(r.hresult==0x276d && !r.guard && !r.set_last_error,"owner-first raw WSA error");
    check(!pilot_network_offline_startup(&s,0).hresult,"startup owner");
    r=pilot_network_offline_random(&s,0,NULL,NULL,NULL,NULL,NULL);check(!r.hresult && !r.guard,"zero length ignores callbacks/pointer");
    memset(f.bytes,0xa5,sizeof(f.bytes));r=random_core(&s,&f,64);
    check(!r.hresult && !r.guard && !r.set_last_error && !f.held,"full supported extent");
    check(f.bytes[0]==0xa5 && f.bytes[65]==0xa5,"no-tail mutation");
    for(unsigned i=0;i<64;++i)check(f.bytes[i+1]==(unsigned char)(0xa5^(i+1)),"exact XOR");
    unsigned char before[66];memcpy(before,f.bytes,66);unsigned old_entropy=f.entropy,old_commits=f.commits;
    r=random_core(&s,&f,65);check(r.guard==PILOT_NETWORK_GUARD_RANDOM_SIZE && !memcmp(before,f.bytes,66) && f.entropy==old_entropy,"oversized unchanged output");
    f.fail_stage=1;r=random_core(&s,&f,4);check(r.guard==PILOT_NETWORK_GUARD_OUTPUT && f.entropy==old_entropy && !memcmp(before,f.bytes,66),"stage failure before entropy/writes");
    f.fail_stage=0;f.fail_entropy=1;r=random_core(&s,&f,4);
    check(r.guard==PILOT_NETWORK_GUARD_ENTROPY && !r.set_last_error && !memcmp(before,f.bytes,66) && f.commits==old_commits && !f.held,"partial entropy failure discards staging and unlocks");
    check(!pilot_network_offline_cleanup(&s).hresult && !f.live && f.starts==f.ends,"balanced retirement after errors");
    r=random_core(&s,&f,4);check(r.hresult==0x276d && !r.guard,"retired owner does not generate");
}
#if defined(_WIN32) && !defined(PILOT_NETWORK_PORTABLE_TEST)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <setjmp.h>
#include "network_host.h"
static SRWLOCK win_lock=SRWLOCK_INIT;
static unsigned destroys,entropy_calls,entropy_failure,block_entropy;
static HANDLE entropy_entered,entropy_release;
static void win_acquire(void *p){(void)p;AcquireSRWLockExclusive(&win_lock);}
static void win_release(void *p){(void)p;ReleaseSRWLockExclusive(&win_lock);}
static int32_t win_start(void *p){(void)p;return 0;}
static void win_end(void *p){(void)p;}
static void *win_create(void *p){(void)p;return malloc(1);}
static void win_destroy(void *p,void *owner){(void)p;++destroys;free(owner);}
const PilotNetworkOps *pilot_network_host_operations(void)
{static const PilotNetworkOps ops={NULL,win_acquire,win_release,win_start,win_end,win_create,win_destroy};return &ops;}
int pilot_network_host_entropy(void *p,unsigned bytes,unsigned char *output)
{
    (void)p;++entropy_calls;
    if(block_entropy){SetEvent(entropy_entered);check(WaitForSingleObject(entropy_release,30000)==WAIT_OBJECT_0,"release entropy");}
    for(unsigned i=0;i<bytes;++i)output[i]=(unsigned char)(i+1);
    return !entropy_failure;
}
typedef void (*recomp_func_t)(void);
static unsigned char *memory;
static uint32_t g_eax,g_ebx,g_esi,g_edi,g_esp;
#define MEM32(a) (*(uint32_t*)(memory+(a)))
#define PUSH32(sp,value) ((sp)-=4,MEM32(sp)=(value))
static jmp_buf guard_jump;static unsigned expected_guard,guards;
static void stop(const char *message,uint32_t value)
{(void)value;if(!expected_guard){fprintf(stderr,"unexpected %s\n",message);exit(1);}++guards;longjmp(guard_jump,1);}
#define PILOT_NETWORK_OFFLINE 1
#include "network_offline.inc"
static void last_error(void){check(0,"random must not change guest LastError");}
static void invoke(uint32_t output,uint32_t bytes)
{
    g_esp=2048;MEM32(g_esp)=0xfeed;MEM32(g_esp+4)=output;MEM32(g_esp+8)=bytes;
    g_ebx=11;g_esi=12;g_edi=13;pilot_network_dispatch(PILOT_NETWORK_TEST_RANDOM,last_error);
    check(g_esp==2060 && g_ebx==11 && g_esi==12 && g_edi==13,"RET8 and nonvolatiles");
}
static void guarded(uint32_t output,uint32_t bytes)
{
    expected_guard=1;unsigned before=guards;
    if(!setjmp(guard_jump))invoke(output,bytes);
    expected_guard=0;check(guards==before+1,"explicit guard");
    check(TryAcquireSRWLockExclusive(&win_lock),"guard releases lifecycle lock");ReleaseSRWLockExclusive(&win_lock);
}
struct Worker {HANDLE done;PilotNetworkResult result;};
static DWORD WINAPI random_worker(void *p)
{struct Worker *w=p;uint32_t output=512;w->result=pilot_network_offline_random(&network_offline,4,network_random_stage,network_random_commit,&output,pilot_network_host_entropy,NULL);SetEvent(w->done);return 0;}
static DWORD WINAPI cleanup_worker(void *p)
{struct Worker *w=p;w->result=pilot_network_offline_cleanup(&network_offline);SetEvent(w->done);return 0;}
static void dispatcher_cases(void)
{
    memory=VirtualAlloc(NULL,8192,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);check(memory!=NULL,"guest fixture");
    pilot_network_initialize(NULL,NULL);invoke(0,UINT32_MAX);check(g_eax==0x276d && !entropy_calls,"missing owner before bad arguments");
    check(!pilot_network_offline_startup(&network_offline,0).hresult,"dispatcher owner");
    invoke(0,0);check(!g_eax && !entropy_calls,"zero/null no entropy");
    memset(memory+508,0xa5,72);invoke(512,64);check(!g_eax && memory[511]==0xa5 && memory[576]==0xa5,"exact dispatcher extent");
    for(unsigned i=0;i<64;++i)check(memory[512+i]==(unsigned char)(0xa5^(i+1)),"dispatcher XOR");
    unsigned old=entropy_calls;guarded(512,65);guarded(0,4);guarded(0xfffffff0,64);check(entropy_calls==old,"invalid/oversized never consumes entropy");
    DWORD protection;memset(memory+4080,0xa5,16);check(VirtualProtect(memory+4096,4096,PAGE_NOACCESS,&protection),"guard tail");
    guarded(4080,64);for(unsigned i=0;i<16;++i)check(memory[4080+i]==0xa5,"cross-page preflight prevents partial write");
    check(VirtualProtect(memory+4096,4096,PAGE_READONLY,&protection),"readonly tail");guarded(4096,4);
    check(VirtualProtect(memory+4096,4096,PAGE_READWRITE,&protection),"restore tail");
    entropy_failure=1;memset(memory+512,0xa5,4);guarded(512,4);check(MEM32(512)==0xa5a5a5a5,"entropy failure no mutation");entropy_failure=0;
    /* Output aliases BOTH incoming pointer/count slots. Both were snapshotted. */
    invoke(2052,8);check(!g_eax && MEM32(2052)==(2052u^0x04030201u) && MEM32(2056)==(8u^0x08070605u),"aliased argument buffer");
    block_entropy=1;entropy_entered=CreateEventA(NULL,TRUE,FALSE,NULL);entropy_release=CreateEventA(NULL,TRUE,FALSE,NULL);
    struct Worker q={CreateEventA(NULL,TRUE,FALSE,NULL),{0}},c={CreateEventA(NULL,TRUE,FALSE,NULL),{0}};
    HANDLE qt=CreateThread(NULL,0,random_worker,&q,0,NULL);check(qt && WaitForSingleObject(entropy_entered,30000)==WAIT_OBJECT_0,"held entropy lease");
    HANDLE ct=CreateThread(NULL,0,cleanup_worker,&c,0,NULL);check(ct && WaitForSingleObject(c.done,0)==WAIT_TIMEOUT && !destroys,"cleanup excluded during staging/entropy/commit");
    SetEvent(entropy_release);check(WaitForSingleObject(qt,30000)==WAIT_OBJECT_0 && WaitForSingleObject(ct,30000)==WAIT_OBJECT_0,"workers finish");
    check(!q.result.guard && !q.result.hresult && !c.result.guard && destroys==1,"commit precedes final retirement");
    CloseHandle(qt);CloseHandle(ct);CloseHandle(q.done);CloseHandle(c.done);CloseHandle(entropy_entered);CloseHandle(entropy_release);
    check(VirtualFree(memory,0,MEM_RELEASE),"release guest fixture");
}
#endif
int main(void)
{
    core_cases();
    puts("PASS portable bounded random core: XOR, owner/error precedence, failed entropy and lease ordering");
#if defined(_WIN32) && !defined(PILOT_NETWORK_PORTABLE_TEST)
    dispatcher_cases();
    puts("PASS Windows production random dispatcher: RET8, alias, page preflight and cleanup contention");
#endif
    return 0;
}
