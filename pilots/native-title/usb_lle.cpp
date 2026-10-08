/* Native-title adapter for our maintained OHCI/XID floor. The device owns
 * register, transfer, completion and IRQ state; no successful transfer is
 * synthesized by this adapter. Interrupt CPU delivery is a separate gate. */
#include <windows.h>
#include <memory>
#include <string>
#include "machine/ohci.hpp"
#include "usb_lle.h"

namespace {
SRWLOCK usb_lock=SRWLOCK_INIT;
std::unique_ptr<xlle::Ohci> controller;
std::string failure;
PilotUsbDmaRelease release_dma;
uint64_t start_ms;
bool irq_level;
}
extern "C" const char *pilot_usb_lle_bind(PilotUsbDmaRead read,PilotUsbDmaWrite write,
                                          PilotUsbDmaRelease release)
{
    AcquireSRWLockExclusive(&usb_lock);
    if(controller || !read || !write || !release)failure="USB LLE invalid/repeated binding";
    else {
        controller=std::make_unique<xlle::Ohci>(0xfed00000u,
            [](const std::string& why){if(failure.empty())failure=why;});
        controller->set_mem_access(read,write);
        controller->set_irq([](int,bool level){irq_level=level;},1);
        release_dma=release;start_ms=GetTickCount64();
    }
    const char *result=failure.empty()?nullptr:failure.c_str();
    ReleaseSRWLockExclusive(&usb_lock);return result;
}
extern "C" const char *pilot_usb_lle_access(uint32_t address,int write,uint32_t *value)
{
    AcquireSRWLockExclusive(&usb_lock);
    if(!controller)failure="USB LLE access before binding";
    if(address<0xfed00000u || address>=0xfed01000u || (address&3))
        failure="USB LLE unsupported register transaction";
    if(failure.empty()) {
        controller->set_frame(GetTickCount64()-start_ms);
        if(failure.empty()) {
            if(write) {
                uint32_t old_control=controller->control();
                controller->mmio_write(address,4,*value);
                if(failure.empty() && ((address==0xfed00008u && (*value&1)) ||
                   (address==0xfed00004u && (old_control&0xc0) && !(*value&0xc0))))
                    release_dma(); // The real controller reset discards its schedules.
            } else *value=controller->mmio_read(address,4);
        }
    }
    const char *result=failure.empty()?nullptr:failure.c_str();
    ReleaseSRWLockExclusive(&usb_lock);return result;
}
