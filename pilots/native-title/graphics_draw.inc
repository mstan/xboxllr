/* Optional private draw inputs/output capture. It never substitutes for a
 * submitted draw and is disabled in ordinary production execution. */
#include "graphics_scene_capture.inc"
static FILE *graphics_draw_capture(const uint32_t args[3])
{
    const char *capture=graphics_diagnostic(GRAPHICS_DIAGNOSTIC_DRAW);if(!capture || !*capture)return NULL;
#ifdef PILOT_GRAPHICS_FRAME_AUDIT
    if(graphics_frame_inputs.file)return NULL;
    if(!graphics_frame_audit_capture_active())return NULL;
#endif
    static unsigned sequence;
    const char *trigger=getenv("XLLE_PILOT_DRAW_CAPTURE_TRIGGER");
    if(trigger && *trigger && GetFileAttributesA(trigger)==INVALID_FILE_ATTRIBUTES)return NULL;
    /* A late trigger can land in a UI pass near the end of a frame. Allow a
     * diagnostic to wait for the next swap so its bounded draw budget starts
     * with the scene instead. Ordinary rendering never enters this path. */
    static unsigned armed,armed_swap;
    const char *after_swap=getenv("XLLE_PILOT_DRAW_CAPTURE_AFTER_SWAP");
    if(after_swap && !strcmp(after_swap,"1")) {
        if(!armed) {armed=1;armed_swap=graphics_swap_submitted;return NULL;}
        if(armed_swap==graphics_swap_submitted)return NULL;
    }
    const char *format_setting=getenv("XLLE_PILOT_DRAW_CAPTURE_FORMAT");
    if(format_setting && *format_setting) {
        char *end;unsigned long selected=strtoul(format_setting,&end,0);
        if(*end || selected>255)stop("draw capture texture format outside range",0);
        uint32_t header=MEM32(PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_SNAPSHOT_TEXTURES);
        unsigned found=0;
        for(unsigned i=0;header && i<graphics_texture_capacity;++i)
            if(graphics_textures[i].header==header) {
                found=graphics_textures[i].format==selected;break;
            }
        if(!found)return NULL;
    }
    const char *requested=getenv("XLLE_PILOT_DRAW_CAPTURE_LIMIT");
    unsigned limit=requested?(unsigned)strtoul(requested,NULL,10):32;
    if(!limit || limit>256)stop("draw capture limit outside range",limit);
    if(sequence>=limit)return NULL;
    /* Texture inspection may need one representative draw per binding rather
     * than spending the entire budget on repeated particle/fur layers. */
    static uint32_t seen_textures[256];static unsigned seen_count;
    const char *unique=getenv("XLLE_PILOT_DRAW_CAPTURE_UNIQUE_TEXTURES");
    if(unique && !strcmp(unique,"1")) {
        uint32_t header=MEM32(PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_SNAPSHOT_TEXTURES);
        for(unsigned i=0;i<seen_count;++i)if(seen_textures[i]==header)return NULL;
        if(seen_count>=256)return NULL;
        seen_textures[seen_count++]=header;
    }
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
    if(!graphics_render_capture(file))stop("draw capture render states",0);
    DRAW_CAPTURE(memory+PILOT_D3D_TEXTURE_STATES,4*128);
    DRAW_CAPTURE(elements,sizeof(elements));DRAW_CAPTURE(constants,sizeof(constants));
    DRAW_CAPTURE(shader->program,header[5]*16);DRAW_CAPTURE(metadata,sizeof(metadata));
    for(unsigned i=0;i<16;++i)if(metadata[i][1])DRAW_CAPTURE(streams[i].data,metadata[i][1]);
    DRAW_CAPTURE(outputs,args[2]*13*16);
#undef DRAW_CAPTURE
    free(outputs);if(fclose(file))stop("draw capture close",0);
    fprintf(stderr,"[DRAW CAPTURE] sequence=%u shader=%u primitive=%u first=%u count=%u target=%08x depth=%.9g\n",
        sequence,shader->native,args[0],args[1],args[2],graphics_target,graphics_float(TRANSFORM_FIELD(DEPTH_SCALE)));
    FILE *scene=graphics_scene_begin(capture,sequence,args);
#ifdef PILOT_GRAPHICS_FRAME_AUDIT
    graphics_frame_audit.capture_sequence=sequence;
#endif
    return scene;
}
#include "graphics_fixed_capture.inc"
#include "graphics_draw_state_capture.inc"
static void graphics_frame_capture_draw(unsigned shader,const uint32_t args[3],uint32_t up_data,uint32_t up_stride,
    const void *indices,unsigned index_count,int32_t base,float x_scale,float x_anchor,int before)
{
    const char *capture=graphics_diagnostic(GRAPHICS_DIAGNOSTIC_FRAME);
    if(!capture || !*capture || graphics_target!=graphics_headers)return;
    static unsigned sequence,caller,selected_shader;
    static int configured,filtered;
    static const char *trigger;
    if(sequence>=32)return;
    if(!configured) {
        configured=1;trigger=getenv("XLLE_PILOT_FRAME_CAPTURE_TRIGGER");
        const char *selection=getenv("XLLE_PILOT_FRAME_CAPTURE_SHADER");
        if(selection && *selection) {
            char *end;unsigned long long value=strtoull(selection,&end,0);
            if(selection[0]<'0' || selection[0]>'9' || *end || !value || value>UINT32_MAX)
                stop("frame capture shader must be a nonzero uint32 literal",0);
            selected_shader=(unsigned)value;filtered=1;
        }
        const char *setting=getenv("XLLE_PILOT_FRAME_CAPTURE_CALLER");
        if(setting && *setting) {
            char *end;unsigned long long value=strtoull(setting,&end,0);
            if(setting[0]<'0' || setting[0]>'9' || *end || !value || value>UINT32_MAX)
                stop("frame capture caller must be a nonzero uint32 literal",0);
            caller=(unsigned)value;filtered=1;
        }
    }
    if((before && !filtered) || (caller && MEM32(g_esp)!=caller) || (selected_shader && shader!=selected_shader))return;
    /* A late trigger leaves the existing startup capture unchanged when
     * absent. A caller filter captures paired before/after images of only
     * that producer, still bounded to 32 total images. */
    if(trigger && *trigger) {
        FILE *gate=fopen(trigger,"rb");if(!gate)return;fclose(gate);
    }
    unsigned width=graphics_parameters[0],height=graphics_parameters[1];
    unsigned char *pixels=malloc(width*height*4);char path[1024];
    if(!pixels || snprintf(path,sizeof(path),"%s/draw-%u.ppm",capture,++sequence)>=(int)sizeof(path))stop("frame capture allocation/path",0);
    if(pilot_gfx_readback(pixels,width*height*4)<0)stop("frame capture readback",0);
    FILE *file=fopen(path,"wb");if(!file || fprintf(file,"P6\n%u %u\n255\n",width,height)<0)stop("frame capture open",0);
    for(unsigned i=0;i<width*height;++i)if(fwrite(pixels+i*4,3,1,file)!=1)stop("frame capture write",0);
    free(pixels);if(fclose(file))stop("frame capture close",0);
    if(before)graphics_fixed_capture_inputs(capture,sequence,shader,args,up_data,up_stride,indices,index_count,base);
    uint32_t target_shape[3]={0},header=MEM32(PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_SNAPSHOT_TEXTURES);
    uint32_t source_width=0,source_height=0;
    /* Observation never adopts an absent texture/alias through the ordinary
     * ownership lookup. Zero dimensions explicitly mean unknown. */
    for(unsigned i=0;header && i<graphics_texture_capacity;++i)if(graphics_textures[i].header==header) {
        source_width=graphics_textures[i].width;source_height=graphics_textures[i].height;break;
    }
    int32_t shape_status=pilot_gfx_reference_target_readback(0,NULL,0,target_shape);
    struct PilotRasterObservation raster;int32_t raster_status=pilot_gfx_reference_raster(&raster);
    fprintf(stderr,"[FRAME DRAW CAPTURE] sequence=%u phase=%s caller=%08x shader=%08x count=%u target=%08x storage=%ux%u shape_status=%08x texture=%08x source_guest=%ux%u x_scale=%.9g x_anchor=%.9g image=%s\n",
        sequence,before?"before":"after",MEM32(g_esp),shader,args[2],graphics_target,target_shape[0],target_shape[1],(uint32_t)shape_status,
        header,source_width,source_height,x_scale,x_anchor,path);
    fprintf(stderr,"[FRAME RASTER CAPTURE] sequence=%u status=%08x available=%u viewport=%.9g,%.9g,%.9g,%.9g,%.9g,%.9g scissor_enabled=%u scissor=%d,%d,%d,%d source_storage=%ux%u\n",
        sequence,(uint32_t)raster_status,raster.available,raster.viewport[0],raster.viewport[1],raster.viewport[2],
        raster.viewport[3],raster.viewport[4],raster.viewport[5],raster.scissor_enabled,
        raster.scissor[0],raster.scissor[1],raster.scissor[2],raster.scissor[3],raster.source_width,raster.source_height);
}
#include "graphics_lighting.inc"
static void graphics_draw_submit_source(const uint32_t args[3],uint32_t up_data,uint32_t up_stride,const void *indices,unsigned index_count,int32_t base,const void *host_vertices)
{
#ifdef PILOT_GRAPHICS_FRAME_AUDIT
    graphics_frame_audit.capture_sequence=0;
#endif
    uint64_t profile=graphics_service_profile_enabled() && !dispatch_in_dpc?dispatch_monotonic():0;
    if(profile)++graphics_service_profile.draw_calls;
    unsigned primitive=args[0],first=args[1],count=args[2];
    unsigned submitted=indices?index_count:count;
    int memory_vertices=up_data || host_vertices;
    int buffered_points=primitive==1 && !indices && !memory_vertices;
    if(!count || (uint64_t)first+count>UINT32_MAX)stop("invalid draw range",count);
    if((!buffered_points && primitive!=5 && primitive!=6 && primitive!=7 && primitive!=8) || (!buffered_points && submitted<3) ||
        (primitive==5 && submitted%3) || (primitive==8 && submitted%4)) {
        fprintf(stderr,"[D3D PRIMITIVE] type=%u first=%u count=%u submitted=%u indexed=%u shader=%08x\n",
            primitive,first,count,submitted,indices!=NULL,MEM32(PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_SNAPSHOT_VERTEX_SHADER));
        if(primitive==1) {
            fprintf(stderr,"[D3D POINT STATE]");
            for(unsigned state=116;state<=123;++state)fprintf(stderr," %u=%08x",state,graphics_render_read(state));
            fputc('\n',stderr);
        }
        stop("unqualified primitive/count",primitive);
    }
    const char *draw_capture=graphics_diagnostic(GRAPHICS_DIAGNOSTIC_DRAW),*fvf_capture=graphics_diagnostic(GRAPHICS_DIAGNOSTIC_FVF);
    if(host_vertices && (indices || up_data || first ||
        (draw_capture && *draw_capture) || (fvf_capture && *fvf_capture) ||
        graphics_diagnostic(GRAPHICS_DIAGNOSTIC_FRAME)))
        stop("unqualified immediate input serialization/source",primitive);
    if(indices && ((draw_capture && *draw_capture) || (fvf_capture && *fvf_capture)))
        stop("indexed draw input capture outside qualified diagnostic scope",primitive);
#ifdef PILOT_GRAPHICS_FRAME_AUDIT
    if(indices && graphics_frame_inputs.file)stop("indexed frame input capture outside qualified diagnostic scope",primitive);
#endif
    if(MEM32(PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_SNAPSHOT_PIXEL_SHADER) &&
        pilot_gfx_pixel_program((const uint32_t*)(memory+PILOT_D3D_RENDER_STATES),
            graphics_render_read(136))<0)stop("unsupported native pixel draw",0);
    graphics_target_prepare(graphics_target);graphics_target_prepare(graphics_depth);
    graphics_draw_profile_step(&profile,0);
    uint32_t shader=MEM32(PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_SNAPSHOT_VERTEX_SHADER);
    if(host_vertices && (shader&15)!=4)stop("unqualified immediate vertex shader",shader);
    struct GraphicsShader *declaration=(shader&1)?graphics_shader(shader):NULL;
    int beta_fvf=0;
#ifdef PILOT_D3D_FVF_FLOAT_BETA_VERSION
    beta_fvf=!(shader&1) && (shader&15)>=6 && (shader&15)<=12;
#endif
    int fixed_fvf=!(shader&1) && ((shader&15)==2 || beta_fvf);
    int fixed=(declaration && declaration->fixed) || fixed_fvf;
    int screen_fvf=!(shader&1) && (shader&15)==4;
    if(beta_fvf) {
        uint32_t blend=graphics_render_read(137);
        if(blend)stop("unqualified active beta-FVF vertex blending",blend);
        /* Publish the observed disabled state even if its original cache did
         * not change from the initializer baseline. No guest cache is written. */
        graphics_render_state(137,0,0);
    }
    if(buffered_points) {
        /* Fixed/screen vertices use the native GPU square/attenuation owner.
         * Programmable/per-vertex size and edge AA still need their consumers. */
        uint32_t minimum=graphics_render_read(117),maximum=graphics_render_read(123);
        if((!fixed && !screen_fvf) || ((fixed_fvf || screen_fvf) && (shader&0x20)) ||
            graphics_render_read(151)) {
            fprintf(stderr,"[POINT STATE] fvf=%08x fixed=%d host_vertices=%d size=%08x min=%08x max=%08x sprite=%u scale=%u smooth=%u attenuation=%08x,%08x,%08x\n",
                shader,fixed,host_vertices!=NULL,graphics_render_read(116),minimum,maximum,
                graphics_render_read(118),graphics_render_read(119),graphics_render_read(151),
                graphics_render_read(120),graphics_render_read(121),graphics_render_read(122));
            stop("unqualified active point raster state",shader);
        }
        uint32_t points[8];for(unsigned i=0;i<8;++i)points[i]=graphics_render_read(116+i);
        if(pilot_gfx_points(points,screen_fvf)<0)stop("invalid native point parameters",shader);
    }
    if(fixed_fvf) {
        if((draw_capture && *draw_capture) || (fvf_capture && *fvf_capture))
            stop("XYZ FVF input serialization outside qualified diagnostic scope",shader);
#ifdef PILOT_GRAPHICS_FRAME_AUDIT
        if(graphics_frame_inputs.file)stop("XYZ FVF frame inputs outside qualified diagnostic scope",shader);
#endif
    }
    if(declaration && !fixed && pilot_gfx_shader_current_inputs(declaration->native)) {
        if((draw_capture && *draw_capture) || (fvf_capture && *fvf_capture))
            stop("current vertex input serialization outside qualified diagnostic scope",shader);
#ifdef PILOT_GRAPHICS_FRAME_AUDIT
        if(graphics_frame_inputs.file)stop("current vertex frame inputs outside qualified diagnostic scope",shader);
#endif
    }
    FILE *scene_reference=NULL;
    uint32_t extents[16]={0};unsigned streams;
    if(shader&1)streams=pilot_gfx_shader_streams(extents);
    else {extents[0]=graphics_fvf_layout(shader,NULL);streams=1;}
    if(memory_vertices) {
        if(streams!=1 || !up_stride || up_stride<extents[0] || (uint64_t)count*up_stride>UINT32_MAX)
            stop("invalid user-pointer draw stream",up_stride);
        if(up_data)graphics_pointer(up_data,count*up_stride);
    }
    const char *feedback_capture=graphics_diagnostic(GRAPHICS_DIAGNOSTIC_FEEDBACK);
    if(pilot_gfx_feedback_mask() && feedback_capture && *feedback_capture && up_data) {
        char path[1024];static unsigned sequence;
        if(snprintf(path,sizeof(path),"%s/feedback-%u.bin",feedback_capture,++sequence)>=(int)sizeof(path))stop("feedback capture path",0);
        FILE *file=fopen(path,"wb");if(!file)stop("feedback capture open",0);
        uint32_t header[]={0x42464c58,1,primitive,first,count,shader,up_stride,pilot_gfx_feedback_mask(),
            MEM32(TRANSFORM_FIELD(SCREEN_OFFSET)),MEM32(TRANSFORM_FIELD(SCREEN_OFFSET)+4)};
        if(fwrite(header,sizeof(header),1,file)!=1 || !graphics_render_capture(file) ||
            fwrite(memory+PILOT_D3D_TEXTURE_STATES,128*4,1,file)!=1 || fwrite(memory+up_data,count*up_stride,1,file)!=1 || fclose(file))
            stop("feedback capture write",0);
    }
    for(unsigned i=0;i<16;++i)if(!memory_vertices && (streams&(1u<<i))) {
        uint32_t cache=PILOT_D3D_STREAMS+i*12,stride=MEM32(cache);
        struct GraphicsVertexObject *vertex=graphics_vertex(MEM32(cache+8));
        if(((uint64_t)first+count-1)*stride+extents[i]>vertex->bytes)stop("draw reads beyond vertex resource",i);
        pilot_gfx_vertex_upload_range(vertex->native,memory+vertex->data,
            first*stride,(count-1)*stride+extents[i]);
    }
    graphics_draw_profile_step(&profile,1);
    for(unsigned stage=0;stage<4;++stage) {
        uint32_t header=MEM32(PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_SNAPSHOT_TEXTURES+stage*4);
        if(header) {
            graphics_texture_sample_prepare(graphics_texture_slot(header));
            pilot_gfx_texture_sampling_format(stage,(MEM32(header+12)>>8)&255);
        }
    }
    graphics_draw_profile_step(&profile,2);
    if(fixed) {
        if(graphics_render_read(102))graphics_lighting_sync(shader);
        graphics_sync_deferred();
    }
    if(!pilot_gfx_initializer_consumer(fixed?2:(shader&1)!=0,primitive,shader))
        stop("unqualified initializer state consumer",shader);
    if(!fixed)graphics_sync_deferred();
    graphics_draw_profile_step(&profile,3);
    float mod_x_scale=1,mod_x_anchor=0;
#ifdef PILOT_TITLE_MODS
    if(title_mod && title_mod->draw) {
        uint32_t stride=up_stride,data=up_data,position_type=fixed_fvf?0x32:0x42,position_offset=0;
        if(!memory_vertices && (streams&1)) {
            stride=MEM32(PILOT_D3D_STREAMS);
            data=graphics_vertex(MEM32(PILOT_D3D_STREAMS+8))->data+first*stride;
        }
        if(shader&1) {
            uint32_t descriptor=graphics_shader(shader)->header+0x14;
            if(MEM32(descriptor)!=0)data=0; /* only stream-zero position here */
            position_offset=MEM32(descriptor+4);position_type=MEM32(descriptor+8);
        }
        struct PilotTitleModDraw draw={MEM32(g_esp),shader,primitive,count,graphics_target==graphics_headers,
            host_vertices?(const unsigned char*)host_vertices+position_offset:data?memory+data+position_offset:NULL,stride,position_type,1,0,
            graphics_render_read(143),graphics_render_read(64)};
        title_mod->draw(&draw);
        if(!(draw.x_scale>0 && draw.x_scale<=1 && isfinite(draw.x_anchor)))
            stop("invalid title mod raster placement",draw.caller);
        mod_x_scale=draw.x_scale;mod_x_anchor=draw.x_anchor;
    }
#endif
    if(fixed) {
        /* The original driver may write its matrix cache inline. Synchronize
         * the profile-owned values without changing guest dirty bits or caches.
         * Active lighting has already synchronized its original light/material
         * snapshot. Neither operation changes original cache or dirty bits. */
        if(mod_x_scale!=1 || mod_x_anchor!=0)stop("unqualified fixed-function mod placement",shader);
        for(unsigned i=0;i<10;++i)
            if(pilot_gfx_transform(i,(const float*)(memory+TRANSFORM_STATE(TRANSFORMS)+i*64))<0)
                stop("native fixed-function transform synchronization failed",i);
        int32_t result=beta_fvf?pilot_gfx_fvf_prepare_unblended(shader):
            fixed_fvf?pilot_gfx_fvf_prepare_fixed(shader):pilot_gfx_shader_prepare_fixed(declaration->native);
        if(result<0)stop("native fixed-function preparation failed",shader);
    } else if(!(shader&1)) {
        if(pilot_gfx_fvf_placement(shader,graphics_float(TRANSFORM_FIELD(SCREEN_OFFSET)),
            graphics_float(TRANSFORM_FIELD(SCREEN_OFFSET)+4),mod_x_scale,mod_x_anchor)<0)stop("native FVF preparation failed",shader);
        if(host_vertices && pilot_gfx_immediate_fvf(shader)<0)stop("native immediate FVF preparation failed",shader);
        if(graphics_render_read(92) &&
            pilot_gfx_fvf_fog(shader,(const float*)(memory+TRANSFORM_STATE(TRANSFORMS)+64),host_vertices!=NULL)<0)
            stop("unqualified pretransformed fog input/mode",shader);
        const char *capture=graphics_diagnostic(GRAPHICS_DIAGNOSTIC_FVF);
        if(capture && *capture && !up_data) {
            struct GraphicsVertexObject *vertex=graphics_vertex(MEM32(PILOT_D3D_STREAMS+8));
            uint32_t stride=MEM32(PILOT_D3D_STREAMS),bytes=pilot_gfx_fvf_extent(shader);
            static unsigned sequence;char path[1024];uint32_t header[]={shader,primitive,first,count,stride,bytes};
            if(snprintf(path,sizeof(path),"%s/fixed-draw-%u.bin",capture,++sequence)>=(int)sizeof(path))stop("FVF draw capture path",0);
            FILE *file=fopen(path,"wb");if(!file || fwrite(header,sizeof(header),1,file)!=1)stop("FVF draw capture header",0);
            if(!graphics_render_capture(file) || fwrite(memory+PILOT_D3D_TEXTURE_STATES,128*4,1,file)!=1)stop("FVF draw capture states",0);
            for(unsigned i=0;i<count;++i)if(fwrite(memory+vertex->data+(first+i)*stride,bytes,1,file)!=1)stop("FVF draw capture vertices",0);
            if(fclose(file))stop("FVF draw capture close",0);
        }
    } else {
    uint32_t viewport[6];if(pilot_gfx_get_viewport(viewport)<0)stop("missing draw viewport",0);
    float low,high;memcpy(&low,viewport+4,4);memcpy(&high,viewport+5,4);
    float depth=graphics_float(TRANSFORM_FIELD(DEPTH_SCALE));
    float sx=graphics_float(TRANSFORM_FIELD(PIXEL_SCALE)),sy=graphics_float(TRANSFORM_FIELD(PIXEL_SCALE)+4);
    if(!viewport[2] || !viewport[3] || !(high>low) || !(depth>0) || !(sx>0) || !(sy>0) || !isfinite(sx) || !isfinite(sy))stop("unqualified draw viewport range",0);
    /* Original programs emit sample-space pixels using the SDK viewport
     * scale/offset. Undo that guest sample scale before native clip space;
     * elective host upscaling is a separate backend viewport operation. */
    float scale[]={2.0f/(viewport[2]*sx),-2.0f/(viewport[3]*sy),1/((high-low)*depth),1};
    float offset[]={-1-viewport[0]*sx*scale[0],1-viewport[1]*sy*scale[1],-low/(high-low),0};
    offset[0]+=(1-mod_x_scale)*mod_x_anchor*sx*scale[0];scale[0]*=mod_x_scale;
    if(pilot_gfx_shader_raster(scale,offset)<0)stop("native raster constants failed",0);
    if(!memory_vertices && !indices)scene_reference=graphics_draw_capture(args);
    }
    graphics_draw_profile_step(&profile,4);
    graphics_draw_state_capture(args,shader,declaration?declaration->native:0,index_count);
    graphics_frame_capture_draw(shader,args,up_data,up_stride,indices,index_count,base,mod_x_scale,mod_x_anchor,1);
#ifdef PILOT_GRAPHICS_FRAME_AUDIT
    if(!indices && !host_vertices)graphics_frame_inputs_draw(args,up_data,up_stride,streams,extents);
#endif
    int32_t result=indices?pilot_gfx_draw_indexed(primitive,index_count,indices,base):
        host_vertices?pilot_gfx_draw_memory(primitive,count,host_vertices,up_stride):
        up_data?pilot_gfx_draw_memory(primitive,count,memory+up_data,up_stride):pilot_gfx_draw_vertices(primitive,first,count);
    graphics_draw_profile_step(&profile,5);
    if(result<0)stop("native vertex draw failed",(uint32_t)result);
    if(scene_reference)graphics_scene_finish(scene_reference);
    if(graphics_diagnostic(GRAPHICS_DIAGNOSTIC_MOVIE))graphics_movie_capture_draw();
    if(pilot_verbose)fprintf(stderr,"[DRAW SUBMIT] primitive=%u first=%u count=%u\n",primitive,first,count);
    graphics_frame_capture_draw(shader,args,up_data,up_stride,indices,index_count,base,mod_x_scale,mod_x_anchor,0);
    graphics_draw_profile_step(&profile,6);
}
static void graphics_draw_submit(const uint32_t args[3],uint32_t up_data,uint32_t up_stride,const void *indices,unsigned index_count,int32_t base)
{graphics_draw_submit_source(args,up_data,up_stride,indices,index_count,base,NULL);}
static void graphics_draw_common(const uint32_t args[3],uint32_t up_data,uint32_t up_stride)
{graphics_draw_submit(args,up_data,up_stride,NULL,0,0);}
static void graphics_draw_indexed(const uint32_t args[3])
{
    unsigned count=args[1];
    if(!count || count>32u*1024*1024)stop("invalid indexed draw count",count);
    const unsigned char *indices=graphics_pointer(args[2],count*2);
    unsigned low=UINT16_MAX,high=0;
    for(unsigned i=0;i<count;++i) {
        unsigned value=(unsigned)indices[i*2]|((unsigned)indices[i*2+1]<<8);
        if(value<low)low=value;if(value>high)high=value;
    }
    int32_t base=(int32_t)MEM32(PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_SNAPSHOT_BASE_VERTEX);
    int64_t first=(int64_t)base+low,last=(int64_t)base+high;
    if(first<0 || last>=UINT32_MAX)stop("indexed base/vertex range overflow",(uint32_t)base);
    uint32_t draw[]={args[0],(uint32_t)first,high-low+1};
    /* The existing draw owner checks and uploads only the live stream span;
     * indices stay indices all the way to native DrawIndexed. */
    graphics_draw_submit(draw,0,0,indices,count,base);
    if(!graphics_index_draws++)fprintf(stderr,"[INDEXED DRAW] primitive=%u indices=%u base=%d first=%u span=%u\n",
        args[0],count,base,draw[1],draw[2]);
    uint32_t header=MEM32(PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_SNAPSHOT_INDICES);
    if(header) {
        struct PilotGpuProgress progress;pilot_gfx_completion_stats(&progress);
        graphics_index(header)->last_use=progress.submitted;
    }
}
