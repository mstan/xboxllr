#pragma once
#include "audio_spatial_filter.hpp"

/* Single publisher/consumer triple buffer. Process owns its front slot for a
 * whole quantum; SetParameters never modifies that slot. Guest code is never
 * entered by the audio callback, and Process never allocates or takes a lock. */
class SpatialEffect final:public IXAPO,public IXAPOParameters {
    std::atomic<ULONG> refs{1};
    std::atomic<unsigned> middle{1};
    unsigned front=0,back=2;
    SRWLOCK parameter_lock=SRWLOCK_INIT;
    PilotAudioSpatialState states[3]{},latest{};
    PilotSpatialFilter filter;
    bool locked=false;
    UINT32 max_frames=0;
public:
    SpatialEffect() {
        for(auto &s:states){s.taps[0][0]=s.taps[1][0]=1;s.frequency_ratio=1;}
        latest=states[0];
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void **out) override {
        if(!out)return E_POINTER;*out=nullptr;
        if(id==__uuidof(IUnknown) || id==__uuidof(IXAPO))*out=static_cast<IXAPO*>(this);
        else if(id==__uuidof(IXAPOParameters))*out=static_cast<IXAPOParameters*>(this);
        else return E_NOINTERFACE;
        AddRef();return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {return ++refs;}
    ULONG STDMETHODCALLTYPE Release() override {ULONG n=--refs;if(!n)delete this;return n;}
    HRESULT STDMETHODCALLTYPE GetRegistrationProperties(XAPO_REGISTRATION_PROPERTIES **out) override {
        if(!out)return E_POINTER;
        auto *p=static_cast<XAPO_REGISTRATION_PROPERTIES*>(CoTaskMemAlloc(sizeof(XAPO_REGISTRATION_PROPERTIES)));
        if(!p)return E_OUTOFMEMORY;
        *p={};p->clsid={0xbdc57524,0x24e2,0x4597,{0xa3,0x35,0xd4,0x62,0x20,0x48,0x29,0x41}};
        wcscpy_s(p->FriendlyName,L"Xbox spatial voice");wcscpy_s(p->CopyrightInfo,L"xboxlle");
        p->MajorVersion=1;p->Flags=XAPO_FLAG_FRAMERATE_MUST_MATCH|XAPO_FLAG_BITSPERSAMPLE_MUST_MATCH|XAPO_FLAG_BUFFERCOUNT_MUST_MATCH;
        p->MinInputBufferCount=p->MaxInputBufferCount=p->MinOutputBufferCount=p->MaxOutputBufferCount=1;
        *out=p;return S_OK;
    }
    HRESULT STDMETHODCALLTYPE IsInputFormatSupported(const WAVEFORMATEX *out,const WAVEFORMATEX *in,WAVEFORMATEX **suggested) override {
        if(suggested)*suggested=nullptr;
        return float_format(in,1) && (!out || float_format(out,PILOT_AUDIO_SPATIAL_CHANNELS))?S_OK:XAPO_E_FORMAT_UNSUPPORTED;
    }
    HRESULT STDMETHODCALLTYPE IsOutputFormatSupported(const WAVEFORMATEX *in,const WAVEFORMATEX *out,WAVEFORMATEX **suggested) override {
        if(suggested)*suggested=nullptr;
        return float_format(out,PILOT_AUDIO_SPATIAL_CHANNELS) && (!in || float_format(in,1))?S_OK:XAPO_E_FORMAT_UNSUPPORTED;
    }
    HRESULT STDMETHODCALLTYPE Initialize(const void *,UINT32 bytes) override {return bytes?E_INVALIDARG:S_OK;}
    void STDMETHODCALLTYPE Reset() override {filter.reset();}
    HRESULT STDMETHODCALLTYPE LockForProcess(UINT32 ni,const XAPO_LOCKFORPROCESS_PARAMETERS *in,UINT32 no,const XAPO_LOCKFORPROCESS_PARAMETERS *out) override {
        if(locked || ni!=1 || no!=1 || !in || !out || !float_format(in->pFormat,1) ||
           !float_format(out->pFormat,PILOT_AUDIO_SPATIAL_CHANNELS) || !in->MaxFrameCount || in->MaxFrameCount!=out->MaxFrameCount)return E_INVALIDARG;
        max_frames=in->MaxFrameCount;locked=true;Reset();return S_OK;
    }
    void STDMETHODCALLTYPE UnlockForProcess() override {locked=false;}
    void STDMETHODCALLTYPE SetParameters(const void *data,UINT32 bytes) override {
        if(!data || bytes!=sizeof(PilotAudioSpatialState))fail("spatial effect parameter shape");
        const auto &p=*static_cast<const PilotAudioSpatialState*>(data);
        if(!pilot_spatial_state_valid(p))fail("spatial effect parameter values");
        AcquireSRWLockExclusive(&parameter_lock);
        states[back]=latest=p;
        back=middle.exchange(back|4,std::memory_order_acq_rel)&3;
        ReleaseSRWLockExclusive(&parameter_lock);
    }
    void STDMETHODCALLTYPE GetParameters(void *data,UINT32 bytes) override {
        if(!data || bytes!=sizeof(PilotAudioSpatialState))fail("spatial effect parameter read shape");
        AcquireSRWLockShared(&parameter_lock);memcpy(data,&latest,bytes);ReleaseSRWLockShared(&parameter_lock);
    }
    void STDMETHODCALLTYPE Process(UINT32 ni,const XAPO_PROCESS_BUFFER_PARAMETERS *in,UINT32 no,XAPO_PROCESS_BUFFER_PARAMETERS *out,BOOL) override {
        if(!locked || ni!=1 || no!=1 || !in || !out || in->ValidFrameCount>max_frames ||
           out->ValidFrameCount!=in->ValidFrameCount)fail("spatial processing shape");
        if(middle.load(std::memory_order_acquire)&4)front=middle.exchange(front,std::memory_order_acq_rel)&3;
        const auto &p=states[front];const auto *src=static_cast<const float*>(in->pBuffer);auto *dst=static_cast<float*>(out->pBuffer);
        bool nonzero=false;
        for(UINT32 frame=0;frame<in->ValidFrameCount;++frame) {
            float sample=in->BufferFlags==XAPO_BUFFER_SILENT?0:src[frame];
            if(!std::isfinite(sample))fail("nonfinite spatial sample");
            filter.sample(sample,p,dst+frame*PILOT_AUDIO_SPATIAL_CHANNELS);
            for(unsigned c=0;c<PILOT_AUDIO_SPATIAL_CHANNELS;++c)nonzero|=dst[frame*PILOT_AUDIO_SPATIAL_CHANNELS+c]!=0;
        }
        out->BufferFlags=nonzero?XAPO_BUFFER_VALID:XAPO_BUFFER_SILENT;
    }
    UINT32 STDMETHODCALLTYPE CalcInputFrames(UINT32 n) override {return n;}
    UINT32 STDMETHODCALLTYPE CalcOutputFrames(UINT32 n) override {return n;}
};
