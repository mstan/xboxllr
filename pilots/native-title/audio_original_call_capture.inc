/* Optional observations of unchanged original CPU audio APIs.
 * Caller stack/output words and passive device counters only: no object/source reads,
 * device service, clock advancement, original-call substitution or ownership.
 * The existing sole original CPU owner serializes these calls. The journal
 * remains a diagnostic prefix, including at a host observation cutoff. */
#include "pilot_audio_bindings.h"
static struct {
    FILE *file;
    uint64_t sequence,records,active;
    int finished;
} original_audio_calls;
static const char *original_audio_call_record(const PilotIoCallDiagnosticEvent *event);

static const char *original_audio_call_capture_start(const char *path)
{
    if(!path || !*path)return NULL;
    if(original_audio_calls.file || original_audio_calls.finished)
        return "original audio call capture already started";
    original_audio_calls.file=fopen(path,"wbx");
    if(!original_audio_calls.file)return "original audio call capture exclusive create failed";
    if(fprintf(original_audio_calls.file,"{\"schema\":\"xboxlle.original-audio-calls.v1\",\"partial\":true,\"scope\":\"original CPU API entry/return; passive device counters\"}\n")<0 ||
       fflush(original_audio_calls.file))return "original audio call capture header failed";
    PilotIoDiagnosticCall calls[PILOT_AUDIO_COUNT];
    for(unsigned i=0;i<PILOT_AUDIO_COUNT;++i) {
        calls[i].entry=pilot_audio_calls[i].address;calls[i].bytes=pilot_audio_calls[i].bytes;
        calls[i].stack_bytes=pilot_audio_calls[i].stack_bytes;calls[i].sha256=pilot_audio_calls[i].sha256;
    }
    return pilot_io_device_diagnostic_calls_bind(calls,PILOT_AUDIO_COUNT,original_audio_call_record);
}

static const char *original_audio_call_record(const PilotIoCallDiagnosticEvent *event)
{
    unsigned entry=event->index;int returning=event->kind==PILOT_IO_DIAGNOSTIC_RETURN;
    if(original_audio_calls.finished || entry>=PILOT_AUDIO_COUNT ||
       original_audio_calls.records>=65536)
        return "original audio call capture invalid entry or exhausted bound";
    if(returning && !original_audio_calls.active)return "original audio call capture unmatched return";
    PilotAudioDeviceState state;pilot_audio_device_lle_state(&state);
    FILE *file=original_audio_calls.file;
    int result=fprintf(file,"{\"phase\":\"%s\",\"id\":%llu,\"entry\":%u,\"name\":\"%s\",\"address\":%u,\"thread\":%u,\"esp\":%u,\"eax\":%u,\"ecx\":%u,\"ticks\":%llu,\"lifetime\":%llu,\"fences\":%llu,\"pcm_words\":%llu,\"spdif_words\":%llu,\"args\":[",
        returning?"return":"enter",(unsigned long long)event->id,entry,pilot_audio_calls[entry].name,
        pilot_audio_calls[entry].address,event->thread,event->regs[4],event->regs[0],event->regs[1],
        (unsigned long long)state.clock_ticks,(unsigned long long)state.lifetime_id,
        (unsigned long long)pilot_audio_device_lle_fences_published(),
        (unsigned long long)state.pcm_words,(unsigned long long)state.spdif_words);
    if(!returning)for(unsigned i=0;i<pilot_audio_calls[entry].stack_bytes/4;++i)
        if(fprintf(file,"%s%u",i?",":"",event->args[i])<0)result=-1;
    if(fprintf(file,"],\"outputs\":[")<0)result=-1;
    if(returning && !event->regs[0]) {
        unsigned mask=entry==PILOT_AUDIO_IDirectSoundBuffer_GetStatus?2:
            entry==PILOT_AUDIO_IDirectSoundBuffer_GetCurrentPosition?6:
            entry==PILOT_AUDIO_IDirectSound_CreateSoundBuffer?4:
            entry==PILOT_AUDIO_DirectSoundCreate?2:0;
        unsigned outputs=0;
        for(unsigned i=0;i<5;++i)if((mask&(1u<<i)) && event->args[i]) {
            uint32_t value;
            if(!pilot_io_device_diagnostic_read(event->args[i],&value,4,0))
                return "original audio call output observation unavailable";
            if(fprintf(file,"%s{\"arg\":%u,\"value\":%u}",outputs++?",":"",i,value)<0)result=-1;
        }
    }
    if(result<0 || fprintf(file,"]}\n")<0 || fflush(file) || ferror(file))
        return "original audio call capture write failed";
    ++original_audio_calls.records;
    if(returning)--original_audio_calls.active;
    else {++original_audio_calls.active;++original_audio_calls.sequence;}
    return NULL;
}
static const char *original_audio_call_capture_finish(void)
{
    if(!original_audio_calls.file || original_audio_calls.finished)return NULL;
    original_audio_calls.finished=1;
    FILE *file=original_audio_calls.file;
    int result=fprintf(file,"{\"footer\":true,\"partial\":true,\"records\":%llu,\"calls\":%llu,\"active_calls\":%llu}\n",
        (unsigned long long)original_audio_calls.records,(unsigned long long)original_audio_calls.sequence,
        (unsigned long long)original_audio_calls.active);
    int flushed=fflush(file),failed=ferror(file),closed=fclose(file);original_audio_calls.file=NULL;
    if(result<0 || flushed || failed || closed)return "original audio call capture persistence failed";
    return NULL;
}
