#ifdef PILOT_D3D_PHOBOS
static int test_combiner_memory_draw(void)
{
    uint32_t viewport[]={0,0,64,64,0,0x3f800000};CHECK(!pilot_gfx_viewport(viewport));
    pilot_gfx_set_state(D3DRS_ZENABLE,0);pilot_gfx_set_state(D3DRS_ALPHABLENDENABLE,0);
    pilot_gfx_set_state(D3DRS_ALPHATESTENABLE,0);pilot_gfx_set_state(D3DRS_FOGENABLE,0);
    pilot_gfx_set_state(D3DRS_CULLMODE,D3DCULL_NONE);
    for(unsigned i=0;i<4;++i)pilot_gfx_texture_bind(i,0);
    /* Public NV_register_combiners final formula: RGB=D=C0, alpha=G=C0.a.
     * Zero general stages. Changes to C0 must affect real GPU pixels. */
    uint32_t state[57]={0};state[8]=1;state[9]=0x1100;state[43]=0xff204060;
    CHECK(!pilot_gfx_pixel_program(state,0));
    struct {float x,y,z,w;uint32_t color;} v[]={{8,8,.5f,1,0xffffffff},{56,8,.5f,1,0xffffffff},{32,56,.5f,1,0xffffffff}};
    CHECK(!pilot_gfx_fvf_prepare(0x44,0,0));
    uint8_t pixels[64*64*4];unsigned center=(24*64+32)*4;
    for(unsigned i=0;i<2;++i) {
        if(i){state[43]=0xff806040;CHECK(!pilot_gfx_pixel_program(state,0));}
        CHECK(!pilot_gfx_draw_memory(5,3,v,sizeof(v[0])) && !pilot_gfx_readback(pixels,sizeof(pixels)));
        uint32_t color=state[43];
        CHECK(pixels[center]==((color>>16)&255) && pixels[center+1]==((color>>8)&255) && pixels[center+2]==(color&255));
        CHECK(pixels[center+3]==255);
    }
    CHECK(pilot_gfx_pixel_program(state,5)<0); /* No ignored clip-plane work. */
    pilot_gfx_pixel_fixed();
    fprintf(stderr,"PASS: real Phobos zero-stage combiner, constant changes and ring-buffer UP draws\n");
    return 0;
}
#endif
