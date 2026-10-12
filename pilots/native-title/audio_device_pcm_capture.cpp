/* Actual AC97 PCM memory acceptance; persistence is a separate operation.
 * No callback file I/O, allocation, CPU reentry or second clock/audio owner. */
#include <windows.h>
#include "audio_device_pcm_capture.h"
#include "audio_device_lle.h"
#include <string>
#include <cstring>
#include <new>
static_assert(sizeof(PilotPcmCaptureHeader)==80,"capture header ABI");
static_assert(sizeof(PilotPcmCaptureStamp)==48,"capture stamp ABI");
static_assert(sizeof(PilotPcmCaptureFooter)==32,"capture footer ABI");
struct PilotPcmCapture {
    HANDLE file=INVALID_HANDLE_VALUE;
    std::string destination,partial;
    uint8_t *pcm=nullptr;
    PilotPcmCaptureStamp *stamps=nullptr;
    uint64_t capacity=0,stamp_capacity=0,bytes=0,count=0,lifetime=0,epoch=0;
    const char *failure=nullptr;
    bool bound=false,finished=false;
#ifdef PILOT_PCM_CAPTURE_TEST
    unsigned injected=0;
#endif
    const char *fail(const char *why){if(!failure)failure=why;return failure;}
    ~PilotPcmCapture(){if(file!=INVALID_HANDLE_VALUE)CloseHandle(file);if(pcm)VirtualFree(pcm,0,MEM_RELEASE);if(stamps)VirtualFree(stamps,0,MEM_RELEASE);}
};
extern "C" const char *pilot_pcm_capture_create(PilotPcmCapture **out,const char *destination,uint64_t bytes,uint64_t stamps) {
    if(!out || *out || !destination || !*destination)return "invalid original PCM capture destination";
    if(!bytes)bytes=PILOT_PCM_CAPTURE_BYTES;if(!stamps)stamps=PILOT_PCM_CAPTURE_STAMPS;
    if(bytes>PILOT_PCM_CAPTURE_BYTES || stamps>PILOT_PCM_CAPTURE_STAMPS)return "original PCM capture exceeds bounded limits";
    PilotPcmCapture *c=new(std::nothrow) PilotPcmCapture;if(!c)return "original PCM capture allocation failed";
    try {c->destination=destination;c->partial=c->destination+".partial";}
    catch(...) {delete c;return "original PCM capture path allocation failed";}
    DWORD attributes=GetFileAttributesA(destination);
    if(attributes!=INVALID_FILE_ATTRIBUTES || GetLastError()!=ERROR_FILE_NOT_FOUND){delete c;return "original PCM destination exists or cannot be inspected";}
    c->file=CreateFileA(c->partial.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(c->file==INVALID_HANDLE_VALUE){delete c;return "original PCM exclusive partial capture create failed";}
    c->pcm=static_cast<uint8_t*>(VirtualAlloc(nullptr,size_t(bytes),MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    c->stamps=static_cast<PilotPcmCaptureStamp*>(VirtualAlloc(nullptr,size_t(stamps)*sizeof(PilotPcmCaptureStamp),MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    if(!c->pcm || !c->stamps){delete c;return "original PCM bounded storage allocation failed (partial retained)";}
    c->capacity=bytes;c->stamp_capacity=stamps;*out=c;return nullptr;
}
extern "C" const char *pilot_pcm_capture_bind(PilotPcmCapture *c) {
    if(!c)return "missing original PCM capture";
    if(c->failure)return c->failure;
    PilotAudioDeviceState state;pilot_audio_device_lle_state(&state);
    if(c->bound || c->finished || !state.owned || !state.lifetime_id || state.pcm_words ||
       !pilot_audio_device_lle_consumer_attached(c))return c->fail("original PCM capture lifetime binding rejected");
    c->lifetime=state.lifetime_id;c->epoch=state.epoch_ticks;c->bound=true;return nullptr;
}
extern "C" int pilot_pcm_capture_consume(void *context,const uint8_t *bytes,size_t count) {
    auto *c=static_cast<PilotPcmCapture*>(context);if(!c)return 0;
    if(c->failure)return 0;
    PilotAudioDeviceState state;pilot_audio_device_lle_state(&state);
    if(!c->bound || c->finished || !state.owned || state.lifetime_id!=c->lifetime ||
       state.epoch_ticks!=c->epoch || !pilot_audio_device_lle_consumer_attached(c) ||
       !bytes || !count || (count&3) || count>1024 || state.clock_ticks<c->epoch)
        {c->fail("original PCM capture invalid state/chunk/lifetime");return 0;}
    if(count>c->capacity-c->bytes || c->count==c->stamp_capacity)
        {c->fail("original PCM capture capacity exhausted; no truncation");return 0;}
    // All admission precedes copy/publication. Rejection may mark terminal
    // state but cannot change accepted bytes, counts or stamps.
    PilotPcmCaptureStamp stamp{c->lifetime,c->epoch,state.clock_ticks,state.codec_frame,c->bytes,uint32_t(count),0};
    std::memcpy(c->pcm+c->bytes,bytes,count);c->stamps[c->count]=stamp;
    c->bytes+=count;++c->count;return 1;
}
static bool write_bytes(PilotPcmCapture *c,const void *bytes,uint64_t count) {
    auto *p=static_cast<const uint8_t*>(bytes);
    while(count){DWORD chunk=DWORD(count>0x100000?0x100000:count),written=0;
        if(!WriteFile(c->file,p,chunk,&written,nullptr) || written!=chunk)return false;
        p+=written;count-=written;}
    return true;
}
extern "C" const char *pilot_pcm_capture_finish(PilotPcmCapture *c,int complete) {
    if(!c)return "missing original PCM capture";
    if(c->finished)return c->failure;
    c->finished=true;
    if(!c->bound)c->fail("original PCM capture finalized without device lifetime");
    if(complete && pilot_audio_device_lle_consumer_attached(c))
        c->fail("original PCM complete capture requires retired device consumer");
    const bool success=complete && !c->failure;
    PilotPcmCaptureHeader h{{'X','L','L','E','A','C','9','7'},1,sizeof(h),48000,2,16,success?0u:1u,
        c->lifetime,c->epoch,c->bytes,c->count,c->capacity,c->stamp_capacity};
    PilotPcmCaptureFooter f{{'X','L','L','E','E','N','D','1'},c->bytes,c->count,success?0u:1u,0};
    bool written=write_bytes(c,&h,sizeof(h)) && write_bytes(c,c->stamps,c->count*sizeof(*c->stamps)) &&
        write_bytes(c,c->pcm,c->bytes) && write_bytes(c,&f,sizeof(f));
#ifdef PILOT_PCM_CAPTURE_TEST
    if(c->injected==2)written=false;
#endif
    if(!written)c->fail("original PCM partial capture write failed");
    bool flushed=FlushFileBuffers(c->file)!=0;
#ifdef PILOT_PCM_CAPTURE_TEST
    if(c->injected==3)flushed=false;
#endif
    if(!flushed)c->fail("original PCM partial capture flush failed");
    bool closed=CloseHandle(c->file)!=0;if(closed)c->file=INVALID_HANDLE_VALUE;
#ifdef PILOT_PCM_CAPTURE_TEST
    if(c->injected==4)closed=false;
#endif
    if(!closed)c->fail("original PCM partial capture close failed");
    // A footer is not persistence certification. Only this non-replacing
    // publish after actual flush/close makes a complete destination visible.
    if(success && !c->failure && !MoveFileExA(c->partial.c_str(),c->destination.c_str(),0))
        c->fail("original PCM non-replacing publish failed; partial retained");
    return c->failure;
}
extern "C" const char *pilot_pcm_capture_destroy(PilotPcmCapture *c) {
    if(!c)return nullptr;
    if(pilot_audio_device_lle_consumer_attached(c))return c->fail("original PCM capture retained with registered device owner");
    delete c;return nullptr;
}
extern "C" const char *pilot_pcm_capture_error(const PilotPcmCapture *c){return c?c->failure:"missing original PCM capture";}
extern "C" void pilot_pcm_capture_view(const PilotPcmCapture *c,const uint8_t **bytes,uint64_t *count,const PilotPcmCaptureStamp **stamps,uint64_t *stamp_count) {
    if(bytes)*bytes=c?c->pcm:nullptr;if(count)*count=c?c->bytes:0;
    if(stamps)*stamps=c?c->stamps:nullptr;if(stamp_count)*stamp_count=c?c->count:0;
}
#ifdef PILOT_PCM_CAPTURE_TEST
extern "C" void pilot_pcm_capture_test_fail(PilotPcmCapture *c,unsigned phase){if(c){c->injected=phase;if(phase==1)c->fail("injected original PCM consumer failure");}}
#endif
