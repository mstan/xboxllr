/* Optional private draw inputs/output capture. It never substitutes for a
 * submitted draw and is disabled in ordinary production execution. */
static void graphics_draw_capture(const uint32_t args[3])
{
    const char *capture=getenv("XLLE_PILOT_DRAW_CAPTURE");if(!capture || !*capture)return;
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
    static unsigned sequence;char path[1024];
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
}
static void graphics_draw_vertices(const uint32_t args[3])
{
    unsigned primitive=args[0],first=args[1],count=args[2];
    if(!count || (uint64_t)first+count>UINT32_MAX)stop("invalid draw range",count);
    if((primitive!=5 && primitive!=6) || count<3 || (primitive==5 && count%3))stop("unqualified primitive/count",primitive);
    if(MEM32(PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_SNAPSHOT_PIXEL_SHADER))stop("unqualified programmable pixel draw",0);
    graphics_target_prepare(graphics_target);graphics_target_prepare(graphics_depth);
    uint32_t extents[16];unsigned streams=pilot_gfx_shader_streams(extents);
    for(unsigned i=0;i<16;++i)if(streams&(1u<<i)) {
        uint32_t cache=PILOT_D3D_STREAMS+i*12,stride=MEM32(cache);
        struct GraphicsVertexObject *vertex=graphics_vertex(MEM32(cache+8));
        if(((uint64_t)first+count-1)*stride+extents[i]>vertex->bytes)stop("draw reads beyond vertex resource",i);
        pilot_gfx_vertex_upload(vertex->native,memory+vertex->data,vertex->bytes);
    }
    for(unsigned stage=0;stage<4;++stage) {
        uint32_t header=MEM32(PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_SNAPSHOT_TEXTURES+stage*4);
        if(header)graphics_texture_sample_prepare(graphics_texture_slot(header));
    }
    graphics_sync_deferred();
    uint32_t viewport[6];if(pilot_gfx_get_viewport(viewport)<0)stop("missing draw viewport",0);
    float low,high;memcpy(&low,viewport+4,4);memcpy(&high,viewport+5,4);
    float depth=graphics_float(TRANSFORM_FIELD(DEPTH_SCALE));
    if(!viewport[2] || !viewport[3] || !(high>low) || !(depth>0))stop("unqualified draw viewport range",0);
    float scale[]={2.0f/viewport[2],-2.0f/viewport[3],1/((high-low)*depth),1};
    float offset[]={-1-viewport[0]*scale[0],1-viewport[1]*scale[1],-low/(high-low),0};
    if(pilot_gfx_shader_raster(scale,offset)<0)stop("native raster constants failed",0);
    graphics_draw_capture(args);
    int32_t result=pilot_gfx_draw_vertices(primitive,first,count);
    if(result<0)stop("native vertex draw failed",(uint32_t)result);
    fprintf(stderr,"[DRAW SUBMIT] primitive=%u first=%u count=%u\n",primitive,first,count);
    const char *capture=getenv("XLLE_PILOT_FRAME_CAPTURE");
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
}
