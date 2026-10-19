/* Authored lifecycle/backend failures and the production guest dispatcher.
 * No original title, NIC, socket traffic, keys or guest private objects. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <setjmp.h>
#include "network_host.h"
static void check(int condition,const char *message){if(!condition){fprintf(stderr,"FAIL %s\n",message);exit(1);}}
static SRWLOCK test_lock=SRWLOCK_INIT;
static unsigned starts,cleanups,creates,destroys,fail_create;
static int32_t startup_error;
static void lock(void *p){(void)p;AcquireSRWLockExclusive(&test_lock);}
static void unlock(void *p){(void)p;ReleaseSRWLockExclusive(&test_lock);}
static int32_t startup(void *p){(void)p;++starts;return startup_error;}
static void cleanup(void *p){(void)p;++cleanups;}
static void *create(void *p){(void)p;++creates;return fail_create ? NULL : malloc(1);}
static void destroy(void *p,void *owner){(void)p;++destroys;free(owner);}
static const PilotNetworkOps test_ops={0,lock,unlock,startup,cleanup,create,destroy};
const PilotNetworkOps *pilot_network_host_operations(void){return &test_ops;}
typedef void (*recomp_func_t)(void);
static uint32_t g_eax,g_ebx,g_esi,g_edi,g_esp;
static unsigned char guest[4096];
static unsigned char *memory=guest;
#define MEM32(a) (*(uint32_t *)(guest+(a)))
#define PUSH32(sp,value) ((sp)-=4,MEM32(sp)=(value))
static jmp_buf guard_jump;static unsigned expected_guard,guard_count;
static void stop(const char *message,uint32_t value)
{
    (void)value;if(!expected_guard){fprintf(stderr,"unexpected guard: %s\n",message);exit(1);}
    ++guard_count;longjmp(guard_jump,1);
}
#define PILOT_NETWORK_OFFLINE 1
#include "network_offline.inc"
static uint32_t guest_error;static unsigned error_calls;
static void set_last_error(void){++error_calls;guest_error=MEM32(g_esp+4);g_eax=0xbad;g_esp+=8;}
static void invoke(unsigned entry,uint32_t parameter)
{
    g_esp=2048;MEM32(g_esp)=0xfeed;MEM32(g_esp+4)=parameter;
    g_ebx=0xb;g_esi=0xc;g_edi=0xd;
    pilot_network_dispatch(entry,set_last_error);
    check(g_ebx==0xb && g_esi==0xc && g_edi==0xd,"nonvolatile guest registers");
    check(g_esp==2048u+4u+(entry==PILOT_NETWORK_TEST_STARTUP?4u:0u),"guest RET cleanup");
}
static void expect_guard(unsigned entry,uint32_t parameter)
{
    unsigned old_starts=starts,old_cleanup=cleanups;uint32_t old_refs=network_offline.references;
    expected_guard=1;if(!setjmp(guard_jump))invoke(entry,parameter);expected_guard=0;
    check(starts==old_starts && cleanups==old_cleanup && network_offline.references==old_refs,"guard precedes owner/backend mutation");
}
static DWORD WINAPI contender(void *unused)
{
    (void)unused;
    for(unsigned i=0;i<200;++i){PilotNetworkResult a=pilot_network_offline_startup(&network_offline,0);check(!a.guard && !a.hresult,"concurrent startup");
        PilotNetworkResult b=pilot_network_offline_cleanup(&network_offline);check(!b.guard && !b.hresult,"concurrent cleanup");}
    return 0;
}
int main(void)
{
    expect_guard(PILOT_NETWORK_TEST_STARTUP,0);pilot_network_initialize(NULL,NULL);
    expect_guard(PILOT_NETWORK_TEST_GUARD,0);expect_guard(PILOT_NETWORK_COUNT,0);expect_guard(PILOT_NETWORK_TEST_STARTUP,1);
    invoke(PILOT_NETWORK_TEST_CLEANUP,0);check(g_eax==0x80150005u && !cleanups,"cleanup before startup");
    guest_error=0x12345678;invoke(PILOT_NETWORK_TEST_STARTUP,0);
    check(!g_eax && network_offline.references==1 && creates==1 && guest_error==0x12345678 && !error_calls,"first real owner, successful LastError preservation");
    expect_guard(PILOT_NETWORK_TEST_STARTUP,1);
    invoke(PILOT_NETWORK_TEST_STARTUP,0);check(!g_eax && network_offline.references==2 && creates==1,"repeat owns separate backend ref");
    startup_error=10093;invoke(PILOT_NETWORK_TEST_STARTUP,0);
    check(g_eax==0x8007276du && guest_error==10093 && error_calls==1 && network_offline.references==2 && !cleanups,"backend error projection and no acquired ref");
    startup_error=-7;invoke(PILOT_NETWORK_TEST_STARTUP,0);check(g_eax==(uint32_t)-7 && guest_error==(uint32_t)-7,"negative error preserved");
    startup_error=0x10000;invoke(PILOT_NETWORK_TEST_STARTUP,0);check(g_eax==0x80070000u && guest_error==0x10000,"positive backend error HRESULT low word");
    startup_error=0;invoke(PILOT_NETWORK_TEST_CLEANUP,0);check(!g_eax && network_offline.references==1 && !destroys && cleanups==1,"nonfinal cleanup");
    invoke(PILOT_NETWORK_TEST_CLEANUP,0);check(!g_eax && !network_offline.owner && !network_offline.references && destroys==1 && cleanups==2,"final cleanup retires owner");
    fail_create=1;unsigned before=cleanups;invoke(PILOT_NETWORK_TEST_STARTUP,0);
    check(g_eax==0x8007000eu && !network_offline.references && !network_offline.owner && cleanups==before+1,"allocation failure balances acquired backend");
    fail_create=0;invoke(PILOT_NETWORK_TEST_STARTUP,0);check(!g_eax,"reinitialize after failure/final cleanup");
    void *owner=network_offline.owner;network_offline.owner=NULL;expect_guard(PILOT_NETWORK_TEST_CLEANUP,0);network_offline.owner=owner;
    uint32_t refs=network_offline.references;network_offline.references=65535;expect_guard(PILOT_NETWORK_TEST_STARTUP,0);network_offline.references=refs;
    HANDLE threads[8];for(unsigned i=0;i<8;++i){threads[i]=CreateThread(NULL,0,contender,NULL,0,NULL);check(threads[i]!=NULL,"create contention thread");}
    check(WaitForMultipleObjects(8,threads,TRUE,30000)==WAIT_OBJECT_0,"contention completion");
    for(unsigned i=0;i<8;++i)CloseHandle(threads[i]);
    check(network_offline.references==1,"contention retains initial owned reference");
    invoke(PILOT_NETWORK_TEST_CLEANUP,0);check(!network_offline.references && !network_offline.owner,"contention final retirement");
    check(starts==cleanups+3,"every successful acquired backend ref is balanced; three backend failures");
    printf("PASS native offline network lifecycle, guest ABI, failure unwind, %u guards, 1600 concurrent pairs\n",guard_count);
    return 0;
}
