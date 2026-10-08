/* Opt-in observation of the selected implementation, never duplicate work.
 * Per-thread totals keep the display/DPC worker out of the drawing thread's
 * elapsed-time accounting. Timings include service waits and profiling cost. */
static int graphics_service_profile_enabled(void)
{
    static __declspec(thread) int enabled=-1;
    if(enabled<0) {const char *value=getenv("XLLE_PILOT_SERVICE_PROFILE");enabled=value && !strcmp(value,"1");}
    return enabled;
}
static __declspec(thread) struct {
    uint64_t start,ticks[PILOT_D3D_COUNT];
    uint64_t draw_ticks[7];
    unsigned draw_calls;
    unsigned frames,calls[PILOT_D3D_COUNT];
} graphics_service_profile;

static void graphics_draw_profile_step(uint64_t *previous,unsigned phase)
{
    if(!*previous)return;
    uint64_t now=dispatch_monotonic();
    graphics_service_profile.draw_ticks[phase]+=now-*previous;
    *previous=now;
}

static void graphics_service_profile_record(unsigned entry,uint64_t begin,uint64_t end)
{
    if(!graphics_service_profile.start)graphics_service_profile.start=begin;
    graphics_service_profile.ticks[entry]+=end-begin;
    ++graphics_service_profile.calls[entry];
    if(entry!=PILOT_D3D_D3DDevice_Swap || ++graphics_service_profile.frames<30)return;
    uint64_t total=0;unsigned calls=0;
    for(unsigned i=0;i<PILOT_D3D_COUNT;++i) {
        total+=graphics_service_profile.ticks[i];calls+=graphics_service_profile.calls[i];
    }
    uint64_t wall=end-graphics_service_profile.start;
    fprintf(stderr,"[GRAPHICS PROFILE] thread=%lu frames=%u wall_ms=%.3f service_ms=%.3f outside_ms=%.3f calls=%u\n",
        GetCurrentThreadId(),graphics_service_profile.frames,wall/10000.0,total/10000.0,
        wall>=total?(wall-total)/10000.0:0.0,calls);
    static const char *phases[]={"pixel_target","vertices","textures","deferred_state","shader","submit","capture"};
    for(unsigned i=0;i<7;++i)fprintf(stderr,"[GRAPHICS PROFILE DRAW] thread=%lu phase=%s calls=%u elapsed_ms=%.3f\n",
        GetCurrentThreadId(),phases[i],graphics_service_profile.draw_calls,graphics_service_profile.draw_ticks[i]/10000.0);
    for(unsigned i=0;i<PILOT_D3D_COUNT;++i)if(graphics_service_profile.calls[i])
        fprintf(stderr,"[GRAPHICS PROFILE METHOD] thread=%lu name=%s calls=%u elapsed_ms=%.3f\n",
            GetCurrentThreadId(),pilot_d3d_calls[i].name,graphics_service_profile.calls[i],
            graphics_service_profile.ticks[i]/10000.0);
    memset(&graphics_service_profile,0,sizeof(graphics_service_profile));
    graphics_service_profile.start=end;
}
