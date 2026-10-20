/* Portable source-contract cases plus the Windows production guest dispatcher. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "network_offline.h"
static void check(int ok,const char *what){if(!ok){fprintf(stderr,"FAIL %s\n",what);exit(1);}}
struct LinkFixture {unsigned locked,starts,ends,calls,fail;uint32_t flags;};
static void acquire(void *p){struct LinkFixture *f=p;check(!f->locked,"nonrecursive owner");f->locked=1;}
static void release(void *p){struct LinkFixture *f=p;check(f->locked,"balanced owner lease");f->locked=0;}
static int32_t start(void *p){++((struct LinkFixture*)p)->starts;return 0;}
static void end(void *p){++((struct LinkFixture*)p)->ends;}
static void *create(void *p){return p;}
static void destroy(void *p,void *owner){check(owner==p,"real owner identity");}
static int query(void *p,uint32_t *flags)
{struct LinkFixture *f=p;check(f->locked,"query has lifecycle lease");++f->calls;*flags=f->flags;return !f->fail;}
static const uint32_t link_flags[]={0,1,2,3,4,7,0xfffffffeu,0xffffffffu};
static void core_cases(void)
{
    struct LinkFixture f={0};PilotNetworkOffline state={0};
    PilotNetworkOps ops={&f,acquire,release,start,end,create,destroy};
    PilotNetworkResult result=pilot_network_offline_link(&state,query,&f);
    check(result.guard==PILOT_NETWORK_GUARD_UNINITIALIZED && !f.calls,"uninitialized does not query");
    check(pilot_network_offline_initialize(&state,&ops),"initialize");
    for(unsigned phase=0;phase<5;++phase) {
        if(phase==1 || phase==2)check(!pilot_network_offline_startup(&state,0).hresult,"acquire owner");
        if(phase==3 || phase==4)check(!pilot_network_offline_cleanup(&state).hresult,"release owner");
        uint32_t references=state.references;void *owner=state.owner;
        unsigned starts=f.starts,ends=f.ends;
        for(unsigned i=0;i<sizeof(link_flags)/sizeof(*link_flags);++i) {
            f.flags=link_flags[i];unsigned calls=f.calls;
            result=pilot_network_offline_link(&state,query,&f);
            check(!result.guard && !result.set_last_error && !result.last_error,"raw query does not set LastError");
            check(result.hresult==((f.flags&1)?f.flags:0),"source-compared raw32-bit mask");
            check(!f.locked && f.calls==calls+1 && state.owner==owner && state.references==references &&
                  f.starts==starts && f.ends==ends,"query preserves lifetime and backend reference counts");
        }
    }
    result=pilot_network_offline_link(&state,NULL,NULL);
    check(result.guard==PILOT_NETWORK_GUARD_LINK && !f.locked,"missing provider guards and unlocks");
    f.fail=1;result=pilot_network_offline_link(&state,query,&f);
    check(result.guard==PILOT_NETWORK_GUARD_LINK && !result.set_last_error && !f.locked,"failed provider never publishes a link result");
    state.references=1;unsigned calls=f.calls;result=pilot_network_offline_link(&state,query,&f);
    check(result.guard==PILOT_NETWORK_GUARD_STATE && f.calls==calls && !f.locked,"invalid lifetime precedes provider");
    state.references=0;check(f.starts==2 && f.ends==2,"no startup side effects from link query");
}
#if defined(_WIN32) && !defined(PILOT_NETWORK_PORTABLE_TEST)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <setjmp.h>
#include "network_host.h"
static SRWLOCK dispatcher_lock=SRWLOCK_INIT;
static unsigned calls,fail_provider;
static uint32_t raw_flags;
static void win_acquire(void *p){(void)p;AcquireSRWLockExclusive(&dispatcher_lock);}
static void win_release(void *p){(void)p;ReleaseSRWLockExclusive(&dispatcher_lock);}
static int32_t win_start(void *p){(void)p;return 0;}
static void win_end(void *p){(void)p;}
static void *win_create(void *p){(void)p;return malloc(1);}
static void win_destroy(void *p,void *owner){(void)p;free(owner);}
const PilotNetworkOps *pilot_network_host_operations(void)
{static const PilotNetworkOps ops={NULL,win_acquire,win_release,win_start,win_end,win_create,win_destroy};return &ops;}
int pilot_network_host_link(void *p,uint32_t *flags)
{const PilotNetworkLinkPolicy *policy=p;check(policy && policy->attachment==PILOT_NETWORK_ATTACHMENT_UNATTACHED,"bound virtual policy");++calls;*flags=raw_flags;return !fail_provider;}
typedef void (*recomp_func_t)(void);
static unsigned char *memory;
static uint32_t g_eax,g_ebx,g_esi,g_edi,g_ebp,g_esp;
#define MEM32(a) (*(uint32_t*)(memory+(a)))
#define PUSH32(sp,value) ((sp)-=4,MEM32(sp)=(value))
static jmp_buf expected_guard;
static void stop(const char *message,uint32_t value)
{(void)value;if(!fail_provider){fprintf(stderr,"unexpected guard: %s\n",message);exit(1);}longjmp(expected_guard,1);}
#define PILOT_NETWORK_OFFLINE 1
#include "network_offline.inc"
static void last_error(void){check(0,"link query must not change guest LastError");}
static void frame(void)
{
    memset(memory+2048,0xa5,64);MEM32(2048)=0xfeedface;g_esp=2048;g_eax=0xabcdef;
    g_ebx=11;g_esi=12;g_edi=13;g_ebp=14;
}
static void dispatcher_cases(void)
{
    memory=VirtualAlloc(NULL,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);check(memory!=NULL,"guest fixture");
    pilot_network_initialize(NULL,NULL);
    for(unsigned phase=0;phase<3;++phase) {
        if(phase==1)check(!pilot_network_offline_startup(&network_offline,0).hresult,"dispatcher startup");
        if(phase==2)check(!pilot_network_offline_cleanup(&network_offline).hresult,"dispatcher cleanup");
        for(unsigned i=0;i<sizeof(link_flags)/sizeof(*link_flags);++i) {
            raw_flags=link_flags[i];frame();unsigned char before[64];memcpy(before,memory+2048,64);
            pilot_network_dispatch(PILOT_NETWORK_TEST_LINK,last_error);
            check(g_esp==2052 && g_eax==((raw_flags&1)?raw_flags:0),"RET0 and exact raw link flags");
            check(g_ebx==11 && g_esi==12 && g_edi==13 && g_ebp==14 && !memcmp(before,memory+2048,64),"nonvolatiles/guest memory unchanged");
        }
    }
    check(calls==3*sizeof(link_flags)/sizeof(*link_flags),"one provider snapshot per query");
    fail_provider=1;frame();
    if(!setjmp(expected_guard)) {pilot_network_dispatch(PILOT_NETWORK_TEST_LINK,last_error);check(0,"provider failure must guard");}
    check(g_eax==0xabcdef && g_esp==2048,"guard never publishes success or consumes frame");
    check(TryAcquireSRWLockExclusive(&dispatcher_lock),"provider failure releases owner lease");ReleaseSRWLockExclusive(&dispatcher_lock);
    check(VirtualFree(memory,0,MEM_RELEASE),"release guest fixture");
}
#endif
int main(void)
{
    core_cases();puts("PASS portable virtual-link mask/lifetime/error precedence, including pre-startup and post-cleanup query");
#if defined(_WIN32) && !defined(PILOT_NETWORK_PORTABLE_TEST)
    dispatcher_cases();puts("PASS production link dispatcher RET0/raw flags/nonvolatiles/no guest writes or LastError and provider-failure guard");
#endif
    return 0;
}
