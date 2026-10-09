/* Raw inputs, ISA words and algebraic expectations authored independently of
 * both shader implementations; observe actual production GPU constant state. */
static int check_shader_arithmetic_reference(const char *path)
{
    FILE *file=fopen(path,"rb");CHECK(file);uint32_t header[4];
    CHECK(fread(header,sizeof(header),1,file)==1);
    CHECK(header[0]==0x41564c58 && header[1]==1 && header[2] && header[2]<=65536);
    unsigned char positions[4*32]={0},attributes[4*32];
    const float triangle[4][4]={{-200,-200,10,1},{8,8,10,1},{56,8,10,1},{8,56,10,1}};
    for(unsigned i=0;i<4;++i)memcpy(positions+i*32,triangle[i],16);
    uint32_t vertices[2],handle=0,previous[136*4]={0};unsigned previous_count=0,comparisons=0;
    for(unsigned i=0;i<2;++i)CHECK(pilot_gfx_vertex_create(sizeof(positions),&vertices[i])==0);
    pilot_gfx_vertex_upload(vertices[0],positions,sizeof(positions));
    pilot_gfx_vertex_bind(0,vertices[0],32);pilot_gfx_vertex_bind(1,vertices[1],32);
    pilot_gfx_set_state(D3DRS_CULLMODE,D3DCULL_NONE);pilot_gfx_set_state(D3DRS_ZENABLE,0);
    pilot_gfx_set_state(D3DRS_ZWRITEENABLE,0);pilot_gfx_set_state(D3DRS_ALPHABLENDENABLE,0);
    pilot_gfx_set_state(D3DRS_ALPHATESTENABLE,0);pilot_gfx_set_state(D3DRS_SPECULARENABLE,0);
    pilot_gfx_set_texture_state(0,D3DTSS_COLOROP,D3DTOP_SELECTARG1);
    pilot_gfx_set_texture_state(0,D3DTSS_COLORARG1,D3DTA_DIFFUSE);
    pilot_gfx_set_texture_state(0,D3DTSS_ALPHAOP,D3DTOP_SELECTARG1);
    pilot_gfx_set_texture_state(0,D3DTSS_ALPHAARG1,D3DTA_DIFFUSE);
    pilot_gfx_set_texture_state(1,D3DTSS_COLOROP,D3DTOP_DISABLE);pilot_gfx_pixel_fixed();
    for(unsigned sample=0;sample<header[2];++sample) {
        uint32_t metadata[4],code[136*4];float input[4],constants[32][4],expected[8],output[13][4];
        CHECK(fread(metadata,sizeof(metadata),1,file)==1);unsigned count=metadata[0];
        CHECK(count && count<=136 && metadata[1]<=1 && !metadata[2] && !metadata[3]);
        CHECK(fread(code,16,count,file)==count && fread(input,16,1,file)==1 &&
            fread(constants,16,32,file)==32 && fread(expected,sizeof(expected),1,file)==1);
        if(count!=previous_count || memcmp(previous,code,count*16)) {
            if(handle)pilot_gfx_shader_destroy(handle);
            struct PilotVertexElement elements[16]={0};
            elements[0]=(struct PilotVertexElement){0x42,0,0};elements[1]=(struct PilotVertexElement){0x42,1,8};
            CHECK(pilot_gfx_shader_create(code,count,elements,&handle)==0 && pilot_gfx_shader_bind(handle)==0);
            const float scale[]={1.0f/32,-1.0f/32,1.0f/16777215,1},offset[]={-1,1,0,0};
            CHECK(pilot_gfx_shader_raster(scale,offset)==0);
            previous_count=count;memcpy(previous,code,count*16);
        }
        memset(attributes,0xcc,sizeof(attributes));
        for(unsigned i=1;i<4;++i)memcpy(attributes+i*32+8,input,16);
        pilot_gfx_vertex_upload(vertices[1],attributes,sizeof(attributes));
        pilot_gfx_shader_constants(88,32,constants);
        struct PilotVertexProbeStream streams[16]={0};
        streams[0]=(struct PilotVertexProbeStream){positions+32,32};
        streams[1]=(struct PilotVertexProbeStream){attributes+32,32};
        CHECK(pilot_gfx_shader_probe(handle,streams,1,NULL,&output[0][0])==0);
        if(metadata[1]) {
            CHECK(output[0][0]==8 && output[0][1]==8 && output[0][2]==10 && output[0][3]==1);
            for(unsigned lane=0;lane<4;++lane) {
                CHECK(output[9][lane]==(lane<2?input[lane]:lane==3?1:0));
                for(unsigned reg=10;reg<13;++reg)CHECK(output[reg][lane]==(lane==3?1:0));
                comparisons+=4;
            }
        }
        for(unsigned lane=0;lane<8;++lane) {
            float actual=output[3+lane/4][lane%4];
            if(!isfinite(actual) || fabsf(actual-expected[lane])>2e-6f*fmaxf(1,fabsf(expected[lane]))) {
                fprintf(stderr,"ARITHMETIC mismatch sample=%u lane=%u actual=%g expected=%g\n",sample,lane,actual,expected[lane]);CHECK(0);
            }
            ++comparisons;
        }
        unsigned char pixels[64*64*4];
        CHECK(pilot_gfx_clear(D3DCLEAR_TARGET,0xff102030,1,0)==0 && pilot_gfx_draw_vertices(5,1,3)==0);
        CHECK(pilot_gfx_readback(pixels,sizeof(pixels))==0);
        for(unsigned lane=0;lane<4;++lane) {
            int want=(int)lroundf(fminf(1,fmaxf(0,expected[lane]))*255);
            CHECK(abs((int)pixels[(16*64+16)*4+lane]-want)<=1);
        }
        CHECK(pixels[0]==0x10 && pixels[1]==0x20 && pixels[2]==0x30);
    }
    CHECK(fgetc(file)==EOF);fclose(file);pilot_gfx_shader_destroy(handle);
    for(unsigned i=0;i<2;++i){pilot_gfx_vertex_bind(i,0,0);pilot_gfx_vertex_destroy(vertices[i]);}
    fprintf(stderr,"PASS: %u independently specified native arithmetic cases, %u raw shader lanes and actual GPU draws\n",header[2],comparisons);
    return 0;
}
