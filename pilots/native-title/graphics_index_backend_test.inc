#ifdef PILOT_D3D_PHOBOS
static int test_indexed_pipeline(void)
{
    uint32_t parameters[17]={64,64,7,1,0x11,1,0,0,1,0x2e,0,60,1};CHECK(!pilot_gfx_create(parameters));
    struct IndexVertex {float x,y,z,w;uint32_t color;};
    struct IndexVertex *vertices=calloc(65538,sizeof(*vertices));CHECK(vertices);
    const struct IndexVertex corners[]={ {8,8,.5f,1,0xffff0000},{8,56,.5f,1,0xff00ff00},
        {56,8,.5f,1,0xff0000ff},{56,56,.5f,1,0xffffffff} };
    for(unsigned i=0;i<4;++i)vertices[i+2]=corners[i];
    uint32_t buffer=0;CHECK(!pilot_gfx_vertex_create(65538*sizeof(*vertices),&buffer));
    pilot_gfx_vertex_upload(buffer,vertices,65538*sizeof(*vertices));pilot_gfx_vertex_bind(0,buffer,sizeof(*vertices));
    pilot_gfx_set_state(D3DRS_CULLMODE,D3DCULL_NONE);pilot_gfx_set_state(D3DRS_ZENABLE,0);
    pilot_gfx_set_state(D3DRS_LIGHTING,0);pilot_gfx_set_state(D3DRS_ALPHABLENDENABLE,0);
    pilot_gfx_set_state(D3DRS_ALPHATESTENABLE,0);pilot_gfx_set_state(D3DRS_FOGENABLE,0);pilot_gfx_pixel_fixed();
    for(unsigned stage=0;stage<4;++stage){pilot_gfx_texture_bind(stage,0);CHECK(pilot_gfx_xbox_texture_state(stage,12,stage?1:2));}
    CHECK(pilot_gfx_xbox_texture_state(0,14,0) && pilot_gfx_xbox_texture_state(0,16,2) && pilot_gfx_xbox_texture_state(0,18,0));
    unsigned char actual[64*64*4],reference[sizeof(actual)];
    for(unsigned primitive=5;primitive<=8;++primitive)for(unsigned flat=0;flat<2;++flat)for(unsigned negative=0;negative<2;++negative) {
        unsigned count=primitive==5?3:4,order[]={0,1,2,3};
        if(primitive>=7){order[2]=3;order[3]=2;}
        unsigned char indices[9]={0};struct IndexVertex expanded[4];
        for(unsigned i=0;i<count;++i){unsigned value=order[i]+(negative?3:0);indices[1+i*2]=(unsigned char)value;indices[2+i*2]=(unsigned char)(value>>8);expanded[i]=corners[order[i]];}
        CHECK(pilot_gfx_xbox_render_state(66,flat?0x1d00:0x1d01));CHECK(!pilot_gfx_fvf_prepare(0x44,0,0));
        CHECK(!pilot_gfx_clear(1,0xff000000,1,0) && !pilot_gfx_draw_indexed(primitive,count,indices+1,negative?-1:2));
        CHECK(!pilot_gfx_readback(actual,sizeof(actual)));
        CHECK(!pilot_gfx_clear(1,0xff000000,1,0) && !pilot_gfx_draw_memory(primitive,count,expanded,sizeof(expanded[0])));
        CHECK(!pilot_gfx_readback(reference,sizeof(reference)));
        unsigned lit=0;for(unsigned i=0;i<sizeof(actual);++i){CHECK(abs((int)actual[i]-(int)reference[i])<=1);if((i&3)!=3 && actual[i])++lit;}
        CHECK(lit>200);
    }
    /* 0xffff is data, including in a strip; it must not restart the strip. */
    vertices[65535]=corners[0];vertices[1]=corners[1];vertices[2]=corners[2];
    pilot_gfx_vertex_upload(buffer,vertices,65538*sizeof(*vertices));
    const unsigned char high_indices[]={255,255,1,0,2,0};struct IndexVertex expanded[]={corners[0],corners[1],corners[2]};
    CHECK(!pilot_gfx_fvf_prepare(0x44,0,0) && !pilot_gfx_clear(1,0xff000000,1,0));
    CHECK(!pilot_gfx_draw_indexed(6,3,high_indices,0) && !pilot_gfx_readback(actual,sizeof(actual)));
    CHECK(!pilot_gfx_clear(1,0xff000000,1,0) && !pilot_gfx_draw_memory(6,3,expanded,sizeof(expanded[0])));
    CHECK(!pilot_gfx_readback(reference,sizeof(reference)) && !memcmp(actual,reference,sizeof(actual)));
    unsigned lit=0;for(unsigned i=0;i<sizeof(actual);++i)if((i&3)!=3 && actual[i])++lit;CHECK(lit>200);
    pilot_gfx_vertex_bind(0,0,0);pilot_gfx_vertex_destroy(buffer);free(vertices);pilot_gfx_close();
    puts("PASS indexed GPU pipeline: list/strip/fan/quad, flat/smooth colors, positive/negative base, unaligned words and valid index ffff versus expanded reference pixels");return 0;
}
#endif
