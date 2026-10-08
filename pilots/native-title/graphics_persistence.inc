/* Original NTSC XDK PersistDisplay: replace the previous persistent surface,
 * copy the displayed front buffer into contiguous storage, preserve its pages
 * and publish its HEADER through AvSetSavedDataAddress. nxdk video.c confirms
 * allocation/persistence ownership on subsequent display setup. Phobos has
 * no usable persistence implementation; its kernel function is a stub.
 * No donor implementation copied. Real GPU readback and a native display
 * snapshot replace driver packets while preserving the observable lifetime. */
static void graphics_persistence_clear(void)
{
    if(av_saved_data) {physical_free(av_saved_data);av_saved_data=0;}
}
static uint32_t graphics_persist_display(void)
{
    graphics_persistence_clear();
    if(!graphics_clock_presented)stop("persistence before a qualified displayed frame",0);
    graphics_present_drain();
    uint32_t width=graphics_parameters[0],height=graphics_parameters[1],pitch=(width*4+63)&~63u;
    uint32_t bytes=pitch*height+64;
    uint32_t base=physical_allocate(bytes,0,0x07ffffff,64,PAGE_READWRITE|PAGE_WRITECOMBINE);
    if(!base)return 0x80004005u;
    unsigned char *pixels=malloc((size_t)width*height*4);
    if(!pixels){physical_free(base);return 0x80004005u;}
    int32_t hr=pilot_gfx_surface_readback(1,pixels,width*height*4);
    if(hr>=0) {
        uint32_t format=(graphics_parameters[2]==6 || graphics_parameters[2]==0x12)?0x12:0x1e;
        uint32_t header[]={0x81050001,(base+64)&0x0fffffff,0,0x10029|(format<<8),
            (width-1)|((height-1)<<12)|((pitch/64-1)<<24),0};
        memcpy(memory+base,header,sizeof(header));
        for(uint32_t y=0;y<height;++y)for(uint32_t x=0;x<width;++x) {
            unsigned char *in=pixels+((size_t)y*width+x)*4,*out=memory+base+64+(size_t)y*pitch+x*4;
            out[0]=in[2];out[1]=in[1];out[2]=in[0];out[3]=in[3];
        }
        hr=pilot_gfx_persist_display(memory+base+64,width,height,pitch);
    }
    free(pixels);
    if(hr<0){physical_free(base);return 0x80004005u;}
    physical_persist(base,bytes,1);av_saved_data=base;
    fprintf(stderr,"[DISPLAY PERSIST] surface=%08x data=%08x bytes=%u %ux%u pitch=%u\n",base,base+64,bytes,width,height,pitch);
    return 0;
}
