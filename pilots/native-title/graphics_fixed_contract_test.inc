static void test_graphics_fixed_declarations(const char *failure)
{
    const uint32_t data=0x19d00;const float positions[3][3]={{-.7f,.6f,.5f},{.6f,.6f,.5f},{0,-.6f,.5f}};
    float identity[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    for(unsigned i=0;i<10;++i)memcpy(memory+TRANSFORM_STATE(TRANSFORMS)+i*64,identity,64);
    graphics_store_float(TRANSFORM_STATE(TRANSFORMS)+6*64+48,.2f); /* original inline world update */
    graphics_render_state(102,0,1);graphics_render_state(143,0,1);graphics_render_state(59,0,1);
    graphics_render_state(60,0,1);graphics_render_state(92,0,1);graphics_render_state(147,0,1);
    graphics_texture_state(0,12,2,1);graphics_texture_state(0,14,0,1); /* SELECTARG1, DIFFUSE */
    for(unsigned variant=0;variant<3;++variant) {
        uint32_t header=0x19800+variant*0x120,handle=header|1u;
        memset(memory+header,0,0x118);MEM32(header)=1;
        for(unsigned i=0;i<16;++i)MEM32(header+0x1c+i*16)=2;
        MEM32(header+0x1c)=0x32;MEM32(header+0x18+2*16)=12;MEM32(header+0x1c+2*16)=0x16;
        if(variant==0){MEM32(header+4)=0x400;MEM32(header+0x18+3*16)=16;MEM32(header+0x1c+3*16)=0x40;}
        if(variant==2){MEM32(header+0x10)=2;MEM32(header+0x18+9*16)=16;MEM32(header+0x1c+9*16)=0x22;}
        unsigned char original[0x118];memcpy(original,memory+header,sizeof(original));
        if(failure && !strcmp(failure,"declaration-shape"))MEM32(header+8)=1;
        uint64_t resident=vm_committed(0,0x100000000ull);
        call_graphics(PILOT_D3D_D3DDevice_SetVertexShader,&handle);
        CHECK(graphics_shader(handle)->fixed && graphics_shader(handle)->guest_owned);
        CHECK(MEM32(PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_SNAPSHOT_VERTEX_OBJECT)==header);
        CHECK(!memcmp(original,memory+header,sizeof(original)) && vm_committed(0,0x100000000ull)==resident);
        if(failure && !strcmp(failure,"declaration-change")) {
            MEM32(header+0x1c+2*16)=0x32;
            call_graphics(PILOT_D3D_D3DDevice_SetVertexShader,&handle);CHECK(0);
        }
        if(failure && !strcmp(failure,"declaration-lighting"))graphics_render_state(102,1,1);
        memset(memory+data,0,3*32);
        for(unsigned i=0;i<3;++i) {
            memcpy(memory+data+i*32,positions[i],12);MEM32(data+i*32+12)=256|(100<<11)|(256<<22);
            if(variant==0)MEM32(data+i*32+16)=0xff20a060;
        }
        uint32_t clear[]={0,0,0xf0,0xff000000,0x3f800000,0};call_graphics(PILOT_D3D_D3DDevice_Clear,clear);
        uint32_t draw[]={5,3,data,32};call_graphics(PILOT_D3D_D3DDevice_DrawVerticesUP,draw);
        CHECK(!failure);
        unsigned char pixels[64*64*4];CHECK(!pilot_gfx_readback(pixels,sizeof(pixels)));
        unsigned center=(24*64+36)*4;CHECK(pixels[center]==(variant==0?0x20:0xff));
        CHECK(pixels[center+1]==(variant==0?0xa0:0xff) && pixels[center+2]==(variant==0?0x60:0xff));
        call_graphics(PILOT_D3D_D3DDevice_SetVertexShader,&handle); /* retained compiled owner */
        CHECK(!memcmp(original,memory+header,sizeof(original)));
    }
    puts("PASS original fixed declarations: three layouts, untouched guest objects, borrowed header lifetime, repeated binding, inline matrix cache to real GPU pixels");
}
