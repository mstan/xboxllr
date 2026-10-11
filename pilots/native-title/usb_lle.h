#ifndef PILOT_USB_LLE_H
#define PILOT_USB_LLE_H
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef int (*PilotUsbDmaRead)(uint32_t,void*,size_t);
typedef int (*PilotUsbDmaWrite)(uint32_t,const void*,size_t);
typedef void (*PilotUsbDmaRelease)(void);
typedef uint32_t (*PilotUsbHostRead)(unsigned,uint8_t[18]);
typedef uint32_t (*PilotUsbHostRumble)(unsigned,uint16_t,uint16_t);
typedef uint32_t (*PilotUsbHostCaps)(unsigned,uint8_t[18],uint8_t[4]);
/* Read-only storage-owner telemetry. Must not reenter guest execution. */
typedef int (*PilotUsbTrayState)(uint8_t*);
const char *pilot_usb_lle_host_bind(PilotUsbHostRead,PilotUsbHostRumble,PilotUsbHostCaps);
const char *pilot_usb_lle_bind(PilotUsbDmaRead, PilotUsbDmaWrite, PilotUsbDmaRelease);
/* One real dword transaction through the shared OHCI model. */
const char *pilot_usb_lle_access(uint32_t address,int write,uint32_t *value);
/* PIC profile restored alongside the qualified kernel bootstrap. The shared
 * port bus also owns PIT ports 40..43; PIT stays unprogrammed until selected. */
const char *pilot_usb_pic_bootstrap(uint16_t mask,uint16_t level);
const char *pilot_usb_pic_port(uint32_t port,int size,int write,uint32_t *value);
/* Shared board bus: retail GPIO15, RTC70..73 CMOS RAM, SMBusC000..C008
 * and SMC profile with actual PIC IRQ11 completion/ack. EEPROM/encoder starts
 * require their storage/graphics owners and remain guarded in this adapter.
 * RTC clock/status/IRQ/NMI behavior remains guarded; no fixed-date response
 * qualifies those modes. GPIO readback/other modes/widths remain guarded. */
const char *pilot_usb_board_port(uint32_t port,int size,int write,uint32_t *value);
const char *pilot_usb_board_bind_tray(PilotUsbTrayState);
const char *pilot_usb_board_outputs(uint32_t *known,uint32_t *levels);
int pilot_usb_pic_vector(int acknowledge);
void pilot_usb_pic_raise(int line,int level);
/* Explicit device clock input, in 1,125,000-Hz PIT ticks. Does not dispatch
 * interrupts or alter kernel time. Qualification must service actual IRQ0. */
const char *pilot_usb_pit_time(uint64_t ticks);
/* Read the actual maintained input-clock position and next IRQ0 deadline. */
const char *pilot_usb_pit_clock(uint64_t *ticks,uint64_t *next_irq);
const char *pilot_usb_lle_tick(void);
#ifdef __cplusplus
}
#endif
#endif
