/* Pixel expectations are primitive color semantics, independent of GS source. */
static int test_flat_pipeline(void)
{
    struct FlatVertex {float x,y,z,w;uint32_t diffuse,specular;float u,v;};
    struct FlatVertex vertices[8]={0};
    const float xy[4][2]={{8,8},{8,56},{56,8},{56,56}};
    const uint32_t colors[]={0xffff0000,0xff00ff00,0xff0000ff,0xffffffff};
    for(unsigned i=0;i<4;++i)vertices[i+2]=(struct FlatVertex){xy[i][0],xy[i][1],.5f,1,
        colors[i],colors[(i+1)%4],(xy[i][0]-8)/48,(xy[i][1]-8)/48};
    uint32_t vb;CHECK(!pilot_gfx_vertex_create(sizeof(vertices),&vb));
    pilot_gfx_vertex_upload(vb,vertices,sizeof(vertices));pilot_gfx_vertex_bind(0,vb,sizeof(vertices[0]));
    uint32_t viewport[]={0,0,64,64,0,0x3f800000};CHECK(!pilot_gfx_viewport(viewport));
    pilot_gfx_set_state(D3DRS_CULLMODE,D3DCULL_NONE);pilot_gfx_set_state(D3DRS_ZENABLE,0);
    pilot_gfx_set_state(D3DRS_ALPHABLENDENABLE,0);pilot_gfx_set_state(D3DRS_ALPHATESTENABLE,0);
    pilot_gfx_set_state(D3DRS_FOGENABLE,0);pilot_gfx_set_state(D3DRS_SPECULARENABLE,0);
    pilot_gfx_set_state(D3DRS_COLORWRITEENABLE,15);pilot_gfx_pixel_fixed();
    for(unsigned i=0;i<4;++i){pilot_gfx_texture_bind(i,0);CHECK(pilot_gfx_xbox_texture_state(i,12,i?1:2));}
    CHECK(pilot_gfx_xbox_texture_state(0,16,2) && pilot_gfx_xbox_texture_state(0,18,0));
    CHECK(pilot_gfx_xbox_texture_state(0,28,0));
    CHECK(pilot_gfx_xbox_render_state(66,0x1d00) && !pilot_gfx_xbox_render_state(66,0xdead));
    uint8_t pixels[64*64*4];unsigned a=(20*64+20)*4,b=(44*64+44)*4;
    for(unsigned specular=0;specular<2;++specular) {
        CHECK(pilot_gfx_xbox_texture_state(0,14,specular?4:0));
        CHECK(!pilot_gfx_fvf_prepare(0x1c4,0,0) && !pilot_gfx_clear(1,0xff000000,1,0));
        CHECK(!pilot_gfx_draw_vertices(6,2,4) && !pilot_gfx_readback(pixels,sizeof(pixels)));
        CHECK(pixels[a+(specular?1:0)]==255 && pixels[b+(specular?2:1)]==255);
        CHECK(!pixels[a+(specular?0:1)] && !pixels[b+(specular?1:2)]);
    }
    CHECK(pilot_gfx_xbox_texture_state(0,14,0));
    CHECK(!pilot_gfx_clear(1,0xff000000,1,0) && !pilot_gfx_draw_vertices(5,2,3));
    CHECK(!pilot_gfx_readback(pixels,sizeof(pixels)) && pixels[a]==255 && !pixels[a+1]);
    CHECK(pilot_gfx_xbox_render_state(66,0x1d01));
    CHECK(!pilot_gfx_draw_vertices(5,2,3) && !pilot_gfx_readback(pixels,sizeof(pixels)));
    CHECK(pixels[a]>90 && pixels[a]<180 && pixels[a+1]>40 && pixels[a+2]>40);
    /* Clipping does not replace the first vertex's flat color. */
    vertices[2].x=-24;pilot_gfx_vertex_upload(vb,vertices,sizeof(vertices));
    CHECK(pilot_gfx_xbox_render_state(66,0x1d00));
    CHECK(!pilot_gfx_draw_vertices(5,2,3) && !pilot_gfx_readback(pixels,sizeof(pixels)));
    CHECK(pixels[a]==255 && !pixels[a+1] && !pixels[a+2]);
    /* Quads use their fourth color for BOTH triangles, including nonzero first. */
    vertices[2].x=8;struct FlatVertex swap=vertices[3];vertices[3]=vertices[4];vertices[4]=vertices[5];vertices[5]=swap;
    pilot_gfx_vertex_upload(vb,vertices,sizeof(vertices));
    CHECK(!pilot_gfx_draw_vertices(8,2,4) && !pilot_gfx_readback(pixels,sizeof(pixels)));
    CHECK(!pixels[a] && pixels[a+1]==255 && !pixels[a+2] && !pixels[b] && pixels[b+1]==255 && !pixels[b+2]);
#ifdef PILOT_D3D_PHOBOS
    /* Real donor register-combiner PS, not only its fixed-function PS. */
    for(unsigned source=4;source<=5;++source) {
        uint32_t states[57]={0};states[8]=source;states[9]=(source|0x10)<<8;
        CHECK(!pilot_gfx_pixel_program(states,0));
        CHECK(!pilot_gfx_draw_vertices(8,2,4) && !pilot_gfx_readback(pixels,sizeof(pixels)));
        CHECK(pixels[a+source-3]==255 && pixels[b+source-3]==255);
        CHECK(!pixels[a] && !pixels[b]);
    }
    pilot_gfx_pixel_fixed();
#endif
    /* Texture coordinates still vary under flat color interpolation. */
    uint32_t texture,texels[32];for(unsigned y=0;y<2;++y)for(unsigned x=0;x<16;++x)texels[y*16+x]=x?0xff0000ff:0xffff0000;
    CHECK(!pilot_gfx_texture_create(2,2,1,0x12,&texture));
    pilot_gfx_texture_prepare_sample(texture,0,texels,sizeof(texels),1);pilot_gfx_texture_bind(0,texture);
    CHECK(pilot_gfx_xbox_texture_state(0,14,2) && pilot_gfx_xbox_texture_state(0,3,1) && pilot_gfx_xbox_texture_state(0,4,1));
    CHECK(pilot_gfx_xbox_texture_state(0,5,0) && pilot_gfx_xbox_texture_state(0,7,0));
    for(unsigned i=2;i<6;++i){vertices[i].u*=2;vertices[i].v*=2;}
    pilot_gfx_vertex_upload(vb,vertices,sizeof(vertices));
    CHECK(!pilot_gfx_draw_vertices(8,2,4) && !pilot_gfx_readback(pixels,sizeof(pixels)));
    CHECK(pixels[a]==255 && !pixels[a+2] && !pixels[b] && pixels[b+2]==255);
    pilot_gfx_texture_bind(0,0);pilot_gfx_texture_destroy(texture);
    pilot_gfx_vertex_bind(0,0,0);pilot_gfx_vertex_destroy(vb);
    CHECK(pilot_gfx_xbox_render_state(66,0x1d01));
    fprintf(stderr,"PASS: flat diffuse/specular, strip leading vertices, triangle/quad, clipping, smooth restoration and interpolated texture pixels\n");return 0;
}
