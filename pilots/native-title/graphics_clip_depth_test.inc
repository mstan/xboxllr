/* Authored affine Z plane, independent of either clipper/interpolator. */
static int check_clip_depth_reference(void)
{
    for(unsigned lifetime=0;lifetime<3;++lifetime) {
        uint32_t color=0,depth=0,vertex=0,shader=0;
        CHECK(!pilot_gfx_texture_create_usage(64,64,1,0x12,1,&color));
        CHECK(!pilot_gfx_texture_create_usage(64,64,1,0x2e,2,&depth));
        uint32_t colors[4096],depths[4096];
        for(unsigned i=0;i<4096;++i){colors[i]=0xff102030;depths[i]=0xffffff5a;}
        pilot_gfx_texture_upload(color,0,colors,sizeof(colors));pilot_gfx_texture_upload(depth,0,depths,sizeof(depths));
        CHECK(!pilot_gfx_targets(color,depth));
        const uint32_t code[]={0,0x0020001b,0x08000000,0x0000f800,0,0x0020021b,0x08000000,0x0000f819};
        struct PilotVertexElement elements[16]={0};elements[0]=(struct PilotVertexElement){0x42,0,0};elements[1]=(struct PilotVertexElement){0x40,0,16};
        struct {float x,y,z,w;uint32_t color;} data[]={{8,8,100,1,0xffffffff},{88,8,740,4,0xffffffff},{8,56,100,2,0xffffffff}};
        CHECK(!pilot_gfx_vertex_create(sizeof(data),&vertex));pilot_gfx_vertex_upload(vertex,data,sizeof(data));pilot_gfx_vertex_bind(0,vertex,sizeof(data[0]));
        CHECK(!pilot_gfx_shader_create(code,2,elements,&shader));CHECK(!pilot_gfx_shader_bind(shader));
        static const uint32_t states[][2]={{57,0x201},{59,0},{60,0},{64,1},{66,0x1d01},{67,0x1010101},{92,0},{143,1},{144,0},{147,0},{159,0}};
        for(unsigned i=0;i<sizeof(states)/sizeof(*states);++i)CHECK(pilot_gfx_xbox_render_state(states[i][0],states[i][1]));
        for(unsigned stage=0;stage<4;++stage)pilot_gfx_texture_bind(stage,0);
        CHECK(pilot_gfx_xbox_texture_state(0,12,2));CHECK(pilot_gfx_xbox_texture_state(0,14,0));
        CHECK(pilot_gfx_xbox_texture_state(0,16,2));CHECK(pilot_gfx_xbox_texture_state(0,18,0));
        for(unsigned stage=1;stage<4;++stage)CHECK(pilot_gfx_xbox_texture_state(stage,12,1));
        pilot_gfx_pixel_fixed();uint32_t viewport[]={0,0,64,64,0,0x3f800000};CHECK(!pilot_gfx_viewport(viewport));
        float scale[]={2.0f/64,-2.0f/64,1.0f/16777215,1},offset[]={-1,1,0,0};CHECK(!pilot_gfx_shader_raster(scale,offset));
        CHECK(!pilot_gfx_draw_vertices(5,0,3));uint32_t shape[3];
        CHECK(!pilot_gfx_reference_target_readback(0,colors,sizeof(colors),shape) && shape[2]==87);
        CHECK(!pilot_gfx_reference_target_readback(1,depths,sizeof(depths),shape) && (shape[2]==44 || shape[2]==45));
        unsigned compared=0;
        for(unsigned y=12;y<28;++y)for(unsigned x=12;x<60;++x) {
            if((x+.5-8)/80+(y+.5-8)/48>=.9)continue;
            uint32_t z=depths[y*64+x]&0xffffff,want=100+8*x+4-64;
            CHECK(abs((int)z-(int)want)<=1 && depths[y*64+x]>>24==0x5a && colors[y*64+x]==0xffffffff);++compared;
        }
        CHECK(compared==713);fprintf(stderr,"PASS native clip depth: lifetime=%u, %u independent interior pixels, <=1 depth step, exact RGB/stencil\n",lifetime,compared);
        CHECK(!pilot_gfx_targets(0,UINT32_MAX));pilot_gfx_texture_destroy(color);pilot_gfx_texture_destroy(depth);
        pilot_gfx_vertex_bind(0,0,0);pilot_gfx_vertex_destroy(vertex);pilot_gfx_shader_destroy(shader);pilot_gfx_close();
        if(lifetime<2){uint32_t pp[17]={64,64,6,1,0x11,1,0,0,1,0x2a,0,0,1};CHECK(!pilot_gfx_create(pp));}
    }
    return 0;
}
