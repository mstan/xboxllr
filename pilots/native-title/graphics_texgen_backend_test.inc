/* Independent constant-coordinate pixel expectations, not a comparison
 * between two callers of the same shader generator. */
#ifdef PILOT_D3D_PHOBOS
static int test_texture_homogeneous_coordinates(void)
{
    uint32_t colors[8],texture=0,shader=0;
    for(unsigned i=0;i<8;i++)colors[i]=0xff000000u|((17+i*23)<<16)|((211-i*19)<<8)|(31+i*13);
    CHECK(!pilot_gfx_texture_create(8,1,1,6,&texture));pilot_gfx_texture_upload(texture,0,colors,sizeof(colors));
    struct {float position[3],uv[4][4];} vertices[3]={0};
    const float positions[3][3]={{-.7f,.6f,.5f},{.7f,.6f,.5f},{0,-.7f,.5f}};
    struct PilotVertexElement elements[16]={0};elements[0]=(struct PilotVertexElement){0x32,0,0};
    for(unsigned set=0;set<4;set++)elements[9+set]=(struct PilotVertexElement){((set+1)<<4)|2,0,12+set*16};
    for(unsigned i=0;i<3;i++) {
        memcpy(vertices[i].position,positions[i],12);
        for(unsigned set=0;set<4;set++){vertices[i].uv[set][0]=.0625f;vertices[i].uv[set][1]=.25f;vertices[i].uv[set][3]=.5f;}
    }
    const float identity[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    CHECK(!pilot_gfx_transform(0,identity) && !pilot_gfx_transform(1,identity) && !pilot_gfx_transform(6,identity));
    CHECK(!pilot_gfx_shader_create_fixed(elements,&shader));
    unsigned char pixels[64*64*4];const unsigned center=(24*64+32)*4;
    for(unsigned stage=0;stage<4;stage++) {
        for(unsigned s=0;s<4;s++) {
            pilot_gfx_texture_bind(s,s==stage?texture:0);
            CHECK(pilot_gfx_xbox_texture_state(s,12,s<=stage?2:1));
            CHECK(pilot_gfx_xbox_texture_state(s,14,s==stage?2:0));
            CHECK(pilot_gfx_xbox_texture_state(s,16,1));
            CHECK(pilot_gfx_xbox_texture_state(s,0,3) && pilot_gfx_xbox_texture_state(s,1,3));
            CHECK(pilot_gfx_xbox_texture_state(s,3,1) && pilot_gfx_xbox_texture_state(s,4,1));
        }
        for(unsigned set=0;set<4;set++)for(unsigned enabled=0;enabled<2;enabled++) {
            float matrix[16];memcpy(matrix,identity,64);
            matrix[(set<3?set+1:3)*4]=set==3?1:.5f;
            CHECK(!pilot_gfx_transform(2+stage,matrix));
            CHECK(pilot_gfx_xbox_texture_state(stage,28,set));
            CHECK(pilot_gfx_xbox_texture_state(stage,21,enabled?2:0));
            CHECK(!pilot_gfx_clear(1,0,1,0) && !pilot_gfx_shader_prepare_fixed(shader));
            CHECK(!pilot_gfx_draw_memory(5,3,vertices,sizeof(vertices[0])) && !pilot_gfx_readback(pixels,sizeof(pixels)));
            uint32_t color=colors[enabled?4:(set==3?1:0)];
            if(pixels[center]!=((color>>16)&255) || pixels[center+1]!=((color>>8)&255) || pixels[center+2]!=(color&255))
                fprintf(stderr,"homogeneous stage=%u dimensions=%u enabled=%u expected=%08x RGB=%02x%02x%02x\n",stage,set+1,enabled,color,pixels[center],pixels[center+1],pixels[center+2]);
            CHECK(pixels[center]==((color>>16)&255) && pixels[center+1]==((color>>8)&255) && pixels[center+2]==(color&255));
        }
        CHECK(!pilot_gfx_transform(2+stage,identity));
    }
    for(unsigned stage=0;stage<4;stage++) {
        pilot_gfx_texture_bind(stage,0);CHECK(pilot_gfx_xbox_texture_state(stage,12,1));
        CHECK(pilot_gfx_xbox_texture_state(stage,21,0));CHECK(pilot_gfx_xbox_texture_state(stage,28,stage));
    }
    pilot_gfx_shader_destroy(shader);pilot_gfx_texture_destroy(texture);
    fprintf(stderr,"PASS original homogeneous texture coordinates: FLOAT1/2/3/4, four stages/source selectors, matrix translations, disabled transforms and cached shader reuse\n");
    return 0;
}
static int test_camera_normal_texgen(void)
{
    CHECK(!test_texture_homogeneous_coordinates());
    const uint32_t texels[4]={0xffff0000,0xff00ff00,0xff0000ff,0xffffff00};
    uint32_t texture=0,shader=0;
    CHECK(!pilot_gfx_texture_create(2,2,1,6,&texture));
    pilot_gfx_texture_upload(texture,0,texels,sizeof(texels));
    struct {float position[3],normal[3],uv[4][2];} vertices[3]={0};
    const float positions[3][3]={{-.7f,.6f,.5f},{.7f,.6f,.5f},{0,-.7f,.5f}};
    struct PilotVertexElement elements[16]={0};
    elements[0]=(struct PilotVertexElement){0x32,0,0};
    elements[2]=(struct PilotVertexElement){0x32,0,12};
    for(unsigned set=0;set<4;++set)elements[9+set]=(struct PilotVertexElement){0x22,0,24+set*8};
    for(unsigned i=0;i<3;++i) {
        memcpy(vertices[i].position,positions[i],12);vertices[i].normal[0]=1;
        for(unsigned set=0;set<4;++set) {
            vertices[i].uv[set][0]=(set&1)?.75f:.25f;
            vertices[i].uv[set][1]=(set&2)?.75f:.25f;
        }
    }
    CHECK(!pilot_gfx_shader_create_fixed(elements,&shader));
    const float identity[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    CHECK(!pilot_gfx_transform(6,identity));
    unsigned char pixels[64*64*4];const unsigned center=(24*64+32)*4;
    for(unsigned stage=0;stage<4;++stage) {
        for(unsigned s=0;s<4;++s) {
            pilot_gfx_texture_bind(s,s==stage?texture:0);
            CHECK(pilot_gfx_xbox_texture_state(s,12,s<=stage?2:1));
            CHECK(pilot_gfx_xbox_texture_state(s,14,s==stage?2:0));
            CHECK(pilot_gfx_xbox_texture_state(s,16,1));
            CHECK(pilot_gfx_xbox_texture_state(s,0,3) && pilot_gfx_xbox_texture_state(s,1,3));
            CHECK(pilot_gfx_xbox_texture_state(s,3,1) && pilot_gfx_xbox_texture_state(s,4,1));
            CHECK(pilot_gfx_xbox_texture_state(s,28,s));
        }
        for(unsigned camera=0;camera<3;++camera)for(unsigned swap=0;swap<2;++swap) {
            float view[16],projection[16],tex[16];
            memcpy(view,identity,64);memcpy(projection,identity,64);memcpy(tex,identity,64);
            /* Camera rotation maps +X normals to +Y; inverse projection keeps
             * the geometry at the same pixels, independently of texgen. */
            if(camera) {
                view[0]=view[5]=0;view[1]=1;view[4]=-1;
                projection[0]=projection[5]=0;projection[1]=-1;projection[4]=1;
            }
            if(swap){tex[0]=tex[5]=0;tex[1]=tex[4]=.8f;tex[12]=tex[13]=.1f;}
            CHECK(!pilot_gfx_transform(0,camera==2?identity:view));
            CHECK(!pilot_gfx_transform(6,camera==2?view:identity) && !pilot_gfx_transform(1,projection));
            CHECK(!pilot_gfx_transform(2+stage,tex));
            for(unsigned enabled=0;enabled<2;++enabled)for(unsigned set=0;set<4;++set)for(unsigned generated=0;generated<2;++generated) {
                CHECK(pilot_gfx_xbox_texture_state(stage,21,enabled?2:0));
                CHECK(d3d8_GetTSS(stage)[24]==(enabled?2u:0));
                CHECK(pilot_gfx_xbox_texture_state(stage,28,(generated?0x10000:0)|set));
                CHECK(d3d8_GetTSS(stage)[D3DTSS_TEXCOORDINDEX]==((generated?0x10000u:0)|set));
                CHECK(!pilot_gfx_clear(1,0xff202020,1,0) && !pilot_gfx_shader_prepare_fixed(shader));
                CHECK(!pilot_gfx_draw_memory(5,3,vertices,sizeof(vertices[0])));
                CHECK(!pilot_gfx_readback(pixels,sizeof(pixels)));
                unsigned texel=generated?(camera?2:1):set;
                if(swap && enabled)texel=((texel&1)<<1)|((texel&2)>>1);
                uint32_t color=texels[texel];
                if(pixels[center]!=((color>>16)&255) || pixels[center+1]!=((color>>8)&255) || pixels[center+2]!=(color&255))
                    fprintf(stderr,"texgen stage=%u camera=%u swap=%u enabled=%u set=%u generated=%u expected=%08x RGB=%02x%02x%02x\n",stage,camera,swap,enabled,set,generated,color,pixels[center],pixels[center+1],pixels[center+2]);
                CHECK(pixels[center]==((color>>16)&255) && pixels[center+1]==((color>>8)&255) && pixels[center+2]==(color&255));
            }
        }
        const uint32_t refused[]={4,0xffff,0x10004,0x20000,0x30000,0x40000,0x80010000};
        uint32_t before=d3d8_GetTSS(stage)[D3DTSS_TEXCOORDINDEX];
        for(unsigned i=0;i<sizeof(refused)/sizeof(refused[0]);++i) {
            CHECK(!pilot_gfx_xbox_texture_state(stage,28,refused[i]));
            CHECK(d3d8_GetTSS(stage)[D3DTSS_TEXCOORDINDEX]==before);
        }
        const uint32_t refused_transforms[]={1,3,4,0x100,0x102,UINT32_MAX};
        for(unsigned i=0;i<sizeof(refused_transforms)/sizeof(refused_transforms[0]);++i) {
            CHECK(!pilot_gfx_xbox_texture_state(stage,21,refused_transforms[i]));
            CHECK(d3d8_GetTSS(stage)[24]==2);
        }
        CHECK(!pilot_gfx_transform(2+stage,identity));
    }
    CHECK(!pilot_gfx_xbox_texture_state(4,28,0x10000));
    for(unsigned stage=0;stage<4;++stage) {
        pilot_gfx_texture_bind(stage,0);CHECK(pilot_gfx_xbox_texture_state(stage,28,stage));
        CHECK(pilot_gfx_xbox_texture_state(stage,12,1));
        CHECK(pilot_gfx_xbox_texture_state(stage,21,0));
    }
    CHECK(!pilot_gfx_transform(0,identity) && !pilot_gfx_transform(1,identity) && !pilot_gfx_transform(6,identity));
    pilot_gfx_shader_destroy(shader);pilot_gfx_texture_destroy(texture);
    fprintf(stderr,"PASS camera-normal texgen: 384 independently expected GPU samples, four stages/selectors, camera/world rotation, disabled/COUNT2 texture matrix, passthrough and rejected modes\n");
    return 0;
}
#endif
