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
    CHECK(pilot_gfx_clear(7,0xff123456,.5f,7)==0);
    CHECK(completion_wait(PILOT_GFX_SURFACE,0)==0 && completion_wait(PILOT_GFX_SURFACE,2)==0);
    CHECK(pilot_gfx_present()==0 && completion_wait(PILOT_GFX_SURFACE,1)==0);
    pilot_gfx_vertex_destroy(vertex);pilot_gfx_texture_destroy(texture);
    CHECK(pilot_gfx_vertex_create(sizeof(data),&vertex)==0); /* no stale serial on slot reuse */
    pilot_gfx_completion_stats(&before);CHECK(!pilot_gfx_resource_busy(PILOT_GFX_VERTEX,vertex));
    pilot_gfx_completion_stats(&after);CHECK(after.created_queries==before.created_queries);
    pilot_gfx_vertex_destroy(vertex);
    fprintf(stderr,"PASS: real resource event completion, batch sharing, changed uploads, pixel results, idle reuse and no repeated idle queries\n");return 0;
}
