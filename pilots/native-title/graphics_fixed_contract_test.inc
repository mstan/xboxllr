static void test_graphics_fixed_declarations(const char *failure)
{
    const uint32_t data=0x19d00;const float positions[3][3]={{-.7f,.6f,.5f},{.6f,.6f,.5f},{0,-.6f,.5f}};
    float identity[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    for(unsigned i=0;i<10;++i)memcpy(memory+TRANSFORM_STATE(TRANSFORMS)+i*64,identity,64);
    /* Exercise the production derived-matrix writer. Directly seeding both
     * matrices with identity cannot detect a profile that aliases its output
     * with the raw projection input. */
    uint32_t source=data+512;memcpy(memory+source,identity,64);
    uint32_t transform_args[]={1,source};call_graphics(PILOT_D3D_D3DDevice_SetTransform,transform_args);
    CHECK(!memcmp(memory+TRANSFORM_STATE(TRANSFORMS)+64,identity,64));
    unsigned char derived[64];memcpy(derived,memory+TRANSFORM_FIELD(PROJECTION_VIEWPORT),64);
    graphics_projection_viewport();
    CHECK(!memcmp(memory+TRANSFORM_STATE(TRANSFORMS)+64,identity,64));
    CHECK(!memcmp(derived,memory+TRANSFORM_FIELD(PROJECTION_VIEWPORT),64));
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
    /* Source-decoded XYZ FVF uses the same cached matrix owner. Both FLOAT3
     * normals and no-normal layouts must reach actual GPU drawing. */
    const uint32_t fvfs[]={0x42,0x52,0x142,0x20152};
    const unsigned strides[]={16,28,24,44};
    uint32_t previous=MEM32(PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_SNAPSHOT_VERTEX_SHADER);
    for(unsigned variant=0;variant<4;++variant) {
        uint32_t fvf=fvfs[variant];call_graphics(PILOT_D3D_D3DDevice_SetVertexShader,&fvf);
        unsigned normal=(fvf&0x10)!=0,color=normal?24:12;
        CHECK(MEM32(PILOT_D3D_FVF_OBJECT+4)==0x400 && MEM32(PILOT_D3D_FVF_OBJECT+0x1c)==0x32);
        CHECK(MEM32(PILOT_D3D_FVF_OBJECT+0x48)==color && MEM32(PILOT_D3D_FVF_OBJECT+0x4c)==0x40);
        CHECK(MEM32(PILOT_D3D_FVF_OBJECT+0x3c)==(normal?0x32:2));
        if(normal)CHECK(MEM32(PILOT_D3D_FVF_OBJECT+0x38)==12);
        if(variant>=2)CHECK(MEM32(PILOT_D3D_FVF_OBJECT+0xa8)==color+4 &&
            MEM32(PILOT_D3D_FVF_OBJECT+0xac)==(variant==2?0x22:0x42) &&
            MEM32(PILOT_D3D_FVF_OBJECT+0x10)==(variant==2?2:4));
        memset(memory+data,0,3*44);
        for(unsigned i=0;i<3;++i) {
            memcpy(memory+data+i*strides[variant],positions[i],12);
            if(normal)graphics_store_float(data+i*strides[variant]+20,1);
            MEM32(data+i*strides[variant]+color)=0xff20a060;
        }
        uint32_t clear[]={0,0,0xf0,0xff000000,0x3f800000,0};call_graphics(PILOT_D3D_D3DDevice_Clear,clear);
        uint32_t draw[]={5,3,data,strides[variant]};call_graphics(PILOT_D3D_D3DDevice_DrawVerticesUP,draw);
        unsigned char pixels[64*64*4];CHECK(!pilot_gfx_readback(pixels,sizeof(pixels)));
        unsigned center=(24*64+36)*4;CHECK(pixels[center]==0x20 && pixels[center+1]==0xa0 && pixels[center+2]==0x60);
        call_graphics(PILOT_D3D_D3DDevice_SetVertexShader,&fvf);
    }
    call_graphics(PILOT_D3D_D3DDevice_SetVertexShader,&previous);
    puts("PASS original fixed declarations: three layouts, untouched guest objects, borrowed header lifetime, repeated binding, inline matrix cache to real GPU pixels");
    puts("PASS XYZ FVF: four source layouts, original header/query owner, FLOAT3 normal, color and coordinates to real GPU pixels");
}
