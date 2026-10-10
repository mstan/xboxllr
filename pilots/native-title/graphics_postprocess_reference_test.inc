/* Observe production native consumers of retained inputs. No expected shader
 * values, expected pixels or captured after-images enter this input protocol. */
static int postprocess_native_save(const char *directory,unsigned draw,unsigned pass,
    unsigned root,const char *phase,const void *data,size_t bytes)
{
    char path[1024];CHECK(snprintf(path,sizeof(path),"%s/native-input-%u-pass-%u-root-%u-%s.bin",
        directory,draw,pass,root,phase)<(int)sizeof(path));
    FILE *file=fopen(path,"wb");CHECK(file && fwrite(data,bytes,1,file)==1 && !fclose(file));return 0;
}
static int postprocess_native_roots(const char *directory,unsigned draw,unsigned pass,
    const uint32_t handles[6],const uint32_t sizes[6],const char *phase)
{
    for(unsigned root=1;root<=5;++root) {
        void *data=malloc(sizes[root]);CHECK(data);
        CHECK(!pilot_gfx_texture_readback(handles[root],0,data,sizes[root]));
        CHECK(!postprocess_native_save(directory,draw,pass,root,phase,data,sizes[root]));free(data);
    }
    return 0;
}
static int observe_postprocess_reference(const char *input,const char *directory)
{
    pilot_gfx_close();
    for(unsigned pass=0;pass<3;++pass) {
        FILE *file=fopen(input,"rb");uint32_t header[4];CHECK(file);
        CHECK(fread(header,sizeof(header),1,file)==1 && header[0]==0x50504c58 &&
            header[1]==1 && header[2]==3 && header[3]==3);
        for(unsigned group=0;group<header[2];++group) {
            uint32_t count;CHECK(fread(&count,4,1,file)==1 && count && count<=3);
            uint32_t pp[17]={640,480,6,1,0x11,1,0,0,1,0x2e,0,0,1};
            CHECK(!pilot_gfx_create(pp));
            uint32_t handles[6]={0},sizes[6]={0};
            for(unsigned root=1;root<=5;++root) {
                uint32_t r[6];CHECK(fread(r,sizeof(r),1,file)==1 && r[0]==root &&
                    r[1] && r[2] && r[1]<=640 && r[2]<=480 && r[3]==r[1]*4 &&
                    (r[4]==0x12 || r[4]==0x2e) && r[5]==r[3]*r[2]);
                sizes[root]=r[5];void *data=malloc(r[5]);CHECK(data && fread(data,r[5],1,file)==1);
                if(root==1) {
                    CHECK(r[1]==640 && r[2]==480 && r[4]==0x12);
                    CHECK(!pilot_gfx_texture_alias(PILOT_GFX_SURFACE,0,0x12,handles+root));
                    /* Supplied sentinel is channel-symmetric. Native default
                     * RGBA storage and owned BGRA roots receive identical bytes. */
                    const unsigned char *p=data;
                    for(unsigned i=0;i<r[5];i+=4)CHECK(p[i]==p[i+2]);
                } else CHECK(!pilot_gfx_texture_create_usage(r[1],r[2],1,r[4],
                    r[4]==0x2e?2:1,handles+root));
                pilot_gfx_texture_upload(handles[root],0,data,r[5]);free(data);
            }
            for(unsigned sample=0;sample<count;++sample) {
                uint32_t h[10],viewport[6],screen[2],render[166],texture_state[128];
                struct PilotVertexElement elements[16];uint32_t code[136*4],metadata[16][2];
                float constants[192][4];
                CHECK(fread(h,sizeof(h),1,file)==1 && h[0] && h[1]>=1 && h[1]<=3 &&
                    (h[2]==6 || h[2]==8) && !h[3] && h[4]==4 && h[6]<=136 &&
                    h[7] && h[7]<=2048 && h[8]>=1 && h[8]<=5 && h[9]>=1 && h[9]<=5);
                CHECK(fread(viewport,sizeof(viewport),1,file)==1 && fread(screen,sizeof(screen),1,file)==1 &&
                    fread(render,sizeof(render),1,file)==1 && fread(texture_state,sizeof(texture_state),1,file)==1 &&
                    fread(elements,sizeof(elements),1,file)==1);
                if(h[6])CHECK(fread(constants,sizeof(constants),1,file)==1 && fread(code,h[6]*16,1,file)==1);
                CHECK(fread(metadata,sizeof(metadata),1,file)==1);
                unsigned char *streams[16]={0};uint32_t vertices[16]={0};
                struct PilotVertexProbeStream probes[16]={0};
                for(unsigned i=0;i<16;++i)if(metadata[i][1]) {
                    CHECK(metadata[i][0] && metadata[i][0]<=2048 && metadata[i][1]<=32*1024*1024 &&
                        metadata[i][1]>=(h[4]-1)*metadata[i][0]+(i?1:pilot_gfx_fvf_extent(0x104)));
                    size_t capacity=metadata[i][1];
                    if(!i && capacity<h[4]*h[7])capacity=h[4]*h[7];
                    streams[i]=malloc(capacity);CHECK(streams[i]);memset(streams[i],0xcc,capacity);
                    CHECK(fread(streams[i],metadata[i][1],1,file)==1);
                    if(h[6]) {
                        CHECK(!pilot_gfx_vertex_create(metadata[i][1],vertices+i));
                        pilot_gfx_vertex_upload(vertices[i],streams[i],metadata[i][1]);
                        pilot_gfx_vertex_bind(i,vertices[i],metadata[i][0]);
                    }
                    probes[i]=(struct PilotVertexProbeStream){streams[i],metadata[i][0]};
                }
                CHECK(streams[0]);
                /* Root1 is the actual default surface; its sampled alias owns
                 * no independent RTV. Bind that surface through its API selector. */
                CHECK(!pilot_gfx_targets(h[8]==1?0:handles[h[8]],handles[h[9]]) && !pilot_gfx_viewport(viewport));
                for(unsigned stage=0;stage<4;++stage) {
                    uint32_t t[2];CHECK(fread(t,sizeof(t),1,file)==1 && t[0]<=5);
                    pilot_gfx_texture_bind(stage,handles[t[0]]);
                    if(t[0])pilot_gfx_texture_sampling_format(stage,t[1]);
                }
                static const unsigned rs[]={57,58,59,60,61,62,63,64,66,67,68,69,70,71,72,73,74,
                    92,93,94,95,96,97,102,103,115,138,139,143,144,145,147,148,150,151,159};
                for(unsigned i=0;i<sizeof(rs)/sizeof(*rs);++i)
                    CHECK(pilot_gfx_xbox_render_state(rs[i],rs[i]==159?0:render[rs[i]]));
                static const unsigned ts[]={0,1,2,3,4,5,6,7,8,12,14,15,16,18,19,22,23,24,25,26,27,28,29};
                for(unsigned stage=0;stage<4;++stage)for(unsigned i=0;i<sizeof(ts)/sizeof(*ts);++i)
                    CHECK(pilot_gfx_xbox_texture_state(stage,ts[i],texture_state[stage*32+ts[i]]));
                CHECK(render[54]==render[136]);
                CHECK(!pilot_gfx_pixel_program(render,render[136]));
                uint32_t shader=0;
                if(h[6]) {
                    CHECK(!pilot_gfx_shader_create(code,h[6],elements,&shader) && !pilot_gfx_shader_bind(shader));
                    pilot_gfx_shader_constants(0,192,constants);
                    float scale[]={2.0f/viewport[2],-2.0f/viewport[3],1.0f/16777215,1},offset[]={-1,1,0,0};
                    CHECK(!pilot_gfx_shader_raster(scale,offset));
                } else {
                    float x,y;memcpy(&x,screen,4);memcpy(&y,screen+1,4);
                    CHECK(!pilot_gfx_fvf_prepare(h[5],x,y));
                }
                CHECK(!postprocess_native_roots(directory,h[0],pass,handles,sizes,"before"));
                if(shader) {
                    float raw[4*52],raster[4*52];
                    CHECK(!pilot_gfx_shader_probe(shader,probes,4,constants,raw));
                    CHECK(!pilot_gfx_shader_raster_probe(shader,probes,4,NULL,raster));
                    CHECK(!postprocess_native_save(directory,h[0],pass,0,"raw",raw,sizeof(raw)));
                    CHECK(!postprocess_native_save(directory,h[0],pass,0,"raster",raster,sizeof(raster)));
                }
                /* UP is the actual captured route, including its independent
                 * stride and any trailing unconsumed bytes in the last vertex. */
                CHECK(!pilot_gfx_draw_memory(h[2],h[4],streams[0],h[7]));
                CHECK(!postprocess_native_roots(directory,h[0],pass,handles,sizes,"after"));
                for(unsigned i=0;i<4;++i)pilot_gfx_texture_bind(i,0);
                for(unsigned i=0;i<16;++i){pilot_gfx_vertex_bind(i,0,0);if(vertices[i])pilot_gfx_vertex_destroy(vertices[i]);free(streams[i]);}
                if(shader)pilot_gfx_shader_destroy(shader);
                fprintf(stderr,"OBSERVED native postprocess input=%u lifetime=%u; independent qualification pending\n",h[0],pass);
            }
            CHECK(!pilot_gfx_targets(0,UINT32_MAX));
            for(unsigned root=1;root<=5;++root)pilot_gfx_texture_destroy(handles[root]);pilot_gfx_close();
        }
        CHECK(fgetc(file)==EOF && !fclose(file));
    }
    return 0;
}
