/* Reuse the Windows DirectSound implementation for 2D PCM mixing, resampling,
 * looping, ownership and device-paced cursors. This is an independent Xbox
 * adapter, not an import of the donors' stub DirectSound or GPL APU bodies. */
#define COBJMACROS
#define DIRECTSOUND_VERSION 0x0800
#include <windows.h>
#include <mmsystem.h>
#include <dsound.h>
#include <string.h>
#include "audio_backend.h"
static IDirectSound8 *audio_host;
static IDirectSoundBuffer *audio_primary;

int32_t pilot_audio_open(void)
{
    if(audio_host)return S_OK;
    HRESULT hr=DirectSoundCreate8(NULL,&audio_host,NULL);
    /* Headless reference output has GLOBALFOCUS buffers. A process-independent
     * desktop HWND avoids creating a window with cross-thread destruction rules. */
    if(SUCCEEDED(hr))hr=IDirectSound8_SetCooperativeLevel(audio_host,GetDesktopWindow(),DSSCL_PRIORITY);
    if(SUCCEEDED(hr)) {
        DSBUFFERDESC desc={0};desc.dwSize=sizeof(desc);desc.dwFlags=DSBCAPS_PRIMARYBUFFER;
        hr=IDirectSound8_CreateSoundBuffer(audio_host,&desc,&audio_primary,NULL);
    }
    if(SUCCEEDED(hr)) {
        WAVEFORMATEX format={WAVE_FORMAT_PCM,2,48000,192000,4,16,0};
        hr=IDirectSoundBuffer_SetFormat(audio_primary,&format);
    }
    if(FAILED(hr))pilot_audio_close();
    return hr;
}
void pilot_audio_close(void)
{
    if(audio_primary){IDirectSoundBuffer_Release(audio_primary);audio_primary=NULL;}
    if(audio_host){IDirectSound8_Release(audio_host);audio_host=NULL;}
}
int32_t pilot_audio_buffer_create(uint32_t bytes,unsigned channels,unsigned rate,unsigned bits,void **out)
{
    if(!audio_host || !out || !bytes || (channels!=1 && channels!=2) ||
       (bits!=8 && bits!=16) || rate<100 || rate>200000 || bytes%(channels*(bits/8)))return E_INVALIDARG;
    WAVEFORMATEX format={WAVE_FORMAT_PCM,(WORD)channels,rate,rate*channels*(bits/8),
                         (WORD)(channels*(bits/8)),(WORD)bits,0};
    DSBUFFERDESC desc={0};desc.dwSize=sizeof(desc);desc.dwBufferBytes=bytes;desc.lpwfxFormat=&format;
    desc.dwFlags=DSBCAPS_GLOBALFOCUS|DSBCAPS_GETCURRENTPOSITION2|DSBCAPS_CTRLVOLUME|DSBCAPS_CTRLFREQUENCY;
    IDirectSoundBuffer *buffer=NULL;
    HRESULT hr=IDirectSound8_CreateSoundBuffer(audio_host,&desc,&buffer,NULL);
    if(SUCCEEDED(hr))*out=buffer;
    return hr;
}
void pilot_audio_buffer_destroy(void *buffer)
{
    if(buffer)IDirectSoundBuffer_Release((IDirectSoundBuffer*)buffer);
}
int32_t pilot_audio_buffer_copy(void *buffer,uint32_t offset,void *data,uint32_t bytes,int readback)
{
    if(!buffer || !data || !bytes)return E_INVALIDARG;
    IDirectSoundBuffer *b=buffer;void *p1=NULL,*p2=NULL;DWORD n1=0,n2=0;
    HRESULT hr=IDirectSoundBuffer_Lock(b,offset,bytes,&p1,&n1,&p2,&n2,0);
    if(FAILED(hr))return hr;
    if(n1+n2!=bytes)hr=E_FAIL;
    else if(readback){memcpy(data,p1,n1);if(n2)memcpy((BYTE*)data+n1,p2,n2);}
    else {memcpy(p1,data,n1);if(n2)memcpy(p2,(BYTE*)data+n1,n2);}
    HRESULT unlock=IDirectSoundBuffer_Unlock(b,p1,n1,p2,n2);
    return FAILED(hr)?hr:unlock;
}
int32_t pilot_audio_buffer_play(void *b,int looping)
{ return IDirectSoundBuffer_Play((IDirectSoundBuffer*)b,0,0,looping?DSBPLAY_LOOPING:0); }
int32_t pilot_audio_buffer_stop(void *b)
{ return IDirectSoundBuffer_Stop((IDirectSoundBuffer*)b); }
int32_t pilot_audio_buffer_status(void *b,uint32_t *status)
{
    DWORD native=0;HRESULT hr=IDirectSoundBuffer_GetStatus((IDirectSoundBuffer*)b,&native);
    if(FAILED(hr))return hr;
    if(native&DSBSTATUS_BUFFERLOST)return DSERR_BUFFERLOST; /* Xbox bit 2 means paused. */
    if(native&~(DSBSTATUS_PLAYING|DSBSTATUS_LOOPING))return E_UNEXPECTED;
    *status=native;return hr;
}
int32_t pilot_audio_buffer_position(void *b,uint32_t *play,uint32_t *write)
{ return IDirectSoundBuffer_GetCurrentPosition((IDirectSoundBuffer*)b,(DWORD*)play,(DWORD*)write); }
int32_t pilot_audio_buffer_seek(void *b,uint32_t position)
{ return IDirectSoundBuffer_SetCurrentPosition((IDirectSoundBuffer*)b,position); }
int32_t pilot_audio_buffer_volume(void *b,int32_t volume)
{ return IDirectSoundBuffer_SetVolume((IDirectSoundBuffer*)b,volume); }
int32_t pilot_audio_buffer_frequency(void *b,uint32_t frequency)
{ return IDirectSoundBuffer_SetFrequency((IDirectSoundBuffer*)b,frequency); }
