#ifndef PILOT_IO_DEVICE_LLE_H
#define PILOT_IO_DEVICE_LLE_H
#include <stdint.h>
/* Interpreter/native ABI transport only, outside all guest allocator ranges.
 * These host backing pages do not consume the modeled Xbox's 64MiB RAM. */
#define PILOT_IO_BRIDGE_BASE 0xe5000000u
#define PILOT_IO_BRIDGE_BYTES 0x10000u
#define PILOT_IO_CLOCK_BASE 0xe6000000u
#define PILOT_IO_CLOCK_BYTES 0x20000u
/* Private loader-to-SystemRoutine transport, outside title address ranges. */
#define PILOT_IO_BOOTSTRAP_ENTRY (PILOT_IO_BRIDGE_BASE+0x9800u)
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
const char *pilot_io_device_clock_bind(PilotIoClock provider,void *context);
/* Runtime original scheduling: execute the reviewed retired INIT PIT routine
 * on the same engine and bind monotonic input time. Requires initialized idle
 * transport, an unprogrammed PIT and no pending device interrupt. */
const char *pilot_io_device_clock_start(void);
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
/* Device-owned MMIO, shared with compiled guest accesses. Bind after the
 * kernel transport is initialized. Callback runs with its lock held and must
 * not reenter this transport. Passing NULL removes the owned mapping. */
typedef const char *(*PilotIoMmio)(uint32_t address,unsigned size,int write,uint32_t *value);
const char *pilot_io_device_mmio_bind(uint32_t first,uint32_t bytes,PilotIoMmio callback);
unsigned pilot_io_device_irql(void);
void pilot_io_device_set_if(int enabled);
const char *pilot_io_device_irq(unsigned char *guest,int *delivered);
/* Loader-owned image VA: real kernel reservation, on-demand original physical
 * backing for resident pages, and DMA permitted only while originally pinned. */
const char *pilot_io_device_image(unsigned char *guest,uint32_t base,uint32_t bytes);
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
