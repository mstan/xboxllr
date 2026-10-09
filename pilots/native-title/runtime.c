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

ptrdiff_t g_xbox_mem_offset;
int g_force_return;
uint32_t g_xbox_code_lo, g_xbox_code_hi;
RECOMP_TLS uint32_t g_eax, g_ecx, g_edx, g_esp, g_ebx, g_esi, g_edi, g_ebp;
RECOMP_TLS uint32_t g_fs_base, g_seh_ebp, g_icall_saved_esp, g_icall_dispatch_form;
RECOMP_TLS double g_fp_stack[8];
RECOMP_TLS int g_fp_top, g_fp_cmp, g_df;
RECOMP_TLS uint16_t g_fp_control_word = 0x037f, g_fp_cc = 0x4000;
RECOMP_TLS RecompXmm g_xmm0, g_xmm1, g_xmm2, g_xmm3, g_xmm4, g_xmm5, g_xmm6, g_xmm7;
RECOMP_TLS RecompMmx g_mm0, g_mm1, g_mm2, g_mm3, g_mm4, g_mm5, g_mm6, g_mm7;
RECOMP_TLS volatile uint32_t g_icall_trace[ICALL_TRACE_SIZE], g_icall_trace_idx;
RECOMP_TLS volatile uint64_t g_icall_count;
static RECOMP_TLS uint32_t recent[32], recent_pos;
static RECOMP_TLS uint64_t entries;
static uint32_t trace_function;
static volatile LONG trace_function_hits;
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
    HANDLE handle,reference_handle;
    uint32_t routine,context1,context2,tls_size,stack_size,extra,tid,object,object_refs;
    int32_t priority;
    volatile uint32_t suspend_count;
    int finished;
};
static struct PilotThread threads[64];
static unsigned thread_count;
static unsigned char eeprom[256];
static int have_eeprom;
/* Phobos kernel_rtl.c's useful boundary: a host recursive lock keyed by guest
 * VA. Guest fields use Cxbx types.h's 28-byte Xbox layout, not Win32's layout. */
static struct { uint32_t va; CRITICAL_SECTION host; } critical_sections[256];
static unsigned critical_count;
static SRWLOCK critical_lock=SRWLOCK_INIT;

__declspec(noreturn) static void stop(const char *reason, uint32_t va)
{
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
#ifdef PILOT_USB_LLE
#include "io_device_lle.h"
#include "usb_lle.h"
static void usb_poll(void);
static RECOMP_TLS unsigned usb_in_interrupt;
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
    if(port==0x80c0 || (port>=0xcf8 && port<=0xcff)) {
        uint32_t value=0;
        const char *error=gpu_lle_port(port,1,0,&value);
        if(error)stop(error,port);
        return (uint8_t)value;
    }
#endif
#ifdef PILOT_USB_LLE
    uint32_t value=0;
    const char *error=pilot_usb_pic_port(port,1,0,&value);
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
    if(port==0x80c0 || (port>=0xcf8 && port<=0xcff)) {
        uint32_t value=byte;
        const char *error=gpu_lle_port(port,1,1,&value);
        if(error)stop(error,port);
        return;
    }
#endif
#ifdef PILOT_USB_LLE
    uint32_t value=byte;
    const char *error=pilot_usb_pic_port(port,1,1,&value);
    if(error)stop(error,port);
#else
    stop("port write requires the machine bus",port);
#endif
}
void pilot_thread_checkpoint(void)
{
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
    LARGE_INTEGER now, frequency;
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&frequency);
    return (uint64_t)(now.QuadPart / frequency.QuadPart) * 733333333ull +
           (uint64_t)(now.QuadPart % frequency.QuadPart) * 733333333ull / frequency.QuadPart;
}
void recomp_cpuid(uint32_t leaf, uint32_t subleaf, uint32_t out[4])
{
    (void)subleaf;
    if (leaf == 0) { out[0]=2; out[1]=0x756e6547; out[2]=0x6c65746e; out[3]=0x49656e69; }
    else if (leaf == 1) { out[0]=0x68a; out[1]=0; out[2]=0; out[3]=0x0383f9ff; }
    else stop("CPUID leaf unsupported", leaf);
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
    MEM32(base+0x1000+0x28)=tls;
    MEM32(base+0x1000+0x12c)=thread->tid;
    uint32_t directory=file32(0x12c);
    if (directory) {
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
    AcquireSRWLockExclusive(&thread_lock);
    if (thread_count==63) { ReleaseSRWLockExclusive(&thread_lock); g_eax=0xc000009au; return; }
    unsigned slot=++thread_count;
    struct PilotThread *thread=&threads[slot];
    thread->routine=args[9]; thread->context1=args[5]; thread->context2=args[6];
    thread->tls_size=args[3]; thread->stack_size=(stack+4095)&~4095u; thread->extra=args[1];
    thread->suspend_count=args[7]!=0;
    /* The object exists while the new host thread is still suspended. Native
     * reference/priority services must not wait for its guest entry to run. */
    uint32_t context_base=0x60000000u+slot*0x100000u;
    commit(context_base,0x2000+thread->extra);thread->object=context_base+0x1000;
    DWORD tid;
    thread->handle=CreateThread(NULL,0,title_thread,(void*)(uintptr_t)slot,CREATE_SUSPENDED,&tid);
    if (!thread->handle) { --thread_count; ReleaseSRWLockExclusive(&thread_lock); g_eax=0xc000009au; return; }
    thread->tid=tid;
    scheduler_publish(slot,thread->suspend_count,thread->priority);
    MEM32(thread->object+0x28)=context_base+0x100000u-thread->tls_size;
    MEM32(thread->object+0x12c)=tid;
    MEM32(args[0])=0x40000000u+slot;
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

static void query_setting(void)
{
    uint32_t index=MEM32(g_esp+4),type=MEM32(g_esp+8),value=MEM32(g_esp+12);
    uint32_t length=MEM32(g_esp+16),result=MEM32(g_esp+20);
    /* XC_* user-settings layout from Cxbx EmuEEPROM.h/types.h. These are
     * actual file-backed settings; encrypted/factory queries remain explicit. */
    static const unsigned char offsets[]={4,8,24,40,12,28,44,48,52,56,60,64,68,72,76,80,84,88,92};
    if (!have_eeprom) stop("console settings require EEPROM input",index);
    if (index>=sizeof(offsets) && index!=0xff) stop("EEPROM index outside pilot scope",index);
    uint32_t size=index==0xff ? 96 : 4;
    if (result) MEM32(result)=size;
    if (length<size) { g_eax=0xc0000023u; return; }
    uint64_t checksum=0;
    for (unsigned i=0x60;i<0xc0;i+=4) {
        uint32_t part; memcpy(&part,eeprom+i,4); checksum+=part;
        checksum=(checksum&0xffffffffu)+(checksum>>32);
    }
    if ((uint32_t)checksum!=0xffffffffu) { g_eax=0xc000009cu; return; }
    MEM32(type)=(index==1 || index==4 || index==0xff) ? 3 : 4;
    memset(memory+value,0,length);
    memcpy(memory+value,eeprom+0x60+(index==0xff ? 0 : offsets[index]),size);
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
#ifdef PILOT_USB_LLE
#include "usb_lle.inc"
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
        typedef BOOLEAN (NTAPI *EqualAnsi)(const ANSI_STRING*,const ANSI_STRING*,BOOLEAN);
        EqualAnsi equal=(EqualAnsi)(void*)GetProcAddress(GetModuleHandleW(L"ntdll.dll"),"RtlEqualString");
        if (!equal) stop("native ANSI comparison unavailable",279);
        uint32_t a=MEM32(g_esp+4),b=MEM32(g_esp+8);
        ANSI_STRING left={MEM16(a),MEM16(a+2),(PCHAR)(memory+MEM32(a+4))};
        ANSI_STRING right={MEM16(b),MEM16(b+2),(PCHAR)(memory+MEM32(b+4))};
        g_eax=equal(&left,&right,(BOOLEAN)MEM8(g_esp+12)); g_esp+=16; return;
    }
    case 289: {
        typedef VOID (NTAPI *InitAnsi)(PANSI_STRING,PCSTR);
        InitAnsi init=(InitAnsi)(void*)GetProcAddress(GetModuleHandleW(L"ntdll.dll"),"RtlInitAnsiString");
        if (!init) stop("native ANSI string initializer unavailable",289);
        uint32_t out=MEM32(g_esp+4),source=MEM32(g_esp+8); ANSI_STRING string={0};
        init(&string,source ? (const char*)(memory+source) : NULL);
        MEM16(out)=string.Length; MEM16(out+2)=string.MaximumLength; MEM32(out+4)=source;
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
        /* Same native NT status table used by Phobos's RTL implementation. */
        typedef ULONG (WINAPI *StatusMap)(LONG);
        StatusMap map=(StatusMap)(void*)GetProcAddress(GetModuleHandleW(L"ntdll.dll"),"RtlNtStatusToDosError");
        if (!map) stop("host NT status table unavailable",301);
        g_eax=map((LONG)MEM32(g_esp+4)); g_esp+=8; return;
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
        uint32_t handle=MEM32(g_esp+4),slot=handle-0x40000000u;
        if((handle&0xf0000000u)==0x30000000u){g_eax=event_close(handle);g_esp+=8;return;}
        if ((handle&0xf0000000u)==0x60000000u) {
            g_eax=namespace_close(handle); g_esp+=8; return;
        }
        if ((handle&0xf0000000u)==0x50000000u) {
            g_eax=media_close(handle); g_esp+=8; return;
        }
        AcquireSRWLockExclusive(&thread_lock);
        g_eax=0xc0000008u;
        if (slot>0 && slot<=thread_count && threads[slot].handle) {
            if (CloseHandle(threads[slot].handle)) { threads[slot].handle=NULL; g_eax=0; }
        }
        ReleaseSRWLockExclusive(&thread_lock);
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
}

int main(int argc,char **argv)
{
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    thread_guest_cpu_configure();
    scheduler_configure();
#ifdef PILOT_CALENDAR_LLE
    fprintf(stderr,"[CALENDAR] LLE original-kernel execution; ordinals=304,305\n");
#else
    fprintf(stderr,"[CALENDAR] HLE native NT conversion; ordinals=304,305\n");
#endif
    const char *verbose=getenv("XLLE_PILOT_TRACE");
    if(verbose && !strcmp(verbose,"0"))pilot_verbose=0;
    const char *trace=getenv("XLLE_PILOT_TRACE_FUNCTION");
    if(trace && *trace) {char *end;trace_function=strtoul(trace,&end,0);if(*end || !trace_function)stop("invalid trace function address",0);}
    install_fault_handler();
    if (argc<2 || argc>5) { fprintf(stderr,"usage: xlle_native_title <matching.xbe> [eeprom] [installed-content-directory] [local-storage-directory]\n"); return 1; }
    if (argc>=3) {
        FILE *input=fopen(argv[2],"rb");
        if (!input || fread(eeprom,1,256,input)!=256 || fgetc(input)!=EOF) stop("invalid EEPROM input",0);
        fclose(input); have_eeprom=1;
    }
    load_image(argv[1]);
#ifdef PILOT_FILES_LLE
    files_lle_start();
#else
    if (argc>=4) media_content_path=argv[3];
    if (argc==5) storage_mount(argv[4]);
    fsc_ensure();
#endif
#ifdef PILOT_MEDIA_INSTALLED
    fprintf(stderr,"[MEDIA POLICY] installed-title HLE; optical authentication is not asserted\n");
#else
    fprintf(stderr,"[MEDIA POLICY] original optical verification routine\n");
#endif
    recomp_func_t entry=recomp_lookup(PILOT_ENTRY);
    if (!entry) stop("entry missing from generated dispatch", PILOT_ENTRY);
    fprintf(stderr,"[RUN] calling actual XBE entry\n");
#ifdef PILOT_GPU_LLE
    gpu_lle_open();
#endif
#ifdef PILOT_USB_LLE
    usb_clock_start();
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
