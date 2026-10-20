#ifdef PILOT_D3D_PHOBOS
static int test_fixed_declarations(void)
{
    uint32_t parameters[17]={64,64,7,1,0x11,1,0,0,1,0x2e,0,60,1};
    CHECK(pilot_gfx_create(parameters)==0);
    pilot_gfx_set_state(D3DRS_CULLMODE,D3DCULL_NONE);pilot_gfx_set_state(D3DRS_ZENABLE,0);
    pilot_gfx_set_state(D3DRS_LIGHTING,0);pilot_gfx_set_state(D3DRS_ALPHABLENDENABLE,0);
    pilot_gfx_set_state(D3DRS_ALPHATESTENABLE,0);pilot_gfx_set_state(D3DRS_FOGENABLE,0);
    for(unsigned stage=0;stage<4;++stage) {
        pilot_gfx_texture_bind(stage,0);pilot_gfx_set_texture_state(stage,D3DTSS_COLOROP,D3DTOP_DISABLE);
        pilot_gfx_set_texture_state(stage,D3DTSS_TEXCOORDINDEX,stage);
    }
    uint32_t texture=0,pixels[64];
    for(unsigned y=0;y<8;++y)for(unsigned x=0;x<8;++x) {
        unsigned at=0;for(unsigned bit=0;bit<3;++bit)at|=((x>>bit)&1)<<(2*bit)|((y>>bit)&1)<<(2*bit+1);
        pixels[at]=0xff000000u|((x*31)<<16)|((y*31)<<8)|((x+y)*15);
    }
    CHECK(!pilot_gfx_texture_create(8,8,1,6,&texture));pilot_gfx_texture_upload(texture,0,pixels,sizeof(pixels));
    const float positions[3][3]={{-.7f,.6f,.5f},{.6f,.6f,.5f},{0,-.6f,.5f}};
    const int packed[3][3]={{256,100,256},{600,300,128},{200,500,200}};
    unsigned char native[64*64*4],reference[sizeof(native)];
    for(unsigned variant=0;variant<4;++variant)for(unsigned transform=0;transform<2;++transform) {
        struct PilotVertexElement elements[16]={0};
        elements[0]=(struct PilotVertexElement){0x32,0,0};elements[2]=(struct PilotVertexElement){0x16,0,12};
        if(variant==0)elements[3]=(struct PilotVertexElement){0x40,0,16};
        if(variant==2)elements[9]=(struct PilotVertexElement){0x22,0,16};
        uint32_t shader=0;CHECK(!pilot_gfx_shader_create_fixed(elements,&shader));
        CHECK(!pilot_gfx_shader_bind(shader));
        uint32_t extents[16];CHECK(pilot_gfx_shader_streams(extents)==1 && extents[0]==(variant==0?20:variant==2?24:16));
        unsigned char source[3][32]={0},expanded[3*40]={0};
        uint32_t fvf=0x12|(variant==0?0x40:variant==2?0x100:0),stride=24+(variant==0?4:variant==2?8:0);
        for(unsigned vertex=0;vertex<3;++vertex) {
            unsigned char *ref=expanded+vertex*stride;
            memcpy(source[vertex],positions[vertex],12);memcpy(ref,positions[vertex],12);
            uint32_t normal=((uint32_t)packed[vertex][0]&2047)|(((uint32_t)packed[vertex][1]&2047)<<11)|(((uint32_t)packed[vertex][2]&1023)<<22);
            memcpy(source[vertex]+12,&normal,4);
            float decoded[3]={packed[vertex][0]/1023.0f,packed[vertex][1]/1023.0f,packed[vertex][2]/511.0f};
            memcpy(ref+12,decoded,12);
            uint32_t color=0xff20a060u;float uv[2]={vertex==1?.8f:.1f,vertex==2?.8f:.1f};
            if(variant==0){memcpy(source[vertex]+16,&color,4);memcpy(ref+24,&color,4);}
            if(variant==2){memcpy(source[vertex]+16,uv,8);memcpy(ref+24,uv,8);}
        }
        float matrix[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
        CHECK(!pilot_gfx_transform(0,matrix) && !pilot_gfx_transform(1,matrix));
        matrix[0]=transform?.75f:1;matrix[5]=transform?.8f:1;matrix[12]=transform?.2f:0;
        CHECK(!pilot_gfx_transform(6,matrix));
        pilot_gfx_texture_bind(0,variant>=2?texture:0);
        pilot_gfx_set_texture_state(0,D3DTSS_TEXCOORDINDEX,variant==3?0x10000:0);
        pilot_gfx_set_texture_state(0,D3DTSS_COLOROP,D3DTOP_SELECTARG1);
        pilot_gfx_set_texture_state(0,D3DTSS_COLORARG1,variant>=2?D3DTA_TEXTURE:D3DTA_DIFFUSE);
        CHECK(!pilot_gfx_clear(1,0xff000000,1,0));CHECK(!pilot_gfx_shader_prepare_fixed(shader));
        CHECK(!pilot_gfx_draw_memory(5,3,source,sizeof(source[0])) && !pilot_gfx_readback(native,sizeof(native)));
        CHECK(!pilot_gfx_clear(1,0xff000000,1,0));
        CHECK(!pilot_gfx_draw_up(4,1,expanded,stride,fvf) && !pilot_gfx_readback(reference,sizeof(reference)));
        unsigned lit=0;for(unsigned i=0;i<sizeof(native);++i){CHECK(abs((int)native[i]-(int)reference[i])<=1);if((i&3)!=3 && native[i])++lit;}
        CHECK(lit>200);pilot_gfx_shader_destroy(shader);
    }
    const uint32_t fvfs[]={2,0x42,0x52,0x142,0x20152};
    const unsigned sizes[]={12,16,28,24,44};
    ID3D11VertexShader *cached[5]={0};
    for(unsigned transform=0;transform<2;++transform)for(unsigned variant=0;variant<5;++variant) {
        uint32_t fvf=fvfs[variant];unsigned stride=sizes[variant];
        CHECK(pilot_gfx_fvf_extent(fvf)==stride);
        unsigned char source[3*44]={0};
        for(unsigned i=0;i<3;++i) {
            unsigned char *vertex=source+i*stride;unsigned offset=12;
            memcpy(vertex,positions[i],12);
            if(fvf&0x10){const float normal[3]={.25f,-.5f,1};memcpy(vertex+offset,normal,12);offset+=12;}
            if(fvf&0x40){uint32_t color=0xff20a060;memcpy(vertex+offset,&color,4);offset+=4;}
            if(fvf&0x100){float uv[4]={i==1?.8f:.1f,i==2?.8f:.1f,.25f,1};memcpy(vertex+offset,uv,stride-offset);}
        }
        float matrix[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
        CHECK(!pilot_gfx_transform(0,matrix) && !pilot_gfx_transform(1,matrix));
        matrix[0]=transform?.75f:1;matrix[5]=transform?.8f:1;matrix[12]=transform?.2f:0;
        CHECK(!pilot_gfx_transform(6,matrix));
        pilot_gfx_texture_bind(0,(fvf&0x100)?texture:0);
        pilot_gfx_set_texture_state(0,D3DTSS_TEXCOORDINDEX,0);
        pilot_gfx_set_texture_state(0,D3DTSS_COLORARG1,(fvf&0x100)?D3DTA_TEXTURE:D3DTA_DIFFUSE);
        CHECK(!pilot_gfx_clear(1,0xff000000,1,0) && !pilot_gfx_fvf_prepare_fixed(fvf));
        ID3D11VertexShader *bound=NULL;ID3D11DeviceContext_VSGetShader(d3d8_GetD3D11Context(),&bound,NULL,NULL);
        if(transform){CHECK(bound==cached[variant]);ID3D11VertexShader_Release(bound);}else cached[variant]=bound;
        CHECK(!pilot_gfx_draw_memory(5,3,source,stride) && !pilot_gfx_readback(native,sizeof(native)));
        CHECK(!pilot_gfx_clear(1,0xff000000,1,0));
        CHECK(!pilot_gfx_draw_up(4,1,source,stride,fvf) && !pilot_gfx_readback(reference,sizeof(reference)));
        unsigned lit=0;for(unsigned i=0;i<sizeof(native);++i){CHECK(abs((int)native[i]-(int)reference[i])<=1);if((i&3)!=3 && native[i])++lit;}
        CHECK(lit>200);
    }
    for(unsigned i=0;i<5;++i)ID3D11VertexShader_Release(cached[i]);
    fprintf(stderr,"PASS XYZ FVF: five source layouts, FLOAT3 normals, color/texture inputs, two matrix states, cached shader identity and pixels versus direct fixed-function reference\n");
    pilot_gfx_texture_bind(0,0);pilot_gfx_texture_destroy(texture);pilot_gfx_close();
    fprintf(stderr,"PASS fixed declarations: three source layout classes, packed-normal GPU decode via normal texgen, two matrix states, actual pixels versus expanded-float fixed-function reference\n");return 0;
}
#endif
