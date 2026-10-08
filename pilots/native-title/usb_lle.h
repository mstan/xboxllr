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
const char *pilot_usb_lle_bind(PilotUsbDmaRead, PilotUsbDmaWrite, PilotUsbDmaRelease);
/* One real dword transaction through the shared OHCI model. */
const char *pilot_usb_lle_access(uint32_t address,int write,uint32_t *value);
#ifdef __cplusplus
}
#endif
#endif
