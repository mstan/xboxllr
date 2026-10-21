#ifdef PILOT_D3D_PHOBOS
static int test_initializer_states(void)
{
    uint32_t p[17]={64,64,7,1,0x11,1,0,0,1,0x2e,0,60,1};CHECK(pilot_gfx_create(p)==0);
    CHECK(!pilot_gfx_command_method(0x1710,1,0));
    CHECK(!pilot_gfx_command_method(0x304,2,0));
    CHECK(!pilot_gfx_command_method(0x7fc,0,0));
    CHECK(pilot_gfx_command_method(0x310,1,0));
    uint32_t sampled=0;CHECK(!pilot_gfx_texture_create(2,2,1,0x12,&sampled));
    for(unsigned stage=0;stage<4;++stage) {
        uint32_t method=0x1b0c+stage*64;pilot_gfx_texture_bind(stage,sampled);
        IDirect3DBaseTexture8 *before=d3d8_GetStageTexture(stage);CHECK(before);
        CHECK(pilot_gfx_command_method(method,0,0) && d3d8_GetStageTexture(stage)==before);
        CHECK(!pilot_gfx_command_method(method,0x40000000,0) && d3d8_GetStageTexture(stage)==before);
        CHECK(pilot_gfx_command_method(method,0,1) && !d3d8_GetStageTexture(stage));
        pilot_gfx_texture_bind(stage,sampled);CHECK(d3d8_GetStageTexture(stage)==before);
        pilot_gfx_texture_bind(stage,0);
    }
    CHECK(!pilot_gfx_command_method(0x1c0c,0,0) && !pilot_gfx_command_method(0x1b10,0,0));
    pilot_gfx_texture_destroy(sampled);
    struct PilotInitializerState untouched;CHECK(pilot_gfx_initializer_snapshot(&untouched) && !untouched.dither);
    CHECK(pilot_gfx_command_method(0x9f8,15,0));
    struct PilotInitializerState probe;CHECK(pilot_gfx_initializer_snapshot(&probe) && !memcmp(&probe,&untouched,sizeof(probe)));
    CHECK(pilot_gfx_command_method(0x9f8,15,1) && pilot_gfx_command_method(0x9f8,4,1));
    CHECK(pilot_gfx_initializer_snapshot(&probe) && probe.swath_width==4 && probe.raster_schedule_policy==PILOT_RASTER_HOST_AUTOMATIC);
    CHECK(!pilot_gfx_command_method(0x9f8,5,1));
    uint32_t buffer=0,data[4]={1,2,3,4},read[4];
    CHECK(!pilot_gfx_vertex_create(sizeof(data),&buffer));pilot_gfx_vertex_upload(buffer,data,sizeof(data));
    CHECK(pilot_gfx_command_method(0x1710,0,0) && pilot_gfx_command_method(0x1710,0,1));
    CHECK(!pilot_gfx_vertex_readback(buffer,read,sizeof(read)) && !memcmp(data,read,sizeof(read)));
    data[0]=9;data[3]=8;pilot_gfx_vertex_upload_range(buffer,data,4,4);
    CHECK(!pilot_gfx_vertex_readback(buffer,read,sizeof(read)) && !memcmp(data,read,sizeof(read)));
    pilot_gfx_vertex_destroy(buffer);
    const uint32_t packets[][2]={{0x4037c,0x1d01},{0x4035c,1},{0x40300,0},{0x40344,1},{0x40348,0},
        {0x40354,0x203},{0x40340,0},{0x4033c,0x207},{0x40310,1},{0x40304,1}};
    for(unsigned i=0;i<sizeof(packets)/sizeof(*packets);++i){uint32_t state;CHECK(pilot_graphics_simple_decode(packets[i][0],&state));CHECK(pilot_gfx_xbox_render_state(state,packets[i][1]));}
    ID3D11DeviceContext *context=d3d8_GetD3D11Context();
    D3D11_RASTERIZER_DESC desc={0};desc.FillMode=D3D11_FILL_SOLID;desc.CullMode=D3D11_CULL_NONE;desc.DepthClipEnable=TRUE;
    desc.DepthBias=31;desc.DepthBiasClamp=2;desc.SlopeScaledDepthBias=3;
    ID3D11RasterizerState *biased=NULL;CHECK(SUCCEEDED(ID3D11Device_CreateRasterizerState(d3d8_GetD3D11Device(),&desc,&biased)));
    ID3D11DeviceContext_RSSetState(context,biased);ID3D11RasterizerState_Release(biased);
    CHECK(pilot_gfx_xbox_render_state(149,0));ID3D11RasterizerState *actual=NULL;
    ID3D11DeviceContext_RSGetState(context,&actual);CHECK(actual);ID3D11RasterizerState_GetDesc(actual,&desc);ID3D11RasterizerState_Release(actual);
    CHECK(!desc.DepthBias && desc.DepthBiasClamp==0 && desc.SlopeScaledDepthBias==0 && desc.CullMode==D3D11_CULL_NONE);
    CHECK(pilot_gfx_xbox_render_state(142,0) && pilot_gfx_xbox_render_state(137,0));
    CHECK(pilot_gfx_xbox_render_state(142,1));
    CHECK(pilot_gfx_xbox_render_state(112,2) && pilot_gfx_xbox_render_state(117,0x3f800000));
    struct PilotInitializerState observed;CHECK(pilot_gfx_initializer_snapshot(&observed));
    CHECK(observed.precision_policy==PILOT_COLOR_NATIVE_RGBA8 && observed.dither && observed.zero_bias && observed.controls==15);
    CHECK(observed.normalize==1);
    const uint32_t rejected[][2]={{65,2},{149,1},{142,2},{137,1},{112,1},{117,0x40000000}};
    for(unsigned i=0;i<sizeof(rejected)/sizeof(*rejected);++i) {
        CHECK(!pilot_gfx_xbox_render_state(rejected[i][0],rejected[i][1]));
        struct PilotInitializerState after;CHECK(pilot_gfx_initializer_snapshot(&after) && !memcmp(&after,&observed,sizeof(after)));
    }
    CHECK(pilot_gfx_initializer_consumer(1,5,0) && pilot_gfx_initializer_consumer(0,5,0x44));
    CHECK(!pilot_gfx_initializer_consumer(0,5,0x42) && !pilot_gfx_initializer_consumer(1,1,0));
    uint32_t invalid=0;CHECK(pilot_gfx_texture_create_usage(8,8,1,5,1,&invalid)<0 && !invalid);
    pilot_gfx_set_state(D3DRS_ZENABLE,0);pilot_gfx_set_state(D3DRS_ALPHABLENDENABLE,0);
    pilot_gfx_set_state(D3DRS_CULLMODE,D3DCULL_NONE);pilot_gfx_set_texture_state(0,D3DTSS_COLOROP,D3DTOP_DISABLE);
    struct {float x,y,z,w;uint32_t color;} triangle[]={{8,8,.5f,1,0xff123456},{56,8,.5f,1,0xff123456},{32,56,.5f,1,0xff123456}};
    unsigned char off[64*64*4],on[sizeof(off)];
    CHECK(pilot_gfx_xbox_render_state(65,0));CHECK(pilot_gfx_clear(1,0xff000000,1,0)==0);
    CHECK(pilot_gfx_draw_up(4,1,triangle,sizeof(*triangle),0x44)==0 && pilot_gfx_readback(off,sizeof(off))==0);
    CHECK(pilot_gfx_xbox_render_state(65,1));CHECK(pilot_gfx_clear(1,0xff000000,1,0)==0);
    CHECK(pilot_gfx_draw_up(4,1,triangle,sizeof(*triangle),0x44)==0 && pilot_gfx_readback(on,sizeof(on))==0);
    CHECK(!memcmp(off,on,sizeof(off))); /* explicit native quantization policy, not NV2A equality */
    unsigned center=(24*64+32)*4;CHECK(on[center]==0x12 && on[center+1]==0x34 && on[center+2]==0x56);
    CHECK(pilot_gfx_draw_up(4,1,triangle,sizeof(*triangle),0x42)<0);
    CHECK(!test_native_draw_pipeline());
    uint32_t bad[17];memcpy(bad,p,sizeof(bad));bad[4]=0x1222;
    CHECK(pilot_gfx_reset(bad)<0);
    struct PilotInitializerState retained;CHECK(pilot_gfx_initializer_snapshot(&retained) && !memcmp(&retained,&observed,sizeof(retained)));
    CHECK(pilot_gfx_reset(p)==0 && pilot_gfx_initializer_snapshot(&observed));
    CHECK(observed.precision_policy==PILOT_COLOR_NATIVE_RGBA8 && !observed.dither && !observed.controls && !observed.zero_bias && !observed.raster_schedule_policy);
    pilot_gfx_close();CHECK(!pilot_gfx_initializer_snapshot(&observed));
    /* The reached guest Gaussian/no-depth presentation uses the same owned
     * RGBA8 precision policy after its real 2x raster and logical resolve. */
    p[4]=0x2222;p[8]=p[9]=0;p[10]=0x20;
    CHECK(pilot_gfx_create(p)==0 && pilot_gfx_initializer_snapshot(&observed));
    CHECK(observed.precision_policy==PILOT_COLOR_NATIVE_RGBA8 && !observed.dither && !observed.controls && !observed.zero_bias);
    CHECK(pilot_gfx_xbox_render_state(65,1) && pilot_gfx_xbox_render_state(149,0));
    pilot_gfx_set_state(D3DRS_ZENABLE,0);pilot_gfx_set_state(D3DRS_ALPHABLENDENABLE,0);
    pilot_gfx_set_state(D3DRS_CULLMODE,D3DCULL_NONE);pilot_gfx_set_texture_state(0,D3DTSS_COLOROP,D3DTOP_DISABLE);
    /* This low-level donor DrawUP test supplies raster coordinates, unlike
     * the runtime's guest FVF/program transform owner. */
    for(unsigned i=0;i<3;++i){triangle[i].x*=2;triangle[i].y*=2;}
    CHECK(pilot_gfx_clear(1,0xff000000,1,0)==0);
    CHECK(pilot_gfx_draw_up(4,1,triangle,sizeof(*triangle),0x44)==0 && pilot_gfx_present()==0 && pilot_gfx_readback(on,sizeof(on))==0);
    CHECK(on[center]==0x12 && on[center+1]==0x34 && on[center+2]==0x56);
    pilot_gfx_close();fprintf(stderr,"PASS initializer native RGBA8 precision request/pixels on legacy and Gaussian/no-depth presentation, real zero bias, inactive controls, program/pretransformed consumers and guards\n");return 0;
}
#endif
