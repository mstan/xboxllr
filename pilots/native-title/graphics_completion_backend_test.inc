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
