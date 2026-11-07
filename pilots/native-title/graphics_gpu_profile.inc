/* R09 private measurement adapter. Authored against our own adapter and the
 * D3D11 query API. No output I/O during the requested measurement window:
 * the harness creates the explicit stop file afterwards. Queries never wait
 * or flush. All calls run under the existing logical graphics owner. */
#ifdef PILOT_GPU_PROFILE
#include "gpu_profile.h"
enum { PILOT_GPU_MEASURE_CAPACITY=4096 };
static struct {
    PilotGpuProfile *gpu;
    PilotGpuProfileSample *samples;
    uint8_t partial[PILOT_GPU_MEASURE_CAPACITY];
    uint32_t presented_size[PILOT_GPU_MEASURE_CAPACITY][2];
    const char *output,*stop,*start;
    uint64_t attempts,admitted,count,query_errors,overflows,failed_presents;
    uint64_t source_payload_begin;
    unsigned frame,open,stopping,done;
} pilot_gpu_measure;
static void pilot_gpu_measure_collect(void)
{
    if(!pilot_gpu_measure.gpu)return;
    for(unsigned i=0;i<PILOT_GPU_PROFILE_FRAMES;++i) {
        PilotGpuProfileSample sample;
        int ready=pilot_gpu_profile_poll(pilot_gpu_measure.gpu,&sample);
        if(ready<0){++pilot_gpu_measure.query_errors;break;}
        if(!ready)break;
        if(sample.source_frame>=PILOT_GPU_MEASURE_CAPACITY){++pilot_gpu_measure.query_errors;continue;}
        if(pilot_gpu_measure.partial[sample.source_frame])sample.flags|=32u;
        if(pilot_gpu_measure.presented_size[sample.source_frame][0]) {
            sample.render_width=pilot_gpu_measure.presented_size[sample.source_frame][0];
            sample.render_height=pilot_gpu_measure.presented_size[sample.source_frame][1];
        }
        if(pilot_gpu_measure.count<PILOT_GPU_MEASURE_CAPACITY)
            pilot_gpu_measure.samples[pilot_gpu_measure.count++]=sample;
        else ++pilot_gpu_measure.overflows;
    }
}
static void pilot_gpu_measure_write(int closing)
{
    if(!pilot_gpu_measure.output || pilot_gpu_measure.done)return;
    unsigned pending=pilot_gpu_profile_pending(pilot_gpu_measure.gpu);
    if(!closing && pending)return;
#ifdef PILOT_D3D_PHOBOS
    pilot_native_cpu_report();
#endif
    uint64_t dropped=pilot_gpu_profile_dropped(pilot_gpu_measure.gpu);
    int complete=pilot_gpu_measure.gpu && pilot_gpu_measure.count>0 &&
        pilot_gpu_measure.count==pilot_gpu_measure.admitted && !pending && !dropped && !pilot_gpu_measure.query_errors &&
        !pilot_gpu_measure.overflows && !pilot_gpu_measure.failed_presents && !closing;
    FILE *file=fopen(pilot_gpu_measure.output,"wb");
    if(!file){fprintf(stderr,"[GPU TIMESTAMPS] capture output open failed; unavailable\n");pilot_gpu_measure.done=1;return;}
    int error=fprintf(file,"{\"schema\":\"xbox.gpu-frame-intervals.v1\",\"upload_coverage\":\"%s\",\"intervals_include_submission_starvation\":true,\"pass2_includes_swap_pacing\":true,\"partial_flag\":32}\n",
        pilot_resource_mirror_diagnostics?"native texture/vertex payload span arguments; excludes shader constants, generated indices and bus traffic":"unavailable: native source counters disabled")<0;
    for(uint64_t i=0;i<pilot_gpu_measure.count;++i) {
        const PilotGpuProfileSample *s=pilot_gpu_measure.samples+i;
        error|=fprintf(file,"{\"frame\":%llu,\"frequency\":%llu,\"native\":[%u,%u],\"render\":[%u,%u],\"flags\":%u,\"upload_bytes\":%llu,\"passes\":[",
            (unsigned long long)s->source_frame,(unsigned long long)s->frequency,s->native_width,s->native_height,
            s->render_width,s->render_height,s->flags,(unsigned long long)s->upload_bytes)<0;
        for(unsigned j=0;j<s->passes;++j)
            error|=fprintf(file,"%s[%u,%llu,%llu]",j?",":"",s->pass_id[j],
                (unsigned long long)s->begin[j],(unsigned long long)s->end[j])<0;
        error|=fprintf(file,"]}\n")<0;
    }
    error|=fprintf(file,"{\"footer\":true,\"interval_capture_complete\":%s,\"full_guide_qualified\":false,\"attempts\":%llu,\"admitted\":%llu,\"samples\":%llu,\"pending\":%u,\"dropped\":%llu,\"query_errors\":%llu,\"overflow\":%llu,\"failed_presents\":%llu}\n",
        complete?"true":"false",(unsigned long long)pilot_gpu_measure.attempts,
        (unsigned long long)pilot_gpu_measure.admitted,(unsigned long long)pilot_gpu_measure.count,pending,
        (unsigned long long)dropped,(unsigned long long)pilot_gpu_measure.query_errors,
        (unsigned long long)pilot_gpu_measure.overflows,(unsigned long long)pilot_gpu_measure.failed_presents)<0;
    error|=fclose(file)!=0;
    fprintf(stderr,"[GPU TIMESTAMPS] samples=%llu complete=%u write_error=%u native source payload spans only when counters enabled; intervals are not GPU-busy time\n",
        (unsigned long long)pilot_gpu_measure.count,complete,(unsigned)(error!=0));
    pilot_gpu_measure.done=1;
}
static void pilot_gpu_measure_begin(void)
{
    if(!pilot_gpu_measure.gpu || pilot_gpu_measure.open || pilot_gpu_measure.stopping || pilot_gpu_measure.done)return;
    if(pilot_gpu_measure.start) {
        if(GetFileAttributesA(pilot_gpu_measure.start)==INVALID_FILE_ATTRIBUTES)return;
        pilot_gpu_measure.start=NULL; /* External setup has finished; start once. */
#ifdef PILOT_D3D_PHOBOS
        pilot_native_cpu_begin();
#endif
    }
    if(pilot_gpu_measure.attempts==PILOT_GPU_MEASURE_CAPACITY){
        ++pilot_gpu_measure.overflows;pilot_gpu_measure.stopping=1;return;
    }
    unsigned frame=(unsigned)pilot_gpu_measure.attempts++;
    D3D11_TEXTURE2D_DESC desc;ID3D11Texture2D_GetDesc(pilot_surfaces[0],&desc);
    if(pilot_gpu_profile_begin_frame(pilot_gpu_measure.gpu,frame,pilot_gfx_width(),pilot_gfx_height(),desc.Width,desc.Height)) {
        pilot_gpu_measure.source_payload_begin=pilot_resource_transfer_stats.texture_payload_bytes+
            pilot_resource_transfer_stats.vertex_full_bytes+pilot_resource_transfer_stats.vertex_range_bytes;
        pilot_gpu_measure.frame=frame;pilot_gpu_measure.open=1;++pilot_gpu_measure.admitted;
        if(!pilot_gpu_profile_begin_pass(pilot_gpu_measure.gpu,0))++pilot_gpu_measure.query_errors;
    }
}
static void pilot_gpu_measure_split(unsigned pass)
{
    if(pilot_gpu_measure.open) {
        if(!pilot_gpu_profile_end_pass(pilot_gpu_measure.gpu) ||
           !pilot_gpu_profile_begin_pass(pilot_gpu_measure.gpu,pass))++pilot_gpu_measure.query_errors;
    }
}
static void pilot_gpu_measure_end(unsigned partial)
{
    if(pilot_gpu_measure.open) {
        pilot_gpu_measure.partial[pilot_gpu_measure.frame]=(uint8_t)(partial!=0);
        uint64_t payload=pilot_resource_transfer_stats.texture_payload_bytes+
            pilot_resource_transfer_stats.vertex_full_bytes+pilot_resource_transfer_stats.vertex_range_bytes;
        payload-=pilot_gpu_measure.source_payload_begin;
        if(!pilot_gpu_profile_end_pass(pilot_gpu_measure.gpu) ||
           !pilot_gpu_profile_end_frame(pilot_gpu_measure.gpu,payload))++pilot_gpu_measure.query_errors;
        pilot_gpu_measure.open=0;
    }
    pilot_gpu_measure_collect();
}
static void pilot_gpu_measure_present(int32_t result)
{
    if(!pilot_gpu_measure.gpu)return;
    if(result<0)++pilot_gpu_measure.failed_presents;
    /* Capture after resolve so an invalid/unavailable companion is recorded
     * at the native fallback size, never at the requested enhanced size. */
    if(pilot_gpu_measure.open)pilot_gfx_render_size(pilot_gpu_measure.presented_size[pilot_gpu_measure.frame]);
    pilot_gpu_measure_end(0);
    int requested=GetFileAttributesA(pilot_gpu_measure.stop)!=INVALID_FILE_ATTRIBUTES;
    if(requested)pilot_gpu_measure.stopping=1;
    /* Capacity exhaustion stops instrumentation, but must not introduce disk
     * I/O before the harness explicitly ends its measurement window. */
    if(requested)pilot_gpu_measure_write(0);
    if(!pilot_gpu_measure.stopping)pilot_gpu_measure_begin();
}
static void pilot_gpu_measure_open(void)
{
    const char *path=getenv("XLLE_PILOT_GPU_PROFILE_FILE"),*stop=getenv("XLLE_PILOT_GPU_PROFILE_STOP_FILE");
    if(!path || !*path)return;
    memset(&pilot_gpu_measure,0,sizeof(pilot_gpu_measure));pilot_gpu_measure.output=path;pilot_gpu_measure.stop=stop;
    pilot_gpu_measure.start=getenv("XLLE_PILOT_GPU_PROFILE_START_FILE");
    if(!stop || !*stop || GetFileAttributesA(stop)!=INVALID_FILE_ATTRIBUTES ||
       GetFileAttributesA(path)!=INVALID_FILE_ATTRIBUTES){
        fprintf(stderr,"[GPU TIMESTAMPS] absent/old stop-file or existing output; unavailable\n");pilot_gpu_measure.done=1;return;
    }
    pilot_gpu_measure.samples=calloc(PILOT_GPU_MEASURE_CAPACITY,sizeof(PilotGpuProfileSample));
    if(pilot_gpu_measure.samples)pilot_gpu_measure.gpu=pilot_gpu_profile_create(d3d8_GetD3D11Device(),d3d8_GetD3D11Context());
    if(!pilot_gpu_measure.gpu){pilot_gpu_measure_write(1);return;}
    fprintf(stderr,"[GPU TIMESTAMPS] bounded=%u passes=render,guest-resolve,swap-interval; optional native source spans; stop file required after timing\n",PILOT_GPU_MEASURE_CAPACITY);
    pilot_gpu_measure_begin();
}
static void pilot_gpu_measure_close(void)
{
    pilot_gpu_measure_end(1);pilot_gpu_measure_write(1);
    pilot_gpu_profile_destroy(pilot_gpu_measure.gpu);free(pilot_gpu_measure.samples);
    memset(&pilot_gpu_measure,0,sizeof(pilot_gpu_measure));
}
#else
#define pilot_gpu_measure_open() ((void)0)
#define pilot_gpu_measure_close() ((void)0)
#define pilot_gpu_measure_begin() ((void)0)
#define pilot_gpu_measure_end(partial) ((void)0)
#define pilot_gpu_measure_split(pass) ((void)0)
#define pilot_gpu_measure_present(result) ((void)0)
#endif
