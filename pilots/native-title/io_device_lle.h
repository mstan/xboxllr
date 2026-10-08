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
/* Execute a registered original ISR body through its existing AOT binding.
 * Registers are x86 EAX,ECX,EDX,EBX,ESP,EBP,ESI,EDI. The kernel owns the
 * interrupt frame, priority transitions and EOI; callback owns its real body. */
typedef const char *(*PilotIoIsr)(uint32_t entry,uint32_t regs[8]);
void pilot_io_device_isr_bind(PilotIoIsr callback);
unsigned pilot_io_device_irql(void);
void pilot_io_device_set_if(int enabled);
const char *pilot_io_device_irq(unsigned char *guest,int *delivered);
/* Loader-owned image VA: real kernel reservation, on-demand original physical
 * backing for resident pages, and DMA permitted only while originally pinned. */
const char *pilot_io_device_image(unsigned char *guest,uint32_t base,uint32_t bytes);
const char *pilot_io_device_image_release(uint32_t base,uint32_t bytes);
const char *pilot_io_device_release_check(uint32_t base,uint32_t bytes);
int pilot_io_device_pinned_dma(uint32_t pa,void *buffer,uint32_t bytes,int write);
#endif
