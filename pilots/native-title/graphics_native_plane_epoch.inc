/* R09: queue only prepared HD GPU work. Canonical commands, uploads and
 * source-version copies stay on the immediate context. Proven owned DEFAULT
 * mesh buffers retain their original bindings under owner write barriers;
 * unproved IA and all CB data own private snapshots. Replay uses the SAME immediate context;
 * there are no D3D deferred contexts or driver command-list caches. */
enum { PILOT_NATIVE_EPOCH_BYTES=128*1024*1024,PILOT_NATIVE_EPOCH_DRAWS=256,
       PILOT_NATIVE_EPOCH_BUFFERS=4096,PILOT_NATIVE_EPOCH_GROUPS=1,
       PILOT_NATIVE_EPOCH_COPIES=D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT+1+3*D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT,
       PILOT_NATIVE_EPOCH_STAGES=3,PILOT_NATIVE_EPOCH_CLASSES=256 };
struct PilotNativeEpochBuffer {
    ID3D11Buffer *buffer;
    UINT size,bind,used,group;
    struct PilotNativeEpochBuffer *next;
};
struct PilotNativeEpochJob;
struct PilotNativeEpochGroup {
    struct PilotNativeEpochJob *jobs;
    struct PreparedResourceLease resources;
    struct PilotNativePlane *outputs[PILOT_NATIVE_EPOCH_DRAWS*2];
    unsigned state,output_count,job_count; /* 0 free, 1 CPU packet collecting. */
};
static int pilot_native_epoch_jobs_prepare(struct PilotNativeEpochGroup *);
static void pilot_native_epoch_jobs_release(struct PilotNativeEpochGroup *);
static int pilot_native_epoch_replay(struct PilotNativeEpochGroup *);
static int pilot_native_prepared_jobs_enabled(void);
struct PilotNativeEpochCopy {
    ID3D11Buffer *source,*destination;
    UINT offset,bytes,target,bind;
};
static struct {
    struct PilotNativeEpochBuffer *buffers;
    struct PilotNativePlane *outputs[PILOT_NATIVE_EPOCH_DRAWS*2];
    struct PilotNativeEpochGroup groups[PILOT_NATIVE_EPOCH_GROUPS];
    struct PilotNativeEpochCopy copies[PILOT_NATIVE_EPOCH_COPIES];
    unsigned output_count,draws,allocated,buffer_count,bytes,serial,recording,copy_count,failed,packet_bytes,borrowed_vertices,borrowed_constants,borrowed_indices;
} pilot_native_epoch;
static unsigned pilot_native_epoch_extents[D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT];
/* Private bounded attribution; CPU cycles exclude time descheduled in driver waits.
 * Counters are nested, not additive. No sampling privilege or timing-window I/O. */
static struct {
    uint64_t start,work,prepare,record,flush,external_flush,replay,submit,copy_bytes;
    unsigned active,inside,draws,packets,samples,random;
} pilot_native_cpu;
static uint64_t pilot_native_cpu_clock(void)
{
#ifdef _WIN32
    ULONG64 cycles=0;if(pilot_native_cpu.active)QueryThreadCycleTime(GetCurrentThread(),&cycles);
    return (uint64_t)cycles;
#else
    return 0;
#endif
}
static void pilot_native_cpu_begin(void)
{
    const char *value=getenv("XLLE_PILOT_NATIVE_RENDER_DIAGNOSTICS");
    if(pilot_native_cpu.start || !value || strcmp(value,"1"))return;
    pilot_native_cpu.active=1;pilot_native_cpu.random=1;pilot_native_cpu.start=pilot_native_cpu_clock();
}
static void pilot_native_cpu_report(void)
{
    if(!pilot_native_cpu.active)return;
    uint64_t total=pilot_native_cpu_clock()-pilot_native_cpu.start;
    pilot_native_cpu.active=0;
    fprintf(stderr,"[NATIVE CPU CYCLES] total=%llu work=%llu prepare=%llu record=%llu flush=%llu external_flush=%llu replay=%llu submit=%llu copy_bytes=%llu draws=%u packets=%u draw_samples=%u draw_weight=64 nested=1\n",
        (unsigned long long)total,(unsigned long long)pilot_native_cpu.work,
        (unsigned long long)pilot_native_cpu.prepare,(unsigned long long)pilot_native_cpu.record,
        (unsigned long long)pilot_native_cpu.flush,(unsigned long long)pilot_native_cpu.external_flush,
        (unsigned long long)pilot_native_cpu.replay,(unsigned long long)pilot_native_cpu.submit,
        (unsigned long long)pilot_native_cpu.copy_bytes,pilot_native_cpu.draws,pilot_native_cpu.packets,pilot_native_cpu.samples);
}
static void pilot_native_epoch_vertex_span(unsigned stream,unsigned extent)
{
    if(stream<D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT)pilot_native_epoch_extents[stream]=extent;
}
static int pilot_native_epoch_enabled(void)
{
    static int mode=-1;
    if(mode<0) {
        const char *value=getenv("XLLE_PILOT_NATIVE_RENDER_EPOCH");
        /* The owned prepared pipeline follows elective upscaling. Native1x
         * and unsupported/budget-declined paths remain outside this queue.
         * Explicit values are private same-binary qualification controls. */
        mode=!value || !strcmp(value,"1")?1:!strcmp(value,"2")?2:0;
    }
    return mode && (mode!=2 || pilot_native_query_active()) &&
        !pilot_native_epoch.failed && pilot_native_enabled();
}
static void pilot_native_epoch_group_release(unsigned slot,int completed)
{
    struct PilotNativeEpochGroup *group=&pilot_native_epoch.groups[slot];
    for(unsigned i=0;i<group->output_count;++i) {
        struct PilotNativePlane *plane=group->outputs[i];
        if(!completed)plane->valid_aspects=plane->coherent=0;
        pilot_native_plane_release(plane);
    }
    pilot_native_epoch_jobs_release(group);
    prepared_state_lease_clear(&group->resources);
    group->output_count=group->state=0;
    for(struct PilotNativeEpochBuffer *p=pilot_native_epoch.buffers;p;p=p->next)
        if(p->group==slot+1)p->used=p->group=0;
}
/* The CPU queue owns immutable inputs until its final Draw call is issued on
 * this one immediate context. D3D11 then owns command ordering and resource
 * lifetime: Copy/Update after Draw cannot overwrite an earlier command's input,
 * and dynamic WRITE_DISCARD preserves its in-flight allocation. No CPU writes
 * use NO_OVERWRITE over retained ranges. This is not a native D3D12/Vulkan
 * command buffer or an application-owned mapped allocation. */
static int pilot_native_epoch_reserve_group(void)
{
    /* A packet is CPU-owned only until replay. Reuse the same bounded storage
     * after submitting its commands, without waiting for GPU completion. */
    if(pilot_native_prepared_jobs_enabled() && pilot_native_epoch.recording)
        return !pilot_native_epoch.failed;
    if(pilot_native_epoch.failed)return 0;
    if(pilot_native_epoch.recording)return 1;
    for(unsigned i=0;i<PILOT_NATIVE_EPOCH_GROUPS;++i)if(!pilot_native_epoch.groups[i].state) {
        struct PilotNativeEpochGroup *group=&pilot_native_epoch.groups[i];
        if(!pilot_native_epoch_jobs_prepare(group))return 0;
        group->state=1;pilot_native_epoch.recording=i+1;return 1;
    }
    return 0;
}
static void pilot_native_epoch_cancel_empty(void)
{
    if(pilot_native_epoch.recording && !pilot_native_epoch.draws) {
        pilot_native_epoch.groups[pilot_native_epoch.recording-1].state=0;pilot_native_epoch.recording=0;
    }
}
static void pilot_native_epoch_flush(void)
{
    if(!pilot_native_epoch.draws)return;
    uint64_t cpu_begin=pilot_native_cpu_clock(),cpu_replay=cpu_begin;
    struct PilotNativeEpochGroup *group=&pilot_native_epoch.groups[pilot_native_epoch.recording-1];
    int executed=pilot_native_epoch_replay(group);
    if(cpu_begin)pilot_native_cpu.replay+=pilot_native_cpu_clock()-cpu_replay;
    if(executed) {
        static int reported;
        if(!reported) {
            reported=1;fprintf(stderr,"[NATIVE EPOCH] executed draws=%u snapshot_bytes=%u pool_buffers=%u pool_bytes=%u max_inflight_groups=%u host_packet_bytes=%u borrowed_vertex_bindings=%u retirement=immediate-ordering replay=immediate\n",
                pilot_native_epoch.draws,pilot_native_epoch.bytes,pilot_native_epoch.buffer_count,
                pilot_native_epoch.allocated,PILOT_NATIVE_EPOCH_GROUPS,pilot_native_epoch.packet_bytes,pilot_native_epoch.borrowed_vertices);
        }
        static int borrowed_reported;
        if(pilot_native_epoch.borrowed_vertices && !borrowed_reported) {
            borrowed_reported=1;
            fprintf(stderr,"[NATIVE EPOCH BORROW] replayed_vertex_bindings=%u draws=%u\n",
                pilot_native_epoch.borrowed_vertices,pilot_native_epoch.draws);
        }
        static int constants_reported;
        if(pilot_native_epoch.borrowed_constants && !constants_reported) {
            constants_reported=1;
            fprintf(stderr,"[NATIVE PREPARED CB] replayed_constant_bindings=%u draws=%u shared_uploaded_versions=1\n",
                pilot_native_epoch.borrowed_constants,pilot_native_epoch.draws);
        }
        static int indices_reported;
        if(pilot_native_epoch.borrowed_indices && !indices_reported) {
            indices_reported=1;
            fprintf(stderr,"[NATIVE PREPARED IB] replayed_index_bindings=%u draws=%u shared_uploaded_versions=1\n",
                pilot_native_epoch.borrowed_indices,pilot_native_epoch.draws);
        }
    }
    if(!executed) {
        for(unsigned i=0;i<pilot_native_epoch.output_count;++i)
            pilot_native_epoch.outputs[i]->valid_aspects=pilot_native_epoch.outputs[i]->coherent=0;
        pilot_native_degrade();
    }
    group->output_count=pilot_native_epoch.output_count;
    memcpy(group->outputs,pilot_native_epoch.outputs,group->output_count*sizeof(*group->outputs));
    /* All replay/copy commands now belong to D3D11. Retain no extra GPU-event
     * lease, and do not force a driver submission for each tiny CPU packet. */
    pilot_native_epoch_group_release(pilot_native_epoch.recording-1,executed);
    pilot_native_epoch.recording=pilot_native_epoch.output_count=pilot_native_epoch.draws=pilot_native_epoch.bytes=pilot_native_epoch.borrowed_vertices=pilot_native_epoch.borrowed_constants=pilot_native_epoch.borrowed_indices=0;
    if(cpu_begin) {
        uint64_t cycles=pilot_native_cpu_clock()-cpu_begin;
        pilot_native_cpu.flush+=cycles;if(!pilot_native_cpu.inside)pilot_native_cpu.external_flush+=cycles;
        ++pilot_native_cpu.packets;
    }
}
static void pilot_native_epoch_close(void)
{
    pilot_native_epoch_flush();
    /* D3D11 owns any already issued commands and their resource references. */
    for(unsigned i=0;i<PILOT_NATIVE_EPOCH_GROUPS;++i) {
        pilot_native_epoch_group_release(i,1);
        free(pilot_native_epoch.groups[i].jobs);
    }
    while(pilot_native_epoch.buffers) {
        struct PilotNativeEpochBuffer *p=pilot_native_epoch.buffers;pilot_native_epoch.buffers=p->next;
        ID3D11Buffer_Release(p->buffer);free(p);
    }
    memset(&pilot_native_epoch,0,sizeof(pilot_native_epoch));
    memset(pilot_native_epoch_extents,0,sizeof(pilot_native_epoch_extents));
}
static ID3D11Buffer *pilot_native_epoch_buffer(ID3D11Buffer *source,UINT offset,UINT bytes,
    UINT destination,UINT size,UINT bind,UINT serial)
{
    if(!source || !bytes || !size || size>PILOT_NATIVE_EPOCH_BYTES)return NULL;
    /* Geometry sizes vary with each exact native draw span. Reuse bounded
     * capacity buckets while copying only that original span. Constant-buffer
     * CopyResource requires identical ByteWidth, so those remain exact. */
    UINT capacity=size;
    if(bind!=D3D11_BIND_CONSTANT_BUFFER) {
        capacity=256;
        while(capacity<size)capacity*=2;
    }
    if(pilot_native_epoch.bytes>PILOT_NATIVE_EPOCH_BYTES-capacity)return NULL;
    struct PilotNativeEpochBuffer *p=pilot_native_epoch.buffers;
    while(p && (p->used || p->size!=capacity || p->bind!=bind))p=p->next;
    if(!p) {
        /* Only unused, GPU-completed nodes are eligible for eviction. */
        struct PilotNativeEpochBuffer **at=&pilot_native_epoch.buffers;
        while((pilot_native_epoch.allocated>PILOT_NATIVE_EPOCH_BYTES-capacity ||
               pilot_native_epoch.buffer_count>=PILOT_NATIVE_EPOCH_BUFFERS) && *at) {
            struct PilotNativeEpochBuffer *old=*at;
            if(!old->used) {
                *at=old->next;pilot_native_epoch.allocated-=old->size;--pilot_native_epoch.buffer_count;
                ID3D11Buffer_Release(old->buffer);free(old);
            } else at=&old->next;
        }
        if(pilot_native_epoch.allocated>PILOT_NATIVE_EPOCH_BYTES-capacity ||
           pilot_native_epoch.buffer_count>=PILOT_NATIVE_EPOCH_BUFFERS)return NULL;
        p=calloc(1,sizeof(*p));if(!p)return NULL;
        D3D11_BUFFER_DESC desc={0};desc.ByteWidth=capacity;desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=bind;
        HRESULT result=ID3D11Device_CreateBuffer(d3d8_GetD3D11Device(),&desc,NULL,&p->buffer);
        if(FAILED(result)){free(p);return NULL;}
        p->size=capacity;p->bind=bind;p->next=pilot_native_epoch.buffers;pilot_native_epoch.buffers=p;
        pilot_native_epoch.allocated+=capacity;++pilot_native_epoch.buffer_count;
    }
    if(pilot_native_epoch.copy_count>=PILOT_NATIVE_EPOCH_COPIES)return NULL;
    p->used=serial;p->group=pilot_native_epoch.recording;pilot_native_epoch.bytes+=capacity;
    struct PilotNativeEpochCopy *copy=&pilot_native_epoch.copies[pilot_native_epoch.copy_count++];
    copy->source=source;copy->destination=p->buffer;copy->offset=offset;copy->bytes=bytes;copy->target=destination;copy->bind=bind;
    return p->buffer;
}
struct PilotNativeEpochJob {
    struct PreparedState state;
    void *vertex_versions[D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT];
    void *constant_versions[PILOT_NATIVE_EPOCH_STAGES][D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT];
    void *index_version;
    UINT count,first;INT base;unsigned indexed;
};
static void pilot_native_epoch_state_release(struct PreparedState *s)
{prepared_state_release(s);}

static int pilot_native_epoch_jobs_prepare(struct PilotNativeEpochGroup *group)
{
    if(!group->jobs) {
        group->jobs=calloc(PILOT_NATIVE_EPOCH_DRAWS,sizeof(*group->jobs));
        if(group->jobs)pilot_native_epoch.packet_bytes+=(unsigned)(PILOT_NATIVE_EPOCH_DRAWS*sizeof(*group->jobs));
    }
    return group->jobs!=NULL;
}
static void pilot_native_epoch_job_release(struct PilotNativeEpochJob *job,int retained)
{
    for(unsigned slot=0;slot<job->state.vertex_count;++slot)
        pilot_native_cb_version_release(&job->vertex_versions[slot]);
    pilot_native_cb_version_release(&job->index_version);
    for(unsigned stage=0;stage<PILOT_NATIVE_EPOCH_STAGES;++stage)
        for(unsigned slot=0;slot<job->state.constant_count[stage];++slot)
            pilot_native_cb_version_release(&job->constant_versions[stage][slot]);
    if(retained)pilot_native_epoch_state_release(&job->state);
    else prepared_state_clear(&job->state);
}
static void pilot_native_epoch_jobs_release(struct PilotNativeEpochGroup *group)
{
    for(unsigned i=0;i<group->job_count;++i)pilot_native_epoch_job_release(&group->jobs[i],0);
    /* The destructors clear every owned field. Draw parameters are overwritten
     * at admission; do not clear the large binding snapshots a second time. */
    group->job_count=0;
}
static int pilot_native_epoch_state_capture(struct PreparedState *s,ID3D11DeviceContext *native)
{
    if(prepared_state_snapshot(native,s)) {
        static int reported;
        if(!reported){reported=1;fprintf(stderr,"[NATIVE PREPARED STATE] setter_owned_snapshots=1\n");}
        return 1;
    }
    ID3D11HullShader *hull=NULL;ID3D11DomainShader *domain=NULL;
    ID3D11Buffer *stream_outputs[D3D11_SO_BUFFER_SLOT_COUNT]={0};
    ID3D11UnorderedAccessView *uavs[D3D11_PS_CS_UAV_REGISTER_COUNT]={0};
    ID3D11UnorderedAccessView *compute_uavs[D3D11_PS_CS_UAV_REGISTER_COUNT]={0};
    ID3D11ShaderResourceView *inactive_resources[3][D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT]={0};
    ID3D11DeviceContext_HSGetShader(native,&hull,NULL,NULL);ID3D11DeviceContext_DSGetShader(native,&domain,NULL,NULL);
    ID3D11DeviceContext_SOGetTargets(native,D3D11_SO_BUFFER_SLOT_COUNT,stream_outputs);
    ID3D11DeviceContext_OMGetRenderTargetsAndUnorderedAccessViews(native,0,NULL,NULL,0,D3D11_PS_CS_UAV_REGISTER_COUNT,uavs);
    ID3D11DeviceContext_CSGetUnorderedAccessViews(native,0,D3D11_PS_CS_UAV_REGISTER_COUNT,compute_uavs);
    ID3D11DeviceContext_HSGetShaderResources(native,0,D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT,inactive_resources[0]);
    ID3D11DeviceContext_DSGetShaderResources(native,0,D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT,inactive_resources[1]);
    ID3D11DeviceContext_CSGetShaderResources(native,0,D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT,inactive_resources[2]);
    int unsupported=hull!=NULL || domain!=NULL;
    if(hull)ID3D11HullShader_Release(hull);if(domain)ID3D11DomainShader_Release(domain);
    for(unsigned i=0;i<D3D11_SO_BUFFER_SLOT_COUNT;++i)if(stream_outputs[i]){unsupported=1;ID3D11Buffer_Release(stream_outputs[i]);}
    for(unsigned i=0;i<D3D11_PS_CS_UAV_REGISTER_COUNT;++i)if(uavs[i]){unsupported=1;ID3D11UnorderedAccessView_Release(uavs[i]);}
    for(unsigned i=0;i<D3D11_PS_CS_UAV_REGISTER_COUNT;++i)if(compute_uavs[i]){unsupported=1;ID3D11UnorderedAccessView_Release(compute_uavs[i]);}
    for(unsigned stage=0;stage<3;++stage)for(unsigned i=0;i<D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT;++i)
        if(inactive_resources[stage][i]){unsupported=1;ID3D11ShaderResourceView_Release(inactive_resources[stage][i]);}
    if(unsupported)return 0;
    s->vertex_count=D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT;
    for(unsigned stage=0;stage<PILOT_NATIVE_EPOCH_STAGES;++stage) {
        s->constant_count[stage]=D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT;
        s->resource_count[stage]=D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT;
        s->sampler_count[stage]=D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT;
    }
    ID3D11DeviceContext_IAGetInputLayout(native,&s->layout);
    ID3D11DeviceContext_IAGetVertexBuffers(native,0,D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT,s->vertices,s->strides,s->offsets);
    ID3D11DeviceContext_IAGetIndexBuffer(native,&s->index,&s->index_format,&s->index_offset);
    ID3D11DeviceContext_IAGetPrimitiveTopology(native,&s->topology);
    for(unsigned i=0;i<PILOT_NATIVE_EPOCH_STAGES;++i)s->class_count[i]=PILOT_NATIVE_EPOCH_CLASSES;
    ID3D11DeviceContext_VSGetShader(native,&s->vertex,s->classes[0],&s->class_count[0]);
    ID3D11DeviceContext_GSGetShader(native,&s->geometry,s->classes[1],&s->class_count[1]);
    ID3D11DeviceContext_PSGetShader(native,&s->pixel,s->classes[2],&s->class_count[2]);
    if(!s->vertex)s->class_count[0]=0;if(!s->geometry)s->class_count[1]=0;if(!s->pixel)s->class_count[2]=0;
#define EPOCH_GET(stage,n) \
    ID3D11DeviceContext_##stage##GetConstantBuffers(native,0,D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT,s->constants[n]); \
    ID3D11DeviceContext_##stage##GetShaderResources(native,0,D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT,s->resources[n]); \
    ID3D11DeviceContext_##stage##GetSamplers(native,0,D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT,s->samplers[n])
    EPOCH_GET(VS,0);EPOCH_GET(GS,1);EPOCH_GET(PS,2);
#undef EPOCH_GET
    ID3D11DeviceContext_RSGetState(native,&s->raster);
    ID3D11DeviceContext_OMGetBlendState(native,&s->blend,s->factors,&s->sample_mask);
    ID3D11DeviceContext_OMGetDepthStencilState(native,&s->depth,&s->stencil);
    ID3D11DeviceContext_GetPredication(native,&s->predicate,&s->predicate_value);
    /* A predicate is a mutable query result, not an immutable state object.
     * References alone cannot freeze it across queued replay. */
    if(s->predicate)return 0;
    /* Buffer SRVs would need their own content snapshots, not just COM refs.
     * The current guest paths have texture SRVs; decline other future routes. */
    for(unsigned stage=0;stage<PILOT_NATIVE_EPOCH_STAGES;++stage)
        for(unsigned i=0;i<D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT;++i)if(s->resources[stage][i]) {
            ID3D11Resource *resource=NULL;D3D11_RESOURCE_DIMENSION type;
            ID3D11ShaderResourceView_GetResource(s->resources[stage][i],&resource);
            ID3D11Resource_GetType(resource,&type);ID3D11Resource_Release(resource);
            if(type==D3D11_RESOURCE_DIMENSION_BUFFER)return 0;
        }
    ID3D11DeviceContext_OMGetRenderTargets(native,D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT,s->outputs,&s->target_depth);
    s->viewport_count=D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
    s->scissor_count=D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
    ID3D11DeviceContext_RSGetViewports(native,&s->viewport_count,s->viewports);
    ID3D11DeviceContext_RSGetScissorRects(native,&s->scissor_count,s->scissors);
    prepared_state_derive_extents(s);return 1;
}
/* Private replay is a balanced raw-context scope: the serialized native
 * binding authority stays frozen, and the complete saved context is restored
 * before any owner lookup, source mutation, publication or retirement. */
static void pilot_native_epoch_state_bind(ID3D11DeviceContext *context,const struct PreparedState *s)
{
    ID3D11ShaderResourceView *none[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT]={0};
    /* OM transitions may implicitly unbind aliased SRVs. Restore/bind from the
     * owned packet, never from state reread after an output transition. */
    context->lpVtbl->VSSetShaderResources(context,0,D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT,none);
    context->lpVtbl->GSSetShaderResources(context,0,D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT,none);
    context->lpVtbl->PSSetShaderResources(context,0,D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT,none);
    context->lpVtbl->OMSetRenderTargets(context,D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT,s->outputs,s->target_depth);
    context->lpVtbl->IASetInputLayout(context,s->layout);
    context->lpVtbl->IASetVertexBuffers(context,0,D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT,s->vertices,s->strides,s->offsets);
    context->lpVtbl->IASetIndexBuffer(context,s->index,s->index_format,s->index_offset);
    context->lpVtbl->IASetPrimitiveTopology(context,s->topology);
    context->lpVtbl->VSSetShader(context,s->vertex,s->classes[0],s->class_count[0]);
    context->lpVtbl->GSSetShader(context,s->geometry,s->classes[1],s->class_count[1]);
    context->lpVtbl->PSSetShader(context,s->pixel,s->classes[2],s->class_count[2]);
#define EPOCH_BIND(stage,n) \
    context->lpVtbl->stage##SetConstantBuffers(context,0,D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT,s->constants[n]); \
    context->lpVtbl->stage##SetShaderResources(context,0,D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT,s->resources[n]); \
    context->lpVtbl->stage##SetSamplers(context,0,D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT,s->samplers[n])
    EPOCH_BIND(VS,0);EPOCH_BIND(GS,1);EPOCH_BIND(PS,2);
#undef EPOCH_BIND
    context->lpVtbl->RSSetState(context,s->raster);
    context->lpVtbl->RSSetViewports(context,s->viewport_count,s->viewports);
    context->lpVtbl->RSSetScissorRects(context,s->scissor_count,s->scissors);
    context->lpVtbl->OMSetBlendState(context,s->blend,s->factors,s->sample_mask);
    context->lpVtbl->OMSetDepthStencilState(context,s->depth,s->stencil);
    context->lpVtbl->SetPredication(context,s->predicate,s->predicate_value);
}
static int pilot_native_prepared_jobs_enabled(void)
{
    static int enabled=-1;
    if(enabled<0) {
        const char *value=getenv("XLLE_PILOT_NATIVE_PREPARED_JOBS");
        enabled=!value || !strcmp(value,"1");
    }
    return enabled;
}
static void pilot_native_cb_epoch_barrier(unsigned reason)
{
    pilot_native_epoch_flush();
    if(reason==PILOT_CB_UNSUPPORTED_MAP) {
        for(unsigned i=0;i<PILOT_NATIVE_EPOCH_GROUPS;++i) {
            struct PilotNativeEpochGroup *group=&pilot_native_epoch.groups[i];
            for(unsigned j=0;j<group->output_count;++j)
                group->outputs[j]->valid_aspects=group->outputs[j]->coherent=0;
        }
        pilot_native_degrade();
    }
}
/* A recorded group owns all of these immutable binding versions. Draw itself
 * does not change them. Within one output lifetime, emit only real changes;
 * clearing/rebinding every texture and target for every packet defeats the
 * purpose of recording a coherent HD pass. Output transitions retain the
 * original complete unbind/bind path, including its alias handling. */
static void pilot_native_epoch_state_bind_changes(ID3D11DeviceContext *context,
    const struct PreparedState *before,const struct PreparedState *s)
{
    if(before->target_depth!=s->target_depth ||
       memcmp(before->outputs,s->outputs,sizeof(s->outputs))) {
        pilot_native_epoch_state_bind(context,s);return;
    }
    if(before->layout!=s->layout)context->lpVtbl->IASetInputLayout(context,s->layout);
    unsigned first=D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT,last=0;
    for(unsigned i=0;i<D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT;++i)
        if(before->vertices[i]!=s->vertices[i] || before->strides[i]!=s->strides[i] ||
           before->offsets[i]!=s->offsets[i]) {
            if(first==D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT)first=i;
            last=i+1;
        }
    if(last)context->lpVtbl->IASetVertexBuffers(context,first,last-first,
        s->vertices+first,s->strides+first,s->offsets+first);
    if(before->index!=s->index || before->index_format!=s->index_format || before->index_offset!=s->index_offset)
        context->lpVtbl->IASetIndexBuffer(context,s->index,s->index_format,s->index_offset);
    if(before->topology!=s->topology)context->lpVtbl->IASetPrimitiveTopology(context,s->topology);
#define EPOCH_SHADER_CHANGED(field,n) (before->field!=s->field || before->class_count[n]!=s->class_count[n] || \
    memcmp(before->classes[n],s->classes[n],s->class_count[n]*sizeof(s->classes[n][0])))
    if(EPOCH_SHADER_CHANGED(vertex,0))context->lpVtbl->VSSetShader(context,s->vertex,s->classes[0],s->class_count[0]);
    if(EPOCH_SHADER_CHANGED(geometry,1))context->lpVtbl->GSSetShader(context,s->geometry,s->classes[1],s->class_count[1]);
    if(EPOCH_SHADER_CHANGED(pixel,2))context->lpVtbl->PSSetShader(context,s->pixel,s->classes[2],s->class_count[2]);
#undef EPOCH_SHADER_CHANGED
#define EPOCH_CHANGED_RANGE(stage,n,field,bound,setter) do { \
    unsigned limit=before->bound[n]>s->bound[n]?before->bound[n]:s->bound[n]; \
    unsigned a=limit,b=0; \
    for(unsigned i=0;i<(limit);++i)if(before->field[n][i]!=s->field[n][i]) {if(a==(limit))a=i;b=i+1;} \
    if(b)context->lpVtbl->stage##setter(context,a,b-a,s->field[n]+a); \
}while(0)
#define EPOCH_CHANGED_STAGE(stage,n) \
    EPOCH_CHANGED_RANGE(stage,n,constants,constant_count,SetConstantBuffers); \
    EPOCH_CHANGED_RANGE(stage,n,resources,resource_count,SetShaderResources); \
    EPOCH_CHANGED_RANGE(stage,n,samplers,sampler_count,SetSamplers)
    EPOCH_CHANGED_STAGE(VS,0);EPOCH_CHANGED_STAGE(GS,1);EPOCH_CHANGED_STAGE(PS,2);
#undef EPOCH_CHANGED_STAGE
#undef EPOCH_CHANGED_RANGE
    if(before->raster!=s->raster)context->lpVtbl->RSSetState(context,s->raster);
    if(before->viewport_count!=s->viewport_count ||
       memcmp(before->viewports,s->viewports,s->viewport_count*sizeof(s->viewports[0])))
        context->lpVtbl->RSSetViewports(context,s->viewport_count,s->viewports);
    if(before->scissor_count!=s->scissor_count ||
       memcmp(before->scissors,s->scissors,s->scissor_count*sizeof(s->scissors[0])))
        context->lpVtbl->RSSetScissorRects(context,s->scissor_count,s->scissors);
    if(before->blend!=s->blend || before->sample_mask!=s->sample_mask ||
       memcmp(before->factors,s->factors,sizeof(s->factors)))
        context->lpVtbl->OMSetBlendState(context,s->blend,s->factors,s->sample_mask);
    if(before->depth!=s->depth || before->stencil!=s->stencil)
        context->lpVtbl->OMSetDepthStencilState(context,s->depth,s->stencil);
    if(before->predicate!=s->predicate || before->predicate_value!=s->predicate_value)
        context->lpVtbl->SetPredication(context,s->predicate,s->predicate_value);
}
static int pilot_native_epoch_replay(struct PilotNativeEpochGroup *group)
{
    ID3D11DeviceContext *context=d3d8_GetD3D11Context();
    if(!context)return 0;
    struct PreparedState saved={0};ID3D11Query *excluded=NULL;
    int admitted=pilot_native_epoch_state_capture(&saved,context);
    if(admitted)admitted=pilot_visibility_internal_try_begin(&excluded);
    if(admitted) {
        const struct PreparedState *previous=&saved;
        for(unsigned i=0;i<group->job_count;++i) {
            const struct PilotNativeEpochJob *job=&group->jobs[i];
            if(pilot_native_prepared_jobs_enabled())pilot_native_epoch_state_bind_changes(context,previous,&job->state);
            else pilot_native_epoch_state_bind(context,&job->state);
            if(job->indexed)ID3D11DeviceContext_DrawIndexed(context,job->count,job->first,job->base);
            else ID3D11DeviceContext_Draw(context,job->count,job->first);
            previous=&job->state;
        }
        pilot_visibility_internal_end(excluded);
        pilot_native_epoch_state_bind(context,&saved);
    }
    pilot_native_epoch_state_release(&saved);return admitted;
}
static int pilot_native_epoch_record(struct PilotNativeDrawState *draw,UINT count,UINT first,INT base,int indexed)
{
    if(!pilot_native_epoch_enabled())return 0;
    if(pilot_native_epoch.draws>=PILOT_NATIVE_EPOCH_DRAWS) {
        pilot_native_epoch_flush();return 0; /* Re-admit current immediate fallback after any degradation. */
    }
    if(!pilot_native_epoch_reserve_group())return 0;
    pilot_native_epoch.copy_count=0;
    struct PilotNativeEpochGroup *group=&pilot_native_epoch.groups[pilot_native_epoch.recording-1];
    struct PilotNativeEpochJob *job=&group->jobs[group->job_count];
    /* The unpublished packet owns admission state and version pins directly.
     * Publish job_count only after every reservation succeeds. Failed admission
     * uses the same destructor as GPU retirement, leaving this slot reusable. */
    struct PreparedState *s=&job->state;ID3D11DeviceContext *native=draw->context;
    /* Capture scratch is not part of immutable replay state. */
    ID3D11Buffer *vertex_copies[D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT]={0},*index_copy=NULL;
    ID3D11Buffer *constant_copies[PILOT_NATIVE_EPOCH_STAGES][D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT]={{0}};
    UINT copy_offsets[D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT]={0};
    unsigned before_bytes=pilot_native_epoch.bytes,serial=++pilot_native_epoch.serial;
    if(!serial)serial=++pilot_native_epoch.serial;
    int admitted=0,borrowed_state=prepared_state_snapshot_borrow(native,s);
    unsigned borrowed=0,borrowed_constants=0;
    if(!borrowed_state && !pilot_native_epoch_state_capture(s,native))goto finished;
    uint32_t prepared_extents[16]={0};unsigned prepared_streams=pilot_gfx_shader_prepared_streams(prepared_extents);
    for(unsigned i=0;i<s->vertex_count;++i)if(s->vertices[i]) {
        if(pilot_native_prepared_jobs_enabled() &&
           pilot_native_cb_version_borrow(s->vertices[i],&job->vertex_versions[i])) {
            vertex_copies[i]=s->vertices[i];copy_offsets[i]=s->offsets[i];++borrowed;continue;
        }
        D3D11_BUFFER_DESC desc;ID3D11Buffer_GetDesc(s->vertices[i],&desc);
        if(desc.Usage==D3D11_USAGE_DEFAULT && !pilot_native_buffer_registered(s->vertices[i]) &&
           pilot_native_vertex_borrowable(i,s->vertices[i])) {
            /* The owner submits all queued readers before a changed write.
             * The capture Get ref becomes the packet's owned original ref. */
            vertex_copies[i]=s->vertices[i];copy_offsets[i]=s->offsets[i];++borrowed;continue;
        }
        UINT offset=0,bytes=desc.ByteWidth,destination=0,size=desc.ByteWidth;
        unsigned extent=pilot_native_epoch_extents[i];
        if(!extent && i<16 && (prepared_streams&(1u<<i)))extent=prepared_extents[i];
        if(!indexed && count && extent && extent<=s->strides[i]) {
            uint64_t begin=(uint64_t)s->offsets[i]+(uint64_t)first*s->strides[i];
            uint64_t length=(uint64_t)count*s->strides[i],end=(uint64_t)(first)+(uint64_t)count;
            uint64_t target=end*s->strides[i];
            if(begin<=desc.ByteWidth && length<=desc.ByteWidth-begin && target<=UINT32_MAX &&
               !((begin|length|target)&3)) {
                offset=(UINT)begin;bytes=(UINT)length;destination=first*s->strides[i];size=(UINT)target;
                copy_offsets[i]=0;
            } else goto finished;
        } else copy_offsets[i]=s->offsets[i];
        vertex_copies[i]=pilot_native_epoch_buffer(s->vertices[i],offset,bytes,destination,size,D3D11_BIND_VERTEX_BUFFER,serial);
        if(!vertex_copies[i])goto finished;
    }
    if(indexed) {
        if(!s->index)goto finished;
        if(pilot_native_prepared_jobs_enabled() &&
           pilot_native_cb_version_borrow(s->index,&job->index_version))index_copy=s->index;
        else {
            D3D11_BUFFER_DESC desc;ID3D11Buffer_GetDesc(s->index,&desc);
            if(pilot_native_prepared_jobs_enabled() && desc.Usage==D3D11_USAGE_IMMUTABLE)index_copy=s->index;
            else index_copy=pilot_native_epoch_buffer(s->index,0,desc.ByteWidth,0,desc.ByteWidth,D3D11_BIND_INDEX_BUFFER,serial);
        }
        if(!index_copy)goto finished;
    }
    for(unsigned stage=0;stage<PILOT_NATIVE_EPOCH_STAGES;++stage)
        for(unsigned i=0;i<s->constant_count[stage];++i)if(s->constants[stage][i]) {
            if(pilot_native_prepared_jobs_enabled() &&
               pilot_native_cb_version_borrow(s->constants[stage][i],&job->constant_versions[stage][i])) {
                constant_copies[stage][i]=s->constants[stage][i];++borrowed_constants;continue;
            }
            D3D11_BUFFER_DESC desc;ID3D11Buffer_GetDesc(s->constants[stage][i],&desc);
            /* D3D11 constant buffers require a full-resource copy. */
            ID3D11Buffer *copy=pilot_native_epoch_buffer(s->constants[stage][i],0,desc.ByteWidth,0,desc.ByteWidth,D3D11_BIND_CONSTANT_BUFFER,serial);
            if(!copy)goto finished;constant_copies[stage][i]=copy;
        }
    /* Snapshot bindings replace unproved mutable sources; known owned mesh
     * bindings keep their captured refs and original byte offsets. */
    for(unsigned i=0;i<s->vertex_count;++i)if(s->vertices[i]) {
        if(s->vertices[i]!=vertex_copies[i]) {
            if(!borrowed_state)ID3D11Buffer_Release(s->vertices[i]);s->vertices[i]=vertex_copies[i];
            if(!borrowed_state)ID3D11Buffer_AddRef(s->vertices[i]);
        }
        s->offsets[i]=copy_offsets[i];
    }
    ID3D11Buffer *replayed_index=indexed?index_copy:NULL;
    if(s->index!=replayed_index) {
        if(s->index && !borrowed_state)ID3D11Buffer_Release(s->index);
        s->index=replayed_index;if(s->index && !borrowed_state)ID3D11Buffer_AddRef(s->index);
    }
    for(unsigned stage=0;stage<PILOT_NATIVE_EPOCH_STAGES;++stage)
        for(unsigned i=0;i<s->constant_count[stage];++i)if(s->constants[stage][i]) {
            if(s->constants[stage][i]!=constant_copies[stage][i]) {
                if(!borrowed_state)ID3D11Buffer_Release(s->constants[stage][i]);s->constants[stage][i]=constant_copies[stage][i];
                if(!borrowed_state)ID3D11Buffer_AddRef(s->constants[stage][i]);
            }
        }
    for(unsigned i=0;i<D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT;++i) {
        if(s->outputs[i] && !borrowed_state)ID3D11RenderTargetView_Release(s->outputs[i]);s->outputs[i]=NULL;
    }
    s->outputs[0]=draw->enhanced_color;if(s->outputs[0] && !borrowed_state)ID3D11RenderTargetView_AddRef(s->outputs[0]);
    if(s->target_depth && !borrowed_state)ID3D11DepthStencilView_Release(s->target_depth);
    s->target_depth=draw->enhanced_depth;if(s->target_depth && !borrowed_state)ID3D11DepthStencilView_AddRef(s->target_depth);
    for(unsigned i=0;i<PILOT_NATIVE_DRAW_SRVS;++i) {
        if(s->resources[2][i] && !borrowed_state)ID3D11ShaderResourceView_Release(s->resources[2][i]);
        s->resources[2][i]=draw->enhanced_inputs[i];
        if(s->resources[2][i] && !borrowed_state)ID3D11ShaderResourceView_AddRef(s->resources[2][i]);
    }
    s->viewport_count=draw->viewport_count;s->scissor_count=draw->scissor_count;
    memcpy(s->viewports,draw->enhanced_viewports,sizeof(s->viewports));
    memcpy(s->scissors,draw->enhanced_scissors,sizeof(s->scissors));
    job->count=count;job->first=first;job->base=base;job->indexed=(unsigned)indexed;
    if(!prepared_state_lease_retain(&group->resources,s))goto finished;
    if(!borrowed_state){prepared_state_drop_refs(s);borrowed_state=1;}
    /* All reservations, including packet-wide leases, succeeded before any
     * copy command. Admission failure can still roll back without GPU writes. */
    for(unsigned i=0;i<pilot_native_epoch.copy_count;++i) {
        struct PilotNativeEpochCopy *copy=&pilot_native_epoch.copies[i];
        if(pilot_native_cpu.active)pilot_native_cpu.copy_bytes+=copy->bytes;
        if(copy->bind==D3D11_BIND_CONSTANT_BUFFER)
            ID3D11DeviceContext_CopyResource(native,(ID3D11Resource*)copy->destination,(ID3D11Resource*)copy->source);
        else {
            D3D11_BOX box={copy->offset,0,0,copy->offset+copy->bytes,1,1};
            ID3D11DeviceContext_CopySubresourceRegion(native,(ID3D11Resource*)copy->destination,
                0,copy->target,0,0,(ID3D11Resource*)copy->source,0,&box);
        }
    }
    ++group->job_count; /* This packet now owns the complete immutable draw. */
    if(pilot_native_prepared_jobs_enabled()) {
        static int reported=0;
        for(unsigned i=0;!reported && i<D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT;++i)
            if(job->vertex_versions[i]) {
                reported=1;fprintf(stderr,"[NATIVE PREPARED VB] shared_uploaded_versions=1 retirement=immediate-ordering\n");
            }
    }
    if(draw->color_plane){pilot_native_plane_retain(draw->color_plane);pilot_native_epoch.outputs[pilot_native_epoch.output_count++]=draw->color_plane;}
    if(draw->enhanced_depth){pilot_native_plane_retain(draw->depth_plane);pilot_native_epoch.outputs[pilot_native_epoch.output_count++]=draw->depth_plane;}
    ++pilot_native_epoch.draws;pilot_native_epoch.borrowed_vertices+=borrowed;
    pilot_native_epoch.borrowed_constants+=borrowed_constants;admitted=1;
    if(job->index_version)++pilot_native_epoch.borrowed_indices;
finished:
    if(!admitted) {
        pilot_native_epoch_job_release(job,!borrowed_state);
        for(struct PilotNativeEpochBuffer *p=pilot_native_epoch.buffers;p;p=p->next)if(p->used==serial)p->used=p->group=0;
        pilot_native_epoch.bytes=before_bytes;
        pilot_native_epoch_cancel_empty();
    }
    pilot_native_epoch.copy_count=0;
    return admitted;
}
