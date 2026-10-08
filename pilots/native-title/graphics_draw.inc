/* Optional private draw inputs/output capture. It never substitutes for a
 * submitted draw and is disabled in ordinary production execution. */
static void graphics_draw_capture(const uint32_t args[3])
{
    const char *capture=graphics_diagnostic(GRAPHICS_DIAGNOSTIC_DRAW);if(!capture || !*capture)return;
    static unsigned sequence;
    const char *trigger=getenv("XLLE_PILOT_DRAW_CAPTURE_TRIGGER");
    if(trigger && *trigger && GetFileAttributesA(trigger)==INVALID_FILE_ATTRIBUTES)return;
    const char *requested=getenv("XLLE_PILOT_DRAW_CAPTURE_LIMIT");
    unsigned limit=requested?(unsigned)strtoul(requested,NULL,10):32;
    if(!limit || limit>256)stop("draw capture limit outside range",limit);
    if(sequence>=limit)return;
    if(!args[2] || args[2]>65536)stop("draw capture count outside range",args[2]);
    struct GraphicsShader *shader=graphics_shader(MEM32(PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_SNAPSHOT_VERTEX_SHADER));
    struct PilotVertexElement elements[16];struct PilotVertexProbeStream streams[16]={0};
    uint32_t metadata[16][2]={0},constants[192][4];
    for(unsigned i=0;i<16;++i) {
        uint32_t descriptor=shader->header+0x14+i*16;
        elements[i]=(struct PilotVertexElement){MEM32(descriptor+8),MEM32(descriptor),MEM32(descriptor+4)};
        uint32_t cache=PILOT_D3D_STREAMS+i*12,header=MEM32(cache+8),stride=MEM32(cache);
        if(header) {
            struct GraphicsVertexObject *vertex=graphics_vertex(header);
            if(((uint64_t)args[1]+args[2])*stride>vertex->bytes)stop("draw capture vertex range",i);
            streams[i]=(struct PilotVertexProbeStream){memory+vertex->data+args[1]*stride,stride};
            metadata[i][0]=stride;metadata[i][1]=args[2]*stride;
        }
    }
    float *outputs=malloc(args[2]*13*16);if(!outputs)stop("draw capture allocation failed",0);
    if(pilot_gfx_shader_probe(shader->native,streams,args[2],NULL,outputs)<0)stop("draw capture GPU probe failed",0);
    pilot_gfx_shader_capture_constants(constants);
    uint32_t viewport[6];if(pilot_gfx_get_viewport(viewport)<0)stop("draw capture viewport failed",0);
    char path[1024];
    if(snprintf(path,sizeof(path),"%s/draw-%u.bin",capture,++sequence)>=(int)sizeof(path))stop("draw capture path",0);
    FILE *file=fopen(path,"wb");if(!file)stop("draw capture open",0);
    uint32_t header[]={0x44534c58,1,args[0],args[1],args[2],MEM32(shader->header+8),shader->native,0};
#define DRAW_CAPTURE(data,bytes) do{if(fwrite(data,bytes,1,file)!=1)stop("draw capture write",0);}while(0)
    DRAW_CAPTURE(header,sizeof(header));DRAW_CAPTURE(viewport,sizeof(viewport));
    DRAW_CAPTURE(memory+PILOT_D3D_RENDER_STATES,166*4);DRAW_CAPTURE(memory+PILOT_D3D_TEXTURE_STATES,4*128);
    DRAW_CAPTURE(elements,sizeof(elements));DRAW_CAPTURE(constants,sizeof(constants));
    DRAW_CAPTURE(shader->program,header[5]*16);DRAW_CAPTURE(metadata,sizeof(metadata));
    for(unsigned i=0;i<16;++i)if(metadata[i][1])DRAW_CAPTURE(streams[i].data,metadata[i][1]);
    DRAW_CAPTURE(outputs,args[2]*13*16);
#undef DRAW_CAPTURE
    free(outputs);if(fclose(file))stop("draw capture close",0);
    fprintf(stderr,"[DRAW CAPTURE] sequence=%u shader=%u primitive=%u first=%u count=%u target=%08x depth=%.9g\n",
        sequence,shader->native,args[0],args[1],args[2],graphics_target,graphics_float(TRANSFORM_FIELD(DEPTH_SCALE)));
}
static void graphics_draw_common(const uint32_t args[3],uint32_t up_data,uint32_t up_stride)
{
    uint64_t profile=graphics_service_profile_enabled() && !dispatch_in_dpc?dispatch_monotonic():0;
    if(profile)++graphics_service_profile.draw_calls;
    unsigned primitive=args[0],first=args[1],count=args[2];
    if(!count || (uint64_t)first+count>UINT32_MAX)stop("invalid draw range",count);
    if((primitive!=5 && primitive!=6 && primitive!=8) || count<3 ||
        (primitive==5 && count%3) || (primitive==8 && count%4))stop("unqualified primitive/count",primitive);
    if(MEM32(PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_SNAPSHOT_PIXEL_SHADER) &&
        pilot_gfx_pixel_program((const uint32_t*)(memory+PILOT_D3D_RENDER_STATES),
            MEM32(PILOT_D3D_RENDER_STATES+136*4))<0)stop("unsupported native pixel draw",0);
    graphics_target_prepare(graphics_target);graphics_target_prepare(graphics_depth);
    graphics_draw_profile_step(&profile,0);
    uint32_t shader=MEM32(PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_SNAPSHOT_VERTEX_SHADER);
    uint32_t extents[16]={0};unsigned streams;
    if(shader&1)streams=pilot_gfx_shader_streams(extents);
    else {extents[0]=pilot_gfx_fvf_extent(shader);streams=1;}
    if(up_data) {
        if(streams!=1 || !up_stride || up_stride<extents[0] || (uint64_t)count*up_stride>UINT32_MAX)
            stop("invalid user-pointer draw stream",up_stride);
        graphics_pointer(up_data,count*up_stride);
    }
    const char *feedback_capture=graphics_diagnostic(GRAPHICS_DIAGNOSTIC_FEEDBACK);
    if(pilot_gfx_feedback_mask() && feedback_capture && *feedback_capture && up_data) {
        char path[1024];static unsigned sequence;
        if(snprintf(path,sizeof(path),"%s/feedback-%u.bin",feedback_capture,++sequence)>=(int)sizeof(path))stop("feedback capture path",0);
        FILE *file=fopen(path,"wb");if(!file)stop("feedback capture open",0);
        uint32_t header[]={0x42464c58,1,primitive,first,count,shader,up_stride,pilot_gfx_feedback_mask(),
            MEM32(TRANSFORM_FIELD(SCREEN_OFFSET)),MEM32(TRANSFORM_FIELD(SCREEN_OFFSET)+4)};
        if(fwrite(header,sizeof(header),1,file)!=1 || fwrite(memory+PILOT_D3D_RENDER_STATES,166*4,1,file)!=1 ||
            fwrite(memory+PILOT_D3D_TEXTURE_STATES,128*4,1,file)!=1 || fwrite(memory+up_data,count*up_stride,1,file)!=1 || fclose(file))
            stop("feedback capture write",0);
    }
    for(unsigned i=0;i<16;++i)if(!up_data && (streams&(1u<<i))) {
        uint32_t cache=PILOT_D3D_STREAMS+i*12,stride=MEM32(cache);
        struct GraphicsVertexObject *vertex=graphics_vertex(MEM32(cache+8));
        if(((uint64_t)first+count-1)*stride+extents[i]>vertex->bytes)stop("draw reads beyond vertex resource",i);
        pilot_gfx_vertex_upload_range(vertex->native,memory+vertex->data,
            first*stride,(count-1)*stride+extents[i]);
    }
    graphics_draw_profile_step(&profile,1);
    for(unsigned stage=0;stage<4;++stage) {
        uint32_t header=MEM32(PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_SNAPSHOT_TEXTURES+stage*4);
        if(header)graphics_texture_sample_prepare(graphics_texture_slot(header));
    }
    graphics_draw_profile_step(&profile,2);
    graphics_sync_deferred();
    graphics_draw_profile_step(&profile,3);
    if(!(shader&1)) {
        if(MEM32(PILOT_D3D_RENDER_STATES+92*4))stop("pretransformed fog is unqualified",shader);
        if(pilot_gfx_fvf_prepare(shader,graphics_float(TRANSFORM_FIELD(SCREEN_OFFSET)),
            graphics_float(TRANSFORM_FIELD(SCREEN_OFFSET)+4))<0)stop("native FVF preparation failed",shader);
        const char *capture=graphics_diagnostic(GRAPHICS_DIAGNOSTIC_FVF);
        if(capture && *capture && !up_data) {
            struct GraphicsVertexObject *vertex=graphics_vertex(MEM32(PILOT_D3D_STREAMS+8));
            uint32_t stride=MEM32(PILOT_D3D_STREAMS),bytes=pilot_gfx_fvf_extent(shader);
            static unsigned sequence;char path[1024];uint32_t header[]={shader,primitive,first,count,stride,bytes};
            if(snprintf(path,sizeof(path),"%s/fixed-draw-%u.bin",capture,++sequence)>=(int)sizeof(path))stop("FVF draw capture path",0);
            FILE *file=fopen(path,"wb");if(!file || fwrite(header,sizeof(header),1,file)!=1)stop("FVF draw capture header",0);
            if(fwrite(memory+PILOT_D3D_RENDER_STATES,166*4,1,file)!=1 || fwrite(memory+PILOT_D3D_TEXTURE_STATES,128*4,1,file)!=1)stop("FVF draw capture states",0);
            for(unsigned i=0;i<count;++i)if(fwrite(memory+vertex->data+(first+i)*stride,bytes,1,file)!=1)stop("FVF draw capture vertices",0);
            if(fclose(file))stop("FVF draw capture close",0);
        }
    } else {
    uint32_t viewport[6];if(pilot_gfx_get_viewport(viewport)<0)stop("missing draw viewport",0);
    float low,high;memcpy(&low,viewport+4,4);memcpy(&high,viewport+5,4);
    float depth=graphics_float(TRANSFORM_FIELD(DEPTH_SCALE));
    if(!viewport[2] || !viewport[3] || !(high>low) || !(depth>0))stop("unqualified draw viewport range",0);
    float scale[]={2.0f/viewport[2],-2.0f/viewport[3],1/((high-low)*depth),1};
    float offset[]={-1-viewport[0]*scale[0],1-viewport[1]*scale[1],-low/(high-low),0};
    if(pilot_gfx_shader_raster(scale,offset)<0)stop("native raster constants failed",0);
    if(!up_data)graphics_draw_capture(args);
    }
    graphics_draw_profile_step(&profile,4);
    int32_t result=up_data?pilot_gfx_draw_memory(primitive,count,memory+up_data,up_stride):pilot_gfx_draw_vertices(primitive,first,count);
    graphics_draw_profile_step(&profile,5);
    if(result<0)stop("native vertex draw failed",(uint32_t)result);
    if(pilot_verbose)fprintf(stderr,"[DRAW SUBMIT] primitive=%u first=%u count=%u\n",primitive,first,count);
    const char *capture=graphics_diagnostic(GRAPHICS_DIAGNOSTIC_FRAME);
    if(capture && *capture && graphics_target==graphics_headers) {
        static unsigned sequence;
        if(sequence<32) {
            unsigned width=graphics_parameters[0],height=graphics_parameters[1];
            unsigned char *pixels=malloc(width*height*4);char path[1024];
            if(!pixels || snprintf(path,sizeof(path),"%s/draw-%u.ppm",capture,++sequence)>=(int)sizeof(path))stop("frame capture allocation/path",0);
            if(pilot_gfx_readback(pixels,width*height*4)<0)stop("frame capture readback",0);
            FILE *file=fopen(path,"wb");if(!file || fprintf(file,"P6\n%u %u\n255\n",width,height)<0)stop("frame capture open",0);
            for(unsigned i=0;i<width*height;++i)if(fwrite(pixels+i*4,3,1,file)!=1)stop("frame capture write",0);
            free(pixels);if(fclose(file))stop("frame capture close",0);
        }
    }
    graphics_draw_profile_step(&profile,6);
}
