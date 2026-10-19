/* Build-selected Xbox DirectSound owner. Guest interfaces retain their Xbox
 * offsets; native COM pointers and transport mechanics never enter guest RAM.
 * Unqualified effect/format/routing requests stop before returning success. */
#include <math.h>
#include "audio_backend.h"
#include "pilot_audio_bindings.h"
/* Previously generated headers use this same qualified object adapter without
 * named layout constants. Partial new metadata must not silently inherit it. */
#if !defined(PILOT_AUDIO_DEVICE_INTERFACE_OFFSET) && !defined(PILOT_AUDIO_BUFFER_INTERFACE_OFFSET) && !defined(PILOT_AUDIO_BUFFER_DESCRIPTOR_BYTES)
#define PILOT_AUDIO_DEVICE_INTERFACE_OFFSET 8u
#define PILOT_AUDIO_BUFFER_INTERFACE_OFFSET 28u
#define PILOT_AUDIO_BUFFER_DESCRIPTOR_BYTES 24u
#endif
#if !defined(PILOT_AUDIO_DEVICE_INTERFACE_OFFSET) || !defined(PILOT_AUDIO_BUFFER_INTERFACE_OFFSET) || !defined(PILOT_AUDIO_BUFFER_DESCRIPTOR_BYTES)
#error Incomplete native audio object layout
#endif
#if PILOT_AUDIO_DEVICE_INTERFACE_OFFSET != 8 || PILOT_AUDIO_BUFFER_INTERFACE_OFFSET != 28 || PILOT_AUDIO_BUFFER_DESCRIPTOR_BYTES != 24
#error Unsupported native audio object adapter
#endif
#ifndef PILOT_AUDIO_HAS_DSP
#ifdef PILOT_AUDIO_DSP_SEED
#define PILOT_AUDIO_HAS_DSP 1
#else
#define PILOT_AUDIO_HAS_DSP 0
#endif
#endif
#ifndef PILOT_AUDIO_HAS_MIXBIN_HEADROOM
#define PILOT_AUDIO_HAS_MIXBIN_HEADROOM 0
#endif
#ifndef PILOT_AUDIO_HAS_NOTIFICATIONS
#define PILOT_AUDIO_HAS_NOTIFICATIONS 0
#endif
#ifdef PILOT_AUDIO_ADPCM
#include "xbox_adpcm.h"
#endif
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
#ifdef PILOT_AUDIO_REFERENCE
#include "audio_reference_clock.inc"
#endif
#ifdef PILOT_SCHEDULING_LLE
/* Original threads serialize before any host audio/VM lock. The backend's
 * completion callbacks consume host-owned storage and never enter this gate. */
static uint32_t audio_original_gate;
static RECOMP_TLS unsigned audio_original_gate_depth,audio_original_dispatch_depth;
static const char *audio_original_enter(void)
{
    if(!audio_original_gate || !pilot_io_device_thread_component_active() || audio_original_gate_depth)
        return "original audio gate outside a nonrecursive component";
    const char *error=native_component_original_enter();
    if(error)return error;
    if(MEM32(audio_original_gate+20)!=1 || MEM32(audio_original_gate+24)!=MEM32(0x8003da64u))
        return "original audio gate lost its actual thread owner";
    audio_original_gate_depth=1;return NULL;
}
static const char *audio_original_leave(void)
{
    if(audio_original_gate_depth!=1 || audio_original_dispatch_depth)
        return "original audio gate release before native dispatch completion";
    const char *error=native_component_original_leave();
    if(!error)audio_original_gate_depth=0;
    return error;
}
#endif
static uint32_t audio_device;
struct PilotAudioBuffer {
    uint32_t base,data,bytes,owned,channels,rate,bits,headroom,frequency;
    uint32_t format,block_align;
    int32_t volume;
    void *host;
    uint32_t lock_offset,lock_first,lock_second;
    uint32_t mix_count,mix_bins[8];
    int32_t mix_volumes[32];
    int locked;
#if PILOT_AUDIO_HAS_NOTIFICATIONS
    unsigned notification_enabled,notification_count;
    struct PilotAudioNotification notifications[64];
#endif
};
static struct PilotAudioBuffer audio_buffers[256];
#include "audio_configuration_capture.inc"
static void *audio_pointer(uint32_t address,uint32_t bytes)
{
    if(!address || (uint64_t)address+bytes>0x100000000ull)stop("invalid audio guest range",address);
    return memory+address;
}
static void audio_device_check(uint32_t iface)
{
    if(!audio_device || iface!=audio_device+PILOT_AUDIO_DEVICE_INTERFACE_OFFSET)stop("unowned audio device",iface);
}
static struct PilotAudioBuffer *audio_buffer(uint32_t iface)
{
    for(unsigned i=0;i<256;++i)if(audio_buffers[i].base && iface==audio_buffers[i].base+PILOT_AUDIO_BUFFER_INTERFACE_OFFSET)return &audio_buffers[i];
    stop("unowned audio buffer",iface);
}
static uint32_t audio_ref(uint32_t base,int acquire)
{
    struct PilotAudioBuffer *b=NULL;
    if(base!=audio_device || !base)b=audio_buffer(base+PILOT_AUDIO_BUFFER_INTERFACE_OFFSET);
    uint32_t refs=MEM32(base+4);
    if(!refs || (acquire && refs==UINT32_MAX))stop("invalid audio reference lifetime",base);
    refs=acquire?refs+1:refs-1;MEM32(base+4)=refs;
    if(refs)return refs;
    if(b) {
        if(b->locked)stop("releasing locked audio buffer",base);
        pilot_audio_buffer_destroy(b->host);
#if PILOT_AUDIO_HAS_NOTIFICATIONS
        for(unsigned i=0;i<b->notification_count;++i)
            if(!CloseHandle(b->notifications[i].event))stop("audio notification owner close failed",base);
#endif
        if(b->owned){uint32_t bytes=0;if(vm_free(&b->data,&bytes,MEM_RELEASE))stop("audio data release failed",base);}
        pool_free(base);memset(b,0,sizeof(*b));audio_ref(audio_device,0);
    } else {
#ifdef PILOT_AUDIO_REFERENCE
        audio_reference_clock_close();
#endif
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
static int32_t audio_route_host(struct PilotAudioBuffer *b,void *host)
{
#ifdef PILOT_AUDIO_DSP
    int32_t volumes[8];
    for(unsigned i=0;i<b->mix_count;++i)volumes[i]=b->mix_volumes[b->mix_bins[i]];
    return host?pilot_audio_buffer_mixbins(host,b->mix_count,b->mix_bins,volumes):0;
#else
    (void)b;(void)host;return 0; /* DSOUND mode admits default routing only. */
#endif
}
static int32_t audio_set_mix(struct PilotAudioBuffer *b,uint32_t address,int volumes_only)
{
#ifdef PILOT_AUDIO_DSP
    struct PilotAudioBuffer updated=*b;
    if(!address) {
        if(volumes_only)return (int32_t)0x88780032;
        updated.mix_count=2;updated.mix_bins[0]=0;updated.mix_bins[1]=1;
        updated.mix_volumes[0]=updated.mix_volumes[1]=0;
    } else {
        const uint32_t *desc=audio_pointer(address,8);unsigned count=desc[0];
        if(count>(volumes_only?32u:8u))return (int32_t)0x88780032;
        const uint32_t *pairs=count?audio_pointer(desc[1],count*8):NULL;
        if(!volumes_only)updated.mix_count=count;
        for(unsigned i=0;i<count;++i) {
            uint32_t bin=pairs[i*2];int32_t volume=(int32_t)pairs[i*2+1];
            if(bin>=32 || volume>0 || volume< -10000)return (int32_t)0x88780032;
            if(!volumes_only)updated.mix_bins[i]=bin;
            updated.mix_volumes[bin]=volume;
        }
    }
    int32_t hr=audio_route_host(&updated,b->host);
    if(hr>=0)*b=updated;return hr;
#else
    stop("custom mix bins require the native DSP mixer",address);
#endif
}
static uint32_t audio_pcm_bytes(const struct PilotAudioBuffer *b,uint32_t bytes)
{
    return b->format==0x69 ? bytes/b->block_align*64*b->channels*2 : bytes;
}
static uint32_t audio_source_bytes(const struct PilotAudioBuffer *b,uint32_t bytes)
{
    return b->format==0x69 ? bytes/(64*b->channels*2)*b->block_align : bytes;
}
static int32_t audio_upload(struct PilotAudioBuffer *b,void *host,const void *data,uint32_t offset,uint32_t bytes)
{
    if(b->format!=0x69)return pilot_audio_buffer_copy(host,offset,(char*)data+offset,bytes,0);
#ifdef PILOT_AUDIO_ADPCM
    /* A partial encoded write changes decoding through the end of its block.
     * Decode complete affected blocks, then publish one contiguous PCM update. */
    uint32_t first=offset/b->block_align,last=(offset+bytes+b->block_align-1)/b->block_align;
    uint32_t samples=(last-first)*64*b->channels;
    int16_t *decoded=malloc(samples*2);if(!decoded)return (int32_t)0x8007000e;
    for(uint32_t block=first;block<last;++block)
        if(!xbox_adpcm_decode_block((const uint8_t*)data+block*b->block_align,b->block_align,b->channels,
                                   decoded+(block-first)*64*b->channels,64*b->channels)) {
            free(decoded);return (int32_t)0x88780032;
        }
    int32_t hr=pilot_audio_buffer_copy(host,first*64*b->channels*2,decoded,samples*2,0);
    free(decoded);return hr;
#else
    stop("Xbox ADPCM decoder is not selected",b->format);
#endif
}
static int32_t audio_set_data(struct PilotAudioBuffer *b,uint32_t source,uint32_t bytes)
{
    if(b->locked)stop("replacing locked audio data",b->base);
    if(!source && !bytes) {
        /* Original SetBufferData stops the voice before clearing storage. The
         * native destroy joins audio readers/callbacks before freeing PCM;
         * the guest object, format, gain and routing survive for rebinding. */
        static unsigned reported;
        if(reported++<8 || pilot_verbose)
            fprintf(stderr,"[AUDIO DETACH] iface=%08x data=%08x bytes=%u owned=%u\n",b->base+PILOT_AUDIO_BUFFER_INTERFACE_OFFSET,b->data,b->bytes,b->owned);
        pilot_audio_buffer_destroy(b->host);
        if(b->owned){uint32_t zero=0;if(vm_free(&b->data,&zero,MEM_RELEASE))stop("audio detach release failed",b->data);}
        b->host=NULL;b->data=b->bytes=b->owned=0;
        return 0;
    }
    if(source && source==b->data && bytes==b->bytes)return 0;
    unsigned align=b->block_align;
    if(!bytes || bytes>64u*1024*1024 || bytes%align)stop("unqualified audio buffer extent",bytes);
    uint32_t data=source,allocated=bytes;
    if(source)audio_pointer(source,bytes);
    else if(vm_allocate(&data,0,&allocated,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE))return (int32_t)0x8007000e;
    if(!source && b->bits==8)memset(memory+data,128,bytes);
    void *host=NULL;
    int32_t hr=pilot_audio_buffer_create(audio_pcm_bytes(b,bytes),b->channels,b->rate,b->format==0x69?16:b->bits,&host);
    if(hr>=0)hr=audio_upload(b,host,memory+data,0,bytes);
    if(hr>=0) {
        int64_t gain=(int64_t)b->volume-b->headroom;
        hr=pilot_audio_buffer_volume(host,(int32_t)(gain< -10000?-10000:gain));
    }
    if(hr>=0)hr=pilot_audio_buffer_frequency(host,b->frequency);
    if(hr>=0)hr=audio_route_host(b,host);
#if PILOT_AUDIO_HAS_NOTIFICATIONS && defined(PILOT_AUDIO_DSP) && !defined(PILOT_AUDIO_REFERENCE)
    if(hr>=0 && b->notification_count)hr=pilot_audio_buffer_notifications(host,b->notification_count,b->notifications);
#endif
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
    const uint32_t *desc=audio_pointer(descriptor,PILOT_AUDIO_BUFFER_DESCRIPTOR_BYTES);audio_pointer(output,4);
    uint32_t allowed=0x20u|0x80u|0x40000u;
#if PILOT_AUDIO_HAS_NOTIFICATIONS && defined(PILOT_AUDIO_DSP) && !defined(PILOT_AUDIO_REFERENCE)
    allowed|=0x100u;
#endif
    if(desc[0]!=PILOT_AUDIO_BUFFER_DESCRIPTOR_BYTES || desc[1]&~allowed || desc[5])
        stop("unqualified sound buffer flags/mix bins",desc[1]);
    uint32_t format=desc[3];audio_pointer(format,18);
    unsigned tag=MEM16(format),channels=MEM16(format+2),rate=MEM32(format+4),bits=MEM16(format+14),align=MEM16(format+12);
    int supported=tag==1 && (bits==8 || bits==16) && align==channels*(bits/8) && MEM32(format+8)==rate*align && !MEM16(format+16);
#ifdef PILOT_AUDIO_ADPCM
    if(tag==0x69) {
        audio_pointer(format,20);
        supported=bits==4 && align==36*channels && MEM32(format+8)==rate*align/64 && MEM16(format+16)==2;
        /* Original MCPX conversions use a fixed 64 samples/block. Blinx leaves
         * the extension's samples-per-block word zero; it is not authoritative. */
    }
#endif
    if(!supported || (channels!=1 && channels!=2) || rate<100 || rate>200000) {
        fprintf(stderr,"[AUDIO FORMAT] return=%08x flags=%08x bytes=%u tag=%x channels=%u rate=%u average=%u align=%u bits=%u extra=%u samples=%u\n",
            MEM32(g_esp),desc[1],desc[2],MEM16(format),channels,rate,MEM32(format+8),MEM16(format+12),bits,MEM16(format+16),
            MEM16(format+16)==2?MEM16(format+18):0);
        stop("unqualified sound buffer format",MEM16(format));
    }
    unsigned slot;for(slot=0;slot<256 && audio_buffers[slot].base;++slot){}
    if(slot==256)return (int32_t)0x8007000e;
    struct PilotAudioBuffer buffer={0};buffer.channels=channels;buffer.rate=rate;buffer.bits=bits;buffer.frequency=rate;
#if PILOT_AUDIO_HAS_NOTIFICATIONS
    buffer.notification_enabled=(desc[1]&0x100)!=0;
#endif
    buffer.format=tag;buffer.block_align=align;
    buffer.mix_count=2;buffer.mix_bins[1]=1;
    if(desc[4]) {int32_t hr=audio_set_mix(&buffer,desc[4],0);if(hr<0)return hr;}
    buffer.headroom=600; /* Original 2D voice default, hundredths of dB. */
    buffer.base=pool_allocate(36,0x626f5344);
    if(!buffer.base)return (int32_t)0x8007000e;
    memset(memory+buffer.base,0,36);MEM32(buffer.base)=PILOT_AUDIO_BUFFER_VTABLE;MEM32(buffer.base+4)=1;
    int32_t hr=0;
    if(desc[2])hr=audio_set_data(&buffer,0,desc[2]);
    if(hr<0){pool_free(buffer.base);return hr;}
    audio_buffers[slot]=buffer;audio_ref(audio_device,1);MEM32(output)=buffer.base+PILOT_AUDIO_BUFFER_INTERFACE_OFFSET;
    fprintf(stderr,"[AUDIO BUFFER] iface=%08x bytes=%u format=%s%u channels=%u rate=%u\n",buffer.base+PILOT_AUDIO_BUFFER_INTERFACE_OFFSET,desc[2],tag==0x69?"XBOX_ADPCM":"PCM",bits,channels,rate);
    return 0;
}
#include "audio_notification_owner.inc"
#if defined(PILOT_AUDIO_DSP) && PILOT_AUDIO_HAS_DSP
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
    if(vm_allocate(&storage,0,&bytes,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE)){pilot_dsp_image_destroy(image);stop("effects allocation failure continuation is unqualified",(uint32_t)0x8007000e);}
    PilotDsp *graph=pilot_dsp_create(image,memory+storage,storage);pilot_dsp_image_destroy(image);
    uint32_t count=MEM32(storage+offset);
    /* Original location setter stores both values verbatim (or -1 for NULL).
     * These select subsequent parameter services, not execution of the graph.
     * Validate them when such a consuming service is qualified, not on upload. */
    int32_t hr=graph?pilot_audio_effects_install(graph):(int32_t)0x8007000e;
    if(hr<0){pilot_dsp_destroy(graph);uint32_t zero=0;if(vm_free(&storage,&zero,MEM_RELEASE))stop("effects install rollback failed",storage);stop("effects install failure continuation is unqualified",(uint32_t)hr);}
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
#ifdef PILOT_SCHEDULING_LLE
    if(usb_device_callback_depth || dispatch_in_dpc)
        stop("original callback cannot acquire native audio owner locks",entry);
    if(audio_original_dispatch_depth ||
       (pilot_io_device_thread_component_active() && audio_original_gate_depth!=1))
        stop("native audio dispatch requires its nonrecursive original gate",entry);
    ++audio_original_dispatch_depth;
#endif
    const uint32_t *a=audio_pointer(g_esp+4,pilot_audio_calls[entry].stack_bytes);
    int32_t result=0;
    AcquireSRWLockExclusive(&audio_lock);
    int configuration=audio_configuration_enabled();
    struct PilotAudioBuffer before_release;unsigned configuration_slot=0;
    if(pilot_verbose){fprintf(stderr,"[AUDIO] %s return=%08x",pilot_audio_calls[entry].name,MEM32(g_esp));
    for(unsigned i=0;i<pilot_audio_calls[entry].stack_bytes/4;++i)fprintf(stderr," %08x",a[i]);
    fprintf(stderr,"\n");}
    if(entry==PILOT_AUDIO_DirectSoundCreate) {
        audio_pointer(a[1],4);
        if(a[0] || a[2])stop("unqualified DirectSound device selection/aggregation",a[0]);
        if(audio_device)audio_ref(audio_device,1);
        else {
            uint32_t base=pool_allocate(44,0x626f5344);
            if(!base)result=(int32_t)0x8007000e;
            else {
                result=pilot_audio_open();
#ifdef PILOT_AUDIO_REFERENCE
                if(result>=0)audio_reference_clock_open();
#endif
                if(result<0)pool_free(base);
                else {audio_device=base;memset(memory+base,0,44);MEM32(base)=PILOT_AUDIO_DEVICE_VTABLE;MEM32(base+4)=1;}
            }
        }
        if(result>=0){MEM32(a[1])=audio_device+PILOT_AUDIO_DEVICE_INTERFACE_OFFSET;
#ifdef PILOT_AUDIO_REFERENCE
            fprintf(stderr,"[AUDIO DEVICE] caller-driven reference APU/GP; 48000 Hz software output\n");
#else
            fprintf(stderr,"[AUDIO DEVICE] real native output; 48000 Hz stereo PCM\n");
#endif
        }
    } else if(entry==PILOT_AUDIO_IDirectSound_Release) {
        audio_device_check(a[0]);result=audio_ref(audio_device,0);
    } else if(entry==PILOT_AUDIO_ObjectAddRef)result=audio_ref(a[0],1);
#if PILOT_AUDIO_HAS_MIXBIN_HEADROOM
    else if(entry==PILOT_AUDIO_IDirectSound_SetMixBinHeadroom) {
        audio_device_check(a[0]);
        if(a[1]>=32)stop("mix-bin headroom index outside qualified range",a[1]);
#ifdef PILOT_AUDIO_DSP
        result=pilot_audio_mixbin_headroom(a[1],a[2]);
#else
        stop("mix-bin headroom requires native multi-bin mixer",a[1]);
#endif
    }
#endif
    else if(entry==PILOT_AUDIO_IDirectSound_CreateSoundBuffer) {
        audio_device_check(a[0]);if(a[3])stop("audio buffer aggregation is unqualified",a[3]);
        result=audio_create_buffer(a[1],a[2]);
    } else if(entry==PILOT_AUDIO_IDirectSound_DownloadEffectsImage) {
#if defined(PILOT_AUDIO_DSP) && PILOT_AUDIO_HAS_DSP
        result=audio_download_effects(a);
#else
        stop("Xbox DSP effects image needs a real native effect implementation",a[2]);
#endif
    } else {
        struct PilotAudioBuffer *b=audio_buffer(a[0]);
        if(configuration && entry==PILOT_AUDIO_IDirectSoundBuffer_Release) {
            before_release=*b;configuration_slot=(unsigned)(b-audio_buffers);
        }
        if(entry==PILOT_AUDIO_IDirectSoundBuffer_Release)result=audio_ref(b->base,0);
#if PILOT_AUDIO_HAS_NOTIFICATIONS
        else if(entry==PILOT_AUDIO_IDirectSoundBuffer_SetNotificationPositions)result=audio_set_notifications(b,a[1],a[2]);
#endif
        else if(entry==PILOT_AUDIO_IDirectSoundBuffer_SetBufferData)result=audio_set_data(b,a[1],a[2]);
        else if(entry==PILOT_AUDIO_IDirectSoundBuffer_SetMixBins)result=audio_set_mix(b,a[1],0);
        else if(entry==PILOT_AUDIO_IDirectSoundBuffer_SetMixBinVolumes_8)result=audio_set_mix(b,a[1],1);
        else {
            if(!b->host && entry!=PILOT_AUDIO_IDirectSoundBuffer_SetVolume &&
               entry!=PILOT_AUDIO_IDirectSoundBuffer_SetHeadroom && entry!=PILOT_AUDIO_IDirectSoundBuffer_SetFrequency &&
               entry!=PILOT_AUDIO_IDirectSoundBuffer_SetPitch)stop("audio operation requires buffer storage",a[0]);
            switch(entry) {
            case PILOT_AUDIO_IDirectSoundBuffer_Play:
                if(a[1] || a[2] || (a[3]&~3u) || b->locked)stop("unqualified sound play arguments",a[3]);
                result=audio_upload(b,b->host,memory+b->data,0,b->bytes);
                if(result>=0 && (a[3]&2))result=pilot_audio_buffer_seek(b->host,0);
                if(result>=0)result=pilot_audio_buffer_play(b->host,a[3]&1);break;
            case PILOT_AUDIO_IDirectSoundBuffer_Stop:result=pilot_audio_buffer_stop(b->host);break;
            case PILOT_AUDIO_IDirectSoundBuffer_GetStatus: {
                uint32_t status;audio_pointer(a[1],4);result=pilot_audio_buffer_status(b->host,&status);
                if(result>=0)MEM32(a[1])=status;break;
            }
            case PILOT_AUDIO_IDirectSoundBuffer_GetCurrentPosition: {
                uint32_t play,write;result=pilot_audio_buffer_position(b->host,&play,&write);
                if(result>=0){if(a[1]){audio_pointer(a[1],4);MEM32(a[1])=audio_source_bytes(b,play);}if(a[2]){audio_pointer(a[2],4);MEM32(a[2])=audio_source_bytes(b,write);}}break;
            }
            case PILOT_AUDIO_IDirectSoundBuffer_SetCurrentPosition:
                result=a[1]>=b->bytes?(int32_t)0x88780032:pilot_audio_buffer_seek(b->host,audio_pcm_bytes(b,a[1]));break;
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
#ifdef PILOT_AUDIO_DSP
                if(a[1]>=b->bytes || a[2]>b->bytes-a[1])result=(int32_t)0x88780032;
                else {
                    uint32_t start=audio_pcm_bytes(b,a[1]),end=audio_pcm_bytes(b,a[2]?a[1]+a[2]:b->bytes);
                    result=end<=start?(int32_t)0x88780032:pilot_audio_buffer_loop_region(b->host,start,end-start);
                }
#else
                if(a[1] || (a[2] && a[2]!=b->bytes))stop("partial audio loop regions are unqualified",a[1]);
                /* Full-buffer region is enforced by native Play's looping flag. */
#endif
                break;
            case PILOT_AUDIO_IDirectSoundBuffer_Lock: {
                uint32_t offset=a[1],bytes=a[2];
                if(b->locked || (a[7]&~3u))stop("unqualified nested audio lock/flags",a[7]);
                audio_pointer(a[3],4);audio_pointer(a[4],4);
                if((!a[5])!=(!a[6]))stop("incomplete second audio lock output",a[5]);
                if(a[5]){audio_pointer(a[5],4);audio_pointer(a[6],4);}
                if(a[7]&1){uint32_t play;result=pilot_audio_buffer_position(b->host,&play,&offset);if(result>=0)offset=audio_source_bytes(b,offset);}
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
                result=audio_upload(b,b->host,memory+b->data,b->lock_offset,a[2]);
                if(result>=0 && a[4])result=audio_upload(b,b->host,memory+b->data,0,a[4]);
                if(result>=0)b->locked=0;break;
            default:stop("unqualified native audio service",entry);
            }
        }
    }
    if(configuration && result>=0) {
        if(entry==PILOT_AUDIO_IDirectSound_CreateSoundBuffer) {
            struct PilotAudioBuffer *b=audio_buffer(MEM32(a[2]));
            audio_configuration_event(b,(unsigned)(b-audio_buffers),entry,a,result,MEM32(a[1]+4));
        } else if(audio_configuration_selected(entry)) {
            struct PilotAudioBuffer *b=entry==PILOT_AUDIO_IDirectSoundBuffer_Release?&before_release:audio_buffer(a[0]);
            unsigned slot=entry==PILOT_AUDIO_IDirectSoundBuffer_Release?configuration_slot:(unsigned)(b-audio_buffers);
            audio_configuration_event(b,slot,entry,a,result,0);
        }
    }
    ReleaseSRWLockExclusive(&audio_lock);
#ifdef PILOT_SCHEDULING_LLE
    --audio_original_dispatch_depth;
#endif
    g_eax=(uint32_t)result;g_esp+=4+pilot_audio_calls[entry].stack_bytes;
}
