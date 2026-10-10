#ifdef PILOT_D3D_PHOBOS
static int depth_argb_check_image(const uint32_t expected[256],unsigned border)
{
    ID3D11DeviceContext *ctx=d3d8_GetD3D11Context();
    ID3D11ShaderResourceView *view=NULL;ID3D11Resource *resource=NULL;
    ID3D11Texture2D *image=NULL,*staging=NULL;
    ID3D11DeviceContext_PSGetShaderResources(ctx,0,1,&view);CHECK(view);
    ID3D11ShaderResourceView_GetResource(view,&resource);
    CHECK(!ID3D11Resource_QueryInterface(resource,&IID_ID3D11Texture2D,(void**)&image));
    D3D11_TEXTURE2D_DESC desc;ID3D11Texture2D_GetDesc(image,&desc);
    CHECK(desc.Width==16 && desc.Height==16 && desc.Format==DXGI_FORMAT_R8G8B8A8_UNORM);
    desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=desc.MiscFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    CHECK(!ID3D11Device_CreateTexture2D(d3d8_GetD3D11Device(),&desc,NULL,&staging));
    ID3D11DeviceContext_CopyResource(ctx,(ID3D11Resource*)staging,resource);
    D3D11_MAPPED_SUBRESOURCE mapped;CHECK(!ID3D11DeviceContext_Map(ctx,(ID3D11Resource*)staging,0,D3D11_MAP_READ,0,&mapped));
    for(unsigned y=border;y<16-border;++y)for(unsigned x=border;x<16-border;++x) {
        const uint8_t *p=(const uint8_t*)mapped.pData+y*mapped.RowPitch+x*4;
        uint32_t value=expected[y*16+x];
        CHECK(p[0]==((value>>16)&255) && p[1]==((value>>8)&255) &&
              p[2]==(value&255) && p[3]==(value>>24));
    }
    ID3D11DeviceContext_Unmap(ctx,(ID3D11Resource*)staging,0);
    ID3D11Texture2D_Release(staging);ID3D11Texture2D_Release(image);
    ID3D11Resource_Release(resource);ID3D11ShaderResourceView_Release(view);
    return 0;
}
static int depth_argb_draw(float u,float v,uint8_t pixels[64*64*4])
{
    struct {float x,y,z,w;uint32_t color;float u,v;} quad[]={
        {16,16,0,1,0xffffffff,u,v},{16,48,0,1,0xffffffff,u,v},
        {48,16,0,1,0xffffffff,u,v},{48,48,0,1,0xffffffff,u,v}};
    CHECK(!pilot_gfx_draw_memory(6,4,quad,sizeof(quad[0])));
    CHECK(!pilot_gfx_readback(pixels,64*64*4));
    return 0;
}
static int test_depth_argb_sampling(void)
{
    struct PilotDepthTextureStats stats;pilot_gfx_depth_texture_stats(&stats);
    CHECK(!stats.allocations && !stats.conversions && !stats.shader_compilations);
    uint32_t texture;CHECK(!pilot_gfx_texture_create_usage(16,16,1,0x2e,2,&texture));
    uint32_t source[256];
    const uint32_t boundary[]={0,1,2,0xfe,0xff,0x100,0xfffe,0xffff,0x10000,
        0x3fffff,0x400000,0x400001,0x7ffffe,0x7fffff,0x800000,0x800001,0xfffffe,0xffffff};
    for(unsigned i=0;i<256;++i)source[i]=(((i<18?boundary[i]:(i*0x10101u)&0xffffff))<<8)|i;
    source[32]=0x12345620;source[33]=0xabcdef21;
    source[48]=0x00808030;source[49]=0xff010231;
    unsigned seen[256]={0};for(unsigned i=0;i<256;++i)++seen[source[i]&255];
    for(unsigned i=0;i<256;++i)CHECK(seen[i]==1);
    pilot_gfx_texture_upload(texture,0,source,sizeof(source));
    uint32_t viewport[]={0,0,64,64,0,0x3f800000};CHECK(!pilot_gfx_viewport(viewport));
    pilot_gfx_set_state(D3DRS_ZENABLE,0);pilot_gfx_set_state(D3DRS_ALPHABLENDENABLE,0);
    pilot_gfx_set_state(D3DRS_ALPHATESTENABLE,0);pilot_gfx_set_state(D3DRS_FOGENABLE,0);
    pilot_gfx_set_state(D3DRS_CULLMODE,D3DCULL_NONE);pilot_gfx_set_state(D3DRS_COLORWRITEENABLE,15);
    pilot_gfx_set_state(D3DRS_SHADEMODE,2);
    for(unsigned i=0;i<4;++i)pilot_gfx_texture_bind(i,0);
    CHECK(pilot_gfx_xbox_texture_state(0,0,3) && pilot_gfx_xbox_texture_state(0,1,3));
    CHECK(pilot_gfx_xbox_texture_state(0,3,1) && pilot_gfx_xbox_texture_state(0,4,1));
    CHECK(pilot_gfx_xbox_texture_state(0,5,0) && pilot_gfx_xbox_texture_state(0,7,0));
    CHECK(pilot_gfx_xbox_texture_state(0,6,0) && pilot_gfx_xbox_texture_state(0,28,0));
    uint32_t ps[57]={0};ps[8]=8;ps[9]=0x1800;ps[54]=1;
    CHECK(!pilot_gfx_pixel_program(ps,1));CHECK(!pilot_gfx_fvf_prepare(0x144,0,0));
    pilot_gfx_texture_bind(0,texture);
    uint8_t pixels[64*64*4];unsigned center=(32*64+32)*4;
    /* Negative control observes the donor's default depth/shadow interpretation. */
    CHECK(!depth_argb_draw(.5f,.5f,pixels));
    fprintf(stderr,"DEPTH CONTROL RGBA=%u,%u,%u,%u\n",pixels[center],pixels[center+1],pixels[center+2],pixels[center+3]);
    CHECK(pixels[center]==255 && pixels[center+1]==255 && pixels[center+2]==255 && pixels[center+3]==255);
    pilot_gfx_texture_sampling_format(0,0x12);
    pilot_gfx_texture_bind(3,texture);pilot_gfx_texture_sampling_format(3,0x12);
    CHECK(!depth_argb_draw(.5f,.5f,pixels));
    CHECK(pixels[center]==0 && pixels[center+1]==0 && pixels[center+2]==0 && pixels[center+3]==0);
    CHECK(!depth_argb_check_image(source,0));
    pilot_gfx_depth_texture_stats(&stats);CHECK(stats.allocations==1 && stats.conversions==1 && stats.shader_compilations==1);
    for(unsigned i=0;i<16;++i) {
        pilot_gfx_texture_bind(0,texture);pilot_gfx_texture_sampling_format(0,0x12);
        CHECK(!depth_argb_draw(.5f,.5f,pixels));
    }
    pilot_gfx_texture_upload(texture,0,source,sizeof(source)); /* identical CPU upload */
    CHECK(!depth_argb_draw(.5f,.5f,pixels));
    pilot_gfx_depth_texture_stats(&stats);CHECK(stats.allocations==1 && stats.conversions==1);
    /* Four independently chosen neighboring ARGB texels; decode before bilinear.
     * Rational quarter weights predict each channel without renderer output. */
    CHECK(pilot_gfx_xbox_texture_state(0,3,2) && pilot_gfx_xbox_texture_state(0,4,2));
    for(unsigned y=0;y<3;++y)for(unsigned x=0;x<3;++x) {
        float u=.75f+x*.25f,v=2.75f+y*.25f;
        CHECK(!depth_argb_draw(u,v,pixels));
        unsigned wx=x+1,wy=y+1,weights[]={(4-wx)*(4-wy),wx*(4-wy),(4-wx)*wy,wx*wy};
        const unsigned indices[]={32,33,48,49},shifts[]={16,8,0,24};
        for(unsigned c=0;c<4;++c) {
            unsigned sum=0;for(unsigned i=0;i<4;++i)sum+=((source[indices[i]]>>shifts[c])&255)*weights[i];
            for(unsigned py=18;py<46;++py)for(unsigned px=18;px<46;++px)
                CHECK(abs((int)pixels[(py*64+px)*4+c]-(int)((sum+8)/16))<=1);
        }
    }
    /* A CPU write and stencil-only GPU clear each invalidate once; stages share. */
    source[0]=0x2468ace1;pilot_gfx_texture_upload(texture,0,source,sizeof(source));
    CHECK(!depth_argb_draw(.5f,.5f,pixels));CHECK(!depth_argb_check_image(source,0));
    pilot_gfx_depth_texture_stats(&stats);CHECK(stats.allocations==1 && stats.conversions==2);
    CHECK(!pilot_gfx_texture_clear_depth(texture,2,0,0x5a));
    for(unsigned i=0;i<256;++i)source[i]=(source[i]&0xffffff00)|0x5a;
    CHECK(!depth_argb_draw(.5f,.5f,pixels));CHECK(!depth_argb_check_image(source,0));
    pilot_gfx_depth_texture_stats(&stats);CHECK(stats.allocations==1 && stats.conversions==3);
    ID3D11ShaderResourceView *a=NULL,*b=NULL;
    ID3D11DeviceContext_PSGetShaderResources(d3d8_GetD3D11Context(),0,1,&a);
    ID3D11DeviceContext_PSGetShaderResources(d3d8_GetD3D11Context(),3,1,&b);
    CHECK(a && a==b);ID3D11ShaderResourceView_Release(a);ID3D11ShaderResourceView_Release(b);
    /* Write through the normal draw/target path, then sample the new bytes. */
    pilot_gfx_texture_bind(0,0);pilot_gfx_texture_bind(3,0);
    uint32_t color;CHECK(!pilot_gfx_texture_create_usage(16,16,1,0x12,1,&color));
    CHECK(!pilot_gfx_targets(color,texture));viewport[2]=viewport[3]=16;CHECK(!pilot_gfx_viewport(viewport));
    CHECK(!pilot_gfx_clear(7,0,1,0));
    pilot_gfx_set_state(D3DRS_ZENABLE,1);pilot_gfx_set_state(D3DRS_ZWRITEENABLE,1);pilot_gfx_set_state(D3DRS_ZFUNC,8);
    pilot_gfx_set_state(D3DRS_STENCILENABLE,1);pilot_gfx_set_state(D3DRS_STENCILFUNC,8);
    pilot_gfx_set_state(D3DRS_STENCILPASS,3);pilot_gfx_set_state(D3DRS_STENCILREF,0x37);
    pilot_gfx_set_state(D3DRS_STENCILWRITEMASK,255);
    struct {float x,y,z,w;uint32_t color;} quad[]={{0,0,.25f,1,0xffffffff},{0,16,.25f,1,0xffffffff},
        {16,0,.25f,1,0xffffffff},{16,16,.25f,1,0xffffffff}};
    uint32_t solid[57]={0};solid[8]=1;solid[9]=0x1100;solid[43]=0xffffffff;
    CHECK(!pilot_gfx_pixel_program(solid,0) && !pilot_gfx_fvf_prepare(0x44,0,0));
    CHECK(!pilot_gfx_draw_memory(6,4,quad,sizeof(quad[0])));
    CHECK(!pilot_gfx_targets(0,UINT32_MAX));viewport[2]=viewport[3]=64;CHECK(!pilot_gfx_viewport(viewport));
    pilot_gfx_set_state(D3DRS_ZENABLE,0);pilot_gfx_set_state(D3DRS_STENCILENABLE,0);
    CHECK(!pilot_gfx_pixel_program(ps,1) && !pilot_gfx_fvf_prepare(0x144,0,0));
    pilot_gfx_texture_bind(0,texture);pilot_gfx_texture_sampling_format(0,0x12);
    CHECK(!depth_argb_draw(.5f,.5f,pixels));
    for(unsigned i=0;i<256;++i)source[i]=0x40000037;
    CHECK(!depth_argb_check_image(source,1));
    pilot_gfx_depth_texture_stats(&stats);CHECK(stats.allocations==1 && stats.conversions==4 && stats.shader_compilations==1);
    pilot_gfx_texture_bind(0,0);pilot_gfx_texture_destroy(texture);pilot_gfx_texture_destroy(color);
    /* Reused ownership slot gets fresh storage/conversion, cached shader persists. */
    CHECK(!pilot_gfx_texture_create_usage(16,16,1,0x2e,2,&texture));
    pilot_gfx_texture_upload(texture,0,source,sizeof(source));
    pilot_gfx_texture_bind(0,texture);pilot_gfx_texture_sampling_format(0,0x12);
    CHECK(!depth_argb_draw(.5f,.5f,pixels));CHECK(!depth_argb_check_image(source,0));
    pilot_gfx_depth_texture_stats(&stats);CHECK(stats.allocations==2 && stats.conversions==5 && stats.shader_compilations==1);
    pilot_gfx_texture_bind(0,0);pilot_gfx_texture_destroy(texture);pilot_gfx_pixel_fixed();
    fprintf(stderr,"PASS: D24S8 ARGB GPU bytes/all stencil values, depth boundaries, rational bilinear pixels, shared-stage reuse, CPU/clear/draw invalidation and ownership reuse (2 allocations /5 conversions /1 shader)\n");
    return 0;
}
#endif
