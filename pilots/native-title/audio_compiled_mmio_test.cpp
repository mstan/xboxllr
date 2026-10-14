/* Offline original ISR/DPC -> native MOV fault -> sole registered device.
 * No title, storage initializer, playback or active voice is constructed. */
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "audio_device_lle.h"
extern "C" {
#include "io_device_lle.h"
#include "usb_lle.h"
#include "mmio_fault.h"
}
#define CHECK(x) do {if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static unsigned char *guest;
static const char *mode;
static unsigned callbacks,dpcs,faults,dma_calls,work_calls,wrong_thread_checks,nested_checks,nested_isrs;
static uint32_t allocation,physical_address;
static uint64_t ticks=60000;
static const char *first_error;
static uint32_t &word(uint32_t address){return *reinterpret_cast<uint32_t*>(guest+address);}
static uint32_t call(unsigned ordinal,const uint32_t *args) {
    uint32_t result=0;const char *error=pilot_io_device_lle(ordinal,guest,args,&result);
    if(error)fprintf(stderr,"ordinal%u: %s\n",ordinal,error);
    CHECK(!error);return result;
}
static uint32_t snapshot_word(FILE *f,uint32_t offset) {
    uint32_t value=0;CHECK(!fseek(f,offset,SEEK_SET));CHECK(fread(&value,4,1,f)==1);return value;
}
static uint32_t captured_thread(const char *path) {
    FILE *f=fopen(path,"rb");CHECK(f);uint32_t va=0x8003da64;
    uint32_t pde=snapshot_word(f,0xf000+(va>>22)*4);CHECK(pde&1);
    uint32_t pa=(pde&128)?(pde&0xffc00000)+(va&0x3fffff):
        (snapshot_word(f,(pde&0xfffff000)+((va>>12)&1023)*4)&0xfffff000)+(va&4095);
    uint32_t result=snapshot_word(f,pa);CHECK(!fclose(f));return result;
}
static const char *clock_input(void*,uint64_t *value){*value=ticks;return nullptr;}
static const char *work(void) {
    ++work_calls;uint32_t value=0xdeadbeef;
    CHECK(pilot_io_device_compiled_mmio(0xfe80200c,4,0,&value) && value==0xdeadbeef);
    return nullptr;
}
static DWORD WINAPI wrong_thread(void*) {
    uint32_t value=0xabcdef12;
    CHECK(pilot_io_device_compiled_mmio(0xfe80200c,4,0,&value) && value==0xabcdef12);
    ++wrong_thread_checks;return 0;
}
static const char *dma_register(uint32_t address,unsigned size,int write,uint32_t *value) {
    if(address==0xfe100ffc)return "registered compiled callback failure";
    CHECK(address==0xfe100000 && size==4 && !write);++dma_calls;
    uint32_t nested=0x12345678;
    CHECK(pilot_io_device_compiled_mmio(0xfe80200c,4,0,&nested) && nested==0x12345678);++nested_checks;
    unsigned char bytes[16];memset(bytes,0xcd,sizeof(bytes));
    uint32_t pa=physical_address+(!strcmp(mode,"dma-failure")?4092:0);
    if(!pilot_io_device_contiguous_dma(pa,bytes,sizeof(bytes),0,PILOT_IO_DMA_SYNC)) {
        for(auto byte:bytes)CHECK(byte==0xcd);
        return "compiled fixture complete DMA rejected";
    }
    CHECK(!memcmp(bytes,guest+allocation,sizeof(bytes)));
    memcpy(value,bytes,4);return nullptr;
}
static LONG CALLBACK fault(EXCEPTION_POINTERS *exception) {
    if(exception->ExceptionRecord->ExceptionCode!=EXCEPTION_ACCESS_VIOLATION)return EXCEPTION_CONTINUE_SEARCH;
    const char *error=nullptr;
    int result=pilot_mmio_fault_range(exception,guest,0xfe800000,0xfe87ffff,pilot_io_device_compiled_mmio,&error);
    if(!result)result=pilot_mmio_fault_range(exception,guest,0xfe100000,0xfe100fff,pilot_io_device_compiled_mmio,&error);
    CHECK(result==1 && !error);++faults;
    return EXCEPTION_CONTINUE_EXECUTION;
}
__declspec(noinline) static uint32_t compiled_load(uint32_t address) {
    return *reinterpret_cast<volatile uint32_t*>(guest+address);
}
static const char *decoded_rejection(uint32_t address,int write,uint32_t value) {
    unsigned char code[15]={0x8b,0x03};if(write)code[0]=0x89;
    CONTEXT context{};EXCEPTION_RECORD record{};EXCEPTION_POINTERS exception{&record,&context};
    context.Rip=reinterpret_cast<uintptr_t>(code);context.Rbx=reinterpret_cast<uintptr_t>(guest)+address;
    context.Rax=value;context.EFlags=0x246;
    record.ExceptionCode=EXCEPTION_ACCESS_VIOLATION;record.NumberParameters=2;
    record.ExceptionInformation[0]=write;record.ExceptionInformation[1]=context.Rbx;
    const char *error=nullptr;
    CHECK(pilot_mmio_fault_range(&exception,guest,address,address+3,pilot_io_device_compiled_mmio,&error)==-1);
    CHECK(error && context.Rip==reinterpret_cast<uintptr_t>(code) && context.Rax==value && context.EFlags==0x246);
    return error;
}
static const char *continuation(uint32_t entry,uint32_t regs[8],bool dpc) {
    if(entry==0x12540 && !dpc) {
        ++nested_isrs;pilot_usb_pic_raise(1,0);
        CHECK(compiled_load(0xfe80200c)==2560);
        regs[0]=1;regs[4]+=12;return nullptr;
    }
    CHECK(entry==(dpc?0x12560u:0x12520u));
    if(dpc)++dpcs;else {++callbacks;pilot_usb_pic_raise(3,0);}
    uint64_t before=0,deadline=0;CHECK(!pilot_usb_pit_clock(&before,&deadline));
    if(!strcmp(mode,"unknown")) {
        first_error=decoded_rejection(0xfe83fffc,1,11); // Abort remains outside reset0/1/3.
        uint32_t value=0xcccccccc;
        CHECK(pilot_io_device_compiled_mmio(0xfe80200c,4,0,&value)==first_error && value==0xcccccccc);
        return "later callback error must not overwrite first device error";
    }
    if(!strcmp(mode,"callback-failure")) {
        first_error=decoded_rejection(0xfe100ffc,0,0x11111111);
        return "later callback error must not overwrite first device error";
    }
    if(!strcmp(mode,"dma-failure")) {
        first_error=decoded_rejection(0xfe100000,0,0x11111111);
        return "later callback error must not overwrite first device error";
    }
    unsigned work_before=work_calls;
    CHECK(compiled_load(0xfe80200c)==2560); // actual counter at frozen60000 PIT ticks
    HANDLE thread=CreateThread(nullptr,0,wrong_thread,nullptr,0,nullptr);CHECK(thread);
    CHECK(WaitForSingleObject(thread,1000)==WAIT_OBJECT_0);CHECK(CloseHandle(thread));
    CHECK(compiled_load(0xfe100000)==0x31415926);
    CHECK(work_calls==work_before); // neither compiled read pumps device work
    unsigned sampled=work_calls;
    if(!dpc)pilot_usb_pic_raise(1,1); // higher original IRQL; real nested dispatcher
    uint32_t value=0;CHECK(!pilot_io_device_lle(125,guest,nullptr,&value));
    CHECK(work_calls>sampled); // nested original entry suspends callback scope
    unsigned after_service=work_calls;
    CHECK(compiled_load(0xfe80200c)==2560); // restored outer scope remains usable
    CHECK(work_calls==after_service);
    uint64_t after=0;CHECK(!pilot_usb_pit_clock(&after,&deadline) && after==before);
    if(!dpc) {
        uint32_t queue[]={0x1c300,0x13579bdf,0x2468ace0};CHECK(call(119,queue)==1);
    }
    regs[0]=1;regs[4]+=dpc?20:12;return nullptr;
}
static const char *isr(uint32_t entry,uint32_t regs[8]) {return continuation(entry,regs,false);}
static const char *dpc(uint32_t entry,uint32_t regs[8]) {return continuation(entry,regs,true);}
int main(int argc,char **argv) {
    CHECK(argc==3);mode=argv[1];
    guest=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x100000000ull,MEM_RESERVE,PAGE_NOACCESS));CHECK(guest);
    CHECK(VirtualAlloc(guest+0x10000,0x10000,MEM_COMMIT,PAGE_READWRITE));
    CHECK(!pilot_io_device_initialize(guest));CHECK(!pilot_io_device_image(guest,0x10000,0x10000));
    uint32_t prep[]={captured_thread(argv[2]),31};call(148,prep);call(36,prep);
    CHECK(!pilot_usb_pit_time(ticks));
    CHECK(!pilot_audio_device_lle_open(nullptr,nullptr));
    CHECK(!pilot_io_device_mmio_bind(0xfe100000,4096,dma_register));
    CHECK(!pilot_io_device_work_bind(work));CHECK(!pilot_io_device_clock_bind(clock_input,nullptr));
    uint32_t allocate[]={4096,0,0x03ffffff,4096,PAGE_READWRITE};allocation=call(166,allocate);CHECK(allocation);
    physical_address=call(173,&allocation);word(allocation)=0x31415926;
    for(unsigned i=4;i<16;++i)guest[allocation+i]=static_cast<unsigned char>(i*17);
    uint32_t outside=0x12345678;CHECK(pilot_io_device_compiled_mmio(0xfe80200c,4,0,&outside) && outside==0x12345678);
    CHECK(!pilot_audio_device_lle_error());
    uint32_t intr[]={0x1c200,0x12520,0xabcdef01,0x33,23,0,0};
    call(109,intr);CHECK((call(98,intr)&255)==1);
    uint32_t nested_intr[]={0x1c280,0x12540,0xabcdef02,0x31,25,0,0};
    call(109,nested_intr);CHECK((call(98,nested_intr)&255)==1);
    uint32_t queue[]={0x1c300,0x12560,0x10203040};call(107,queue);
    pilot_io_device_isr_bind(isr);pilot_io_device_dpc_bind(dpc);
    PVOID handler=AddVectoredExceptionHandler(1,fault);CHECK(handler);
    pilot_usb_pic_raise(3,1);int delivered=0;
    const char *error=pilot_io_device_irq(guest,&delivered);
    CHECK(callbacks==1);
    PilotAudioDeviceState state{};pilot_audio_device_lle_state(&state);
    CHECK(state.owned && state.gp_held && state.ep_held && !state.pcm_words && !state.descriptors);
    if(first_error) {
        CHECK(error==first_error);CHECK(pilot_audio_device_lle_close());
        pilot_audio_device_lle_state(&state);CHECK(state.owned && state.apu_mapping && state.aci_mapping && state.work_binding);
        CHECK(RemoveVectoredExceptionHandler(handler));
        puts("PASS terminal compiled rejection preserves first error, host context and registered lifetime");return 0;
    }
    if(error)fprintf(stderr,"IRQ: %s\n",error);
    CHECK(!error && delivered && faults==7 && dpcs==1 && nested_isrs==1 &&
        dma_calls==2 && nested_checks==2 && wrong_thread_checks==2);
    outside=0x12345678;CHECK(pilot_io_device_compiled_mmio(0xfe80200c,4,0,&outside) && outside==0x12345678);
    CHECK(!pilot_io_device_work_unbind(work));CHECK(!pilot_io_device_clock_bind(nullptr,nullptr));
    CHECK(!pilot_io_device_mmio_bind(0xfe100000,4096,nullptr));CHECK(!pilot_audio_device_lle_close());
    call(171,&allocation);CHECK(RemoveVectoredExceptionHandler(handler));
    puts("PASS real original ISR+DPC, native MOV faults, sole APU, owned DMA, wrong-thread/nested-work guards and scope retirement");
    return 0;
}
