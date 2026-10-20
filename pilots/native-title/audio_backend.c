/* Reuse the Windows DirectSound implementation for 2D PCM mixing, resampling,
 * looping, ownership and device-paced cursors. This is an independent Xbox
 * adapter, not an import of the donors' stub DirectSound or GPL APU bodies. */
#define COBJMACROS
#define DIRECTSOUND_VERSION 0x0800
#include <windows.h>
#include <mmsystem.h>
#include <dsound.h>
#include <stdlib.h>
#include <string.h>
#include "audio_backend.h"
static IDirectSound8 *audio_host;
static IDirectSoundBuffer *audio_primary;
struct AudioBuffer {IDirectSoundBuffer *voice;int playing,looping,paused;};

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
    if(SUCCEEDED(hr)) {
        struct AudioBuffer *owned=calloc(1,sizeof(*owned));
        if(!owned){IDirectSoundBuffer_Release(buffer);return E_OUTOFMEMORY;}
        owned->voice=buffer;*out=owned;
    }
    return hr;
}
void pilot_audio_buffer_destroy(void *buffer)
{
    struct AudioBuffer *b=buffer;if(b){IDirectSoundBuffer_Release(b->voice);free(b);}
}
int32_t pilot_audio_buffer_copy(void *buffer,uint32_t offset,void *data,uint32_t bytes,int readback)
{
    if(!buffer || !data || !bytes)return E_INVALIDARG;
    IDirectSoundBuffer *b=((struct AudioBuffer*)buffer)->voice;void *p1=NULL,*p2=NULL;DWORD n1=0,n2=0;
    HRESULT hr=IDirectSoundBuffer_Lock(b,offset,bytes,&p1,&n1,&p2,&n2,0);
    if(FAILED(hr))return hr;
    if(n1+n2!=bytes)hr=E_FAIL;
    else if(readback){memcpy(data,p1,n1);if(n2)memcpy((BYTE*)data+n1,p2,n2);}
    else {memcpy(p1,data,n1);if(n2)memcpy(p2,(BYTE*)data+n1,n2);}
    HRESULT unlock=IDirectSoundBuffer_Unlock(b,p1,n1,p2,n2);
    return FAILED(hr)?hr:unlock;
}
int32_t pilot_audio_buffer_play(void *opaque,int looping)
{ struct AudioBuffer *b=opaque;HRESULT hr=IDirectSoundBuffer_Play(b->voice,0,0,looping?DSBPLAY_LOOPING:0);
  if(SUCCEEDED(hr)){b->playing=1;b->looping=!!looping;b->paused=0;}return hr; }
int32_t pilot_audio_buffer_stop(void *opaque)
{ struct AudioBuffer *b=opaque;HRESULT hr=IDirectSoundBuffer_Stop(b->voice);
  if(SUCCEEDED(hr))b->playing=b->paused=0;return hr; }
int32_t pilot_audio_buffer_status(void *opaque,uint32_t *status)
{
    struct AudioBuffer *b=opaque;DWORD native=0;HRESULT hr=IDirectSoundBuffer_GetStatus(b->voice,&native);
    if(FAILED(hr))return hr;
    if(native&DSBSTATUS_BUFFERLOST)return DSERR_BUFFERLOST; /* Xbox bit 2 means paused. */
    if(native&~(DSBSTATUS_PLAYING|DSBSTATUS_LOOPING))return E_UNEXPECTED;
    if(!b->paused && !(native&DSBSTATUS_PLAYING))b->playing=0;
    *status=b->paused?2u|(b->looping?4u:0u):native;return hr;
}
int32_t pilot_audio_buffer_pause(void *opaque,uint32_t mode)
{
    struct AudioBuffer *b=opaque;uint32_t status;
    if(!b)return E_INVALIDARG;if(mode>1)return E_NOTIMPL;
    HRESULT hr=pilot_audio_buffer_status(b,&status);if(FAILED(hr))return hr;
    if(!b->playing)return (int32_t)mode;
    if(b->paused==!!mode)return S_OK;
    hr=mode?IDirectSoundBuffer_Stop(b->voice):IDirectSoundBuffer_Play(b->voice,0,0,b->looping?DSBPLAY_LOOPING:0);
    if(SUCCEEDED(hr))b->paused=!!mode;return hr;
}
int32_t pilot_audio_buffer_position(void *b,uint32_t *play,uint32_t *write)
{ return IDirectSoundBuffer_GetCurrentPosition(((struct AudioBuffer*)b)->voice,(DWORD*)play,(DWORD*)write); }
int32_t pilot_audio_buffer_seek(void *b,uint32_t position)
{ return IDirectSoundBuffer_SetCurrentPosition(((struct AudioBuffer*)b)->voice,position); }
int32_t pilot_audio_buffer_volume(void *b,int32_t volume)
{ return IDirectSoundBuffer_SetVolume(((struct AudioBuffer*)b)->voice,volume); }
int32_t pilot_audio_buffer_frequency(void *b,uint32_t frequency)
{ return IDirectSoundBuffer_SetFrequency(((struct AudioBuffer*)b)->voice,frequency); }
