#ifndef PILOT_IO_DEVICE_LLE_H
#define PILOT_IO_DEVICE_LLE_H
#include <stdint.h>
/* Interpreter/native ABI transport only, outside all guest allocator ranges.
 * These host backing pages do not consume the modeled Xbox's 64MiB RAM. */
#define PILOT_IO_BRIDGE_BASE 0xe5000000u
#define PILOT_IO_BRIDGE_BYTES 0x10000u
#define PILOT_IO_CLOCK_BASE 0xe6000000u
#define PILOT_IO_CLOCK_BYTES 0x20000u
/* Private original-kernel device and owned-page implementation. Returns an explicit diagnostic on
 * unsupported execution; it never substitutes successful completion. */
const char *pilot_io_device_lle(unsigned ordinal, unsigned char *guest,
                               const uint32_t *args, uint32_t *result);
/* Attach the explicitly selected private writable HDD. Original file/cache
 * work runs on the captured worker, raised through KeSetPriorityThread so
 * unrelated captured boot workers cannot preempt this bounded reference. */
const char *pilot_io_device_files(unsigned char *guest);
/* Immutable boot-derived ULONG export, presently HalDiskCachePartitionCount. */
const char *pilot_io_device_file_data(unsigned ordinal,uint32_t *value);
/* Execute a registered original ISR body through its existing AOT binding.
 * Registers are x86 EAX,ECX,EDX,EBX,ESP,EBP,ESI,EDI. The kernel owns the
 * interrupt frame, priority transitions and EOI; callback owns its real body. */
typedef const char *(*PilotIoIsr)(uint32_t entry,uint32_t regs[8]);
void pilot_io_device_isr_bind(PilotIoIsr callback);
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
 * Stack allocations currently last for the title process lifetime. */
const char *pilot_io_device_stack(unsigned char *guest,uint32_t base,uint32_t bytes);
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
