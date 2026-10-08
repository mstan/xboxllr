static int check_draw_capture(const char *path)
{
    FILE *file=fopen(path,"rb");CHECK(file);
    uint32_t header[8],viewport[6],render[166],texture[128],code[136*4],metadata[16][2];
    struct PilotVertexElement elements[16];float constants[192][4];unsigned char *streams[16]={0};
    CHECK(fread(header,sizeof(header),1,file)==1 && header[0]==0x44534c58 && header[1]==1 && header[4] && header[4]<=65536 && header[5] && header[5]<=136);
    CHECK(fread(viewport,sizeof(viewport),1,file)==1 && fread(render,sizeof(render),1,file)==1 && fread(texture,sizeof(texture),1,file)==1);
    CHECK(fread(elements,sizeof(elements),1,file)==1 && fread(constants,sizeof(constants),1,file)==1 && fread(code,header[5]*16,1,file)==1);
    CHECK(fread(metadata,sizeof(metadata),1,file)==1);
    for(unsigned i=0;i<16;++i)if(metadata[i][1]) {
        CHECK(metadata[i][0] && metadata[i][0]<=2048 && metadata[i][1]==header[4]*metadata[i][0]);
        streams[i]=malloc(metadata[i][1]);CHECK(streams[i] && fread(streams[i],metadata[i][1],1,file)==1);
    }
    unsigned outputs=header[4]*13*4;float *captured=malloc(outputs*4);CHECK(captured && fread(captured,outputs*4,1,file)==1 && fgetc(file)==EOF);fclose(file);
    uint32_t replay_shader=0;float *replay=malloc(outputs*4);CHECK(replay);
    struct PilotVertexProbeStream replay_streams[16]={0};
    for(unsigned i=0;i<16;++i)replay_streams[i]=(struct PilotVertexProbeStream){streams[i],metadata[i][0]};
    CHECK(pilot_gfx_shader_create(code,header[5],elements,&replay_shader)==0);
    CHECK(pilot_gfx_shader_probe(replay_shader,replay_streams,header[4],constants,replay)==0);
    double maximum=0;unsigned comparisons=0;
    for(unsigned vertex=0;vertex<header[4];++vertex) {
        Nv2aVshCPUFullExecutionState storage={0};Nv2aVshExecutionState state=nv2a_vsh_emu_initialize_full_execution_state(&storage);
        memcpy(storage.context_regs,constants,sizeof(constants));
        for(unsigned reg=0;reg<16;++reg) {
            struct PilotVertexElement element=elements[reg];if(element.type==2)continue;
            CHECK(element.stream<16 && streams[element.stream]);
            CHECK(element.offset+pilot_gfx_vertex_type_size(element.type)<=metadata[element.stream][0]);
            const unsigned char *data=streams[element.stream]+vertex*metadata[element.stream][0]+element.offset;
            float *value=storage.input_regs+reg*4;value[3]=1;
            if(element.type==0x40) {value[0]=data[2]/255.0f;value[1]=data[1]/255.0f;value[2]=data[0]/255.0f;value[3]=data[3]/255.0f;}
            else if(element.type==0x16) {
                uint32_t bits;memcpy(&bits,data,4);
                value[0]=fmaxf(-1,(float)((int32_t)(bits<<21)>>21)/1023);
                value[1]=fmaxf(-1,(float)((int32_t)(bits<<10)>>21)/1023);
                value[2]=fmaxf(-1,(float)((int32_t)bits>>22)/511);
            } else if((element.type&15)==5 || (element.type&15)==1) {
                for(unsigned lane=0;lane<element.type>>4;++lane) {int16_t raw;memcpy(&raw,data+lane*2,2);value[lane]=(element.type&15)==1?fmaxf(-1,raw/32767.0f):raw;}
            } else if(element.type==0x72) {memcpy(value,data,8);memcpy(value+3,data+8,4);}
            else {CHECK((element.type&15)==2 && element.type>>4<=4);memcpy(value,data,(element.type>>4)*4);}
        }
        for(unsigned i=0;i<header[5];++i) {Nv2aVshStep step={0};CHECK(nv2a_vsh_parse_step(&step,code+i*4)==NV2AVPR_SUCCESS);nv2a_vsh_emu_apply(&state,&step);}
        const unsigned widths[13]={4,0,0,4,4,1,1,4,4,4,4,4,4};
        for(unsigned reg=0;reg<13;++reg)for(unsigned lane=0;lane<widths[reg];++lane) {
            float reference=storage.output_regs[reg*4+lane],native=captured[(vertex*13+reg)*4+lane];
            float repeated=replay[(vertex*13+reg)*4+lane];
            CHECK(isfinite(reference) && isfinite(native) && isfinite(repeated));
            CHECK(fabs((double)native-repeated)/fmax(1,fabs(native))<=2e-4);
            double error=fabs((double)reference-native)/fmax(1,fabs(reference));
            if(error>2e-4)fprintf(stderr,"MISMATCH %s vertex=%u output=%u lane=%u reference=%.9g native=%.9g replay=%.9g scaled=%.9g\n",
                path,vertex,reg,lane,reference,native,replay[(vertex*13+reg)*4+lane],error);
            CHECK(error<=2e-4);if(error>maximum)maximum=error;++comparisons;
        }
    }
    pilot_gfx_shader_destroy(replay_shader);free(replay);
    for(unsigned i=0;i<16;++i)free(streams[i]);free(captured);
    fprintf(stderr,"PASS actual draw %s: %u vertices, %u reference outputs, maximum scaled error %.9g\n",path,header[4],comparisons,maximum);return 0;
}
