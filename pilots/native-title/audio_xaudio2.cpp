/* Independent native transport: XAudio2 mixes/resamples voices into 32 Xbox
 * bins; a whole GP graph produces six speaker channels. Backend selection is
 * a build option. There is no reference/native switching or duplicate run. */
/* Behavioral references for queue ownership/loop spans: Phobos xboxrecomp
 * bcccf8c, src/hle/audio_output_xaudio2.cpp and dsound_buffer_model.c, credited
 * upstream to NoRain211/doaxbv-re. No GPL source body is copied into this
 * independently implemented DSP transport. See THIRD_PARTY_ATTRIBUTION.md. */
/* Ordered send-to-source-channel routing was checked against xemu MCPX VP
 * 482088490c2d5983ed5e1eafbd09906157c1eca0 and original XDK setter behavior.
 * Behavioral reference only; implementation and real output matrix are local. */
#include <windows.h>
#include <xaudio2.h>
#include <xapo.h>
#include <ks.h>
#include <ksmedia.h>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include "audio_backend.h"
#include "audio_dsp.h"
#include "audio_capture_windows.hpp"

static void fail(const char *what,HRESULT hr=E_FAIL)
{fprintf(stderr,"[PILOT AUDIO STOP] %s: %08x\n",what,(unsigned)hr);fflush(stderr);ExitProcess(2);}
static bool float_format(const WAVEFORMATEX *f,unsigned channels)
{
    if(!f || f->nChannels!=channels || f->nSamplesPerSec!=48000 || f->wBitsPerSample!=32 ||
       f->nBlockAlign!=channels*4 || f->nAvgBytesPerSec!=48000*channels*4)return false;
    if(f->wFormatTag==WAVE_FORMAT_IEEE_FLOAT)return true;
    if(f->wFormatTag!=WAVE_FORMAT_EXTENSIBLE || f->cbSize<22)return false;
    const auto *e=reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(f);
    return e->Samples.wValidBitsPerSample==32 && e->SubFormat==KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
}
struct CaptureStamp {uint64_t frame,time;};
static uint64_t capture_clock(void)
{
    LARGE_INTEGER q,f;QueryPerformanceCounter(&q);QueryPerformanceFrequency(&f);
    return (uint64_t)(q.QuadPart/f.QuadPart)*10000000ull+
        (uint64_t)(q.QuadPart%f.QuadPart)*10000000ull/(uint64_t)f.QuadPart;
}
class Effects final:public IXAPO {
    std::atomic<ULONG> refs{1};
    bool locked=false;
    UINT32 max_frames=0,offset=0;
    uint32_t bins[1024]{},speakers[192]{};
public:
    PilotDsp *graph=nullptr; // Changed only while XAudio2's engine is stopped.
    std::atomic<unsigned> headroom[32]{};
    HANDLE capture_done=nullptr;
    float *capture=nullptr;
    UINT32 capture_capacity=0,capture_frames=0;
    bool capture_output=false;
    CaptureStamp *capture_stamps=nullptr;
    UINT32 capture_stamp_count=0,capture_stamp_capacity=0,capture_next_stamp=0;
    ~Effects(){if(capture_done)CloseHandle(capture_done);}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void **out) override {
        if(!out)return E_POINTER;*out=nullptr;
        if(id==__uuidof(IUnknown) || id==__uuidof(IXAPO)){*out=static_cast<IXAPO*>(this);AddRef();return S_OK;}
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {return ++refs;}
    ULONG STDMETHODCALLTYPE Release() override {ULONG n=--refs;if(!n)delete this;return n;}
    HRESULT STDMETHODCALLTYPE GetRegistrationProperties(XAPO_REGISTRATION_PROPERTIES **out) override {
        if(!out)return E_POINTER;
        auto *p=static_cast<XAPO_REGISTRATION_PROPERTIES*>(CoTaskMemAlloc(sizeof(XAPO_REGISTRATION_PROPERTIES)));
        if(!p)return E_OUTOFMEMORY;
        *p={};p->clsid={0x9b9c03be,0x8af8,0x49f4,{0x94,0xc1,0x54,0xbc,0x75,0xde,0xc8,0x42}};
        wcscpy_s(p->FriendlyName,L"Xbox GP effects");wcscpy_s(p->CopyrightInfo,L"xboxlle; MIT DSP donor");
        p->MajorVersion=1;p->Flags=XAPO_FLAG_FRAMERATE_MUST_MATCH|XAPO_FLAG_BITSPERSAMPLE_MUST_MATCH|XAPO_FLAG_BUFFERCOUNT_MUST_MATCH;
        p->MinInputBufferCount=p->MaxInputBufferCount=p->MinOutputBufferCount=p->MaxOutputBufferCount=1;
        *out=p;return S_OK;
    }
    HRESULT STDMETHODCALLTYPE IsInputFormatSupported(const WAVEFORMATEX *out,const WAVEFORMATEX *in,WAVEFORMATEX **suggested) override {
        if(suggested)*suggested=nullptr;
        return float_format(in,32) && (!out || float_format(out,6))?S_OK:XAPO_E_FORMAT_UNSUPPORTED;
    }
    HRESULT STDMETHODCALLTYPE IsOutputFormatSupported(const WAVEFORMATEX *in,const WAVEFORMATEX *out,WAVEFORMATEX **suggested) override {
        if(suggested)*suggested=nullptr;
        return float_format(out,6) && (!in || float_format(in,32))?S_OK:XAPO_E_FORMAT_UNSUPPORTED;
    }
    HRESULT STDMETHODCALLTYPE Initialize(const void *,UINT32 bytes) override {return bytes?E_INVALIDARG:S_OK;}
    void STDMETHODCALLTYPE Reset() override {
        // Only carry storage belongs to the XAPO. Installed graph state belongs
        // to DirectSound and survives voice-chain transport reset.
        offset=0;memset(bins,0,sizeof(bins));memset(speakers,0,sizeof(speakers));
    }
    HRESULT STDMETHODCALLTYPE LockForProcess(UINT32 ni,const XAPO_LOCKFORPROCESS_PARAMETERS *in,
                                              UINT32 no,const XAPO_LOCKFORPROCESS_PARAMETERS *out) override {
        if(locked || ni!=1 || no!=1 || !in || !out || !float_format(in->pFormat,32) ||
           !float_format(out->pFormat,6) || !in->MaxFrameCount || in->MaxFrameCount!=out->MaxFrameCount)return E_INVALIDARG;
        max_frames=in->MaxFrameCount;locked=true;Reset();return S_OK;
    }
    void STDMETHODCALLTYPE UnlockForProcess() override {locked=false;}
    void STDMETHODCALLTYPE Process(UINT32 ni,const XAPO_PROCESS_BUFFER_PARAMETERS *in,UINT32 no,
                                   XAPO_PROCESS_BUFFER_PARAMETERS *out,BOOL enabled) override {
        if(!locked || ni!=1 || no!=1 || !in || !out || in->ValidFrameCount>max_frames ||
           out->ValidFrameCount!=in->ValidFrameCount)fail("XAPO processing shape");
        const auto *src=static_cast<const float*>(in->pBuffer);auto *dst=static_cast<float*>(out->pBuffer);
        if(capture_stamps && capture_frames<capture_capacity && capture_frames>=capture_next_stamp) {
            if(capture_stamp_count==capture_stamp_capacity)fail("audio capture clock capacity");
            capture_stamps[capture_stamp_count++]={capture_frames,capture_clock()};
            capture_next_stamp=capture_frames+4800; /* ~10 Hz, no I/O in Process. */
        }
        float gains[32];for(unsigned c=0;c<32;++c)
            gains[c]=1.0f/(1u<<headroom[c].load(std::memory_order_relaxed));
        bool nonzero=false;
        for(UINT32 frame=0;frame<in->ValidFrameCount;++frame) {
            // Carry imposes a documented 32/48000-second transport latency and
            // handles arbitrary XAudio2 quantum sizes without dropping frames.
            for(unsigned c=0;c<6;++c) {
                int32_t sample=(int32_t)(speakers[c*32+offset]<<8)>>8;
                dst[frame*6+c]=sample*(1.0f/8388608.0f);nonzero|=sample!=0;
            }
            for(unsigned c=0;c<32;++c) {
                float sample=in->BufferFlags==XAPO_BUFFER_SILENT?0.0f:src[frame*32+c];
                if(!std::isfinite(sample))fail("nonfinite mixed sample");
                sample*=gains[c];
                double q=sample*8388608.0;
                bins[c*32+offset]=(uint32_t)(int32_t)(q>8388607?8388607:q< -8388608?-8388608:q)&0xffffff;
            }
            if(capture && capture_frames<capture_capacity) {
                unsigned channels=capture_output?6:32;
                float *destination=capture+(size_t)capture_frames++*channels;
                if(capture_output)memcpy(destination,dst+frame*6,channels*sizeof(float));
                else if(in->BufferFlags==XAPO_BUFFER_SILENT)memset(destination,0,channels*sizeof(float));
                else for(unsigned c=0;c<32;++c)
                    destination[c]=src[frame*32+c]*gains[c];
                if(capture_frames==capture_capacity && capture_done && !SetEvent(capture_done))
                    fail("audio capture completion event",HRESULT_FROM_WIN32(GetLastError()));
            }
            if(++offset==32) {
                if(graph && enabled)pilot_dsp_process(graph,bins,speakers);
                else memcpy(speakers,bins,sizeof(speakers));
                offset=0;
            }
        }
        out->BufferFlags=nonzero?XAPO_BUFFER_VALID:XAPO_BUFFER_SILENT;
    }
    UINT32 STDMETHODCALLTYPE CalcInputFrames(UINT32 n) override {return n;}
    UINT32 STDMETHODCALLTYPE CalcOutputFrames(UINT32 n) override {return n;}
};
static IXAudio2 *engine;
static IXAudio2MasteringVoice *master;
static IXAudio2SubmixVoice *mixer;
static Effects *effects;
static CO_MTA_USAGE_COOKIE mta;
static HANDLE wake,feeder;
static std::atomic<bool> exiting{false};
static SRWLOCK lock=SRWLOCK_INIT;
struct EngineEvents final:public IXAudio2EngineCallback {
    void STDMETHODCALLTYPE OnProcessingPassStart() override {}
    void STDMETHODCALLTYPE OnProcessingPassEnd() override {}
    void STDMETHODCALLTYPE OnCriticalError(HRESULT hr) override {fail("native audio device failed",hr);}
};
static EngineEvents engine_events;
struct Guard {Guard(){AcquireSRWLockExclusive(&lock);}~Guard(){ReleaseSRWLockExclusive(&lock);}};
struct Chunk {std::atomic<bool> busy{false};BYTE data[2048];};
struct Buffer final:public IXAudio2VoiceCallback {
    IXAudio2SourceVoice *voice=nullptr;
    BYTE *data=nullptr;
    UINT32 bytes=0,align=0,channels=0,rate=0,frequency=0,origin=0,next=0;
    UINT32 loop_start=0,loop_end=0;
    UINT64 baseline=0;
    UINT64 capture_id=0;
    bool playing=false,paused=false,looping=false,submitted_end=false;
    PilotAudioNotification notifications[64]{};
    unsigned notification_count=0;
    UINT64 notification_bytes=0;
    std::atomic<HRESULT> error{S_OK};
    Chunk chunks[32];
    void STDMETHODCALLTYPE OnVoiceProcessingPassStart(UINT32) override {}
    void STDMETHODCALLTYPE OnVoiceProcessingPassEnd() override {}
    void STDMETHODCALLTYPE OnStreamEnd() override {SetEvent(wake);}
    void STDMETHODCALLTYPE OnBufferStart(void*) override {}
    void STDMETHODCALLTYPE OnBufferEnd(void *context) override {static_cast<Chunk*>(context)->busy.store(false);SetEvent(wake);}
    void STDMETHODCALLTYPE OnLoopEnd(void*) override {}
    void STDMETHODCALLTYPE OnVoiceError(void*,HRESULT hr) override {error=hr;SetEvent(wake);}
};
static Buffer *buffers[256];
#include "audio_xaudio2_notifications.hpp"
static LONG64 audio_capture_next_id;
static void diagnostic_capture_start(void);
static void diagnostic_capture_close(void);
static UINT32 position(Buffer *b,XAUDIO2_VOICE_STATE *state=nullptr)
{
    XAUDIO2_VOICE_STATE s;b->voice->GetState(&s);
    UINT64 cursor=(UINT64)b->origin+(s.SamplesPlayed-b->baseline)*b->align;
    if(state)*state=s;
    if(!b->looping)return cursor>=b->bytes?0:(UINT32)cursor;
    UINT32 end=b->origin<b->loop_end?b->loop_end:b->bytes;
    if(cursor<end)return (UINT32)cursor;
    return b->loop_start+(UINT32)((cursor-end)%(b->loop_end-b->loop_start));
}
static bool audio_stream_profile(void)
{
    static const bool enabled=[](){const char *p=getenv("XLLE_PILOT_AUDIO_STREAM_PROFILE");return p && !strcmp(p,"1");}();
    return enabled;
}
/* Bounded observation under the existing owner lock; no trace-file I/O while
 * either the guest producer or the native feeder owns audio. The PCM capture
 * worker detaches this array once, then writes it outside the owner lock. */
struct AudioTimelineRecord {
    uint64_t time,buffer,samples,baseline,cpu;
    const char *operation;
    uint32_t offset,bytes,play,write,queued,playing,capacity,rate,thread;
};
static AudioTimelineRecord *audio_timeline;
static constexpr unsigned audio_timeline_capacity=100000;
static unsigned audio_timeline_count,audio_timeline_overflow;
static FILE *audio_timeline_file;
/* Optional bounded, exact source bytes. One tap observes caller writes; the
 * other observes bytes successfully queued to XAudio2. No file I/O runs under
 * the audio owner lock, and no second decoder/mixer execution is involved. */
struct AudioIngressHeader {
    uint32_t magic,version,kind,header_bytes;
    uint64_t time,id,buffer;
    uint32_t rate,channels,bits,capacity,offset,bytes;
    uint32_t loop_start,loop_end,looping,playing;
};
static_assert(sizeof(AudioIngressHeader)==80,"source capture v2 header layout");
static BYTE *audio_ingress;
static constexpr uint32_t audio_ingress_capacity=128*1024*1024;
static uint32_t audio_ingress_size,audio_ingress_overflow;
static HANDLE audio_ingress_file;
static void audio_ingress_record(Buffer *b,uint32_t kind,uint32_t offset,const void *data,uint32_t bytes)
{
    if(!audio_ingress)return;
    if(audio_ingress_size>audio_ingress_capacity-sizeof(AudioIngressHeader) ||
        bytes>audio_ingress_capacity-audio_ingress_size-sizeof(AudioIngressHeader)) {++audio_ingress_overflow;return;}
    /* Every record carries actual controls from this same locked owner. Caller
     * writes wrap at capacity; successfully queued PCM wraps at loop_end/start
     * only when looping. Kind4 is a control observation; kind5 retires the ID. */
    AudioIngressHeader h{0x50414c58,2,kind,sizeof(AudioIngressHeader),capture_clock(),b->capture_id,
        (uint64_t)(uintptr_t)b,b->frequency,b->channels,b->align/b->channels*8,b->bytes,offset,bytes,
        b->loop_start,b->loop_end,b->looping?1u:0u,b->playing?1u:0u};
    memcpy(audio_ingress+audio_ingress_size,&h,sizeof(h));audio_ingress_size+=sizeof(h);
    if(bytes)memcpy(audio_ingress+audio_ingress_size,data,bytes);audio_ingress_size+=bytes;
}
static void audio_stream_record(Buffer *b,const char *operation,UINT32 offset=0,UINT32 bytes=0)
{
    if(!audio_timeline && !audio_stream_profile())return;
    XAUDIO2_VOICE_STATE state;UINT32 play=position(b,&state);
    FILETIME created,exited,kernel,user;
    if(!GetThreadTimes(GetCurrentThread(),&created,&exited,&kernel,&user))
        fail("audio diagnostic thread times",HRESULT_FROM_WIN32(GetLastError()));
    UINT64 cpu=((UINT64)kernel.dwHighDateTime<<32)+kernel.dwLowDateTime+
        ((UINT64)user.dwHighDateTime<<32)+user.dwLowDateTime;
    uint64_t time=capture_clock();DWORD thread=GetCurrentThreadId();
    if(audio_timeline) {
        if(audio_timeline_count<audio_timeline_capacity)
            audio_timeline[audio_timeline_count++]={time,(uint64_t)(uintptr_t)b,state.SamplesPlayed,b->baseline,cpu,
                operation,offset,bytes,play,b->next,state.BuffersQueued,b->playing,b->bytes,b->frequency,thread};
        else ++audio_timeline_overflow;
    }
    if(!audio_stream_profile())return;
    // Synchronous text is for targeted diagnosis, not timing qualification.
    fprintf(stderr,"[AUDIO STREAM] time=%llu buffer=%p operation=%s offset=%u bytes=%u play=%u write=%u samples=%llu baseline=%llu queued=%u playing=%u capacity=%u rate=%u thread=%lu cpu=%llu\n",
        (unsigned long long)time,(void*)b,operation,offset,bytes,play,b->next,
        (unsigned long long)state.SamplesPlayed,(unsigned long long)b->baseline,state.BuffersQueued,b->playing,b->bytes,b->frequency,
        thread,(unsigned long long)cpu);
}
static HRESULT fill(Buffer *b)
{
    if(FAILED(b->error))return b->error;
    if(!b->playing)return S_OK;
    notifications_progress(b);
    if(b->paused || b->submitted_end)return S_OK;
    XAUDIO2_VOICE_STATE s;b->voice->GetState(&s,XAUDIO2_VOICE_NOSAMPLESPLAYED);
    if(!s.BuffersQueued)audio_stream_record(b,"empty");
    UINT32 frames=b->frequency/100;if(!frames)frames=1;if(frames>512)frames=512;
    // Four nominal 10-ms chunks, more at high playback rates. Every queued
    // chunk owns its bytes until XAudio2's real completion callback releases it.
    UINT32 target=(b->frequency/25+frames-1)/frames;
    if(target<4)target=4;if(target>32)target=32;
    for(auto &chunk:b->chunks) {
        if(s.BuffersQueued>=target || b->submitted_end)break;
        bool free=false;if(!chunk.busy.compare_exchange_strong(free,true))continue;
        UINT32 n=frames*b->align;if(!b->looping && n>b->bytes-b->next)n=b->bytes-b->next;
        UINT32 copied=0,cursor=b->next;
        while(copied<n) {
            UINT32 end=b->looping && cursor<b->loop_end?b->loop_end:b->bytes;
            UINT32 part=n-copied;if(part>end-cursor)part=end-cursor;
            memcpy(chunk.data+copied,b->data+cursor,part);copied+=part;cursor+=part;
            if(cursor==end)cursor=b->looping?b->loop_start:0;
        }
        XAUDIO2_BUFFER submit{};submit.AudioBytes=n;submit.pAudioData=chunk.data;submit.pContext=&chunk;
        HRESULT hr=b->voice->SubmitSourceBuffer(&submit);
        if(FAILED(hr)){chunk.busy=false;return hr;}
        audio_ingress_record(b,2,b->next,chunk.data,n);
        ++s.BuffersQueued;b->next=cursor;
        if(!b->looping && !cursor)b->submitted_end=true;
    }
    return S_OK;
}
static DWORD WINAPI feed(void*)
{
    while(!exiting.load()) {
        DWORD wait=WaitForSingleObject(wake,20);
        if(wait!=WAIT_OBJECT_0 && wait!=WAIT_TIMEOUT)fail("audio feeder event",HRESULT_FROM_WIN32(GetLastError()));
        Guard guard;
        for(auto *b:buffers)if(b && b->playing){HRESULT hr=fill(b);if(FAILED(hr))fail("audio source queue",hr);}
    }
    return 0;
}
int32_t pilot_audio_open(void)
{
    if(engine)return S_OK;
    HRESULT hr=CoIncrementMTAUsage(&mta);
    if(SUCCEEDED(hr))hr=XAudio2Create(&engine,0,XAUDIO2_DEFAULT_PROCESSOR);
    if(FAILED(hr))fprintf(stderr,"[AUDIO INIT] XAudio2Create=%08x\n",(unsigned)hr);
    if(SUCCEEDED(hr))hr=engine->RegisterForCallbacks(&engine_events);
    if(SUCCEEDED(hr))hr=engine->CreateMasteringVoice(&master,2,48000);
    char output_mute[2]{};
    if(SUCCEEDED(hr) && GetEnvironmentVariableA("XLLE_PILOT_AUDIO_OUTPUT_MUTE",output_mute,sizeof(output_mute))==1 && output_mute[0]=='1') {
        hr=master->SetVolume(0.0f);
        if(SUCCEEDED(hr))fprintf(stderr,"[AUDIO OUTPUT] mastering voice muted; source/mixer/DSP remain active\n");
    }
    if(FAILED(hr))fprintf(stderr,"[AUDIO INIT] mastering=%08x\n",(unsigned)hr);
    if(SUCCEEDED(hr)) {
        effects=new(std::nothrow) Effects();if(!effects)hr=E_OUTOFMEMORY;
        else {
            XAUDIO2_EFFECT_DESCRIPTOR effect{effects,TRUE,6};XAUDIO2_EFFECT_CHAIN chain{1,&effect};
            hr=engine->CreateSubmixVoice(&mixer,32,48000,0,0,nullptr,&chain);
            if(FAILED(hr))fprintf(stderr,"[AUDIO INIT] mix/effects=%08x\n",(unsigned)hr);
        }
    }
    if(SUCCEEDED(hr)){wake=CreateEventW(nullptr,FALSE,FALSE,nullptr);if(!wake)hr=HRESULT_FROM_WIN32(GetLastError());}
    if(SUCCEEDED(hr)){exiting=false;feeder=CreateThread(nullptr,0,feed,nullptr,0,nullptr);if(!feeder)hr=HRESULT_FROM_WIN32(GetLastError());}
    if(FAILED(hr))pilot_audio_close();
    else diagnostic_capture_start();
    return hr;
}
void pilot_audio_close(void)
{
    diagnostic_capture_close();
    exiting=true;if(wake)SetEvent(wake);
    if(feeder){WaitForSingleObject(feeder,INFINITE);CloseHandle(feeder);feeder=nullptr;}
    for(auto *b:buffers)if(b)fail("closing device with owned voices");
    if(engine)engine->StopEngine();
    if(mixer){mixer->DestroyVoice();mixer=nullptr;}
    if(master){master->DestroyVoice();master=nullptr;}
    if(effects){effects->Release();effects=nullptr;}
    if(engine){engine->UnregisterForCallbacks(&engine_events);engine->Release();engine=nullptr;}
    if(wake){CloseHandle(wake);wake=nullptr;}
    if(mta){CoDecrementMTAUsage(mta);mta=nullptr;}
}
int32_t pilot_audio_effects_install(PilotDsp *graph)
{
    if(!engine || !effects || !graph)return E_INVALIDARG;
    // StopEngine completes its current quantum before returning; Process takes
    // no guest/owner lock. Installation replaces the graph only at this boundary.
    Guard guard;engine->StopEngine();PilotDsp *old=effects->graph;effects->graph=graph;effects->Reset();
    HRESULT hr=engine->StartEngine();if(FAILED(hr))effects->graph=old;
    return hr;
}
int32_t pilot_audio_effects_patch(PilotDsp *graph,unsigned effect,unsigned count,const PilotDspPatch *patches)
{
    Guard guard;
    if(!engine || !effects || !graph || effects->graph!=graph)return E_INVALIDARG;
    // Complete the current quantum; change the complete batch between Process
    // calls without resetting the graph, queued source data or effect history.
    engine->StopEngine();
    int accepted=pilot_dsp_effect_patch(graph,effect,count,patches);
    HRESULT hr=engine->StartEngine();
    return FAILED(hr)?hr:accepted?S_OK:E_INVALIDARG;
}
static int32_t audio_capture_begin(float *samples,uint32_t frames,int after_effects,
                                  CaptureStamp *stamps,UINT32 stamp_capacity)
{
    if(!engine || !effects || !samples || !frames || effects->capture)return E_INVALIDARG;
    Guard guard;
    if(!effects->capture_done)effects->capture_done=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    if(!effects->capture_done || !ResetEvent(effects->capture_done))return HRESULT_FROM_WIN32(GetLastError());
    engine->StopEngine();effects->capture=samples;effects->capture_capacity=frames;
    effects->capture_frames=0;effects->capture_output=!!after_effects;
    effects->capture_stamps=stamps;effects->capture_stamp_capacity=stamp_capacity;
    effects->capture_stamp_count=effects->capture_next_stamp=0;
    HRESULT hr=engine->StartEngine();if(FAILED(hr)){effects->capture=nullptr;effects->capture_stamps=nullptr;}return hr;
}
int32_t pilot_audio_capture_begin(float *samples,uint32_t frames,int after_effects)
{return audio_capture_begin(samples,frames,after_effects,nullptr,0);}
int32_t pilot_audio_capture_wait(uint32_t milliseconds)
{
    HANDLE reference=nullptr;
    {
        Guard guard;
        if(!effects || !effects->capture || !effects->capture_done)return E_INVALIDARG;
        if(!DuplicateHandle(GetCurrentProcess(),effects->capture_done,GetCurrentProcess(),&reference,
                            SYNCHRONIZE,FALSE,0))return HRESULT_FROM_WIN32(GetLastError());
    }
    // Never hold the feeder's lock while waiting for real device processing.
    DWORD result=WaitForSingleObject(reference,milliseconds),error=GetLastError();
    CloseHandle(reference);
    return result==WAIT_OBJECT_0?1:result==WAIT_TIMEOUT?0:HRESULT_FROM_WIN32(error);
}
uint32_t pilot_audio_capture_end(void)
{
    if(!engine || !effects)return 0;
    Guard guard;engine->StopEngine();UINT32 frames=effects->capture_frames;effects->capture=nullptr;effects->capture_stamps=nullptr;
    HRESULT hr=engine->StartEngine();if(FAILED(hr))fail("audio capture restart",hr);return frames;
}
/* Opt-in title diagnostic through the same sample tap as the audio fixtures.
 * Capture a bounded span of actual six-channel GP output, float32 at 48 kHz.
 * A separate writer keeps file I/O off both the mixer and its voice feeder.
 * Captures are instrumentation, never cadence/performance measurements. */
static HANDLE diagnostic_thread,diagnostic_stop,diagnostic_file;
static float *diagnostic_samples;
static CaptureStamp *diagnostic_stamps;
static FILE *diagnostic_clock_file;
static unsigned diagnostic_seconds=30;
static AudioCaptureWindows diagnostic_windows;
static uint64_t diagnostic_origin;
static unsigned diagnostic_window;
static void diagnostic_capture_write(void)
{
    UINT32 frames=pilot_audio_capture_end();DWORD written=0,bytes=frames*6*sizeof(float);
    AudioTimelineRecord *timeline=nullptr;unsigned records=0,overflow=0;
    BYTE *ingress=nullptr;uint32_t ingress_size=0,ingress_overflow=0;
    {
        Guard guard;timeline=audio_timeline;audio_timeline=nullptr;
        records=audio_timeline_count;overflow=audio_timeline_overflow;
        ingress=audio_ingress;audio_ingress=nullptr;
        ingress_size=audio_ingress_size;ingress_overflow=audio_ingress_overflow;
    }
    if(ingress) {
        DWORD stored;
        if(!WriteFile(audio_ingress_file,ingress,ingress_size,&stored,nullptr) || stored!=ingress_size ||
            !CloseHandle(audio_ingress_file))fail("audio source capture write",HRESULT_FROM_WIN32(GetLastError()));
        audio_ingress_file=nullptr;delete[] ingress;
        fprintf(stderr,"[AUDIO INGRESS] bytes=%u overflow=%u window=%u\n",ingress_size,ingress_overflow,diagnostic_window);
    }
    if(timeline) {
        for(unsigned i=0;i<records;++i) {
            const auto &r=timeline[i];
            if(fprintf(audio_timeline_file,"%llu,%llu,%s,%u,%u,%u,%u,%llu,%llu,%u,%u,%u,%u,%u,%llu\n",
                (unsigned long long)r.time,(unsigned long long)r.buffer,r.operation,r.offset,r.bytes,r.play,r.write,
                (unsigned long long)r.samples,(unsigned long long)r.baseline,r.queued,r.playing,r.capacity,r.rate,r.thread,
                (unsigned long long)r.cpu)<0)fail("audio timeline write");
        }
        if(fclose(audio_timeline_file))fail("audio timeline close");
        audio_timeline_file=nullptr;delete[] timeline;
        fprintf(stderr,"[AUDIO TIMELINE] records=%u overflow=%u window=%u\n",records,overflow,diagnostic_window);
    }
    if(!WriteFile(diagnostic_file,diagnostic_samples,bytes,&written,nullptr) || written!=bytes ||
       !CloseHandle(diagnostic_file))fail("audio capture write",HRESULT_FROM_WIN32(GetLastError()));
    diagnostic_file=nullptr;delete[] diagnostic_samples;diagnostic_samples=nullptr;
    for(UINT32 i=0;i<effects->capture_stamp_count;++i)
        if(fprintf(diagnostic_clock_file,"%llu,%llu\n",(unsigned long long)diagnostic_stamps[i].frame,
            (unsigned long long)diagnostic_stamps[i].time)<0)fail("audio capture clock write");
    if(fclose(diagnostic_clock_file))fail("audio capture clock close");
    diagnostic_clock_file=nullptr;delete[] diagnostic_stamps;diagnostic_stamps=nullptr;
    fprintf(stderr,"[AUDIO CAPTURE] complete frames=%u rate=48000 channels=6 format=float32 bytes=%lu window=%u\n",frames,bytes,diagnostic_window);
}
static bool diagnostic_capture_path(const char *variable,char *path,size_t size)
{
    const char *base=getenv(variable);
    if(!base || !*base)return false;
    int bytes=diagnostic_windows.explicit_windows?snprintf(path,size,"%s.window%u",base,diagnostic_window):snprintf(path,size,"%s",base);
    if(bytes<0 || (size_t)bytes>=size)fail("audio capture path");
    return true;
}
static void diagnostic_capture_arm(void)
{
    char path[2048];
    if(!diagnostic_capture_path("XLLE_PILOT_AUDIO_CAPTURE",path,sizeof(path)))fail("audio capture path removed");
    diagnostic_file=CreateFileA(path,GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(diagnostic_file==INVALID_HANDLE_VALUE)fail("audio capture create",HRESULT_FROM_WIN32(GetLastError()));
    char clock_path[2048];
    if(snprintf(clock_path,sizeof(clock_path),"%s.clock.csv",path)>=(int)sizeof(clock_path))fail("audio capture clock path");
    diagnostic_clock_file=fopen(clock_path,"wx");if(!diagnostic_clock_file)fail("audio capture clock create");
    if(fprintf(diagnostic_clock_file,"frame,time_100ns\n")<0)fail("audio capture clock header");
    diagnostic_samples=new(std::nothrow) float[48000*diagnostic_seconds*6]{};
    if(!diagnostic_samples)fail("audio capture allocation",E_OUTOFMEMORY);
    UINT32 stamp_capacity=diagnostic_seconds*10+2;
    diagnostic_stamps=new(std::nothrow) CaptureStamp[stamp_capacity];
    if(!diagnostic_stamps)fail("audio capture clock allocation",E_OUTOFMEMORY);
    BYTE *ingress=nullptr;AudioTimelineRecord *timeline=nullptr;
    char ingress_path[2048],timeline_path[2048];
    if(diagnostic_capture_path("XLLE_PILOT_AUDIO_INGRESS",ingress_path,sizeof(ingress_path))) {
        audio_ingress_file=CreateFileA(ingress_path,GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(audio_ingress_file==INVALID_HANDLE_VALUE)fail("audio source capture create",HRESULT_FROM_WIN32(GetLastError()));
        ingress=new(std::nothrow) BYTE[audio_ingress_capacity];
        if(!ingress)fail("audio source capture allocation",E_OUTOFMEMORY);
    }
    if(diagnostic_capture_path("XLLE_PILOT_AUDIO_TIMELINE",timeline_path,sizeof(timeline_path))) {
        audio_timeline_file=fopen(timeline_path,"wx");if(!audio_timeline_file)fail("audio timeline create");
        if(fprintf(audio_timeline_file,"time,buffer,operation,offset,bytes,play,write,samples,baseline,queued,playing,capacity,rate,thread,cpu\n")<0)
            fail("audio timeline header");
        timeline=new(std::nothrow) AudioTimelineRecord[audio_timeline_capacity];
        if(!timeline)fail("audio timeline allocation",E_OUTOFMEMORY);
    }
    {
        Guard guard;audio_ingress_size=audio_ingress_overflow=0;audio_ingress=ingress;
        audio_timeline_count=audio_timeline_overflow=0;audio_timeline=timeline;
        /* A late observation needs the actual current caller-owned ring, not
         * an assumed zero initializer. Preserve capture IDs and playback. */
        for(auto *b:buffers)if(b) {
            audio_ingress_record(b,3,0,b->data,b->bytes);
            audio_stream_record(b,"window-snapshot");
        }
    }
    HRESULT hr=audio_capture_begin(diagnostic_samples,48000*diagnostic_seconds,1,diagnostic_stamps,stamp_capacity);
    if(FAILED(hr))fail("audio capture begin",hr);
    fprintf(stderr,"[AUDIO CAPTURE] armed seconds=%u real GP output tap; clock is mixer processing time, not speaker latency window=%u time=%llu\n",diagnostic_seconds,diagnostic_window,(unsigned long long)capture_clock());
}
static bool diagnostic_capture_wait(uint64_t deadline)
{
    for(;;) {
        DWORD wait=WaitForSingleObject(diagnostic_stop,audio_capture_remaining_ms(capture_clock(),deadline));
        if(wait==WAIT_OBJECT_0)return false;
        if(wait!=WAIT_TIMEOUT)fail("audio capture wait");
        if(capture_clock()>=deadline)return true;
    }
}
static DWORD WINAPI diagnostic_capture_worker(void*)
{
    for(unsigned i=0;i<diagnostic_windows.count;++i) {
        const auto &window=diagnostic_windows.items[i];
        uint64_t begin=diagnostic_origin+(uint64_t)window.start*10000000;
        uint64_t end=begin+(uint64_t)window.seconds*10000000;
        if(i && capture_clock()>=begin)fail("audio capture missed next window");
        if(!diagnostic_capture_wait(begin))break;
        diagnostic_window=i+1;diagnostic_seconds=window.seconds;
        diagnostic_capture_arm();
        if(capture_clock()>=end)fail("audio capture window missed during allocation");
        bool complete=diagnostic_capture_wait(end);
        diagnostic_capture_write();
        fprintf(stderr,"[AUDIO WINDOW] index=%u scheduled_begin=%llu scheduled_end=%llu complete=%u time=%llu\n",diagnostic_window,
            (unsigned long long)begin,(unsigned long long)end,complete?1:0,(unsigned long long)capture_clock());
        if(!complete)break;
    }
    return 0;
}
static void diagnostic_capture_start(void)
{
    static bool used=false;
    const char *path=getenv("XLLE_PILOT_AUDIO_CAPTURE");
    if(used || !path || !*path)return;
    used=true;
    const char *duration=getenv("XLLE_PILOT_AUDIO_CAPTURE_SECONDS");
    if(duration) {
        char *end=nullptr;unsigned long seconds=strtoul(duration,&end,10);
        if(!*duration || *end || seconds<1 || seconds>300)fail("audio capture duration outside 1..300 seconds");
        diagnostic_seconds=(unsigned)seconds;
    }
    if(!audio_capture_windows_parse(getenv("XLLE_PILOT_AUDIO_CAPTURE_WINDOWS"),diagnostic_seconds,diagnostic_windows))
        fail("audio capture windows outside bounded ordered contract");
    diagnostic_origin=capture_clock();
    diagnostic_stop=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    if(!diagnostic_stop)fail("audio capture stop event",HRESULT_FROM_WIN32(GetLastError()));
    diagnostic_thread=CreateThread(nullptr,0,diagnostic_capture_worker,nullptr,0,nullptr);
    if(!diagnostic_thread)fail("audio capture worker",HRESULT_FROM_WIN32(GetLastError()));
}
static void diagnostic_capture_close(void)
{
    if(!diagnostic_thread)return;
    if(!SetEvent(diagnostic_stop) || WaitForSingleObject(diagnostic_thread,INFINITE)!=WAIT_OBJECT_0)
        fail("audio capture shutdown");
    CloseHandle(diagnostic_thread);CloseHandle(diagnostic_stop);
    diagnostic_thread=diagnostic_stop=nullptr;
}
int32_t pilot_audio_mixbin_headroom(unsigned bin,uint32_t amount)
{
    if(bin>=32)return E_INVALIDARG;
    Guard guard;if(!effects)return E_INVALIDARG;
    // Original setter stores a BYTE; its hardware method consumes bits 0..2.
    // Apply before the existing GP input quantization/clipping, including voices
    // created later. This device control is independent of per-voice headroom.
    effects->headroom[bin].store(amount&7,std::memory_order_relaxed);return S_OK;
}
int32_t pilot_audio_buffer_mixbins(void *opaque,unsigned count,const uint32_t *bins,const int32_t *volumes)
{
    auto *b=static_cast<Buffer*>(opaque);
    if(!b || count>8 || (count && (!bins || !volumes)))return E_INVALIDARG;
    float matrix[64]{};
    for(unsigned slot=0;slot<count;++slot) {
        if(bins[slot]>=32 || volumes[slot]>0 || volumes[slot]<-10000)return E_INVALIDARG;
        // MCPX routes successive mix-bin slots from successive source channels.
        // Repeated destination bins accumulate, rather than overwriting a send.
        matrix[bins[slot]*b->channels+slot%b->channels]+=volumes[slot]==-10000?0.0f:powf(10.0f,volumes[slot]/2000.0f);
    }
    Guard guard;return b->voice->SetOutputMatrix(mixer,b->channels,32,matrix);
}
int32_t pilot_audio_buffer_create(uint32_t bytes,unsigned channels,unsigned rate,unsigned bits,void **out)
{
    if(!engine || !out || !bytes || (channels!=1 && channels!=2) || (bits!=8 && bits!=16) ||
       rate<100 || rate>200000 || bytes%(channels*(bits/8)))return E_INVALIDARG;
    Buffer *b=new(std::nothrow) Buffer();if(!b)return E_OUTOFMEMORY;
    b->capture_id=(UINT64)InterlockedIncrement64(&audio_capture_next_id);
    b->data=new(std::nothrow) BYTE[bytes];if(!b->data){delete b;return E_OUTOFMEMORY;}
    b->bytes=b->loop_end=bytes;b->channels=channels;b->align=channels*bits/8;b->rate=b->frequency=rate;
    memset(b->data,bits==8?128:0,bytes);
    // Declared source rate 48 kHz plus the native ratio covers Xbox 100..200000
    // Hz; SamplesPlayed still counts the original PCM frames, not output frames.
    WAVEFORMATEX fmt{WAVE_FORMAT_PCM,(WORD)channels,48000,48000*b->align,(WORD)b->align,(WORD)bits,0};
    XAUDIO2_SEND_DESCRIPTOR dest{0,mixer};XAUDIO2_VOICE_SENDS sends{1,&dest};
    HRESULT hr=engine->CreateSourceVoice(&b->voice,&fmt,0,200000.0f/48000,b,&sends);
    if(SUCCEEDED(hr))hr=b->voice->SetFrequencyRatio(rate/48000.0f);
    if(SUCCEEDED(hr)) {
        float matrix[64]{};
        if(channels==2){matrix[0]=1;matrix[3]=1;}
        else {matrix[0]=matrix[1]=1;}
        hr=b->voice->SetOutputMatrix(mixer,channels,32,matrix);
    }
    if(SUCCEEDED(hr)) {
        Guard guard;unsigned slot=0;while(slot<256 && buffers[slot])++slot;
        if(slot==256)hr=E_OUTOFMEMORY;else {
            /* Record the bytes actually initialized above before publishing
             * this lifetime to the feeder; late windows never infer zeros. */
            audio_ingress_record(b,3,0,b->data,b->bytes);buffers[slot]=b;
        }
    }
    if(FAILED(hr)){if(b->voice)b->voice->DestroyVoice();delete[] b->data;delete b;return hr;}
    *out=b;return S_OK;
}
void pilot_audio_buffer_destroy(void *opaque)
{
    auto *b=static_cast<Buffer*>(opaque);if(!b)return;
    {Guard guard;audio_stream_record(b,"destroy");audio_ingress_record(b,5,0,nullptr,0);for(auto &p:buffers)if(p==b)p=nullptr;
     notifications_progress(b);notifications_close(b);}
    b->voice->DestroyVoice();delete[] b->data;delete b;
}
int32_t pilot_audio_buffer_copy(void *opaque,uint32_t offset,void *data,uint32_t bytes,int readback)
{
    auto *b=static_cast<Buffer*>(opaque);
    if(!b || !data || !bytes || offset>=b->bytes || bytes>b->bytes)return E_INVALIDARG;
    Guard guard;UINT32 first=bytes;if(first>b->bytes-offset)first=b->bytes-offset;
    if(readback){memcpy(data,b->data+offset,first);memcpy((BYTE*)data+first,b->data,bytes-first);}
    else {memcpy(b->data+offset,data,first);memcpy(b->data,(BYTE*)data+first,bytes-first);
        audio_ingress_record(b,1,offset,data,bytes);}
    audio_stream_record(b,readback?"read":"write",offset,bytes);
    return b->error;
}
static HRESULT seek(Buffer *b,UINT32 at,bool play);
static XAUDIO2_VOICE_STATE refresh(Buffer *b)
{
    notifications_progress(b);
    XAUDIO2_VOICE_STATE s;b->voice->GetState(&s);
    if(b->playing && !b->paused && b->submitted_end && !s.BuffersQueued){b->playing=false;b->origin=b->next=0;b->baseline=s.SamplesPlayed;b->notification_bytes=0;b->submitted_end=false;audio_ingress_record(b,4,0,nullptr,0);}
    return s;
}
int32_t pilot_audio_buffer_play(void *opaque,int looping)
{
    auto *b=static_cast<Buffer*>(opaque);Guard guard;
    refresh(b);
    b->paused=false;
    if(b->playing && b->looping!=!!looping){UINT32 at=position(b);b->looping=!!looping;return seek(b,at,true);}
    b->looping=!!looping;b->playing=true;
    audio_ingress_record(b,4,0,nullptr,0);
    HRESULT hr=fill(b);if(SUCCEEDED(hr))hr=b->voice->Start();if(FAILED(hr)){b->playing=false;audio_ingress_record(b,4,0,nullptr,0);}
    audio_stream_record(b,"play");
    SetEvent(wake);return hr;
}
static HRESULT seek(Buffer *b,UINT32 at,bool play)
{
    notifications_progress(b);
    HRESULT hr=b->voice->Stop();if(SUCCEEDED(hr))hr=b->voice->FlushSourceBuffers();
    if(FAILED(hr))return hr;
    XAUDIO2_VOICE_STATE s;b->voice->GetState(&s);
    b->origin=b->next=at;b->baseline=s.SamplesPlayed;b->notification_bytes=0;b->submitted_end=false;b->playing=play;
    if(!play)b->paused=false;
    audio_ingress_record(b,4,0,nullptr,0);
    if(play && !b->paused){hr=fill(b);if(SUCCEEDED(hr))hr=b->voice->Start();SetEvent(wake);}
    audio_stream_record(b,play?"seek-playing":"seek-stopped",at);
    return hr;
}
int32_t pilot_audio_buffer_stop(void *opaque)
{auto *b=static_cast<Buffer*>(opaque);Guard guard;return seek(b,position(b),false);}
int32_t pilot_audio_buffer_pause(void *opaque,uint32_t mode)
{
    auto *b=static_cast<Buffer*>(opaque);
    if(!b)return E_INVALIDARG;if(mode>1)return E_NOTIMPL;
    Guard guard;refresh(b);if(FAILED(b->error))return b->error;
    if(!b->playing)return (int32_t)mode;
    if(b->paused==!!mode)return S_OK;
    HRESULT hr;
    if(mode) {
        // Stop preserves XAudio2's queued PCM and real SamplesPlayed epoch.
        // Flush/seek would discard already-owned streaming data and callbacks.
        hr=b->voice->Stop();if(SUCCEEDED(hr))b->paused=true;
    } else {
        b->paused=false;hr=fill(b);if(SUCCEEDED(hr))hr=b->voice->Start();
        if(FAILED(hr))b->paused=true;else SetEvent(wake);
    }
    notifications_progress(b);audio_stream_record(b,mode?"pause":"resume");
    return hr;
}
int32_t pilot_audio_buffer_status(void *opaque,uint32_t *status)
{
    auto *b=static_cast<Buffer*>(opaque);if(!status)return E_POINTER;Guard guard;
    refresh(b);
    *status=b->playing?((b->paused?2u:1u)|(b->looping?4u:0u)):0;return b->error;
}
int32_t pilot_audio_buffer_position(void *opaque,uint32_t *play,uint32_t *write)
{auto *b=static_cast<Buffer*>(opaque);Guard guard;refresh(b);if(play)*play=position(b);if(write)*write=b->next;audio_stream_record(b,"position");return b->error;}
int32_t pilot_audio_buffer_seek(void *opaque,uint32_t at)
{auto *b=static_cast<Buffer*>(opaque);if(at>=b->bytes || at%b->align)return E_INVALIDARG;Guard guard;return seek(b,at,b->playing);}
int32_t pilot_audio_buffer_loop_region(void *opaque,uint32_t start,uint32_t bytes)
{
    auto *b=static_cast<Buffer*>(opaque);
    if(!b || start>=b->bytes || start%b->align)return E_INVALIDARG;
    if(!bytes)bytes=b->bytes-start;
    if(bytes>b->bytes-start || bytes%b->align)return E_INVALIDARG;
    Guard guard;UINT32 at=position(b);
    b->loop_start=start;b->loop_end=start+bytes;
    audio_ingress_record(b,4,0,nullptr,0);
    /* Rebuild queued owned PCM at the actual device cursor; no elapsed-time
     * completion. This extends the donor's loop-start/span model to loop end. */
    return seek(b,at,b->playing);
}
int32_t pilot_audio_buffer_volume(void *opaque,int32_t volume)
{auto *b=static_cast<Buffer*>(opaque);if(volume>0 || volume< -10000)return E_INVALIDARG;Guard guard;return b->voice->SetVolume(volume==-10000?0.0f:powf(10.0f,volume/2000.0f));}
int32_t pilot_audio_buffer_frequency(void *opaque,uint32_t frequency)
{
    auto *b=static_cast<Buffer*>(opaque);if(!frequency)frequency=b->rate;
    if(frequency<100 || frequency>200000)return E_INVALIDARG;Guard guard;
    HRESULT hr=b->voice->SetFrequencyRatio(frequency/48000.0f);
    if(SUCCEEDED(hr)){b->frequency=frequency;audio_ingress_record(b,4,0,nullptr,0);SetEvent(wake);}return hr;
}
