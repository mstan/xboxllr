#ifndef PILOT_IO_DEVICE_LLE_H
#define PILOT_IO_DEVICE_LLE_H
#include <stdint.h>
/* Interpreter/native ABI transport only, outside all guest allocator ranges.
 * These host backing pages do not consume the modeled Xbox's 64MiB RAM. */
#define PILOT_IO_BRIDGE_BASE 0xe5000000u
#define PILOT_IO_BRIDGE_BYTES 0x10000u
/* Bounded C-service frames have independent guard pages. They must not
 * overlap bridge workspace/GDT or the original thread/DPC stacks. */
#define PILOT_IO_SERVICE_BASE 0xe8000000u
#define PILOT_IO_SERVICE_FRAMES 4u
#define PILOT_IO_SERVICE_STRIDE 0x4000u
#define PILOT_IO_CLOCK_BASE 0xe6000000u
#define PILOT_IO_CLOCK_BYTES 0x20000u
/* PCR of the hash-checked captured kernel, shared by its sole CPU and every
 * generated continuation of that CPU (thread, ISR and DPC). */
#define PILOT_IO_PCR_BASE 0x8003da3cu
/* Private loader-to-SystemRoutine transport, outside title address ranges. */
#define PILOT_IO_BOOTSTRAP_ENTRY (PILOT_IO_BRIDGE_BASE+0x9800u)
/* Authored native-renderer DPC transport. Only an initialized original KDPC
 * dequeued by the original dispatcher may invoke this private entry. */
#define PILOT_IO_GRAPHICS_DPC_ENTRY (PILOT_IO_BRIDGE_BASE+0x9900u)
/* Private original-kernel device and owned-page implementation. Returns an explicit diagnostic on
 * unsupported execution; it never substitutes successful completion. */
const char *pilot_io_device_lle(unsigned ordinal, unsigned char *guest,
                               const uint32_t *args, uint32_t *result);
/* Attach the explicitly selected private writable HDD. Original file/cache
 * work runs on the captured worker, raised through KeSetPriorityThread so
 * unrelated captured boot workers cannot preempt this bounded reference. */
const char *pilot_io_device_files(unsigned char *guest);
/* Original KeQueryInterruptTime returns EDX:EAX over the shared kernel clock.
 * This observation does not advance time or manufacture timer expiration. */
const char *pilot_io_device_interrupt_time(unsigned char *guest,uint64_t *value);
/* Immutable boot-derived ULONG export, presently HalDiskCachePartitionCount. */
const char *pilot_io_device_file_data(unsigned ordinal,uint32_t *value);
/* Execute a registered original ISR body through its existing AOT binding.
 * Registers are x86 EAX,ECX,EDX,EBX,ESP,EBP,ESI,EDI. The kernel owns the
 * interrupt frame, priority transitions and EOI; callback owns its real body. */
typedef const char *(*PilotIoIsr)(uint32_t entry,uint32_t regs[8]);
void pilot_io_device_isr_bind(PilotIoIsr callback);
/* Original DPC dispatch uses four stdcall arguments, not the ISR's two.
 * Admission requires the captured kernel dispatcher, active IRQL2 and the
 * real dequeued DPC object. Its original stack pages share native backing. */
void pilot_io_device_dpc_bind(PilotIoIsr callback);
/* An original created thread invokes its generated body on the original
 * stack/PCR. A SystemRoutine has two arguments and must request termination;
 * a PsCreateSystemThread start body has one argument and may return normally.
 * Termination is completed by the original kernel, never by the host thread.
 * Host stacks only carry native continuations; original dispatch controls
 * which continuation resumes. This does not select a host scheduler. */
typedef const char *(*PilotIoThread)(uint32_t entry,uint32_t regs[8],
    unsigned arguments,int *terminate,uint32_t *exit_status);
void pilot_io_device_thread_bind(PilotIoThread callback);
/* Bounded read-only entry/return observation on the original CPU. Selection
 * is diagnostic only; it does not admit a callback or alter guest state. */
const char *pilot_io_device_thread_trace(uint32_t entry);
/* Elective passive observer, disabled until an idle hash-bound registration.
 * ENTRY and RETURN mean real raw-engine execution, never a generated RET site.
 * A receiver may copy original RAM through diagnostic_read while this same
 * owner is stopped. It may not call services, clocks, device work or reenter.
 * The receiver cannot admit execution or change a retained transport guard. */
enum { PILOT_IO_DIAGNOSTIC_ENTRY=1,PILOT_IO_DIAGNOSTIC_RETURN=2,
       PILOT_IO_DIAGNOSTIC_MMIO=3 };
typedef struct PilotIoDiagnosticEvent {
    uint32_t kind,pc,regs[8],flags,thread,in_kernel;
    uint32_t entry,entry_sp,caller,driver,address,size,value;
} PilotIoDiagnosticEvent;
typedef void (*PilotIoDiagnostic)(const PilotIoDiagnosticEvent *event);
const char *pilot_io_device_diagnostic_bind(uint32_t entry,uint32_t bytes,
    const char *sha256,PilotIoDiagnostic receiver);
/* Borrowed context only inside the actual raw-MMIO callback. The caller
 * chooses its exact pre-write point after ordinary device sampling. */
int pilot_io_device_diagnostic_context(PilotIoDiagnosticEvent *event);
/* Copy-only existing plain engine RAM/native view, or original RAM translation
 * when no alias is cached. MMIO aliases and ROM are rejected. No publication, fault
 * admission, guest service, DMA lease, MMIO or transport-failure mutation.
 * physical!=0 selects actual retail RAM addresses. Valid only in receiver or
 * raw-MMIO callback on the sole engine owner. Validate entire extent first. */
int pilot_io_device_diagnostic_read(uint32_t address,void *out,uint32_t bytes,int physical);
/* Repeated stdcall observations on the actual CPU. Source bytes are verified
 * before execution. Entry/return pairing uses the original thread, caller and
 * exact cleaned stack. Receivers have the same read-only diagnostic borrow;
 * they must not execute services, change time or release simulation owners. */
typedef struct PilotIoDiagnosticCall {
    uint32_t entry,bytes,stack_bytes;
    const char *sha256;
} PilotIoDiagnosticCall;
typedef struct PilotIoCallDiagnosticEvent {
    uint64_t id;
    unsigned index,kind;
    uint32_t thread,regs[8],args[8],caller,entry_sp;
} PilotIoCallDiagnosticEvent;
typedef const char *(*PilotIoCallDiagnostic)(const PilotIoCallDiagnosticEvent *event);
const char *pilot_io_device_diagnostic_calls_bind(const PilotIoDiagnosticCall *calls,
    unsigned count,PilotIoCallDiagnostic receiver);
/* Build-selected complete zero-argument helper: EAX result and RET0 only.
 * Bind only reviewed original bytes by SHA256. The retained native worker
 * runs the selected implementation while the same original CPU is stopped.
 * Internal kernel calls use the executing original thread's real stack and
 * scheduler; the engine retains flags/FP state and sole execution ownership. */
typedef const char *(*PilotIoComponent)(uint32_t entry,uint32_t regs[8]);
const char *pilot_io_device_thread_component_bind(uint32_t entry,uint32_t bytes,
    const char *sha256,PilotIoComponent callback);
/* Whole stdcall/fastcall entries retain register arguments and declare their
 * actual RET cleanup. Only EAX and ESP may change. Entries are immutable,
 * nonoverlapping and sealed before original CPU execution; callbacks cannot
 * bind additional entries or change the engine's flags/FP ownership. */
const char *pilot_io_device_thread_component_bind_abi(uint32_t entry,uint32_t bytes,
    const char *sha256,uint32_t stack_bytes,PilotIoComponent callback);
int pilot_io_device_thread_component_active(void);
const char *pilot_io_device_thread_component_workspace(uint32_t bytes,uint32_t *base);
/* Retire the exact manual lease after all original calls using it return.
 * A VM-owned workspace retains its separate component lifetime. */
const char *pilot_io_device_thread_component_workspace_release(uint32_t base,uint32_t bytes);
const char *pilot_io_device_thread_component_call(unsigned ordinal,
    const uint32_t *args,uint32_t *result);
/* Called from a generated thread body's kernel-dispatch boundary. The real
 * guest call frame is at regs[ESP]; original execution returns updated GPRs.
 * The host call remains suspended if the original kernel switches threads. */
const char *pilot_io_device_thread_call(unsigned ordinal,uint32_t regs[8]);
/* Sole-engine controller requests a certified native integer boundary when
 * the real modeled clock is due. These calls retain the actual C continuation
 * and transfer PC/all eight GPRs/materialized EFLAGS. They do not create a
 * guest CALL frame or complete waits. Caller must account for STI inhibition
 * and qualify its lowering; FP/SIMD compute transport is not admitted here. */
volatile int32_t *pilot_io_device_thread_checkpoint_request(void);
const char *pilot_io_device_thread_initial_flags(uint32_t *flags);
const char *pilot_io_device_thread_checkpoint(uint32_t pc,uint32_t regs[8],uint32_t *flags);
/* Execute original thread bytes on the already owning CPU, starting before
 * any generated guest instruction. The loader adapter may supply the XBE
 * entry frame; an ordinary worker must supply its actual original entry.
 * PC/EFLAGS/x87/MMX/SSE stay in that one engine across IRQs and switches.
 * This is an LLE build route, not a conversion of an executing native body. */
const char *pilot_io_device_thread_execute(uint32_t entry,uint32_t regs[8]);
/* CPU execution may reach an actual nonreturning PsTerminateSystemThread
 * CALL. Report that request to unwind the host loader continuation; original
 * kernel termination/cleanup still executes on its unchanged guest frame. */
const char *pilot_io_device_thread_completion(int *terminated,uint32_t *status);
/* Input clock only, in 1,125,000-Hz PIT ticks. Called solely by the original
 * engine owner between execution slices. The provider must not reenter this
 * transport or complete kernel waits. NULL unbinds; bind only while idle. */
typedef const char *(*PilotIoClock)(void *context,uint64_t *ticks);
/* A controlled external-input driver is separate from the read-only clock.
 * It runs on the sole controller before input sampling, with the CPU stopped.
 * block_entries counts observed engine block entries (including stopped
 * boundaries), not retired instructions or hardware cycles. Native callbacks
 * are not charged as original CPU execution. This is a fixture opportunity,
 * never a source of guest wait results. No CPU/transport reentry is permitted.
 * Bind only while idle; ordinary clock_bind selects no step driver. */
typedef const char *(*PilotIoClockStep)(void *context,uint64_t block_entries);
const char *pilot_io_device_clock_bind_step(PilotIoClock provider,void *context,
    PilotIoClockStep step);
/* Read-only interpreter cache occupancy/capacity; no page or owner mutation. */
const char *pilot_io_device_alias_cache_info(unsigned *count,unsigned *capacity);
const char *pilot_io_device_alias_cache_retired(unsigned *retired);
/* Read-only memo diagnostics, also available after a fatal validation guard. */
const char *pilot_io_device_publication_memo_info(uint64_t *hits,uint64_t *misses);
const char *pilot_io_device_clock_bind(PilotIoClock provider,void *context);
/* Optional pure guest-counter observer. Bind only on an initialized idle CPU
 * with an input clock; clock rebind/unbind clears it. The actual RDTSC remains
 * interpreter-owned, including architectural privilege checks. NULL preserves
 * the engine's ordinary counter behavior. The provider must not reenter us. */
typedef const char *(*PilotIoTsc)(void *context,uint64_t *counter);
const char *pilot_io_device_tsc_bind(PilotIoTsc provider,void *context);
/* Query the actual sole engine's bound clock; a programmed PIT alone does
 * not establish a producer for finite original waits. No guest execution. */
int pilot_io_device_clock_bound(void);
/* Runtime original scheduling: execute the reviewed retired INIT PIT routine
 * on the same engine and bind monotonic input time. Requires initialized idle
 * transport, an unprogrammed PIT and no pending device interrupt. */
const char *pilot_io_device_clock_start(void);
/* Activate the registered installed title after genuine captured-launch
 * suspension and original clock dispatch, before creating its bootstrap. */
const char *pilot_io_device_sections_start(void);
/* Publish the original thread-type data and observe managed live threads.
 * Neither operation changes readiness, priority or completion state. */
const char *pilot_io_device_thread_type(uint32_t *address);
const char *pilot_io_device_next_thread(uint32_t *object);
/* Up to eight disjoint device-owned port ranges. NULL clears all bindings.
 * Bind only while the transport is idle. PIC/storage remain on their shared bus.
 * Callback returns an error on unsupported access. It must not reenter this
 * kernel transport; original port instructions execute with its lock held. */
typedef const char *(*PilotIoPort)(uint16_t port,int size,int write,uint32_t *value);
void pilot_io_device_port_bind(uint16_t first,uint16_t last,PilotIoPort callback);
/* Initialize the immutable original transport without executing a service.
 * Idempotent for the same guest address space; requires idle CPU ownership. */
const char *pilot_io_device_initialize(unsigned char *guest);
/* Up to eight disjoint device-owned MMIO ranges, shared with compiled guest accesses. Bind after the
 * kernel transport is initialized. Callback runs with its lock held and must
 * not execute original CPU services recursively. Synchronous bootstrap,
 * contiguous and pinned RAM DMA may borrow this callback's existing ownership;
 * every existing extent/PTE/protection/pin check still applies. Passing NULL
 * with its exact first/bytes removes only that owner; NULL with zero/zero
 * clears all mappings. Invalid admission leaves existing owners unchanged. */
typedef const char *(*PilotIoMmio)(uint32_t address,unsigned size,int write,uint32_t *value);
const char *pilot_io_device_mmio_bind(uint32_t first,uint32_t bytes,PilotIoMmio callback);
/* Dispatch a sized compiled access to the exact existing registered owner.
 * Only the same-thread stopped original ISR/DPC continuation is admitted;
 * idle callers, raw CPU/clock/device work and nested bus callbacks reject.
 * Uses the sole engine lock and that owner's synchronous DMA capability.
 * A callback failure poisons this transport; failed reads leave value intact.
 * Does not execute/pump CPU, clocks or device work. */
const char *pilot_io_device_compiled_mmio(uint32_t address,unsigned size,int write,uint32_t *value);
/* Admit one explicit RAM lease capability for this device owner. Generic
 * mappings use synchronous DMA; only the shared OHCI owner admits USB leases. */
const char *pilot_io_device_mmio_bind_dma(uint32_t first,uint32_t bytes,PilotIoMmio callback,unsigned dma_lease);
/* Up to eight device owners sample external time between original CPU slices,
 * on the sole engine under each owner's synchronous RAM/DMA capability.
 * No CPU reentry. Remove a particular callback before destroying that device.
 * NULL bind clears all callbacks; exact unbind preserves the other devices. */
typedef const char *(*PilotIoDeviceWork)(void);
const char *pilot_io_device_work_bind(PilotIoDeviceWork callback);
const char *pilot_io_device_work_bind_dma(PilotIoDeviceWork callback,unsigned dma_lease);
const char *pilot_io_device_work_unbind(PilotIoDeviceWork callback);
unsigned pilot_io_device_irql(void);
void pilot_io_device_set_if(int enabled);
const char *pilot_io_device_irq(unsigned char *guest,int *delivered);
/* Loader-owned image VA: real kernel reservation, on-demand original physical
 * backing for resident pages, and DMA permitted only while originally pinned. */
const char *pilot_io_device_image(unsigned char *guest,uint32_t base,uint32_t bytes);
/* Bind the current immutable XBE descriptor table and a retained installed
 * filename before actual original CPU XeLoadSection/XeUnloadSection calls.
 * This supplies launch ownership, never section bytes or completion results. */
const char *pilot_io_device_sections(unsigned char *guest,const char *image_file);
/* Register a resident runtime-owned guest stack. The original VM reserves its
 * address range; only resident owned pages can acquire original PFNs/pins.
 * Release only after native execution and all caller references retire. */
const char *pilot_io_device_stack(unsigned char *guest,uint32_t base,uint32_t bytes);
/* Execute the real original VM release of one exactly registered stack.
 * Live DMA pins leave released=0 without changing storage. Otherwise retire
 * publication and Unicorn aliases before native decommit/address reuse. */
const char *pilot_io_device_stack_release(unsigned char *guest,uint32_t base,uint32_t bytes,int *released);
/* Materialize original physical backing for currently resident pages in an
 * already registered image/stack range. The native loader's actual bytes are
 * retained and become the single backing for CPU, original services and DMA. */
const char *pilot_io_device_resident(uint32_t base,uint32_t bytes);
const char *pilot_io_device_image_release(uint32_t base,uint32_t bytes);
const char *pilot_io_device_release_check(uint32_t base,uint32_t bytes);
int pilot_io_device_pinned_dma(uint32_t pa,void *buffer,uint32_t bytes,int write);
/* Original contiguous allocations are inherently physical owners. GPU bus
 * copies are synchronous; USB retains its lease until controller release.
 * Every transaction validates the full original mapping and host permission. */
enum {PILOT_IO_DMA_SYNC=0,PILOT_IO_DMA_USB=1};
int pilot_io_device_contiguous_dma(uint32_t pa,void *buffer,uint32_t bytes,int write,unsigned lease);
void pilot_io_device_dma_release(unsigned lease);
/* Native resources retain the original contiguous allocation, independently
 * of device DMA leases. Query/reference validate all readable published bytes
 * from address to the actual allocation end; available is that exact extent.
 * Each successful reference has a unique, single-release token. Rejected calls
 * leave outputs and ownership unchanged, including stale tokens after VA reuse.
 * These APIs do not execute guest code or acquire native graphics/VM locks. */
int pilot_io_device_contiguous_resource_query(uint32_t address,uint32_t bytes,uint32_t *available);
int pilot_io_device_contiguous_resource_reference(uint32_t address,uint32_t bytes,
    uint64_t *reference,uint32_t *available);
int pilot_io_device_contiguous_resource_release(uint64_t reference);
/* Publish the original console-owned page-zero DMA bootstrap mapping. This
 * page lasts for the console session, independent of GPU/ordinary allocation
 * lifetimes. Admission verifies current original PTE identity and writability. */
const char *pilot_io_device_gpu_bootstrap(unsigned char *guest);
int pilot_io_device_bootstrap_dma(uint32_t pa,void *buffer,uint32_t bytes,int write);
/* Original Nt VM calls plus publication of the kernel's real allocated pages.
 * inputs: base,size,type,protect,zero_bits. Outputs: base,size,oldProtect for
 * mutations, or seven original MEMORY_BASIC_INFORMATION words for query. */
const char *pilot_io_device_vm(unsigned ordinal,unsigned char *guest,
    const uint32_t inputs[5],uint32_t outputs[7],uint32_t *status);
#endif
