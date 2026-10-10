/* Actual native draw observations from retained INPUTS. This fixture supplies
 * no expected pixels or shader outputs and cannot itself claim qualification. */
static int scene_native_save(const char *directory,unsigned draw,unsigned pass,const char *name,const void *data,size_t bytes)
{
    char path[1024];CHECK(snprintf(path,sizeof(path),"%s/native-draw-%u-pass-%u-%s",directory,draw,pass,name)<(int)sizeof(path));
    FILE *file=fopen(path,"wb");CHECK(file && fwrite(data,bytes,1,file)==1 && !fclose(file));return 0;
}
static int observe_scene_reference(const char *input,const char *directory)
{
    for(unsigned pass=0;pass<3;++pass) {
        FILE *file=fopen(input,"rb");CHECK(file);uint32_t file_header[4];
        CHECK(fread(file_header,sizeof(file_header),1,file)==1 && file_header[0]==0x534e4c58 && (file_header[1]==1 || file_header[1]==2) &&
            file_header[2] && file_header[2]<=32 && file_header[3]==3);
        uint32_t retained_targets=0;
        if(file_header[1]==2)CHECK(fread(&retained_targets,4,1,file)==1 && retained_targets<=1);
        uint32_t shader=0,previous_code[136*4]={0},previous_count=0;
        struct PilotVertexElement previous_elements[16]={0};
        uint32_t targets[2]={0};unsigned target_width=0,target_height=0;
        for(unsigned sample=0;sample<file_header[2];++sample) {
            uint32_t h[10],viewport[6],render[166],texture_state[128],code[136*4],metadata[16][2];
            struct PilotVertexElement elements[16];float constants[192][4];
            CHECK(fread(h,sizeof(h),1,file)==1 && h[2]<=0xffffff && h[3] && h[3]<=65536 && h[4] && h[4]<=136 &&
                h[5] && h[5]<=4096 && h[6] && h[6]<=4096 && h[7]==87 && (h[8]==44 || h[8]==45));
            CHECK(fread(viewport,sizeof(viewport),1,file)==1 && fread(render,sizeof(render),1,file)==1 &&
                fread(texture_state,sizeof(texture_state),1,file)==1 && fread(elements,sizeof(elements),1,file)==1 &&
                fread(constants,sizeof(constants),1,file)==1 && fread(code,h[4]*16,1,file)==1 && fread(metadata,sizeof(metadata),1,file)==1);
            uint32_t vertex_handles[16]={0};unsigned char *streams[16]={0};
            struct PilotVertexProbeStream probes[16]={0};
            for(unsigned i=0;i<16;++i)if(metadata[i][1]) {
                CHECK(metadata[i][0] && metadata[i][0]<=2048 && metadata[i][1]==h[3]*metadata[i][0]);
                CHECK((uint64_t)(h[2]+h[3])*metadata[i][0]<=32*1024*1024);
                streams[i]=malloc(metadata[i][1]);CHECK(streams[i] && fread(streams[i],metadata[i][1],1,file)==1);
                CHECK(!pilot_gfx_vertex_create((h[2]+h[3])*metadata[i][0],vertex_handles+i));
                /* Place the captured slice at its real first-vertex address. */
                if(h[2]) {
                    size_t size=(h[2]+h[3])*metadata[i][0];unsigned char *buffer=malloc(size);CHECK(buffer);
                    memset(buffer,0xcc,size);memcpy(buffer+h[2]*metadata[i][0],streams[i],metadata[i][1]);
                    pilot_gfx_vertex_upload(vertex_handles[i],buffer,(uint32_t)size);free(buffer);
                } else pilot_gfx_vertex_upload(vertex_handles[i],streams[i],metadata[i][1]);
                pilot_gfx_vertex_bind(i,vertex_handles[i],metadata[i][0]);
                probes[i]=(struct PilotVertexProbeStream){streams[i],metadata[i][0]};
            }
            size_t target_bytes=(size_t)h[5]*h[6]*4;uint32_t *color=malloc(target_bytes),*depth=malloc(target_bytes);
            CHECK(color && depth);
            if(!retained_targets || sample==0)CHECK(fread(color,target_bytes,1,file)==1 && fread(depth,target_bytes,1,file)==1);
            if(retained_targets && sample)CHECK(target_width==h[5] && target_height==h[6]);
            if(!targets[0] || target_width!=h[5] || target_height!=h[6]) {
                CHECK(!pilot_gfx_targets(0,UINT32_MAX));
                for(unsigned i=0;i<2;++i)if(targets[i])pilot_gfx_texture_destroy(targets[i]);
                CHECK(!pilot_gfx_texture_create_usage(h[5],h[6],1,0x12,1,targets));
                CHECK(!pilot_gfx_texture_create_usage(h[5],h[6],1,0x2e,2,targets+1));
                target_width=h[5];target_height=h[6];
            }
            if(!retained_targets || sample==0) {
                pilot_gfx_texture_upload(targets[0],0,color,(uint32_t)target_bytes);
                pilot_gfx_texture_upload(targets[1],0,depth,(uint32_t)target_bytes);
            }
            CHECK(!pilot_gfx_targets(targets[0],targets[1]));
            if(retained_targets) {
                uint32_t before_shape[3];
                CHECK(!pilot_gfx_reference_target_readback(0,color,(uint32_t)target_bytes,before_shape));
                CHECK(!pilot_gfx_reference_target_readback(1,depth,(uint32_t)target_bytes,before_shape));
                for(size_t i=0;i<target_bytes/4;++i)depth[i]=((depth[i]&0xffffff)<<8)|(depth[i]>>24);
                CHECK(!scene_native_save(directory,h[0],pass,"before-color.bin",color,target_bytes));
                CHECK(!scene_native_save(directory,h[0],pass,"before-depth.bin",depth,target_bytes));
            }
            uint32_t textures[4]={0};
            for(unsigned stage=0;stage<4;++stage) {
                uint32_t t[5];CHECK(fread(t,sizeof(t),1,file)==1);
                if(!t[0]){pilot_gfx_texture_bind(stage,0);continue;}
                CHECK(t[1]<=4096 && t[2]<=4096 && t[3] && t[3]<=13);
                CHECK(!pilot_gfx_texture_create(t[1],t[2],t[3],t[4],textures+stage));
                for(unsigned level=0;level<t[3];++level) {
                    uint32_t bytes;CHECK(fread(&bytes,4,1,file)==1 && bytes && bytes<=32*1024*1024);
                    void *data=malloc(bytes);CHECK(data && fread(data,bytes,1,file)==1);
                    pilot_gfx_texture_upload(textures[stage],level,data,bytes);free(data);
                }
                pilot_gfx_texture_bind(stage,textures[stage]);
            }
            static const unsigned rs[]={57,58,59,60,61,62,63,64,66,67,68,69,70,71,72,73,74,
                92,93,94,95,96,97,102,103,115,138,139,143,144,145,147,148,150,151,159};
            for(unsigned i=0;i<sizeof(rs)/sizeof(*rs);++i)CHECK(pilot_gfx_xbox_render_state(rs[i],rs[i]==159?0:render[rs[i]]));
            static const unsigned ts[]={0,1,2,3,4,5,6,7,8,12,14,15,16,18,19,22,23,24,25,26,27,28,29};
            for(unsigned stage=0;stage<4;++stage)for(unsigned i=0;i<sizeof(ts)/sizeof(*ts);++i)
                CHECK(pilot_gfx_xbox_texture_state(stage,ts[i],texture_state[stage*32+ts[i]]));
            pilot_gfx_pixel_fixed();CHECK(!pilot_gfx_viewport(viewport));
            if(!shader || previous_count!=h[4] || memcmp(previous_code,code,h[4]*16) || memcmp(previous_elements,elements,sizeof(elements))) {
                if(shader)pilot_gfx_shader_destroy(shader);
                CHECK(!pilot_gfx_shader_create(code,h[4],elements,&shader));
                memcpy(previous_code,code,h[4]*16);memcpy(previous_elements,elements,sizeof(elements));previous_count=h[4];
            }
            CHECK(!pilot_gfx_shader_bind(shader));pilot_gfx_shader_constants(0,192,constants);
            float scale[]={2.0f/viewport[2],-2.0f/viewport[3],1.0f/16777215,1},offset[]={-1,1,0,0};
            CHECK(!pilot_gfx_shader_raster(scale,offset));
            float *raw=malloc(h[3]*52*4);CHECK(raw && !pilot_gfx_shader_probe(shader,probes,h[3],constants,raw));
            float *raster=malloc(h[3]*52*4);CHECK(raster && !pilot_gfx_shader_raster_probe(shader,probes,h[3],NULL,raster));
            CHECK(!pilot_gfx_draw_vertices(h[1],h[2],h[3]));
            uint32_t shape[3];CHECK(!pilot_gfx_reference_target_readback(0,color,(uint32_t)target_bytes,shape));
            CHECK(shape[0]==h[5] && shape[1]==h[6] && shape[2]==87);
            CHECK(!pilot_gfx_reference_target_readback(1,depth,(uint32_t)target_bytes,shape));
            CHECK(shape[0]==h[5] && shape[1]==h[6] && (shape[2]==44 || shape[2]==45));
            for(size_t i=0;i<target_bytes/4;++i)depth[i]=((depth[i]&0xffffff)<<8)|(depth[i]>>24);
            CHECK(!scene_native_save(directory,h[0],pass,"color.bin",color,target_bytes));
            CHECK(!scene_native_save(directory,h[0],pass,"depth.bin",depth,target_bytes));
            CHECK(!scene_native_save(directory,h[0],pass,"raw.bin",raw,h[3]*52*4));
            CHECK(!scene_native_save(directory,h[0],pass,"raster.bin",raster,h[3]*52*4));
            char report[512];int length=snprintf(report,sizeof(report),"{\"draw\":%u,\"pass\":%u,\"primitive\":%u,\"first\":%u,\"vertices\":%u,\"noise_disabled_fixture\":true,\"native_draw_submitted\":true,\"retained_targets\":%s,\"shader_uploads\":%u}\n",
                h[0],pass,h[1],h[2],h[3],retained_targets?"true":"false",pilot_gfx_shader_uploads());CHECK(length>0 && length<(int)sizeof(report));
            CHECK(!scene_native_save(directory,h[0],pass,"aot-private.json",report,(size_t)length));
            free(color);free(depth);free(raw);free(raster);
            for(unsigned i=0;i<16;++i){pilot_gfx_vertex_bind(i,0,0);if(vertex_handles[i])pilot_gfx_vertex_destroy(vertex_handles[i]);free(streams[i]);}
            for(unsigned i=0;i<4;++i){pilot_gfx_texture_bind(i,0);if(textures[i])pilot_gfx_texture_destroy(textures[i]);}
            fprintf(stderr,"OBSERVED native scene draw=%u lifetime=%u vertices=%u; qualification belongs to independent oracle\n",h[0],pass,h[3]);
        }
        CHECK(fgetc(file)==EOF);fclose(file);CHECK(!pilot_gfx_targets(0,UINT32_MAX));
        for(unsigned i=0;i<2;++i)pilot_gfx_texture_destroy(targets[i]);pilot_gfx_shader_destroy(shader);
        pilot_gfx_close();
        if(pass<2){uint32_t pp[17]={64,64,6,1,0x11,1,0,0,1,0x2a,0,0,1};CHECK(!pilot_gfx_create(pp));}
    }
    return 0;
}
