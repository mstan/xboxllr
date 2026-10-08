static int test_native_draw_pipeline(void)
{
    /* Native NV2A MOV program: position, diffuse, specular, texture 0. The
     * independent decoder tests establish its raw output; these pixels test
     * the subsequent raster interface, not a replacement fixed-function VS. */
    const uint32_t program[]={
        0,0x0020001b,0x08000000,0x0000f800,
        0,0x0020021b,0x08000000,0x0000f818,
        0,0x0020041b,0x08000000,0x0000f820,
        0,0x0020061b,0x08000000,0x0000f849};
    struct PilotVertexElement elements[16]={0};
    elements[0]=(struct PilotVertexElement){0x42,0,0};
    elements[1]=(struct PilotVertexElement){0x42,7,0};
    elements[2]=(struct PilotVertexElement){0x42,7,16};
    elements[3]=(struct PilotVertexElement){0x42,7,32};
    uint32_t shader=0,positions=0,attributes=0,texture=0;
    CHECK(pilot_gfx_shader_create(program,4,elements,&shader)==0);
    float xyz[6][4]={{0},{0},{8,8,4194303.75f,1},{8,56,4194303.75f,2},{56,8,4194303.75f,4},{56,56,4194303.75f,2}};
    struct {float diffuse[4],specular[4],uv[4];} vertex[6]={0};
    const float coordinates[4][4]={{0,0,0,1},{0,8,0,2},{16,0,0,4},{16,8,0,2}};
    for(unsigned i=2;i<6;++i) {
        for(unsigned j=0;j<4;++j)vertex[i].diffuse[j]=1;
        vertex[i].specular[0]=.25f;vertex[i].specular[1]=.5f;vertex[i].specular[2]=.75f;vertex[i].specular[3]=1;
        memcpy(vertex[i].uv,coordinates[i-2],16);
    }
    CHECK(pilot_gfx_vertex_create(sizeof(xyz),&positions)==0 && pilot_gfx_vertex_create(sizeof(vertex),&attributes)==0);
    pilot_gfx_vertex_upload(positions,xyz,sizeof(xyz));pilot_gfx_vertex_upload(attributes,vertex,sizeof(vertex));
    pilot_gfx_vertex_bind(0,positions,16);pilot_gfx_vertex_bind(7,attributes,sizeof(vertex[0]));
    CHECK(pilot_gfx_texture_create(8,8,1,0x12,&texture)==0);
    uint32_t texels[128];for(unsigned y=0;y<8;++y)for(unsigned x=0;x<16;++x)texels[y*16+x]=0xff000080|(x*32<<16)|(y*32<<8);
    pilot_gfx_texture_prepare_sample(texture,0,texels,sizeof(texels),1);pilot_gfx_texture_bind(0,texture);
    for(unsigned i=0;i<4;++i)CHECK(pilot_gfx_xbox_texture_state(i,12,i?1:2));
    CHECK(pilot_gfx_xbox_texture_state(0,14,2) && pilot_gfx_xbox_texture_state(0,16,2) && pilot_gfx_xbox_texture_state(0,18,2));
    CHECK(pilot_gfx_xbox_texture_state(0,3,1) && pilot_gfx_xbox_texture_state(0,4,1));
    CHECK(pilot_gfx_xbox_texture_state(0,5,0) && pilot_gfx_xbox_texture_state(0,7,0));
    pilot_gfx_set_state(D3DRS_SPECULARENABLE,0);pilot_gfx_set_state(D3DRS_ALPHABLENDENABLE,0);
    pilot_gfx_set_state(D3DRS_ZENABLE,1);pilot_gfx_set_state(D3DRS_ZWRITEENABLE,1);pilot_gfx_set_state(D3DRS_ZFUNC,2);
    uint32_t viewport[]={4,4,56,56,0x3e800000,0x3f400000};CHECK(pilot_gfx_viewport(viewport)==0);
    float scale[]={2.0f/56,-2.0f/56,2.0f/16777215,1},offset[]={-1-8.0f/56,1+8.0f/56,-.5f,0};
    CHECK(pilot_gfx_shader_bind(shader)==0 && pilot_gfx_shader_raster(scale,offset)==0);
    uint32_t extents[16];CHECK(pilot_gfx_shader_streams(extents)==0x81 && extents[0]==16 && extents[7]==48);
    CHECK(pilot_gfx_clear(3,0xff000000,1,0)==0 && pilot_gfx_draw_vertices(6,2,4)==0);
    unsigned char pixels[64*64*4];CHECK(pilot_gfx_readback(pixels,sizeof(pixels))==0);
    unsigned sample=(20*64+20)*4;
    /* LLE perspective weights at pixel center (20.5,20.5): lambda=(23,12.5,12.5)/48.
     * Projected UV = sum(lambda*uv/w)/sum(lambda*q/w) -> (25/24,25/24)
     * in this 8x8 linear texture, selecting texel (1,1). Projecting before
     * interpolation, dropping Q, wrong strip/count, or ignoring first fails. */
    CHECK(pixels[sample]==32 && pixels[sample+1]==32 && pixels[sample+2]==128);
    CHECK(!pixels[(2*64+2)*4] && pixels[(50*64+50)*4+2]==128);
    /* Fixed-function selection replaces VS/layout and b0/b1. Rebinding the
     * same native program must restore them even when constants are unchanged. */
    CHECK(pilot_gfx_fvf_prepare(0x44,.53125f,.53125f)==0);
    CHECK(pilot_gfx_shader_bind(shader)==0 && pilot_gfx_shader_raster(scale,offset)==0);
    /* A farther, differently colored submission must fail the actual depth test. */
    for(unsigned i=2;i<6;++i)xyz[i][2]=12582911.25f;
    pilot_gfx_vertex_upload_range(positions,xyz,2*sizeof(xyz[0]),4*sizeof(xyz[0]));CHECK(pilot_gfx_xbox_texture_state(0,14,4));
    CHECK(pilot_gfx_draw_vertices(6,2,4)==0 && pilot_gfx_readback(pixels,sizeof(pixels))==0);
    CHECK(pixels[sample]==32 && pixels[sample+1]==32 && pixels[sample+2]==128);
    pilot_gfx_set_state(D3DRS_ZENABLE,0);
    CHECK(pilot_gfx_draw_vertices(6,2,4)==0 && pilot_gfx_readback(pixels,sizeof(pixels))==0);
    CHECK(abs((int)pixels[sample]-64)<=1 && abs((int)pixels[sample+1]-128)<=1 && abs((int)pixels[sample+2]-191)<=1);
    /* Flat colors must survive the programmable VS -> borrowed PS linkage,
     * including the second strip primitive with reversed vertex winding. */
    for(unsigned i=2;i<6;++i) {
        memset(vertex[i].specular,0,16);vertex[i].specular[(i-2)%3]=1;vertex[i].specular[3]=1;
    }
    pilot_gfx_vertex_upload_range(attributes,vertex,2*sizeof(vertex[0]),4*sizeof(vertex[0]));
    CHECK(pilot_gfx_xbox_render_state(66,0x1d00));
    CHECK(pilot_gfx_draw_vertices(6,2,4)==0 && pilot_gfx_readback(pixels,sizeof(pixels))==0);
    CHECK(pixels[sample]==255 && !pixels[sample+1] && !pixels[sample+2]);
    CHECK(!pixels[(44*64+44)*4] && pixels[(44*64+44)*4+1]==255 && !pixels[(44*64+44)*4+2]);
    CHECK(pilot_gfx_xbox_render_state(66,0x1d01));
    pilot_gfx_texture_bind(0,0);pilot_gfx_texture_destroy(texture);
    pilot_gfx_vertex_bind(0,0,0);pilot_gfx_vertex_bind(7,0,0);pilot_gfx_vertex_destroy(positions);pilot_gfx_vertex_destroy(attributes);
    pilot_gfx_shader_destroy(shader);viewport[0]=viewport[1]=0;viewport[2]=viewport[3]=64;viewport[4]=0;viewport[5]=0x3f800000;
    CHECK(pilot_gfx_viewport(viewport)==0);
    fprintf(stderr,"PASS: native NV2A raster pixels, strip/start/multistream, perspective W/Q, linear texture coordinates, viewport/depth and specular argument\n");return 0;
}
