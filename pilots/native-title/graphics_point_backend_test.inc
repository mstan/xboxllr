#ifdef PILOT_D3D_PHOBOS
static int test_pretransformed_unit_points(void)
{
    struct {float p[4];uint32_t color,specular;float uv[2];} vertices[]={
        {{16.5f,20.5f,.5f,.5f},0xffff0000,0,{0,0}},
        {{30.5f,33.5f,.5f,.5f},0xff00ff00,0,{0,0}},
        {{48.5f,47.5f,.5f,.5f},0xff0000ff,0,{0,0}}};
    const unsigned locations[]={20*64+16,33*64+30,47*64+48};
    uint32_t buffer=0;CHECK(!pilot_gfx_vertex_create(sizeof(vertices),&buffer));
    pilot_gfx_vertex_upload_range(buffer,vertices,0,sizeof(vertices));pilot_gfx_vertex_bind(0,buffer,sizeof(vertices[0]));
    CHECK(pilot_gfx_initializer_consumer(0,1,0x1c4));
    CHECK(!pilot_gfx_initializer_consumer(1,1,0) && !pilot_gfx_initializer_consumer(0,1,0x24));
    float projection[16]={0};projection[0]=projection[5]=projection[10]=projection[11]=1;
    pilot_gfx_set_state(D3DRS_FOGTABLEMODE,3);pilot_gfx_set_state(D3DRS_FOGSTART,0);
    pilot_gfx_set_state(D3DRS_FOGEND,0x40800000);pilot_gfx_set_state(D3DRS_RANGEFOGENABLE,0);
    pilot_gfx_set_state(D3DRS_FOGCOLOR,0xff000000);pilot_gfx_set_state(D3DRS_SPECULARENABLE,0);
    for(unsigned fog=0;fog<2;++fog)for(unsigned first=0;first<3;++first)for(unsigned all=0;all<2;++all) {
        unsigned count=all?3-first:1;pilot_gfx_set_state(D3DRS_FOGENABLE,fog);
        CHECK(!pilot_gfx_fvf_placement(0x1c4,0,0,1,0));
        if(fog)CHECK(!pilot_gfx_fvf_fog(0x1c4,projection,0));
        CHECK(!pilot_gfx_clear(1,0xff111111,1,0) && !pilot_gfx_draw_vertices(1,first,count));
        unsigned char pixels[64*64*4];CHECK(!pilot_gfx_readback(pixels,sizeof(pixels)));
        for(unsigned pixel=0;pixel<64*64;++pixel)for(unsigned lane=0;lane<3;++lane) {
            unsigned expected=17;
            for(unsigned i=first;i<first+count;++i)if(pixel==locations[i])
                expected=lane==i?(fog?128:255):0;
            CHECK(abs((int)pixels[pixel*4+lane]-(int)expected)<=1);
        }
    }
    pilot_gfx_set_state(D3DRS_FOGENABLE,0);pilot_gfx_vertex_bind(0,0,0);pilot_gfx_vertex_destroy(buffer);
    fprintf(stderr,"PASS pretransformed unit points: complete images, buffered source offsets/counts, original screen placement and linear clip-W fog\n");
    return 0;
}
static int test_unit_point_pixels(void)
{
    struct {float x,y,z;uint32_t color;} vertices[]={
        {-.484375f,.359375f,.5f,0xffff0000},
        {-.046875f,-.046875f,.5f,0xff00ff00},
        {.515625f,-.484375f,.5f,0xff0000ff}};
    const unsigned locations[]={20*64+16,33*64+30,47*64+48};
    uint32_t buffer=0;CHECK(!pilot_gfx_vertex_create(sizeof(vertices),&buffer));
    pilot_gfx_vertex_upload_range(buffer,vertices,0,sizeof(vertices));
    pilot_gfx_vertex_bind(0,buffer,sizeof(vertices[0]));
    for(unsigned stage=0;stage<4;++stage) {
        pilot_gfx_texture_bind(stage,0);CHECK(pilot_gfx_xbox_texture_state(stage,12,stage?1:2));
    }
    CHECK(pilot_gfx_xbox_texture_state(0,14,0));
    CHECK(pilot_gfx_initializer_consumer(2,1,0x42));
    CHECK(!pilot_gfx_initializer_consumer(1,1,0x42) && pilot_gfx_initializer_consumer(0,1,0x44));
    unsigned char pixels[64*64*4];
    for(unsigned shade=1;shade<=2;++shade)for(unsigned first=0;first<3;++first)for(unsigned all=0;all<2;++all) {
        unsigned count=all?3-first:1;
        pilot_gfx_set_state(D3DRS_SHADEMODE,shade);
        CHECK(!pilot_gfx_clear(1,0xff111111,1,0) && !pilot_gfx_fvf_prepare_fixed(0x42));
        CHECK(!pilot_gfx_draw_vertices(1,first,count) && !pilot_gfx_readback(pixels,sizeof(pixels)));
        for(unsigned pixel=0;pixel<64*64;++pixel) {
            uint32_t color=0xff111111;
            for(unsigned i=first;i<first+count;++i)if(pixel==locations[i])color=vertices[i].color;
            CHECK(pixels[pixel*4]==((color>>16)&255) && pixels[pixel*4+1]==((color>>8)&255) && pixels[pixel*4+2]==(color&255));
        }
    }
    pilot_gfx_vertex_bind(0,0,0);pilot_gfx_vertex_destroy(buffer);
    CHECK(!test_pretransformed_unit_points());
    fprintf(stderr,"PASS unit-point raster: exact complete images for one/multiple buffered points, nonzero first offsets and flat/Gouraud colors\n");
    return 0;
}
#endif
