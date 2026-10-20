/* Actual RHI storage/resolve/readback, authored pixels; no title or audio. */
#ifdef PILOT_D3D_PHOBOS
static int test_guest_presentation(void)
{
    uint32_t p[17]={16,8,7,1,0x2222,1,0,0,0,0,0x20,0,1,0,0,0,0};
    struct PilotVisualMods scaled={1,0,2,0,1,0,0,4,3};
    CHECK(pilot_gfx_visual_mods(&scaled) && pilot_gfx_parameters(p)<0);
    scaled.supersample=1;CHECK(pilot_gfx_visual_mods(&scaled));
    for(unsigned cycle=0;cycle<2;++cycle) {
        CHECK(pilot_gfx_create(p)==0);
        ID3D11DeviceContext *ctx=d3d8_GetD3D11Context();ID3D11DepthStencilView *depth=NULL;
        ID3D11DeviceContext_OMGetRenderTargets(ctx,0,NULL,&depth);CHECK(!depth && !pilot_donor_default_depth());
        CHECK(pilot_gfx_surface_ref(2,1)<0 && pilot_gfx_targets(0,UINT32_MAX)<0);
        CHECK(pilot_gfx_clear(2,0,1,0)<0);
        CHECK(pilot_gfx_multisample(1,UINT32_MAX) && pilot_gfx_multisample(0,UINT32_MAX) && !pilot_gfx_multisample(2,UINT32_MAX) && !pilot_gfx_multisample(1,1));
        uint32_t size[2];CHECK(pilot_gfx_render_size(size) && size[0]==32 && size[1]==16);
        uint32_t viewport[]={0,0,16,8,0,0x3f800000};CHECK(pilot_gfx_viewport(viewport)==0);
        D3D11_VIEWPORT actual;UINT count=1;ID3D11DeviceContext_RSGetViewports(ctx,&count,&actual);
        CHECK(count==1 && actual.Width==32 && actual.Height==16);
        uint32_t target=0;CHECK(pilot_gfx_texture_create_usage(4,4,1,0x12,1,&target)==0);
        CHECK(pilot_gfx_targets(target,0)==0);
        uint32_t offscreen_viewport[]={0,0,4,4,0,0x3f800000};CHECK(pilot_gfx_viewport(offscreen_viewport)==0);
        count=1;ID3D11DeviceContext_RSGetViewports(ctx,&count,&actual);
        CHECK(count==1 && actual.Width==4 && actual.Height==4);
        CHECK(pilot_gfx_targets(0,0)==0 && pilot_gfx_viewport(viewport)==0);
        pilot_gfx_texture_destroy(target);
        uint32_t alias=0;CHECK(pilot_gfx_texture_alias(PILOT_GFX_SURFACE,1,0x12,&alias)==0);
        pilot_gfx_texture_bind(0,alias);
        ID3D11ShaderResourceView *before=NULL;ID3D11DeviceContext_PSGetShaderResources(ctx,0,1,&before);CHECK(before);
        unsigned char source[32*16*4],raw[sizeof(source)],resolved[16*8*4];
        for(unsigned pattern=0;pattern<4;++pattern) {
            for(unsigned y=0;y<16;++y)for(unsigned x=0;x<32;++x)for(unsigned c=0;c<4;++c) {
                unsigned value=pattern==0?73:pattern==1?(x==8 && y==8?255:0):pattern==2?(x<16?0:255):((x*37+y*19+c*53)&255);
                source[(y*32+x)*4+c]=(unsigned char)value;
            }
            ID3D11Resource *image=(ID3D11Resource*)rhi_d3d11_native_image(d3d8_GetSceneImage());
            ID3D11DeviceContext_UpdateSubresource(ctx,image,0,NULL,source,32*4,0);
            CHECK(pilot_gfx_surface_readback(0,raw,sizeof(raw))==0 && !memcmp(raw,source,sizeof(raw)));
            CHECK(pilot_gfx_present()==0);
            ID3D11ShaderResourceView *after=NULL;ID3D11DeviceContext_PSGetShaderResources(ctx,0,1,&after);
            CHECK(after==before);ID3D11ShaderResourceView_Release(after);
            CHECK(pilot_gfx_surface_readback(1,resolved,sizeof(resolved))==0);
            static const unsigned weights[]={1,3,3,1};
            for(unsigned y=0;y<8;++y)for(unsigned x=0;x<16;++x)for(unsigned c=0;c<4;++c) {
                unsigned sum=0;
                for(unsigned j=0;j<4;++j)for(unsigned i=0;i<4;++i) {
                    int sx=(int)(x*2+i)-1,sy=(int)(y*2+j)-1;
                    if(sx<0)sx=0;if(sy<0)sy=0;if(sx>31)sx=31;if(sy>15)sy=15;
                    sum+=source[((unsigned)sy*32+(unsigned)sx)*4+c]*weights[i]*weights[j];
                }
                int expected=(int)((sum+32)/64),observed=resolved[(y*16+x)*4+c];
                CHECK(abs(expected-observed)<=1);
            }
            unsigned char ordinary[sizeof(resolved)];CHECK(pilot_gfx_readback(ordinary,sizeof(ordinary))==0 && !memcmp(ordinary,resolved,sizeof(ordinary)));
        }
        ID3D11ShaderResourceView_Release(before);pilot_gfx_texture_bind(0,0);pilot_gfx_texture_destroy(alias);
        for(unsigned i=0;i<2;++i){CHECK(pilot_gfx_surface_ref(i,1)==1);CHECK(pilot_gfx_surface_ref(i,0)==0);}
        /* Failed reset validates before replacing either storage or depth. */
        uint32_t bad[17];memcpy(bad,p,sizeof(bad));bad[4]=0x1222;CHECK(pilot_gfx_reset(bad)<0);
        CHECK(pilot_gfx_render_size(size) && size[0]==32 && size[1]==16);
        uint32_t legacy[17];memcpy(legacy,p,sizeof(legacy));legacy[4]=0x11;legacy[8]=1;legacy[9]=0x2e;legacy[10]=0;
        CHECK(pilot_gfx_reset(legacy)==0);ID3D11DeviceContext_OMGetRenderTargets(ctx,0,NULL,&depth);CHECK(depth);
        ID3D11DepthStencilView_Release(depth);depth=NULL;CHECK(pilot_gfx_render_size(size) && size[0]==16 && size[1]==8);
        CHECK(pilot_gfx_clear(7,0xff123456,1,0)==0);CHECK(pilot_gfx_present()==0);
        CHECK(pilot_gfx_reset(p)==0);ID3D11DeviceContext_OMGetRenderTargets(ctx,0,NULL,&depth);CHECK(!depth);
        CHECK(pilot_gfx_render_size(size) && size[0]==32 && size[1]==16);pilot_gfx_close();
    }
    fprintf(stderr,"PASS guest Gaussian real raster/resolve pixels/readback, absent depth, legacy depth reset and recreate\n");return 0;
}
#endif
