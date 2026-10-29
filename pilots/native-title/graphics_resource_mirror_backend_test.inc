/* Actual write observer and native vertex storage; no synthetic page epochs. */
static int test_resource_mirror(void)
{
    enum {TOTAL=8192};
    BYTE *allocation=VirtualAlloc(NULL,6*4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    CHECK(allocation && pilot_write_watch_initialize(allocation,6*4096));
    BYTE *source=allocation+3,readback[TOTAL];
    for(unsigned i=0;i<TOTAL;++i)source[i]=(BYTE)(i*17);
    uint32_t vertex=0,alias=0;
    CHECK(!pilot_gfx_vertex_create(TOTAL,&vertex));
    CHECK(!pilot_gfx_vertex_create(TOTAL,&alias));
    int owner=pilot_gfx_resource_mirror_owner();
    pilot_resource_mirror_diagnostics=1;
    uint64_t budget=pilot_resource_mirror_stats.metadata_bytes;
    pilot_gfx_vertex_upload_range(vertex,source,700,80);
    pilot_gfx_vertex_upload_range(alias,source,700,80);
    uint64_t hits=pilot_resource_mirror_stats.hits;
    pilot_gfx_vertex_upload_range(vertex,source,700,80);
    CHECK(pilot_resource_mirror_stats.hits==hits+(owner?1:0));
    /* A CPU alias writes outside the consumed subset on its watched page.
     * Consumed-subset equality cannot certify the stale rest of that page. */
    BYTE previous=source[2000];source[2000]^=0x51;
    pilot_gfx_vertex_upload_range(vertex,source,700,80);
    CHECK(pilot_resource_mirror_stats.hits==hits+(owner?1:0));
    CHECK(!pilot_gfx_vertex_readback(vertex,readback,TOTAL));
    CHECK(readback[2000]==previous);
    pilot_gfx_vertex_upload_range(vertex,source,2000,1);
    pilot_gfx_vertex_upload_range(alias,source,2000,1);
    CHECK(!pilot_gfx_vertex_readback(vertex,readback,TOTAL) && !memcmp(source,readback,TOTAL));
    CHECK(!pilot_gfx_vertex_readback(alias,readback,TOTAL) && !memcmp(source,readback,TOTAL));
    /* A full native upload mutates the CPU mirror independently of guest
     * writes. Old certificates must not bless that different payload. */
    memset(readback,0xa5,TOTAL);pilot_gfx_vertex_upload(vertex,readback,TOTAL);
    pilot_gfx_vertex_upload_range(vertex,source,700,80);
    CHECK(!pilot_gfx_vertex_readback(vertex,readback,TOTAL) && !memcmp(readback+700,source+700,80));
    CHECK(readback[6000]==0xa5);
    CHECK(pilot_gfx_vertex_cache_break(1));
    pilot_gfx_vertex_upload_range(vertex,source,4090,20);
    CHECK(!pilot_gfx_vertex_readback(vertex,readback,TOTAL) && !memcmp(readback,source,TOTAL));
    BYTE *alternate=allocation+3*4096+5;
    memcpy(alternate,source,TOTAL);alternate[700]^=0x7d;
    pilot_gfx_vertex_upload_range(vertex,alternate,700,80);
    CHECK(!pilot_gfx_vertex_readback(vertex,readback,TOTAL) && !memcmp(readback+700,alternate+700,80));
    /* A native writer lease blocks certification even before its eventual
     * write. Ending it and consuming the changed bytes restores byte checks. */
    CHECK(pilot_write_watch_native_begin(source+4100,1));
    source[4100]^=0x2f;pilot_gfx_vertex_upload_range(vertex,source,4090,20);
    pilot_write_watch_native_end(source+4100,1);
    pilot_gfx_vertex_upload_range(vertex,source,4090,20);
    CHECK(!pilot_gfx_vertex_readback(vertex,readback,TOTAL) && !memcmp(readback,source,TOTAL));
    pilot_gfx_vertex_destroy(vertex);pilot_gfx_vertex_destroy(alias);
    CHECK(pilot_resource_mirror_stats.metadata_bytes==budget);
    /* Same source address and native slot, different storage generation. */
    CHECK(!pilot_gfx_vertex_create(TOTAL,&vertex));source[TOTAL-1]^=0x33;
    pilot_gfx_vertex_upload_range(vertex,source,TOTAL-8,8);
    CHECK(!pilot_gfx_vertex_readback(vertex,readback,TOTAL) && !memcmp(source,readback,TOTAL));
    pilot_gfx_vertex_destroy(vertex);CHECK(pilot_resource_mirror_stats.metadata_bytes==budget);
#ifdef PILOT_D3D_PHOBOS
    /* A protected source also feeds actual queued draws. Updating it must
     * flush the red draw before green bytes replace the native snapshot. */
    const uint32_t program[]={0,0x0020001b,0x08000000,0x0000f800,0,0x0020021b,0x08000000,0x0000f819};
    struct PilotVertexElement elements[16]={0};
    elements[0]=(struct PilotVertexElement){0x42,0,0};elements[1]=(struct PilotVertexElement){0x42,0,16};
    uint32_t shader=0;CHECK(!pilot_gfx_shader_create(program,2,elements,&shader));
    struct BatchTestVertex *quad=(struct BatchTestVertex*)allocation;
    batch_test_vertex(quad,2,2,0xffff0000);batch_test_vertex(quad+1,2,30,0xffff0000);
    batch_test_vertex(quad+2,30,2,0xffff0000);batch_test_vertex(quad+3,30,30,0xffff0000);
    CHECK(!pilot_gfx_vertex_create(TOTAL,&vertex));
    pilot_gfx_vertex_upload_range(vertex,quad,0,4*sizeof(*quad));
    pilot_gfx_vertex_bind(0,vertex,sizeof(*quad));pilot_gfx_pixel_fixed();
    pilot_gfx_set_state(D3DRS_ZENABLE,0);pilot_gfx_set_state(D3DRS_LIGHTING,0);
    pilot_gfx_set_state(D3DRS_CULLMODE,D3DCULL_NONE);pilot_gfx_set_state(D3DRS_ALPHABLENDENABLE,0);
    for(unsigned stage=0;stage<4;++stage)pilot_gfx_set_texture_state(stage,D3DTSS_COLOROP,D3DTOP_DISABLE);
    float scale[]={2.f/64,-2.f/64,1,1},offset[]={-1,1,0,0};
    CHECK(!pilot_gfx_shader_bind(shader) && !pilot_gfx_shader_raster(scale,offset));
    CHECK(!pilot_gfx_clear(1,0xff000000,1,0));pilot_gfx_batch_allow(1);
    hits=pilot_resource_mirror_stats.hits;pilot_gfx_vertex_upload_range(vertex,quad,0,4*sizeof(*quad));
    CHECK(pilot_resource_mirror_stats.hits==hits+(owner?1:0));
    CHECK(!pilot_gfx_draw_vertices(6,0,4));
    for(unsigned i=0;i<4;++i){quad[i].position[0]+=32;quad[i].color[0]=0;quad[i].color[1]=1;}
    pilot_gfx_vertex_upload_range(vertex,quad,0,4*sizeof(*quad));CHECK(!pilot_gfx_draw_vertices(6,0,4));
    BYTE pixels[64*64*4];CHECK(!pilot_gfx_readback(pixels,sizeof(pixels)));
    CHECK(pixels[(10*64+10)*4]==255 && !pixels[(10*64+10)*4+1]);
    CHECK(!pixels[(10*64+42)*4] && pixels[(10*64+42)*4+1]==255);
    pilot_gfx_batch_allow(0);pilot_gfx_vertex_bind(0,0,0);pilot_gfx_vertex_destroy(vertex);
    pilot_gfx_shader_destroy(shader);CHECK(pilot_resource_mirror_stats.metadata_bytes==budget);
#endif
    pilot_write_watch_finish();CHECK(VirtualFree(allocation,0,MEM_RELEASE));
    fprintf(stderr,"PASS resource mirror: watched unaligned/cross-page vertices, CPU aliases, full/range/refetch writes, native writer lease, actual GPU readbacks and slot retirement (owner=%d)\n",owner);
    return 0;
}
