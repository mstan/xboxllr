static void graphics_test_packet(uint32_t method,uint32_t value)
{
    g_ecx=method; g_edx=value;
    call_graphics(PILOT_D3D_D3DDevice_SetRenderState_Simple,NULL);
}
static void graphics_test_triangle(float depth,uint32_t color)
{
    struct {float x,y,z,w;uint32_t color;} vertices[]={
        {8,8,depth,1,color},{56,8,depth,1,color},{32,56,depth,1,color}};
    /* Same batch hook required by future guest draw entry points; the title
     * currently stops before shaders/draws are qualified. */
    graphics_sync_deferred();
    CHECK(pilot_gfx_draw_up(4,1,vertices,sizeof(vertices[0]),0x44)>=0);
}
static void graphics_test_pixel(unsigned red,unsigned green,unsigned blue,unsigned tolerance)
{
    unsigned char pixels[64*64*4];
    CHECK(pilot_gfx_readback(pixels,sizeof(pixels))>=0);
    unsigned char *p=pixels+(24*64+32)*4;
    fprintf(stderr,"STATE PIXEL rgb=%u,%u,%u expected=%u,%u,%u\n",p[0],p[1],p[2],red,green,blue);
    CHECK(abs((int)p[0]-(int)red)<=(int)tolerance);
    CHECK(abs((int)p[1]-(int)green)<=(int)tolerance);
    CHECK(abs((int)p[2]-(int)blue)<=(int)tolerance);
}
static void test_graphics_deferred_relevance(unsigned failure)
{
    const uint32_t states[]={104,105,106,107,108,109,110,111,113,114,116,118,119,120,121,122,123};
    const uint32_t values[]={0,0,1,2,0,2,1,2,1,0xff123456,0x40200000,1,1,0,0x3f800000,0x40000000,0x42000000};
    for(unsigned i=0;i<sizeof(states)/sizeof(*states);++i)
        graphics_render_state(states[i],values[i],1);
    graphics_render_state(102,1,1); /* Pretransformed vertices do not consume vertex lighting. */
    graphics_render_state(147,0,1);graphics_render_state(143,0,1);
    graphics_render_state(59,0,1);graphics_render_state(60,0,1);
    graphics_texture_state(0,12,2,1);graphics_texture_state(0,14,0,1);
    graphics_texture_state(0,16,2,1);graphics_texture_state(0,18,0,1);
    uint32_t fvf=failure==1?0x42:0x44;
    call_graphics(PILOT_D3D_D3DDevice_SetVertexShader,&fvf);
    struct Vertex {float x,y,z,w;uint32_t color;} source[]={
        {8,8,.25f,1,0xffff0000},{56,8,.25f,1,0xffff0000},{32,56,.25f,1,0xffff0000}};
    memcpy(memory+0x19600,source,sizeof(source));
    graphics_sync_deferred(); /* Invalid raw writes must invalidate a warm cache. */
    if(failure==3)graphics_render_write(104,2);
    if(failure==4)graphics_render_write(120,0x7f800000);
    uint32_t draw[]={failure==2?1u:5u,0,3};
    CHECK(pilot_gfx_clear(3,0xff0000ff,1,0)>=0);
    graphics_draw_submit(draw,0x19600,sizeof(source[0]),NULL,0,0);
    CHECK(!failure);graphics_test_pixel(255,0,0,0);
    for(unsigned i=0;i<sizeof(states)/sizeof(*states);++i)
        CHECK(graphics_render_read(states[i])==values[i]);
    CHECK(!graphics_lighting_parameter_valid(104,2) && !graphics_lighting_parameter_valid(111,3));
    fprintf(stderr,"PASS deferred relevance: 17 cached lighting/point fields, actual pretransformed triangle pixels, lighting enabled without a vertex-lighting consumer\n");
}
static void test_graphics_states(void)
{
    /* Exercise the actual three-argument callee-cleaned setter, all stages
     * and six coefficients. Adjacent cache words must survive unchanged. */
    for(unsigned stage=0;stage<4;++stage)for(unsigned state=22;state<28;++state) {
        uint32_t args[]={stage,state,0x40800000u+(stage*6+state-22)*0x10000u};
        uint32_t address=PILOT_D3D_TEXTURE_STATES+stage*128+state*4;
        uint32_t left=MEM32(address-4),right=MEM32(address+4),saved=MEM32(address);
        call_graphics(PILOT_D3D_D3DDevice_SetTextureState_BumpEnv,args);
        CHECK(MEM32(address)==args[2] && MEM32(address-4)==left && MEM32(address+4)==right);
        args[2]=saved;call_graphics(PILOT_D3D_D3DDevice_SetTextureState_BumpEnv,args);
    }
    uint32_t viewport[]={0,0,64,64,0,0x3f800000},arg=0x19200;
    memcpy(memory+arg,viewport,sizeof(viewport));
    call_graphics(PILOT_D3D_D3DDevice_SetViewport,&arg);
    CHECK(MEM32(PILOT_D3D_RENDER_STATES+57*4)==0x203);
    CHECK(MEM32(PILOT_D3D_RENDER_STATES+147*4)==0x901);
    CHECK(MEM32(PILOT_D3D_TEXTURE_STATES+128+28*4)==1);
    MEM32(PILOT_D3D_DEVICE_OBJECT+8)=0x244;
    arg=1;call_graphics(PILOT_D3D_D3DDevice_SetShaderConstantMode,&arg);
    CHECK(MEM32(PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_SHADER_CONSTANT_MODE_OFFSET)==1);
    CHECK(MEM32(PILOT_D3D_DEVICE_OBJECT+8)==0x44);
    arg=0;call_graphics(PILOT_D3D_D3DDevice_SetRenderState_CullMode,&arg);
    CHECK(MEM32(PILOT_D3D_RENDER_STATES+147*4)==0);
    graphics_test_packet(0x40354,0x201); /* LESS, with separate guest cache. */
    CHECK(MEM32(PILOT_D3D_RENDER_STATES+57*4)==0x203);
    graphics_test_packet(0x4035c,1);
    graphics_test_packet(0x40304,0);
    graphics_test_packet(0x40358,0x01010101);
    MEM32(PILOT_D3D_RENDER_STATES+102*4)=0;
    MEM32(PILOT_D3D_RENDER_STATES+103*4)=0;
    MEM32(PILOT_D3D_TEXTURE_STATES+12*4)=2; /* SELECTARG1 */
    MEM32(PILOT_D3D_TEXTURE_STATES+14*4)=0; /* DIFFUSE is a real zero value. */
    MEM32(PILOT_D3D_TEXTURE_STATES+16*4)=2;
    MEM32(PILOT_D3D_TEXTURE_STATES+18*4)=0;
    CHECK(pilot_gfx_clear(3,0xff0000ff,1,0)>=0);
    graphics_test_triangle(0.25f,0xffff0000);
    graphics_test_triangle(0.75f,0xff00ff00);
    graphics_test_pixel(255,0,0,0); /* Second, farther triangle rejected. */
    /* Warm the exact same guest words, then change native state through a
     * packet-style boundary which does not update that guest cache. A cache
     * keyed only by guest bytes would leave green TFACTOR selected here. */
    CHECK(pilot_gfx_xbox_render_state(148,0xff00ff00));
    CHECK(pilot_gfx_xbox_texture_state(0,14,3));
    CHECK(pilot_gfx_clear(3,0xff0000ff,1,0)>=0);
    graphics_test_triangle(.25f,0xffff0000);graphics_test_pixel(255,0,0,0);
    /* Conversely, raw inline writes have no native revision event. */
    MEM32(PILOT_D3D_TEXTURE_STATES+14*4)=3;
    CHECK(pilot_gfx_clear(3,0xff0000ff,1,0)>=0);
    graphics_test_triangle(.25f,0xffff0000);graphics_test_pixel(0,255,0,0);
    MEM32(PILOT_D3D_TEXTURE_STATES+14*4)=0;
    CHECK(pilot_gfx_clear(3,0xff0000ff,1,0)>=0);
    graphics_test_triangle(.25f,0xffff0000);graphics_test_pixel(255,0,0,0);
    /* Keep the intervening combiners active so stage 3 affects real pixels. */
    for(unsigned stage=1;stage<3;++stage) {
        MEM32(PILOT_D3D_TEXTURE_STATES+stage*128+12*4)=2;
        MEM32(PILOT_D3D_TEXTURE_STATES+stage*128+14*4)=1;
        MEM32(PILOT_D3D_TEXTURE_STATES+stage*128+16*4)=2;
        MEM32(PILOT_D3D_TEXTURE_STATES+stage*128+18*4)=1;
    }
    CHECK(pilot_gfx_clear(3,0xff0000ff,1,0)>=0);
    graphics_test_triangle(.25f,0xffff0000);graphics_test_pixel(255,0,0,0);
    /* The last stage has its own revision and inline-byte snapshot too. */
    CHECK(pilot_gfx_xbox_texture_state(3,12,2));
    CHECK(pilot_gfx_xbox_texture_state(3,14,3));
    CHECK(pilot_gfx_clear(3,0xff0000ff,1,0)>=0);
    graphics_test_triangle(.25f,0xffff0000);graphics_test_pixel(255,0,0,0);
    MEM32(PILOT_D3D_TEXTURE_STATES+3*128+12*4)=2;
    MEM32(PILOT_D3D_TEXTURE_STATES+3*128+14*4)=3;
    CHECK(pilot_gfx_clear(3,0xff0000ff,1,0)>=0);
    graphics_test_triangle(.25f,0xffff0000);graphics_test_pixel(0,255,0,0);
    MEM32(PILOT_D3D_TEXTURE_STATES+3*128+12*4)=graphics_texture_baseline[3][12];
    MEM32(PILOT_D3D_TEXTURE_STATES+3*128+14*4)=graphics_texture_baseline[3][14];
    CHECK(pilot_gfx_clear(3,0xff0000ff,1,0)>=0);
    graphics_test_triangle(.25f,0xffff0000);graphics_test_pixel(255,0,0,0);
    for(unsigned stage=1;stage<3;++stage) {
        const unsigned fields[]={12,14,16,18};
        for(unsigned i=0;i<4;++i)
            MEM32(PILOT_D3D_TEXTURE_STATES+stage*128+fields[i]*4)=graphics_texture_baseline[stage][fields[i]];
    }
    CHECK(pilot_gfx_xbox_render_state(148,graphics_render_read(148)));
    arg=0;call_graphics(PILOT_D3D_D3DDevice_SetRenderState_ZEnable,&arg);
    graphics_test_triangle(0.75f,0xff00ff00);
    graphics_test_pixel(0,255,0,0);

    CHECK(pilot_gfx_clear(1,0xff0000ff,1,0)>=0);
    graphics_test_packet(0x40358,0x00010000); /* Red only; retain blue. */
    graphics_test_triangle(0.25f,0xffffff00);
    graphics_test_pixel(255,0,255,0);
    graphics_test_packet(0x40358,0x01010101);
    graphics_test_packet(0x40344,0x302);graphics_test_packet(0x40348,0x303);
    graphics_test_packet(0x40304,1);
    CHECK(pilot_gfx_clear(1,0xff0000ff,1,0)>=0);
    graphics_test_triangle(0.25f,0x80ff0000);
    graphics_test_pixel(128,0,127,2); /* UNORM blend rounding tolerance. */
    arg=0x1503;call_graphics(PILOT_D3D_D3DDevice_SetRenderState_LogicOp,&arg);
    CHECK(MEM32(PILOT_D3D_RENDER_STATES+150*4)==arg);
    CHECK(pilot_gfx_clear(1,0xff0000ff,1,0)>=0);
    graphics_test_triangle(.25f,0x80ff0000);graphics_test_pixel(255,0,0,0);
    unsigned char copy_pixels[64*64*4];
    CHECK(pilot_gfx_readback(copy_pixels,sizeof(copy_pixels))>=0 && copy_pixels[(24*64+32)*4+3]==128);
    graphics_test_packet(0x40358,0x00010000);
    CHECK(pilot_gfx_clear(1,0xff0000ff,1,0)>=0);
    graphics_test_triangle(.25f,0x80ffff00);graphics_test_pixel(255,0,255,0);
    graphics_test_packet(0x40358,0x01010101);
    graphics_test_packet(0x40304,0);graphics_test_packet(0x40304,1);
    CHECK(pilot_gfx_clear(1,0xff0000ff,1,0)>=0);
    graphics_test_triangle(.25f,0x80ff0000);graphics_test_pixel(255,0,0,0);
    CHECK(!pilot_gfx_xbox_render_state(150,0x1506)); /* XOR cannot become COPY. */
    arg=0;call_graphics(PILOT_D3D_D3DDevice_SetRenderState_LogicOp,&arg);
    CHECK(MEM32(PILOT_D3D_RENDER_STATES+150*4)==0);
    CHECK(pilot_gfx_clear(1,0xff0000ff,1,0)>=0);
    graphics_test_triangle(.25f,0x80ff0000);graphics_test_pixel(128,0,127,2);
    graphics_test_packet(0x40304,0);
    graphics_test_packet(0x4033c,0x204);graphics_test_packet(0x40340,128);
    graphics_test_packet(0x40300,1);
    CHECK(pilot_gfx_clear(1,0xff0000ff,1,0)>=0);
    graphics_test_triangle(0.25f,0x40ff0000);
    graphics_test_pixel(0,0,255,0);
    graphics_test_triangle(0.25f,0xff00ff00);
    graphics_test_pixel(0,255,0,0);
    graphics_test_packet(0x40300,0);
    CHECK(pilot_gfx_clear(1,0xff0000ff,1,0)>=0);
    arg=0x900;call_graphics(PILOT_D3D_D3DDevice_SetRenderState_CullMode,&arg);
    graphics_test_triangle(0.25f,0xffff0000); graphics_test_pixel(0,0,255,0);
    arg=0x901;call_graphics(PILOT_D3D_D3DDevice_SetRenderState_CullMode,&arg);
    graphics_test_triangle(0.25f,0xff00ff00); graphics_test_pixel(0,255,0,0);
    CHECK(!pilot_gfx_xbox_render_state(143,2)); /* W-buffer cannot silently become Z. */
    CHECK(!pilot_gfx_xbox_render_state(139,0x1b00)); /* Point fill cannot become wireframe. */
    CHECK(!pilot_gfx_xbox_texture_state(0,12,99));
    /* Original setter/cache qualification distinguishes the requested value
     * from target-conditioned hardware enable. HLE owns its native sampling;
     * both boolean requests retain a readable cache and real draw completion. */
    const uint32_t noise_values[]={0,1,1,0};
    for(unsigned i=0;i<4;++i) {
        uint32_t noise_args[]={159,noise_values[i]};
        call_graphics(PILOT_D3D_D3DDevice_SetRenderStateNotInline,noise_args);
        CHECK(MEM32(PILOT_D3D_RENDER_STATES+159*4)==noise_values[i]);
        CHECK(pilot_gfx_clear(3,0xff0000ff,1,0)>=0);
        graphics_test_triangle(.25f,0xff2468ac);graphics_test_pixel(36,104,172,0);
    }
    CHECK(!pilot_gfx_xbox_render_state(159,2));
    fprintf(stderr,"PASS: actual depth rejection, write masks, blend, alpha test, culling and inline diffuse texture-stage selection\n");
}
