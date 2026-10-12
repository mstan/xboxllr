/* One offline shared-engine integration fixture. No files/storage initializer,
 * host input, title, renderer, playback or hardware transport is called. */
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "audio_device_lle.h"
extern "C" {
#include "io_device_lle.h"
#include "usb_lle.h"
}
#define CHECK(x) do { if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);exit(1);} }while(0)
static unsigned char *guest;
static uint64_t ticks=60000;
static unsigned raw_calls,clock_calls;
static std::vector<uint8_t> received;
static uint32_t &word(uint32_t address){return *reinterpret_cast<uint32_t*>(guest+address);}
static uint32_t call(unsigned ordinal,const uint32_t *args) {
    uint32_t result=0;const char *error=pilot_io_device_lle(ordinal,guest,args,&result);
    if(error)fprintf(stderr,"ordinal %u: %s\n",ordinal,error);
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
    uint32_t result=snapshot_word(f,pa);fclose(f);return result;
}
static const char *body(uint32_t entry,uint32_t regs[8],unsigned arguments,int *terminate,uint32_t *status) {
    CHECK(entry==0x12000 && arguments==1);++raw_calls;
    const char *error=pilot_io_device_thread_execute(entry,regs);
    if(error)return error;
    return pilot_io_device_thread_completion(terminate,status);
}
static const char *clock_input(void*,uint64_t *value) {
    ++clock_calls;if(word(0x19020)==1){ticks+=6000;word(0x19020)=2;}
    *value=ticks;return nullptr;
}
static int consume(void*,const uint8_t *bytes,size_t count) {
    received.insert(received.end(),bytes,bytes+count);return 1;
}
static int reject(void*,const uint8_t*,size_t){return 0;}
static void access(uint32_t address,unsigned size,uint32_t value) {
    const char *error=pilot_audio_device_lle_access(address,size,1,&value);
    if(error)fprintf(stderr,"access %08x: %s\n",address,error);CHECK(!error);
}
static const char *dummy(uint32_t,unsigned,int,uint32_t*){return nullptr;}
int main(int argc,char **argv) {
    CHECK(argc==3);const char *mode=argv[1];
    guest=static_cast<unsigned char*>(VirtualAlloc(nullptr,size_t(1)<<32,MEM_RESERVE,PAGE_NOACCESS));CHECK(guest);
    CHECK(VirtualAlloc(guest+0x10000,0x10000,MEM_COMMIT,PAGE_READWRITE));
    CHECK(!pilot_io_device_initialize(guest));CHECK(!pilot_io_device_image(guest,0x10000,0x10000));
    CHECK(!pilot_io_device_resident(0x10000,0x10000));
    uint32_t prep[]={captured_thread(argv[2]),31};call(148,prep);call(36,prep);
    CHECK(!pilot_usb_pit_time(ticks));
    if(!strcmp(mode,"rollback")) {
        CHECK(!pilot_io_device_mmio_bind(0xfec00000,0x1000,dummy));
        CHECK(pilot_audio_device_lle_open(consume,nullptr));PilotAudioDeviceState s;pilot_audio_device_lle_state(&s);
        CHECK(!s.owned && !s.apu_mapping && !s.work_binding);
        CHECK(!pilot_io_device_mmio_bind(0xfe800000,0x80000,dummy));
        CHECK(!pilot_io_device_mmio_bind(0xfe800000,0x80000,nullptr));
        CHECK(!pilot_io_device_mmio_bind(0xfec00000,0x1000,nullptr));
        puts("PASS exact rollback preserves preexisting ACI owner");return 0;
    }
    CHECK(!pilot_audio_device_lle_open(!strcmp(mode,"absent")?nullptr:!strcmp(mode,"reject")?reject:consume,nullptr));
    PilotAudioDeviceState state;pilot_audio_device_lle_state(&state);
    CHECK(state.owned && state.apu_mapping && state.aci_mapping && state.work_binding && state.gp_held && state.ep_held && state.clock_ticks==60000);
    if(!strcmp(mode,"unknown") || !strcmp(mode,"gap")) {
        // Actual original setup records GP reset=3 at FE83FFFC. Recording
        // that write did not qualify its device semantics.
        uint32_t value=3;const char *error=pilot_audio_device_lle_access(!strcmp(mode,"gap")?0xfe803030:0xfe83fffc,4,1,&value);CHECK(error);
        CHECK(pilot_audio_device_lle_access(0xfe800000,4,1,&value)==error);
        pilot_audio_device_lle_state(&state);CHECK(!state.dma_reads && !state.dma_writes && !state.pcm_words);
        CHECK(!pilot_audio_device_lle_close());puts("PASS unknown reset/FIFO gap before mutation; persistent first failure");return 0;
    }
    uint32_t alloc[]={4096};uint32_t va=call(165,alloc);CHECK(va);uint32_t pa=call(173,&va);CHECK(pa);
    memset(guest+va,0,4096);word(va)=pa+256;word(va+4)=0x80000200;
    for(unsigned i=0;i<1024;++i)guest[va+256+i]=uint8_t(i*17);
    access(0xfec00110,4,pa);access(0xfec00115,1,0);
    if(!strcmp(mode,"invalid")) {
        access(0xfec00110,4,pa+4092);uint32_t run=0x11;
        CHECK(pilot_audio_device_lle_access(0xfec0011b,1,1,&run));
        pilot_audio_device_lle_state(&state);CHECK(state.dma_reads==1 && !state.pcm_words && !state.descriptors && received.empty());
        CHECK(pilot_audio_device_lle_close());pilot_audio_device_lle_state(&state);CHECK(state.owned);
        puts("PASS invalid complete DMA extent; no descriptor publication; terminal active owner retained");return 0;
    }
    if(!strcmp(mode,"absent")) {
        uint32_t run=0x11;CHECK(pilot_audio_device_lle_access(0xfec0011b,1,1,&run));
        pilot_audio_device_lle_state(&state);CHECK(!state.dma_reads && !state.pcm_words && !state.descriptors);
        CHECK(!pilot_audio_device_lle_close());call(171,&va);puts("PASS absent consumer before descriptor fetch/publication");return 0;
    }
    if(!strcmp(mode,"reject") || !strcmp(mode,"paused")) {
        access(0xfec0011b,1,0x11);
        CHECK(!pilot_audio_device_lle_sample());pilot_audio_device_lle_state(&state);CHECK(!state.pcm_words);
        if(!strcmp(mode,"paused"))access(0xfec0011b,1,0);
        else {CHECK(!pilot_usb_pit_time(ticks+6000));CHECK(pilot_audio_device_lle_sample());}
        pilot_audio_device_lle_state(&state);CHECK(!state.pcm_words && !state.descriptors && received.empty());
        CHECK(pilot_audio_device_lle_close());pilot_audio_device_lle_state(&state);CHECK(state.owned && state.work_binding);
        puts("PASS consumer rejection/paused descriptor retains terminal owner; no completion");return 0;
    }
    CHECK(!strcmp(mode,"positive") || !strcmp(mode,"engine-fault"));
    // Real raw CPU writes RUN (descriptor DMA inside registered callback),
    // then real original pump advances one ACI interval while CPU loops.
    const uint8_t code[]={0xc6,0x05,0x1b,0x01,0xc0,0xfe,0x11,
        0xc7,0x05,0x20,0x90,0x01,0,1,0,0,0,
        0xb9,0xa0,0x86,0x01,0,0x49,0x75,0xfd,0xc2,4,0};
    memcpy(guest+0x12000,code,sizeof(code));pilot_io_device_thread_bind(body);
    if(!strcmp(mode,"engine-fault")) {
        const uint8_t unknown[]={0xc7,0x05,0xfc,0xff,0x83,0xfe,3,0,0,0,0xc2,4,0};
        memcpy(guest+0x12000,unknown,sizeof(unknown));
    }
    CHECK(!pilot_io_device_clock_bind(clock_input,nullptr));
    uint32_t create[]={0x19000,0,0x12000,0x19040,0};CHECK(!call(254,create));uint32_t handle=word(0x19000);
    uint32_t ref[]={handle,0,0x19004};CHECK(!call(246,ref));uint32_t object=word(0x19004);
    uint32_t priority[]={object,29};call(148,priority);
    uint32_t join[]={object,0,0,0,0};
    if(!strcmp(mode,"engine-fault")) {
        uint32_t result=0;CHECK(pilot_io_device_lle(159,guest,join,&result));CHECK(raw_calls==1);
        pilot_audio_device_lle_state(&state);CHECK(!state.dma_reads && !state.pcm_words && !state.descriptors);
        CHECK(pilot_audio_device_lle_close());pilot_audio_device_lle_state(&state);
        CHECK(state.owned && state.apu_mapping && state.aci_mapping && state.work_binding);
        puts("PASS actual raw unknown startup guard; terminal engine refuses cleanup; registered owner retained");return 0;
    }
    CHECK(!call(159,join));CHECK(!call(187,&handle));call(250,&object);
    CHECK(raw_calls==1 && clock_calls>1 && word(0x19020)==2);
    pilot_audio_device_lle_state(&state);CHECK(state.dma_reads>=2 && state.pcm_words==512 && state.descriptors==1 && state.gp_held && state.ep_held);
    CHECK(received.size()==1024 && !memcmp(received.data(),guest+va+256,1024));
    uint32_t mask=0xbf;CHECK(!pilot_usb_pic_port(0x21,1,1,&mask));CHECK(pilot_usb_pic_vector(0)==0x36);
    CHECK(pilot_usb_pic_vector(1)==0x36);
    access(0xfec00116,2,0x1c);uint32_t eoi=0x20;CHECK(!pilot_usb_pic_port(0x20,1,1,&eoi));
    CHECK(pilot_usb_pic_vector(0)==-1);
    access(0xfec0011b,1,0);CHECK(!pilot_audio_device_lle_close());
    CHECK(!pilot_io_device_clock_bind(nullptr,nullptr));call(171,&va);
    CHECK(!pilot_io_device_mmio_bind(0xfe800000,0x80000,dummy));CHECK(!pilot_io_device_mmio_bind(0xfe800000,0x80000,nullptr));
    puts("PASS raw CPU registered audio callback + owned DMA + original clock work + shared IRQ + retired cleanup");return 0;
}
