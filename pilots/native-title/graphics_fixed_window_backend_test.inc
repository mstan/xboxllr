#ifdef PILOT_D3D_PHOBOS
/* Independently authored post-transform program coordinates versus XYZ fixed
 * geometry. Angled depth planes expose a half-pixel disagreement as failed
 * decal occlusion, rather than merely checking a uniform-color quad. */
static int test_fixed_window_grid(void)
{
    const float identity[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    const float ndc[3][2]={{-.8125f,.6875f},{.75f,.4375f},{-.4375f,-.75f}};
    const uint32_t code[]={0,0x0020001b,0x08000000,0x0000f800,
        0,0x0020021b,0x08000000,0x0000f819};
    for(unsigned lifetime=0;lifetime<3;++lifetime) {
        uint32_t parameters[17]={64,64,7,1,0x11,1,0,0,1,0x2e,0,60,1};
        CHECK(!pilot_gfx_create(parameters));
        pilot_gfx_set_state(D3DRS_CULLMODE,D3DCULL_NONE);pilot_gfx_set_state(D3DRS_LIGHTING,0);
        pilot_gfx_set_state(D3DRS_ALPHABLENDENABLE,0);pilot_gfx_set_state(D3DRS_ALPHATESTENABLE,0);
        pilot_gfx_set_state(D3DRS_FOGENABLE,0);pilot_gfx_set_state(D3DRS_STENCILENABLE,0);
        pilot_gfx_set_state(D3DRS_SPECULARENABLE,0);
        for(unsigned stage=0;stage<4;++stage) {
            pilot_gfx_texture_bind(stage,0);
            pilot_gfx_set_texture_state(stage,D3DTSS_COLOROP,stage?D3DTOP_DISABLE:D3DTOP_SELECTARG1);
            pilot_gfx_set_texture_state(stage,D3DTSS_COLORARG1,D3DTA_DIFFUSE);
            pilot_gfx_set_texture_state(stage,D3DTSS_ALPHAOP,D3DTOP_SELECTARG1);
            pilot_gfx_set_texture_state(stage,D3DTSS_ALPHAARG1,D3DTA_DIFFUSE);
        }
        pilot_gfx_pixel_fixed();
        struct PilotVertexElement elements[16]={0};
        elements[0]=(struct PilotVertexElement){0x42,0,0};elements[1]=(struct PilotVertexElement){0x40,0,16};
        uint32_t shader=0;CHECK(!pilot_gfx_shader_create(code,2,elements,&shader));
        for(unsigned variant=0;variant<4;++variant) {
            uint32_t viewport[]={variant?4:0,variant?6:0,variant?56:64,variant?48:64,0,0x3f800000};
            CHECK(!pilot_gfx_viewport(viewport));
            float sample=variant==2?2.0f:1.0f;
            float scale[]={viewport[2]*sample*.5f,viewport[3]*sample*-.5f,0,1};
            float offset[]={(viewport[0]+viewport[2]*.5f)*sample+.53125f,
                (viewport[1]+viewport[3]*.5f)*sample+.53125f,
                (viewport[0]+viewport[2]*.5f)*sample,(viewport[1]+viewport[3]*.5f)*sample};
            float projection[16];memcpy(projection,identity,sizeof(projection));
            if(variant==3){projection[10]=1;projection[11]=1;projection[14]=-.5f;projection[15]=0;}
            CHECK(!pilot_gfx_transform(0,identity) && !pilot_gfx_transform(1,projection) && !pilot_gfx_transform(6,identity));
            struct {float position[3];uint32_t color;} fixed[3];
            struct {float position[4];uint32_t color;} program[3];
            for(unsigned i=0;i<3;++i) {
                float w=variant==3?(float)(1u<<i):1;
                fixed[i].position[0]=ndc[i][0]*w;fixed[i].position[1]=ndc[i][1]*w;
                fixed[i].position[2]=variant==3?w:.25f+i*.125f;fixed[i].color=0xffff0000;
                program[i].position[0]=ndc[i][0]*scale[0]+offset[0];
                program[i].position[1]=ndc[i][1]*scale[1]+offset[1];
                program[i].position[2]=variant==3?1-.5f/w:fixed[i].position[2];
                program[i].position[3]=w;program[i].color=0xffff0000;
            }
            float raster_scale[]={1/scale[0],1/scale[1],1,1};
            float raster_offset[]={-offset[2]/scale[0],-offset[3]/scale[1],0,0};
            unsigned char expected[64*64*4],actual[sizeof(expected)];
            pilot_gfx_set_state(D3DRS_ZENABLE,0);
            CHECK(!pilot_gfx_clear(3,0xff102030,1,0) && !pilot_gfx_shader_bind(shader));
            CHECK(!pilot_gfx_shader_raster(raster_scale,raster_offset));
            CHECK(!pilot_gfx_draw_memory(5,3,program,sizeof(program[0])) && !pilot_gfx_readback(expected,sizeof(expected)));
            CHECK(!pilot_gfx_clear(3,0xff102030,1,0) && !pilot_gfx_fvf_prepare_fixed(0x42));
            CHECK(!pilot_gfx_draw_memory(5,3,fixed,sizeof(fixed[0])) && !pilot_gfx_readback(actual,sizeof(actual)));
            if(!variant)CHECK(memcmp(actual,expected,sizeof(actual))!=0); /* Fixture detects the original defect. */
            CHECK(!pilot_gfx_clear(3,0xff102030,1,0) && !pilot_gfx_fixed_window(scale,offset));
            CHECK(!pilot_gfx_fvf_prepare_fixed(0x42) && !pilot_gfx_draw_memory(5,3,fixed,sizeof(fixed[0])));
            CHECK(!pilot_gfx_readback(actual,sizeof(actual)) && !memcmp(actual,expected,sizeof(actual)));
            /* The program decal must cover a coplanar fixed wall at every
             * interior pixel with ordinary LESS_EQUAL depth, without bias. */
            for(unsigned i=0;i<3;++i)fixed[i].color=0xff00ff00;
            pilot_gfx_set_state(D3DRS_ZENABLE,1);pilot_gfx_set_state(D3DRS_ZWRITEENABLE,1);
            pilot_gfx_set_state(D3DRS_ZFUNC,D3DCMP_LESSEQUAL);
            CHECK(!pilot_gfx_clear(3,0xff102030,1,0) && !pilot_gfx_fixed_window(scale,offset));
            CHECK(!pilot_gfx_fvf_prepare_fixed(0x42) && !pilot_gfx_draw_memory(5,3,fixed,sizeof(fixed[0])));
            pilot_gfx_set_state(D3DRS_ZWRITEENABLE,0);
            CHECK(!pilot_gfx_shader_bind(shader) && !pilot_gfx_shader_raster(raster_scale,raster_offset));
            CHECK(!pilot_gfx_draw_memory(5,3,program,sizeof(program[0])) && !pilot_gfx_readback(actual,sizeof(actual)));
            CHECK(!memcmp(actual,expected,sizeof(actual)));
        }
        pilot_gfx_shader_destroy(shader);pilot_gfx_close();
    }
    fprintf(stderr,"PASS fixed/program window grid: exact pixels and coplanar decal depth, offset viewports, 2x samples and perspective W, three devices\n");
    return 0;
}
#endif
