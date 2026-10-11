static void test_graphics_present_status(void)
{
    unsigned char pixels[64*64*4];
    uint32_t clear[]={0,0,0xf0,0xffff0000,0x3f800000,0},arg=0;
    /* Device-error invocations retain their selected result until the guard. */
    int error_case=test_desktop_override && test_desktop_result<0;
    for(unsigned immediate=0;immediate<2;++immediate) {
        MEM32(PILOT_D3D_PRESENT_INTERVAL)=immediate?0x80000000u:1;
        CHECK(!call_graphics(PILOT_D3D_D3DDevice_Clear,clear));
        struct PilotGpuProgress before,after;
        pilot_gfx_completion_stats(&before);
        uint32_t previous=graphics_swap_submitted;
        test_desktop_override=1;
        if(!error_case)test_desktop_result=(int32_t)0x087a0001u;
        uint32_t submitted=call_graphics(PILOT_D3D_D3DDevice_Swap,&arg);
        CHECK(!error_case && submitted==previous+1);
        test_desktop_override=0;
        pilot_gfx_completion_stats(&after);
        CHECK(after.submitted>before.submitted && graphics_present_count<=2);
        DWORD start=GetTickCount();
        uint32_t completed;
        do {
            AcquireSRWLockExclusive(&graphics_lock);
            graphics_present_retire(graphics_frame_at(dispatch_monotonic()));
            completed=graphics_swap_completed;
            ReleaseSRWLockExclusive(&graphics_lock);
            if(completed==submitted)break;
            Sleep(1);
        } while(GetTickCount()-start<2000);
        CHECK(completed==submitted && !pilot_gfx_checkpoint_busy(after.submitted));
        CHECK(!pilot_gfx_surface_readback(1,pixels,sizeof(pixels)));
        CHECK(pixels[0]==255 && !pixels[1] && !pixels[2]);
        clear[3]=0xff0000ff;
        CHECK(!call_graphics(PILOT_D3D_D3DDevice_Clear,clear));
        CHECK(!pilot_gfx_surface_readback(0,pixels,sizeof(pixels)));
        CHECK(!pixels[0] && !pixels[1] && pixels[2]==255);
        CHECK(!pilot_gfx_surface_readback(1,pixels,sizeof(pixels)));
        CHECK(pixels[0]==255 && !pixels[1] && !pixels[2]);
        CHECK(call_graphics(PILOT_D3D_D3DDevice_Swap,&arg)==submitted+1);
        CHECK(!pilot_gfx_surface_readback(1,pixels,sizeof(pixels)));
        CHECK(!pixels[0] && !pixels[1] && pixels[2]==255);
        clear[3]=0xffff0000;
    }
    fprintf(stderr,"PASS: occluded desktop return preserves paced/immediate guest swaps, real GPU retirement, distinct front/back pixels and next-frame progress\n");
}
