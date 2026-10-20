#ifdef PILOT_D3D_PHOBOS
static int test_fvf_unblended_pixels(void)
{
    struct PilotInitializerState before;CHECK(pilot_gfx_initializer_snapshot(&before));
    if(!(before.controls&4))CHECK(pilot_gfx_fvf_prepare_unblended(0x1118)<0);
    CHECK(pilot_gfx_xbox_render_state(137,0));
    CHECK(!pilot_gfx_xbox_render_state(137,1));
    pilot_gfx_set_state(D3DRS_CULLMODE,D3DCULL_NONE);pilot_gfx_set_state(D3DRS_ZENABLE,0);
    pilot_gfx_set_state(D3DRS_LIGHTING,0);pilot_gfx_set_state(D3DRS_ALPHABLENDENABLE,0);
    pilot_gfx_set_state(D3DRS_ALPHATESTENABLE,0);pilot_gfx_set_state(D3DRS_FOGENABLE,0);
    pilot_gfx_set_state(D3DRS_SPECULARENABLE,0);
    float identity[16]={0};for(unsigned i=0;i<4;++i)identity[i*5]=1;
    CHECK(!pilot_gfx_transform(0,identity) && !pilot_gfx_transform(1,identity) && !pilot_gfx_transform(6,identity));
    uint32_t texture=0,texels[]={0xffff0000,0xff00ff00};
    CHECK(!pilot_gfx_texture_create(2,1,1,6,&texture));pilot_gfx_texture_upload(texture,0,texels,sizeof(texels));
    pilot_gfx_texture_bind(0,texture);
    CHECK(pilot_gfx_xbox_texture_state(0,12,2) && pilot_gfx_xbox_texture_state(0,14,2));
    CHECK(pilot_gfx_xbox_texture_state(0,16,2) && pilot_gfx_xbox_texture_state(0,18,2));
    CHECK(pilot_gfx_xbox_texture_state(0,28,0) && pilot_gfx_xbox_texture_state(0,21,0));
    CHECK(pilot_gfx_xbox_texture_state(0,3,1) && pilot_gfx_xbox_texture_state(0,4,1));
    CHECK(pilot_gfx_xbox_texture_state(0,7,0));
    CHECK(pilot_gfx_xbox_texture_state(1,12,1));
    const float positions[3][3]={{-.75f,.75f,.5f},{.75f,.75f,.5f},{0,-.75f,.5f}};
    const float normal[]={0,0,1};
    unsigned char vertices[3*48],pixels[64*64*4];
    for(unsigned n=1;n<=4;++n) {
        unsigned stride=32+4*n;uint32_t vertex=0,fvf=(4+n*2)|0x1110;
        CHECK(!pilot_gfx_vertex_create(stride*3,&vertex));
        pilot_gfx_vertex_bind(0,vertex,stride);
        for(unsigned texel=0;texel<2;++texel)for(unsigned weights=0;weights<2;++weights) {
            memset(vertices,0,sizeof(vertices));
            for(unsigned i=0;i<3;++i) {
                unsigned char *v=vertices+i*stride;memcpy(v,positions[i],12);
                for(unsigned j=0;j<n;++j) {
                    uint32_t weight=weights?(j&1?0x7fc00001u:0x7f800000u):0x3e800000u;
                    memcpy(v+12+j*4,&weight,4);
                }
                memcpy(v+12+4*n,normal,12);
                float uv[]={texel?.75f:.25f,.5f};memcpy(v+24+4*n,uv,8);
            }
            pilot_gfx_vertex_upload(vertex,vertices,stride*3);
            CHECK(!pilot_gfx_fvf_prepare_unblended(fvf));
            uint32_t extents[16];CHECK(pilot_gfx_shader_streams(extents)==1 && extents[0]==stride);
            CHECK(!pilot_gfx_clear(1,0xff0000ff,1,0) && !pilot_gfx_draw_vertices(5,0,3));
            CHECK(!pilot_gfx_readback(pixels,sizeof(pixels)));
            unsigned char *p=pixels+(24*64+32)*4;
            CHECK(p[0]==(texel?0:255) && p[1]==(texel?255:0) && p[2]==0);
        }
        pilot_gfx_vertex_bind(0,0,0);pilot_gfx_vertex_destroy(vertex);
    }
    pilot_gfx_texture_bind(0,0);pilot_gfx_texture_destroy(texture);
    fprintf(stderr,"PASS unblended beta FVF: four strides, original normal/UV offsets, explicit disabled blend, unchanged pixels with finite/infinite/NaN unused weights\n");
    return 0;
}
#endif
