/* SPDX-License-Identifier: GPL-3.0-or-later
 * The title's shared voice/DSP graph consumes the native FAudio engine.
 * This adapter preserves real queue callbacks and operation-set commits.
 */
#pragma once
#include "xapo.h"
#include <new>
#include <vector>
#include <memory>
using XAUDIO2_BUFFER=FAudioBuffer;
using XAUDIO2_VOICE_STATE=FAudioVoiceState;
#define XAUDIO2_DEFAULT_PROCESSOR FAUDIO_DEFAULT_PROCESSOR
#define XAUDIO2_END_OF_STREAM FAUDIO_END_OF_STREAM
#define XAUDIO2_VOICE_NOSAMPLESPLAYED FAUDIO_VOICE_NOSAMPLESPLAYED
#define XAUDIO2_LOOP_INFINITE FAUDIO_LOOP_INFINITE
struct IXAudio2VoiceCallback {
    virtual void OnVoiceProcessingPassStart(UINT32)=0;virtual void OnVoiceProcessingPassEnd()=0;
    virtual void OnStreamEnd()=0;virtual void OnBufferStart(void*)=0;virtual void OnBufferEnd(void*)=0;
    virtual void OnLoopEnd(void*)=0;virtual void OnVoiceError(void*,HRESULT)=0;
};
struct IXAudio2EngineCallback {virtual void OnProcessingPassStart()=0;virtual void OnProcessingPassEnd()=0;virtual void OnCriticalError(HRESULT)=0;};
struct PilotVoiceCallback:public FAudioVoiceCallback {
    IXAudio2VoiceCallback *target;
    explicit PilotVoiceCallback(IXAudio2VoiceCallback *callback):target(callback) {
        OnVoiceProcessingPassStart=[](FAudioVoiceCallback *p,uint32_t bytes){static_cast<PilotVoiceCallback*>(p)->target->OnVoiceProcessingPassStart(bytes);};
        OnVoiceProcessingPassEnd=[](FAudioVoiceCallback *p){static_cast<PilotVoiceCallback*>(p)->target->OnVoiceProcessingPassEnd();};
        OnStreamEnd=[](FAudioVoiceCallback *p){static_cast<PilotVoiceCallback*>(p)->target->OnStreamEnd();};
        OnBufferStart=[](FAudioVoiceCallback *p,void *c){static_cast<PilotVoiceCallback*>(p)->target->OnBufferStart(c);};
        OnBufferEnd=[](FAudioVoiceCallback *p,void *c){static_cast<PilotVoiceCallback*>(p)->target->OnBufferEnd(c);};
        OnLoopEnd=[](FAudioVoiceCallback *p,void *c){static_cast<PilotVoiceCallback*>(p)->target->OnLoopEnd(c);};
        OnVoiceError=[](FAudioVoiceCallback *p,void *c,uint32_t error){static_cast<PilotVoiceCallback*>(p)->target->OnVoiceError(c,(HRESULT)error);};
    }
};
class IXAudio2Voice {
public:
    FAudioVoice *native=nullptr;std::unique_ptr<PilotVoiceCallback> callback;
    void DestroyVoice(){FAudioVoice_DestroyVoice(native);delete this;}
    HRESULT SetVolume(float value,UINT32 operation=0){return (HRESULT)FAudioVoice_SetVolume(native,value,operation);}
    HRESULT SetOutputMatrix(IXAudio2Voice *destination,UINT32 source_channels,UINT32 destination_channels,const float *matrix,UINT32 operation=0)
    {return (HRESULT)FAudioVoice_SetOutputMatrix(native,destination?destination->native:nullptr,source_channels,destination_channels,matrix,operation);}
    HRESULT SetEffectParameters(UINT32 index,const void *data,UINT32 bytes,UINT32 operation=0)
    {return (HRESULT)FAudioVoice_SetEffectParameters(native,index,data,bytes,operation);}
    HRESULT SetFrequencyRatio(float ratio,UINT32 operation=0){return (HRESULT)FAudioSourceVoice_SetFrequencyRatio(native,ratio,operation);}
    HRESULT SubmitSourceBuffer(const XAUDIO2_BUFFER *buffer){return (HRESULT)FAudioSourceVoice_SubmitSourceBuffer(native,buffer,nullptr);}
    HRESULT Start(UINT32 flags=0,UINT32 operation=0){return (HRESULT)FAudioSourceVoice_Start(native,flags,operation);}
    HRESULT Stop(UINT32 flags=0,UINT32 operation=0){return (HRESULT)FAudioSourceVoice_Stop(native,flags,operation);}
    HRESULT FlushSourceBuffers(){return (HRESULT)FAudioSourceVoice_FlushSourceBuffers(native);}
    void GetState(XAUDIO2_VOICE_STATE *state,UINT32 flags=0){FAudioSourceVoice_GetState(native,state,flags);}
};
using IXAudio2SourceVoice=IXAudio2Voice;
using IXAudio2SubmixVoice=IXAudio2Voice;
using IXAudio2MasteringVoice=IXAudio2Voice;
struct XAUDIO2_SEND_DESCRIPTOR {UINT32 Flags;IXAudio2Voice *pOutputVoice;};
struct XAUDIO2_VOICE_SENDS {UINT32 SendCount;const XAUDIO2_SEND_DESCRIPTOR *pSends;};
struct XAUDIO2_EFFECT_DESCRIPTOR {IXAPO *pEffect;BOOL InitialState;UINT32 OutputChannels;};
struct XAUDIO2_EFFECT_CHAIN {UINT32 EffectCount;const XAUDIO2_EFFECT_DESCRIPTOR *pEffectDescriptors;};
struct PilotAudioSends {
    std::vector<FAudioSendDescriptor> descriptors;FAudioVoiceSends sends{};
    explicit PilotAudioSends(const XAUDIO2_VOICE_SENDS *source){if(source){for(unsigned i=0;i<source->SendCount;++i)descriptors.push_back({source->pSends[i].Flags,source->pSends[i].pOutputVoice->native});sends={source->SendCount,descriptors.data()};}}
};
struct PilotAudioEffects {
    std::vector<FAudioEffectDescriptor> descriptors;FAudioEffectChain chain{};
    explicit PilotAudioEffects(const XAUDIO2_EFFECT_CHAIN *source){if(source){for(unsigned i=0;i<source->EffectCount;++i){const auto &effect=source->pEffectDescriptors[i];descriptors.push_back({new PilotFapo(effect.pEffect),effect.InitialState,effect.OutputChannels});}chain={source->EffectCount,descriptors.data()};}}
    ~PilotAudioEffects(){for(auto &descriptor:descriptors){auto *effect=static_cast<FAPO*>(descriptor.pEffect);effect->Release(effect);}}
};
struct PilotEngineCallback:public FAudioEngineCallback {
    IXAudio2EngineCallback *target=nullptr;
    PilotEngineCallback(){
        OnProcessingPassStart=[](FAudioEngineCallback *p){static_cast<PilotEngineCallback*>(p)->target->OnProcessingPassStart();};
        OnProcessingPassEnd=[](FAudioEngineCallback *p){static_cast<PilotEngineCallback*>(p)->target->OnProcessingPassEnd();};
        OnCriticalError=[](FAudioEngineCallback *p,uint32_t error){static_cast<PilotEngineCallback*>(p)->target->OnCriticalError((HRESULT)error);};
    }
};
class IXAudio2 {
    FAudio *native=nullptr;PilotEngineCallback callback;
public:
    HRESULT initialize(UINT32 flags,UINT32 processor){return (HRESULT)FAudioCreate(&native,flags,processor);}
    ULONG Release(){FAudio_Release(native);delete this;return 0;}
    HRESULT StartEngine(){return (HRESULT)FAudio_StartEngine(native);}
    void StopEngine(){FAudio_StopEngine(native);}
    HRESULT CommitChanges(UINT32 operation){return (HRESULT)FAudio_CommitOperationSet(native,operation);}
    HRESULT RegisterForCallbacks(IXAudio2EngineCallback *target){if(callback.target)return E_UNEXPECTED;callback.target=target;return (HRESULT)FAudio_RegisterForCallbacks(native,&callback);}
    void UnregisterForCallbacks(IXAudio2EngineCallback *target){if(target==callback.target){FAudio_UnregisterForCallbacks(native,&callback);callback.target=nullptr;}}
    HRESULT CreateMasteringVoice(IXAudio2MasteringVoice **out,UINT32 channels,UINT32 rate){
        auto voice=std::make_unique<IXAudio2Voice>();HRESULT result=(HRESULT)FAudio_CreateMasteringVoice(native,&voice->native,channels,rate,0,0,nullptr);
        if(SUCCEEDED(result))*out=voice.release();return result;
    }
    HRESULT CreateSubmixVoice(IXAudio2SubmixVoice **out,UINT32 channels,UINT32 rate,UINT32 flags,UINT32 stage,const XAUDIO2_VOICE_SENDS *sends=nullptr,const XAUDIO2_EFFECT_CHAIN *effects=nullptr){
        auto voice=std::make_unique<IXAudio2Voice>();PilotAudioSends native_sends(sends);PilotAudioEffects native_effects(effects);
        HRESULT result=(HRESULT)FAudio_CreateSubmixVoice(native,&voice->native,channels,rate,flags,stage,sends?&native_sends.sends:nullptr,effects?&native_effects.chain:nullptr);
        if(SUCCEEDED(result))*out=voice.release();return result;
    }
    HRESULT CreateSourceVoice(IXAudio2SourceVoice **out,const WAVEFORMATEX *format,UINT32 flags,float max_ratio,IXAudio2VoiceCallback *target,const XAUDIO2_VOICE_SENDS *sends=nullptr,const XAUDIO2_EFFECT_CHAIN *effects=nullptr){
        auto voice=std::make_unique<IXAudio2Voice>();if(target)voice->callback=std::make_unique<PilotVoiceCallback>(target);PilotAudioSends native_sends(sends);PilotAudioEffects native_effects(effects);
        HRESULT result=(HRESULT)FAudio_CreateSourceVoice(native,&voice->native,format,flags,max_ratio,voice->callback.get(),sends?&native_sends.sends:nullptr,effects?&native_effects.chain:nullptr);
        if(SUCCEEDED(result))*out=voice.release();return result;
    }
};
inline HRESULT XAudio2Create(IXAudio2 **out,UINT32 flags,UINT32 processor)
{auto engine=std::make_unique<IXAudio2>();HRESULT result=engine->initialize(flags,processor);if(SUCCEEDED(result))*out=engine.release();return result;}
