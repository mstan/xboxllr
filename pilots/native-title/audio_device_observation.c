#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "audio_device_observation.h"
#include "audio_device_lle.h"
#include "io_device_lle.h"

static struct {
    PilotPcmCapture *pcm,*spdif;
    uint64_t started,duration;
    int bound,finished;
    const char *(*finish)(void);
    char *stop_file;
    uint64_t next_poll;
} observation;

static const char *observe(void)
{
    if(observation.finished)return "original audio observation already finalized";
    uint64_t now=GetTickCount64(),elapsed=now-observation.started;
    int requested=0;
    if(observation.stop_file && now>=observation.next_poll) {
        observation.next_poll=now+250;
        DWORD attributes=GetFileAttributesA(observation.stop_file);
        if(attributes!=INVALID_FILE_ATTRIBUTES) {
            if(attributes&FILE_ATTRIBUTE_DIRECTORY)return "original observation stop request is a directory";
            requested=1;
        } else {
            DWORD error=GetLastError();
            if(error!=ERROR_FILE_NOT_FOUND && error!=ERROR_PATH_NOT_FOUND)
                return "original observation stop request lookup failed";
        }
    }
    if(elapsed<observation.duration && !requested)return NULL;
    observation.finished=1;
    /* The existing work dispatcher owns the CPU lock and invokes callbacks
     * serially. No audio consumer can change these buffers during persistence.
     * Keep both original device lifetimes registered until process disposal. */
    PilotAudioDeviceState state;pilot_audio_device_lle_state(&state);
    uint64_t pcm_bytes=0,pcm_stamps=0,spdif_bytes=0,spdif_stamps=0;
    pilot_pcm_capture_view(observation.pcm,NULL,&pcm_bytes,NULL,&pcm_stamps);
    pilot_pcm_capture_view(observation.spdif,NULL,&spdif_bytes,NULL,&spdif_stamps);
    const char *pcm_error=pilot_pcm_capture_finish(observation.pcm,0);
    const char *spdif_error=pilot_pcm_capture_finish(observation.spdif,0);
    const char *trace_error=pilot_audio_device_lle_trace_finish();
    const char *call_error=observation.finish?observation.finish():NULL;
    fprintf(stderr,"[OBSERVATION STOP] partial=1 status=%s host_ms=%llu ticks=%llu codec_frame=%llu pcm_bytes=%llu pcm_stamps=%llu spdif_bytes=%llu spdif_stamps=%llu owned=%u pcm_error=%s spdif_error=%s reason=%s\n",
        pcm_error||spdif_error||trace_error||call_error?"error":"ok",(unsigned long long)elapsed,
        (unsigned long long)state.clock_ticks,(unsigned long long)state.codec_frame,
        (unsigned long long)pcm_bytes,(unsigned long long)pcm_stamps,
        (unsigned long long)spdif_bytes,(unsigned long long)spdif_stamps,state.owned,
        pcm_error?pcm_error:"none",spdif_error?spdif_error:"none",requested?"request":"duration");
    fflush(stderr);
    if(trace_error)fprintf(stderr,"[AUDIO STAGE TRACE] error=%s\n",trace_error);
    if(call_error)fprintf(stderr,"[AUDIO CALL CAPTURE] error=%s\n",call_error);
    if(pcm_error || spdif_error)return pcm_error?pcm_error:spdif_error;
    if(trace_error)return trace_error;
    if(call_error)return call_error;
    ExitProcess(0);
}

const char *pilot_audio_device_observation_on_finish(const char *(*finish)(void))
{
    if(!finish || observation.bound || observation.finished || observation.finish)
        return "original observation finish callback requires an unbound observer";
    observation.finish=finish;return NULL;
}

const char *pilot_audio_device_observation_stop_file(const char *path)
{
    if(!path || !*path)return NULL;
    if(observation.bound || observation.finished || observation.stop_file)
        return "original observation stop file requires an unbound observer";
    if(GetFileAttributesA(path)!=INVALID_FILE_ATTRIBUTES)
        return "original observation stop file already exists";
    DWORD error=GetLastError();
    if(error!=ERROR_FILE_NOT_FOUND && error!=ERROR_PATH_NOT_FOUND)
        return "original observation stop file preflight failed";
    size_t size=strlen(path)+1;observation.stop_file=malloc(size);
    if(!observation.stop_file)return "original observation stop file allocation failed";
    memcpy(observation.stop_file,path,size);return NULL;
}

const char *pilot_audio_device_observation_start(const char *milliseconds,
    PilotPcmCapture *pcm,PilotPcmCapture *spdif)
{
    if(!milliseconds || !*milliseconds)return NULL;
    if(observation.bound)return "original audio observation already bound";
    uint64_t duration=0;
    for(const char *p=milliseconds;*p;++p) {
        if(*p<'0' || *p>'9' || duration>360000)
            return "original audio observation duration requires 1..3600000 decimal milliseconds";
        duration=duration*10+(unsigned)(*p-'0');
    }
    if(!duration || duration>3600000)
        return "original audio observation duration requires 1..3600000 decimal milliseconds";
    if(!pcm || !spdif || pcm==spdif || !pilot_audio_device_lle_consumer_attached(pcm) ||
       !pilot_audio_device_lle_spdif_consumer_attached(spdif))
        return "original audio observation requires both bound capture consumers";
    observation.pcm=pcm;observation.spdif=spdif;
    observation.duration=duration;observation.started=GetTickCount64();
    const char *error=pilot_io_device_work_bind(observe);
    if(error) {
        observation.pcm=observation.spdif=NULL;observation.duration=observation.started=0;
        return error;
    }
    observation.bound=1;
    return NULL;
}
