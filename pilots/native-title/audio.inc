/* Build-selected Xbox DirectSound owner. Guest interfaces retain their Xbox
 * offsets; native COM pointers and transport mechanics never enter guest RAM.
 * Unqualified effect/format/routing requests stop before returning success. */
#include <math.h>
#include "audio_backend.h"
#include "pilot_audio_bindings.h"
#ifdef PILOT_AUDIO_DSP
#include "audio_dsp.h"
static PilotDsp *audio_effects;
static uint32_t audio_effects_storage,audio_effects_location[2];
static void audio_effects_free(void)
{
    pilot_dsp_destroy(audio_effects);audio_effects=NULL;
    if(audio_effects_storage){uint32_t zero=0;if(vm_free(&audio_effects_storage,&zero,MEM_RELEASE))stop("effects storage release failed",audio_effects_storage);audio_effects_storage=0;}
}
#endif
static SRWLOCK audio_lock=SRWLOCK_INIT;
static uint32_t audio_device;
struct PilotAudioBuffer {
    uint32_t base,data,bytes,owned,channels,rate,bits,headroom,frequency;
    int32_t volume;
    void *host;
    uint32_t lock_offset,lock_first,lock_second;
    int locked;
};
static struct PilotAudioBuffer audio_buffers[256];
static void *audio_pointer(uint32_t address,uint32_t bytes)
{
    if(!address || (uint64_t)address+bytes>0x100000000ull)stop("invalid audio guest range",address);
    return memory+address;
}
static void audio_device_check(uint32_t iface)
{
    if(!audio_device || iface!=audio_device+8)stop("unowned audio device",iface);
}
static struct PilotAudioBuffer *audio_buffer(uint32_t iface)
{
    for(unsigned i=0;i<256;++i)if(audio_buffers[i].base && iface==audio_buffers[i].base+28)return &audio_buffers[i];
    stop("unowned audio buffer",iface);
}
static uint32_t audio_ref(uint32_t base,int acquire)
{
    struct PilotAudioBuffer *b=NULL;
    if(base!=audio_device || !base)b=audio_buffer(base+28);
    uint32_t refs=MEM32(base+4);
    if(!refs || (acquire && refs==UINT32_MAX))stop("invalid audio reference lifetime",base);
    refs=acquire?refs+1:refs-1;MEM32(base+4)=refs;
    if(refs)return refs;
    if(b) {
        if(b->locked)stop("releasing locked audio buffer",base);
        pilot_audio_buffer_destroy(b->host);
        if(b->owned){uint32_t bytes=0;if(vm_free(&b->data,&bytes,MEM_RELEASE))stop("audio data release failed",base);}
        pool_free(base);memset(b,0,sizeof(*b));audio_ref(audio_device,0);
    } else {
        pilot_audio_close();
#ifdef PILOT_AUDIO_DSP
        audio_effects_free();
#endif
        pool_free(audio_device);audio_device=0;
    }
    return 0;
}
static int32_t audio_gain(struct PilotAudioBuffer *b)
{
    int64_t gain=(int64_t)b->volume-b->headroom;
    return b->host?pilot_audio_buffer_volume(b->host,(int32_t)(gain< -10000?-10000:gain)):0;
}
static int32_t audio_frequency(struct PilotAudioBuffer *b,uint32_t value)
{
    if(value<100 || value>200000)return (int32_t)0x88780032;
    int32_t hr=b->host?pilot_audio_buffer_frequency(b->host,value):0;
    if(hr>=0)b->frequency=value;
    return hr;
}
static int32_t audio_set_data(struct PilotAudioBuffer *b,uint32_t source,uint32_t bytes)
{
    if(b->locked)stop("replacing locked audio data",b->base);
    if(source && source==b->data && bytes==b->bytes)return 0;
    unsigned align=b->channels*(b->bits/8);
    if(!bytes || bytes>64u*1024*1024 || bytes%align)stop("unqualified audio buffer extent",bytes);
    uint32_t data=source,allocated=bytes;
    if(source)audio_pointer(source,bytes);
    else if(vm_allocate(&data,0,&allocated,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE))return (int32_t)0x8007000e;
    if(!source && b->bits==8)memset(memory+data,128,bytes);
    void *host=NULL;
    int32_t hr=pilot_audio_buffer_create(bytes,b->channels,b->rate,b->bits,&host);
    if(hr>=0)hr=pilot_audio_buffer_copy(host,0,memory+data,bytes,0);
    if(hr>=0) {
        int64_t gain=(int64_t)b->volume-b->headroom;
        hr=pilot_audio_buffer_volume(host,(int32_t)(gain< -10000?-10000:gain));
    }
    if(hr>=0)hr=pilot_audio_buffer_frequency(host,b->frequency);
    if(hr<0) {
        pilot_audio_buffer_destroy(host);
        if(!source){uint32_t zero=0;if(vm_free(&data,&zero,MEM_RELEASE))stop("audio allocation cleanup failed",data);}
        return hr;
    }
    pilot_audio_buffer_destroy(b->host);
    if(b->owned){uint32_t zero=0;if(vm_free(&b->data,&zero,MEM_RELEASE))stop("audio replacement release failed",b->data);}
    b->host=host;b->data=data;b->bytes=bytes;b->owned=!source;
    return 0;
}
static int32_t audio_create_buffer(uint32_t descriptor,uint32_t output)
{
    const uint32_t *desc=audio_pointer(descriptor,24);audio_pointer(output,4);
    if(desc[0]!=24 || desc[1]&~(0x20u|0x80u|0x40000u) || desc[4] || desc[5])
        stop("unqualified sound buffer flags/mix bins",desc[1]);
    uint32_t format=desc[3];audio_pointer(format,18);
    unsigned channels=MEM16(format+2),rate=MEM32(format+4),bits=MEM16(format+14),align=channels*(bits/8);
    if(MEM16(format)!=1 || (channels!=1 && channels!=2) || (bits!=8 && bits!=16) ||
       rate<100 || rate>200000 || MEM16(format+12)!=align || MEM32(format+8)!=rate*align || MEM16(format+16))
        stop("unqualified sound buffer format",MEM16(format));
    unsigned slot;for(slot=0;slot<256 && audio_buffers[slot].base;++slot){}
    if(slot==256)return (int32_t)0x8007000e;
    struct PilotAudioBuffer buffer={0};buffer.channels=channels;buffer.rate=rate;buffer.bits=bits;buffer.frequency=rate;
    buffer.headroom=600; /* Original 2D voice default, hundredths of dB. */
    buffer.base=pool_allocate(36,0x626f5344);
    if(!buffer.base)return (int32_t)0x8007000e;
    memset(memory+buffer.base,0,36);MEM32(buffer.base)=PILOT_AUDIO_BUFFER_VTABLE;MEM32(buffer.base+4)=1;
    int32_t hr=0;
    if(desc[2])hr=audio_set_data(&buffer,0,desc[2]);
    if(hr<0){pool_free(buffer.base);return hr;}
    audio_buffers[slot]=buffer;audio_ref(audio_device,1);MEM32(output)=buffer.base+28;
    fprintf(stderr,"[AUDIO BUFFER] iface=%08x bytes=%u format=PCM%u channels=%u rate=%u\n",buffer.base+28,desc[2],bits,channels,rate);
    return 0;
}
#ifdef PILOT_AUDIO_DSP
static int32_t audio_download_effects(const uint32_t *a)
{
    audio_device_check(a[0]);
    if(a[4])audio_pointer(a[4],4);
    const char *error=NULL;
    PilotDspImage *image=pilot_dsp_decode(audio_pointer(a[1],a[2]),a[2],PILOT_AUDIO_DSP_SEED,&error);
    if(!image)stop(error?error:"DSP image decode failed",a[2]);
    uint32_t location[2]={UINT32_MAX,UINT32_MAX};
    if(a[3])memcpy(location,audio_pointer(a[3],8),8);
    fprintf(stderr,"[AUDIO EFFECTS LOCATION] reverb=%08x crosstalk=%08x\n",location[0],location[1]);
    uint32_t storage=0,bytes=pilot_dsp_storage_bytes(image),offset=pilot_dsp_descriptor_offset(image);
    if(vm_allocate(&storage,0,&bytes,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE)){pilot_dsp_image_destroy(image);return (int32_t)0x8007000e;}
    PilotDsp *graph=pilot_dsp_create(image,memory+storage,storage);pilot_dsp_image_destroy(image);
    uint32_t count=MEM32(storage+offset);
    /* Original location setter stores both values verbatim (or -1 for NULL).
     * These select subsequent parameter services, not execution of the graph.
     * Validate them when such a consuming service is qualified, not on upload. */
    int32_t hr=graph?pilot_audio_effects_install(graph):(int32_t)0x8007000e;
    if(hr<0){pilot_dsp_destroy(graph);uint32_t zero=0;if(vm_free(&storage,&zero,MEM_RELEASE))stop("effects install rollback failed",storage);return hr;}
    /* The audio thread now owns graph execution; the previous graph has no
     * remaining processing reader. Its descriptor lifetime ends on replacement. */
    audio_effects_free();audio_effects=graph;audio_effects_storage=storage;
    memcpy(audio_effects_location,location,8);if(a[4])MEM32(a[4])=storage+offset;
    fprintf(stderr,"[AUDIO EFFECTS] installed real GP graph: effects=%u storage=%u descriptor=%08x\n",count,bytes,storage+offset);
    return 0;
}
#endif
void pilot_audio_dispatch(unsigned entry)
{
    if(entry>=PILOT_AUDIO_COUNT)stop("unknown audio entry",entry);
    const uint32_t *a=audio_pointer(g_esp+4,pilot_audio_calls[entry].stack_bytes);
    int32_t result=0;
    AcquireSRWLockExclusive(&audio_lock);
    fprintf(stderr,"[AUDIO] %s return=%08x",pilot_audio_calls[entry].name,MEM32(g_esp));
    for(unsigned i=0;i<pilot_audio_calls[entry].stack_bytes/4;++i)fprintf(stderr," %08x",a[i]);
    fprintf(stderr,"\n");
    if(entry==PILOT_AUDIO_DirectSoundCreate) {
        audio_pointer(a[1],4);
        if(a[0] || a[2])stop("unqualified DirectSound device selection/aggregation",a[0]);
        if(audio_device)audio_ref(audio_device,1);
        else {
            uint32_t base=pool_allocate(44,0x626f5344);
            if(!base)result=(int32_t)0x8007000e;
            else {
                result=pilot_audio_open();
                if(result<0)pool_free(base);
                else {audio_device=base;memset(memory+base,0,44);MEM32(base)=PILOT_AUDIO_DEVICE_VTABLE;MEM32(base+4)=1;}
            }
        }
        if(result>=0){MEM32(a[1])=audio_device+8;fprintf(stderr,"[AUDIO DEVICE] real native output; 48000 Hz stereo PCM\n");}
    } else if(entry==PILOT_AUDIO_IDirectSound_Release) {
        audio_device_check(a[0]);result=audio_ref(audio_device,0);
    } else if(entry==PILOT_AUDIO_ObjectAddRef)result=audio_ref(a[0],1);
    else if(entry==PILOT_AUDIO_IDirectSound_CreateSoundBuffer) {
        audio_device_check(a[0]);if(a[3])stop("audio buffer aggregation is unqualified",a[3]);
        result=audio_create_buffer(a[1],a[2]);
    } else if(entry==PILOT_AUDIO_IDirectSound_DownloadEffectsImage) {
#ifdef PILOT_AUDIO_DSP
        result=audio_download_effects(a);
#else
        stop("Xbox DSP effects image needs a real native effect implementation",a[2]);
#endif
    } else {
        struct PilotAudioBuffer *b=audio_buffer(a[0]);
        if(entry==PILOT_AUDIO_IDirectSoundBuffer_Release)result=audio_ref(b->base,0);
        else if(entry==PILOT_AUDIO_IDirectSoundBuffer_SetBufferData)result=audio_set_data(b,a[1],a[2]);
        else {
            if(!b->host && entry!=PILOT_AUDIO_IDirectSoundBuffer_SetVolume &&
               entry!=PILOT_AUDIO_IDirectSoundBuffer_SetHeadroom && entry!=PILOT_AUDIO_IDirectSoundBuffer_SetFrequency &&
               entry!=PILOT_AUDIO_IDirectSoundBuffer_SetPitch)stop("audio operation requires buffer storage",a[0]);
            switch(entry) {
            case PILOT_AUDIO_IDirectSoundBuffer_Play:
                if(a[1] || a[2] || (a[3]&~1u) || b->locked)stop("unqualified sound play arguments",a[3]);
                result=pilot_audio_buffer_copy(b->host,0,memory+b->data,b->bytes,0);
                if(result>=0)result=pilot_audio_buffer_play(b->host,a[3]&1);break;
            case PILOT_AUDIO_IDirectSoundBuffer_Stop:result=pilot_audio_buffer_stop(b->host);break;
            case PILOT_AUDIO_IDirectSoundBuffer_GetStatus: {
                uint32_t status;audio_pointer(a[1],4);result=pilot_audio_buffer_status(b->host,&status);
                if(result>=0)MEM32(a[1])=status;break;
            }
            case PILOT_AUDIO_IDirectSoundBuffer_GetCurrentPosition: {
                uint32_t play,write;result=pilot_audio_buffer_position(b->host,&play,&write);
                if(result>=0){if(a[1]){audio_pointer(a[1],4);MEM32(a[1])=play;}if(a[2]){audio_pointer(a[2],4);MEM32(a[2])=write;}}break;
            }
            case PILOT_AUDIO_IDirectSoundBuffer_SetCurrentPosition:result=pilot_audio_buffer_seek(b->host,a[1]);break;
            case PILOT_AUDIO_IDirectSoundBuffer_SetVolume:
                if((int32_t)a[1]>0 || (int32_t)a[1]<-10000)result=(int32_t)0x88780032;
                else {int32_t old=b->volume;b->volume=(int32_t)a[1];result=audio_gain(b);if(result<0)b->volume=old;}break;
            case PILOT_AUDIO_IDirectSoundBuffer_SetHeadroom:
                if(a[1]>10000)result=(int32_t)0x88780032;
                else {uint32_t old=b->headroom;b->headroom=a[1];result=audio_gain(b);if(result<0)b->headroom=old;}break;
            case PILOT_AUDIO_IDirectSoundBuffer_SetFrequency:result=audio_frequency(b,a[1]?a[1]:b->rate);break;
            case PILOT_AUDIO_IDirectSoundBuffer_SetPitch: {
                int32_t pitch=(int32_t)a[1];
                if(pitch< -32768 || pitch>32767)result=(int32_t)0x88780032;
                else result=audio_frequency(b,(uint32_t)(48000.0*pow(2.0,pitch/4096.0)+0.5));break;
            }
            case PILOT_AUDIO_IDirectSoundBuffer_SetLoopRegion:
                if(a[1] || (a[2] && a[2]!=b->bytes))stop("partial audio loop regions are unqualified",a[1]);
                /* Full-buffer region is enforced by native Play's looping flag. */
                break;
            case PILOT_AUDIO_IDirectSoundBuffer_Lock: {
                uint32_t offset=a[1],bytes=a[2];
                if(b->locked || (a[7]&~3u))stop("unqualified nested audio lock/flags",a[7]);
                audio_pointer(a[3],4);audio_pointer(a[4],4);
                if((!a[5])!=(!a[6]))stop("incomplete second audio lock output",a[5]);
                if(a[5]){audio_pointer(a[5],4);audio_pointer(a[6],4);}
                if(a[7]&1){uint32_t play;result=pilot_audio_buffer_position(b->host,&play,&offset);}
                if(a[7]&2)bytes=b->bytes;
                if(result<0)break;
                if(offset>=b->bytes || bytes>b->bytes || !bytes){result=(int32_t)0x88780032;break;}
                uint32_t first=bytes<b->bytes-offset?bytes:b->bytes-offset;
                if(bytes>first && !a[5])stop("wrapping audio lock without second output",offset);
                b->locked=1;b->lock_offset=offset;b->lock_first=first;b->lock_second=bytes-first;
                MEM32(a[3])=b->data+offset;MEM32(a[4])=first;
                if(a[5]){MEM32(a[5])=bytes>first?b->data:0;MEM32(a[6])=bytes-first;}break;
            }
            case PILOT_AUDIO_IDirectSoundBuffer_Unlock:
                if(!b->locked || a[1]!=b->data+b->lock_offset || a[2]!=b->lock_first ||
                   a[3]!=(b->lock_second?b->data:0) || a[4]!=b->lock_second)stop("audio unlock differs from owned lock",a[0]);
                result=pilot_audio_buffer_copy(b->host,b->lock_offset,memory+a[1],a[2],0);
                if(result>=0 && a[4])result=pilot_audio_buffer_copy(b->host,0,memory+a[3],a[4],0);
                if(result>=0)b->locked=0;break;
            default:stop("unqualified native audio service",entry);
            }
        }
    }
    ReleaseSRWLockExclusive(&audio_lock);
    g_eax=(uint32_t)result;g_esp+=4+pilot_audio_calls[entry].stack_bytes;
}
