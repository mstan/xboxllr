/* Independent native transport: XAudio2 mixes/resamples voices into 32 Xbox
 * bins; a whole GP graph produces six speaker channels. Backend selection is
 * a build option. There is no reference/native switching or duplicate run. */
#include <windows.h>
#include <xaudio2.h>
#include <xapo.h>
#include <ks.h>
#include <ksmedia.h>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <new>
#include "audio_backend.h"
#include "audio_dsp.h"

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
class Effects final:public IXAPO {
    std::atomic<ULONG> refs{1};
    bool locked=false;
    UINT32 max_frames=0,offset=0;
    uint32_t bins[1024]{},speakers[192]{};
public:
    PilotDsp *graph=nullptr; // Changed only while XAudio2's engine is stopped.
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
                double q=sample*8388608.0;
                bins[c*32+offset]=(uint32_t)(int32_t)(q>8388607?8388607:q< -8388608?-8388608:q)&0xffffff;
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
    UINT64 baseline=0;
    bool playing=false,looping=false,submitted_end=false;
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
static UINT32 position(Buffer *b,XAUDIO2_VOICE_STATE *state=nullptr)
{
    XAUDIO2_VOICE_STATE s;b->voice->GetState(&s);
    UINT64 cursor=(UINT64)b->origin+(s.SamplesPlayed-b->baseline)*b->align;
    if(state)*state=s;
    if(!b->looping && cursor>=b->bytes)return 0; // DirectSound wraps end cursor.
    return (UINT32)(cursor%b->bytes);
}
static HRESULT fill(Buffer *b)
{
    if(FAILED(b->error))return b->error;
    if(!b->playing || b->submitted_end)return S_OK;
    XAUDIO2_VOICE_STATE s;b->voice->GetState(&s,XAUDIO2_VOICE_NOSAMPLESPLAYED);
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
            UINT32 part=n-copied;if(part>b->bytes-cursor)part=b->bytes-cursor;
            memcpy(chunk.data+copied,b->data+cursor,part);copied+=part;cursor=(cursor+part)%b->bytes;
        }
        XAUDIO2_BUFFER submit{};submit.AudioBytes=n;submit.pAudioData=chunk.data;submit.pContext=&chunk;
        HRESULT hr=b->voice->SubmitSourceBuffer(&submit);
        if(FAILED(hr)){chunk.busy=false;return hr;}
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
    return hr;
}
void pilot_audio_close(void)
{
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
    engine->StopEngine();PilotDsp *old=effects->graph;effects->graph=graph;effects->Reset();
    HRESULT hr=engine->StartEngine();if(FAILED(hr))effects->graph=old;
    return hr;
}
int32_t pilot_audio_buffer_create(uint32_t bytes,unsigned channels,unsigned rate,unsigned bits,void **out)
{
    if(!engine || !out || !bytes || (channels!=1 && channels!=2) || (bits!=8 && bits!=16) ||
       rate<100 || rate>200000 || bytes%(channels*(bits/8)))return E_INVALIDARG;
    Buffer *b=new(std::nothrow) Buffer();if(!b)return E_OUTOFMEMORY;
    b->data=new(std::nothrow) BYTE[bytes];if(!b->data){delete b;return E_OUTOFMEMORY;}
    b->bytes=bytes;b->channels=channels;b->align=channels*bits/8;b->rate=b->frequency=rate;
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
        if(slot==256)hr=E_OUTOFMEMORY;else buffers[slot]=b;
    }
    if(FAILED(hr)){if(b->voice)b->voice->DestroyVoice();delete[] b->data;delete b;return hr;}
    *out=b;return S_OK;
}
void pilot_audio_buffer_destroy(void *opaque)
{
    auto *b=static_cast<Buffer*>(opaque);if(!b)return;
    {Guard guard;for(auto &p:buffers)if(p==b)p=nullptr;}
    b->voice->DestroyVoice();delete[] b->data;delete b;
}
int32_t pilot_audio_buffer_copy(void *opaque,uint32_t offset,void *data,uint32_t bytes,int readback)
{
    auto *b=static_cast<Buffer*>(opaque);
    if(!b || !data || !bytes || offset>=b->bytes || bytes>b->bytes)return E_INVALIDARG;
    Guard guard;UINT32 first=bytes;if(first>b->bytes-offset)first=b->bytes-offset;
    if(readback){memcpy(data,b->data+offset,first);memcpy((BYTE*)data+first,b->data,bytes-first);}
    else {memcpy(b->data+offset,data,first);memcpy(b->data,(BYTE*)data+first,bytes-first);}
    return b->error;
}
static HRESULT seek(Buffer *b,UINT32 at,bool play);
static XAUDIO2_VOICE_STATE refresh(Buffer *b)
{
    XAUDIO2_VOICE_STATE s;b->voice->GetState(&s);
    if(b->playing && b->submitted_end && !s.BuffersQueued){b->playing=false;b->origin=b->next=0;b->baseline=s.SamplesPlayed;b->submitted_end=false;}
    return s;
}
int32_t pilot_audio_buffer_play(void *opaque,int looping)
{
    auto *b=static_cast<Buffer*>(opaque);Guard guard;
    refresh(b);
    if(b->playing && b->looping!=!!looping){UINT32 at=position(b);b->looping=!!looping;return seek(b,at,true);}
    b->looping=!!looping;b->playing=true;
    HRESULT hr=fill(b);if(SUCCEEDED(hr))hr=b->voice->Start();if(FAILED(hr))b->playing=false;
    SetEvent(wake);return hr;
}
static HRESULT seek(Buffer *b,UINT32 at,bool play)
{
    HRESULT hr=b->voice->Stop();if(SUCCEEDED(hr))hr=b->voice->FlushSourceBuffers();
    if(FAILED(hr))return hr;
    XAUDIO2_VOICE_STATE s;b->voice->GetState(&s);
    b->origin=b->next=at;b->baseline=s.SamplesPlayed;b->submitted_end=false;b->playing=play;
    if(play){hr=fill(b);if(SUCCEEDED(hr))hr=b->voice->Start();SetEvent(wake);}
    return hr;
}
int32_t pilot_audio_buffer_stop(void *opaque)
{auto *b=static_cast<Buffer*>(opaque);Guard guard;return seek(b,position(b),false);}
int32_t pilot_audio_buffer_status(void *opaque,uint32_t *status)
{
    auto *b=static_cast<Buffer*>(opaque);if(!status)return E_POINTER;Guard guard;
    refresh(b);
    *status=b->playing?(b->looping?5:1):0;return b->error;
}
int32_t pilot_audio_buffer_position(void *opaque,uint32_t *play,uint32_t *write)
{auto *b=static_cast<Buffer*>(opaque);Guard guard;refresh(b);if(play)*play=position(b);if(write)*write=b->next;return b->error;}
int32_t pilot_audio_buffer_seek(void *opaque,uint32_t at)
{auto *b=static_cast<Buffer*>(opaque);if(at>=b->bytes || at%b->align)return E_INVALIDARG;Guard guard;return seek(b,at,b->playing);}
int32_t pilot_audio_buffer_volume(void *opaque,int32_t volume)
{auto *b=static_cast<Buffer*>(opaque);if(volume>0 || volume< -10000)return E_INVALIDARG;Guard guard;return b->voice->SetVolume(volume==-10000?0.0f:powf(10.0f,volume/2000.0f));}
int32_t pilot_audio_buffer_frequency(void *opaque,uint32_t frequency)
{
    auto *b=static_cast<Buffer*>(opaque);if(!frequency)frequency=b->rate;
    if(frequency<100 || frequency>200000)return E_INVALIDARG;Guard guard;
    HRESULT hr=b->voice->SetFrequencyRatio(frequency/48000.0f);
    if(SUCCEEDED(hr)){b->frequency=frequency;SetEvent(wake);}return hr;
}
