#ifdef PILOT_D3D_PHOBOS
static int test_unit_point_pixels(void);
static int test_scaled_point_pixels(void)
{
    for(unsigned lifetime=0;lifetime<3;++lifetime) {
        uint32_t parameters[17]={64,64,7,1,0x11,1,0,0,1,0x2e,0,60,1};CHECK(!pilot_gfx_create(parameters));
        pilot_gfx_set_state(D3DRS_ZENABLE,0);pilot_gfx_set_state(D3DRS_LIGHTING,0);
        pilot_gfx_set_state(D3DRS_ALPHABLENDENABLE,0);pilot_gfx_set_state(D3DRS_ALPHATESTENABLE,0);
        pilot_gfx_set_state(D3DRS_SPECULARENABLE,0);pilot_gfx_set_state(D3DRS_RANGEFOGENABLE,0);
        pilot_gfx_set_state(D3DRS_CULLMODE,D3DCULL_CW);pilot_gfx_set_state(D3DRS_FILLMODE,D3DFILL_WIREFRAME);
        pilot_gfx_set_state(D3DRS_FOGTABLEMODE,3);pilot_gfx_set_state(D3DRS_FOGSTART,0);
        pilot_gfx_set_state(D3DRS_FOGEND,0x40800000);pilot_gfx_set_state(D3DRS_FOGCOLOR,0);
        pilot_gfx_pixel_fixed();
        for(unsigned stage=0;stage<4;++stage) {
            pilot_gfx_texture_bind(stage,0);pilot_gfx_set_texture_state(stage,D3DTSS_TEXCOORDINDEX,stage);
            pilot_gfx_set_texture_state(stage,D3DTSS_COLOROP,stage?D3DTOP_DISABLE:D3DTOP_SELECTARG1);
            pilot_gfx_set_texture_state(stage,D3DTSS_COLORARG1,D3DTA_DIFFUSE);
            pilot_gfx_set_texture_state(stage,D3DTSS_ALPHAOP,D3DTOP_SELECTARG1);
            pilot_gfx_set_texture_state(stage,D3DTSS_ALPHAARG1,D3DTA_DIFFUSE);
        }
        float identity[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1},projection[16];
        memcpy(projection,identity,sizeof(projection));projection[10]=-.25f;
        CHECK(!pilot_gfx_transform(0,identity) && !pilot_gfx_transform(1,projection) && !pilot_gfx_transform(6,identity));
        struct {float x,y,z;uint32_t color;} vertex={0,0,-2,0xffff0000};
        uint32_t buffer=0;CHECK(!pilot_gfx_vertex_create(sizeof(vertex),&buffer));
        pilot_gfx_vertex_upload(buffer,&vertex,sizeof(vertex));pilot_gfx_vertex_bind(0,buffer,sizeof(vertex));
        unsigned char pixels[64*64*4];
        for(unsigned variant=0;variant<8;++variant) {
            float values[8]={variant==0?8:variant==5?.01f:.5f,variant==5?4:0,0,0,0,0,0,variant==4?4:64};
            uint32_t states[8];memcpy(states,values,sizeof(states));states[3]=variant!=0;
            if(variant==1)states[4]=0x41800000; /* A=16 -> 64*.5/sqrt(16)=8 pixels */
            else if(variant==2)states[5]=0x41000000; /* B=8, distance=2 -> 8 pixels */
            else states[6]=0x40800000; /* C=4, distance=2 -> 8 pixels */
            CHECK(!pilot_gfx_points(states,0));
            uint32_t viewport[6]={0,0,variant==7?32:64,variant==7?32:64,0,0x3f800000};
            CHECK(!pilot_gfx_viewport(viewport));pilot_gfx_set_state(D3DRS_FOGENABLE,variant==6);
            CHECK(!pilot_gfx_clear(1,0xff111111,1,0) && !pilot_gfx_fvf_prepare_fixed(0x42));
            CHECK(!pilot_gfx_draw_vertices(1,0,1) && !pilot_gfx_readback(pixels,sizeof(pixels)));
            unsigned size=variant==4 || variant==5 || variant==7?4:8,center=variant==7?16:32;
            for(unsigned y=0;y<64;++y)for(unsigned x=0;x<64;++x)for(unsigned lane=0;lane<3;++lane) {
                int inside=x>=center-size/2 && x<center+size/2 && y>=center-size/2 && y<center+size/2;
                unsigned expected=inside?(lane?0:variant==6?128:255):17;
                if(abs((int)pixels[(y*64+x)*4+lane]-(int)expected)>1)
                    fprintf(stderr,"POINT variant=%u xy=%u,%u lane=%u got=%u expected=%u\n",variant,x,y,lane,pixels[(y*64+x)*4+lane],expected);
                CHECK(abs((int)pixels[(y*64+x)*4+lane]-(int)expected)<=1);
            }
            ID3D11RasterizerState *raster=NULL;D3D11_RASTERIZER_DESC desc;
            ID3D11DeviceContext_RSGetState(d3d8_GetD3D11Context(),&raster);CHECK(raster);
            ID3D11RasterizerState_GetDesc(raster,&desc);ID3D11RasterizerState_Release(raster);
            CHECK(desc.CullMode==D3D11_CULL_FRONT && desc.FillMode==D3D11_FILL_WIREFRAME);
        }
        uint32_t viewport[6]={0,0,64,64,0,0x3f800000};CHECK(!pilot_gfx_viewport(viewport));
        pilot_gfx_set_state(D3DRS_FOGENABLE,0);
        uint32_t texture=0,texels[4]={0xffff0000,0xff00ff00,0xff0000ff,0xffffffff};
        CHECK(!pilot_gfx_texture_create(2,2,1,6,&texture));pilot_gfx_texture_upload(texture,0,texels,sizeof(texels));
        uint32_t states[8]={0x41000000,0,1,0,0,0,0,0x42800000};CHECK(!pilot_gfx_points(states,0));
        for(unsigned stage=0;stage<4;++stage) {
            pilot_gfx_set_texture_state(stage,D3DTSS_COLOROP,D3DTOP_SELECTARG1);
            pilot_gfx_set_texture_state(stage,D3DTSS_COLORARG1,stage==3?D3DTA_TEXTURE:D3DTA_DIFFUSE);
        }
        pilot_gfx_texture_bind(3,texture);pilot_gfx_set_texture_state(3,D3DTSS_MINFILTER,D3DTEXF_POINT);
        pilot_gfx_set_texture_state(3,D3DTSS_MAGFILTER,D3DTEXF_POINT);
        CHECK(!pilot_gfx_clear(1,0xff111111,1,0) && !pilot_gfx_fvf_prepare_fixed(0x42));
        CHECK(!pilot_gfx_draw_vertices(1,0,1) && !pilot_gfx_readback(pixels,sizeof(pixels)));
        for(unsigned y=0;y<64;++y)for(unsigned x=0;x<64;++x) {
            uint32_t color=0xff111111;
            if(x>=28 && x<36 && y>=28 && y<36)color=texels[(y>=32)*2+(x>=32)];
            CHECK(pixels[(y*64+x)*4]==((color>>16)&255) && pixels[(y*64+x)*4+1]==((color>>8)&255) && pixels[(y*64+x)*4+2]==(color&255));
        }
        /* Screen-space inputs keep their pixel size even with scale enabled. */
        struct {float x,y,z,w;uint32_t color;} screen={32,32,.5f,1,0xff00ff00};
        uint32_t screen_buffer=0;CHECK(!pilot_gfx_vertex_create(sizeof(screen),&screen_buffer));
        pilot_gfx_vertex_upload(screen_buffer,&screen,sizeof(screen));pilot_gfx_vertex_bind(0,screen_buffer,sizeof(screen));
        for(unsigned stage=0;stage<4;++stage)pilot_gfx_set_texture_state(stage,D3DTSS_COLOROP,stage?D3DTOP_DISABLE:D3DTOP_SELECTARG1);
        states[2]=0;states[3]=1;states[6]=0x40800000;CHECK(!pilot_gfx_points(states,1));
        CHECK(!pilot_gfx_fvf_placement(0x44,0,0,1,0) && !pilot_gfx_clear(1,0xff111111,1,0));
        CHECK(!pilot_gfx_draw_vertices(1,0,1) && !pilot_gfx_readback(pixels,sizeof(pixels)));
        for(unsigned y=0;y<64;++y)for(unsigned x=0;x<64;++x)for(unsigned lane=0;lane<3;++lane) {
            unsigned expected=x>=28 && x<36 && y>=28 && y<36?(lane==1?255:0):17;
            CHECK(pixels[(y*64+x)*4+lane]==expected);
        }
        pilot_gfx_vertex_bind(0,0,0);pilot_gfx_vertex_destroy(screen_buffer);
        uint32_t invalid[8];memcpy(invalid,states,sizeof(invalid));invalid[0]=0x7fc00000;
        CHECK(pilot_gfx_points(invalid,0)<0);memcpy(invalid,states,sizeof(invalid));invalid[1]=0x43000000;
        CHECK(pilot_gfx_points(invalid,0)<0);memcpy(invalid,states,sizeof(invalid));invalid[6]=0;
        CHECK(pilot_gfx_points(invalid,0)<0 && pilot_gfx_points(states,2)<0);
        /* Reverting to the unit-point path must clear the GS and preserve pixels. */
        states[0]=0x3f800000;states[2]=states[3]=0;CHECK(!pilot_gfx_points(states,0));
        pilot_gfx_vertex_bind(0,0,0);pilot_gfx_vertex_destroy(buffer);
        pilot_gfx_texture_bind(3,0);pilot_gfx_texture_destroy(texture);
        pilot_gfx_set_state(D3DRS_FILLMODE,D3DFILL_SOLID);pilot_gfx_set_state(D3DRS_CULLMODE,D3DCULL_NONE);
        CHECK(!pilot_gfx_transform(1,identity));
        CHECK(!test_unit_point_pixels());pilot_gfx_close();
    }
    fprintf(stderr,"PASS scaled points: complete images, A/B/C eye attenuation, size clamps, viewport height, color/fog, Xbox texture-set3 UVs, raster restoration and device recreation\n");
    return 0;
}
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
