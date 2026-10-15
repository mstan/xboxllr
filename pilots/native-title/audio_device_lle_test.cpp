/* One offline shared-engine integration fixture. No files/storage initializer,
 * host input, title, renderer, playback or hardware transport is called. */
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "audio_device_lle.h"
#include "audio_device_pcm_capture.h"
#include "audio_device_timeline.h"
#include <string>
extern "C" {
#include "io_device_lle.h"
#include "usb_lle.h"
}
#define CHECK(x) do { if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);exit(1);} }while(0)
static unsigned char *guest;
static uint64_t ticks=60000;
static unsigned raw_calls,clock_calls;
static std::vector<uint8_t> received;
static PilotPcmCapture *capture=nullptr;
static std::string destination;
static uint64_t timeline_input_ms,timeline_body_counter;
static bool timeline_body,timeline_input_reject;
static uint32_t timeline_input(uint64_t milliseconds) {
    if(timeline_input_reject && milliseconds)return 1;
    CHECK(milliseconds>=timeline_input_ms);timeline_input_ms=milliseconds;return 0;
}
static void capture_view_check(uint64_t expected_bytes,uint64_t expected_stamps,const uint8_t *expected) {
    const uint8_t *bytes=nullptr;const PilotPcmCaptureStamp *stamps=nullptr;uint64_t count=0,records=0;
    pilot_pcm_capture_view(capture,&bytes,&count,&stamps,&records);
    CHECK(count==expected_bytes && records==expected_stamps);
    if(count)CHECK(!memcmp(bytes,expected,size_t(count)));
    if(records)CHECK(stamps[0].lifetime_id && stamps[0].epoch_ticks==60000 &&
        stamps[0].observed_pit_ticks==66000 && stamps[0].codec_frame==11 && stamps[0].byte_offset==0 && stamps[0].bytes==1024);
}
static void capture_readback(const std::string& path,uint64_t expected_bytes,uint64_t expected_records,unsigned status,const uint8_t *expected) {
    FILE *file=fopen(path.c_str(),"rb");CHECK(file);PilotPcmCaptureHeader h{};CHECK(fread(&h,sizeof(h),1,file)==1);
    CHECK(!memcmp(h.magic,"XLLEAC97",8) && h.version==1 && h.header_bytes==sizeof(h) &&
        h.rate==48000 && h.channels==2 && h.bits==16 && h.status==status && h.pcm_bytes==expected_bytes && h.stamp_count==expected_records);
    PilotAudioDeviceState state;pilot_audio_device_lle_state(&state);
    for(uint64_t i=0;i<h.stamp_count;++i){PilotPcmCaptureStamp stamp{};CHECK(fread(&stamp,sizeof(stamp),1,file)==1);
        CHECK(stamp.lifetime_id==h.lifetime_id && stamp.epoch_ticks==h.epoch_ticks && stamp.bytes && !stamp.reserved);
        if(i==0)CHECK(stamp.observed_pit_ticks==66000 && stamp.codec_frame==11 && !stamp.byte_offset);}
    std::vector<uint8_t> actual(size_t(h.pcm_bytes));if(h.pcm_bytes)CHECK(fread(actual.data(),1,actual.size(),file)==actual.size());
    if(h.pcm_bytes)CHECK(!memcmp(actual.data(),expected,actual.size()));
    PilotPcmCaptureFooter footer{};CHECK(fread(&footer,sizeof(footer),1,file)==1);
    CHECK(!memcmp(footer.magic,"XLLEEND1",8) && footer.pcm_bytes==h.pcm_bytes && footer.stamp_count==h.stamp_count && footer.status==status && fgetc(file)==EOF);
    CHECK(!fclose(file));
}
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
    if(timeline_body)CHECK(!pilot_audio_device_timeline_counter(&timeline_body_counter));
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
    CHECK(argc==3 || argc==4);const char *mode=argv[1];
    const bool sink_mode=!strcmp(mode,"positive") || !strncmp(mode,"capture-",8);
    if(sink_mode){CHECK(argc==4);destination=std::string(argv[3])+"/"+mode+"-"+std::to_string(GetCurrentProcessId())+".bin";
        CHECK(!pilot_pcm_capture_create(&capture,destination.c_str(),!strcmp(mode,"capture-full")?1024:4096,
            !strcmp(mode,"capture-stamps")?1:8));
        PilotPcmCapture *duplicate=nullptr;CHECK(pilot_pcm_capture_create(&duplicate,destination.c_str(),1024,1) && !duplicate);
        CHECK(GetFileAttributesA(destination.c_str())==INVALID_FILE_ATTRIBUTES);}
    guest=static_cast<unsigned char*>(VirtualAlloc(nullptr,size_t(1)<<32,MEM_RESERVE,PAGE_NOACCESS));CHECK(guest);
    CHECK(VirtualAlloc(guest+0x10000,0x10000,MEM_COMMIT,PAGE_READWRITE));
    CHECK(!pilot_io_device_initialize(guest));CHECK(!pilot_io_device_image(guest,0x10000,0x10000));
    CHECK(!pilot_io_device_resident(0x10000,0x10000));
    uint32_t prep[]={captured_thread(argv[2]),31};call(148,prep);call(36,prep);
    CHECK(!pilot_usb_pit_time(ticks));
    if(!strcmp(mode,"rollback") || !strcmp(mode,"capture-rollback")) {
        CHECK(!pilot_io_device_mmio_bind(0xfec00000,0x1000,dummy));
        CHECK(pilot_audio_device_lle_open(capture?pilot_pcm_capture_consume:consume,capture));PilotAudioDeviceState s;pilot_audio_device_lle_state(&s);
        CHECK(!s.owned && !s.apu_mapping && !s.work_binding);
        CHECK(!pilot_io_device_mmio_bind(0xfe800000,0x80000,dummy));
        CHECK(!pilot_io_device_mmio_bind(0xfe800000,0x80000,nullptr));
        CHECK(!pilot_io_device_mmio_bind(0xfec00000,0x1000,nullptr));
        if(capture){CHECK(!pilot_audio_device_lle_consumer_attached(capture));CHECK(!pilot_pcm_capture_destroy(capture));}
        puts("PASS exact rollback preserves preexisting ACI owner");return 0;
    }
    CHECK(!pilot_audio_device_lle_open(capture?pilot_pcm_capture_consume:!strcmp(mode,"absent")?nullptr:!strcmp(mode,"reject")?reject:consume,capture));
    if(!strcmp(mode,"runtime-clock") || !strcmp(mode,"runtime-clock-input-failure")) {
        uint64_t counter=UINT64_MAX;
        CHECK(pilot_audio_device_timeline_counter(&counter) && counter==UINT64_MAX);
        CHECK(pilot_audio_device_timeline_start(0,timeline_input));
        CHECK(pilot_audio_device_timeline_start(2000001,timeline_input));
        CHECK(pilot_audio_device_timeline_start(32000,nullptr));
        CHECK(pilot_audio_device_timeline_start(32000,timeline_input)); // unprogrammed PIT
        CHECK(!pilot_io_device_clock_start());
        CHECK(!pilot_audio_device_timeline_start(32000,timeline_input));
        CHECK(pilot_audio_device_timeline_start(32000,timeline_input)); // no rebind
        CHECK(!pilot_audio_device_timeline_counter(&counter));
        const auto initial_counter=counter;
        if(!strcmp(mode,"runtime-clock-input-failure")) {
            timeline_input_reject=true;
            uint32_t us=10000,result=0;const char *error=pilot_io_device_lle(151,guest,&us,&result);
            CHECK(error && strstr(error,"reference controller time rejected"));
            uint64_t pit=0,next=0;CHECK(!pilot_usb_pit_clock(&pit,&next) && pit==60000 && !timeline_input_ms);
            counter=UINT64_MAX;CHECK(pilot_audio_device_timeline_counter(&counter) && counter==UINT64_MAX);
            CHECK(pilot_audio_device_lle_close());PilotAudioDeviceState failed;pilot_audio_device_lle_state(&failed);
            CHECK(failed.owned && failed.work_binding && failed.clock_ticks==60000 && !failed.dma_reads && !failed.pcm_words);
            puts("PASS original input rejection stops before PIT/device publication or fabricated TSC; registered owner retained");return 0;
        }
        Sleep(30);CHECK(!pilot_audio_device_timeline_counter(&counter) && counter==initial_counter);
        CHECK(!pilot_audio_device_lle_sample());
        PilotAudioDeviceState initial;pilot_audio_device_lle_state(&initial);CHECK(initial.clock_ticks==60000);

        uint32_t bytes=3*4096,va=call(165,&bytes),pa=call(173,&va);CHECK(va && pa);
        memset(guest+va,0,bytes);word(va)=pa+4096;word(va+8)=pa+8192;
        const uint32_t program[]={0x08f485,0xfff,0x08f484,1,0x0a8581,4,0x08f485,2,0x08f484,1,0x0af080,4};
        memcpy(guest+va+4096,program,sizeof(program));
        access(0xfe80200c,4,0);access(0xfe801100,4,0x7130f);
        access(0xfe802040,4,pa);access(0xfe8020d4,4,1);
        access(0xfe802048,4,pa);access(0xfe8020dc,4,1);
        access(0xfe83fffc,4,1);access(0xfe83fffc,4,3);access(0xfe85fffc,4,3);
        pilot_audio_device_lle_state(&initial);
        uint32_t us=10000;call(151,&us); // unchanged original calibrated loop, no service substitution
        PilotAudioDeviceState stalled;pilot_audio_device_lle_state(&stalled);
        CHECK(stalled.clock_ticks>=initial.clock_ticks+11250 && stalled.gp_phase==3 && stalled.ep_phase==2);
        CHECK(stalled.ep_cycles>=1333125 && !stalled.gp_quanta && !stalled.ep_events && !stalled.pcm_words);
        CHECK(timeline_input_ms==(stalled.clock_ticks-60000)/1125);
        CHECK(!pilot_audio_device_timeline_counter(&counter));
        CHECK(counter==stalled.clock_ticks/1125000*733333333ull+(stalled.clock_ticks%1125000)*733333333ull/1125000);
        uint32_t usb_before=0;CHECK(!pilot_usb_lle_access(0xfe80200c,0,&usb_before));
        Sleep(30);
        for(unsigned i=0;i<100;++i) {
            uint64_t observed=0;CHECK(!pilot_audio_device_timeline_counter(&observed) && observed==counter);
            uint32_t usb_after=0;CHECK(!pilot_usb_lle_access(0xfe80200c,0,&usb_after) && usb_after==usb_before);
            CHECK(!pilot_audio_device_lle_sample());
        }
        PilotAudioDeviceState frozen;pilot_audio_device_lle_state(&frozen);
        CHECK(frozen.clock_ticks==stalled.clock_ticks && frozen.gp_cycles==stalled.gp_cycles && frozen.ep_cycles==stalled.ep_cycles);
        access(0xfe802000,4,15);us=1000;call(151,&us);
        PilotAudioDeviceState active;pilot_audio_device_lle_state(&active);
        CHECK(active.gp_quanta && active.ep_events && !active.pcm_words);
        access(0xfe802000,4,7);call(151,&us);pilot_audio_device_lle_state(&frozen);
        CHECK(frozen.gp_quanta==active.gp_quanta && frozen.ep_events==active.ep_events && frozen.gp_phase==3 && frozen.ep_phase==2);

        // Two real RDTSC instructions on the original CPU must equal the
        // same pure counter used by the maintained compiled runtime helper.
        const uint8_t code[]={0x0f,0x31,0xa3,0x40,0x90,0x01,0,0x89,0x15,0x44,0x90,0x01,0,
            0x0f,0x31,0xa3,0x48,0x90,0x01,0,0x89,0x15,0x4c,0x90,0x01,0,0xc2,4,0};
        memcpy(guest+0x12000,code,sizeof(code));timeline_body=true;pilot_io_device_thread_bind(body);
        uint32_t create[]={0x19000,0,0x12000,0x19040,0};CHECK(!call(254,create));uint32_t handle=word(0x19000);
        uint32_t ref[]={handle,0,0x19004};CHECK(!call(246,ref));uint32_t object=word(0x19004);
        uint32_t priority[]={object,29};call(148,priority);
        uint32_t join[]={object,0,0,0,0};CHECK(!call(159,join));
        CHECK(raw_calls==1 && (uint64_t(word(0x19044))<<32|word(0x19040))==timeline_body_counter &&
            (uint64_t(word(0x1904c))<<32|word(0x19048))==timeline_body_counter);
        CHECK(!call(187,&handle));call(250,&object);
        CHECK(pilot_audio_device_lle_close());pilot_audio_device_lle_state(&frozen);CHECK(frozen.owned && frozen.work_binding);
        printf("PASS native runtime clock: real10000us original stall returns after %llu explicit PITticks; active GP=%llu EP=%llu; host pauses/polls inert; original RDTSC agrees; active reset owner retained\n",
            (unsigned long long)(stalled.clock_ticks-initial.clock_ticks),(unsigned long long)active.gp_quanta,(unsigned long long)active.ep_events);
        return 0;
    }
    if(!strcmp(mode,"ep-lookup")) {
        CHECK(argc==4);std::vector<uint8_t> image(8192);
        FILE *file=fopen(argv[3],"rb");CHECK(file && fread(image.data(),1,image.size(),file)==image.size() && fgetc(file)==EOF);CHECK(!fclose(file));
        uint32_t bytes=5*4096,va=call(165,&bytes),pa=call(173,&va);CHECK(va && pa);
        memset(guest+va,0,bytes);
        for(unsigned i=0;i<4;++i)word(va+i*8)=pa+4096+i*4096;
        for(unsigned i=0;i<2048;++i)word(va+4096+i*4)=0x87;
        // Actual EP instructions copy the entire immutable Y0800 image into
        // X200, then the real DSP DMA publishes it to original allocated RAM.
        std::vector<uint32_t> program={0x60f400,0x800,0x61f400,0x200,0x060098,4,0x4cd800,0x445900,0,
            0x08f496,2,0x08f494,0x2e00,0x08f496,1};
        const auto wait=uint32_t(program.size());
        program.insert(program.end(),{0x0a8587,wait,0x08f485,0x80,0x08f496,2,0x87});
        for(unsigned i=0;i<program.size();++i)word(va+4096+i*4)=program[i];
        const uint32_t node[]={0x4000,0x59e2,2048,0x200,0x2000};
        memcpy(guest+va+4096+0x600*4,node,sizeof(node));
        access(0xfe802048,4,pa);access(0xfe8020dc,4,3);access(0xfe85fffc,4,3);
        ticks+=750;CHECK(!pilot_usb_pit_time(ticks));CHECK(!pilot_audio_device_lle_sample());
        PilotAudioDeviceState s;pilot_audio_device_lle_state(&s);
        CHECK(s.gp_held && s.ep_phase==4 && s.ep_cycles && s.dma_writes && !s.pcm_words);
        CHECK(!memcmp(guest+va+12288,image.data(),image.size()));
        const auto reads=s.dma_reads,writes=s.dma_writes,cycles=s.ep_cycles;
        CHECK(!pilot_audio_device_lle_sample());pilot_audio_device_lle_state(&s);
        CHECK(s.dma_reads==reads && s.dma_writes==writes && s.ep_cycles==cycles);
        CHECK(pilot_audio_device_lle_close());pilot_audio_device_lle_state(&s);CHECK(s.owned && s.work_binding);
        puts("PASS selected native EP lookup: all2048 actual CPU reads and exact owned DMA bytes; released owner retained for pending reset qualification");return 0;
    }
    if(!strcmp(mode,"clock-active")) {
        uint32_t bytes=3*4096,va=call(165,&bytes),pa=call(173,&va);CHECK(va && pa);
        memset(guest+va,0,bytes);word(va)=pa+4096;word(va+8)=pa+8192;
        const uint32_t program[]={0x08f485,0xfff,0x08f484,1,0x0a8581,4,0x08f485,2,0x08f484,1,0x0af080,4};
        memcpy(guest+va+4096,program,sizeof(program));
        access(0xfe80200c,4,0);access(0xfe801100,4,0x7130f);
        access(0xfe802040,4,pa);access(0xfe8020d4,4,1);
        access(0xfe802048,4,pa);access(0xfe8020dc,4,1);
        access(0xfe83fffc,4,1);access(0xfe83fffc,4,3);access(0xfe85fffc,4,3);
        ticks+=200;CHECK(!pilot_usb_pit_time(ticks));CHECK(!pilot_audio_device_lle_sample());
        PilotAudioDeviceState s;pilot_audio_device_lle_state(&s);
        CHECK(s.gp_phase==3 && s.ep_phase==2 && !s.gp_quanta && !s.ep_events);
        access(0xfe802000,4,15);ticks=78037;
        CHECK(!pilot_usb_pit_time(ticks));CHECK(!pilot_audio_device_lle_sample());pilot_audio_device_lle_state(&s);
        CHECK(s.gp_quanta==24 && s.ep_events==3 && s.gp_phase==3 && s.ep_phase==2 && !s.pcm_words);
        const auto gp_cycles=s.gp_cycles,ep_cycles=s.ep_cycles;
        CHECK(!pilot_audio_device_lle_sample());pilot_audio_device_lle_state(&s);
        CHECK(s.gp_cycles==gp_cycles && s.ep_cycles==ep_cycles && s.gp_quanta==24 && s.ep_events==3);
        access(0xfe802000,4,7);ticks+=6000;CHECK(!pilot_usb_pit_time(ticks));CHECK(!pilot_audio_device_lle_sample());
        pilot_audio_device_lle_state(&s);CHECK(s.gp_quanta==24 && s.ep_events==3 && s.gp_phase==3 && s.ep_phase==2);
        CHECK(pilot_audio_device_lle_close());pilot_audio_device_lle_state(&s);CHECK(s.owned && s.work_binding);
        puts("PASS native shared clock:24 actual GP frames,3 EP events, no repeated-time work and retained active-reset ownership");return 0;
    }
    if(!strcmp(mode,"host-controls")) {
        auto read=[](uint32_t address){uint32_t value=0;CHECK(!pilot_audio_device_lle_access(address,4,0,&value));return value;};
        for(unsigned life=0;life<3;++life) {
            if(life)CHECK(!pilot_audio_device_lle_open(consume,nullptr));
            for(uint32_t address:{0xfe83ff00u,0xfe83ff04u,0xfe85ff00u,0xfe85ff04u,0xfe85ff5cu}) {
                access(address,4,0);CHECK(read(address)==0);
            }
            const uint32_t requests[]={0xfffffff1,0xd000b815,0xfffffff5};
            access(0xfe83fffc,4,1);access(0xfe83ff10,4,requests[life]);
            CHECK(read(0xfe83ff10)==1);access(0xfe83ff10,4,0);CHECK(read(0xfe83ff10)==1);
            access(0xfe83ff14,4,0xff);CHECK(read(0xfe83ff14)==0);
            access(0xfe83fffc,4,0);CHECK(read(0xfe83ff10)==0);
            access(0xfe85ff10,4,1);access(0xfe85ff10,4,0);CHECK(read(0xfe85ff10)==1);
            access(0xfe85ff14,4,0xffffffff);CHECK(read(0xfe85ff14)==0);
            ++ticks;CHECK(!pilot_audio_device_lle_sample());CHECK(read(0xfe85ff10)==1);
            PilotAudioDeviceState s;pilot_audio_device_lle_state(&s);
            CHECK(s.gp_held && s.ep_held && !s.dma_reads && !s.dma_writes && !s.pcm_words && received.empty());
            CHECK(!pilot_audio_device_lle_close());
        }
        puts("PASS three native host-control lifetimes with shared clock and no DSP/PCM work");return 0;
    }
    if(!strcmp(mode,"codec-startup")) {
        auto read=[](unsigned off,unsigned size){uint32_t value=0;CHECK(!pilot_audio_device_lle_access(0xfec00000+off,size,0,&value));return value;};
        for(unsigned life=0;life<3;++life) {
            if(life)CHECK(!pilot_audio_device_lle_open(consume,nullptr));
            CHECK(read(0x12c,4)==2 && read(0x130,4)==0x300100);
            CHECK(read(0x7c,2)==0x574d && read(0x7e,2)==0x4c09);
            CHECK(read(0x134,1)==0);access(0xfec00018,2,0x0808);
            for(unsigned i=0;i<100;++i)CHECK(read(0x134,1)==1);
            CHECK(!pilot_audio_device_lle_sample());CHECK(read(0x134,1)==1);
            ticks+=24;CHECK(!pilot_usb_pit_time(ticks));CHECK(!pilot_audio_device_lle_sample());
            CHECK(read(0x134,1)==0 && read(0x18,2)==0);
            uint32_t args[]={0x2000,0,0x03ffffff,0x1000,PAGE_READWRITE};
            const uint32_t va=call(166,args),pa=call(173,&va);CHECK(va && pa);
            for(unsigned stream:{0x110u,0x170u}) {
                access(0xfec00000+stream+11,1,2);CHECK(read(stream+11,1)==0 && read(stream,4)==0);
                access(0xfec00000+stream,4,pa+(stream==0x170?0x1000:0));
                access(0xfec00000+stream+4,1,0);access(0xfec00000+stream+5,1,0);
                CHECK(read(stream+6,2)==1 && read(stream+8,2)==0);
            }
            access(0xfec0017c,4,0x02000000);CHECK(read(0x17c,4)==0x02000000);
            PilotAudioDeviceState state;pilot_audio_device_lle_state(&state);
            CHECK(!state.dma_reads && !state.dma_writes && !state.pcm_words && received.empty());
            CHECK(!pilot_audio_device_lle_close());call(171,&va);
        }
        puts("PASS native clock services asynchronous codec writes; stopped PCM/SPDIF rings release all original RAM");return 0;
    }
    if(!strcmp(mode,"startup-methods")) {
        for(unsigned life=0;life<3;++life) {
            if(life)CHECK(!pilot_audio_device_lle_open(consume,nullptr));
            uint32_t args[]={0x10000,0,0x03ffffff,0x4000,PAGE_READWRITE};
            uint32_t va=call(166,args),pa=call(173,&va);CHECK(va && pa && !(pa&0x3fff));
            memset(guest+va,0xa5,0x10000);
            access(0xfe801100,4,0x88);access(0xfe801138,4,0x7ff);access(0xfe801148,4,1);
            access(0xfe802030,4,pa);access(0xfe802044,4,pa+0x4000);
            access(0xfe801510,4,0);access(0xfe801100,4,0x100f);
            for(uint32_t off:{0x2a4u,0x2b0u,0x2a0u,0x2a8u,0x2acu})access(0xfe820000+off,4,0xfff);
            access(0xfe8202c0,4,0x09070806);
            access(0xfe820804,4,0x7ff);access(0xfe820808,4,pa+0xe000);
            access(0xfe821800,4,1);access(0xfe821808,4,pa+0xf000);
            CHECK(word(va+0x3ff8)==pa+0xe000 && word(va+0x3ffc)==0xa5a5a5a5);
            CHECK(word(va+0x4008)==pa+0xf000 && word(va+0x400c)==0xa5a5a5a5);
            CHECK(word(va+8)==0xa5a5a5a5); // Output belongs to GPF, independently of VPSGE.
            access(0xfe803024,4,0);access(0xfe803028,4,0x2000);
            access(0xfe821000,4,0);access(0xfe821004,4,0x2000);
            for(uint32_t offset:{0x3024u,0x3034u,0x3044u,0x3054u}) {
                uint32_t value=99;CHECK(!pilot_audio_device_lle_access(0xfe800000+offset,4,0,&value));CHECK(value==0);
            }
            PilotAudioDeviceState s;pilot_audio_device_lle_state(&s);
            CHECK(s.gp_held && s.ep_held && s.dma_writes==2 && !s.pcm_words && received.empty());
            CHECK(!pilot_audio_device_lle_close());call(171,&va);
        }
        puts("PASS three native startup-method lifetimes with real owned VPSGE/GPF writes and held DSPs");return 0;
    }
    if(!strcmp(mode,"startup-mappings")) {
        for(unsigned life=0;life<3;++life) {
            if(life)CHECK(!pilot_audio_device_lle_open(consume,nullptr));
            uint32_t args[]={0xc000,0,0x03ffffff,0x4000,PAGE_READWRITE};
            uint32_t va=call(166,args),pa=call(173,&va);CHECK(va && pa && !(pa&0x3fff));
            access(0xfe802038,4,pa);access(0xfe80203c,4,pa+0x4000);
            access(0xfe802034,4,pa+0x8000);
            for(uint32_t off:{0x2038u,0x203cu}) {
                uint32_t value=0;CHECK(!pilot_audio_device_lle_access(0xfe800000+off,4,0,&value));
                CHECK(value==pa+(off==0x2038?0:0x4000));
            }
            PilotAudioDeviceState s;pilot_audio_device_lle_state(&s);
            CHECK(s.gp_held && s.ep_held && !s.dma_reads && !s.dma_writes && !s.pcm_words && received.empty());
            if(life==2) {
                uint32_t value=0xfff;
                CHECK(pilot_audio_device_lle_access(0xfe8202a4,4,1,&value)); // rejection permanently faults this engine
            }
            CHECK(!pilot_audio_device_lle_close());call(171,&va);
        }
        puts("PASS three native original-aligned mapping lifetimes, readback, no DMA and unchanged next-method guard");return 0;
    }
    if(!strcmp(mode,"configuration")) {
        access(0xfe801100,4,0x88);access(0xfe802000,4,0);
        const uint32_t registers[][2]={{0x1500,0},{0x1508,0x800040},{0x150c,0},
            {0x2008,0x800},{0x2010,0x800},{0x2020,0x600},{0x2028,0x100},
            {0x1104,0xff},{0x1124,0x1fff},{0x1130,0x08000000},{0x1158,0x20}};
        for(const auto& reg:registers) {
            access(0xfe800000+reg[0],4,reg[1]);uint32_t value=0xa5a5a5a5;
            CHECK(!pilot_audio_device_lle_access(0xfe800000+reg[0],4,0,&value) && value==reg[1]);
        }
        access(0xfe80200c,4,0);access(0xfe802000,4,7);
        for(uint32_t control:{8u,0x100fu}) {
            access(0xfe801100,4,control);uint32_t value=0;
            CHECK(!pilot_audio_device_lle_access(0xfe801100,4,0,&value) && value==(0x70000|control));
        }
        PilotAudioDeviceState s;pilot_audio_device_lle_state(&s);
        CHECK(s.gp_held && s.ep_held && !s.dma_reads && !s.dma_writes && !s.pcm_words && received.empty());
        CHECK(!pilot_audio_device_lle_close());
        puts("PASS native stopped configuration groups and physical FE control readback without DMA");return 0;
    }
    if(!strcmp(mode,"counter-unknown")) {
        uint32_t value=0xa5a55a5a;
        CHECK(pilot_audio_device_lle_access(0xfe80200c,4,0,&value) && value==0xa5a55a5a);
        CHECK(!pilot_audio_device_lle_close());
        puts("PASS uninitialized warm counter rejects without fabricating output");return 0;
    }
    if(!strcmp(mode,"counter")) {
        auto sample=[&]() {uint32_t value=0;CHECK(!pilot_audio_device_lle_access(0xfe80200c,4,0,&value));return value;};
        auto advance=[&](uint64_t delta) {ticks+=delta;CHECK(!pilot_usb_pit_time(ticks));CHECK(!pilot_audio_device_lle_sample());};
        for(unsigned life=0;life<3;++life) {
            if(life)CHECK(!pilot_audio_device_lle_open(consume,nullptr));
            access(0xfe80200c,4,0);advance(11250);CHECK(sample()==0);
            access(0xfe802000,4,15);advance(1125);CHECK(sample()==48);
            for(unsigned read=0;read<100;++read)CHECK(sample()==48);
            access(0xfe802000,4,7);advance(11250);CHECK(sample()==48);
            access(0xfe802000,4,15);advance(2250);CHECK(sample()==144);
            access(0xfe802000,4,7);access(0xfe80200c,4,0);advance(11250);CHECK(sample()==0);
            PilotAudioDeviceState s;pilot_audio_device_lle_state(&s);
            CHECK(s.gp_held && s.ep_held && !s.dma_reads && !s.dma_writes && !s.pcm_words && received.empty());
            CHECK(!pilot_audio_device_lle_close());
        }
        puts("PASS three real shared-clock counter lifetimes: stopped, run, pause, resume and reset without DMA");return 0;
    }
    if(!strcmp(mode,"reset-bootstrap") || !strcmp(mode,"reset-clock") || !strcmp(mode,"reset-clock-active")) {
        access(0xfe80200c,4,0);
        if(!strcmp(mode,"reset-clock-active"))access(0xfe801100,4,0x7130f);
        uint32_t bytes=12288,va=call(165,&bytes),pa=call(173,&va);CHECK(va && pa);
        word(va)=pa+4096;word(va+8)=pa+8192;
        for(unsigned i=0;i<2048;++i)word(va+4096+i*4)=0xa7000087; // Authored STOP image.
        access(0xfe802040,4,pa);access(0xfe8020d4,4,1);
        access(0xfe802048,4,pa);access(0xfe8020dc,4,1);
        access(0xfe83fffc,4,1);access(0xfe83fffc,4,3);
        access(0xfe85fffc,4,1);access(0xfe85fffc,4,3);
        PilotAudioDeviceState s;pilot_audio_device_lle_state(&s);
        CHECK(!s.gp_held && !s.ep_held && s.dma_reads==8 && !s.dma_writes && !s.pcm_words);
        for(uint32_t a:{0xfe83fffcu,0xfe85fffcu}) {
            uint32_t v=0;CHECK(!pilot_audio_device_lle_access(a,4,0,&v) && v==3);access(a,4,3);
        }
        pilot_audio_device_lle_state(&s);CHECK(s.dma_reads==8 && received.empty());
        if(!strcmp(mode,"reset-clock")) {
            CHECK(!pilot_audio_device_lle_sample());pilot_audio_device_lle_state(&s);CHECK(!s.gp_cycles && !s.ep_cycles);
            ticks+=1;CHECK(!pilot_usb_pit_time(ticks));CHECK(!pilot_audio_device_lle_sample());
            pilot_audio_device_lle_state(&s);CHECK(s.gp_phase==4 && s.ep_phase==4 && s.gp_cycles && s.ep_cycles);
            const auto gp_cycles=s.gp_cycles,ep_cycles=s.ep_cycles;
            ticks+=11250;CHECK(!pilot_usb_pit_time(ticks));CHECK(!pilot_audio_device_lle_sample());
            pilot_audio_device_lle_state(&s);CHECK(s.gp_cycles==gp_cycles && s.ep_cycles==ep_cycles && s.dma_reads==8);
        }
        if(!strcmp(mode,"reset-clock-active")) {
            access(0xfe802000,4,15);
            CHECK(!pilot_audio_device_lle_sample()); // no elapsed time or invented frame
            ticks+=750;CHECK(!pilot_usb_pit_time(ticks));
            const char *error=pilot_audio_device_lle_sample();CHECK(error && strstr(error,"overrun remains unqualified"));
            CHECK(pilot_audio_device_lle_close());
            pilot_audio_device_lle_state(&s);CHECK(s.owned && s.work_binding && !s.pcm_words && s.dma_reads==8);
            CHECK(!s.gp_quanta && !s.ep_events);
            puts("PASS active clock cannot invent a completion for a stopped GP; released owners retained");return 0;
        }
        access(0xfe83fffc,4,0);access(0xfe85fffc,4,1);
        pilot_audio_device_lle_state(&s);CHECK(s.gp_held && s.ep_held && s.dma_reads==8);
        CHECK(!pilot_audio_device_lle_close());call(171,&va);
        puts("PASS real pinned-SG GP/EP bootstrap, repeated release/passive reads, hold and clean teardown");return 0;
    }
    if(!strcmp(mode,"startup-active")) {
        access(0xfe802000,4,15);access(0xfe802000,4,7);
        uint32_t value=0xa5a55a5a;
        CHECK(pilot_audio_device_lle_access(0xfe801510,4,0,&value));CHECK(value==0xa5a55a5a);
        PilotAudioDeviceState s;pilot_audio_device_lle_state(&s);
        CHECK(s.gp_held && s.ep_held && !s.dma_reads && !s.dma_writes && !s.pcm_words);
        CHECK(!pilot_audio_device_lle_close());
        puts("PASS processing history revokes startup FE control; failed read preserves caller value");return 0;
    }
    if(!strcmp(mode,"startup") || !strcmp(mode,"startup-width")) {
        uint32_t value=0xa5a5a5a5;
        if(!strcmp(mode,"startup-width")) {
            CHECK(pilot_audio_device_lle_access(0xfe802000,2,0,&value));
            CHECK(value==0xa5a5a5a5);CHECK(!pilot_audio_device_lle_close());
            puts("PASS unsupported SECTL width rejects before supplying a value");return 0;
        }
        for(unsigned life=0;life<3;++life) {
            if(life)CHECK(!pilot_audio_device_lle_open(consume,nullptr));
            CHECK(!pilot_audio_device_lle_access(0xfe83fffc,4,0,&value));CHECK(value==0);
            CHECK(!pilot_audio_device_lle_access(0xfe85fffc,4,0,&value));CHECK(value==1);
            access(0xfe83fffc,4,0);access(0xfe85fffc,4,1);access(0xfe83fffc,4,1);
            CHECK(!pilot_audio_device_lle_access(0xfe801510,4,0,&value));CHECK(value==1);
            for(unsigned read=0;read<3;++read) {
                CHECK(!pilot_audio_device_lle_access(0xfe802000,4,0,&value));CHECK(value==7);
                CHECK(!pilot_audio_device_lle_access(0xfe801004,4,0,&value));CHECK(value==0xafa);
                CHECK(!pilot_audio_device_lle_sample());
            }
            for(uint32_t next:{1u,0u,1u}) {
                access(0xfe801510,4,next);
                CHECK(!pilot_audio_device_lle_access(0xfe801510,4,0,&value));CHECK(value==next);
            }
            // Values independently observed from the original title RMW helper.
            for(uint32_t next:{7u,15u,23u,31u,7u}) {
                access(0xfe802000,4,next);
                CHECK(!pilot_audio_device_lle_access(0xfe802000,4,0,&value));CHECK(value==next);
            }
            for(uint32_t next:{0xafau,0xafbu,0xafau,0xd8u}) {
                access(0xfe801004,4,next);
                CHECK(!pilot_audio_device_lle_access(0xfe801004,4,0,&value));CHECK(value==next);
            }
            PilotAudioDeviceState s;pilot_audio_device_lle_state(&s);
            CHECK(s.gp_held && s.ep_held && !s.dma_reads && !s.dma_writes && !s.pcm_words && !s.descriptors);
            CHECK(s.clock_ticks==ticks && received.empty());CHECK(!pilot_audio_device_lle_close());
        }
        puts("PASS three post-kernel SECTL/IEN lifetimes, real register RMW storage, held DSPs and passive reads");return 0;
    }
    if(capture)CHECK(!pilot_pcm_capture_bind(capture));
    PilotAudioDeviceState state;pilot_audio_device_lle_state(&state);
    CHECK(state.owned && state.apu_mapping && state.aci_mapping && state.work_binding && state.gp_held && state.ep_held && state.clock_ticks==60000);
    if(!strcmp(mode,"unknown") || !strcmp(mode,"unknown-read") || !strcmp(mode,"gap")) {
        // Reset0/1/3 is qualified separately. Abort bit3 is not admitted.
        const bool read=!strcmp(mode,"unknown-read");
        uint32_t value=read?0xa5a55a5a:11;const char *error=pilot_audio_device_lle_access(!strcmp(mode,"gap")?0xfe803030:0xfe83fffc,read?2:4,!read,&value);CHECK(error);
        CHECK(strstr(error,!strcmp(mode,"gap")?"address=fe803030":"address=fe83fffc"));
        CHECK(strstr(error,read?"width=2":"width=4"));
        CHECK(strstr(error,read?" read":" write value=0000000b"));
        if(read){CHECK(!strstr(error,"value="));CHECK(value==0xa5a55a5a);}
        CHECK(pilot_audio_device_lle_access(0xfe800000,4,1,&value)==error);
        pilot_audio_device_lle_state(&state);CHECK(!state.dma_reads && !state.dma_writes && !state.pcm_words);
        CHECK(!pilot_audio_device_lle_close());puts("PASS unknown reset/FIFO gap before mutation; persistent first failure");return 0;
    }
    uint32_t alloc[]={4096};uint32_t va=call(165,alloc);CHECK(va);uint32_t pa=call(173,&va);CHECK(pa);
    memset(guest+va,0,4096);word(va)=pa+256;word(va+4)=0x80000200;
    for(unsigned i=0;i<1024;++i)guest[va+256+i]=uint8_t(i*17);
    access(0xfec00110,4,pa);access(0xfec00115,1,0);
    if(!strcmp(mode,"capture-catchup")) {
        word(va+4)=0x80000600;
        for(unsigned i=0;i<3072;++i)guest[va+256+i]=uint8_t(i*17);
        access(0xfec0011b,1,0x11);ticks=78000;CHECK(!pilot_usb_pit_time(ticks));CHECK(!pilot_audio_device_lle_sample());
        capture_view_check(3072,3,guest+va+256);
        const PilotPcmCaptureStamp *stamps=nullptr;pilot_pcm_capture_view(capture,nullptr,nullptr,&stamps,nullptr);
        for(unsigned i=0;i<3;++i)CHECK(stamps[i].observed_pit_ticks==66000+i*6000 && stamps[i].codec_frame==11+i &&
            stamps[i].byte_offset==i*1024 && stamps[i].bytes==1024);
        pilot_audio_device_lle_state(&state);CHECK(state.clock_ticks==78000 && state.pcm_words==1536 && state.descriptors==1);
        CHECK(!pilot_audio_device_lle_sample());capture_view_check(3072,3,guest+va+256);
        access(0xfec0011b,1,0);CHECK(!pilot_audio_device_lle_close());
        CHECK(!pilot_pcm_capture_finish(capture,1));capture_readback(destination,3072,3,0,guest+va+256);
        CHECK(!pilot_pcm_capture_destroy(capture));capture=nullptr;call(171,&va);
        puts("PASS native catch-up: three exact PCM frames retain their individual serviced timestamps and retire cleanly");return 0;
    }
    if(!strcmp(mode,"capture-full") || !strcmp(mode,"capture-stamps") || !strcmp(mode,"capture-failed") || !strcmp(mode,"capture-closed")) {
        word(va+4)=0x80000400;for(unsigned i=1024;i<2048;++i)guest[va+256+i]=uint8_t(i*17);
        access(0xfec0011b,1,0x11);CHECK(!pilot_usb_pit_time(66000));CHECK(!pilot_audio_device_lle_sample());
        capture_view_check(1024,1,guest+va+256);pilot_audio_device_lle_state(&state);CHECK(state.pcm_words==512 && !state.descriptors);
        const PilotPcmCaptureStamp *before_stamps=nullptr;pilot_pcm_capture_view(capture,nullptr,nullptr,&before_stamps,nullptr);
        PilotPcmCaptureStamp before=before_stamps[0];
        if(!strcmp(mode,"capture-failed"))pilot_pcm_capture_test_fail(capture,1);
        if(!strcmp(mode,"capture-closed"))CHECK(pilot_pcm_capture_finish(capture,1));
        CHECK(!pilot_usb_pit_time(72000));CHECK(pilot_audio_device_lle_sample());
        capture_view_check(1024,1,guest+va+256);pilot_audio_device_lle_state(&state);CHECK(state.pcm_words==512 && !state.descriptors);
        const PilotPcmCaptureStamp *after_stamps=nullptr;pilot_pcm_capture_view(capture,nullptr,nullptr,&after_stamps,nullptr);
        CHECK(!memcmp(&before,after_stamps,sizeof(before)));
        CHECK(pilot_audio_device_lle_close());CHECK(pilot_pcm_capture_destroy(capture));
        CHECK(pilot_audio_device_lle_consumer_attached(capture));capture_view_check(1024,1,guest+va+256);
        pilot_pcm_capture_finish(capture,0);CHECK(GetFileAttributesA(destination.c_str())==INVALID_FILE_ATTRIBUTES);
        capture_readback(destination+".partial",1024,1,1,guest+va+256);
        puts("PASS production sink rejects full/failed/closed without changing accepted prefix or ACI completion; registered sink retained");return 0;
    }
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
    CHECK(!strcmp(mode,"positive") || !strcmp(mode,"engine-fault") || !strcmp(mode,"capture-engine"));
    // Real raw CPU writes RUN (descriptor DMA inside registered callback),
    // then real original pump advances one ACI interval while CPU loops.
    const uint8_t code[]={0xc6,0x05,0x1b,0x01,0xc0,0xfe,0x11,
        0xc7,0x05,0x20,0x90,0x01,0,1,0,0,0,
        0xb9,0xa0,0x86,0x01,0,0x49,0x75,0xfd,0xc2,4,0};
    memcpy(guest+0x12000,code,sizeof(code));pilot_io_device_thread_bind(body);
    if(!strcmp(mode,"engine-fault") || !strcmp(mode,"capture-engine")) {
        const uint8_t unknown[]={0xc7,0x05,0xfc,0xff,0x83,0xfe,11,0,0,0,0xc2,4,0};
        memcpy(guest+0x12000,unknown,sizeof(unknown));
    }
    CHECK(!pilot_io_device_clock_bind(clock_input,nullptr));
    uint32_t create[]={0x19000,0,0x12000,0x19040,0};CHECK(!call(254,create));uint32_t handle=word(0x19000);
    uint32_t ref[]={handle,0,0x19004};CHECK(!call(246,ref));uint32_t object=word(0x19004);
    uint32_t priority[]={object,29};call(148,priority);
    uint32_t join[]={object,0,0,0,0};
    if(!strcmp(mode,"engine-fault") || !strcmp(mode,"capture-engine")) {
        uint32_t result=0;CHECK(pilot_io_device_lle(159,guest,join,&result));CHECK(raw_calls==1);
        const char *first=pilot_audio_device_lle_error();CHECK(first);
        CHECK(strstr(first,"address=fe83fffc width=4 write value=0000000b"));
        pilot_audio_device_lle_state(&state);CHECK(!state.dma_reads && !state.pcm_words && !state.descriptors);
        CHECK(pilot_audio_device_lle_close());pilot_audio_device_lle_state(&state);
        CHECK(state.owned && state.apu_mapping && state.aci_mapping && state.work_binding);
        if(capture){CHECK(pilot_pcm_capture_destroy(capture));CHECK(pilot_audio_device_lle_consumer_attached(capture));
            pilot_pcm_capture_finish(capture,0);capture_view_check(0,0,nullptr);CHECK(GetFileAttributesA(destination.c_str())==INVALID_FILE_ATTRIBUTES);}
        puts("PASS actual raw unknown startup guard; terminal engine refuses cleanup; registered owner retained");return 0;
    }
    CHECK(!call(159,join));CHECK(!call(187,&handle));call(250,&object);
    CHECK(raw_calls==1 && clock_calls>1 && word(0x19020)==2);
    pilot_audio_device_lle_state(&state);CHECK(state.dma_reads>=2 && state.pcm_words==512 && state.descriptors==1 && state.gp_held && state.ep_held);
    if(capture)capture_view_check(1024,1,guest+va+256);
    else CHECK(received.size()==1024 && !memcmp(received.data(),guest+va+256,1024));
    uint32_t mask=0xbf;CHECK(!pilot_usb_pic_port(0x21,1,1,&mask));CHECK(pilot_usb_pic_vector(0)==0x36);
    CHECK(pilot_usb_pic_vector(1)==0x36);
    access(0xfec00116,2,0x1c);uint32_t eoi=0x20;CHECK(!pilot_usb_pic_port(0x20,1,1,&eoi));
    CHECK(pilot_usb_pic_vector(0)==-1);
    access(0xfec0011b,1,0);CHECK(!pilot_audio_device_lle_close());
    if(capture) {
        CHECK(!pilot_pcm_capture_finish(capture,1));CHECK(GetFileAttributesA((destination+".partial").c_str())==INVALID_FILE_ATTRIBUTES);
        capture_readback(destination,1024,1,0,guest+va+256);CHECK(!pilot_pcm_capture_destroy(capture));capture=nullptr;
        CHECK(pilot_pcm_capture_create(&capture,destination.c_str(),1024,1) && !capture);
        // Successive empty real-device lifetimes isolate persistence controls
        // from already-proved raw CPU PCM acceptance. A readable footer in a
        // failed partial never makes the final destination visible.
        for(unsigned phase=2;phase<=5;++phase) {
            std::string path=destination+"-io-"+std::to_string(phase);
            CHECK(!pilot_pcm_capture_create(&capture,path.c_str(),1024,1));
            CHECK(!pilot_audio_device_lle_open(pilot_pcm_capture_consume,capture));CHECK(!pilot_pcm_capture_bind(capture));
            PilotAudioDeviceState fresh;pilot_audio_device_lle_state(&fresh);CHECK(fresh.lifetime_id>state.lifetime_id && fresh.epoch_ticks==66000);
            CHECK(!pilot_audio_device_lle_close());
            if(phase<5)pilot_pcm_capture_test_fail(capture,phase);
            else {FILE *existing=fopen(path.c_str(),"wbx");CHECK(existing);CHECK(fwrite("KEEP",4,1,existing)==1);CHECK(!fclose(existing));}
            CHECK(pilot_pcm_capture_finish(capture,1));
            if(phase<5)CHECK(GetFileAttributesA(path.c_str())==INVALID_FILE_ATTRIBUTES);
            else {FILE *existing=fopen(path.c_str(),"rb");char sentinel[4];CHECK(existing && fread(sentinel,4,1,existing)==1 && !memcmp(sentinel,"KEEP",4));CHECK(!fclose(existing));}
            capture_readback(path+".partial",0,0,0,nullptr);CHECK(!pilot_pcm_capture_destroy(capture));capture=nullptr;
        }
        printf("[AC97 CAPTURE] %s\n",destination.c_str());
    }
    CHECK(!pilot_io_device_clock_bind(nullptr,nullptr));call(171,&va);
    CHECK(!pilot_io_device_mmio_bind(0xfe800000,0x80000,dummy));CHECK(!pilot_io_device_mmio_bind(0xfe800000,0x80000,nullptr));
    puts("PASS raw CPU registered audio callback + owned DMA + original clock work + shared IRQ + retired cleanup");return 0;
}
