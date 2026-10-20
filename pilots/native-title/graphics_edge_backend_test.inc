#ifdef PILOT_D3D_PHOBOS
static int test_edge_antialias(void)
{
    for(unsigned cycle=0;cycle<2;++cycle) {
        uint32_t p[17]={64,64,7,1,cycle?0x2222:0x11,1,0,0,0,0,0,60,1};CHECK(!pilot_gfx_create(p));
        struct EdgeVertex {float x,y,z,w;uint32_t color;} vertices[]={
            {8,8,.5f,1,0xffff0000},{53.5f,12.25f,.5f,1,0xffff0000},{19.25f,55.5f,.5f,1,0xffff0000}};
        uint32_t buffer=0;CHECK(!pilot_gfx_vertex_create(sizeof(vertices),&buffer));
        pilot_gfx_vertex_upload(buffer,vertices,sizeof(vertices));pilot_gfx_vertex_bind(0,buffer,sizeof(vertices[0]));
        pilot_gfx_set_state(D3DRS_CULLMODE,D3DCULL_NONE);pilot_gfx_set_state(D3DRS_ZENABLE,0);
        pilot_gfx_set_state(D3DRS_LIGHTING,0);pilot_gfx_set_state(D3DRS_ALPHABLENDENABLE,0);
        pilot_gfx_set_state(D3DRS_ALPHATESTENABLE,0);pilot_gfx_set_state(D3DRS_FOGENABLE,0);pilot_gfx_pixel_fixed();
        for(unsigned stage=0;stage<4;++stage)CHECK(pilot_gfx_xbox_texture_state(stage,12,stage?1:2));
        CHECK(pilot_gfx_xbox_texture_state(0,14,0) && pilot_gfx_xbox_texture_state(0,16,2) && pilot_gfx_xbox_texture_state(0,18,0));
        CHECK(!pilot_gfx_fvf_prepare(0x44,0,0));unsigned side=cycle?128:64,bytes=side*side*4;
        unsigned char *hard=malloc(bytes),*smooth=malloc(bytes),*blend=malloc(bytes);CHECK(hard && smooth && blend);
        const uint16_t indices[]={0,1,2};
        CHECK(pilot_gfx_xbox_render_state(151,0) && !pilot_gfx_xbox_render_state(151,2));
        CHECK(!pilot_gfx_clear(1,0xff0000ff,1,0) && !pilot_gfx_draw_indexed(5,3,indices,0));
        CHECK(!pilot_gfx_surface_readback(0,hard,bytes));
        CHECK(pilot_gfx_xbox_render_state(151,1) && pilot_gfx_multisample(0,UINT32_MAX));
        CHECK(!pilot_gfx_clear(1,0xff0000ff,1,0) && !pilot_gfx_draw_indexed(5,3,indices,0));
        CHECK(!pilot_gfx_surface_readback(0,smooth,bytes));unsigned partial=0,interior=0;
        for(unsigned i=0;i<side*side;++i) {
            CHECK((hard[i*4]==0 || hard[i*4]==255) && !hard[i*4+1]);
            CHECK(!smooth[i*4+1] && abs((int)smooth[i*4]+smooth[i*4+2]-255)<=1 && smooth[i*4+3]==255);
            if(smooth[i*4] && smooth[i*4]<255)++partial;
            if(smooth[i*4]==255)++interior;
        }
        CHECK(partial>20 && interior>100 && !smooth[0] && smooth[2]==255);
        /* Blending uses the existing destination in every covered sample;
         * alpha is not a screenshot filter and far pixels remain untouched. */
        for(unsigned i=0;i<3;++i)vertices[i].color=0x80ff0000;
        pilot_gfx_vertex_upload(buffer,vertices,sizeof(vertices));
        pilot_gfx_set_state(D3DRS_ALPHABLENDENABLE,1);pilot_gfx_set_state(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA);
        pilot_gfx_set_state(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA);
        CHECK(!pilot_gfx_clear(1,0xff0000ff,1,0) && !pilot_gfx_draw_vertices(5,0,3));
        CHECK(!pilot_gfx_surface_readback(0,blend,bytes));
        for(unsigned i=0;i<side*side;++i) {
            CHECK(abs((int)blend[i*4]-(int)((smooth[i*4]*128u+127)/255))<=1);
            CHECK(abs((int)blend[i*4]+blend[i*4+2]-255)<=1);
        }
        CHECK(!pilot_gfx_visibility_begin());
        CHECK(pilot_gfx_draw_vertices(5,0,3)==E_NOTIMPL && !pilot_gfx_visibility_end(77));
        CHECK(!pilot_gfx_surface_readback(0,smooth,bytes) && !memcmp(smooth,blend,bytes));
        CHECK(pilot_gfx_xbox_render_state(151,0));pilot_gfx_set_state(D3DRS_ALPHABLENDENABLE,0);
        for(unsigned i=0;i<3;++i)vertices[i].color=0xffff0000;pilot_gfx_vertex_upload(buffer,vertices,sizeof(vertices));
        CHECK(!pilot_gfx_clear(1,0xff0000ff,1,0) && !pilot_gfx_draw_indexed(5,3,indices,0));
        CHECK(!pilot_gfx_surface_readback(0,blend,bytes) && !memcmp(hard,blend,bytes));
        pilot_gfx_vertex_bind(0,0,0);pilot_gfx_vertex_destroy(buffer);free(hard);free(smooth);free(blend);pilot_gfx_close();
    }
    puts("PASS native edge coverage: fractional triangle edges, indexed/bound draws, retained destination/blending, disable restoration, ordinary/Gaussian images and device recreation");return 0;
}
#endif
