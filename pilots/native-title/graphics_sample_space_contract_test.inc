/* A programmable shader emits original sample-space pixels, unlike the
 * fixed-function path's clip-space output. Check actual GPU coverage so the
 * two-times position/size regression cannot pass with identity matrices. */
static void test_graphics_sample_space(void)
{
    uint32_t scratch=0,bytes=4096;CHECK(!vm_allocate(&scratch,0,&bytes,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    const uint32_t declaration[]={0x12345,0x20000000,0x40420000,0x82000064,
        0x3f800000,0x3f800000,0x3f800000,0x3f800000,0xffffffffu};
    const uint32_t program[]={0x00022078,0,0x0020001b,0x08000000,0x0000f800,0,0x002c801b,0x0c000000,0x0000f819};
    memcpy(memory+scratch,declaration,sizeof(declaration));memcpy(memory+scratch+64,program,sizeof(program));
    uint32_t previous=MEM32(PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_SNAPSHOT_VERTEX_SHADER);
    uint32_t create[]={scratch,scratch+64,scratch+128,0};CHECK(!call_graphics(PILOT_D3D_D3DDevice_CreateVertexShader,create));
    uint32_t shader=MEM32(scratch+128);call_graphics(PILOT_D3D_D3DDevice_SetVertexShader,&shader);
    graphics_render_state(143,0,1);graphics_render_state(59,0,1);graphics_render_state(60,0,1);
    graphics_render_state(92,0,1);graphics_render_state(147,0,1);
    graphics_texture_state(0,12,2,1);graphics_texture_state(0,14,0,1);
    float sx=graphics_float(TRANSFORM_FIELD(PIXEL_SCALE)),sy=graphics_float(TRANSFORM_FIELD(PIXEL_SCALE)+4);
    float depth=graphics_float(TRANSFORM_FIELD(DEPTH_SCALE));
    const float positions[3][2]={{8,8},{52,8},{8,52}};
    for(unsigned i=0;i<3;++i) {
        float vertex[]={positions[i][0]*sx,positions[i][1]*sy,.5f*depth,1};
        memcpy(memory+scratch+256+i*16,vertex,sizeof(vertex));
    }
    uint32_t clear[]={0,0,0xf0,0xff000000,0x3f800000,0};call_graphics(PILOT_D3D_D3DDevice_Clear,clear);
    uint32_t draw[]={5,3,scratch+256,16};call_graphics(PILOT_D3D_D3DDevice_DrawVerticesUP,draw);
    /* Gaussian's logical readback surface is produced by presentation resolve. */
    if(graphics_parameters[4]==0x2222)CHECK(!pilot_gfx_present());
    unsigned char pixels[64*64*4];CHECK(!pilot_gfx_readback(pixels,sizeof(pixels)));
    unsigned inside=(16*64+16)*4,outside=(56*64+56)*4;
    for(unsigned c=0;c<3;++c)CHECK(pixels[inside+c]==255 && pixels[outside+c]==0);
    if(previous)call_graphics(PILOT_D3D_D3DDevice_SetVertexShader,&previous);
    bytes=0;CHECK(!vm_free(&scratch,&bytes,MEM_RELEASE));
    fprintf(stderr,"PASS programmable sample-space raster: axis=%.0f,%.0f actual inside/outside GPU coverage\n",sx,sy);
}
