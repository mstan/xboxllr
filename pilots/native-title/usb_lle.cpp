/* Native-title adapter for our maintained OHCI/XID floor. The device owns
 * register, transfer, completion and IRQ state; no successful transfer is
 * synthesized by this adapter. Original kernel gates own CPU dispatch/EOI. */
#include <windows.h>
#include <memory>
#include <string>
#include <cstring>
#include <cstdio>
#include "machine/ohci.hpp"
#include "machine/pic.hpp"
#include "machine/apu_clock.hpp"
#include "usb_lle.h"

namespace {
SRWLOCK usb_lock=SRWLOCK_INIT;
std::unique_ptr<xlle::Ohci> controller;
std::string failure;
PilotUsbDmaRelease release_dma;
uint64_t start_ms;
bool irq_level;
xlle::DualPic pic;
bool pic_ready;
PilotUsbHostRead host_read;
PilotUsbHostRumble host_rumble;
PilotUsbHostCaps host_caps;
bool caps_bound;
uint64_t output_reports,input_reports;
uint64_t clock_ms()
{
    LARGE_INTEGER now,frequency;
    QueryPerformanceCounter(&now);QueryPerformanceFrequency(&frequency);
    return (uint64_t)(now.QuadPart/frequency.QuadPart)*1000+
        (uint64_t)(now.QuadPart%frequency.QuadPart)*1000/frequency.QuadPart;
}
void advance()
{
    if(host_read) {
        uint8_t report[18];uint32_t status=host_read(0,report);
        if(status && status!=ERROR_DEVICE_NOT_CONNECTED){failure="USB LLE host input poll failed";return;}
        controller->set_xid_connected(status==0);
        if(status)caps_bound=false;
        if(!status && !caps_bound) {
            std::array<uint8_t,18> input;std::array<uint8_t,4> output;
            status=host_caps(0,input.data(),output.data());
            if(status==ERROR_DEVICE_NOT_CONNECTED){controller->set_xid_connected(false);return;}
            if(status){failure="USB LLE host capabilities unavailable";return;}
            controller->set_xid_capabilities(input,output);caps_bound=true;
        }
        if(!status) {
            xlle::Ohci::XidGamepadState state;
            std::memcpy(&state.buttons,report,2);
            std::memcpy(state.analog.data(),report+2,8);
            std::memcpy(&state.thumb_lx,report+10,2);std::memcpy(&state.thumb_ly,report+12,2);
            std::memcpy(&state.thumb_rx,report+14,2);std::memcpy(&state.thumb_ry,report+16,2);
            controller->set_xid_gamepad_state(state);
        }
    }
    controller->set_frame(clock_ms()-start_ms);
    if(host_rumble && controller->xid_output_reports()!=output_reports) {
        uint32_t status=host_rumble(0,controller->xid_left_rumble(),controller->xid_right_rumble());
        if(status){failure="USB LLE host cannot deliver output report";return;}
        output_reports=controller->xid_output_reports();
    }
    uint64_t reports=controller->xid_endpoint2_input_reports();
    if(reports!=input_reports && (reports<5 || reports>=input_reports+1000)) {
        std::fprintf(stderr,"[USB LLE INPUT] endpoint2_reports=%llu configured=%u address=%u\n",
            (unsigned long long)reports,controller->xid_configuration(),controller->xid_address());
        input_reports=reports;
    }
}
}
extern "C" const char *pilot_usb_lle_host_bind(PilotUsbHostRead read,PilotUsbHostRumble rumble,PilotUsbHostCaps caps)
{
    AcquireSRWLockExclusive(&usb_lock);
    const char *error=nullptr;
    if(host_read || !read || !rumble || !caps)error="USB LLE invalid/repeated host input binding";
    else {host_read=read;host_rumble=rumble;host_caps=caps;}
    ReleaseSRWLockExclusive(&usb_lock);return error;
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
        controller->set_irq([](int irq,bool level){irq_level=level;pic.raise(irq,level);},1);
        release_dma=release;start_ms=clock_ms();
    }
    const char *result=failure.empty()?nullptr:failure.c_str();
    ReleaseSRWLockExclusive(&usb_lock);return result;
}
extern "C" void pilot_usb_pic_raise(int line,int level)
{
    AcquireSRWLockExclusive(&usb_lock);
    pic.raise(line,level!=0);
    ReleaseSRWLockExclusive(&usb_lock);
}
extern "C" const char *pilot_usb_pic_bootstrap(uint16_t mask,uint16_t level)
{
    AcquireSRWLockExclusive(&usb_lock);
    if(!pic_ready) {
        // Flat ring-0 Xbox IRQ vectors: 0x30..0x3f, slave on master IRQ2.
        pic.io_write(0x20,1,0x11);pic.io_write(0xa0,1,0x11);
        pic.io_write(0x21,1,0x30);pic.io_write(0xa1,1,0x38);
        pic.io_write(0x21,1,4);pic.io_write(0xa1,1,2);
        pic.io_write(0x21,1,1);pic.io_write(0xa1,1,1);
        pic.io_write(0x4d0,1,level&255);pic.io_write(0x4d1,1,level>>8);
        pic.io_write(0x21,1,mask&255);pic.io_write(0xa1,1,mask>>8);
        pic.raise(1,irq_level);pic_ready=true;
    }
    ReleaseSRWLockExclusive(&usb_lock);return nullptr;
}
extern "C" const char *pilot_usb_pic_port(uint32_t port,int size,int write,uint32_t *value)
{
    AcquireSRWLockExclusive(&usb_lock);
    const char *error=nullptr;
    if(!pic_ready || port>65535 || !pic.owns((uint16_t)port) || size!=1)
        error="USB LLE unsupported PIC port transaction";
    else if(write)pic.io_write((uint16_t)port,size,*value);
    else *value=pic.io_read((uint16_t)port,size);
    ReleaseSRWLockExclusive(&usb_lock);return error;
}
extern "C" int pilot_usb_pic_vector(int acknowledge)
{
    AcquireSRWLockExclusive(&usb_lock);
    int vector=pic_ready?(acknowledge?pic.acknowledge():pic.peek()):-1;
    ReleaseSRWLockExclusive(&usb_lock);return vector;
}
extern "C" const char *pilot_usb_lle_tick(void)
{
    AcquireSRWLockExclusive(&usb_lock);
    if(controller && failure.empty())advance();
    const char *result=failure.empty()?nullptr:failure.c_str();
    ReleaseSRWLockExclusive(&usb_lock);return result;
}
extern "C" const char *pilot_usb_lle_access(uint32_t address,int write,uint32_t *value)
{
    /* XGSCNT is used by the original USB DPC for sample/frame synchronization.
     * Share the LLE counter contract without importing an entire APU backend. */
    if(address==0xfe80200cu && !write) {
        LARGE_INTEGER now,frequency;QueryPerformanceCounter(&now);QueryPerformanceFrequency(&frequency);
        *value=xlle::apu_sample_counter((uint64_t)now.QuadPart,(uint64_t)frequency.QuadPart);
        return nullptr;
    }
    AcquireSRWLockExclusive(&usb_lock);
    if(!controller)failure="USB LLE access before binding";
    if(address<0xfed00000u || address>=0xfed01000u || (address&3))
        failure="USB LLE unsupported register transaction";
    if(failure.empty()) {
        advance();
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
