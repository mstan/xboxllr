/* Complete draw inputs alongside the existing bounded shader capture. All
 * reads are diagnostic-only. GPU readback observes live storage without
 * changing guest bytes, resource exposure hints or production upload policy. */
static void graphics_scene_write(FILE *file,const void *data,size_t bytes)
{
    if(bytes && fwrite(data,bytes,1,file)!=1)stop("scene capture write failed",0);
}
static void graphics_scene_target(FILE *file,unsigned depth)
{
    uint32_t metadata[3];int32_t status=pilot_gfx_reference_target_readback(depth,NULL,0,metadata);
    if(status<0)stop("scene target shape failed",(uint32_t)status);
    graphics_scene_write(file,metadata,sizeof(metadata));
    if(status==1)return; /* no bound depth/color target */
    uint64_t bytes=(uint64_t)metadata[0]*metadata[1]*4;
    if(!bytes || bytes>64u*1024*1024)stop("scene target extent outside capture range",metadata[0]);
    void *data=malloc((size_t)bytes);if(!data)stop("scene target allocation failed",0);
    if(pilot_gfx_reference_target_readback(depth,data,(uint32_t)bytes,metadata)<0)stop("scene target readback failed",depth);
    graphics_scene_write(file,data,(size_t)bytes);free(data);
}
static FILE *graphics_scene_begin(const char *capture,unsigned sequence,const uint32_t args[3])
{
    char path[1024];
    if(snprintf(path,sizeof(path),"%s/scene-%u.bin",capture,sequence)>=(int)sizeof(path))stop("scene capture path",0);
    FILE *file=fopen(path,"wb");if(!file)stop("scene capture open",0);
    uint32_t pixel=MEM32(PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_SNAPSHOT_PIXEL_SHADER);
    uint32_t metadata[]={0x534c4c58,1,sequence,args[0],args[1],args[2],graphics_target,graphics_depth,pixel,MEM32(TRANSFORM_FIELD(DEPTH_SCALE))};
    graphics_scene_write(file,metadata,sizeof(metadata));
    uint32_t absent[60]={0};
    graphics_scene_write(file,graphics_pointer(graphics_target,20),20);
    graphics_scene_write(file,graphics_depth?graphics_pointer(graphics_depth,20):absent,20);
    graphics_scene_write(file,pixel?graphics_pointer(pixel+12,240):absent,240);
    graphics_scene_write(file,memory+PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_PIXEL_PACKED_CONSTANTS,16*4);
    graphics_scene_target(file,0);graphics_scene_target(file,1);
    for(unsigned stage=0;stage<4;++stage) {
        uint32_t header=MEM32(PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_SNAPSHOT_TEXTURES+stage*4);
        if(!header) {uint32_t empty[10]={stage};graphics_scene_write(file,empty,sizeof(empty));continue;}
        struct GraphicsTextureObject *texture=&graphics_textures[graphics_texture_slot(header)];
        if(texture->parent)stop("scene sampled surface requires texture view",header);
        uint32_t cpu_bytes=texture->guest_owned?0:texture->bytes;
        uint32_t info[]={stage,header,texture->width,texture->height,texture->levels,texture->format,
            texture->usage,texture->guest_owned,cpu_bytes,0};
        graphics_scene_write(file,info,sizeof(info));
        graphics_scene_write(file,graphics_pointer(header,20),20);
        if(cpu_bytes)graphics_scene_write(file,graphics_pointer(texture->data,cpu_bytes),cpu_bytes);
        for(unsigned level=0;level<texture->levels;++level) {
            unsigned width=graphics_level_width(texture,level),height=graphics_level_height(texture,level);
            unsigned block=graphics_texture_block(texture),yuv=texture->format==0x24 || texture->format==0x25;
            unsigned pitch=block?((width+3)/4)*block:width*(yuv?4:graphics_texture_bpp(texture));
            unsigned rows=block?(height+3)/4:height;
            uint32_t mip[]={width,height,pitch,rows,pitch*rows,yuv};
            graphics_scene_write(file,mip,sizeof(mip));
            void *data=malloc(mip[4]);if(!data)stop("scene texture readback allocation",0);
            if(pilot_gfx_texture_readback(texture->native,level,data,mip[4])<0)stop("scene texture GPU readback failed",header);
            graphics_scene_write(file,data,mip[4]);free(data);
        }
    }
    return file;
}
static void graphics_scene_finish(FILE *file)
{
    graphics_scene_target(file,0);graphics_scene_target(file,1);
    if(fclose(file))stop("scene capture close",0);
}
