/* SPDX-License-Identifier: GPL-3.0-or-later
 * Native FAudio effect adapter. The DSP/spatial implementations remain shared
 * with Windows; only the native FAPO callback interface differs.
 */
#pragma once
#include "windows.h"
#include <unknwn.h>
#include <FAudio.h>
#include <FAPO.h>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <cwchar>
using WAVEFORMATEX=FAudioWaveFormatEx;
using WAVEFORMATEXTENSIBLE=FAudioWaveFormatExtensible;
using XAPO_LOCKFORPROCESS_PARAMETERS=FAPOLockForProcessBufferParameters;
using XAPO_PROCESS_BUFFER_PARAMETERS=FAPOProcessBufferParameters;
#define WAVE_FORMAT_PCM 1
#define WAVE_FORMAT_IEEE_FLOAT 3
#define WAVE_FORMAT_EXTENSIBLE 0xfffe
#define XAPO_FLAG_FRAMERATE_MUST_MATCH FAPO_FLAG_FRAMERATE_MUST_MATCH
#define XAPO_FLAG_BITSPERSAMPLE_MUST_MATCH FAPO_FLAG_BITSPERSAMPLE_MUST_MATCH
#define XAPO_FLAG_BUFFERCOUNT_MUST_MATCH FAPO_FLAG_BUFFERCOUNT_MUST_MATCH
#define XAPO_BUFFER_VALID FAPO_BUFFER_VALID
#define XAPO_BUFFER_SILENT FAPO_BUFFER_SILENT
#define XAPO_E_FORMAT_UNSUPPORTED ((HRESULT)0x88970001)
inline const GUID KSDATAFORMAT_SUBTYPE_IEEE_FLOAT={3,0,0x10,{0x80,0,0,0xaa,0,0x38,0x9b,0x71}};
inline bool operator==(const FAudioGUID &a,const GUID &b){return std::memcmp(&a,&b,sizeof(b))==0;}
inline void *CoTaskMemAlloc(size_t bytes){return std::malloc(bytes);}
inline void CoTaskMemFree(void *p){std::free(p);}
template<size_t N> inline int wcscpy_s(WCHAR (&out)[N],const WCHAR *text)
{size_t n=std::wcslen(text);if(n>=N)return E_INVALIDARG;std::wmemcpy(out,text,n+1);return 0;}
struct XAPO_REGISTRATION_PROPERTIES {
    GUID clsid;WCHAR FriendlyName[256],CopyrightInfo[256];
    UINT32 MajorVersion,MinorVersion,Flags,MinInputBufferCount,MaxInputBufferCount,MinOutputBufferCount,MaxOutputBufferCount;
};
struct IXAPO:public IUnknown {
    virtual HRESULT GetRegistrationProperties(XAPO_REGISTRATION_PROPERTIES**)=0;
    virtual HRESULT IsInputFormatSupported(const WAVEFORMATEX*,const WAVEFORMATEX*,WAVEFORMATEX**)=0;
    virtual HRESULT IsOutputFormatSupported(const WAVEFORMATEX*,const WAVEFORMATEX*,WAVEFORMATEX**)=0;
    virtual HRESULT Initialize(const void*,UINT32)=0;
    virtual void Reset()=0;
    virtual HRESULT LockForProcess(UINT32,const XAPO_LOCKFORPROCESS_PARAMETERS*,UINT32,const XAPO_LOCKFORPROCESS_PARAMETERS*)=0;
    virtual void UnlockForProcess()=0;
    virtual void Process(UINT32,const XAPO_PROCESS_BUFFER_PARAMETERS*,UINT32,XAPO_PROCESS_BUFFER_PARAMETERS*,BOOL)=0;
    virtual UINT32 CalcInputFrames(UINT32)=0;virtual UINT32 CalcOutputFrames(UINT32)=0;
};
struct IXAPOParameters:public IUnknown {virtual void SetParameters(const void*,UINT32)=0;virtual void GetParameters(void*,UINT32)=0;};
DECLARE_UUIDOF_HELPER(IXAPO,0xa410b984,0x9839,0x4819,0xa0,0xbe,0x28,0x56,0xae,0x6b,0x3a,0xdb)
DECLARE_UUIDOF_HELPER(IXAPOParameters,0x26d95c66,0x80f2,0x499a,0xad,0x54,0x5a,0xe7,0xf0,0x1c,0x6d,0x98)
struct PilotFapo:public FAPO {
    std::atomic<int32_t> references{1};IXAPO *effect;IXAPOParameters *parameters=nullptr;
    static PilotFapo *self(void *p){return static_cast<PilotFapo*>(static_cast<FAPO*>(p));}
    explicit PilotFapo(IXAPO *object):effect(object) {
        effect->AddRef();void *parameter_object=nullptr;
        if(SUCCEEDED(effect->QueryInterface(__uuidof(IXAPOParameters),&parameter_object)))parameters=static_cast<IXAPOParameters*>(parameter_object);
        AddRef=[](void *p)->int32_t{return ++self(p)->references;};
        Release=[](void *p)->int32_t{auto *s=self(p);int32_t refs=--s->references;if(!refs){if(s->parameters)s->parameters->Release();s->effect->Release();delete s;}return refs;};
        GetRegistrationProperties=[](void *p,FAPORegistrationProperties **out)->uint32_t {
            if(!out)return E_POINTER;XAPO_REGISTRATION_PROPERTIES *source=nullptr;
            HRESULT result=self(p)->effect->GetRegistrationProperties(&source);if(FAILED(result))return (uint32_t)result;
            auto *target=static_cast<FAPORegistrationProperties*>(std::calloc(1,sizeof(FAPORegistrationProperties)));
            if(!target){CoTaskMemFree(source);return E_OUTOFMEMORY;}
            std::memcpy(&target->clsid,&source->clsid,sizeof(GUID));
            for(unsigned i=0;i<256;++i){target->FriendlyName[i]=(int16_t)source->FriendlyName[i];target->CopyrightInfo[i]=(int16_t)source->CopyrightInfo[i];}
            target->MajorVersion=source->MajorVersion;target->MinorVersion=source->MinorVersion;target->Flags=source->Flags;
            target->MinInputBufferCount=source->MinInputBufferCount;target->MaxInputBufferCount=source->MaxInputBufferCount;
            target->MinOutputBufferCount=source->MinOutputBufferCount;target->MaxOutputBufferCount=source->MaxOutputBufferCount;
            CoTaskMemFree(source);*out=target;return 0;
        };
        IsInputFormatSupported=[](void *p,const FAudioWaveFormatEx *a,const FAudioWaveFormatEx *b,FAudioWaveFormatEx **out)->uint32_t{return (uint32_t)self(p)->effect->IsInputFormatSupported(a,b,out);};
        IsOutputFormatSupported=[](void *p,const FAudioWaveFormatEx *a,const FAudioWaveFormatEx *b,FAudioWaveFormatEx **out)->uint32_t{return (uint32_t)self(p)->effect->IsOutputFormatSupported(a,b,out);};
        Initialize=[](void *p,const void *data,uint32_t bytes)->uint32_t{return (uint32_t)self(p)->effect->Initialize(data,bytes);};
        Reset=[](void *p){self(p)->effect->Reset();};
        LockForProcess=[](void *p,uint32_t ni,const FAPOLockForProcessBufferParameters *in,uint32_t no,const FAPOLockForProcessBufferParameters *out)->uint32_t{return (uint32_t)self(p)->effect->LockForProcess(ni,in,no,out);};
        UnlockForProcess=[](void *p){self(p)->effect->UnlockForProcess();};
        Process=[](void *p,uint32_t ni,const FAPOProcessBufferParameters *in,uint32_t no,FAPOProcessBufferParameters *out,int32_t enabled){self(p)->effect->Process(ni,in,no,out,enabled);};
        CalcInputFrames=[](void *p,uint32_t n)->uint32_t{return self(p)->effect->CalcInputFrames(n);};
        CalcOutputFrames=[](void *p,uint32_t n)->uint32_t{return self(p)->effect->CalcOutputFrames(n);};
        SetParameters=[](void *p,const void *data,uint32_t bytes){auto *s=self(p);if(!s->parameters)std::abort();s->parameters->SetParameters(data,bytes);};
        GetParameters=[](void *p,void *data,uint32_t bytes){auto *s=self(p);if(!s->parameters)std::abort();s->parameters->GetParameters(data,bytes);};
    }
};
