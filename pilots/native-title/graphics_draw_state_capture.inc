/* Opt-in private draw-state observations, including indexed/immediate draws.
 * This is deliberately not a vertex/pixel replay: bounded fixed/immediate
 * inputs and optional texture readback observe existing storage without
 * adopting resources, uploading or changing guest bytes. Updating the trigger
 * timestamp requests another swap interval; all captures together remain
 * bounded to 2048 draw records. Disabled captures do no I/O. */
static void graphics_draw_state_capture(const uint32_t args[3],uint32_t shader,
    uint32_t native,unsigned index_count,uint32_t up_data,uint32_t up_stride,
    const void *indices,int32_t base,const void *host_vertices)
{
    static int configured;
    static const char *directory,*trigger;
    static unsigned sequence,captured_swap,capturing,armed,armed_swap,polled,polled_swap,vertex_limit=16;
    static uint64_t trigger_stamp;
    if(!configured) {
        directory=getenv("XLLE_PILOT_DRAW_STATE_CAPTURE");
        trigger=getenv("XLLE_PILOT_DRAW_STATE_TRIGGER");configured=1;
        const char *limit=getenv("XLLE_PILOT_DRAW_STATE_MAX_VERTICES");
        if(limit && *limit) {
            char *end;unsigned long count=strtoul(limit,&end,10);
            if(*end || !count || count>1024)stop("draw-state vertex limit outside range",0);
            vertex_limit=(unsigned)count;
        }
    }
    if(!directory || !*directory || sequence>=2048)return;
    /* An unarmed playtest can run for hours. Poll at most once per guest
     * swap, rather than performing a filesystem query for every draw. */
    if(!polled || polled_swap!=graphics_swap_submitted) {
        polled=1;polled_swap=graphics_swap_submitted;
        unsigned changed=!armed && (!trigger || !*trigger);
        if(trigger && *trigger) {
            WIN32_FILE_ATTRIBUTE_DATA data;
            if(GetFileAttributesExA(trigger,GetFileExInfoStandard,&data) && !(data.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)) {
                uint64_t stamp=((uint64_t)data.ftLastWriteTime.dwHighDateTime<<32)|data.ftLastWriteTime.dwLowDateTime;
                changed=!armed || stamp!=trigger_stamp;trigger_stamp=stamp;
            }
        }
        if(changed){armed=1;armed_swap=graphics_swap_submitted;capturing=0;}
    }
    if(!armed || (!capturing && armed_swap==graphics_swap_submitted))return;
    if(!capturing){captured_swap=graphics_swap_submitted;capturing=1;}
    if(captured_swap!=graphics_swap_submitted)return;
    uint32_t constants[192][4],textures[4][8]={0},pixel[60]={0};
    pilot_gfx_shader_capture_constants(constants);
    uint32_t pixel_header=MEM32(PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_SNAPSHOT_PIXEL_SHADER);
    if(pixel_header)memcpy(pixel,graphics_pointer(pixel_header+12,sizeof(pixel)),sizeof(pixel));
    for(unsigned stage=0;stage<4;++stage) {
        uint32_t header=MEM32(PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_SNAPSHOT_TEXTURES+stage*4);
        textures[stage][0]=header;
        for(unsigned i=0;header && i<graphics_texture_capacity;++i)
            if(graphics_textures[i].header==header) {
                const struct GraphicsTextureObject *texture=&graphics_textures[i];
                const uint32_t row[]={header,texture->native,texture->width,texture->height,
                    texture->levels,texture->format,texture->usage,texture->guest_owned};
                memcpy(textures[stage],row,sizeof(row));break;
            }
    }
    /* Indexed draws can use this observation path too. Capture each sampled
     * linear color image once, bounded to 8 MiB total. This is an atlas/content
     * observation, not complete draw replay. Never adopt or prepare a texture
     * merely to inspect it, since that could change the coherence under test. */
    static uint32_t sampled[32],sampled_count,sampled_bytes;
    const char *images=getenv("XLLE_PILOT_DRAW_STATE_TEXTURES");
    if(images && !strcmp(images,"1"))for(unsigned stage=0;stage<4;++stage) {
        uint32_t *t=textures[stage],format=t[5];unsigned seen=0;
        for(unsigned i=0;i<sampled_count;++i)seen|=sampled[i]==t[0];
        if(!t[0] || !t[1] || seen || sampled_count>=32 || (format!=0x11 && format!=0x12))continue;
        uint64_t pitch=(uint64_t)t[2]*(format==0x11?2:4),bytes=pitch*t[3];
        if(!bytes || bytes>8u*1024*1024-sampled_bytes)continue;
        void *pixels=malloc((size_t)bytes);if(!pixels)stop("draw-state texture allocation",t[0]);
        if(pilot_gfx_texture_readback(t[1],0,pixels,(uint32_t)bytes)<0)stop("draw-state texture readback",t[0]);
        char path[1024];
        if(snprintf(path,sizeof(path),"%s/texture-%08x.bin",directory,t[0])>=(int)sizeof(path))
            stop("draw-state texture path",t[0]);
        FILE *file=fopen(path,"wb");
        uint32_t shape[]={0x54444c58u,1,t[0],t[2],t[3],format,(uint32_t)pitch,(uint32_t)bytes};
        if(!file || fwrite(shape,sizeof(shape),1,file)!=1 || fwrite(pixels,(size_t)bytes,1,file)!=1 || fclose(file))
            stop("draw-state texture write",t[0]);
        free(pixels);sampled[sampled_count++]=t[0];sampled_bytes+=(uint32_t)bytes;
    }
    uint32_t header[]={0x53444c58u,1,++sequence,captured_swap,shader,native,
        args[0],args[1],args[2],index_count,graphics_target,graphics_depth,pixel_header,
        MEM32(g_esp),166,128};
    char path[1024];
    if(snprintf(path,sizeof(path),"%s/draw-state-%u.bin",directory,sequence)>=(int)sizeof(path))
        stop("draw-state capture path",0);
    FILE *file=fopen(path,"wb");
    if(!file || fwrite(header,sizeof(header),1,file)!=1 || !graphics_render_capture(file) ||
       fwrite(memory+PILOT_D3D_TEXTURE_STATES,128*4,1,file)!=1 ||
       fwrite(constants,sizeof(constants),1,file)!=1 || fwrite(textures,sizeof(textures),1,file)!=1 ||
       fwrite(pixel,sizeof(pixel),1,file)!=1 || fclose(file))stop("draw-state capture write",sequence);
    struct PilotRasterObservation raster;
    if(pilot_gfx_reference_raster(&raster)<0)stop("draw-state raster observation",sequence);
    if(snprintf(path,sizeof(path),"%s/raster-%u.bin",directory,sequence)>=(int)sizeof(path))
        stop("draw-state raster path",sequence);
    file=fopen(path,"wb");
    if(!file || fwrite(&raster,sizeof(raster),1,file)!=1 || fclose(file))stop("draw-state raster write",sequence);
    /* Serialize submitted bytes of small indexed and UP screen quads.
     * The fixed-input format describes XYZRHW without inventing a program.
     * Immediate host vertices have a different owner and remain excluded. */
    if(images && !strcmp(images,"1") && !host_vertices && ((shader&1) || (shader&15)==4 || (shader&15)==2) &&
       args[2] && args[2]<=vertex_limit && index_count<=vertex_limit*8)
        graphics_fixed_capture_inputs(directory,sequence,shader,args,up_data,up_stride,indices,index_count,base);
    if(images && !strcmp(images,"1") && host_vertices && args[2] && args[2]<=16 && up_stride<=256) {
        if(snprintf(path,sizeof(path),"%s/immediate-%u.bin",directory,sequence)>=(int)sizeof(path))stop("immediate capture path",sequence);
        uint32_t shape[]={0x49444c58u,1,shader,args[0],args[2],up_stride,MEM32(g_esp),0};
        file=fopen(path,"wb");
        if(!file || fwrite(shape,sizeof(shape),1,file)!=1 || fwrite(host_vertices,args[2]*up_stride,1,file)!=1 || fclose(file))stop("immediate capture write",sequence);
    }
}
