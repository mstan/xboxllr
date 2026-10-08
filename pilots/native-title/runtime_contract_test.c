/* Exercise native ownership/ABI contracts without a game executing. */
#define main pilot_unused_main
#include "runtime.c"
#undef main

#define CHECK(c) do { if (!(c)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#c); ExitProcess(1); } } while (0)
static LONG test_counter;
static uint32_t test_lock=0x14000, test_worker_address=0x12000;
static void test_dpc(void);
static void test_timers(void);
static void test_memory(void);
static void test_scheduler_worker(void);
#ifdef PILOT_D3D_NATIVE
static void test_vblank_callback(void);
static void test_gpu_callback(void);
#endif

static void test_worker(void)
{
    CHECK(MEM32(g_esp+4)==0x12345678 && MEM32(g_esp+8)==0x87654321);
    CHECK(MEM32(MEM32(g_fs_base+0x28)+0x28)==MEM32(g_fs_base+4)-20);
    CHECK(MEM32(MEM32(g_fs_base+4)-20)==0);
    g_eax=99; /* caller registers must remain private */
    for (unsigned i=0;i<10000;++i) {
        critical_section(277,test_lock);
        critical_section(277,test_lock);
        CHECK(MEM32(test_lock+20)==2);
        ++test_counter;
        critical_section(294,test_lock);
        critical_section(294,test_lock);
    }
}

recomp_func_t recomp_lookup(uint32_t va) {
#ifdef PILOT_D3D_NATIVE
    if(va==0x12300 || va==0x12320)return test_vblank_callback;
    if(va==0x12360)return test_gpu_callback;
#endif
    return va==test_worker_address ? test_worker : va==0x12100 ? test_dpc : va==0x12200 ? test_scheduler_worker : NULL;
}

static void call_kernel(unsigned ordinal,const uint32_t *args,unsigned argc)
{
    g_esp=0x18000;
    MEM32(g_esp)=0x12340;
    for (unsigned i=0;i<argc;++i) MEM32(g_esp+4+i*4)=args[i];
    next_kernel_ordinal=ordinal;
    kernel_dispatch();
    CHECK(g_esp==0x18000+4+argc*4);
}

#include "media_contract_test.inc"
#include "clock_contract_test.inc"
#include "storage_contract_test.inc"
#include "cache_contract_test.inc"
#include "file_cache_contract_test.inc"
#include "nonlocal_jump_contract_test.inc"
#include "thread_objects_contract_test.inc"
#include "event_objects_contract_test.inc"

static int test_write_faults(uint32_t va)
{
#ifdef _MSC_VER
    __try { *(volatile unsigned char*)(memory+va)=0x5a; }
    __except(GetExceptionCode()==EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) { return 1; }
    return 0;
#else
    MEMORY_BASIC_INFORMATION info;
    CHECK(VirtualQuery(memory+va,&info,sizeof(info))==sizeof(info));
    return info.State!=MEM_COMMIT || info.Protect==PAGE_NOACCESS || info.Protect==PAGE_READONLY;
#endif
}
#include "sections_contract_test.inc"
#include "physical_memory_contract_test.inc"
#include "memory_statistics_contract_test.inc"
#ifdef PILOT_AUDIO_NATIVE
#include "audio_contract_test.inc"
#include "audio_mix_contract_test.inc"
#include "audio_adpcm_contract_test.inc"
#endif
#ifdef PILOT_INPUT_NATIVE
#include "input_contract_test.inc"
#endif
static void test_memory(void)
{
    const uint32_t bp=0x1b000,sp=0x1b004,info=0x1b020;
    uint32_t args[]={bp,0,sp,MEM_RESERVE,PAGE_READWRITE};
    MEM32(bp)=0; MEM32(sp)=128*1024*1024;
    uint64_t before=vm_committed(0,0x100000000ull);
    call_kernel(184,args,5); CHECK(g_eax==0);
    uint32_t base=MEM32(bp),reserved=MEM32(sp);
    CHECK(base>=0x20000 && !(base&0xffff) && reserved==128*1024*1024);
    CHECK(vm_committed(0,0x100000000ull)==before && test_write_faults(base));
    uint32_t query[]={base,info}; call_kernel(217,query,2);
    CHECK(g_eax==0 && MEM32(info)==base && MEM32(info+4)==base && MEM32(info+12)==reserved && MEM32(info+16)==MEM_RESERVE);

    MEM32(bp)=base+4095; MEM32(sp)=2; args[3]=MEM_COMMIT;
    call_kernel(184,args,5); CHECK(g_eax==0 && MEM32(bp)==base && MEM32(sp)==8192);
    CHECK(MEM32(base)==0 && MEM32(base+4096)==0);
    MEM32(base)=0x12345678; MEM32(base+4096)=0x87654321;
    call_kernel(184,args,5); CHECK(g_eax==0 && MEM32(base)==0x12345678);
    call_kernel(217,query,2); CHECK(g_eax==0 && MEM32(info+12)==8192 && MEM32(info+16)==MEM_COMMIT);
    query[0]=base+8192; call_kernel(217,query,2);
    CHECK(g_eax==0 && MEM32(info+12)==reserved-8192 && MEM32(info+16)==MEM_RESERVE);

    MEM32(bp)=base+1; MEM32(sp)=1;
    uint32_t protection[]={bp,sp,PAGE_READONLY,0x1b008};
    call_kernel(204,protection,4);
    CHECK(g_eax==0 && MEM32(bp)==base && MEM32(sp)==4096 && MEM32(0x1b008)==PAGE_READWRITE);
    CHECK(MEM32(base)==0x12345678 && test_write_faults(base));
    CHECK(!test_write_faults(base+4096));
    MEM32(base+4096)=0x87654321;
    protection[2]=PAGE_READWRITE; call_kernel(204,protection,4);
    CHECK(g_eax==0 && MEM32(0x1b008)==PAGE_READONLY);

    uint32_t free_args[]={bp,sp,MEM_DECOMMIT};
    call_kernel(199,free_args,3); CHECK(g_eax==0 && test_write_faults(base));
    call_kernel(184,args,5); CHECK(g_eax==0 && MEM32(base)==0);
    CHECK(MEM32(base+4096)==0x87654321); /* neighboring committed page retained */
    MEM32(bp)=base+reserved-1; MEM32(sp)=2;
    call_kernel(184,args,5); CHECK(g_eax==VM_CONFLICT && MEM32(bp)==base+reserved-1 && MEM32(sp)==2);
    MEM32(bp)=base; MEM32(sp)=4096; args[3]=MEM_RESERVE;
    call_kernel(184,args,5); CHECK(g_eax==VM_CONFLICT);
    MEM32(sp)=64*1024*1024; args[3]=MEM_COMMIT;
    call_kernel(184,args,5); CHECK(g_eax==VM_NOMEM && MEM32(sp)==64*1024*1024);
    query[0]=base+8192; call_kernel(217,query,2); CHECK(MEM32(info+16)==MEM_RESERVE);
    MEM32(sp)=0; free_args[2]=MEM_RELEASE;
    call_kernel(199,free_args,3); CHECK(g_eax==0 && MEM32(sp)==reserved && test_write_faults(base));
    query[0]=base; call_kernel(217,query,2); CHECK(g_eax==0 && MEM32(info+16)==MEM_FREE);
    MEM32(sp)=0; call_kernel(199,free_args,3); CHECK(g_eax==VM_NOT_ALLOCATED);

    /* Whole-range reuse must not retain old bytes or reservation metadata. */
    MEM32(bp)=base; MEM32(sp)=8192; args[3]=MEM_RESERVE|MEM_COMMIT;
    call_kernel(184,args,5); CHECK(g_eax==0 && MEM32(base)==0 && MEM32(base+4096)==0);
    MEM32(sp)=0; call_kernel(199,free_args,3); CHECK(g_eax==0);
    /* MEM_NOZERO is an initialization permission, not MEM_ROTATE. Exercise
     * fresh commitment, writes, recommit preservation, budget and release. */
    MEM32(bp)=base; MEM32(sp)=8192; args[3]=MEM_RESERVE|MEM_COMMIT|0x00800000u;
    call_kernel(184,args,5); CHECK(g_eax==0 && MEM32(bp)==base && MEM32(sp)==8192);
    MEM32(base)=0x24681357;MEM32(base+8192-4)=0x12345678;
    args[3]=MEM_COMMIT|0x00800000u;
    call_kernel(184,args,5); CHECK(g_eax==0 && MEM32(base)==0x24681357 && MEM32(base+8192-4)==0x12345678);
    uint64_t service=vm_service_bytes;vm_service_bytes=64u*1024*1024;
    MEM32(bp)=0;MEM32(sp)=4096;
    call_kernel(184,args,5); CHECK(g_eax==VM_NOMEM && !MEM32(bp) && MEM32(sp)==4096);
    vm_service_bytes=service;MEM32(bp)=base;MEM32(sp)=0;
    call_kernel(199,free_args,3); CHECK(g_eax==0 && test_write_faults(base));
    MEM32(bp)=0; MEM32(sp)=4096; args[1]=16; args[3]=MEM_RESERVE|MEM_TOP_DOWN;
    call_kernel(184,args,5); CHECK(g_eax==VM_NOMEM); /* all usable low 64 KiB excluded */
    args[1]=0; call_kernel(184,args,5); CHECK(g_eax==0 && MEM32(bp)==0x7ffe0000);
    MEM32(sp)=0; call_kernel(199,free_args,3); CHECK(g_eax==0);
    CHECK(vm_committed(0,0x100000000ull)==before);
    fprintf(stderr,"PASS: virtual reservation/commit/protection/decommit/release/query ABI, NOZERO commitment/recommit, real access protection, reuse zero-fill, budget/failure preservation\n");
    /* Exercise the actual failure shape: >512 independent small resources,
     * all within the console budget, then verify contents/query and reuse. */
    uint32_t small[600];
    for(unsigned pass=0;pass<2;++pass) {
        for(unsigned i=0;i<600;++i) {
            uint32_t span=4096;small[i]=0;
            CHECK(!vm_allocate(&small[i],0,&span,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
            CHECK(MEM32(small[i])==0);MEM32(small[i])=0xa5500000u+i;
        }
        for(unsigned i=0;i<600;++i) {
            CHECK(MEM32(small[i])==0xa5500000u+i);
            query[0]=small[i];call_kernel(217,query,2);
            CHECK(g_eax==0 && MEM32(info+4)==small[i] && MEM32(info+16)==MEM_COMMIT);
        }
        for(unsigned i=0;i<600;++i) {uint32_t span=0;CHECK(!vm_free(&small[i],&span,MEM_RELEASE));}
        CHECK(vm_committed(0,0x100000000ull)==before);
    }
    fprintf(stderr,"PASS: 600 simultaneous small reservations, preserved contents/query, two complete cleanup/reuse cycles within the 64 MiB budget\n");
    test_physical_memory();
}

static HANDLE dpc_entered,dpc_release,dpc_done;
static volatile LONG dpc_calls;
static uint32_t dpc_seen[4];
static int dpc_mode;
struct WaitTest { uint32_t object, status; };
static DWORD WINAPI test_wait_worker(void *argument)
{
    struct WaitTest *w=argument;
    w->status=dispatch_wait(&w->object,1,0,0,0x1a308);
    return 0;
}
static void test_dpc(void)
{
    CHECK(dispatch_in_dpc && MEM8(g_fs_base+0x24)==2);
    uint32_t va=MEM32(g_esp+4);
    CHECK(!MEM8(va+2));
    if (va==0x1a100 && dpc_mode==1) {
        SetEvent(dpc_entered);
        CHECK(WaitForSingleObject(dpc_release,5000)==WAIT_OBJECT_0);
    } else {
        for (unsigned i=0;i<4;++i) dpc_seen[i]=MEM32(g_esp+4+i*4);
        LONG count=InterlockedIncrement(&dpc_calls);
        if (dpc_mode==2 && count==3) CHECK(dispatch_cancel_timer(0x1a000)==1);
        if (dpc_mode!=2 || count==3) SetEvent(dpc_done);
    }
    g_eax=0xdeadc0de; g_esp+=20;
}
static void test_timers(void)
{
    const uint32_t timer=0x1a000,dpc=0x1a100,second=0x1a120,timeout=0x1a300;
    uint32_t args[8]={timer,0};
    call_kernel(113,args,2);
    CHECK(MEM8(timer)==8 && MEM8(timer+2)==10 && MEM32(timer+8)==timer+8);
    int64_t relative=-500000;
    args[1]=(uint32_t)relative; args[2]=(uint32_t)((uint64_t)relative>>32); args[3]=0;
    call_kernel(149,args,4); CHECK(g_eax==0 && MEM8(timer+3)==1 && MEM32(timer+4)==0);
    MEM64(timeout)=(uint64_t)-50000000ll;
    uint32_t wait[]={timer,0,0,0,timeout};
    call_kernel(159,wait,5); CHECK(g_eax==0 && !MEM8(timer+3));
    MEM64(timeout)=0;
    call_kernel(159,wait,5); CHECK(g_eax==0); /* notification remains signaled */

    /* Absolute future deadlines must not become immediate success. */
    uint64_t absolute=dispatch_wall_time()+1000000;
    args[1]=(uint32_t)absolute; args[2]=(uint32_t)(absolute>>32);
    call_kernel(149,args,4); CHECK(g_eax==0);
    call_kernel(159,wait,5); CHECK(g_eax==0x102);
    MEM64(timeout)=(uint64_t)-50000000ll;
    call_kernel(159,wait,5); CHECK(g_eax==0);

    /* Rearm replaces the old deadline; cancel preserves a previous signal. */
    relative=-600000000;
    args[1]=(uint32_t)relative; args[2]=(uint32_t)((uint64_t)relative>>32);
    call_kernel(149,args,4); CHECK(g_eax==0);
    call_kernel(149,args,4); CHECK(g_eax==1);
    call_kernel(97,&timer,1); CHECK(g_eax==1);
    call_kernel(97,&timer,1); CHECK(g_eax==0);
    MEM64(timeout)=(uint64_t)-200000;
    call_kernel(159,wait,5); CHECK(g_eax==0x102);
    args[1]=0; args[2]=0;
    call_kernel(149,args,4); CHECK(g_eax==0);
    call_kernel(97,&timer,1); CHECK(g_eax==0 && MEM32(timer+4)==1);

    uint32_t init[]={timer,1}; call_kernel(113,init,2);
    call_kernel(149,args,4);
    MEM64(timeout)=0;
    call_kernel(159,wait,5); CHECK(g_eax==0 && MEM32(timer+4)==0);
    call_kernel(159,wait,5); CHECK(g_eax==0x102);

    /* WaitAll must not consume a synchronization event until all are ready. */
    uint32_t event1=0x1a200,event2=0x1a220,objects[]={event1,event2};
    dispatch_initialize(event1,1); dispatch_initialize(event2,0);
    CHECK(dispatch_event(event1,1)==0);
    CHECK(dispatch_wait(objects,2,1,0,timeout)==0x102 && MEM32(event1+4)==1);
    dispatch_event(event2,1);
    CHECK(dispatch_wait(objects,2,1,0,timeout)==0 && MEM32(event1+4)==0 && MEM32(event2+4)==1);
    CHECK(dispatch_wait(objects,2,0,0,timeout)==1);

    /* A waiting thread owns a successful signal even if reset precedes its
     * next host timeslice. Synchronization signals reserve only one waiter. */
    for (unsigned type=0;type<2;++type) {
        dispatch_initialize(event1,type);
        MEM64(0x1a308)=(uint64_t)-50000000ll;
        struct WaitTest a={event1,0xffffffff},b={event1,0xffffffff};
        HANDLE waiters[]={CreateThread(NULL,0,test_wait_worker,&a,0,NULL),CreateThread(NULL,0,test_wait_worker,&b,0,NULL)};
        CHECK(waiters[0] && waiters[1]);
        ULONGLONG limit=GetTickCount64()+2000;
        for (;;) {
            unsigned count=0;
            AcquireSRWLockExclusive(&dispatch_lock);
            for (struct PilotWaiter *w=dispatch_waiters;w;w=w->next) ++count;
            ReleaseSRWLockExclusive(&dispatch_lock);
            if (count==2) break;
            CHECK(GetTickCount64()<limit); Sleep(1);
        }
        dispatch_event(event1,1); dispatch_event(event1,0);
        CHECK(WaitForMultipleObjects(2,waiters,FALSE,2000)<WAIT_OBJECT_0+2);
        if (type==1) {
            CHECK(WaitForMultipleObjects(2,waiters,TRUE,0)==WAIT_TIMEOUT);
            dispatch_event(event1,1);
        }
        CHECK(WaitForMultipleObjects(2,waiters,TRUE,2000)==WAIT_OBJECT_0);
        CHECK(a.status==0 && b.status==0);
        CloseHandle(waiters[0]); CloseHandle(waiters[1]);
    }

    dpc_entered=CreateEventW(NULL,FALSE,FALSE,NULL);
    dpc_release=CreateEventW(NULL,FALSE,FALSE,NULL);
    dpc_done=CreateEventW(NULL,FALSE,FALSE,NULL);
    CHECK(dpc_entered && dpc_release && dpc_done);
    uint32_t dinit[]={dpc,0x12100,0xabcdef01}; call_kernel(107,dinit,3);
    dinit[0]=second; call_kernel(107,dinit,3);
    dpc_mode=1;
    uint32_t queue[]={dpc,0x12345678,0x87654321};
    call_kernel(119,queue,3); CHECK(g_eax==1);
    CHECK(WaitForSingleObject(dpc_entered,5000)==WAIT_OBJECT_0);
    queue[0]=second;
    call_kernel(119,queue,3); CHECK(g_eax==1);
    call_kernel(119,queue,3); CHECK(g_eax==0);
    call_kernel(137,&second,1); CHECK(g_eax==1 && !MEM8(second+2));
    call_kernel(137,&second,1); CHECK(g_eax==0);
    /* Timer expiry queues the callback with system time arguments; cancelling
     * an already expired timer cannot erase that queued work. */
    args[3]=second;
    uint64_t before=dispatch_wall_time();
    call_kernel(149,args,4); CHECK(g_eax==0 && MEM8(second+2));
    call_kernel(97,&timer,1); CHECK(g_eax==0 && MEM8(second+2));
    g_eax=42; SetEvent(dpc_release);
    CHECK(WaitForSingleObject(dpc_done,5000)==WAIT_OBJECT_0 && g_eax==42);
    CHECK(dpc_calls==1 && dpc_seen[0]==second && dpc_seen[1]==0xabcdef01);
    uint64_t seen=((uint64_t)dpc_seen[3]<<32)|dpc_seen[2];
    CHECK(seen>=before && seen<=dispatch_wall_time());

    dpc_mode=2; InterlockedExchange(&dpc_calls,0);
    relative=-100000;
    uint32_t periodic[]={timer,(uint32_t)relative,(uint32_t)((uint64_t)relative>>32),10,second};
    call_kernel(150,periodic,5); CHECK(g_eax==0);
    CHECK(WaitForSingleObject(dpc_done,5000)==WAIT_OBJECT_0 && dpc_calls==3 && !MEM8(timer+3));
    CHECK(WaitForSingleObject(dpc_done,40)==WAIT_TIMEOUT && dpc_calls==3);
    CloseHandle(dpc_entered); CloseHandle(dpc_release); CloseHandle(dpc_done);
    fprintf(stderr,"PASS: relative/absolute/periodic timers, cancellation, wait state, queued DPC arguments/lifetime, isolated callback registers\n");
}

#ifdef PILOT_D3D_NATIVE
#include "graphics_contract_test.inc"
#endif

int main(int argc,char **argv)
{
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    EXCEPTION_RECORD diagnostic={0};EXCEPTION_POINTERS pointers={&diagnostic,NULL};
    diagnostic.ExceptionCode=0x406d1388u;
    CHECK(fault_handler(&pointers)==EXCEPTION_CONTINUE_SEARCH);
    memory=VirtualAlloc(NULL,(SIZE_T)0x100000000ull,MEM_RESERVE,PAGE_NOACCESS);
    CHECK(memory!=NULL);
    g_xbox_mem_offset=(ptrdiff_t)memory;
    commit(0x10000,0x10000);
    g_esp=0x18000; MEM32(g_esp)=0x12340;
    image_size=0x200; image_bytes=calloc(1,image_size); CHECK(image_bytes!=NULL);
#ifdef PILOT_AUDIO_DSP
    if(argc==3 && !strcmp(argv[1],"audio-effects")){test_audio_effects(argv[2]);return 0;}
    if(argc==2 && !strcmp(argv[1],"audio-mix")){test_audio_mix();return 0;}
#ifdef PILOT_AUDIO_ADPCM
    if(argc==2 && !strcmp(argv[1],"audio-adpcm")){test_audio_adpcm();return 0;}
#endif
#endif
    if (argc==2) {
#ifdef PILOT_AUDIO_NATIVE
        if (!strcmp(argv[1],"audio")) { test_audio(); return 0; }
        if (!strncmp(argv[1],"audio-",6)) { test_audio_failure(argv[1]+6); return 0; }
#endif
#ifdef PILOT_INPUT_NATIVE
        if (!strcmp(argv[1],"input")) { test_input(); return 0; }
        if (!strncmp(argv[1],"input-",6)) { test_input_failure(argv[1]+6); return 0; }
#endif
#ifdef PILOT_D3D_NATIVE
        if (!strcmp(argv[1],"graphics")) { test_graphics(); return 0; }
        if (!strcmp(argv[1],"graphics-unsupported")) {
            graphics_active=1; /* Exercise the unsupported-family guard. */
            pilot_d3d_dispatch(PILOT_D3D_D3DDevice_Reset); return 1;
        }
#endif
        if (!strcmp(argv[1],"media")) { test_media(); return 0; }
        if (!strcmp(argv[1],"storage")) { test_storage(); return 0; }
        if (!strcmp(argv[1],"cache")) { test_cache(); return 0; }
        if (!strcmp(argv[1],"file-cache")) { test_file_cache(); return 0; }
        if (!strcmp(argv[1],"sections")) { test_sections(); return 0; }
        if (!strcmp(argv[1],"memory")) { test_memory(); return 0; }
        if (!strcmp(argv[1],"memory-statistics")) { test_memory_statistics(); return 0; }
        if (!strcmp(argv[1],"timers")) { test_timers(); return 0; }
        if (!strcmp(argv[1],"system-clock")) { test_system_clock(); return 0; }
        if (!strcmp(argv[1],"threads")) { test_thread_objects(); return 0; }
        if (!strcmp(argv[1],"events")) { test_event_objects(); return 0; }
        if (!strcmp(argv[1],"jumps")) { test_jumps(); return 0; }
        if (!strncmp(argv[1],"jump-",5)) { test_jump_failure(argv[1]+5); return 1; }
        if (!strcmp(argv[1],"instruction")) recomp_unimpl("test unsupported instruction",0x12340);
        if (!strcmp(argv[1],"indirect")) recomp_icall_fail_log(0x12340);
        if (!strcmp(argv[1],"kernel")) { next_kernel_ordinal=366; kernel_dispatch(); }
        return 1;
    }
    critical_section(291,test_lock);
    CHECK(MEM32(test_lock+16)==0xffffffffu && MEM32(test_lock+20)==0);
    uint32_t args[10]={0x19000,0,0x10000,20,0x19004,0x12345678,0x87654321,1,0,0x12000};
    call_kernel(255,args,10); CHECK(g_eax==0);
    uint32_t handle1=MEM32(0x19000),tid1=MEM32(0x19004);
    HANDLE host1=threads[1].handle;
    CHECK(WaitForSingleObject(host1,0)==WAIT_TIMEOUT && test_counter==0);
    call_kernel(255,args,10); CHECK(g_eax==0);
    HANDLE host2=threads[2].handle;
    CHECK(MEM32(0x19000)!=handle1 && MEM32(0x19004)!=tid1);
    uint32_t resume[]={handle1,0x19008};
    call_kernel(224,resume,2); CHECK(g_eax==0 && MEM32(0x19008)==1);
    resume[0]=MEM32(0x19000);
    call_kernel(224,resume,2); CHECK(g_eax==0 && MEM32(0x19008)==1);
    g_eax=42;
    CHECK(WaitForSingleObject(host1,5000)==WAIT_OBJECT_0);
    CHECK(WaitForSingleObject(host2,5000)==WAIT_OBJECT_0);
    CHECK(test_counter==20000 && g_eax==42);
    CHECK(MEM32(test_lock+16)==0xffffffffu && MEM32(test_lock+20)==0 && MEM32(test_lock+24)==0);
    args[0]=handle1; call_kernel(187,args,1); CHECK(g_eax==0);
    call_kernel(187,args,1); CHECK(g_eax==0xc0000008u);

    /* User settings: real payload, checksum, typed/short-buffer behavior. */
    have_eeprom=1;
    uint32_t language=6,checksum=~language;
    memcpy(eeprom+0x90,&language,4); memcpy(eeprom+0x60,&checksum,4);
    uint32_t query[]={7,0x19100,0x19104,4,0x19108};
    call_kernel(24,query,5);
    CHECK(g_eax==0 && MEM32(0x19100)==4 && MEM32(0x19104)==6 && MEM32(0x19108)==4);
    query[3]=3; MEM32(0x19104)=0xaabbccdd;
    call_kernel(24,query,5); CHECK(g_eax==0xc0000023u && MEM32(0x19104)==0xaabbccdd);
    query[3]=4; eeprom[0x90]^=1;
    call_kernel(24,query,5); CHECK(g_eax==0xc000009cu && MEM32(0x19104)==0xaabbccdd);
    fprintf(stderr,"PASS: isolated threads/TLS/registers, recursive exclusion, handles, EEPROM ABI\n");
    return 0;
}
