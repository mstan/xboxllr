/* Native title pilot. CPU ABI follows Phobos 63f18f0's generated register
 * model. This harness owns launch memory and reached services; no donor
 * kernel, device acknowledgement thread or missing-call success is linked.
 * Unsupported operations terminate before returning to the title.
 */
#include <windows.h>
#include <winternl.h>
#include <bcrypt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "event_trace.h"
#define RECOMP_GENERATED_CODE
#include "recomp_types.h"
#include "pilot_image.h"
#include "guest_cpu_config.h"
#ifdef PILOT_AUDIO_DEVICE_LLE
#include "audio_device_lle.h"
#include "audio_device_pcm_capture.h"
static PilotPcmCapture *original_pcm_capture;
/* Compiled continuations do not borrow the raw engine's lock/DMA capability.
 * Until that transport is independently admitted, stop before device work. */
static const char *original_audio_compiled_guard(uint32_t address,unsigned size,int write,uint32_t *value)
{
    (void)address;(void)size;(void)write;(void)value;
    return "compiled original audio MMIO lacks serialized shared-engine dispatch";
}
#endif

ptrdiff_t g_xbox_mem_offset;
int g_force_return;
uint32_t g_xbox_code_lo, g_xbox_code_hi;
RECOMP_TLS uint32_t g_eax, g_ecx, g_edx, g_esp, g_ebx, g_esi, g_edi, g_ebp;
RECOMP_TLS uint32_t g_fs_base, g_seh_ebp, g_icall_saved_esp, g_icall_dispatch_form;
RECOMP_TLS double g_fp_stack[8];
RECOMP_TLS int g_fp_top, g_fp_cmp, g_df;
/* Original thread context initialization: CW027f, cleared status. The saved
 * MXCSR1f80/tag state is separately scoped by native_thread_fp_reference.py. */
RECOMP_TLS uint16_t g_fp_control_word = 0x027f, g_fp_cc = 0;
RECOMP_TLS RecompXmm g_xmm0, g_xmm1, g_xmm2, g_xmm3, g_xmm4, g_xmm5, g_xmm6, g_xmm7;
RECOMP_TLS RecompMmx g_mm0, g_mm1, g_mm2, g_mm3, g_mm4, g_mm5, g_mm6, g_mm7;
RECOMP_TLS volatile uint32_t g_icall_trace[ICALL_TRACE_SIZE], g_icall_trace_idx;
RECOMP_TLS volatile uint64_t g_icall_count;
static RECOMP_TLS uint32_t recent[32], recent_pos;
static RECOMP_TLS uint64_t entries;
static uint32_t trace_function;
static volatile LONG trace_function_hits;
#ifdef PILOT_USB_LLE
static int trace_original_usb;
static unsigned trace_original_usb_count;
#endif
static int pilot_verbose=1;
static unsigned char *memory, *image_bytes;
static size_t image_size;
static uint32_t image_base, header_bytes, section_table, section_count;
static RECOMP_TLS uint32_t next_kernel_ordinal;
static RECOMP_TLS unsigned current_thread_slot;
static SRWLOCK thread_lock = SRWLOCK_INIT;
static CONDITION_VARIABLE thread_condition = CONDITION_VARIABLE_INIT;
RECOMP_TLS volatile uint32_t *g_pilot_suspend_count;
static SRWLOCK vm_lock=SRWLOCK_INIT;
/* Native-export backing is transport storage, outside hardware apertures.
 * FD000000 is NV2A, so putting XboxHardwareInfo there masks real MMIO reads. */
#include "guest_layout.h"
#define PILOT_THREAD_OBJECT_TYPE (PILOT_KERNEL_DATA_BASE+0x5000u)
struct PilotThread {
    /* execution_handle owns the host lifetime. handle is its borrowed view
     * while the independently identified guest handle remains open. */
    HANDLE handle,reference_handle,execution_handle;
    uint32_t routine,context1,context2,tls_size,stack_size,extra,tid,object,object_refs;
    uint32_t identity;
    int32_t priority;
    volatile uint32_t suspend_count;
    int finished,retiring,stack_registered;
};
static struct PilotThread threads[64];
static unsigned thread_count;
static uint32_t thread_identity_sequence;
static unsigned char eeprom[256];
static int have_eeprom;
static SRWLOCK eeprom_lock=SRWLOCK_INIT;
/* Original settings retains a boot-derived cache. HAL writes alone do not
 * refresh it; this program state is separate from the EEPROM storage owner. */
static unsigned char settings_cache[256];
static int settings_cache_valid;
#ifdef PILOT_USB_LLE
static int shared_eeprom_access(uint8_t offset,int size,int write,uint32_t *value)
{
    if(!value || (size!=1 && size!=2) || (write && *value>(size==1?255u:65535u)))return 0;
    AcquireSRWLockExclusive(&eeprom_lock);
    int valid=have_eeprom;
    if(valid) {
        uint32_t data=0;
        for(int i=0;i<size;++i) {
            uint8_t at=(uint8_t)(offset+i);
            if(write)eeprom[at]=(uint8_t)(*value>>(8*i));
            else data|=(uint32_t)eeprom[at]<<(8*i);
        }
        if(!write)*value=data;
    }
    ReleaseSRWLockExclusive(&eeprom_lock);return valid;
}
#endif
/* Phobos kernel_rtl.c's useful boundary: a host recursive lock keyed by guest
 * VA. Guest fields use Cxbx types.h's 28-byte Xbox layout, not Win32's layout. */
static struct { uint32_t va; CRITICAL_SECTION host; } critical_sections[256];
static unsigned critical_count;
static SRWLOCK critical_lock=SRWLOCK_INIT;

__declspec(noreturn) static void stop(const char *reason, uint32_t va)
{
#ifdef PILOT_AUDIO_DEVICE_LLE
    if(original_pcm_capture) {
        const char *capture_error=pilot_pcm_capture_finish(original_pcm_capture,0);
        if(capture_error)fprintf(stderr,"[ORIGINAL PCM PARTIAL FAILURE] %s\n",capture_error);
        /* Keep callback storage alive with any retained terminal owner. */
    }
#endif
#ifdef PILOT_AUDIO_REFERENCE
    /* Preserve failed observations without acquiring a suspended owner lock. */
    extern int32_t reference_capture_finish(void);
    reference_capture_finish();
#endif
    fprintf(stderr, "[PILOT STOP] %s at %08x; entries=%llu esp=%08x\n",
            reason, va, (unsigned long long)entries, g_esp);
    fprintf(stderr, "[RECENT] oldest to newest:");
    unsigned count = recent_pos < 32 ? recent_pos : 32;
    for (unsigned i = recent_pos - count; i < recent_pos; ++i)
        fprintf(stderr, " %08x", recent[i & 31]);
    fprintf(stderr, "\n");
    fflush(stderr);
    ExitProcess(2);
}

#include "thread_affinity.inc"
#ifdef PILOT_AUDIO_REFERENCE
static void audio_reference_clock_start(void);
static uint64_t audio_reference_counter(void);
#endif
#ifdef PILOT_USB_LLE
#include "io_device_lle.h"
#include "usb_lle.h"
static void usb_poll(void);
static RECOMP_TLS unsigned usb_in_interrupt;
#ifdef PILOT_SCHEDULING_LLE
static RECOMP_TLS unsigned usb_device_callback_depth;
#if defined(PILOT_D3D_NATIVE) && !defined(PILOT_DISPLAY_LLE)
static const char *graphics_original_clock_bind(void);
static void graphics_original_callback_dpc(void);
#endif
#endif
static int cpu_interrupt_flag=1,cpu_interrupt_shadow;
#endif
#include "thread_scheduler.inc"
#ifdef PILOT_DISPLAY_LLE
static void display_lle_tick(void);
static const char *display_lle_access(uint32_t address,unsigned size,int write,uint32_t *value);
static uint8_t display_lle_field(void);
static uint32_t display_lle_completed(void);
static void display_lle_present(uint32_t swap,uint32_t interval);
#endif
#ifdef PILOT_GPU_LLE
static void gpu_lle_tick(void);
static const char *gpu_lle_access(uint32_t address,unsigned size,int write,uint32_t *value);
static const char *gpu_lle_port(uint16_t port,int size,int write,uint32_t *value);
#endif
static uint32_t file32(size_t offset)
{
    uint32_t value;
    if (offset > image_size || image_size - offset < 4) stop("invalid XBE range", (uint32_t)offset);
    memcpy(&value, image_bytes + offset, 4);
    return value;
}

static void commit(uint32_t address, uint32_t size)
{
    if (!size || (uint64_t)address + size > 0x100000000ull)
        stop("invalid guest allocation", address);
    AcquireSRWLockExclusive(&vm_lock);
    if (!VirtualAlloc(memory + address, size, MEM_COMMIT, PAGE_READWRITE))
        stop("host memory commit failed", address);
    ReleaseSRWLockExclusive(&vm_lock);
}

void recomp_unimpl(const char *text, uint32_t va) { stop(text, va); }
void recomp_icall_fail_log(uint32_t va) { stop("unresolved indirect target", va); }
void recomp_icall_not_code_log(uint32_t va, uint32_t saved) { (void)saved; stop("non-code indirect target", va); }
void recomp_int3_reached(uint32_t va) { stop("guest breakpoint", va); }
void recomp_cxx_throw(uint32_t object, uint32_t info) { (void)info; stop("C++ exception unwinding unsupported", object); }
void recomp_debug_service(uint32_t service, uint32_t argument) { (void)argument; stop("debug service unsupported", service); }
uint32_t recomp_eflags_push(void) { stop("PUSHFD arithmetic flags not exported by donor lowering", recent[(recent_pos-1)&31]); }
void recomp_eflags_pop(uint32_t value) { (void)value; stop("POPFD arithmetic flags not imported by donor lowering", recent[(recent_pos-1)&31]); }
void pilot_cpu_cli(void)
{
#ifdef PILOT_USB_LLE
    cpu_interrupt_flag=0;cpu_interrupt_shadow=0;pilot_io_device_set_if(0);
#else
    stop("CLI requires the interrupt LLE build",recent[(recent_pos-1)&31]);
#endif
}
void pilot_cpu_sti(void)
{
#ifdef PILOT_USB_LLE
    cpu_interrupt_shadow=!cpu_interrupt_flag;cpu_interrupt_flag=1;pilot_io_device_set_if(1);
#else
    stop("STI requires the interrupt LLE build",recent[(recent_pos-1)&31]);
#endif
}
void pilot_cpu_sti_complete(void)
{
#ifdef PILOT_USB_LLE
    cpu_interrupt_shadow=0;usb_poll();scheduler_checkpoint();
#else
    stop("STI completion requires the interrupt LLE build",0);
#endif
}
void pilot_cpu_wbinvd(void)
{
    /* The selected CPU/device bus has one coherent backing and no deferred
     * CPU data-cache/write-combining store. Earlier guest stores are already
     * in that backing; order them before any later MMIO/DMA publication.
     * There are no private emulated CPU cache lines left to invalidate.
     * This is not a GPU completion wait or permission to flush guest work.
     * EFLAGS and guest register TLS are untouched. Native privilege/cache
     * timing and modified executable-code translation are separate contracts. */
    MemoryBarrier();
}
uint8_t pilot_cpu_in8(uint16_t port)
{
#ifdef PILOT_DISPLAY_LLE
    if(port==0x80c0)return display_lle_field();
#endif
#ifdef PILOT_GPU_LLE
    if(port==0x80c0 || port==0x80d3 || port==0x80d6 || port==0x80d8 || (port>=0xcf8 && port<=0xcff)) {
        uint32_t value=0;
        const char *error=gpu_lle_port(port,1,0,&value);
        if(error)stop(error,port);
        return (uint8_t)value;
    }
#endif
#ifdef PILOT_USB_LLE
    uint32_t value=0;
    const char *error=(port==0x80cf || (port>=0x70 && port<=0x73) || (port>=0xc000 && port<=0xc00f))?pilot_usb_board_port(port,1,0,&value):
        pilot_usb_pic_port(port,1,0,&value);
    if(error)stop(error,port);
    return (uint8_t)value;
#else
    stop("port read requires the machine bus",port);
    return 0; /* Unreachable after the explicit diagnostic. */
#endif
}
#include "nonlocal_jump.inc"
void pilot_cpu_out8(uint16_t port,uint8_t byte)
{
#ifdef PILOT_GPU_LLE
    if(port==0x80c0 || port==0x80d3 || port==0x80d6 || port==0x80d8 || (port>=0xcf8 && port<=0xcff)) {
        uint32_t value=byte;
        const char *error=gpu_lle_port(port,1,1,&value);
        if(error)stop(error,port);
        return;
    }
#endif
#ifdef PILOT_USB_LLE
    uint32_t value=byte;
    const char *error=(port==0x80cf || (port>=0x70 && port<=0x73) || (port>=0xc000 && port<=0xc00f))?pilot_usb_board_port(port,1,1,&value):
        pilot_usb_pic_port(port,1,1,&value);
    if(error)stop(error,port);
#else
    stop("port write requires the machine bus",port);
#endif
}
void pilot_thread_checkpoint(void)
{
#if defined(PILOT_SCHEDULING_LLE) && defined(PILOT_USB_LLE)
    /* A whole selected component returns to an unchanged original PC with
     * the engine's retained flags/FP state. Its completion is the handoff;
     * a generated compute checkpoint cannot export this engine context. */
    if(pilot_io_device_thread_component_active())return;
#endif
#ifdef PILOT_USB_LLE
    if(pilot_io_device_thread_checkpoint_request())
        stop("original native compute requires a certified PC/flags checkpoint",0);
#endif
#ifdef PILOT_USB_LLE
    usb_poll();
#endif
    if(scheduler_enabled){scheduler_checkpoint();return;}
    if (!current_thread_slot) return;
    AcquireSRWLockExclusive(&thread_lock);
    while (threads[current_thread_slot].suspend_count) {
        if (!SleepConditionVariableSRW(&thread_condition,&thread_lock,INFINITE,0))
            stop("guest suspension wait failed",GetLastError());
    }
    ReleaseSRWLockExclusive(&thread_lock);
}
static void thread_finished(void)
{
    scheduler_retire();
    AcquireSRWLockExclusive(&thread_lock);
    threads[current_thread_slot].finished=1;
    ReleaseSRWLockExclusive(&thread_lock);
}
void recomp_trace_enter(const char *name, uint32_t va)
{
    PILOT_THREAD_CHECKPOINT();
#ifdef PILOT_USB_LLE
    if(trace_original_usb && trace_original_usb_count<128 &&
       (va==0x188247u || va==0x188de9u || va==0x188f41u || va==0x188fc8u || va==0x1890a2u)) {
        uint32_t controller=va==0x1890a2u?MEM32(g_esp+8):g_ecx;
        uint32_t head=MEM32(controller+0x42c);
        uint32_t ed=va==0x188247u?g_edx:head?MEM32(head+0x10):0;
        fprintf(stderr,"[USB ORIGINAL FLOW] sample=%u va=%08x eax=%08x ecx=%08x edx=%08x esi=%08x edi=%08x ebp=%08x esp=%08x controller=%08x cancel=%08x posted=%08x high=%08x frame=%04x ed=%08x flags=%02x target=%08x count=%u sf_byte=%u\n",
            ++trace_original_usb_count,va,g_eax,g_ecx,g_edx,g_esi,g_edi,g_ebp,g_esp,controller,head,
            MEM32(controller+0x438),MEM32(controller+0x418),MEM16(MEM32(controller+8)+0x80),ed,
            ed?MEM8(ed+0x10):0,ed?MEM32(ed+0x1c):0,ed?MEM8(ed+0x20):0,
            va==0x188f41u || va==0x188fc8u?MEM8(g_ebp+0xf):0);
    }
#endif
    (void)name;
    recent[recent_pos++ & 31] = va;
    ++entries;
    if (entries <= 24) fprintf(stderr, "[ENTER] %08x\n", va);
    if(trace_function && va==trace_function && InterlockedIncrement(&trace_function_hits)<=8) {
        fprintf(stderr,"[FUNCTION TRACE] va=%08x thread=%u eax=%08x ecx=%08x edx=%08x ebx=%08x esi=%08x edi=%08x ebp=%08x esp=%08x stack=",
                va,current_thread_slot,g_eax,g_ecx,g_edx,g_ebx,g_esi,g_edi,g_ebp,g_esp);
        for(unsigned i=0;i<8;++i)fprintf(stderr," %08x",MEM32(g_esp+i*4));
        fprintf(stderr," recent=");unsigned n=recent_pos<32?recent_pos:32;
        for(unsigned i=recent_pos-n;i<recent_pos;++i)fprintf(stderr," %08x",recent[i&31]);
        fprintf(stderr,"\n");
    }
}
void recomp_trace_exit(const char *name, uint32_t va) { (void)name; (void)va; }
void recomp_trace_esp(const char *name, const char *tag) { fprintf(stderr, "[ESP] %s %s %08x\n", name, tag, g_esp); }
uint64_t xbox_ReadTimeStampCounter(void)
{
#ifdef PILOT_AUDIO_REFERENCE
    return audio_reference_counter();
#else
    LARGE_INTEGER now, frequency;
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&frequency);
    return (uint64_t)(now.QuadPart / frequency.QuadPart) * 733333333ull +
           (uint64_t)(now.QuadPart % frequency.QuadPart) * 733333333ull / frequency.QuadPart;
#endif
}
void recomp_cpuid(uint32_t leaf, uint32_t subleaf, uint32_t out[4])
{
    if(pilot_guest_cpuid(leaf,subleaf,out))stop("CPUID leaf unsupported",leaf);
}

/* Whole cdecl capability-query boundary. General PUSHFD/POPFD still require
 * flag-aware lowering. Report the configured guest CPU, not the host CPU. */
void pilot_cpu_mmx_available(void)
{
    uint32_t capabilities[4];
    recomp_cpuid(1,0,capabilities);
    g_ecx=capabilities[2];
    g_eax=g_edx=(capabilities[3]>>23)&1u;
    g_esp+=4;
}

recomp_func_t recomp_lookup_manual(uint32_t va) { (void)va; return NULL; }
static DWORD WINAPI title_thread(void *argument)
{
    thread_guest_cpu_enter();
    unsigned slot=(unsigned)(uintptr_t)argument;
    struct PilotThread *thread=&threads[slot];
    current_thread_slot=slot;
    g_pilot_suspend_count=&thread->suspend_count;
    scheduler_enter(slot);
    PILOT_THREAD_CHECKPOINT();
    uint32_t base=0x60000000u+slot*0x100000u;
    uint32_t stack_top=base+0x100000u;
    uint32_t tls=stack_top-thread->tls_size;
    g_fs_base=base;
    commit(base,0x2000+thread->extra);
    commit(stack_top-thread->stack_size,thread->stack_size);
#ifdef PILOT_USB_LLE
    const char *stack_error=pilot_io_device_stack(memory,stack_top-thread->stack_size,thread->stack_size);
    if(stack_error)stop(stack_error,stack_top-thread->stack_size);
    thread->stack_registered=1;
#ifdef PILOT_MEMORY_LLE
    stack_error=pilot_io_device_resident(stack_top-thread->stack_size,thread->stack_size);
    if(stack_error)stop(stack_error,stack_top-thread->stack_size);
#endif
#endif
    MEM32(base)=0xffffffffu;
    MEM32(base+4)=stack_top;
    MEM32(base+8)=stack_top-thread->stack_size;
    MEM32(base+0x18)=base;
    MEM32(base+0x1c)=base;
    MEM32(base+0x20)=base+0x28;
    MEM32(base+0x28)=base+0x1000;
    MEM32(base+0x1000+0x28)=thread->tls_size?tls:0;
    MEM32(base+0x1000+0x12c)=thread->tid;
    uint32_t directory=file32(0x12c);
    if (directory && thread->tls_size) {
        uint32_t start=MEM32(directory),end=MEM32(directory+4);
        if (end<start || end-start>thread->tls_size) stop("thread TLS template does not fit",directory);
        memcpy(memory+tls,memory+start,end-start);
    }
    g_esp=(tls&~15u)-12;
    MEM32(g_esp)=0;
    MEM32(g_esp+4)=thread->context1;
    MEM32(g_esp+8)=thread->context2;
    recomp_func_t fn=recomp_lookup(thread->routine);
    if (!fn) stop("thread routine is not generated",thread->routine);
    fprintf(stderr,"[THREAD] id=%u routine=%08x context=%08x,%08x tls=%u\n",
            thread->tid,thread->routine,thread->context1,thread->context2,thread->tls_size);
    fn();
    /* A returned system routine ends this actual host thread. */
    thread_finished();
    return g_eax;
}

#include "thread_lifetime.inc"

static void create_thread(void)
{
    uint32_t args[10];
    for (unsigned i=0;i<10;++i) args[i]=MEM32(g_esp+4+i*4);
    if (!args[0]) { g_eax=0xc000000du; return; }
    uint32_t stack=args[2] ? args[2] : file32(0x130);
    if (!stack) stack=0x10000;
    if (stack>0xc0000 || args[3]>0x10000 || args[1]>0x10000 || args[3]+4096>stack)
        stop("thread allocation exceeds pilot scope",args[9]);
    if (!recomp_lookup(args[9])) stop("thread entry not generated",args[9]);
    thread_reap();
    AcquireSRWLockExclusive(&thread_lock);
    unsigned slot;
    for(slot=1;slot<=thread_count && threads[slot].object;++slot){}
    if(slot==64 || thread_identity_sequence==0x400000u) {
        ReleaseSRWLockExclusive(&thread_lock); g_eax=0xc000009au; return;
    }
    int new_slot=slot>thread_count;
    if(new_slot)thread_count=slot;
    struct PilotThread *thread=&threads[slot];
    thread->identity=0x40000000u|(thread_identity_sequence++<<6)|slot;
    thread->routine=args[9]; thread->context1=args[5]; thread->context2=args[6];
    thread->tls_size=args[3]; thread->stack_size=(stack+4095)&~4095u; thread->extra=args[1];
    thread->suspend_count=args[7]!=0;
    /* The object exists while the new host thread is still suspended. Native
     * reference/priority services must not wait for its guest entry to run. */
    uint32_t context_base=0x60000000u+slot*0x100000u;
    commit(context_base,0x2000+thread->extra);thread->object=context_base+0x1000;
    DWORD tid;
    thread->execution_handle=CreateThread(NULL,0,title_thread,(void*)(uintptr_t)slot,CREATE_SUSPENDED,&tid);
    if (!thread->execution_handle) {
        AcquireSRWLockExclusive(&vm_lock);
        if(!VirtualFree(memory+context_base,0x100000u,MEM_DECOMMIT))stop("failed thread context retirement",context_base);
        ReleaseSRWLockExclusive(&vm_lock);
        memset(thread,0,sizeof(*thread));if(new_slot)--thread_count;
        ReleaseSRWLockExclusive(&thread_lock); g_eax=0xc000009au; return;
    }
    thread->handle=thread->execution_handle;
    thread->tid=tid;
    scheduler_publish(slot,thread->suspend_count,thread->priority);
    MEM32(thread->object+0x28)=thread->tls_size?context_base+0x100000u-thread->tls_size:0;
    MEM32(thread->object+0x12c)=tid;
    MEM32(args[0])=thread->identity;
    if (args[4]) MEM32(args[4])=tid;
    /* OS suspension is used only before entry, to publish metadata. Guest
     * suspension thereafter waits at generated-code points with no host locks. */
    if (ResumeThread(thread->handle)==(DWORD)-1) stop("cannot start created thread",tid);
    ReleaseSRWLockExclusive(&thread_lock);
    g_eax=0;
}

static void critical_section(unsigned operation,uint32_t va)
{
    if (!va) stop("null critical section",va);
    AcquireSRWLockExclusive(&critical_lock);
    unsigned index;
    for (index=0;index<critical_count;++index) if (critical_sections[index].va==va) break;
    if (index==critical_count) {
        if (critical_count==256) stop("critical section table exhausted",va);
        InitializeCriticalSection(&critical_sections[index].host);
        critical_sections[index].va=va;
        ++critical_count;
    }
    ReleaseSRWLockExclusive(&critical_lock);
    CRITICAL_SECTION *host=&critical_sections[index].host;
    if (operation==291) {
        if (host->RecursionCount) stop("reinitializing owned critical section",va);
        memset(memory+va,0,28);
        MEM32(va)=0x00040001u; /* synchronization event, size four ULONGs */
        MEM32(va+8)=va+8; MEM32(va+12)=va+8;
        MEM32(va+16)=0xffffffffu;
    } else if (operation==277) {
        InterlockedIncrement((LONG*)(memory+va+16));
        EnterCriticalSection(host);
        MEM32(va+20)++;
        MEM32(va+24)=MEM32(g_fs_base+0x28);
    } else {
        if (MEM32(va+24)!=MEM32(g_fs_base+0x28) || !MEM32(va+20))
            stop("critical section release without ownership",va);
        if (--MEM32(va+20)==0) MEM32(va+24)=0;
        InterlockedDecrement((LONG*)(memory+va+16));
        LeaveCriticalSection(host);
    }
}

#include "status_map.inc"
#include "counted_string.inc"

static void query_setting(void)
{
    uint32_t index=MEM32(g_esp+4),type=MEM32(g_esp+8),value=MEM32(g_esp+12);
    uint32_t length=MEM32(g_esp+16),result=MEM32(g_esp+20);
    /* XC_* user-settings layout from Cxbx EmuEEPROM.h/types.h. These are
     * actual file-backed settings; encrypted/factory queries remain explicit. */
    static const unsigned char offsets[]={4,8,24,40,12,28,44,48,52,56,60,64,68,72,76,80,84,88,92};
    if (index>0xff) stop("EEPROM index outside pilot scope",index);
    if (index>=sizeof(offsets) && index!=0xff) { g_eax=0xc0000034u; return; }
    if (!have_eeprom) stop("console settings require EEPROM input",index);
    uint32_t size=index==0xff ? 96 : 4;
    AcquireSRWLockExclusive(&eeprom_lock);
    if(!settings_cache_valid) {
        memcpy(settings_cache,eeprom,sizeof(settings_cache));
        for(unsigned i=0;i<sizeof(settings_cache);++i)
            if(settings_cache[i]!=0xff){settings_cache_valid=1;break;}
        if(!settings_cache_valid) {
            ReleaseSRWLockExclusive(&eeprom_lock);g_eax=0xc0000184u;return;
        }
    }
    uint64_t checksum=0;
    for (unsigned i=0x60;i<0xc0;i+=4) {
        uint32_t part; memcpy(&part,settings_cache+i,4); checksum+=part;
        checksum=(checksum&0xffffffffu)+(checksum>>32);
    }
    if ((uint32_t)checksum!=0xffffffffu) {
        ReleaseSRWLockExclusive(&eeprom_lock);g_eax=0xc000009cu;return;
    }
    unsigned char payload[96];
    memcpy(payload,settings_cache+0x60+(index==0xff ? 0 : offsets[index]),size);
    ReleaseSRWLockExclusive(&eeprom_lock);
    if (length<size) { g_eax=0xc0000023u; return; }
    /* Guest output accesses may enter memory transport. Keep owner locks out
     * of that path; payload is only this query's immutable read snapshot. */
    MEM32(type)=(index==1 || index==2 || index==4 || index==5 || index==18 || index==0xff) ? 3 : 4;
    if (result) MEM32(result)=size;
    memset(memory+value,0,length);
    memcpy(memory+value,payload,size);
    g_eax=0;
}

#include "dispatcher.inc"
#include "system_time.inc"
#include "event_objects.inc"
#ifdef PILOT_IO_DEVICE_LLE
#include "io_device_lle.h"
#endif
#include "virtual_memory.inc"
#include "physical_memory.inc"
#include "pool_memory.inc"
#ifdef PILOT_SCHEDULING_LLE
#include "component_owner.inc"
static void scheduling_lle_bootstrap(void);
static void scheduling_lle_execute(uint32_t entry);
#endif
#ifdef PILOT_USB_LLE
#include "usb_lle.inc"
#include "component_wait.inc"
#endif
#ifdef PILOT_GPU_LLE
#include "gpu_lle.inc"
#endif
#include "sections.inc"
#include "media.inc"
#include "cache.inc"
#ifdef PILOT_FILES_LLE
#include "files_lle.inc"
#endif
#if defined(PILOT_MEDIA_INSTALLED) && defined(PILOT_SCHEDULING_LLE)
#include "pilot_media_cpu_binding.h"
static const char *installed_media_component(uint32_t entry,uint32_t regs[8])
{
    if(entry!=PILOT_MEDIA_CPU_ENTRY)return "unbound installed-media CPU helper";
    const char *owner_error=native_component_original_enter();if(owner_error)return owner_error;
#ifdef PILOT_FILES_LLE
    /* Each original thread leases its own stack workspace. All actual file,
     * VM and close calls may yield through the existing original scheduler. */
    regs[0]=files_verify(PILOT_XBE_SHA256);
    fprintf(stderr,"[MEDIA VERIFY CPU LLE FILES] actual installed HDD XBE status=%08x\n",regs[0]);
#else
    AcquireSRWLockExclusive(&media_lock);regs[0]=media_verify(PILOT_XBE_SHA256);
    ReleaseSRWLockExclusive(&media_lock);
    fprintf(stderr,"[MEDIA VERIFY CPU] installed default.xbe identity/status=%08x\n",regs[0]);
#endif
    owner_error=native_component_original_leave();if(owner_error)return owner_error;
    regs[4]+=4;return NULL;
}
#endif
#ifdef PILOT_SCHEDULING_LLE
#include "scheduling_lle.inc"
#endif
#include "memory_statistics.inc"
#include "thread_objects.inc"
#ifdef PILOT_AUDIO_NATIVE
#include "audio.inc"
#endif
#ifdef PILOT_INPUT_NATIVE
#include "input.inc"
#endif
#ifdef PILOT_D3D_NATIVE
#include "graphics.inc"
#endif
#if defined(PILOT_SCHEDULING_LLE) && defined(PILOT_D3D_NATIVE)
static const char *native_graphics_component(uint32_t entry,uint32_t regs[8])
{
    unsigned index;for(index=0;index<PILOT_D3D_COUNT && pilot_d3d_calls[index].address!=entry;++index){}
    if(index==PILOT_D3D_COUNT || !pilot_io_device_thread_component_active())
        return "unbound original CPU native graphics entry";
    /* Reset/teardown own real original timed waits for queued work before
     * replacement/collection. Without the clock owner, reject pre-mutation. */
    if((index==PILOT_D3D_D3DDevice_Reset || index==PILOT_D3D_D3DDevice_Release) &&
       !pilot_io_device_clock_bound())
        return "original scheduling native graphics teardown requires bound clock";
    /* These native paths still use a generated guest callback/native pulse
     * dispatcher. Stop before queueing work against the original scheduler. */
    if((index==PILOT_D3D_D3DDevice_InsertCallback || index==PILOT_D3D_D3DDevice_SetVerticalBlankCallback ||
       graphics_gpu_head || graphics_gpu_running || graphics_callback)
#ifndef PILOT_DISPLAY_LLE
       && !graphics_original_clock_bound
#endif
       )
        return "original scheduling native graphics callback ownership requires integration";
#ifndef PILOT_DISPLAY_LLE
    if(index==PILOT_D3D_D3DDevice_Swap && !graphics_original_clock_bound)
        return "original scheduling native graphics display pulse requires integration";
#endif
    /* Display waits have an independent pulse owner. GPU fence waits use
     * actual native completion and the original passive timed-wait route. */
    if(index==PILOT_D3D_D3DDevice_BlockUntilVerticalBlank
#ifndef PILOT_DISPLAY_LLE
       && !graphics_original_clock_bound
#endif
       )
        return "original scheduling native graphics wait ownership requires integration";
    /* The original whole vblank API owns only a display-event lifetime.
     * Holding the shared renderer/audio gate across this wait introduces
     * priority inversion against Blinx's lower-priority display worker. */
    int display_wait=index==PILOT_D3D_D3DDevice_BlockUntilVerticalBlank;
    const char *error=display_wait?NULL:graphics_original_enter();
    if(error)return error;
    if(index==PILOT_D3D_D3DDevice_BlockOnFence) {
        uint32_t token=MEM32(regs[4]+4);
        AcquireSRWLockExclusive(&graphics_lock);
        int invalid=!token || token>=graphics_fence_next;
        ReleaseSRWLockExclusive(&graphics_lock);
        if(invalid) {
            error=graphics_original_leave();
            return error?error:"unowned GPU fence";
        }
    }
    uint32_t saved[]={g_eax,g_ecx,g_edx,g_ebx,g_esp,g_ebp,g_esi,g_edi};
    g_eax=regs[0];g_ecx=regs[1];g_edx=regs[2];g_ebx=regs[3];
    g_esp=regs[4];g_ebp=regs[5];g_esi=regs[6];g_edi=regs[7];
    pilot_d3d_dispatch(index);
    uint32_t returned[]={g_eax,g_ecx,g_edx,g_ebx,g_esp,g_ebp,g_esi,g_edi};
    memcpy(regs,returned,sizeof(returned));
    g_eax=saved[0];g_ecx=saved[1];g_edx=saved[2];g_ebx=saved[3];
    g_esp=saved[4];g_ebp=saved[5];g_esi=saved[6];g_edi=saved[7];
    return display_wait?NULL:graphics_original_leave();
}
static void native_graphics_components_bind(void)
{
    if(graphics_original_gate)stop("original graphics components already bound",graphics_original_gate);
    /* Before original CPU admission there are no component contenders or host
     * renderer locks. The normal original pool owner publishes this real RTL
     * object's storage; initialization is VOID, not an EAX success status. */
    native_component_gate_bind();graphics_original_gate=native_component_original_gate;
    for(unsigned index=0;index<PILOT_D3D_COUNT;++index) {
        const char *error=pilot_io_device_thread_component_bind_abi(pilot_d3d_calls[index].address,
            pilot_d3d_calls[index].bytes,pilot_d3d_calls[index].sha256,
            pilot_d3d_calls[index].stack_bytes,native_graphics_component);
        if(error)stop(error,pilot_d3d_calls[index].address);
    }
    fprintf(stderr,"[GRAPHICS CPU COMPONENTS] bound=%u; original flags/FP/thread owner retained\n",PILOT_D3D_COUNT);
}
#endif

#if defined(PILOT_SCHEDULING_LLE) && defined(PILOT_AUDIO_NATIVE)
static const char *native_audio_component(uint32_t entry,uint32_t regs[8])
{
    unsigned index;for(index=0;index<PILOT_AUDIO_COUNT && pilot_audio_calls[index].address!=entry;++index){}
    if(index==PILOT_AUDIO_COUNT || !pilot_io_device_thread_component_active())
        return "unbound original CPU native audio entry";
    if(index==PILOT_AUDIO_IDirectSound_DownloadEffectsImage) {
#ifdef PILOT_AUDIO_DSP
        if(!audio_device || MEM32(regs[4]+4)!=audio_device+8)
            return "unowned original CPU native audio effects device";
#else
        return "original scheduling native audio effects ownership requires integration";
#endif
    }
    const char *error=audio_original_enter();if(error)return error;
    uint32_t saved[]={g_eax,g_ecx,g_edx,g_ebx,g_esp,g_ebp,g_esi,g_edi};
    g_eax=regs[0];g_ecx=regs[1];g_edx=regs[2];g_ebx=regs[3];
    g_esp=regs[4];g_ebp=regs[5];g_esi=regs[6];g_edi=regs[7];
    pilot_audio_dispatch(index);
    uint32_t returned[]={g_eax,g_ecx,g_edx,g_ebx,g_esp,g_ebp,g_esi,g_edi};memcpy(regs,returned,sizeof(returned));
    g_eax=saved[0];g_ecx=saved[1];g_edx=saved[2];g_ebx=saved[3];
    g_esp=saved[4];g_ebp=saved[5];g_esi=saved[6];g_edi=saved[7];
    error=audio_original_leave();if(error)return error;
#ifdef PILOT_AUDIO_DSP
    /* The qualified successful upload owns its copied graph/descriptor.
     * Original allocation/install failures retire old state earlier than
     * this backend's transactional rollback; never resume as equivalent. */
    if(index==PILOT_AUDIO_IDirectSound_DownloadEffectsImage && (int32_t)regs[0]<0)
        return "original audio effects failure-state semantics require qualification";
#endif
    return NULL;
}
static const char *native_audio_object_guard(uint32_t entry,uint32_t regs[8])
{
    (void)entry;(void)regs;
    return "original audio inner object destruction requires its native lifetime owner";
}
static void native_audio_components_bind(void)
{
    if(audio_original_gate)stop("original audio components already bound",audio_original_gate);
    native_component_gate_bind();audio_original_gate=native_component_original_gate;
    const char *error=NULL;
    for(unsigned index=0;index<PILOT_AUDIO_COUNT;++index) {
        error=pilot_io_device_thread_component_bind_abi(pilot_audio_calls[index].address,pilot_audio_calls[index].bytes,
            pilot_audio_calls[index].sha256,pilot_audio_calls[index].stack_bytes,native_audio_component);
        if(error)stop(error,pilot_audio_calls[index].address);
    }
    for(unsigned index=0;index<sizeof(pilot_audio_guards)/sizeof(pilot_audio_guards[0]);++index) {
        error=pilot_io_device_thread_component_bind_abi(pilot_audio_guards[index].address,pilot_audio_guards[index].bytes,
            pilot_audio_guards[index].sha256,4,native_audio_object_guard);
        if(error)stop(error,pilot_audio_guards[index].address);
    }
    fprintf(stderr,"[AUDIO CPU COMPONENTS] bound=%u; inner object destruction guarded; original flags/FP/thread owner retained\n",PILOT_AUDIO_COUNT);
}
#endif

static void kernel_dispatch_impl(void)
{
#ifdef PILOT_FILES_LLE
    if(files_lle_kernel(next_kernel_ordinal))return;
#endif
    const char *name = "unknown ordinal";
    for (size_t i=0; i<sizeof(pilot_imports)/sizeof(pilot_imports[0]); ++i)
        if (pilot_imports[i].ordinal == next_kernel_ordinal) name=pilot_imports[i].name;
    if(pilot_verbose)fprintf(stderr, "[KERNEL] %u %s return=%08x\n", next_kernel_ordinal, name, MEM32(g_esp));
    switch (next_kernel_ordinal) {
#ifdef PILOT_GPU_LLE
    case 2:case 3:case 45:case 50: {
        uint32_t args[6],result=g_eax;unsigned count=next_kernel_ordinal==3?6:4;
        for(unsigned i=0;i<count;++i)args[i]=MEM32(g_esp+4+i*4);
        gpu_lle_tick();
        fprintf(stderr,"[AV KERNEL LLE] ordinal=%u option/step=%08x param/mode=%08x\n",
                next_kernel_ordinal,args[1],args[2]);
        const char *error=pilot_io_device_lle(next_kernel_ordinal,memory,args,&result);
        if(error)stop(error,next_kernel_ordinal);
        g_eax=result;g_esp+=4+count*4;return;
    }
    case 168: {
        uint32_t args[]={MEM32(g_esp+4),MEM32(g_esp+8)},result=0;
        const char *error=pilot_io_device_lle(168,memory,args,&result);
        if(error)stop(error,168);
        fprintf(stderr,"[GPU INSTANCE LLE] bytes=%08x top=%08x padding=%08x\n",args[0],result,MEM32(args[1]));
        g_eax=result;g_esp+=12;return;
    }
#endif
#ifdef PILOT_USB_LLE
    case 98:case 100:case 103:case 129:case 151:case 160:case 161: {
        uint32_t arg=next_kernel_ordinal>=160?g_ecx:
            (next_kernel_ordinal==98 || next_kernel_ordinal==100 || next_kernel_ordinal==151)?MEM32(g_esp+4):0;
        uint32_t result=g_eax;
        const char *error=pilot_io_device_lle(next_kernel_ordinal,memory,&arg,&result);
        if(error)stop(error,next_kernel_ordinal);
        if(next_kernel_ordinal==98 || next_kernel_ordinal==100 || next_kernel_ordinal==103 ||
           next_kernel_ordinal==129 || next_kernel_ordinal==160)g_eax=(g_eax&~255u)|(result&255);
        if(g_fs_base)MEM8(g_fs_base+0x24)=(uint8_t)pilot_io_device_irql();
        g_esp+=(next_kernel_ordinal==98 || next_kernel_ordinal==100 || next_kernel_ordinal==151)?8:4;
        return;
    }
#endif
#ifdef PILOT_IO_DEVICE_LLE
    case 44:case 46:case 47:case 65:case 68:case 109:case 173:case 175: {
        uint32_t args[7],result=g_eax;
        unsigned count=(next_kernel_ordinal==46 || next_kernel_ordinal==65)?6:next_kernel_ordinal==109?7:
                       next_kernel_ordinal==175?3:(next_kernel_ordinal==44 || next_kernel_ordinal==47)?2:1;
        for(unsigned i=0;i<count;++i)args[i]=MEM32(g_esp+4+i*4);
        int page_service=next_kernel_ordinal==173 || next_kernel_ordinal==175;
        if(page_service) {
            AcquireSRWLockShared(&vm_lock);
            if(args[0]>=0x80000000u && args[0]<0x90000000u &&
               !physical_find(args[0],next_kernel_ordinal==175?args[1]:1))
                stop("page service requires owned contiguous storage",args[0]);
        }
        const char *error=pilot_io_device_lle(next_kernel_ordinal,memory,args,&result);
        if(page_service)ReleaseSRWLockShared(&vm_lock);
        if(error)stop(error,next_kernel_ordinal);
        if(next_kernel_ordinal==44 || next_kernel_ordinal==173)g_eax=result;
        if(page_service && pilot_verbose)
            fprintf(stderr,"[IO PAGE LLE] ordinal=%u address=%08x size=%u result=%08x\n",
                    next_kernel_ordinal,args[0],next_kernel_ordinal==175?args[1]:1,result);
        if(next_kernel_ordinal==65) {
            g_eax=result;
            fprintf(stderr,"[IO DEVICE LLE CREATE] status=%08x object=%08x extension=%u type=%u\n",
                    result,MEM32(args[5]),args[1],args[3]);
        }
        g_esp+=4+count*4;return;
    }
#endif
    case 304:case 305:system_time_fields(next_kernel_ordinal);return;
    case 124: case 143: case 144: case 224: case 231: case 246: case 250:
        thread_object_kernel(next_kernel_ordinal);return;
    case 14:case 15:case 17:case 23:pool_kernel(next_kernel_ordinal);return;
    case 181:g_eax=memory_statistics(MEM32(g_esp+4));g_esp+=8;return;
    case 1: case 4: case 165: case 166: case 171: case 178: case 179: case 180: case 182: physical_kernel(next_kernel_ordinal); return;
    case 35: case 36: case 37: fsc_kernel(next_kernel_ordinal); return;
    case 327: case 328:
        g_eax=section_change(MEM32(g_esp+4),next_kernel_ordinal==327); g_esp+=8; return;
    case 279: {
        g_eax=counted_string_equal(MEM32(g_esp+4),MEM32(g_esp+8),MEM8(g_esp+12));
        g_esp+=16; return;
    }
    case 316:g_eax=counted_string_upper(MEM8(g_esp+4));g_esp+=8;return;
    case 289:case 290: {
        counted_string_init(next_kernel_ordinal==289?1:2,MEM32(g_esp+4),MEM32(g_esp+8));
        g_esp+=12; return;
    }
    case 67: case 69: case 203: case 215: namespace_kernel(next_kernel_ordinal); return;
    case 184: case 199: case 204: case 217: vm_kernel(next_kernel_ordinal); return;
    case 190: case 198: case 202: case 207: case 211: case 218: case 219: case 226: case 236: media_kernel(next_kernel_ordinal); return;
    case 186: case 189: case 209: case 225: case 233: case 234: event_kernel(next_kernel_ordinal);return;
    case 97: g_eax=dispatch_cancel_timer(MEM32(g_esp+4)); g_esp+=8; return;
    case 108: {
        uint32_t va=MEM32(g_esp+4),type=MEM32(g_esp+8),state=MEM32(g_esp+12);
        if (type>1) stop("invalid event type",type);
        dispatch_initialize(va,type); dispatch_event(va,state!=0); g_esp+=16; return;
    }
    case 119: g_eax=dispatch_queue(MEM32(g_esp+4),MEM32(g_esp+8),MEM32(g_esp+12),0); g_esp+=16; return;
    case 121: g_eax=dispatch_in_dpc; g_esp+=4; return;
    case 128: {
        uint32_t out=MEM32(g_esp+4);
        if(!out || out>UINT32_MAX-7)stop("invalid system-time output",out);
        uint64_t now=dispatch_wall_time();memcpy(memory+out,&now,8);
        g_esp+=8;return; /* VOID, one callee-cleaned pointer argument. */
    }
    case 137: g_eax=dispatch_queue(MEM32(g_esp+4),0,0,1); g_esp+=8; return;
    case 138: g_eax=dispatch_event(MEM32(g_esp+4),0); g_esp+=8; return;
    case 145:
        if (MEM32(g_esp+12)) stop("KeSetEvent Wait=TRUE requires scheduler handoff",145);
        g_eax=dispatch_event(MEM32(g_esp+4),1); g_esp+=16; return;
    case 149: case 150: {
        unsigned ex=next_kernel_ordinal==150;
        g_eax=dispatch_set_timer(MEM32(g_esp+4),(int64_t)MEM64(g_esp+8),ex ? MEM32(g_esp+16) : 0,MEM32(g_esp+(ex ? 20 : 16)));
        g_esp+=ex ? 24 : 20; return;
    }
    case 158: {
        uint32_t count=MEM32(g_esp+4),list=MEM32(g_esp+8),kind=MEM32(g_esp+12),objects[64];
        if (!count || count>64 || kind>1) { g_eax=0xc000000du; g_esp+=36; return; }
        for (unsigned i=0;i<count;++i) objects[i]=MEM32(list+i*4);
        g_eax=dispatch_wait(objects,count,kind==0,MEM32(g_esp+24)!=0,MEM32(g_esp+28)); g_esp+=36; return;
    }
    case 159: {
        uint32_t va=MEM32(g_esp+4);
        g_eax=dispatch_wait(&va,1,0,MEM32(g_esp+16)!=0,MEM32(g_esp+20)); g_esp+=24; return;
    }
    case 301: {
        g_eax=pilot_nt_status_to_dos_error(MEM32(g_esp+4));g_esp+=8;return;
    }
    case 24: query_setting(); g_esp+=24; return;
    case 107: {
        uint32_t va=MEM32(g_esp+4);
        memset(memory+va,0,28);
        MEM16(va)=0x13; MEM8(va+2)=0;
        MEM32(va+12)=MEM32(g_esp+8); MEM32(va+16)=MEM32(g_esp+12);
        g_esp+=16; return;
    }
    case 113: {
        uint32_t va=MEM32(g_esp+4),type=MEM32(g_esp+8);
        if (type>1) stop("invalid timer type",type);
        dispatch_initialize(va,8+type);
        g_esp+=12; return;
    }
    case 277: case 291: case 294:
        critical_section(next_kernel_ordinal,MEM32(g_esp+4));
        g_eax=0; g_esp+=8; return;
    case 255: create_thread(); g_esp+=44; return;
    case 258:
        if (!current_thread_slot) stop("bootstrap thread termination",next_kernel_ordinal);
        thread_finished();
        ExitThread(MEM32(g_esp+4));
    case 187: {
        uint32_t handle=MEM32(g_esp+4),slot=handle&63u;
        if((handle&0xf0000000u)==0x30000000u){g_eax=event_close(handle);g_esp+=8;return;}
        if ((handle&0xf0000000u)==0x60000000u) {
            g_eax=namespace_close(handle); g_esp+=8; return;
        }
        if ((handle&0xf0000000u)==0x50000000u) {
            g_eax=media_close(handle); g_esp+=8; return;
        }
        AcquireSRWLockExclusive(&thread_lock);
        g_eax=0xc0000008u;
        if ((handle&0xf0000000u)==0x40000000u && slot>0 && slot<=thread_count &&
            threads[slot].identity==handle && threads[slot].handle && !threads[slot].retiring) {
            /* The execution owner survives close-before-exit. No host resource
             * is dropped until thread_reap observes its actual termination. */
            threads[slot].handle=NULL;g_eax=0;
        }
        ReleaseSRWLockExclusive(&thread_lock);
        if(!g_eax)thread_reap_slot(slot);
        g_esp+=8;
        return;
    }
    }
    stop(name, next_kernel_ordinal);
}
#ifdef PILOT_INPUT_QUALIFICATION
#include "input_qualification.inc"
#endif
static void kernel_dispatch(void)
{
#ifdef PILOT_USB_LLE
#ifdef PILOT_SCHEDULING_LLE
    if(usb_device_callback_depth) {
        const char *error=original_callback_kernel_call(next_kernel_ordinal);
        if(error)stop(error,next_kernel_ordinal);
        return;
    }
#endif
    if(original_thread_exit.active){original_thread_kernel_dispatch();return;}
#endif
    int blocked=scheduler_kernel_blocks(next_kernel_ordinal)?scheduler_block_begin():0;
    kernel_dispatch_impl();
    scheduler_block_end(blocked);
#ifdef PILOT_USB_LLE
    usb_poll();
#endif
    PILOT_THREAD_CHECKPOINT();
}
recomp_func_t recomp_lookup_kernel(uint32_t va)
{
    if (va >= 0xfe000000u && va < 0xfe001000u && !(va & 3)) {
        next_kernel_ordinal = (va - 0xfe000000u) / 4;
        return kernel_dispatch;
    }
    return NULL;
}

static LONG CALLBACK fault_handler(EXCEPTION_POINTERS *exception)
{
    DWORD code = exception->ExceptionRecord->ExceptionCode;
    /* Windows libraries use this diagnostic exception to name their host
     * threads, inside their own SEH handler. Let that handler receive it;
     * this does not resume or hide an unhandled guest/native fault. */
    if(code==0x406d1388u)return EXCEPTION_CONTINUE_SEARCH;
    if (code == EXCEPTION_ACCESS_VIOLATION) {
#ifdef PILOT_AUDIO_DEVICE_LLE
        const char *audio_error=NULL;
        int audio=pilot_mmio_fault_range(exception,memory,0xfe800000u,0xfe87ffffu,original_audio_compiled_guard,&audio_error);
        if(!audio)audio=pilot_mmio_fault_range(exception,memory,0xfec00000u,0xfec00fffu,original_audio_compiled_guard,&audio_error);
        if(audio<0)stop(audio_error,0xfe800000u);
#endif
#ifdef PILOT_GPU_LLE
        const char *gpu_error=NULL;
        int gpu=pilot_mmio_fault_range(exception,memory,0xfd000000u,0xfdffffffu,gpu_lle_access,&gpu_error);
        if(gpu>0)return EXCEPTION_CONTINUE_EXECUTION;
        if(gpu<0)stop(gpu_error,0xfd000000u);
#endif
#ifdef PILOT_DISPLAY_LLE
        const char *display_error=NULL;
        int display=pilot_mmio_fault_range(exception,memory,0xfd000000u,0xfdffffffu,display_lle_access,&display_error);
        if(display>0)return EXCEPTION_CONTINUE_EXECUTION;
        if(display<0)stop(display_error,0xfd000000u);
#endif
#ifdef PILOT_USB_LLE
        const char *mmio_error=NULL;
        int mmio=pilot_mmio_fault(exception,memory,usb_access,&mmio_error);
        if(mmio>0)return EXCEPTION_CONTINUE_EXECUTION;
        if(mmio<0)stop(mmio_error,0xfed00000u);
#endif
        uintptr_t at = exception->ExceptionRecord->ExceptionInformation[1];
        HMODULE fault_module=NULL;char fault_name[MAX_PATH]={0};
        if(GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            (LPCSTR)exception->ExceptionRecord->ExceptionAddress,&fault_module))
            GetModuleFileNameA(fault_module,fault_name,MAX_PATH);
        fprintf(stderr,"[FAULT SITE] pc=%p module=%s base=%p rva=%llx\n",
                exception->ExceptionRecord->ExceptionAddress,fault_name,(void*)fault_module,
                (unsigned long long)((uintptr_t)exception->ExceptionRecord->ExceptionAddress-(uintptr_t)fault_module));
        fprintf(stderr,"[FAULT GUEST] eax=%08x ebx=%08x ecx=%08x edx=%08x esi=%08x edi=%08x ebp=%08x esp=%08x\n",
                g_eax,g_ebx,g_ecx,g_edx,g_esi,g_edi,g_ebp,g_esp);
        fprintf(stderr, "[FAULT] host=%p guest=%08llx access=%llu\n", (void*)at,
                (unsigned long long)(at - (uintptr_t)memory),
                (unsigned long long)exception->ExceptionRecord->ExceptionInformation[0]);
        stop("unmapped guest access or native lowering fault", (uint32_t)(at-(uintptr_t)memory));
    }
    stop("native exception", code);
}

static void install_fault_handler(void)
{
#if defined(PILOT_CALENDAR_LLE) || defined(PILOT_IO_DEVICE_LLE)
    /* Unicorn commits its reserved translation buffer through its own VEH.
     * Let that handler run before treating an exception as an unhandled
     * failure. Genuine faults still reach our terminating diagnostic. */
    SetUnhandledExceptionFilter(fault_handler);
#else
    AddVectoredExceptionHandler(1,fault_handler);
#endif
}

static void load_image(const char *path)
{
    FILE *file = fopen(path, "rb");
    if (!file) stop("cannot open XBE", 0);
    if (fseek(file, 0, SEEK_END) || (long)(image_size=ftell(file)) < 0 ||
        image_size < 0x178 || image_size > 256*1024*1024) stop("invalid XBE file size", 0);
    rewind(file);
    image_bytes = malloc(image_size);
    if (!image_bytes || fread(image_bytes, 1, image_size, file) != image_size) stop("cannot read XBE", 0);
    fclose(file);
    unsigned char hash[32]; char hex[65];
    if (BCryptHash(BCRYPT_SHA256_ALG_HANDLE, NULL, 0, image_bytes, (ULONG)image_size, hash, 32) < 0)
        stop("SHA256 failed", 0);
    for (unsigned i=0;i<32;++i) sprintf(hex+i*2,"%02x",hash[i]);
    if (strcmp(hex, PILOT_XBE_SHA256)) stop("XBE differs from generated artifact", 0);
    fprintf(stderr, "[IMAGE] sha256=%s entry=%08x\n", hex, PILOT_ENTRY);
    memory = VirtualAlloc(NULL, (SIZE_T)0x100000000ull, MEM_RESERVE, PAGE_NOACCESS);
    if (!memory) stop("cannot reserve guest address space", 0);
    g_xbox_mem_offset = (ptrdiff_t)memory;
    image_base=file32(0x104); header_bytes=file32(0x108);
    section_count=file32(0x11c); section_table=file32(0x120);
    if (header_bytes > image_size || section_table < image_base ||
        (uint64_t)section_table-image_base+(uint64_t)section_count*56>header_bytes)
        stop("invalid XBE section table", 0);
    commit(image_base,header_bytes);
    memcpy(memory+image_base,image_bytes,header_bytes);
    g_xbox_code_lo=0xffffffffu;
    unsigned preloaded=0;
    for (unsigned i=0;i<section_count;++i) {
        size_t off=section_table-image_base+i*56;
        uint32_t flags=file32(off), address=file32(off+4), size=file32(off+8);
        uint32_t raw=file32(off+12), bytes=file32(off+16);
        if ((uint64_t)raw+bytes>image_size || bytes>size) stop("invalid section payload", address);
        if (flags&4) {
            if (address<g_xbox_code_lo) g_xbox_code_lo=address;
            if (address+size>g_xbox_code_hi) g_xbox_code_hi=address+size;
        }
        if (!(flags&2)) continue;
        commit(address,size);
        memcpy(memory+address,image_bytes+raw,bytes);
        /* Explicit zero-fill is needed on pages shared by adjacent sections. */
        memset(memory+address+bytes,0,size-bytes);
        MEM32(section_table+i*56+24)=1;
        ++preloaded;
    }
    for (size_t i=0;i<sizeof(pilot_imports)/sizeof(pilot_imports[0]);++i) {
        MEM32(pilot_imports[i].address)=0xfe000000u+4*pilot_imports[i].ordinal;
        if (pilot_imports[i].ordinal==259) {
            /* Native object-type identity, used by ObReferenceObjectByHandle.
             * Guest inspection of unqualified OBJECT_TYPE internals faults. */
            MEM32(pilot_imports[i].address)=PILOT_THREAD_OBJECT_TYPE;
        }
        if (pilot_imports[i].ordinal==164) {
            /* LaunchDataPage is an exported pointer, not a function. This
             * process is a cold launch with no previous title's sticky page.
             * nxdk declares PLAUNCH_DATA_PAGE; Cxbx EmuKrnlMm starts it null.
             * Preserve a writable guest pointer cell; warm launch/persistence
             * is not advertised and HalReturnToFirmware still stops. */
            const uint32_t data=PILOT_KERNEL_DATA_BASE+0x3000u;
            commit(data,4096); MEM32(data)=0;
            MEM32(pilot_imports[i].address)=data;
        }
        if (pilot_imports[i].ordinal==356) {
            /* HalBootSMCVideoMode is a ULONG data export, not a callable thunk.
             * Same composite AV pack (SMC register 04 = 06) as machine/smbus.cpp.
             * Original XGetAudioFlags uses this to mask unsupported digital audio. */
            const uint32_t data=PILOT_KERNEL_DATA_BASE+0x4000u;
            commit(data,4096); MEM32(data)=6;
            MEM32(pilot_imports[i].address)=data;
            fprintf(stderr,"[CONFIG] HalBootSMCVideoMode=6 (composite AV pack)\n");
        }
        if (pilot_imports[i].ordinal==322 || pilot_imports[i].ordinal==324) {
            const uint32_t data=PILOT_KERNEL_DATA_BASE+0x1000u*(pilot_imports[i].ordinal-322);
            commit(data,4096);
            if (pilot_imports[i].ordinal==322) {
                MEM32(data)=0; /* XboxHardwareInfo: retail hardware flags */
                MEM8(data+4)=0xa1; MEM8(data+5)=0xb1;
            } else {
                MEM16(data)=1; MEM16(data+2)=0;
                MEM16(data+4)=5838; MEM16(data+6)=1;
            }
            MEM32(pilot_imports[i].address)=data;
        }
    }
    fprintf(stderr,"[LOAD] %u preload sections; %u deferred sections\n",preloaded,section_count-preloaded);
    sections_initialize();
#ifdef PILOT_USB_LLE
    const char *image_error=pilot_io_device_image(memory,image_base,(file32(0x10c)+4095)&~4095u);
    if(image_error)stop(image_error,image_base);
#ifdef PILOT_MEMORY_LLE
    image_error=pilot_io_device_resident(image_base,(file32(0x10c)+4095)&~4095u);
    if(image_error)stop(image_error,image_base);
#endif
#endif

#ifndef PILOT_SCHEDULING_LLE
    /* Post-loader bootstrap thread. Xbox TLS is below NtTib.StackBase;
     * the original XAPI entry computes and writes its negative TLS index. */
    const uint32_t stack_top=0x70010000u, stack_bottom=0x70000000u;
    const uint32_t thread=0x71001000u, tls=0x71002000u;
    commit(stack_bottom,stack_top-stack_bottom);
    commit(0x71000000u,0x10000u);
#ifdef PILOT_MEMORY_LLE
    const char *bootstrap_error=pilot_io_device_stack(memory,stack_bottom,stack_top-stack_bottom);
    if(!bootstrap_error)bootstrap_error=pilot_io_device_resident(stack_bottom,stack_top-stack_bottom);
    if(bootstrap_error)stop(bootstrap_error,stack_bottom);
#endif
    g_fs_base=0x71000000u;
    MEM32(g_fs_base)=0xffffffffu;
    MEM32(g_fs_base+4)=stack_top;
    MEM32(g_fs_base+8)=stack_bottom;
    MEM32(g_fs_base+0x18)=g_fs_base;
    MEM32(g_fs_base+0x1c)=g_fs_base;
    MEM32(g_fs_base+0x20)=g_fs_base+0x28;
    MEM32(g_fs_base+0x28)=thread;
    MEM32(thread+0x12c)=1;
    uint32_t directory=file32(0x12c);
    if (directory) {
        uint32_t start=MEM32(directory), end=MEM32(directory+4), zero=MEM32(directory+16);
        if (end<start || (uint64_t)end-start+zero>0xd000) stop("unsupported TLS size",directory);
        uint32_t total=(((end-start)+zero+15)&~15u)+4;
        memcpy(memory+tls,memory+start,end-start);
        MEM32(g_fs_base+4)=tls+total;
        MEM32(thread+0x28)=tls;
        fprintf(stderr,"[TLS] initialized=%u zero=%u allocation=%u\n",end-start,zero,total);
    }
    g_esp=stack_top-4;
    MEM32(g_esp)=0;
#endif
}

int main(int argc,char **argv)
{
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    thread_guest_cpu_configure();
    scheduler_configure();
#ifdef PILOT_SCHEDULING_LLE
    scheduler_enabled=0; /* Original kernel owns readiness and preemption. */
#endif
#ifdef PILOT_CALENDAR_LLE
    fprintf(stderr,"[CALENDAR] LLE original-kernel execution; ordinals=304,305\n");
#else
    fprintf(stderr,"[CALENDAR] HLE native NT conversion; ordinals=304,305\n");
#endif
    const char *verbose=getenv("XLLE_PILOT_TRACE");
    if(verbose && !strcmp(verbose,"0"))pilot_verbose=0;
#ifdef PILOT_USB_LLE
    const char *usb_progress=getenv("XLLE_PILOT_ORIGINAL_PROGRESS");
    trace_original_usb=usb_progress && !strcmp(usb_progress,"1");
#endif
    const char *trace=getenv("XLLE_PILOT_TRACE_FUNCTION");
    if(trace && *trace) {char *end;trace_function=strtoul(trace,&end,0);if(*end || !trace_function)stop("invalid trace function address",0);}
    install_fault_handler();
    if (argc<2 || argc>5) { fprintf(stderr,"usage: xlle_native_title <matching.xbe> [eeprom] [installed-content-directory] [local-storage-directory]\n"); return 1; }
    if (argc>=3) {
        FILE *input=fopen(argv[2],"rb");
        if (!input || fread(eeprom,1,256,input)!=256 || fgetc(input)!=EOF) stop("invalid EEPROM input",0);
        fclose(input); have_eeprom=1;
#ifdef PILOT_USB_LLE
        const char *eeprom_error=pilot_usb_board_bind_eeprom(shared_eeprom_access);
        if(eeprom_error)stop(eeprom_error,0);
#endif
    }
    load_image(argv[1]);
#ifdef PILOT_FILES_LLE
    files_lle_start();
#else
    if (argc>=4) media_content_path=argv[3];
    if (argc==5) storage_mount(argv[4]);
    fsc_ensure();
#endif
#ifdef PILOT_SCHEDULING_LLE
    scheduling_lle_finalize_imports();
#endif
    /* Selected data imports are finalized after the original mount setup.
     * Protect preloads only after every loader-owned import write is complete. */
    sections_finalize_preloads();
#ifdef PILOT_MEDIA_INSTALLED
    fprintf(stderr,"[MEDIA POLICY] installed-title HLE; optical authentication is not asserted\n");
#else
    fprintf(stderr,"[MEDIA POLICY] original optical verification routine\n");
#endif
#ifdef PILOT_IO_DEVICE_LLE
    if(trace_function) {
        const char *trace_error=pilot_io_device_thread_trace(trace_function);
        if(trace_error)stop(trace_error,trace_function);
    }
#endif
#if defined(PILOT_MEDIA_INSTALLED) && defined(PILOT_SCHEDULING_LLE)
    native_component_gate_bind();
    const char *media_error=pilot_io_device_thread_component_bind(PILOT_MEDIA_CPU_ENTRY,
        PILOT_MEDIA_CPU_BYTES,PILOT_MEDIA_CPU_SHA256,installed_media_component);
    if(media_error)stop(media_error,PILOT_MEDIA_CPU_ENTRY);
#endif
#if defined(PILOT_SCHEDULING_LLE) && defined(PILOT_D3D_NATIVE)
    native_graphics_components_bind();
#endif
#if defined(PILOT_SCHEDULING_LLE) && defined(PILOT_AUDIO_NATIVE)
    native_audio_components_bind();
#endif
    recomp_func_t entry=recomp_lookup(PILOT_ENTRY);
    if (!entry) stop("entry missing from generated dispatch", PILOT_ENTRY);
    fprintf(stderr,"[RUN] calling actual XBE entry\n");
#ifdef PILOT_GPU_LLE
    gpu_lle_open();
#endif
#ifdef PILOT_USB_LLE
#ifndef PILOT_SCHEDULING_LLE
    usb_clock_start();
#else
    usb_host_start();
    uint32_t revision=0;const char *usb_error=usb_access(0xfed00000u,0,&revision);
    if(usb_error)stop(usb_error,0xfed00000u);
#endif
#endif
#ifdef PILOT_SCHEDULING_LLE
#ifdef PILOT_AUDIO_DEVICE_LLE
    /* Diagnostic destination selects a real bounded AC97 memory consumer;
     * absent selection retains the existing before-DMA RUN guard. */
    const char *capture_destination=getenv("XLLE_PILOT_ORIGINAL_PCM_CAPTURE");
    const char *audio_error=NULL;
    if(capture_destination && *capture_destination) {
        audio_error=pilot_pcm_capture_create(&original_pcm_capture,capture_destination,0,0);
        if(audio_error)stop(audio_error,0xfe800000u);
    }
    audio_error=pilot_audio_device_lle_open(original_pcm_capture?pilot_pcm_capture_consume:NULL,original_pcm_capture);
    if(audio_error)stop(audio_error,0xfe800000u);
    if(original_pcm_capture) {
        audio_error=pilot_pcm_capture_bind(original_pcm_capture);
        if(audio_error)stop(audio_error,0xfe800000u);
    }
#endif
    scheduling_lle_run();
#ifdef PILOT_AUDIO_DEVICE_LLE
    audio_error=pilot_audio_device_lle_close();if(audio_error)stop(audio_error,0xfe800000u);
    if(original_pcm_capture) {
        audio_error=pilot_pcm_capture_finish(original_pcm_capture,1);if(audio_error)stop(audio_error,0xfe800000u);
        audio_error=pilot_pcm_capture_destroy(original_pcm_capture);if(audio_error)stop(audio_error,0xfe800000u);
        original_pcm_capture=NULL;
    }
#endif
    return 0;
#endif
    thread_guest_cpu_enter();
    scheduler_publish(0,0,0);scheduler_enter(0);
    entry();
    scheduler_retire();
    AcquireSRWLockShared(&thread_lock);
    unsigned launched=thread_count;
    ReleaseSRWLockShared(&thread_lock);
    if (!launched) stop("bootstrap returned without a running title thread",PILOT_ENTRY);
    fprintf(stderr,"[BOOTSTRAP] returned; native title threads own execution\n");
    ExitThread(0);
}
