/* Authored images and mathematical expectations, never maintained decoder output. */
static int check_texture_reference(const char *path)
{
    FILE *file=fopen(path,"rb");CHECK(file);uint32_t header[4];
    CHECK(fread(header,sizeof(header),1,file)==1);
    CHECK(header[0]==0x54584c58 && header[1]==1 && header[2] && header[2]<=65536 && header[3]<=2);
    /* Finite variable-coordinate corpus permits two channel steps of native
     * filtering error; the retained affine corpus keeps its one-step bound. */
    const unsigned tolerance=header[3]==2?2:1;
    uint32_t viewport[]={0,0,64,64,0,0x3f800000};CHECK(!pilot_gfx_viewport(viewport));
    pilot_gfx_set_state(D3DRS_ZENABLE,0);pilot_gfx_set_state(D3DRS_ALPHABLENDENABLE,0);
    pilot_gfx_set_state(D3DRS_ALPHATESTENABLE,0);pilot_gfx_set_state(D3DRS_CULLMODE,D3DCULL_NONE);
    pilot_gfx_set_state(D3DRS_FOGENABLE,0);
    for(unsigned i=0;i<4;++i)pilot_gfx_texture_bind(i,0);
    const uint32_t code[]={0,0x0020001b,0x08000000,header[3]==1?0x0000e800:0x0000f800,
        0,0x0020021b,0x08000000,header[3]==1?0x0000c849:0x0000f849};
    struct PilotVertexElement elements[16]={0};
    elements[0]=(struct PilotVertexElement){0x42,0,0};elements[1]=(struct PilotVertexElement){0x42,0,16};
    uint32_t handle,vertex;unsigned char bytes[4*32]={0};
    CHECK(!pilot_gfx_shader_create(code,2,elements,&handle) && !pilot_gfx_shader_bind(handle));
    CHECK(!pilot_gfx_vertex_create(sizeof(bytes),&vertex));pilot_gfx_vertex_bind(0,vertex,32);
    const float xy[3][2]={{8,8},{56,8},{8,56}};
    const float scale[4]={1.0f/32,-1.0f/32,1.0f/16777215,1},offset[4]={-1,1,0,0};
    CHECK(!pilot_gfx_shader_raster(scale,offset));
    uint32_t pixel[57]={0};pixel[0]=0x18200000;pixel[34]=0x08200000;
    pixel[26]=pixel[45]=0xc00;pixel[53]=1;pixel[8]=0xc;pixel[9]=0x1c00;
    CHECK(!pilot_gfx_pixel_program(pixel,1));
    unsigned compared=0,failed_cases=0,maximum_error=0;
    for(unsigned sample=0;sample<header[2];++sample) {
        uint32_t meta[10];float params[4],w[3],q[3];
        CHECK(fread(meta,sizeof(meta),1,file)==1 && meta[0]<=2 && meta[7]<=4096);
        CHECK(fread(params,sizeof(params),1,file)==1);
        CHECK(fread(w,sizeof(w),1,file)==1 && fread(q,sizeof(q),1,file)==1);
        CHECK((meta[8]==8 && meta[9]==4) || (meta[8]==128 && meta[9]==8));
        uint32_t texture;CHECK(!pilot_gfx_texture_create(meta[8],meta[8],meta[9],meta[0]==2?7:meta[0]?0xc:6,&texture));
        for(unsigned level=0;level<meta[9];++level) {
            uint32_t length;unsigned char data[65536];
            CHECK(fread(&length,4,1,file)==1 && length && length<=sizeof(data));
            CHECK(fread(data,length,1,file)==1);pilot_gfx_texture_upload(texture,level,data,length);
        }
        pilot_gfx_texture_bind(0,texture);
        const uint32_t states[]={D3DTSS_MAGFILTER,D3DTSS_MINFILTER,D3DTSS_MIPFILTER,D3DTSS_MAXMIPLEVEL,D3DTSS_ADDRESSU,D3DTSS_ADDRESSV};
        for(unsigned i=0;i<6;++i)pilot_gfx_set_texture_state(0,states[i],meta[i+1]);
        uint32_t bias;memcpy(&bias,params+1,4);pilot_gfx_set_texture_state(0,D3DTSS_MIPMAPLODBIAS,bias);
        pilot_gfx_set_texture_state(0,D3DTSS_MAXANISOTROPY,1);pilot_gfx_set_texture_state(0,D3DTSS_BORDERCOLOR,0x7f123456);
        for(unsigned i=0;i<3;++i) {
            const float position[4]={xy[i][0],xy[i][1],10,header[3]==1?31:w[i]};
            const float texcoord[4]={(params[2]+(xy[i][0]-8)*params[0]/meta[8])*q[i],(params[3]+(xy[i][1]-8)*params[0]/meta[8])*q[i],0,header[3]==1?31:q[i]};
            memcpy(bytes+(i+1)*32,position,16);memcpy(bytes+(i+1)*32+16,texcoord,16);
        }
        pilot_gfx_vertex_upload(vertex,bytes,sizeof(bytes));unsigned char pixels[64*64*4];
        CHECK(!pilot_gfx_clear(D3DCLEAR_TARGET,0xff102030,1,0) && !pilot_gfx_draw_vertices(5,1,3));
        CHECK(!pilot_gfx_readback(pixels,sizeof(pixels)));
        unsigned char repeated[sizeof(pixels)];
        CHECK(!pilot_gfx_clear(D3DCLEAR_TARGET,0xff102030,1,0) && !pilot_gfx_draw_vertices(5,1,3));
        CHECK(!pilot_gfx_readback(repeated,sizeof(repeated)) && !memcmp(pixels,repeated,sizeof(pixels)));
        unsigned case_error=0,error_index=0,error_lane=0,error_actual=0,error_expected=0;
        for(unsigned i=0;i<meta[7];++i) {
            uint32_t index;unsigned char rgba[4];CHECK(fread(&index,4,1,file)==1 && index<4096 && fread(rgba,4,1,file)==1);
            for(unsigned lane=0;lane<4;++lane) {
                const int difference=abs((int)pixels[index*4+lane]-(int)rgba[lane]);
                if((unsigned)difference>case_error) {
                    case_error=(unsigned)difference;error_index=index;error_lane=lane;
                    error_actual=pixels[index*4+lane];error_expected=rgba[lane];
                }
            }
            compared+=2;
        }
        if(case_error>maximum_error)maximum_error=case_error;
        if(case_error>tolerance) {
            ++failed_cases;
            fprintf(stderr,"TEXTURE mismatch sample=%u mip=%u filter=%u maxlevel=%u maximum_channel_error=%u index=%u lane=%u actual=%u expected=%u\n",
                sample,meta[3],meta[2],meta[4],case_error,error_index,error_lane,error_actual,error_expected);
        }
        CHECK(pixels[0]==0x10 && pixels[1]==0x20 && pixels[2]==0x30 && pixels[3]==255);
        pilot_gfx_texture_bind(0,0);pilot_gfx_texture_destroy(texture);
    }
    CHECK(fgetc(file)==EOF);fclose(file);pilot_gfx_shader_destroy(handle);
    pilot_gfx_vertex_bind(0,0,0);pilot_gfx_vertex_destroy(vertex);pilot_gfx_pixel_fixed();
    fprintf(stderr,"TEXTURE comparison: %u cases exceed bound=%u; maximum=%u\n",failed_cases,tolerance,maximum_error);
    CHECK(!failed_cases);
    fprintf(stderr,"PASS: %u independent native texture cases / %u real draws / %u interior pixels\n",header[2],2*header[2],compared);
    return 0;
}
