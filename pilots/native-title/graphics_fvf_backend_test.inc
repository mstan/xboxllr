static int test_fvf_pipeline(void)
{
    struct FvfVertex {float x,y,z,rhw;uint32_t color;float uv0[2],uv1[2];};
    struct FvfVertex vertices[8]={0};
    vertices[2]=(struct FvfVertex){8,8,.375f,1,0xffffffff,{.5f,.5f},{1,1}};
    vertices[3]=(struct FvfVertex){8,56,.375f,.5f,0xffffffff,{.5f,.5f},{1,9}};
    vertices[4]=(struct FvfVertex){56,8,.375f,.25f,0xffffffff,{.5f,.5f},{9,1}};
    vertices[5]=(struct FvfVertex){56,56,.375f,.5f,0xffffffff,{.5f,.5f},{9,9}};
    uint32_t vertex,texture;CHECK(pilot_gfx_vertex_create(sizeof(vertices),&vertex)==0);
    pilot_gfx_vertex_upload(vertex,vertices,sizeof(vertices));pilot_gfx_vertex_bind(0,vertex,sizeof(vertices[0]));
    CHECK(pilot_gfx_texture_create(8,8,1,0x12,&texture)==0);
    uint32_t texels[128];for(unsigned y=0;y<8;++y)for(unsigned x=0;x<16;++x)texels[y*16+x]=0xff000080|(x*32<<16)|(y*32<<8);
    pilot_gfx_texture_prepare_sample(texture,0,texels,sizeof(texels),1);pilot_gfx_texture_bind(0,texture);
    for(unsigned i=0;i<4;++i)CHECK(pilot_gfx_xbox_texture_state(i,12,i?1:2));
    CHECK(pilot_gfx_xbox_texture_state(0,14,2) && pilot_gfx_xbox_texture_state(0,16,2) && pilot_gfx_xbox_texture_state(0,18,2));
    CHECK(pilot_gfx_xbox_texture_state(0,3,1) && pilot_gfx_xbox_texture_state(0,4,1) && pilot_gfx_xbox_texture_state(0,5,0));
    CHECK(pilot_gfx_xbox_texture_state(0,7,0) && pilot_gfx_xbox_texture_state(0,28,1));
    pilot_gfx_set_state(D3DRS_CULLMODE,D3DCULL_NONE);pilot_gfx_set_state(D3DRS_ZENABLE,1);
    pilot_gfx_set_state(D3DRS_ZWRITEENABLE,1);pilot_gfx_set_state(D3DRS_ZFUNC,2);
    pilot_gfx_set_state(D3DRS_ALPHABLENDENABLE,0);pilot_gfx_set_state(D3DRS_ALPHATESTENABLE,0);pilot_gfx_set_state(D3DRS_SPECULARENABLE,0);
    uint32_t vp[]={4,4,56,56,0x3e800000,0x3f400000};CHECK(pilot_gfx_viewport(vp)==0);
    /* Correct pretransformed Z=.375 passes .4; applying viewport depth a
     * second time gives .4375 and wrongly rejects the draw. */
    CHECK(pilot_gfx_clear(3,0xff000000,.4f,0)==0 && pilot_gfx_fvf_prepare(0x244,.53125f,.53125f)==0);
    CHECK(pilot_gfx_draw_vertices(6,2,4)==0);
    unsigned char pixels[64*64*4];CHECK(pilot_gfx_readback(pixels,sizeof(pixels))==0);
    unsigned sample=(20*64+20)*4;
    /* Perspective-correct native_tex1 gives (1+25/32.375,1+50/32.375),
     * selecting linear texel(1,2). Wrong coord index/RHW/normalization fails. */
    CHECK(pixels[sample]==32 && pixels[sample+1]==64 && pixels[sample+2]==128);
    CHECK(pixels[(2*64+2)*4]==0 && pixels[(50*64+50)*4+2]==128);
    /* Quad-list conversion and nonzero first: perimeter order, two quads.
     * The blue background must remain outside the two rectangles. */
    for(unsigned i=0;i<8;++i)vertices[i]=(struct FvfVertex){0,0,.375f,1,i<4?0xffff0000:0xff00ff00,{0,0},{0,0}};
    const float xy[8][2]={{8,8},{28,8},{28,28},{8,28},{36,36},{56,36},{56,56},{36,56}};
    for(unsigned i=0;i<8;++i){vertices[i].x=xy[i][0];vertices[i].y=xy[i][1];}
    pilot_gfx_vertex_upload(vertex,vertices,sizeof(vertices));CHECK(pilot_gfx_xbox_texture_state(0,14,0));
    CHECK(pilot_gfx_clear(3,0xff0000ff,1,0)==0 && pilot_gfx_fvf_prepare(0x244,.53125f,.53125f)==0);
    CHECK(pilot_gfx_draw_vertices(8,0,8)==0 && pilot_gfx_readback(pixels,sizeof(pixels))==0);
    CHECK(pixels[sample]==255 && !pixels[sample+1] && pixels[(44*64+44)*4+1]==255);
    CHECK(pixels[(32*64+32)*4+2]==255);
    CHECK(pilot_gfx_clear(3,0xff0000ff,1,0)==0 && pilot_gfx_draw_vertices(8,4,4)==0 && pilot_gfx_readback(pixels,sizeof(pixels))==0);
    CHECK(pixels[sample+2]==255 && pixels[(44*64+44)*4+1]==255);
    /* The actual title's 2D font vertices carry RHW zero. The original RCP
     * yields infinity; HLE uses a finite limiting W so native clip coordinates
     * remain representable and equal-RHW 2D quads preserve interpolation. */
    for(unsigned i=0;i<8;++i)vertices[i].rhw=0;
    pilot_gfx_vertex_upload(vertex,vertices,sizeof(vertices));
    CHECK(pilot_gfx_clear(3,0xff0000ff,1,0)==0 && pilot_gfx_draw_vertices(8,0,8)==0 && pilot_gfx_readback(pixels,sizeof(pixels))==0);
    CHECK(pixels[sample]==255 && pixels[(44*64+44)*4+1]==255 && pixels[(32*64+32)*4+2]==255);
    CHECK(pilot_gfx_xbox_texture_state(0,14,2));
    unsigned char packed[3*44]={0};const unsigned codes[]={3,1,2},lanes[]={1,3,4};
    for(unsigned shape=0;shape<3;++shape) {
        unsigned stride=20+lanes[shape]*4+8;
        for(unsigned i=0;i<3;++i) {
            float position[]={i==2?56:8,i==1?56:8,.375f,1},uv[]={2.5f,3.5f};uint32_t color=0xffffffff;
            memcpy(packed+i*stride,position,16);memcpy(packed+i*stride+16,&color,4);
            memset(packed+i*stride+20,0,lanes[shape]*4);memcpy(packed+i*stride+20+lanes[shape]*4,uv,8);
        }
        uint32_t typed;CHECK(pilot_gfx_vertex_create(stride*3,&typed)==0);
        pilot_gfx_vertex_upload(typed,packed,stride*3);pilot_gfx_vertex_bind(0,typed,stride);
        CHECK(pilot_gfx_clear(3,0xff000000,1,0)==0 && pilot_gfx_fvf_prepare(0x244|(codes[shape]<<16),.53125f,.53125f)==0);
        CHECK(pilot_gfx_draw_vertices(5,0,3)==0 && pilot_gfx_readback(pixels,sizeof(pixels))==0);
        CHECK(pixels[sample]==64 && pixels[sample+1]==96 && pixels[sample+2]==128);
        pilot_gfx_vertex_bind(0,0,0);pilot_gfx_vertex_destroy(typed);
    }
    ID3D11RasterizerState *raster=NULL;ID3D11DeviceContext_RSGetState(d3d8_GetD3D11Context(),&raster);CHECK(raster);
    D3D11_RASTERIZER_DESC rd;ID3D11RasterizerState_GetDesc(raster,&rd);ID3D11RasterizerState_Release(raster);
    CHECK(!rd.MultisampleEnable && !rd.AntialiasedLineEnable);
    pilot_gfx_vertex_bind(0,0,0);pilot_gfx_vertex_destroy(vertex);pilot_gfx_texture_bind(0,0);pilot_gfx_texture_destroy(texture);
    vp[0]=vp[1]=0;vp[2]=vp[3]=64;vp[4]=0;vp[5]=0x3f800000;CHECK(pilot_gfx_viewport(vp)==0);
    CHECK(pilot_gfx_xbox_texture_state(0,28,0));
    fprintf(stderr,"PASS: donor native FVF pixels, RHW/viewport/depth, coordinate remap, quad batches/first and real disabled edge AA\n");return 0;
}
