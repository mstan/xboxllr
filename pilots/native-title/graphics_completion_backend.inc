/* Completion is observed from actual D3D11 events. Draws/uploads only record
 * serials: a query is issued on demand and covers a whole submitted batch.
 * Polling never waits for the GPU. A later checkpoint may conservatively cover
 * unrelated work too; it can delay idle, never report unfinished work idle. */
struct PilotCompletion { ID3D11Query *query; uint64_t serial; struct PilotCompletion *next; };
static struct PilotCompletion *pilot_completion_head,*pilot_completion_tail;
static uint64_t pilot_submitted,pilot_completed;
static unsigned pilot_query_count,pilot_query_created;
static uint64_t pilot_surface_use[3];
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
        HRESULT hr=ID3D11DeviceContext_GetData(context,(ID3D11Asynchronous*)entry->query,&complete,sizeof(complete),D3D11_ASYNC_GETDATA_DONOTFLUSH);
        pilot_d3d_require(hr,__FILE__,__LINE__);
        if(hr!=S_OK || !complete)break;
        pilot_completed=entry->serial;pilot_completion_head=entry->next;
        if(!pilot_completion_head)pilot_completion_tail=NULL;
        ID3D11Query_Release(entry->query);free(entry);--pilot_query_count;
    }
}
static int pilot_serial_busy(uint64_t serial)
{
    if(serial<=pilot_completed)return 0;
    pilot_completion_poll();if(serial<=pilot_completed)return 0;
    if(!pilot_completion_tail || pilot_completion_tail->serial<serial) {
        struct PilotCompletion *entry=calloc(1,sizeof(*entry));
        if(!entry)pilot_d3d_backend_stop("native completion allocation failed");
        D3D11_QUERY_DESC desc={D3D11_QUERY_EVENT,0};
        pilot_d3d_require(ID3D11Device_CreateQuery(d3d8_GetD3D11Device(),&desc,&entry->query),__FILE__,__LINE__);
        entry->serial=pilot_submitted;
        ID3D11DeviceContext *context=d3d8_GetD3D11Context();
        ID3D11DeviceContext_End(context,(ID3D11Asynchronous*)entry->query);
        if(pilot_completion_tail)pilot_completion_tail->next=entry;else pilot_completion_head=entry;
        pilot_completion_tail=entry;++pilot_query_count;++pilot_query_created;
        /* Submit once, then use DONOTFLUSH on repeated nonblocking polls. */
        ID3D11DeviceContext_Flush(context);
        pilot_completion_poll();
    }
    return serial>pilot_completed;
}
static void pilot_completion_close(void)
{
    while(pilot_completion_head) {
        struct PilotCompletion *entry=pilot_completion_head;pilot_completion_head=entry->next;
        ID3D11Query_Release(entry->query);free(entry);
    }
    pilot_completion_tail=NULL;pilot_submitted=pilot_completed=0;pilot_query_count=pilot_query_created=0;
    memset(pilot_surface_use,0,sizeof(pilot_surface_use));
}
void pilot_gfx_completion_stats(struct PilotGpuProgress *progress)
{*progress=(struct PilotGpuProgress){pilot_submitted,pilot_completed,pilot_query_count,pilot_query_created};}
static void pilot_mark_targets(unsigned flags,uint64_t serial)
{
    if(flags&1) {
        if(pilot_color_target)pilot_textures[pilot_color_target-1].last_use=serial;
        else pilot_surface_use[0]=serial;
    }
    if(flags&6) {
        if(pilot_depth_target==UINT32_MAX)pilot_surface_use[2]=serial;
        else if(pilot_depth_target)pilot_textures[pilot_depth_target-1].last_use=serial;
    }
}
static void pilot_mark_draw(void)
{
    uint64_t serial=pilot_mark_work();pilot_mark_targets(7,serial);
    for(unsigned i=0;i<4;++i)if(pilot_bound_textures[i])pilot_textures[pilot_bound_textures[i]-1].last_use=serial;
    for(unsigned i=0;i<16;++i)if(pilot_bound_vertices[i])pilot_vertices[pilot_bound_vertices[i]-1].last_use=serial;
}
