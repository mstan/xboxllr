/* The private corpus contains authored ISA, original inputs and mathematical
 * expectations. Neither maintained shader interpreter produces expectations. */
static int check_fog_reference(const char *path)
{
    FILE *file=fopen(path,"rb");CHECK(file);uint32_t header[4];
    CHECK(fread(header,sizeof(header),1,file)==1);
    CHECK(header[0]==0x46474c58 && header[1]==1 && header[2] && header[2]<=65536 && !header[3]);
    uint32_t viewport[]={0,0,64,64,0,0x3f800000};CHECK(!pilot_gfx_viewport(viewport));
    pilot_gfx_set_state(D3DRS_ZENABLE,0);pilot_gfx_set_state(D3DRS_ALPHABLENDENABLE,0);
    pilot_gfx_set_state(D3DRS_ALPHATESTENABLE,0);pilot_gfx_set_state(D3DRS_CULLMODE,D3DCULL_NONE);
    pilot_gfx_set_state(D3DRS_FOGCOLOR,0xa7123456);pilot_gfx_set_state(D3DRS_SHADEMODE,2);
    for(unsigned i=0;i<4;++i)pilot_gfx_texture_bind(i,0);
    unsigned char bytes[4*32]={0};uint32_t vertex,handle=0,previous[12]={0};
    CHECK(!pilot_gfx_vertex_create(sizeof(bytes),&vertex));pilot_gfx_vertex_bind(0,vertex,32);
    const float xy[3][2]={{8,8},{56,8},{8,56}};
    const float diffuse[4]={.8f,.2f,.1f,.6f};
    const float scale[4]={1.0f/32,-1.0f/32,1.0f/16777215,1},offset[4]={-1,1,0,0};
    unsigned lanes=0,draws=0,compared_pixels=0;
    for(unsigned sample=0;sample<header[2];++sample) {
        uint32_t meta[4],code[12];float params[3],input[3][4],w[3],raw[3],expected[4],output[3][13][4];
        CHECK(fread(meta,sizeof(meta),1,file)==1 && meta[0]==3 && meta[1]<=1 && meta[2]>=1 && meta[2]<=3 && meta[3]<=1);
        CHECK(fread(code,sizeof(code),1,file)==1 && fread(params,sizeof(params),1,file)==1 &&
              fread(input,sizeof(input),1,file)==1 && fread(w,sizeof(w),1,file)==1 &&
              fread(raw,sizeof(raw),1,file)==1 && fread(expected,sizeof(expected),1,file)==1);
        if(!handle || memcmp(previous,code,sizeof(code))) {
            if(handle)pilot_gfx_shader_destroy(handle);
            struct PilotVertexElement elements[16]={0};
            elements[0]=(struct PilotVertexElement){0x42,0,0};elements[1]=(struct PilotVertexElement){0x42,0,16};
            CHECK(!pilot_gfx_shader_create(code,3,elements,&handle) && !pilot_gfx_shader_bind(handle));
            memcpy(previous,code,sizeof(code));
        }
        memset(bytes,0xcc,sizeof(bytes));
        for(unsigned i=0;i<3;++i) {
            const float position[4]={xy[i][0],xy[i][1],10,w[i]};
            memcpy(bytes+(i+1)*32,position,16);memcpy(bytes+(i+1)*32+16,input[i],16);
        }
        pilot_gfx_vertex_upload(vertex,bytes,sizeof(bytes));pilot_gfx_shader_constants(96,1,diffuse);
        pilot_gfx_set_state(D3DRS_FOGENABLE,meta[3]);pilot_gfx_set_state(D3DRS_FOGTABLEMODE,meta[2]);
        for(unsigned i=0;i<3;++i){uint32_t word;memcpy(&word,params+i,4);pilot_gfx_set_state(D3DRS_FOGSTART+i,word);}
        CHECK(!pilot_gfx_shader_raster(scale,offset));
        struct PilotVertexProbeStream streams[16]={0};streams[0]=(struct PilotVertexProbeStream){bytes+32,32};
        CHECK(!pilot_gfx_shader_probe(handle,streams,3,NULL,&output[0][0][0]));
        for(unsigned i=0;i<3;++i){CHECK(isfinite(output[i][5][0]) && fabsf(output[i][5][0]-raw[i])<1e-6f);++lanes;}
        uint32_t pixel[57]={0};
        pixel[0]=meta[1]?0x14200000:0x13200000;pixel[34]=meta[1]?0x04200000:0x03200000;
        pixel[26]=pixel[45]=0xc00;pixel[53]=1;pixel[8]=meta[1]?0x130c0300:0xc;pixel[9]=0x1c00;
        CHECK(!pilot_gfx_pixel_program(pixel,0));
        for(unsigned route=0;route<(meta[1]?2u:1u);++route) {
            if(route) {
                pilot_gfx_pixel_fixed();
                pilot_gfx_set_texture_state(0,D3DTSS_COLOROP,D3DTOP_SELECTARG1);
                pilot_gfx_set_texture_state(0,D3DTSS_COLORARG1,D3DTA_DIFFUSE);
                pilot_gfx_set_texture_state(0,D3DTSS_ALPHAOP,D3DTOP_SELECTARG1);
                pilot_gfx_set_texture_state(0,D3DTSS_ALPHAARG1,D3DTA_DIFFUSE);
                pilot_gfx_set_texture_state(1,D3DTSS_COLOROP,D3DTOP_DISABLE);
            }
            unsigned char pixels[64*64*4];
            CHECK(!pilot_gfx_clear(D3DCLEAR_TARGET,0xff102030,1,0) && !pilot_gfx_draw_vertices(5,1,3));
            CHECK(!pilot_gfx_readback(pixels,sizeof(pixels)));
            for(unsigned i=0;i<4;++i) {
                const int want=(int)lroundf(expected[i]*255),actual=pixels[(16*64+16)*4+i];
                if(abs(actual-want)>1)fprintf(stderr,"FOG mismatch sample=%u route=%u lane=%u actual=%d expected=%d\n",sample,route,i,actual,want);
                CHECK(abs(actual-want)<=1);
            }
            CHECK(pixels[0]==0x10 && pixels[1]==0x20 && pixels[2]==0x30 && pixels[3]==255);++draws;
            double factors[3];
            for(unsigned i=0;i<3;++i)factors[i]=!meta[3]?1:(meta[2]==3?(3.0-raw[i])/2:
                exp(meta[2]==1?-(double)params[2]*raw[i]:-pow((double)params[2]*raw[i],2)));
            const double fog_rgb[]={18.0/255,52.0/255,86.0/255};
            for(unsigned y=8;y<56;++y)for(unsigned x=8;x<56;++x) {
                if(x+y+1>=64)continue;
                const double b=(x+.5-8)/48,c=(y+.5-8)/48,a=1-b-c;
                const double q0=a/w[0],q1=b/w[1],q2=c/w[2];
                const double f=fmin(1,fmax(0,(q0*factors[0]+q1*factors[1]+q2*factors[2])/(q0+q1+q2)));
                for(unsigned i=0;i<4;++i) {
                    const double value=i==3?(meta[1]?.6:f):(meta[1]?f*diffuse[i]+(1-f)*fog_rgb[i]:fog_rgb[i]);
                    CHECK(abs((int)pixels[(y*64+x)*4+i]-(int)lround(value*255))<=1);
                }
                ++compared_pixels;
            }
        }
    }
    CHECK(fgetc(file)==EOF);fclose(file);pilot_gfx_shader_destroy(handle);
    pilot_gfx_vertex_bind(0,0,0);pilot_gfx_vertex_destroy(vertex);pilot_gfx_pixel_fixed();
    fprintf(stderr,"PASS: %u independent native fog cases / %u raw scalar lanes / %u real draws / %u interior pixels\n",header[2],lanes,draws,compared_pixels);
    return 0;
}
