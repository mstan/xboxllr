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
#include "machine/pit.hpp"
#include "machine/apu_clock.hpp"
#include "machine/mcpx_static_outputs.hpp"
#include "machine/rtc.hpp"
#include "machine/smbus.hpp"
#include "usb_lle.h"

namespace {
SRWLOCK usb_lock=SRWLOCK_INIT;
std::unique_ptr<xlle::Ohci> controller;
std::string failure;
PilotUsbDmaRelease release_dma;
uint64_t start_ms;
bool irq_level;
xlle::DualPic pic;
xlle::Pit8254 pit;
uint64_t pit_ticks;
bool pic_ready;
// Retail board profile: GPIO15/FWENB has no external termination (Xbox
// hardware specification 1.02 section 2.6.5). Retain the actual programmed
// output; it cannot grant flash access or mutate ROM in this profile.
std::string board_failure;
xlle::McpxStaticOutputs board_outputs(1u<<15,
    [](const std::string& why){board_failure=why;});
xlle::Mc146818Rtc rtc;
std::unique_ptr<xlle::Amd756Smbus> smbus;
xlle::XboxSmc *smc_owner;
std::string smbus_failure;
PilotUsbTrayState tray_state;
PilotUsbEepromAccess eeprom_access;
std::unique_ptr<xlle::XboxEeprom> shared_eeprom(PilotUsbEepromAccess access)
{
    return std::make_unique<xlle::XboxEeprom>([access](uint8_t offset,unsigned width,bool write,uint16_t& value) {
        uint32_t data=value;
        if(!access(offset,(int)width,write,&data) || data>(width==1?255u:65535u))return false;
        value=(uint16_t)data;return true;
    });
}
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
        pit.set_irq0([]{pic.raise(0,true);pic.raise(0,false);});
        auto fault=[](const std::string& why){if(smbus_failure.empty())smbus_failure=why;};
        smbus=std::make_unique<xlle::Amd756Smbus>(0xc000,fault);
        auto smc=std::make_unique<xlle::XboxSmc>(fault);
        smc_owner=smc.get();
        smc->set_tray_state_reader([] {
            uint8_t value=0;
            if(!tray_state || !tray_state(&value))smbus_failure="SMC tray state owner unavailable";
            return value;
        });
        smbus->add_slave(std::move(smc));
        if(eeprom_access)smbus->add_slave(shared_eeprom(eeprom_access));
        smbus->set_irq([](int irq,bool level){pic.raise(irq,level);},11);
    }
    ReleaseSRWLockExclusive(&usb_lock);return nullptr;
}
extern "C" const char *pilot_usb_pic_port(uint32_t port,int size,int write,uint32_t *value)
{
    AcquireSRWLockExclusive(&usb_lock);
    const char *error=nullptr;
    xlle::IoPortDevice *device=port<=65535 && pic.owns((uint16_t)port)?
        static_cast<xlle::IoPortDevice*>(&pic):port>=0x40 && port<=0x43?&pit:nullptr;
    if(!pic_ready || !device || size!=1 || !value)
        error="USB LLE unsupported PIC port transaction";
    else if(write)device->io_write((uint16_t)port,size,*value);
    else *value=device->io_read((uint16_t)port,size);
    ReleaseSRWLockExclusive(&usb_lock);return error;
}
extern "C" const char *pilot_usb_board_port(uint32_t port,int size,int write,uint32_t *value)
{
    AcquireSRWLockExclusive(&usb_lock);
    const char *error=nullptr;
    if(port>=0xc000 && port<=0xc00f) {
        unsigned offset=port-0xc000;
        bool qualified_register=size==1?(offset==0 || offset==2 || offset==4 || offset==6 || offset==7 || offset==8):
            size==2 && (offset==0 || offset==2 || offset==6);
        if(!smbus || !value || !qualified_register || (write && (size==1?*value>255:*value>65535)))
            error="SMBus unsupported register/width transaction";
        else if(!smbus_failure.empty())error=smbus_failure.c_str();
        else if(write && offset==2 && (*value&8)) {
            uint32_t address=smbus->io_read(0xc004,1),command=smbus->io_read(0xc008,1);
            bool modeled=address&1?(command==1 || (command==3 && tray_state) || command==4 || command==0x0f ||
                command==0x10 || command==0x11 || command==0x1b):
                (command==1 || command==6 || command==7 || command==8 || command==0x0e || command==0x1b);
            if((address>>1)==0x54 && eeprom_access) {
                if((*value&7)!=2 && (*value&7)!=3)error="SMBus EEPROM protocol outside qualified byte/word profile";
            } else if((address>>1)!=0x10)
                error="SMBus endpoint requires connected storage/encoder owner";
            else if((*value&7)!=2 || !modeled || (!(address&1) && command==7 && smbus->io_read(0xc006,1)>1)) {
                std::fprintf(stderr,"[SMBus SMC GUARD] address=%02x command=%02x data=%02x control=%02x\n",
                    address,command,smbus->io_read(0xc006,1),*value);
                error="SMBus SMC operation outside qualified byte-register profile";
            }
            if(!error) {
                smbus->io_write((uint16_t)port,size,*value);
                if(!smbus_failure.empty())error=smbus_failure.c_str();
            }
        }
        else {
            if(write)smbus->io_write((uint16_t)port,size,*value);
            else *value=smbus->io_read((uint16_t)port,size);
            if(!smbus_failure.empty())error=smbus_failure.c_str();
        }
    } else if(port>=0x70 && port<=0x73) {
        if(!pic_ready || size!=1 || !value || (write && *value>255))
            error="RTC unsupported port transaction";
        else if(port==0x70 && write && (*value&128))
            error="RTC NMI mask programming outside qualified CMOS profile";
        else if((port&1) && !xlle::Mc146818Rtc::signature_ram(rtc.selected_index()))
            error="RTC clock/status access outside qualified CMOS RAM profile";
        else if(write)rtc.io_write((uint16_t)port,size,*value);
        else *value=rtc.io_read((uint16_t)port,size);
    } else if(port!=0x80cf || size!=1 || !write || !value)
        error="MCPX retail FWENB unsupported register transaction";
    else if(!board_outputs.write((uint16_t)port,*value))error=board_failure.c_str();
    ReleaseSRWLockExclusive(&usb_lock);return error;
}
extern "C" const char *pilot_usb_board_smc_led_requests(uint32_t *known,uint32_t *mode,uint32_t *sequence)
{
    AcquireSRWLockExclusive(&usb_lock);
    const char *error=nullptr;
    if(!smc_owner || !known || !mode || !sequence)error="SMC LED observer outside initialized owner";
    else {
        const auto state=smc_owner->led_requests();
        *known=state.known;*mode=state.mode;*sequence=state.sequence;
    }
    ReleaseSRWLockExclusive(&usb_lock);return error;
}
extern "C" const char *pilot_usb_board_bind_tray(PilotUsbTrayState read)
{
    AcquireSRWLockExclusive(&usb_lock);
    const char *error=nullptr;
    if(!read || tray_state)error="SMC invalid/repeated storage tray binding";
    else tray_state=read;
    ReleaseSRWLockExclusive(&usb_lock);return error;
}
extern "C" const char *pilot_usb_board_bind_eeprom(PilotUsbEepromAccess access)
{
    AcquireSRWLockExclusive(&usb_lock);
    const char *error=nullptr;
    if(!access || eeprom_access)error="SMBus invalid/repeated EEPROM owner binding";
    else {
        eeprom_access=access;
        if(smbus)smbus->add_slave(shared_eeprom(access));
    }
    ReleaseSRWLockExclusive(&usb_lock);return error;
}
extern "C" const char *pilot_usb_board_outputs(uint32_t *known,uint32_t *levels)
{
    AcquireSRWLockExclusive(&usb_lock);
    const char *error=nullptr;
    if(!known || !levels)error="MCPX retail output observation requires destinations";
    else {*known=board_outputs.known();*levels=board_outputs.levels();}
    ReleaseSRWLockExclusive(&usb_lock);return error;
}
extern "C" int pilot_usb_board_eeprom_bound(void)
{
    AcquireSRWLockShared(&usb_lock);
    int bound=eeprom_access!=nullptr;
    ReleaseSRWLockShared(&usb_lock);return bound;
}
extern "C" const char *pilot_usb_pit_time(uint64_t ticks)
{
    AcquireSRWLockExclusive(&usb_lock);
    const char *error=nullptr;
    if(!pic_ready || ticks<pit_ticks)error="PIT LLE uninitialized/backwards clock";
    else {pit_ticks=ticks;pit.set_time(ticks);}
    ReleaseSRWLockExclusive(&usb_lock);return error;
}
extern "C" const char *pilot_usb_pit_clock(uint64_t *ticks,uint64_t *next_irq)
{
    AcquireSRWLockExclusive(&usb_lock);
    const char *error=nullptr;
    if(!pic_ready || !ticks || !next_irq)error="PIT LLE clock query outside initialized device";
    else {*ticks=pit_ticks;*next_irq=pit.next_irq0_tick();}
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
