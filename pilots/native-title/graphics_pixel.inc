/* Original CreatePixelShader owns a 252-byte object containing a copy of the
 * 240-byte definition. It submits no GPU work. Keep that caller-visible layout;
 * native compilation is a separate operation and must succeed before binding. */
static uint32_t graphics_pixels[256];
static void graphics_pixel_bind(uint32_t handle)
{
    graphics_sync_deferred();
    if(handle) {
        unsigned slot=0;while(slot<256 && graphics_pixels[slot]!=handle)++slot;
        if(slot==256 || MEM32(handle+8)!=handle+12)stop("unowned pixel shader",handle);
        const uint32_t *definition=graphics_pointer(handle+12,240);
        if(pilot_gfx_pixel_program(definition,definition[54])<0)
            stop("unsupported native pixel program",handle);
        /* Original XDK SetPixelShader publishes these 57 DWORDs plus the
         * complex texture-mode slot; inline setters can change them later. */
        memcpy(memory+PILOT_D3D_RENDER_STATES,definition,57*4);
        MEM32(PILOT_D3D_RENDER_STATES+136*4)=definition[54];
        uint32_t snapshot=PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_SNAPSHOT_PIXEL_SHADER;
        MEM32(snapshot+4)=definition[8]|definition[9];
        MEM32(snapshot+8)=definition[59]&0x100;
        MEM32(snapshot+12)=definition[54];
    } else pilot_gfx_pixel_fixed();
    MEM32(PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_SNAPSHOT_PIXEL_SHADER)=handle;
}
static uint32_t graphics_pixel_create(const uint32_t *args)
{
    const void *definition=graphics_pointer(args[0],240);
    graphics_pointer(args[1],4);
    unsigned slot;
    for(slot=0;slot<256 && graphics_pixels[slot];++slot){}
    if(slot==256)return 0x8007000eu;
    uint32_t header=0,bytes=4096;
    if(vm_allocate(&header,0,&bytes,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE))return 0x8007000eu;
    MEM32(header)=1;MEM32(header+4)=1;MEM32(header+8)=header+12;
    memcpy(memory+header+12,definition,240);
    const char *capture=getenv("XLLE_PILOT_PIXEL_CAPTURE");
    if(capture && *capture) {
        static unsigned sequence;char path[1024];
        if(snprintf(path,sizeof(path),"%s/pixel-%u.bin",capture,++sequence)>=(int)sizeof(path))stop("pixel capture path too long",0);
        FILE *file=fopen(path,"wb");
        if(!file || fwrite(definition,240,1,file)!=1 || fclose(file))stop("pixel definition capture failed",0);
    }
    graphics_pixels[slot]=header;MEM32(args[1])=header;
    fprintf(stderr,"[PIXEL CREATE] object=%08x definition=%08x stages=%u\n",header,header+12,MEM32(header+12+212)&15);
    return 0;
}
static void graphics_pixels_release(void)
{
    for(unsigned i=0;i<256;++i)if(graphics_pixels[i]) {
        uint32_t bytes=0;
        if(vm_free(&graphics_pixels[i],&bytes,MEM_RELEASE))stop("pixel definition release failed",i);
        graphics_pixels[i]=0;
    }
}
