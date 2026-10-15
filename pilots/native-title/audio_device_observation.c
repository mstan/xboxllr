#include <windows.h>
#include <stdio.h>
#include "audio_device_observation.h"
#include "audio_device_lle.h"
#include "io_device_lle.h"

static struct {
    PilotPcmCapture *pcm,*spdif;
    uint64_t started,duration;
    int bound,finished;
} observation;

static const char *observe(void)
{
    if(observation.finished)return "original audio observation already finalized";
    uint64_t elapsed=GetTickCount64()-observation.started;
    if(elapsed<observation.duration)return NULL;
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
    fprintf(stderr,"[OBSERVATION STOP] partial=1 status=%s host_ms=%llu ticks=%llu codec_frame=%llu pcm_bytes=%llu pcm_stamps=%llu spdif_bytes=%llu spdif_stamps=%llu owned=%u pcm_error=%s spdif_error=%s\n",
        pcm_error||spdif_error||trace_error?"error":"ok",(unsigned long long)elapsed,
        (unsigned long long)state.clock_ticks,(unsigned long long)state.codec_frame,
        (unsigned long long)pcm_bytes,(unsigned long long)pcm_stamps,
        (unsigned long long)spdif_bytes,(unsigned long long)spdif_stamps,state.owned,
        pcm_error?pcm_error:"none",spdif_error?spdif_error:"none");
    fflush(stderr);
    if(trace_error)fprintf(stderr,"[AUDIO STAGE TRACE] error=%s\n",trace_error);
    if(pcm_error || spdif_error)return pcm_error?pcm_error:spdif_error;
    if(trace_error)return trace_error;
    ExitProcess(0);
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
