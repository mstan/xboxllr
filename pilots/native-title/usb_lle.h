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
const char *pilot_usb_lle_host_bind(PilotUsbHostRead,PilotUsbHostRumble,PilotUsbHostCaps);
const char *pilot_usb_lle_bind(PilotUsbDmaRead, PilotUsbDmaWrite, PilotUsbDmaRelease);
/* One real dword transaction through the shared OHCI model. */
const char *pilot_usb_lle_access(uint32_t address,int write,uint32_t *value);
/* PIC profile restored alongside the qualified kernel bootstrap. */
const char *pilot_usb_pic_bootstrap(uint16_t mask,uint16_t level);
const char *pilot_usb_pic_port(uint32_t port,int size,int write,uint32_t *value);
int pilot_usb_pic_vector(int acknowledge);
void pilot_usb_pic_raise(int line,int level);
const char *pilot_usb_lle_tick(void);
#ifdef __cplusplus
}
#endif
#endif
