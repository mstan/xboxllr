#ifdef PILOT_D3D_PHOBOS
static int test_gamma_presentation(void)
{
    uint8_t ramp[768];
    for(unsigned i=0;i<256;++i){ramp[i]=(uint8_t)((i*i+127)/255);ramp[256+i]=(uint8_t)(255-i);ramp[512+i]=(uint8_t)(i/2);}
    for(unsigned mode=0;mode<3;++mode) {
        struct PilotVisualMods options;pilot_visual_mods_default(&options);
        options.antialiasing=mode==1;
        if(mode==2){options.frame_mode=PILOT_FRAMES_INTERPOLATED;options.frame_rate=60;}
        CHECK(pilot_gfx_visual_mods(&options));pilot_gfx_frame_scope(1);
        uint32_t pp[17]={64,48,D3DFMT_X8R8G8B8,1,0x11,1,0,0,1,D3DFMT_D24S8,0,60,1};
        CHECK(!pilot_gfx_create(pp));CHECK(!pilot_gfx_gamma(ramp));
        RhiImageDesc desc={0};desc.type=RHI_IMAGE_2D;desc.width=64;desc.height=48;desc.depth=desc.mip_levels=desc.samples=1;
        desc.format=RHI_FORMAT_R8G8B8A8_UNORM;desc.usage=RHI_USAGE_DEFAULT;desc.bind=RHI_BIND_RENDER_TARGET|RHI_BIND_SAMPLED;
        RhiImage *image=rhi_image_create(&desc,NULL);CHECK(image);
        RhiView *view=rhi_view_create(image,RHI_VIEW_RENDER_TARGET,NULL);CHECK(view);
        const D3D8DisplayFit fit={0,0,64,48};
        uint8_t before[64*48*4],after[64*48*4],output[64*48*4];
        CHECK(!pilot_gfx_clear(3,0xff204080,1,0));
        unsigned red=32,green=64,blue=128;
        if(mode==2) {
            CHECK(!pilot_gfx_present());CHECK(!pilot_gfx_clear(3,0xffa0c040,1,0));CHECK(!pilot_gfx_present());
            struct PilotInterpolationStats info;pilot_gfx_interpolation_stats(&info);CHECK(info.history==2);
            CHECK(!pilot_d3d_interpolate_image(info.current+(info.current-info.previous)/2,view));
            red=96;green=128;blue=96;
        } else CHECK(!pilot_d3d_visual_resolve(d3d8_GetSceneView(),64,48,view,fit,64,48));
        CHECK(!rhi_image_readback(image,0,output,64*4));
        unsigned center=(24*64+32)*4;
        CHECK(abs((int)output[center]-ramp[red])<=1);
        CHECK(abs((int)output[center+1]-ramp[256+green])<=1);
        CHECK(abs((int)output[center+2]-ramp[512+blue])<=1 && output[center+3]==255);
        CHECK(!pilot_gfx_readback(before,sizeof(before)));
        CHECK(!pilot_gfx_present());CHECK(!pilot_gfx_readback(after,sizeof(after)) && !memcmp(before,after,sizeof(before)));
        /* Reset reallocates presentation resources while the logical display
         * ramp survives. Closing the device starts the next one at identity. */
        CHECK(!pilot_gfx_reset(pp));CHECK(!pilot_gfx_clear(3,0xff204080,1,0));
        CHECK(!pilot_d3d_visual_resolve(d3d8_GetSceneView(),64,48,view,fit,64,48));
        CHECK(!rhi_image_readback(image,0,output,64*4));
        CHECK(output[center]==ramp[32] && output[center+1]==ramp[256+64] && output[center+2]==ramp[512+128]);
        rhi_view_destroy(view);rhi_image_destroy(image);pilot_gfx_close();
        pilot_visual_mods_default(&options);CHECK(pilot_gfx_visual_mods(&options));
        CHECK(!pilot_gfx_create(pp) && !pilot_gfx_clear(3,0xff204080,1,0));
        image=rhi_image_create(&desc,NULL);CHECK(image);
        view=rhi_view_create(image,RHI_VIEW_RENDER_TARGET,NULL);CHECK(view);
        CHECK(!pilot_d3d_visual_resolve(d3d8_GetSceneView(),64,48,view,fit,64,48));
        CHECK(!rhi_image_readback(image,0,output,64*4));
        CHECK(output[center]==32 && output[center+1]==64 && output[center+2]==128);
        rhi_view_destroy(view);rhi_image_destroy(image);pilot_gfx_close();
    }
    fprintf(stderr,"PASS actual gamma channel pixels, mapping after interpolation/AA, untouched guest targets, reset persistence and fresh identity\n");return 0;
}
#endif
