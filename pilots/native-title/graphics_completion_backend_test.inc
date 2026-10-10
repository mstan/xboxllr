static int completion_wait(unsigned kind,uint32_t handle)
{
    ULONGLONG deadline=GetTickCount64()+5000;
    while(pilot_gfx_resource_busy(kind,handle)) {
        CHECK(GetTickCount64()<deadline);Sleep(0);
    }
    return 0;
}
static int test_resource_completion(void)
{
    uint32_t vertex=0,texture=0,data[32];
    CHECK(pilot_gfx_vertex_create(sizeof(data),&vertex)==0);
    CHECK(pilot_gfx_texture_create_usage(8,4,1,0x12,1,&texture)==0);
    struct PilotGpuProgress before,after;pilot_gfx_completion_stats(&before);
    CHECK(!pilot_gfx_resource_busy(PILOT_GFX_VERTEX,vertex));
    CHECK(!pilot_gfx_resource_busy(PILOT_GFX_TEXTURE,texture));
    pilot_gfx_completion_stats(&after);CHECK(after.created_queries==before.created_queries);
    for(unsigned i=0;i<32;++i)data[i]=0xff13579b;
    pilot_gfx_vertex_upload(vertex,data,sizeof(data));
    CHECK(pilot_gfx_texture_clear(texture,0xff2468ac)==0);
    pilot_gfx_completion_stats(&before);CHECK(before.submitted>before.completed);
    CHECK(completion_wait(PILOT_GFX_VERTEX,vertex)==0);
    CHECK(completion_wait(PILOT_GFX_TEXTURE,texture)==0);
    pilot_gfx_completion_stats(&after);
    CHECK(after.created_queries==before.created_queries+1 && !after.pending_queries);
    CHECK(after.completed==before.submitted); /* one event covers both objects */
    unsigned char pixels[8*4*4];CHECK(pilot_gfx_texture_readback(texture,0,pixels,sizeof(pixels))==0);
    uint32_t pixel;memcpy(&pixel,pixels,4);CHECK(pixel==0xff2468ac);
    for(unsigned i=0;i<32;++i)CHECK(!pilot_gfx_resource_busy(PILOT_GFX_VERTEX,vertex));
    pilot_gfx_vertex_upload(vertex,data,sizeof(data)); /* unchanged shadow needs no new GPU work */
    pilot_gfx_completion_stats(&before);
    CHECK(before.submitted==after.submitted && before.created_queries==after.created_queries);
    data[7]^=0x11223344;pilot_gfx_vertex_upload(vertex,data,sizeof(data));
    CHECK(completion_wait(PILOT_GFX_VERTEX,vertex)==0);
    pilot_gfx_completion_stats(&after);CHECK(after.created_queries==before.created_queries+1 && after.completed>before.completed);
    uint32_t readback[32];CHECK(pilot_gfx_vertex_readback(vertex,readback,sizeof(readback))==0 && !memcmp(data,readback,sizeof(data)));
    /* A mutation outside the consumed range must not change queued GPU data;
     * it becomes visible when a later draw consumes that range. */
    pilot_gfx_vertex_bind(5,vertex,4);
    ID3D11Buffer *bound=NULL,*snapshot=NULL;UINT stride=0,offset=0;
    ID3D11DeviceContext *context=d3d8_GetD3D11Context();
    ID3D11DeviceContext_IAGetVertexBuffers(context,5,1,&bound,&stride,&offset);
    CHECK(bound && stride==4);pilot_gfx_vertex_bind(5,0,0);
    D3D11_BUFFER_DESC desc;ID3D11Buffer_GetDesc(bound,&desc);
    desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    CHECK(SUCCEEDED(ID3D11Device_CreateBuffer(d3d8_GetD3D11Device(),&desc,NULL,&snapshot)));
    ID3D11DeviceContext_CopyResource(context,(ID3D11Resource*)snapshot,(ID3D11Resource*)bound);
    ID3D11Buffer_Release(bound);uint32_t queued[32];memcpy(queued,data,sizeof(queued));
    uint32_t previous=data[20];data[20]^=0xff00ff00;
    pilot_gfx_completion_stats(&before);
    pilot_gfx_vertex_upload_range(vertex,data,0,16);
    pilot_gfx_completion_stats(&after);CHECK(after.submitted==before.submitted);
    data[2]^=0x00ffff00;pilot_gfx_vertex_upload_range(vertex,data,4,12);
    D3D11_MAPPED_SUBRESOURCE mapped;
    CHECK(SUCCEEDED(ID3D11DeviceContext_Map(context,(ID3D11Resource*)snapshot,0,D3D11_MAP_READ,0,&mapped)));
    CHECK(!memcmp(mapped.pData,queued,sizeof(queued))); /* earlier queued work saw old contents */
    ID3D11DeviceContext_Unmap(context,(ID3D11Resource*)snapshot,0);ID3D11Buffer_Release(snapshot);
    CHECK(completion_wait(PILOT_GFX_VERTEX,vertex)==0);
    CHECK(pilot_gfx_vertex_readback(vertex,readback,sizeof(readback))==0);
    CHECK(readback[2]==data[2] && readback[20]==previous);
    pilot_gfx_vertex_upload_range(vertex,data,20*4,4);
    CHECK(pilot_gfx_vertex_readback(vertex,readback,sizeof(readback))==0 && !memcmp(data,readback,sizeof(data)));
    /* Overlap, then full synchronization, retains all earlier updated spans. */
    data[3]^=0x11223344;pilot_gfx_vertex_upload_range(vertex,data,8,12);
    data[31]^=0x55667788;pilot_gfx_vertex_upload(vertex,data,sizeof(data));
    CHECK(pilot_gfx_vertex_readback(vertex,readback,sizeof(readback))==0 && !memcmp(data,readback,sizeof(data)));
    CHECK(pilot_gfx_clear(7,0xff123456,.5f,7)==0);
    CHECK(completion_wait(PILOT_GFX_SURFACE,0)==0 && completion_wait(PILOT_GFX_SURFACE,2)==0);
    CHECK(pilot_gfx_present()==0 && completion_wait(PILOT_GFX_SURFACE,1)==0);
    pilot_gfx_vertex_destroy(vertex);pilot_gfx_texture_destroy(texture);
    CHECK(pilot_gfx_vertex_create(sizeof(data),&vertex)==0); /* no stale serial on slot reuse */
    pilot_gfx_completion_stats(&before);CHECK(!pilot_gfx_resource_busy(PILOT_GFX_VERTEX,vertex));
    pilot_gfx_completion_stats(&after);CHECK(after.created_queries==before.created_queries);
    pilot_gfx_vertex_upload_range(vertex,data,4,8); /* initialize a reused slot */
    CHECK(pilot_gfx_vertex_readback(vertex,readback,sizeof(readback))==0 && !memcmp(data,readback,sizeof(data)));
    pilot_gfx_vertex_destroy(vertex);
    /* Real GPU work may finish before the caller asks for its event. When a
     * duplicate is returned, it must outlive query retirement and still be
     * signaled after that owner closes its original handle. */
    unsigned event_waits=0;
    for(unsigned i=0;i<16;++i) {
        CHECK(pilot_gfx_clear(1,0xff102030+i,1,0)==0);
        uint64_t serial=pilot_gfx_checkpoint();
        HANDLE event=pilot_gfx_checkpoint_event(serial);
        if(event){CHECK(WaitForSingleObject(event,5000)==WAIT_OBJECT_0);++event_waits;}
        CHECK(completion_wait(PILOT_GFX_DEVICE,0)==0 && !pilot_gfx_checkpoint_busy(serial));
        if(event){CHECK(WaitForSingleObject(event,0)==WAIT_OBJECT_0);CHECK(CloseHandle(event));}
        unsigned char frame[64*64*4];CHECK(pilot_gfx_readback(frame,sizeof(frame))==0);
        CHECK(frame[0]==0x10 && frame[1]==0x20 && frame[2]==0x30+i);
    }
    fprintf(stderr,"PASS: real resource event completion, batch sharing, changed uploads, pixel results, idle reuse, no repeated idle queries and %u owned GPU event waits\n",event_waits);return 0;
}

#ifdef PILOT_D3D_PHOBOS
static int alias_completion_draw(void)
{
    struct {float x,y,z,w;uint32_t color;float u,v;} quad[]={
        {16,16,.5f,1,0xffffffff,.5f,.5f},{48,16,.5f,1,0xffffffff,.5f,.5f},
        {16,48,.5f,1,0xffffffff,.5f,.5f},{48,48,.5f,1,0xffffffff,.5f,.5f}};
    CHECK(!pilot_gfx_draw_up(D3DPT_TRIANGLESTRIP,2,quad,sizeof(quad[0]),0x144));
    return 0;
}
static int test_alias_completion(const uint32_t parameters[17])
{
    /* Submission ownership is exact even when a fast GPU has already completed.
     * No timing or pixel readback determines these expected use serials. */
    pilot_gfx_pixel_fixed();
    pilot_gfx_set_state(D3DRS_CULLMODE,D3DCULL_NONE);
    pilot_gfx_set_state(D3DRS_ZENABLE,0);pilot_gfx_set_state(D3DRS_LIGHTING,0);
    pilot_gfx_set_state(D3DRS_ALPHABLENDENABLE,0);
    for(unsigned i=0;i<4;++i)pilot_gfx_set_texture_state(i,D3DTSS_COLOROP,D3DTOP_DISABLE);
    pilot_gfx_set_texture_state(0,D3DTSS_COLOROP,D3DTOP_SELECTARG1);
    pilot_gfx_set_texture_state(0,D3DTSS_COLORARG1,D3DTA_TEXTURE);
    pilot_gfx_set_texture_state(0,D3DTSS_ALPHAOP,D3DTOP_SELECTARG1);
    pilot_gfx_set_texture_state(0,D3DTSS_ALPHAARG1,D3DTA_TEXTURE);
    CHECK(!pilot_gfx_clear(1,0xff123456,1,0));
    uint64_t original=pilot_gfx_resource_serial(PILOT_GFX_SURFACE,0);
    uint32_t target=0,alias=0;
    CHECK(!pilot_gfx_texture_create_usage(64,64,1,0x12,1,&target));
    CHECK(!pilot_gfx_texture_alias(PILOT_GFX_SURFACE,0,0x12,&alias));
    CHECK(!pilot_gfx_targets(target,0));pilot_gfx_texture_bind(0,alias);
    CHECK(!alias_completion_draw());
    struct PilotGpuProgress progress;pilot_gfx_completion_stats(&progress);
    uint64_t sampled=progress.submitted;
    CHECK(sampled>original && pilot_gfx_resource_serial(PILOT_GFX_TEXTURE,alias)==sampled);
    CHECK(pilot_gfx_resource_serial(PILOT_GFX_SURFACE,0)==sampled);
    pilot_gfx_texture_bind(0,0);pilot_gfx_texture_destroy(alias);
    CHECK(pilot_gfx_resource_serial(PILOT_GFX_SURFACE,0)==sampled);
    CHECK(!pilot_gfx_clear(1,0xff654321,1,0));
    CHECK(pilot_gfx_resource_serial(PILOT_GFX_TEXTURE,target)>sampled);
    CHECK(pilot_gfx_resource_serial(PILOT_GFX_SURFACE,0)==sampled);

    /* Shared ownership also survives an alias chain and original-wrapper
     * retirement. Allocation identity must not depend on a reusable slot. */
    uint32_t source=0,first=0,second=0;
    CHECK(!pilot_gfx_texture_create_usage(64,64,1,0x12,1,&source));
    CHECK(!pilot_gfx_texture_clear(source,0xff789abc));
    CHECK(!pilot_gfx_texture_alias(PILOT_GFX_TEXTURE,source,0x12,&first));
    CHECK(!pilot_gfx_texture_alias(PILOT_GFX_TEXTURE,first,0x12,&second));
    pilot_gfx_texture_bind(0,second);CHECK(!alias_completion_draw());
    pilot_gfx_completion_stats(&progress);sampled=progress.submitted;
    CHECK(pilot_gfx_resource_serial(PILOT_GFX_TEXTURE,source)==sampled);
    pilot_gfx_texture_bind(0,0);pilot_gfx_texture_destroy(second);
    CHECK(pilot_gfx_resource_serial(PILOT_GFX_TEXTURE,source)==sampled);
    pilot_gfx_texture_destroy(source);
    CHECK(pilot_gfx_resource_serial(PILOT_GFX_TEXTURE,first)==sampled);
    uint32_t reused=0;CHECK(!pilot_gfx_texture_create_usage(64,64,1,0x12,1,&reused));
    CHECK(!pilot_gfx_resource_serial(PILOT_GFX_TEXTURE,reused));
    pilot_gfx_texture_bind(0,first);CHECK(!alias_completion_draw());
    pilot_gfx_completion_stats(&progress);sampled=progress.submitted;
    pilot_gfx_texture_bind(0,0);pilot_gfx_texture_destroy(first);
    CHECK(!pilot_gfx_resource_serial(PILOT_GFX_TEXTURE,reused));
    pilot_gfx_texture_destroy(reused);

    /* Presentation replacement retires its owner but aliases retain old
     * storage. A new presentation allocation must not inherit old reads. */
    CHECK(!pilot_gfx_targets(0,UINT32_MAX));
    CHECK(!pilot_gfx_texture_alias(PILOT_GFX_SURFACE,0,0x12,&alias));
    original=pilot_gfx_resource_serial(PILOT_GFX_SURFACE,0);
    CHECK(pilot_gfx_resource_serial(PILOT_GFX_TEXTURE,alias)==original);
    CHECK(!pilot_gfx_reset(parameters));
    CHECK(pilot_gfx_resource_serial(PILOT_GFX_TEXTURE,alias)==original);
    uint64_t replacement=pilot_gfx_resource_serial(PILOT_GFX_SURFACE,0);
    CHECK(!pilot_gfx_targets(target,0));pilot_gfx_texture_bind(0,alias);
    CHECK(!alias_completion_draw());
    pilot_gfx_completion_stats(&progress);
    CHECK(pilot_gfx_resource_serial(PILOT_GFX_TEXTURE,alias)==progress.submitted);
    CHECK(pilot_gfx_resource_serial(PILOT_GFX_SURFACE,0)==replacement);
    pilot_gfx_texture_bind(0,0);pilot_gfx_texture_destroy(alias);
    CHECK(!pilot_gfx_targets(0,UINT32_MAX));pilot_gfx_texture_destroy(target);
    fprintf(stderr,"PASS: allocation use survives surface/texture alias retirement, owner retirement, slot reuse and reset; unrelated work remains separate\n");
    return 0;
}
#endif
