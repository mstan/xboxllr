/* Ordered HLE lowering: compatible buffered strips become one indexed strip.
 * UINT32_MAX is the D3D11 strip-cut index, so winding/parity restarts exactly
 * at each original strip. No vertex, primitive or original completion serial
 * is omitted. Callers flush at non-draw service and observation boundaries;
 * draw preparation flushes before changing a native input/state/resource. */
enum {PILOT_STRIP_BATCH_DRAWS=64,PILOT_STRIP_BATCH_INDICES=4096};
static struct {
    struct {uint32_t first,count;} ranges[PILOT_STRIP_BATCH_DRAWS];
    unsigned count,indices,allowed,prepared;
    ID3D11Buffer *buffer;
    struct PilotDrawBatchStats stats;
} pilot_strip_batch;
/* Diagnostic override is resolved from matching build symbols for same-process A/B. */
int pilot_draw_batch_enabled=-1;
/* Resolve once, before the first group. The checked path remains available
 * for matched diagnostic runs; this never changes execution ownership. */
static int pilot_draw_group_reuse=-1;
static int pilot_strip_group_reuse(void)
{
    if(pilot_draw_group_reuse<0) {
        const char *value=getenv("XLLE_PILOT_DRAW_GROUP_REUSE");
        pilot_draw_group_reuse=!(value && !strcmp(value,"0"));
    }
    return pilot_draw_group_reuse;
}

void pilot_gfx_batch_flush(void)
{
    unsigned count=pilot_strip_batch.count;
    pilot_strip_batch.prepared=0;
    if(!count)return;
    pilot_strip_batch.count=0;pilot_strip_batch.indices=0;
    ID3D11DeviceContext *context=d3d8_GetD3D11Context();
    int merged=0;
    if(count>1) {
        if(!pilot_strip_batch.buffer) {
            D3D11_BUFFER_DESC desc={0};
            desc.ByteWidth=PILOT_STRIP_BATCH_INDICES*4;
            desc.Usage=D3D11_USAGE_DYNAMIC;desc.BindFlags=D3D11_BIND_INDEX_BUFFER;
            desc.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
            /* Index storage is an optional acceleration. Allocation failure
             * replays the retained ranges through the original native path. */
            (void)ID3D11Device_CreateBuffer(d3d8_GetD3D11Device(),&desc,NULL,&pilot_strip_batch.buffer);
        }
        if(pilot_strip_batch.buffer) {
            D3D11_MAPPED_SUBRESOURCE mapped;
            HRESULT hr=ID3D11DeviceContext_Map(context,(ID3D11Resource*)pilot_strip_batch.buffer,
                0,D3D11_MAP_WRITE_DISCARD,0,&mapped);
            pilot_d3d_require(hr,__FILE__,__LINE__);
            uint32_t *indices=mapped.pData;unsigned words=0;
            for(unsigned draw=0;draw<count;++draw) {
                if(draw)indices[words++]=UINT32_MAX;
                for(unsigned i=0;i<pilot_strip_batch.ranges[draw].count;++i)
                    indices[words++]=pilot_strip_batch.ranges[draw].first+i;
            }
            ID3D11DeviceContext_Unmap(context,(ID3D11Resource*)pilot_strip_batch.buffer,0);
            ID3D11Buffer *previous=NULL;DXGI_FORMAT format;UINT offset;
            ID3D11DeviceContext_IAGetIndexBuffer(context,&previous,&format,&offset);
            ID3D11DeviceContext_IASetIndexBuffer(context,pilot_strip_batch.buffer,DXGI_FORMAT_R32_UINT,0);
            ID3D11DeviceContext_DrawIndexed(context,words,0,0);
            ID3D11DeviceContext_IASetIndexBuffer(context,previous,format,offset);
            if(previous)ID3D11Buffer_Release(previous);
            merged=1;
        }
    }
    if(!merged)for(unsigned draw=0;draw<count;++draw)
        ID3D11DeviceContext_Draw(context,pilot_strip_batch.ranges[draw].count,pilot_strip_batch.ranges[draw].first);
    pilot_strip_batch.stats.queued_draws+=count;
    pilot_strip_batch.stats.native_draws+=merged?1:count;
    if(count>pilot_strip_batch.stats.largest_batch)pilot_strip_batch.stats.largest_batch=count;
}

void pilot_gfx_batch_allow(int eligible)
{
    if(pilot_draw_batch_enabled<0) {
        const char *value=getenv("XLLE_PILOT_DRAW_BATCHING");
        pilot_draw_batch_enabled=!(value && !strcmp(value,"0"));
    }
#ifdef PILOT_D3D_PHOBOS
    pilot_strip_batch.allowed=eligible && pilot_draw_batch_enabled;
#else
    (void)eligible;pilot_strip_batch.allowed=0; /* Retain the audited immediate path. */
#endif
    if(!pilot_strip_batch.allowed)pilot_gfx_batch_flush();
}

void pilot_gfx_batch_stats(struct PilotDrawBatchStats *stats)
{*stats=pilot_strip_batch.stats;}
int pilot_gfx_batch_prepared(void)
{return pilot_strip_batch.allowed && pilot_strip_batch.count && pilot_strip_batch.prepared;}

static int pilot_strip_batch_room(unsigned first,unsigned count)
{
    if(count<3 || count>1024 || (uint64_t)first+count>UINT32_MAX)return 0;
    if(pilot_strip_batch.count==PILOT_STRIP_BATCH_DRAWS ||
       pilot_strip_batch.indices+count+(pilot_strip_batch.count!=0)>PILOT_STRIP_BATCH_INDICES)
        pilot_gfx_batch_flush();
    return 1;
}
static void pilot_strip_batch_queue(unsigned first,unsigned count)
{
    unsigned slot=pilot_strip_batch.count++;
    pilot_strip_batch.ranges[slot].first=first;
    pilot_strip_batch.ranges[slot].count=count;
    pilot_strip_batch.indices+=count+(slot!=0);
}
static void pilot_strip_batch_close(void)
{
    pilot_gfx_batch_flush();
    if(pilot_strip_batch.buffer)ID3D11Buffer_Release(pilot_strip_batch.buffer);
    memset(&pilot_strip_batch,0,sizeof(pilot_strip_batch));
}
static void pilot_strip_batch_profile(void)
{
    static int enabled=-1;static unsigned frames;
    if(enabled<0){const char *value=getenv("XLLE_PILOT_BATCH_PROFILE");enabled=value && !strcmp(value,"1");}
    if(enabled && ++frames%30==0)
        fprintf(stderr,"[DRAW BATCH] queued=%llu native=%llu largest=%u vertex_updates=%llu vertex_bytes=%llu prepared_reuses=%llu\n",
            (unsigned long long)pilot_strip_batch.stats.queued_draws,
            (unsigned long long)pilot_strip_batch.stats.native_draws,pilot_strip_batch.stats.largest_batch,
            (unsigned long long)pilot_strip_batch.stats.vertex_updates,
            (unsigned long long)pilot_strip_batch.stats.vertex_bytes,
            (unsigned long long)pilot_strip_batch.stats.prepared_reuses);
}
