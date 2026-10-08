/* Separate qualification build: actual Create/Wait/Swap/Reset/Release calls,
 * real rendered pixels, original IRQ/DPC completion and allocator teardown.
 * This authored API consumer exits after its tests; it is never an attract run. */
static uint32_t display_qualification_call(unsigned entry,const uint32_t *args)
{
    unsigned bytes=pilot_d3d_calls[entry].stack_bytes;uint32_t saved=g_esp;
    g_esp-=bytes+4;MEM32(g_esp)=0;
    if(bytes)memcpy(memory+g_esp+4,args,bytes);
    pilot_d3d_dispatch(entry);
    if(g_esp!=saved)stop("display qualification return ABI",entry);
    return g_eax;
}
static void display_qualification(void)
{
    static int running;
    if(running)return;running=1;
    uint32_t parameters[17];memcpy(parameters,graphics_parameters,sizeof(parameters));
    uint32_t storage=g_esp-512,params=storage+128;
    g_esp=storage;
    for(unsigned cycle=0;cycle<3;++cycle) {
        if(cycle) {
            memcpy(memory+params,parameters,sizeof(parameters));
            uint32_t args[]={0,1,0,0x40,params,0};
            if(display_qualification_call(PILOT_D3D_Direct3D_CreateDevice,args))stop("display qualification recreate",cycle);
        }
        uint32_t zero=0;
        display_qualification_call(PILOT_D3D_D3DDevice_SetVerticalBlankCallback,&zero);
        for(unsigned immediate=0;immediate<2;++immediate) {
            if(immediate) {
                memcpy(memory+params,parameters,sizeof(parameters));MEM32(params+48)=0x80000000u;
                if(display_qualification_call(PILOT_D3D_D3DDevice_Reset,&params))stop("display qualification reset",cycle);
            }
            uint32_t color=immediate?0xff774422u:0xff123456u;
            uint32_t clear[]={0,0,0xf0,color,0x3f800000,0};
            if(display_qualification_call(PILOT_D3D_D3DDevice_Clear,clear))stop("display qualification clear",cycle);
            uint32_t swap=display_qualification_call(PILOT_D3D_D3DDevice_Swap,&zero);
            uint64_t deadline=dispatch_monotonic()+30000000;
            do {
                if(display_qualification_call(PILOT_D3D_D3DDevice_BlockUntilVerticalBlank,NULL))stop("display qualification wait",cycle);
                if(dispatch_monotonic()>deadline)stop("display qualification actual flip timeout",swap);
            } while(display_lle_completed()!=swap);
            unsigned bytes=display_lle.width*display_lle.height*4;
            uint8_t *rgba=malloc(bytes);uint32_t width=0,height=0;
            if(!rgba || !display_lle.scanout(display_lle.device,rgba,bytes,&width,&height)) {
                display_lle_check();stop("display qualification actual scanout failed",swap);
            }
            if(width!=parameters[0] || height!=parameters[1])stop("display qualification geometry",swap);
            for(unsigned i=0;i<bytes;i+=4)
                if(rgba[i]!=(uint8_t)(color>>16) || rgba[i+1]!=(uint8_t)(color>>8) || rgba[i+2]!=(uint8_t)color)
                    stop("display qualification actual pixel mismatch",i);
            free(rgba);
        }
        if(display_qualification_call(PILOT_D3D_D3DDevice_Release,NULL) || display_lle.device || display_lle.active)
            stop("display qualification release",cycle);
        for(unsigned i=0;i<4;++i)if(display_lle.allocations[i])stop("display qualification retained DMA allocation",i);
        fprintf(stderr,"[DISPLAY QUALIFICATION] cycle=%u original waits/paced+immediate flips/pixels/reset/teardown passed\n",cycle+1);
    }
    fprintf(stderr,"PASS: selected display LLE actual API/lifetime qualification\n");
    ExitProcess(0);
}
