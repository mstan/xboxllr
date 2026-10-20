/* Opt-in private draw-state observations, including indexed/immediate draws.
 * This is deliberately not a vertex/pixel replay: it never reads vertex data,
 * samples GPU images, adopts resources or changes the original caches. One
 * swap interval is bounded to 256 records. Disabled captures perform no I/O. */
static void graphics_draw_state_capture(const uint32_t args[3],uint32_t shader,
                                       uint32_t native,unsigned index_count)
{
    static int configured;
    static const char *directory,*trigger;
    static unsigned sequence,captured_swap,armed,armed_swap;
    if(!configured) {
        directory=getenv("XLLE_PILOT_DRAW_STATE_CAPTURE");
        trigger=getenv("XLLE_PILOT_DRAW_STATE_TRIGGER");configured=1;
    }
    if(!directory || !*directory || sequence>=256)return;
    if(sequence && captured_swap!=graphics_swap_submitted)return;
    if(trigger && *trigger && GetFileAttributesA(trigger)==INVALID_FILE_ATTRIBUTES)return;
    if(!armed) {armed=1;armed_swap=graphics_swap_submitted;return;}
    if(!sequence && armed_swap==graphics_swap_submitted)return;
    if(!sequence)captured_swap=graphics_swap_submitted;
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
}
