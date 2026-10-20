static void test_graphics_immediate_vertices(int interleaved_cpu)
{
#ifdef PILOT_D3D_IMMEDIATE_VERSION
    uint32_t fvf=0x1c4;call_graphics(PILOT_D3D_D3DDevice_SetVertexShader,&fvf);
    graphics_render_state(143,0,1);graphics_render_state(147,0,1);
    graphics_render_state(59,0,1);graphics_render_state(60,0,1);
    graphics_texture_state(0,12,2,1);graphics_texture_state(0,14,0,1);
    uint32_t clear[]={0,0,0xf0,0xff123456,0x3f800000,0};
    call_graphics(PILOT_D3D_D3DDevice_Clear,clear);
    uint32_t flags=MEM32(PILOT_D3D_DEVICE_OBJECT+8),primitive=6;
    uint32_t color[]={3,0,0x3f800000,0,0x3f800000};
    call_graphics(PILOT_D3D_IMMEDIATE_VERTEX_DATA4F_ENTRY,color);
    CHECK(!graphics_immediate.primitive && !graphics_immediate.count);
    call_graphics(PILOT_D3D_IMMEDIATE_BEGIN_ENTRY,&primitive);
    CHECK(MEM32(PILOT_D3D_DEVICE_OBJECT+8)==(flags|0x800));
    if(interleaved_cpu) {
        uint32_t output=0x19400;
        call_graphics(PILOT_D3D_D3DDevice_GetGammaRamp,&output);CHECK(0);
    }
    const float positions[4][4]={{8,8,.5f,.5f},{8,56,.5f,.5f},{56,8,.5f,.5f},{56,56,.5f,.5f}};
    for(unsigned i=0;i<4;++i) {
        uint32_t uv[]={9,(i/2)?0x3f800000:0,(i%2)?0x3f800000:0};
        call_graphics(PILOT_D3D_IMMEDIATE_VERTEX_DATA2F_ENTRY,uv);
        CHECK(graphics_immediate.count==i);
        uint32_t position[5]={i&1?0:UINT32_MAX};memcpy(position+1,positions[i],16);
        call_graphics(PILOT_D3D_IMMEDIATE_VERTEX_DATA4F_ENTRY,position);
        CHECK(graphics_immediate.count==i+1 && !memcmp(graphics_immediate.vertices[i][0],positions[i],16));
        CHECK(graphics_immediate.vertices[i][3][1]==1 && graphics_immediate.vertices[i][9][2]==0 && graphics_immediate.vertices[i][9][3]==1);
    }
    CHECK(graphics_immediate.vertices[0][9][0]==0 && graphics_immediate.vertices[0][9][1]==0);
    call_graphics(PILOT_D3D_IMMEDIATE_END_ENTRY,NULL);
    CHECK(!graphics_immediate.primitive && !graphics_immediate.count && MEM32(PILOT_D3D_DEVICE_OBJECT+8)==(flags&~0x1800u));
    unsigned char pixels[64*64*4];CHECK(!pilot_gfx_readback(pixels,sizeof(pixels)));
    for(unsigned y=0;y<64;++y)for(unsigned x=0;x<64;++x) {
        unsigned pixel=(y*64+x)*4;int inside=x>=8 && x<56 && y>=8 && y<56;
        CHECK(pixels[pixel]==(inside?0:0x12) && pixels[pixel+1]==(inside?255:0x34) && pixels[pixel+2]==(inside?0:0x56));
    }
    float current[4];CHECK(pilot_gfx_vertex_current_get(3,current) && current[1]==1);
    call_graphics(PILOT_D3D_IMMEDIATE_BEGIN_ENTRY,&primitive);call_graphics(PILOT_D3D_IMMEDIATE_END_ENTRY,NULL);
    CHECK(pilot_gfx_vertex_current_get(3,current) && current[1]==1);
    fprintf(stderr,"PASS original immediate ABI, persistent colors, 2f Z/W completion, position0/-1 latching, independent vertex snapshots, flags, whole quad pixels and empty restart\n");
#else
    (void)interleaved_cpu;
    fprintf(stderr,"SKIP immediate API fixture: profile has no source-proved admission\n");exit(77);
#endif
}
