static void test_graphics_fvf(void)
{
    GRAPHICS_TEST_REQUIRE("FVF query/restore fixture",PILOT_D3D_D3DDevice_GetVertexShader);
    uint32_t previous=MEM32(PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_SNAPSHOT_VERTEX_SHADER);
    uint32_t constants[192][4];memcpy(constants,memory+graphics_vertex_constants_address(),sizeof(constants));
    /* Sequential calls also expose stale format/offset fields. Descriptor
     * captures are checked against the complete original decoder offline. */
    const uint32_t formats[]={0x144,0,0x344,0x3c4,0x30144,0x10144,0x20144,0x444,4,0xc4,0,0x144};
    const unsigned sizes[]={28,0,44,48,24,32,36,52,16,24,0,28};
    for(unsigned i=0;i<sizeof(formats)/sizeof(*formats);++i) {
        uint32_t fvf=formats[i];call_graphics(PILOT_D3D_D3DDevice_SetVertexShader,&fvf);
        if(fvf)CHECK(pilot_gfx_fvf_extent(fvf)==sizes[i]);
        uint32_t output=0x198d0;call_graphics(PILOT_D3D_D3DDevice_GetVertexShader,&output);CHECK(MEM32(output)==fvf);
        CHECK(MEM32(PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_SNAPSHOT_VERTEX_OBJECT)==PILOT_D3D_FVF_OBJECT);
        CHECK(MEM32(PILOT_D3D_FVF_OBJECT+0x1c)==(fvf?0x42:2));
        if(!fvf) {
            CHECK(MEM32(PILOT_D3D_FVF_OBJECT+4)==0 && MEM32(PILOT_D3D_FVF_OBJECT+0x10)==0);
            for(unsigned reg=0;reg<16;++reg)CHECK(MEM32(PILOT_D3D_FVF_OBJECT+0x1c+reg*16)==2);
        }
        CHECK(!memcmp(constants,memory+graphics_vertex_constants_address(),sizeof(constants)));
    }
    call_graphics(PILOT_D3D_D3DDevice_SetVertexShader,&previous);
    CHECK(MEM32(PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_SNAPSHOT_VERTEX_OBJECT)==previous-1);
#ifdef PILOT_D3D_PHOBOS
    uint32_t enabled=1;call_graphics(PILOT_D3D_D3DDevice_SetRenderState_EdgeAntiAlias,&enabled);
    CHECK(MEM32(PILOT_D3D_RENDER_STATES+151*4)==1);
#endif
    uint32_t disabled=0;call_graphics(PILOT_D3D_D3DDevice_SetRenderState_EdgeAntiAlias,&disabled);
    CHECK(MEM32(PILOT_D3D_RENDER_STATES+151*4)==0 && !pilot_gfx_xbox_render_state(151,2));
    fprintf(stderr,"PASS: FVF formats, query/cache ABI, programmable shader restoration, preserved constants and explicit edge-AA disable\n");
}
