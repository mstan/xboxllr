/* Completion is observed from actual D3D11 events. Draws/uploads only record
 * serials: a query is issued on demand and covers a whole submitted batch.
 * Polling never waits for the GPU. A later checkpoint may conservatively cover
 * unrelated work too; it can delay idle, never report unfinished work idle. */
struct PilotCompletion { ID3D11Query *query; HANDLE event; uint64_t serial; struct PilotCompletion *next; };
static struct PilotCompletion *pilot_completion_head,*pilot_completion_tail;
static ID3D11DeviceContext3 *pilot_completion_context;
static int pilot_completion_events_checked;
static uint64_t pilot_submitted,pilot_completed;
static unsigned pilot_query_count,pilot_query_created;
/* Completion belongs to image storage, not a reallocatable texture slot or
 * presentation handle. Aliases retain this small owner across retirement and
 * reset; recording/querying use needs no search or GPU readback. */
/* Reads advance completion ownership, but only content writes invalidate a
 * prepared CPU snapshot. Both revisions follow the shared image's lifetime. */
struct PilotImageUse {uint64_t serial,write_serial;unsigned refs;};
static struct PilotImageUse *pilot_surface_use[4];
static struct PilotImageUse *pilot_image_use_create(void)
{
    struct PilotImageUse *use=calloc(1,sizeof(*use));
    if(use)use->refs=1;
    return use;
}
static void pilot_image_use_retain(struct PilotImageUse *use)
{
    if(!use || !use->refs || use->refs==UINT_MAX)pilot_d3d_backend_stop("invalid image completion retain");
    ++use->refs;
}
static void pilot_image_use_release(struct PilotImageUse *use)
{
    if(!use)return;
    if(!use->refs)pilot_d3d_backend_stop("invalid image completion release");
    if(!--use->refs)free(use);
}
/* Optional sparse diagnosis of native driver stalls. Report only calls over
 * 50 ms, after the measured call returns; ordinary completion is unchanged. */
static uint64_t pilot_gpu_call_begin(void)
{
    static int enabled=-1;
    if(enabled<0){const char *v=getenv("XLLE_PILOT_GPU_CALL_PROFILE");enabled=v && !strcmp(v,"1");}
    if(!enabled)return 0;
    LARGE_INTEGER now;QueryPerformanceCounter(&now);return (uint64_t)now.QuadPart;
}
static void pilot_gpu_call_end(const char *name,uint64_t begin)
{
    if(!begin)return;
    LARGE_INTEGER now,frequency;QueryPerformanceCounter(&now);QueryPerformanceFrequency(&frequency);
    double ms=(now.QuadPart-begin)*1000.0/frequency.QuadPart;
    static LONG records;
    if(ms<=50 || InterlockedIncrement(&records)>256)return;
    uint64_t ticks=(uint64_t)now.QuadPart,hz=(uint64_t)frequency.QuadPart,cpu=0;
    unsigned cpu_available=0;
#ifdef _WIN32
    FILETIME created,exited,kernel,user;
    if(GetThreadTimes(GetCurrentThread(),&created,&exited,&kernel,&user)) {
        cpu=((uint64_t)kernel.dwHighDateTime<<32)+kernel.dwLowDateTime+
            ((uint64_t)user.dwHighDateTime<<32)+user.dwLowDateTime;
        cpu_available=1;
    }
#endif
    /* Absolute 100-ns QPC time shares the media/audio diagnostic clock.
     * CPU is a cumulative end sample, not a claim about this call's CPU cost.
     * Sampling it only for slow calls avoids another API on every bind/poll. */
    fprintf(stderr,"[GPU CALL SLOW] thread=%lu call=%s serial=%llu queries=%u elapsed_ms=%.3f begin=%llu end=%llu thread_cpu_total_100ns=%llu cpu_available=%u\n",
        GetCurrentThreadId(),name,(unsigned long long)pilot_submitted,pilot_query_count,ms,
        (unsigned long long)(begin/hz*10000000ull+begin%hz*10000000ull/hz),
        (unsigned long long)(ticks/hz*10000000ull+ticks%hz*10000000ull/hz),
        (unsigned long long)cpu,cpu_available);
}
static void pilot_completion_events_init(void)
{
    if(pilot_completion_events_checked)return;
    pilot_completion_events_checked=1;
    const char *setting=getenv("XLLE_PILOT_GPU_EVENTS");
    if(!(setting && !strcmp(setting,"0"))) {
        HRESULT hr=ID3D11DeviceContext_QueryInterface(d3d8_GetD3D11Context(),&IID_ID3D11DeviceContext3,(void**)&pilot_completion_context);
        if(hr!=E_NOINTERFACE)pilot_d3d_require(hr,__FILE__,__LINE__);
    }
    fprintf(stderr,"[GPU COMPLETION] native_events=%u\n",pilot_completion_context!=NULL);
}
static int pilot_completion_event_ready_raw(HANDLE event)
{
    DWORD result=WaitForSingleObject(event,0);
    if(result==WAIT_FAILED)pilot_d3d_backend_stop("native GPU event wait failed");
    return result==WAIT_OBJECT_0;
}
static int pilot_completion_event_ready(HANDLE event)
{
    int actual=pilot_completion_event_ready_raw(event);
    int recorded=(int)pilot_session_value64(PILOT_SESSION_GPU_BUSY,UINT64_C(0x8000000000000000),actual);
    if(pilot_session_mode()==PILOT_SESSION_REPLAY && recorded && !actual && WaitForSingleObject(event,30000)!=WAIT_OBJECT_0)
        pilot_session_diverge("recorded GPU event did not signal",0);
    return recorded;
}
static uint64_t pilot_mark_work(void)
{
    if(pilot_submitted==UINT64_MAX)pilot_d3d_backend_stop("native submission serial exhausted");
    return ++pilot_submitted;
}
static void pilot_completion_poll(void)
{
    ID3D11DeviceContext *context=d3d8_GetD3D11Context();
    while(pilot_completion_head) {
        struct PilotCompletion *entry=pilot_completion_head;BOOL complete=FALSE;
        /* A CPU waiter may prevent further draws/presents. Let the native
         * runtime advance command submission while checking actual completion;
         * DONOTFLUSH can leave that waiter dependent on later API traffic. */
        uint64_t started=pilot_gpu_call_begin();
        HRESULT hr=ID3D11DeviceContext_GetData(context,(ID3D11Asynchronous*)entry->query,&complete,sizeof(complete),0);
        pilot_gpu_call_end("GetData",started);
        pilot_d3d_require(hr,__FILE__,__LINE__);
        int ready=hr==S_OK && complete && (!entry->event || pilot_completion_event_ready_raw(entry->event));
        int recorded=(int)pilot_session_value64(PILOT_SESSION_GPU_BUSY,entry->serial,ready);
        if(!recorded)break;
        if(pilot_session_mode()==PILOT_SESSION_REPLAY && !ready) {
            uint64_t began=GetTickCount64();
            do {
                if(GetTickCount64()-began>30000)pilot_session_diverge("recorded GPU work did not complete",entry->serial);
                Sleep(1);complete=FALSE;
                hr=ID3D11DeviceContext_GetData(context,(ID3D11Asynchronous*)entry->query,&complete,sizeof(complete),0);
                pilot_d3d_require(hr,__FILE__,__LINE__);
                ready=hr==S_OK && complete && (!entry->event || pilot_completion_event_ready_raw(entry->event));
            } while(!ready);
        }
        pilot_completed=entry->serial;pilot_completion_head=entry->next;
        if(!pilot_completion_head)pilot_completion_tail=NULL;
        started=pilot_gpu_call_begin();
        ID3D11Query_Release(entry->query);if(entry->event)CloseHandle(entry->event);free(entry);--pilot_query_count;
        pilot_gpu_call_end("query/event release",started);
    }
}
static int pilot_serial_busy(uint64_t serial)
{
    pilot_gfx_batch_flush();
    if(serial<=pilot_completed)return 0;
    pilot_completion_poll();if(serial<=pilot_completed)return 0;
    if(!pilot_completion_tail || pilot_completion_tail->serial<serial) {
        struct PilotCompletion *entry=calloc(1,sizeof(*entry));
        if(!entry)pilot_d3d_backend_stop("native completion allocation failed");
        D3D11_QUERY_DESC desc={D3D11_QUERY_EVENT,0};
        uint64_t started=pilot_gpu_call_begin();
        pilot_d3d_require(ID3D11Device_CreateQuery(d3d8_GetD3D11Device(),&desc,&entry->query),__FILE__,__LINE__);
        pilot_gpu_call_end("CreateQuery",started);
        entry->serial=pilot_submitted;
        ID3D11DeviceContext *context=d3d8_GetD3D11Context();
        started=pilot_gpu_call_begin();
        ID3D11DeviceContext_End(context,(ID3D11Asynchronous*)entry->query);
        pilot_gpu_call_end("EndQuery",started);
        if(pilot_completion_tail)pilot_completion_tail->next=entry;else pilot_completion_head=entry;
        pilot_completion_tail=entry;++pilot_query_count;++pilot_query_created;
        /* Submit now; subsequent nonblocking GetData may also advance work. */
        pilot_completion_events_init();
        if(pilot_completion_context) {
            entry->event=CreateEventW(NULL,TRUE,FALSE,NULL);
            if(!entry->event)pilot_d3d_backend_stop("native GPU event allocation failed");
            started=pilot_gpu_call_begin();
            ID3D11DeviceContext3_Flush1(pilot_completion_context,D3D11_CONTEXT_TYPE_ALL,entry->event);
            pilot_gpu_call_end("Flush1",started);
        } else {
            started=pilot_gpu_call_begin();ID3D11DeviceContext_Flush(context);
            pilot_gpu_call_end("Flush",started);
        }
        pilot_completion_poll();
    }
    return serial>pilot_completed;
}
static void pilot_completion_close(void)
{
    while(pilot_completion_head) {
        struct PilotCompletion *entry=pilot_completion_head;pilot_completion_head=entry->next;
        if(entry->event) {
            if(WaitForSingleObject(entry->event,5000)!=WAIT_OBJECT_0)pilot_d3d_backend_stop("native GPU event did not retire during close");
            CloseHandle(entry->event);
        }
        ID3D11Query_Release(entry->query);free(entry);
    }
    pilot_completion_tail=NULL;pilot_submitted=pilot_completed=0;pilot_query_count=pilot_query_created=0;
    if(pilot_completion_context){ID3D11DeviceContext3_Release(pilot_completion_context);pilot_completion_context=NULL;}
    pilot_completion_events_checked=0;
}
uint64_t pilot_gfx_checkpoint(void)
{
    uint64_t serial=pilot_submitted;(void)pilot_serial_busy(serial);return serial;
}
int pilot_gfx_checkpoint_busy(uint64_t serial)
{
    if(serial>pilot_submitted)pilot_d3d_backend_stop("completion checkpoint beyond submitted work");
    return pilot_serial_busy(serial);
}
void *pilot_gfx_checkpoint_event(uint64_t serial)
{
    if(!pilot_gfx_checkpoint_busy(serial))return NULL;
    for(struct PilotCompletion *entry=pilot_completion_head;entry;entry=entry->next)if(entry->serial>=serial) {
        if(!entry->event || pilot_completion_event_ready(entry->event))return NULL;
        HANDLE event=NULL;
        if(!DuplicateHandle(GetCurrentProcess(),entry->event,GetCurrentProcess(),&event,0,FALSE,DUPLICATE_SAME_ACCESS))
            pilot_d3d_backend_stop("native GPU event duplication failed");
        return event;
    }
    pilot_d3d_backend_stop("missing GPU checkpoint owner");
}
int pilot_gfx_completion_events(void)
{pilot_completion_events_init();return pilot_completion_context!=NULL;}
void pilot_gfx_completion_stats(struct PilotGpuProgress *progress)
{*progress=(struct PilotGpuProgress){pilot_submitted,pilot_completed,pilot_query_count,pilot_query_created};}
static void pilot_mark_targets(unsigned flags,uint64_t serial)
{
    if(flags&1) {
        if(pilot_color_target)pilot_textures[pilot_color_target-1].use->serial=
            pilot_textures[pilot_color_target-1].use->write_serial=serial;
        else pilot_surface_use[0]->serial=pilot_surface_use[0]->write_serial=serial;
    }
    if(flags&6) {
        if(pilot_depth_target==UINT32_MAX)pilot_surface_use[2]->serial=pilot_surface_use[2]->write_serial=serial;
        else if(pilot_depth_target) {
            struct PilotNativeTexture *texture=&pilot_textures[pilot_depth_target-1];
            texture->content_serial=texture->use->serial=texture->use->write_serial=serial;
        }
    }
}
static void pilot_mark_draw(void)
{
    uint64_t serial=pilot_mark_work();pilot_mark_targets(7,serial);
    for(unsigned i=0;i<4;++i)if(pilot_bound_textures[i])pilot_textures[pilot_bound_textures[i]-1].use->serial=serial;
    for(unsigned i=0;i<16;++i)if(pilot_bound_vertices[i])pilot_vertices[pilot_bound_vertices[i]-1].last_use=serial;
}
