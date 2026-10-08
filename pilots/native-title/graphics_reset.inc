/* Original NTSC XDK Reset waits for GPU/flip work, replaces the embedded
 * presentation surfaces, restores default targets/full viewport and clears
 * depth/stencil. Texture/vertex/shader/state-block ownership is retained.
 * The unhooked reference fixture covers original buffer setup/publication;
 * NV2A scanout and native integration tests cover the two display backends. */
static uint32_t graphics_reset(uint32_t pointer)
{
    uint32_t p[17];memcpy(p,graphics_pointer(pointer,sizeof(p)),sizeof(p));
    int32_t hr=pilot_gfx_parameters(p);if(hr<0)return (uint32_t)hr;
    if(dispatch_in_dpc || graphics_clock_closing)stop("reset during callback/device close",pointer);
    uint32_t data[3]={0},width=p[0],height=p[1],pitch=(width*4+63)&~63u;
    for(unsigned i=0;i<3;++i) {
        uint32_t bytes=pitch*height;
        if(vm_allocate(&data[i],0,&bytes,MEM_RESERVE,PAGE_NOACCESS)) {hr=(int32_t)0x8007000eu;goto failed;}
    }
    graphics_present_drain();
    graphics_fence_wait(graphics_fence_insert());
    hr=pilot_gfx_reset(p);if(hr<0)goto failed;
    for(unsigned i=0;i<3;++i) {
        uint32_t bytes=0;
        if(vm_free(&graphics_data[i],&bytes,MEM_RELEASE))stop("reset surface VA release failed",i);
        graphics_data[i]=data[i];
        graphics_surface_refs[i]=0;
        uint32_t *header=(uint32_t*)(memory+graphics_headers+i*32);
        uint32_t format=i==2?0x2e:(p[2]==6 || p[2]==0x12)?0x12:0x1e;
        header[0]=0x010d0000;header[1]=data[i];header[2]=0;
        header[3]=0x10021|(format<<8);header[4]=(width-1)|((height-1)<<12)|((pitch/64-1)<<24);header[5]=0;
    }
    memcpy(graphics_parameters,p,sizeof(p));graphics_swap_deadline=0;
    MEM32(PILOT_D3D_PRESENT_INTERVAL)=p[12];
    graphics_set_targets(graphics_headers,graphics_headers+64);
    if(pilot_gfx_clear(6,0,1,0)<0)stop("reset depth/stencil clear failed",pointer);
    graphics_persistence_clear();
    fprintf(stderr,"[DISPLAY RESET] %ux%u interval=%08x retained-device/resources swap=%u\n",width,height,p[12],graphics_swap_submitted);
    return 0;
failed:
    for(unsigned i=0;i<3;++i)if(data[i]) {
        uint32_t bytes=0;if(vm_free(&data[i],&bytes,MEM_RELEASE))stop("failed reset reservation cleanup",i);
    }
    return (uint32_t)hr;
}
