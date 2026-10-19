/* Exercise native ownership/ABI contracts without a game executing. */
#include <windows.h>
#ifdef PILOT_D3D_NATIVE
static HANDLE WINAPI test_clock_timer_ex(LPSECURITY_ATTRIBUTES,LPCWSTR,DWORD,DWORD);
static HANDLE WINAPI test_clock_timer_legacy(LPSECURITY_ATTRIBUTES,BOOL,LPCWSTR);
static BOOL WINAPI test_clock_handle_close(HANDLE);
#define CreateWaitableTimerExW test_clock_timer_ex
#define CreateWaitableTimerW test_clock_timer_legacy
#define CloseHandle test_clock_handle_close
/* Change only the desktop return status after the real rendering/Swap. */
#define pilot_gfx_present test_desktop_present
#endif
#if defined(PILOT_D3D_NATIVE) && defined(PILOT_SCHEDULING_LLE) && defined(PILOT_EVENT_TIMER_LLE) && !defined(PILOT_DISPLAY_LLE)
/* Assert actual Release completion before retiring fixture-owned GPU work. */
#define pilot_gfx_close test_lifetime_close
static void test_lifetime_close(void);
#endif
#define main pilot_unused_main
#include "runtime.c"
#undef main
#if defined(PILOT_D3D_NATIVE) && defined(PILOT_SCHEDULING_LLE) && defined(PILOT_EVENT_TIMER_LLE) && !defined(PILOT_DISPLAY_LLE)
#undef pilot_gfx_close
void pilot_gfx_close(void);
#endif
#ifdef PILOT_D3D_NATIVE
#undef CreateWaitableTimerExW
#undef CreateWaitableTimerW
#undef CloseHandle
#undef pilot_gfx_present
int32_t pilot_gfx_present(void);
static int test_desktop_override;
static int32_t test_desktop_result;
#endif

#define CHECK(c) do { if (!(c)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#c); ExitProcess(1); } } while (0)
#ifdef PILOT_D3D_NATIVE
#include "graphics_clock_handle_contract_test.inc"
int32_t test_desktop_present(void)
{
    int32_t result=pilot_gfx_present();
    CHECK(result>=0);
    return test_desktop_override?test_desktop_result:result;
}
#endif
static int test_pool_owned(uint32_t pointer)
{
    for(unsigned i=0;i<4096;++i)if(pool_blocks[i].span && pool_blocks[i].payload==pointer)return 1;
    return 0;
}
static LONG test_counter;
static uint32_t test_lock=0x14000, test_worker_address=0x12000;
static void test_dpc(void);
static void test_timers(void);
static void test_memory(void);
static int test_write_faults(uint32_t va);
static void test_scheduler_worker(void);
static void test_thread_fp_body(void);
static void test_thread_lifetime_body(void);
#ifdef PILOT_EVENT_TIMER_LLE
static void test_timer_lle_dpc(void);
#ifdef PILOT_SCHEDULING_LLE
static void test_synchronize_dpc(void);
static void test_synchronize_callback(void);
#endif
static void test_thread_lle_body(void);
static void test_thread_wait_body(void);
static void test_thread_timeout_body(void);
static void test_thread_idle_clock_body(void);
static void test_idle_interrupt_callback(void);
static void test_idle_dpc_callback(void);
static void test_thread_compute_body(void);
static void test_thread_cpu_body(void);
static void test_thread_cpu_peer(void);
#endif
#ifdef PILOT_USB_LLE
static void test_usb_isr(void);
static void test_usb_dpc(void);
#endif
#ifdef PILOT_D3D_NATIVE
static void test_vblank_callback(void);
static void test_gpu_callback(void);
#if defined(PILOT_SCHEDULING_LLE) && !defined(PILOT_DISPLAY_LLE)
static void test_graphics_callback_vblank_a(void);
static void test_graphics_callback_vblank_b(void);
static void test_graphics_callback_gpu(void);
#endif
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

#ifdef PILOT_GPU_AV_TEST_CALLBACKS
recomp_func_t pilot_gpu_av_original_lookup(uint32_t va);
#endif
recomp_func_t recomp_lookup(uint32_t va) {
#ifdef PILOT_GPU_AV_TEST_CALLBACKS
    recomp_func_t original=pilot_gpu_av_original_lookup(va);
    if(original)return original;
#endif
    if(va==0x12640)return test_thread_fp_body;
    if(va==0x12680)return test_thread_lifetime_body;
#ifdef PILOT_EVENT_TIMER_LLE
    if(va==0x12480)return test_timer_lle_dpc;
#ifdef PILOT_SCHEDULING_LLE
    if(va==0x124c0)return test_synchronize_dpc;
    if(va==0x177ecc)return test_synchronize_callback;
#endif
    if(va==0x12500)return test_thread_lle_body;
    if(va==0x12520)return test_idle_interrupt_callback;
    if(va==0x12560)return test_idle_dpc_callback;
    if(va==0x12540)return test_thread_wait_body;
    if(va==0x125c0)return test_thread_timeout_body;
    if(va==0x126c0)return test_thread_idle_clock_body;
    if(va==0x126e0)return test_thread_compute_body;
    if(va==0x12740 || va==0x127a0 || va==0x12840 || va==0x12a00 || va==0x12c00 || va==0x12c80 || va==0x12d00 || va==0x12e00 || va==0x12f00 || va==0x13200 || va==0x13500 || va==0x13800 || va==0x13900 || va==0x13a00 || va==0x13b00 || va==0x13c00 || va==0x13d00 || va==0x13e00 || va==0x13f00 || va==0x15000 || va==0x16000 || va==0x16400 || va==0x16500 || va==0x16700 || va==0x16800 || va==0x17000 || va==0x17200 || va==0x17400 || va==0x17600 || va==0x17800)return test_thread_cpu_body;
    if(va==0x18100 || va==0x18b00 || va==0x18e00)return test_thread_cpu_body;
    if(va==0x1e000 || va==0x1e400)return test_thread_cpu_body;
    if(va==0x12800)return test_thread_cpu_peer;
#endif
#ifdef PILOT_USB_LLE
    if(va==0x12400)return test_usb_isr;
    if(va==0x12440)return test_usb_dpc;
#endif
#ifdef PILOT_D3D_NATIVE
    if(va==0x12300 || va==0x12320)return test_vblank_callback;
    if(va==0x12360)return test_gpu_callback;
#if defined(PILOT_SCHEDULING_LLE) && !defined(PILOT_DISPLAY_LLE)
    if(va==0x17a00)return test_graphics_callback_vblank_a;
    if(va==0x17a40)return test_graphics_callback_vblank_b;
    if(va==0x17a80)return test_graphics_callback_gpu;
#endif
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
#include "thread_lifetime_contract_test.inc"
#include "thread_fp_contract_test.inc"
#include "thread_scheduler_contract_test.inc"
#include "event_objects_contract_test.inc"
#include "mutant_objects_contract_test.inc"
#ifdef PILOT_EVENT_TIMER_LLE
#include "event_timer_lle_contract_test.inc"
#include "callback_kernel_lle_contract_test.inc"
#include "synchronize_lle_contract_test.inc"
#include "timer_dpc_lle_contract_test.inc"
#include "thread_lle_contract_test.inc"
#include "thread_wait_lle_contract_test.inc"
#include "thread_idle_clock_contract_test.inc"
#include "thread_compute_clock_contract_test.inc"
#include "thread_fp_compute_contract_test.inc"
#include "thread_cpu_lle_contract_test.inc"
#include "thread_child_cpu_contract_test.inc"
#include "thread_critical_cpu_contract_test.inc"
#include "thread_vm_cpu_contract_test.inc"
#include "thread_vm_alias_contract_test.inc"
#include "thread_publication_memo_contract_test.inc"
#include "thread_files_cpu_contract_test.inc"
#include "thread_contiguous_cpu_contract_test.inc"
#include "thread_component_cpu_contract_test.inc"
#include "thread_component_abi_contract_test.inc"
#include "thread_component_vm_contract_test.inc"
#include "thread_component_pool_contract_test.inc"
#include "thread_component_physical_owner_contract_test.inc"
#include "thread_component_contiguous_contract_test.inc"
#include "thread_calendar_cpu_contract_test.inc"
#include "thread_ordinary_helpers_cpu_contract_test.inc"
#include "thread_component_long_owner_contract_test.inc"
#include "thread_component_wait_contract_test.inc"
#if defined(PILOT_FILES_LLE) && defined(PILOT_SCHEDULING_LLE)
#include "thread_sections_cpu_contract_test.inc"
#endif
#include "thread_string_cpu_contract_test.inc"
#include "thread_string_compare_cpu_contract_test.inc"
#include "thread_device_cpu_contract_test.inc"
#include "thread_delay_cpu_contract_test.inc"
#include "thread_priority_cpu_contract_test.inc"
#include "idle_interrupt_stack_contract_test.inc"
#include "rtc_contract_test.inc"
#include "smbus_contract_test.inc"
#include "settings_lle_contract_test.inc"
#endif
#ifdef PILOT_IO_DEVICE_LLE
#include "io_device_contract_test.inc"
#endif

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
#include "section_mods_contract_test.inc"
#include "sections_readonly_contract_test.inc"
#ifdef PILOT_INPUT_NATIVE
#include "input_contract_test.inc"
#endif
#ifdef PILOT_USB_LLE
#include "usb_contract_test.inc"
#include "usb_original_dma_contract_test.inc"
#include "usb_irq_contract_test.inc"
#endif
#include "physical_memory_contract_test.inc"
#ifdef PILOT_GPU_LLE
#include "gpu_lle_contract_test.inc"
#include "gpu_thread_cpu_contract_test.inc"
#include "gpu_first_context_contract_test.inc"
#include "gpu_thread_av_contract_test.inc"
#include "gpu_av_interrupt_contract_test.inc"
#endif
#include "memory_statistics_contract_test.inc"
#ifdef PILOT_AUDIO_NATIVE
#include "audio_contract_test.inc"
#ifdef PILOT_AUDIO_REFERENCE
#include "audio_reference_runtime_contract_test.inc"
#endif
#include "audio_mix_contract_test.inc"
#include "audio_headroom_contract_test.inc"
#include "audio_detach_contract_test.inc"
#include "audio_adpcm_contract_test.inc"
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
#ifndef PILOT_MEMORY_LLE
    call_kernel(184,args,5); CHECK(g_eax==VM_NOMEM); /* all usable low 64 KiB excluded */
#endif
    args[1]=0; call_kernel(184,args,5); CHECK(g_eax==0 && MEM32(bp)==0x7ffd0000);
    MEM32(sp)=0; call_kernel(199,free_args,3); CHECK(g_eax==0);
    CHECK(vm_committed(0,0x100000000ull)==before);
    /* Original kernel fixture qualifies partial release, surviving reservation
     * boundaries, free-page protection and rejected out-of-reservation frees. */
    uint32_t partial=0,partial_size=5*4096,view[7];
    CHECK(!vm_allocate(&partial,0,&partial_size,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    MEM32(partial)=0x12345678;MEM32(partial+8192)=0xabcdef01;
    uint32_t hole=partial+4097,length=2;
    CHECK(!vm_free(&hole,&length,MEM_RELEASE) && hole==partial+4096 && length==4096);
    CHECK(test_write_faults(hole) && MEM32(partial)==0x12345678 && MEM32(partial+8192)==0xabcdef01);
    CHECK(!vm_query(partial,view) && view[1]==partial && view[3]==4096);
    CHECK(!vm_query(hole,view) && !view[1] && !view[2] && view[3]==4096 && view[4]==MEM_FREE && view[5]==PAGE_NOACCESS && !view[6]);
    CHECK(!vm_query(partial+8192,view) && view[1]==partial+8192 && view[3]==3*4096);
    hole=partial+8192;length=4*4096;CHECK(vm_free(&hole,&length,MEM_RELEASE)==0xc000001au);
    hole=partial+3*4096;length=0;CHECK(vm_free(&hole,&length,MEM_RELEASE)==0xc000009fu);
    hole=partial;length=4096;uint32_t old_access=0;
    CHECK(!vm_protect(&hole,&length,PAGE_NOACCESS,&old_access) && old_access==PAGE_READWRITE);
    CHECK(test_write_faults(hole) && !vm_query(hole,view) && view[5]==PAGE_NOACCESS);
    hole=partial+1;length=0;CHECK(!vm_free(&hole,&length,MEM_RELEASE) && hole==partial && length==4096);
    hole=partial+8192;length=0;CHECK(!vm_free(&hole,&length,MEM_RELEASE));
    CHECK(vm_query(VM_END,view)==VM_INVALID);
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
    dispatch_ensure();
    /* Independent original IRQ0 observations, not host wakeup precision.
     * Five callbacks at each period must preserve the original logical cadence. */
    const unsigned periods[]={1,2,3,10,16,31,32,33,100};
    const unsigned intervals[]={2,3,4,11,17,32,33,34,101};
    for(unsigned i=0;i<sizeof(periods)/sizeof(periods[0]);++i) {
        uint64_t start=dispatch_clock_origin+50000,at=start;
        for(unsigned n=1;n<=5;++n) {
            at=dispatch_relative_deadline(at,(uint64_t)periods[i]*10000);
            CHECK(at==start+(uint64_t)n*intervals[i]*10000);
        }
        CHECK(dispatch_relative_deadline(start+9999,(uint64_t)periods[i]*10000)==start+(uint64_t)intervals[i]*10000);
    }
    const uint64_t durations[]={1,9999,10000,10001,100000,330000};
    const unsigned ticks[]={1,1,2,2,11,34};
    for(unsigned i=0;i<sizeof(ticks)/sizeof(ticks[0]);++i)
        CHECK(dispatch_relative_deadline(dispatch_clock_origin+50000,durations[i])==dispatch_clock_origin+50000+10000ull*ticks[i]);
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
#if defined(PILOT_SCHEDULING_LLE) && defined(PILOT_EVENT_TIMER_LLE)
#include "thread_graphics_component_contract_test.inc"
#include "thread_graphics_resource_contract_test.inc"
#include "thread_graphics_fence_contract_test.inc"
#ifndef PILOT_DISPLAY_LLE
#include "thread_graphics_reset_lifetime_contract_test.inc"
#include "thread_graphics_pulse_contract_test.inc"
#include "thread_graphics_callback_contract_test.inc"
#endif
#include "thread_graphics_gate_contract_test.inc"
#endif
#endif

#if defined(PILOT_AUDIO_NATIVE) && defined(PILOT_SCHEDULING_LLE) && defined(PILOT_EVENT_TIMER_LLE)
#include "thread_audio_component_contract_test.inc"
#ifdef PILOT_AUDIO_DSP
#include "thread_audio_effects_contract_test.inc"
#endif
#include "thread_audio_gate_contract_test.inc"
#endif

int main(int argc,char **argv)
{
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    EXCEPTION_RECORD diagnostic={0};EXCEPTION_POINTERS pointers={&diagnostic,NULL};
    diagnostic.ExceptionCode=0x406d1388u;
    CHECK(fault_handler(&pointers)==EXCEPTION_CONTINUE_SEARCH);
    diagnostic.ExceptionCode=0x40010006u;
    CHECK(fault_handler(&pointers)==EXCEPTION_CONTINUE_SEARCH);
    diagnostic.ExceptionCode=0x4001000au;
    CHECK(fault_handler(&pointers)==EXCEPTION_CONTINUE_SEARCH);
    if(argc==2 && !strcmp(argv[1],"host-diagnostics")) {
        install_fault_handler();
        OutputDebugStringA("xboxlle host diagnostic ANSI regression\n");
        OutputDebugStringW(L"xboxlle host diagnostic Unicode regression\n");
        fprintf(stderr,"PASS: host debug output returns through the production exception handler\n");
        return 0;
    }
#ifdef PILOT_GPU_AV_TEST_CALLBACKS
    if(argc>=2 && (!strcmp(argv[1],"gpu-av-interrupt-lle") || !strcmp(argv[1],"gpu-av-interrupt-thread-lle") ||
                  !strcmp(argv[1],"gpu-av-service-counterexample-lle"))) {
        CHECK(argc==3);
        /* Use the production hash-checked loader, original code pages and
         * import bindings before any authored harness scratch reservation. */
        load_image(argv[2]);
        if(!strcmp(argv[1],"gpu-av-service-counterexample-lle"))test_gpu_av_service_counterexample_lle(0);
        else test_gpu_av_interrupt_lle(0,!strcmp(argv[1],"gpu-av-interrupt-thread-lle"));
        return 0;
    }
#endif
    memory=VirtualAlloc(NULL,(SIZE_T)0x100000000ull,MEM_RESERVE,PAGE_NOACCESS);
    CHECK(memory!=NULL);
    g_xbox_mem_offset=(ptrdiff_t)memory;
    commit(0x10000,0x10000);
    g_esp=0x18000; MEM32(g_esp)=0x12340;
    image_size=0x200; image_bytes=calloc(1,image_size); CHECK(image_bytes!=NULL);
#ifdef PILOT_USB_LLE
    /* Production initializes the selected kernel during image reservation.
     * Include that one-time host engine setup before resource baselines. */
    uint32_t boot_irql=0,unused=0;
    CHECK(!pilot_io_device_lle(103,memory,&unused,&boot_irql));
#ifdef PILOT_MEMORY_LLE
    /* Statistics supplies a larger authored image reservation below. Match
     * that loader input in the original allocator before any dynamic calls. */
    uint32_t fixture_image_size=argc==2 && !strcmp(argv[1],"memory-statistics")?0x20000:0x10000;
    if(argc==2 && !strncmp(argv[1],"callback-synchronize",20))fixture_image_size=0x170000;
    if(argc==2 && !strcmp(argv[1],"sections"))fixture_image_size=0x16000;
    if(argc==3 && (!strcmp(argv[1],"sections-readonly") || !strcmp(argv[1],"thread-component-sections-lle") || !strcmp(argv[1],"thread-component-sections-pinned-lle"))) {
        FILE *input=fopen(argv[2],"rb");uint32_t header[2];CHECK(input);
        CHECK(!fseek(input,0x108,SEEK_SET) && fread(header,4,2,input)==2);fclose(input);
        CHECK(header[1]>header[0] && header[1]<0x60000000);
        fixture_image_size=(header[1]+4095)&~4095u;
    }
#if defined(PILOT_AUDIO_NATIVE) && defined(PILOT_SCHEDULING_LLE) && defined(PILOT_EVENT_TIMER_LLE)
    if(argc>=2 && !strncmp(argv[1],"audio-component-",16)) {
        uint32_t end=0x20000;
        for(unsigned i=0;i<PILOT_AUDIO_COUNT;++i) {
            uint32_t bound=pilot_audio_calls[i].address+pilot_audio_calls[i].bytes;if(bound>end)end=bound;
        }
        for(unsigned i=0;i<sizeof(pilot_audio_guards)/sizeof(pilot_audio_guards[0]);++i) {
            uint32_t bound=pilot_audio_guards[i].address+pilot_audio_guards[i].bytes;if(bound>end)end=bound;
        }
        fixture_image_size=((end+4095)&~4095u)-0x10000;
        image_base=0x10000;*(uint32_t*)(image_bytes+0x10c)=fixture_image_size;
    }
#endif
#ifdef PILOT_D3D_NATIVE
    if(argc>=2 && (!strcmp(argv[1],"graphics") || !strcmp(argv[1],"graphics-scheduler") || !strcmp(argv[1],"graphics-depth-argb") || !strcmp(argv[1],"graphics-x8-texture") || !strncmp(argv[1],"graphics-present",16) || !strncmp(argv[1],"graphics-component-",19) || !strncmp(argv[1],"audio-component-",16))) {
        uint32_t graphics_image_size=((PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_SHADER_CONSTANT_MODE_OFFSET+4+4095)&~4095u)-0x10000;
        if(graphics_image_size>fixture_image_size)fixture_image_size=graphics_image_size;
        image_base=0x10000;*(uint32_t*)(image_bytes+0x10c)=fixture_image_size;
    }
#endif
    CHECK(!pilot_io_device_image(memory,0x10000,fixture_image_size));
#endif
#endif
    if(argc==2 && !strcmp(argv[1],"scheduler")){test_guest_scheduler();return 0;}
#ifdef PILOT_AUDIO_DSP
    if(argc==3 && !strcmp(argv[1],"audio-effects")){test_audio_effects(argv[2]);return 0;}
    if(argc==2 && !strcmp(argv[1],"audio-mix")){test_audio_mix();return 0;}
#if PILOT_AUDIO_HAS_MIXBIN_HEADROOM
    if(argc==2 && !strcmp(argv[1],"audio-headroom")){test_audio_mixbin_headroom();return 0;}
#endif
    if(argc==2 && !strcmp(argv[1],"audio-detach")){test_audio_detach();return 0;}
#ifdef PILOT_AUDIO_ADPCM
    if(argc==2 && !strcmp(argv[1],"audio-adpcm")){test_audio_adpcm();return 0;}
#endif
#endif
    if (argc>1) {
#if defined(PILOT_AUDIO_NATIVE) && defined(PILOT_SCHEDULING_LLE) && defined(PILOT_EVENT_TIMER_LLE)
        if(!strncmp(argv[1],"audio-component-",16)) {
            if(!strcmp(argv[1],"audio-component-callback-guard-lle")) {
                usb_device_callback_depth=1;pilot_audio_dispatch(PILOT_AUDIO_DirectSoundCreate);return 1;
            }
#ifdef PILOT_AUDIO_DSP
            if(!strcmp(argv[1],"audio-component-effects-lle")) {
                CHECK(argc==4);test_thread_audio_effects_lle(argv[2],argv[3]);return 0;
            }
#endif
            CHECK(argc==3);
            if(!strcmp(argv[1],"audio-component-gate-lle")) {test_thread_audio_gate_lle(argv[2]);return 0;}
            int kind=!strcmp(argv[1],"audio-component-cpu-lle")?0:
                !strcmp(argv[1],"audio-component-effects-guard-lle")?1:
                !strcmp(argv[1],"audio-component-device-destructor-guard-lle")?2:
                !strcmp(argv[1],"audio-component-device-release-guard-lle")?3:
                !strcmp(argv[1],"audio-component-buffer-destructor-guard-lle")?4:
                !strcmp(argv[1],"audio-component-buffer-release-guard-lle")?5:-1;
            CHECK(kind>=0);test_thread_audio_component_lle(argv[2],kind);return 0;
        }
#endif
#if defined(PILOT_D3D_NATIVE) && defined(PILOT_SCHEDULING_LLE) && defined(PILOT_EVENT_TIMER_LLE)
        if(!strncmp(argv[1],"graphics-component-",19)) {
            CHECK(argc==3);
#ifndef PILOT_DISPLAY_LLE
            if(!strcmp(argv[1],"graphics-component-reset-lifetime-lle")) {test_thread_graphics_reset_lifetime(argv[2]);return 0;}
            if(!strcmp(argv[1],"graphics-component-pulse-lle")) {test_thread_graphics_pulse_lle(argv[2]);return 0;}
            if(!strcmp(argv[1],"graphics-component-callbacks-lle")) {test_thread_graphics_callback_lle(argv[2]);return 0;}
#endif
            if(!strcmp(argv[1],"graphics-component-gate-lle")) {test_thread_graphics_gate_lle(argv[2]);return 0;}
            if(!strcmp(argv[1],"graphics-component-fence-lle") ||
               !strcmp(argv[1],"graphics-component-fence-zero-guard-lle") ||
               !strcmp(argv[1],"graphics-component-fence-future-guard-lle")) {
                test_thread_graphics_fence_lle(argv[2],!strcmp(argv[1],"graphics-component-fence-lle")?0:
                    !strcmp(argv[1],"graphics-component-fence-zero-guard-lle")?1:2);return 0;
            }
            if(!strcmp(argv[1],"graphics-component-resource-lle") || !strcmp(argv[1],"graphics-component-resource-free-guard-lle")) {
                test_thread_graphics_resource_lle(argv[2],!strcmp(argv[1],"graphics-component-resource-free-guard-lle"));return 0;
            }
            int kind=!strcmp(argv[1],"graphics-component-cpu-lle")?0:
                !strcmp(argv[1],"graphics-component-memory-guard-lle")?1:
                !strcmp(argv[1],"graphics-component-callback-guard-lle")?2:
                !strcmp(argv[1],"graphics-component-pulse-guard-lle")?3:
                !strcmp(argv[1],"graphics-component-constructor-lle")?4:
                !strcmp(argv[1],"graphics-component-vblank-guard-lle")?5:-1;
            CHECK(kind>=0);test_thread_graphics_component_lle(argv[2],kind);return 0;
        }
#endif
#ifdef PILOT_GPU_LLE
        if (!strcmp(argv[1],"gpu-lle-transport")) { test_gpu_lle_transport(); return 0; }
        if (!strcmp(argv[1],"gpu-thread-cpu-lle")) {test_gpu_thread_cpu_lle();return 0;}
        if (!strcmp(argv[1],"gpu-first-context-observer")) {test_gpu_first_context_observer();return 0;}
        if (!strcmp(argv[1],"gpu-compiled-context-prepare")) {test_gpu_compiled_context_prepare();return 0;}
        if (!strcmp(argv[1],"gpu-first-context-capture-guard") && argc==3) {test_gpu_first_context_capture_guard(argv[2]);return 0;}
        if (!strcmp(argv[1],"gpu-thread-av-lle")) {test_gpu_thread_av_lle(0);return 0;}
        if (!strcmp(argv[1],"gpu-thread-av-mode-startup")) {test_gpu_thread_av_lle(1);return 0;}
        if (!strcmp(argv[1],"gpu-thread-av-mode-phase1-startup")) {test_gpu_thread_av_lle(2);return 0;}
        if (!strcmp(argv[1],"gpu-lle-av-mode-startup")) { test_gpu_lle_av_mode_startup(0); return 0; }
        if (!strcmp(argv[1],"gpu-lle-launch-profile")) { test_gpu_lle_launch_profile(); return 0; }
        if (!strcmp(argv[1],"gpu-lle-av-mode-phase1-startup")) { test_gpu_lle_av_mode_startup(1); return 0; }
#ifdef PILOT_GPU_FILES_WORKER
        if (!strcmp(argv[1],"gpu-lle-encoder-timing")) { test_gpu_lle_encoder_timing(); return 0; }
        if (!strcmp(argv[1],"gpu-lle-encoder-phase-guard")) { test_gpu_lle_encoder_phase_guard(); return 0; }
#endif
#endif
#ifdef PILOT_USB_LLE
        if (!strcmp(argv[1],"usb-irq")) { test_usb_irq(); return 0; }
        if (!strcmp(argv[1],"usb-irq-clock")) { test_usb_irq_clock(); return 0; }
        if (!strcmp(argv[1],"usb-lle")) { test_usb_lle(); return 0; }
#if defined(PILOT_INPUT_NATIVE) && defined(PILOT_INPUT_REFERENCE_CLOCK)
        if (!strcmp(argv[1],"usb-reference-replay") || !strcmp(argv[1],"usb-reference-replay-unbound")) {
            test_usb_reference_replay(!strcmp(argv[1],"usb-reference-replay-unbound"));return 0;
        }
#endif
#ifdef PILOT_MEMORY_LLE
        if (!strcmp(argv[1],"usb-original-dma-lock-lle") || !strcmp(argv[1],"usb-original-dma-borrowed-free-lle")) {
            test_usb_original_dma_lock_lle(!strcmp(argv[1],"usb-original-dma-borrowed-free-lle"));return 0;
        }
#endif
        if (!strcmp(argv[1],"usb-dma-release")) {
            install_fault_handler();uint32_t bytes=4096;call_kernel(165,&bytes,1);
            uint32_t base=g_eax;CHECK(base);uint32_t pa=base&0x03ffffffu,value;
            CHECK(usb_dma_read(pa,&value,4));physical_free(base);return 1;
        }
        if (!strcmp(argv[1],"usb-dma-unowned")) {
            install_fault_handler();usb_test_write(0x18,0x1000);usb_test_write(4,0x80);
            Sleep(20);(void)usb_test_read(0x0c);return 1;
        }
        if (!strcmp(argv[1],"usb-dma-image-release")) {
#ifndef PILOT_MEMORY_LLE
            CHECK(!pilot_io_device_image(memory,0x10000,0x10000));
#endif
            uint32_t args[]={0x15000,8,0};call_kernel(175,args,3);
            const char *error=pilot_io_device_image_release(0x15000,4096);
            if(error)stop(error,0x15000);return 1;
        }
        if (!strcmp(argv[1],"usb-dma-unowned-stack")) {
            commit(0x601f0000,0x10000);
            CHECK(!pilot_io_device_stack(memory,0x601f1000,0xf000));
            uint32_t args[]={0x601f0000,8,0};call_kernel(175,args,3);return 1;
        }
#endif
#ifdef PILOT_IO_DEVICE_LLE
        if (!strcmp(argv[1],"io-device-lle")) { test_io_device_lle(); return 0; }
        if (!strcmp(argv[1],"io-device-oversize")) {
            install_fault_handler();io_test_create(0x171,0x22,0,NULL);return 1;
        }
        if (!strcmp(argv[1],"io-device-callback")) {
            install_fault_handler();uint32_t object=io_test_create(12,0x22,0,NULL);
            CHECK(g_eax==0 && object);MEM32(0x19304)=0x12380;
            call_kernel(68,&object,1);return 1;
        }
#endif
#ifdef PILOT_AUDIO_NATIVE
        if (!strcmp(argv[1],"audio")) { test_audio(); return 0; }
#ifdef PILOT_AUDIO_REFERENCE
        if (!strcmp(argv[1],"audio-reference-runtime")) { test_audio_reference_runtime();return 0; }
        if (!strcmp(argv[1],"reference-tsc")) { test_reference_tsc(0);return 0; }
        if (!strcmp(argv[1],"reference-tsc-unbound")) { test_reference_tsc(1);return 1; }
        if (!strcmp(argv[1],"reference-tsc-failure")) { test_reference_tsc(2);return 1; }
#ifdef PILOT_INPUT_REFERENCE_CLOCK
        if (!strcmp(argv[1],"input-reference-clock")) { test_input_reference_clock();return 0; }
#endif
#endif
        if (!strncmp(argv[1],"audio-",6)) { test_audio_failure(argv[1]+6); return 0; }
#endif
#ifdef PILOT_INPUT_NATIVE
        if (!strcmp(argv[1],"input")) { test_input(); return 0; }
        if (!strncmp(argv[1],"input-",6)) { test_input_failure(argv[1]+6); return 0; }
#endif
#ifdef PILOT_D3D_NATIVE
        if (!strcmp(argv[1],"graphics-scheduler")) { test_graphics_guest_scheduler(); return 0; }
        if (!strcmp(argv[1],"graphics")) { test_graphics(0); return 0; }
        if (!strcmp(argv[1],"graphics-depth-argb")) { test_graphics(1); return 0; }
        if (!strcmp(argv[1],"graphics-x8-texture")) { test_graphics(2); return 0; }
        if (!strcmp(argv[1],"graphics-present")) { test_graphics(3); return 0; }
        if (!strcmp(argv[1],"graphics-present-device-removed") || !strcmp(argv[1],"graphics-present-device-reset")) {
            test_desktop_override=1;
            test_desktop_result=!strcmp(argv[1],"graphics-present-device-removed")?(int32_t)0x887a0005u:(int32_t)0x887a0007u;
            test_graphics(3);CHECK(0);return 1;
        }
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
        if (!strcmp(argv[1],"section-mods")) { test_section_mods(1,0x30);return 0; }
        if (!strcmp(argv[1],"section-mods-deferred")) { test_section_mods(0,0x30);return 0; }
        if (!strcmp(argv[1],"section-mods-boundary")) { test_section_mods(1,0);return 0; }
        if (!strcmp(argv[1],"sections-readonly")) { CHECK(argc==3);test_sections_readonly(argv[2]);return 0; }
        if (!strcmp(argv[1],"sections-readonly-recommit")) { CHECK(argc==2);test_sections_readonly_recommit();return 0; }
        if (!strcmp(argv[1],"memory")) { test_memory(); return 0; }
        if (!strcmp(argv[1],"memory-statistics")) { test_memory_statistics(); return 0; }
        if (!strcmp(argv[1],"timers")) { test_timers(); return 0; }
        if (!strcmp(argv[1],"system-clock")) { test_system_clock(); return 0; }
        if (!strcmp(argv[1],"calendar-host-fault")) {
            install_fault_handler();
            *(volatile uint32_t*)(memory+0x40000000u)=1;
            return 0;
        }
        if (!strcmp(argv[1],"threads")) { test_thread_objects(); return 0; }
        if (!strcmp(argv[1],"thread-fp")) { test_thread_fp(); return 0; }
        if (!strcmp(argv[1],"thread-lifetimes")) { test_thread_lifetimes(); return 0; }
        if (!strcmp(argv[1],"events")) { test_event_objects(); return 0; }
        if (!strcmp(argv[1],"mutants")) { test_mutant_objects();return 0; }
        if (!strcmp(argv[1],"mutants-dpc233")) { test_mutant_dpc_wait(0);return 1; }
        if (!strcmp(argv[1],"mutants-dpc234")) { test_mutant_dpc_wait(1);return 1; }
#ifdef PILOT_EVENT_TIMER_LLE
        if (!strcmp(argv[1],"event-timer-lle")) { test_event_timer_lle(); return 0; }
        if (!strcmp(argv[1],"timer-dpc-lle")) { test_timer_dpc_lle(); return 0; }
        if (!strcmp(argv[1],"thread-lle")) { test_thread_lle(); return 0; }
        if (!strcmp(argv[1],"thread-wait-lle")) { test_thread_wait_lle(); return 0; }
        if (!strcmp(argv[1],"thread-timeout-lle")) { test_thread_timeout_lle(); return 0; }
        if (!strcmp(argv[1],"thread-idle-clock-lle")) { test_thread_idle_clock_lle(); return 0; }
        if (!strcmp(argv[1],"thread-idle-runtime-clock-lle")) { thread_idle_runtime_clock=1;test_thread_idle_clock_lle();return 0; }
        if (!strcmp(argv[1],"thread-controlled-clock-lle")) { thread_idle_runtime_clock=2;test_thread_idle_clock_lle();return 0; }
        if (!strcmp(argv[1],"thread-controlled-delayed-clock-lle")) { thread_idle_runtime_clock=3;test_thread_idle_clock_lle();return 0; }
        if (!strcmp(argv[1],"thread-compute-clock-lle")) { test_thread_compute_clock_lle(); return 0; }
        if (!strcmp(argv[1],"thread-compute-controlled-clock-lle")) { thread_idle_runtime_clock=2;test_thread_compute_clock_lle();return 0; }
        if (!strcmp(argv[1],"thread-compute-controlled-delayed-clock-lle")) { thread_idle_runtime_clock=3;test_thread_compute_clock_lle();return 0; }
        if (!strcmp(argv[1],"thread-delay-cpu-lle")) { test_thread_delay_cpu_lle(); return 0; }
        if (!strcmp(argv[1],"thread-priority-cpu-lle")) { test_thread_priority_cpu_lle(); return 0; }
        if (!strcmp(argv[1],"thread-cpu-lle")) { test_thread_cpu_lle(); return 0; }
#ifdef PILOT_SCHEDULING_LLE
        if (!strcmp(argv[1],"thread-calendar-cpu-lle")) { test_thread_calendar_cpu_lle(); return 0; }
        if (!strcmp(argv[1],"thread-heap-compare-cpu-lle")) { test_thread_ordinary_helpers_cpu_lle(269,0); return 0; }
        if (!strcmp(argv[1],"thread-unicode-cpu-lle")) { test_thread_ordinary_helpers_cpu_lle(308,0); return 0; }
        if (!strcmp(argv[1],"thread-unicode-allocate-guard-lle")) {
            CHECK(argc==2 || argc==3);test_thread_ordinary_helpers_cpu_lle(308,argc==3?(uint32_t)strtoul(argv[2],NULL,0):1);return 0;
        }
#endif
        if (!strcmp(argv[1],"thread-child-cpu-lle")) { test_thread_child_cpu_lle(); return 0; }
        if (!strcmp(argv[1],"thread-vm-cpu-lle")) { test_thread_vm_cpu_lle(); return 0; }
        if (!strcmp(argv[1],"thread-vm-alias-cpu-lle")) { test_thread_vm_alias_cpu_lle(); return 0; }
        if (!strcmp(argv[1],"thread-vm-alias-pressure-cpu-lle")) { test_thread_vm_alias_pressure_cpu_lle(); return 0; }
        if (!strcmp(argv[1],"thread-publication-memo-pte-lle")) {test_thread_publication_memo_guard(0);return 0;}
        if (!strcmp(argv[1],"thread-publication-memo-pin-lle")) {test_thread_publication_memo_guard(1);return 0;}
        if (!strcmp(argv[1],"thread-publication-memo-directory-lle")) {test_thread_publication_memo_guard(2);return 0;}
        if (!strcmp(argv[1],"thread-vm-pinned-release-lle")) { test_thread_vm_pinned_release_lle(); return 0; }
        if (!strcmp(argv[1],"thread-files-cpu-lle")) { CHECK(argc==3);test_thread_files_cpu_lle(argv[2]);return 0; }
#if defined(PILOT_FILES_LLE) && defined(PILOT_SCHEDULING_LLE)
        if (!strcmp(argv[1],"thread-component-sections-lle") || !strcmp(argv[1],"thread-component-sections-pinned-lle")) {
            CHECK(argc==3);section_test_pinned=!strcmp(argv[1],"thread-component-sections-pinned-lle");
            test_thread_sections_cpu_lle(argv[2]);return 0;
        }
#endif
        if (!strcmp(argv[1],"thread-contiguous-cpu-lle")) { test_thread_contiguous_cpu_lle(0);return 0; }
        if (!strcmp(argv[1],"thread-contiguous-borrowed-free-lle")) { test_thread_contiguous_cpu_lle(1);return 0; }
        if(!strncmp(argv[1],"thread-component-wait-",22)) {
            CHECK(argc==2);int kind=!strcmp(argv[1],"thread-component-wait-owner-lle")?0:
                !strcmp(argv[1],"thread-component-wait-workspace-guard-lle")?1:
                !strcmp(argv[1],"thread-component-wait-clock-guard-lle")?2:-1;
            CHECK(kind>=0);test_thread_component_wait_lle(kind);return 0;
        }
        if(!strcmp(argv[1],"thread-component-long-owner-lle") || !strcmp(argv[1],"thread-component-long-owner-before-lle") || !strcmp(argv[1],"thread-component-long-owner-unclocked-guard-lle")) {
            CHECK(argc==2);test_thread_component_long_owner_lle(!strcmp(argv[1],"thread-component-long-owner-before-lle")?1:
                !strcmp(argv[1],"thread-component-long-owner-unclocked-guard-lle")?2:0);return 0;
        }
#ifdef PILOT_SCHEDULING_LLE
        if(!strcmp(argv[1],"thread-component-contiguous-lle") || !strcmp(argv[1],"thread-component-contiguous-root-guard-lle")) {
            CHECK(argc==2);test_thread_component_contiguous_lle(!strcmp(argv[1],"thread-component-contiguous-root-guard-lle"));return 0;
        }
#endif
        if(!strncmp(argv[1],"thread-component-physical-",26)) {
            CHECK(argc==2);int kind=!strcmp(argv[1],"thread-component-physical-owner-lle")?0:
                !strcmp(argv[1],"thread-component-physical-free-guard-lle")?1:
                !strcmp(argv[1],"thread-component-physical-protect-guard-lle")?2:-1;
            CHECK(kind>=0);test_thread_component_physical_owner_lle(kind);return 0;
        }
        if(!strcmp(argv[1],"thread-component-pool-cpu-lle") || !strcmp(argv[1],"thread-component-pool-pinned-lle")) {
            CHECK(argc==2);test_thread_component_pool_lle(!strcmp(argv[1],"thread-component-pool-pinned-lle"));return 0;
        }
        if(!strcmp(argv[1],"thread-component-vm-cpu-lle") || !strcmp(argv[1],"thread-component-vm-workspace-lle")) {
            CHECK(argc==2);test_thread_component_vm_lle(!strcmp(argv[1],"thread-component-vm-workspace-lle"));return 0;
        }
        if (!strncmp(argv[1],"thread-component-typed-",23)) {
            CHECK(argc==3);int kind=!strcmp(argv[1],"thread-component-typed-cpu-lle")?0:
                !strcmp(argv[1],"thread-component-typed-stack-lle")?1:!strcmp(argv[1],"thread-component-typed-register-lle")?2:
                !strcmp(argv[1],"thread-component-typed-bytes-lle")?3:-1;
            CHECK(kind>=0);test_thread_component_abi_lle(argv[2],kind);return 0;
        }
        if (!strncmp(argv[1],"thread-component-",17)) {
            CHECK(argc==3);int kind=!strcmp(argv[1],"thread-component-cpu-lle")?0:
                !strcmp(argv[1],"thread-component-abi-lle")?1:!strcmp(argv[1],"thread-component-bytes-lle")?2:
                !strcmp(argv[1],"thread-component-return-lle")?3:-1;
            CHECK(kind>=0);test_thread_component_cpu_lle(argv[2],kind);return 0;
        }
        if (!strcmp(argv[1],"thread-critical-cpu-lle")) { test_thread_critical_cpu_lle(); return 0; }
        if (!strcmp(argv[1],"thread-string-cpu-lle")) {test_thread_string_cpu_lle();return 0;}
        if (!strcmp(argv[1],"thread-string-compare-cpu-lle")) {test_thread_string_compare_cpu_lle();return 0;}
        if (!strcmp(argv[1],"thread-device-cpu-lle")) {test_thread_device_cpu_lle(0);return 0;}
        if (!strcmp(argv[1],"thread-device-wait-dma-lle")) {test_thread_device_cpu_lle(3);return 0;}
        if (!strcmp(argv[1],"thread-device-dma-guard-lle")) { test_thread_device_cpu_lle(2); return 0; }
        if (!strcmp(argv[1],"thread-device-pin-guard-lle")) {test_thread_device_cpu_lle(1);return 0;}
#ifdef PILOT_SCHEDULING_LLE
        if (!strcmp(argv[1],"callback-kernel-guard-lle")) {test_callback_kernel_guard_lle();return 0;}
        if (!strcmp(argv[1],"callback-synchronize-lle")) {test_synchronize_lle(0);return 0;}
        if (!strcmp(argv[1],"callback-synchronize-disconnected-lle")) {test_synchronize_lle(1);return 0;}
        if (!strcmp(argv[1],"idle-isr-stack-lle")) {test_idle_interrupt_stack_lle(0);return 0;}
        if (!strcmp(argv[1],"idle-isr-stack-guard-lle")) {test_idle_interrupt_stack_lle(1);return 0;}
        if (!strcmp(argv[1],"idle-isr-service-stack-lle")) {test_idle_interrupt_stack_lle(2);return 0;}
        if (!strcmp(argv[1],"idle-isr-service-stack-guard-lle")) {test_idle_interrupt_stack_lle(3);return 0;}
        if (!strcmp(argv[1],"idle-dpc-page-memory-lle")) {test_idle_dpc_page_memory_lle();return 0;}
#endif
        if (!strcmp(argv[1],"rtc-cmos-lle")) { test_rtc_cmos_lle(); return 0; }
        if (!strcmp(argv[1],"smbus-smc-lle")) { test_smbus_smc_lle(); return 0; }
        if (!strcmp(argv[1],"settings-lle")) { test_settings_lle(0); return 0; }
        if (!strcmp(argv[1],"settings-lle-checksum")) { test_settings_lle(1); return 0; }
        if (!strcmp(argv[1],"settings-lle-empty")) { test_settings_lle(2); return 0; }
        if (!strcmp(argv[1],"status-map-lle")) { test_status_map_lle(NULL); return 0; }
        if (!strcmp(argv[1],"status-map-audit")) { CHECK(argc==3);test_status_map_lle(argv[2]);return 0; }
        if (!strcmp(argv[1],"thread-lle-blocked")) { thread_lle_reject=1;test_thread_lle();return 0; }
#endif
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
    call_kernel(24,query,5); CHECK(!g_eax && MEM32(0x19104)==6); /* Raw writes retain the API cache. */
    fprintf(stderr,"PASS: isolated threads/TLS/registers, recursive exclusion, handles, EEPROM ABI\n");
    return 0;
}
