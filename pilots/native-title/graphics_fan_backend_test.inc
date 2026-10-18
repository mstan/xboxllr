static int test_native_fan(void)
{
    struct FanVertex {float x,y,z,w;uint32_t color;} v[134]={0},list[390];
    uint32_t vb=0;CHECK(!pilot_gfx_vertex_create(sizeof(v),&vb));
    pilot_gfx_set_state(D3DRS_ZENABLE,0);pilot_gfx_set_state(D3DRS_LIGHTING,0);
    pilot_gfx_set_state(D3DRS_ALPHABLENDENABLE,0);pilot_gfx_set_state(D3DRS_ALPHATESTENABLE,0);
    pilot_gfx_set_state(D3DRS_FOGENABLE,0);pilot_gfx_set_state(D3DRS_SPECULARENABLE,0);
    pilot_gfx_set_state(D3DRS_COLORWRITEENABLE,15);pilot_gfx_pixel_fixed();
    for(unsigned s=0;s<4;++s){pilot_gfx_texture_bind(s,0);CHECK(pilot_gfx_xbox_texture_state(s,12,s?1:2));}
    CHECK(pilot_gfx_xbox_texture_state(0,14,0) && pilot_gfx_xbox_texture_state(0,16,2) && pilot_gfx_xbox_texture_state(0,18,0));
    uint32_t viewport[]={0,0,64,64,0,0x3f800000};CHECK(!pilot_gfx_viewport(viewport));
    uint16_t old_indices[4]={0,1,2,3};ID3D11Buffer *old=NULL;
    D3D11_BUFFER_DESC desc={0};desc.ByteWidth=sizeof(old_indices);desc.Usage=D3D11_USAGE_IMMUTABLE;desc.BindFlags=D3D11_BIND_INDEX_BUFFER;
    D3D11_SUBRESOURCE_DATA data={old_indices,0,0};
    CHECK(SUCCEEDED(ID3D11Device_CreateBuffer(d3d8_GetD3D11Device(),&desc,&data,&old)));
    ID3D11DeviceContext *ctx=d3d8_GetD3D11Context();
    ID3D11DeviceContext_IASetIndexBuffer(ctx,old,DXGI_FORMAT_R16_UINT,2);
    uint8_t pixels[64*64*4],reference[64*64*4];
    static const unsigned counts[]={4,132,4}; /* Grow topology, then reuse it. */
    for(unsigned n=0;n<3;++n) {
        unsigned count=counts[n];
        for(unsigned i=0;i<count;++i) {
            double angle=(i-1)*6.283185307179586/(count-2);
            v[2+i]=(struct FanVertex){i?32+24*(float)cos(angle):32,i?32+24*(float)sin(angle):32,.5f,1,
                i%3==0?0xffff0000:i%3==1?0xff00ff00:0xff0000ff};
        }
        if(count==4) {
            v[2]=(struct FanVertex){8,8,.5f,1,0xffff0000};
            v[3]=(struct FanVertex){8,56,.5f,1,0xff00ff00};
            v[4]=(struct FanVertex){56,56,.5f,1,0xff0000ff};
            v[5]=(struct FanVertex){56,8,.5f,1,0xffffff00};
        }
        for(unsigned i=0;i<count-2;++i) {
            list[i*3]=v[3+i];list[i*3+1]=v[4+i];list[i*3+2]=v[2];
        }
        pilot_gfx_vertex_upload(vb,v,sizeof(v));
        for(unsigned flat=0;flat<2;++flat)for(unsigned cull=1;cull<=3;++cull) {
            CHECK(pilot_gfx_xbox_render_state(66,flat?0x1d00:0x1d01));pilot_gfx_set_state(D3DRS_CULLMODE,cull);
            CHECK(!pilot_gfx_fvf_prepare(0x44,0,0));
            pilot_gfx_vertex_bind(0,vb,sizeof(v[0]));
            CHECK(!pilot_gfx_clear(1,0xff000000,1,0));
            CHECK(!pilot_gfx_draw_vertices(7,2,count) && !pilot_gfx_readback(pixels,sizeof(pixels)));
            ID3D11Buffer *restored=NULL;DXGI_FORMAT format;UINT offset;
            ID3D11DeviceContext_IAGetIndexBuffer(ctx,&restored,&format,&offset);
            CHECK(restored==old && format==DXGI_FORMAT_R16_UINT && offset==2);ID3D11Buffer_Release(restored);
            if(count==4 && flat && cull==1) {
                unsigned a=(40*64+20)*4,b=(20*64+40)*4;
                CHECK(!pixels[a] && pixels[a+1]==255 && !pixels[a+2]);
                CHECK(!pixels[b] && !pixels[b+1] && pixels[b+2]==255);
            }
            /* Independently listed reference checks every pixel, shared edges,
             * both winding/cull choices and flat/smooth interpolation. */
            uint32_t reference_vb=0;CHECK(!pilot_gfx_vertex_create((count-2)*3*sizeof(list[0]),&reference_vb));
            pilot_gfx_vertex_upload(reference_vb,list,(count-2)*3*sizeof(list[0]));
            pilot_gfx_vertex_bind(0,reference_vb,sizeof(list[0]));
            CHECK(!pilot_gfx_clear(1,0xff000000,1,0));
            CHECK(!pilot_gfx_draw_vertices(5,0,(count-2)*3) && !pilot_gfx_readback(reference,sizeof(reference)));
            CHECK(!memcmp(pixels,reference,sizeof(pixels)));
            pilot_gfx_vertex_bind(0,vb,sizeof(v[0]));pilot_gfx_vertex_destroy(reference_vb);
#ifdef PILOT_D3D_PHOBOS
            CHECK(!pilot_gfx_clear(1,0xff000000,1,0));
            CHECK(!pilot_gfx_draw_memory(7,count,v+2,sizeof(v[0])) && !pilot_gfx_readback(reference,sizeof(reference)));
            CHECK(!memcmp(pixels,reference,sizeof(pixels)));
#endif
        }
    }
    CHECK(pilot_gfx_draw_vertices(7,0,2)<0);
    CHECK(pilot_gfx_draw_vertices(7,0x80000000u,4)<0);
    CHECK(pilot_gfx_draw_vertices(7,0,UINT32_MAX/12+3)<0);
    ID3D11DeviceContext_IASetIndexBuffer(ctx,NULL,DXGI_FORMAT_UNKNOWN,0);ID3D11Buffer_Release(old);
    pilot_gfx_vertex_bind(0,0,0);pilot_gfx_vertex_destroy(vb);
    CHECK(pilot_gfx_xbox_render_state(66,0x1d01));
    fprintf(stderr,"PASS: fan GPU pixels, rim provoking colors, winding/culling, nonzero first, cached growth/reuse and index-state restoration\n");
    return 0;
}
