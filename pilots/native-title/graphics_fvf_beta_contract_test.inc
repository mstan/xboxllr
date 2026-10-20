static void test_graphics_fvf_beta_declaration(int active_blend)
{
#ifdef PILOT_D3D_FVF_FLOAT_BETA_VERSION
    uint32_t fvf=0x1118;call_graphics(PILOT_D3D_D3DDevice_SetVertexShader,&fvf);
    CHECK(MEM32(PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_SNAPSHOT_VERTEX_SHADER)==fvf);
    CHECK(MEM32(PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_SNAPSHOT_VERTEX_OBJECT)==PILOT_D3D_FVF_OBJECT);
    uint32_t object=PILOT_D3D_FVF_OBJECT;
    CHECK(!MEM32(object+4) && MEM32(object+0x10)==2);
    CHECK(MEM32(object+0x1c)==0x32);
    CHECK(MEM32(object+0x28)==12 && MEM32(object+0x2c)==0x22);
    CHECK(MEM32(object+0x38)==20 && MEM32(object+0x3c)==0x32);
    CHECK(MEM32(object+0xa8)==32 && MEM32(object+0xac)==0x22);
    if(active_blend) {
        graphics_render_write(137,1); /* Original inline-cache writer. */
        uint32_t draw[]={5,3,0x1c000,40};
        call_graphics(PILOT_D3D_D3DDevice_DrawVerticesUP,draw);CHECK(0);
    }
    uint32_t ordinary=0x1c4;call_graphics(PILOT_D3D_D3DDevice_SetVertexShader,&ordinary);
    CHECK(MEM32(object+0x2c)==2 && MEM32(object+0x3c)==2);
    CHECK(MEM32(object+4)==0xc02 && MEM32(object+0x10)==2);
    CHECK(MEM32(object+0x1c)==0x42 && MEM32(object+0xa8)==24);
    fprintf(stderr,"PASS source-admitted float beta FVF binding ABI, original descriptor and ordinary rebind clears beta/normal types\n");
#else
    (void)active_blend;
    fprintf(stderr,"SKIP float beta FVF: no source-proved admission\n");exit(77);
#endif
}
