#ifdef PILOT_D3D_PHOBOS
struct BatchTestVertex {float position[4],color[4];};
static void batch_test_vertex(struct BatchTestVertex *v,float x,float y,unsigned color)
{
    *v=(struct BatchTestVertex){{x,y,.5f,1},
        {((color>>16)&255)/255.f,((color>>8)&255)/255.f,(color&255)/255.f,((color>>24)&255)/255.f}};
}
static int batch_test_read(unsigned char pixels[64*64*4])
{CHECK(!pilot_gfx_readback(pixels,64*64*4));return 0;}
static int batch_test_report(unsigned index,uint32_t *samples)
{
    uint64_t stamp;DWORD start=GetTickCount();int32_t hr;
    do {hr=pilot_gfx_visibility_result(index,samples,&stamp);if((uint32_t)hr!=0x88760828u)break;Sleep(1);}
    while(GetTickCount()-start<3000);
    CHECK(!hr);return 0;
}
static int test_strip_batch(void)
{
    const uint32_t program[]={0,0x0020001b,0x08000000,0x0000f800,
                            0,0x0020021b,0x08000000,0x0000f819};
    struct PilotVertexElement elements[16]={0};
    elements[0]=(struct PilotVertexElement){0x42,0,0};
    elements[1]=(struct PilotVertexElement){0x42,0,16};
    uint32_t shader=0,buffer=0;
    struct BatchTestVertex vertices[320]={0}; /* crosses two upload pages */
    const float xy[][2]={{2,2},{2,32},{34,2},
        {8,8},{8,54},{52,8},{52,54},
        {20,12},{20,58},{40,12},{40,58},{60,12}};
    for(unsigned i=0;i<12;++i)
        batch_test_vertex(vertices+i,xy[i][0],xy[i][1],i<3?0x80ff0000:i<7?0x8000ff00:0x800000ff);
    CHECK(!pilot_gfx_shader_create(program,2,elements,&shader));
    CHECK(!pilot_gfx_vertex_create(sizeof(vertices),&buffer));
    pilot_gfx_vertex_upload(buffer,vertices,sizeof(vertices));pilot_gfx_vertex_bind(0,buffer,sizeof(vertices[0]));
    pilot_gfx_pixel_fixed();
    pilot_gfx_set_state(D3DRS_ZENABLE,0);pilot_gfx_set_state(D3DRS_ZWRITEENABLE,0);
    pilot_gfx_set_state(D3DRS_LIGHTING,0);pilot_gfx_set_state(D3DRS_SPECULARENABLE,0);
    pilot_gfx_set_state(D3DRS_SHADEMODE,2);pilot_gfx_set_state(D3DRS_FOGENABLE,0);
    pilot_gfx_set_state(D3DRS_ALPHATESTENABLE,0);pilot_gfx_set_state(D3DRS_STENCILENABLE,0);
    pilot_gfx_set_state(D3DRS_COLORWRITEENABLE,15);
    pilot_gfx_set_state(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA);
    pilot_gfx_set_state(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA);pilot_gfx_set_state(D3DRS_BLENDOP,1);
    for(unsigned stage=0;stage<4;++stage) {
        pilot_gfx_texture_bind(stage,0);pilot_gfx_set_texture_state(stage,D3DTSS_COLOROP,D3DTOP_DISABLE);
    }
    float scale[]={2.f/64,-2.f/64,1,1},offset[]={-1,1,0,0};
    CHECK(!pilot_gfx_shader_bind(shader) && !pilot_gfx_shader_raster(scale,offset));
    unsigned char baseline[64*64*4],batched[64*64*4];
    const unsigned first[]={0,3,7},count[]={3,4,5};
    for(unsigned blend=0;blend<2;++blend)for(unsigned cull=1;cull<=3;++cull) {
        pilot_gfx_set_state(D3DRS_ALPHABLENDENABLE,blend);pilot_gfx_set_state(D3DRS_CULLMODE,cull);
        struct PilotDrawBatchStats before,after;
        for(unsigned merged=0;merged<2;++merged) {
            CHECK(!pilot_gfx_clear(1,0xff102030,1,0));pilot_gfx_batch_allow(merged);
            pilot_gfx_batch_stats(&before);
            for(unsigned i=0;i<3;++i) {
                CHECK(!pilot_gfx_shader_raster(scale,offset)); /* identical preparation must retain the batch */
                CHECK(!pilot_gfx_draw_vertices(6,first[i],count[i]));
            }
            if(merged) {
                /* Completion must cover all logical draws even while their
                 * single native submission is still queued. */
                struct PilotGpuProgress progress;pilot_gfx_completion_stats(&progress);
                CHECK(pilot_gfx_checkpoint()==progress.submitted);
            }
            CHECK(!batch_test_read(merged?batched:baseline));pilot_gfx_batch_stats(&after);
            if(merged)CHECK(after.queued_draws-before.queued_draws==3 && after.native_draws-before.native_draws==1);
        }
        CHECK(!memcmp(baseline,batched,sizeof(baseline)));
        if(cull==1){unsigned covered=0;for(unsigned i=0;i<64*64;++i)covered+=memcmp(baseline+i*4,"\x10\x20\x30",3)!=0;CHECK(covered>500);}
    }
    pilot_gfx_batch_allow(0);pilot_gfx_set_state(D3DRS_ALPHABLENDENABLE,0);pilot_gfx_set_state(D3DRS_CULLMODE,1);
    /* Same native buffer updated between draws: old queued pixels must use
     * the old bytes, and later writes in a prefetched page must be detected. */
    for(unsigned merged=0;merged<2;++merged) {
        CHECK(!pilot_gfx_clear(1,0xff000000,1,0));
        batch_test_vertex(vertices+0,2,2,0xffff0000);batch_test_vertex(vertices+1,2,30,0xffff0000);
        batch_test_vertex(vertices+2,30,2,0xffff0000);batch_test_vertex(vertices+3,30,30,0xffff0000);
        pilot_gfx_vertex_upload(buffer,vertices,sizeof(vertices));pilot_gfx_batch_allow(merged);
        CHECK(!pilot_gfx_draw_vertices(6,0,4));
        for(unsigned i=0;i<4;++i){vertices[i].position[0]+=32;vertices[i].color[0]=0;vertices[i].color[1]=1;}
        pilot_gfx_vertex_upload_range(buffer,vertices,0,4*sizeof(vertices[0]));
        CHECK(!pilot_gfx_draw_vertices(6,0,4));
        CHECK(!batch_test_read(merged?batched:baseline));
    }
    CHECK(!memcmp(baseline,batched,sizeof(baseline)));
    CHECK(batched[(10*64+10)*4]==255 && batched[(10*64+42)*4+1]==255);
    /* A disabled color array and a changed raster cbuffer must split pending
     * work before later draws consume their new values. */
    uint32_t current_shader=0;elements[1]=(struct PilotVertexElement){0};
    CHECK(!pilot_gfx_shader_create(program,2,elements,&current_shader));
    for(unsigned merged=0;merged<2;++merged) {
        pilot_gfx_batch_allow(0);CHECK(!pilot_gfx_clear(1,0xff000000,1,0));
        for(unsigned i=0;i<4;++i)vertices[i].position[0]-=32;
        pilot_gfx_vertex_upload(buffer,vertices,sizeof(vertices));
        CHECK(!pilot_gfx_shader_bind(current_shader));
        float red[]={1,0,0,1},green[]={0,1,0,1},moved[]={offset[0]+1,offset[1],offset[2],offset[3]};
        CHECK(pilot_gfx_vertex_current_set(1,red));CHECK(!pilot_gfx_shader_raster(scale,offset));
        pilot_gfx_batch_allow(merged);CHECK(!pilot_gfx_draw_vertices(6,0,4));
        CHECK(pilot_gfx_vertex_current_set(1,green));CHECK(!pilot_gfx_shader_raster(scale,moved));
        CHECK(!pilot_gfx_draw_vertices(6,0,4));CHECK(!batch_test_read(merged?batched:baseline));
        for(unsigned i=0;i<4;++i)vertices[i].position[0]+=32;
    }
    CHECK(!memcmp(baseline,batched,sizeof(baseline)));
    CHECK(batched[(10*64+10)*4]==255 && batched[(10*64+42)*4+1]==255);
    pilot_gfx_batch_allow(0);CHECK(!pilot_gfx_shader_bind(shader));CHECK(!pilot_gfx_shader_raster(scale,offset));
    pilot_gfx_shader_destroy(current_shader);
    /* Render/combiner mutations and CPU texture uploads are ordered boundaries. */
    uint32_t texture=0,texels[32];CHECK(!pilot_gfx_texture_create(2,2,1,0x12,&texture));
    for(unsigned merged=0;merged<2;++merged) {
        pilot_gfx_batch_allow(0);CHECK(!pilot_gfx_clear(1,0xff000000,1,0));
        for(unsigned i=0;i<4;++i){vertices[i].position[0]-=32;vertices[i].color[0]=vertices[i].color[1]=vertices[i].color[2]=1;}
        pilot_gfx_vertex_upload(buffer,vertices,sizeof(vertices));
        pilot_gfx_set_texture_state(0,D3DTSS_COLOROP,D3DTOP_SELECTARG1);
        pilot_gfx_set_texture_state(0,D3DTSS_COLORARG1,D3DTA_TFACTOR);
        pilot_gfx_set_state(D3DRS_TEXTUREFACTOR,0xffff0000);pilot_gfx_batch_allow(merged);
        CHECK(!pilot_gfx_draw_vertices(6,0,4));
        pilot_gfx_set_state(D3DRS_TEXTUREFACTOR,0xff00ff00);
        for(unsigned i=0;i<4;++i)vertices[i].position[0]+=32;
        pilot_gfx_vertex_upload_range(buffer,vertices,0,4*sizeof(vertices[0]));
        CHECK(!pilot_gfx_draw_vertices(6,0,4));
        CHECK(!batch_test_read(merged?batched:baseline));
    }
    CHECK(!memcmp(baseline,batched,sizeof(baseline)));
    for(unsigned merged=0;merged<2;++merged) {
        pilot_gfx_batch_allow(0);CHECK(!pilot_gfx_clear(1,0xff000000,1,0));
        for(unsigned i=0;i<4;++i)vertices[i].position[0]-=32;
        pilot_gfx_vertex_upload(buffer,vertices,sizeof(vertices));
        for(unsigned i=0;i<32;++i)texels[i]=0xffff0000;
        pilot_gfx_texture_prepare_sample(texture,0,texels,sizeof(texels),1);pilot_gfx_texture_bind(0,texture);
        pilot_gfx_set_texture_state(0,D3DTSS_COLORARG1,D3DTA_TEXTURE);pilot_gfx_batch_allow(merged);
        CHECK(!pilot_gfx_draw_vertices(6,0,4));
        for(unsigned i=0;i<32;++i)texels[i]=0xff00ff00;
        pilot_gfx_texture_prepare_sample(texture,0,texels,sizeof(texels),1);
        for(unsigned i=0;i<4;++i)vertices[i].position[0]+=32;
        pilot_gfx_vertex_upload_range(buffer,vertices,0,4*sizeof(vertices[0]));
        CHECK(!pilot_gfx_draw_vertices(6,0,4));CHECK(!batch_test_read(merged?batched:baseline));
    }
    CHECK(!memcmp(baseline,batched,sizeof(baseline)));
    pilot_gfx_batch_allow(0);pilot_gfx_texture_bind(0,0);pilot_gfx_texture_destroy(texture);
    pilot_gfx_set_texture_state(0,D3DTSS_COLOROP,D3DTOP_DISABLE);
    /* Real visibility samples and the capacity boundary include every strip. */
    uint32_t samples[2]={0};
    for(unsigned merged=0;merged<2;++merged) {
        CHECK(!pilot_gfx_clear(1,0xff000000,1,0));CHECK(!pilot_gfx_visibility_begin());pilot_gfx_batch_allow(merged);
        struct PilotDrawBatchStats before,after;pilot_gfx_batch_stats(&before);
        for(unsigned i=0;i<65;++i)CHECK(!pilot_gfx_draw_vertices(6,0,4));
        CHECK(!pilot_gfx_visibility_end(merged));CHECK(!batch_test_report(merged,samples+merged));
        pilot_gfx_batch_stats(&after);
        if(merged)CHECK(after.queued_draws-before.queued_draws==65 && after.native_draws-before.native_draws==2 && after.largest_batch==64);
    }
    CHECK(samples[0]>0 && samples[0]==samples[1]);
    pilot_gfx_batch_allow(0);pilot_gfx_vertex_bind(0,0,0);pilot_gfx_vertex_destroy(buffer);pilot_gfx_shader_destroy(shader);
    fprintf(stderr,"PASS strip batch: exact GPU pixels with odd/even strips, culling, alpha blending, state/texture/vertex/current/raster mutation, readback/completion, query sample counts and capacity flush\n");
    return 0;
}
#endif
